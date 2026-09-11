#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -static rigid_retirement_fixture.cpp ../../src/renderer/rigid_motion.cpp ../../src/renderer/rigid_replay_program.cpp ../../src/renderer/rigid_position.cpp -o build/rigid_retirement_fixture.exe -luser32
