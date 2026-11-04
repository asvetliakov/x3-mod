cmake_minimum_required(VERSION 3.20)
project(x3_modern_renderer VERSION 1.9.1 LANGUAGES CXX)
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
include(cmake/FogField.cmake)
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
# Game edition (docs/architecture/farnhams-legacy-build.md): one source tree, two d3d9.dll builds.
# ap: X3: Albion Prelude (X3AP.exe, the existing DLL). fl: X3: Farnham's Legacy (X3FL.exe): the shared
# D3D/renderer sources, the AP engine modules they link against (inert) and the FL identity.
set(X3M_EDITION "ap" CACHE STRING "Game edition: ap (X3: Albion Prelude) or fl (X3: Farnham's Legacy)")
set_property(CACHE X3M_EDITION PROPERTY STRINGS ap fl)
if(NOT X3M_EDITION STREQUAL "ap" AND NOT X3M_EDITION STREQUAL "fl")
  message(FATAL_ERROR "X3M_EDITION must be ap or fl, not '${X3M_EDITION}'")
endif()
# The ap target links this list in its original order (the AP DLL stays byte-identical apart from the
# source-commit string); the fl target links the two groups below, both subsets of it.
set(X3M_AP_SOURCES
  "${X3M_COMPOSITOR_ASM}" "${X3M_COMPOSITOR_SEH}"
  src/proxy/compositor_owner.cpp src/renderer/bloom_pass.cpp
  src/proxy/loader.cpp src/proxy/config.cpp src/proxy/capture.cpp src/proxy/session_log.cpp src/proxy/proxy_identity.cpp src/proxy/capture_state.cpp src/proxy/comparison_notice.cpp
  src/proxy/motion_capture.cpp src/ownership/execution_state.cpp
  src/ownership/application_admission.cpp src/ownership/application_admission_abi.cpp
  src/proxy/scene_capture.cpp src/proxy/object_trace.cpp src/proxy/scene_hook.cpp src/proxy/camera_state.cpp src/proxy/sun_light_poll.cpp src/proxy/chase_camera.cpp src/proxy/chase_aim_trace.cpp src/proxy/chase_fire.cpp src/proxy/telemetry.cpp src/proxy/engine_memory.cpp src/proxy/fade_region.cpp
  src/proxy/chase_transition.cpp src/proxy/chase_lead.cpp src/proxy/game_phases.cpp src/proxy/voice_dmo_fallback.cpp src/proxy/terran_station_lod.cpp src/proxy/lod_occlusion.cpp src/proxy/engine_effects_patch.cpp src/proxy/fov.cpp src/proxy/ui_scale.cpp src/proxy/text_density.cpp src/proxy/sun_flare_fix.cpp src/proxy/dust_leak_fix.cpp src/proxy/circle_fill.cpp src/proxy/ship_steering_install.cpp src/proxy/ship_steering_decide.cpp src/proxy/ship_steering_clamp.cpp src/proxy/ship_steering_warn.cpp src/proxy/ship_steering_rows.cpp src/proxy/ship_gate_ctl_glue.cpp src/proxy/ship_dock_glue.cpp src/proxy/point_light_admission.cpp src/proxy/cull_census.cpp src/proxy/scene_graph_census.cpp src/proxy/address_space.cpp src/proxy/cull_small_parts.cpp src/proxy/engine_far_jets.cpp src/proxy/occlusion_engine_cull.cpp src/proxy/engine_effects.cpp src/proxy/frame_timing.cpp src/proxy/frame_phases.cpp src/proxy/pass_phases.cpp src/proxy/residual_phases.cpp src/proxy/submit_phases.cpp src/proxy/light_phases.cpp src/proxy/loop_phases.cpp src/proxy/lean_stub.cpp src/proxy/media_cue.cpp src/proxy/collide_box_cull.cpp src/proxy/pause_key_only.cpp src/proxy/collide_narrow_census.cpp src/proxy/collide_sat_sse2.cpp src/proxy/collide_memo.cpp src/proxy/music_keep.cpp src/proxy/seta_restore.cpp src/proxy/seta_manual_flight.cpp src/proxy/music_alert.cpp src/proxy/run_in_background.cpp src/proxy/collide_query_phases.cpp src/proxy/own_sun_hook.cpp
  src/proxy/window_mode.cpp src/proxy/window_trace.cpp src/proxy/cursor_reassert.cpp
  src/proxy/loading_trace.cpp src/proxy/loading_trace_light.cpp src/proxy/gz_buffer.cpp src/proxy/crypt_cache.cpp src/proxy/sampling_profiler.cpp src/ownership/d3d9_ownership.cpp
  src/proxy/engine_patch.cpp src/proxy/loading_probes.cpp src/proxy/resource_reader.cpp src/proxy/resource_reader_core.cpp
  src/ownership/finite_buffer_evidence.cpp src/ownership/portable_managed_upload.cpp
  src/proxy/draw_input.cpp src/proxy/object_lifetime.cpp src/proxy/mesh_adjacency_fast.cpp
  src/renderer/temporal_pass.cpp src/renderer/hdr_pass.cpp src/renderer/exposure.cpp src/renderer/material_radiance.cpp src/renderer/engine_plumes_pass.cpp src/renderer/engine_ribbons_pass.cpp src/renderer/engine_shimmer_pass.cpp
  src/renderer/own_sun_pass.cpp src/renderer/sky_stars_pass.cpp src/proxy/sector_key.cpp
  src/renderer/gpu_sync_timing.cpp src/renderer/fog_pass.cpp src/fog/fog_density_cache.cpp src/fog/fog_density_generator.cpp src/renderer/shadow_replay_pass.cpp src/renderer/sun_shadow_apply_pass.cpp src/renderer/occlusion_cull_pass.cpp
  src/renderer/motion_history.cpp src/renderer/rigid_position.cpp src/renderer/shader_population.cpp
  src/renderer/material_motion.cpp src/renderer/linear_material.cpp src/renderer/linear_emission.cpp src/renderer/linear_emission_sm1.cpp src/renderer/linear_emission_pass.cpp src/renderer/motion_row_history.cpp src/proxy/motion_output.cpp
  src/renderer/rigid_motion.cpp src/renderer/rigid_replay_program.cpp src/proxy/d3d9.def)
# fl edition: the shared D3D/renderer sources and the mixed files.
set(X3M_SHARED_SOURCES
  "${X3M_COMPOSITOR_ASM}" "${X3M_COMPOSITOR_SEH}"
  src/renderer/bloom_pass.cpp src/proxy/loader.cpp src/proxy/config.cpp
  src/proxy/capture.cpp src/proxy/session_log.cpp src/proxy/proxy_identity.cpp src/proxy/capture_state.cpp
  src/proxy/comparison_notice.cpp src/proxy/motion_capture.cpp src/ownership/execution_state.cpp
  src/ownership/application_admission.cpp src/ownership/application_admission_abi.cpp src/proxy/scene_capture.cpp
  src/proxy/telemetry.cpp src/proxy/address_space.cpp src/proxy/frame_timing.cpp src/proxy/window_mode.cpp
  src/proxy/window_trace.cpp src/proxy/cursor_reassert.cpp src/proxy/sampling_profiler.cpp
  src/ownership/d3d9_ownership.cpp src/ownership/finite_buffer_evidence.cpp src/ownership/portable_managed_upload.cpp
  src/proxy/draw_input.cpp src/proxy/mesh_adjacency_fast.cpp src/renderer/temporal_pass.cpp src/renderer/hdr_pass.cpp
  src/renderer/exposure.cpp src/renderer/material_radiance.cpp src/renderer/engine_plumes_pass.cpp
  src/renderer/engine_ribbons_pass.cpp src/renderer/engine_shimmer_pass.cpp src/renderer/gpu_sync_timing.cpp
  src/renderer/own_sun_pass.cpp src/renderer/sky_stars_pass.cpp src/proxy/sector_key.cpp
  src/renderer/fog_pass.cpp src/fog/fog_density_cache.cpp src/fog/fog_density_generator.cpp
  src/renderer/shadow_replay_pass.cpp src/renderer/sun_shadow_apply_pass.cpp
  src/renderer/occlusion_cull_pass.cpp src/renderer/motion_history.cpp
  src/renderer/rigid_position.cpp src/renderer/shader_population.cpp src/renderer/material_motion.cpp
  src/renderer/linear_material.cpp src/renderer/linear_emission.cpp src/renderer/linear_emission_sm1.cpp
  src/renderer/linear_emission_pass.cpp src/renderer/motion_row_history.cpp src/proxy/motion_output.cpp
  src/renderer/rigid_motion.cpp src/renderer/rigid_replay_program.cpp src/proxy/d3d9.def)
# AP engine modules both editions link: motion_output, its *_inc.h fragments and the capture.cpp draw/compositor
# paths call into them. In the fl edition their initializers are fenced out (capture.cpp, loader.cpp) and
# object_trace::executable_verified() is a constant false, so their hooks are never installed.
# camera_state.cpp, object_trace.cpp and object_lifetime.cpp are ap only: the fl target links its own providers
# of camera_state.h, object_trace.h and object_lifetime.h below (src/engine/fl/camera_state_fl.cpp,
# docs/architecture/farnhams-legacy-camera.md; src/engine/fl/object_trace_fl.cpp,
# docs/architecture/farnhams-legacy-object-identity.md; src/engine/fl/object_lifetime_fl.cpp,
# docs/architecture/farnhams-legacy-lifetime.md). Likewise scene_hook.cpp and compositor_owner.cpp: the fl target
# links src/engine/fl/scene_hook_fl.cpp and src/engine/fl/compositor_owner_fl.cpp
# (docs/architecture/farnhams-legacy-scene.md). sun_light_poll.cpp too: the fl target links
# src/engine/fl/sun_light_poll_fl.cpp (docs/architecture/farnhams-legacy-sun-light.md). The flare-probe hook module too: the fl
# target links src/engine/fl/sun_occlusion_fl.cpp (docs/architecture/farnhams-legacy-sun-occlusion.md). The engine-effects
# call redirects too: the fl target links src/engine/fl/engine_effects_fl.cpp (docs/architecture/engine-effects-modern.md,
# "FL port, step 1"); engine_effects.cpp stays shared. The small-parts cull, the Terran station LOD and the LOD occlusion
# patches too: the fl target links src/engine/fl/cull_small_parts_fl.cpp, terran_station_lod_fl.cpp and
# lod_occlusion_fl.cpp (docs/verification/cull-small-parts.md, "FL port").
# The field of view too: the fl target links src/engine/fl/fov_fl.cpp (docs/reverse-engineering/fl/field-of-view.md,
# "Verified 2026-11-02"); fov.cpp is AP only. The chase lead marker and central HUD too: the fl target links
# src/engine/fl/chase_lead_fl.cpp (docs/reverse-engineering/fl/chase-lead.md, "Verified 2026-11-02"); chase_lead.cpp is
# AP only. The chase cursor fire too: the fl target links src/engine/fl/chase_fire_fl.cpp
# (docs/reverse-engineering/fl/chase-fire-aim.md, "Verified 2026-11-03"); chase_fire.cpp is AP only. The UI scale too:
# the fl target links src/engine/fl/ui_scale_fl.cpp (docs/reverse-engineering/fl/ui-scale.md, "Verified 2026-11-03");
# ui_scale.cpp is AP only. The text density too: the fl target links src/engine/fl/text_density_fl.cpp
# (docs/reverse-engineering/fl/text-density.md, "Verified 2026-11-03"); text_density.cpp is AP only.
# The dust leak fix too: the fl target links src/engine/fl/dust_leak_fix_fl.cpp
# (docs/reverse-engineering/fl/dust-light.md, "Port (2026-11-03)"); dust_leak_fix.cpp is AP only.
# The SETA restore and manual flight too: the fl target links src/engine/fl/seta_restore_fl.cpp and
# seta_manual_flight_fl.cpp (docs/reverse-engineering/fl/seta.md, "Port (2026-11-03)"); seta_restore.cpp and
# seta_manual_flight.cpp are AP only.
# The collision trio too: the fl target links src/engine/fl/collide_box_cull_fl.cpp, collide_sat_sse2_fl.cpp and
# collide_memo_fl.cpp (docs/reverse-engineering/fl/collision-box.md, collision-sat.md, collision-memo.md, "Port
# (2026-11-03)"); collide_box_cull.cpp, collide_sat_sse2.cpp and collide_memo.cpp are AP only.
set(X3M_AP_ENGINE_LINKED_SOURCES
  src/proxy/chase_camera.cpp src/proxy/chase_aim_trace.cpp src/proxy/engine_memory.cpp
  src/proxy/fade_region.cpp src/proxy/chase_transition.cpp
  src/proxy/cull_census.cpp
  src/proxy/engine_far_jets.cpp src/proxy/occlusion_engine_cull.cpp src/proxy/engine_effects.cpp
  src/proxy/engine_patch.cpp)
# AP-only engine modules (derived: the ap list minus both groups): hooks, patches and reads at X3AP.exe
# addresses (0x400000 based), gated by object_trace::executable_verified(). Their call sites in capture.cpp,
# loader.cpp and telemetry.cpp are fenced with #if !X3M_EDITION_FL (no edition define, as in the host and
# Wine fixtures that compile production sources directly, means AP).
set(X3M_AP_ENGINE_SOURCES ${X3M_AP_SOURCES})
foreach(source IN LISTS X3M_SHARED_SOURCES X3M_AP_ENGINE_LINKED_SOURCES)
  if(NOT source IN_LIST X3M_AP_SOURCES)
    message(FATAL_ERROR "${source} is in an fl group but not in X3M_AP_SOURCES")
  endif()
endforeach()
list(REMOVE_ITEM X3M_AP_ENGINE_SOURCES ${X3M_SHARED_SOURCES} ${X3M_AP_ENGINE_LINKED_SOURCES})
if(X3M_EDITION STREQUAL "ap")
  add_library(d3d9 SHARED ${X3M_AP_SOURCES})
  target_compile_definitions(d3d9 PRIVATE X3M_EDITION_AP=1)
else()
  add_library(d3d9 SHARED ${X3M_SHARED_SOURCES} ${X3M_AP_ENGINE_LINKED_SOURCES}
    src/engine/fl/executable_identity_fl.cpp src/engine/fl/camera_state_fl.cpp src/engine/fl/object_trace_fl.cpp
    src/engine/fl/object_lifetime_fl.cpp src/engine/fl/scene_hook_fl.cpp src/engine/fl/compositor_owner_fl.cpp)
  # The saturating replacement of X3FL.exe's fixed-point scaler (docs/architecture/farnhams-legacy-scale-overflow.md).
  target_sources(d3d9 PRIVATE src/engine/fl/scale_overflow_fix_fl.cpp)
  # The hook-free poll of the engine's brightest directional light (docs/architecture/farnhams-legacy-sun-light.md).
  target_sources(d3d9 PRIVATE src/engine/fl/sun_light_poll_fl.cpp)
  # The active sector's TBackgrounds record for the fog (docs/architecture/farnhams-legacy-sector.md).
  target_sources(d3d9 PRIVATE src/engine/fl/sector_background_fl.cpp)
  # The fog transit prefill's walk over X3FL.exe's object list (docs/architecture/farnhams-legacy-prefill.md).
  target_sources(d3d9 PRIVATE src/engine/fl/fog_prefill_fl.cpp)
  # The flare probe override for the own sun (docs/architecture/farnhams-legacy-sun-occlusion.md).
  target_sources(d3d9 PRIVATE src/engine/fl/sun_occlusion_fl.cpp)
  # The engine-effects call redirects (docs/architecture/engine-effects-modern.md, "FL port, step 1").
  target_sources(d3d9 PRIVATE src/engine/fl/engine_effects_fl.cpp)
  # The small-parts cull stub and the two one-byte LOD patches (docs/verification/cull-small-parts.md, "FL port").
  target_sources(d3d9 PRIVATE src/engine/fl/cull_small_parts_fl.cpp src/engine/fl/terran_station_lod_fl.cpp
    src/engine/fl/lod_occlusion_fl.cpp)
  # The field of view: the constructor literal, the INS_SetFocus remap and the load store
  # (docs/reverse-engineering/fl/field-of-view.md, "Verified 2026-11-02").
  target_sources(d3d9 PRIVATE src/engine/fl/fov_fl.cpp)
  # The chase lead marker and central HUD: four seams in the cockpit and overlay updaters, the AP policy on FL bindings
  # (docs/reverse-engineering/fl/chase-lead.md, "Verified 2026-11-02"); it provides chase_lead.h in place of the AP
  # src/proxy/chase_lead.cpp.
  target_sources(d3d9 PRIVATE src/engine/fl/chase_lead_fl.cpp)
  # The chase cursor fire: one relocated JE in the native fire function, the AP policy on FL bindings
  # (docs/reverse-engineering/fl/chase-fire-aim.md, "Verified 2026-11-03"); it provides chase_fire.h in place of the AP
  # src/proxy/chase_fire.cpp.
  target_sources(d3d9 PRIVATE src/engine/fl/chase_fire_fl.cpp)
  # The UI scale: eleven claims (script screen size, the two mouse-delta callbacks, the cursor stores, the overlay/aim
  # click points, the galaxy-map label and pick, the 2D projection), the AP policy on FL bindings
  # (docs/reverse-engineering/fl/ui-scale.md, "Verified 2026-11-03"); it provides ui_scale.h in place of the AP
  # src/proxy/ui_scale.cpp.
  target_sources(d3d9 PRIVATE src/engine/fl/ui_scale_fl.cpp)
  # The text density: the font entry, the two block blits, the Materials seam and KC's image block claimed, the config
  # field, the cockpit opener's four bytes and the style factor edited at CreateDevice, the AP policy on FL bindings
  # (docs/reverse-engineering/fl/text-density.md, "Verified 2026-11-03"); it provides text_density.h in place of the AP
  # src/proxy/text_density.cpp.
  target_sources(d3d9 PRIVATE src/engine/fl/text_density_fl.cpp)
  # The dust leak fix: the fill loop's common tail claimed with the release-and-leave stub, the AP policy on FL
  # bindings (docs/reverse-engineering/fl/dust-light.md, "Port (2026-11-03)"); it provides dust_leak_fix.h in place of
  # the AP src/proxy/dust_leak_fix.cpp.
  target_sources(d3d9 PRIVATE src/engine/fl/dust_leak_fix_fl.cpp)
  # The oversized scanner disc: the filled-ellipse entry claimed, large or non-unit discs through the 64-bit port
  # (docs/reverse-engineering/fl/circle-fill.md, "Port (2026-11-03)"); it provides circle_fill.h in place of the AP
  # src/proxy/circle_fill.cpp.
  target_sources(d3d9 PRIVATE src/engine/fl/circle_fill_fl.cpp)
  # The pause key and run in background: two in-place instruction patches, the AP policy on FL bindings
  # (docs/reverse-engineering/fl/pause-background.md, "Port (2026-11-03)"); they provide pause_key_only.h and
  # run_in_background.h in place of the AP src/proxy/pause_key_only.cpp and run_in_background.cpp.
  target_sources(d3d9 PRIVATE src/engine/fl/pause_key_only_fl.cpp src/engine/fl/run_in_background_fl.cpp)
  # The music family: the ID2 skip / media trace gate, the music keep with its queue, dock keep and save pump, and the
  # combat trigger, the AP policy cores on FL bindings (docs/reverse-engineering/fl/media-cue.md, music-keep.md,
  # music-alert.md, "Port (2026-11-03)"); they provide media_cue.h, music_keep.h and music_alert.h in place of the AP
  # providers of the same names under src/proxy.
  target_sources(d3d9 PRIVATE src/engine/fl/media_cue_fl.cpp src/engine/fl/music_keep_fl.cpp
    src/engine/fl/music_alert_fl.cpp)
  # The SETA restore (record-only factor hook, the shared injection consumer) and manual flight (the eight stop rows, the
  # five SSE spans), the AP policy on FL bindings (docs/reverse-engineering/fl/seta.md, "Port (2026-11-03)"); they
  # provide seta_restore.h and seta_manual_flight.h in place of the two AP modules of the same names.
  target_sources(d3d9 PRIVATE src/engine/fl/seta_restore_fl.cpp src/engine/fl/seta_manual_flight_fl.cpp)
  # The base ship steering and avoidance: the three steering-leaf entries, the object-loop continue and the warn test E, the AP
  # decision, clamp, snapshot and E-verdict cores on FL bindings (docs/reverse-engineering/fl/ship-steering.md,
  # ship-avoidance.md, "Port (2026-11-03)"); they provide ship_steering.h in place of the AP src/proxy/ship_steering_*.cpp. The
  # gate, capital, traffic, lane and dock groups stay AP only (one ap_only row each).
  target_sources(d3d9 PRIVATE src/engine/fl/ship_steering_fl.cpp src/engine/fl/ship_avoidance_fl.cpp)
  # The material-strength texture hook: X3FL.exe's D3DXCreateTextureFromFileInMemoryEx import slot (AP patches the
  # same row through loading_trace.cpp; docs/architecture/overlay-material-strength.md, "Farnham's Legacy").
  target_sources(d3d9 PRIVATE src/proxy/d3dx_texture_hook.cpp)
  # The collision trio: the box early-out (two integer stubs, FL margin 256, local MXCSR admission), the native SSE SAT
  # report (nothing patched) and the no-contact memo (the FL call boundary on AP's table), the AP policy cores on FL
  # bindings; they provide collide_box_cull.h, collide_sat_sse2.h and collide_memo.h in place of the AP modules of the
  # same names.
  target_sources(d3d9 PRIVATE src/engine/fl/collide_box_cull_fl.cpp src/engine/fl/collide_sat_sse2_fl.cpp
    src/engine/fl/collide_memo_fl.cpp)
  # The loading quintet on FL: gz_buffer's six zlib1.dll slots and crypt_cache's four ADVAPI32.dll slots (their own FL
  # transactions over the shared cores), loading_trace's two mesh rows on d3dx9_43.dll, the resource reader's ECX entry
  # stub and the DAT pool's eighteen call sites over the shared reader core (docs/architecture/platform-portability.md,
  # "2026-11-03: loading quintet on FL"; docs/reverse-engineering/fl/resource-reader.md, "Port (2026-11-03)").
  target_sources(d3d9 PRIVATE src/proxy/gz_buffer.cpp src/proxy/crypt_cache.cpp src/proxy/loading_trace.cpp
    src/proxy/loading_trace_light.cpp src/engine/fl/gz_buffer_fl.cpp src/engine/fl/crypt_cache_fl.cpp
    src/proxy/resource_reader_core.cpp src/engine/fl/resource_reader_fl.cpp)
  target_compile_definitions(d3d9 PRIVATE X3M_EDITION_FL=1)
endif()
x3m_add_fog_field_assets(d3d9)
target_compile_features(d3d9 PRIVATE cxx_std_17)
target_include_directories(d3d9 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_dependencies(d3d9 x3m_source_commit)
# Keep compiler EH bookends out of injected CPU-state boundaries: MinGW SJLJ
# registration otherwise runs before capture and unregistration after restore.
# These adapters do not throw; the remaining sources retain their EH policy.
set_source_files_properties(src/ownership/application_admission_abi.cpp
  src/proxy/chase_camera.cpp src/proxy/chase_aim_trace.cpp src/proxy/chase_fire.cpp src/proxy/scene_hook.cpp
  src/proxy/chase_transition.cpp src/proxy/chase_lead.cpp src/proxy/game_phases.cpp src/proxy/frame_phases.cpp src/proxy/pass_phases.cpp src/proxy/residual_phases.cpp src/proxy/submit_phases.cpp src/proxy/light_phases.cpp src/proxy/loop_phases.cpp src/proxy/lean_stub.cpp src/proxy/media_cue.cpp src/proxy/voice_dmo_fallback.cpp src/proxy/own_sun_hook.cpp
  src/proxy/scene_graph_census.cpp src/proxy/address_space.cpp src/proxy/window_mode.cpp src/proxy/window_trace.cpp src/proxy/cursor_reassert.cpp
  src/proxy/ship_steering_install.cpp src/proxy/ship_steering_decide.cpp src/proxy/ship_steering_clamp.cpp src/proxy/ship_steering_warn.cpp
  src/proxy/ship_steering_rows.cpp src/proxy/ship_gate_ctl_glue.cpp src/proxy/ship_dock_glue.cpp src/proxy/seta_restore.cpp src/proxy/seta_manual_flight.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The engine-memory reader runs inside the lifetime observer's mutation scopes
# and the light hooks, which do not preserve XMM state: build it without SSE/MMX
# (it has no floating-point arithmetic; check_no_x87.py proves no x87 either).
set_source_files_properties(src/proxy/engine_memory.cpp
  PROPERTIES COMPILE_OPTIONS "-mno-sse;-mno-mmx;-mfpmath=387")
# The FL material-submission dispatcher and its helpers (src/engine/fl/object_trace_fl.cpp) run inside the
# engine's call with no CPU-state boundary: no SSE/MMX, no floating point, no exception bookends; the periodic
# log row saves XMM0-7 and runs under call_preserved.
set_source_files_properties(src/engine/fl/object_trace_fl.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387")
# The FL registry observer (src/engine/fl/object_lifetime_fl.cpp) runs inside the engine's map helpers behind a
# PUSHFD/PUSHAD/FXSAVE envelope: same rule, no SSE/MMX, no floating point, no exception bookends.
set_source_files_properties(src/engine/fl/object_lifetime_fl.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387")
# The FL scene-end trampoline (src/engine/fl/scene_hook_fl.cpp) runs inside the engine's complete pass: same rule;
# its signal saves XMM0-7 and the x87/MXCSR/LastError image (cpu_state.h) around the SSE listener.
set_source_files_properties(src/engine/fl/scene_hook_fl.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387")
# The FL scaler replacement (src/engine/fl/scale_overflow_fix_fl.cpp) replaces an engine function called with live
# XMM/x87 state and no CPU-state boundary: same flags; its rare QPC/row path saves XMM0-7 and runs under call_preserved.
set_source_files_properties(src/engine/fl/scale_overflow_fix_fl.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387")
# The FL sun light poll (src/engine/fl/sun_light_poll_fl.cpp) runs in the proxy's draw path, not inside engine code:
# SSE2 stays on (its score is the engine's double-precision sum); no exception bookends.
set_source_files_properties(src/engine/fl/sun_light_poll_fl.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The FL sector-background reader (src/engine/fl/sector_background_fl.cpp) runs inside capture.cpp's device-call
# boundary like the AP reader: default SSE2 flags, no exception bookends.
set_source_files_properties(src/engine/fl/sector_background_fl.cpp PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The FL prefill walk (src/engine/fl/fog_prefill_fl.cpp) runs inside capture.cpp's resource-creation wrappers like the
# AP walk: default SSE2 flags, no exception bookends.
set_source_files_properties(src/engine/fl/fog_prefill_fl.cpp PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The FL flare-probe thunk (src/engine/fl/sun_occlusion_fl.cpp) runs inside the engine's flare walk: no SSE/MMX, no
# floating point, no exception bookends; the probe handler saves XMM0-7 and LastError.
set_source_files_properties(src/engine/fl/sun_occlusion_fl.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387")
# The FL engine-effects stubs (src/engine/fl/engine_effects_fl.cpp) are file-scope asm, integer only; the install code
# runs on the backend-load path: default SSE2 flags, no exception bookends.
set_source_files_properties(src/engine/fl/engine_effects_fl.cpp PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The FL small-parts cull (its stub is emitted integer code; the far-jet callee is engine_far_jets.cpp) and the two FL
# one-byte LOD patches run on the backend-load path and in the Present/Reset hooks: default SSE2 flags, no exception
# bookends.
set_source_files_properties(src/engine/fl/cull_small_parts_fl.cpp src/engine/fl/terran_station_lod_fl.cpp
  src/engine/fl/lod_occlusion_fl.cpp PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The FL field of view (its two stubs are emitted integer code in the arena; the install code runs on the backend-load
# path, the confirm rows in the Present hook): default SSE2 flags, no exception bookends.
set_source_files_properties(src/engine/fl/fov_fl.cpp PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The FL chase lead (src/engine/fl/chase_lead_fl.cpp): its handler runs inside the emitted PUSHFD/PUSHAD/XMM stub under
# PreserveCpuState like the AP module: default SSE2 flags, no exception bookends inside the CPU-state boundary.
set_source_files_properties(src/engine/fl/chase_lead_fl.cpp PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The FL chase cursor fire (src/engine/fl/chase_fire_fl.cpp): its handler runs inside the emitted PUSHFD/PUSHAD/XMM stub
# under PreserveCpuState like the AP module: default SSE2 flags, no exception bookends inside the CPU-state boundary.
set_source_files_properties(src/engine/fl/chase_fire_fl.cpp PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The FL UI scale (src/engine/fl/ui_scale_fl.cpp): emitted integer/SSE stubs, no callout; the install and Present code
# throws nothing.
set_source_files_properties(src/engine/fl/ui_scale_fl.cpp PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The FL text density (src/engine/fl/text_density_fl.cpp): its C sides run inside the asm thunks' envelope (EFLAGS, GPRs,
# XMM0-7, FNSAVE image, MXCSR; LastError in the C side): default SSE2 flags, no exception bookends.
set_source_files_properties(src/engine/fl/text_density_fl.cpp PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The FL dust leak fix (src/engine/fl/dust_leak_fix_fl.cpp): its stub is emitted code in the arena (no C side runs inside
# the engine); the install code runs on the backend-load path: default SSE2 flags, no exception bookends.
set_source_files_properties(src/engine/fl/dust_leak_fix_fl.cpp PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The FL oversized-disc port (src/engine/fl/circle_fill_fl.cpp) runs inside the engine's filled-ellipse entry behind a
# pushad/pushfd stub that saves no XMM or x87 state: the same rule as the AP module, integer arithmetic only (the
# engine's B conversion is file-scope asm).
set_source_files_properties(src/engine/fl/circle_fill_fl.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387")
# The FL pause key and run in background (two in-place instruction patches, no stub, no runtime code of ours; install on
# the backend-load path): default SSE2 flags, no exception bookends.
set_source_files_properties(src/engine/fl/pause_key_only_fl.cpp src/engine/fl/run_in_background_fl.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The FL music family (src/engine/fl/media_cue_fl.cpp, music_keep_fl.cpp, music_alert_fl.cpp): handlers inside the
# integer stubs' envelopes and the gate's PUSHFD/XMM envelope, same rule as their AP twins.
set_source_files_properties(src/engine/fl/media_cue_fl.cpp src/engine/fl/music_keep_fl.cpp
  src/engine/fl/music_alert_fl.cpp PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The FL SETA restore and manual flight (src/engine/fl/seta_restore_fl.cpp, seta_manual_flight_fl.cpp): their handlers run
# inside file-scope asm stubs (PUSHFD, GPRs, XMM0-7, FNSAVE, MXCSR; LastError in the C side): default SSE2 flags, no
# exception bookends inside the CPU-state boundary.
set_source_files_properties(src/engine/fl/seta_restore_fl.cpp src/engine/fl/seta_manual_flight_fl.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The FL base ship steering and avoidance (src/engine/fl/ship_steering_fl.cpp, ship_avoidance_fl.cpp): their handlers run inside
# file-scope asm stubs (PUSHFD, EAX/ECX/EDX, XMM0-7, FNSAVE, MXCSR, CLD; LastError in the C side): default SSE2 flags, no exception
# bookends inside the CPU-state boundary.
set_source_files_properties(src/engine/fl/ship_steering_fl.cpp src/engine/fl/ship_avoidance_fl.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The FL collision trio (src/engine/fl/collide_box_cull_fl.cpp, collide_sat_sse2_fl.cpp, collide_memo_fl.cpp): the box
# stubs are emitted integer code (no C side inside the engine); the memo's C lookup/store run inside its asm thunk's
# envelope (ECX/EDX, XMM0-7; callee-saved GPRs by the C ABI; no FP arithmetic, MXCSR read only); the SAT report is
# cold: default SSE2 flags, no exception bookends inside the CPU-state boundary.
set_source_files_properties(src/engine/fl/collide_box_cull_fl.cpp src/engine/fl/collide_sat_sse2_fl.cpp
  src/engine/fl/collide_memo_fl.cpp PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The FL crypt cache entries (src/engine/fl/crypt_cache_fl.cpp) run on the game's script thread in its CryptoAPI import
# slots with no CPU-state boundary, like the shared cache they call: no SSE/MMX, no floating point. The FL gz_buffer
# rows (src/engine/fl/gz_buffer_fl.cpp) install only; the slots point at the shared buffer.
set_source_files_properties(src/engine/fl/crypt_cache_fl.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387")
set_source_files_properties(src/engine/fl/gz_buffer_fl.cpp PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The FL resource reader module (src/engine/fl/resource_reader_fl.cpp) installs on the backend-load path; inside the
# loader thread only its verify sink runs, under PreserveCpuState: default SSE2 flags, no exception bookends. The entry
# handler and the pool it binds live in the shared no-SSE core (src/proxy/resource_reader_core.cpp).
set_source_files_properties(src/engine/fl/resource_reader_fl.cpp PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The FL texture import-slot stub (src/proxy/d3dx_texture_hook.cpp) forwards inside a CpuCallBoundary like AP's
# loading-trace D3DX rows: default SSE2 flags, no exception bookends around the boundary.
set_source_files_properties(src/proxy/d3dx_texture_hook.cpp PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
# The point-light root-admission handler is called from the engine's submission
# routine between two of its own helper calls with no CPU-state boundary: no
# SSE/MMX, no floating point, no exception bookends (check_no_x87.py walks it).
set_source_files_properties(src/proxy/point_light_admission.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387")
# The cull-census handlers run inside the engine's cull/LOD pass on the render
# thread with no CPU-state boundary: same rule (check_no_x87.py walks them).
set_source_files_properties(src/proxy/cull_census.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387")
# The far-jet handler (src/proxy/engine_far_jets.cpp) is called by the small-parts
# cull stub inside the same pass, no CPU-state boundary: same rule (check_no_x87.py
# walks _x3m_engine_far_jet).
set_source_files_properties(src/proxy/engine_far_jets.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387")
# The oversized-disc port (src/proxy/circle_fill.cpp) runs inside the engine's filled-ellipse entry behind a
# pushad/pushfd thunk that saves no XMM or x87 state: same rule, integer arithmetic only (check_no_x87.py walks
# _x3m_circle_fill_wide).
set_source_files_properties(src/proxy/circle_fill.cpp
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
# Stored-density fog generator: the FP16 word-identity gate (test_fog_density_generator.py)
# holds only without FP contraction. The cache worker reaches it through FogPass alone and
# stays idle until FogDensityConfig::enabled (checkpoint 4 wires the option).
set_property(SOURCE src/fog/fog_density_generator.cpp src/fog/fog_density_cache.cpp APPEND PROPERTY COMPILE_OPTIONS -ffp-contract=off)
target_compile_definitions(d3d9 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
# The one version number is project(VERSION) above; tools/config/generate.py and tools/release/package.py read it
# from here, and the DLL's x3-modern-renderer log row prints its major.minor through X3M_VERSION.
target_compile_definitions(d3d9 PRIVATE X3M_VERSION="${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
# Our arithmetic uses SSE2 while preserving the game's i686 Windows ABI.
# Legacy callers provide four-byte stack alignment; align SIMD locals internally.
# Do not enable fast-math or globally require unverified AVX/FMA instructions.
target_compile_options(d3d9 PRIVATE -Wall -Wextra -Wno-cast-function-type -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2)
target_link_options(d3d9 PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--kill-at -Wl,--enable-stdcall-fixup)
# Only _except_handler3 comes from msvcrt.dll; never substitute broad msvcrt-os,
# which can silently redirect the existing UCRT imports.
target_link_libraries(d3d9 PRIVATE "${X3M_COMPOSITOR_RUNTIME}" dxguid advapi32 psapi)
set_target_properties(d3d9 PROPERTIES PREFIX "" OUTPUT_NAME "d3d9")
# Verification fixture (built, run by the Wine queue owner): the Win32 read path of the
# data-driven fog family file, same source and flags as the DLL's loader.
add_executable(fog_family_file_fixture verification/probe/fog_family_file_fixture.cpp src/renderer/fog_field_assets.cpp)
target_compile_features(fog_family_file_fixture PRIVATE cxx_std_17)
target_include_directories(fog_family_file_fixture PRIVATE src/renderer "${CMAKE_CURRENT_BINARY_DIR}/generated/fog_field")
target_compile_definitions(fog_family_file_fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(fog_family_file_fixture PRIVATE -Wall -Wextra -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2)
target_link_options(fog_family_file_fixture PRIVATE -static -static-libgcc -static-libstdc++)
add_dependencies(fog_family_file_fixture x3m_fog_field_assets)
# Verification fixture (built here, run by the Wine queue owner through
# verification/probe/run_gpu_sync_timing.py): the production --gpu-sync-timing owner on
# the bottle's D3D9 device (soft fail, 48 frames of fake passes, Reset, detach).
add_executable(gpu_sync_timing_fixture verification/probe/gpu_sync_timing_fixture.cpp src/renderer/gpu_sync_timing.cpp)
target_compile_features(gpu_sync_timing_fixture PRIVATE cxx_std_17)
target_compile_definitions(gpu_sync_timing_fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(gpu_sync_timing_fixture PRIVATE -Wall -Wextra -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2)
target_link_options(gpu_sync_timing_fixture PRIVATE -static -static-libgcc -static-libstdc++)
target_link_libraries(gpu_sync_timing_fixture PRIVATE user32)
# Verification fixtures (built here, run by the Wine queue owner through verification/probe/run_window_mode.py
# and run_cursor_reassert.py): the production --window-monitor-rect move on the bottle's display driver, and the
# --cursor-reassert state machine / Win32 sequence with the window-thread hooks it shares with --window-trace.
add_executable(window_mode_fixture verification/probe/window_mode_fixture.cpp src/proxy/window_mode.cpp)
add_executable(cursor_reassert_fixture verification/probe/cursor_reassert_fixture.cpp src/proxy/cursor_reassert.cpp src/proxy/window_trace.cpp
  src/proxy/loading_trace_light.cpp)
foreach(fixture window_mode_fixture cursor_reassert_fixture)
  target_compile_features(${fixture} PRIVATE cxx_std_17)
  target_compile_definitions(${fixture} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(${fixture} PRIVATE -Wall -Wextra -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2)
  target_link_options(${fixture} PRIVATE -static -static-libgcc -static-libstdc++)
  target_link_libraries(${fixture} PRIVATE user32 gdi32)
endforeach()
# Feasibility probe (run by the Wine queue owner through verification/probe/run_d3d11_interop.py):
# D3D9 / D3D9Ex / D3D11 devices in one process, cross-API texture sharing, the mask dilation
# as ps_3_0 passes vs one cs_5_0 dispatch, DXGI HDR colour-space queries, D3D11 caps. Every
# graphics API is loaded at run time (no d3d9/d3d11/dxgi import); version.dll reads the
# module version resources for provenance.
add_executable(d3d11_interop_fixture verification/probe/d3d11_interop_fixture.cpp)
target_compile_features(d3d11_interop_fixture PRIVATE cxx_std_17)
target_compile_definitions(d3d11_interop_fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(d3d11_interop_fixture PRIVATE -Wall -Wextra -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2)
target_link_options(d3d11_interop_fixture PRIVATE -static -static-libgcc -static-libstdc++)
target_link_libraries(d3d11_interop_fixture PRIVATE user32 version)
# GPU backend A/B fixture (run by the Wine queue owner through verification/probe/run_gpu_backend_ab.py):
# the same full-screen pass and 460-draw scene on D3D9 and on D3D11, event-query and timestamp
# brackets (docs/architecture/d3d9-to-d3d11-translation.md). Graphics APIs loaded at run time.
add_executable(gpu_backend_ab_fixture verification/probe/gpu_backend_ab_fixture.cpp)
target_compile_features(gpu_backend_ab_fixture PRIVATE cxx_std_17)
target_compile_definitions(gpu_backend_ab_fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(gpu_backend_ab_fixture PRIVATE -Wall -Wextra -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2)
target_link_options(gpu_backend_ab_fixture PRIVATE -static -static-libgcc -static-libstdc++)
target_link_libraries(gpu_backend_ab_fixture PRIVATE user32 version advapi32)
# Occlusion cull fixture (run by the Wine queue owner through verification/probe/run_occlusion_cull.py): the production
# pass on a real device, the cull on and off frame by frame (docs/architecture/occlusion-cull.md).
add_executable(occlusion_cull_fixture verification/probe/occlusion_cull_fixture.cpp src/renderer/occlusion_cull_pass.cpp)
target_compile_features(occlusion_cull_fixture PRIVATE cxx_std_17)
target_compile_definitions(occlusion_cull_fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(occlusion_cull_fixture PRIVATE -Wall -Wextra -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2)
target_link_options(occlusion_cull_fixture PRIVATE -static -static-libgcc -static-libstdc++)
target_link_libraries(occlusion_cull_fixture PRIVATE user32)
# D3D9 backend smoke probe (run by the Wine queue owner through verification/probe/run_d3d9_backend_smoke.py):
# loads a given d3d9.dll (wined3d, CrossOver's DXVK, a DXVK-over-MoltenVK build) and d3dx9_37 at run
# time, draws readback cases in the game's device shape and sweeps the game's shader programs.
add_executable(d3d9_backend_smoke_fixture verification/probe/d3d9_backend_smoke_fixture.cpp)
target_compile_features(d3d9_backend_smoke_fixture PRIVATE cxx_std_17)
target_compile_definitions(d3d9_backend_smoke_fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(d3d9_backend_smoke_fixture PRIVATE -Wall -Wextra -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2)
target_link_options(d3d9_backend_smoke_fixture PRIVATE -static -static-libgcc -static-libstdc++)
target_link_libraries(d3d9_backend_smoke_fixture PRIVATE user32)
