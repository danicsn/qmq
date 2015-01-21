#ifndef CONTEXT_H
#define CONTEXT_H

#include <QObject>

#ifndef QMQ_EXPORT
#define QMQ_EXPORT
#endif

class SocketBase;
class Socket;

class ContextPrivate;
class QMQ_EXPORT Context : public QObject
{
    friend class SocketBase;
    friend class Socket;
public:
    explicit Context(QObject *parent = 0);
    ~Context();

    //  --------------------------------------------------------------------------
    //  Configure number of I/O threads in context, only has effect if called
    //  before creating first socket, or calling zctx_shadow. Default I/O
    //  threads is 1, sufficient for all except very high volume applications.
    virtual void setIoThreads(int iothreads);


    //  --------------------------------------------------------------------------
    //  Configure linger timeout in msecs. Call this before destroying sockets or
    //  context. Default is no linger, i.e. any pending messages or connects will
    //  be lost.
    void setLinger(int linger);


    //  --------------------------------------------------------------------------
    //  Set initial high-water mark for inter-thread pipe sockets. Note that
    //  this setting is separate from the default for normal sockets. You
    //  should change the default for pipe sockets *with care*. Too low values
    //  will cause blocked threads, and an infinite setting can cause memory
    //  exhaustion. The default, no matter the underlying ZeroMQ version, is
    //  1,000.
    void setPipehwm(int pipehwm);


    //  --------------------------------------------------------------------------
    //  Set initial send HWM for all new normal sockets created in context.
    //  You can set this per-socket after the socket is created.
    //  The default, no matter the underlying ZeroMQ version, is 1,000.
    void setSndhwm(int sndhwm);


    //  --------------------------------------------------------------------------
    //  Set initial receive HWM for all new normal sockets created in context.
    //  You can set this per-socket after the socket is created.
    //  The default, no matter the underlying ZeroMQ version, is 1,000.
    void setRcvhwm(int rcvhwm);

    Socket *createSocket(int type);
    Socket *createPipe();
    void* desctiptor();

    virtual void append(SocketBase* sock, int type);
    virtual void appendPipe(SocketBase* frontend, SocketBase* backend);

    virtual int closeSocket(SocketBase *zocket);

    static void test();

protected:
    ContextPrivate* const d_ptr;
    Context(ContextPrivate& d, QObject *parent = 0);

private:
    Q_DECLARE_PRIVATE(Context)    

    void destroy_sockets();
};

class QMNetPrivate;
class QMQ_EXPORT QMNet : public Context
{
public:
    QMNet(QObject *parent=0);
    ~QMNet();

    static QMNet* instance();

    void init();
    void shutdown();

    Socket* createSocket(int type);
    int closeSocket(SocketBase* sock);
    std::pair<Socket*, Socket*> createPipe();

    void append(SocketBase* sock, int type);
    void appendPipe(SocketBase* frontend, SocketBase* backend);

    void handleError(const char* reason);

    QString hostName();

    //
    void setIoThreads(int iothreads);

private:
    Q_DECLARE_PRIVATE(QMNet)
    static QMNet* m_instance;
};

#define srnet QMNet::instance()

#endif // CONTEXT_H
