#pragma once
#include <windows.h>

// Session identity: one `proxy_identity` line and one `proxy_options` line at
// proxy attach, before the derived *_mode lines, so that a gameplay log names
// the DLL it came from (docs/architecture/platform-portability.md, "Session
// identity"). Documented Win32 only (GetModuleFileNameW, CreateFileW/ReadFile,
// CryptoAPI SHA-256, GetEnvironmentStringsW); runs once, off the render path.
namespace x3m::proxy_identity {
void log_identity(HMODULE self);
// One `loaded_module` line for a DLL this process has loaded: the resolved
// path, the file size and the first 16 hex digits of its SHA-256, so which
// copy of a DLL that exists twice on disk (the game directory ships its own
// d3dx9_37.dll; the D3D9 backend comes from the system directory) is actually
// in the process is a recorded fact rather than a name-based guess. Same
// documented Win32 set as log_identity; one hash, once, off the render path.
void log_loaded_module(const wchar_t* name);
void log_loaded_module(HMODULE module, const char* name);
}
