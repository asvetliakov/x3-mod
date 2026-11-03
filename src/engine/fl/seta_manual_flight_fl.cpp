// FL edition of x3m::seta_manual_flight (src/proxy/seta_manual_flight.h; the ap target keeps
// src/proxy/seta_manual_flight.cpp): X3M_SETA_MANUAL_FLIGHT on X3FL.exe (docs/reverse-engineering/fl/seta.md sections
// 3 and 4, "Port (2026-11-03)"; sites, windows and the FL evaluate: seta_manual_flight_sites_fl.h; the per-axis
// decision core::decide and the key latch policy are AP's, src/proxy/seta_manual_flight_core.h and
// seta_key_latch_core.h).
//
//   1 = part A: the eight stop-row Jcc at base+0x0d2186..0x0d21ce (seven JNE -> `66 90`, the look JE -> JMP), each two
//       bytes written under one VirtualProtect of the span and read back; seven through lock cmpxchg8b, the pitch row
//       0x0d218f (straddles a qword) through the plain copy, and the install refuses unless every row took exactly the
//       path its geometry predicts (write_path_mismatch) -- the emitted atomicity check;
//   2 = part A and part B: the yaw/pitch/roll SSE spans base+0x0ce878/0x0ceb88/0x0ced98 (16 bytes) and the strafe
//       spans base+0x0d1f04/0x0d1f8b (15 bytes) claimed through engine_patch with the stubs below in front; each claim
//       read back (the strafe Y jump straddles a qword: plain write inside the install window).
// Install (backend-load path, inside the engine_patch install window; later: late_claim): X3FL.exe identity
// (fl::module_base() != 0), every window of the mode's parts at base + RVA with its fixups bound to base, before any
// write; part A, then part B; a failure puts back everything written so far (a rollback that cannot complete keeps the
// rows/sites registered: rollback_failed). The stubs are file-scope asm in this DLL, so the module is pinned. The FL
// loader has no unload path (its DLL_PROCESS_DETACH calls are AP only): shutdown() is for the fixture.
//
// Stubs (one per axis, ESP leaves as it entered on both paths): PUSHFD, EAX/ECX/EDX, XMM0..7 (all 128 bits), FNSAVE (the
// whole x87 image; FNINIT for the helper), STMXCSR, CLD; call the helper (axis, the record/cfg register, the ship); then
// LDMXCSR, FRSTOR, XMM0..7 back and the helper's decision byte read; the apply path replays the span with the scale on
// the engine's restored registers (SSE only: no GPR or EFLAGS write), and only then EDX/ECX/EAX and POPFD, so EFLAGS,
// DF and every GPR leave as they entered; the original path jumps to the claim tail (the displaced span replayed by
// engine_patch). The helper restores LastError (LightCallBoundary) and runs under MXCSR 0x1f80.
#include "../../proxy/seta_manual_flight.h"
#include "../../proxy/seta_key_latch_core.h"
#include "../../proxy/seta_restore.h"
#include "../../proxy/config.h"
#include "../../proxy/cpu_state.h"
#include "../../proxy/engine_memory.h"
#include "../../proxy/engine_patch.h"
#include "../../proxy/log_tiers.h"
#include "executable_identity_fl.h"
#include "seta_manual_flight_sites_fl.h"
#include "seta_restore_sites_fl.h"
#include <windows.h>
#include <atomic>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
namespace F = x3m::fl::seta_manual_flight_fl;
namespace W = x3m::fl::seta_windows_fl;
namespace core = x3m::seta_manual_flight::core;
namespace engine_patch = x3m::engine_patch;

const char* state_ = "disabled";
unsigned mode_ = 0;
std::uintptr_t base_ = 0;
unsigned failed_window_ = F::window_count;
// Part A: stop_patched_[i] is published before its write, so a failed rollback stays registered for shutdown().
bool stop_patched_[F::stop_patch_count]{};
DWORD stop_protection_ = 0;
const char* stop_status_ = "unpatched";
unsigned stop_atomic_ = 0, stop_plain_ = 0;
// Part B: one engine_patch site per axis; registered_ = claimed (or rollback_failed), restored by shutdown().
engine_patch::Site sites_[core::axis_count]{};
bool registered_[core::axis_count]{};
std::atomic<bool> armed_{false}; // a complete install; until then (and after shutdown) every stub takes its original path
bool debug_ = false;
std::atomic<std::uint64_t> frame_{0};
unsigned long applied_[core::axis_count]{}, skipped_[core::skip_count]{};
unsigned long long last_row_ms_ = 0;
constexpr unsigned long long row_interval_ms = 500; // wall clock: the row is for reading real-time rise times
#ifdef X3M_SETA_MANUAL_FLIGHT_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
unsigned fixture_fault_row_ = ~0u, fixture_fault_claim_ = ~0u; // one-shot: that row's / claim's read-back sees a foreign byte
#endif
std::uintptr_t module_base() {
#ifdef X3M_SETA_MANUAL_FLIGHT_FL_FIXTURE
    if (fixture_base_) return fixture_base_;
#endif
    return x3m::fl::module_base();
}
}

extern "C" {
// Written by the helper on every call (main thread, the input routine is not reentrant), read by the stub right after.
volatile unsigned char x3m_fl_smf_apply = 0;
volatile double x3m_fl_smf_scale = 1;
volatile std::int32_t x3m_fl_smf_target = 0;
// The chain continuation per axis (the claim tail) and the span ends (base + RVA + length), set at install.
volatile std::uint32_t x3m_fl_smf_continue[core::axis_count] = {};
volatile std::uint32_t x3m_fl_smf_resume[core::axis_count] = {};
void x3m_fl_smf_yaw_stub();
void x3m_fl_smf_pitch_stub();
void x3m_fl_smf_roll_stub();
void x3m_fl_smf_strafe_y_stub();
void x3m_fl_smf_strafe_x_stub();
}

namespace {
// Rate-limited under debug = 1, from the yaw stub only while the scale applies (the AP row with edition=fl).
void debug_row(unsigned axis, core::Skip skip, const core::Inputs& in, const core::Out& out, std::uint32_t ship) {
    if (axis < core::axis_count && skip == core::applied) ++applied_[axis];
    if (skip < core::skip_count && skip != core::applied) ++skipped_[skip];
    if (axis != core::yaw || skip != core::applied) return;
    const unsigned long long now = GetTickCount64();
    if (last_row_ms_ && now - last_row_ms_ < row_interval_ms) return;
    last_row_ms_ = now;
    std::int32_t ship_rate = 0;
    const bool readable = ship && !(ship & 3) && x3m::engine_memory::read(ship + F::ship_yaw_rate, &ship_rate, 4);
    const std::int32_t ship_rate_k = core::round_scaled(ship_rate, double(out.rate) / 65536.0);
    x3m::log("seta_manual_flight scale edition=fl frame=%llu wall_ms=%llu rate=0x%08lx factor=0x%08lx governor=0x%08lx game_ms=%ld target=%ld scaled_target=%ld ship=0x%08lx ship_read=%u ship_rate=%ld ship_rate_k=%ld applied=%lu,%lu,%lu,%lu,%lu skipped_warp_off=%lu skipped_rate=%lu skipped_mouse=%lu skipped_contract=%lu mouse=%lu,%lu,%lu,%lu",
             static_cast<unsigned long long>(frame_.load(std::memory_order_relaxed)), now,
             static_cast<unsigned long>(out.rate), static_cast<unsigned long>(in.factor),
             static_cast<unsigned long>(in.governor), static_cast<long>(in.game_ms), static_cast<long>(in.target),
             static_cast<long>(out.target), static_cast<unsigned long>(ship), unsigned(readable),
             static_cast<long>(ship_rate), static_cast<long>(ship_rate_k), applied_[core::yaw], applied_[core::pitch],
             applied_[core::roll], applied_[core::strafe_y], applied_[core::strafe_x], skipped_[core::warp_off],
             skipped_[core::rate_out_of_range], skipped_[core::mouse_delta], skipped_[core::contract],
             static_cast<unsigned long>(in.mouse_x), static_cast<unsigned long>(in.mouse_y),
             static_cast<unsigned long>(in.mouse_turret_x), static_cast<unsigned long>(in.mouse_turret_y));
    for (unsigned long& n : applied_) n = 0;
    for (unsigned long& n : skipped_) n = 0;
}
}

extern "C" {
// Called by the five stubs on the main thread inside the input routine, once per axis per frame: plain integer reads,
// a few SSE2 double operations under MXCSR 0x1f80, three stores; no x87 (the stub's FNSAVE keeps the engine's image
// anyway), no allocation, no lock. `reg` is EDI (rotation: the record) or ESI (strafe: the cfg pointer).
__attribute__((force_align_arg_pointer)) void __cdecl x3m_fl_seta_manual_flight_axis(unsigned axis, std::uint32_t reg,
                                                                                    std::uint32_t ship) {
    x3m::LightCallBoundary cpu; // MXCSR + LastError of the engine, restored on return
    if (!armed_.load(std::memory_order_acquire)) {
        x3m_fl_smf_apply = 0;
        return;
    }
    const std::uint32_t mxcsr = 0x1f80;
    asm volatile("ldmxcsr %0" ::"m"(mxcsr));
    core::Inputs in;
    core::Out out;
    const core::Skip skip = F::evaluate(axis, reg, base_, in, out);
    if (skip == core::applied) {
        x3m_fl_smf_scale = F::step_scale(axis, out);
        x3m_fl_smf_target = out.target;
    }
    x3m_fl_smf_apply = skip == core::applied ? 1 : 0;
    if (debug_) debug_row(axis, skip, in, out, ship);
}
}

asm(R"(
    .intel_syntax noprefix
    .macro X3M_FL_SMF_CALL axis, reg, ship
    pushfd
    push eax
    push ecx
    push edx
    sub esp, 0xf0
    movdqu [esp+0x00], xmm0
    movdqu [esp+0x10], xmm1
    movdqu [esp+0x20], xmm2
    movdqu [esp+0x30], xmm3
    movdqu [esp+0x40], xmm4
    movdqu [esp+0x50], xmm5
    movdqu [esp+0x60], xmm6
    movdqu [esp+0x70], xmm7
    fnsave [esp+0x80]
    stmxcsr [esp+0xec]
    cld
    push \ship
    push \reg
    push \axis
    call _x3m_fl_seta_manual_flight_axis
    add esp, 12
    ldmxcsr [esp+0xec]
    frstor [esp+0x80]
    movdqu xmm0, [esp+0x00]
    movdqu xmm1, [esp+0x10]
    movdqu xmm2, [esp+0x20]
    movdqu xmm3, [esp+0x30]
    movdqu xmm4, [esp+0x40]
    movdqu xmm5, [esp+0x50]
    movdqu xmm6, [esp+0x60]
    movdqu xmm7, [esp+0x70]
    cmp byte ptr [_x3m_fl_smf_apply], 0
    .endm

    .macro X3M_FL_SMF_LEAVE slots, axis
    add esp, 0xf0
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [\slots + 4*\axis]
    .endm

    .macro X3M_FL_SMF_ROTATION name, axis
    .text
    .p2align 4
    .globl _x3m_fl_smf_\name\()_stub
_x3m_fl_smf_\name\()_stub:
    X3M_FL_SMF_CALL \axis, edi, esi
    je 1f
    mulsd xmm0, qword ptr [_x3m_fl_smf_scale]
    cvtpd2ps xmm3, xmm0
    movd xmm0, dword ptr [_x3m_fl_smf_target]
    cvtdq2ps xmm0, xmm0
    mulss xmm0, xmm4
    X3M_FL_SMF_LEAVE _x3m_fl_smf_resume, \axis
1:
    X3M_FL_SMF_LEAVE _x3m_fl_smf_continue, \axis
    .endm

    .macro X3M_FL_SMF_STRAFE name, axis, out
    .text
    .p2align 4
    .globl _x3m_fl_smf_\name\()_stub
_x3m_fl_smf_\name\()_stub:
    X3M_FL_SMF_CALL \axis, esi, edi
    je 1f
    mulss xmm0, xmm6
    cvtps2pd xmm0, xmm0
    mulsd xmm0, xmm7
    mulsd xmm0, qword ptr [_x3m_fl_smf_scale]
    cvtpd2ps \out, xmm0
    X3M_FL_SMF_LEAVE _x3m_fl_smf_resume, \axis
1:
    X3M_FL_SMF_LEAVE _x3m_fl_smf_continue, \axis
    .endm

    X3M_FL_SMF_ROTATION yaw, 0
    X3M_FL_SMF_ROTATION pitch, 1
    X3M_FL_SMF_ROTATION roll, 2
    X3M_FL_SMF_STRAFE strafe_y, 3, xmm4
    X3M_FL_SMF_STRAFE strafe_x, 4, xmm1
    .att_syntax
)");

namespace {
void* stub(unsigned axis) {
    switch (axis) {
    case core::yaw: return reinterpret_cast<void*>(&x3m_fl_smf_yaw_stub);
    case core::pitch: return reinterpret_cast<void*>(&x3m_fl_smf_pitch_stub);
    case core::roll: return reinterpret_cast<void*>(&x3m_fl_smf_roll_stub);
    case core::strafe_y: return reinterpret_cast<void*>(&x3m_fl_smf_strafe_y_stub);
    default: return reinterpret_cast<void*>(&x3m_fl_smf_strafe_x_stub);
    }
}
std::uint32_t address_of(const void* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
bool pin_self() {
#ifdef X3M_SETA_MANUAL_FLIGHT_FL_FIXTURE
    return true; // the fixture executable is never unloaded
#else
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&armed_), &module) != FALSE &&
           module != nullptr;
#endif
}
bool read_back(std::uintptr_t at, unsigned char* out, unsigned n) {
    return engine_patch::read_code(at, out, n);
}
// Two bytes of one stop rewrite under the page protection the caller arranged, flushed; true when the read-back holds
// `value`. *atomic: whether engine_patch::write_code took lock cmpxchg8b.
bool write_stop(unsigned i, const unsigned char* value, bool* atomic) {
    const std::uintptr_t at = base_ + F::stop_patches[i].rva;
    const bool used_atomic = engine_patch::write_code(at, value, 2);
    if (atomic) *atomic = used_atomic;
    unsigned char back[2]{};
    const bool ok = FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at), 2) != FALSE &&
                    read_back(at, back, 2);
#ifdef X3M_SETA_MANUAL_FLIGHT_FL_FIXTURE
    if (fixture_fault_row_ == i) {
        fixture_fault_row_ = ~0u;
        back[1] ^= 0xff;
    }
#endif
    return ok && !std::memcmp(back, value, 2);
}
bool restore_stop_rows() {
    bool back = true;
    for (unsigned i = F::stop_patch_count; i-- > 0;) {
        if (!stop_patched_[i]) continue;
        if (write_stop(i, F::stop_patches[i].original, nullptr))
            stop_patched_[i] = false;
        else
            back = false;
    }
    return back;
}
bool any_stop_patched() {
    for (bool p : stop_patched_)
        if (p) return true;
    return false;
}
// The eight rows, each read back; a row whose write path differs from its qword geometry (atomic except the pitch row)
// refuses the install too: the plain copy is admitted only for the straddling row, inside the install window.
bool patch_stop() {
    auto* code = reinterpret_cast<void*>(base_ + F::stop_span_rva);
    DWORD unused = 0;
    stop_atomic_ = stop_plain_ = 0;
    if (!engine_patch::install_window_open()) {
        stop_status_ = "late_claim";
        return false;
    }
    if (!VirtualProtect(code, F::stop_span_length, PAGE_EXECUTE_READWRITE, &stop_protection_)) {
        stop_status_ = "protect_failed";
        return false;
    }
    bool written = true, paths = true;
    for (unsigned i = 0; i < F::stop_patch_count && written; ++i) {
        stop_patched_[i] = true;
        bool used_atomic = false;
        written = write_stop(i, F::stop_patches[i].patched, &used_atomic);
        ++(used_atomic ? stop_atomic_ : stop_plain_);
        paths = paths && used_atomic == !F::straddles(std::uint32_t(base_ + F::stop_patches[i].rva), 2);
    }
    const bool protected_again = VirtualProtect(code, F::stop_span_length, stop_protection_, &unused) != FALSE;
    if (written && paths && protected_again) {
        stop_status_ = "active";
        return true;
    }
    DWORD writable = 0;
    if (!protected_again || VirtualProtect(code, F::stop_span_length, PAGE_EXECUTE_READWRITE, &writable)) {
        const bool back = restore_stop_rows();
        VirtualProtect(code, F::stop_span_length, stop_protection_, &unused);
        if (back) {
            stop_status_ = !written ? "patch_rolled_back" : !paths ? "write_path_mismatch" : "protect_rolled_back";
            return false;
        }
    }
    stop_status_ = "rollback_failed"; // a rewrite may be live: kept registered for shutdown()
    return false;
}
bool unpatch_stop() {
    if (!any_stop_patched()) return true;
    for (unsigned i = 0; i < F::stop_patch_count; ++i) { // a foreign write is not undone
        if (!stop_patched_[i]) continue;
        unsigned char current[2]{};
        if (!read_back(base_ + F::stop_patches[i].rva, current, 2) ||
            (std::memcmp(current, F::stop_patches[i].patched, 2) && std::memcmp(current, F::stop_patches[i].original, 2))) {
            stop_status_ = "restore_not_owned";
            return false;
        }
    }
    auto* code = reinterpret_cast<void*>(base_ + F::stop_span_rva);
    DWORD previous = 0, unused = 0;
    if (!VirtualProtect(code, F::stop_span_length, PAGE_EXECUTE_READWRITE, &previous)) {
        stop_status_ = "restore_protect_failed";
        return false;
    }
    const bool back = restore_stop_rows();
    const bool protected_again = VirtualProtect(code, F::stop_span_length, previous, &unused) != FALSE;
    stop_status_ = back && protected_again ? "restored" : back ? "restored_unprotected" : "restore_failed";
    return back;
}
// The five claims in axis order: the continuation and resume slots are set before the stub becomes the chain head;
// each claim is read back (this module's jump at the site, the path its geometry predicts).
bool claim_sites(const char** reason) {
    for (unsigned a = 0; a < core::axis_count; ++a) {
        const F::Site& s = F::sites[a];
        engine_patch::SiteSpec spec{};
        spec.name = s.name;
        spec.address = base_ + s.rva;
        spec.length = s.length;
        std::memcpy(spec.expected, s.bytes, s.length);
        sites_[a] = engine_patch::Site{};
        if (!engine_patch::claim(sites_[a], spec)) {
            *reason = sites_[a].status;
            registered_[a] = sites_[a].patched_in; // rollback_failed: the jmp may be live
            return false;
        }
        registered_[a] = true;
        unsigned char now[5]{};
        const bool read = read_back(spec.address, now, 5);
#ifdef X3M_SETA_MANUAL_FLIGHT_FL_FIXTURE
        if (fixture_fault_claim_ == a) {
            fixture_fault_claim_ = ~0u;
            now[1] ^= 0xff;
        }
#endif
        if (!read || std::memcmp(now, sites_[a].patched, 5)) {
            *reason = "readback_failed";
            return false;
        }
        if (sites_[a].atomic_write == F::straddles(std::uint32_t(spec.address), 5)) {
            *reason = "write_path_mismatch";
            return false;
        }
        x3m_fl_smf_continue[a] = address_of(*sites_[a].entry);
        x3m_fl_smf_resume[a] = std::uint32_t(spec.address + s.length);
        if (!engine_patch::push_front(sites_[a], stub(a))) {
            *reason = "chain_failed";
            return false;
        }
    }
    return true;
}
bool release_sites() {
    bool ok = true;
    for (unsigned a = core::axis_count; a-- > 0;) {
        if (!registered_[a]) continue;
        if (engine_patch::restore(sites_[a]))
            registered_[a] = false;
        else
            ok = false;
    }
    return ok;
}
bool any_registered() {
    if (any_stop_patched()) return true;
    for (bool r : registered_)
        if (r) return true;
    return false;
}
bool install(unsigned mode, const char** reason) {
    failed_window_ = F::window_count;
    base_ = module_base();
    if (!base_) {
        *reason = "executable_mismatch";
        return false;
    }
    if (!engine_patch::install_window_open()) {
        *reason = "late_claim";
        return false;
    }
    if (const char* refusal = W::plan(F::windows, mode, base_,
                                      [](std::uintptr_t at, void* out, unsigned n) {
                                          return engine_patch::read_code(at, static_cast<unsigned char*>(out), n);
                                      },
                                      &failed_window_)) {
        *reason = refusal;
        return false;
    }
    if (mode >= 2 && !pin_self()) { // the engine jumps into this module's stubs
        *reason = "pin_failed";
        return false;
    }
    if (!patch_stop()) {
        *reason = stop_status_;
        return false;
    }
    if (mode >= 2 && !claim_sites(reason)) {
        const bool sites_back = release_sites();
        const bool stop_back = unpatch_stop();
        if (!sites_back || !stop_back) *reason = "rollback_failed";
        return false;
    }
    *reason = "ok";
    return true;
}
const char* site_status(unsigned a) {
    return mode_ >= 2 || registered_[a] ? sites_[a].status : "off";
}
const char* site_write(unsigned a) {
    return sites_[a].patched_in ? (sites_[a].atomic_write ? "atomic" : "plain") : "none";
}
}

namespace x3m::seta_manual_flight {
bool initialize() {
    const DWORD error = GetLastError();
    if (any_registered()) {
        SetLastError(error);
        return std::strcmp(state_, "ok") == 0;
    }
    wchar_t setting[4]{};
    const DWORD length = x3m::config::get(L"X3M_SETA_MANUAL_FLIGHT", setting, 4);
    const bool digit = length == 1 && setting[0] >= L'0' && setting[0] <= L'2';
    mode_ = digit ? unsigned(setting[0] - L'0') : 0;
    if (!mode_) {
        state_ = length && !digit ? "invalid_value" : "disabled";
        if (length) log("seta_manual_flight installed=0 mode=0 reason=%s edition=fl", state_);
        SetLastError(error);
        return false;
    }
    debug_ = x3m::log_tier::debug();
    for (unsigned long& n : applied_) n = 0;
    for (unsigned long& n : skipped_) n = 0;
    last_row_ms_ = 0;
    const bool ok = install(mode_, &state_);
    armed_.store(ok && mode_ >= 2, std::memory_order_release);
    if (ok) {
        log("seta_manual_flight installed=1 mode=%u edition=fl base=0x%08lx stop=speed_only stop_sites=%u stop_rows_rva=0x%06lx stop_status=%s stop_write=atomic:%u,plain:%u stop_plain_rva=0x%06lx yaw=%s:%s pitch=%s:%s roll=%s:%s strafe_y=%s:%s strafe_x=%s:%s gate=factor>0x10000,rate>0x10000 k=rate/65536 rate=(factor*governor+0x8000)>>16 step=1/k^2 target=1/k strafe=1/k mouse_delta_skip=1a6c60,1a6c64,1a6c68,1a6c6c debug=%u",
            mode_, static_cast<unsigned long>(base_), F::stop_patch_count,
            static_cast<unsigned long>(F::stop_rows_rva), stop_status_, stop_atomic_, stop_plain_,
            static_cast<unsigned long>(F::stop_patches[F::stop_straddling].rva), site_status(core::yaw),
            site_write(core::yaw), site_status(core::pitch), site_write(core::pitch), site_status(core::roll),
            site_write(core::roll), site_status(core::strafe_y), site_write(core::strafe_y),
            site_status(core::strafe_x), site_write(core::strafe_x), unsigned(debug_));
        namespace kl = x3m::seta_key_latch::core;
        namespace R = x3m::fl::seta_restore_fl;
        const char* latch = x3m::seta_restore::key_latch_enable(debug_);
        log("seta_manual_flight key_latch installed=%u reason=%s edition=fl arm_rva=0x%06lx:toggle_stop inject_rva=0x%06lx counter_rva=0x%06lx call_rva=0x%06lx selector=%u key=0x96 method=%s tail_entries=%u cap_entries=%u debug=%u",
            unsigned(std::strcmp(latch, "ok") == 0), latch, static_cast<unsigned long>(R::hook_rva),
            static_cast<unsigned long>(R::inject_rva), static_cast<unsigned long>(R::seta_counter_rva),
            static_cast<unsigned long>(R::script_call_rva), R::callback_none, kl::call_method, kl::tail_entries,
            kl::cap_entries, unsigned(debug_));
    } else {
        log("seta_manual_flight refused reason=%s mode=%u edition=fl base=0x%08lx window=%s window_rva=0x%06lx stop_status=%s yaw=%s pitch=%s roll=%s strafe_y=%s strafe_x=%s patched=%u",
            state_, mode_, static_cast<unsigned long>(base_),
            failed_window_ < F::window_count ? F::windows[failed_window_].name : "-",
            static_cast<unsigned long>(failed_window_ < F::window_count ? F::windows[failed_window_].rva : 0),
            stop_status_, site_status(core::yaw), site_status(core::pitch), site_status(core::roll),
            site_status(core::strafe_y), site_status(core::strafe_x), unsigned(any_registered()));
    }
    SetLastError(error);
    return ok;
}
bool shutdown() {
    const DWORD error = GetLastError();
    armed_.store(false, std::memory_order_release); // a stub still entered takes its original path
    const bool sites_back = release_sites();
    const bool stop_back = unpatch_stop();
    SetLastError(error);
    return sites_back && stop_back;
}
const char* state() {
    return state_;
}
void present(unsigned long long frame) {
    if (debug_ && armed_.load(std::memory_order_relaxed)) frame_.store(frame, std::memory_order_relaxed);
}
#ifdef X3M_SETA_MANUAL_FLIGHT_FL_FIXTURE
void fixture_set_base(std::uintptr_t base) {
    fixture_base_ = base;
}
void fixture_fault_row(unsigned row) {
    fixture_fault_row_ = row;
}
void fixture_fault_claim(unsigned axis) {
    fixture_fault_claim_ = axis;
}
void fixture_reset() { // after a shutdown: the next initialize() installs afresh
    state_ = "disabled";
    mode_ = 0;
    stop_status_ = "unpatched";
}
#endif
}
