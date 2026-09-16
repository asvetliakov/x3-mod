#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static -static-libgcc -static-libstdc++ \
 -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 \
 state_hook_benchmark.cpp -o build/state_hook_benchmark.exe -ldxguid -luser32
