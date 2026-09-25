#include "cull_small_parts.h"
#include "cull_small_parts_core.h"
#include "cull_census.h"
#include "camera_state.h"
#include "fov.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <cstring>

// Nothing here runs inside the engine's pass: the stub is straight-line
// integer code emitted from cull_small_parts_core.h (no call into this
// module), so this file is compiled with the ordinary proxy flags. begin_frame
// and present run in the proxy's Present hook, after_reset in its Reset hook,
// on the application's render thread (the one that runs the pass), where the
// other frame-scoped modules already use SSE.
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
std::uint32_t last_focus_ = core::focus_default;
core::Source last_source_ = core::Source::registry;
core::Fallback last_fallback_ = core::Fallback::no_scene;
std::int32_t last_threshold_ = 0;
unsigned value_lines_ = 0;
constexpr unsigned value_line_cap = 128;  // cull_small_parts_value rows per process (a menu FOV sweep is about 70 steps)
// The scene view's P[0]/P[5] from the motion route's scene-phase Clear
// (note_scene_projection); written and read on the engine's render thread
// only (the Clear hook and the Present hook that calls begin_frame run
// there), so no synchronisation.
core::SceneLatch scene_{};
bool projectiles_ = true;               // the installed stub exempts marked projectile nodes
const char* projectiles_state_ = "on";  // install-line value: on, off, marker_mismatch, invalid

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
std::uintptr_t emit_stub(std::uint32_t cull_target, bool exempt_projectiles, void*** slot_out) {
    engine_patch::Emitter e(core::stub_length + 8);
    if (!e.ok()) return 0;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.here());
    const std::uintptr_t slot = (at + core::stub_length + 3) & ~std::uintptr_t(3);
    unsigned char code[core::stub_length];
    core::encode_stub(std::uint32_t(at), std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_cull_small_parts_threshold)),
                      std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_cull_small_parts_culled)),
                      std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_cull_small_parts_exempt)), cull_target, std::uint32_t(slot), code, exempt_projectiles);
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
volatile std::uint32_t x3m_cull_small_parts_exempt = 0;

namespace x3m::cull_small_parts {
bool install_at(std::uintptr_t site, std::uintptr_t cull_target, bool exempt_projectiles) {
    if (patched_) { state_ = "already_installed"; return false; }
    const char* reason = nullptr;
    if (!site || site < core::site_offset || cull_target != site - core::site_offset + core::cull_offset) reason = "invalid_site";
    else if (!engine_patch::install_window_open()) reason = "late_claim";
    else if (!bytes_match(site - core::site_offset, core::window, core::window_length)) reason = "bytes_mismatch";
    else if (!pin_self()) reason = "pin_failed";
    std::uintptr_t stub = 0; void** slot = nullptr;
    if (!reason && !(stub = emit_stub(std::uint32_t(cull_target), exempt_projectiles, &slot))) reason = "arena_full";
    if (!reason) {
        x3m_cull_small_parts_threshold = 0; x3m_cull_small_parts_culled = 0; x3m_cull_small_parts_exempt = 0;
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
            patched_ = true; stub_ = stub; projectiles_ = exempt_projectiles; reason = "ok";
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
    char projectiles_text[32]{};
    read_setting(L"X3M_CULL_SMALL_PARTS_PROJECTILES", projectiles_text, sizeof projectiles_text);   // unset = the default, on
    bool exempt = true;
    const bool projectiles_ok = core::parse_projectiles(projectiles_text, &exempt);
    projectiles_state_ = !projectiles_ok ? "invalid" : exempt ? "on" : "off";
    double px = 0;
    const bool parsed = core::parse_px(setting, &px);
    bool applied = false;
    if (parsed && px == 0.0) state_ = "disabled";                     // an explicit 0 is the documented off
    else if (!parsed || !core::valid_px(px)) state_ = "invalid_px";
    else if (!projectiles_ok) state_ = "invalid_projectiles";         // fail closed: nothing patched
    else if (!object_trace::executable_verified()) state_ = "executable_mismatch";
    else {
        // The exemption relies on the engine's own class-0 marker: both instructions that
        // establish it must be the verified bytes, else the stub culls projectiles like any node.
        if (exempt && !(bytes_match(core::marker_store_va, core::marker_store, core::marker_store_length) && bytes_match(core::marker_or_va, core::marker_or, core::marker_or_length))) {
            exempt = false; projectiles_state_ = "marker_mismatch";
        }
        px_ = px; applied = install_at(core::site_va, core::cull_va, exempt);
    }
    log("cull_small_parts requested=%s px=%.4g patched=%u reason=%s site=0x%08lx cull=0x%08lx write=%s stub=0x%08lx camera=%s scope=%s projectiles=%s",
        setting, applied ? px : 0.0, patched_ ? 1u : 0u, state_, static_cast<unsigned long>(core::site_va), static_cast<unsigned long>(core::cull_va),
        site_.patched_in ? (site_.atomic_write ? "atomic" : "plain") : "none", static_cast<unsigned long>(stub_), camera_state::status(), "all",
        projectiles_state_);
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
bool projectiles_exempt() { return patched_ && projectiles_; }
std::uintptr_t stub_address() { return patched_ ? stub_ : 0; }
double requested_px() { return patched_ ? px_ : 0.0; }
bool set_px(double px) { if (!core::valid_px(px)) return false; px_ = px; return true; }
namespace {
std::int32_t apply(float m00, unsigned width, std::uint32_t focus, core::Source source, core::Fallback fallback) {
    if (!patched_) return 0;
    const std::int32_t threshold = core::threshold_for(px_, m00, width, focus);
    x3m_cull_small_parts_threshold = threshold;
    cull_census::note_small_threshold(threshold, projectiles_);
    last_m00_ = m00;
    if (threshold != last_threshold_ || width != width_ || focus != last_focus_ || source != last_source_ || fallback != last_fallback_) {
        last_threshold_ = threshold; width_ = width; last_focus_ = focus; last_source_ = source; last_fallback_ = fallback;
        // The view's FOV changes the focus (and P[0]), a Reset the width, the
        // first scene latch the source: a bounded line per change keeps the
        // applied threshold visible.
        if (value_lines_ < value_line_cap) {
            ++value_lines_;
            log("cull_small_parts_value px=%.4g m00=%.9g width=%u threshold=%ld focus=0x%04lx source=%s fallback=%s", px_, static_cast<double>(m00), width, static_cast<long>(threshold),
                static_cast<unsigned long>(focus), core::source_name(source), core::fallback_name(fallback));
        }
    }
    return threshold;
}
}
std::int32_t publish(float m00, unsigned width, std::uint32_t focus, bool scene) {
    return apply(m00, width, focus, scene ? core::Source::scene : core::Source::registry, core::Fallback::none);
}
bool wants_scene_projection() { return patched_; }
void note_scene_projection(float m00, float m11) { if (patched_) scene_.note(m00, m11); }
void begin_frame() {
    if (!patched_) return;
    const DWORD error = GetLastError();
    x3m_cull_small_parts_culled = 0; x3m_cull_small_parts_exempt = 0;
    // The engine's live projection through the read-only camera latch gates
    // the frame: an unreadable or non-perspective matrix (menus, loading)
    // leaves it vanilla. The width is the back buffer's from CreateDevice/Reset.
    // The view's FOV: s = r*640/D' with D' = D * F/0x4000, F the view camera's
    // +0x298 (base / zoom), so the pixel scale of s carries F/0x4000. P[0] and
    // F (cot(F/2) = max(0.75*m11, m00), zoom included) come from the scene
    // view's projection the motion route latched at the scene Clear, not from
    // the live buffer, which in the Present hook holds the last view of the
    // frame just presented (core::SceneLatch). Without a usable latch: the registry base F
    // (one registry read), P[0] the live projection rescaled to it.
    // A vanilla frame (no valid live projection) keeps the latch's state in its row.
    core::Choice choice{0.0f, core::focus_default, core::Source::registry, scene_.fallback()};
    camera_state::Sample sample{};
    if (camera_state::available() && camera_state::read(&sample) && sample.state.valid)
        choice = core::choose(scene_, sample.state.m00, sample.state.m11, scene_.usable() ? 0u : fov::current_focus());
    scene_.advance();
    apply(choice.m00, width_, choice.focus, choice.source, choice.fallback);
    SetLastError(error);
}
void set_backbuffer_width(unsigned width) { width_ = width; }
void after_reset(unsigned width) {
    width_ = width;
    scene_.clear();   // a Reset can change the aspect: the next scene Clear latches again
    x3m_cull_small_parts_threshold = 0;
    if (patched_) cull_census::note_small_threshold(0);
}
void present(unsigned long long device, unsigned long long frame, bool captured) {
    if (!patched_) return;
    if (captured) {
        const DWORD error = GetLastError();
        log("cull_small_parts_frame device=%llu frame=%llu px=%.4g threshold=%ld culled=%lu m00=%.9g width=%u scope=%s projectiles=%s exempt_bullet=%lu focus=0x%04lx source=%s fallback=%s",
            device, frame, px_, static_cast<long>(x3m_cull_small_parts_threshold), static_cast<unsigned long>(x3m_cull_small_parts_culled), static_cast<double>(last_m00_), width_, "all",
            projectiles_ ? "on" : "off", static_cast<unsigned long>(x3m_cull_small_parts_exempt), static_cast<unsigned long>(last_focus_), core::source_name(last_source_),
            core::fallback_name(last_fallback_));
        SetLastError(error);
    }
    x3m_cull_small_parts_culled = 0; x3m_cull_small_parts_exempt = 0;
}
Stats stats() {
    Stats s{};
    s.threshold = x3m_cull_small_parts_threshold; s.culled = x3m_cull_small_parts_culled; s.exempt = x3m_cull_small_parts_exempt; s.m00 = last_m00_; s.width = width_; s.focus = last_focus_; s.scene = last_source_ == core::Source::scene; s.fallback = unsigned(last_fallback_);
    return s;
}
}
