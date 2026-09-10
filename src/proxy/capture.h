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
}
