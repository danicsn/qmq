#include "msg_p.hpp"
#include <QIODevice>
#include <QDataStream>
#include <QDebug>

class GossipFramePrivate : public FramePrivate {
public:
    GossipFramePrivate() {
        value = 0;
    }
    ~GossipFramePrivate() {
        if(value)
            free(value);
    }

    int id;                             //  gossipframe message ID
    byte *needle;                       //  Read/write pointer for serialization
    byte *ceiling;                      //  Valid upper limit for read pointer
    char key [256];                     //  Tuple key, globally unique
    char *value;                        //  Tuple value, as printable string
    uint32_t ttl;                       //  Time to live, msecs
};

//  Put a block of octets to the frame
#define PUT_OCTETS(host,size) { \
    memcpy (d->needle, (host), size); \
    d->needle += size; \
}

//  Get a block of octets from the frame
#define GET_OCTETS(host,size) { \
    if (d->needle + size > d->ceiling) { \
        qWarning("gossipframe: GET_OCTETS failed"); \
        goto malformed; \
    } \
    memcpy ((host), d->needle, size); \
    d->needle += size; \
}

//  Put a 1-byte number to the frame
#define PUT_NUMBER1(host) { \
    *(byte *) d->needle = (host); \
    d->needle++; \
}

//  Put a 2-byte number to the frame
#define PUT_NUMBER2(host) { \
    d->needle [0] = (byte) (((host) >> 8)  & 255); \
    d->needle [1] = (byte) (((host))       & 255); \
    d->needle += 2; \
}

//  Put a 4-byte number to the frame
#define PUT_NUMBER4(host) { \
    d->needle [0] = (byte) (((host) >> 24) & 255); \
    d->needle [1] = (byte) (((host) >> 16) & 255); \
    d->needle [2] = (byte) (((host) >> 8)  & 255); \
    d->needle [3] = (byte) (((host))       & 255); \
    d->needle += 4; \
}

//  Put a 8-byte number to the frame
#define PUT_NUMBER8(host) { \
    d->needle [0] = (byte) (((host) >> 56) & 255); \
    d->needle [1] = (byte) (((host) >> 48) & 255); \
    d->needle [2] = (byte) (((host) >> 40) & 255); \
    d->needle [3] = (byte) (((host) >> 32) & 255); \
    d->needle [4] = (byte) (((host) >> 24) & 255); \
    d->needle [5] = (byte) (((host) >> 16) & 255); \
    d->needle [6] = (byte) (((host) >> 8)  & 255); \
    d->needle [7] = (byte) (((host))       & 255); \
    d->needle += 8; \
}

//  Get a 1-byte number from the frame
#define GET_NUMBER1(host) { \
    if (d->needle + 1 > d->ceiling) { \
        qWarning("gossipframe: GET_NUMBER1 failed"); \
        goto malformed; \
    } \
    (host) = *(byte *) d->needle; \
    d->needle++; \
}

//  Get a 2-byte number from the frame
#define GET_NUMBER2(host) { \
    if (d->needle + 2 > d->ceiling) { \
        qWarning("gossipframe: GET_NUMBER2 failed"); \
        goto malformed; \
    } \
    (host) = ((uint16_t) (d->needle [0]) << 8) \
           +  (uint16_t) (d->needle [1]); \
    d->needle += 2; \
}

//  Get a 4-byte number from the frame
#define GET_NUMBER4(host) { \
    if (d->needle + 4 > d->ceiling) { \
        qWarning("gossipframe: GET_NUMBER4 failed"); \
        goto malformed; \
    } \
    (host) = ((uint32_t) (d->needle [0]) << 24) \
           + ((uint32_t) (d->needle [1]) << 16) \
           + ((uint32_t) (d->needle [2]) << 8) \
           +  (uint32_t) (d->needle [3]); \
    d->needle += 4; \
}

//  Get a 8-byte number from the frame
#define GET_NUMBER8(host) { \
    if (d->needle + 8 > d->ceiling) { \
        qWarning("gossipframe: GET_NUMBER8 failed"); \
        goto malformed; \
    } \
    (host) = ((uint64_t) (d->needle [0]) << 56) \
           + ((uint64_t) (d->needle [1]) << 48) \
           + ((uint64_t) (d->needle [2]) << 40) \
           + ((uint64_t) (d->needle [3]) << 32) \
           + ((uint64_t) (d->needle [4]) << 24) \
           + ((uint64_t) (d->needle [5]) << 16) \
           + ((uint64_t) (d->needle [6]) << 8) \
           +  (uint64_t) (d->needle [7]); \
    d->needle += 8; \
}

//  Put a string to the frame
#define PUT_STRING(host) { \
    size_t string_size = strlen (host); \
    PUT_NUMBER1 (string_size); \
    memcpy (d->needle, (host), string_size); \
    d->needle += string_size; \
}

//  Get a string from the frame
#define GET_STRING(host) { \
    size_t string_size; \
    GET_NUMBER1 (string_size); \
    if (d->needle + string_size > (d->ceiling)) { \
        qWarning("gossipframe: GET_STRING failed"); \
        goto malformed; \
    } \
    memcpy ((host), d->needle, string_size); \
    (host) [string_size] = 0; \
    d->needle += string_size; \
}

//  Put a long string to the frame
#define PUT_LONGSTR(host) { \
    size_t string_size = strlen (host); \
    PUT_NUMBER4 (string_size); \
    memcpy (d->needle, (host), string_size); \
    d->needle += string_size; \
}

//  Get a long string from the frame
#define GET_LONGSTR(host) { \
    size_t string_size; \
    GET_NUMBER4 (string_size); \
    if (d->needle + string_size > (d->ceiling)) { \
        qWarning("gossipframe: GET_LONGSTR failed"); \
        goto malformed; \
    } \
    free ((host)); \
    (host) = (char *) malloc (string_size + 1); \
    memcpy ((host), d->needle, string_size); \
    (host) [string_size] = 0; \
    d->needle += string_size; \
}

// message
GossipFrame::GossipFrame() : Frame(*(new GossipFramePrivate)){}

GossipFrame::GossipFrame(const QByteArray &frout) : Frame(*(new GossipFramePrivate))
{
    reset(frout);
}

bool GossipFrame::recv(SocketBase &input)
{
    Q_D(GossipFrame);
    if (input.type() == ZMQ_ROUTER) {
        bool rcv = Frame::recv(input);
        if (!rcv || !d->more) {
            qWarning("gossipframe: no routing ID");
            return false;          //  Interrupted or malformed
        }
    }
    zmq_msg_t frame;
    zmq_msg_init (&frame);
    int size = zmq_msg_recv (&frame, input.resolve(), 0);
    if (size == -1) {
        qWarning("gossipframe: interrupted");
        goto malformed;         //  Interrupted
    }
    //  Get and check protocol signature
    d->needle = (byte *) zmq_msg_data (&frame);
    d->ceiling = d->needle + zmq_msg_size (&frame);

    uint16_t signature;
    GET_NUMBER2 (signature);
    if (signature != (0xAAA0 | 0)) {
        qWarning("gossipframe: invalid signature");
        //  TODO: discard invalid messages and loop, and return
        //  -1 only on interrupt
        goto malformed;         //  Interrupted
    }
    //  Get message id and parse per message type
    GET_NUMBER1 (d->id);

    switch (d->id) {
        case GOSSIP_MSG_HELLO:
            {
                byte version;
                GET_NUMBER1 (version);
                if (version != 1) {
                    qWarning("gossipframe: version is invalid");
                    goto malformed;
                }
            }
            break;

        case GOSSIP_MSG_PUBLISH:
            {
                byte version;
                GET_NUMBER1 (version);
                if (version != 1) {
                    qWarning("gossipframe: version is invalid");
                    goto malformed;
                }
            }
            GET_STRING (d->key);
            GET_LONGSTR (d->value);
            GET_NUMBER4 (d->ttl);
            break;

        case GOSSIP_MSG_PING:
            {
                byte version;
                GET_NUMBER1 (version);
                if (version != 1) {
                    qWarning("gossipframe: version is invalid");
                    goto malformed;
                }
            }
            break;

        case GOSSIP_MSG_PONG:
            {
                byte version;
                GET_NUMBER1 (version);
                if (version != 1) {
                    qWarning("gossipframe: version is invalid");
                    goto malformed;
                }
            }
            break;

        case GOSSIP_MSG_INVALID:
            {
                byte version;
                GET_NUMBER1 (version);
                if (version != 1) {
                    qWarning("gossipframe: version is invalid");
                    goto malformed;
                }
            }
            break;

        default:
            qWarning("gossipframe: bad message ID");
            goto malformed;
    }
    //  Successful return
    zmq_msg_close (&frame);
    return true;

    //  Error returns
    malformed:
        qWarning("gossipframe: gossipframe malformed message, fail");
        zmq_msg_close (&frame);
        return false;              //  Invalid message
}

int GossipFrame::send(SocketBase &output, int flags)
{
    Q_D(GossipFrame);
    if (output.type() == ZMQ_ROUTER)
        Frame::send(output, QFRAME_MORE + QFRAME_REUSE + flags);

    size_t frame_size = 2 + 1;          //  Signature and message ID
    switch (d->id) {
        case GOSSIP_MSG_HELLO:
            frame_size += 1;            //  version
            break;
        case GOSSIP_MSG_PUBLISH:
            frame_size += 1;            //  version
            frame_size += 1 + strlen (d->key);
            frame_size += 4;
            if (d->value)
                frame_size += strlen (d->value);
            frame_size += 4;            //  ttl
            break;
        case GOSSIP_MSG_PING:
            frame_size += 1;            //  version
            break;
        case GOSSIP_MSG_PONG:
            frame_size += 1;            //  version
            break;
        case GOSSIP_MSG_INVALID:
            frame_size += 1;            //  version
            break;
    }
    //  Now serialize message into the frame
    zmq_msg_t frame;
    zmq_msg_init_size (&frame, frame_size);
    d->needle = (byte *) zmq_msg_data (&frame);
    PUT_NUMBER2 (0xAAA0 | 0);
    PUT_NUMBER1 (d->id);
    size_t nbr_frames = 1;              //  Total number of frames to send

    switch (d->id) {
        case GOSSIP_MSG_HELLO:
            PUT_NUMBER1 (1);
            break;

        case GOSSIP_MSG_PUBLISH:
            PUT_NUMBER1 (1);
            PUT_STRING (d->key);
            if (d->value) {
                PUT_LONGSTR (d->value);
            }
            else
                PUT_NUMBER4 (0);    //  Empty string
            PUT_NUMBER4 (d->ttl);
            break;

        case GOSSIP_MSG_PING:
            PUT_NUMBER1 (1);
            break;

        case GOSSIP_MSG_PONG:
            PUT_NUMBER1 (1);
            break;

        case GOSSIP_MSG_INVALID:
            PUT_NUMBER1 (1);
            break;

    }
    //  Now send the data frame
    zmq_msg_send (&frame, output.resolve(), --nbr_frames? ZMQ_SNDMORE: 0);

    return 0;
}

int GossipFrame::id() const
{
    Q_D(const GossipFrame);
    return d->id;
}

void GossipFrame::setId(int id)
{
    Q_D(GossipFrame);
    d->id = id;
}

QString GossipFrame::key() const
{
    Q_D(const GossipFrame);
    return QString(reinterpret_cast<const char*>(d->key));
}

void GossipFrame::setKey(const QString &k)
{
    Q_D(GossipFrame);
    if (k == d->key)
            return;
    strncpy (d->key, k.toLatin1().data(), 255);
    d->key [255] = 0;
}

QByteArray GossipFrame::routingId() const
{
    return bdata();
}

void GossipFrame::setRoutingId(const QByteArray &frout)
{
    reset(frout);
}

QString GossipFrame::value() const
{
    Q_D(const GossipFrame);
    return QString::fromLocal8Bit(d->value);
}

void GossipFrame::setValue(const QString &v)
{
    Q_D(GossipFrame);
    free(d->value);
    d->value = strdup(v.toLocal8Bit().data());
}

int GossipFrame::timeTolive() const
{
    Q_D(const GossipFrame);
    return d->ttl;
}

void GossipFrame::setTimeToLive(int ttl)
{
    Q_D(GossipFrame);
    d->ttl = ttl;
}

QString GossipFrame::command() const
{
    Q_D(const GossipFrame);
    switch (d->id) {
        case GOSSIP_MSG_HELLO:
            return QString("HELLO");
            break;
        case GOSSIP_MSG_PUBLISH:
            return QString("PUBLISH");
            break;
        case GOSSIP_MSG_PING:
            return QString("PING");
            break;
        case GOSSIP_MSG_PONG:
            return QString("PONG");
            break;
        case GOSSIP_MSG_INVALID:
            return QString("INVALID");
            break;
    }
    return QString("?");
}

void GossipFrame::print()
{
    Q_D(GossipFrame);
    switch (d->id) {
        case GOSSIP_MSG_HELLO:
            qDebug ("GOSSIP_MSG_HELLO:");
            qDebug ("    version=1");
            break;

        case GOSSIP_MSG_PUBLISH:
            qDebug ("GOSSIP_MSG_PUBLISH:");
            qDebug ("    version=1");
            if (d->key)
                qDebug ("    key='%s'", d->key);
            else
                qDebug ("    key=");
            if (d->value)
                qDebug ("    value='%s'", d->value);
            else
                qDebug ("    value=");
            qDebug ("    ttl=%ld", (long) d->ttl);
            break;

        case GOSSIP_MSG_PING:
            qDebug ("GOSSIP_MSG_PING:");
            qDebug ("    version=1");
            break;

        case GOSSIP_MSG_PONG:
            qDebug ("GOSSIP_MSG_PONG:");
            qDebug ("    version=1");
            break;

        case GOSSIP_MSG_INVALID:
            qDebug ("GOSSIP_MSG_INVALID:");
            qDebug ("    version=1");
            break;

    }
}

void GossipFrame::test()
{
    printf (" * gossipframe: ");

    //  @selftest
    //  Simple create/destroy test
    GossipFrame *self = new GossipFrame;
    assert (self);
    delete self;

    //  Create pair of sockets we can send through
    Socket *input = Socket::createRouter();
    assert (input);
    input->connect("inproc://selftest-gossipframe");

    Socket *output = Socket::createDealer();
    assert (output);
    output->bind("inproc://selftest-gossipframe");

    //  Encode/send/decode and verify each message type
    int instance;
    self = new GossipFrame();
    self->setId(GOSSIP_MSG_HELLO);

    //  Send twice
    self->send(*output);
    self->send(*output);

    for (instance = 0; instance < 2; instance++) {
        bool rec = self->recv(*input);
        assert (rec);
    }
    self->setId(GOSSIP_MSG_PUBLISH);

    self->setKey("Life is short but Now lasts for ever");
    self->setValue("Life is short but Now lasts for ever");
    self->setTimeToLive(123);
    //  Send twice
    self->send(*output);
    self->send(*output);

    for (instance = 0; instance < 2; instance++) {
        bool rec = self->recv(*input);
        assert (rec);
        assert (self->key() == "Life is short but Now lasts for ever");
        assert (self->value() == "Life is short but Now lasts for ever");
        assert (self->timeTolive() == 123);
    }
    self->setId(GOSSIP_MSG_PING);

    //  Send twice
    self->send(*output);
    self->send(*output);

    for (instance = 0; instance < 2; instance++) {
        bool rec = self->recv(*input);
        assert (rec);
    }
    self->setId(GOSSIP_MSG_PONG);

    //  Send twice
    self->send(*output);
    self->send(*output);

    for (instance = 0; instance < 2; instance++) {
        bool rec = self->recv(*input);
        assert (rec);
    }
    self->setId(GOSSIP_MSG_INVALID);

    //  Send twice
    self->send(*output);
    self->send(*output);

    for (instance = 0; instance < 2; instance++) {
        bool rec = self->recv(*input);
        assert (rec);
    }

    delete self;
    delete input;
    delete output;
    //  @end

    printf ("OK\n");
}


/*
 *
 *
 * Actor Implementation ***/

class Server_t {
public:
    Server_t();
    //  These properties must always be present in the server_t
    //  and are set by the generated engine; do not modify them!
    Socket *pipe;              //  Actor pipe back to caller
    //zconfig_t *config;          //  Current loaded configuration

    //  Add any properties you need here
    QList<Socket*> remotes;          //  Parents, as Socket instances
    QHash<QString, QString> tuples;  //  Tuples, indexed by key

    QString cur_tuple;         //  Holds current tuple to publish
    GossipFrame message;       //  Message to broadcast
};

class Client_t {
public:
    Server_t* parent;
    GossipFrame frame;
};

class GossipHandler {
public:
    GossipHandler() {

    }

    QList<zmq_pollitem_t> readers;

};


void qgossip(Socket *pipe, void *)
{

}


void gossipTest(bool verbose)
{

}
