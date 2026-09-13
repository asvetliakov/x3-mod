#!/bin/sh
# Detached prototype only. Builds EXE and freezes authored composite CSO natively;
# never invokes Wine, production CMake, installation or another fixture runner.
set -eu
cd "$(dirname "$0")"
mkdir -p build/distance-fade
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -static -DX3M_LINEAR_EMISSION_PASS_FIXTURE -DX3M_LINEAR_DISTANCE_FADE_FIXTURE linear_material_fixture.cpp ../../src/renderer/linear_material.cpp ../../src/renderer/material_motion.cpp ../../src/renderer/linear_emission_pass.cpp -o build/distance-fade/linear_material_fixture.exe -luser32 -ldxguid -luuid
c++ -std=c++17 -O2 -Wall -Wextra -Werror linear_distance_fade_structure.cpp ../../src/renderer/linear_material.cpp ../../src/renderer/material_motion.cpp -o build/distance-fade/structure
build/distance-fade/structure --composite build/distance-fade/composite.bin
