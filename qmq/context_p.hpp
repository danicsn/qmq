#ifndef PRIVATE_HPP
#define PRIVATE_HPP

#include "helper.h"

#include <QQueue>
#include <QMutex>
#include <QSharedPointer>
/* context private,
 *
 * ******************/

class ContextPrivate {
public:
    ContextPrivate() {
        s_io_threads = 1;
        s_pipehwm = 1000;
        s_sndhwm = 1000;
        s_rcvhwm = 1000;

        context = 0;
    }
    ~ContextPrivate() {
        zmq_term(context);
    }

    void initialize_underlying();
    void init_shadow(ContextPrivate *p);

    void* context;              //  Our 0MQ context
    QList<SocketBase*> sockets;           //  Sockets held by this thread
    QMutex mutex;            //  Make zctx threadsafe
    bool shadow;                //  True if this is a shadow context
    int s_io_threads;              //  Number of IO threads, default 1
    int s_linger;                 //  Linger timeout, default 0
    int s_pipehwm;                //  Send/receive HWM for pipes
    int s_sndhwm;                 //  ZMQ_SNDHWM for normal sockets
    int s_rcvhwm;                 //  ZMQ_RCVHWM for normal sockets
};

/*
 * SRNetworkPrivate
 *
 * ******************************/

class QMNetPrivate : public ContextPrivate {
public:
    QMNetPrivate() {
        s_io_threads = 1;
        s_max_sockets = 1023;
        s_linger = 0;
        s_rcvhwm = 1000;
        s_sndhwm = 1000;
        s_pipehwm = 1000;
        s_ipv6 = 0;
        s_open_sockets = 0;
        s_initialized = false;
    }

    int s_max_sockets;
    int s_ipv6;
    int s_open_sockets;
    bool s_initialized;
};

#endif // PRIVATE_HPP
