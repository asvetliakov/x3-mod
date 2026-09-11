#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
 -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 \
 geometry_lease_benchmark.cpp ../../src/ownership/d3d9_ownership.cpp \
 ../../src/ownership/execution_state.cpp ../../src/ownership/finite_buffer_evidence.cpp ../../src/ownership/portable_managed_upload.cpp \
 -o build/geometry_lease_benchmark.exe -ldxguid -luser32 -ladvapi32
