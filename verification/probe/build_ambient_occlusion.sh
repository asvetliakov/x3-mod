#!/bin/sh
# Detached ambient occlusion fixture plus the production pass (embedded
# programs from src/renderer/ambient_occlusion_*_program_inc.h; no compiler
# DLL at run time). X3M_AMBIENT_OCCLUSION_FIXTURE exposes the half-depth
# accessor used for the upsample check only.
set -eu
cd "$(dirname "$0")"
mkdir -p build/ambient-occlusion
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -static -DX3M_AMBIENT_OCCLUSION_FIXTURE ambient_occlusion_fixture.cpp ../../src/renderer/ambient_occlusion_pass.cpp -o build/ambient-occlusion/ambient_occlusion_fixture.exe -luser32
