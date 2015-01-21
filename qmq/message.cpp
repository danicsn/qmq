#include "msg_p.hpp"
#include <QIODevice>
#include <QDataStream>
#include <QDebug>

Frame::Frame(): d_ptr(new FramePrivate)
{}

Frame::Frame(const Frame &data) : d_ptr(new FramePrivate(data.constData(), data.size()))
{}

Frame::Frame(const QByteArray &data): d_ptr(new FramePrivate(data.constData(), data.size()))
{}

Frame::Frame(const void *data, int length): d_ptr(new FramePrivate(data, length))
{}

Frame::Frame(FramePrivate &d): d_ptr(&d)
{}

Frame::~Frame()
{
    delete d_ptr;
}

void Frame::reset(const QByteArray &data)
{
    Q_D(Frame);

    zmq_msg_close(&(d->msg));
    zmq_msg_init_size(&(d->msg), data.length());
    memcpy(zmq_msg_data(&(d->msg)), data.constData(), data.length());
}

void Frame::reset(const void *data, int length)
{
    Q_D(Frame);

    zmq_msg_close(&(d->msg));
    if(length <= 0) return;
    zmq_msg_init_size(&(d->msg), length);
    memcpy(zmq_msg_data(&(d->msg)), data, length);
}

QByteArray Frame::bdata() const
{
    int length = size();
    QByteArray array((const char*)constData(), length);
    return array;
}

void *Frame::data()
{
    Q_D(Frame);
    return zmq_msg_data(&(d->msg));
}

const void *Frame::constData() const
{
    Q_D(const Frame);
    return d->data();
}

QString Frame::hexString()
{
    static const char
        hex_char[] = "0123456789ABCDEF";

    int ss = size();
    byte *dd = (byte*)data();
    QByteArray hex_str(ss * 2 + 1, '\0');

    int byte_nbr;
    for (byte_nbr = 0; byte_nbr < ss; byte_nbr++) {
        hex_str[byte_nbr * 2 + 0] = hex_char[dd [byte_nbr] >> 4];
        hex_str[byte_nbr * 2 + 1] = hex_char[dd [byte_nbr] & 15];
    }

    return QString(hex_str);
}

QString Frame::toString() const
{
    return QString::fromLocal8Bit(toByteArray());
}

QByteArray Frame::toByteArray() const
{
    int s = size();
    char *arr = (char *) malloc(s);
    if (arr) {
        memcpy (arr, constData(), s);
    }

    QByteArray res(arr, s);
    free(arr);

    return res;
}

int Frame::size() const
{
    Q_D(const Frame);
    return d->size();
}

bool Frame::isEmpty()
{
    return size() == 0;
}

bool Frame::hasMore() const
{
    Q_D(const Frame);
    return d->more > 0;
}

void Frame::setMore(bool more)
{
    Q_D(Frame);
    d->more = more ? 1 : 0;
}

int Frame::send(SocketBase &socket, int flags)
{
    Q_D(Frame);
    void* handle = socket.resolve();
    int send_flags = (flags & QFRAME_MORE) ? ZMQ_SNDMORE : 0;
    send_flags |= (flags & QFRAME_DONTWAIT) ? ZMQ_DONTWAIT : 0;
    if (flags & QFRAME_REUSE) {
        zmq_msg_t copy;
        zmq_msg_init (&copy);
        if (zmq_msg_copy (&copy, &(d->msg)))
            return -1;
        if (zmq_sendmsg (handle, &copy, send_flags) == -1) {
            zmq_msg_close (&copy);
            return -1;
        }
    }
    else {
        int rc = zmq_sendmsg(handle, &(d->msg), send_flags);
        zmq_msg_close(&(d->msg));
        if (rc == -1)
            return rc;
    }

    return 0;
}

bool Frame::recv(SocketBase &socket)
{
    Q_D(Frame);
    void* handle = socket.resolve();
    if (zmq_recvmsg(handle, &(d->msg), 0) < 0) {
        zmq_msg_close(&(d->msg));
        return false;
    }
    d->more = socket.receiveMore();
    return true;
}

int Frame::recvnowait(SocketBase &socket)
{
    Q_D(Frame);
    void* handle = socket.resolve();
    if (zmq_recvmsg(handle, &(d->msg), ZMQ_DONTWAIT) < 0) {
        zmq_msg_close(&(d->msg));
    }
    d->more = socket.receiveMore();
    return d->more;
}

Frame &Frame::operator=(const Frame &other)
{
    this->reset(other.constData(), other.size());
    return *this;
}

Frame &Frame::operator=(const QString &other)
{
    this->reset(other.toLocal8Bit());
    return *this;
}

bool operator ==(const Frame &f, const Frame &f1)
{
    if(f.size() == f1.size() &&
       memcmp(f.constData(), f1.constData(), f.size()) == 0)
        return true;

    return false;
}

void Frame::test()
{
    printf (" * frame: ");
    int rc;
    Frame frame;

    //  @selftest
    //  Create two PAIR sockets and connect over inproc
    Socket *output = Socket::createPair("@inproc://zframe.test");
    assert (output);
    Socket *input = Socket::createPair(">inproc://zframe.test");
    assert (input);

    //  Send five different frames, test ZFRAME_MORE
    int frame_nbr;
    for (frame_nbr = 0; frame_nbr < 5; frame_nbr++) {
        frame.reset("Hello");
        rc = frame.send(*output, QFRAME_MORE);
        assert (rc == 0);
    }

    //  Send same frame five times, test ZFRAME_REUSE
    frame.reset("Hello");
    for (frame_nbr = 0; frame_nbr < 5; frame_nbr++) {
        rc = frame.send(*output, QFRAME_MORE + QFRAME_REUSE);
        assert (rc == 0);
    }

    //  Send END frame
    frame = Frame("NOT");

    frame.reset("END", 3);
    QString string = frame.hexString();
    if(string != "454E44")
        assert (false);

    rc = frame.send(*output, 0);
    assert (rc == 0);

    //  Read and count until we receive END
    Frame rec;
    frame_nbr = 0;
    for (frame_nbr = 0;; frame_nbr++) {
        rec.recv(*input);
        if (rec.toString() == "END") {
            break;
        }
        assert (rec.hasMore());
        rec.setMore(false);
        assert (rec.hasMore() == false);
    }
    assert (frame_nbr == 10);

    delete input;
    delete output;

    //  @end
    printf ("OK\n");
}

/*
 * message contains many frame
 *
 * **************/

Messages::Messages(): d_ptr(new MessagesPrivate)
{
}

Messages::Messages(const Messages &msg) : d_ptr(new MessagesPrivate)
{
    clear();
    foreach (Frame* frame, msg.d_ptr->frames) {
        appendmem(frame->data(), frame->size());
    }
}

Messages::~Messages()
{
    delete d_ptr;
}

int Messages::send(SocketBase &socket)
{
    Q_D(Messages);
    int rc = 0;

    if(d->frames.count() < 1) return rc;

    Frame *frame = d->frames.dequeue();
    while (frame) {
        int s = frame->size();
        rc = frame->send(socket, d->frames.size() ? QFRAME_MORE : 0);
        if (rc != 0)
            break;

        d->contentsize -= s;
        delete frame;

        if(d->frames.count() < 1) break;
        frame = d->frames.dequeue();
    }

    return rc;
}

bool Messages::recv(SocketBase &socket)
{
    clear();

    while (true) {
        Frame frame;
        if (!frame.recv(socket)) {
            break;              //  Interrupted or terminated
        }
        append(frame);
        if (!frame.hasMore())
            break;              //  Last message frame
    }

    return size() > 0;
}

void Messages::recvnowait(SocketBase &socket)
{
    clear();

    while (true) {
        Frame frame;
        if (!frame.recvnowait(socket)) {
            break;              //  Interrupted or terminated
        }
        append(frame);
        if (frame.hasMore())
            break;              //  Last message frame
    }
}

void Messages::decode(const QByteArray &buffer)
{
    byte *source = (byte*) buffer.data();
    byte *limit = (byte*)buffer.data() + buffer.size();
    while (source < limit) {
        size_t frame_size = *source++;
        if (frame_size == 255) {
            if (source > limit - 4) {
                clear();
                break;
            }
            frame_size = (source [0] << 24)
                       + (source [1] << 16)
                       + (source [2] << 8)
                       +  source [3];
            source += 4;
        }
        if (source > limit - frame_size) {
            clear();
            break;
        }
        Frame *frame = new (std::nothrow) Frame(source, frame_size);
        if (frame) {
            append(frame);
            source += frame_size;
        }
        else {
            clear();
            break;
        }
    }
}

void Messages::decode(const uchar *data, int length)
{
    byte *source = (byte*)data;
    byte *limit = (byte*)data + length;
    while (source < limit) {
        int frame_size = *source++;
        if (frame_size == 255) {
            if (source > limit - 4) {
                clear();
                break;
            }
            frame_size = (source [0] << 24)
                       + (source [1] << 16)
                       + (source [2] << 8)
                       +  source [3];
            source += 4;
        }
        if (source > limit - frame_size) {
            clear();
            break;
        }
        Frame *frame = new (std::nothrow) Frame(source, frame_size);
        if (frame) {
            append(frame);
            source += frame_size;
        }
        else {
            clear();
            break;
        }
    }
}

QByteArray Messages::encode()
{
    Q_D(Messages);
    //  Calculate real size of buffer
    int buffer_size = 0;
    foreach (Frame* frame, d->frames) {
        int frame_size = frame->size();
        if (frame_size < 255)
            buffer_size += frame_size + 1;
        else
            buffer_size += frame_size + 1 + 4;
    }

    if(buffer_size == 0) return QByteArray();

    QByteArray buffer(buffer_size, '\0');

    //  Encode message now
    byte *dest = (byte*) buffer.data();
    foreach (Frame* frame, d->frames) {
        int frame_size = frame->size();
        if (frame_size < 255) {
            *dest++ = (byte) frame_size;
            memcpy(dest, frame->constData(), frame_size);
            dest += frame_size;
        }
        else {
            *dest++ = 0xFF;
            *dest++ = (frame_size >> 24) & 255;
            *dest++ = (frame_size >> 16) & 255;
            *dest++ = (frame_size >>  8) & 255;
            *dest++ =  frame_size        & 255;
            memcpy(dest, frame->constData(), frame_size);
            dest += frame_size;
        }
    }
    assert ((dest - (byte*)buffer.data()) == buffer_size);

    return buffer;
}

// load from file

int Messages::load(QIODevice *file)
{
    clear();
    QDataStream reader(file);
    while (true) {
        int frame_size = 0;
        reader >> frame_size;
        if (frame_size > 0) {
            Frame *frame = new (std::nothrow) Frame(NULL, frame_size);
            if (!frame) {
                clear();
                return 0;    //  Unable to allocate frame, fail
            }
            char* buf; // = malloc(frame_size);
            uint l;
            reader.readBytes(buf, l);
            if (frame_size > 0 && l != frame_size) {
                delete frame;
                delete [] buf;
                clear();
                return 0;    //  Corrupt file, fail
            }
            frame->reset(buf, frame_size);
            append(frame);
        }
        else
            break;              //  Unable to read properly, quit
    }

    return size();
}

int Messages::save(QIODevice* file)
{
    Q_D(Messages);
    QDataStream writer(file);
    foreach (Frame* frame, d->frames) {
        int frame_size = frame->size();
        writer << frame_size;
        writer.writeBytes((const char*)frame->constData(), frame_size);
    }
    return 0;
}

void Messages::append(Messages &mlist)
{
    QByteArray data = mlist.encode();
    if(!data.isEmpty())
        append(new Frame(data));
}

void Messages::append(const Frame &frame)
{
    Q_D(Messages);
    Frame* nf = new Frame(frame);
    d->frames.append(nf);

    d->contentsize += nf->size();
}

void Messages::prepend(const Frame &frame)
{
    Q_D(Messages);
    Frame* nf = new Frame(frame);
    d->frames.prepend(nf);

    d->contentsize += nf->size();
}

void Messages::append(Frame *frame)
{
    Q_D(Messages);
    d->frames.append(frame);

    d->contentsize += frame->size();
}

void Messages::appendmem(const void *data, int length)
{
    Q_D(Messages);
    Frame* nf = new Frame(data, length);
    d->frames.append(nf);

    d->contentsize += nf->size();
}

void Messages::append(const QString &data)
{
    Q_D(Messages);
    Frame* nf = new Frame(data.toLocal8Bit());
    d->frames.append(nf);

    d->contentsize += nf->size();
}

int Messages::append(const char *format, ...)
{
    va_list argptr;
    va_start (argptr, format);
    char *string = zsys_vprintf (format, argptr);
    va_end (argptr);
    if (!string)
        return -1;

    Frame *frame = new Frame(QString(string).toLocal8Bit());
    if (frame) {
        append(frame);
    }
    else
        return -1;

    return 0;
}

void Messages::wrap(Frame *frame)
{
    assert(frame);
    push("", 0);
    push(frame);
}

void Messages::prepend(Frame *frame)
{
    Q_D(Messages);
    d->frames.prepend(frame);

    d->contentsize += frame->size();
}

Frame *Messages::pop()
{
    Q_D(Messages);
    if(d->frames.count() < 1) return NULL;
    d->contentsize -= d->frames.first()->size();
    return d->frames.dequeue();
}

QString Messages::popstr()
{
    Q_D(Messages);
    if(d->frames.count() < 1) return QString();
    d->contentsize -= d->frames.first()->size();
    Frame* frame = d->frames.dequeue();
    QString res = frame->toString();
    delete frame;
    return res;
}

Frame *Messages::unwrap()
{
    Frame* frame = pop();
    Frame* empty = first();
    if(empty && empty->size() == 0)
    {
        empty = pop();
        delete empty;
    }

    return frame;
}

Messages* Messages::popmsg()
{
    Frame* frame = pop();
    if(!frame) return NULL;

    Messages* mlist = new Messages;
    mlist->decode(frame->bdata());

    if(mlist->size() == 0)
    {
        delete mlist;
        mlist = 0;
    }

    return mlist;
}

Frame *Messages::remove(int i)
{
    Q_D(Messages);
    if(d->frames.count() < i) return NULL;

    Frame* idx = d->frames.at(i);
    d->frames.removeAt(i);
    d->contentsize -= idx->size();
    return idx;
}

int Messages::removeAll(Frame *f)
{
    Q_D(Messages);
    int rc = d->frames.removeAll(f);
    d->contentsize -= rc * f->size();
    return rc;
}

void Messages::clear()
{
    Q_D(Messages);
    if(d->frames.count() > 0)
        qDeleteAll(d->frames);
    d->frames.clear();
    d->contentsize = 0;
}

void Messages::push(Frame *frame)
{
    prepend(frame);
}

void Messages::push(const void *data, int length)
{
    prepend(new Frame(data, length));
}

void Messages::push(const QString &data)
{
    prepend(new Frame(data.toLatin1()));
}

Frame *Messages::first()
{
    Q_D(Messages);
    if(d->frames.isEmpty()) return NULL;
    return d->frames.first();
}

Frame *Messages::last()
{
    Q_D(Messages);
    if(d->frames.isEmpty()) return NULL;
    return d->frames.last();
}

Frame *Messages::next(Frame *before)
{
    Q_D(Messages);
    if(d->frames.isEmpty()) return NULL;
    int idx = d->frames.indexOf(before);
    if(idx == -1 || idx + 1 == d->frames.count()) return NULL;
    return d->frames.at(idx + 1);
}

Frame *Messages::at(int i)
{
    Q_D(Messages);
    return d->frames.at(i);
}

QString Messages::firstStr()
{
    Q_D(Messages);
    return d->frames.first()->toString();
}

QString Messages::lastStr()
{
    Q_D(Messages);
    return d->frames.last()->toString();
}

QStringList Messages::toStringList()
{
    Q_D(Messages);
    QStringList res;
    foreach (Frame* frame, d->frames) {
        res << frame->toString();
    }

    return res;
}

int Messages::size()
{
    Q_D(Messages);
    return d->frames.count();
}

int Messages::contentSize()
{
    Q_D(Messages);
    return d->contentsize;
}

Messages &Messages::operator =(const Messages &msgs)
{
    this->clear();

    foreach (Frame* frame, msgs.d_ptr->frames) {
        appendmem(frame->data(), frame->size());
    }

    return *this;
}

#include <QFile>

void Messages::test()
{
    printf (" * messages: ");

    int rc = 0;
    //  @selftest
    //  Create two PAIR sockets and connect over inproc
    Socket *output = Socket::createPair("@inproc://zmsg.test");
    assert (output);
    Socket *input = Socket::createPair (">inproc://zmsg.test");
    assert (input);

    //  Test send and receive of single-frame message
    Messages msg;
    Frame frame("Hello");
    msg.prepend(frame);
    assert (msg.size() == 1);
    assert (msg.contentSize() == 5);
    rc = msg.send(*output);
    assert (msg.size() == 0);
    assert (rc == 0);

    msg.recv(*input);
    assert (msg.size() == 1);
    assert (msg.contentSize() == 5);
    msg.clear();

    //  Test send and receive of multi-frame message
    msg.appendmem("Frame0", 6);
    msg.appendmem("Frame1", 6);
    msg.appendmem("Frame2", 6);
    msg.appendmem("Frame3", 6);
    msg.appendmem("Frame4", 6);
    msg.appendmem("Frame5", 6);
    msg.appendmem("Frame6", 6);
    msg.appendmem("Frame7", 6);
    msg.appendmem("Frame8", 6);
    msg.appendmem("Frame9", 6);

    Messages copy = msg;
    assert (copy.size() == 10);
    rc = copy.send(*output);
    assert (rc == 0);
    rc = msg.send(*output);
    assert (rc == 0);

    copy.recv(*input);
    assert (copy.size() == 10);
    assert (copy.contentSize() == 60);
    copy.clear();

    msg.recv(*input);
    assert (msg.size() == 10);
    assert (msg.contentSize() == 60);

    // create empty file for null test
    QFile file("messages.test");
    file.open(QFile::WriteOnly);
    assert (file.exists());
    file.close();

    file.open(QFile::ReadOnly);
    Messages nulmsg;
    nulmsg.load(&file);
    assert (nulmsg.size() == 0);
    file.close();
    file.remove();

    //  Save to a file, read back
    file.open(QFile::WriteOnly);
    assert (file.exists());
    rc = msg.save(&file);
    assert (rc == 0);
    file.close();

    file.open(QFile::ReadOnly);
    msg.load(&file);
    file.close();    
    assert (msg.size() == 10);
    assert (msg.contentSize() == 60);
    file.remove();

    //  Remove all frames except first and last
    int frame_nbr;
    for (frame_nbr = 0; frame_nbr < 8; frame_nbr++) {
        Frame* frm = msg.first();
        frm = msg.next(frm);
        msg.removeAll(frm);
        delete frm;
    }

    //  Test message frame manipulation
    assert (msg.size() == 2);
    Frame* frm = msg.last();
    assert(frm);
    assert (*frm == "Frame9");
    assert (msg.contentSize() == 12);
    frm = new Frame("Address", 7);
    assert (frm);
    msg.prepend(frm);
    assert (msg.size() == 3);
    msg.append(QString("Body"));
    assert (msg.size() == 4);
    frm = msg.pop();
    QString str = frm->toString();
    delete frm;
    assert (msg.size() == 3);
    QString body = msg.popstr();
    assert (body == "Frame0");

    //  Test encoding/decoding
    msg.clear();
    byte *blank = (byte *) zmalloc (100000);
    assert (blank);
    msg.appendmem(blank, 0);
    msg.appendmem(blank, 1);
    msg.appendmem(blank, 253);
    msg.appendmem(blank, 254);
    msg.appendmem(blank, 255);
    msg.appendmem(blank, 256);
    msg.appendmem(blank, 65535);
    msg.appendmem(blank, 65536);
    msg.appendmem(blank, 65537);
    free (blank);
    assert (msg.size() == 9);
    QByteArray buffer = msg.encode();

    msg.clear();
    msg.decode(buffer);
    assert (msg.size() == 9);
    msg.clear();

    //  Test submessages
    msg.clear();
    Messages submsg;
    msg.push(QString("matr"));
    submsg.push(QString("joska"));
    msg.append(submsg);

    Messages *su = msg.popmsg();
    assert (su == NULL);   // string "matr" is not encoded zmsg_t, so was discarded
    su = msg.popmsg();
    assert (su);
    body = su->popstr();
    assert (body == "joska");
    delete su;

    frm = msg.pop();
    assert (frm == NULL);

    //  Now try methods on an empty message
    assert (msg.size() == 0);
    assert (msg.unwrap() == NULL);
    assert (msg.first() == NULL);
    assert (msg.last() == NULL);
    assert (msg.next(NULL) == NULL);
    assert (msg.pop() == NULL);
    //  Sending an empty message is valid and destroys the message
    assert (msg.send(*output) == 0);

    delete output;
    delete input;

    //  @end
    printf ("OK\n");
}


