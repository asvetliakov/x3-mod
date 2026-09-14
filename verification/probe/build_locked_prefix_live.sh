#!/bin/sh
# Builds the proxy-loaded locked-prefix fixture (step B of
# docs/architecture/screen-emission-region.md). The fixture loads whatever
# d3d9.dll sits beside it; run_locked_prefix_live.py pairs it with the built
# production DLL. Never invokes Wine or the production CMake build.
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -static -static-libgcc -static-libstdc++ locked_prefix_live_fixture.cpp -o build/locked_prefix_live_fixture.exe -ldxguid -luser32
