#pragma once
#include <windows.h>

// Session identity: one `proxy_identity` line and one `proxy_options` line at
// proxy attach, before the derived *_mode lines, so that a gameplay log names
// the DLL it came from (docs/architecture/platform-portability.md, "Session
// identity"). Documented Win32 only (GetModuleFileNameW, CreateFileW/ReadFile,
// CryptoAPI SHA-256, GetEnvironmentStringsW); runs once, off the render path.
namespace x3m::proxy_identity {
void log_identity(HMODULE self);
}
