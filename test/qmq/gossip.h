#ifndef GOSSIP_H
#define GOSSIP_H

#include "message.h"

#define GOSSIP_MSG_HELLO                   1
#define GOSSIP_MSG_PUBLISH                 2
#define GOSSIP_MSG_PING                    3
#define GOSSIP_MSG_PONG                    4
#define GOSSIP_MSG_INVALID                 5

class GossipFramePrivate;
class QMQ_EXPORT GossipFrame : public Frame
{
public:
    GossipFrame();
    GossipFrame(const QByteArray& frout);

    bool recv(SocketBase &input);
    int send(SocketBase &output, int flags=0);

    int id() const;
    void setId(int id);

    QString key() const;
    void setKey(const QString& k);

    QByteArray routingId() const;
    void setRoutingId(const QByteArray& frout);

    QString value() const;
    void setValue(const QString& v);

    int timeTolive() const;
    void setTimeToLive(int ttl);

    QString command() const;
    void print();

    static void test();

private:
    Q_DECLARE_PRIVATE(GossipFrame)
};

// its not implemented yet
class Socket;
extern "C" QMQ_EXPORT void qgossip(Socket *pipe, void *);
extern "C" QMQ_EXPORT void gossipTest(bool verbose=false);

#endif // GOSSIP_H
