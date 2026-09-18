#include "cull_small_parts.h"
#include "cull_small_parts_core.h"
#include "cull_census.h"
#include "camera_state.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <cstring>

// Nothing here runs inside the engine's pass: the stub is straight-line
// integer code emitted from cull_small_parts_core.h (no call into this
// module), so this file is compiled with the ordinary proxy flags. begin_frame,
// present and after_reset run on the proxy's own frame path (the thread that
// issues BeginScene/Present and runs the pass), where the other frame-scoped
// modules already use SSE.
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
namespace core = x3m::cull_small_parts::core;
namespace engine_patch = x3m::engine_patch;
bool patched_ = false;
engine_patch::Site site_{};
std::uintptr_t stub_ = 0;
const char* state_ = "disabled";
double px_ = 0;
unsigned width_ = 0;
float last_m00_ = 0;
std::int32_t last_threshold_ = 0;
unsigned value_lines_ = 0;
core::Scope scope_ = core::Scope::bodies;

bool bytes_match(std::uintptr_t at, const unsigned char* expected, unsigned length) {
    unsigned char actual[core::window_length]{};
    return length <= core::window_length && engine_patch::read_code(at, actual, length) && !std::memcmp(actual, expected, length);
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN),
// so the stub's absolute operands can never point into freed memory.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE && module != nullptr;
}
// A setting variable as printable ASCII (anything else is logged as "?").
bool read_setting(const wchar_t* name, char* out, unsigned capacity) {
    wchar_t text[32]{};
    const DWORD length = GetEnvironmentVariableW(name, text, 32);
    if (length == 0) return false;
    if (length >= 32 || length + 1 > capacity) { out[0] = '?'; out[1] = 0; return true; }
    for (DWORD i = 0; i < length; ++i) out[i] = (text[i] >= 0x21 && text[i] <= 0x7e) ? static_cast<char>(text[i]) : '?';
    out[length] = 0; return true;
}
// Emits the stub followed by its 4-aligned continuation slot; 0 when the arena is full.
std::uintptr_t emit_stub(std::uint32_t cull_target, core::Scope scope, void*** slot_out) {
    engine_patch::Emitter e(core::stub_length + 8);
    if (!e.ok()) return 0;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.here());
    const std::uintptr_t slot = (at + core::stub_length + 3) & ~std::uintptr_t(3);
    unsigned char code[core::stub_length];
    core::encode_stub(std::uint32_t(at), std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_cull_small_parts_threshold)),
                      std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_cull_small_parts_culled)), cull_target, std::uint32_t(slot), code, scope);
    e.bytes(code, core::stub_length);
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < slot) e.byte(0xcc);
    e.dword(0);
    if (!e.finish()) return 0;
    *slot_out = reinterpret_cast<void**>(slot);
    return at;
}
}

volatile std::int32_t x3m_cull_small_parts_threshold = 0;   // declared extern "C" in the header
volatile std::uint32_t x3m_cull_small_parts_culled = 0;

namespace x3m::cull_small_parts {
bool install_at(std::uintptr_t site, std::uintptr_t cull_target, bool bodies_only) {
    if (patched_) { state_ = "already_installed"; return false; }
    const core::Scope scope = bodies_only ? core::Scope::bodies : core::Scope::all;
    const char* reason = nullptr;
    if (!site || site < core::site_offset || cull_target != site - core::site_offset + core::cull_offset) reason = "invalid_site";
    else if (!engine_patch::install_window_open()) reason = "late_claim";
    else if (!bytes_match(site - core::site_offset, core::window, core::window_length)) reason = "bytes_mismatch";
    else if (!pin_self()) reason = "pin_failed";
    std::uintptr_t stub = 0; void** slot = nullptr;
    if (!reason && !(stub = emit_stub(std::uint32_t(cull_target), scope, &slot))) reason = "arena_full";
    if (!reason) {
        x3m_cull_small_parts_threshold = 0; x3m_cull_small_parts_culled = 0;
        engine_patch::SiteSpec spec{};
        spec.name = "cull_small_parts"; spec.address = site; spec.length = core::site_length; spec.ret_pop = core::ret_pop; spec.rel32_offset = 0;
        std::memcpy(spec.expected, core::site, core::site_length);
        site_ = engine_patch::Site{};
        if (!engine_patch::claim(site_, spec)) {
            reason = site_.status;
            if (site_.patched_in) { patched_ = true; stub_ = 0; state_ = "rollback_failed"; return false; } // registered for shutdown()
            site_ = engine_patch::Site{};
        } else if (!engine_patch::store_pointer(slot, *site_.entry) || !engine_patch::push_front(site_, reinterpret_cast<void*>(stub))) {
            // The site is live with its tail only (vanilla behaviour): put the bytes back;
            // a failed restore keeps the site registered so shutdown() tries again.
            reason = "chain_failed";
            if (!engine_patch::restore(site_)) { patched_ = true; stub_ = 0; state_ = "rollback_failed"; return false; }
            patched_ = false; stub_ = 0; site_ = engine_patch::Site{};
        } else {
            patched_ = true; stub_ = stub; scope_ = scope; reason = "ok";
        }
    }
    state_ = reason;
    return patched_ && !std::strcmp(reason, "ok");
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) { SetLastError(error); return true; }
    char setting[32]{};
    if (!read_setting(L"X3M_CULL_SMALL_PARTS_PX", setting, sizeof setting)) { state_ = "disabled"; SetLastError(error); return false; }
    char scope_text[32]{};
    read_setting(L"X3M_CULL_SMALL_PARTS_SCOPE", scope_text, sizeof scope_text);   // unset = the default, bodies
    core::Scope scope = core::Scope::bodies;
    const bool scope_ok = core::parse_scope(scope_text, &scope);
    double px = 0;
    const bool parsed = core::parse_px(setting, &px);
    bool applied = false;
    if (parsed && px == 0.0) state_ = "disabled";                     // an explicit 0 is the documented off
    else if (!parsed || !core::valid_px(px)) state_ = "invalid_px";
    else if (!scope_ok) state_ = "invalid_scope";                     // fail closed: nothing patched
    else if (!object_trace::executable_verified()) state_ = "executable_mismatch";
    else { px_ = px; applied = install_at(core::site_va, core::cull_va, scope == core::Scope::bodies); }
    log("cull_small_parts requested=%s px=%.4g patched=%u reason=%s site=0x%08lx cull=0x%08lx write=%s stub=0x%08lx camera=%s scope=%s",
        setting, applied ? px : 0.0, patched_ ? 1u : 0u, state_, static_cast<unsigned long>(core::site_va), static_cast<unsigned long>(core::cull_va),
        site_.patched_in ? (site_.atomic_write ? "atomic" : "plain") : "none", static_cast<unsigned long>(stub_), camera_state::status(), scope_ok ? core::scope_name(scope) : "invalid");
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    x3m_cull_small_parts_threshold = 0;
    cull_census::note_small_threshold(0);
    const bool ok = engine_patch::restore(site_);
    patched_ = false; stub_ = 0; // the stub stays in the arena (a thread may still be inside it)
    state_ = ok ? "restored" : "restore_failed";
    SetLastError(error);
    return ok;
}
const char* state() { return state_; }
const char* scope() { return core::scope_name(scope_); }
std::uintptr_t stub_address() { return patched_ ? stub_ : 0; }
double requested_px() { return patched_ ? px_ : 0.0; }
bool set_px(double px) { if (!core::valid_px(px)) return false; px_ = px; return true; }
std::int32_t publish(float m00, unsigned width) {
    if (!patched_) return 0;
    const std::int32_t threshold = core::threshold_for(px_, m00, width);
    x3m_cull_small_parts_threshold = threshold;
    cull_census::note_small_threshold(threshold, scope_ == core::Scope::bodies);
    if (threshold != last_threshold_ || m00 != last_m00_ || width != width_) {
        last_threshold_ = threshold; last_m00_ = m00; width_ = width;
        // The projection scale changes with the FOV and the width with a Reset:
        // a bounded line per change keeps the applied threshold visible.
        if (value_lines_ < 16) {
            ++value_lines_;
            log("cull_small_parts_value px=%.4g m00=%.9g width=%u threshold=%ld", px_, static_cast<double>(m00), width, static_cast<long>(threshold));
        }
    }
    return threshold;
}
void begin_frame() {
    if (!patched_) return;
    const DWORD error = GetLastError();
    x3m_cull_small_parts_culled = 0;
    // The engine's live projection (P[0]) through the read-only camera latch;
    // an unreadable or non-perspective matrix (menus, loading) leaves the
    // frame vanilla. The width is the back buffer's from CreateDevice/Reset.
    float m00 = 0;
    camera_state::Sample sample{};
    if (camera_state::available() && camera_state::read(&sample) && sample.state.valid) m00 = sample.state.m00;
    publish(m00, width_);
    SetLastError(error);
}
void set_backbuffer_width(unsigned width) { width_ = width; }
void after_reset(unsigned width) {
    width_ = width;
    x3m_cull_small_parts_threshold = 0;
    if (patched_) cull_census::note_small_threshold(0);
}
void present(unsigned long long device, unsigned long long frame, bool captured) {
    if (!patched_) return;
    if (captured) {
        const DWORD error = GetLastError();
        log("cull_small_parts_frame device=%llu frame=%llu px=%.4g threshold=%ld culled=%lu m00=%.9g width=%u scope=%s",
            device, frame, px_, static_cast<long>(x3m_cull_small_parts_threshold), static_cast<unsigned long>(x3m_cull_small_parts_culled), static_cast<double>(last_m00_), width_, core::scope_name(scope_));
        SetLastError(error);
    }
    x3m_cull_small_parts_culled = 0;
}
Stats stats() {
    Stats s{};
    s.threshold = x3m_cull_small_parts_threshold; s.culled = x3m_cull_small_parts_culled; s.m00 = last_m00_; s.width = width_;
    return s;
}
}
