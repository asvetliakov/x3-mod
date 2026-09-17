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
# The fixture links the production TemporalPass as its reference resolve (TAA
# environments) with the same embedded resolve bytecode the DLL carries, and
# the production SunShadowApplyPass and ShadowReplayPass it drives directly in the "sunapply" mode.
# X3M_QUAD_FVF_SWITCH (fixture and seam only): X3M_FIXTURE_QUAD_FVF=1 selects
# the previous XYZRHW quads so the seam-taa-quad-fvf twin proves the vs_3_0
# quads byte-identical; production never compiles the switch.
i686-w64-mingw32-g++ $FLAGS -DX3M_QUAD_FVF_SWITCH -static -static-libgcc -static-libstdc++ motion_output_fixture.cpp ../../src/renderer/temporal_pass.cpp ../../src/renderer/sun_shadow_apply_pass.cpp ../../src/renderer/shadow_replay_pass.cpp -o build/motion_output_fixture.exe -ldxguid -luser32

if [ "${1:-}" = "--fixture-only" ]; then exit 0; fi

OBJECTS=../../build/CMakeFiles/d3d9.dir/src
test -f "$OBJECTS/proxy/capture.cpp.obj" || { echo "build/ objects missing; run the CMake build first" >&2; exit 1; }
# MotionOutput references the material and emission transformers. They are shared
# unchanged from the candidate build, never rebuilt implicitly by this runner.
test -f "$OBJECTS/renderer/linear_material.cpp.obj" || { echo "build/ linear material object missing; run the CMake build first" >&2; exit 1; }
test -f "$OBJECTS/renderer/linear_emission_pass.cpp.obj" || { echo "build/ linear emission object missing; run the CMake build first" >&2; exit 1; }
BRIDGE=../../build/compositor_bridge
# These external objects sit outside CMakeFiles/d3d9.dir/src. Reuse the exact
# qualified production SEH package, including its narrow compiler-runtime import.
for artifact in compositor_bridge.o compositor_bridge_seh_gnu.obj libx3m_compositor_seh_runtime.a; do
  test -f "$BRIDGE/$artifact" || { echo "build/ compositor package missing; run the CMake build first" >&2; exit 1; }
done
DEFINES="-DWIN32_LEAN_AND_MEAN -DNOMINMAX -DX3M_MOTION_OUTPUT_FIXTURE -DX3M_QUAD_FVF_SWITCH -DX3M_LINEAR_EMISSION_PASS_FIXTURE"
i686-w64-mingw32-g++ $FLAGS -g $DEFINES -c ../../src/proxy/capture.cpp -o build/motion-output-seam/capture.o
i686-w64-mingw32-g++ $FLAGS -g $DEFINES -c ../../src/proxy/motion_output.cpp -o build/motion-output-seam/motion_output.o
i686-w64-mingw32-g++ $FLAGS -g $DEFINES -c ../../src/proxy/camera_state.cpp -o build/motion-output-seam/camera_state.o
i686-w64-mingw32-g++ $FLAGS -g $DEFINES -c ../../src/proxy/sun_light_poll.cpp -o build/motion-output-seam/sun_light_poll.o
i686-w64-mingw32-g++ $FLAGS -g $DEFINES -fno-exceptions -c ../../src/proxy/scene_hook.cpp -o build/motion-output-seam/scene_hook.o
# The HDR pass carries the fault-injection seam of the design's case 4; the
# temporal pass carries the quad twin switch.
i686-w64-mingw32-g++ $FLAGS -g $DEFINES -c ../../src/renderer/hdr_pass.cpp -o build/motion-output-seam/hdr_pass.o
i686-w64-mingw32-g++ $FLAGS -g $DEFINES -c ../../src/renderer/temporal_pass.cpp -o build/motion-output-seam/temporal_pass.o
i686-w64-mingw32-g++ $FLAGS -g $DEFINES -c ../../src/renderer/linear_emission_pass.cpp -o build/motion-output-seam/linear_emission_pass.o
SHARED=$(find "$OBJECTS" -name '*.obj' ! -name 'capture.cpp.obj' ! -name 'motion_output.cpp.obj' ! -name 'camera_state.cpp.obj' ! -name 'sun_light_poll.cpp.obj' ! -name 'scene_hook.cpp.obj' ! -name 'hdr_pass.cpp.obj' ! -name 'temporal_pass.cpp.obj' ! -name 'linear_emission_pass.cpp.obj' | sort)
i686-w64-mingw32-g++ -shared -static -static-libgcc -static-libstdc++ -Wl,--kill-at -Wl,--enable-stdcall-fixup \
  -o build/motion-output-seam/d3d9.dll build/motion-output-seam/capture.o build/motion-output-seam/motion_output.o build/motion-output-seam/camera_state.o build/motion-output-seam/sun_light_poll.o build/motion-output-seam/scene_hook.o build/motion-output-seam/hdr_pass.o build/motion-output-seam/temporal_pass.o build/motion-output-seam/linear_emission_pass.o $SHARED \
  "$BRIDGE/compositor_bridge.o" "$BRIDGE/compositor_bridge_seh_gnu.obj" \
  "$BRIDGE/libx3m_compositor_seh_runtime.a" ../../src/proxy/d3d9.def -ldxguid -ladvapi32
i686-w64-mingw32-objdump -p build/motion-output-seam/d3d9.dll | grep -q x3m_motion_output_fixture_configure
i686-w64-mingw32-objdump -p build/motion-output-seam/d3d9.dll | grep -q x3m_camera_state_fixture_install
i686-w64-mingw32-objdump -p build/motion-output-seam/d3d9.dll | grep -q x3m_scene_hook_fixture_install
i686-w64-mingw32-objdump -p build/motion-output-seam/d3d9.dll | grep -q x3m_sun_light_poll_fixture_install
i686-w64-mingw32-objdump -p build/motion-output-seam/d3d9.dll | grep -q x3m_hdr_fixture_fault
i686-w64-mingw32-objdump -p build/motion-output-seam/d3d9.dll | grep -q x3m_hdr_fixture_exposure
i686-w64-mingw32-objdump -p build/motion-output-seam/d3d9.dll | grep -q x3m_shadow_replay_fixture_readback
i686-w64-mingw32-objdump -p build/motion-output-seam/d3d9.dll | grep -q x3m_shadow_replay_fixture_cascade_readback
i686-w64-mingw32-objdump -p build/motion-output-seam/d3d9.dll | grep -q x3m_sun_shadow_fixture_toggle
i686-w64-mingw32-objdump -p build/motion-output-seam/d3d9.dll | grep -q x3m_shadow_own_ship_fixture_install
