#!/bin/sh
# Synthetic standalone verification, deliberately separate from production build.
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -static \
  depth_sampling.cpp -o build/depth_sampling.exe -luser32
