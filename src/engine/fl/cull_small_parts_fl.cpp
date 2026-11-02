// FL edition of x3m::cull_small_parts (src/proxy/cull_small_parts.h; the ap target keeps src/proxy/cull_small_parts.cpp):
// the projected-size cull of small parts, dock-port parts and the projectile exemption on X3FL.exe's cull/LOD selector (docs/verification/cull-small-parts.md, "FL port"; site and ABI:
// docs/reverse-engineering/fl/cull-chain.md, "Verified 2026-11-02 against X3FL.exe"; constants and the stub encoder:
// cull_small_parts_sites_fl.h). The policy is AP's (cull_small_parts_core.h: threshold_for, upper_for, the scene latch,
// the dock ranges, the flag words); the site, registers and offsets are FL's.
//
// Far jets (X3M_ENGINE_EFFECTS exactly plumes, as on AP): the stub hands culled JET nodes (+0x130 & 0x4000001) to
// x3m_engine_far_jet (src/proxy/engine_far_jets.cpp) with (node ESI, measure ECX, view EBX). Its FL read set is verified
// from X3FL.exe's bytes (docs/reverse-engineering/fl/cull-chain.md "Verified 2026-11-03", engine_far_jets_layout_fl.h):
// model +0x144, every other field at the AP offset, and the context scale owned at the site rather than read later. A
// JET writer whose bytes differ (base+0x03deb3) drops only the far block (far_jets=writer_mismatch), as on AP.
//
// Install (backend-load path, in the engine_patch install window): X3FL.exe identity (fl::module_base() != 0), the
// 55-byte window at base+0x0961c5 compared before anything is emitted, the projectile marker writers (base+0x049b1d,
// base+0x04a807) compared as on AP (a mismatch drops only the exemption from the stub),
// the stub emitted into the engine_patch arena, then engine_patch::claim of the nine-byte span at base+0x0961d3 (the
// five jump bytes in one aligned qword: lock cmpxchg8b), a read-back of those five bytes, and the stub pushed in front
// of the tail. Any failure after the write puts the original five bytes back (readback_rolled_back, chain_failed), or
// keeps the site registered when even that fails (rollback_failed; shutdown() tries again). No other consumer chains
// on FL (the gain-0 engine cull was deleted on 2026-11-02 and occlusion_engine_cull is not ported: chain_stub/site_chainable refuse, edition_fl).
// Nothing here runs inside the engine's pass but the emitted stub and the far-jet handler it calls. shutdown() is for the fixture: the FL loader's DLL_PROCESS_DETACH unload calls are AP only, and the module is
// pinned before the jump goes live, so on FL the stub stays until process exit.
#include "cull_small_parts_fl.h"
#include "../../proxy/camera_state.h"
#include "../../proxy/config.h"
#include "../../proxy/cull_census.h"
#include "../../proxy/cull_small_parts_core.h"
#include "../../proxy/engine_effects_option.h"
#include "../../proxy/engine_far_jets.h"
#include "../../proxy/engine_patch.h"
#include "../../proxy/fov.h"
#include "cull_small_parts_sites_fl.h"
#include "executable_identity_fl.h"
#include <windows.h>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
namespace core = x3m::cull_small_parts::core;
namespace S = x3m::fl::cull_small_parts_fl;
namespace engine_patch = x3m::engine_patch;
bool patched_ = false;      // the stub is chained and live
bool site_claimed_ = false; // the site holds (or may hold) this module's jump: registered for shutdown()
engine_patch::Site site_{};
std::uintptr_t base_ = 0, stub_ = 0;
const char* state_ = "disabled";
double px_ = 0;
double dock_px_ = 0; // X3M_CULL_DOCK_PARTS_PX; 0 = the dock-port rule off
unsigned width_ = 0;
float last_m00_ = 0;
std::uint32_t last_focus_ = core::focus_default;
core::Source last_source_ = core::Source::registry;
core::Fallback last_fallback_ = core::Fallback::no_scene;
std::int32_t last_threshold_ = 0, last_dock_threshold_ = 0;
unsigned value_lines_ = 0;
constexpr unsigned value_line_cap = 128;
core::SceneLatch scene_{}; // render thread only (the scene Clear and the Present hook), as on AP
bool projectiles_ = true;
const char* projectiles_state_ = "on"; // on, off, marker_mismatch, invalid
bool far_jets_ = false;
const char* far_jets_state_ = "off"; // install-line value: on, off, writer_mismatch
#ifdef X3M_CULL_SMALL_PARTS_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
#endif

bool bytes_match(std::uintptr_t at, const unsigned char* expected, unsigned length) {
    unsigned char actual[S::window_length]{};
    return length <= S::window_length && engine_patch::read_code(at, actual, length) &&
           !std::memcmp(actual, expected, length);
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN): the stub's absolute operands and
// its far-jet call point into it.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE &&
           module != nullptr;
}
bool read_setting(const wchar_t* name, char* out, unsigned capacity) {
    wchar_t text[32]{};
    const DWORD length = x3m::config::get(name, text, 32);
    if (length == 0) return false;
    if (length >= 32 || length + 1 > capacity) {
        out[0] = '?';
        out[1] = 0;
        return true;
    }
    for (DWORD i = 0; i < length; ++i) out[i] = (text[i] >= 0x21 && text[i] <= 0x7e) ? static_cast<char>(text[i]) : '?';
    out[length] = 0;
    return true;
}
// Emits the stub followed by its 4-aligned continuation slot; 0 when the arena is full.
std::uintptr_t emit_stub(std::uintptr_t base, bool exempt_projectiles, bool far_jets, void*** slot_out) {
    engine_patch::Emitter e(S::stub_length + 8);
    if (!e.ok()) return 0;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.here());
    const std::uintptr_t slot = (at + S::stub_length + 3) & ~std::uintptr_t(3);
    unsigned char code[S::stub_length];
    S::encode_stub(std::uint32_t(at), std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_cull_small_parts_threshold)),
                   std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_cull_small_parts_upper)),
                   std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_cull_small_parts_culled)),
                   std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_cull_small_parts_exempt)),
                   std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_cull_small_parts_dock_culled)),
                   std::uint32_t(base + S::cull_rva), std::uint32_t(slot), code, exempt_projectiles,
                   std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_engine_far_jet)), far_jets);
    e.bytes(code, S::stub_length);
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < slot) e.byte(0xcc);
    e.dword(0);
    if (!e.finish()) return 0;
    *slot_out = reinterpret_cast<void**>(slot);
    return at;
}
// The five bytes are back (nullptr: vanilla) or the site stays registered ("rollback_failed").
const char* roll_back(const char* reason) {
    if (engine_patch::restore(site_)) {
        site_claimed_ = false;
        site_ = engine_patch::Site{};
        return reason;
    }
    return "rollback_failed";
}
}

volatile std::int32_t x3m_cull_small_parts_threshold = 0; // declared extern "C" in cull_small_parts.h
volatile std::uint32_t x3m_cull_small_parts_culled = 0;
volatile std::uint32_t x3m_cull_small_parts_exempt = 0;
volatile std::int32_t x3m_cull_small_parts_upper = 0;
volatile std::uint32_t x3m_cull_small_parts_dock_culled = 0;

namespace x3m::cull_small_parts {
bool install_base(std::uintptr_t base, bool exempt_projectiles, bool far_jets) {
    const DWORD error = GetLastError();
    const auto done = [&](const char* reason) {
        state_ = reason;
        SetLastError(error);
        return patched_;
    };
    if (patched_) {
        state_ = "already_installed";
        SetLastError(error);
        return false;
    }
    if (site_claimed_) return done("rollback_failed"); // a failed rollback stays registered: no second claim
    if (!base || (base & 0xffff)) return done("invalid_site");
    if (!engine_patch::install_window_open()) return done("late_claim");
    unsigned char window[S::window_length]{};
    if (!engine_patch::read_code(base + S::window_rva, window, S::window_length)) return done("unreadable");
    if (const char* refused = S::plan(window)) return done(refused);
    void** slot = nullptr;
    const std::uintptr_t stub = emit_stub(base, exempt_projectiles, far_jets, &slot);
    if (!stub) return done("arena_full"); // the site untouched
    if (!pin_self()) return done("pin_failed");
    x3m_cull_small_parts_threshold = 0; // inert until begin_frame publishes a threshold
    x3m_cull_small_parts_upper = 0;
    x3m_cull_small_parts_culled = 0;
    x3m_cull_small_parts_exempt = 0;
    x3m_cull_small_parts_dock_culled = 0;
    engine_patch::SiteSpec spec{};
    spec.name = "cull_small_parts_fl";
    spec.address = base + S::site_rva;
    spec.length = S::site_length;
    spec.ret_pop = S::ret_pop;
    spec.rel32_offset = 0;
    std::memcpy(spec.expected, S::site, S::site_length);
    site_ = engine_patch::Site{};
    if (!engine_patch::claim(site_, spec)) {
        const char* reason = site_.status;
        if (site_.patched_in) {
            site_claimed_ = true; // rollback_failed: registered for shutdown()
            return done("rollback_failed");
        }
        site_ = engine_patch::Site{};
        return done(reason);
    }
    site_claimed_ = true;
    // Read-back: the jump to the dispatcher (which still targets the tail: the engine's own bytes run).
    unsigned char now[5]{};
    if (!engine_patch::read_code(spec.address, now, 5) || std::memcmp(now, site_.patched, 5))
        return done(roll_back("readback_rolled_back"));
    // The stub's continuation is the tail; then the chain head is the stub.
    if (!engine_patch::store_pointer(slot, *site_.entry) || !engine_patch::push_front(site_, reinterpret_cast<void*>(stub)))
        return done(roll_back("chain_failed"));
    base_ = base;
    stub_ = stub;
    projectiles_ = exempt_projectiles;
    far_jets_ = far_jets;
    patched_ = true;
    return done("ok");
}
#ifdef X3M_CULL_SMALL_PARTS_FL_FIXTURE
void fixture_base(std::uintptr_t base) {
    fixture_base_ = base;
}
#endif
bool install_at(std::uintptr_t site, std::uintptr_t cull_target, bool exempt_projectiles, bool far_jets) {
    const std::uintptr_t base = site - S::site_rva;
    if (!site || site < S::site_rva || cull_target != base + S::cull_rva) {
        state_ = "invalid_site";
        return false;
    }
    return install_base(base, exempt_projectiles, far_jets);
}
// No other stub chains on FL: the lens-flare and engine occlusion stubs encode AP's registers.
bool chain_stub(std::uintptr_t, std::uintptr_t, void*, void**, const char** status) {
    *status = "edition_fl";
    return false;
}
bool site_chainable(std::uintptr_t, std::uintptr_t, const char** status) {
    *status = "edition_fl";
    return false;
}
bool site_claimed() {
    return site_claimed_;
}
const char* site_write() {
    return site_.patched_in ? (site_.atomic_write ? "atomic" : "plain") : "none";
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) {
        SetLastError(error);
        return true;
    }
    char setting[32]{};
    if (!read_setting(L"X3M_CULL_SMALL_PARTS_PX", setting, sizeof setting)) {
        state_ = "disabled"; // unset: no row, as on AP
        SetLastError(error);
        return false;
    }
    char projectiles_text[32]{};
    read_setting(L"X3M_CULL_SMALL_PARTS_PROJECTILES", projectiles_text, sizeof projectiles_text);
    bool exempt = true;
    const bool projectiles_ok = core::parse_projectiles(projectiles_text, &exempt);
    projectiles_state_ = !projectiles_ok ? "invalid" : exempt ? "on" : "off";
    char dock_text[32]{};
    const bool dock_set = read_setting(L"X3M_CULL_DOCK_PARTS_PX", dock_text, sizeof dock_text);
    double dock_px = 0;
    const bool dock_ok =
        !dock_set || (core::parse_px(dock_text, &dock_px) && (dock_px == 0.0 || core::valid_px(dock_px)));
    char effects_text[32]{};
    namespace option = x3m::engine_effects::option;
    option::Mode effects = option::Mode::native;
    // The AP rule: X3M_ENGINE_EFFECTS exactly plumes asks for the far block.
    bool far_jets = read_setting(L"X3M_ENGINE_EFFECTS", effects_text, sizeof effects_text) &&
                    option::parse_mode(effects_text, &effects) && effects == option::Mode::plumes;
    far_jets_state_ = far_jets ? "on" : "off";
#ifdef X3M_CULL_SMALL_PARTS_FL_FIXTURE
    const std::uintptr_t base = fixture_base_ ? fixture_base_ : x3m::fl::module_base();
#else
    const std::uintptr_t base = x3m::fl::module_base();
#endif
    double px = 0;
    const bool parsed = core::parse_px(setting, &px);
    bool applied = false;
    if (parsed && px == 0.0)
        state_ = "disabled"; // an explicit 0 is the documented off: nothing patched
    else if (!parsed || !core::valid_px(px))
        state_ = "invalid_px";
    else if (!projectiles_ok)
        state_ = "invalid_projectiles";
    else if (!dock_ok)
        state_ = "invalid_dock_px";
    else if (!base)
        state_ = "executable_mismatch";
    else {
        if (exempt && !(bytes_match(base + S::marker_store_rva, S::marker_store, S::marker_store_length) &&
                        bytes_match(base + S::marker_or_rva, S::marker_or, S::marker_or_length))) {
            exempt = false;
            projectiles_state_ = "marker_mismatch";
        }
        // The far block relies on the engine's JET flag pair: its writer must be the verified bytes, else the block
        // stays out (the rest of the stub installs).
        if (far_jets && !bytes_match(base + S::jet_writer_rva, S::jet_writer, S::jet_writer_length)) {
            far_jets = false;
            far_jets_state_ = "writer_mismatch";
        }
        px_ = px;
        dock_px_ = dock_px;
        applied = install_base(base, exempt, far_jets);
    }
    // state: active (the stub is live), off (explicit 0), refused (nothing written), patched_unverified (a rollback
    // failed: the jump may still be in place, registered).
    const char* status = applied                                 ? "active"
                         : site_claimed_                         ? "patched_unverified"
                         : !std::strcmp(state_, "disabled")      ? "off"
                                                                 : "refused";
    log("cull_small_parts requested=%s px=%.4g patched=%u edition=fl state=%s reason=%s site=0x%08lx rva=0x%06lx cull=0x%08lx write=%s stub=0x%08lx camera=%s scope=%s projectiles=%s dock_px=%.4g dock_requested=%s far_jets=%s",
        setting, applied ? px : 0.0, patched_ ? 1u : 0u, status, state_,
        static_cast<unsigned long>(base ? base + S::site_rva : 0), static_cast<unsigned long>(S::site_rva),
        static_cast<unsigned long>(base ? base + S::cull_rva : 0), site_write(), static_cast<unsigned long>(stub_),
        camera_state::status(), "all", projectiles_state_, applied ? dock_px : 0.0, dock_set ? dock_text : "unset",
        far_jets_state_);
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!site_claimed_) return true;
    const DWORD error = GetLastError();
    x3m_cull_small_parts_upper = 0;
    x3m_cull_small_parts_threshold = 0;
    cull_census::note_small_threshold(0);
    const bool ok = engine_patch::restore(site_);
    patched_ = false;
    if (ok) {
        site_claimed_ = false;
        site_ = engine_patch::Site{};
    }
    stub_ = 0; // the stub stays in the arena (a thread may still be inside it)
    state_ = ok ? "restored" : "restore_failed";
    SetLastError(error);
    return ok;
}
const char* state() {
    return state_;
}
bool projectiles_exempt() {
    return patched_ && projectiles_;
}
bool far_jets() {
    return patched_ && far_jets_;
}
std::uintptr_t stub_address() {
    return patched_ ? stub_ : 0;
}
double requested_px() {
    return patched_ ? px_ : 0.0;
}
bool set_px(double px) {
    if (!core::valid_px(px)) return false;
    px_ = px;
    return true;
}
bool set_dock_px(double px) {
    if (px != 0.0 && !core::valid_px(px)) return false;
    dock_px_ = px;
    return true;
}
double requested_dock_px() {
    return patched_ ? dock_px_ : 0.0;
}
namespace {
std::int32_t apply(float m00, unsigned width, std::uint32_t focus, core::Source source, core::Fallback fallback) {
    if (!patched_) return 0;
    const std::int32_t threshold = core::threshold_for(px_, m00, width, focus);
    const std::int32_t dock = dock_px_ > 0.0 ? core::threshold_for(dock_px_, m00, width, focus) : 0;
    const std::int32_t upper = core::upper_for(threshold, dock);
    x3m_cull_small_parts_threshold = threshold; // the pass runs on this thread after the Present hook
    x3m_cull_small_parts_upper = upper;
    cull_census::note_small_threshold(threshold, projectiles_, upper);
    last_m00_ = m00;
    if (threshold != last_threshold_ || dock != last_dock_threshold_ || width != width_ || focus != last_focus_ ||
        source != last_source_ || fallback != last_fallback_) {
        last_threshold_ = threshold;
        last_dock_threshold_ = dock;
        width_ = width;
        last_focus_ = focus;
        last_source_ = source;
        last_fallback_ = fallback;
        if (value_lines_ < value_line_cap) {
            ++value_lines_;
            log("cull_small_parts_value px=%.4g m00=%.9g width=%u threshold=%ld focus=0x%04lx source=%s fallback=%s dock_px=%.4g dock_threshold=%ld edition=fl",
                px_, static_cast<double>(m00), width, static_cast<long>(threshold), static_cast<unsigned long>(focus),
                core::source_name(source), core::fallback_name(fallback), dock_px_, static_cast<long>(dock));
        }
    }
    return threshold;
}
}
std::int32_t publish(float m00, unsigned width, std::uint32_t focus, bool scene) {
    return apply(m00, width, focus, scene ? core::Source::scene : core::Source::registry, core::Fallback::none);
}
bool wants_scene_projection() {
    return patched_;
}
void note_scene_projection(float m00, float m11) {
    if (patched_) scene_.note(m00, m11);
}
// As on AP: the scene view's P[0]/P[5] latched at its Clear (motion_output read_camera), else the live projection
// through the FL camera reader with the registry focus. On FL fov::current_focus() returns its configured value, the
// engine default 0x4000 (fov.cpp is linked but its initialize() is AP only, so it never reads an engine address).
void begin_frame() {
    if (!patched_) return;
    const DWORD error = GetLastError();
    x3m_cull_small_parts_culled = 0;
    x3m_cull_small_parts_exempt = 0;
    x3m_cull_small_parts_dock_culled = 0;
    core::Choice choice{0.0f, core::focus_default, core::Source::registry, scene_.fallback()};
    camera_state::Sample sample{};
    if (camera_state::available() && camera_state::read(&sample) && sample.state.valid)
        choice = core::choose(scene_, sample.state.m00, sample.state.m11, scene_.usable() ? 0u : fov::current_focus());
    scene_.advance();
    apply(choice.m00, width_, choice.focus, choice.source, choice.fallback);
    SetLastError(error);
}
void set_backbuffer_width(unsigned width) {
    width_ = width;
}
void after_reset(unsigned width) {
    width_ = width;
    scene_.clear();
    x3m_cull_small_parts_upper = 0;
    x3m_cull_small_parts_threshold = 0;
    if (patched_) cull_census::note_small_threshold(0);
}
void present(unsigned long long device, unsigned long long frame, bool captured) {
    if (!patched_) return;
    if (captured) {
        const DWORD error = GetLastError();
        const std::int32_t threshold = x3m_cull_small_parts_threshold, upper = x3m_cull_small_parts_upper;
        log("cull_small_parts_frame device=%llu frame=%llu px=%.4g threshold=%ld culled=%lu m00=%.9g width=%u scope=%s projectiles=%s exempt_bullet=%lu focus=0x%04lx source=%s fallback=%s dock_px=%.4g dock_threshold=%ld dock_culled=%lu far_jets=%s edition=fl",
            device, frame, px_, static_cast<long>(threshold), static_cast<unsigned long>(x3m_cull_small_parts_culled),
            static_cast<double>(last_m00_), width_, "all", projectiles_ ? "on" : "off",
            static_cast<unsigned long>(x3m_cull_small_parts_exempt), static_cast<unsigned long>(last_focus_),
            core::source_name(last_source_), core::fallback_name(last_fallback_), dock_px_,
            static_cast<long>(upper > threshold ? upper : 0),
            static_cast<unsigned long>(x3m_cull_small_parts_dock_culled), far_jets_ ? "on" : "off");
        SetLastError(error);
    }
    x3m_cull_small_parts_culled = 0;
    x3m_cull_small_parts_exempt = 0;
    x3m_cull_small_parts_dock_culled = 0;
}
Stats stats() {
    Stats s{};
    s.threshold = x3m_cull_small_parts_threshold;
    s.upper = x3m_cull_small_parts_upper;
    s.dock_threshold = s.upper > s.threshold ? s.upper : 0;
    s.culled = x3m_cull_small_parts_culled;
    s.exempt = x3m_cull_small_parts_exempt;
    s.dock_culled = x3m_cull_small_parts_dock_culled;
    s.m00 = last_m00_;
    s.width = width_;
    s.focus = last_focus_;
    s.scene = last_source_ == core::Source::scene;
    s.fallback = unsigned(last_fallback_);
    return s;
}
}
