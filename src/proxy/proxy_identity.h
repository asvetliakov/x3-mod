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
// in the process is a recorded fact rather than a name-based guess. The line
// also carries what the mapped image says about itself (image_size, stamp,
// exports and the informational wine_builtin marker), because under Wine a
// builtin module keeps the native file's path and size and is otherwise
// indistinguishable on that line. Same documented Win32/PE set as
// log_identity; one hash, once, off the render path.
void log_loaded_module(const wchar_t* name);
void log_loaded_module(HMODULE module, const char* name);
}
