#!/bin/sh
# Build the stored-density generator witness tool: native host (clang++, the
# host architecture; -msse2 is an x86-only flag), optional x86_64 host build
# under Rosetta with the SSE2 flags, and the i686 MinGW cross build with the
# project's SSE2 / four-byte incoming stack flags. FP contraction is disabled
# everywhere so the word-identity gate holds on every target.
# Usage: build_fog_density_generator_host.sh [output-dir]  (default build/)
set -eu
out="${1:-$(dirname "$0")/build}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
cd "$(dirname "$0")"
common="-std=c++17 -O2 -Wall -Wextra -Werror -ffp-contract=off"
sources="fog_density_generator_host.cpp ../../src/fog/fog_density_generator.cpp"
clang++ $common $sources -o "$out/fog_density_generator_host"
if clang++ -arch x86_64 -c -x c++ /dev/null -o /dev/null 2>/dev/null; then
  clang++ -arch x86_64 -msse2 -mfpmath=sse $common $sources -o "$out/fog_density_generator_host_x86_64"
fi
i686-w64-mingw32-g++ $common -static -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 \
  $sources -o "$out/fog_density_generator_host.exe"
echo "built $out/fog_density_generator_host $out/fog_density_generator_host.exe"
