#!/bin/sh
# Build only the documented-API silent voice probe; Wine execution is root-owned.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p verification/probe/build/voice-stream
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -municode -static verification/probe/voice_stream_probe.cpp -o verification/probe/build/voice-stream/voice_stream_probe.exe -lole32 -loleaut32 -luuid -lstrmiids -ldsound -ldxguid -lwinmm
shasum -a 256 verification/probe/build/voice-stream/voice_stream_probe.exe
