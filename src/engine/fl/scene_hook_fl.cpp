// FL edition of x3m::scene_hook (src/proxy/scene_hook.h; the ap target keeps src/proxy/scene_hook.cpp): the
// engine's scene-end boundary in X3FL.exe (docs/architecture/farnhams-legacy-scene.md;
// docs/reverse-engineering/fl/compositor.md sections 1 and 3, fl/compositor-owner.md section 3). The complete
// pass (RVA 0x8b6d0) walks its cameras sorted by layer and, before the first camera above layer 17, calls the
// compositor once (`call rel32` at RVA 0x8b929, done byte [ebp-0xd]); a pass without such a camera makes no call.
// That call is redirected through engine_patch::claim_call (install window, one lock cmpxchg8b, rollback) to
//   - the qualified compositor bridge (x3m_compositor_bridge_entry: full CPU/SEH pre/original/post transport)
//     when capture's binding is supplied (bloom + route + HDR AgX + this hook), or
//   - the signal trampoline below: EFLAGS and all GPRs (pushfl/pushal), then in the signal XMM0-7, the x87
//     image (FNSAVE/FNINIT/FRSTOR), MXCSR and LastError around the listener; then a tail jump into the
//     compositor, whose plain RET returns to the caller at RVA 0x8b92e as before.
// Every address is fl::module_base() + RVA from the generated scene_hook_fl_inc.h; the gate compares the 24-byte
// boundary window (the call's rel32 checked as target - (site + 5)), the compositor's 14 entry bytes and the
// glow/owner/device anchors with each relocated disp32 equal to base + slot RVA; then the compositor owner read
// (compositor_owner_fl.cpp) is admitted for that base. The compositor reads no stack argument and ends in bare
// RETs (generator census), which is what both the tail jump and the bridge's call on its own stack need.
//
// Built -fno-exceptions -mno-sse -mno-mmx -mfpmath=387: the signal path has no x87 arithmetic and no compiler
// XMM use; its only x87/XMM instructions are the save/restore envelopes (cpu_state.h and XmmSave below). Hook
// counters are 32-bit atomics (a 64-bit atomic load without SSE would be x87 FILD/FISTP).
#include "../../proxy/scene_hook.h"
#include "../../proxy/compositor_owner.h"
#include "../../proxy/config.h"
#include "../../proxy/cpu_state.h"
#include "../../proxy/engine_patch.h"
#include "executable_identity_fl.h"
#include <atomic>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "x86 callsite only");
namespace {
namespace layout {
#include "scene_hook_fl_inc.h"
}
static_assert(sizeof layout::window == 24 && layout::window_site_offset == 9 && layout::window[9] == 0xe8 &&
                  layout::site_rva == layout::window_rva + layout::window_site_offset,
              "cmp; jle; call rel32 at +9; done byte; flags reload");
static_assert(layout::window[7] == 0x7e && layout::window[8] == 0x09, "jle over the call and the done-byte write");
using Listener = x3m::scene_hook::Listener;
std::atomic<Listener> listener{nullptr};
std::atomic<unsigned long> signal_count{0}, nested_entries{0};
x3m::engine_patch::CallSite site_;
X3mCompositorBinding bridge_binding{}, bridge_callbacks{};
bool bridge_bound = false, requested_ = false, bridge_requested = false;
std::atomic<const char*> state{"disabled"};
std::uintptr_t install_base = 0, install_site = 0, install_target = 0;

// XMM0-7 saved for the lifetime of the object (the listener is SSE code and may change any of them; the Win32
// calls in PreserveCpuState may change the volatile ones). movups: no alignment assumption.
struct XmmSave {
    alignas(16) unsigned char xmm[128];
    XmmSave() {
        asm volatile("movups %%xmm0,(%0)\n\tmovups %%xmm1,16(%0)\n\tmovups %%xmm2,32(%0)\n\tmovups %%xmm3,48(%0)\n\t"
                     "movups %%xmm4,64(%0)\n\tmovups %%xmm5,80(%0)\n\tmovups %%xmm6,96(%0)\n\tmovups %%xmm7,112(%0)" ::"r"(
                         xmm)
                     : "memory");
    }
    ~XmmSave() {
        asm volatile("movups (%0),%%xmm0\n\tmovups 16(%0),%%xmm1\n\tmovups 32(%0),%%xmm2\n\tmovups 48(%0),%%xmm3\n\t"
                     "movups 64(%0),%%xmm4\n\tmovups 80(%0),%%xmm5\n\tmovups 96(%0),%%xmm6\n\tmovups 112(%0),%%xmm7" ::"r"(
                         xmm)
                     : "memory");
    }
    XmmSave(const XmmSave&) = delete;
    XmmSave& operator=(const XmmSave&) = delete;
};

void bridge_pre(const X3mCompositorFrame* frame, void* storage, void*) {
    signal_count.fetch_add(1, std::memory_order_relaxed);
    if (x3m_compositor_bridge_active() > 1) nested_entries.fetch_add(1, std::memory_order_relaxed);
    bridge_callbacks.pre(frame, storage, bridge_callbacks.context);
}
bool bind_bridge(std::uintptr_t target, const X3mCompositorBinding* callbacks) {
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
// The compositor, written before the claim (a redirect may be live even when the claim fails late).
std::uintptr_t x3m_fl_scene_original = 0;
// Full CPU boundary around the listener (capture.cpp's scene_end_signal: the temporal resolve, the HDR
// write-back, logging): XMM0-7 first and last, then LastError, the x87 image (FNSAVE leaves an empty stack and
// the default control word for the listener) and MXCSR. The trampoline holds EFLAGS and the GPRs.
__attribute__((force_align_arg_pointer)) void __cdecl x3m_fl_scene_signal() {
    XmmSave xmm;
    x3m::PreserveCpuState cpu;
    signal_count.fetch_add(1, std::memory_order_relaxed);
    if (Listener fn = listener.load()) fn();
}
// Replaces the callee of the compositor call: [esp] is the caller's return (site + 5); the compositor then runs
// with every register, EFLAGS and the FP/SIMD state the complete pass left, on the same stack, and its plain
// RET returns to the caller. No prologue is relocated.
__attribute__((naked)) void x3m_fl_scene_trampoline() {
    __asm__ __volatile__("pushfl\n\tpushal\n\tcld\n\t"
                         "call _x3m_fl_scene_signal\n\t"
                         "popal\n\tpopfl\n\t"
                         "jmp *_x3m_fl_scene_original\n\t");
}
}
namespace {
// The boundary window at `window` (24 bytes, the call at +9 naming `target`, every other byte as generated) and
// the compositor's entry bytes. nullptr when the gate passes, else the reason.
const char* gate(std::uintptr_t window, std::uintptr_t target) {
    unsigned char code[sizeof layout::window]{};
    constexpr unsigned call = layout::window_site_offset;
    if (!x3m::engine_patch::read_code(window, code, sizeof code) || code[call] != 0xe8) return "callsite_mismatch";
    std::uint32_t displacement = 0;
    std::memcpy(&displacement, code + call + 1, 4);
    if (window + call + 5 + displacement != target) return "target_mismatch";
    unsigned char expected[sizeof layout::window];
    std::memcpy(expected, layout::window, sizeof expected);
    std::memcpy(expected + call + 1, code + call + 1, 4);
    if (std::memcmp(code, expected, sizeof code)) return "boundary_mismatch";
    if (!x3m::engine_patch::verify_bytes(target, layout::target_entry, sizeof layout::target_entry))
        return "entry_mismatch";
    return nullptr;
}
// An anchor at base + rva: its disp32 equal to base + slot_rva, every other byte as generated.
bool slot_anchor(std::uintptr_t base, std::uint32_t rva, const std::uint8_t* bytes, unsigned length,
                 unsigned operand, std::uint32_t slot_rva) {
    unsigned char actual[32]{};
    if (length > sizeof actual || !x3m::engine_patch::read_code(base + rva, actual, length)) return false;
    std::uint32_t value = 0;
    std::memcpy(&value, actual + operand, 4);
    if (value != base + slot_rva) return false;
    std::memset(actual + operand, 0, 4);
    return !std::memcmp(actual, bytes, length);
}
const char* anchors(std::uintptr_t base) {
    if (!slot_anchor(base, layout::glow_anchor_rva, layout::glow_anchor, sizeof layout::glow_anchor,
                     layout::glow_operand_offset, layout::config_slot_rva))
        return "glow_anchor_mismatch";
    unsigned char device[sizeof layout::device_anchor]{};
    if (!slot_anchor(base, layout::owner_anchor_rva, layout::owner_anchor, sizeof layout::owner_anchor,
                     layout::owner_operand_offset, layout::renderer_slot_rva) ||
        !x3m::engine_patch::read_code(base + layout::device_anchor_rva, device, sizeof device) ||
        std::memcmp(device, layout::device_anchor, sizeof device))
        return "owner_anchor_mismatch";
    return nullptr;
}
bool pin_self() {
    HMODULE self = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&site_), &self) != FALSE &&
           self != nullptr;
}

// ---- diagnostics: one fl.scene_sample row per period, at Present ----------------------------------------------
long long sample_period = 0, sample_due = 0; // QPC ticks; period 0: off
bool sample_force = false;
unsigned long sample_rows = 0, frames = 0, frames_without_call = 0, frames_multi_call = 0, last_signals = 0, row_signals = 0;
// X3M_FL_SCENE_LOG (fl_scene_log, as fl_trace_log): seconds between rows, 0 off, unset 10, at most 3600. Digits
// only, else the default. Read once at install (load_backend), never in the hook.
unsigned sample_seconds() {
    wchar_t text[8]{};
    const DWORD length = x3m::config::get(L"X3M_FL_SCENE_LOG", text, 8);
    if (length == 0 || length >= 8) return 10;
    unsigned value = 0;
    for (DWORD i = 0; i < length; ++i) {
        if (text[i] < L'0' || text[i] > L'9') return 10;
        value = value * 10 + unsigned(text[i] - L'0');
    }
    return value > 3600 ? 3600 : value;
}
void arm_sample() {
    sample_period = sample_due = 0;
    sample_force = false;
    frames = frames_without_call = frames_multi_call = 0;
    last_signals = row_signals = signal_count.load(std::memory_order_relaxed);
    const unsigned seconds = sample_seconds();
    LARGE_INTEGER frequency{}, now{};
    if (seconds && QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 && QueryPerformanceCounter(&now)) {
        sample_period = frequency.QuadPart * static_cast<long long>(seconds);
        sample_due = now.QuadPart + sample_period;
    }
}
const char* path_name() {
    return !site_.patched_in ? "none" : bridge_bound ? "bridge" : "signal";
}
std::uint32_t renderer_now() {
    std::uint32_t renderer = 0;
    if (install_base) x3m::engine_patch::read_code(install_base + layout::renderer_slot_rva,
                                                   reinterpret_cast<unsigned char*>(&renderer), 4);
    return renderer;
}
void log_status() {
    x3m::log("fl.scene status=%s base=0x%08lx site=0x%08lx target=0x%08lx path=%s bridge_requested=%u renderer=0x%08lx "
             "write=%s",
             state.load(), static_cast<unsigned long>(install_base), static_cast<unsigned long>(install_site),
             static_cast<unsigned long>(install_target), path_name(), unsigned(bridge_requested),
             static_cast<unsigned long>(renderer_now()),
             site_.patched_in ? (site_.atomic_write ? "atomic" : "plain") : "none");
}
// The gates passed: window, pin, bridge binding (optional), claim. On any refusal the site stays untouched; a
// claim whose rollback failed leaves the redirect registered (installed(), not active()) and forwarding.
bool install(std::uintptr_t site, std::uintptr_t target, Listener fn, const X3mCompositorBinding* callbacks) {
    if (!x3m::engine_patch::install_window_open()) {
        state = "late_claim";
        return false;
    }
    if (!pin_self()) {
        state = "pin_failed";
        return false;
    }
    if (callbacks && !bind_bridge(target, callbacks)) {
        state = "bridge_bind_failed";
        return false;
    }
    listener.store(fn);
    x3m_fl_scene_original = target;
    void* replacement = bridge_bound ? reinterpret_cast<void*>(&x3m_compositor_bridge_entry)
                                     : reinterpret_cast<void*>(&x3m_fl_scene_trampoline);
    if (!x3m::engine_patch::claim_call(site_, site, target, replacement)) {
        state = site_.status; // late_claim, callsite_mismatch, target_mismatch, protect_failed, patch_rolled_back,
                              // rollback_failed (static strings)
        if (!site_.patched_in) unbind_bridge(); // an incomplete rollback still owns the bytes: keep its binding
        return false;
    }
    arm_sample();
    state = "active";
    return true;
}
}
namespace x3m::scene_hook {
// X3M_SCENE_HOOK: "1" on, "0" off, unset (or any other value) follows the route: on with X3M_MOTION_OUTPUT=1.
bool wanted() {
    wchar_t setting[4]{};
    const DWORD length = x3m::config::get(L"X3M_SCENE_HOOK", setting, 4);
    if (length == 1 && setting[0] == L'1') return true;
    if (length == 1 && setting[0] == L'0') return false;
    return x3m::config::get(L"X3M_MOTION_OUTPUT", setting, 4) == 1 && setting[0] == L'1';
}
// The X3FL.exe identity, the window/entry gate and the module anchors at the live base; then, inside the
// engine_patch install window only (load_backend), the claim. One fl.scene row either way.
bool initialize(Listener fn, const X3mCompositorBinding* callbacks) {
    const DWORD error = GetLastError();
    if (site_.patched_in) {
        SetLastError(error);
        return active();
    }
    bool result = false;
    bridge_requested = callbacks != nullptr;
    if (!wanted())
        state = "disabled";
    else {
        requested_ = true;
        if (!x3m::fl::verified())
            state = "executable_mismatch";
        else {
            const std::uintptr_t base = x3m::fl::module_base();
            install_base = base;
            install_site = base + layout::site_rva;
            install_target = base + layout::target_rva;
            const char* refusal = gate(base + layout::window_rva, install_target);
            if (!refusal) refusal = anchors(base);
            if (refusal)
                state = refusal;
            else if ((result = install(install_site, install_target, fn, callbacks)))
                x3m::compositor_owner::admit(base);
        }
    }
    log_status();
    SetLastError(error);
    return result;
}
bool requested() {
    return requested_;
}
bool installed() {
    return site_.patched_in;
}
bool active() {
    return site_.patched_in && !std::strcmp(state.load(), "active");
}
bool compositor_active() {
    return bridge_bound && active();
}
std::uintptr_t compositor_caller_pc() {
    return compositor_active() ? install_site + 5 : 0;
}
const char* status() {
    return state.load();
}
unsigned long long signals() {
    return signal_count.load(std::memory_order_relaxed);
}
// Restores only this module's five bytes, for an externally quiescent caller (never called by the production
// loader: the module is pinned once the redirect is live).
bool shutdown() {
    const DWORD error = GetLastError();
    if (!site_.patched_in) {
        const bool result = unbind_bridge();
        SetLastError(error);
        return result;
    }
    if (bridge_bound && x3m_compositor_bridge_active()) {
        SetLastError(error);
        return false;
    }
    listener.store(nullptr);
    const bool restored = x3m::engine_patch::restore_call(site_);
    state = site_.status; // restored, restore_not_owned, restore_protect_failed, restore_failed
    const bool unbound = restored && unbind_bridge();
    SetLastError(error);
    return restored && unbound;
}
void fl_sample_row() {
    if (!sample_period) return;
    const DWORD error = GetLastError();
    const unsigned long now_signals = signal_count.load(std::memory_order_relaxed);
    const unsigned long delta = now_signals - last_signals;
    last_signals = now_signals;
    ++frames;
    if (!delta)
        ++frames_without_call;
    else if (delta > 1)
        ++frames_multi_call;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (!sample_force && now.QuadPart < sample_due) {
        SetLastError(error);
        return;
    }
    sample_force = false;
    sample_due = now.QuadPart + sample_period;
    const unsigned long entries = now_signals - row_signals;
    row_signals = now_signals;
    const auto reads = x3m::compositor_owner::take_read_counts();
    const bool glow = x3m::compositor_owner::glow_enabled(install_base);
    ++sample_rows;
    x3m::log("fl.scene_sample row=%lu status=%s path=%s frames=%lu entries=%lu frames_without_call=%lu "
             "frames_multi_call=%lu nested=%lu compositor_active=%u owner_reads=%lu owner_failed=%lu "
             "owner_last_failure=%s glow=%u renderer=0x%08lx",
             sample_rows, state.load(), path_name(), frames, entries, frames_without_call, frames_multi_call,
             nested_entries.exchange(0, std::memory_order_relaxed), unsigned(compositor_active()), reads.ok,
             reads.failed, x3m::compositor_owner::result_name(reads.last_failure), unsigned(glow),
             static_cast<unsigned long>(renderer_now()));
    frames = frames_without_call = frames_multi_call = 0;
    SetLastError(error);
}
#ifdef X3M_SCENE_HOOK_FIXTURE
bool fixture_install(void* window, void* target, Listener fn, const X3mCompositorBinding* callbacks) {
    const std::uintptr_t w = reinterpret_cast<std::uintptr_t>(window), t = reinterpret_cast<std::uintptr_t>(target);
    if (site_.patched_in) {
        state = "already_installed";
        return false;
    }
    requested_ = true;
    bridge_requested = callbacks != nullptr;
    install_base = 0;
    install_site = w + layout::window_site_offset;
    install_target = t;
    bool result = false;
    if (const char* refusal = gate(w, t))
        state = refusal;
    else
        result = install(install_site, t, fn, callbacks);
    log_status();
    return result;
}
const char* fixture_gate(std::uintptr_t base) {
    if (const char* refusal = gate(base + layout::window_rva, base + layout::target_rva)) return refusal;
    return anchors(base);
}
void fixture_force_sample() {
    sample_force = true;
}
#endif
}
