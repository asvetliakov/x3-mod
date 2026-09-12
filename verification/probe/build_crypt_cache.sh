#!/bin/sh
# CryptoAPI context/key cache fixture: the production module (src/proxy/crypt_cache.cpp,
# compiled as CMakeLists.txt does: no SSE/MMX, integer only) linked into a console
# executable that drives the bottle's real ADVAPI32/rsaenh.
set -eu
cd "$(dirname "$0")"
mkdir -p build/crypt_cache
cxx=i686-w64-mingw32-g++
abi="-msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2"
"$cxx" -std=c++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
  -mno-sse -mno-mmx -mfpmath=387 -mstackrealign -mincoming-stack-boundary=2 \
  -c ../../src/proxy/crypt_cache.cpp -o build/crypt_cache/crypt_cache.o
"$cxx" $abi -std=c++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
  -static -static-libgcc -static-libstdc++ \
  crypt_cache_fixture.cpp build/crypt_cache/crypt_cache.o -ladvapi32 -o build/crypt_cache/crypt_cache_fixture.exe
