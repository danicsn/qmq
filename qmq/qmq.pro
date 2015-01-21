#-------------------------------------------------
#
# Project created by QtCreator 2014-12-14T08:57:37
#
#-------------------------------------------------

QT += core network concurrent
QT  -= gui

TARGET = qmq
TEMPLATE = lib

DEFINES += QMQ_LIBRARY _CRT_SECURE_NO_WARNINGS

SOURCES += \
    context.cpp \
    socket.cpp \
    message.cpp \
    helper.cpp \
    actor.cpp \
    poller.cpp \
    beacon.cpp \
    gossip.cpp \
    proxy.cpp \
    mdp.cpp \
    sevent.cpp \
    forwarder.cpp \
    qhub.cpp

HEADERS += qmq.h \
    context.h \
    socket.h \
    helper.h \
    message.h \
    context_p.hpp \
    socket_p.hpp \
    msg_p.hpp \
    actor.h \
    poller.h \
    gossip.h \
    mdp.h \
    sevent_p.h \
    sevent.h \
    hub_p.h \
    qhub.h

unix {
    target.path = /usr/lib
    INSTALLS += target
}

DESTDIR = ../lib
MOC_DIR = tmp
OBJECTS_DIR = tmp

CONFIG += debug_and_release

unix: LIBS += -L/usr/local/lib -lzmq
win32: LIBS += -lzmq Ws2_32.lib

CONFIG(debug, debug|release) {
    mac: TARGET = $$join(TARGET,,,_debug)
    win32: TARGET = $$join(TARGET,,,d)
    unix: TARGET = $$join(TARGET,,,d)
}
