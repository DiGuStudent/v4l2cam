#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("v4l2cam");
    app.setOrganizationName("v4l2cam");
    app.setApplicationDisplayName(QObject::tr("V4L2 采集卡查看器"));

    MainWindow w;
    w.show();
    return app.exec();
}
