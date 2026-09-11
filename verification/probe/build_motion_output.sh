#!/bin/sh
# Builds the live motion route fixture and a seam DLL. The seam DLL links the
# freshly built production objects from build/ with capture.cpp and
# motion_output.cpp recompiled under X3M_MOTION_OUTPUT_FIXTURE, exactly as the
# ownership fallback runner links a test double; production build/d3d9.dll is
# tested unchanged in the "production" mode.
set -eu
cd "$(dirname "$0")"
mkdir -p build/motion-output-seam
FLAGS="-std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2"
i686-w64-mingw32-g++ $FLAGS -static -static-libgcc -static-libstdc++ motion_output_fixture.cpp -o build/motion_output_fixture.exe -ldxguid -luser32

OBJECTS=../../build/CMakeFiles/d3d9.dir/src
test -f "$OBJECTS/proxy/capture.cpp.obj" || { echo "build/ objects missing; run the CMake build first" >&2; exit 1; }
DEFINES="-DWIN32_LEAN_AND_MEAN -DNOMINMAX -DX3M_MOTION_OUTPUT_FIXTURE"
i686-w64-mingw32-g++ $FLAGS -g $DEFINES -c ../../src/proxy/capture.cpp -o build/motion-output-seam/capture.o
i686-w64-mingw32-g++ $FLAGS -g $DEFINES -c ../../src/proxy/motion_output.cpp -o build/motion-output-seam/motion_output.o
SHARED=$(find "$OBJECTS" -name '*.obj' ! -name 'capture.cpp.obj' ! -name 'motion_output.cpp.obj' | sort)
i686-w64-mingw32-g++ -shared -static -static-libgcc -static-libstdc++ -Wl,--kill-at -Wl,--enable-stdcall-fixup \
  -o build/motion-output-seam/d3d9.dll build/motion-output-seam/capture.o build/motion-output-seam/motion_output.o $SHARED \
  ../../src/proxy/d3d9.def -ldxguid -ladvapi32
i686-w64-mingw32-objdump -p build/motion-output-seam/d3d9.dll | grep -q x3m_motion_output_fixture_configure
