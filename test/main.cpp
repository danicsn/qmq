#include <QCoreApplication>
#include "../qmq/qmq.h"
#include <QThread>

#undef min
#undef max

#include <QTime>
#include <QDebug>

int main (int argc, char *argv [])
{
    QCoreApplication a(argc, argv);

    // you can set main context properties
//    QMNet main_ctx;
//    main_ctx.setIoThreads(4);

    Context::test();
    Frame::test();
    GossipFrame::test();
    Messages::test();
    Socket::test(false);
    Poller::test();
    ActorSocket::test();
    monitorTest(false);
    beaconTest(false); // buggy in linux
    proxyTest(false);
    SockEvent::test(false);

    return a.exec();
}
