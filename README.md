# v4l2cam — V4L2 采集卡快速查看器 (Qt5)

直接以原生 V4L2 mmap 方式从 `/dev/videoX` 拉取 USB 采集卡(如 MACROSILICON
MS21xx 系列)画面。不经过 GStreamer / PipeWire,**原始像素原样输出,天然无镜像、
延迟最低**。

## 功能

- 设备下拉框自动枚举 `/dev/video*`
- 格式下拉框:YUYV(YUV 4:2:2,未压缩,码率最高、延迟最低)/ MJPG(libjpeg 解码)
- 分辨率下拉框按所选格式枚举,默认 YUYV 1920x1080
- 鼠标滚轮以光标为中心缩放(1x 适合窗口 ~ 64x),左键拖拽平移
- 适配窗口 / 1:1 / 全屏(F11)/ 小窗切换
- 状态栏实时显示 格式·分辨率·帧率·吞吐量(MB/s)

## 快捷键

| 按键 | 功能 |
|------|------|
| F11 / Esc | 全屏 开 / 关 |
| 空格 | 开始 / 停止采集 |
| F | 适合窗口 |
| 1 | 1:1 原始像素 |
| + / − | 放大 / 缩小 |

## 构建与运行

```sh
cd v4l2cam
qmake && make
./v4l2cam
```

依赖:Qt5 (qtbase5-dev)、Linux V4L2 头文件、libjpeg-dev。

## 说明

- **采集走 libv4l2(v4l2_open)**,与 OBS 的 linux-v4l2 插件同路径——这是 MS2109
  这类采集卡颜色正确的关键(OBS 源码实测:裸 ioctl 拿到的是芯片原始流,颜色错;
  libv4l2 会做格式协商/转换)。
- YUYV→RGB32 用查表整数转换(32 位表,short 会溢出错色),支持 4 字节序 × 4 矩阵
  (BT.601/709 × 全/窄范围),默认 `YUYV · BT.709窄`(= OBS 对 YUYV 输入的默认
  Rec.709 + partial)。
- 「存帧」按钮把下一帧原始字节流写 `~/v4l2cam_snapshots/frame_<ms>.raw`,
  供调色/抓包诊断(含 YUYV/MJPG 两种)。
- 若设备被占用("设备或资源忙"),先关掉 OBS / gnome-camera 等其他占用程序。
