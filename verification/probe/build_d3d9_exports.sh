#!/bin/sh
# Export-table fixture (W1/W3 of the native-Windows audit): a console program
# that loads the proxy next to it and resolves/calls the system d3d9 exports.
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 \
  -static -static-libgcc -static-libstdc++ d3d9_exports_fixture.cpp -o build/d3d9_exports_fixture.exe
