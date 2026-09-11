#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
 -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 \
 managed_upload_contract_fixture.cpp managed_upload_contract.cpp \
 -o build/managed_upload_contract_fixture.exe -ladvapi32 -luser32
