#ifndef ACTOR_H
#define ACTOR_H

#include "socket.h"

/// this function run in different thread
/// must send an signal in initialization
/// you must terminate function with $TERM command
typedef void (ActorHandler) (Socket* pipe, void* args);

class ActorSocketPrivate;
class QMQ_EXPORT ActorSocket : public Socket
{
public:
    ActorSocket(ActorHandler* handler, void *args, Context* cntx = 0);
    ~ActorSocket();

    static void test();
protected:
    int close();

private:
    Q_DECLARE_PRIVATE(ActorSocket)
};


/// Monitor handler
/// see sample for using monitors
extern "C" QMQ_EXPORT void qmonitor(Socket* pipe, void* sock);
extern "C" QMQ_EXPORT void monitorTest(bool verbose=false);

/// Beacon handler
extern "C" QMQ_EXPORT void qbeacon(Socket* pipe, void*);
extern "C" QMQ_EXPORT void beaconTest(bool verbose=false);

/// Proxy handler
extern "C" QMQ_EXPORT void qproxy(Socket* pipe, void*);
extern "C" QMQ_EXPORT void qforwarder(Socket *pipe, void *);
extern "C" QMQ_EXPORT void proxyTest(bool verbose=false);

#endif // ACTOR_H
