# v4l2cam

A minimal, low-latency **V4L2 capture card viewer** for Linux, built with Qt5.
It talks to `/dev/videoX` directly through mmap — no GStreamer, no PipeWire —
so the frames are passed through untouched: no mirroring, no extra latency.

一个基于 Qt5 的 Linux **V4L2 采集卡查看器**。直接以 mmap 方式从
`/dev/videoX` 取流，不经过 GStreamer / PipeWire，原始像素原样输出，
天然无镜像、延迟最低。

[English](#english) · [简体中文](#简体中文)

---

## English

### Features

- Auto-enumerates `/dev/video*` devices
- Format selector: **YUYV** (YUV 4:2:2, uncompressed, lowest latency) and
  **MJPG** (decoded with libjpeg)
- Resolution list enumerated per selected format, defaults to YUYV 1920×1080
- Mouse-wheel zoom centered on the cursor (fit-to-window up to 64×),
  left-button drag to pan
- Fit-to-window / 1:1 / fullscreen (F11) / windowed modes
- Live status bar: format · resolution · FPS · throughput (MB/s)
- Watchdog: auto-reconnects when the stream stalls for 3 s or the capture
  thread exits unexpectedly (up to 5 attempts, with 8 s cooldown)

### Shortcuts

| Key | Action |
|-----|--------|
| `F11` / `Esc` | Toggle fullscreen |
| `Space` | Start / stop capture |
| `F` | Fit to window |
| `1` | 1:1 actual pixels |
| `+` / `-` | Zoom in / out |

### Dependencies

- Qt5 (`qtbase5-dev`)
- Linux V4L2 headers
- `libjpeg-dev`
- `libv4l2` runtime (`libv4l2.so.0`)

On Debian/Ubuntu:

```sh
sudo apt install qtbase5-dev libjpeg-dev libv4l-0
```

### Build

```sh
./scripts/build.sh
./build/v4l2cam
```

Or manually:

```sh
mkdir -p build && cd build
qmake ../v4l2cam.pro
make -j"$(nproc)"
./v4l2cam
```

### Install

```sh
# Debian/Ubuntu 软件包（推荐）
./packaging/build-deb.sh
sudo apt install ./dist/v4l2cam_1.0.0_amd64.deb

# 或者 qmake 安装
sudo make -C build install          # installs to /usr/local by default
sudo make -C build install PREFIX=/usr
```

This installs the binary, the `.desktop` entry and the icon.

For a per-user install without root:

```sh
qmake PREFIX="$HOME/.local" v4l2cam.pro && make && make install
```

(`~/.local/bin` is on the session `PATH`, so the `.desktop` entry's
`Exec=v4l2cam` resolves.)

### Notes

- Capture goes through **libv4l2** (`v4l2_open`), the same path as OBS's
  `linux-v4l2` plugin. For cards such as MS2109 this is what makes the colors
  correct: raw ioctls return the chip's unprocessed stream, while libv4l2
  performs format negotiation/conversion.
- YUYV → RGB32 uses integer lookup tables (32-bit tables — 16-bit ones
  overflow at `516 × 127` and corrupt colors). Four byte orders × four
  matrices (BT.601/709 × full/limited) are available; the default is
  `YUYV · BT.709 limited`, matching OBS's default for YUYV input.
- **Save frame** writes the next raw frame to
  `~/v4l2cam_snapshots/frame_<ms>...raw` for color/packet diagnostics
  (works for both YUYV and MJPG).
- **Screenshot** saves the current rendered image as PNG.
- If the device is busy ("Device or resource busy"), close OBS, gnome-camera
  or any other program using it first.

### Project layout

```
.
├── src/                    # C++ sources
│   ├── main.cpp
│   ├── mainwindow.{h,cpp}
│   ├── capturethread.{h,cpp}
│   └── viewwidget.{h,cpp}
├── assets/icons/           # application icons (SVG + PNG)
├── packaging/              # .desktop entry + build-deb.sh
├── scripts/build.sh        # one-step out-of-source build
├── dist/                   # generated .deb packages
├── v4l2cam.pro             # qmake project
├── LICENSE
└── README.md
```

### License

[MIT](LICENSE)

---

## 简体中文

### 功能

- 设备下拉框自动枚举 `/dev/video*`
- 格式下拉框：**YUYV**（YUV 4:2:2，未压缩，码率最高、延迟最低）/
  **MJPG**（libjpeg 解码）
- 分辨率下拉框按所选格式枚举，默认 YUYV 1920×1080
- 鼠标滚轮以光标为中心缩放（适合窗口 ~ 64×），左键拖拽平移
- 适配窗口 / 1:1 / 全屏（F11）/ 小窗切换
- 状态栏实时显示 格式 · 分辨率 · 帧率 · 吞吐量（MB/s）
- 看门狗：连续 3 秒无新帧或采集线程异常退出时自动重连
  （最多 5 次，每次间隔 ≥ 8 秒）

### 快捷键

| 按键 | 功能 |
|------|------|
| `F11` / `Esc` | 全屏 开 / 关 |
| `空格` | 开始 / 停止采集 |
| `F` | 适合窗口 |
| `1` | 1:1 原始像素 |
| `+` / `-` | 放大 / 缩小 |

### 依赖

- Qt5（`qtbase5-dev`）
- Linux V4L2 头文件
- `libjpeg-dev`
- `libv4l2` 运行库（`libv4l2.so.0`）

Debian/Ubuntu：

```sh
sudo apt install qtbase5-dev libjpeg-dev libv4l-0
```

### 构建

```sh
./scripts/build.sh
./build/v4l2cam
```

或手动构建：

```sh
mkdir -p build && cd build
qmake ../v4l2cam.pro
make -j"$(nproc)"
./v4l2cam
```

### 安装

```sh
# Debian/Ubuntu 安装包（推荐）
./packaging/build-deb.sh
sudo apt install ./dist/v4l2cam_1.0.0_amd64.deb

# 或者用 qmake 安装
sudo make -C build install          # 默认安装到 /usr/local
sudo make -C build install PREFIX=/usr
```

会安装可执行文件、`.desktop` 启动项和图标。

只装给当前用户（不需要 root）：

```sh
qmake PREFIX="$HOME/.local" v4l2cam.pro && make && make install
```

（`~/.local/bin` 已在会话 `PATH` 中，启动项的 `Exec=v4l2cam` 能直接找到。）

### 说明

- **采集走 libv4l2（`v4l2_open`）**，与 OBS 的 `linux-v4l2` 插件同路径——
  这是 MS2109 这类采集卡颜色正确的关键（OBS 源码实测：裸 ioctl 拿到的是
  芯片原始流，颜色错；libv4l2 会做格式协商/转换）。
- YUYV → RGB32 用查表整数转换（32 位表，16 位表会在 `516 × 127` 处溢出
  导致错色），支持 4 种字节序 × 4 种矩阵（BT.601/709 × 全/窄范围），
  默认 `YUYV · BT.709 窄`（= OBS 对 YUYV 输入的默认 Rec.709 + partial）。
- **存帧**按钮把下一帧原始字节流写
  `~/v4l2cam_snapshots/frame_<ms>...raw`，供调色/抓包诊断（含 YUYV/MJPG）。
- **截图**按钮保存当前渲染画面为 PNG。
- 若设备被占用（"设备或资源忙"），先关掉 OBS / gnome-camera 等其他占用程序。

### 目录结构

```
.
├── src/                    # C++ 源码
│   ├── main.cpp
│   ├── mainwindow.{h,cpp}
│   ├── capturethread.{h,cpp}
│   └── viewwidget.{h,cpp}
├── assets/icons/           # 应用图标（SVG + PNG）
├── packaging/              # .desktop 启动项 + deb 打包脚本
├── scripts/build.sh        # 一键源码外构建
├── dist/                   # 生成的 .deb 安装包
├── v4l2cam.pro             # qmake 工程文件
├── LICENSE
└── README.md
```

### 许可证

[MIT](LICENSE)
