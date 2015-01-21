#include "helper.h"

class PollerPrivate {
public:
    PollerPrivate() {
        b_rebuild = true;
        b_expired = false;
        b_terminated = false;
        poll_set = 0;
    }
    ~PollerPrivate() {
        free(poll_set);
    }

    zmq_pollitem_t* poll_set;
    QList<SocketBase*> reader_list;
    bool b_rebuild;
    bool b_expired;
    bool b_terminated;
};

Poller::Poller(QObject *parent) : d_ptr(new PollerPrivate),
    QObject(parent)
{}

Poller::Poller(const QList<SocketBase *> inlist, QObject *parent): d_ptr(new PollerPrivate), QObject(parent)
{
    Q_D(Poller);
    foreach (SocketBase* sock, inlist) {
        d->reader_list.append(sock);
    }
}

Poller::~Poller()
{
    delete d_ptr;
}

bool Poller::expired() const
{
    Q_D(const Poller);
    return d->b_expired;
}

bool Poller::terminated() const
{
    Q_D(const Poller);
    return d->b_terminated;
}

bool Poller::append(SocketBase *socket)
{
    Q_D(Poller);
    if(!socket || d->reader_list.contains(socket)) return false;
    d->b_rebuild = true;
    d->reader_list.append(socket);
    return true;
}

bool Poller::remove(SocketBase *socket)
{
    if(!socket) return false;
    Q_D(Poller);
    bool res = d->reader_list.removeOne(socket);
    d->b_rebuild = d->b_rebuild || res;
    return res;
}

void Poller::clear()
{
    Q_D(Poller);
    d->reader_list.clear();
    d->b_rebuild = true;
    d->b_expired = true;
    d->b_terminated = true;
}

SocketBase *Poller::wait(int msec)
{
    Q_D(Poller);
    d->b_expired = false;
    d->b_terminated = false;
    if(d->b_rebuild)
        rebuild();

    int rc = zmq_poll(d->poll_set, d->reader_list.size(), msec);
    if(rc > 0)
    {
        for(int ir = 0; ir < d->reader_list.size(); ir++)
            if(d->poll_set[ir].revents & ZMQ_POLLIN)
                return d->reader_list[ir];
    }
    else if(rc == 0)
        d->b_expired = true;
    else
        d->b_terminated = true;

    return NULL;
}

int Poller::rebuild()
{
    Q_D(Poller);
    free(d->poll_set);

    d->poll_set = (zmq_pollitem_t*) zmalloc(d->reader_list.size() * sizeof(zmq_pollitem_t));
    if(!d->poll_set) return -1;

    int reader_nbr = 0;
    foreach (SocketBase* reader, d->reader_list) {
        void *socket = reader->resolve();
        if (socket == NULL) {
            d->poll_set [reader_nbr].socket = NULL;
#ifdef _WIN32
            d->poll_set [reader_nbr].fd = *(SOCKET *) reader;
#else
            d->poll_set [reader_nbr].fd = *(int *) reader;
#endif
        }
        else
            d->poll_set [reader_nbr].socket = socket;
        d->poll_set [reader_nbr].events = ZMQ_POLLIN;

        reader_nbr++;
    }

    d->b_rebuild = false;
    return 0;
}

void Poller::test()
{
    printf (" * poller: ");

    //  @selftest
    //  Create a few sockets
    Socket *vent = Socket::createPush();
    assert (vent);
    int port_nbr = vent->bind("tcp://127.0.0.1:*");
    assert (port_nbr != -1);
    Socket *sink = Socket::createPull();
    assert (sink);
    int rc = sink->connect("tcp://127.0.0.1:%d", port_nbr);
    assert (rc != -1);
    Socket *bowl = Socket::createPull();
    assert (bowl);
    Socket *dish = Socket::createPull();
    assert (dish);

    //  Set-up poller
    QList<SocketBase*> mlist;
    mlist << bowl << dish;
    Poller poller(mlist);

    // Add a reader to the existing poller
    bool s = poller.append(sink);
    assert (s);

    vent->sendstr("Hello, World");

    //  We expect a message only on the sink
    Socket *which = (Socket *) poller.wait(-1);
    assert (which == sink);
    assert (poller.expired() == false);
    assert (poller.terminated() == false);
    QString message = which->recvstr();
    assert (message == "Hello, World");

    //  Stop polling reader
    s= poller.remove(sink);
    assert (s);

    delete vent;
    delete sink;
    delete bowl;
    delete dish;
    //  @end

    printf ("OK\n");
}
