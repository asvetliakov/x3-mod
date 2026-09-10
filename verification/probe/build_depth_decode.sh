#!/bin/sh
# Independent numeric/cost fixture using the actual original decoder HLSL.
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -static \
  depth_decode.cpp -o build/depth_decode.exe -luser32
