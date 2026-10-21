QT       += core gui widgets
CONFIG   += c++17
TARGET   = v4l2cam
TEMPLATE = app

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    capturethread.cpp \
    viewwidget.cpp

HEADERS += \
    mainwindow.h \
    capturethread.h \
    viewwidget.h

# libjpeg: 用于 MJPG 格式解码
LIBS += -ljpeg
