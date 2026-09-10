#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -static -DX3M_MESH_CACHE_FIXTURE mesh_adjacency_cache_fixture.cpp ../../src/proxy/mesh_adjacency_cache.cpp -o build/mesh_adjacency_cache_fixture.exe -luser32
