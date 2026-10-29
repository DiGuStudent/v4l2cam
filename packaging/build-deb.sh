#!/usr/bin/env bash
# 构建 Debian 安装包（产物在 dist/）
#   ./packaging/build-deb.sh            # 先构建再打包
#   ./packaging/build-deb.sh --no-build # 直接用 build/ 下已有二进制
# 安装：sudo apt install ./dist/v4l2cam_<版本>_<架构>.deb
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG=v4l2cam
VERSION="$(sed -n 's/^VERSION[[:space:]]*=[[:space:]]*//p' "$ROOT/$PKG.pro" | head -n1)"
[ -n "$VERSION" ] || { echo "无法从 $PKG.pro 读取 VERSION" >&2; exit 1; }

ARCH="$(dpkg --print-architecture)"
BIN="$ROOT/build/$PKG"
DIST="$ROOT/dist"

if [ "${1:-}" != "--no-build" ] || [ ! -x "$BIN" ]; then
    "$ROOT/scripts/build.sh"
fi
[ -x "$BIN" ] || { echo "缺少二进制：$BIN" >&2; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

# ---- 安装目录树 ----
install -Dm755 "$BIN" "$STAGE/usr/bin/$PKG"
install -Dm644 "$ROOT/packaging/$PKG.desktop" "$STAGE/usr/share/applications/$PKG.desktop"
install -Dm644 "$ROOT/assets/icons/$PKG.png" "$STAGE/usr/share/icons/hicolor/256x256/apps/$PKG.png"
install -Dm644 "$ROOT/assets/icons/$PKG.svg" "$STAGE/usr/share/icons/hicolor/scalable/apps/$PKG.svg"
install -Dm644 "$ROOT/README.md" "$STAGE/usr/share/doc/$PKG/README.md"
install -Dm644 "$ROOT/LICENSE"    "$STAGE/usr/share/doc/$PKG/copyright"

# ---- 运行库依赖 ----
# 优先用 dpkg-shlibdeps 生成带版本约束的 Depends；失败则按 ldd 反查包名兜底
SHLIBS=""
if command -v dpkg-shlibdeps >/dev/null; then
    WORK="$STAGE/.shlibdeps"
    mkdir -p "$WORK/debian"
    cat > "$WORK/debian/control" <<EOF
Source: $PKG
Section: graphics
Priority: optional
Maintainer: v4l2cam <noreply@localhost>
Build-Depends: debhelper-compat (= 13)

Package: $PKG
Architecture: any
EOF
    SHLIBS="$(cd "$WORK" && dpkg-shlibdeps -O -e"$STAGE/usr/bin/$PKG" 2>/dev/null \
              | sed -n 's/^shlibs:Depends=//p')" || SHLIBS=""
    rm -rf "$WORK"
fi
if [ -z "$SHLIBS" ]; then
    SHLIBS="$(ldd "$BIN" | sed 's/ *(.*//' | awk '$2 == "=>" && $3 ~ /^\// {print $3}' \
              | while read -r lib; do dpkg -S "$(readlink -f "$lib")" 2>/dev/null | cut -d: -f1; done \
              | sort -u | paste -sd, -)"
fi
[ -n "$SHLIBS" ] || SHLIBS="libc6"

SIZE_KB="$(du -sk "$STAGE" | cut -f1)"

mkdir -p "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: $PKG
Version: $VERSION
Architecture: $ARCH
Maintainer: v4l2cam <noreply@localhost>
Installed-Size: $SIZE_KB
Depends: $SHLIBS
Section: graphics
Priority: optional
Description: V4L2 capture card viewer (Qt5)
 Low-latency viewer for USB capture cards and webcams, reading /dev/videoX
 through libv4l2/mmap with no GStreamer or PipeWire in the path. Supports
 YUYV and MJPG, format/resolution selection, zoom/pan, fullscreen, and
 frame-rate/throughput display.
EOF

cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
[ -x /usr/bin/update-desktop-database ] && update-desktop-database -q /usr/share/applications || true
[ -x /usr/bin/gtk-update-icon-cache ] && gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
exit 0
EOF

cat > "$STAGE/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
[ -x /usr/bin/update-desktop-database ] && update-desktop-database -q /usr/share/applications || true
[ -x /usr/bin/gtk-update-icon-cache ] && gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
exit 0
EOF
chmod 755 "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/postrm"

# ---- md5sums ----
(cd "$STAGE" && find usr -type f -exec md5sum {} + | sed 's/  / /' > DEBIAN/md5sums)

mkdir -p "$DIST"
OUT="$DIST/${PKG}_${VERSION}_${ARCH}.deb"
dpkg-deb --root-owner-group -Zxz --build "$STAGE" "$OUT" >/dev/null
echo "OK: $OUT"
