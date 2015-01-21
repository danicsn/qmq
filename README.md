# Qmq - Qmq is almost a Qt implementation of czmq library

ZMQ is simple and good implemented and i love use it directly in my Qt projects, but for higher level implementation
i feel converting czmq code to Qt is better than binding it. 

My main goal is adding Higher level implementation of zmq for supporting Peer to peer messaging bus, RPC driver and etc.

implemented these classes:

Frame -- zframe

Messages -- zmsg

SocketBase -- zsocket

Socket -- zsock

ActorSocket -- zactor

Poller -- zpoller

SockEvent -- zloop

qmonitor -- zmonitor

qbeacon -- zbeacon

mdpclient and mdpworker implemented too 

please feel free for contribute and updating code 

#Build and install

Before building you need to: 

    zmq v3.0 library and later 
    Qt v4.5 and later
    qmake qmq.pro
    make

Test on windows using Qt5.0 and visual studio 10: works ok

Test on linux(centos and ubuntu) with Qt4.7: qbeacon has problem in linux yet
