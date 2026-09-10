#!/bin/sh
# Separate baseline/wrapper executables; this does not edit the shipping DLL.
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static -static-libgcc -static-libstdc++ ownership_fixture.cpp -o build/ownership_baseline.exe -ldxguid -luser32
if [ "${1:-}" != "--baseline-only" ]; then
  i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static -static-libgcc -static-libstdc++ -DX3M_OWNERSHIP_WRAPPED ownership_fixture.cpp ../../src/ownership/d3d9_ownership.cpp -o build/ownership_wrapped.exe -ldxguid -luser32
fi
