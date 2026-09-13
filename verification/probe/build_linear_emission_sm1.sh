#!/bin/sh
# Standalone probe only. Never rebuild the proxy DLL or launch any executable.
set -eu
cd "$(dirname "$0")"
mkdir -p build
CXX=${CXX:-i686-w64-mingw32-g++}
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse \
  -mstackrealign -mincoming-stack-boundary=2 -static -static-libgcc -static-libstdc++ \
  linear_emission_sm1_fixture.cpp ../../src/renderer/linear_emission_sm1.cpp \
  -o build/linear_emission_sm1_fixture.exe -ld3d9 -luser32
