#include "executable_identity_fl.h"
#include <atomic>

namespace x3m::fl {
namespace {
// Fault-free bounded read of this process (documented ReadProcessMemory on the current process).
bool read_self(std::uintptr_t address, void* out, std::size_t size) {
    SIZE_T done = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &done) &&
           done == size;
}
struct Result {
    std::uintptr_t base;
    const char* reason;
};
// Racing first callers compute the same value; the static initialiser runs once (C++11 thread-safe statics).
const Result& result() {
    static const Result value = [] {
        HMODULE module = GetModuleHandleW(nullptr);
        const auto base = reinterpret_cast<std::uintptr_t>(module);
        const char* reason = structure_mismatch(base, read_self);
        if (!reason && !known_file_size(module)) reason = "file_size";
        return Result{reason ? 0 : base, reason};
    }();
    return value;
}
}
bool verified() {
    const DWORD error = GetLastError();
    const bool ok = result().reason == nullptr;
    SetLastError(error);
    return ok;
}
std::uintptr_t module_base() {
    const DWORD error = GetLastError();
    const std::uintptr_t base = result().base;
    SetLastError(error);
    return base;
}
const char* mismatch_reason() {
    const DWORD error = GetLastError();
    const char* reason = result().reason;
    SetLastError(error);
    return reason;
}
}
