#include "context_p.hpp"
#include <qthread.h>

void ContextPrivate::initialize_underlying()
{
    mutex.lock();
    context = zmq_init(s_io_threads);
    mutex.unlock();
}

void ContextPrivate::init_shadow(ContextPrivate* p)
{
    if(!p->context) {
        p->initialize_underlying();
        if(!p->context)
            return;
    }

    s_io_threads = p->s_io_threads;
    context = p->context;
    shadow = true;
    s_linger = p->s_linger;
    s_pipehwm = p->s_pipehwm;
    s_sndhwm = p->s_sndhwm;
    s_rcvhwm = p->s_rcvhwm;
}

Context::Context(QObject *parent) : d_ptr(new ContextPrivate),
    QObject(parent)
{}

Context::~Context()
{
    destroy_sockets();
    delete d_ptr;
}

Context::Context(ContextPrivate &d, QObject *parent): d_ptr(&d) , QObject(parent)
{}

void Context::setIoThreads(int iothreads)
{
    Q_D(Context);
    d->mutex.lock();
    d->s_io_threads = iothreads;
    d->mutex.unlock();
}

void Context::setLinger(int linger)
{
    Q_D(Context);
    d->mutex.lock();
    d->s_linger = linger;
    d->mutex.unlock();
}

void Context::setPipehwm(int pipehwm)
{
    Q_D(Context);
    d->mutex.lock();
    d->s_pipehwm = pipehwm;
    d->mutex.unlock();
}

void Context::setSndhwm(int sndhwm)
{
    Q_D(Context);
    d->mutex.lock();
    d->s_sndhwm = sndhwm;
    d->mutex.unlock();
}

void Context::setRcvhwm(int rcvhwm)
{
    Q_D(Context);
    d->mutex.lock();
    d->s_rcvhwm = rcvhwm;
    d->mutex.unlock();
}

Socket *Context::createSocket(int type)
{
    Q_D(Context);
    //  Initialize context now if necessary
    if (!d->context) {
        d->initialize_underlying();
        if (!d->context)
            return NULL;
    }
    //  Create and register socket
    Socket *zocket = new (std::nothrow) Socket;
    if (!zocket)
        return NULL;

    zocket->reset(this, type);

#if (ZMQ_VERSION_MAJOR == 2)
    //  For ZeroMQ/2.x we use sndhwm for both send and receive
    zocket->setHwm(d->sndhwm);
#else
    //  For later versions we use separate SNDHWM and RCVHWM
    zocket->setSndhwm(d->s_sndhwm);
    zocket->setRcvhwm(d->s_rcvhwm);
#endif
    d->mutex.lock();
    d->sockets.append(zocket);
    d->mutex.unlock();
    return zocket;
}

Socket *Context::createPipe()
{
    Q_D(Context);
    Socket *pipe = createSocket(ZMQ_PAIR);
    if (pipe)
        pipe->setHwm(d->s_pipehwm);
    return pipe;
}

void *Context::desctiptor()
{
    Q_D(Context);
    return d->context;
}

void Context::append(SocketBase *sock, int type)
{
    Q_D(Context);
    //  Initialize context now if necessary
    if (!d->context) {
        d->initialize_underlying();
        if (!d->context)
            return;
    }
    //  Create and register socket
    sock->reset(this, type);

#if (ZMQ_VERSION_MAJOR == 2)
    //  For ZeroMQ/2.x we use sndhwm for both send and receive
    zocket->setHwm(d->sndhwm);
#else
    //  For later versions we use separate SNDHWM and RCVHWM
    sock->setSndhwm(d->s_sndhwm);
    sock->setRcvhwm(d->s_rcvhwm);
#endif
    d->mutex.lock();
    d->sockets.removeAll(sock);
    d->sockets.append(sock);
    d->mutex.unlock();
}

void Context::appendPipe(SocketBase *frontend, SocketBase *backend)
{
    Q_D(Context);
    append(frontend, ZMQ_PAIR);
    frontend->setHwm(d->s_pipehwm);

    append(backend, ZMQ_PAIR);
    backend->setHwm(d->s_pipehwm);
}

int Context::closeSocket(SocketBase *zocket)
{
    Q_D(Context);
    assert (zocket);
    zocket->setLinger(d->s_linger);
    int rc = zocket->close();

    d->mutex.lock();
    d->sockets.removeAll(zocket);
    d->mutex.unlock();

    //zocket->deleteLater();

    return rc;
}

void Context::test()
{
    printf (" * ctx (deprecated): ");

    //  @selftest
    Context ctx;

    //  Create a context with many busy sockets, destroy it
    ctx.setIoThreads(1);
    ctx.setLinger(5);       //  5 msecs
    SocketBase *s1 = ctx.createSocket(ZMQ_PAIR);
    assert (s1);
    SocketBase *s2 = ctx.createSocket(ZMQ_XREQ);
    assert (s2);
    SocketBase *s3 = ctx.createSocket(ZMQ_REQ);
    assert (s3);
    SocketBase *s4 = ctx.createSocket(ZMQ_REP);
    assert (s4);
    SocketBase *s5 = ctx.createSocket(ZMQ_PUB);
    assert (s5);
    SocketBase *s6 = ctx.createSocket(ZMQ_SUB);
    assert (s6);
    int rc = s1->connect("tcp://127.0.0.1:5555");
    assert (rc == 0);
    rc = s2->connect("tcp://127.0.0.1:5555");
    assert (rc == 0);
    rc = s3->connect("tcp://127.0.0.1:5555");
    assert (rc == 0);
    rc = s4->connect("tcp://127.0.0.1:5555");
    assert (rc == 0);
    rc = s5->connect("tcp://127.0.0.1:5555");
    assert (rc == 0);
    rc = s6->connect("tcp://127.0.0.1:5555");
    assert (rc == 0);
    //  @end

    printf ("OK\n");
}

void Context::destroy_sockets()
{
    Q_D(Context);
    d->mutex.lock();

    foreach (SocketBase* so, d->sockets) {
        so->setLinger(d->s_linger);
    }

    foreach (SocketBase* so, d->sockets) {
        so->close();
    }
    qDeleteAll(d->sockets);
    d->sockets.clear();

    d->mutex.unlock();
}

/*
 *
 *
 *
 * *******************************************/

QMNet* QMNet::m_instance = 0;

QMNet::QMNet(QObject* parent) : Context(*(new QMNetPrivate), parent)
{
    m_instance = this;
}

QMNet::~QMNet()
{
    shutdown();
}

QMNet *QMNet::instance()
{
    if(!m_instance)
        m_instance = new QMNet;

    return m_instance;
}

void QMNet::init()
{
    Q_D(QMNet);
    if(d->s_initialized)
        return;

    //  Pull process defaults from environment
    if (getenv ("QMN_IO_THREADS"))
        d->s_io_threads = atoi (getenv ("QMN_IO_THREADS"));

    if (getenv ("QMN_MAX_SOCKETS"))
        d->s_max_sockets = atoi (getenv ("QMN_MAX_SOCKETS"));

    if (getenv ("QMN_LINGER"))
        d->s_linger = atoi (getenv ("QMN_LINGER"));

    if (getenv ("QMN_SNDHWM"))
        d->s_pipehwm = atoi (getenv ("QMN_SNDHWM"));

    if (getenv ("QMN_RCVHWM"))
        d->s_pipehwm = atoi (getenv ("QMN_RCVHWM"));

    if (getenv ("QMN_PIPEHWM"))
        d->s_pipehwm = atoi (getenv ("QMN_PIPEHWM"));

    if (getenv ("QMN_IPV6"))
        d->s_ipv6 = atoi (getenv ("QMN_IPV6"));

    d->context = zmq_init(d->s_io_threads);
    zmq_ctx_set (d->context, ZMQ_MAX_SOCKETS, d->s_max_sockets);
    d->s_initialized = true;
}

void QMNet::shutdown()
{
    Q_D(QMNet);
    if(!d->s_initialized) return;

    d->mutex.lock();
    int busy = d->s_open_sockets;
    d->mutex.unlock();

    if(busy)
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
        QThread::msleep(200);
#else
        usleep(200);
#endif

    //  No matter, we are now going to shut down
    //  Print the source reference for any sockets the app did not
    //  destroy properly.
    d->mutex.lock();
    foreach (SocketBase* s, d->sockets) {
        s->close();
        delete s;
    }
    d->sockets.clear();
    d->mutex.unlock();
}

Socket *QMNet::createSocket(int type)
{
    Q_D(QMNet);
    init();
    d->mutex.lock();
    Socket* sock = new (std::nothrow) Socket;

    if(!sock) {
        d->mutex.unlock();
        return NULL;
    }

    sock->reset(this, type);

    sock->setLinger(d->s_linger);
    sock->setSndhwm(d->s_sndhwm);
    sock->setRcvhwm(d->s_rcvhwm);
    sock->setIpv6(d->s_ipv6);

    d->sockets.append(sock);
    d->s_open_sockets++;
    d->mutex.unlock();

    return sock;
}

int QMNet::closeSocket(SocketBase *sock)
{
    Q_D(QMNet);
    if(!sock)
        return 0;

    if(sock->parent() != this)
        return 0;

    d->mutex.lock();
    int count = d->sockets.removeAll(sock);
    if(count == 0)
    {
        d->mutex.unlock();
        return 0;
    }
    int rc = sock->close();
    d->s_open_sockets -= count;
    d->mutex.unlock();

    // thread safe deleting QObject
    // sock->deleteLater();

    return rc;
}

std::pair<Socket *, Socket *> QMNet::createPipe()
{
    Q_D(QMNet);std::pair<Socket *, Socket *> pair;

    Socket *frontend = createSocket(ZMQ_PAIR);
    Socket *backend = createSocket(ZMQ_PAIR);
    if (!frontend || !backend) {
        closeSocket(frontend);
        closeSocket(backend);

        delete frontend;
        delete backend;

        pair.first = NULL;
        pair.second = NULL;
        return pair;
    }

    frontend->setSndhwm(d->s_sndhwm);
    backend->setSndhwm(d->s_sndhwm);

    //  Now bind and connect pipe ends
    char endpoint [32];
    while (true) {
        sprintf(endpoint, "inproc://pipe-%04x-%04x\n",
                 randof (0x10000), randof (0x10000));
        if (frontend->bind("%s", endpoint) == 0)
            break;
    }
    int rc = backend->connect("%s", endpoint);
    assert (rc != -1);          //  Connect cannot fail

    //  Return frontend and backend sockets
    pair.first = frontend;
    pair.second = backend;

    return pair;
}

void QMNet::append(SocketBase *sock, int type)
{
    Q_D(QMNet);
    if(!sock) {
        return;
    }

    init();
    d->mutex.lock();
    sock->reset(this, type);

    int rm = d->sockets.removeAll(sock);
    d->s_open_sockets -= rm;

    sock->setLinger(d->s_linger);
    sock->setSndhwm(d->s_sndhwm);
    sock->setRcvhwm(d->s_rcvhwm);
    sock->setIpv6(d->s_ipv6);

    d->sockets.append(sock);
    d->s_open_sockets++;
    d->mutex.unlock();
}

void QMNet::appendPipe(SocketBase *frontend, SocketBase *backend)
{
    Q_D(QMNet);

    if (!frontend || !backend) {
        return;
    }

    append(frontend, ZMQ_PAIR);
    append(backend, ZMQ_PAIR);

    frontend->setSndhwm(d->s_sndhwm);
    backend->setSndhwm(d->s_sndhwm);

    //  Now bind and connect pipe ends
    char endpoint [32];
    while (true) {
        sprintf(endpoint, "inproc://pipe-%04x-%04x\n",
                 randof (0x10000), randof (0x10000));
        if (frontend->bind("%s", endpoint) == 0)
            break;
    }
    int rc = backend->connect("%s", endpoint);
    assert (rc != -1);          //  Connect cannot fail
}

void QMNet::handleError(const char *reason)
{
#if defined (Q_OS_WIN)
    switch (WSAGetLastError ()) {
        case WSAEINTR:        errno = EINTR;      break;
        case WSAEBADF:        errno = EBADF;      break;
        case WSAEWOULDBLOCK:  errno = EAGAIN;     break;
        case WSAEINPROGRESS:  errno = EAGAIN;     break;
        case WSAENETDOWN:     errno = ENETDOWN;   break;
        case WSAECONNRESET:   errno = ECONNRESET; break;
        case WSAECONNABORTED: errno = EPIPE;      break;
        case WSAESHUTDOWN:    errno = ECONNRESET; break;
        case WSAEINVAL:       errno = EPIPE;      break;
        default:              errno = GetLastError ();
    }
#endif
    if (  errno == EAGAIN
       || errno == ENETDOWN
       || errno == EHOSTUNREACH
       || errno == ENETUNREACH
       || errno == EINTR
       || errno == EPIPE
       || errno == ECONNRESET
#if defined (ENOPROTOOPT)
       || errno == ENOPROTOOPT
#endif
#if defined (EHOSTDOWN)
       || errno == EHOSTDOWN
#endif
#if defined (EOPNOTSUPP)
       || errno == EOPNOTSUPP
#endif
#if defined (EWOULDBLOCK)
       || errno == EWOULDBLOCK
#endif
#if defined (EPROTO)
       || errno == EPROTO
#endif
#if defined (ENONET)
       || errno == ENONET
#endif
          )
        return;             //  Ignore error and try again
    else {
        qFatal("(UDP) error '%s' on %s", strerror (errno), reason);
    }
}

QString QMNet::hostName()
{
    char hostname [NI_MAXHOST];
    gethostname (hostname, NI_MAXHOST);
    hostname [NI_MAXHOST - 1] = 0;
    struct hostent *host = gethostbyname (hostname);
    return QString(host->h_name);
}

void QMNet::setIoThreads(int iothreads)
{
    Q_D(QMNet);
    init();

    d->mutex.lock();
    if(d->s_open_sockets > 0)
        qFatal("setIoThreads is not valid after creating sockets");
    zmq_term(d->context);

    d->s_io_threads = iothreads;
    d->context = zmq_init(d->s_io_threads);
    zmq_ctx_set (d->context, ZMQ_MAX_SOCKETS, d->s_max_sockets);
    d->mutex.unlock();
}
