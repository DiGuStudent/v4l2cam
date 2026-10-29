#ifndef VIEWWIDGET_H
#define VIEWWIDGET_H

#include <QWidget>
#include <QImage>
#include <QMutex>

// 视频显示控件:
//   - 鼠标滚轮:  以光标为中心缩放 (1x 适合窗口 ~ 64x)
//   - 左键拖拽:  平移画面
//   - 适配窗口 / 1:1 / 全屏 由 MainWindow 按钮或快捷键触发
class ViewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ViewWidget(QWidget *parent = nullptr);

    void setFrame(const QImage &img);   // 采集线程投递,内部加锁
    void clear();
    bool saveDisplayedPng(const QString &path);   // 存当前显示的渲染画面

    void fitToWindow();                 // 缩放=1 (适合窗口)
    void zoom100();                     // 实际像素 1:1
    void zoomIn();
    void zoomOut();

    double zoomFactor() const;          // 相对"适合窗口"的倍率

signals:
    void zoomChanged(double factor);

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
    // 以下 *_Locked 助手不自己加锁,调用方必须先持有 m_mutex
    double fitScaleLocked() const;             // 适合窗口时的缩放系数
    void clampPanLocked(QSizeF drawn);         // 把 pan 限制在合理范围
    void applyZoomLocked(const QPointF &anchor, double newZoom);

    mutable QMutex m_mutex;
    QImage m_img;
    double m_zoom = 1.0;                // 1.0 = 适合窗口
    QPointF m_pan{0, 0};                // 图像左上角像素在控件内的坐标
    bool m_dragging = false;
    QPoint m_dragLast;
};

#endif // VIEWWIDGET_H
