#!/bin/sh
# Build only the detached port-distance fixture. No production DLL, no Wine run.
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse \
  -mstackrealign -mincoming-stack-boundary=2 -static port_distance_fixture.cpp \
  -o build/port_distance_fixture.exe -ld3d9 -luser32
