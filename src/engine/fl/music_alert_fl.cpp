// FL edition of x3m::music_alert (src/proxy/music_alert.h; the ap target keeps src/proxy/music_alert.cpp): the combat
// trigger narrowed to engagement on X3FL.exe (docs/reverse-engineering/fl/music-alert.md, "Port (2026-11-03)"; site
// and bindings: music_alert_sites_fl.h). The policy is AP's music_alert_core.h (core::frame, State, the walk, the
// relation, the hold and its parsers), unchanged.
//
//   base+0x02fada  `mov ecx,[ebp+8]; mov eax,[ebp+4]` claimed (6 bytes, one lock cmpxchg8b) with the integer stub in
//                  front: EFLAGS, DF and EAX/ECX/EDX saved, handler(registry = EBP, player = EBX), restored, the tail
//                  replays the two moves (reading the filtered [R+8]) and resumes at base+0x02fae0.
//
// Install (backend-load path, inside the engine_patch install window): X3FL.exe identity, every alert window of
// music_windows_fl_inc.h and the main loop's call before the one claim; read back; a failure restores the bytes
// (rollback_failed keeps the site registered and the handler inert). The FL loader has no unload path: no shutdown
// wiring. FL has no save_load_complete marker: the counters re-baseline on a changed player pointer or a clock that
// went backwards (the shared rule), loaded() is kept for the interface.
#include "../../proxy/music_alert.h"
#include "../../proxy/capture.h"
#include "../../proxy/config.h"
#include "../../proxy/cpu_state.h"
#include "../../proxy/engine_memory.h"
#include "../../proxy/engine_patch.h"
#include "executable_identity_fl.h"
#include "music_alert_sites_fl.h"
#include <windows.h>
#include <cstring>

static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
namespace core = x3m::music_alert::core;
namespace engine_patch = x3m::engine_patch;
namespace F = x3m::fl::music_alert_fl;
namespace W = x3m::fl::music_windows;
bool patched_ = false, armed_ = false;
const char* state_ = "disabled";
const char* hold_source_ = "default";
const char* window_ = "-";
engine_patch::Site site_{};
std::uint32_t hold_ms_ = core::default_hold_ms;
core::State rule_;
unsigned long lines_ = 0, suppressed_lines_ = 0;
bool cap_noted_ = false;
volatile long load_pending_ = 0;
std::uintptr_t base_ = 0;
constexpr unsigned long line_cap = 2000;
#ifdef X3M_MUSIC_ALERT_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
#endif

std::uint32_t load32(std::uintptr_t at) {
    std::uint32_t v = 0;
    std::memcpy(&v, reinterpret_cast<const void*>(at), 4);
    return v;
}
// The shared policy's memory, with the FL image bindings: the two image loads core::frame makes (AP cfg_va and
// player_root_va) answer FL's cfg pointer minus cfg_bias and FL's M; every other read is a validated engine read.
struct EngineMemory {
    bool read(std::uint32_t at, void* out, unsigned n) { return x3m::engine_memory::read(at, out, n); }
    std::uint32_t image32(std::uint32_t at) {
        if (at == core::cfg_va) {
            const std::uint32_t cfg = load32(base_ + F::cfg_slot_rva);
            return cfg ? cfg - F::cfg_bias : 0u;
        }
        if (at == core::player_root_va) return load32(base_ + F::root_slot_rva);
        return 0;
    }
    void write32(std::uint32_t at, std::uint32_t v) { std::memcpy(reinterpret_cast<void*>(std::uintptr_t(at)), &v, 4); }
};
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE &&
           module != nullptr;
}
const char* write_kind(bool in, bool atomic) {
    return in ? (atomic ? "atomic" : "plain") : "none";
}
template <class T> std::uint32_t address_of(T* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
bool row_allowed() {
    if (lines_ < line_cap) {
        ++lines_;
        return true;
    }
    ++suppressed_lines_;
    if (!cap_noted_) {
        cap_noted_ = true;
        x3m::log("music_alert cap lines=%lu note=further_music_alert_rows_suppressed edition=fl", lines_);
    }
    return false;
}
}

extern "C" {
volatile std::uint32_t x3m_music_alert_fl_continue = 0; // the claim tail: the two displaced moves + jmp base+0x02fae0

// Once per main-loop iteration at base+0x02fada on the main thread: AP's handler body on the FL bindings. The only
// engine write is [R+8] = 0. No engine call, no script, no allocation.
__attribute__((force_align_arg_pointer)) void __cdecl x3m_music_alert_fl_frame(std::uint32_t registry,
                                                                               std::uint32_t player) {
    if (!armed_) return;
    x3m::LightCallBoundary cpu;
    EngineMemory memory;
    if (load_pending_ && InterlockedExchange(&load_pending_, 0)) rule_.rebaseline = true;
    const core::Frame f = core::frame(memory, rule_, registry, player, hold_ms_);
    if (!f.ok) return;
    if (f.alert_end && row_allowed())
        x3m::log("music_alert episode=end frames=%u reads_max=%u reads_mean=%lu visited_max=%u visited_mean=%lu cuts=%u now=%lu edition=fl",
                 f.alert.frames, f.alert.reads_max,
                 static_cast<unsigned long>(f.alert.frames ? f.alert.reads / f.alert.frames : 0), f.alert.visited_max,
                 static_cast<unsigned long>(f.alert.frames ? f.alert.visited / f.alert.frames : 0), f.alert.cuts,
                 static_cast<unsigned long>(f.now));
    if (!f.edge && !f.suppressed_start && !f.suppressed_end) return;
    char reason[core::reason_text_size];
    core::reason_text(f.reasons ? f.reasons : rule_.last_reason, reason);
    if (f.suppressed_end && row_allowed())
        x3m::log("music_alert suppressed=end frames=%u vanilla=%lu to=%lu now=%lu reads=%u visited=%u edition=fl",
                 f.episode_frames, static_cast<unsigned long>(f.vanilla), static_cast<unsigned long>(f.filtered),
                 static_cast<unsigned long>(f.now), f.reads, f.walk.visited);
    if (f.edge && row_allowed())
        x3m::log(
            "music_alert edge from=%lu to=%lu vanilla=%lu committed=%lu reason=%s age_ms=%lu cause=%s now=%lu step=%lu seta=%lu visited=%u owned_near=%u hostile_orders=%u cuts=%u walk=%s fire=%s reads=%u edition=fl",
            static_cast<unsigned long>(f.from), static_cast<unsigned long>(f.filtered),
            static_cast<unsigned long>(f.vanilla), static_cast<unsigned long>(f.committed), reason,
            static_cast<unsigned long>(f.age), core::cause_name(f.cause), static_cast<unsigned long>(f.now),
            static_cast<unsigned long>(f.step), static_cast<unsigned long>(f.seta), f.walk.visited, f.walk.owned_near,
            f.walk.hostile_orders, f.walk.cuts, core::walk_status_name(f.walk.status),
            core::fire_status_name(rule_.last_fire), f.reads);
    if (f.suppressed_start && row_allowed())
        x3m::log(
            "music_alert suppressed=start vanilla=%lu committed=%lu reason=%s age_ms=%lu cause=%s now=%lu step=%lu seta=%lu visited=%u owned_near=%u hostile_orders=%u cuts=%u walk=%s fire=%s reads=%u edition=fl",
            static_cast<unsigned long>(f.vanilla), static_cast<unsigned long>(f.committed), reason,
            static_cast<unsigned long>(f.age), core::cause_name(f.cause), static_cast<unsigned long>(f.now),
            static_cast<unsigned long>(f.step), static_cast<unsigned long>(f.seta), f.walk.visited, f.walk.owned_near,
            f.walk.hostile_orders, f.walk.cuts, core::walk_status_name(f.walk.status),
            core::fire_status_name(rule_.last_fire), f.reads);
}
}

// Integer-only stub, entered by the claim's dispatcher at base+0x02fada with EBP = R and EBX = P. It saves EFLAGS and
// EAX/ECX/EDX, clears DF for the handler's `rep movsb` reads (popfd restores the engine's DF), calls the handler (cdecl,
// four-byte stack: EBX then EBP pushed, so the arguments are (registry, player)), restores them and leaves through the
// claim tail. ESP is exactly as at entry; EBX/EBP/ESI/EDI are untouched (the handler is cdecl).
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_music_alert_fl_stub
_x3m_music_alert_fl_stub:
    pushfd
    cld
    push eax
    push ecx
    push edx
    push ebx
    push ebp
    call _x3m_music_alert_fl_frame
    add esp, 8
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_alert_fl_continue]
    .att_syntax
)");
extern "C" void x3m_music_alert_fl_stub();

namespace {
bool read_code(std::uintptr_t at, unsigned char* out, unsigned n) {
    return engine_patch::read_code(at, out, n);
}
bool install(std::uintptr_t base, const char** reason) {
    if (!engine_patch::install_window_open()) {
        *reason = "late_claim";
        return false;
    }
    if (const char* bad = W::first_mismatch(read_code, base, W::alert_windows, W::alert_fixups)) {
        window_ = bad;
        *reason = "bytes_mismatch";
        return false;
    }
    unsigned char code[5]{};
    if (!read_code(base + F::main_loop_call_rva, code, 5) ||
        !W::calls(code, std::uint32_t(base + F::main_loop_call_rva), std::uint32_t(base + F::registry_update_rva))) {
        *reason = "callers_mismatch";
        return false;
    }
    if (!pin_self()) {
        *reason = "pin_failed";
        return false;
    }
    rule_ = core::State{};
    engine_patch::SiteSpec spec{};
    spec.name = "music_alert_fl";
    spec.address = base + F::site_rva;
    spec.length = F::site_length;
    std::memcpy(spec.expected, F::site_bytes, F::site_length);
    site_ = engine_patch::Site{};
    if (!engine_patch::claim(site_, spec)) {
        *reason = site_.status;
        if (site_.patched_in) patched_ = true;
        return false;
    }
    x3m_music_alert_fl_continue = address_of(*site_.entry);
    unsigned char now[5]{};
    const char* failure = nullptr;
    if (!engine_patch::push_front(site_, reinterpret_cast<void*>(&x3m_music_alert_fl_stub)))
        failure = "chain_failed";
    else if (!read_code(spec.address, now, 5) || std::memcmp(now, site_.patched, 5))
        failure = "readback_failed";
    if (failure) {
        engine_patch::restore(site_);
        if (site_.patched_in) patched_ = true;
        *reason = site_.patched_in ? "rollback_failed" : failure;
        return false;
    }
    patched_ = true;
    armed_ = true;
    *reason = "ok";
    return true;
}
}

namespace x3m::music_alert {
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) {
        SetLastError(error);
        return armed_;
    }
    wchar_t text[16]{};
    DWORD length = x3m::config::get(L"X3M_MUSIC_COMBAT_TRIGGER", text, 16);
    if (length >= 16) length = 15;
    const core::Trigger trigger = core::parse_trigger(text, length);
    wchar_t hold_text[16]{};
    const DWORD hold_length = x3m::config::get(L"X3M_MUSIC_COMBAT_HOLD_S", hold_text, 16);
    hold_ms_ = core::default_hold_ms;
    if (!hold_length)
        hold_source_ = "default";
    else if (core::parse_hold_s(hold_text, hold_length < 16 ? hold_length : 15, hold_ms_))
        hold_source_ = "setting";
    else
        hold_source_ = "invalid_default";
    lines_ = suppressed_lines_ = 0;
    cap_noted_ = false;
    window_ = "-";
#ifdef X3M_MUSIC_ALERT_FL_FIXTURE
    base_ = fixture_base_ ? fixture_base_ : x3m::fl::module_base();
#else
    base_ = x3m::fl::module_base();
#endif
    bool applied = false;
    if (trigger == core::Trigger::vanilla)
        state_ = "vanilla";
    else if (trigger == core::Trigger::invalid)
        state_ = "invalid_setting";
    else if (!base_)
        state_ = "executable_mismatch";
    else
        applied = install(base_, &state_);
    log("music_alert trigger=%s hold_ms=%lu hold_source=%s patched=%u armed=%u reason=%s site=0x%08lx rva=0x%06lx continue=0x%08lx write=%s stub=0x%08lx windows=%u walk_limit=%u node_budget=%u window=%s edition=fl",
        trigger == core::Trigger::vanilla ? "vanilla" : trigger == core::Trigger::engaged ? "engaged" : "invalid",
        static_cast<unsigned long>(hold_ms_), hold_source_, patched_ ? 1u : 0u, armed_ ? 1u : 0u, state_,
        static_cast<unsigned long>(base_ ? base_ + F::site_rva : 0), static_cast<unsigned long>(F::site_rva),
        static_cast<unsigned long>(x3m_music_alert_fl_continue), write_kind(site_.patched_in, site_.atomic_write),
        static_cast<unsigned long>(address_of(&x3m_music_alert_fl_stub)),
        unsigned(sizeof W::alert_windows / sizeof W::alert_windows[0]) + 1, core::list_walk_limit,
        core::walk_node_budget, window_);
    SetLastError(error);
    return applied;
}
// Fixture only on FL (the FL loader's unload calls are AP only): restores the site over our jump.
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    armed_ = false;
    const bool back = engine_patch::restore(site_);
    patched_ = site_.patched_in;
    state_ = back ? "restored" : "restore_failed";
    SetLastError(error);
    return back;
}
void loaded() {
    if (armed_) InterlockedExchange(&load_pending_, 1);
}
const char* state() {
    return state_;
}
#ifdef X3M_MUSIC_ALERT_FL_FIXTURE
void fixture_base(std::uintptr_t base) {
    fixture_base_ = base;
}
const char* fixture_window() {
    return window_;
}
const engine_patch::Site* fixture_site() {
    return &site_;
}
#endif
}
