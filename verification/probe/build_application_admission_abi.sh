#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
flags="-std=c++17 -O2 -Wall -Wextra -Werror -pthread -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2"
# Only the thin adapter suppresses compiler EH bookends. The core and caller
# keep their normal exception policy; this is not a global production option.
i686-w64-mingw32-g++ $flags -fno-exceptions -c ../../src/ownership/application_admission_abi.cpp -o build/application_admission_abi.o
i686-w64-mingw32-g++ $flags -c ../../src/ownership/application_admission.cpp -o build/application_admission_abi_core.o
i686-w64-mingw32-g++ $flags -c application_admission_abi_fixture.cpp -o build/application_admission_abi_fixture.o
i686-w64-mingw32-g++ -pthread -static build/application_admission_abi.o build/application_admission_abi_core.o build/application_admission_abi_fixture.o -o build/application_admission_abi_fixture.exe
