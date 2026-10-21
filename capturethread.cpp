#include "capturethread.h"

#include <QDebug>
#include <QDateTime>
#include <QDir>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <linux/videodev2.h>
#include <cstring>
#include <cstdint>
#include <csetjmp>
#include <vector>

#include <jpeglib.h>

// ===================== YUYV -> RGB32 (BT.601, 整数查表) =====================
static short g_crv[256]; //  1.402 * (V-128) * 256
static short g_cgu[256]; // -0.344 * (U-128) * 256
static short g_cbu[256]; //  1.772 * (U-128) * 256
static short g_cgv[256]; // -0.714 * (V-128) * 256

static void initYuvTables()
{
    static bool done = false;
    if (done) return;
    for (int i = 0; i < 256; ++i) {
        int u = i - 128, v = i - 128;
        // 按下标即原始字节值 0~255 建表,转换时直接查表
        g_crv[i] = short(409  * v);   // 1.596 * 256
        g_cgu[i] = short(-100 * u);   // 0.391 * 256
        g_cbu[i] = short(516  * u);   // 2.018 * 256
        g_cgv[i] = short(-208 * v);   // 0.813 * 256
    }
    done = true;
}

static inline quint32 clampRgb(int r, int g, int b)
{
    r = r >> 8; g = g >> 8; b = b >> 8;
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    return 0xff000000u | (quint32(r) << 16) | (quint32(g) << 8) | quint32(b);
}

static QImage yuyvToRgb32(const uchar *src, int w, int h, int stride)
{
    initYuvTables();
    const int rowBytes = w * 2;                 // YUYV: 每像素 2 字节
    if (stride < rowBytes) stride = rowBytes;

    QImage img(w, h, QImage::Format_RGB32);
    uchar *dst = img.bits();
    const int pairs = w / 2;                    // 一行的像素对

    for (int y = 0; y < h; ++y) {
        const uchar *p = src + size_t(y) * stride;
        quint32 *d = reinterpret_cast<quint32 *>(dst + size_t(y) * w * 4);
        for (int x = 0; x < pairs; ++x) {
            int y0 = p[0], u = p[1], y1 = p[2], v = p[3];
            p += 4;
            int cu0 = y0 * 298 + 128, cv0 = y0 * 298 + 128;
            int cu1 = y1 * 298 + 128, cv1 = y1 * 298 + 128;
            d[0] = clampRgb(cu0 + g_crv[v], cv0 + g_cgu[u] + g_cgv[v], cu0 + g_cbu[u]);
            d[1] = clampRgb(cu1 + g_crv[v], cv1 + g_cgu[u] + g_cgv[v], cu1 + g_cbu[u]);
            d += 2;
        }
        if (w & 1) {            // 奇数宽: 最后 1 像素用上一对的 U/V
            d[0] = 0xff000000u;
        }
    }
    return img;
}

// ===================== MJPG -> RGB32 (libjpeg) =====================
struct JpegErrMgr {
    jpeg_error_mgr pub;
    jmp_buf jb;
};

static void jpegErrorExit(j_common_ptr cinfo)
{
    JpegErrMgr *e = reinterpret_cast<JpegErrMgr *>(cinfo->err);
    longjmp(e->jb, 1);
}

static QImage mjpgToRgb32(const uchar *src, int size)
{
    jpeg_decompress_struct cinfo{};
    JpegErrMgr jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpegErrorExit;

    if (setjmp(jerr.jb)) {
        jpeg_destroy_decompress(&cinfo);
        return QImage();
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, const_cast<uchar *>(src), size);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return QImage();
    }
    cinfo.out_color_space = JCS_RGB;    // 输出 3 通道,方便转 RGB32
    jpeg_start_decompress(&cinfo);

    const int w = cinfo.output_width, h = cinfo.output_height;
    QImage img(w, h, QImage::Format_RGB32);
    quint32 *dst = reinterpret_cast<quint32 *>(img.bits());

    std::vector<JSAMPLE> rowbuf(size_t(w) * 3);
    while (cinfo.output_scanline < (JDIMENSION)h) {
        JSAMPROW row[1] = { rowbuf.data() };
        jpeg_read_scanlines(&cinfo, row, 1);
        const uchar *s = rowbuf.data();
        quint32 *d = dst + size_t(cinfo.output_scanline - 1) * w;
        for (int x = 0; x < w; ++x, s += 3)
            d[x] = 0xff000000u | (quint32(s[0]) << 16) | (quint32(s[1]) << 8) | quint32(s[2]);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return img;
}

// ============================ 构造 / 析构 ============================
CaptureThread::CaptureThread(QObject *parent) : QThread(parent) {}

CaptureThread::~CaptureThread()
{
    requestStop();
    if (isRunning())
        wait(1000);
}

// ============================ 静态枚举工具 ============================
QStringList CaptureThread::devices()
{
    QStringList list;
    const QStringList files = QDir("/dev").entryList(QStringList() << "video*",
                                                     QDir::System | QDir::Files);
    for (const QString &f : files)
        list << "/dev/" + f;
    return list;
}

std::vector<CaptureThread::FormatInfo> CaptureThread::enumFormats(const QString &dev)
{
    std::vector<FormatInfo> out;
    int fd = ::open(dev.toUtf8().constData(), O_RDWR);
    if (fd < 0) return out;

    for (int i = 0; i < 32; ++i) {
        v4l2_fmtdesc f{};
        f.index = i;
        f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (::ioctl(fd, VIDIOC_ENUM_FMT, &f) < 0)
            break;
        FormatInfo fi;
        fi.fourcc = f.pixelformat;
        fi.description = QString::fromUtf8(reinterpret_cast<const char *>(f.description))
                             .trimmed();
        if (fi.description.isEmpty())
            fi.description = fourccToString(f.pixelformat);
        out.push_back(fi);
    }
    ::close(fd);
    return out;
}

std::vector<CaptureThread::SizeInfo> CaptureThread::enumSizes(const QString &dev, unsigned int fourcc)
{
    std::vector<SizeInfo> out;
    int fd = ::open(dev.toUtf8().constData(), O_RDWR);
    if (fd < 0) return out;

    for (int i = 0; i < 64; ++i) {
        v4l2_frmsizeenum fr{};
        fr.index = i;
        fr.pixel_format = fourcc;
        if (::ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fr) < 0)
            break;
        if (fr.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
            out.push_back({ int(fr.discrete.width), int(fr.discrete.height) });
        } else if (fr.type == V4L2_FRMSIZE_TYPE_STEPWISE) {
            for (int w = int(fr.stepwise.min_width); w <= int(fr.stepwise.max_width);
                 w += int(fr.stepwise.step_width))
                for (int h = int(fr.stepwise.min_height); h <= int(fr.stepwise.max_height);
                     h += int(fr.stepwise.step_height))
                    out.push_back({ w, h });
        }
    }
    ::close(fd);
    return out;
}

QString CaptureThread::fourccToString(unsigned int fourcc)
{
    char s[5];
    s[0] = char(fourcc & 0xff);
    s[1] = char((fourcc >> 8) & 0xff);
    s[2] = char((fourcc >> 16) & 0xff);
    s[3] = char((fourcc >> 24) & 0xff);
    s[4] = '\0';
    return QString::fromLatin1(s);
}

// ============================ 采集主循环 ============================
void CaptureThread::run()
{
    m_stop.store(false);
    initYuvTables();

    const QByteArray devBytes = m_device.toUtf8();
    int fd = ::open(devBytes.constData(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        emit errorOccurred(QString("无法打开设备 %1:").arg(m_device) + strerror(errno));
        return;
    }

    v4l2_capability cap{};
    if (::ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        emit errorOccurred(QString("VIDIOC_QUERYCAP 失败: ") + strerror(errno));
        ::close(fd);
        return;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        emit errorOccurred(m_device + " 不是视频采集设备");
        ::close(fd);
        return;
    }

    // 设置格式
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = uint(m_w);
    fmt.fmt.pix.height = uint(m_h);
    fmt.fmt.pix.pixelformat = m_fourcc;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    if (::ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        emit errorOccurred(QString("设置格式失败(%1x%2): ").arg(m_w).arg(m_h)
                           + strerror(errno));
        ::close(fd);
        return;
    }
    const int w = int(fmt.fmt.pix.width);
    const int h = int(fmt.fmt.pix.height);
    const int stride = int(fmt.fmt.pix.bytesperline);

    // 申请 mmap 缓冲
    v4l2_requestbuffers req{};
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (::ioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) {
        emit errorOccurred(QString("VIDIOC_REQBUFS 失败: ") + strerror(errno));
        ::close(fd);
        return;
    }

    std::vector<uchar *> bufs(req.count);
    std::vector<size_t> lens(req.count);
    for (size_t i = 0; i < req.count; ++i) {
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = uint(i);
        if (::ioctl(fd, VIDIOC_QUERYBUF, &b) < 0) {
            emit errorOccurred("VIDIOC_QUERYBUF 失败");
            ::close(fd);
            return;
        }
        lens[i] = b.length;
        bufs[i] = static_cast<uchar *>(
            ::mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset));
        if (bufs[i] == MAP_FAILED) {
            emit errorOccurred("mmap 失败");
            bufs[i] = nullptr;
        }
        // QUERYBUF 后立即 QBUF 入队
        ::ioctl(fd, VIDIOC_QBUF, &b);
    }

    {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ::ioctl(fd, VIDIOC_STREAMON, &type);
    }

    pollfd pfd{ fd, POLLIN, 0 };
    qint64 frameCount = 0;
    qint64 byteCount = 0;
    qint64 lastT = QDateTime::currentMSecsSinceEpoch();

    while (!m_stop.load()) {
        int pr = ::poll(&pfd, 1, 150);          // 150ms 超时,保证能退出
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) continue;                  // 超时,检查退出标志

        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        if (::ioctl(fd, VIDIOC_DQBUF, &b) < 0) {
            if (errno == EAGAIN) continue;
            emit errorOccurred(QString("VIDIOC_DQBUF 失败: ") + strerror(errno));
            break;
        }
        if (b.index >= bufs.size() || !bufs[b.index]) {
            ::ioctl(fd, VIDIOC_QBUF, &b);
            continue;
        }

        QImage img;
        if (m_fourcc == V4L2_PIX_FMT_MJPEG)
            img = mjpgToRgb32(bufs[b.index], int(b.bytesused));
        else
            img = yuyvToRgb32(bufs[b.index], w, h, stride);

        ++frameCount;
        byteCount += b.bytesused;

        if (!img.isNull())
            emit frameReady(img);               // 队列连接,自动投递到 GUI 线程

        ::ioctl(fd, VIDIOC_QBUF, &b);

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastT >= 500) {
            const double dt = (now - lastT) / 1000.0;
            emit stats(frameCount / dt, byteCount / dt / 1048576.0, w, h, m_fourcc);
            frameCount = 0;
            byteCount = 0;
            lastT = now;
        }
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ::ioctl(fd, VIDIOC_STREAMOFF, &type);
    for (size_t i = 0; i < bufs.size(); ++i)
        if (bufs[i])
            ::munmap(bufs[i], lens[i]);
    ::close(fd);
}
