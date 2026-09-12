#!/bin/sh
set -eu
cd "$(dirname "$0")"
admission_objects_dir=build/crypt_cache_hook_admission
sh build_admission_dependencies.sh "$admission_objects_dir"
light_objects_dir=build/crypt_cache_hook_light
sh build_loading_light.sh "$light_objects_dir" -DX3M_LOADING_TRACE_FIXTURE
mkdir -p build/crypt_cache
light_objects="$light_objects_dir/loading_trace_light.o $light_objects_dir/resource_reader_core.o $light_objects_dir/crypt_cache.o $light_objects_dir/engine_patch.o $light_objects_dir/loading_probes.o $light_objects_dir/resource_reader.o"
i686-w64-mingw32-g++ -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -std=c++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DX3M_LOADING_TRACE_FIXTURE -static -pthread \
 crypt_cache_hook_fixture.cpp ../../src/proxy/loading_trace.cpp $light_objects ../../src/proxy/gz_buffer.cpp ../../src/proxy/mesh_adjacency_cache.cpp ../../src/proxy/mesh_adjacency_fast.cpp ../../src/ownership/d3d9_ownership.cpp \
 "$admission_objects_dir/application_admission.o" "$admission_objects_dir/application_admission_abi.o" ../../src/ownership/execution_state.cpp ../../src/ownership/finite_buffer_evidence.cpp ../../src/ownership/portable_managed_upload.cpp \
 -luser32 -ldxguid -ladvapi32 -Wl,--image-base,0x10000000 -o build/crypt_cache/crypt_cache_hook_fixture.exe
