#include "scene_hook.h"
#include "object_trace.h"
#include "cpu_state.h"
#include <array>
#include <atomic>
#include <cstring>

static_assert(sizeof(void*) == 4, "Verified x86 callsite only");
namespace {
using Listener = x3m::scene_hook::Listener;
std::atomic<Listener> listener{nullptr};
std::atomic<unsigned long long> signal_count{0};
unsigned char* patched_site = nullptr;
std::array<unsigned char, 5> before_bytes{}, our_bytes{};
bool installed_ = false, requested_ = false;
DWORD original_protection = 0;
std::atomic<const char*> state{"disabled"};
// The verified callsite and its callee (preferred base 0x00400000): `CALL
// 0x004c4750` at 0x004721b1, next instruction 0x004721b6, so rel32 =
// 0x004c4750 - 0x004721b6 = 0x0005259a.
constexpr std::uintptr_t callsite_va = 0x004721b1, target_va = 0x004c4750;
constexpr unsigned char expected_bytes[5] = {0xe8, 0x9a, 0x25, 0x05, 0x00};
bool read_memory(std::uintptr_t address, void* out, std::size_t size) {
    SIZE_T copied = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), out, size, &copied) && copied == size;
}
}
extern "C" {
void (*x3m_scene_end_original)() = nullptr;
// Full CPU boundary (cpu_state.h, the same FNSAVE/FRSTOR transport as the
// heavy device hooks): the listener runs the temporal resolve, telemetry and
// the log formatter, which execute x87 code (int64-to-double conversions,
// the CRT's float formatting), so the light MXCSR-only contract does not
// apply. The x87 stack, control and status words, MXCSR and the thread's
// last error return to the frame routine as they were; once per frame.
__attribute__((force_align_arg_pointer)) void __cdecl x3m_scene_end_signal() {
    x3m::PreserveCpuState cpu;
    signal_count.fetch_add(1, std::memory_order_relaxed);
    if (Listener fn = listener.load()) fn();
}
// Replaces the callee of `CALL 0x004c4750`: every general register and the
// flags are as the frame routine left them when the original runs; the
// original's `ret` returns to 0x004721b6. No prologue is relocated.
__attribute__((naked)) void x3m_scene_end_trampoline() {
    __asm__ __volatile__(
        "pushfl\n\tpushal\n\t"
        "call _x3m_scene_end_signal\n\t"
        "popal\n\tpopfl\n\t"
        "jmp *_x3m_scene_end_original\n\t");
}
}
namespace x3m::scene_hook {
namespace {
bool patch(void* site, void* target) {
    if (installed_) return false; // Owned already: the status of the live patch stands.
    if (!site || !target) { state = "invalid_patch_request"; return false; }
    std::array<unsigned char, 5> code{};
    if (!read_memory(reinterpret_cast<std::uintptr_t>(site), code.data(), code.size()) || code[0] != 0xe8) { state = "callsite_mismatch"; return false; }
    std::uint32_t displacement = 0; std::memcpy(&displacement, code.data() + 1, 4);
    if (reinterpret_cast<std::uintptr_t>(site) + 5 + displacement != reinterpret_cast<std::uintptr_t>(target)) { state = "target_mismatch"; return false; }
    auto replacement = code;
    const std::uint32_t redirected = reinterpret_cast<std::uintptr_t>(&x3m_scene_end_trampoline) - (reinterpret_cast<std::uintptr_t>(site) + 5);
    std::memcpy(replacement.data() + 1, &redirected, 4);
    DWORD protection = 0;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &protection)) { state = "protect_failed"; return false; }
    // Publish ownership before mutation so a failed rollback stays recoverable.
    patched_site = static_cast<unsigned char*>(site); before_bytes = code; our_bytes = replacement;
    original_protection = protection; installed_ = true;
    x3m_scene_end_original = reinterpret_cast<void (*)()>(target);
    std::memcpy(site, replacement.data(), 5);
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, 5) != FALSE;
    DWORD unused = 0;
    const bool protected_again = VirtualProtect(site, 5, protection, &unused) != FALSE;
    if (!flushed || !protected_again) {
        DWORD writable = 0;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &writable)) { state = "rollback_protect_failed"; return false; }
        std::memcpy(site, code.data(), 5);
        const bool rollback_flush = FlushInstructionCache(GetCurrentProcess(), site, 5) != FALSE;
        const bool rollback_protect = VirtualProtect(site, 5, protection, &unused) != FALSE;
        state = rollback_flush && rollback_protect ? "patch_rolled_back" : "rollback_failed";
        if (rollback_flush && rollback_protect) { installed_ = false; patched_site = nullptr; }
        return false;
    }
    state = "active"; return true;
}
}
// X3M_SCENE_HOOK: "1" on, "0" off, unset (or any other value) follows the
// route: on with X3M_MOTION_OUTPUT=1, off without it (the hook has no consumer
// then). Parsed on the startup path only.
bool wanted() {
    wchar_t setting[4]{};
    const DWORD length = GetEnvironmentVariableW(L"X3M_SCENE_HOOK", setting, 4);
    if (length == 1 && setting[0] == L'1') return true;
    if (length == 1 && setting[0] == L'0') return false;
    return GetEnvironmentVariableW(L"X3M_MOTION_OUTPUT", setting, 4) == 1 && setting[0] == L'1';
}
bool initialize(Listener fn) {
    const DWORD error = GetLastError();
    if (installed_) { SetLastError(error); return active(); }
    if (!wanted()) { state = "disabled"; SetLastError(error); return false; }
    requested_ = true;
    if (!object_trace::executable_verified()) { state = "executable_mismatch"; SetLastError(error); return false; }
    unsigned char call[5]{};
    if (!read_memory(callsite_va, call, 5) || std::memcmp(call, expected_bytes, 5)) { state = "callsite_mismatch"; SetLastError(error); return false; }
    listener.store(fn);
    const bool result = patch(reinterpret_cast<void*>(callsite_va), reinterpret_cast<void*>(target_va));
    SetLastError(error); return result;
}
bool requested() { return requested_; }
bool installed() { return installed_; }
bool active() { return installed_ && !std::strcmp(state.load(), "active"); }
const char* status() { return state.load(); }
unsigned long long signals() { return signal_count.load(std::memory_order_relaxed); }
bool shutdown() {
    const DWORD error = GetLastError();
    if (!installed_) { SetLastError(error); return true; }
    unsigned char code[5]{}; DWORD protection = 0, unused = 0;
    if (!read_memory(reinterpret_cast<std::uintptr_t>(patched_site), code, 5) ||
        (std::memcmp(code, our_bytes.data(), 5) && std::memcmp(code, before_bytes.data(), 5))) {
        state = "shutdown_not_owned"; SetLastError(error); return false;
    }
    listener.store(nullptr);
    if (!VirtualProtect(patched_site, 5, PAGE_EXECUTE_READWRITE, &protection)) { state = "shutdown_protect_failed"; SetLastError(error); return false; }
    std::memcpy(patched_site, before_bytes.data(), 5);
    const bool flush = FlushInstructionCache(GetCurrentProcess(), patched_site, 5) != FALSE;
    const bool protect = VirtualProtect(patched_site, 5, original_protection, &unused) != FALSE;
    state = flush && protect ? "restored" : "restore_failed";
    if (flush && protect) { installed_ = false; patched_site = nullptr; }
    SetLastError(error); return flush && protect;
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
bool fixture_install(void* site, void* target, Listener fn) {
    listener.store(fn); requested_ = true;
    return patch(site, target);
}
bool fixture_shutdown() { return shutdown(); }
#endif
}
