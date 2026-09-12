#!/bin/sh
# Sampling profiler fixture: links the production profiler object (built with
# the DLL's flags and -Werror) into a standalone exe. The frame-pointer unit
# keeps definition order so *_end markers bound each function; the FPO unit
# omits frame pointers and stack realignment so its frames are invisible to
# the EBP chain.
set -eu
cd "$(dirname "$0")"
mkdir -p build/sampling_profiler
cxx=i686-w64-mingw32-g++
abi="-msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2"
"$cxx" $abi -std=c++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
  -c ../../src/proxy/sampling_profiler.cpp -o build/sampling_profiler/sampling_profiler.o
"$cxx" -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -fomit-frame-pointer \
  -fno-toplevel-reorder -fno-reorder-blocks-and-partition -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
  -c sampling_profiler_fpo.cpp -o build/sampling_profiler/sampling_profiler_fpo.o
"$cxx" $abi -std=c++17 -O2 -Wall -Wextra -Werror -fno-omit-frame-pointer \
  -fno-toplevel-reorder -fno-reorder-blocks-and-partition -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
  -static -static-libgcc -static-libstdc++ \
  sampling_profiler_fixture.cpp build/sampling_profiler/sampling_profiler_fpo.o build/sampling_profiler/sampling_profiler.o \
  -o build/sampling_profiler/sampling_profiler_fixture.exe
