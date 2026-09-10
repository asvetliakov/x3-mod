#pragma once
#include <windows.h>
#include <d3d9.h>

// Hooks preserve the backend's COM pointers, including resource GetDevice identity.
// No GPU state is modified by capture. Detailed reads occur only in requested frames.
namespace x3m {
void initialize_log(HMODULE module);
void log(const char* format, ...);
void hook_direct3d(IDirect3D9* object);
}
