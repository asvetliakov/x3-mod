#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
  auto_depth_fixture.cpp ../../src/ownership/d3d9_ownership.cpp \
  -o build/auto_depth_fixture.exe -luser32 -ldxguid
