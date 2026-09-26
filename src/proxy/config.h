#pragma once
// The proxy's settings resolver (docs/architecture/config-file.md, section 5): config::get is a drop-in for
// GetEnvironmentVariableW over the X3M_* names, with the precedence schema default < x3m.ini < environment.
// Header-only on purpose: the fixture and host builds that compile one proxy source against stubs link nothing new;
// until config::load (src/proxy/config.cpp, the first statement of initialize_log) installs the resolver, and in every
// such build, get is exactly GetEnvironmentVariableW. After load the resolver is immutable and lock-free: no
// allocation, no lock, one binary search over the generated table.
#include <windows.h>

namespace x3m::config {
using Resolver = DWORD (*)(const wchar_t* name, wchar_t* out, DWORD capacity) noexcept;
inline Resolver resolver = nullptr; // written once by load(), before any device and any other reader thread
// The GetEnvironmentVariableW contract: the length written (without the terminator), the size needed (with it) when
// `capacity` is too small, 0 when nothing is set (last error ERROR_ENVVAR_NOT_FOUND) or the value is empty.
inline DWORD get(const wchar_t* name, wchar_t* out, DWORD capacity) noexcept {
    const Resolver r = resolver;
    return r ? r(name, out, capacity) : GetEnvironmentVariableW(name, out, capacity);
}
}
