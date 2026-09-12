#!/bin/sh
# Standalone fake D3DX + named-import fixture. Never place the fake DLL in X3.
set -eu
cd "$(dirname "$0")"
admission_objects_dir=build/loading_trace_admission
sh build_admission_dependencies.sh "$admission_objects_dir"
mkdir -p build/loading_trace
# The light rows, probe machinery and resource reader that loading_trace.cpp now reports through.
light_objects_dir=build/loading_light
sh build_loading_light.sh "$light_objects_dir" -DX3M_LOADING_TRACE_FIXTURE
light_objects="$light_objects_dir/loading_trace_light.o $light_objects_dir/resource_reader_core.o $light_objects_dir/engine_patch.o $light_objects_dir/loading_probes.o $light_objects_dir/resource_reader.o"
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
  loading_trace_fixture.cpp ../../src/proxy/loading_trace.cpp $light_objects ../../src/proxy/gz_buffer.cpp ../../src/proxy/mesh_adjacency_cache.cpp ../../src/proxy/mesh_adjacency_fast.cpp ../../src/ownership/d3d9_ownership.cpp "$admission_objects_dir/application_admission.o" "$admission_objects_dir/application_admission_abi.o" -pthread ../../src/ownership/execution_state.cpp ../../src/ownership/finite_buffer_evidence.cpp ../../src/ownership/portable_managed_upload.cpp \
  build/loading_trace/libd3dx9_fixture.a build/loading_trace/libz_fixture.a build/loading_trace/libxml_fixture.a -luser32 -ldxguid -ladvapi32 -o build/loading_trace/loading_trace_fixture.exe
mkdir -p build/loading_mesh
"$cxx" $abi -std=c++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
  -DX3M_LOADING_TRACE_FIXTURE -static -static-libgcc -static-libstdc++ \
  loading_mesh_fixture.cpp ../../src/proxy/loading_trace.cpp $light_objects ../../src/proxy/gz_buffer.cpp ../../src/proxy/mesh_adjacency_cache.cpp ../../src/proxy/mesh_adjacency_fast.cpp ../../src/ownership/d3d9_ownership.cpp "$admission_objects_dir/application_admission.o" "$admission_objects_dir/application_admission_abi.o" -pthread ../../src/ownership/execution_state.cpp ../../src/ownership/finite_buffer_evidence.cpp ../../src/ownership/portable_managed_upload.cpp \
  build/loading_trace/libd3dx9_fixture.a -luser32 -ldxguid -ladvapi32 -o build/loading_mesh/loading_mesh_fixture.exe
mkdir -p build/mesh_adjacency_fast
"$cxx" $abi -std=c++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
  -DX3M_LOADING_TRACE_FIXTURE -static -static-libgcc -static-libstdc++ \
  mesh_adjacency_fast_fixture.cpp ../../src/proxy/loading_trace.cpp $light_objects ../../src/proxy/gz_buffer.cpp ../../src/proxy/mesh_adjacency_cache.cpp ../../src/proxy/mesh_adjacency_fast.cpp ../../src/ownership/d3d9_ownership.cpp "$admission_objects_dir/application_admission.o" "$admission_objects_dir/application_admission_abi.o" -pthread ../../src/ownership/execution_state.cpp ../../src/ownership/finite_buffer_evidence.cpp ../../src/ownership/portable_managed_upload.cpp \
  build/loading_trace/libd3dx9_fixture.a -luser32 -ldxguid -ladvapi32 -o build/mesh_adjacency_fast/mesh_adjacency_fast_fixture.exe
