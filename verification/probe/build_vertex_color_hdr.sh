#!/bin/sh
# Standalone original shader interpolation experiment; no production dependencies.
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -static \
  vertex_color_hdr.cpp -o build/vertex_color_hdr.exe -luser32
