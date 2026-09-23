#pragma once
#include <cstdint>

// Sector-music keep (X3M_MUSIC_KEEP=1) and the music trace (X3M_MUSIC_TRACE=1):
// the engine sites, the byte windows the installer compares, the stop-all
// caller table and the two decisions, as pure functions the host test
// compiles (docs/reverse-engineering/music-restart.md section 4 and
// "Implementation"). No Windows header, no allocation, no floating point.
//
// Engine layout (music-restart.md section 1, "Data"): a media record is 0x40
// bytes; +0x10 id, +0x14 completion context, +0x18 completion slot, +0x24
// media object, +0x2c flags (1 loop, 2 playing, 0x80 music-volume class);
// the record list head is *0x00606f44 (first node = [head]); nodes follow
// +0x00 until the sentinel whose next is 0.
namespace x3m::music_keep::core {
constexpr std::uint32_t list_head_va = 0x00606f44;
constexpr unsigned record_id = 0x10, record_context = 0x14, record_slot = 0x18, record_media = 0x24, record_flags = 0x2c;
constexpr std::uint32_t flag_loop = 1, flag_playing = 2, flag_directsound = 0x40, flag_music = 0x80;
constexpr unsigned list_walk_limit = 4096;   // nodes visited at most (the engine's own walks are unbounded; a cut walk is logged); walked only on a music play or stop

// ---- Patch A: the stop-all classifier, 0x004982db (two whole instructions) ----
constexpr std::uint32_t stop_all_va = 0x004982b0, stop_all_end_va = 0x00498367;
constexpr std::uint32_t a_site_va = 0x004982db, a_next_va = 0x004982e1, a_skip_va = 0x00498322;
constexpr unsigned a_site_length = 6, a_return_slot = 0x10;   // four pushes since entry: the caller's return address is at [esp+0x10]
// From the second push: push ebp; mov ebp,[eax]; cmp [ebp],ebx; je; push esi; push edi; mov edi,edi; mov esi,ebp;
// test [esi+0x2c],2; mov ebp,[ebp]; je; <site>; je; mov eax,[edi+0x74]. With `push ebx` at 0x004982b5 (stop_all_head)
// these are the four pushes that put the caller's return address at [esp+0x10] at the site.
constexpr std::uint32_t a_window_va = 0x004982c0;
constexpr unsigned a_window_length = 38, a_site_offset = a_site_va - a_window_va;
constexpr unsigned char a_window[a_window_length] = {
    0x55, 0x8b, 0x28, 0x39, 0x5d, 0x00, 0x0f, 0x84, 0x98, 0x00, 0x00, 0x00, 0x56, 0x57, 0x8b, 0xff, 0x8b, 0xf5, 0xf6,
    0x46, 0x2c, 0x02, 0x8b, 0x6d, 0x00, 0x74, 0x7e, 0x8b, 0x7e, 0x24, 0x39, 0x5f, 0x04, 0x74, 0x13, 0x8b, 0x47, 0x74};
constexpr unsigned a_skip_length = 15;                          // mov ecx,[0x6085e4]; and [esi+0x2c],~2; cmp ecx,ebx; mov eax,[esi+0x18]
constexpr unsigned char a_skip_window[a_skip_length] = {0x8b, 0x0d, 0xe4, 0x85, 0x60, 0x00, 0x83, 0x66, 0x2c, 0xfd, 0x3b, 0xcb, 0x8b, 0x46, 0x18};

// ---- Patch C: the seek call of the play routine, 0x00498d54 -> 0x004d0430 ----
constexpr std::uint32_t play_va = 0x00498c90, play_end_va = 0x00498e28;
constexpr std::uint32_t c_site_va = 0x00498d54, c_target_va = 0x004d0430, c_return_va = 0x00498d59;
constexpr std::uint32_t c_pre_va = 0x00498d4d, c_post_va = 0x00498d59, c_run_call_va = 0x00498d71, run_va = 0x004d1870, pause_va = 0x004d1810;
constexpr unsigned call_length = 5;
constexpr unsigned c_pre_length = 7;                            // add edi,[esp+0x2c]; mov eax,esi; push edi
constexpr unsigned char c_pre_window[c_pre_length] = {0x03, 0x7c, 0x24, 0x2c, 0x8b, 0xc6, 0x57};
constexpr unsigned c_post_length = 29;                          // add esp,4; test eax,eax; je; xor ebp,ebp; cmp ebx,ebp; jle; mov eax,[esi+0x24]; mov [eax+0x94],ebx; mov eax,esi; call run
constexpr unsigned char c_post_window[c_post_length] = {
    0x83, 0xc4, 0x04, 0x85, 0xc0, 0x74, 0x7e, 0x33, 0xed, 0x3b, 0xdd, 0x7e, 0x09, 0x8b, 0x46, 0x24,
    0x89, 0x98, 0x94, 0x00, 0x00, 0x00, 0x8b, 0xc6, 0xe8, 0xfa, 0x8a, 0x03, 0x00};
constexpr std::uint32_t play_set_playing_va = 0x00498d8c;      // or ecx,2; or [esi+0x2c],ecx
constexpr unsigned play_set_playing_length = 6;
constexpr unsigned char play_set_playing_window[play_set_playing_length] = {0x83, 0xc9, 0x02, 0x09, 0x4e, 0x2c};
constexpr unsigned seek_head_length = 26;                       // 0x004d0430: push ebp; mov ebp,esp; and esp,-0x40; sub esp,0x34; push ebx; push esi; mov esi,[eax+0x24]; ...
constexpr unsigned char seek_head[seek_head_length] = {
    0x55, 0x8b, 0xec, 0x83, 0xe4, 0xc0, 0x83, 0xec, 0x34, 0x53, 0x56, 0x8b, 0x70, 0x24, 0xbb, 0x01,
    0x00, 0x00, 0x00, 0x39, 0x5e, 0x48, 0x57, 0x8b, 0x7d, 0x08};
constexpr unsigned pause_body_length = 0x57;                    // 0x004d1810 whole: EAX = record; Pause + DirectSound Stop; clobbers EAX/ECX/EDX only
constexpr unsigned char pause_body[pause_body_length] = {
    0x56, 0x8b, 0x70, 0x24, 0x83, 0x7e, 0x04, 0x00, 0x74, 0x13, 0x8b, 0x46, 0x74, 0x85, 0xc0, 0x74,
    0x0c, 0x8b, 0x08, 0x8b, 0x51, 0x20, 0x50, 0xff, 0xd2, 0x85, 0xc0, 0x7c, 0x21, 0x8b, 0x86, 0x8c,
    0x00, 0x00, 0x00, 0xa8, 0x08, 0x75, 0x29, 0xa8, 0x40, 0x74, 0x25, 0x8b, 0x46, 0x44, 0x85, 0xc0,
    0x74, 0x10, 0x8b, 0x08, 0x8b, 0x51, 0x48, 0x50, 0xff, 0xd2, 0x85, 0xc0, 0x7d, 0x04, 0x33, 0xc0,
    0x5e, 0xc3, 0xc7, 0x46, 0x48, 0x01, 0x00, 0x00, 0x00, 0xc7, 0x46, 0x64, 0x01, 0x00, 0x00, 0x00,
    0xb8, 0x01, 0x00, 0x00, 0x00, 0x5e, 0xc3};
constexpr unsigned run_head_length = 14;                        // 0x004d1870: sub esp,0x70; push ebx; mov ebx,[eax+0x24]; test byte [ebx+0x8c],0x10
constexpr unsigned char run_head[run_head_length] = {0x83, 0xec, 0x70, 0x53, 0x8b, 0x58, 0x24, 0xf6, 0x83, 0x8c, 0x00, 0x00, 0x00, 0x10};

// ---- Trace sites: three entry trampolines ----
constexpr std::uint32_t t_stop_all_site_va = stop_all_va;            // mov eax,[0x606f44] (5 bytes, absolute operand)
constexpr unsigned t_stop_all_length = 5, stop_all_head_length = 16;
constexpr unsigned char stop_all_head[stop_all_head_length] = {0xa1, 0x44, 0x6f, 0x60, 0x00, 0x53, 0x33, 0xdb, 0x3b, 0xc3, 0x0f, 0x84, 0xa5, 0x00, 0x00, 0x00};
constexpr std::uint32_t t_play_site_va = play_va;                    // push ecx; push ebx; mov ebx,[esp+0x14] (6 bytes; ESP-relative, replayed at the entry ESP)
constexpr unsigned t_play_length = 6, play_head_length = 18;
constexpr unsigned char play_head[play_head_length] = {0x51, 0x53, 0x8b, 0x5c, 0x24, 0x14, 0x55, 0x8b, 0x6c, 0x24, 0x28, 0x56, 0x57, 0xbf, 0x02, 0x00, 0x00, 0x00};
constexpr std::uint32_t t_stop_movie_site_va = 0x00498810;           // mov ecx,[0x606f44] (6 bytes); EAX = id, [esp+4] = context, EDI = slot
constexpr unsigned t_stop_movie_length = 6, stop_movie_head_length = 14;
constexpr unsigned char stop_movie_head[stop_movie_head_length] = {0x8b, 0x0d, 0x44, 0x6f, 0x60, 0x00, 0x55, 0x8b, 0x6c, 0x24, 0x08, 0x56, 0x8b, 0x31};
// Play routine arguments at its entry frame (dwords from the return address): a1 slot, a2 context, a3 id, a4 minutes, a5 seconds, a6 ms, a7..a9 end, a10 loop.
constexpr unsigned play_arg_id = 3, play_arg_minutes = 4, play_arg_seconds = 5, play_arg_ms = 6;
constexpr unsigned trace_line_cap = 1000;

// ---- Stop-all callers (return addresses) and their policy ----
enum class StopMode : unsigned { vanilla = 0, keep_running = 1, paused = 2 };
struct StopCaller { std::uint32_t call_va, return_va; const char* name; StopMode mode; };
constexpr StopCaller stop_callers[] = {
    {0x004d36bd, 0x004d36c2, "alt_tab", StopMode::paused},          // WndProc WM_ACTIVATE inactive arm
    {0x0040455c, 0x00404561, "save", StopMode::keep_running},       // save routine 0x00404530, first statement
    {0x00407064, 0x00407069, "pause", StopMode::keep_running},      // X2_SetPause
    {0x00404ced, 0x00404cf2, "load", StopMode::vanilla},            // load 0x00404cc0
    {0x00497bb6, 0x00497bbb, "p_leave", StopMode::vanilla},         // P_Leave
    {0x00403878, 0x0040387d, "session_start", StopMode::vanilla},   // main-loop entry 0x00403840
};
constexpr unsigned stop_caller_count = sizeof stop_callers / sizeof stop_callers[0];
struct PlayCaller { std::uint32_t call_va, return_va; const char* name; };
constexpr PlayCaller play_callers[] = {
    {0x0049981f, 0x00499824, "MOV_PlayMovie"},
    {0x00499982, 0x00499987, "MOV_PlayMovieFrom"},
    {0x004f6668, 0x004f666d, "helper_0x004f6640"},
};
constexpr unsigned play_caller_count = sizeof play_callers / sizeof play_callers[0];
constexpr PlayCaller stop_movie_callers[] = {   // the three direct callers of the MOV_StopMovie native 0x00498810 (EAX = id)
    {0x00499881, 0x00499886, "MOV_StopMovie"},
    {0x0045c27d, 0x0045c282, "selector_0x0045b720"},    // the per-sector object pass (station screens), sector-post-pass.md
    {0x004f66be, 0x004f66c3, "caller_0x004f66be"},
};
constexpr unsigned stop_movie_caller_count = sizeof stop_movie_callers / sizeof stop_movie_callers[0];
inline const StopCaller* stop_caller(std::uint32_t return_va) {
    for (const auto& c : stop_callers) if (c.return_va == return_va) return &c;
    return nullptr;
}
inline const char* stop_caller_name(std::uint32_t return_va) { const StopCaller* c = stop_caller(return_va); return c ? c->name : "unknown"; }
inline StopMode stop_mode(std::uint32_t return_va) { const StopCaller* c = stop_caller(return_va); return c ? c->mode : StopMode::vanilla; }
inline const char* play_caller_name(std::uint32_t return_va) {
    for (const auto& c : play_callers) if (c.return_va == return_va) return c.name;
    return "unknown";
}
inline const char* stop_movie_caller_name(std::uint32_t return_va) {
    for (const auto& c : stop_movie_callers) if (c.return_va == return_va) return c.name;
    return "unknown";
}
inline const char* mode_name(StopMode m) { return m == StopMode::keep_running ? "keep_running" : m == StopMode::paused ? "paused" : "vanilla"; }

// ---- Holds ----
// A hold is a music record the classifier let through a preserve caller:
// (record, id, media) identify it without dereferencing the record later;
// mode says whether its graph is still running (keep_running) or paused.
struct Hold { std::uint32_t record, id, media; StopMode mode; };
constexpr unsigned hold_capacity = 4;
struct Holds {
    Hold slots[hold_capacity]{};
    unsigned count = 0, next = 0;   // next: the slot replaced when full (oldest first)
    void clear() { count = 0; next = 0; }
    const Hold* find(std::uint32_t record, std::uint32_t id, std::uint32_t media) const {
        for (unsigned i = 0; i < count; ++i) if (slots[i].record == record && slots[i].id == id && slots[i].media == media) return &slots[i];
        return nullptr;
    }
    void add(const Hold& h) {
        for (unsigned i = 0; i < count; ++i) if (slots[i].record == h.record) { slots[i] = h; return; }
        if (count < hold_capacity) { slots[count++] = h; return; }
        slots[next] = h; next = (next + 1) % hold_capacity;
    }
};

// What a live-list lookup reports for a held record: its flags and completion context.
struct LiveRecord { std::uint32_t flags, context; };

// Patch A decision. In: the record's flags, the stop-all caller's return
// address. Out: the mode; the stub falls through to the vanilla Pause on
// vanilla/paused and jumps to a_skip_va on keep_running. A vanilla caller
// (load, P_Leave, session start, unknown) drops every hold; a non-music
// record is never held and takes the vanilla path whatever the caller. A
// DirectSound-path record (flag 0x40) is paused, not kept running, under
// save and pause too: the pump services flag-2 records only and the save's
// file write blocks the loop, so a running buffer would starve.
struct StopDecision { StopMode mode; bool held; };
inline StopDecision decide_stop(Holds& holds, std::uint32_t record, std::uint32_t flags, std::uint32_t id, std::uint32_t media, std::uint32_t return_va) {
    StopMode mode = stop_mode(return_va);
    if (mode == StopMode::vanilla) { holds.clear(); return {StopMode::vanilla, false}; }
    if (!(flags & flag_music)) return {StopMode::vanilla, false};
    if (mode == StopMode::keep_running && (flags & flag_directsound)) mode = StopMode::paused;
    holds.add(Hold{record, id, media, mode});
    return {mode, true};
}
// Stop-all entry, any caller: a keep-running hold the script never replayed
// (still linked with the same id and media, flag 2 clear, context 0) is a
// track playing outside the engine's bookkeeping; it must not keep playing
// through an alt-tab or a load. Returns the next such record to pause with the
// engine's 0x004d1810 and removes its hold; every other keep-running hold
// examined (unlinked, playing again or claimed by a task) is dropped without a
// pause; 0 when nothing is left. The stub loops until 0 (bounded by
// hold_capacity). Paused-mode holds stay: their graph is already paused and
// the replay after the alt-tab still needs them.
template<class Lookup>
inline std::uint32_t stale_hold_to_pause(Holds& holds, Lookup&& live) {
    for (unsigned i = 0; i < holds.count; ++i) {
        const Hold h = holds.slots[i];
        if (h.mode != StopMode::keep_running) continue;
        for (unsigned j = i + 1; j < holds.count; ++j) holds.slots[j - 1] = holds.slots[j];
        --holds.count; holds.next = 0;
        LiveRecord r{};
        if (live(h.record, h.id, h.media, r) && !(r.flags & flag_playing) && r.context == 0 && (r.flags & flag_music)) return h.record;
        return stale_hold_to_pause(holds, live);
    }
    return 0;
}
// MOV_StopMovie native 0x00498810 (EAX = id): the script stops the track
// itself, and the engine pauses it there; a later replay must restart from 0
// as vanilla does, so the id's holds go. Returns how many were dropped.
inline unsigned drop_holds_by_id(Holds& holds, std::uint32_t id) {
    unsigned dropped = 0;
    for (unsigned i = 0; i < holds.count;) {
        if (holds.slots[i].id != id) { ++i; continue; }
        for (unsigned j = i + 1; j < holds.count; ++j) holds.slots[j - 1] = holds.slots[j];
        --holds.count; holds.next = 0; ++dropped;
    }
    return dropped;
}

// Patch C decision. In: the found record (EAX of the seek call), its flags, id
// and media, the requested start, and a live-list membership test for a held
// record (the callback returns the record's flags and context, or false when
// the record is no longer linked). Out: skip (return 1 without seeking),
// vanilla (original seek), pause_then_vanilla (the engine's 0x004d1810 on
// `pause_record`, then the original seek). Holds are dropped by every music play.
enum class SeekAction : unsigned { vanilla = 0, skip = 1, pause_then_vanilla = 2 };
struct SeekDecision { SeekAction action; std::uint32_t pause_record; std::uint32_t hold_id; };
template<class Lookup>
inline SeekDecision decide_seek(Holds& holds, std::uint32_t record, std::uint32_t flags, std::uint32_t id, std::uint32_t media, std::int32_t start_ms, Lookup&& live) {
    if (!(flags & flag_music)) return {SeekAction::vanilla, 0, 0};
    const Hold* mine = holds.find(record, id, media);
    if (start_ms == 0 && (mine || (flags & flag_playing))) {
        const std::uint32_t hold_id = mine ? mine->id : id;
        holds.clear();
        return {SeekAction::skip, 0, hold_id};
    }
    // Another id (or a positioned play): a keep-running hold whose graph is
    // still running would overlap the new track. Pause it when it is still
    // linked with the same id and media, not playing again and not owned by a
    // script task again (context 0: the keep-running stop-all cleared it).
    SeekDecision d{SeekAction::vanilla, 0, 0};
    for (unsigned i = 0; i < holds.count; ++i) {
        const Hold& h = holds.slots[i];
        if (h.mode != StopMode::keep_running || h.record == record) continue;
        LiveRecord r{};
        if (!live(h.record, h.id, h.media, r)) continue;
        if ((r.flags & flag_playing) || r.context != 0 || !(r.flags & flag_music)) continue;
        d = {SeekAction::pause_then_vanilla, h.record, h.id};
        break;
    }
    holds.clear();
    return d;
}
}
