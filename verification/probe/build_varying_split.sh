#!/bin/sh
# Detached authored shader fixture; no production objects or runtime compiler.
set -eu
cd "$(dirname "$0")"
mkdir -p build/varying-split
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -static varying_split_fixture.cpp -o build/varying-split/varying_split_fixture.exe -luser32
