#ifndef SEVENT_H
#define SEVENT_H

#include "socket.h"

/*
 *  The QNodeBase class provides an event-driven reactor pattern. The reactor
    handles Poller items (pollers or writers, sockets or fds), and
    once-off or repeated timers. Its resolution is 1 msec. It uses a tickless
    timer to reduce CPU interrupts in inactive processes.
 */

#ifndef ULONG_MAX
#define ULONG_MAX 0xffffffffUL
#endif

class Socket;
class SockEventPrivate;
class QMQ_EXPORT SockEvent : public QObject
{
    Q_OBJECT
public:
    explicit SockEvent(QObject *parent = 0);
    ~SockEvent();

    void setTicketDelay(int msec);
    void setMaxTimers(int ms);
    void setVerbose(bool v);

    int ticketDelay() const;
    int maxTimers() const;

    bool terminated() const;
    bool verbose() const;

    int lastTimerId();

    int appendReader(Socket* socket);
    int appendPoller(Socket* poller);

    void removeReader(Socket* socket);
    void removePoller(Socket* socket);

    // not thread safe functions, use these functions before starting node
    int appendTimer(int delay, int repeattimes=0);
    void appendTicket();
    void removeTimer(int tid);

    static void test(bool verbose);

signals:
    // the rc set to -1 to finish thread
    void readyRead(Socket* sock, int* rc);
    void handleEvent(void* item, int* rc);
    void timeout(int timerid, int* rc);
    void ticket(int* rc);
    void finished();

public slots:
    void start();
    void abort();
    void wait(ulong msec=ULONG_MAX);
    void terminate();

protected:
    SockEventPrivate* const d_ptr;

private:
    Q_DECLARE_PRIVATE(SockEvent)
};

class SETest : public QObject
{
    Q_OBJECT
public:
    SETest(QObject* parent = 0);

    int cancleid;
    int deleteid;
    int pingid;
    Socket* ping;
    SockEvent* node;

public slots:
    void timeoutHappend(int timerid, int *rc);
    void readHappend(Socket*, int* rc);
};

#endif // SEVENT_H
