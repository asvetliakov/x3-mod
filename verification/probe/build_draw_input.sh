#!/bin/sh
set -eu
cd "$(dirname "$0")"
admission_objects_dir=build/draw_input_admission
sh build_admission_dependencies.sh "$admission_objects_dir"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -static -DX3M_DRAW_INPUT_FIXTURE \
 draw_input_fixture.cpp ../../src/proxy/draw_input.cpp ../../src/proxy/capture_state.cpp \
 ../../src/ownership/d3d9_ownership.cpp "$admission_objects_dir/application_admission.o" "$admission_objects_dir/application_admission_abi.o" -pthread ../../src/ownership/execution_state.cpp ../../src/ownership/finite_buffer_evidence.cpp ../../src/ownership/portable_managed_upload.cpp ../../src/renderer/rigid_position.cpp \
 ../../src/renderer/rigid_motion.cpp ../../src/renderer/rigid_replay_program.cpp \
 -o build/draw_input_fixture.exe -ldxguid -luser32 -ladvapi32
