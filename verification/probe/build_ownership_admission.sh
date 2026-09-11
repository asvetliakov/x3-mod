#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build/ownership_admission
sh build_admission_dependencies.sh build/ownership_admission
flags="-std=c++17 -O2 -Wall -Wextra -Werror -pthread -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2"
i686-w64-mingw32-g++ $flags -c ../../src/ownership/d3d9_ownership.cpp -o build/ownership_admission/ownership.o
i686-w64-mingw32-g++ $flags -static ownership_admission_fixture.cpp \
 build/ownership_admission/ownership.o build/ownership_admission/application_admission.o build/ownership_admission/application_admission_abi.o \
 ../../src/ownership/execution_state.cpp ../../src/ownership/finite_buffer_evidence.cpp ../../src/ownership/portable_managed_upload.cpp \
 -o build/ownership_admission_fixture.exe -ldxguid -luser32 -ladvapi32
