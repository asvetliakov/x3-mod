#!/bin/sh
# Independent 32-bit Windows capability probe; deliberately outside production src.
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -static-libgcc -static-libstdc++ capability_probe.cpp -o build/capability_probe.exe -ldxguid -luser32 -lole32
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -static-libgcc -static-libstdc++ d3d9_smoke.cpp -o build/d3d9_smoke.exe -ldxguid -luser32

# Compile-only SDK ABI assertions for production vtable interception.
i686-w64-mingw32-g++ -std=c++17 -Wall -Wextra -c abi_check.cpp -o build/abi_check.o

# Extended capture fixture; runs independently of X3.
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -static-libgcc -static-libstdc++ capture_state_fixture.cpp -o build/capture_state_fixture.exe -ldxguid -luser32
