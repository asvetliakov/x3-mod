#!/bin/sh
set -eu
cd "$(dirname "$0")"
admission_objects_dir=build/managed_upload_performance_admission
sh build_admission_dependencies.sh "$admission_objects_dir"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
 -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 \
 managed_upload_performance.cpp ../../src/ownership/d3d9_ownership.cpp "$admission_objects_dir/application_admission.o" "$admission_objects_dir/application_admission_abi.o" -pthread \
 ../../src/ownership/execution_state.cpp ../../src/ownership/finite_buffer_evidence.cpp \
 ../../src/ownership/portable_managed_upload.cpp \
 -o build/managed_upload_performance.exe -ldxguid -luser32
