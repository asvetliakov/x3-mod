#!/bin/sh
# gz read-ahead buffer fixture: the production module (src/proxy/gz_buffer.cpp)
# linked into a console executable that loads the game's real zlib1.dll at run
# time (the runner copies it next to the executable; never from this tree).
set -eu
cd "$(dirname "$0")"
mkdir -p build/gz_buffer
cxx=i686-w64-mingw32-g++
abi="-msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2"
"$cxx" $abi -std=c++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
  -static -static-libgcc -static-libstdc++ \
  gz_buffer_fixture.cpp ../../src/proxy/gz_buffer.cpp -o build/gz_buffer/gz_buffer_fixture.exe
