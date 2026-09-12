#!/bin/sh
# Original synthetic executable only. No game install or launch.
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -DX3M_OBJECT_LIFETIME_FIXTURE -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -static \
  object_lifetime.cpp ../../src/proxy/object_lifetime.cpp ../../src/proxy/engine_memory.cpp -o build/object_lifetime.exe -ladvapi32
