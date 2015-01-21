#include "helper.h"

#undef min
#undef max

#include <QTime>
#include <QDebug>
#include <QThread>
#include <QtConcurrent>

class MdpClientPrivate {
public:
    MdpClientPrivate(Context *ctx) {
        timeout = 2500;
        verbose = false;
        if(ctx)
            client = ctx->createSocket(ZMQ_DEALER);
        else client = srnet->createSocket(ZMQ_DEALER);
    }
    ~MdpClientPrivate() {
        delete client;
    }

    Socket* client;
    QString broker;
    bool verbose;
    int timeout;
};

MdpClient::MdpClient(Context *ctx, QObject *parent) : d_ptr(new MdpClientPrivate(ctx)) ,QObject(parent)
{}

MdpClient::~MdpClient()
{
    delete d_ptr;
}

void MdpClient::setBrokerAddress(const QString &broker)
{
    Q_D(MdpClient);
    d->broker = broker;
}

QString MdpClient::brokerAddress() const
{
    Q_D(const MdpClient);
    return d->broker;
}

Socket *MdpClient::socket()
{
    Q_D(MdpClient);
    return d->client;
}

void MdpClient::setTimeOut(int ms)
{
    Q_D(MdpClient);
    d->timeout = ms;
}

void MdpClient::setVerbose(bool v)
{
    Q_D(MdpClient);
    d->verbose = v;
}

bool MdpClient::connectToBroker()
{
    Q_D(MdpClient);
    if(d->client)
    {
        // for reconnect
        Context* c = d->client->context();
        c->closeSocket(d->client);
        delete d->client;
        d->client = c->createSocket(ZMQ_DEALER);

        int rc = d->client->attach(d->broker.toLocal8Bit().data(), false);
        if(d->verbose)
            qDebug() << "I: connecting to broker at " << d->broker << " ....";
        return rc == 0;
    }

    return false;
}

bool MdpClient::send(const QString &service, Messages *request)
{
    if(!request) return false;
    Q_D(MdpClient);

    //  Prefix request with protocol frames
    //  Frame 1: empty frame (delimiter)
    //  Frame 2: "QMDPCxy" (six bytes, MDP/Client x.y)
    //  Frame 3: Service name (printable string)
    request->push(service);
    request->push(QMDPC);
    request->push("");
    if (d->verbose) {
        qDebug("I: send request to '%s' service:", service.toLatin1().data());
    }
    return request->send(*d->client) == 0;
}

void MdpClient::testRun()
{
    int count;
    for(count = 0; count < 100; count++)
    {
        Messages req;
        req.push("Hello world!");
        send("echo", &req);
        QString cmd, srv;
        req = recv(cmd, srv);
        if(req.size() <=0) break;
    }
}

Messages MdpClient::recv(QString &command, QString &service)
{
    Q_D(MdpClient);
    Messages msgs;
    msgs.recv(*(d->client));
    if(msgs.size() < 5) return msgs;

    if(d->verbose)
        qDebug() << "I: received reply:\n" << msgs.toStringList();

    //  Message format:
    //  Frame 1: empty frame (delimiter)
    //  Frame 2: "QMDPCxy" (six bytes, MDP/Client x.y)
    //  Frame 3: REPORT|NAK
    //  Frame 4: Service name (printable string)
    //  Frame 5..n: Application frames

    QString emp = msgs.popstr();
    assert(emp == "");

    QString header = msgs.popstr();
    assert(header == QMDPC);

    command = msgs.popstr();

    assert(command == MDPC_REPORT || command == MDPC_NAK);

    service = msgs.popstr();
    return msgs;
}

void MdpClient::startTest()
{
    QtConcurrent::run(this, &MdpClient::testRun);
}


/*
 * Worker implementation
 *
 * **********************************************/

class MdpWorkerPrivate {
public:
    MdpWorkerPrivate(Context* ctx) {
        heartbeat = 2500;
        reconnect = 2500;
        verbose = false;
        if(ctx)
            worker = ctx->createSocket(ZMQ_DEALER);
        else worker = srnet->createSocket(ZMQ_DEALER);
    }
    ~MdpWorkerPrivate() {
        delete worker;
    }

    Socket* worker;
    QString broker;
    QString service;
    bool verbose;

    //  Heartbeat management
    QTime heartbeat_at;      //  When to send HEARTBEAT
    size_t liveness;            //  How many attempts left
    int heartbeat;              //  Heartbeat delay, msecs
    int reconnect;              //  Reconnect delay, msecs
};

MdpWorker::MdpWorker(Context *cntx, QObject *parent) : d_ptr(new MdpWorkerPrivate(cntx)), QObject(parent)
{}

MdpWorker::~MdpWorker()
{
    Q_D(MdpWorker);
    sendToBroker(MDPW_DISCONNECT, d->service);

    delete d_ptr;
}

void MdpWorker::setBrokerAddress(const QString &broker)
{
    Q_D(MdpWorker);
    d->broker = broker;
}

QString MdpWorker::brokerAddress() const
{
    Q_D(const MdpWorker);
    return d->broker;
}

void MdpWorker::setServiceName(const QString &name)
{
    Q_D(MdpWorker);
    d->service = name;
}

QString MdpWorker::serviceName() const
{
    Q_D(const MdpWorker);
    return d->service;
}

Socket *MdpWorker::socket()
{
    Q_D(MdpWorker);
    return d->worker;
}

void MdpWorker::setHeartbitInterval(int ms)
{
    Q_D(MdpWorker);
    d->heartbeat = ms;
}

int MdpWorker::hinterval() const
{
    Q_D(const MdpWorker);
    return d->heartbeat;
}

void MdpWorker::setConnectInterval(int ms)
{
    Q_D(MdpWorker);
    d->reconnect = ms;
}

int MdpWorker::reconnectInterval() const
{
    Q_D(const MdpWorker);
    return d->reconnect;
}

int MdpWorker::liveness() const
{
    Q_D(const MdpWorker);
    return d->liveness;
}

void MdpWorker::setVerbose(bool v)
{
    Q_D(MdpWorker);
    d->verbose = v;
}

bool MdpWorker::connectToBroker()
{
    Q_D(MdpWorker);

    if(d->worker)
    {
        Context* c = d->worker->context();
        delete d->worker;
        d->worker = c->createSocket(ZMQ_DEALER);
        d->worker->attach(d->broker.toLocal8Bit().data(), false);
        if(d->verbose)
            qDebug() << "I: connecting to broker at " << d->broker << " ....";

        sendToBroker(MDPW_READY, d->service, NULL);
        d->liveness = HEARTBEAT_LIVENESS;
        d->heartbeat_at = QTime::currentTime().addMSecs(d->heartbeat);
    }

    return false;
}

//  ---------------------------------------------------------------------
//  Send message to broker
//  If no msg is provided, creates one internally

bool MdpWorker::sendToBroker(const QString &command, const QString &option, Messages *msgs)
{
    Q_D(MdpWorker);
    msgs = msgs ? msgs : new Messages;
    if(!option.isNull())
        msgs->push(option);

    msgs->push(command);
    msgs->push(QMDPW);
    msgs->push("");

    int rc = msgs->send(*d->worker);
    if(d->verbose && rc == 0)
        qDebug("I: sending %s to broker",
                    mdpw_commands [command.toInt()]);

    return rc == 0;
}

bool MdpWorker::sendToClient(const QString &reply, Messages *report)
{
    if(!report || reply.isNull()) return false;

    Messages rep_p(*report);
    rep_p.wrap(new Frame(reply.toLocal8Bit().data()));
    return sendToBroker(MDPW_REPORT, NULL, &rep_p);
}

void MdpWorker::testRun()
{
    while(true) {
        QString rep_t;
        Messages msg;
        recv(rep_t, msg);
        if(msg.size() <= 0)
            break;

        sendToClient(rep_t, &msg);
    }
}

void MdpWorker::recv(QString &replyAdd, Messages &rmsg)
{
    Q_D(MdpWorker);
    while(true) {
        zmq_pollitem_t items[] = { { d->worker->resolve(), 0, ZMQ_POLLIN, 0 } };
        int rc = zmq_poll(items, 1, d->heartbeat);
        if (rc == -1)
            break;              //  Interrupted

        if (items[0].revents & ZMQ_POLLIN) {
            Messages msg;
            msg.recv(*d->worker);
            if (msg.size() == 0)
                break;          //  Interrupted
            if (d->verbose) {
               qDebug("I: received message from broker:");
               qDebug() << msg.toStringList();
            }
            d->liveness = HEARTBEAT_LIVENESS;

            //  Don't try to handle errors, just assert noisily
            assert (msg.size() >= 3);

            QString empty = msg.popstr();
            assert (empty == "");

            QString header = msg.popstr();
            assert (header ==QMDPW);

            QString command = msg.popstr();
            if (command == MDPW_REQUEST) {
                //  We should pop and save as many addresses as there are
                //  up to a null part, but for now, just save one...
                Frame *reply_to = msg.unwrap();
                //emit replyTo(*reply);
                replyAdd = reply_to->toString();
                delete reply_to;

                //  Here is where we actually have a message to process; we
                //  return it to the caller application
                rmsg = msg;     //  We have a request to process
            }
            else
            if (command == MDPW_HEARTBEAT)
                ;               //  Do nothing for heartbeats
            else
            if (command == MDPW_DISCONNECT)
                connectToBroker();
            else {
                qDebug("E: invalid input message");
            }
        }
        else
        if (--d->liveness == 0) {
            if (d->verbose)
                qDebug("W: disconnected from broker - retrying...");
            QThread::msleep(d->reconnect);
            connectToBroker();
        }
        //  Send HEARTBEAT if it's time
        if (QTime::currentTime() > d->heartbeat_at) {
            sendToBroker(MDPW_HEARTBEAT, NULL, NULL);
            d->heartbeat_at = QTime::currentTime().addMSecs(d->heartbeat);
        }
        // end while
    }
}

void MdpWorker::startTest()
{
    QtConcurrent::run(this, &MdpWorker::testRun);
}


/*
 * Broker implementation
 *
 * ************************************************/

class Worker_t;
class Service_t {
public:
    Service_t(MdpBrokerPrivate* b)
    { broker = b; }

    void dispatch();
    void enableCommand(const QString& cmd);
    void disableCommand(const QString& cmd);
    bool isCommandEnabled(const QString& cmd);

    QString name;
    MdpBrokerPrivate* broker;

    QList<Messages*> requests;
    QList<Worker_t*> waiting;
    QStringList blacklisted;
};

class Worker_t {
public:
    Worker_t(MdpBrokerPrivate* b) { broker = b; }

    void remove(bool disconnect);
    bool send(const QString& command, const QString& option, Messages* msgs);

    MdpBrokerPrivate* broker;
    QString identity;           //  Identity of worker
    QString address;            //  Address frame to route to
    Service_t *service;         //  Owning service, if known
    QTime expiry;             //  Expires at unless heartbeat
};

/*
 *
 * */

class MdpBrokerPrivate {
public:
    MdpBrokerPrivate(Context* ctx) {
        if(ctx)
            broker = ctx->createSocket(ZMQ_ROUTER);
        else broker = srnet->createSocket(ZMQ_ROUTER);

        verbose = false;
        heartbeat_at = QTime::currentTime().addMSecs(HEARTBEAT_INTERVAL);
    }
    ~MdpBrokerPrivate() {
        delete broker;
        foreach (Service_t* s, ls_services.values()) {
            delete s;
        }

        foreach (Worker_t* w, ls_workers) {
            delete w;
        }
    }
    Service_t* requireService(const QString& name);
    Worker_t* requireWorker(Frame *ident);

    void workerMessage(Frame* sender, Messages* msgs);
    //  Process a request coming from a client. We implement MMI requests
    //  directly here (at present, we implement only the mmi.service request)

    void clientMessage(Frame *sender, Messages *msg);

    //  The purge method deletes any idle workers that haven't pinged us in a
    //  while. We hold workers from oldest to most recent, so we can stop
    //  scanning whenever we find a live worker. This means we'll mainly stop
    //  at the first worker, which is essential when we have large numbers of
    //  workers (since we call this method in our critical path)
    void purge();

    Socket* broker;
    QHash<QString, Service_t*> ls_services;
    QHash<QString, Worker_t*> ls_workers;
    QList<Worker_t*> ls_waitings;
    QTime heartbeat_at;
    bool verbose;
};

// service

void Service_t::dispatch()
{
    broker->purge();
    if(waiting.size() == 0) return;

    while(requests.size() > 0) {
        Worker_t* worker = waiting.takeFirst();
        waiting.removeOne(worker);
        Messages* msg = requests.takeFirst();
        worker->send(MDPW_REQUEST, NULL, msg);
        //  Workers are scheduled in the round-robin fashion
        waiting.append(worker);
        delete msg;
    }
}

void Service_t::enableCommand(const QString &cmd)
{
    blacklisted.removeOne(cmd);
}

void Service_t::disableCommand(const QString &cmd)
{
    if(blacklisted.contains(cmd)) return;
    blacklisted.append(cmd);
}

bool Service_t::isCommandEnabled(const QString &cmd)
{
    return !blacklisted.contains(cmd);
}

// worker

void Worker_t::remove(bool disconnect)
{
    if(disconnect)
        send(MDPW_DISCONNECT, NULL, NULL);

    if(service)
        service->waiting.removeOne(this);

    broker->ls_waitings.removeOne(this);
    broker->ls_workers.remove(identity);
    delete this;
}

bool Worker_t::send(const QString &command, const QString &option, Messages *msgs)
{
    msgs = msgs ? msgs : new Messages;
    if(!option.isNull())
        msgs->push(option);

    msgs->push(command);
    msgs->push(QMDPW);
    msgs->wrap(new Frame(address.toLocal8Bit()));

    int rc = msgs->send(*broker->broker);
    if(rc == 0)
        qDebug("I: sending %s to broker",
                mdpw_commands [command.toInt()]);

    return rc == 0;
}

Service_t* MdpBrokerPrivate::requireService(const QString &name) {
    Service_t* service = ls_services.value(name, 0);
    if(!service)
    {
        service = new Service_t(this);
        service->name = name;
        ls_services.insert(name, service);
    }

    return service;
}

Worker_t *MdpBrokerPrivate::requireWorker(Frame *ident)
{
    Worker_t* worker = ls_workers.value(ident->hexString(), 0);
    if(!worker)
    {
        worker = new Worker_t(this);
        worker->identity = ident->hexString();
        worker->address = ident->toString();
        ls_workers.insert(worker->identity, worker);
        if(verbose)
            qDebug() << "I: registering new worker: " << worker->identity;
    }
    return worker;
}

/*
 *
 * *****/

void MdpBrokerPrivate::workerMessage(Frame *sender, Messages *msgs)
{
    assert(msgs->size() >= 1);

    QString command = msgs->popstr();
    QString ident = sender->hexString();
    bool worker_ready = ls_workers.value(ident, 0) != 0;
    Worker_t* worker = requireWorker(sender);

    if(command == MDPW_READY) {
        if(worker_ready)
            delete worker;
        else if(sender->size() >= 4  //  Reserved service name
                && memcmp (sender->data(), "mmi.", 4) == 0)
            worker->remove(true);
        else
        {
            QString service_frame = msgs->popstr();
            worker->service = requireService(service_frame);
            ls_waitings.append(worker);
            worker->service->waiting.append(worker);

            worker->expiry = QTime::currentTime().addMSecs(HEARTBEAT_EXPIRY);
            worker->service->dispatch();
            qDebug("worker created");
        }
    }
    else
    if (command == MDPW_REPORT) {
        if (worker_ready) {
            //  Remove & save client return envelope and insert the
            //  protocol header and service name, then rewrap envelope.
            Frame *client = msgs->unwrap();
            msgs->push(worker->service->name);
            msgs->push(MDPC_REPORT);
            msgs->push(QMDPC);
            msgs->wrap(client);
            msgs->send(*broker);
        }
        else
            worker->remove(true);
    }
    else
    if (command == MDPW_HEARTBEAT) {
        if (worker_ready) {
            if (ls_waitings.size() > 1) {
                // Move worker to the end of the waiting queue,
                // so purge will only check old worker(s)
                ls_waitings.removeOne(worker);
                ls_waitings.append(worker);
            }
            worker->expiry = QTime::currentTime().addMSecs(HEARTBEAT_EXPIRY);
        }
        else
            worker->remove(true);
    }
    else
    if (command == MDPW_DISCONNECT)
        worker->remove(false);
    else {
        qDebug ("E: invalid input message");
        qDebug() << msgs->toStringList();
    }
}

void MdpBrokerPrivate::clientMessage(Frame *sender, Messages *msg)
{
    assert (msg->size() >= 2);     //  Service name + body

    QString service_frame = msg->popstr();
    Service_t *service = requireService(service_frame);

    //  If we got a MMI service request, process that internally
    if (service_frame.size() >= 4 && service_frame.startsWith("mmi.")) {
        QString return_code;
        if (service_frame == "mmi.service") {
            QString name = msg->lastStr();
            Service_t *service = ls_services.value(name, 0);
            return_code = service && service->waiting.size() ? "200": "404";
        }
        else
            // The filter service that can be used to manipulate
            // the command filter table.
            if (service_frame == "mmi.filter"
                    && msg->size() == 3) {
                QString operation = msg->popstr();
                QString service_frame = msg->popstr();
                QString command_frame = msg->popstr();

                if (operation == "enable") {
                    Service_t *service = requireService(service_frame);
                    service->enableCommand(command_frame);
                    return_code = "200";
                }
                else
                    if (operation == "disable") {
                        Service_t *service = requireService(service_frame);
                        service->disableCommand(command_frame);
                        return_code = "200";
                    }
                    else
                        return_code = "400";

                //  Add an empty frame; it will be replaced by the return code.
                msg->push("");
            }
            else
                return_code = "501";

        msg->last()->reset(return_code.toLocal8Bit().data(), strlen (return_code.toLocal8Bit().data()));

        //  Insert the protocol header and service name, then rewrap envelope.
        msg->push(service_frame);
        msg->push(MDPC_REPORT);
        msg->push(QMDPC);
        msg->wrap(new Frame(*sender));
        msg->send(*broker);
    }
    else {
        bool enabled = true;
        if (msg->size() >= 1) {
            QString cmd_frame = msg->firstStr();
            enabled = service->isCommandEnabled(cmd_frame);
        }

        //  Forward the message to the worker.
        if (enabled) {
            msg->wrap(new Frame(*sender));
            service->requests.append(msg);
            service->dispatch();
        }
        //  Send a NAK message back to the client.
        else {
            msg->push(service_frame);
            msg->push(MDPC_NAK);
            msg->push(QMDPC);
            msg->wrap(new Frame(*sender));
            msg->send(*broker);
        }
    }
}

void MdpBrokerPrivate::purge()
{
    foreach (Worker_t* w, ls_workers) {
        if (QTime::currentTime() < w->expiry)
            break;                  //  Worker is alive, we're done here
        if (verbose)
            qDebug() << "I: deleting expired worker: " << w->identity;

        w->remove(false);
    }
}

MdpBroker::MdpBroker(Context *cntx, QObject *parent) : d_ptr(new MdpBrokerPrivate(cntx)), QObject(parent)
{ terminated = true; }

MdpBroker::~MdpBroker()
{
    terminated = true;

    delete d_ptr;
}

int MdpBroker::bind(const char *endpoint)
{
    Q_D(MdpBroker);
    return d->broker->bind("%s", endpoint);
}

void MdpBroker::start()
{
    terminated = false;
    QtConcurrent::run(this, &MdpBroker::run);
}

void MdpBroker::stop()
{
    terminated = true;
}

void MdpBroker::test()
{
    MdpBroker broker(srnet);
    broker.bind("tcp://*:5555");
    broker.start();


    MdpClient client1(srnet);
    client1.setVerbose(true);
    client1.setBrokerAddress("tcp://127.0.0.1:5555");
    client1.connectToBroker();

    MdpWorker worker(srnet);
    worker.setVerbose(true);
    worker.setBrokerAddress("tcp://127.0.0.1:5555");
    worker.setServiceName("echo");
    worker.connectToBroker();

    client1.startTest();
    worker.startTest();
}

void MdpBroker::run()
{
    Q_D(MdpBroker);
    //  Get and process messages forever or until interrupted
    while (!terminated) {
        zmq_pollitem_t items [] = {
            { d->broker->resolve(),  0, ZMQ_POLLIN, 0 } };
        int rc = zmq_poll (items, 1, HEARTBEAT_INTERVAL);
        if (rc == -1)
            break;              //  Interrupted

        //  Process next input message, if any
        if (items [0].revents & ZMQ_POLLIN) {
            Messages* msgs = new Messages;
            msgs->recv(*d->broker);
            if (d->verbose) {
                qDebug("I: received message:\n");
                qDebug() << msgs->toStringList();
            }
            Frame *sender = msgs->pop();
            QString empty  = msgs->popstr();
            QString header = msgs->popstr();

            if (header == QMDPC)
                d->clientMessage(sender, msgs);
            if (header == QMDPW)
                d->workerMessage(sender, msgs);
            else {
                qDebug("E: invalid message:");
                delete msgs;
            }
            delete sender;
        }
        //  Disconnect and delete any expired workers
        //  Send heartbeats to idle workers if needed
        if (QTime::currentTime() > d->heartbeat_at) {
            d->purge();
            foreach (Worker_t *worker, d->ls_waitings) {
                worker->send(MDPW_HEARTBEAT, NULL, NULL);
            }
            d->heartbeat_at = QTime::currentTime().addMSecs(HEARTBEAT_INTERVAL);
        }
    }
}





