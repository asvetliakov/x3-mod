#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
# X3M_QUAD_FVF_SWITCH: the fixture-only XYZRHW quad twin (X3M_FIXTURE_QUAD_FVF=1
# at initialize), never compiled into production.
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -DX3M_QUAD_FVF_SWITCH -static temporal_pass_fixture.cpp ../../src/renderer/temporal_pass.cpp -o build/temporal_pass_fixture.exe -luser32
