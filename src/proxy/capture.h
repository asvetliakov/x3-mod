#pragma once
#include <windows.h>
#include <d3d9.h>

// Hooks preserve the backend's COM pointers, including resource GetDevice identity.
// Ordinary capture performs readback queries only. Explicit scene-depth capture
// uses the ownership copy helper's scoped, restored GPU state changes. Detailed
// reads and optional depth preservation occur only in requested capture frames.
namespace x3m {
void initialize_log(HMODULE module);
void log(const char* format, ...);
void hook_direct3d(IDirect3D9* object);
// Capture output directory (wide path, no trailing separator). Valid after initialize_log.
const wchar_t* capture_directory();
// Listener of the engine scene-end hook (scene_hook.h): called on the render
// thread before the frame routine's compositing call; forwards to every hooked
// device's route under the capture mutex.
void scene_end_signal();
// QPC stamp taken in DllMain (DLL_PROCESS_ATTACH): the origin of the
// frame_end elapsed_ms field, which exists in every mode so a plain --direct
// run's load times can be read from the log without telemetry.
extern unsigned long long dll_load_qpc;
}
