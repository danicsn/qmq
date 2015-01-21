#-------------------------------------------------
#
# Project created by QtCreator 2014-12-13T16:18:07
#
#-------------------------------------------------

QT       += core
QT       -= gui

TARGET = test
CONFIG   += console
CONFIG   -= app_bundle

DESTDIR = ../lib
OBJECTS_DIR = tmp
MOC_DIR = tmp

TEMPLATE = app


SOURCES += main.cpp

INCLUDEPATH += ../include

CONFIG += debug_and_release

LIBS += -L../lib
CONFIG(debug, debug|release) {
    mac: LIBS += -lqmq_debug
    win32: LIBS += qmqd.lib
    unix: LIBS += -lqmqd
}

CONFIG(release, debug|release) {
    mac: { LIBS += -lqmq }
    win32: { LIBS += qmq.lib }
    unix: { LIBS += -lqmq }
}

HEADERS +=
