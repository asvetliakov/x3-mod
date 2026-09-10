#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -Wall -Wextra -Werror -static \
 scene_capture_fixture.cpp ../../src/ownership/d3d9_ownership.cpp ../../src/ownership/execution_state.cpp ../../src/ownership/finite_buffer_evidence.cpp ../../src/ownership/managed_upload_contract.cpp \
 ../../src/proxy/scene_capture.cpp ../../src/proxy/capture_state.cpp \
 -o build/scene_capture_fixture.exe -luser32 -ladvapi32 -ldxguid
