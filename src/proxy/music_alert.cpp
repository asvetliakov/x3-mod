#include "music_alert.h"
#include "config.h"
#include "engine_patch.h"
#include "engine_memory.h"
#include "object_trace.h"
#include "cpu_state.h"
#include "capture.h"
#include <windows.h>
#include <cstring>

static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
namespace core = x3m::music_alert::core;
namespace engine_patch = x3m::engine_patch;

// patched_: the site is registered for shutdown() (also after a failed rollback); armed_: a complete install, the
// only state in which the handler acts. A stub left chained by an unrestorable site sees armed_ false and replays the
// displaced instructions unchanged.
bool patched_ = false, armed_ = false;
const char* state_ = "disabled";
const char* hold_source_ = "default";
engine_patch::Site site_{};
std::uint32_t hold_ms_ = core::default_hold_ms;
core::State rule_;
unsigned long lines_ = 0, suppressed_lines_ = 0;
bool cap_noted_ = false;
volatile long load_pending_ = 0; // set by loaded() on the Present thread, consumed by the handler (InterlockedExchange)
constexpr unsigned long line_cap = 2000;

// Bounded, validated reads of engine memory (engine_memory.h: VirtualQuery-checked regions, rep movsb copy); the one
// write goes to [R+8], a word the handler has just read through the same validation.
struct EngineMemory {
    bool read(std::uint32_t at, void* out, unsigned n) { return x3m::engine_memory::read(at, out, n); }
    void write32(std::uint32_t at, std::uint32_t v) { std::memcpy(reinterpret_cast<void*>(std::uintptr_t(at)), &v, 4); }
};
bool bytes_match(std::uint32_t at, const unsigned char* expected, unsigned length) {
    return engine_patch::verify_bytes(at, expected, length);
}
bool call_targets(std::uint32_t at, std::uint32_t target) {
    unsigned char code[5]{};
    if (!engine_patch::read_code(at, code, 5) || code[0] != 0xe8) return false;
    std::uint32_t rel = 0;
    std::memcpy(&rel, code + 1, 4);
    return at + 5 + rel == target;
}
bool windows_match() {
    for (const core::Window& w : core::windows)
        if (!bytes_match(w.va, w.bytes, w.length)) return false;
    // The main loop's call at 0x00403f2f is not compared here: the game-phase group may hold that call site in the
    // same install window (music_alert_core.h, the note at walk_caller); the verifier checks it on the unmodified image.
    return bytes_match(core::site_va, core::site_bytes, core::site_length) &&
           call_targets(core::walk_caller_va, core::alert_va);
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN): the engine jumps into the stub.
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
// Event rows through the session log (formatted behind call_preserved inside x3m::log; no file I/O on the game
// thread). Capped per session, then one notice.
bool row_allowed() {
    if (lines_ < line_cap) {
        ++lines_;
        return true;
    }
    ++suppressed_lines_;
    if (!cap_noted_) {
        cap_noted_ = true;
        x3m::log("music_alert cap lines=%lu note=further_music_alert_rows_suppressed", lines_);
    }
    return false;
}
}

extern "C" {
volatile std::uint32_t x3m_music_alert_continue = 0; // the claim tail: the two displaced moves + jmp 0x0042c97a

// Once per frame at 0x0042c974 on the main thread. Everything below is integer work on validated reads; the only
// engine write is [R+8] = 0. No engine call, no script, no allocation.
__attribute__((force_align_arg_pointer)) void __cdecl x3m_music_alert_frame(std::uint32_t registry,
                                                                            std::uint32_t player) {
    if (!armed_) return;
    x3m::LightCallBoundary cpu; // MXCSR + LastError; no x87 below
    EngineMemory memory;
    if (load_pending_ && InterlockedExchange(&load_pending_, 0)) rule_.rebaseline = true;
    const core::Frame f = core::frame(memory, rule_, registry, player, hold_ms_);
    if (!f.ok) return;
    if (!f.edge && !f.suppressed_start && !f.suppressed_end) return;
    char reason[core::reason_text_size];
    core::reason_text(f.reasons ? f.reasons : rule_.last_reason, reason);
    if (f.suppressed_end && row_allowed())
        x3m::log("music_alert suppressed=end frames=%u vanilla=%lu to=%lu now=%lu", f.episode_frames,
                 static_cast<unsigned long>(f.vanilla), static_cast<unsigned long>(f.filtered),
                 static_cast<unsigned long>(f.now));
    if (f.edge && row_allowed())
        x3m::log(
            "music_alert edge from=%lu to=%lu vanilla=%lu committed=%lu reason=%s age_ms=%lu cause=%s now=%lu step=%lu seta=%lu visited=%u owned_near=%u hostile_orders=%u cuts=%u",
            static_cast<unsigned long>(f.from), static_cast<unsigned long>(f.filtered),
            static_cast<unsigned long>(f.vanilla), static_cast<unsigned long>(f.committed), reason,
            static_cast<unsigned long>(f.age), core::cause_name(f.cause), static_cast<unsigned long>(f.now),
            static_cast<unsigned long>(f.step), static_cast<unsigned long>(f.seta), f.walk.visited, f.walk.owned_near,
            f.walk.hostile_orders, f.walk.cuts);
    if (f.suppressed_start && row_allowed())
        x3m::log(
            "music_alert suppressed=start vanilla=%lu committed=%lu reason=%s age_ms=%lu cause=%s now=%lu step=%lu seta=%lu visited=%u owned_near=%u hostile_orders=%u cuts=%u",
            static_cast<unsigned long>(f.vanilla), static_cast<unsigned long>(f.committed), reason,
            static_cast<unsigned long>(f.age), core::cause_name(f.cause), static_cast<unsigned long>(f.now),
            static_cast<unsigned long>(f.step), static_cast<unsigned long>(f.seta), f.walk.visited, f.walk.owned_near,
            f.walk.hostile_orders, f.walk.cuts);
}
}

// Integer-only stub, entered by the claim's dispatcher at 0x0042c974 with ESI = R and EDI = P. It saves EFLAGS and
// EAX/ECX/EDX (dead at the site, preserved anyway), clears DF for the handler's `rep movsb` reads (engine_memory;
// popfd restores the engine's DF), calls the handler (cdecl, four-byte stack: EDI then ESI pushed,
// so the arguments are (registry, player)), restores them and leaves through the claim tail, which replays
// `mov ecx,[esi+8]; mov eax,[esi+4]` (reading the filtered [R+8]) and jumps to 0x0042c97a. ESP is exactly as at entry;
// EBX/EBP/ESI/EDI are untouched (the handler is cdecl).
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_music_alert_stub
_x3m_music_alert_stub:
    pushfd
    cld
    push eax
    push ecx
    push edx
    push edi
    push esi
    call _x3m_music_alert_frame
    add esp, 8
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_alert_continue]
    .att_syntax
)");
extern "C" void x3m_music_alert_stub();

namespace {
engine_patch::SiteSpec site_spec() {
    engine_patch::SiteSpec spec{};
    spec.name = "music_alert";
    spec.address = core::site_va;
    spec.length = core::site_length;
    spec.ret_pop = 0;
    spec.rel32_offset = 0;
    std::memcpy(spec.expected, core::site_bytes, core::site_length);
    return spec;
}
// Window checks, pin, one claim, one chain; a failure after the claim restores the bytes. A live site that cannot be
// restored keeps the module registered (patched_) so shutdown() tries again, and armed_ stays false.
bool install(const char** reason) {
    if (!engine_patch::install_window_open()) {
        *reason = "late_claim";
        return false;
    }
    if (!windows_match()) {
        *reason = "bytes_mismatch";
        return false;
    }
    if (!pin_self()) {
        *reason = "pin_failed";
        return false;
    }
    rule_ = core::State{};
    site_ = engine_patch::Site{};
    if (!engine_patch::claim(site_, site_spec())) {
        *reason = site_.status;
        if (site_.patched_in) patched_ = true; // rollback_failed: shutdown() retries the restore
        return false;
    }
    x3m_music_alert_continue = address_of(*site_.entry);
    if (!engine_patch::push_front(site_, reinterpret_cast<void*>(&x3m_music_alert_stub))) {
        *reason = engine_patch::restore(site_) ? "chain_failed" : "rollback_failed";
        if (site_.patched_in) patched_ = true;
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
        return true;
    }
    wchar_t text[16]{};
    DWORD length = x3m::config::get(L"X3M_MUSIC_COMBAT_TRIGGER", text, 16);
    if (length >= 16) length = 15; // longer than any accepted word: invalid below
    const core::Trigger trigger = core::parse_trigger(text, length);
    wchar_t hold_text[16]{};
    const DWORD hold_length = x3m::config::get(L"X3M_MUSIC_COMBAT_HOLD_S", hold_text, 16);
    if (!hold_length)
        hold_source_ = "default";
    else if (core::parse_hold_s(hold_text, hold_length < 16 ? hold_length : 15, hold_ms_))
        hold_source_ = "setting";
    else
        hold_source_ = "invalid_default"; // out of 1..600 or not a number: the default stands
    lines_ = suppressed_lines_ = 0;
    cap_noted_ = false;
    bool applied = false;
    if (trigger == core::Trigger::vanilla)
        state_ = "vanilla";
    else if (trigger == core::Trigger::invalid)
        state_ = "invalid_setting";
    else if (!object_trace::executable_verified())
        state_ = "executable_mismatch";
    else
        applied = install(&state_);
    log("music_alert trigger=%s hold_ms=%lu hold_source=%s patched=%u armed=%u reason=%s site=0x%08lx continue=0x%08lx write=%s stub=0x%08lx windows=%u walk_limit=%u",
        trigger == core::Trigger::vanilla ? "vanilla" : trigger == core::Trigger::engaged ? "engaged" : "invalid",
        static_cast<unsigned long>(hold_ms_), hold_source_, patched_ ? 1u : 0u, armed_ ? 1u : 0u, state_,
        static_cast<unsigned long>(core::site_va), static_cast<unsigned long>(x3m_music_alert_continue),
        write_kind(site_.patched_in, site_.atomic_write), static_cast<unsigned long>(address_of(&x3m_music_alert_stub)),
        core::window_count + 1, core::list_walk_limit);
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    armed_ = false;
    const bool back = engine_patch::restore(site_);
    patched_ = false; // the stub stays in the DLL (pinned); the claim tail stays in the arena
    state_ = back ? "restored" : "restore_failed";
    SetLastError(error);
    return back;
}
// The save_load_complete marker (capture.cpp Present path, the event fov::loaded takes): the next frame's counters
// are a baseline, never a shot. The handler also re-baselines by itself on a changed player ship pointer or a clock
// that went backwards, which covers loads after the once-per-process marker.
void loaded() {
    if (armed_) InterlockedExchange(&load_pending_, 1);
}
const char* state() {
    return state_;
}
}
