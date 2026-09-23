#include "music_keep.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "cpu_state.h"
#include "capture.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
using namespace x3m::music_keep::core;
namespace core = x3m::music_keep::core;
namespace engine_patch = x3m::engine_patch;

// patched_: sites registered for shutdown() (also after a failed rollback);
// armed_: a complete install, the only state in which the handlers act. A
// stub left chained by a partial install or an unrestorable site sees
// armed_ false and behaves as vanilla.
bool keep_patched_ = false, keep_armed_ = false, trace_patched_ = false, trace_armed_ = false;
const char* keep_state_ = "disabled";
const char* trace_state_ = "disabled";
// Sites the keep owns alone (A, C) and the trace owns alone (play entry);
// the stop-all entry and the MOV_StopMovie entry are shared: claimed once,
// each feature chains its own stub in front (users counts the chained
// features; the claim is restored when the last one releases it).
struct SharedSite { engine_patch::Site site{}; unsigned users = 0; };
engine_patch::Site a_site_{}, t_play_site_{};
engine_patch::CallSite c_site_{};
SharedSite entry_site_{}, stop_movie_site_{};
Holds holds_;
volatile unsigned long long current_frame_ = 0;   // stored at Present, read by the handlers
unsigned long lines_ = 0, seq_ = 0, suppressed_ = 0, walks_cut_ = 0;
bool cap_noted_ = false;

std::uint32_t load32(std::uint32_t at) { std::uint32_t v = 0; std::memcpy(&v, reinterpret_cast<const void*>(std::uintptr_t(at)), 4); return v; }
std::uint64_t qpc() { LARGE_INTEGER v{}; return QueryPerformanceCounter(&v) && v.QuadPart > 0 ? std::uint64_t(v.QuadPart) : 0; }
bool bytes_match(std::uintptr_t at, const unsigned char* expected, unsigned length) {
    unsigned char actual[128]{};
    return length <= sizeof actual && engine_patch::read_code(at, actual, length) && !std::memcmp(actual, expected, length);
}
bool call_targets(std::uintptr_t at, std::uintptr_t target) {
    unsigned char code[5]{};
    if (!engine_patch::read_code(at, code, 5) || code[0] != 0xe8) return false;
    std::uint32_t rel = 0; std::memcpy(&rel, code + 1, 4);
    return at + 5 + rel == target;
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN): the engine jumps into the stubs.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&keep_patched_), &module) != FALSE && module != nullptr;
}
const char* write_kind(bool in, bool atomic) { return in ? (atomic ? "atomic" : "plain") : "none"; }
engine_patch::SiteSpec spec_for(const char* name, std::uintptr_t address, const unsigned char* bytes, unsigned length) {
    engine_patch::SiteSpec spec{};
    spec.name = name; spec.address = address; spec.length = length; spec.ret_pop = 0; spec.rel32_offset = 0;
    std::memcpy(spec.expected, bytes, length);
    return spec;
}
void write_line(const char* format, ...);
// Bounded walk of the engine's record list; a walk that reaches the limit is logged (music_walk_cut) and counts.
template<class Visit>
void walk_records(const char* what, Visit&& visit) {
    const std::uint32_t head = load32(list_head_va);
    if (!head) return;
    std::uint32_t node = load32(head);
    unsigned i = 0;
    for (; i < list_walk_limit && node; ++i) {
        if (load32(node) == 0) return;   // the sentinel
        if (visit(node)) return;
        node = load32(node);
    }
    if (i == list_walk_limit) {
        ++walks_cut_;
        write_line("music_walk_cut frame=%llu seq=%lu qpc=%llu walk=%s limit=%u cuts=%lu\n", current_frame_, ++seq_, qpc(), what, list_walk_limit, walks_cut_);
    }
}
// The first record with `id`; 0 when absent.
std::uint32_t find_record(std::uint32_t id) {
    std::uint32_t found = 0;
    walk_records("find_record", [&](std::uint32_t node) { if (load32(node + record_id) == id) { found = node; return true; } return false; });
    return found;
}
// Whether `record` is still linked with the same id and media object; fills its flags and completion context.
bool live_record(std::uint32_t record, std::uint32_t id, std::uint32_t media, LiveRecord& out) {
    bool live = false;
    walk_records("live_record", [&](std::uint32_t node) {
        if (node != record) return false;
        live = load32(node + record_id) == id && load32(node + record_media) == media;
        if (live) { out.flags = load32(node + record_flags); out.context = load32(node + record_context); }
        return true;
    });
    return live;
}
// One event line, written synchronously through the session log's OS handle
// (no stdio buffer, no log mutex; it may precede buffered lines written
// earlier: grep for the prefix, not for order). The CRT formatter is x87
// code, so it runs under call_preserved's FNSAVE/FRSTOR envelope,
// indirectly, keeping the handlers' audited graph x87-free. Capped at
// trace_line_cap event lines per session, then one notice.
void write_line(const char* format, ...) {
    bool notice = false;
    if (lines_ >= trace_line_cap) { ++suppressed_; if (cap_noted_) return; cap_noted_ = true; notice = true; }
    else ++lines_;
    const HANDLE handle = x3m::log_handle();
    if (handle == INVALID_HANDLE_VALUE || !handle) return;
    va_list args;
    va_start(args, format);
    x3m::call_preserved([&] {
        char line[256];
        const int n = notice ? std::snprintf(line, sizeof line, "music_trace_cap frame=%llu lines=%lu note=further_music_lines_suppressed\n", current_frame_, lines_)
                             : std::vsnprintf(line, sizeof line, format, args);
        if (n > 0 && unsigned(n) < sizeof line) { DWORD written = 0; WriteFile(handle, line, DWORD(n), &written, nullptr); }
    });
    va_end(args);
}
const char* action_name(SeekAction a) { return a == SeekAction::skip ? "skip" : a == SeekAction::pause_then_vanilla ? "pause_then_vanilla" : "vanilla"; }
}

extern "C" {
// Pointer words the stubs jump or call through: set before a site goes live.
volatile std::uint32_t x3m_music_stop_all_continue = 0;        // Patch A claim tail (the displaced instructions + jmp 0x004982e1)
volatile std::uint32_t x3m_music_stop_all_skip = 0;            // 0x00498322
volatile std::uint32_t x3m_music_seek_original = 0;            // 0x004d0430
volatile std::uint32_t x3m_music_pause_fn = 0;                 // 0x004d1810
volatile std::uint32_t x3m_music_keep_entry_continue = 0, x3m_music_keep_stop_movie_continue = 0;
volatile std::uint32_t x3m_music_trace_stop_continue = 0, x3m_music_trace_play_continue = 0, x3m_music_trace_stop_movie_continue = 0;
std::uint32_t x3m_music_keep_pause_record = 0;

__attribute__((force_align_arg_pointer)) unsigned __cdecl x3m_music_keep_stop_all(std::uint32_t record, std::uint32_t return_va) {
    x3m::LightCallBoundary cpu;   // MXCSR + LastError; no x87 below
    if (!keep_armed_) return unsigned(StopMode::vanilla);
    const std::uint32_t flags = load32(record + record_flags), id = load32(record + record_id), media = load32(record + record_media);
    const StopDecision d = decide_stop(holds_, record, flags, id, media, return_va);
    if (flags & flag_music)
        write_line("music_keep_stop frame=%llu seq=%lu qpc=%llu caller=0x%08lx name=%s record=0x%08lx id=%lu flags=0x%lx mode=%s held=%u holds=%u\n",
                   current_frame_, ++seq_, qpc(), static_cast<unsigned long>(return_va), stop_caller_name(return_va), static_cast<unsigned long>(record),
                   static_cast<unsigned long>(id), static_cast<unsigned long>(flags), mode_name(d.mode), d.held ? 1u : 0u, holds_.count);
    return unsigned(d.mode);
}
__attribute__((force_align_arg_pointer)) unsigned __cdecl x3m_music_keep_seek(std::uint32_t record, std::int32_t start_ms) {
    x3m::LightCallBoundary cpu;
    if (!keep_armed_) return unsigned(SeekAction::vanilla);
    const std::uint32_t flags = load32(record + record_flags);
    if (!(flags & flag_music)) return unsigned(SeekAction::vanilla);   // the common path: speech, video and every other record class
    const std::uint32_t id = load32(record + record_id), media = load32(record + record_media);
    const unsigned holds_before = holds_.count;
    const SeekDecision d = decide_seek(holds_, record, flags, id, media, start_ms, live_record);
    x3m_music_keep_pause_record = d.pause_record;
    write_line("music_keep_seek frame=%llu seq=%lu qpc=%llu record=0x%08lx id=%lu start_ms=%ld flags=0x%lx action=%s hold_id=%lu pause_record=0x%08lx holds_before=%u\n",
               current_frame_, ++seq_, qpc(), static_cast<unsigned long>(record), static_cast<unsigned long>(id), static_cast<long>(start_ms), static_cast<unsigned long>(flags),
               action_name(d.action), static_cast<unsigned long>(d.hold_id), static_cast<unsigned long>(d.pause_record), holds_before);
    return unsigned(d.action);
}
// Stop-all entry (any caller): the next orphaned keep-running track to pause, 0 when none (the stub loops).
__attribute__((force_align_arg_pointer)) std::uint32_t __cdecl x3m_music_keep_stop_all_entry(const std::uint32_t* block) {
    x3m::LightCallBoundary cpu;
    if (!keep_armed_ || !holds_.count) return 0;
    const std::uint32_t return_va = block[4];
    const std::uint32_t record = stale_hold_to_pause(holds_, live_record);
    if (record)
        write_line("music_keep_orphan frame=%llu seq=%lu qpc=%llu caller=0x%08lx name=%s record=0x%08lx id=%lu action=pause holds=%u\n",
                   current_frame_, ++seq_, qpc(), static_cast<unsigned long>(return_va), stop_caller_name(return_va), static_cast<unsigned long>(record),
                   static_cast<unsigned long>(load32(record + record_id)), holds_.count);
    return record;
}
// MOV_StopMovie native entry (EAX = id): the id's holds go; the engine pauses the record itself.
__attribute__((force_align_arg_pointer)) void __cdecl x3m_music_keep_stop_movie(const std::uint32_t* block) {
    x3m::LightCallBoundary cpu;
    if (!keep_armed_ || !holds_.count) return;
    const std::uint32_t id = block[2], return_va = block[4];
    const unsigned dropped = drop_holds_by_id(holds_, id);
    if (dropped)
        write_line("music_keep_stop_movie frame=%llu seq=%lu qpc=%llu id=%lu caller=0x%08lx name=%s dropped=%u holds=%u\n",
                   current_frame_, ++seq_, qpc(), static_cast<unsigned long>(id), static_cast<unsigned long>(return_va), stop_movie_caller_name(return_va), dropped, holds_.count);
}
__attribute__((force_align_arg_pointer)) void __cdecl x3m_music_trace_stop(const std::uint32_t* block) {
    x3m::LightCallBoundary cpu;
    if (!trace_armed_) return;
    const std::uint32_t return_va = block[4];
    write_line("music_trace_stop frame=%llu seq=%lu qpc=%llu caller=0x%08lx name=%s\n", current_frame_, ++seq_, qpc(), static_cast<unsigned long>(return_va), stop_caller_name(return_va));
}
__attribute__((force_align_arg_pointer)) void __cdecl x3m_music_trace_play(const std::uint32_t* block) {
    x3m::LightCallBoundary cpu;
    if (!trace_armed_) return;
    const std::uint32_t return_va = block[4], id = block[4 + play_arg_id];
    const std::int32_t minutes = std::int32_t(block[4 + play_arg_minutes]), seconds = std::int32_t(block[4 + play_arg_seconds]), ms = std::int32_t(block[4 + play_arg_ms]);
    const std::int32_t start_ms = std::int32_t((std::uint32_t(minutes) * 60u + std::uint32_t(seconds)) * 1000u + std::uint32_t(ms));   // the engine's own wrap-around arithmetic
    const std::uint32_t record = find_record(id);
    write_line("music_trace_play frame=%llu seq=%lu qpc=%llu id=%lu start_ms=%ld caller=0x%08lx name=%s record=0x%08lx flags=0x%lx\n",
               current_frame_, ++seq_, qpc(), static_cast<unsigned long>(id), static_cast<long>(start_ms), static_cast<unsigned long>(return_va), play_caller_name(return_va),
               static_cast<unsigned long>(record), static_cast<unsigned long>(record ? load32(record + record_flags) : 0));
}
__attribute__((force_align_arg_pointer)) void __cdecl x3m_music_trace_stop_movie(const std::uint32_t* block) {
    x3m::LightCallBoundary cpu;
    if (!trace_armed_) return;
    const std::uint32_t id = block[2], return_va = block[4];
    const std::uint32_t record = find_record(id);
    write_line("music_trace_stop_movie frame=%llu seq=%lu qpc=%llu id=%lu caller=0x%08lx name=%s record=0x%08lx flags=0x%lx\n",
               current_frame_, ++seq_, qpc(), static_cast<unsigned long>(id), static_cast<unsigned long>(return_va), stop_movie_caller_name(return_va),
               static_cast<unsigned long>(record), static_cast<unsigned long>(record ? load32(record + record_flags) : 0));
}
}

// Integer-only stubs. Every stub saves EFLAGS and EAX/ECX/EDX, calls its
// handler (cdecl, four-byte stack), restores them and leaves with ESP exactly
// as at entry. Patch A: entered by the claim's dispatcher at 0x004982db with
// ESI = record, EBX = 0, EBP = next node and the stop-all's return address at
// [esp+0x10]; keep_running leaves for 0x00498322 with EDI loaded as the
// displaced `mov edi,[esi+0x24]` would have (the flags are dead: the
// continuation rewrites them before any reader), every other mode replays the
// two displaced instructions through the tail so the `je 0x4982f6` consumes
// the tail's CMP. Patch C: entered by `call` from 0x00498d54 with EAX =
// record and [esp+4] = start ms; skip returns 1 with the stack untouched
// (the caller pops the argument), the vanilla paths tail-jump to 0x004d0430
// with the same stack; the pause of a held record calls 0x004d1810 (EAX =
// record; it clobbers EAX/ECX/EDX only, all saved here). Keep entry stub
// (stop-all entry, shared site): loops on the handler, pausing each orphaned
// keep-running record it returns until 0. Keep stop-movie stub and the trace
// stubs: the saved block's address is the handler's one argument.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_music_keep_a_stub
_x3m_music_keep_a_stub:
    pushfd
    push eax
    push ecx
    push edx
    push dword ptr [esp+0x20]
    push esi
    call _x3m_music_keep_stop_all
    add esp, 8
    cmp eax, 1
    pop edx
    pop ecx
    pop eax
    je 1f
    popfd
    jmp dword ptr [_x3m_music_stop_all_continue]
1:  popfd
    mov edi, dword ptr [esi+0x24]
    jmp dword ptr [_x3m_music_stop_all_skip]

    .p2align 4
    .globl _x3m_music_keep_c_thunk
_x3m_music_keep_c_thunk:
    pushfd
    push eax
    push ecx
    push edx
    push dword ptr [esp+0x14]
    push eax
    call _x3m_music_keep_seek
    add esp, 8
    cmp eax, 1
    je 1f
    cmp eax, 2
    jne 2f
    mov eax, dword ptr [_x3m_music_keep_pause_record]
    call dword ptr [_x3m_music_pause_fn]
2:  pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_seek_original]
1:  pop edx
    pop ecx
    pop eax
    popfd
    mov eax, 1
    ret

    .p2align 4
    .globl _x3m_music_keep_entry_stub
_x3m_music_keep_entry_stub:
    pushfd
    push eax
    push ecx
    push edx
1:  push esp
    call _x3m_music_keep_stop_all_entry
    add esp, 4
    test eax, eax
    jz 2f
    call dword ptr [_x3m_music_pause_fn]
    jmp 1b
2:  pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_keep_entry_continue]

    .p2align 4
    .globl _x3m_music_keep_stop_movie_stub
_x3m_music_keep_stop_movie_stub:
    pushfd
    push eax
    push ecx
    push edx
    push esp
    call _x3m_music_keep_stop_movie
    add esp, 4
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_keep_stop_movie_continue]

    .p2align 4
    .globl _x3m_music_trace_stop_stub
_x3m_music_trace_stop_stub:
    pushfd
    push eax
    push ecx
    push edx
    push esp
    call _x3m_music_trace_stop
    add esp, 4
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_trace_stop_continue]

    .p2align 4
    .globl _x3m_music_trace_play_stub
_x3m_music_trace_play_stub:
    pushfd
    push eax
    push ecx
    push edx
    push esp
    call _x3m_music_trace_play
    add esp, 4
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_trace_play_continue]

    .p2align 4
    .globl _x3m_music_trace_stop_movie_stub
_x3m_music_trace_stop_movie_stub:
    pushfd
    push eax
    push ecx
    push edx
    push esp
    call _x3m_music_trace_stop_movie
    add esp, 4
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_music_trace_stop_movie_continue]
    .att_syntax
)");
extern "C" void x3m_music_keep_a_stub();
extern "C" void x3m_music_keep_c_thunk();
extern "C" void x3m_music_keep_entry_stub();
extern "C" void x3m_music_keep_stop_movie_stub();
extern "C" void x3m_music_trace_stop_stub();
extern "C" void x3m_music_trace_play_stub();
extern "C" void x3m_music_trace_stop_movie_stub();

namespace {
template<class T> std::uint32_t address_of(T* p) { return std::uint32_t(reinterpret_cast<std::uintptr_t>(p)); }
// Claims a site the feature owns alone, stores the current chain head (the tail) into `continuation` and chains the stub in front.
bool hook_site(engine_patch::Site& site, const engine_patch::SiteSpec& spec, void (*stub)(), volatile std::uint32_t& continuation, const char** reason) {
    site = engine_patch::Site{};
    if (!engine_patch::claim(site, spec)) { *reason = site.status; return false; }
    continuation = address_of(*site.entry);
    if (!engine_patch::push_front(site, reinterpret_cast<void*>(stub))) { *reason = "chain_failed"; return false; }
    return true;
}
// Restores a site the feature owns alone; false only when a live patch could not be put back.
bool unhook_site(engine_patch::Site& site) { return engine_patch::restore(site); }
// Shared site: the first user claims, every user chains its stub in front of
// the current head. A push that fails leaves the chain untouched (store_pointer
// is the only write). A feature that later fails its install releases the
// site: the last user restores the bytes; while another user remains, the
// failed feature's stub stays chained but its armed_ flag keeps it inert.
bool acquire_shared(SharedSite& s, const engine_patch::SiteSpec& spec, void (*stub)(), volatile std::uint32_t& continuation, const char** reason) {
    if (!s.users) {
        s.site = engine_patch::Site{};
        if (!engine_patch::claim(s.site, spec)) { *reason = s.site.status; return false; }
    }
    continuation = address_of(*s.site.entry);
    if (!engine_patch::push_front(s.site, reinterpret_cast<void*>(stub))) {
        *reason = "chain_failed";
        if (!s.users && !engine_patch::restore(s.site)) *reason = "rollback_failed";
        return false;
    }
    ++s.users;
    return true;
}
bool release_shared(SharedSite& s) {
    if (!s.users) return !s.site.patched_in || engine_patch::restore(s.site);   // a claim whose push failed: already restored above
    if (--s.users) return true;
    return engine_patch::restore(s.site);
}
bool keep_windows_match() {
    return bytes_match(stop_all_va, stop_all_head, stop_all_head_length) && bytes_match(a_window_va, a_window, a_window_length) && bytes_match(a_skip_va, a_skip_window, a_skip_length)
        && bytes_match(c_pre_va, c_pre_window, c_pre_length) && bytes_match(c_post_va, c_post_window, c_post_length)
        && bytes_match(play_set_playing_va, play_set_playing_window, play_set_playing_length)
        && bytes_match(c_target_va, seek_head, seek_head_length) && bytes_match(pause_va, pause_body, pause_body_length) && bytes_match(run_va, run_head, run_head_length)
        && bytes_match(t_stop_movie_site_va, stop_movie_head, stop_movie_head_length)
        && call_targets(c_site_va, c_target_va) && call_targets(c_run_call_va, run_va) && call_targets(t_stop_movie_site_va + 0x4f, pause_va);   // 0x0049885f: MOV_StopMovie's own pause call
}
bool callers_match() {
    for (const auto& c : stop_callers) if (!call_targets(c.call_va, stop_all_va) || c.return_va != c.call_va + call_length) return false;
    for (const auto& c : play_callers) if (!call_targets(c.call_va, play_va) || c.return_va != c.call_va + call_length) return false;
    for (const auto& c : stop_movie_callers) if (!call_targets(c.call_va, t_stop_movie_site_va) || c.return_va != c.call_va + call_length) return false;
    return true;
}
bool trace_windows_match() {
    return bytes_match(t_stop_all_site_va, stop_all_head, stop_all_head_length) && bytes_match(t_play_site_va, play_head, play_head_length)
        && bytes_match(t_stop_movie_site_va, stop_movie_head, stop_movie_head_length);
}
engine_patch::SiteSpec entry_spec() { return spec_for("music_stop_all_entry", t_stop_all_site_va, stop_all_head, t_stop_all_length); }
engine_patch::SiteSpec stop_movie_spec() { return spec_for("music_stop_movie_entry", t_stop_movie_site_va, stop_movie_head, t_stop_movie_length); }
// A, then C, then the two shared entries; a later failure puts the earlier
// ones back. A live site that cannot be restored keeps the module registered
// (patched_) so shutdown() tries again, and armed_ stays false.
bool install_keep(const char** reason) {
    if (!engine_patch::install_window_open()) { *reason = "late_claim"; return false; }
    if (!keep_windows_match()) { *reason = "bytes_mismatch"; return false; }
    if (!callers_match()) { *reason = "callers_mismatch"; return false; }
    if (!pin_self()) { *reason = "pin_failed"; return false; }
    holds_.clear();
    x3m_music_keep_pause_record = 0;
    x3m_music_stop_all_skip = a_skip_va; x3m_music_seek_original = c_target_va; x3m_music_pause_fn = pause_va;
    unsigned stage = 0;
    bool ok = hook_site(a_site_, spec_for("music_keep_a", a_site_va, a_window + a_site_offset, a_site_length), &x3m_music_keep_a_stub, x3m_music_stop_all_continue, reason);
    if (ok) { ++stage; c_site_ = engine_patch::CallSite{}; ok = engine_patch::claim_call(c_site_, c_site_va, c_target_va, reinterpret_cast<void*>(&x3m_music_keep_c_thunk)); if (!ok) *reason = c_site_.status; }
    if (ok) { ++stage; ok = acquire_shared(entry_site_, entry_spec(), &x3m_music_keep_entry_stub, x3m_music_keep_entry_continue, reason); }
    if (ok) { ++stage; ok = acquire_shared(stop_movie_site_, stop_movie_spec(), &x3m_music_keep_stop_movie_stub, x3m_music_keep_stop_movie_continue, reason); }
    if (!ok) {
        // Reverse order; restore_call/restore are no-ops on a site that is not live, so a claim that failed
        // after going live (rollback_failed) is retried here too. Shared sites are released only when acquired.
        bool back = true;
        if (stage >= 3) back = release_shared(entry_site_) && back;
        back = engine_patch::restore_call(c_site_) && back;
        back = unhook_site(a_site_) && back;
        if (!back) { keep_patched_ = true; *reason = "rollback_failed"; }
        return false;
    }
    keep_patched_ = true; keep_armed_ = true;
    *reason = "ok";
    return true;
}
// Play entry (own), then the shared stop-movie and stop-all entries.
bool install_trace(const char** reason) {
    if (!engine_patch::install_window_open()) { *reason = "late_claim"; return false; }
    if (!trace_windows_match()) { *reason = "bytes_mismatch"; return false; }
    if (!callers_match()) { *reason = "callers_mismatch"; return false; }
    if (!pin_self()) { *reason = "pin_failed"; return false; }
    unsigned stage = 0;
    bool ok = hook_site(t_play_site_, spec_for("music_trace_play", t_play_site_va, play_head, t_play_length), &x3m_music_trace_play_stub, x3m_music_trace_play_continue, reason);
    if (ok) { ++stage; ok = acquire_shared(stop_movie_site_, stop_movie_spec(), &x3m_music_trace_stop_movie_stub, x3m_music_trace_stop_movie_continue, reason); }
    if (ok) { ++stage; ok = acquire_shared(entry_site_, entry_spec(), &x3m_music_trace_stop_stub, x3m_music_trace_stop_continue, reason); }
    if (!ok) {
        bool back = true;
        if (stage >= 2) back = release_shared(stop_movie_site_) && back;   // acquired shared sites only: the keep's users stay counted
        back = unhook_site(t_play_site_) && back;
        if (!back) { trace_patched_ = true; *reason = "rollback_failed"; }
        return false;
    }
    trace_patched_ = true; trace_armed_ = true;
    *reason = "ok";
    return true;
}
bool requested(const wchar_t* name, bool* present) {
    wchar_t setting[4]{};
    const DWORD length = GetEnvironmentVariableW(name, setting, 4);
    *present = length != 0;
    return length == 1 && setting[0] == L'1';
}
}

namespace x3m::music_keep {
bool initialize() {
    const DWORD error = GetLastError();
    if (keep_patched_ || trace_patched_) { SetLastError(error); return true; }
    bool keep_present = false, trace_present = false;
    const bool keep_requested = requested(L"X3M_MUSIC_KEEP", &keep_present), trace_requested = requested(L"X3M_MUSIC_TRACE", &trace_present);
    if (!keep_present && !trace_present) { keep_state_ = trace_state_ = "disabled"; SetLastError(error); return false; }
    lines_ = seq_ = suppressed_ = walks_cut_ = 0; cap_noted_ = false;
    const bool image_ok = object_trace::executable_verified();
    bool keep_applied = false, trace_applied = false;
    if (!keep_requested) keep_state_ = "disabled";
    else if (!image_ok) keep_state_ = "executable_mismatch";
    else keep_applied = install_keep(&keep_state_);
    if (!trace_requested) trace_state_ = "disabled";
    else if (!image_ok) trace_state_ = "executable_mismatch";
    else trace_applied = install_trace(&trace_state_);
    if (keep_present)
        log("music_keep requested=%u patched=%u armed=%u reason=%s site_a=0x%08lx site_c=0x%08lx site_entry=0x%08lx site_stop_movie=0x%08lx write_a=%s write_c=%s write_entry=%s write_stop_movie=%s "
            "stub_a=0x%08lx thunk_c=0x%08lx stub_entry=0x%08lx stub_stop_movie=0x%08lx skip=0x%08lx seek=0x%08lx pause=0x%08lx holds=%u walk_limit=%u",
            keep_requested ? 1u : 0u, keep_patched_ ? 1u : 0u, keep_armed_ ? 1u : 0u, keep_state_, static_cast<unsigned long>(a_site_va), static_cast<unsigned long>(c_site_va),
            static_cast<unsigned long>(t_stop_all_site_va), static_cast<unsigned long>(t_stop_movie_site_va),
            write_kind(a_site_.patched_in, a_site_.atomic_write), write_kind(c_site_.patched_in, c_site_.atomic_write),
            write_kind(entry_site_.site.patched_in, entry_site_.site.atomic_write), write_kind(stop_movie_site_.site.patched_in, stop_movie_site_.site.atomic_write),
            static_cast<unsigned long>(address_of(&x3m_music_keep_a_stub)), static_cast<unsigned long>(address_of(&x3m_music_keep_c_thunk)),
            static_cast<unsigned long>(address_of(&x3m_music_keep_entry_stub)), static_cast<unsigned long>(address_of(&x3m_music_keep_stop_movie_stub)),
            static_cast<unsigned long>(a_skip_va), static_cast<unsigned long>(c_target_va), static_cast<unsigned long>(pause_va), hold_capacity, list_walk_limit);
    if (trace_present)
        log("music_trace requested=%u patched=%u armed=%u reason=%s site_stop_all=0x%08lx site_play=0x%08lx site_stop_movie=0x%08lx write_stop_all=%s write_play=%s write_stop_movie=%s "
            "stub_stop_all=0x%08lx stub_play=0x%08lx stub_stop_movie=0x%08lx cap=%u",
            trace_requested ? 1u : 0u, trace_patched_ ? 1u : 0u, trace_armed_ ? 1u : 0u, trace_state_, static_cast<unsigned long>(t_stop_all_site_va), static_cast<unsigned long>(t_play_site_va),
            static_cast<unsigned long>(t_stop_movie_site_va), write_kind(entry_site_.site.patched_in, entry_site_.site.atomic_write), write_kind(t_play_site_.patched_in, t_play_site_.atomic_write),
            write_kind(stop_movie_site_.site.patched_in, stop_movie_site_.site.atomic_write), static_cast<unsigned long>(address_of(&x3m_music_trace_stop_stub)),
            static_cast<unsigned long>(address_of(&x3m_music_trace_play_stub)), static_cast<unsigned long>(address_of(&x3m_music_trace_stop_movie_stub)), trace_line_cap);
    SetLastError(error);
    return keep_applied || trace_applied;
}
bool shutdown() {
    if (!keep_patched_ && !trace_patched_) return true;
    const DWORD error = GetLastError();
    bool ok = true;
    keep_armed_ = trace_armed_ = false;
    if (keep_patched_) {
        const bool back_sm = release_shared(stop_movie_site_), back_e = release_shared(entry_site_), back_c = engine_patch::restore_call(c_site_), back_a = unhook_site(a_site_);
        keep_patched_ = false; holds_.clear();   // the stubs stay in the DLL (pinned); the claim tails stay in the arena
        keep_state_ = back_sm && back_e && back_c && back_a ? "restored" : "restore_failed";
        ok = ok && back_sm && back_e && back_c && back_a;
    }
    if (trace_patched_) {
        const bool back_e = release_shared(entry_site_), back_sm = release_shared(stop_movie_site_), back_p = unhook_site(t_play_site_);
        trace_patched_ = false;
        trace_state_ = back_e && back_sm && back_p ? "restored" : "restore_failed";
        ok = ok && back_e && back_sm && back_p;
    }
    SetLastError(error);
    return ok;
}
const char* state() { return keep_state_; }
const char* trace_state() { return trace_state_; }
void present(unsigned long long, unsigned long long frame, bool) {
    if (keep_patched_ || trace_patched_) current_frame_ = frame;   // one store, no API call
}
}
