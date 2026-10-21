#!/bin/bash
# 一键构建 v4l2cam
set -e
cd "$(dirname "$0")"
qmake
make -j"$(nproc)"
echo "OK: $(pwd)/v4l2cam"
