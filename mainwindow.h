#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class QComboBox;
class QToolButton;
class QLabel;
class ViewWidget;
class CaptureThread;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void keyPressEvent(QKeyEvent *e) override;

private slots:
    void onDeviceChanged();
    void onFormatChanged();
    void startCapture();
    void stopCapture();
    void toggleCapture();
    void onFrame(const QImage &img);
    void onStats(double fps, double mbps, int w, int h, unsigned int fourcc);
    void onError(const QString &msg);
    void onZoomChanged(double factor);

private:
    void buildUi();
    void repopulateFormats();
    void repopulateSizes();
    void setStreamingUi(bool streaming);

    ViewWidget *m_view = nullptr;
    CaptureThread *m_thread = nullptr;

    QComboBox *m_deviceBox = nullptr;
    QComboBox *m_formatBox = nullptr;
    QComboBox *m_sizeBox = nullptr;
    QToolButton *m_startBtn = nullptr;
    QLabel *m_statLabel = nullptr;

    bool m_streaming = false;
};

#endif // MAINWINDOW_H
