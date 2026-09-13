#!/bin/sh
# Independent authored fixture only: no production objects/generated headers.
set -eu
cd "$(dirname "$0")"
mkdir -p build/linear-emission
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -static linear_emission_fixture.cpp -o build/linear-emission/linear_emission_fixture.exe -luser32
