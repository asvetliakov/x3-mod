#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static -static-libgcc -static-libstdc++ ownership_integration_lifetime.cpp -o build/ownership_integration_lifetime.exe -ldxguid -luser32
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static -static-libgcc -static-libstdc++ ownership_integration_auto_depth.cpp -o build/ownership_integration_auto_depth.exe -ldxguid -luser32
# Keep this copy separate from standalone ownership provenance.
i686-w64-mingw32-g++ -std=c++17 -O2 -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -Wall -Wextra -Werror -static -static-libgcc -static-libstdc++ ownership_fixture.cpp -o build/ownership_integration_baseline.exe -ldxguid -luser32 -ladvapi32
