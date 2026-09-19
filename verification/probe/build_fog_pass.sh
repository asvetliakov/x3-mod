#!/bin/sh
# Detached volumetric fog fixture plus the production pass (embedded programs
# from src/renderer/fog_*_program_inc.h; no compiler DLL at run time).
# X3M_FOG_PASS_FIXTURE exposes the sky-history accessors used for readback only.
set -eu
cd "$(dirname "$0")"
mkdir -p build/fog-pass
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -static -DX3M_FOG_PASS_FIXTURE fog_pass_fixture.cpp ../../src/renderer/fog_pass.cpp -o build/fog-pass/fog_pass_fixture.exe -luser32
