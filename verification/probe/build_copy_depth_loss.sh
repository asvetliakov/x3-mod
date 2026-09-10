#!/bin/sh
# Disposable executable only; no install or game launch.
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
  copy_depth_loss.cpp ../../src/ownership/d3d9_ownership.cpp \
  -o build/copy_depth_loss.exe -ldxguid -luser32
