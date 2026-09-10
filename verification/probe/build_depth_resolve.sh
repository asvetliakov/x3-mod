#!/bin/sh
# Independent original D24X8-to-INTZ RESZ verification, outside production build.
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -static \
  depth_resolve.cpp -o build/depth_resolve.exe -luser32
