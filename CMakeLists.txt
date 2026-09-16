cmake_minimum_required(VERSION 3.20)
project(x3_modern_renderer VERSION 0.4.0 LANGUAGES CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Build for 32-bit Windows using cmake/mingw-i686.cmake")
endif()
# This project uses GNU PE ld without a SafeSEH table. The isolated MS-target
# Clang SEH object requires the reviewed metadata-only conversion for that link.
# An enabled SafeSEH build needs a compatible linker and a separate qualification.
option(X3M_GNU_PE_NO_SAFESEH "Use the current GNU PE link without SafeSEH" ON)
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR NOT X3M_GNU_PE_NO_SAFESEH)
  message(FATAL_ERROR "The compositor bridge requires the qualified GNU/no-SafeSEH link policy")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(DEFINED ENV{X3M_SEH_CLANG} AND NOT DEFINED X3M_SEH_CLANG)
  set(X3M_SEH_CLANG "$ENV{X3M_SEH_CLANG}" CACHE FILEPATH "Clang with i686-pc-windows-msvc support")
endif()
find_program(X3M_SEH_CLANG NAMES clang REQUIRED)
# Compiled-in source commit for the session log's proxy_identity line. The
# script rewrites the generated fragment only when the value changes, so an
# unchanged tree never relinks; it runs once here (the header must exist for the
# first configure) and again on every build through x3m_source_commit.
set(X3M_SOURCE_COMMIT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/x3m_source_commit_inc.h")
set(X3M_SOURCE_COMMIT_TOOL "${CMAKE_CURRENT_SOURCE_DIR}/tools/build/write_source_commit.py")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${X3M_SOURCE_COMMIT_TOOL}"
  --root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${X3M_SOURCE_COMMIT_HEADER}"
  RESULT_VARIABLE X3M_SOURCE_COMMIT_STATUS)
if(NOT X3M_SOURCE_COMMIT_STATUS EQUAL 0)
  message(FATAL_ERROR "Failed to generate ${X3M_SOURCE_COMMIT_HEADER}")
endif()
# Reconfigure when HEAD moves or the index changes; the build-time target keeps
# the header current for working-tree edits between configures.
foreach(X3M_GIT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD" "${CMAKE_CURRENT_SOURCE_DIR}/.git/index")
  if(EXISTS "${X3M_GIT_FILE}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${X3M_GIT_FILE}")
    list(APPEND X3M_GIT_DEPENDS "${X3M_GIT_FILE}")
  endif()
endforeach()
add_custom_target(x3m_source_commit ALL
  COMMAND "${Python3_EXECUTABLE}" "${X3M_SOURCE_COMMIT_TOOL}"
    --root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${X3M_SOURCE_COMMIT_HEADER}"
  DEPENDS "${X3M_SOURCE_COMMIT_TOOL}" ${X3M_GIT_DEPENDS}
  BYPRODUCTS "${X3M_SOURCE_COMMIT_HEADER}"
  COMMENT "Refreshing compiled-in source commit" VERBATIM)
set_source_files_properties("${X3M_SOURCE_COMMIT_HEADER}" PROPERTIES GENERATED TRUE)
set(X3M_COMPOSITOR_BUILD "${CMAKE_CURRENT_BINARY_DIR}/compositor_bridge")
set(X3M_COMPOSITOR_ASM "${X3M_COMPOSITOR_BUILD}/compositor_bridge.o")
set(X3M_COMPOSITOR_SEH "${X3M_COMPOSITOR_BUILD}/compositor_bridge_seh_gnu.obj")
set(X3M_COMPOSITOR_RUNTIME "${X3M_COMPOSITOR_BUILD}/libx3m_compositor_seh_runtime.a")
# Use the configured cross compiler, not CMake's host assembler. This .S unit
# includes only compositor_bridge.h; changes to C++ callers do not rebuild it.
add_custom_command(OUTPUT "${X3M_COMPOSITOR_ASM}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${X3M_COMPOSITOR_BUILD}"
  COMMAND "${CMAKE_CXX_COMPILER}" -x assembler-with-cpp -c
    -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2
    "${CMAKE_CURRENT_SOURCE_DIR}/src/proxy/compositor_bridge.S" -o "${X3M_COMPOSITOR_ASM}"
  DEPENDS src/proxy/compositor_bridge.S src/proxy/compositor_bridge.h
  COMMENT "Building compositor CPU boundary" VERBATIM)
add_custom_command(OUTPUT "${X3M_COMPOSITOR_SEH}" "${X3M_COMPOSITOR_RUNTIME}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/build/build_compositor_bridge.py"
    --output-dir "${X3M_COMPOSITOR_BUILD}" --gnu-pe-no-safeseh
    --clang "${X3M_SEH_CLANG}" --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}"
    --objdump "${CMAKE_OBJDUMP}" --dlltool "${CMAKE_DLLTOOL}"
  DEPENDS tools/build/build_compositor_bridge.py src/proxy/compositor_bridge_seh.c src/proxy/compositor_bridge.h
  BYPRODUCTS "${X3M_COMPOSITOR_BUILD}/compositor_bridge_seh.obj"
    "${X3M_COMPOSITOR_BUILD}/compositor_bridge_seh.s"
    "${X3M_COMPOSITOR_BUILD}/compositor_bridge_seh_runtime.def"
    "${X3M_COMPOSITOR_BUILD}/compositor_bridge_build.json"
  COMMENT "Packaging compositor SEH (GNU/no-SafeSEH, narrow CRT import)" VERBATIM)
set_source_files_properties("${X3M_COMPOSITOR_ASM}" "${X3M_COMPOSITOR_SEH}"
  PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
add_library(d3d9 SHARED
  "${X3M_COMPOSITOR_ASM}" "${X3M_COMPOSITOR_SEH}"
  src/proxy/compositor_owner.cpp src/renderer/bloom_pass.cpp
  src/proxy/loader.cpp src/proxy/capture.cpp src/proxy/proxy_identity.cpp src/proxy/capture_state.cpp src/proxy/comparison_notice.cpp
  src/proxy/motion_capture.cpp src/ownership/execution_state.cpp
  src/ownership/application_admission.cpp src/ownership/application_admission_abi.cpp
  src/proxy/scene_capture.cpp src/proxy/object_trace.cpp src/proxy/scene_hook.cpp src/proxy/camera_state.cpp src/proxy/chase_camera.cpp src/proxy/chase_aim_trace.cpp src/proxy/chase_fire.cpp src/proxy/telemetry.cpp src/proxy/engine_memory.cpp src/proxy/fade_region.cpp
  src/proxy/chase_transition.cpp src/proxy/chase_lead.cpp src/proxy/game_phases.cpp src/proxy/voice_dmo_fallback.cpp src/proxy/lod_scale.cpp src/proxy/point_light_admission.cpp src/proxy/frame_timing.cpp src/proxy/frame_phases.cpp src/proxy/pass_phases.cpp src/proxy/loop_phases.cpp src/proxy/lean_stub.cpp src/proxy/media_cue.cpp
  src/proxy/loading_trace.cpp src/proxy/loading_trace_light.cpp src/proxy/gz_buffer.cpp src/proxy/crypt_cache.cpp src/proxy/sampling_profiler.cpp src/ownership/d3d9_ownership.cpp
  src/proxy/engine_patch.cpp src/proxy/loading_probes.cpp src/proxy/resource_reader.cpp src/proxy/resource_reader_core.cpp
  src/ownership/finite_buffer_evidence.cpp src/ownership/portable_managed_upload.cpp
  src/proxy/draw_input.cpp src/proxy/object_lifetime.cpp src/proxy/mesh_adjacency_cache.cpp src/proxy/mesh_adjacency_fast.cpp
  src/renderer/temporal_pass.cpp src/renderer/hdr_pass.cpp src/renderer/exposure.cpp src/renderer/material_radiance.cpp
  src/renderer/ambient_occlusion_pass.cpp src/renderer/shadow_replay_pass.cpp
  src/renderer/motion_history.cpp src/renderer/rigid_position.cpp src/renderer/shader_population.cpp
  src/renderer/material_motion.cpp src/renderer/linear_material.cpp src/renderer/linear_emission.cpp src/renderer/linear_emission_sm1.cpp src/renderer/linear_emission_pass.cpp src/renderer/motion_row_history.cpp src/proxy/motion_output.cpp
  src/renderer/rigid_motion.cpp src/renderer/rigid_replay_program.cpp src/proxy/d3d9.def)
target_compile_features(d3d9 PRIVATE cxx_std_17)
target_include_directories(d3d9 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_dependencies(d3d9 x3m_source_commit)
# Keep compiler EH bookends out of injected CPU-state boundaries: MinGW SJLJ
# registration otherwise runs before capture and unregistration after restore.
# These adapters do not throw; the remaining sources retain their EH policy.
set_source_files_properties(src/ownership/application_admission_abi.cpp
  src/proxy/chase_camera.cpp src/proxy/chase_aim_trace.cpp src/proxy/chase_fire.cpp src/proxy/scene_hook.cpp
  src/proxy/chase_transition.cpp src/proxy/chase_lead.cpp src/proxy/game_phases.cpp src/proxy/frame_phases.cpp src/proxy/pass_phases.cpp src/proxy/loop_phases.cpp src/proxy/lean_stub.cpp src/proxy/media_cue.cpp src/proxy/voice_dmo_fallback.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The engine-memory reader runs inside the lifetime observer's mutation scopes
# and the light hooks, which do not preserve XMM state: build it without SSE/MMX
# (it has no floating-point arithmetic; check_no_x87.py proves no x87 either).
set_source_files_properties(src/proxy/engine_memory.cpp
  PROPERTIES COMPILE_OPTIONS "-mno-sse;-mno-mmx;-mfpmath=387")
# The point-light root-admission handler is called from the engine's submission
# routine between two of its own helper calls with no CPU-state boundary: no
# SSE/MMX, no floating point, no exception bookends (check_no_x87.py walks it).
set_source_files_properties(src/proxy/point_light_admission.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387")
# The light loading-trace rows, the engine probe handlers, the resource
# reader core and the CryptoAPI context/key cache run inside game threads with
# no CPU-state boundary: same rule (docs/verification/loading-probes.md has the
# objdump proof; check_no_x87.py walks the crypt cache entry points too).
set_source_files_properties(src/proxy/loading_trace_light.cpp src/proxy/resource_reader_core.cpp src/proxy/crypt_cache.cpp
  PROPERTIES COMPILE_OPTIONS "-mno-sse;-mno-mmx;-mfpmath=387")
# Light spans have no cleanup/catch obligations. SJLJ registration otherwise
# reaches lazy pthread allocation before the saved caller LastError boundary.
set_property(SOURCE src/proxy/loading_trace_light.cpp APPEND PROPERTY COMPILE_OPTIONS -fno-exceptions)
target_compile_definitions(d3d9 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
# Our arithmetic uses SSE2 while preserving the game's i686 Windows ABI.
# Legacy callers provide four-byte stack alignment; align SIMD locals internally.
# Do not enable fast-math or globally require unverified AVX/FMA instructions.
target_compile_options(d3d9 PRIVATE -Wall -Wextra -Wno-cast-function-type -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2)
target_link_options(d3d9 PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--kill-at -Wl,--enable-stdcall-fixup)
# Only _except_handler3 comes from msvcrt.dll; never substitute broad msvcrt-os,
# which can silently redirect the existing UCRT imports.
target_link_libraries(d3d9 PRIVATE "${X3M_COMPOSITOR_RUNTIME}" dxguid advapi32)
set_target_properties(d3d9 PROPERTIES PREFIX "" OUTPUT_NAME "d3d9")
