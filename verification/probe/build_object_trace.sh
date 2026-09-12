#!/bin/sh
# Original synthetic executable only. No game install or launch.
set -eu
cd "$(dirname "$0")"
mkdir -p build
# engine_memory.cpp is built without SSE/MMX, as in CMakeLists.txt: it runs
# inside the mutation scopes whose XMM state the in-mutation probe compares.
i686-w64-mingw32-g++ -DX3M_OBJECT_TRACE_FIXTURE -std=c++17 -O2 -Wall -Wextra -Werror -mno-sse -mno-mmx -mfpmath=387 -mstackrealign -mincoming-stack-boundary=2 \
  -c ../../src/proxy/engine_memory.cpp -o build/engine_memory_object_trace.o
i686-w64-mingw32-g++ -DX3M_OBJECT_TRACE_FIXTURE -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -static \
  object_trace.cpp ../../src/proxy/object_trace.cpp build/engine_memory_object_trace.o -o build/object_trace.exe -ladvapi32
