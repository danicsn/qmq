#include "helper.h"
#include "socket_p.hpp"
#include <QThread>
#include <QDebug>

/// FIX This implemetation of Shim if its wrong

class Shim : public QThread {
public:
    Shim() {
        handler = 0;
        pipe = 0;
        args = 0;
    }

    ~Shim() {
        wait(); // wait for finishing
        delete pipe; // pipe delete outside thread
    }

    void run()
    {
        if(handler)
        {
            handler(pipe, args);

            //  Do not block, if the other end of the pipe is already deleted
            pipe->setSndtimeo(0);

            // send destruct signal
            pipe->signal(0);
        }
    }

    ActorHandler *handler;
    Socket* pipe;
    void* args;
};


class ActorSocketPrivate : public SocketPrivate {
public:
    ActorSocketPrivate() {}
    Shim shim;
};

//

ActorSocket::ActorSocket(ActorHandler *handler, void* args, Context *cntx) :
    Socket(*(new ActorSocketPrivate), cntx)
{
    Q_D(ActorSocket);

    if(!cntx)
    {
        cntx = srnet;
        if(cntx->thread() == this->thread())
            setParent(cntx);
    }

    d->shim.pipe = new Socket;
    cntx->appendPipe(d->shim.pipe, this);

    d->shim.handler = handler;
    d->shim.args = args;

    // using same priority of main thread
    d->shim.start();

    // wait untill thread run
    wait();
}

ActorSocket::~ActorSocket()
{
    Q_D(ActorSocket);
    if(d->m_pcntxt)
        d->m_pcntxt->closeSocket(this);
}

int ActorSocket::close()
{
    // wait until thread finished
    setSndtimeo(0);
    if(sendstr("$TERM") == 0)
        wait();

    return Socket::close();
}

static void
echo_actor (Socket* pipe, void *args)
{
    //  Do some initialization
    assert (streq ((char *) args, "Hello, World"));
    // send start signal don't forget this
    pipe->signal(0);

    bool terminated = false;
    Messages msg;
    while (!terminated) {
        msg.recv(*pipe);
        if (msg.size() == 0)
            break;              //  Interrupted
        QString command = msg.popstr();
        //  All actors must handle $TERM in this way
        if (command == "$TERM")
            terminated = true;
        else
        //  This is an example command for our test actor
        if (command == "ECHO")
            msg.send(*pipe);
        else {
            puts ("E: invalid message to actor");
            assert (false);
        }
    }
}

void ActorSocket::test()
{
    printf (" * actor: ");

    //  @selftest
    ActorSocket actor(echo_actor, (void*)"Hello, World");

    actor.sendx("ECHO", "This is a string", NULL);
    QString string = actor.recvstr();
    assert (string == "This is a string");
    //  @end

    printf ("OK\n");
}

// Monitor implementation
// a monitor has different class of pollings

class MonitorHandler {
public:
    MonitorHandler(Socket* pip, Socket* monitored) {
        pipe = pip;
        monitor = monitored;
        terminated = false;
        verbose = false;
        poller.append(pipe);
        sink = 0;
    }

    ~MonitorHandler()
    {
#if defined (ZMQ_EVENT_ALL)
        zmq_socket_monitor (monitor->resolve(), NULL, 0);
#endif
        sink->deleteLater();
    }

    void listen(const QString &msg);
    void start();
    int handlePipe();
    void handleSink();

    Socket* sink;
    Socket* pipe;
    Socket* monitor;
    Poller poller;
    int events;
    bool terminated;
    bool verbose;
};

void MonitorHandler::listen(const QString& msg)
{
#if defined (ZMQ_EVENT_ALL)
    if (msg == "CONNECTED")
        events |= ZMQ_EVENT_CONNECTED;
    else
    if (msg == "CONNECT_DELAYED")
        events |= ZMQ_EVENT_CONNECT_DELAYED;
    else
    if (msg == "CONNECT_RETRIED")
        events |= ZMQ_EVENT_CONNECT_RETRIED;
    else
    if (msg == "LISTENING")
        events |= ZMQ_EVENT_LISTENING;
    else
    if (msg == "BIND_FAILED")
        events |= ZMQ_EVENT_BIND_FAILED;
    else
    if (msg == "ACCEPTED")
        events |= ZMQ_EVENT_ACCEPTED;
    else
    if (msg == "ACCEPT_FAILED")
        events |= ZMQ_EVENT_ACCEPT_FAILED;
    else
    if (msg == "CLOSED")
        events |= ZMQ_EVENT_CLOSED;
    else
    if (msg == "CLOSE_FAILED")
        events |= ZMQ_EVENT_CLOSE_FAILED;
    else
    if (msg == "DISCONNECTED")
        events |= ZMQ_EVENT_DISCONNECTED;
    else
#if (ZMQ_VERSION_MAJOR == 4)
    if (msg == "MONITOR_STOPPED")
        events |= ZMQ_EVENT_MONITOR_STOPPED;
    else
#endif
    if (msg == "ALL")
        events |= ZMQ_EVENT_ALL;
    else
        qWarning() << "zmonitor: - invalid listen event=" << msg;
#endif
}

void MonitorHandler::start()
{
    assert (!sink);
    char *endpoint = zsys_sprintf ("inproc://qmonitor-%p", monitor->resolve());
    assert (endpoint);
    int rc;
#if defined (ZMQ_EVENT_ALL)
    rc = zmq_socket_monitor(monitor->resolve(), endpoint, events);
    assert (rc == 0);
#endif
    Context* cntx = monitor->context();
    sink = (Socket*)cntx->createSocket(ZMQ_PAIR);
    rc = sink->connect("%s", endpoint);
    assert (rc == 0);
    poller.append(sink);
    free (endpoint);
}

int MonitorHandler::handlePipe()
{
    Messages mlist;
    if(!mlist.recv(*pipe))
        return -1;

    QString cmd = mlist.popstr();
    if(cmd.isNull())
        return -1;

    if(verbose)
        qDebug() << "qmonitor: API command=" << cmd;

    if(cmd == "LISTEN")
    {
        QString ev = mlist.popstr();
        while (!ev.isNull()) {
            if(verbose)
                qDebug() << "qmonitor: - listening to event=" << ev;
            listen(ev);
            ev = mlist.popstr();
        }
    }
    else
    if(cmd == "START")
    {
        start();
        pipe->signal(0);
    }
    else
    if (cmd == "VERBOSE")
        verbose = true;
    else
    if (cmd == "$TERM")
    {
        terminated = true;
    }
    else {
        qFatal("qmonitor: - invalid command: %s", cmd.toLocal8Bit().data());
    }

    return 0;
}

void MonitorHandler::handleSink()
{
#if defined (ZMQ_EVENT_ALL)
#if (ZMQ_VERSION_MAJOR == 4)
    //  First frame is event number and value
    Frame frame;
    frame.recv(*sink);
    int event = *(uint16_t *) (frame.data());
    int value = *(uint32_t *) (((uchar*)frame.data()) + 2);
    //  Address is in second message frame
    QString address = sink->recvstr();

#elif (ZMQ_VERSION_MAJOR == 3 && ZMQ_VERSION_MINOR == 2)
    //  zmq_event_t is passed as-is in the frame
    QSRFrame frame;
    frame.recv(*sink);
    zmq_event_t *eptr = (zmq_event_t *) frame.data();
    int event = eptr->event;
    int value = eptr->data.listening.fd;
    QString address(eptr->data.listening.addr);

#else
    //  We can't plausibly be here with other versions of libzmq
    assert (false);
#endif

    //  Now map event to text equivalent
    QString name;
    switch (event) {
        case ZMQ_EVENT_ACCEPTED:
            name = "ACCEPTED";
            break;
        case ZMQ_EVENT_ACCEPT_FAILED:
            name = "ACCEPT_FAILED";
            break;
        case ZMQ_EVENT_BIND_FAILED:
            name = "BIND_FAILED";
            break;
        case ZMQ_EVENT_CLOSED:
            name = "CLOSED";
            break;
        case ZMQ_EVENT_CLOSE_FAILED:
            name = "CLOSE_FAILED";
            break;
        case ZMQ_EVENT_DISCONNECTED:
            name = "DISCONNECTED";
            break;
        case ZMQ_EVENT_CONNECTED:
            name = "CONNECTED";
            break;
        case ZMQ_EVENT_CONNECT_DELAYED:
            name = "CONNECT_DELAYED";
            break;
        case ZMQ_EVENT_CONNECT_RETRIED:
            name = "CONNECT_RETRIED";
            break;
        case ZMQ_EVENT_LISTENING:
            name = "LISTENING";
            break;
#if (ZMQ_VERSION_MAJOR == 4)
        case ZMQ_EVENT_MONITOR_STOPPED:
            name = "MONITOR_STOPPED";
            break;
#endif
        default:
            qCritical("illegal socket monitor event: %d", event);
            name = "UNKNOWN";
            break;
    }
    if (verbose)
        qDebug("qmonitor: %s - %s", name.toLocal8Bit().data(), address.toLocal8Bit().data());

    pipe->sendstr(name, true);
    pipe->sendstr(QString::number(value), true);
    pipe->sendstr(address);
#endif
}

void qmonitor(Socket* pipe, void* sock)
{
    MonitorHandler self(pipe, static_cast<Socket*>(sock));
    //  Signal successful initialization
    pipe->signal(0);

    while (!self.terminated) {
        SocketBase *which = self.poller.wait(-1);
        if (which == pipe)
            self.handlePipe();
        else
        if (which == self.sink)
            self.handleSink();
        else
        if (self.poller.terminated())
            break;          //  Interrupted
    }
}

//  --------------------------------------------------------------------------
//  Selftest

#if defined (ZMQ_EVENT_ALL)
static void
s_assert_event (ActorSocket &self, const QString& expected)
{
    Messages mlist;
    mlist.recv(self);
    assert (mlist.size() > 0);
    QString event = mlist.popstr();
    assert (event == expected);
}
#endif

void monitorTest(bool verbose)
{
    printf (" * qmonitor: ");
    if (verbose)
        printf ("\n");

#if defined (ZMQ_EVENT_ALL)
    //  @selftest
    Socket *client = Socket::createDealer();
    assert (client);
    ActorSocket *clientmon = new ActorSocket(qmonitor, client, srnet);
    assert (clientmon);
    if (verbose)
        clientmon->sendx("VERBOSE", NULL);
    clientmon->sendx("LISTEN", "LISTENING", "ACCEPTED", NULL);
    clientmon->sendx("START", NULL);
    clientmon->wait();

    Socket *server = Socket::createDealer();
    assert (server);
    ActorSocket *servermon = new ActorSocket(qmonitor, server, srnet);
    assert (servermon);
    if (verbose)
        servermon->sendx("VERBOSE", NULL);
    servermon->sendx("LISTEN", "CONNECTED", "DISCONNECTED", NULL);
    servermon->sendx("START", NULL);
    servermon->wait();

    //  Allow a brief time for the message to get there...
    zmq_poll (NULL, 0, 10);

    //  Check client is now listening
    int port_nbr = client->bind("tcp://127.0.0.1:*");
    assert (port_nbr != -1);
    s_assert_event (*clientmon, "LISTENING");

    //  Check server connected to client
    server->connect("tcp://127.0.0.1:%d", port_nbr);
    s_assert_event (*servermon, "CONNECTED");

    //  Check client accepted connection
    s_assert_event (*clientmon, "ACCEPTED");

    delete clientmon;
    delete servermon;
    delete client;
    delete server;
#endif
    //  @end
    printf ("OK\n");
}
