#!/bin/sh
# Standalone fake D3DX + named-import fixture. Never place the fake DLL in X3.
set -eu
cd "$(dirname "$0")"
mkdir -p build/loading_trace
cxx=i686-w64-mingw32-g++
abi="-msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2"
"$cxx" $abi -std=c++17 -O2 -Wall -Wextra -shared -static -static-libgcc -static-libstdc++ \
  loading_trace_stub.cpp -o build/loading_trace/d3dx9_37.dll \
  -Wl,--kill-at -Wl,--out-implib,build/loading_trace/libd3dx9_fixture.a
"$cxx" $abi -std=c++17 -O2 -Wall -Wextra -shared -static -static-libgcc -static-libstdc++ \
  loading_codec_stub.cpp -o build/loading_trace/zlib1.dll -Wl,--out-implib,build/loading_trace/libz_fixture.a
"$cxx" $abi -std=c++17 -O2 -Wall -Wextra -shared -static -static-libgcc -static-libstdc++ -DXML_ONLY \
  loading_codec_stub.cpp -o build/loading_trace/libxml2.dll -Wl,--out-implib,build/loading_trace/libxml_fixture.a
i686-w64-mingw32-dlltool --kill-at -D d3dx9_37.dll -d loading_trace_stub.def -l build/loading_trace/libd3dx9_fixture.a
"$cxx" $abi -std=c++17 -O2 -Wall -Wextra -Wno-cast-function-type -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
  -DX3M_LOADING_TRACE_FIXTURE -static -static-libgcc -static-libstdc++ \
  loading_trace_fixture.cpp ../../src/proxy/loading_trace.cpp ../../src/proxy/mesh_adjacency_cache.cpp ../../src/ownership/d3d9_ownership.cpp ../../src/ownership/finite_buffer_evidence.cpp ../../src/ownership/managed_upload_contract.cpp \
  build/loading_trace/libd3dx9_fixture.a build/loading_trace/libz_fixture.a build/loading_trace/libxml_fixture.a -luser32 -ldxguid -ladvapi32 -o build/loading_trace/loading_trace_fixture.exe
mkdir -p build/loading_mesh
"$cxx" $abi -std=c++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
  -DX3M_LOADING_TRACE_FIXTURE -static -static-libgcc -static-libstdc++ \
  loading_mesh_fixture.cpp ../../src/proxy/loading_trace.cpp ../../src/proxy/mesh_adjacency_cache.cpp ../../src/ownership/d3d9_ownership.cpp ../../src/ownership/finite_buffer_evidence.cpp ../../src/ownership/managed_upload_contract.cpp \
  build/loading_trace/libd3dx9_fixture.a -luser32 -ldxguid -ladvapi32 -o build/loading_mesh/loading_mesh_fixture.exe
