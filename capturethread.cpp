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

// ============== 经 libv4l2 采集(与 OBS v4l2 插件同一路径) ==============
// 关键:MS2109 这类采集卡裸 ioctl 拿到的是芯片原始流,颜色就是错的;
// libv4l2 会做格式协商/转换,和 OBS 看到的一致。头文件用显式声明避免依赖 libv4l-dev。
extern "C" {
int  v4l2_open(const char *file, int oflag, ...);
int  v4l2_close(int fd);
void *v4l2_mmap(void *start, size_t length, int prot, int flags, int fd, long long offset);
int  v4l2_munmap(void *start, size_t length);
int  v4l2_ioctl(int fd, unsigned long int request, ...);
}

// ============== YUV -> RGB32:可切换 字节序 + 颜色矩阵 (整数查表) ==============
// 四种字节序:每 4 字节 = 两个像素共享一对色差。字节序下标:
//   {y0, c0, y1, c1} = 该 4 字节组中 亮度0/色差0/亮度1/色差1 的偏移
static const int kBgLayout[4][4] = {
    {0, 1, 2, 3},   // YUYV: [Y0, Cb, Y1, Cr]
    {0, 3, 2, 1},   // YVYU: [Y0, Cr, Y1, Cb]
    {1, 0, 3, 2},   // UYVY: [Cb, Y0, Cr, Y1]
    {1, 2, 3, 0},   // VYUY: [Cr, Y0, Cb, Y1]
};
// 颜色矩阵(系数 /1024,作用于色差 C-128):
//   行 = <Y缩放, Y偏移, CbR, CrR, CbG, CrG, CbB, CrB>
// 默认 0..2 顺序:BT.601全 → BT.601窄 → BT.709全
static const int kMat[4][8] = {
    {1024,   0,   0, 1436, -352, -731, 1815,    0},  // BT.601 全范围 0..255
    {1164, -16,   0, 1634, -400, -833, 2066,    0},  // BT.601 限制范围 16..235
    {1024,   0,   0, 1613, -192, -479, 1900,    0},  // BT.709 全范围
    {1164, -16,   0, 1836, -218, -546, 2163,    0},  // BT.709 限制范围 16..235 (OBS v4l2 默认)
};
// OBS shader 转换前先按范围钳制 YUV; 行=<yMin,yMax,cMin,cMax>
static const int kRange[4][4] = {
    {  0, 255,   0, 255},  // 全范围
    { 16, 235,  16, 240},  // 限制范围 16..235 / 16..240
    {  0, 255,   0, 255},  // 全范围
    { 16, 235,  16, 240},  // 限制范围(与 OBS video_matrices 的 range_min/max 一致)
};
// 查表(必须 int;/短整型会被 516*127≈6.5万 撑爆 —— 曾导致 YUYV 颜色错乱)
struct Tabs { int cbR[256], crR[256], cbG[256], crG[256], cbB[256], crB[256]; };
static Tabs g_tabs[4];
static void initTabs()
{
    for (int m = 0; m < 4; ++m)
        for (int i = 0; i < 256; ++i) {
            const int c = i - 128;
            g_tabs[m].cbR[i] = kMat[m][2] * c;
            g_tabs[m].crR[i] = kMat[m][3] * c;
            g_tabs[m].cbG[i] = kMat[m][4] * c;
            g_tabs[m].crG[i] = kMat[m][5] * c;
            g_tabs[m].cbB[i] = kMat[m][6] * c;
            g_tabs[m].crB[i] = kMat[m][7] * c;
        }
}

static inline quint32 clampRgb(int r, int g, int b)
{
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    return 0xff000000u | (quint32(r) << 16) | (quint32(g) << 8) | quint32(b);
}

static QImage yuvToRgb32(const uchar *src, int w, int h, int stride,
                         int layout, int matrix)
{
    if (layout < 0 || layout > 3) layout = 0;
    if (matrix < 0 || matrix > 3) matrix = 0;
    const int *bg = kBgLayout[layout];
    const Tabs &T = g_tabs[matrix];
    const int ys = kMat[matrix][0], yo = kMat[matrix][1];
    const int ymin = kRange[matrix][0], ymax = kRange[matrix][1];
    const int cmin = kRange[matrix][2], cmax = kRange[matrix][3];

    int rowBytes = w * 2;                   // YUYV: 每像素 2 字节
    if (stride < rowBytes) stride = rowBytes;

    QImage img(w, h, QImage::Format_RGB32);
    uchar *dst = img.bits();
    const int pairs = w / 2;                // 一行的像素对

    for (int y = 0; y < h; ++y) {
        const uchar *p = src + size_t(y) * stride;
        quint32 *d = reinterpret_cast<quint32 *>(dst + size_t(y) * w * 4);
        for (int x = 0; x < pairs; ++x) {
            // 先按范围钳制(对齐 OBS shader clamp)再查表
            const int y0 = qBound(ymin, int(p[bg[0]]), ymax);
            const int c0 = qBound(cmin, int(p[bg[1]]), cmax);
            const int y1 = qBound(ymin, int(p[bg[2]]), ymax);
            const int c1 = qBound(cmin, int(p[bg[3]]), cmax);
            p += 4;
            const int yb0 = (ys * (y0 + yo) + 512) >> 10;
            const int yb1 = (ys * (y1 + yo) + 512) >> 10;
            d[0] = clampRgb(yb0 + T.cbR[c0] + T.crR[c1],
                            yb0 + T.cbG[c0] + T.crG[c1],
                            yb0 + T.cbB[c0] + T.crB[c1]);
            d[1] = clampRgb(yb1 + T.cbR[c0] + T.crR[c1],
                            yb1 + T.cbG[c0] + T.crG[c1],
                            yb1 + T.cbB[c0] + T.crB[c1]);
            d += 2;
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
    int fd = v4l2_open(dev.toUtf8().constData(), O_RDWR);
    if (fd < 0) return out;

    for (int i = 0; i < 32; ++i) {
        v4l2_fmtdesc f{};
        f.index = i;
        f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (v4l2_ioctl(fd, VIDIOC_ENUM_FMT, &f) < 0)
            break;
        FormatInfo fi;
        fi.fourcc = f.pixelformat;
        fi.description = QString::fromUtf8(reinterpret_cast<const char *>(f.description))
                             .trimmed();
        if (fi.description.isEmpty())
            fi.description = fourccToString(f.pixelformat);
        out.push_back(fi);
    }
    v4l2_close(fd);
    return out;
}

std::vector<CaptureThread::SizeInfo> CaptureThread::enumSizes(const QString &dev, unsigned int fourcc)
{
    std::vector<SizeInfo> out;
    int fd = v4l2_open(dev.toUtf8().constData(), O_RDWR);
    if (fd < 0) return out;

    for (int i = 0; i < 64; ++i) {
        v4l2_frmsizeenum fr{};
        fr.index = i;
        fr.pixel_format = fourcc;
        if (v4l2_ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fr) < 0)
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
    v4l2_close(fd);
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
    initTabs();

    const QByteArray devBytes = m_device.toUtf8();
    int fd = v4l2_open(devBytes.constData(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        emit errorOccurred(QString("无法打开设备 %1:").arg(m_device) + strerror(errno));
        return;
    }

    v4l2_capability cap{};
    if (v4l2_ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        emit errorOccurred(QString("VIDIOC_QUERYCAP 失败: ") + strerror(errno));
        v4l2_close(fd);
        return;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        emit errorOccurred(m_device + " 不是视频采集设备");
        v4l2_close(fd);
        return;
    }

    // 设置格式
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = uint(m_w);
    fmt.fmt.pix.height = uint(m_h);
    fmt.fmt.pix.pixelformat = m_fourcc;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    if (v4l2_ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        emit errorOccurred(QString("设置格式失败(%1x%2): ").arg(m_w).arg(m_h)
                           + strerror(errno));
        v4l2_close(fd);
        return;
    }
    const int w = int(fmt.fmt.pix.width);
    const int h = int(fmt.fmt.pix.height);
    const int stride = int(fmt.fmt.pix.bytesperline);
    // 以实际落地格式为准(经 libv4l2 协商后可能与请求不同)
    const unsigned actualFcc = fmt.fmt.pix.pixelformat;

    // 申请 mmap 缓冲
    v4l2_requestbuffers req{};
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (v4l2_ioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) {
        emit errorOccurred(QString("VIDIOC_REQBUFS 失败: ") + strerror(errno));
        v4l2_close(fd);
        return;
    }

    std::vector<uchar *> bufs(req.count);
    std::vector<size_t> lens(req.count);
    for (size_t i = 0; i < req.count; ++i) {
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = uint(i);
        if (v4l2_ioctl(fd, VIDIOC_QUERYBUF, &b) < 0) {
            emit errorOccurred("VIDIOC_QUERYBUF 失败");
            v4l2_close(fd);
            return;
        }
        lens[i] = b.length;
        bufs[i] = static_cast<uchar *>(
            v4l2_mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                      static_cast<long long>(b.m.offset)));
        if (bufs[i] == MAP_FAILED) {
            emit errorOccurred("v4l2_mmap 失败");
            bufs[i] = nullptr;
        }
        // QUERYBUF 后立即 QBUF 入队
        v4l2_ioctl(fd, VIDIOC_QBUF, &b);
    }

    {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        v4l2_ioctl(fd, VIDIOC_STREAMON, &type);
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
        if (v4l2_ioctl(fd, VIDIOC_DQBUF, &b) < 0) {
            if (errno == EAGAIN) continue;
            emit errorOccurred(QString("VIDIOC_DQBUF 失败: ") + strerror(errno));
            break;
        }
        if (b.index >= bufs.size() || !bufs[b.index]) {
            v4l2_ioctl(fd, VIDIOC_QBUF, &b);
            continue;
        }

        // 「存帧」诊断:本帧原样写盘
        if (m_snap.exchange(false) && !m_snapDir.isEmpty()) {
            const QString path = m_snapDir + QString("/frame_%1.raw")
                                     .arg(QDateTime::currentMSecsSinceEpoch());
            FILE *fp = fopen(path.toUtf8().constData(), "wb");
            if (fp) {
                fwrite(bufs[b.index], 1, b.bytesused, fp);
                fclose(fp);
                emit snapshotSaved(path);
            }
        }

        QImage img;
        if (actualFcc == V4L2_PIX_FMT_MJPEG)
            img = mjpgToRgb32(bufs[b.index], int(b.bytesused));
        else
            img = yuvToRgb32(bufs[b.index], w, h, stride,
                             m_layout.load(), m_matrix.load());

        ++frameCount;
        byteCount += b.bytesused;

        if (!img.isNull())
            emit frameReady(img);               // 队列连接,自动投递到 GUI 线程

        v4l2_ioctl(fd, VIDIOC_QBUF, &b);

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
    v4l2_ioctl(fd, VIDIOC_STREAMOFF, &type);
    for (size_t i = 0; i < bufs.size(); ++i)
        if (bufs[i])
            v4l2_munmap(bufs[i], lens[i]);
    v4l2_close(fd);
}

void CaptureThread::requestRawSnapshot(const QString &dir)
{
    m_snapDir = dir;
    m_snap.store(true);
}
