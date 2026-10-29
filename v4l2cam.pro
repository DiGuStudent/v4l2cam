QT       += core gui widgets
CONFIG   += c++17
TARGET   = v4l2cam
TEMPLATE = app

INCLUDEPATH += src

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/capturethread.cpp \
    src/viewwidget.cpp

HEADERS += \
    src/mainwindow.h \
    src/capturethread.h \
    src/viewwidget.h

# libjpeg: 用于 MJPG 格式解码
LIBS += -ljpeg
# libv4l2: 与 OBS v4l2 插件同路径采集(裸 ioctl 对 MS2109 颜色错)
LIBS += -l:libv4l2.so.0

# ---- make install ----
isEmpty(PREFIX): PREFIX = /usr/local

target.path = $$PREFIX/bin

desktop.path = $$PREFIX/share/applications
desktop.files = packaging/v4l2cam.desktop

icon.path = $$PREFIX/share/icons/hicolor/256x256/apps
icon.files = assets/icons/v4l2cam.png

INSTALLS += target desktop icon
