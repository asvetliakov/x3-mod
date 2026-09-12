#!/bin/sh
# Resource reader / engine probe fixture: the production core (no-SSE build),
# the hook module, the patch arena and the probes with their fixture seams,
# linked into a console executable that loads the game's real zlib1.dll at run
# time (the runner copies it next to the executable; never from this tree).
set -eu
cd "$(dirname "$0")"
out=build/resource_reader
mkdir -p "$out"
sh build_loading_light.sh "$out" -DX3M_LOADING_PROBES_FIXTURE -DX3M_RESOURCE_READER_FIXTURE
cxx=i686-w64-mingw32-g++
abi="-msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2"
"$cxx" $abi -std=c++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type -Wno-misleading-indentation -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
  -DX3M_LOADING_PROBES_FIXTURE -DX3M_RESOURCE_READER_FIXTURE -static -static-libgcc -static-libstdc++ \
  resource_reader_fixture.cpp \
  "$out/loading_trace_light.o" "$out/resource_reader_core.o" "$out/engine_patch.o" "$out/loading_probes.o" "$out/resource_reader.o" \
  -o "$out/resource_reader_fixture.exe"
