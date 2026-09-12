#!/bin/sh
# Objects the loading-trace fixtures link since loading_trace.cpp gained the
# light rows, the engine probes and the resource reader: the two no-SSE units
# are compiled exactly as CMakeLists.txt compiles them (-mno-sse -mno-mmx
# -mfpmath=387; they do no floating-point work), the others with the ordinary
# SSE2 ABI flags. Usage: build_loading_light.sh <out-dir> [extra flags...]
set -eu
cd "$(dirname "$0")"
test "$#" -ge 1
out=$1; shift
mkdir -p "$out"
cxx=i686-w64-mingw32-g++
common="-std=c++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type -DWIN32_LEAN_AND_MEAN -DNOMINMAX -mstackrealign -mincoming-stack-boundary=2"
"$cxx" $common -mno-sse -mno-mmx -mfpmath=387 "$@" -c ../../src/proxy/loading_trace_light.cpp -o "$out/loading_trace_light.o"
"$cxx" $common -mno-sse -mno-mmx -mfpmath=387 "$@" -c ../../src/proxy/resource_reader_core.cpp -o "$out/resource_reader_core.o"
"$cxx" $common -msse2 -mfpmath=sse "$@" -c ../../src/proxy/engine_patch.cpp -o "$out/engine_patch.o"
"$cxx" $common -msse2 -mfpmath=sse "$@" -c ../../src/proxy/loading_probes.cpp -o "$out/loading_probes.o"
"$cxx" $common -msse2 -mfpmath=sse "$@" -c ../../src/proxy/resource_reader.cpp -o "$out/resource_reader.o"
