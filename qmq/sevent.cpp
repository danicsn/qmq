#include "sevent_p.h"
#include "helper.h"

#include <QDebug>
#include <QEventLoop>

class QReaderSocket
{
public:
    QReaderSocket()
    { m_sock = 0; }

    QReaderSocket(Socket* sock)
    {
        tolerant = true;
        errors = 0;
        m_sock = sock;
    }

    Socket* m_sock;
    int errors;
    bool tolerant;
};

class QPollerT
{
public:
    QPollerT() {}
    QPollerT(zmq_pollitem_t* handle)
    {
        item = *handle;
        tolerant = true;
        errors = 0;
    }

    zmq_pollitem_t item;
    int errors;
    bool tolerant;
};

class NTimer {
public:
    NTimer(int ms, int repeat) {
        delay = ms; when = clock_mono() + ms; repeattimes = repeat;
    }

    inline friend bool operator <(const NTimer& f,const NTimer& s);
    inline friend bool operator ==(const NTimer& f,const NTimer& s);
    int timerid;
    int delay;
    int repeattimes;
    int64_t when;
};

bool operator <(const NTimer &f, const NTimer &s)
{
    return f.when < s.when;
}

bool operator ==(const NTimer &f, const NTimer &s)
{
    return f.when == s.when;
}

class NTicket {
public:
    NTicket(int ms) {
        delay = ms; when = clock_mono() + ms;
    }

    inline friend bool operator <(const NTicket& f,const NTicket& s);
    inline friend bool operator ==(const NTicket& f,const NTicket& s);
    int delay;
    int64_t when;
};

bool operator <(const NTicket &f, const NTicket &s)
{
    return f.when < s.when;
}

bool operator ==(const NTicket &f, const NTicket &s)
{
    return f.when == s.when;
}

int
s_sock_type (void *self)
{
    assert (self);
#   if defined (ZMQ_TYPE)
    int type;
    size_t option_len = sizeof (int);
    zmq_getsockopt (self, ZMQ_TYPE, &type, &option_len);
    return type;
#   else
    return 0;
#   endif
}

char *
s_sockname (int socktype)
{
    char *type_names [] = {
        "PAIR", "PUB", "SUB", "REQ", "REP",
        "DEALER", "ROUTER", "PULL", "PUSH",
        "XPUB", "XSUB", "STREAM"
    };
    //  This array matches ZMQ_XXX type definitions
    assert (ZMQ_PAIR == 0);
#if defined (ZMQ_STREAM)
    assert (socktype >= 0 && socktype <= ZMQ_STREAM);
#else
    assert (socktype >= 0 && socktype <= ZMQ_XSUB);
#endif
    return type_names [socktype];
}

/*
 *
 *
 *
 * private part
 * ***********************************/

SockEventPrivate::SockEventPrivate(SockEvent* parent) : q_ptr(parent) {
    maxtimers = 2000;
    pollset = 0; readacts = 0; pollacts = 0;
    lasttimerid = 0;
    need_rebuild = true;
    verbose = false;
    terminated = false;

    connect(this, SIGNAL(finished()), parent , SIGNAL(finished()));
}

SockEventPrivate::~SockEventPrivate()
{
    terminated = true;
    wait();

    foreach (int timer_id, zombies) {
        NTimer* timer = timers_list.value(timer_id);
        delete timer;
        timers_list.remove(timer_id);
    }

    foreach (QReaderSocket* r, readers_list) {
        readers_list.removeAll(r);
        delete r;
    }

    foreach (QPollerT* t, pollers_list) {
        pollers_list.removeAll(t);
        delete t;
    }

    foreach (NTimer* r, timers_list.values()) {
        delete r;
    }

    foreach (NTicket* r, tickets_list) {
        tickets_list.removeAll(r);
        delete r;
    }

    free(pollset);
    delete readacts;
    delete pollacts;
}

void SockEventPrivate::run()
{
    Q_Q(SockEvent);
    int rc = 0;
    while(!terminated)
    {
        m_rebuild.lock();
        if(need_rebuild)
        {
            rc = rebuild();
            if (rc) break;
        }
        m_rebuild.unlock();

        rc = zmq_poll(pollset, poll_size, tickless());
        if (rc == -1) {
            if (verbose)
                qDebug("QNODE: interrupted");
            rc = 0;
            break;              //  Context has been shut down
        }

        //  Handle any timers that have now expired
        int64_t time_now = clock_mono ();
        foreach (NTimer* timer, timers_list) {
            if (time_now >= timer->when) {
                if (verbose)
                    qDebug("QNODE: call timer handler id=%d", timer->timerid);
                emit q->timeout(timer->timerid, &rc);
                if (rc == -1)
                    break;      //  Timer handler signaled break
                if (timer->repeattimes && --timer->repeattimes == 0)
                    removeTimer(timer->timerid);
                else
                    timer->when += timer->delay;
            }
        }

        //  Handle any tickets that have now expired
        foreach(NTicket* ticket, tickets_list) {
            if(time_now >= ticket->when) {
                if (verbose)
                    qDebug("QNODE: call ticket handler");
                emit q->ticket(&rc);
                if (rc == -1)
                    break;      //  Timer handler signaled break
                deleteTicket(ticket);
            }
        }

        //  Handle any readers and pollers that are ready
        int item_nbr;
        for (item_nbr = 0; item_nbr < poll_size && rc >= 0; item_nbr++) {
            QReaderSocket *reader = &readacts[item_nbr];
            if(reader->m_sock) {
                if ((pollset[item_nbr].revents & ZMQ_POLLERR)
                && !reader->tolerant) {
                    if (verbose)
                        qWarning("QNODE: can't read %s socket: %s",
                                      reader->m_sock->type_str(),
                                      zmq_strerror (zmq_errno ()));
                    //  Give handler one chance to handle error, then kill
                    //  reader because it'll disrupt the reactor otherwise.
                    if (reader->errors++) {
                        readerEnd(reader->m_sock);
                        pollset[item_nbr].revents = 0;
                    }
                }
                else
                    reader->errors = 0;     //  A non-error happened

                if (pollset[item_nbr].revents) {
                    if (verbose)
                        qDebug("QNODE: call %s socket handler", reader->m_sock->type_str());
                    emit q->readyRead(reader->m_sock, &rc);
                    if (rc == -1 || need_rebuild)
                        break;
                }
            }
            else {
                QPollerT *poller = &pollacts[item_nbr];
                assert (pollset[item_nbr].socket == poller->item.socket);

                if ((pollset[item_nbr].revents & ZMQ_POLLERR)
                && !poller->tolerant) {
                    if (verbose)
                        qWarning("QNODE: can't poll %s socket (%p, %d): %s",
                                      poller->item.socket ?
                                      s_sockname (s_sock_type (poller->item.socket)) : "FD",
                                      poller->item.socket, poller->item.fd,
                                      zmq_strerror (zmq_errno ()));
                    //  Give handler one chance to handle error, then kill
                    //  poller because it'll disrupt the reactor otherwise.
                    if (poller->errors++) {
                        pollend(&poller->item);
                        pollset[item_nbr].revents = 0;
                    }
                }
                else
                    poller->errors = 0;     //  A non-error happened

                if (pollset [item_nbr].revents) {
                    if (verbose)
                        qDebug("QNODE: call %s socket handler (%p, %d)",
                                    poller->item.socket ?
                                    s_sockname (s_sock_type (poller->item.socket)) : "FD",
                                    poller->item.socket, poller->item.fd);
                        emit q->handleEvent(&(poller->item), &rc);
                    if (rc == -1 || need_rebuild)
                        break;
                }
            }
        }

        //  Now handle any timer zombies
        //  This is going to be slow if we have many timers; we might use
        //  a faster lookup on the timer list.
        foreach (int timer_id, zombies) {
            removeTimer(timer_id);
        }

        if (rc == -1)
            break;

        // while end
    }

    terminated = true;
}

int SockEventPrivate::rebuild()
{
    free(pollset);
    delete pollacts;
    delete readacts;

    pollset = NULL; pollacts = NULL; readacts = NULL;

    poll_size = readers_list.count() + pollers_list.count();
    pollset = (zmq_pollitem_t*)zmalloc(poll_size * sizeof(zmq_pollitem_t));
    if(!pollset)
        return -1;

    readacts = new (std::nothrow) QReaderSocket[poll_size];
    if(!readacts)
        return -1;

    pollacts = new (std::nothrow) QPollerT[poll_size];
    if(!pollacts)
        return -1;

    int item_nr = 0;
    foreach (QReaderSocket* r, readers_list) {
        zmq_pollitem_t poll_item = { r->m_sock->resolve(), 0, ZMQ_POLLIN };
        pollset[item_nr] = poll_item;
        readacts[item_nr] = *r;
        item_nr++;
    }

    foreach (QPollerT* p, pollers_list) {
        pollset[item_nr] = p->item;
        pollacts[item_nr] = *p;
        item_nr++;
    }

    need_rebuild = false;
    return 0;
}

int SockEventPrivate::removeTimer(int id)
{
    NTimer* timer = timers_list.value(id, 0);
    int n = timers_list.remove(id);
    delete timer;
    return n;
}

long SockEventPrivate::tickless()
{
    int64_t lesstick = clock_mono() + 1000 * 3600;

    //  Scan timers, which are not sorted
    //  Find earliest timer
    foreach (NTimer* t, timers_list.values()) {
        if(t->when < lesstick) lesstick = t->when;
    }

    if(!tickets_list.isEmpty())
    {
        NTicket* ticket = tickets_list.first();
        if(lesstick > ticket->when) lesstick = ticket->when;
    }

    long timeout = long(lesstick - clock_mono());
    if (timeout < 0)
        timeout = 0;
    if (verbose)
        qDebug("loop polling for %d msec", (int) timeout);

    return timeout;
}

int SockEventPrivate::appendReader(Socket *sock)
{
    assert(sock);

    QReaderSocket* reader = new (std::nothrow) QReaderSocket(sock);
    if(reader) {
        readers_list.append(reader);

        m_rebuild.lock();
        need_rebuild = true;
        m_rebuild.unlock();

        if (verbose)
            qDebug("QNODE: register %s reader", sock->type_str());
        return 0;
    }

    return -1;
}

void SockEventPrivate::readerEnd(Socket *sock)
{
    foreach (QReaderSocket* rsock, readers_list) {
        if(rsock->m_sock == sock){
            m_rebuild.lock();
            need_rebuild = true;
            m_rebuild.unlock();

            readers_list.removeOne(rsock);
            delete rsock;
        }
    }
}

void SockEventPrivate::readerSetTollerant(Socket *sock)
{
    foreach (QReaderSocket* rsock, readers_list) {
        if(rsock->m_sock == sock){
            rsock->tolerant = true;
        }
    }
}

int SockEventPrivate::appendPoll(zmq_pollitem_t *item)
{
    if (item->socket
    &&  streq (s_sockname (s_sock_type (item->socket)), "UNKNOWN"))
        return -1;

    QPollerT* poller = new (std::nothrow) QPollerT(item);
    if(poller) {
        pollers_list.append(poller);

        m_rebuild.lock();
        need_rebuild = true;
        m_rebuild.unlock();

        if(verbose) {
            qDebug("QNODE: register %s poller (%p, %d)",
                    item->socket ? s_sockname (s_sock_type (item->socket)) : "FD",
                    item->socket, item->fd);
        }

        return 0;
    }
    return -1;
}

void SockEventPrivate::pollend(zmq_pollitem_t *item)
{
    foreach (QPollerT* poller, pollers_list) {
        bool match = false;
        if (item->socket) {
            if (item->socket == poller->item.socket)
                match = true;
        }
        else {
            if (item->fd == poller->item.fd)
                match = true;
        }

        if(match)
        {
            pollers_list.removeAll(poller);
            m_rebuild.lock();
            need_rebuild = true;
            m_rebuild.unlock();
        }
    }

    if(verbose) {
        qDebug("QNODE: cancel %s poller (%p, %d)",
                item->socket ? s_sockname (s_sock_type (item->socket)) : "FD",
                item->socket, item->fd);
    }
}

void SockEventPrivate::pollSetTollerant(zmq_pollitem_t *item)
{
    foreach (QPollerT* poller, pollers_list) {
        bool match = false;
        if (item->socket) {
            if (item->socket == poller->item.socket)
                match = true;
        }
        else {
            if (item->fd == poller->item.fd)
                match = true;
        }

        if(match)
        {
            poller->tolerant = true;
        }
    }
}

int SockEventPrivate::appendTimer(int times, int dms)
{
    QMutexLocker locker(&m_rebuild);

    if(timers_list.count() >= maxtimers)
    {
        qDebug("QNODE: timer limit reached (max=%d)", maxtimers);
        return -1;
    }

    int timer_id = ++lasttimerid;
    NTimer* t = new NTimer(dms, times);
    t->timerid = timer_id;
    timers_list.insert(timer_id, t);
    if(verbose)
        qDebug("QNODE: register timer id=%d delay=%d times=%d",
                            timer_id, (int) dms, (int) times);

    return timer_id;
}

int SockEventPrivate::timerEnd(int timer_id)
{
    if(terminated)
        return removeTimer(timer_id);
    else
    {
        QMutexLocker locker(&m_rebuild);
        zombies.append(timer_id);
    }
    return 0;
}

void *SockEventPrivate::appendTicket()
{
    QMutexLocker locker(&m_rebuild);
    assert(ticketdelay);
    NTicket* handle = new NTicket(ticketdelay);
    tickets_list.append(handle);
    return handle;
}

void SockEventPrivate::resetTicket(void *handle)
{
    QMutexLocker locker(&m_rebuild);
    NTicket* ticket = static_cast<NTicket*>(handle);
    ticket->when = clock_mono() + ticket->delay;

    // send to end
    tickets_list.removeOne(ticket);
    tickets_list.insert(tickets_list.size() - 1, ticket);
}

void SockEventPrivate::deleteTicket(void *handle)
{
    QMutexLocker locker(&m_rebuild);
    NTicket* ticket = static_cast<NTicket*>(handle);

    tickets_list.removeOne(ticket);
    delete ticket;
}


/*
 *
 * ****/

SockEvent::SockEvent(QObject *parent) : d_ptr(new SockEventPrivate(this)),
    QObject(parent)
{
}

SockEvent::~SockEvent()
{
    delete d_ptr;
}

void SockEvent::setTicketDelay(int msec)
{
    Q_D(SockEvent);
    d->ticketdelay = msec;
}

void SockEvent::setMaxTimers(int ms)
{
    Q_D(SockEvent);
    d->maxtimers = ms;
}

void SockEvent::setVerbose(bool v)
{
    Q_D(SockEvent);
    d->verbose = v;
}

int SockEvent::ticketDelay() const
{
    Q_D(const SockEvent);
    return d->ticketdelay;
}

int SockEvent::maxTimers() const
{
    Q_D(const SockEvent);
    return d->maxtimers;
}

bool SockEvent::terminated() const
{
    Q_D(const SockEvent);
    return d->terminated;
}

bool SockEvent::verbose() const
{
    Q_D(const SockEvent);
    return d->verbose;
}

int SockEvent::lastTimerId()
{
    Q_D(const SockEvent);
    return d->lasttimerid;
}

int SockEvent::appendReader(Socket *socket)
{
    Q_D(SockEvent);
    return d->appendReader(socket);
}

int SockEvent::appendPoller(Socket *poller)
{
    Q_D(SockEvent);
    zmq_pollitem_t item = { poller->resolve(), 0, ZMQ_POLLIN };
    return d->appendPoll(&item);
}

int SockEvent::appendTimer(int delay, int repeattimes)
{
    Q_D(SockEvent);
    return d->appendTimer(repeattimes, delay);
}

void SockEvent::appendTicket()
{
    Q_D(SockEvent);
    d->appendTicket();
}

void SockEvent::removeTimer(int tid)
{
    Q_D(SockEvent);
    d->removeTimer(tid);
}

void SockEvent::removeReader(Socket *socket)
{
    Q_D(SockEvent);
    d->readerEnd(socket);
}

void SockEvent::removePoller(Socket *socket)
{
    Q_D(SockEvent);
    zmq_pollitem_t item = { socket->resolve(), 0, ZMQ_POLLIN };
    d->pollend(&item);
}

void SockEvent::test(bool verbose)
{
    printf (" * SockEvent: ");
    int rc = 0;
    //  @selftest
    //  Create two PAIR sockets and connect over inproc
    Socket *output = Socket::createPair();
    assert (output);
    output->bind("inproc://loop.test");

    Socket *input = Socket::createPair();
    assert (input);
    input->connect("inproc://loop.test");

    SockEvent node;
    node.setVerbose(verbose);

    //  Create a timer that will be cancelled
    int fid = node.appendTimer(1000, 1);
    int cid = node.appendTimer(5, 1);

    //  After 20 msecs, send a ping message to output3
    int pid = node.appendTimer(20, 1);

    //  When we get the ping message, end the reactor
    rc = node.appendReader(input);
    assert (rc == 0);

    node.setTicketDelay(10000);
    node.appendTicket();
    node.appendTicket();
    node.appendTicket();

    SETest nt;
    nt.ping = output;
    nt.deleteid = fid;
    nt.cancleid = cid;
    nt.pingid = pid;
    nt.node = &node;

    node.start();

    QObject::connect(&node, SIGNAL(timeout(int,int*)), &nt, SLOT(timeoutHappend(int,int*)));
    QObject::connect(&node, SIGNAL(readyRead(Socket*,int*)), &nt, SLOT(readHappend(Socket*,int*)));

    QEventLoop q;
    QObject::connect(&node, SIGNAL(finished()), &q, SLOT(quit()));
    q.exec();

    delete output;
    delete input;
    //  @end
    printf ("OK\n");
}

void SockEvent::start()
{
    Q_D(SockEvent);
    d->start();
}

void SockEvent::abort()
{
    Q_D(SockEvent);
    d->terminated = true;
}

void SockEvent::wait(ulong msec)
{
    Q_D(SockEvent);
    d->wait(msec);
}

void SockEvent::terminate()
{
    Q_D(SockEvent);
    d->terminated = true;
    d->terminate();
}



SETest::SETest(QObject *parent) : QObject(parent)
{}

void SETest::timeoutHappend(int timerid, int *rc)
{
    if(timerid == cancleid)
        node->removeTimer(deleteid);
    else if(timerid == pingid)
        ping->sendstr("PING");

    *rc = 0;
}

void SETest::readHappend(Socket *, int *rc)
{
    *rc = -1;
}
