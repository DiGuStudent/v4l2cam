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
    void onStalled(double seconds);
    void restartIfStreaming();
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
    QToolBar *m_toolbar = nullptr;

    QComboBox *m_deviceBox = nullptr;
    QComboBox *m_formatBox = nullptr;
    QComboBox *m_sizeBox = nullptr;
    QToolButton *m_startBtn = nullptr;
    QLabel *m_statLabel = nullptr;

    bool m_streaming = false;
    int m_restartCount = 0;         // 自动重连计数(成功收帧后清零)
    bool m_restarting = false;      // 重连进行中,防止叠加
    qint64 m_lastFrameAt = 0;       // 最近一次收帧时刻(看门狗判停滞)
    qint64 m_lastRestartAt = 0;     // 最近一次重连时刻(冷却用)

    void diag(const QString &msg);  // 追加诊断日志 ~/v4l2cam_snapshots/diag.log
};

#endif // MAINWINDOW_H
