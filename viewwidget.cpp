#include "viewwidget.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>

ViewWidget::ViewWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(160, 120);
    setMouseTracking(true);          // 悬停也能更新手型,提示可拖拽
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    setFocusPolicy(Qt::ClickFocus);
}

void ViewWidget::setFrame(const QImage &img)
{
    QMutexLocker lock(&m_mutex);
    m_img = img;
}

void ViewWidget::clear()
{
    QMutexLocker lock(&m_mutex);
    m_img = QImage();
    m_zoom = 1.0;
    m_pan = QPointF(0, 0);
}

bool ViewWidget::saveDisplayedPng(const QString &path)
{
    QMutexLocker lock(&m_mutex);
    return !m_img.isNull() && m_img.save(path, "PNG");
}

// ---- 以下私有助手均假定调用方已持有 m_mutex ----

double ViewWidget::fitScaleLocked() const
{
    if (m_img.isNull())
        return 1.0;
    const QSizeF isz = m_img.size();
    const QSizeF wsz = size();
    if (isz.width() <= 0 || isz.height() <= 0)
        return 1.0;
    return qMin(wsz.width() / isz.width(), wsz.height() / isz.height());
}

void ViewWidget::clampPanLocked(QSizeF drawn)
{
    // 允许自由拖拽:画面至少留 24px 可见(任意缩放、含适合窗口时都能按住拖动)
    const double w = width(), h = height();
    const double dx = drawn.width(), dy = drawn.height();
    const double margin = 24.0;
    m_pan.setX(qBound(margin - dx, m_pan.x(), w - margin));
    m_pan.setY(qBound(margin - dy, m_pan.y(), h - margin));
}

void ViewWidget::applyZoomLocked(const QPointF &anchor, double newZoom)
{
    if (m_img.isNull())
        return;
    newZoom = qBound(1.0, newZoom, 64.0);
    const double sfit = fitScaleLocked();
    const double oldEff = sfit * m_zoom;
    const double newEff = sfit * newZoom;
    if (oldEff <= 0)
        return;
    // anchor 所指的像素点保持不动
    const QPointF ip = (anchor - m_pan) / oldEff;
    m_pan = anchor - ip * newEff;
    m_zoom = newZoom;
    clampPanLocked(QSizeF(m_img.width() * newEff, m_img.height() * newEff));
}

// ---- 事件与公开接口 ----

void ViewWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    QMutexLocker lock(&m_mutex);
    if (m_img.isNull()) {
        p.setPen(Qt::gray);
        p.drawText(rect(), Qt::AlignCenter, tr("未开始采集 - 点工具栏「开始」"));
        return;
    }

    const QSizeF isz = m_img.size();
    const double eff = fitScaleLocked() * m_zoom;
    const QSizeF drawn = QSizeF(isz.width() * eff, isz.height() * eff);

    clampPanLocked(drawn);

    p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom > 1.5);
    p.drawImage(QRectF(m_pan, drawn), m_img);
}

void ViewWidget::resizeEvent(QResizeEvent *)
{
    QMutexLocker lock(&m_mutex);
    if (!m_img.isNull())
        clampPanLocked(QSizeF(m_img.width() * fitScaleLocked() * m_zoom,
                              m_img.height() * fitScaleLocked() * m_zoom));
    QWidget::resizeEvent(nullptr);
    update();
}

void ViewWidget::wheelEvent(QWheelEvent *e)
{
    const double f = qPow(1.15, e->angleDelta().y() / 120.0);
    {
        QMutexLocker lock(&m_mutex);
        applyZoomLocked(e->position(), m_zoom * f);
    }
    emit zoomChanged(m_zoom);
    update();
}

void ViewWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragLast = e->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void ViewWidget::mouseMoveEvent(QMouseEvent *e)
{
    bool hasImg;
    {
        QMutexLocker lock(&m_mutex);
        hasImg = !m_img.isNull();
    }
    if (!m_dragging) {
        setCursor(hasImg ? Qt::OpenHandCursor : Qt::ArrowCursor);   // 悬停提示可拖拽
        return;
    }
    const QPoint delta = e->pos() - m_dragLast;
    m_dragLast = e->pos();
    {
        QMutexLocker lock(&m_mutex);
        m_pan += delta;
        if (hasImg)
            clampPanLocked(QSizeF(m_img.width() * fitScaleLocked() * m_zoom,
                                  m_img.height() * fitScaleLocked() * m_zoom));
    }
    update();
}

void ViewWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
    }
}

void ViewWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        fitToWindow();          // 双击 = 复原到适合窗口
}

void ViewWidget::fitToWindow()
{
    {
        QMutexLocker lock(&m_mutex);
        applyZoomLocked(QPointF(rect().center()), 1.0);
        // 强制整幅居中:不受之前拖拽残留偏移影响
        if (!m_img.isNull()) {
            const double eff = fitScaleLocked();          // m_zoom=1 → 适合窗口
            const double dx = m_img.width() * eff, dy = m_img.height() * eff;
            m_pan.setX((width() - dx) / 2.0);
            m_pan.setY((height() - dy) / 2.0);
        }
    }
    emit zoomChanged(m_zoom);
    update();
}

void ViewWidget::zoom100()
{
    {
        QMutexLocker lock(&m_mutex);
        const double sfit = fitScaleLocked();
        if (sfit > 0 && !m_img.isNull()) {
            m_zoom = 1.0 / sfit;                                   // eff = 实际像素 1:1
            const double dx = m_img.width(), dy = m_img.height();  // eff=1 → 绘制尺寸=原始尺寸
            m_pan.setX((width() - dx) / 2.0);                      // 视图中心对准画面中心
            m_pan.setY((height() - dy) / 2.0);
        }
    }
    emit zoomChanged(m_zoom);
    update();
}

void ViewWidget::zoomIn()
{
    {
        QMutexLocker lock(&m_mutex);
        applyZoomLocked(QPointF(rect().center()), m_zoom * 1.4);
    }
    emit zoomChanged(m_zoom);
    update();
}

void ViewWidget::zoomOut()
{
    {
        QMutexLocker lock(&m_mutex);
        applyZoomLocked(QPointF(rect().center()), m_zoom / 1.4);
    }
    emit zoomChanged(m_zoom);
    update();
}

double ViewWidget::zoomFactor() const
{
    QMutexLocker lock(&m_mutex);
    return m_zoom;
}
