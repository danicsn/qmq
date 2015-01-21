#include "socket_p.hpp"
#include "context_p.hpp"
#include <qthread.h>
#include <QDebug>

#define zocket d->handle
//  This port range is defined by IANA for dynamic or private ports
//  We use this when choosing a port for dynamic binding.
#define DYNAMIC_FIRST       0xc000    // 49152
#define DYNAMIC_LAST        0xffff    // 65535

#define ZSOCK_BSEND_MAX_FRAMES 32  // Arbitrary limit, for now


SocketBase::SocketBase(Context *parent) : d_ptr(new SocketBasePrivate),
    QObject(parent)
{
    Q_D(SocketBase);
    d->m_pcntxt = parent;
}

SocketBase::SocketBase(Context *cntx, int type) : d_ptr(new SocketBasePrivate), QObject(cntx)
{
    if(!cntx)
        return;

    Q_D(SocketBase);
    d->m_pcntxt = cntx;
    cntx->append(this, type);
}

SocketBase::~SocketBase()
{
    Q_D(SocketBase);
    if(d->m_pcntxt)
        d->m_pcntxt->closeSocket(this);
    delete d_ptr;
}

SocketBase::SocketBase(SocketBasePrivate &dp, Context *parent):
    d_ptr(&dp), QObject(parent)
{
    Q_D(SocketBase);
    d->m_pcntxt = parent;
}

void SocketBase::reset(Context *cntx, int type)
{
    Q_D(SocketBase);
    if(d->m_pcntxt && d->m_pcntxt != cntx)
    {
        d->m_pcntxt->closeSocket(this);
    }

    // check if you can set parent
    // its so common for context in different thread than socket
    if(cntx->thread() == this->thread())
        setParent(cntx);

    d->m_pcntxt = cntx;
    d->type = type;
    d->handle = zmq_socket(cntx->desctiptor(), type);
}

int SocketBase::bind(const char *format, ...)
{
    Q_D(SocketBase);
    //  Ephemeral port needs 4 additional characters
    char endpoint [256 + 4];
    va_list argptr;
    va_start (argptr, format);
    int endpoint_size = vsnprintf (endpoint, 256, format, argptr);
    va_end (argptr);

    int rc = -1;
    //  Port must be at end of endpoint
    if (  endpoint [endpoint_size - 2] == ':'
          && endpoint [endpoint_size - 1] == '*') {
        int port = ZSOCKET_DYNFROM;
        while (port <= ZSOCKET_DYNTO) {
            //  Try to bind on the next plausible port
            sprintf (endpoint + endpoint_size - 1, "%d", port);
            zmq_bind(zocket, endpoint);
            port++;
        }
        return -1;
    }
    else {
        //  Return actual port used for binding
        rc = zmq_bind(zocket, endpoint);
        if(rc == -1) return -1;

        int port = atoi(strrchr(endpoint, ':') + 1);

        return port;
    }

    return -1;
}

int SocketBase::unbind(const char *format, ...)
{
    Q_D(SocketBase);
#if (ZMQ_VERSION >= ZMQ_MAKE_VERSION (3, 2, 0))    
    char endpoint [256];
    va_list argptr;
    va_start (argptr, format);
    vsnprintf (endpoint, 256, format, argptr);
    va_end (argptr);
    int rc = zmq_unbind(zocket, endpoint);
    return rc;
#else
    return -1;
#endif
}

int SocketBase::connect(const char *format, ...)
{
    Q_D(SocketBase);
    char endpoint [256];
    va_list argptr;
    va_start (argptr, format);
    vsnprintf (endpoint, 256, format, argptr);
    va_end (argptr);
    int rc = zmq_connect(zocket, endpoint);
    return rc;
}

int SocketBase::disconnect(const char *format, ...)
{
    Q_D(SocketBase);
#if (ZMQ_VERSION >= ZMQ_MAKE_VERSION (3, 2, 0))
    char endpoint [256];
    va_list argptr;
    va_start (argptr, format);
    vsnprintf (endpoint, 256, format, argptr);
    va_end (argptr);
    int rc = zmq_disconnect(zocket, endpoint);
    return rc;
#else
    return -1;
#endif
}

bool SocketBase::poll(int msecs)
{
    Q_D(SocketBase);
    zmq_pollitem_t items [] = { { zocket, 0, ZMQ_POLLIN, 0 } };
    int rc = zmq_poll(items, 1, msecs);
    return rc != -1 && (items [0].revents & ZMQ_POLLIN) != 0;
}

const char *SocketBase::type_str()
{
    Q_D(SocketBase);
    int socktype = d->type;
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

int SocketBase::sendmem(const void *data, size_t size, int flags)
{
    Q_D(SocketBase);
    assert (size == 0 || data);

    int snd_flags = (flags & QFRAME_MORE) ? ZMQ_SNDMORE : 0;
    snd_flags |= (flags & QFRAME_DONTWAIT) ? ZMQ_DONTWAIT : 0;

    zmq_msg_t msg;
    zmq_msg_init_size(&msg, size);
    memcpy(zmq_msg_data(&msg), data, size);

    if (zmq_msg_send(&msg, zocket, flags) == -1) {
        return -1;
    }
    else
        return 0;
}

int SocketBase::sendstr(const QString &string, bool more)
{
    Q_D(SocketBase);

    int len = string.length();
    zmq_msg_t message;
    zmq_msg_init_size (&message, len);
    memcpy (zmq_msg_data (&message), string.toLocal8Bit().data(), len);
    if (zmq_sendmsg (d->handle, &message, more ? ZMQ_SNDMORE : 0) == -1) {
        zmq_msg_close (&message);
        return -1;
    }
    else
        return 0;
}

int SocketBase::sendx(const char *string, ...)
{
    Messages msg;

    va_list args;
    va_start (args, string);
    while (string) {
        msg.append(QString(string));
        string = va_arg (args, char *);
    }
    va_end (args);
    return msg.send(*this);
}

QString SocketBase::recvstr()
{
    Q_D(SocketBase);

    zmq_msg_t message;
    zmq_msg_init (&message);
    if (zmq_recvmsg (d->handle, &message, 0) < 0)
        return QString();

    size_t size = zmq_msg_size (&message);
    char *string = (char *) malloc (size + 1);
    if (string) {
        memcpy (string, zmq_msg_data (&message), size);
        string [size] = 0;
    }
    zmq_msg_close(&message);
    QString res(string);
    free(string);

    return res;
}

int SocketBase::signal()
{
    return sendstr("");
}

void *SocketBase::resolve()
{
    Q_D(SocketBase);
    return d->handle;
}

Context *SocketBase::context()
{
    Q_D(SocketBase);
    return d->m_pcntxt;
}

int SocketBase::wait()
{
    QString message = recvstr();
    if(!message.isNull()) {
        return 0;
    }
    else
        return -1;
}

int SocketBase::close()
{
    // just context must close socket
    Q_D(SocketBase);
    int rc = zmq_close(d->handle);
    if(rc == 0)
        d->m_pcntxt = 0;
    return rc;
}

void SocketBase::setTOS(int tos)
{
    Q_D(SocketBase);
#   if defined (ZMQ_TOS)
    zmq_setsockopt(zocket, ZMQ_TOS, &tos, sizeof (int));
#   endif
}

int SocketBase::tos()
{
    Q_D(SocketBase);
#   if defined (ZMQ_TOS)
    int tos;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket, ZMQ_TOS, &tos, &option_len);
    return tos;
#   else
    return 0;
#   endif
}

void SocketBase::setRouterHandover(int router_handover)
{
    Q_D(SocketBase);
#   if defined (ZMQ_ROUTER_HANDOVER)
    if (socket_type() != ZMQ_ROUTER) {
        qFatal("ZMQ_ROUTER_HANDOVER is not valid on %s sockets\n", type_str());
    }
    zmq_setsockopt(zocket, ZMQ_ROUTER_HANDOVER, &router_handover, sizeof (int));
#   endif
}

void SocketBase::setRouterMandatory(int router_mandatory)
{
    Q_D(SocketBase);
#   if defined (ZMQ_ROUTER_MANDATORY)
    if (socket_type() != ZMQ_ROUTER) {
        qFatal("ZMQ_ROUTER_MANDATORY is not valid on %s sockets\n", type_str());
    }
    zmq_setsockopt(zocket, ZMQ_ROUTER_MANDATORY, &router_mandatory, sizeof (int));

#   endif
}

void SocketBase::setProbeRouter(int probe_router)
{
    Q_D(SocketBase);
#   if defined (ZMQ_PROBE_ROUTER)
    if (socket_type() != ZMQ_ROUTER
            &&  socket_type() != ZMQ_DEALER
            &&  socket_type() != ZMQ_REQ) {
        qFatal("ZMQ_PROBE_ROUTER is not valid on %s sockets\n", type_str());
    }
    zmq_setsockopt(zocket, ZMQ_PROBE_ROUTER, &probe_router, sizeof (int));
#   endif
}

void SocketBase::setReqRelaxed(int req_relaxed)
{
    Q_D(SocketBase);
#   if defined (ZMQ_REQ_RELAXED)
    if (socket_type() != ZMQ_REQ) {
        qFatal("ZMQ_REQ_RELAXED is not valid on %s sockets\n", type_str());
    }
    zmq_setsockopt(zocket, ZMQ_REQ_RELAXED, &req_relaxed, sizeof (int));
#   endif
}

void SocketBase::setReqCorrelate(int req_correlate)
{
    Q_D(SocketBase);
#   if defined (ZMQ_REQ_CORRELATE)
    if (socket_type() != ZMQ_REQ) {
        qFatal("ZMQ_REQ_CORRELATE is not valid on %s sockets\n", type_str());
    }
    zmq_setsockopt(zocket, ZMQ_REQ_CORRELATE, &req_correlate, sizeof (int));
#   endif
}

void SocketBase::setReqConflate(int conflate)
{
    Q_D(SocketBase);

#   if defined (ZMQ_CONFLATE)
    if (socket_type() != ZMQ_PUSH
            &&  socket_type() != ZMQ_PULL
            &&  socket_type() != ZMQ_PUB
            &&  socket_type() != ZMQ_SUB
            &&  socket_type() != ZMQ_DEALER) {
        qFatal("ZMQ_CONFLATE is not valid on %s sockets\n", type_str());
    }
    zmq_setsockopt(zocket, ZMQ_CONFLATE, &conflate, sizeof (int));
#   endif
}

void SocketBase::setZapDomain(const QString &zap_domain)
{
    Q_D(SocketBase);
#   if defined (ZMQ_ZAP_DOMAIN)
    zmq_setsockopt(zocket, ZMQ_ZAP_DOMAIN, zap_domain.toLatin1().constData(), zap_domain.size());
#   endif
}

QString SocketBase::zapDomain()
{
    Q_D(SocketBase);
#   if defined (ZMQ_ZAP_DOMAIN)
    size_t option_len = 255;
    char *zap_domain = (char *) zmalloc (option_len);
    zmq_getsockopt(zocket,ZMQ_ZAP_DOMAIN, zap_domain, &option_len);
    return QString(zap_domain);
#   else
    return QString();
#   endif
}

int SocketBase::mechanism()
{
    Q_D(SocketBase);
#   if defined (ZMQ_MECHANISM)
    int mechanism;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_MECHANISM, &mechanism, &option_len);
    return mechanism;
#   else
    return 0;
#   endif
}

void SocketBase::setPlainServer(int plain_server)
{
    Q_D(SocketBase);
#   if defined (ZMQ_PLAIN_SERVER)
    zmq_setsockopt(zocket, ZMQ_PLAIN_SERVER, &plain_server, sizeof (int));
#   endif
}

int SocketBase::plainServer()
{
    Q_D(SocketBase);
#   if defined (ZMQ_PLAIN_SERVER)
    int plain_server;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_PLAIN_SERVER, &plain_server, &option_len);
    return plain_server;
#   else
    return 0;
#   endif
}

void SocketBase::setPlainUsername(const QString &plain_username)
{
    Q_D(SocketBase);
#   if defined (ZMQ_PLAIN_USERNAME)
    zmq_setsockopt(zocket, ZMQ_PLAIN_USERNAME, plain_username.toLatin1().constData()
                       , plain_username.size());
#   endif
}

QString SocketBase::plainUsername()
{
    Q_D(SocketBase);
#   if defined (ZMQ_PLAIN_USERNAME)
    size_t option_len = 255;
    char *plain_username = (char *) zmalloc (option_len);
    zmq_getsockopt(zocket,ZMQ_PLAIN_USERNAME, plain_username, &option_len);
    return QString(plain_username);
#   else
    return QString();
#   endif
}

void SocketBase::setPlainPassword(const QString &plain_password)
{
    Q_D(SocketBase);
#   if defined (ZMQ_PLAIN_PASSWORD)
    zmq_setsockopt(zocket, ZMQ_PLAIN_PASSWORD, plain_password.toLatin1().constData()
                       , plain_password.size());
#   endif
}

QString SocketBase::plainPassword()
{
    Q_D(SocketBase);
#   if defined (ZMQ_PLAIN_PASSWORD)
    size_t option_len = 255;
    char *plain_password = (char *) zmalloc (option_len);
    zmq_getsockopt(zocket,ZMQ_PLAIN_PASSWORD, plain_password, &option_len);
    return QString(plain_password);
#   else
    return QString();
#   endif
}

void SocketBase::setCurveServer(int curve_server)
{
    Q_D(SocketBase);
#   if defined (ZMQ_CURVE_SERVER)
    zmq_setsockopt(zocket, ZMQ_CURVE_SERVER, &curve_server, sizeof (int));
#   endif
}

int SocketBase::curveServer()
{
    Q_D(SocketBase);
#   if defined (ZMQ_CURVE_SERVER)
    int curve_server;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_CURVE_SERVER, &curve_server, &option_len);
    return curve_server;
#   else
    return 0;
#   endif
}

void SocketBase::setCurvePublickey(const QString &curve_publickey)
{
    Q_D(SocketBase);
#   if defined (ZMQ_CURVE_PUBLICKEY)
    zmq_setsockopt(zocket, ZMQ_CURVE_PUBLICKEY, curve_publickey.toLatin1().constData(),
                       curve_publickey.size());
#   endif
}

void SocketBase::setCurvePublickey_bin(const byte *curve_publickey)
{
    Q_D(SocketBase);
#   if defined (ZMQ_CURVE_PUBLICKEY)
    zmq_setsockopt(zocket, ZMQ_CURVE_PUBLICKEY, curve_publickey, 32);
#   endif
}

QString SocketBase::curvePublickey()
{
    Q_D(SocketBase);
#   if defined (ZMQ_CURVE_PUBLICKEY)
    size_t option_len = 40 + 1;     //  Z85 key + terminator
    char *curve_publickey = (char *) zmalloc (option_len);
    zmq_getsockopt(zocket,ZMQ_CURVE_PUBLICKEY, curve_publickey, &option_len);
    return QString(curve_publickey);
#   else
    return QString();
#   endif
}

void SocketBase::setCurveSecretkey(const QString &curve_secretkey)
{
    Q_D(SocketBase);
#   if defined (ZMQ_CURVE_SECRETKEY)
    zmq_setsockopt(zocket, ZMQ_CURVE_SECRETKEY,
                       curve_secretkey.toLatin1().constData(),
                       curve_secretkey.size());
#   endif
}

void SocketBase::setCurveSecretkey_bin(const byte *curve_secretkey)
{
    Q_D(SocketBase);
#   if defined (ZMQ_CURVE_SECRETKEY)
    zmq_setsockopt(zocket, ZMQ_CURVE_SECRETKEY, curve_secretkey, 32);
#   endif
}

QString SocketBase::curveSecretkey()
{
    Q_D(SocketBase);
#   if defined (ZMQ_CURVE_SECRETKEY)
    size_t option_len = 40 + 1;     //  Z85 key + terminator
    char *curve_secretkey = (char *) zmalloc (option_len);
    zmq_getsockopt(zocket,ZMQ_CURVE_SECRETKEY, curve_secretkey, &option_len);
    return QString(curve_secretkey);
#   else
    return QString();
#   endif
}

void SocketBase::setCurveServerkey(const QString &curve_serverkey)
{
    Q_D(SocketBase);
#   if defined (ZMQ_CURVE_SERVERKEY)
    zmq_setsockopt(zocket, ZMQ_CURVE_SERVERKEY, curve_serverkey.toLatin1().constData(),
                       curve_serverkey.size());
#   endif
}

void SocketBase::setCurveServerkey_bin(const byte *curve_serverkey)
{
    Q_D(SocketBase);
#   if defined (ZMQ_CURVE_SERVERKEY)
    zmq_setsockopt(zocket, ZMQ_CURVE_SERVERKEY, curve_serverkey, 32);
#   endif
}

QString SocketBase::curveServerkey()
{
    Q_D(SocketBase);
#   if defined (ZMQ_CURVE_SERVERKEY)
    size_t option_len = 40 + 1;     //  Z85 key + terminator
    char *curve_serverkey = (char *) zmalloc (option_len);
    zmq_getsockopt(zocket,ZMQ_CURVE_SERVERKEY, curve_serverkey, &option_len);
    return QString(curve_serverkey);
#   else
    return QString();
#   endif
}

void SocketBase::setGssapiServer(int gssapi_server)
{
    Q_D(SocketBase);
#   if defined (ZMQ_GSSAPI_SERVER)
    zmq_setsockopt(zocket, ZMQ_GSSAPI_SERVER, &gssapi_server, sizeof (int));
#   endif
}

int SocketBase::gssapiServer()
{
    Q_D(SocketBase);
#   if defined (ZMQ_GSSAPI_SERVER)
    int gssapi_server;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_GSSAPI_SERVER, &gssapi_server, &option_len);
    return gssapi_server;
#   else
    return 0;
#   endif
}

void SocketBase::setGssapiPlaintext(int gssapi_plaintext)
{
    Q_D(SocketBase);
#   if defined (ZMQ_GSSAPI_PLAINTEXT)
    zmq_setsockopt(zocket, ZMQ_GSSAPI_PLAINTEXT, &gssapi_plaintext, sizeof (int));
#   endif
}

int SocketBase::gssapiPlaintext()
{
    Q_D(SocketBase);
#   if defined (ZMQ_GSSAPI_PLAINTEXT)
    int gssapi_plaintext;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_GSSAPI_PLAINTEXT, &gssapi_plaintext, &option_len);
    return gssapi_plaintext;
#   else
    return 0;
#   endif
}

void SocketBase::setGssapiPrincipal(const QString &gssapi_principal)
{
    Q_D(SocketBase);
#   if defined (ZMQ_GSSAPI_PRINCIPAL)
    zmq_setsockopt(zocket, ZMQ_GSSAPI_PRINCIPAL, gssapi_principal.toLatin1().constData(),
                       gssapi_principal.size());
#   endif
}

QString SocketBase::gssapiPrincipal()
{
    Q_D(SocketBase);
#   if defined (ZMQ_GSSAPI_PRINCIPAL)
    size_t option_len = 255;
    char *gssapi_principal = (char *) zmalloc (option_len);
    zmq_getsockopt(zocket,ZMQ_GSSAPI_PRINCIPAL, gssapi_principal, &option_len);
    return QString(gssapi_principal);
#   else
    return QString();
#   endif
}

void SocketBase::setGssapiServicePrincipal(const QString &gssapi_service_principal)
{
    Q_D(SocketBase);
#   if defined (ZMQ_GSSAPI_SERVICE_PRINCIPAL)
    zmq_setsockopt(zocket, ZMQ_GSSAPI_SERVICE_PRINCIPAL, gssapi_service_principal.toLatin1().constData()
                       , gssapi_service_principal.size());
#   endif
}

QString SocketBase::gssapiServicePrincipal()
{
    Q_D(SocketBase);
#   if defined (ZMQ_GSSAPI_SERVICE_PRINCIPAL)
    size_t option_len = 255;
    char *gssapi_service_principal = (char *) zmalloc (option_len);
    zmq_getsockopt(zocket,ZMQ_GSSAPI_SERVICE_PRINCIPAL, gssapi_service_principal, &option_len);
    return QString(gssapi_service_principal);
#   else
    return QString();
#   endif
}

void SocketBase::setIpv6(int ipv6)
{
    Q_D(SocketBase);
#   if defined (ZMQ_IPV6)
    zmq_setsockopt(zocket, ZMQ_IPV6, &ipv6, sizeof (int));
#   endif
}

int SocketBase::ipv6()
{
    Q_D(SocketBase);
#   if defined (ZMQ_IPV6)
    int ipv6;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_IPV6, &ipv6, &option_len);
    return ipv6;
#   else
    return 0;
#   endif
}

void SocketBase::setImmediate(int immediate)
{
    Q_D(SocketBase);
#   if defined (ZMQ_IMMEDIATE)
    zmq_setsockopt(zocket, ZMQ_IMMEDIATE, &immediate, sizeof (int));
#   endif
}

int SocketBase::immediate()
{
    Q_D(SocketBase);
#   if defined (ZMQ_IMMEDIATE)
    int immediate;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_IMMEDIATE, &immediate, &option_len);
    return immediate;
#   else
    return 0;
#   endif
}

void SocketBase::setRouterRaw(int router_raw)
{
    Q_D(SocketBase);
#   if defined (ZMQ_ROUTER_RAW)
    if (socket_type() != ZMQ_ROUTER) {
        printf ("ZMQ_ROUTER_RAW is not valid on %s sockets\n", type_str());
        assert (false);
    }
    zmq_setsockopt(zocket, ZMQ_ROUTER_RAW, &router_raw, sizeof (int));
#   endif
}

void SocketBase::setIpv4only(int ipv4only)
{
    Q_D(SocketBase);
#   if defined (ZMQ_IPV4ONLY)
    zmq_setsockopt(zocket, ZMQ_IPV4ONLY, &ipv4only, sizeof (int));
#   endif
}

int SocketBase::ipv4only()
{
    Q_D(SocketBase);
#   if defined (ZMQ_IPV4ONLY)
    int ipv4only;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_IPV4ONLY, &ipv4only, &option_len);
    return ipv4only;
#   else
    return 0;
#   endif
}

void SocketBase::setDelayAttach_on_connect(int delay_attach_on_connect)
{
    Q_D(SocketBase);
#   if defined (ZMQ_DELAY_ATTACH_ON_CONNECT)
    zmq_setsockopt(zocket, ZMQ_DELAY_ATTACH_ON_CONNECT, &delay_attach_on_connect, sizeof (int));
#   endif
}

int SocketBase::type()
{
    Q_D(SocketBase);
#   if defined (ZMQ_TYPE)
    int type;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_TYPE, &type, &option_len);
    return type;
#   else
    return 0;
#   endif
}

void SocketBase::setSndhwm(int sndhwm)
{
    Q_D(SocketBase);
#   if defined (ZMQ_SNDHWM)
    zmq_setsockopt(zocket, ZMQ_SNDHWM, &sndhwm, sizeof (int));
#   endif
}

int SocketBase::sndhwm()
{
    Q_D(SocketBase);
#   if defined (ZMQ_SNDHWM)
    int sndhwm;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_SNDHWM, &sndhwm, &option_len);
    return sndhwm;
#   else
    return 0;
#   endif
}

void SocketBase::setRcvhwm(int rcvhwm)
{
    Q_D(SocketBase);
#   if defined (ZMQ_RCVHWM)
    zmq_setsockopt(zocket, ZMQ_RCVHWM, &rcvhwm, sizeof (int));
#   endif
}

int SocketBase::rcvhwm()
{
    Q_D(SocketBase);
#   if defined (ZMQ_RCVHWM)
    int rcvhwm;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_RCVHWM, &rcvhwm, &option_len);
    return rcvhwm;
#   else
    return 0;
#   endif
}

void SocketBase::setAffinity(int affinity)
{
    Q_D(SocketBase);
#   if defined (ZMQ_AFFINITY)
    uint64_t value = affinity;
    zmq_setsockopt(zocket, ZMQ_AFFINITY, &value, sizeof (uint64_t));
#   endif
}

int SocketBase::affinity()
{
    Q_D(SocketBase);
#   if defined (ZMQ_AFFINITY)
    uint64_t affinity;
    size_t option_len = sizeof (uint64_t);
    zmq_getsockopt(zocket,ZMQ_AFFINITY, &affinity, &option_len);
    return (int) affinity;
#   else
    return 0;
#   endif
}

void SocketBase::setSubscribe(const QString &subscribe)
{
    Q_D(SocketBase);
#   if defined (ZMQ_SUBSCRIBE)
    if (socket_type() != ZMQ_SUB) {
        qFatal("ZMQ_SUBSCRIBE is not valid on %s sockets\n", type_str());
    }
    zmq_setsockopt(zocket, ZMQ_SUBSCRIBE, subscribe.toLatin1().constData(), subscribe.toLatin1().size());
#   endif
}

void SocketBase::setUnsubscribe(const QString &unsubscribe)
{
    Q_D(SocketBase);
#   if defined (ZMQ_UNSUBSCRIBE)
    if (socket_type() != ZMQ_SUB) {
        qFatal("ZMQ_UNSUBSCRIBE is not valid on %s sockets\n", type_str());
    }
    zmq_setsockopt(zocket, ZMQ_UNSUBSCRIBE, unsubscribe.toLatin1().constData(), unsubscribe.toLatin1().size());
#   endif
}

void SocketBase::setIdentity(const QString &identity)
{
    Q_D(SocketBase);
#   if defined (ZMQ_IDENTITY)
    if (socket_type() != ZMQ_REQ
            &&  socket_type() != ZMQ_REP
            &&  socket_type() != ZMQ_DEALER
            &&  socket_type() != ZMQ_ROUTER) {
        qFatal("ZMQ_IDENTITY is not valid on %s sockets\n", type_str());
    }
    zmq_setsockopt(zocket, ZMQ_IDENTITY, identity.toLatin1().constData(), identity.toLatin1().size());
#   endif
}

QString SocketBase::identity()
{
    Q_D(SocketBase);
#   if defined (ZMQ_IDENTITY)
    size_t option_len = 255;
    char *identity = (char *) zmalloc (option_len);
    zmq_getsockopt(zocket,ZMQ_IDENTITY, identity, &option_len);
    return QString(identity);
#   else
    return NULL;
#   endif
}

void SocketBase::setRate(int rate)
{
    Q_D(SocketBase);
#   if defined (ZMQ_RATE)
    zmq_setsockopt(zocket, ZMQ_RATE, &rate, sizeof (int));
#   endif
}

int SocketBase::rate()
{
    Q_D(SocketBase);
#   if defined (ZMQ_RATE)
    int rate;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_RATE, &rate, &option_len);
    return rate;
#   else
    return 0;
#   endif
}

void SocketBase::setRecovery_ivl(int recovery_ivl)
{
    Q_D(SocketBase);
#   if defined (ZMQ_RECOVERY_IVL)
    zmq_setsockopt(zocket, ZMQ_RECOVERY_IVL, &recovery_ivl, sizeof (int));
#   endif
}

int SocketBase::recoveryIvl()
{
    Q_D(SocketBase);
#   if defined (ZMQ_RECOVERY_IVL)
    int recovery_ivl;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_RECOVERY_IVL, &recovery_ivl, &option_len);
    return recovery_ivl;
#   else
    return 0;
#   endif
}

void SocketBase::setSndbuf(int sndbuf)
{
    Q_D(SocketBase);
#   if defined (ZMQ_SNDBUF)
    zmq_setsockopt(zocket, ZMQ_SNDBUF, &sndbuf, sizeof (int));
#   endif
}

int SocketBase::sndbuf()
{
    Q_D(SocketBase);
#   if defined (ZMQ_SNDBUF)
    int sndbuf;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_SNDBUF, &sndbuf, &option_len);
    return sndbuf;
#   else
    return 0;
#   endif
}

void SocketBase::setRcvbuf(int rcvbuf)
{
    Q_D(SocketBase);
#   if defined (ZMQ_RCVBUF)
    zmq_setsockopt(zocket, ZMQ_RCVBUF, &rcvbuf, sizeof (int));
#   endif
}

int SocketBase::rcvbuf()
{
    Q_D(SocketBase);
#   if defined (ZMQ_RCVBUF)
    int rcvbuf;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_RCVBUF, &rcvbuf, &option_len);
    return rcvbuf;
#   else
    return 0;
#   endif
}

void SocketBase::setLinger(int linger)
{
    Q_D(SocketBase);
#   if defined (ZMQ_LINGER)
    zmq_setsockopt(zocket, ZMQ_LINGER, &linger, sizeof (int));
#   endif
}

int SocketBase::linger()
{
    Q_D(SocketBase);
#   if defined (ZMQ_LINGER)
    int linger;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_LINGER, &linger, &option_len);
    return linger;
#   else
    return 0;
#   endif
}

void SocketBase::setReconnect_ivl(int reconnect_ivl)
{
    Q_D(SocketBase);
#   if defined (ZMQ_RECONNECT_IVL)
    zmq_setsockopt(zocket, ZMQ_RECONNECT_IVL, &reconnect_ivl, sizeof (int));
#   endif
}

int SocketBase::reconnect_ivl()
{
    Q_D(SocketBase);
#   if defined (ZMQ_RECONNECT_IVL)
    int reconnect_ivl;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_RECONNECT_IVL, &reconnect_ivl, &option_len);
    return reconnect_ivl;
#   else
    return 0;
#   endif
}

void SocketBase::setReconnect_ivl_max(int reconnect_ivl_max)
{
    Q_D(SocketBase);
#   if defined (ZMQ_RECONNECT_IVL_MAX)
    zmq_setsockopt(zocket, ZMQ_RECONNECT_IVL_MAX, &reconnect_ivl_max, sizeof (int));
#   endif
}

int SocketBase::reconnect_ivl_max()
{
    Q_D(SocketBase);
#   if defined (ZMQ_RECONNECT_IVL_MAX)
    int reconnect_ivl_max;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_RECONNECT_IVL_MAX, &reconnect_ivl_max, &option_len);
    return reconnect_ivl_max;
#   else
    return 0;
#   endif
}

void SocketBase::setBacklog(int backlog)
{
    Q_D(SocketBase);
#   if defined (ZMQ_BACKLOG)
    zmq_setsockopt(zocket, ZMQ_BACKLOG, &backlog, sizeof (int));
#   endif
}

int SocketBase::backlog()
{
    Q_D(SocketBase);
#   if defined (ZMQ_BACKLOG)
    int backlog;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_BACKLOG, &backlog, &option_len);
    return backlog;
#   else
    return 0;
#   endif
}

void SocketBase::setMaxmsgsize(int maxmsgsize)
{
    Q_D(SocketBase);
#   if defined (ZMQ_MAXMSGSIZE)
    int64_t value = maxmsgsize;
    zmq_setsockopt(zocket, ZMQ_MAXMSGSIZE, &value, sizeof (int64_t));
#   endif
}

int SocketBase::maxmsgsize()
{
    Q_D(SocketBase);
#   if defined (ZMQ_MAXMSGSIZE)
    int64_t maxmsgsize;
    size_t option_len = sizeof (int64_t);
    zmq_getsockopt(zocket,ZMQ_MAXMSGSIZE, &maxmsgsize, &option_len);
    return (int) maxmsgsize;
#   else
    return 0;
#   endif
}

void SocketBase::setMulticastHops(int multicast_hops)
{
    Q_D(SocketBase);
#   if defined (ZMQ_MULTICAST_HOPS)
    zmq_setsockopt(zocket, ZMQ_MULTICAST_HOPS, &multicast_hops, sizeof (int));
#   endif
}

int SocketBase::multicastHops()
{
    Q_D(SocketBase);
#   if defined (ZMQ_MULTICAST_HOPS)
    int multicast_hops;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_MULTICAST_HOPS, &multicast_hops, &option_len);
    return multicast_hops;
#   else
    return 0;
#   endif
}

void SocketBase::setRcvtimeo(int rcvtimeo)
{
    Q_D(SocketBase);
#   if defined (ZMQ_RCVTIMEO)
    zmq_setsockopt(zocket, ZMQ_RCVTIMEO, &rcvtimeo, sizeof (int));
#   endif
}

int SocketBase::rcvtimeo()
{
    Q_D(SocketBase);
#   if defined (ZMQ_RCVTIMEO)
    int rcvtimeo;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_RCVTIMEO, &rcvtimeo, &option_len);
    return rcvtimeo;
#   else
    return 0;
#   endif
}

void SocketBase::setSndtimeo(int sndtimeo)
{
    Q_D(SocketBase);
#   if defined (ZMQ_SNDTIMEO)
    zmq_setsockopt(zocket, ZMQ_SNDTIMEO, &sndtimeo, sizeof (int));
#   endif
}

int SocketBase::sndtimeo()
{
    Q_D(SocketBase);
#   if defined (ZMQ_SNDTIMEO)
    int sndtimeo;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_SNDTIMEO, &sndtimeo, &option_len);
    return sndtimeo;
#   else
    return 0;
#   endif
}

void SocketBase::setXPubVerbose(int xpub_verbose)
{
    Q_D(SocketBase);
#   if defined (ZMQ_XPUB_VERBOSE)
    if (socket_type() != ZMQ_XPUB) {
        qFatal("ZMQ_XPUB_VERBOSE is not valid on %s sockets\n", type_str());
    }
    zmq_setsockopt(zocket, ZMQ_XPUB_VERBOSE, &xpub_verbose, sizeof (int));
#   endif
}

void SocketBase::setTcpKeepalive(int tcp_keepalive)
{
    Q_D(SocketBase);
#   if defined (ZMQ_TCP_KEEPALIVE)
    zmq_setsockopt(zocket, ZMQ_TCP_KEEPALIVE, &tcp_keepalive, sizeof (int));
#   endif
}

int SocketBase::tcpKeepalive()
{
    Q_D(SocketBase);
#   if defined (ZMQ_TCP_KEEPALIVE)
    int tcp_keepalive;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_TCP_KEEPALIVE, &tcp_keepalive, &option_len);
    return tcp_keepalive;
#   else
    return 0;
#   endif
}

void SocketBase::setTcpKeepAlive_idle(int tcp_keepalive_idle)
{
    Q_D(SocketBase);
#   if defined (ZMQ_TCP_KEEPALIVE_IDLE)
    zmq_setsockopt(zocket, ZMQ_TCP_KEEPALIVE_IDLE, &tcp_keepalive_idle, sizeof (int));
#   endif
}

int SocketBase::tcpKeepalive_idle()
{
    Q_D(SocketBase);
#   if defined (ZMQ_TCP_KEEPALIVE_IDLE)
    int tcp_keepalive_idle;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_TCP_KEEPALIVE_IDLE, &tcp_keepalive_idle, &option_len);
    return tcp_keepalive_idle;
#   else
    return 0;
#   endif
}

void SocketBase::setTcpKeepalive_cnt(int tcp_keepalive_cnt)
{
    Q_D(SocketBase);
#   if defined (ZMQ_TCP_KEEPALIVE_CNT)
    zmq_setsockopt(zocket, ZMQ_TCP_KEEPALIVE_CNT, &tcp_keepalive_cnt, sizeof (int));
#   endif
}

int SocketBase::tcpKeepalive_cnt()
{
    Q_D(SocketBase);
#   if defined (ZMQ_TCP_KEEPALIVE_CNT)
    int tcp_keepalive_cnt;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_TCP_KEEPALIVE_CNT, &tcp_keepalive_cnt, &option_len);
    return tcp_keepalive_cnt;
#   else
    return 0;
#   endif
}

void SocketBase::setTcpKeepalive_intvl(int tcp_keepalive_intvl)
{
    Q_D(SocketBase);
#   if defined (ZMQ_TCP_KEEPALIVE_INTVL)
    zmq_setsockopt(zocket, ZMQ_TCP_KEEPALIVE_INTVL, &tcp_keepalive_intvl, sizeof (int));
#   endif
}

int SocketBase::tcpKeepalive_intvl()
{
    Q_D(SocketBase);
#   if defined (ZMQ_TCP_KEEPALIVE_INTVL)
    int tcp_keepalive_intvl;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_TCP_KEEPALIVE_INTVL, &tcp_keepalive_intvl, &option_len);
    return tcp_keepalive_intvl;
#   else
    return 0;
#   endif
}

void SocketBase::setTcpAccept_filter(const char *tcp_accept_filter)
{
    Q_D(SocketBase);
#   if defined (ZMQ_TCP_ACCEPT_FILTER)
    zmq_setsockopt(zocket, ZMQ_TCP_ACCEPT_FILTER, tcp_accept_filter, strlen (tcp_accept_filter));
#   endif
}

QString SocketBase::tcpAccept_filter()
{    
    Q_D(SocketBase);
#   if defined (ZMQ_TCP_ACCEPT_FILTER)
    size_t option_len = 255;
    char *tcp_accept_filter = (char *) zmalloc (option_len);
    zmq_getsockopt(zocket,ZMQ_TCP_ACCEPT_FILTER, tcp_accept_filter, &option_len);
    return QString(tcp_accept_filter);
#   else
    return QString();
#   endif
}

int SocketBase::receiveMore()
{
    Q_D(SocketBase);
#   if defined (ZMQ_RCVMORE)
    int rcvmore;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_RCVMORE, &rcvmore, &option_len);
    return rcvmore;
#   else
    return 0;
#   endif
}

int SocketBase::fd()
{
    Q_D(SocketBase);
#   if defined (ZMQ_FD)
    int fd;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket, ZMQ_FD, &fd, &option_len);
    return fd;
#   else
    return 0;
#   endif
}

int SocketBase::events()
{
    Q_D(SocketBase);
#   if defined (ZMQ_EVENTS)
    int events;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_EVENTS, &events, &option_len);
    return events;
#   else
    return 0;
#   endif
}

QString SocketBase::lastEndpoint()
{
    Q_D(SocketBase);
#   if defined (ZMQ_LAST_ENDPOINT)
    size_t option_len = 255;
    char *last_endpoint = (char *) zmalloc (option_len);
    zmq_getsockopt(zocket,ZMQ_LAST_ENDPOINT, last_endpoint, &option_len);
    return QString(last_endpoint);
#   else
    return NULL;
#   endif
}

void SocketBase::setHwm(int hwm)
{
    setSndhwm(hwm);
    setRcvhwm(hwm);
}

int SocketBase::socket_type()
{
    Q_D(SocketBase);
#   if defined (ZMQ_TYPE)
    int type;
    size_t option_len = sizeof (int);
    zmq_getsockopt(zocket,ZMQ_TYPE, &type, &option_len);
    return type;
#   else
    return 0;
#   endif
}


/*
 *
 *
 *
 *
 * main implemantation of sockets in our framework ***/

Socket::Socket(Context *parent): SocketBase(*(new SocketPrivate), parent)
{}

Socket::Socket(SocketBasePrivate &d, Context *parent) : SocketBase(d, parent)
{}

Socket::Socket(Context *parent, int type) : SocketBase(*(new SocketPrivate), parent)
{
    if(parent)
    {
        parent->append(this, type);
    }
}

Socket::~Socket()
{

}

int Socket::bind(const char *format, ...)
{
    Q_D(Socket);
    //  Expand format to get full endpoint
    va_list argptr;
    va_start (argptr, format);
    char *endpoint = zsys_vprintf (format, argptr);
    va_end (argptr);
    if (!endpoint)
        return -1;
    int rc;

    //  If tcp:// endpoint, parse to get or make port number
    zrex_t *rex = zrex_new (NULL);
    if (zrex_eq (rex, endpoint, "^tcp://.*:(\\d+)$")) {
        assert (zrex_hits (rex) == 2);
        if (zmq_bind (zocket, endpoint) == 0)
            rc = atoi (zrex_hit (rex, 1));
        else
            rc = -1;
    }
    else
        if (zrex_eq (rex, endpoint, "^(tcp://.*):([*!])(\\[(\\d+)?-(\\d+)?\\])?$")) {
            assert (zrex_hits (rex) == 6);
            const char *hostname, *opcode, *group, *first_str, *last_str;
            zrex_fetch (rex, &hostname, &opcode, &group, &first_str, &last_str, NULL);

            int first = *first_str ? atoi (first_str) : DYNAMIC_FIRST;
            int last = *last_str ? atoi (last_str) : DYNAMIC_LAST;

            //  This is how many times we'll try before giving up
            int attempts = last - first + 1;

            //  If operator is '*', take first available port.
            //  If operator is '!', take a random leap into our port space; we'll
            //  still scan sequentially to make sure we find a free slot rapidly.
            int port = first;
            if (streq (opcode, "!"))
                port += randof (attempts);

            rc = -1;                //  Assume we don't succeed
            while (rc == -1 && attempts--) {
                free (endpoint);
                endpoint = zsys_sprintf ("%s:%d", hostname, port);
                if (!endpoint)
                    break;
                if (zmq_bind (zocket, endpoint) == 0)
                    rc = port;
                if (++port > last)
                    port = first;
            }
        }
        else
            rc = zmq_bind (zocket, endpoint);

    //  Store successful endpoint for later reference
    if (rc >= 0) {
        d->endpoint = QString(endpoint);
    }
    else
        free (endpoint);

    zrex_destroy (&rex);
    return rc;
}

QString Socket::endpoint()
{
    Q_D(Socket);
    return d->endpoint;
}

int Socket::unbind(const char *format, ...)
{
    Q_D(Socket);
#if (ZMQ_VERSION >= ZMQ_MAKE_VERSION (3, 2, 0))
    //  Expand format to get full endpoint
    va_list argptr;
    va_start (argptr, format);
    char *endpoint = zsys_vprintf (format, argptr);
    va_end (argptr);
    if (!endpoint)
        return -1;

    int rc = zmq_unbind (zocket, endpoint);
    free (endpoint);
    return rc;
#else
    return -1;
#endif
}

int Socket::connect(const char *format, ...)
{
    Q_D(Socket);
    //  Expand format to get full endpoint
    va_list argptr;
    va_start (argptr, format);
    char *endpoint = zsys_vprintf (format, argptr);
    va_end (argptr);
    if (!endpoint)
        return -1;
    int rc = zmq_connect (zocket, endpoint);

#if (ZMQ_VERSION < ZMQ_MAKE_VERSION (4, 0, 0))
    int retries = 4;
    while (rc == -1 && zmq_errno () == ECONNREFUSED && retries) {
        //  This bruteforces a synchronization between connecting and
        //  binding threads on ZeroMQ v3.2 and earlier, where the bind
        //  MUST happen before the connect on inproc transports.
        zclock_sleep (250);
        rc = zmq_connect (zocket, endpoint);
        retries--;
    }
#endif
    free (endpoint);
    return rc;
}

int Socket::disconnect(const char *format, ...)
{
    Q_D(Socket);
#if (ZMQ_VERSION >= ZMQ_MAKE_VERSION (3, 2, 0))
    //  Expand format to get full endpoint
    va_list argptr;
    va_start (argptr, format);
    char *endpoint = zsys_vprintf (format, argptr);
    va_end (argptr);
    if (!endpoint)
        return -1;
    int rc = zmq_disconnect (zocket, endpoint);
    free (endpoint);
    return rc;
#else
    return -1;
#endif
}

int Socket::attach(const char *endpoints, bool serverish)
{
    if (!endpoints)
        return 0;

    //  We hold each individual endpoint here
    char endpoint [256];
    while (*endpoints) {
        const char *delimiter = strchr (endpoints, ',');
        if (!delimiter)
            delimiter = endpoints + strlen (endpoints);
        if (delimiter - endpoints > 255)
            return -1;
        memcpy (endpoint, endpoints, delimiter - endpoints);
        endpoint [delimiter - endpoints] = 0;

        int rc;
        if (endpoint [0] == '@')
            rc = bind("%s", endpoint + 1);
        else
            if (endpoint [0] == '>')
                rc = connect("%s", endpoint + 1);
            else
                if (serverish)
                    rc = bind("%s", endpoint);
                else
                    rc = connect("%s", endpoint);

        if (rc == -1)
            return -1;          //  Bad endpoint syntax

        if (*delimiter == 0)
            break;
        endpoints = delimiter + 1;
    }
    return 0;
}

int Socket::send(const char *picture, ...)
{
    assert (picture);

    va_list argptr;
    va_start (argptr, picture);
    Messages mlist;
    while (*picture) {
        if (*picture == 'i')
            mlist.append("%d", va_arg (argptr, int));
        else
        if (*picture == 'u')
            mlist.append("%ud", va_arg (argptr, uint));
        else
        if (*picture == 's')
            mlist.append(QString(va_arg (argptr, char *)));
        else
        if (*picture == 'b') {
            //  Note function arguments may be expanded in reverse order,
            //  so we cannot use va_arg macro twice in a single call
            byte *data = va_arg (argptr, byte *);
            mlist.appendmem(data, va_arg (argptr, size_t));
        }
        else
        if (*picture == 'c') {
            QByteArray *chunk = va_arg (argptr, QByteArray *);
            mlist.append(new Frame(*chunk));
        }
        else
        if (*picture == 'f') {
            Frame *frame = dynamic_cast<Frame *>(va_arg (argptr, Frame *));
            assert(frame);
            mlist.append(*frame);
        }
        else
        if (*picture == 'p') {
            void *pointer = va_arg (argptr, void *);
            mlist.appendmem(&pointer, sizeof (void *));
        }
        else
        if (*picture == 'm') {
            Frame *frame;
            Messages *zmsg = va_arg (argptr, Messages*);
            for (frame = zmsg->first(); frame;
                 frame = zmsg->next(frame) ) {
                mlist.append(*frame);
            }
        }
        else
        if (*picture == 'z')
            mlist.appendmem(NULL, 0);
        else {
            qWarning("zsock: invalid picture element '%c'", *picture);
            assert (false);
        }
        picture++;
    }
    va_end (argptr);
    return mlist.send(*this);
}

int Socket::recv(const char *picture, ...)
{
    assert (picture);
    Messages mlist;
    if (!mlist.recv(*this))
        return -1;              //  Interrupted

    int rc = 0;
    va_list argptr;
    va_start (argptr, picture);
    while (*picture) {
        if (*picture == 'i') {
            QString string = mlist.popstr();
            int *int_p = va_arg (argptr, int *);
            if (int_p)
                *int_p = !string.isNull() ? string.toInt() : 0;
        }
        else
        if (*picture == 'u') {
            QString string = mlist.popstr();
            uint *uint_p = va_arg (argptr, uint *);
            if (uint_p)
                *uint_p = !string.isNull() ? (uint) string.toLong() : 0;
        }
        else
        if (*picture == 's') {
            QString string = mlist.popstr();
            QString *string_p = va_arg (argptr, QString *);
            if (string_p)
                *string_p = string;
        }
        else
        if (*picture == 'b') {
            Frame *frame = mlist.pop();
            byte **data_p = va_arg (argptr, byte **);
            size_t *size = va_arg (argptr, size_t *);
            if (data_p) {
                if (frame) {
                    *size = frame->size();
                    *data_p = (byte *) malloc (*size);
                    memcpy (*data_p, frame->data(), *size);
                }
                else {
                    *data_p = NULL;
                    *size = 0;
                }
            }
            delete frame;
        }
        else
        if (*picture == 'c') {
            Frame *frame = mlist.pop();
            QByteArray **chunk_p = va_arg (argptr, QByteArray **);
            if (chunk_p) {
                if (frame)
                    *chunk_p = new QByteArray(reinterpret_cast<const char*>(frame->data()), frame->size());
                else
                    *chunk_p = NULL;
            }
            delete frame;
        }
        else
        if (*picture == 'f') {
            Frame *frame = mlist.pop();
            Frame **frame_p = va_arg (argptr, Frame **);
            if (frame_p)
                *frame_p = frame;
            else
                delete frame;
        }
        else
        if (*picture == 'p') {
            Frame *frame = mlist.pop();
            void **pointer_p = va_arg (argptr, void **);
            if (pointer_p) {
                if (frame) {
                    if(frame->size() == sizeof (void *))
                        *pointer_p = *((void **) frame->data());
                    else
                        rc = -1;
                }
                else
                    *pointer_p = NULL;
            }
            delete frame;
        }
        else
        if (*picture == 'm') {
            Messages **zmsg_p = va_arg (argptr, Messages **);
            if (zmsg_p) {
                *zmsg_p = new Messages;
                Frame *frame;
                while ((frame = mlist.pop()))
                    (*zmsg_p)->append(frame);
            }
        }
        else
        if (*picture == 'z') {
            Frame *frame = mlist.pop();
            if (frame && frame->size() != 0)
                rc = -1;
            delete frame;
        }
        else {
            qWarning("zsock: invalid picture element '%c'", *picture);
            assert (false);
        }
        picture++;
    }
    va_end (argptr);    
    return rc;
}

//  --------------------------------------------------------------------------
//  Network data encoding macros that we use in bsend/brecv

//  Put a 1-byte number to the frame
#define PUT_NUMBER1(host) { \
    *(byte *) needle = (host); \
    needle++; \
    }

//  Put a 2-byte number to the frame
#define PUT_NUMBER2(host) { \
    needle [0] = (byte) (((host) >> 8)  & 255); \
    needle [1] = (byte) (((host))       & 255); \
    needle += 2; \
    }

//  Put a 4-byte number to the frame
#define PUT_NUMBER4(host) { \
    needle [0] = (byte) (((host) >> 24) & 255); \
    needle [1] = (byte) (((host) >> 16) & 255); \
    needle [2] = (byte) (((host) >> 8)  & 255); \
    needle [3] = (byte) (((host))       & 255); \
    needle += 4; \
    }

//  Put a 8-byte number to the frame
#define PUT_NUMBER8(host) { \
    needle [0] = (byte) (((host) >> 56) & 255); \
    needle [1] = (byte) (((host) >> 48) & 255); \
    needle [2] = (byte) (((host) >> 40) & 255); \
    needle [3] = (byte) (((host) >> 32) & 255); \
    needle [4] = (byte) (((host) >> 24) & 255); \
    needle [5] = (byte) (((host) >> 16) & 255); \
    needle [6] = (byte) (((host) >> 8)  & 255); \
    needle [7] = (byte) (((host))       & 255); \
    needle += 8; \
    }

//  Get a 1-byte number from the frame
#define GET_NUMBER1(host) { \
    if (needle + 1 > ceiling) \
    goto malformed; \
    (host) = *(byte *) needle; \
    needle++; \
    }

//  Get a 2-byte number from the frame
#define GET_NUMBER2(host) { \
    if (needle + 2 > ceiling) \
    goto malformed; \
    (host) = ((uint16_t) (needle [0]) << 8) \
    +  (uint16_t) (needle [1]); \
    needle += 2; \
    }

//  Get a 4-byte number from the frame
#define GET_NUMBER4(host) { \
    if (needle + 4 > ceiling) \
    goto malformed; \
    (host) = ((uint32_t) (needle [0]) << 24) \
    + ((uint32_t) (needle [1]) << 16) \
    + ((uint32_t) (needle [2]) << 8) \
    +  (uint32_t) (needle [3]); \
    needle += 4; \
    }

//  Get a 8-byte number from the frame
#define GET_NUMBER8(host) { \
    if (needle + 8 > ceiling) \
    goto malformed; \
    (host) = ((uint64_t) (needle [0]) << 56) \
    + ((uint64_t) (needle [1]) << 48) \
    + ((uint64_t) (needle [2]) << 40) \
    + ((uint64_t) (needle [3]) << 32) \
    + ((uint64_t) (needle [4]) << 24) \
    + ((uint64_t) (needle [5]) << 16) \
    + ((uint64_t) (needle [6]) << 8) \
    +  (uint64_t) (needle [7]); \
    needle += 8; \
    }


int Socket::bsend(const char *picture, ...)
{
    assert (picture);

    //  Pass 1: calculate total size of data frame
    size_t frame_size = 0;
    Frame *frames [ZSOCK_BSEND_MAX_FRAMES]; //  Non-data frames to send
    size_t nbr_frames = 0;                     //  Size of this table
    va_list argptr;
    va_start (argptr, picture);
    const char *picptr = picture;
    while (*picptr) {
        if (*picptr == '1') {
            va_arg (argptr, int);
            frame_size += 1;
        }
        else
        if (*picptr == '2') {
            va_arg (argptr, int);
            frame_size += 2;
        }
        else
        if (*picptr == '4') {
            va_arg (argptr, uint32_t);
            frame_size += 4;
        }
        else
        if (*picptr == '8') {
            va_arg (argptr, uint64_t);
            frame_size += 8;
        }
        else
        if (*picptr == 's') {
            char *string = va_arg (argptr, char *);
            frame_size += 1 + (string? strlen (string): 0);
        }
        else
        if (*picptr == 'S') {
            char *string = va_arg (argptr, char *);
            frame_size += 4 + (string? strlen (string): 0);
        }
        else
        if (*picptr == 'c') {
            QByteArray *chunk = va_arg (argptr, QByteArray *);
            frame_size += 4 + (chunk ? chunk->size(): 0);
        }
        else
        if (*picptr == 'p') {
            va_arg (argptr, void *);
            frame_size += sizeof (void *);
        }
        else
        if (*picptr == 'f') {
            Frame *frame = va_arg (argptr, Frame *);
            assert (nbr_frames < ZSOCK_BSEND_MAX_FRAMES - 1);
            frames [nbr_frames++] = frame;
        }
        else
        if (*picptr == 'm') {
            if (picptr [1]) {
                qWarning("zsock_bsend: 'm' (zmsg) only valid at end of picptr");
                assert (false);
            }
            Messages *msg = va_arg (argptr, Messages *);
            if (msg) {
                Frame *frame = msg->first();
                while (frame) {
                    assert (nbr_frames < ZSOCK_BSEND_MAX_FRAMES - 1);
                    frames [nbr_frames++] = frame;
                    frame = msg->next(frame);
                }
            }
            else
                frames [nbr_frames++] = new Frame;
        }
        else {
            qWarning("zsock_bsend: invalid picptr element '%c'", *picptr);
            assert (false);
        }
        picptr++;
    }
    va_end (argptr);

    //  Pass 2: encode data into data frame
    zmq_msg_t msg;
    zmq_msg_init_size (&msg, frame_size);
    byte *needle = (byte *) zmq_msg_data (&msg);

    va_start (argptr, picture);
    picptr = picture;
    while (*picptr) {
        if (*picptr == '1') {
            int number1 = va_arg (argptr, int);
            PUT_NUMBER1 (number1);
        }
        else
        if (*picptr == '2') {
            int number2 = va_arg (argptr, int);
            PUT_NUMBER2 (number2);
        }
        else
        if (*picptr == '4') {
            uint32_t number4 = va_arg (argptr, uint32_t);
            PUT_NUMBER4 (number4);
        }
        else
        if (*picptr == '8') {
            uint64_t number8 = va_arg (argptr, uint64_t);
            PUT_NUMBER8 (number8);
        }
        else
        if (*picptr == 'p') {
            void *pointer = va_arg (argptr, void *);
            memcpy (needle, &pointer, sizeof (void *));
            needle += sizeof (void *);
        }
        else
        if (*picptr == 's') {
            char *string = va_arg (argptr, char *);
            if (!string)
                string = "";
            size_t string_size = strlen (string);
            PUT_NUMBER1 (string_size);
            memcpy (needle, string, string_size);
            needle += string_size;
        }
        else
        if (*picptr == 'S') {
            char *string = va_arg (argptr, char *);
            if (!string)
                string = "";
            size_t string_size = strlen (string);
            PUT_NUMBER4 (string_size);
            memcpy (needle, string, string_size);
            needle += string_size;
        }
        else
        if (*picptr == 'c') {
            QByteArray *chunk = va_arg (argptr, QByteArray *);
            if (chunk) {
                PUT_NUMBER4 (chunk->size());
                memcpy (needle, chunk->constData(), chunk->size());
                needle += chunk->size();
            }
        }
        picptr++;
    }
    va_end (argptr);

    //  Now send the data frame
    void *handle = resolve();
    zmq_msg_send (&msg, handle, nbr_frames ? ZMQ_SNDMORE: 0);

    //  Now send any additional frames
    unsigned int frame_nbr;
    for (frame_nbr = 0; frame_nbr < nbr_frames; frame_nbr++) {
        bool more = frame_nbr < nbr_frames - 1;
        frames [frame_nbr]->send(*this, QFRAME_REUSE + (more ? QFRAME_MORE: 0));
    }
    return 0;
}

//  This is the largest size we allow for an incoming longstr or chunk (1M)
#define MAX_ALLOC_SIZE      1024 * 1024

int Socket::brecv(const char *picture, ...)
{
    Q_D(Socket);
    assert (picture);

    zmq_msg_t msg;
    zmq_msg_init (&msg);
    if (zmq_msg_recv (&msg, resolve(), 0) == -1)
        return -1;              //  Interrupted

    //  If we don't have a string cache, create one now with arbitrary
    //  value; this will grow if needed. Do not use an initial size less
    //  than 256, or cache expansion will not work properly.

    if (!(d->cache)) {
        d->cache = (char *) malloc (512);
        d->cache_size = 512;
    }
    //  Last received strings are cached per socket
    uint cache_used = 0;
    byte *needle = (byte *) zmq_msg_data (&msg);
    byte *ceiling = needle + zmq_msg_size (&msg);

    va_list argptr;
    va_start (argptr, picture);
    const char *picptr = picture;
    while (*picptr) {
    if (*picptr == '1') {
        uint8_t *number1_p = va_arg (argptr, uint8_t *);
        GET_NUMBER1 (*number1_p);
    }
        else
            if (*picptr == '2') {
                uint16_t *number2_p = va_arg (argptr, uint16_t *);
                GET_NUMBER2 (*number2_p);
            }
            else
            if (*picptr == '4') {
                uint32_t *number4_p = va_arg (argptr, uint32_t *);
                GET_NUMBER4 (*number4_p);
            }
            else
            if (*picptr == '8') {
                uint64_t *number8_p = va_arg (argptr, uint64_t *);
                GET_NUMBER8 (*number8_p);
            }
            else
            if (*picptr == 'p') {
                void **pointer_p = va_arg (argptr, void **);
                memcpy (pointer_p, needle, sizeof (void *));
                needle += sizeof (void *);
            }
            else
            if (*picptr == 's') {
                char **string_p = va_arg (argptr, char **);
                uint string_size;
                GET_NUMBER1 (string_size);
                if (needle + string_size > ceiling)
                    goto malformed;
                //  Expand cache if we need to; string is guaranteed to fit into
                //  expansion space
                if (cache_used + string_size > d->cache_size) {
                    puts ("REALLOC");
                    d->cache = (char *) realloc (d->cache, d->cache_size);
                    d->cache_size *= 2;
                }
                *string_p = d->cache + cache_used;
                memcpy (*string_p, needle, string_size);
                cache_used += string_size;
                d->cache[cache_used++] = 0;
                needle += string_size;
            }
            else
            if (*picptr == 'S') {
                char **string_p = va_arg (argptr, char **);
                size_t string_size;
                GET_NUMBER4 (string_size);
                if (string_size > MAX_ALLOC_SIZE
                        ||  needle + string_size > (ceiling))
                    goto malformed;
                *string_p = (char *) malloc (string_size + 1);
                assert (string_p);
                memcpy (*string_p, needle, string_size);
                (*string_p) [string_size] = 0;
                needle += string_size;
            }
            else
            if (*picptr == 'c') {
                QByteArray **chunk_p = va_arg (argptr, QByteArray **);
                size_t chunk_size;
                GET_NUMBER4 (chunk_size);
                if (chunk_size > MAX_ALLOC_SIZE
                        ||  needle + chunk_size > (ceiling))
                    goto malformed;
                *chunk_p = new QByteArray((const char*)needle, chunk_size);
                needle += chunk_size;
            }
            else
            if (*picptr == 'f') {
                Frame **frame_p = va_arg (argptr, Frame **);
                //  Get next frame off socket
                if (!receiveMore())
                    goto malformed;
                *frame_p = new Frame;
                (*frame_p)->recv(*this);
            }
            else
            if (*picptr == 'm') {
                if (picptr [1]) {
                    qWarning("zsock_brecv: 'm' (zmsg) only valid at end of picptr");
                    assert (false);
                }
                Messages **msg_p = va_arg (argptr, Messages **);
                //  Get zero or more remaining frames
                if (!receiveMore())
                    goto malformed;
                *msg_p = new Messages;
                (*msg_p)->recv(*this);
            }
            else {
                qWarning("zsock_brecv: invalid picptr element '%c'", *picptr);
                assert (false);
            }
        picptr++;
    }
    va_end (argptr);
    zmq_msg_close (&msg);
    return 0;

    //  Error return
malformed:
    zmq_msg_close (&msg);
    return -1;              //  Invalid message
}

void Socket::set_unbounded()
{
#if (ZMQ_VERSION_MAJOR == 2)
    setHwm (0);
#else
    setSndhwm(0);
    setRcvhwm(0);
#endif
}

int Socket::signal(byte status)
{
    int64_t signal_value = 0x7766554433221100L + status;
    Messages mlist;
    mlist.appendmem((void*)(&signal_value), 8);
    int rc = mlist.send(*this);
    return rc;
}

int Socket::wait()
{
    //  A signal is a message containing one frame with our 8-byte magic
    //  value. If we get anything else, we discard it and continue to look
    //  for the signal message
    while (true) {
        Messages mlist;
        if (!mlist.recv(*this))
            return -1;
        if (mlist.size() == 1
              && mlist.contentSize() == 8) {
            Frame *frame = mlist.first();
            int64_t signal_value = *((int64_t *) frame->data());
            if ((signal_value & 0xFFFFFFFFFFFFFF00L) == 0x7766554433221100L) {
                return signal_value & 255;
            }
        }
    }
    return -1;
}

void Socket::flush()
{
    if (receiveMore()) {
        Messages mlist;
        mlist.recv(*this);
    }
}

void Socket::test(bool verbose)
{
    printf (" * socket: ");
    if (verbose)
        printf ("\n");

    //  @selftest
    Socket *writer = Socket::createPush("@tcp://127.0.0.1:5560");
    assert (writer);
    assert (streq (writer->type_str(), "PUSH"));

    int rc;
    //  Check unbind
    rc = writer->unbind("tcp://127.0.0.1:%d", 5560);
    assert (rc == 0);

    //  In some cases and especially when running under Valgrind, doing
    //  a bind immediately after an unbind causes an EADDRINUSE error.
    //  Even a short sleep allows the OS to release the port for reuse.
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    QThread::msleep(10);
#else
    usleep(10);
#endif

    //  Bind again
    rc = writer->bind("tcp://127.0.0.1:%d", 5560);
    assert (rc == 5560);
    assert (writer->endpoint() == "tcp://127.0.0.1:5560");

    Socket *reader = Socket::createPull(">tcp://127.0.0.1:5560");
    assert (reader);
    assert (streq (reader->type_str(), "PULL"));

    //  Basic Hello, World
    writer->sendstr("Hello, World");
    Messages msg;
    msg.recv(*reader);
    assert (msg.size() == 1);
    QString string = msg.popstr();
    assert (string == "Hello, World");

    //  Test binding to ephemeral ports, sequential and random
    int port = writer->bind("tcp://127.0.0.1:*");
    assert (port >= DYNAMIC_FIRST && port <= DYNAMIC_LAST);
    port = writer->bind("tcp://127.0.0.1:*[50000-]");
    assert (port >= 50000 && port <= DYNAMIC_LAST);
    port = writer->bind("tcp://127.0.0.1:*[-50001]");
    assert (port >= DYNAMIC_FIRST && port <= 50001);
    port = writer->bind("tcp://127.0.0.1:*[60000-60010]");
    assert (port >= 60000 && port <= 60010);

    port = writer->bind("tcp://127.0.0.1:!");
    assert (port >= DYNAMIC_FIRST && port <= DYNAMIC_LAST);
    port = writer->bind("tcp://127.0.0.1:![50000-]");
    assert (port >= 50000 && port <= DYNAMIC_LAST);
    port = writer->bind("tcp://127.0.0.1:![-50001]");
    assert (port >= DYNAMIC_FIRST && port <= 50001);
    port = writer->bind("tcp://127.0.0.1:![60000-60010]");
    assert (port >= 60000 && port <= 60010);

    //  Test socket_attach method
    Socket *server = new Socket(srnet, ZMQ_DEALER);
    assert (server);
    rc = server->attach("@inproc://myendpoint,tcp://127.0.0.1:5556,inproc://others", true);
    assert (rc == 0);
    rc = server->attach("", false);
    assert (rc == 0);
    rc = server->attach(NULL, true);
    assert (rc == 0);
    rc = server->attach(">a,@b, c,, ", false);
    assert (rc == -1);
    delete server;

    //  Test socket_endpoint method
    rc = writer->bind("inproc://test.%s", "writer");
    assert (rc == 0);
    assert (writer->endpoint() == "inproc://test.writer");

    //  Test error state when connecting to an invalid socket type
    //  ('txp://' instead of 'tcp://', typo intentional)
    rc = reader->connect("txp://127.0.0.1:5560");
    assert (rc == -1);

    //  Test signal/wait methods
    rc = writer->signal(123);
    assert (rc == 0);
    rc = reader->wait();
    assert (rc == 123);

    //  Test zsock_send/recv pictures
    QByteArray ba("HELLO", 5);
    Frame frame("WORLD", 5);
    char *original = "pointer";

    //  We can send signed integers, strings, blocks of memory, chunks,
    //  frames, hashes and pointers
    writer->send("izsbcfp",
                -12345, "This is a string", "ABCDE", 5, &ba, &frame, original);
    msg.recv(*reader);
    assert (msg.size() == 7);
    if (verbose)
        qDebug() << msg.toStringList();

    //  Test recv into each supported type
    writer->send("izsbcfp",
                -12345, "This is a string", "ABCDE", 5, &ba, &frame, original);

    int integer;
    byte *data;
    size_t size;
    Frame* frm;
    QByteArray* pba;
    char *pointer;
    rc = reader->recv("izsbcfp", &integer, &string, &data, &size, &pba, &frm, &pointer);
    assert (rc == 0);
    assert (integer == -12345);
    assert (string == "This is a string");
    assert (memcmp (data, "ABCDE", 5) == 0);
    assert (size == 5);
    assert (memcmp (pba->data(), "HELLO", 5) == 0);
    assert (pba->size() == 5);
    assert (memcmp (frm->data(), "WORLD", 5) == 0);
    assert (frm->size() == 5);
    assert (original == pointer);
    free (data);

    delete pba;
    delete frm;

    //  Test zsock_recv of short message; this lets us return a failure
    //  with a status code and then nothing else; the receiver will get
    //  the status code and NULL/zero for all other values
    writer->send("i", -1);
    reader->recv("izsbcfp", &integer, &string, &data, &size, &pba, &frm, &pointer);
    assert (integer == -1);
    assert (string.isNull());
    assert (data == NULL);
    assert (size == 0);
    assert (pba == NULL);
    assert (frm == NULL);
    assert (pointer == NULL);

    msg.clear();
    msg.append(QString("frame 1"));
    msg.append(QString("frame 2"));
    writer->send("szm", "header", &msg);
    msg.clear();

    Messages* pmsg;
    reader->recv("szm", &string, &pmsg);

    assert ("header" == string);
    assert (pmsg->size() == 2);
    assert (pmsg->first()->toString() == "frame 1");
    assert (pmsg->last()->toString() == "frame 2");
    delete pmsg;

    //  Test zsock_bsend/brecv pictures with binary encoding
    uint8_t  number1 = 123;
    uint16_t number2 = 123 * 123;
    uint32_t number4 = 123 * 123 * 123;
    uint64_t number8 = 123 * 123 * 123 * 123;

    frame.reset("Hello", 5);
    ba = QByteArray::fromRawData("World", 5);

    msg.clear();
    msg.append(QString("Hello"));
    msg.append(QString("World"));

    writer->bsend("1248sSpcfm",
                 number1, number2, number4, number8,
                 "Hello, World",
                 "Goodbye cruel World!",
                 original,
                 &ba, &frame, &msg);

    number8 = number4 = number2 = number1 = 0;
    char *longstr, *str;
    reader->brecv("1248sSpcfm",
                 &number1, &number2, &number4, &number8,
                 &str, &longstr,
                 &pointer,
                 &pba, &frm, &pmsg);
    assert (number1 == 123);
    assert (number2 == 123 * 123);
    assert (number4 == 123 * 123 * 123);
    assert (number8 == 123 * 123 * 123 * 123);
    assert (streq (str, "Hello, World"));
    assert (streq (longstr, "Goodbye cruel World!"));
    assert (pointer == original);
    zstr_free (&str);
    zstr_free (&longstr);
    delete pba;
    delete frm;
    delete pmsg;

    //  Check that we can send a zproto format message
    writer->bsend("1111sS4", 0xAA, 0xA0, 0x02, 0x01, "key", "value", 1234);
    GossipFrame gossip;
    gossip.recv(*reader);
    assert (gossip.id() == GOSSIP_MSG_PUBLISH);

    delete reader;
    delete writer;

    //  @end
    printf ("OK\n");
}

/// static functions

Socket *Socket::createPub(const char *endpoints)
{
    Socket *sock = srnet->createSocket(ZMQ_PUB);
    if (sock)
        if(sock->attach(endpoints, true))
        {
            delete sock;
            sock = NULL;
        }
    return sock;
}

Socket *Socket::createSub(const char *endpoints, const char *subscribe)
{
    Socket *sock = srnet->createSocket(ZMQ_SUB);
    if (sock) {
        if(sock->attach(endpoints, false) == 0)
        {
            if(subscribe)
                sock->setSubscribe(subscribe);
        }
        else {
            delete sock;
            sock = NULL;
        }
    }
    return sock;
}

Socket *Socket::createReq(const char *endpoints)
{
    Socket *sock = srnet->createSocket(ZMQ_REQ);
    if (sock)
        if(sock->attach(endpoints, false))
        {
            delete sock;
            sock = NULL;
        }
    return sock;
}

Socket *Socket::createRep(const char *endpoints)
{
    Socket *sock = srnet->createSocket(ZMQ_REP);
    if (sock)
        if(sock->attach(endpoints, true))
        {
            delete sock;
            sock = NULL;
        }
    return sock;
}

Socket *Socket::createDealer(const char *endpoints)
{
    Socket *sock = srnet->createSocket(ZMQ_DEALER);
    if (sock)
        if(sock->attach(endpoints, false))
        {
            delete sock;
            sock = NULL;
        }
    return sock;
}

Socket *Socket::createRouter(const char *endpoints)
{
    Socket *sock = srnet->createSocket(ZMQ_ROUTER);
    if (sock)
        if(sock->attach(endpoints, true))
        {
            delete sock;
            sock = NULL;
        }
    return sock;
}

Socket *Socket::createPush(const char *endpoints)
{
    Socket *sock = srnet->createSocket(ZMQ_PUSH);
    if (sock)
        if(sock->attach(endpoints, false))
        {
            delete sock;
            sock = NULL;
        }
    return sock;
}

Socket *Socket::createPull(const char *endpoints)
{
    Socket *sock = srnet->createSocket(ZMQ_PULL);
    if (sock)
        if(sock->attach(endpoints, true))
        {
            delete sock;
            sock = NULL;
        }
    return sock;
}

Socket *Socket::createXPub(const char *endpoints)
{
#if defined ZMQ_XPUB
    Socket *sock = srnet->createSocket(ZMQ_XPUB);
    if (sock)
        if(sock->attach(endpoints, true))
        {
            delete sock;
            sock = NULL;
        }
    return sock;
#else
    return NULL;            //  Not implemented
#endif
}

Socket *Socket::createXSub(const char *endpoints)
{
#if defined ZMQ_XSUB
    Socket *sock = srnet->createSocket(ZMQ_XSUB);
    if (sock)
        if(sock->attach(endpoints, false))
        {
            delete sock;
            sock = NULL;
        }
    return sock;
#else
    return NULL;            //  Not implemented
#endif
}

Socket *Socket::createPair(const char *endpoints)
{
    Socket *sock = srnet->createSocket(ZMQ_PAIR);
    if (sock)
        if(sock->attach(endpoints, false))
        {
            delete sock;
            sock = NULL;
        }
    return sock;
}

Socket *Socket::createStream(const char *endpoints)
{
#if defined ZMQ_STREAM
    Socket *sock = srnet->createSocket(ZMQ_STREAM);
    if (sock)
        if(sock->attach(endpoints, false))
        {
            delete sock;
            sock = NULL;
        }
    return sock;
#else
    return NULL;            //  Not implemented
#endif
}

int Socket::bind(const QString &add)
{
    return bind(add.toLocal8Bit().data());
}

int Socket::close()
{
    return SocketBase::close();
}
