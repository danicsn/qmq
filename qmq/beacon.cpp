#include "helper.h"

#undef min
#undef max

#include <QTime>

#include <QUdpSocket>
#include <QHostAddress>
#include <QNetworkInterface>

class BeaconHandler {
public:
    BeaconHandler(Socket* pip) {
        this->pipe = pip;
        terminated = false;
        verbose = false;
        transmit = 0;
        filter = 0;
    }

    ~BeaconHandler() {
        delete transmit;
        delete filter;
        udpsock.close();
    }

    void prepareUdp(const QString &iface);
    void configureUdp(const QString &iface, int port);

    int handlePipe();
    int handleUdp();

    Socket *pipe;              //  Actor command pipe
    QUdpSocket udpsock;         //  UDP socket for send/recv
    int port_nbr;               //  UDP port number we work on
    int interval;               //  Beacon broadcast interval
    QTime  ping_at;             //  Next broadcast time
    Frame* transmit;          //  Beacon transmit data
    Frame* filter;            //  Beacon filter data
    QHostAddress broadcast;     //  Our broadcast address
    bool terminated;            //  Did caller ask us to quit?
    bool verbose;               //  Verbose logging enabled?
    QString hostname;           //  Saved host name
};

void BeaconHandler::prepareUdp(const QString& iface)
{
    if(udpsock.isOpen())
        udpsock.close();

    hostname = "";
    //  Get the network interface from iface or else use first
    //  broadcast interface defined on system. iface=* means
    //  use INADDR_ANY + INADDR_BROADCAST.

    uint64_t bind_to = 0;
    uint64_t send_to = 0;
    if(iface == "*")
    {
        bind_to = INADDR_ANY;
        send_to = INADDR_BROADCAST;
    }
    else
    {
       QList<QNetworkInterface> ifaces = QNetworkInterface::allInterfaces();
       foreach (QNetworkInterface nf, ifaces) {
           if(nf.name() == iface || iface.isNull() || iface.isEmpty())
           {
               if(nf.flags() & QNetworkInterface::CanBroadcast)
               {
                   foreach (QNetworkAddressEntry na, nf.addressEntries()) {
                       if(na.broadcast().toString() != "") {
                           send_to = na.broadcast().toIPv4Address();
                           bind_to = na.ip().toIPv4Address();
                           hostname = na.ip().toString();
                           if (verbose)
                               qDebug("qbeacon: using address=%s broadcast=%s",
                                          na.ip().toString().toLocal8Bit().data(), na.broadcast().toString().toLocal8Bit().data());
                           break;
                       }
                   }
               }
           }
           if(hostname != "") break;
       }
    }

    if(bind_to)
    {
        broadcast.setAddress(send_to);
#ifdef Q_OS_WIN
        QHostAddress address(bind_to);
#else Q_OS_LINUX
        QHostAddress address(send_to); // dont work on centos Qt 4.7
#endif

        if(!udpsock.bind(address, port_nbr, QUdpSocket::ShareAddress
                         | QUdpSocket::ReuseAddressHint))
            qFatal("bind udp");

        if(verbose)
            qDebug() << "qbeacon: configured, hostname=" << hostname;
    }
}

void BeaconHandler::configureUdp(const QString& iface, int port)
{
    port_nbr = port;
    prepareUdp(iface);
    pipe->sendstr(hostname);
    if(hostname == "")
        qFatal("No broadcast interface found");
}

int BeaconHandler::handlePipe()
{
    QString command = pipe->recvstr();
    if(command.isNull())
        return -1;

    if(verbose)
        qDebug("qbeacon: API command=%s", command.toLocal8Bit().data());

    if(command == "VERBOSE")
        verbose = true;
    else if(command == "CONFIGURE")
    {
        int port;
        int rc = pipe->recv("i", &port);
        assert(rc == 0);
        configureUdp("", port);
    }
    else if(command == "PUBLISH")
    {
        pipe->recv("fi", &transmit, &interval);
        assert(transmit->size() <= 255);
        ping_at = QTime::currentTime();
    }
    else if(command == "SILENCE") {
        delete transmit;
        transmit = 0;
    }
    else if(command == "SUBSCRIBE") {
        pipe->recv("f", &filter);
        assert (filter->size() <= 255);
    }
    else if(command == "UNSUBSCRIBE")
    {
        delete filter;
        filter = 0;
    }
    else if(command == "$TERM") {
        terminated = true;
    }
    else {
        qFatal("qbeacon: - invalid command: %s", command.toLocal8Bit().data());
    }

    return 0;
}

int BeaconHandler::handleUdp()
{
    if(!udpsock.hasPendingDatagrams()) return -1;
    char data[1024];
    QHostAddress peerAddress;

    int rsize = udpsock.readDatagram(data, 1024, &peerAddress);
    if(rsize <= 0) return -1;

    Frame frame(data, rsize);

    bool is_valid = false;

    if(filter)
    {
        const void *filter_data = filter->constData();
        int filter_size = filter->size();
        if (frame.size() >= filter_size
        && memcmp(frame.constData(), filter_data, filter_size) == 0)
            is_valid = true;
    }

    //  If valid, discard our own broadcasts, which UDP echoes to us
    if(is_valid && transmit)
    {
        const void *transmit_data = transmit->constData();
        int transmit_size = transmit->size();
        if (frame.size() == transmit_size
        && memcmp (frame.constData(), transmit_data, transmit_size) == 0)
            is_valid = false;
    }

    //  If still a valid beacon, send on to the API
    if (is_valid) {
        Messages msg;
        msg.append(udpsock.peerName());
        msg.append(frame);
        msg.send(*pipe);
    }

    return 0;
}

void qbeacon(Socket* pipe, void*)
{
    BeaconHandler beacon(pipe);
    pipe->signal(0);

    Poller poll;
    poll.append(pipe);

    while (!beacon.terminated) {

        long timeout = -1;
        if(beacon.transmit)
        {
            timeout = (long)(beacon.ping_at.msecsTo(QTime::currentTime()));
            if(timeout < 0) timeout = 0;
        }

        Socket* sock = (Socket*)poll.wait(timeout);
        if(sock)
            beacon.handlePipe();
        if(beacon.udpsock.isValid() && beacon.udpsock.waitForReadyRead(timeout + 1)) // -1 is not defined
            beacon.handleUdp();

        if(beacon.transmit
                && QTime::currentTime() >= beacon.ping_at)
        {
            //  Send beacon to any listening peers
            if(beacon.udpsock.writeDatagram(beacon.transmit->bdata(), beacon.broadcast, beacon.port_nbr))
                //beacon.prepareUdp("");
                beacon.ping_at = QTime::currentTime().addMSecs(beacon.interval);
        }
    }
}

void beaconTest(bool verbose)
{
    printf (" * qbeacon: ");
    if (verbose)
        printf ("\n");

    //  @selftest
    //  Test 1 - two beacons, one speaking, one listening
    //  Create speaker beacon to broadcast our service
    ActorSocket *speaker = new ActorSocket(qbeacon, NULL, srnet);
    assert (speaker);
    if (verbose)
        speaker->sendx("VERBOSE", NULL);

    speaker->send("si", "CONFIGURE", 9999);
    QString hostname = speaker->recvstr();
    if (hostname.isNull()) {
        qDebug("OK (skipping test, no UDP broadcasting)\n");
        delete speaker;
        return;
    }

    //  Create listener beacon on port 9999 to lookup service
    ActorSocket listener(qbeacon, NULL);
    if (verbose)
        listener.sendx("VERBOSE", NULL);
    listener.send("si", "CONFIGURE", 9999);
    hostname = listener.recvstr();

    //  We will broadcast the magic value 0xCAFE
    byte announcement [2] = { 0xCA, 0xFE };
    speaker->send("sbi", "PUBLISH", announcement, 2, 100);
    //  We will listen to anything (empty subscription)
    listener.send("sb", "SUBSCRIBE", "", 0);

    //  Wait for at most 1/2 second if there's no broadcasting
    listener.setRcvtimeo(500);
    QString ipaddress = listener.recvstr();
    if (!ipaddress.isNull()) {
        Frame content;
        content.recv(listener);
        assert (content.size() == 2);
        QByteArray data = content.bdata();
        assert (data.at(0) == 0xCA);
        assert (data.at(1) == 0xFE);
        speaker->sendx("SILENCE", NULL);
    }
    srnet->closeSocket(&listener);
    delete speaker;

    //  Test subscription filter using a 3-node setup
    ActorSocket node1(qbeacon, NULL, srnet);
    node1.send("si", "CONFIGURE", 5670);
    hostname = node1.recvstr();

    ActorSocket node2(qbeacon, NULL, srnet);
    node2.send("si", "CONFIGURE", 5670);
    hostname = node2.recvstr();

    ActorSocket node3(qbeacon, NULL, srnet);
    node3.send("si", "CONFIGURE", 5670);
    hostname = node3.recvstr();

    node1.send("sbi", "PUBLISH", "NODE/1", 6, 250);
    node2.send("sbi", "PUBLISH", "NODE/2", 6, 250);
    node3.send("sbi", "PUBLISH", "RANDOM", 6, 250);
    node1.send("sb", "SUBSCRIBE", "NODE", 4);

    //  Poll on three API sockets at once
    Poller poller;
    poller.append(&node1); poller.append(&node2); poller.append(&node3);

    QTime stop_at = QTime::currentTime().addSecs(1);
    while (QTime::currentTime() < stop_at) {
        long timeout = (long)stop_at.msecsTo(QTime::currentTime());
        if (timeout < 0)
            timeout = 0;
        SocketBase *which = poller.wait(timeout);
        if (which) {
            assert (which == (SocketBase*)&node1);
            QString ipaddress, received;
            node1.recv("ss", &ipaddress, &received);
            assert (received == "NODE/2");
        }
    }

    //  Stop listening
    node1.sendstr("UNSUBSCRIBE");

    //  Stop all node broadcasts
    node1.sendstr("SILENCE");
    node2.sendstr("SILENCE");
    node3.sendstr("SILENCE");

    //  Destroy the test nodes automatically

    //  @end
    printf ("OK\n");
}
