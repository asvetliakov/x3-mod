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
// The engine_memory summary line (phase create|summary); telemetry.cpp calls
// it from every summary. Integers only.
void engine_memory_line(const char* phase, unsigned long long device, unsigned long long frame);
// Listener of the engine scene-end hook (scene_hook.h): called on the render
// thread before the frame routine's compositing call; forwards to every hooked
// device's route under the capture mutex.
void scene_end_signal();
}
