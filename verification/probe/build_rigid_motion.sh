#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -static rigid_motion_fixture.cpp ../../src/renderer/rigid_motion.cpp -o build/rigid_motion_fixture.exe -luser32
