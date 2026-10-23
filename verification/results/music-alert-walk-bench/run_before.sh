#!/bin/sh
# The same walk_bench.cpp built against music_alert_core.h as of 2105a9c7 (field-by-field reads, before the span-read
# pass), so the before/after rows come from one bench and one session. Native through WSL interop; not Wine, no game.
# Usage: sh run_before.sh [frames]  (output kept in bench_output_before_rerun.txt)
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../../.." && pwd); out=${TMPDIR:-/tmp}/walk_bench_before
mkdir -p "$out/include"
git -C "$root" show 2105a9c7:src/proxy/music_alert_core.h > "$out/include/music_alert_core.h"
i686-w64-mingw32-g++ -std=c++17 -O2 -mno-sse -mno-mmx -mfpmath=387 -c "$root/src/proxy/engine_memory.cpp" -o "$out/em.o"
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -msse2 -mfpmath=sse -mstackrealign -I "$out/include" -I "$root/src/proxy" -c "$here/walk_bench.cpp" -o "$out/wb.o"
i686-w64-mingw32-g++ -static "$out/wb.o" "$out/em.o" -o "$out/walk_bench_before.exe"
cp "$out/walk_bench_before.exe" /mnt/c/Users/Public/walk_bench_before.exe 2>/dev/null && exe=/mnt/c/Users/Public/walk_bench_before.exe || exe="$out/walk_bench_before.exe"
"$exe" "${1:-2000}"
