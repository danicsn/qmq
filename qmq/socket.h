#ifndef SOCKET_H
#define SOCKET_H

#include <QObject>

//- Data types --------------------------------------------------------------

typedef unsigned char   byte;           //  Single unsigned byte = 8 bits

//  @interface
//  This port range is defined by IANA for dynamic or private ports
//  We use this when choosing a port for dynamic binding.
#define ZSOCKET_DYNFROM     0xc000
#define ZSOCKET_DYNTO       0xffff

//  Callback function for zero-copy methods
typedef void (zsocket_free_fn) (void *data, void *arg);

#ifndef QMQ_EXPORT
#define QMQ_EXPORT
#endif


class Context;

class SocketBasePrivate;

/// dont create object from this class directly
/// this class has all needed function for creating a socket
/// but dont use it
class QMQ_EXPORT SocketBase : public QObject
{
    friend class Context;
    friend class ContextPrivate;
    friend class QMNet;

public:
    SocketBase(Context *parent = 0);
    SocketBase(Context *cntx, int type);    
    virtual ~SocketBase();

    // main functions
    //  --------------------------------------------------------------------------
    //  Bind a socket to a formatted endpoint. If the port is specified as
    //  '*', binds to any free port from ZSOCKET_DYNFROM to ZSOCKET_DYNTO
    //  and returns the actual port number used.  Always returns the
    //  port number if successful. Note that if a previous process or thread
    //  used the same port, peers may connect to the caller thinking it was
    //  the previous process/thread.
    virtual int bind(const char *format, ...);

    //  --------------------------------------------------------------------------
    //  Unbind a socket from a formatted endpoint.
    //  Returns 0 if OK, -1 if the endpoint was invalid or the function
    //  isn't supported.
    virtual int unbind(const char *format, ...);


    //  --------------------------------------------------------------------------
    //  Connect a socket to a formatted endpoint
    //  Returns 0 if the endpoint is valid, -1 if the connect failed.

    virtual int connect(const char *format, ...);

    virtual int disconnect(const char *format, ...);

    virtual bool poll (int msecs);

    const char * type_str();
    int sendmem(const void *data, size_t size, int flags);
    int sendstr(const QString& string, bool more = false);
    int sendx(const char *string, ...);

    virtual QString recvstr();

    virtual int signal();

    //  --------------------------------------------------------------------------
    //  Wait on a signal. Use this to coordinate between threads, over
    //  pipe pairs. Blocks until the signal is received. Returns -1 on error,
    //  0 on success.
    virtual int wait();

    // Accessories
    virtual int receiveMore();
    int fd();

    int events();
    QString lastEndpoint();

    // socket Options
    // set/read all possible parameters

    void setTOS(int tos);
    int tos();

    void setRouterHandover (int router_handover);
    void setRouterMandatory (int router_mandatory);
    void setProbeRouter (int probe_router);
    void setReqRelaxed (int req_relaxed);
    void setReqCorrelate (int req_correlate);
    void setReqConflate (int conflate);
    void setZapDomain (const QString& zap_domain);

    QString zapDomain();
    int mechanism();

    void setPlainServer(int plain_server);
    int plainServer();

    void setPlainUsername (const QString& plain_username);
    QString plainUsername();
    void setPlainPassword(const QString& plain_password);
    QString plainPassword();

    void setCurveServer(int curve_server);
    int curveServer();

    void setCurvePublickey(const QString& curve_publickey);
    void setCurvePublickey_bin(const byte *curve_publickey);
    QString curvePublickey();

    void setCurveSecretkey(const QString& curve_secretkey);
    void setCurveSecretkey_bin(const byte *curve_secretkey);

    QString curveSecretkey();
    void setCurveServerkey(const QString& curve_serverkey);
    void setCurveServerkey_bin(const byte *curve_serverkey);
    QString curveServerkey();

    void setGssapiServer(int gssapi_server);
    int gssapiServer();

    void setGssapiPlaintext(int gssapi_plaintext);
    int gssapiPlaintext();

    void setGssapiPrincipal(const QString& gssapi_principal);
    QString gssapiPrincipal();

    void setGssapiServicePrincipal(const QString& gssapi_service_principal);
    QString gssapiServicePrincipal();

    void setIpv6(int ipv6);
    int ipv6();

    void setImmediate(int immediate);
    int immediate();

    void setRouterRaw(int router_raw);
    void setIpv4only(int ipv4only);
    int ipv4only();

    void setDelayAttach_on_connect(int delay_attach_on_connect);
    int type();

    void setSndhwm(int sndhwm);
    int sndhwm();

    void setRcvhwm(int rcvhwm);
    int rcvhwm();

    void setAffinity(int affinity);
    int affinity();

    void setSubscribe(const QString& subscribe);
    void setUnsubscribe(const QString& unsubscribe);

    void setIdentity(const QString& identity);
    QString identity();

    void setRate(int rate);
    int rate();

    void setRecovery_ivl(int recovery_ivl);
    int recoveryIvl();

    void setSndbuf(int sndbuf);
    int sndbuf();

    void setRcvbuf (int rcvbuf);
    int rcvbuf();

    void setLinger(int linger);
    int linger();

    void setReconnect_ivl(int reconnect_ivl);
    int reconnect_ivl();

    void setReconnect_ivl_max(int reconnect_ivl_max);
    int reconnect_ivl_max();

    void setBacklog(int backlog);
    int backlog();

    void setMaxmsgsize(int maxmsgsize);
    int maxmsgsize();

    void setMulticastHops(int multicast_hops);
    int multicastHops();

    void setRcvtimeo(int rcvtimeo);
    int rcvtimeo();

    void setSndtimeo(int sndtimeo);
    int sndtimeo();

    void setXPubVerbose(int xpub_verbose);
    void setTcpKeepalive(int tcp_keepalive);
    int tcpKeepalive();

    void setTcpKeepAlive_idle(int tcp_keepalive_idle);
    int tcpKeepalive_idle();

    void setTcpKeepalive_cnt(int tcp_keepalive_cnt);
    int tcpKeepalive_cnt();

    void setTcpKeepalive_intvl(int tcp_keepalive_intvl);
    int tcpKeepalive_intvl();

    void setTcpAccept_filter(const char * tcp_accept_filter);
    QString tcpAccept_filter();

    void setHwm(int hwm);
    int socket_type();

    virtual void *resolve();
    Context* context();

protected:
    SocketBasePrivate* const d_ptr;
    SocketBase(SocketBasePrivate &d, Context *parent=0);

    // only friend class can use these fuctions    
    virtual void reset(Context* cntx, int type);    
    virtual int close();
private:
    Q_DISABLE_COPY(SocketBase)
    Q_DECLARE_PRIVATE(SocketBase)
};

class SocketPrivate;
class QMQ_EXPORT Socket : public SocketBase
{
    friend class Context;
    friend class ContextPrivate;
    friend class QMNet;

public:
    Socket(Context *parent=0);
    Socket(Context *cntx, int type);
    virtual ~Socket();

    /* static functions */
    static Socket *createPub(const char *endpoints=NULL);
    static Socket *createSub(const char *endpoints=NULL, const char *subscribe=NULL);
    static Socket *createReq(const char *endpoints=NULL);
    static Socket *createRep(const char *endpoints=NULL);
    static Socket *createDealer(const char *endpoints=NULL);
    static Socket *createRouter(const char *endpoints=NULL);
    static Socket *createPush(const char *endpoints=NULL);
    static Socket *createPull(const char *endpoints=NULL);
    static Socket *createXPub(const char *endpoints=NULL);
    static Socket *createXSub(const char *endpoints=NULL);
    static Socket *createPair(const char *endpoints=NULL);
    static Socket *createStream(const char *endpoints=NULL);

    //  --------------------------------------------------------------------------
    //  Bind a socket to a formatted endpoint. For tcp:// endpoints, supports
    //  ephemeral ports, if you specify the port number as "*". By default
    //  zsock uses the IANA designated range from C000 (49152) to FFFF (65535).
    //  To override this range, follow the "*" with "[first-last]". Either or
    //  both first and last may be empty. To bind to a random port within the
    //  range, use "!" in place of "*".
    //
    //  Examples:
    //  tcp://127.0.0.1:*                bind to first free port from C000 up
    //  tcp://127.0.0.1:!                bind to random port from C000 to FFFF
    //  tcp://127.0.0.1:*[60000-]        bind to first free port from 60000 up
    //  tcp://127.0.0.1:![-60000]        bind to random port from C000 to 60000
    //  tcp://127.0.0.1:![55000-55999]   bind to random port from 55000-55999
    //
    //  On success, returns the actual port number used, for tcp:// endpoints,
    //  and 0 for other transports. On failure, returns -1. Note that when using
    //  ephemeral ports, a port may be reused by different services without
    //  clients being aware. Protocols that run on ephemeral ports should take
    //  this into account.

    int bind(const QString& add);
    int bind(const char *format, ...);
    QString endpoint();

    int unbind(const char *format, ...);

    int connect(const char *format, ...);

    int disconnect(const char *format, ...);


    //  --------------------------------------------------------------------------
    //  Attach a socket to zero or more endpoints. If endpoints is not null,
    //  parses as list of ZeroMQ endpoints, separated by commas, and prefixed by
    //  '@' (to bind the socket) or '>' (to attach the socket). Returns 0 if all
    //  endpoints were valid, or -1 if there was a syntax error. If the endpoint
    //  does not start with '@' or '>', the serverish argument defines whether
    //  it is used to bind (serverish = true) or connect (serverish = false).
    int attach(const char *endpoints, bool serverish);


    //  --------------------------------------------------------------------------
    //  Send a 'picture' message to the socket (or actor). The picture is a
    //  string that defines the type of each frame. This makes it easy to send
    //  a complex multiframe message in one call. The picture can contain any
    //  of these characters, each corresponding to one or two arguments:
    //
    //      i = int
    //      u = uint
    //      s = char *
    //      b = byte *, size_t (2 arguments)
    //      c = QByteArray *
    //      f = Frame *
    //      p = void * (sends the pointer value, only meaningful over inproc)
    //      m = Messages * (sends all frames in the zmsg)
    //      z = sends zero-sized frame (0 arguments)
    //
    //  Note that s, b, c, and f are encoded the same way and the choice is
    //  offered as a convenience to the sender, which may or may not already
    //  have data in a zchunk or zframe. Does not change or take ownership of
    //  any arguments. Returns 0 if successful, -1 if sending failed for any
    //  reason.
    virtual int send(const char *picture, ...);


    //  --------------------------------------------------------------------------
    //  Receive a 'picture' message to the socket (or actor). See zsock_send for
    //  the format and meaning of the picture. Returns the picture elements into
    //  a series of pointers as provided by the caller:
    //
    //      i = int * (stores integer)
    //      u = uint * (stores unsigned integer)
    //      s = char ** (allocates new string)
    //      b = byte **, size_t * (2 arguments) (allocates memory)
    //      c = zchunk_t ** (creates zchunk)
    //      f = zframe_t ** (creates zframe)
    //      p = void ** (stores pointer)
    //      m = zmsg_t ** (creates a zmsg with the remaing frames)
    //      z = null, asserts empty frame (0 arguments)
    //
    //  Note that zsock_recv creates the returned objects, and the caller must
    //  destroy them when finished with them. The supplied pointers do not need
    //  to be initialized. Returns 0 if successful, or -1 if it failed to recv
    //  a message, in which case the pointers are not modified. When message
    //  frames are truncated (a short message), sets return values to zero/null.
    //  If an argument pointer is NULL, does not store any value (skips it).
    //  An 'n' picture matches an empty frame; if the message does not match,
    //  the method will return -1.

    virtual int recv(const char *picture, ...);

    //  --------------------------------------------------------------------------
    //  Send a binary encoded 'picture' message to the socket (or actor). This
    //  method is similar to send, except the arguments are encoded in a
    //  binary format that is compatible with zproto, and is designed to reduce
    //  memory allocations. The pattern argument is a string that defines the
    //  type of each argument. Supports these argument types:
    //
    //   pattern    C type                  zproto type:
    //      1       uint8_t                 type = "number" size = "1"
    //      2       uint16_t                type = "number" size = "2"
    //      4       uint32_t                type = "number" size = "3"
    //      8       uint64_t                type = "number" size = "4"
    //      s       char *, 0-255 chars     type = "string"
    //      S       char *, 0-2^32-1 chars  type = "longstr"
    //      c       QByteArray *            type = "bytearr"
    //      f       Frame *                 type = "frame"
    //      m       Messages *              type = "msg"
    //      p       void *, sends pointer value, only over inproc
    //
    //  Does not change or take ownership of any arguments. Returns 0 if
    //  successful, -1 if sending failed for any reason.
    virtual int bsend(const char *picture, ...);


    //  --------------------------------------------------------------------------
    //  Receive a binary encoded 'picture' message from the socket (or actor).
    //  This method is similar to recv, except the arguments are encoded
    //  in a binary format that is compatible with zproto, and is designed to
    //  reduce memory allocations. The pattern argument is a string that defines
    //  the type of each argument. See zsock_bsend for the supported argument
    //  types. All arguments must be pointers; this call sets them to point to
    //  values held on a per-socket basis. Do not modify or destroy the returned
    //  values. Returns 0 if successful, or -1 if it failed to read a message.
    virtual int brecv(const char *picture, ...);


    //  --------------------------------------------------------------------------
    //  Set socket to use unbounded pipes (HWM=0); use this in cases when you are
    //  totally certain the message volume can fit in memory. This method works
    //  across all versions of ZeroMQ. Takes a polymorphic socket reference.
    virtual void set_unbounded();

    //  --------------------------------------------------------------------------
    //  Send a signal over a socket. A signal is a short message carrying a
    //  success/failure code (by convention, 0 means OK). Signals are encoded
    //  to be distinguishable from "normal" messages. Accepts a zock_t or a
    //  zactor_t argument, and returns 0 if successful, -1 if the signal could
    //  not be sent. Takes a polymorphic socket reference.
    virtual int signal(byte status);


    //  --------------------------------------------------------------------------
    //  Wait on a signal. Use this to coordinate between threads, over pipe
    //  pairs. Blocks until the signal is received. Returns -1 on error, 0 or
    //  greater on success. Accepts a zsock_t or a zactor_t as argument.
    //  Takes a polymorphic socket reference.
    int wait();


    //  --------------------------------------------------------------------------
    //  If there is a partial message still waiting on the socket, remove and
    //  discard it. This is useful when reading partial messages, to get specific
    //  message types.
    void flush();    

    static void test(bool verbose);

protected:
    Socket(SocketBasePrivate &d, Context* parent);

    virtual int close();

private:
    Q_DISABLE_COPY(Socket)
    Q_DECLARE_PRIVATE(Socket)
};

#endif // SOCKET_H
