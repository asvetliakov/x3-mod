#include "scene_hook.h"
#include "config.h"
#include "object_trace.h"
#include "cpu_state.h"
#include "engine_patch.h"
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
X3mCompositorBinding bridge_binding{}, bridge_callbacks{};
bool bridge_bound = false;
std::atomic<const char*> state{"disabled"};
// The verified callsite and its callee (preferred base 0x00400000): `CALL
// 0x004c4750` at 0x004721b1, next instruction 0x004721b6, so rel32 =
// 0x004c4750 - 0x004721b6 = 0x0005259a.
constexpr std::uintptr_t callsite_va = 0x004721b1, target_va = 0x004c4750;
constexpr unsigned char expected_bytes[5] = {0xe8, 0x9a, 0x25, 0x05, 0x00};
// Whole adjacent instructions: layer test, JLE, CALL, done-byte write, reload.
// The wrapper's extra CALL depends on this already disassembled caller ABI.
constexpr unsigned char expected_boundary[] = {
    0x83,0xbe,0x9c,0x02,0x00,0x00,0x11,0x7e,0x0a,
    0xe8,0x9a,0x25,0x05,0x00,0xc6,0x44,0x24,0x13,0x01,
    0x8b,0x86,0x70,0x02,0x00,0x00};
bool read_memory(std::uintptr_t address, void* out, std::size_t size) {
    SIZE_T copied = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), out, size, &copied) && copied == size;
}
void bridge_pre(const X3mCompositorFrame* frame, void* storage, void*) {
    signal_count.fetch_add(1, std::memory_order_relaxed);
    bridge_callbacks.pre(frame, storage, bridge_callbacks.context);
}
bool bind_bridge(void* target, const X3mCompositorBinding* callbacks) {
    if (!callbacks || !callbacks->pre || !callbacks->post || !callbacks->cleanup) return false;
    bridge_callbacks = *callbacks;
    bridge_binding = *callbacks;
    bridge_binding.original = reinterpret_cast<void (*)()>(target);
    bridge_binding.pre = &bridge_pre;
    if (!x3m_compositor_bridge_bind(&bridge_binding)) return false;
    bridge_bound = true;
    return true;
}
bool unbind_bridge() {
    if (!bridge_bound) return true;
    if (!x3m_compositor_bridge_unbind()) return false;
    bridge_bound = false;
    return true;
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
    const auto entry = bridge_bound ? &x3m_compositor_bridge_entry : &x3m_scene_end_trampoline;
    const std::uint32_t redirected = reinterpret_cast<std::uintptr_t>(entry) - (reinterpret_cast<std::uintptr_t>(site) + 5);
    std::memcpy(replacement.data() + 1, &redirected, 4);
    DWORD protection = 0;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &protection)) { state = "protect_failed"; return false; }
    // Publish ownership before mutation so a failed rollback stays recoverable.
    patched_site = static_cast<unsigned char*>(site); before_bytes = code; our_bytes = replacement;
    original_protection = protection; installed_ = true;
    x3m_scene_end_original = reinterpret_cast<void (*)()>(target);
    engine_patch::write_code(reinterpret_cast<std::uintptr_t>(site), replacement.data(), 5);
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, 5) != FALSE;
    DWORD unused = 0;
    const bool protected_again = VirtualProtect(site, 5, protection, &unused) != FALSE;
    if (!flushed || !protected_again) {
        DWORD writable = 0;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &writable)) { state = "rollback_protect_failed"; return false; }
        engine_patch::write_code(reinterpret_cast<std::uintptr_t>(site), code.data(), 5);
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
    const DWORD length = x3m::config::get(L"X3M_SCENE_HOOK", setting, 4);
    if (length == 1 && setting[0] == L'1') return true;
    if (length == 1 && setting[0] == L'0') return false;
    return x3m::config::get(L"X3M_MOTION_OUTPUT", setting, 4) == 1 && setting[0] == L'1';
}
bool initialize(Listener fn, const X3mCompositorBinding* callbacks) {
    const DWORD error = GetLastError();
    if (installed_) { SetLastError(error); return active(); }
    if (!wanted()) { state = "disabled"; SetLastError(error); return false; }
    requested_ = true;
    if (!engine_patch::install_window_open()) { state = "late_claim"; SetLastError(error); return false; }
    if (!object_trace::executable_verified()) { state = "executable_mismatch"; SetLastError(error); return false; }
    unsigned char call[5]{};
    if (!read_memory(callsite_va, call, 5) || std::memcmp(call, expected_bytes, 5)) { state = "callsite_mismatch"; SetLastError(error); return false; }
    if (callbacks) {
        unsigned char boundary[sizeof(expected_boundary)]{};
        if (!read_memory(callsite_va-9, boundary, sizeof(boundary)) ||
            std::memcmp(boundary, expected_boundary, sizeof(boundary))) {
            state = "boundary_mismatch"; SetLastError(error); return false;
        }
        if (!bind_bridge(reinterpret_cast<void*>(target_va), callbacks)) {
            state = "bridge_bind_failed"; SetLastError(error); return false;
        }
    }
    listener.store(fn);
    const bool result = patch(reinterpret_cast<void*>(callsite_va), reinterpret_cast<void*>(target_va));
    // An incomplete rollback still owns executable bytes: retain its binding.
    if (!result && !installed_) unbind_bridge();
    SetLastError(error); return result;
}
bool requested() { return requested_; }
bool installed() { return installed_; }
bool active() { return installed_ && !std::strcmp(state.load(), "active"); }
bool compositor_active() { return bridge_bound && active(); }
std::uintptr_t compositor_caller_pc() { return compositor_active() ? reinterpret_cast<std::uintptr_t>(patched_site)+5 : 0; }
const char* status() { return state.load(); }
unsigned long long signals() { return signal_count.load(std::memory_order_relaxed); }
bool shutdown() {
    const DWORD error = GetLastError();
    if (!installed_) { const bool result = unbind_bridge(); SetLastError(error); return result; }
    // Caller still owes external quiescence. This rejects an observed active
    // wrapper; zero alone does not make code mutation or DLL unload safe.
    if (bridge_bound && x3m_compositor_bridge_active()) { SetLastError(error); return false; }
    unsigned char code[5]{}; DWORD protection = 0, unused = 0;
    if (!read_memory(reinterpret_cast<std::uintptr_t>(patched_site), code, 5) ||
        (std::memcmp(code, our_bytes.data(), 5) && std::memcmp(code, before_bytes.data(), 5))) {
        state = "shutdown_not_owned"; SetLastError(error); return false;
    }
    listener.store(nullptr);
    if (!VirtualProtect(patched_site, 5, PAGE_EXECUTE_READWRITE, &protection)) { state = "shutdown_protect_failed"; SetLastError(error); return false; }
    engine_patch::write_code(reinterpret_cast<std::uintptr_t>(patched_site), before_bytes.data(), 5);
    const bool flush = FlushInstructionCache(GetCurrentProcess(), patched_site, 5) != FALSE;
    const bool protect = VirtualProtect(patched_site, 5, original_protection, &unused) != FALSE;
    state = flush && protect ? "restored" : "restore_failed";
    bool unbound = true;
    if (flush && protect) { installed_ = false; patched_site = nullptr; unbound = unbind_bridge(); }
    SetLastError(error); return flush && protect && unbound;
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
bool fixture_install(void* site, void* target, Listener fn) {
    listener.store(fn); requested_ = true;
    return patch(site, target);
}
bool fixture_shutdown() { return shutdown(); }
bool fixture_install_compositor(void* site, void* target, const X3mCompositorBinding* callbacks) {
    if (installed_ || !bind_bridge(target, callbacks)) return false;
    requested_ = true;
    const bool result = patch(site, target);
    if (!result && !installed_) unbind_bridge();
    return result;
}
#endif
}
