#include "mainwindow.h"
#include "capturethread.h"
#include "viewwidget.h"

#include <QToolBar>
#include <QComboBox>
#include <QToolButton>
#include <QLabel>
#include <QStatusBar>
#include <QMenuBar>
#include <QKeyEvent>
#include <QShortcut>
#include <QMessageBox>
#include <QDir>
#include <QDateTime>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    m_thread = new CaptureThread(this);
    connect(m_thread, &CaptureThread::frameReady, this, &MainWindow::onFrame);
    connect(m_thread, &CaptureThread::errorOccurred, this, &MainWindow::onError);
    connect(m_thread, &CaptureThread::stats, this, &MainWindow::onStats);
    connect(m_thread, &CaptureThread::stalled, this, &MainWindow::onStalled);
    connect(m_thread, &CaptureThread::finished, this, &MainWindow::restartIfStreaming);
    connect(m_thread, &CaptureThread::snapshotSaved, this, [this](const QString &p) {
        statusBar()->showMessage(tr("原始帧已存: ") + p, 6000);
    });

    buildUi();
    repopulateFormats();
    repopulateSizes();

    setWindowTitle(tr("V4L2 采集卡查看器"));
    resize(1100, 720);
    statusBar()->showMessage(tr("就绪 - 选择设备/格式/分辨率后点「开始」"), 4000);
}

MainWindow::~MainWindow()
{
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestStop();
        m_thread->wait(1500);
    }
}

void MainWindow::buildUi()
{
    setCentralWidget(m_view = new ViewWidget(this));
    connect(m_view, &ViewWidget::zoomChanged, this, &MainWindow::onZoomChanged);

    m_toolbar = addToolBar(tr("主工具栏"));
    QToolBar *tb = m_toolbar;
    tb->setMovable(false);
    tb->setIconSize(QSize(16, 16));

    tb->addWidget(new QLabel(tr(" 设备 ")));
    m_deviceBox = new QComboBox(tb);
    m_deviceBox->setMinimumWidth(160);
    tb->addWidget(m_deviceBox);
    connect(m_deviceBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { onDeviceChanged(); });

    tb->addWidget(new QLabel(tr(" 格式 ")));
    m_formatBox = new QComboBox(tb);
    tb->addWidget(m_formatBox);
    connect(m_formatBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { onFormatChanged(); });

    tb->addWidget(new QLabel(tr(" 分辨率 ")));
    m_sizeBox = new QComboBox(tb);
    m_sizeBox->setMinimumWidth(130);
    tb->addWidget(m_sizeBox);

    tb->addSeparator();

    m_startBtn = new QToolButton(tb);
    m_startBtn->setText(tr("开始"));
    m_startBtn->setCheckable(true);
    tb->addWidget(m_startBtn);
    connect(m_startBtn, &QToolButton::clicked, this, &MainWindow::toggleCapture);

    QToolButton *fitBtn = new QToolButton(tb);
    fitBtn->setText(tr("适合窗口"));
    tb->addWidget(fitBtn);
    connect(fitBtn, &QToolButton::clicked, m_view, &ViewWidget::fitToWindow);

    QToolButton *oobBtn = new QToolButton(tb);
    oobBtn->setText(tr("1:1"));
    tb->addWidget(oobBtn);
    connect(oobBtn, &QToolButton::clicked, m_view, &ViewWidget::zoom100);

    QToolButton *fsBtn = new QToolButton(tb);
    fsBtn->setText(tr("全屏"));
    fsBtn->setCheckable(true);
    tb->addWidget(fsBtn);
    // 全屏:只留图像,隐藏工具栏/状态栏,避免遮挡画面;退出再恢复
    connect(fsBtn, &QToolButton::toggled, this, [this](bool on) {
        if (on) {
            m_toolbar->setVisible(false);
            statusBar()->setVisible(false);
            showFullScreen();
        } else {
            m_toolbar->setVisible(true);
            statusBar()->setVisible(true);
            showNormal();
        }
    });

    QToolButton *snapBtn = new QToolButton(tb);
    snapBtn->setText(tr("存帧"));
    snapBtn->setToolTip(tr("将当前一帧原始 YUYV/MJPG 字节流写盘(调色诊断用)"));
    tb->addWidget(snapBtn);
    connect(snapBtn, &QToolButton::clicked, this, [this]() {
        if (!m_thread->isRunning()) {
            statusBar()->showMessage(tr("请先开始采集再存帧"), 2500);
            return;
        }
        const QString dir = QDir::homePath() + "/v4l2cam_snapshots";
        QDir().mkpath(dir);
        m_thread->requestRawSnapshot(dir);
        statusBar()->showMessage(tr("下一帧将存到 ~/v4l2cam_snapshots/"), 2500);
    });

    QToolButton *shotBtn = new QToolButton(tb);
    shotBtn->setText(tr("截图"));
    shotBtn->setToolTip(tr("保存当前界面渲染出的画面 PNG(文件名带解码参数)"));
    tb->addWidget(shotBtn);
    connect(shotBtn, &QToolButton::clicked, this, [this]() {
        const QString dir = QDir::homePath() + "/v4l2cam_snapshots";
        QDir().mkpath(dir);
        const QString path = dir + QString("/shot_%1_YUYV709full.png")
                                 .arg(QDateTime::currentMSecsSinceEpoch());
        if (m_view->saveDisplayedPng(path))
            statusBar()->showMessage(tr("截图已存: ") + path, 6000);
        else
            statusBar()->showMessage(tr("尚无画面可截"), 2500);
    });

    m_statLabel = new QLabel(tr("—"), this);
    statusBar()->addPermanentWidget(m_statLabel);

    // 快捷键
    new QShortcut(QKeySequence(Qt::Key_F11), this, [this, fsBtn]() {
        fsBtn->setChecked(!fsBtn->isChecked());
    });
    new QShortcut(QKeySequence(Qt::Key_Escape), this, [this, fsBtn]() {
        if (isFullScreen()) fsBtn->setChecked(false);
    });
    new QShortcut(QKeySequence(Qt::Key_F), m_view, [this] { m_view->fitToWindow(); });
    new QShortcut(QKeySequence(Qt::Key_1), m_view, [this] { m_view->zoom100(); });
    new QShortcut(QKeySequence(Qt::Key_Plus), m_view, [this] { m_view->zoomIn(); });
    new QShortcut(QKeySequence(Qt::Key_Minus), m_view, [this] { m_view->zoomOut(); });
    new QShortcut(QKeySequence(Qt::Key_Space), this, [this] { toggleCapture(); });

    // 填充设备列表
    const QStringList devs = CaptureThread::devices();
    m_deviceBox->blockSignals(true);
    for (const QString &d : devs)
        m_deviceBox->addItem(d, d);
    // 默认选已有的 video0
    const int idx = devs.indexOf("/dev/video0");
    m_deviceBox->setCurrentIndex(idx >= 0 ? idx : 0);
    m_deviceBox->blockSignals(false);
}

void MainWindow::onDeviceChanged()
{
    repopulateFormats();
    repopulateSizes();
}

void MainWindow::repopulateFormats()
{
    const QString dev = m_deviceBox->currentData().toString();
    m_formatBox->clear();
    const auto fmts = CaptureThread::enumFormats(dev);
    int prefer = -1;
    for (size_t i = 0; i < fmts.size(); ++i) {
        m_formatBox->addItem(
            CaptureThread::fourccToString(fmts[i].fourcc) + tr(" — ") + fmts[i].description,
            fmts[i].fourcc);
        // OBS 截图对照(均值19,136,220):YUYV·709窄 与 OBS YUYV 数值一致 → 默认优先 YUYV
        if (fmts[i].fourcc == V4L2_PIX_FMT_YUYV)
            prefer = int(i);
    }
    if (prefer >= 0)
        m_formatBox->setCurrentIndex(prefer);
    if (fmts.empty())
        m_formatBox->addItem(tr("(无格式)"), 0);
}

void MainWindow::onFormatChanged()
{
    repopulateSizes();
}

void MainWindow::repopulateSizes()
{
    const QString dev = m_deviceBox->currentData().toString();
    const unsigned fcc = m_formatBox->currentData().toUInt();
    m_sizeBox->clear();
    const auto sizes = CaptureThread::enumSizes(dev, fcc);
    int prefer = -1;
    for (size_t i = 0; i < sizes.size(); ++i) {
        m_sizeBox->addItem(QString("%1 × %2").arg(sizes[i].w).arg(sizes[i].h),
                           QSize(sizes[i].w, sizes[i].h));
        if (sizes[i].w == 1920 && sizes[i].h == 1080)
            prefer = int(i);                // 优先 1080p
    }
    if (prefer >= 0)
        m_sizeBox->setCurrentIndex(prefer);
    if (sizes.empty())
        m_sizeBox->addItem(tr("(无分辨率)"), QSize(0, 0));
}

void MainWindow::startCapture()
{
    if (m_thread->isRunning())
        return;
    const QString dev = m_deviceBox->currentData().toString();
    const unsigned fcc = m_formatBox->currentData().toUInt();
    const QSize sz = m_sizeBox->currentData().toSize();
    if (dev.isEmpty() || sz.isEmpty() || sz.width() <= 0) {
        QMessageBox::warning(this, tr("无法启动"), tr("请选择有效的设备/格式/分辨率。"));
        return;
    }
    diag(QString("开始采集 %1 %2 %3x%4").arg(dev).arg(fourcc).arg(sz.width()).arg(sz.height()));
    m_thread->setDevice(dev);
    m_thread->setFourcc(fcc);
    m_thread->setSize(sz.width(), sz.height());
    m_thread->start();
    setStreamingUi(true);
}

void MainWindow::stopCapture()
{
    if (m_thread->isRunning()) {
        m_thread->requestStop();
        m_thread->wait(1500);
    }
    m_restartCount = 0;
    setStreamingUi(false);
    statusBar()->showMessage(tr("已停止"), 2000);
}

void MainWindow::toggleCapture()
{
    if (m_streaming) {
        m_startBtn->setChecked(false);
        stopCapture();
    } else {
        m_startBtn->setChecked(true);
        startCapture();
    }
}

void MainWindow::setStreamingUi(bool streaming)
{
    m_streaming = streaming;
    m_startBtn->setChecked(streaming);
    m_startBtn->setText(streaming ? tr("停止") : tr("开始"));
    m_deviceBox->setEnabled(!streaming);
    m_formatBox->setEnabled(!streaming);
    m_sizeBox->setEnabled(!streaming);
    setWindowTitle(streaming ? tr("V4L2 采集卡查看器 — 采集中…") : tr("V4L2 采集卡查看器"));
}

void MainWindow::onFrame(const QImage &img)
{
    m_view->setFrame(img);
    m_view->update();
    m_lastFrameAt = QDateTime::currentMSecsSinceEpoch();
    if (m_restartCount != 0)
        m_restartCount = 0;              // 又收到帧了,重连计数清零
}

void MainWindow::onStalled(double seconds)
{
    diag(QString("STALL 无新帧 %.0fs").arg(seconds));
    statusBar()->showMessage(tr("画面停滞 %.0fs —— 尝试自动重连…").arg(seconds));
    restartIfStreaming();
}

void MainWindow::restartIfStreaming()
{
    if (!m_streaming || m_restarting)
        return;
    // 冷却 ≥8s,避免连续重连造成图像一直闪烁
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastRestartAt && now - m_lastRestartAt < 8000)
        return;
    if (m_restartCount >= 5) {
        diag("重连达上限(5),放弃自动重连");
        statusBar()->showMessage(tr("已多次自动重连失败,请检查采集卡信号/接线"), 0);
        return;
    }
    m_restarting = true;
    m_lastRestartAt = now;
    ++m_restartCount;
    diag(QString("RESTART 第%1次").arg(m_restartCount));
    QTimer::singleShot(700, this, [this]() {
        m_restarting = false;
        if (!m_streaming) {             // 期间用户手动停了,作罢
            m_restartCount = 0;
            return;
        }
        if (m_thread->isRunning()) {
            m_thread->requestStop();
            m_thread->wait(1500);
        }
        diag("开始新一次采集(自动重连)");
        startCapture();
        statusBar()->showMessage(tr("已自动重连(%1/5)…").arg(m_restartCount), 3000);
    });
}

void MainWindow::diag(const QString &msg)
{
    const QString dir = QDir::homePath() + "/v4l2cam_snapshots";
    QDir().mkpath(dir);
    QFile f(dir + "/diag.log");
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz ")
           << (isFullScreen() ? "[全屏] " : "") << msg << "\n";
    }
}

void MainWindow::onStats(double fps, double mbps, int w, int h, unsigned int fourcc)
{
    m_statLabel->setText(QString("%1  %2x%3  %4 fps   吞吐 %5 MB/s")
                             .arg(CaptureThread::fourccToString(fourcc))
                             .arg(w).arg(h)
                             .arg(fps, 0, 'f', 1)
                             .arg(mbps, 0, 'f', 1));
}

void MainWindow::onError(const QString &msg)
{
    diag("ERROR " + msg);
    if (m_streaming) {
        m_startBtn->setChecked(false);
        setStreamingUi(false);
    }
    QMessageBox::critical(this, tr("采集错误"), msg);
    statusBar()->showMessage(tr("采集错误: ") + msg, 8000);
}

void MainWindow::onZoomChanged(double factor)
{
    statusBar()->showMessage(
        QString(tr("缩放 %1x (%2)  滚轮缩放 · 左键拖拽平移 · F11 全屏"))
            .arg(factor, 0, 'f', 2)
            .arg(factor <= 1.01 ? tr("适合窗口") : tr("放大")),
        1500);
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    QMainWindow::keyPressEvent(e);
}
