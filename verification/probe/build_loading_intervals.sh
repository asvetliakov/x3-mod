#!/bin/sh
# Build only. Owner serializes any Wine execution separately.
set -eu
cd "$(dirname "$0")"
mkdir -p build/loading_intervals
common="-std=c++17 -O2 -Wall -Wextra -Werror -DWIN32_LEAN_AND_MEAN -DNOMINMAX -mstackrealign -mincoming-stack-boundary=2"
i686-w64-mingw32-g++ $common -mno-sse -mno-mmx -mfpmath=387 -fno-exceptions -DX3M_LOADING_TRACE_FIXTURE -c ../../src/proxy/loading_trace_light.cpp -o build/loading_intervals/light.o
i686-w64-mingw32-g++ $common -msse2 -mfpmath=sse -DX3M_LOADING_TRACE_FIXTURE loading_intervals_fixture.cpp build/loading_intervals/light.o -static -static-libgcc -static-libstdc++ -pthread -luser32 -ladvapi32 -o build/loading_intervals/loading_intervals_fixture.exe
