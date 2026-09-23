#pragma once
#include <windows.h>
#include <d3d9.h>
#include "compositor_bridge.h"

// Hooks preserve the backend's COM pointers, including resource GetDevice identity.
// Ordinary capture performs readback queries only. Explicit scene-depth capture
// uses the ownership copy helper's scoped, restored GPU state changes. Detailed
// reads and optional depth preservation occur only in requested capture frames.
namespace x3m {
// Step C screen emission requested with its prerequisites (linear materials,
// TAA); computed once in initialize_log. The loader enables the step B
// Unlock scan through this gate, not through the raw variable.
bool screen_emission_route_enabled() noexcept;
// Bolt footprint requested with its prerequisites (the additive route and
// X3M_OWNERSHIP=1); computed once in initialize_log. The loader enables the
// step D Unlock scan for it as well: the footprint reads the scanned vertices.
bool bolt_footprint_requested_gate() noexcept;
void initialize_log(HMODULE module);
void log(const char* format, ...);
// The session log's OS handle (INVALID_HANDLE_VALUE when there is none): for a
// best-effort unbuffered WriteFile from exception context, where neither the
// log mutex nor stdio may be touched. Set once when the log opens.
HANDLE log_handle() noexcept;
void hook_direct3d(IDirect3D9* object);
// Capture output directory (wide path, no trailing separator). Valid after initialize_log.
const wchar_t* capture_directory();
// The engine_memory summary line (phase create|summary); telemetry.cpp calls
// it from every summary. Integers only.
void engine_memory_line(const char* phase, unsigned long long device, unsigned long long frame);
// Listener of the engine scene-end hook (scene_hook.h): called on the render
// thread before the frame routine's compositing call; forwards to every hooked
// device's route under the capture mutex.
void scene_end_signal();
// Optional original/pre/post transport. scene_hook fills original from its
// verified target and retains an immutable copy for the process lifetime.
const X3mCompositorBinding* compositor_binding() noexcept;
// QPC stamp taken in DllMain (DLL_PROCESS_ATTACH): the origin of the
// frame_end elapsed_ms field, which exists in every mode so a plain --direct
// run's load times can be read from the log without telemetry.
extern unsigned long long dll_load_qpc;
// DllMain DLL_PROCESS_DETACH only: abandons every live stored-density fog worker (no join, no
// lock, no log) so the static teardown that follows cannot wait on a thread the OS already ended.
void abandon_fog_density_workers() noexcept;
}
