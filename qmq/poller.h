#ifndef QPOLLER_H
#define QPOLLER_H

#include <QObject>

#ifndef QMQ_EXPORT
#define QMQ_EXPORT
#endif

class SocketBase;

class PollerPrivate;
class QMQ_EXPORT Poller : public QObject
{
public:
    explicit Poller(QObject *parent = 0);
    explicit Poller(const QList<SocketBase*> inlist, QObject *parent = 0);
    ~Poller();

    bool expired() const;
    bool terminated() const;

    bool append(SocketBase* socket);
    bool remove(SocketBase* socket);

    void clear();

    SocketBase* wait(int msec);

    static void test();

protected:
    PollerPrivate* const d_ptr;

    int rebuild();

private:
    Q_DISABLE_COPY(Poller)
    Q_DECLARE_PRIVATE(Poller)
};

#endif // QPOLLER_H
