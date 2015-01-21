#ifndef MDP_H
#define MDP_H

#include  "socket.h"

#define QMDPC "QMDPC01"

//  MDP/Client commands, as strings
#define MDPC_REQUEST        "\001"
#define MDPC_REPORT         "\002"
#define MDPC_NAK            "\003"

static char *mdpc_commands [] = {
    NULL, "REQUEST", "REPORT", "NAK",
};

//  This is the version of MDP/Worker we implement
#define QMDPW         "QMDPW0X"

//  MDP/Worker commands, as strings
#define MDPW_READY          "\001"
#define MDPW_REQUEST        "\002"
#define MDPW_REPORT         "\003"
#define MDPW_HEARTBEAT      "\004"
#define MDPW_DISCONNECT     "\005"

static char *mdpw_commands [] = {
    NULL, "READY", "REQUEST", "REPORT", "HEARTBEAT", "DISCONNECT"
};

class Messages;

class MdpClientPrivate;
class QMQ_EXPORT MdpClient : public QObject
{
    Q_OBJECT
public:
    MdpClient(Context *ctx, QObject* parent=0);
    ~MdpClient();

    void setBrokerAddress(const QString& broker);
    QString brokerAddress() const;
    Socket* socket();

    void setTimeOut(int ms);
    int timeOut() const;

    void setVerbose(bool);
    Messages recv(QString& command, QString& service);

    void startTest();
    void testRun();
signals:
    void receiveCommand(const QString& command, const QString& service);

public slots:
    bool connectToBroker();
    bool send(const QString& service, Messages* request);

protected:
    MdpClientPrivate* const d_ptr;


private:
    Q_DECLARE_PRIVATE(MdpClient)
};

class Frame;
class MdpWorkerPrivate;
class QMQ_EXPORT MdpWorker : public QObject
{
    Q_OBJECT
public:
    MdpWorker(Context* cntx, QObject* parent=0);
    ~MdpWorker();

    void setBrokerAddress(const QString& broker);
    QString brokerAddress() const;

    void setServiceName(const QString& name);
    QString serviceName() const;

    Socket* socket();

    void setHeartbitInterval(int ms);
    int hinterval() const;

    void setConnectInterval(int ms);
    int reconnectInterval() const;

    int liveness() const;

    void setVerbose(bool);
    void recv(QString& replyAdd, Messages& rmsg);

    void startTest();
    void testRun();

signals:
    void replyTo(const QString& address);

public slots:
    bool connectToBroker();
    bool sendToBroker(const QString& command = QString(), const QString& option = QString(),
              Messages* msgs = 0);
    bool sendToClient(const QString& reply, Messages* report);

protected:
    MdpWorkerPrivate* const d_ptr;


private:
    Q_DECLARE_PRIVATE(MdpWorker)
};

#define HEARTBEAT_LIVENESS  3       //  3-5 is reasonable
#define HEARTBEAT_INTERVAL  2500    //  msecs
#define HEARTBEAT_EXPIRY    HEARTBEAT_INTERVAL * HEARTBEAT_LIVENESS

//  The broker class defines a single broker instance and just a sample
// you can use yourself broker

class MdpBrokerPrivate;
class QMQ_EXPORT MdpBroker : public QObject
{
public:
    MdpBroker(Context* cntx, QObject* parent=0);
    ~MdpBroker();

    int bind(const char* endpoint);

    void start();
    void stop();

    static void test();

protected:
    MdpBrokerPrivate* const d_ptr;

    void run();

private:
    Q_DECLARE_PRIVATE(MdpBroker)
    bool terminated;
};

#endif // MDP_H
