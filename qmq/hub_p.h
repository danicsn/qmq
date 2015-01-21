#ifndef HUB_P_H
#define HUB_P_H

#include <QMap>
#include <QThread>
#include <QMutex>

#undef min
#undef max

#include <QDateTime>

class Socket;
class QHub;
class Frame;
class QTimer;

class Worker_t {
public:

    QString id;
    QString name;
    QString address;
    QDateTime expiry;
    int liveness;

    friend inline bool operator ==(const Worker_t& w1, const Worker_t& w2);
};

inline bool operator ==(const Worker_t& w1, const Worker_t& w2) {
    return w1.id == w2.id;
}

class Client_t {
public:

    QString id;
    QString address;
    QString name;
    QHash<int, QString> requests;
    friend inline bool operator ==(const Client_t& c1, const Client_t& c2);
};

inline bool operator ==(const Client_t& c1, const Client_t& c2) {
    return c1.id == c2.id;
}

class QHubPrivate : public QThread {
    Q_OBJECT
public:
    QHubPrivate(QHub* parent);
    ~QHubPrivate();

    void purge();

    bool removeClient(Client_t* c);
    bool removeWorker(Worker_t* w, bool d=true);
    bool removeWorkerFromHash(Worker_t* w, bool d=true);

    void appendWorker(Client_t* c, Worker_t* w);

    void appendWorker(const Worker_t& w);
    void appendClient(const Client_t& c);

    void clientQuery(Frame* command, Frame* senderinfo, Frame *sender);
    void workerRequest(Frame* command, Frame* senderinfo, Frame *sender);

    void workerPong(Frame* sender);

    void run();

    Socket* registrar;
    Socket* ping;
    Socket* pong;
    Socket* monitor;
    Socket* notifier;

    int hport, mport, pport, poport, nport;

    bool terminate;
    QString hubid;

    QMap<QString ,Worker_t*> m_workers;
    QMap<QString ,Client_t*> m_clients;
    QHash<Client_t*, Worker_t*> m_cw;

    QDateTime heartbeat_at;
    int heartbeat;
    int liveness;
    QTimer* m_timer;

    QHub* q_ptr;
    Q_DECLARE_PUBLIC(QHub)

public slots:
    void pubPing();
    void notifyClients(const QString &nmessage);
    void notifyClients(const QStringList &nmessage);
};

#endif // HUB_P_H
