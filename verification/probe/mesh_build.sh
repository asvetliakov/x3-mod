#!/bin/sh
# Build only original standalone mesh-cache characterization.
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -static mesh_preparation.cpp -o build/mesh_preparation.exe -luser32
