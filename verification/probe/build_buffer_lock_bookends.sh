#!/bin/sh
set -eu
cd "$(dirname "$0")"
build_dir="${X3M_LOCK_BOOKENDS_BUILD_DIR:-build/buffer_lock_bookends}"
sh build_admission_dependencies.sh "$build_dir"
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
 -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 \
 buffer_lock_bookends_fixture.cpp "$build_dir/application_admission.o" "$build_dir/application_admission_abi.o" \
 -pthread ../../src/ownership/execution_state.cpp ../../src/ownership/finite_buffer_evidence.cpp \
 ../../src/ownership/portable_managed_upload.cpp -o "$build_dir/buffer_lock_bookends.exe" -ldxguid -luser32 -ladvapi32
