#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build/mesh_cache_hook
i686-w64-mingw32-dlltool --kill-at -D d3dx9_37.dll -d loading_trace_stub.def -l build/mesh_cache_hook/libnative_mesh.a
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DX3M_LOADING_TRACE_FIXTURE -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -static mesh_cache_hook_fixture.cpp ../../src/proxy/loading_trace.cpp ../../src/proxy/mesh_adjacency_cache.cpp ../../src/ownership/d3d9_ownership.cpp build/mesh_cache_hook/libnative_mesh.a -ldxguid -luser32 -ladvapi32 -o build/mesh_cache_hook/mesh_cache_hook_fixture.exe
