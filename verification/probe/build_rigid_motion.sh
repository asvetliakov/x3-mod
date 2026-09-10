#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -DX3M_RIGID_MOTION_VERIFICATION=1 -static rigid_motion_fixture.cpp ../../src/renderer/rigid_motion.cpp ../../src/renderer/rigid_replay_program.cpp ../../src/renderer/rigid_position.cpp -o build/rigid_motion_fixture.exe -luser32
# Separately prove the production translation unit has no verification issuer.
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -c ../../src/renderer/rigid_motion.cpp -o build/rigid_motion_production_contract.o
