# Qmq - Qmq is almost a Qt implementation of czmq library

ZMQ is simple and good implemented, i think use it directly in Qt projects is better than using binding libraries, but for higher level implementations library like czmq, majardomo and zyre, binding them in Qt application without using Qt event loops doesn't feel right, so i decide implement a Qt-ish interface for these libraries.

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
