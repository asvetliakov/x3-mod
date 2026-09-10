#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
clang++ -std=c++17 -O2 -Wall -Wextra -Werror finite_buffer_evidence.cpp ../../src/ownership/finite_buffer_evidence.cpp -o build/finite_buffer_evidence
clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer finite_buffer_evidence.cpp ../../src/ownership/finite_buffer_evidence.cpp -o build/finite_buffer_evidence_sanitized
# Compile the actual target core as well; this object is inspected for accidental
# x87/SSE floating arithmetic. The portable fixtures execute on the host CPU.
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -c ../../src/ownership/finite_buffer_evidence.cpp -o build/finite_buffer_evidence_i686.o
