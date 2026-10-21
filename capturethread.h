#ifndef CAPTURETHREAD_H
#define CAPTURETHREAD_H

#include <QThread>
#include <QImage>
#include <QString>
#include <QStringList>
#include <vector>
#include <atomic>
#include <linux/videodev2.h>     // V4L2_PIX_FMT_*

// 原生 V4L2 (mmap) 采集线程。
// 直接 ioctl /dev/videoX,不经过 GStreamer / PipeWire,因此
//   - 无任何翻转/镜像(原始画面原样出)
//   - 延迟最低
// 支持 YUYV(YUV 4:2:2,未压缩,码率最高)与 MJPG(需 libjpeg 解码)。
class CaptureThread : public QThread
{
    Q_OBJECT
public:
    explicit CaptureThread(QObject *parent = nullptr);
    ~CaptureThread() override;

    void setDevice(const QString &device) { m_device = device; }
    void setFourcc(unsigned int fourcc) { m_fourcc = fourcc; }
    void setSize(int w, int h) { m_w = w; m_h = h; }

    // YUV 解析参数(可随时改,下一帧生效,无需重启采集)
    // layout: 0=YUYV 1=YVYU 2=UYVY 3=VYUY
    // matrix: 0=BT.601全范围 1=BT.601限制范围(16-235) 2=BT.709全范围
    void setDecode(int layout, int matrix)
    {
        m_layout.store(layout);
        m_matrix.store(matrix);
    }

    void requestStop() { m_stop.store(true); }

    struct FormatInfo { unsigned int fourcc; QString description; };
    struct SizeInfo { int w; int h; };

    // -------- 以下静态工具在 GUI 线程调用,用于填充下拉框 --------
    static QStringList devices();                                   // /dev/video*
    static std::vector<FormatInfo> enumFormats(const QString &dev); // 该设备的像素格式
    static std::vector<SizeInfo> enumSizes(const QString &dev, unsigned int fourcc);
    static QString fourccToString(unsigned int fourcc);

signals:
    void frameReady(const QImage &img);
    void errorOccurred(const QString &msg);
    // fps / 吞吐统计,约每 500ms 发一次
    void stats(double fps, double mbps, int w, int h, unsigned int fourcc);

protected:
    void run() override;

private:
    QString m_device = "/dev/video0";
    unsigned int m_fourcc = V4L2_PIX_FMT_YUYV;  // 默认 YUYV(422,最低延迟)
    int m_w = 1920;
    int m_h = 1080;
    std::atomic_bool m_stop{false};
    std::atomic_int m_layout{0};    // YUYV 字节序
    std::atomic_int m_matrix{0};    // BT.601 全范围
};

#endif // CAPTURETHREAD_H
