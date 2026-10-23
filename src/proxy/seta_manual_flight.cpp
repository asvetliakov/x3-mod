#include "seta_manual_flight.h"
#include "seta_manual_flight_stubs.h"
#include "capture.h"
#include "config.h"
#include "cpu_state.h"
#include "engine_memory.h"
#include "engine_patch.h"
#include "log_tiers.h"
#include "object_trace.h"
#include <windows.h>
#include <atomic>
#include <cstring>

static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
using namespace x3m::seta_manual_flight::core;
namespace engine_patch = x3m::engine_patch;

const char* state_ = "disabled";
unsigned mode_ = 0;
// Part A: the eight stop rewrites. stop_patched_[i] is published before its write, so a failed rollback stays
// registered for shutdown().
bool stop_patched_[stop_patch_count]{};
DWORD stop_protection_ = 0;
const char* stop_status_ = "unpatched";
const char* stop_write_ = "none";
// Part B: one engine_patch site per axis; registered_ = claimed (or rollback_failed), restored by shutdown().
engine_patch::Site sites_[axis_count]{};
bool registered_[axis_count]{};
// armed_: a complete install; until then (and after shutdown) every stub takes its original path.
std::atomic<bool> armed_{false};
bool debug_ = false;
std::atomic<std::uint64_t> frame_{0};
// The debug row's counters since the previous row (main thread only: the stubs run inside the input routine).
unsigned long applied_[axis_count]{}, skipped_[skip_count]{};
unsigned long long last_row_ms_ = 0;
constexpr unsigned long long row_interval_ms = 500; // wall clock: the row is for reading real-time rise times
Mismatch mismatch_{};

void* stub(unsigned axis) {
    switch (axis) {
    case yaw: return reinterpret_cast<void*>(&x3m_smf_yaw_stub);
    case pitch: return reinterpret_cast<void*>(&x3m_smf_pitch_stub);
    case roll: return reinterpret_cast<void*>(&x3m_smf_roll_stub);
    case strafe_y: return reinterpret_cast<void*>(&x3m_smf_strafe_y_stub);
    default: return reinterpret_cast<void*>(&x3m_smf_strafe_x_stub);
    }
}
std::uint32_t address_of(const void* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}

// Rate-limited under debug = 1, from the yaw stub only while the scale applies: the section 6 flight check reads the
// real-time rise of ship_rate (obj+0x24 at the span: the rate the controller stored last frame) and its steady
// ship_rate_k = obj+0x24 * k, which equals the unscaled target when the ship turns at its unwarped real-time rate.
void debug_row(unsigned axis, Skip skip, const Inputs& in, const Out& out, std::uint32_t ship) {
    if (axis < axis_count && skip == applied) ++applied_[axis];
    if (skip < skip_count && skip != applied) ++skipped_[skip];
    if (axis != yaw || skip != applied) return;
    const unsigned long long now = GetTickCount64();
    if (last_row_ms_ && now - last_row_ms_ < row_interval_ms) return;
    last_row_ms_ = now;
    std::int32_t ship_rate = 0;
    const bool readable = ship && !(ship & 3) && x3m::engine_memory::read(ship + ship_yaw_rate, &ship_rate, 4);
    const std::int32_t ship_rate_k = round_scaled(ship_rate, double(out.rate) / 65536.0);
    x3m::log("seta_manual_flight scale frame=%llu wall_ms=%llu rate=0x%08lx factor=0x%08lx governor=0x%08lx game_ms=%ld target=%ld scaled_target=%ld ship=0x%08lx ship_read=%u ship_rate=%ld ship_rate_k=%ld applied=%lu,%lu,%lu,%lu,%lu skipped_warp_off=%lu skipped_rate=%lu skipped_mouse=%lu skipped_contract=%lu mouse=%lu,%lu,%lu,%lu",
             static_cast<unsigned long long>(frame_.load(std::memory_order_relaxed)), now,
             static_cast<unsigned long>(out.rate), static_cast<unsigned long>(in.factor),
             static_cast<unsigned long>(in.governor), static_cast<long>(in.game_ms), static_cast<long>(in.target),
             static_cast<long>(out.target), static_cast<unsigned long>(ship), unsigned(readable),
             static_cast<long>(ship_rate), static_cast<long>(ship_rate_k), applied_[yaw], applied_[pitch],
             applied_[roll], applied_[strafe_y], applied_[strafe_x], skipped_[warp_off], skipped_[rate_out_of_range],
             skipped_[mouse_delta], skipped_[contract], static_cast<unsigned long>(in.mouse_x),
             static_cast<unsigned long>(in.mouse_y), static_cast<unsigned long>(in.mouse_turret_x),
             static_cast<unsigned long>(in.mouse_turret_y));
    for (unsigned long& n : applied_) n = 0;
    for (unsigned long& n : skipped_) n = 0;
}
}

extern "C" {
// Called by the five stubs (seta_manual_flight_stubs.h) on the main thread inside the input routine 0x00410100, once
// per axis per frame. Integer reads, a few SSE2 double operations under the default MXCSR (every exception masked),
// three stores; the debug row is formatted behind call_preserved (x3m::log). No x87, no allocation, no lock.
__attribute__((force_align_arg_pointer)) void __cdecl x3m_seta_manual_flight_axis(unsigned axis, std::uint32_t record,
                                                                                 std::uint32_t ship) {
    x3m::LightCallBoundary cpu; // MXCSR + LastError of the engine, restored on return
    if (!armed_.load(std::memory_order_acquire)) {
        x3m_smf_apply = 0;
        return;
    }
    const std::uint32_t mxcsr = 0x1f80; // round to nearest, all exceptions masked: the engine's MXCSR is not trusted
    asm volatile("ldmxcsr %0" ::"m"(mxcsr));
    Inputs in;
    Out out;
    const Skip skip = x3m::seta_manual_flight::publish(axis, record, in, out);
    if (debug_) debug_row(axis, skip, in, out, ship);
}
}

namespace {
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&armed_), &module) != FALSE &&
           module != nullptr;
}
bool windows_match(unsigned mode) {
    return compare_windows(
        [](std::uint32_t at, void* out, unsigned n) {
            return engine_patch::read_code(at, static_cast<unsigned char*>(out), n);
        },
        mode, mismatch_);
}
void hex(const unsigned char* bytes, unsigned n, char* out) {
    static const char digits[] = "0123456789abcdef";
    if (!n) {
        out[0] = '-';
        out[1] = 0;
        return;
    }
    for (unsigned i = 0; i < n; ++i) {
        out[2 * i] = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & 15];
    }
    out[2 * n] = 0;
}
// Two bytes of one stop rewrite under the page protection the caller arranged, flushed; returns whether the read-back
// holds `value`.
bool write_stop(const Patch& p, const unsigned char* value, bool* atomic) {
    const bool used_atomic = engine_patch::write_code(p.va, value, p.length);
    if (atomic) *atomic = used_atomic;
    unsigned char back[2]{};
    return FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(p.va), p.length) != FALSE &&
           engine_patch::read_code(p.va, back, p.length) && !std::memcmp(back, value, p.length);
}
// Puts every registered rewrite back in reverse order (the span already writable); a row whose original read-back holds
// is unregistered. Returns whether all are back.
bool restore_stop_rows() {
    bool back = true;
    for (unsigned i = stop_patch_count; i-- > 0;) {
        if (!stop_patched_[i]) continue;
        if (write_stop(stop_patches[i], stop_patches[i].original, nullptr))
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
bool patch_stop() {
    auto* code = reinterpret_cast<void*>(stop_span_va);
    DWORD unused = 0;
    if (!VirtualProtect(code, stop_span_length, PAGE_EXECUTE_READWRITE, &stop_protection_)) {
        stop_status_ = "protect_failed";
        return false;
    }
    bool written = true, atomic = true;
    for (unsigned i = 0; i < stop_patch_count && written; ++i) {
        stop_patched_[i] = true;
        bool used_atomic = false;
        written = write_stop(stop_patches[i], stop_patches[i].patched, &used_atomic);
        atomic = atomic && used_atomic;
    }
    stop_write_ = atomic ? "atomic" : "plain";
    const bool protected_again = VirtualProtect(code, stop_span_length, stop_protection_, &unused) != FALSE;
    if (written && protected_again) {
        stop_status_ = "active";
        return true;
    }
    // Rollback of every row written so far, judged by read-backs of the original bytes, not by the protection calls.
    DWORD writable = 0;
    if (!protected_again || VirtualProtect(code, stop_span_length, PAGE_EXECUTE_READWRITE, &writable)) {
        const bool back = restore_stop_rows();
        VirtualProtect(code, stop_span_length, stop_protection_, &unused);
        if (back) {
            stop_status_ = "patch_rolled_back";
            return false;
        }
    }
    stop_status_ = "rollback_failed"; // a rewrite may be live: kept registered for shutdown()
    return false;
}
bool unpatch_stop() {
    if (!any_stop_patched()) return true;
    // Only when every registered row still holds this feature's bytes (or the original): a foreign write is not undone.
    for (unsigned i = 0; i < stop_patch_count; ++i) {
        if (!stop_patched_[i]) continue;
        const Patch& p = stop_patches[i];
        unsigned char current[2]{};
        if (!engine_patch::read_code(p.va, current, p.length) ||
            (std::memcmp(current, p.patched, p.length) && std::memcmp(current, p.original, p.length))) {
            stop_status_ = "restore_not_owned";
            return false;
        }
    }
    auto* code = reinterpret_cast<void*>(stop_span_va);
    DWORD previous = 0, unused = 0;
    if (!VirtualProtect(code, stop_span_length, PAGE_EXECUTE_READWRITE, &previous)) {
        stop_status_ = "restore_protect_failed";
        return false;
    }
    const bool back = restore_stop_rows();
    const bool protected_again = VirtualProtect(code, stop_span_length, previous, &unused) != FALSE;
    stop_status_ = back && protected_again ? "restored" : back ? "restored_unprotected" : "restore_failed";
    return back;
}
// The five claims in axis order; the continuation slot is set before the stub becomes the chain head.
bool claim_sites(const char** reason) {
    for (unsigned a = 0; a < axis_count; ++a) {
        engine_patch::SiteSpec spec{};
        spec.name = sites[a].name;
        spec.address = sites[a].va;
        spec.length = sites[a].length;
        spec.ret_pop = 0;
        spec.rel32_offset = 0;
        std::memcpy(spec.expected, sites[a].bytes, sites[a].length);
        sites_[a] = engine_patch::Site{};
        if (!engine_patch::claim(sites_[a], spec)) {
            *reason = sites_[a].status;
            registered_[a] = sites_[a].patched_in; // rollback_failed: the jmp may be live
            return false;
        }
        registered_[a] = true;
        x3m_smf_continue[a] = address_of(*sites_[a].entry);
        if (!engine_patch::push_front(sites_[a], stub(a))) {
            *reason = "chain_failed";
            return false;
        }
    }
    return true;
}
bool release_sites() {
    bool ok = true;
    for (unsigned a = axis_count; a-- > 0;) {
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
// Part A, then part B; a failure puts back everything written so far.
bool install(unsigned mode, const char** reason) {
    mismatch_ = Mismatch{};
    if (!x3m::object_trace::executable_verified()) {
        *reason = "executable_mismatch";
        return false;
    }
    if (!engine_patch::install_window_open()) {
        *reason = "late_claim";
        return false;
    }
    if (!windows_match(mode)) {
        *reason = "bytes_mismatch";
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
        if (length) log("seta_manual_flight installed=0 mode=0 reason=%s", state_);
        SetLastError(error);
        return false;
    }
    debug_ = x3m::log_tier::debug();
    for (unsigned long& n : applied_) n = 0;
    for (unsigned long& n : skipped_) n = 0;
    last_row_ms_ = 0;
    const bool ok = install(mode_, &state_);
    armed_.store(ok && mode_ >= 2, std::memory_order_release);
    if (ok)
        log("seta_manual_flight installed=1 mode=%u stop=speed_only stop_sites=%u stop_rows=0x%08lx stop_status=%s stop_write=%s yaw=%s:%s pitch=%s:%s roll=%s:%s strafe_y=%s:%s strafe_x=%s:%s gate=factor>0x10000,rate>0x10000 k=rate/65536 rate=(factor*governor+0x8000)>>16 mouse_delta_skip=e40,e44,e48,e4c debug=%u",
            mode_, stop_patch_count, static_cast<unsigned long>(stop_rows_va), stop_status_, stop_write_, site_status(yaw),
            site_write(yaw), site_status(pitch), site_write(pitch), site_status(roll), site_write(roll),
            site_status(strafe_y), site_write(strafe_y), site_status(strafe_x), site_write(strafe_x),
            unsigned(debug_));
    else {
        const Window* w = mismatch_.window;
        char expected[2 * shown_bytes + 1], got[2 * shown_bytes + 1];
        hex(w ? w->bytes : nullptr, w ? mismatch_.shown : 0, expected);
        if (w && !mismatch_.readable)
            std::memcpy(got, "unreadable", sizeof "unreadable");
        else
            hex(mismatch_.got, w ? mismatch_.shown : 0, got);
        log("seta_manual_flight refused reason=%s mode=%u window=0x%08lx window_name=%s window_length=%u expected=%s got=%s stop_status=%s yaw=%s pitch=%s roll=%s strafe_y=%s strafe_x=%s patched=%u",
            state_, mode_, static_cast<unsigned long>(w ? w->va : 0), w ? w->name : "-", w ? w->length : 0u, expected,
            got, stop_status_, site_status(yaw), site_status(pitch), site_status(roll), site_status(strafe_y),
            site_status(strafe_x), unsigned(any_registered()));
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
}
