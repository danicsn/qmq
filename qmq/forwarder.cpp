
#include "helper.h"

#include <QDebug>

class ForwarderHandler {
public:
    ForwarderHandler(Socket* pip) {
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
            qFatal("forwarder: invalid socket type '%s'", type.toLatin1().data());
            return NULL;
        }

        Socket* sock = (Socket*)pipe->context()->createSocket(index);
        if(sock)
        {
            if(sock->attach(endpoint, true))
            {
                delete sock;
                sock = NULL;
                qFatal("forwarder: invalid endpoints '%s'", endpoint);
            }
        }
        return sock;
    }

    void configure(Socket **socket, Messages& msgs, const QString& name);
    int handlePipe();
    void s_switch(Socket* in, Socket* out);

    ~ForwarderHandler() {
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


void ForwarderHandler::configure(Socket **socket, Messages &msgs, const QString &name)
{
    QString type = msgs.popstr();
    QString endpoint1 = msgs.popstr();
    QString endpoint2 = msgs.popstr();

    if(endpoint2.isEmpty()) endpoint2 = endpoint1;

    if(*socket == NULL)
    {
        *socket = createSocket(type, 0);
        poller.append(*socket);
    }

    if(endpoint1 == "SUBSCRIBER")
    {
        (*socket)->setSubscribe(endpoint2);
        return;
    }
    else if(endpoint1 == "SETID")
    {
        (*socket)->setIdentity(endpoint2);
        return;
    }
    else
    {
        int rc = (*socket)->attach(endpoint2.toLatin1().data(), true);
        if(rc == -1)
            poller.remove(*socket);
    }

    if(verbose)
        qDebug("srproxy: - %s type=%s attach=%s", name.toLatin1().data(), type.toLatin1().data(), endpoint2.toLatin1().data());

}

int ForwarderHandler::handlePipe()
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

void ForwarderHandler::s_switch(Socket *in, Socket *out)
{
    void *zmq_input = in->resolve();
    void *zmq_output = out->resolve();
    void *zmq_capture = capture ? capture->resolve() : NULL;

    zmq_msg_t msg;
    zmq_msg_init (&msg);

    if(out->type() == ZMQ_DEALER)
    {
        zmq_send(zmq_output, "", 0, ZMQ_SNDMORE);
    }
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

void qforwarder(Socket *pipe, void *)
{
    ForwarderHandler ph(pipe);
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
