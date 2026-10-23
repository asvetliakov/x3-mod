#!/bin/sh
# Builds walk_bench.exe (i686 MinGW, production flags: -O2 SSE2 for the core, engine_memory.cpp with -mno-sse) and runs it
# natively through WSL interop. Not Wine, no game. Usage: sh run.sh [frames]
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../../.." && pwd); out=${TMPDIR:-/tmp}/walk_bench
mkdir -p "$out"
i686-w64-mingw32-g++ -std=c++17 -O2 -mno-sse -mno-mmx -mfpmath=387 -c "$root/src/proxy/engine_memory.cpp" -o "$out/em.o"
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -msse2 -mfpmath=sse -mstackrealign -I "$root/src/proxy" -c "$here/walk_bench.cpp" -o "$out/wb.o"
i686-w64-mingw32-g++ -static "$out/wb.o" "$out/em.o" -o "$out/walk_bench.exe"
cp "$out/walk_bench.exe" /mnt/c/Users/Public/walk_bench.exe 2>/dev/null && exe=/mnt/c/Users/Public/walk_bench.exe || exe="$out/walk_bench.exe"
"$exe" "${1:-2000}"
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -msse2 -mfpmath=sse -mstackrealign -I "$root/src/proxy" -static "$here/gz_bench.cpp" -o "$out/gz_bench.exe"
cp "$out/gz_bench.exe" /mnt/c/Users/Public/gz_bench.exe 2>/dev/null && gz=/mnt/c/Users/Public/gz_bench.exe || gz="$out/gz_bench.exe"
"$gz"
