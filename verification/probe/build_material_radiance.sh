#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -msse2 -mfpmath=sse -Wall -Wextra -Werror -static \
 material_radiance_fixture.cpp ../../src/renderer/material_radiance.cpp \
 -o build/material_radiance_fixture.exe -luser32
