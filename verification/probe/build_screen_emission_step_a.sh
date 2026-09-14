#!/bin/sh
# Step A detached fixture (docs/architecture/screen-emission-region.md): the
# production pass with the packed policy inside the frozen packed prototype's
# corpus. Standalone EXE only; never builds a proxy DLL or runs Wine/the game.
set -eu
cd "$(dirname "$0")"
mkdir -p build
CXX=${CXX:-i686-w64-mingw32-g++}
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse \
  -mstackrealign -mincoming-stack-boundary=2 -static -static-libgcc -static-libstdc++ \
  -DX3M_LINEAR_EMISSION_PASS_FIXTURE \
  screen_emission_step_a_fixture.cpp ../../src/renderer/linear_emission_sm1.cpp \
  ../../src/renderer/linear_emission_pass.cpp \
  -o build/screen_emission_step_a_fixture.exe -ld3d9 -ldxguid -luuid -luser32
