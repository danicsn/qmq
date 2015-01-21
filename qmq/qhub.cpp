#include "qhub.h"

#include "helper.h"


#include <QTimer>
#include <QDebug>
// hub implementaion version 1.00

// client and worker header
#define SRCLHD "SRCL010"
#define SRWOHD "SRWO010"

//
#include "hub_p.h"

QHubPrivate::QHubPrivate(QHub *parent): q_ptr(parent), QThread(parent)
{
    registrar = srnet->createSocket(ZMQ_ROUTER);
    ping = srnet->createSocket(ZMQ_PUB);
    pong = srnet->createSocket(ZMQ_ROUTER);
    monitor = srnet->createSocket(ZMQ_SUB);
    notifier = srnet->createSocket(ZMQ_PUB);

    hport = registrar->bind("tcp://*:*[5000-]");
    Q_ASSERT_X(hport != -1, "HUB create", "registrar binding");

    pport = ping->bind("tcp://*:*[%d-]", hport);
    Q_ASSERT_X(pport != -1, "HUB create", "ping binding");

    poport = pong->bind("tcp://*:*[%d-]", pport);
    Q_ASSERT_X(pport != -1, "HUB create", "pong binding");

    mport = monitor->bind("tcp://*:*[%d-]", poport);
    Q_ASSERT_X(pport != -1, "HUB create", "monitor binding");

    nport = notifier->bind("tcp://*:*[%d-]", mport);
    Q_ASSERT_X(pport != -1, "HUB create", "notify binding");

    terminate = true;
    heartbeat = 2000; // 2 sec
    liveness = 2; // 4 sec and then remove worker
    heartbeat_at = QDateTime::currentDateTime().addMSecs(heartbeat);

    m_timer = new QTimer(this);
    m_timer->setInterval(heartbeat / 2);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(pubPing()));
    hubid = QString("hub1:%1").arg(hport);
}

QHubPrivate::~QHubPrivate() {
    terminate = true;
    wait();

    if(m_workers.values().count() > 0)
    {
        qDeleteAll(m_workers.values());
    }

    if(m_clients.values().count() > 0)
        qDeleteAll(m_clients.values());

    delete registrar;
    delete ping;
    delete pong;
    delete monitor;
    delete notifier;
}

void QHubPrivate::purge()
{
    QMap<QString, Worker_t*>::iterator bit = m_workers.begin();
    for(;bit != m_workers.end();) {
        Worker_t* w = bit.value();
        if(w->expiry < QDateTime::currentDateTime())
        {
            if(w->liveness-- <= 0)
            {
                m_workers.erase(bit++);
                removeWorkerFromHash(w);
                delete w;
            }
            else
                ++bit;
        }
        else ++bit;
    }
}

bool QHubPrivate::removeClient(Client_t *c)
{
    Client_t* rc = m_clients.value(c->id, NULL);
    if(rc)
    {
        //foreach (Worker_t* w, m_cw.values(c)) {
            // send notify
        //}
        m_cw.remove(rc);
        m_clients.remove(c->id);

        delete rc;
        return true;
    }

    return false;
}

bool QHubPrivate::removeWorker(Worker_t *w, bool d)
{
    Worker_t* rw = m_workers.value(w->id, NULL);
    if(rw)
    {
        removeWorkerFromHash(rw, d);
        m_workers.remove(rw->id);
        delete rw;
        return true;
    }

    return false;
}

bool QHubPrivate::removeWorkerFromHash(Worker_t *w, bool d)
{
    Client_t* c = m_cw.key(w, NULL);
    if(c) {
        QList<Worker_t*> cw = m_cw.values(c);
        m_cw.remove(c);

        foreach (Worker_t* r, cw) {
            if(r->id != w->id)
                m_cw.insertMulti(c, r);
        }

        // send for client notification
        notifyClients("disconnected Worker: " + w->name + " | Worker State: lost");

        // send for worker notification
        if(d)
        {

        }

        return true;
    }

    return false;
}

void QHubPrivate::appendWorker(Client_t *c, Worker_t *w)
{
    if(!m_clients.contains(c->id)) m_clients.insert(c->id, c);
    if(!m_workers.contains(w->id)) m_workers.insert(w->id, w);

    if(!m_cw.keys().contains(c))
    {
        m_cw.insert(c, w);
    }
    else if(!m_cw.values(c).contains(w)) {
        m_cw.insertMulti(c, w);
    }
}

void QHubPrivate::appendWorker(const Worker_t &w)
{
    Worker_t* nw = new Worker_t(w);
    m_workers.insert(nw->id, nw);
}

void QHubPrivate::appendClient(const Client_t &c)
{
    Client_t* nc = new Client_t(c);
    m_clients.insert(nc->id, nc);
}

void QHubPrivate::clientQuery(Frame *command, Frame *senderinfo, Frame* sender)
{
    QString sinf = senderinfo->toString();
    QStringList info = sinf.split('%');

    if(info.count() < 2)
    {
        qWarning() << "Sender info isnt Client Valid: " << info;
        return;
    }
    Client_t c; c.id = sender->hexString(); c.address = info.at(0); c.name = info.at(1);

    QStringList cmds = command->toString().split('-');
    if(cmds.count() < 1)
    {
        qWarning() << "Commands isnt Client Valid: " << cmds;
        return;
    }

    if(!m_clients.contains(c.id) && cmds.at(0).toInt() == CMD_REQ)
    {
        // send to client needed port numbers, non blocking
        registrar->setSndtimeo(0);

        Messages msg;
        msg.push(QString::number(nport));
        msg.push(QString::number(CMD_REQ)); // command
        msg.push(SRCLHD);
        msg.wrap(new Frame(*sender));
        msg.send(*registrar);

        appendClient(c);
        return;
    }

    if(cmds.at(0).toInt() == CMD_STATE)
    {
        if(cmds.at(1) == "Disconnected")
        {
            removeClient(&c);
            return;
        }
    }

    m_clients.value(c.id)->requests.insert(cmds.at(0).toInt(), cmds.at(1));
}

void QHubPrivate::workerRequest(Frame *command, Frame *senderinfo, Frame* sender)
{
    QString sinfo = senderinfo->toString();
    QStringList info = sinfo.split('%');

    if(info.count() < 2)
    {
        qWarning() << "Sender info isn't Worker Valid: " << info;
        return;
    }
    Worker_t w; w.id = sender->hexString(); w.address = info.at(0); w.name = info.at(1);
    w.expiry = QDateTime::currentDateTime().addMSecs(heartbeat);
    w.liveness = liveness;

    QStringList cmds = command->toString().split('-');
    if(cmds.count() < 2)
    {
        qWarning() << "Commands isnt Worker Valid: " << cmds;
        return;
    }

    if(cmds.at(0).toInt() == CMD_REG)
    {
        Messages msg;
        msg.push(w.id);
        msg.push(hubid);
        msg.push(QString::number(pport));
        msg.push(QString::number(poport));
        msg.push(QString::number(CMD_REG)); // command
        msg.push(SRWOHD);
        msg.wrap(new Frame(*sender));
        msg.send(*registrar);

        notifyClients("Connected Worker: " + w.name + " | Worker State: avail");
    }


    // first initialization of heartbeat
    if(m_workers.count() == 0)
        heartbeat_at = QDateTime::currentDateTime().addMSecs(heartbeat);

    if(!m_workers.contains(w.id))
        appendWorker(w);
}

void QHubPrivate::workerPong(Frame *sender)
{
    QString id = sender->toString(); // must be so carefull here
    Worker_t* w = m_workers.value(id, NULL);
    if(w)
    {
        w->expiry = QDateTime::currentDateTime().addMSecs(heartbeat);
        w->liveness = liveness;
    }
}

void QHubPrivate::run()
{
    Q_Q(QHub);

    while(!terminate) {
        // add another sockets too
        zmq_pollitem_t items[] = { { registrar->resolve(), 0, ZMQ_POLLIN, 0 },
                                   { pong->resolve(), 0, ZMQ_POLLIN, 0} };

        int rc = zmq_poll(items, 2, heartbeat);
        if(rc == -1) {
            // add error handling
            break;
        }

        if(items[0].revents & ZMQ_POLLIN) {
            // we have a request
            Messages command;
            command.recv(*registrar);

            if(command.size() <=4)
                break;

            // first sender identity
            Frame* sender = command.pop();
            QString empty = command.popstr();
            if(empty != "") break;
            QString header = command.popstr();

            Frame* senderinfo = command.pop();
            Frame* cmdinfo = command.pop();

            if(header == SRCLHD)
                clientQuery(cmdinfo, senderinfo, sender);
            else if(header == SRWOHD)
                workerRequest(cmdinfo, senderinfo, sender);
            else
                qCritical() << "Invalid message: " << header;

            delete cmdinfo;
            delete senderinfo;
            delete sender;
        }

        //  get pong to idle workers if needed
        if(items[1].revents & ZMQ_POLLIN) {
            Messages msg;
            msg.recv(*pong);

            // worker pong identity
            Frame* sender = msg.pop();
            QString d = sender->toString();
            QString empty = msg.popstr();
            if(empty != "") break;

            QString hid = msg.popstr();
            assert(hid == hubid);

            QString p = msg.popstr();
            if(p != "Ping") break;

            workerPong(sender);
            delete sender;
        }

        //  Disconnect and delete any expired workers
        if(QDateTime::currentDateTime() > heartbeat_at)
        {
            purge(); // purge disconnected workers
            pubPing(); // publish ping to workers
            heartbeat_at = QDateTime::currentDateTime().addMSecs(heartbeat);
        }
    }

    terminate = true;
    emit q->finish();
}

void QHubPrivate::pubPing()
{
    ping->sendstr(hubid, true);
    ping->sendstr("Ping");
}

void QHubPrivate::notifyClients(const QString &nmessage)
{
    Messages msg;
    msg.push(nmessage);
    msg.push(hubid);
    msg.send(*notifier);
}

void QHubPrivate::notifyClients(const QStringList &nmessage)
{
    Messages msg;
    foreach (QString nm, nmessage) {
        msg.push(nm);
    }
    msg.push(hubid);
    msg.send(*notifier);
}

// hub
// implement SRHub
/*
 *
 *
 * **************************/

QHub::QHub(QObject *parent) : d_ptr(new QHubPrivate(this)), QObject(parent){}

QHub::~QHub() { delete d_ptr; }

int QHub::hubPort() const { Q_D(const QHub); return d->hport; }

int QHub::notifyPort() const { Q_D(const QHub); return d->nport; }

int QHub::pingPort() const { Q_D(const QHub); return d->pport; }

int QHub::pongPort() const { Q_D(const QHub); return d->poport; }

int QHub::monitorPort() const { Q_D(const QHub); return d->mport; }

QString QHub::hubid() const { Q_D(const QHub); return d->hubid; }

void QHub::setHeartbeat(int msec) { Q_D(QHub); d->heartbeat = msec; }

void QHub::setWorkerLiveness(int liveTime) { Q_D(QHub); d->liveness = liveTime; }

int QHub::numberOfClients() const { Q_D(const QHub); return d->m_clients.count(); }

int QHub::numberOfWorkers() const { Q_D(const QHub); return d->m_workers.count(); }

void QHub::startHUb()
{
    Q_D(QHub);
    d->terminate = false;
//    d->m_timer->start();
    d->start();
}

void QHub::stopHUb()
{
    Q_D(QHub);
    d->terminate = false;
    d->wait();
}

//
/*
 *
 *
 *
 *
 *
 * **********************************/

class QClientPrivate {
public:
    QClientPrivate() {
        client = 0;
    }
    ~QClientPrivate() {
        delete client;
    }

    int connectToHub();

    Socket* client;
    Socket* iopub;
    Socket* task;
    Socket* control;

    int notifyport;
    int taskport;

    QString hubadd;
    QString id;
    QString name;
};

int QClientPrivate::connectToHub()
{
    if(hubadd.isEmpty())
    {
        qWarning("First must set Hub address");
        return -1;
    }

    delete client;
    client = srnet->createSocket(ZMQ_DEALER);

    if(!id.isEmpty())
        client->setIdentity(id); // set identity for connect to router

    int rc = client->connect(hubadd.toLocal8Bit().data());
    return rc;
}


QClient::QClient(QObject *parent) : d_ptr(new QClientPrivate), QObject(parent)
{
}

QClient::QClient(const QString& hadd, const QString& name, const QString& id, QObject *parent) : d_ptr(new QClientPrivate), QObject(parent)
{
    Q_D(QClient);
    d->id = id;
    d->hubadd = hadd;
    d->name = name;
}

QClient::~QClient()
{
    queryToHub(CMD_STATE, "Disconnected");
    delete d_ptr;
}

void QClient::setHubAddress(const QString &endpoint)
{
    Q_D(QClient);
    d->hubadd = endpoint;
}

void QClient::setIdentification(const QByteArray &id)
{
    Q_D(QClient);
    d->id = id;
}

void QClient::setName(const QString &name)
{
    Q_D(QClient);
    d->name = name;
}

bool QClient::connectToHub(int timeout)
{
    Q_D(QClient);
    int rc = d->connectToHub();
    if(rc == 0)
    {
        d->client->setSndtimeo(timeout);
        d->client->setRcvtimeo(timeout);

        // request connect
        queryToHub(CMD_REQ, "");

        Messages msgs;
        if(msgs.recv(*d->client)) {
            QString emp = msgs.popstr();
            assert(emp == "");

            QString header = msgs.popstr();
            assert(header == SRCLHD);

            QString command = msgs.popstr();
            if(command.toInt() != CMD_REQ)
                return false;

            d->notifyport = msgs.popstr().toInt();

            return true;
        }
    }

    return false;
}

int QClient::notifyPort() const
{
    Q_D(const QClient);
    return d->notifyport;
}

void QClient::queryToHub(int command, const QString &cval)
{
    Q_D(QClient);

    //  Prefix request with protocol frames
    //  Frame 1: empty frame (delimiter)
    //  Frame 2: "SRCL0XY" (six bytes, SR/Client x.y)
    //  Frame 3: Service name (printable string)
    //  Frame 4..n: Commands Or Objects

    Messages msg;

    QString cmd= QString("%1-%2").arg(command).arg(cval);
    msg.push(cmd);
    msg.push("127.0.0.1%" + d->name);
    msg.push(SRCLHD);
    msg.push("");
    msg.send(*d->client);
}

void QClient::sendToWorker(int command, const QString &cval)
{
    queryToHub(CMD_WORKER_CMD, QString::number(command) + "|" + cval);
}


/*
 *
 *
 *
 * *******************************************/

class QWorkerPrivate : public QThread
{
public:
    QWorkerPrivate(QWorker* parent): q_ptr(parent)
    {
        worker = srnet->createSocket(ZMQ_DEALER);
        forwarder = new ActorSocket(qforwarder, NULL);
    }
    ~QWorkerPrivate() {
        delete worker;
        delete forwarder;
    }

    void startHeartbeat();
    void pauseHeartbeat();
    void resumeHeartbeat();

    QString name;
    QString hubid;
    QString wid;

    Socket* worker;
    ActorSocket* forwarder;

    QString endpoint;
    int pport, poport;

    QWorker* q_ptr;
    Q_DECLARE_PUBLIC(QWorker)
};

void QWorkerPrivate::startHeartbeat()
{
    QString eping = ">" + endpoint.mid(0, endpoint.lastIndexOf(":")) + ":" + QString::number(pport);
    QString epong = ">" + endpoint.mid(0, endpoint.lastIndexOf(":")) + ":" + QString::number(poport);

    forwarder->sendx("FRONTEND", "SUB", eping.toLatin1().data(), NULL);
    forwarder->wait();
    forwarder->sendx("FRONTEND", "SUB" , "SUBSCRIBER", hubid.toLatin1().data(), NULL);
    forwarder->wait();


    forwarder->sendx("BACKEND", "DEALER", "SETID", wid.toLatin1().data(), NULL);
    forwarder->wait();
    forwarder->sendx("BACKEND", "DEALER", epong.toLatin1().data(), NULL);
    forwarder->wait();
}

void QWorkerPrivate::pauseHeartbeat()
{
    forwarder->sendx("PAUSE", NULL);
    forwarder->wait();
}

void QWorkerPrivate::resumeHeartbeat()
{
    forwarder->sendx("RESUME", NULL);
    forwarder->wait();
}

//

QWorker::QWorker(const QString &name, QObject *parent) : d_ptr(new QWorkerPrivate(this)), QObject(parent)
{
    Q_D(QWorker);
    d->name = name;
}

QWorker::~QWorker()
{
    delete d_ptr;
}

void QWorker::setHubid(const QString &hid)
{
    Q_D(QWorker);
    d->hubid = hid;
}

bool QWorker::registerToHub(const QString &endpoint, int timeou)
{
    Q_D(QWorker);
    d->endpoint = endpoint;
    int rc = d->worker->connect(endpoint.toLatin1().data());
    if(rc == 0) {
        d->worker->setSndtimeo(timeou);
        d->worker->setRcvtimeo(timeou);

        //
        requestToHub(CMD_REG, "");

        Messages msgs;
        if(msgs.recv(*d->worker))
        {
            QString emp = msgs.popstr();
            assert(emp == "");

            QString header = msgs.popstr();
            assert(header == SRWOHD);

            QString command = msgs.popstr();
            if(command.toInt() != CMD_REG)
                return false;

            d->poport = msgs.popstr().toInt();
            assert(d->poport != 0);

            d->pport = msgs.popstr().toInt();
            assert(d->pport != 0);

            d->hubid = msgs.popstr();
            d->wid = msgs.popstr();

            d->startHeartbeat();

            return true;
        }
    }

    return false;
}

void QWorker::requestToHub(int command, const QString &cval)
{
    Q_D(QWorker);
    //  Prefix request with protocol frames
    //  Frame 1: empty frame (delimiter)
    //  Frame 2: "SRWO0XY" (six bytes, SR/Worker x.y)
    //  Frame 3: Service name (printable string)
    //  Frame 4..n: Commands Or Objects

    Messages msg;

    QString cmd= QString("%1-%2").arg(command).arg(cval);
    msg.push(cmd);
    msg.push("127.0.0.1%" + d->name);
    msg.push(SRWOHD);
    msg.push("");
    msg.send(*d->worker);
}

