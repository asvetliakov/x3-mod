cmake_minimum_required(VERSION 3.20)
project(x3_modern_renderer VERSION 0.4.0 LANGUAGES CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Build for 32-bit Windows using cmake/mingw-i686.cmake")
endif()
add_library(d3d9 SHARED
  src/proxy/loader.cpp src/proxy/capture.cpp src/proxy/capture_state.cpp
  src/proxy/motion_capture.cpp src/ownership/execution_state.cpp
  src/ownership/application_admission.cpp src/ownership/application_admission_abi.cpp
  src/proxy/scene_capture.cpp src/proxy/object_trace.cpp src/proxy/scene_hook.cpp src/proxy/camera_state.cpp src/proxy/telemetry.cpp
  src/proxy/loading_trace.cpp src/proxy/sampling_profiler.cpp src/ownership/d3d9_ownership.cpp
  src/ownership/finite_buffer_evidence.cpp src/ownership/portable_managed_upload.cpp
  src/proxy/draw_input.cpp src/proxy/object_lifetime.cpp src/proxy/mesh_adjacency_cache.cpp
  src/renderer/temporal_pass.cpp src/renderer/hdr_pass.cpp src/renderer/exposure.cpp src/renderer/material_radiance.cpp
  src/renderer/motion_history.cpp src/renderer/rigid_position.cpp
  src/renderer/material_motion.cpp src/renderer/motion_row_history.cpp src/proxy/motion_output.cpp
  src/renderer/rigid_motion.cpp src/renderer/rigid_replay_program.cpp src/proxy/d3d9.def)
target_compile_features(d3d9 PRIVATE cxx_std_17)
# Keep compiler EH bookends out of the admission adapter's CPU-state boundary.
# The core and every other source retain their normal exception policy.
set_source_files_properties(src/ownership/application_admission_abi.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
target_compile_definitions(d3d9 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
# Our arithmetic uses SSE2 while preserving the game's i686 Windows ABI.
# Legacy callers provide four-byte stack alignment; align SIMD locals internally.
# Do not enable fast-math or globally require unverified AVX/FMA instructions.
target_compile_options(d3d9 PRIVATE -Wall -Wextra -Wno-cast-function-type -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2)
target_link_options(d3d9 PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--kill-at -Wl,--enable-stdcall-fixup)
target_link_libraries(d3d9 PRIVATE dxguid advapi32)
set_target_properties(d3d9 PROPERTIES PREFIX "" OUTPUT_NAME "d3d9")
