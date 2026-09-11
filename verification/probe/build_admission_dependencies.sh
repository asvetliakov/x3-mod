#!/bin/sh
# Fresh, target-local objects shared by standalone ownership/capture fixtures.
set -eu
cd "$(dirname "$0")"
test "$#" -eq 1
admission_output_dir=$1
mkdir -p "$admission_output_dir"
admission_flags="-std=c++17 -O2 -Wall -Wextra -Werror -pthread -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2"
# Only the ABI adapter disables exceptions. The core and every caller retain
# their ordinary exception policy and the legacy four-byte incoming ABI.
i686-w64-mingw32-g++ $admission_flags -fno-exceptions -c ../../src/ownership/application_admission_abi.cpp -o "$admission_output_dir/application_admission_abi.o"
i686-w64-mingw32-g++ $admission_flags -c ../../src/ownership/application_admission.cpp -o "$admission_output_dir/application_admission.o"
