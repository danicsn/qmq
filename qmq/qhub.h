#ifndef QHUB_H
#define QHUB_H

/* =========================================================================
 * QHub - a higher level MDPBrocker class
 * Writed by Daniel Nowrozi
 *
 * try to implement this pattern : http://ipython.org/ipython-doc/2/development/parallel_connections.html
 * additionally i want to connect hubs together like nameservers using udp broadcast or multicast for brockerless pattern
 *
 * not completed yet but work..
=========================================================================*/

#include <QObject>

#ifndef QMQ_EXPORT
#if defined LIBQMQ_STATIC
#   define QMQ_EXPORT
#elif defined QMQ_LIBRARY
#   define QMQ_EXPORT Q_DECL_EXPORT
#else
#   define QMQ_EXPORT Q_DECL_IMPORT
#endif
#endif

#define HUBV "1.0.0"
#define HUBID "Q_HUB_"

#define CMD_HUB_REP 001
#define CMD_HUB_NAK 002
#define CMD_HUB_WOK 003

class QHubPrivate;
/// a HUB is a broker for connecting clients to workers
/// a client can be a RPC Client and a worker can be a RPC Server class
/// multi clients can connect to one worker and vice vera
/// each worker received ping from HUB and send pong heartbeat
/// each client notify changes in workers list from HUB
/// each hub connect to other hub too (N-N)
/// each client and each worker can get near hub addresses
///
class QMQ_EXPORT QHub : public QObject
{
    Q_OBJECT
public:
    explicit QHub(QObject *parent = 0);
    ~QHub();

    int hubPort() const;
    int notifyPort() const;
    int pingPort() const;
    int pongPort() const;
    int monitorPort() const;
    QString hubid() const;

    void setHeartbeat(int msec);
    void setWorkerLiveness(int liveTime);

    int numberOfClients() const;
    int numberOfWorkers() const;

signals:
    void clientQuery(const QStringList& q); // client address, client id, client command
    void workerRegistration(const QStringList& wr); // a worker can connect to client
    void monitorReport(const QStringList& rep); // a report, from MUX
    void pong(const QStringList& png);

    void finish();

public slots:
    void startHUb();
    void stopHUb();

protected:
    QHubPrivate* const d_ptr;

private:
    Q_DECLARE_PRIVATE(QHub)
};

// client commands table
#define CMD_REQ        001
#define CMD_STATE      005
#define CMD_WORKER_CMD 003

class QClientPrivate;
class QMQ_EXPORT QClient : public QObject
{
    Q_OBJECT
public:
    explicit QClient(QObject* parent=0);
    QClient(const QString& hadd, const QString& name, const QString& id=QString(), QObject* parent=0);
    ~QClient();

    void setHubAddress(const QString& endpoint);
    void setIdentification(const QByteArray& id);
    void setName(const QString& name);

    bool connectToHub(int timeout);

    int notifyPort() const;

signals:
    void accepted(const QStringList& endpoints);
    void workerList(const QStringList& lst);

public slots:
    void queryToHub(int command, const QString& cval); // search for specific worker(s), or query information
    void sendToWorker(int command, const QString& cval); // send commands to worker(s)

protected:
    QClientPrivate* const d_ptr;

private:
    Q_DECLARE_PRIVATE(QClient)
};

#define CMD_REG 004

class QWorkerPrivate;
class QMQ_EXPORT QWorker : public QObject
{
    Q_OBJECT
public:
    explicit QWorker(const QString& name, QObject* parent=0);
    ~QWorker();

    void setHubid(const QString& hid);

    bool registerToHub(const QString& endpoint, int timeou=3000);

signals:
    void receivedCommand(const QStringList& commands);

public slots:
    void requestToHub(int command, const QString& cval);

protected:
    QWorkerPrivate* const d_ptr;

private:
    Q_DECLARE_PRIVATE(QWorker)
};

#endif // QHUB_H
