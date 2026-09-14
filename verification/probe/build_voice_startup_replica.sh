#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p verification/probe/build/voice-startup-replica
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -municode -static -ffunction-sections verification/probe/voice_startup_replica.cpp -Wl,--gc-sections -o verification/probe/build/voice-startup-replica/voice_startup_replica.exe -lole32 -loleaut32 -luuid -lstrmiids -ldsound -ldxguid -lwinmm
shasum -a 256 verification/probe/build/voice-startup-replica/voice_startup_replica.exe
