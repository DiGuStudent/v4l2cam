#!/usr/bin/env bash
# 一键构建 v4l2cam（源码外构建，产物在 build/）
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"

mkdir -p "$BUILD"
cd "$BUILD"
qmake "$ROOT/v4l2cam.pro"
make -j"$(nproc)"

echo "OK: $BUILD/v4l2cam"
