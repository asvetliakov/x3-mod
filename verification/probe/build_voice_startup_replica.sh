#!/bin/sh
# Builds the startup replica with the production DMO fallback hook linked in
# (mode game-dmo-hook installs it through engine_patch on a replica site).
set -eu
cd "$(dirname "$0")/../.."
out=verification/probe/build/voice-startup-replica
mkdir -p "$out"
cxx=i686-w64-mingw32-g++
common="-std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -DX3M_VOICE_DMO_FIXTURE"
hook="-DWIN32_LEAN_AND_MEAN -DNOMINMAX"
$cxx $common $hook -c src/proxy/voice_dmo_fallback.cpp -o "$out/voice_dmo_fallback.o"
$cxx $common $hook -c src/proxy/engine_patch.cpp -o "$out/engine_patch.o"
$cxx $common $hook -mno-sse -mno-mmx -mfpmath=387 -c src/proxy/engine_memory.cpp -o "$out/engine_memory.o"
$cxx $common -municode -static -ffunction-sections verification/probe/voice_startup_replica.cpp "$out/voice_dmo_fallback.o" "$out/engine_patch.o" "$out/engine_memory.o" -Wl,--gc-sections -o "$out/voice_startup_replica.exe" -lole32 -loleaut32 -luuid -lstrmiids -ldsound -ldxguid -lwinmm
shasum -a 256 "$out/voice_startup_replica.exe"
