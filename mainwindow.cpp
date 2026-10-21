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
#include <QSettings>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    m_thread = new CaptureThread(this);
    connect(m_thread, &CaptureThread::frameReady, this, &MainWindow::onFrame);
    connect(m_thread, &CaptureThread::errorOccurred, this, &MainWindow::onError);
    connect(m_thread, &CaptureThread::stats, this, &MainWindow::onStats);

    buildUi();
    repopulateFormats();
    repopulateSizes();
    fillColorBox();

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

    QToolBar *tb = addToolBar(tr("主工具栏"));
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

    tb->addWidget(new QLabel(tr(" 颜色 ")));
    m_colorBox = new QComboBox(tb);
    m_colorBox->setToolTip(tr("YUYV 字节序与颜色矩阵。画面颜色不对时在静止画面上逐个试,\n选到正确的那项自动记住。"));
    tb->addWidget(m_colorBox);
    connect(m_colorBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { onColorChanged(); });

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
    connect(fsBtn, &QToolButton::toggled, this, [this](bool on) {
        if (on) showFullScreen();
        else    showNormal();
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
        if (fmts[i].fourcc == V4L2_PIX_FMT_YUYV)
            prefer = int(i);                // 优先 YUYV(422): 低延迟高码率
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

void MainWindow::fillColorBox()
{
    static const char *lay[] = { "YUYV", "YVYU", "UYVY", "VYUY" };
    static const char *mat[] = { "BT.601全", "BT.601窄", "BT.709全", "BT.709窄(=OBS)" };
    QSettings s;
    const int savedLayout = s.value("decode/layout", 0).toInt();
    const int savedMatrix = s.value("decode/matrix", 3).toInt();   // 默认同 OBS: 709 窄

    m_colorBox->blockSignals(true);
    for (int l = 0; l < 4; ++l)
        for (int m = 0; m < 4; ++m)
            m_colorBox->addItem(QString("%1 · %2").arg(lay[l]).arg(mat[m]),
                                l * 4 + m);
    const int idx = qBound(0, savedLayout * 4 + savedMatrix, m_colorBox->count() - 1);
    m_colorBox->setCurrentIndex(idx);
    m_colorBox->blockSignals(false);

    m_thread->setDecode(savedLayout, savedMatrix);
}

void MainWindow::onColorChanged()
{
    const int v = m_colorBox->currentData().toInt();
    const int layout = v / 4, matrix = v % 4;
    m_thread->setDecode(layout, matrix);
    QSettings s;
    s.setValue("decode/layout", layout);
    s.setValue("decode/matrix", matrix);
    statusBar()->showMessage(tr("颜色解析: ") + m_colorBox->currentText(), 2000);
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
