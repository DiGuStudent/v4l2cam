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

- 采集线程(V4L2 mmap,环形 4 缓冲)与 GUI 线程通过 `frameReady / stats`
  信号解耦,不阻塞界面。
- YUYV→RGB32 用 BT.601 查表整数转换,1080p60 下无压力;JMJPEG 走 libjpeg-turbo。
- 若设备被占用("设备或资源忙"),先关掉 gnome-camera 等其他占用程序。
