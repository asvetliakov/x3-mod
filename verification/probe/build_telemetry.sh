#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -static-libgcc -static-libstdc++ telemetry_fixture.cpp -o build/telemetry_fixture.exe -ldxguid -luser32
