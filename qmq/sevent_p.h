#ifndef SEVENT_P_H
#define SEVENT_P_H

#include <QThread>
#include <QMutex>
#include <QMap>

class Socket;
struct zmq_pollitem_t;

class QReaderSocket;
class QPollerT;
class NTimer;
class NTicket;

class SockEvent;
class SockEventPrivate : public QThread
{
public:
    SockEventPrivate(SockEvent* parent);
    ~SockEventPrivate();

    void run();

    int rebuild();
    int removeTimer(int id);
    long tickless();

    int appendReader(Socket* sock);
    void readerEnd(Socket* sock);
    void readerSetTollerant(Socket* sock);

    int appendPoll(zmq_pollitem_t* item);
    void pollend(zmq_pollitem_t* item);
    void pollSetTollerant(zmq_pollitem_t* item);

    int appendTimer(int times, int dms);
    int timerEnd(int timer_id);

    void* appendTicket();
    void resetTicket(void* handle);
    void deleteTicket(void* handle);

    QList<QReaderSocket*> readers_list;
    QList<QPollerT*> pollers_list;
    QMap<int, NTimer*> timers_list;
    QList<NTicket*> tickets_list;
    int ticketdelay;
    int maxtimers;
    int lasttimerid;
    int poll_size;

    QReaderSocket* readacts;
    QPollerT* pollacts;
    zmq_pollitem_t* pollset;

    bool need_rebuild;
    QMutex m_rebuild;
    bool verbose;
    bool terminated;
    QList<int> zombies;

    SockEvent* const q_ptr;
    Q_DECLARE_PUBLIC(SockEvent)
};

#endif // SEVENT_P_H
