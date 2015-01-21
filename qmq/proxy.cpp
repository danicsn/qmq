#include "helper.h"

#include <QDebug>

class ProxyHandler {
public:
    ProxyHandler(Socket* pip) {
        pipe = pip;
        poller.append(pipe);

        terminated = false;
        verbose = false;

        frontend = 0; backend = 0; capture = 0;
    }

    Socket* createSocket(QString type, char* endpoint)
    {
        QString type_names [] = {
                "PAIR", "PUB", "SUB", "REQ", "REP",
                "DEALER", "ROUTER", "PULL", "PUSH",
                "XPUB", "XSUB", type
            };

        int index;
        for (index = 0; type != type_names[index]; index++) ;
        if (index > ZMQ_XSUB) {
            qFatal("proxy: invalid socket type '%s'", type.toLatin1().data());
            return NULL;
        }

        Socket* sock = (Socket*)pipe->context()->createSocket(index);
        if(sock)
        {
            if(sock->attach(endpoint, true))
            {
                delete sock;
                sock = NULL;
                qFatal("proxy: invalid endpoints '%s'", endpoint);
            }
        }
        return sock;
    }

    void configure(Socket **socket, Messages& msgs, const QString& name);
    int handlePipe();
    void s_switch(Socket* in, Socket* out);

    ~ProxyHandler() {
        delete frontend;
        delete backend;
        delete capture;
    }

    Socket *pipe;            //  Actor command pipe
    Poller poller;          //  Socket poller
    Socket *frontend;        //  Frontend socket
    Socket *backend;         //  Backend socket
    Socket *capture;         //  Capture socket
    bool terminated;         //  Did caller ask us to quit?
    bool verbose;            //  Verbose logging enabled?
};


void ProxyHandler::configure(Socket **socket, Messages &msgs, const QString &name)
{
    QString type = msgs.popstr();
    QString endpoint = msgs.popstr();

    if(verbose)
        qDebug("qproxy: - %s type=%s attach=%s", name.toLatin1().data(), type.toLatin1().data(), endpoint.toLatin1().data());

    assert(*socket == NULL);
    *socket = createSocket(type, endpoint.toLatin1().data());
    poller.append(*socket);
}

int ProxyHandler::handlePipe()
{
    Messages request;
    request.recv(*pipe);
    if(request.size() <= 0) return -1;

    QString command = request.popstr();
    if (verbose)
        qDebug("proxy: API command=%s", command.toLatin1().data());

    if (command == "FRONTEND") {
        configure(&frontend, request, "frontend");
        pipe->signal(0);
    }
    else
    if (command == "BACKEND") {
        configure(&backend, request, "backend");
        pipe->signal(0);
    }
    else
    if (command == "CAPTURE") {
        capture = (Socket*)pipe->context()->createSocket(ZMQ_PUSH);
        assert (capture);
        QString endpoint = request.popstr();
        int rc = capture->connect("%s", endpoint.toLatin1().data());
        assert (rc == 0);
        pipe->signal(0);
    }
    else
    if (command == "PAUSE") {
        poller.clear();
        poller.append(pipe);
        pipe->signal(0);
    }
    else
    if (command == "RESUME") {
        poller.clear();
        poller.append(pipe);
        poller.append(frontend);
        poller.append(backend);
        pipe->signal(0);
    }
    else
    if (command == "VERBOSE") {
        verbose = true;
        pipe->signal(0);
    }
    else
    if (command == "$TERM")
        terminated = true;
    else {
        qFatal("proxy: - invalid command: %s", command.toLatin1().data());
    }

    return 0;
}

void ProxyHandler::s_switch(Socket *in, Socket *out)
{
    void *zmq_input = in->resolve();
    void *zmq_output = out->resolve();
    void *zmq_capture = capture ? capture->resolve() : NULL;

    zmq_msg_t msg;
    zmq_msg_init (&msg);
    while (true) {
        if (zmq_recvmsg (zmq_input, &msg, ZMQ_DONTWAIT) == -1)
            break;      //  Presumably EAGAIN
        int send_flags = in->receiveMore() ? ZMQ_SNDMORE : 0;
        if (zmq_capture) {
            zmq_msg_t dup;
            zmq_msg_init (&dup);
            zmq_msg_copy (&dup, &msg);
            if (zmq_sendmsg (zmq_capture, &dup, send_flags) == -1)
                zmq_msg_close (&dup);
        }
        if (zmq_sendmsg (zmq_output, &msg, send_flags) == -1) {
            zmq_msg_close (&msg);
            break;
        }
    }
}

void qproxy(Socket *pipe, void *)
{
    ProxyHandler ph(pipe);
    pipe->signal(0);

    while (!ph.terminated) {
        Socket* witch = (Socket*)ph.poller.wait(-1);
        if(ph.poller.terminated())
            break;
        else if(witch == pipe)
            ph.handlePipe();
        else if(witch == ph.frontend)
            ph.s_switch(ph.frontend, ph.backend);
        else if(witch == ph.backend)
            ph.s_switch(ph.backend, ph.frontend);
    }
}

void proxyTest(bool verbose)
{
    printf (" * qproxy: ");
    if (verbose)
        printf ("\n");

    //  @selftest
    //  Create and configure our proxy
    ActorSocket proxy(qproxy, NULL);
    if (verbose) {
        proxy.sendstr("VERBOSE");
        proxy.wait();
    }
    proxy.sendx("FRONTEND", "PULL", "inproc://frontend", NULL);
    proxy.wait();
    proxy.sendx("BACKEND", "PUSH", "inproc://backend", NULL);
    proxy.wait();

    //  Connect application sockets to proxy
    Socket *faucet = Socket::createPush(">inproc://frontend");
    assert (faucet);
    Socket *sink = Socket::createPull(">inproc://backend");
    assert (sink);

    //  Send some messages and check they arrived
    QString hello, world;
    faucet->sendx("Hello", "World", NULL);
    sink->recv("ss", &hello, &world);
    assert (hello == "Hello");
    assert (world == "World");

    //  Test pause/resume functionality
    hello = world = QString();
    proxy.sendx("PAUSE", NULL);
    proxy.wait();
    faucet->sendx("Hello", "World", NULL);
    sink->setRcvtimeo(100);
    sink->recv("ss", &hello, &world);
    assert (hello.isNull() && world.isNull());

    proxy.sendx("RESUME", NULL);
    proxy.wait();
    sink->recv("ss", &hello, &world);
    assert (hello == "Hello");
    assert (world == "World");

    //  Test capture functionality
    Socket *capture = Socket::createPull("inproc://capture");
    assert (capture);

    //  Switch on capturing, check that it works
    proxy.sendx("CAPTURE", "inproc://capture", NULL);
    proxy.wait();
    faucet->sendx("Hello", "World", NULL);
    sink->recv("ss", &hello, &world);
    assert (hello == "Hello");
    assert (world == "World");

    capture->recv("ss", &hello, &world);
    assert (hello == "Hello");
    assert (world == "World");

    delete faucet;
    delete sink;
    delete capture;
    //  @end
    printf ("OK\n");
}
