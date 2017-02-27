#-------------------------------------------------
#
# Project created by QtCreator 2016-04-06T18:23:32
#
#-------------------------------------------------

QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = vixdisklibsamplegui
TEMPLATE = app


SOURCES += main.cpp\
        vixdisklibsamplegui.cpp \
    worker.cpp \
    sslclient.cpp

HEADERS  += vixdisklibsamplegui.h \
    vm_basic_types.h \
    worker.h \
    sslclient.h

FORMS    += vixdisklibsamplegui.ui \
    advanced.ui

INCLUDEPATH +=  c:/source/boost_1_63_0
LIBS += c:/source/vixdisklibsamplegui/vixdisklibsamplegui\vixDiskLib.lib

LIBS += -Lc:/source/boost_1_63_0/stage/lib \
        -Llibboost_system-vc140-mt-s-1_63

CONFIG += c++11

DISTFILES +=
