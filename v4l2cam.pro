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
# libv4l2: 与 OBS v4l2 插件同路径采集(裸 ioctl 对 MS2109 颜色错)
LIBS += -l:libv4l2.so.0
