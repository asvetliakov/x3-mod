#!/bin/sh
# Build only the detached fixture. No production DLL or Wine execution.
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -static linear_material_fixture.cpp ../../src/renderer/linear_material.cpp ../../src/renderer/material_motion.cpp -o build/linear_material_fixture.exe -luser32
