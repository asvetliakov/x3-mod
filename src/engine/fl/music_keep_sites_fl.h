#pragma once
#include <cstdint>
#include "../../proxy/music_keep_core.h"
#include "code_window_fl.h"

// FL edition of the music keep's sites, register contracts and caller tables (the AP twin is the site part of
// src/proxy/music_keep_core.h; its policy, the holds, decide_stop/seek/status, stale_hold_to_pause, SavePump, the
// resume memory, TrackQueue with queue_on_stop/play/stop_all/alert, KcPatch and story_detect.h, is shared unchanged).
// Facts: docs/reverse-engineering/fl/music-keep.md sections 1 to 6 and "Port (2026-11-03)"; verifier
// verification/probe/verify_music_keep_sites_fl.py. Every value is an RVA added to x3m::fl::module_base().
//
// The policy keys on the AP callers' return addresses (stop_callers, stop_movie_native_return_va, play_callers[0],
// story_load_caller, save/pause_return_va). The FL glue maps each FL caller return to the AP return of the same role
// (`ap` below) before it calls the policy, and logs the FL address; an FL return outside these tables maps to 0, the
// vanilla/unknown caller of every AP rule.
//
// Stop-all 0x0b4210 (no argument, plain RET; EBX/ESI/EDI pushed, EBP untouched):
//   0b4210  53 / 8b 1d <owner>         push ebx; mov ebx,[base+0x1a84b4]     <- T1 entry claimed (7 bytes, the HIGHLOW
//                                                                               operand copied live; aligned)
//   0b423e  8b 7e 24 / 83 7f 04 00     mov edi,[esi+0x24]; cmp [edi+4],0     <- Patch A claimed (7 bytes; the jump
//   0b4245  74 11                      je 0x0b4258                              crosses a qword: plain write)
//   ESI = the record, EBX = the next node (loaded at 0x0b4236), three pushes since entry: the caller's return at
//   [esp+0xc]. keep_running leaves for 0x0b4285 (clear flag 2, completion (ctx,1), clear slot/context) with EDI loaded;
//   skip_all leaves for 0x0b42c4 (`cmp [ebx],0; jne 0x0b4230`: EBX already the next node, ESI/EDI dead).
// Update manager 0x0b42e0: Patch D at 0x0b434a `call 0x0ff580` (ECX = record, EDX = [record+0x28], AX consumed). The
//   status query answers EAX = 2 without looking when [base+0x1aaf34] == 0 and [[base+0x1a8478]] & 0x4000 is clear.
// Play 0x0b4c50 (ECX = slot, EDX = context, eight caller-popped words: id, start min/sec/ms, end min/sec/ms, loop;
//   ESI = the task at the script callers; plain RET):
//   0b4c50  51 / 53 / 55 / 56 / 57     push ecx/ebx/ebp/esi/edi              <- T2 entry claimed (5 bytes, aligned)
//   0b4d26  e8 <0x0fe5f0>              Patch C: the seek, ECX = record, EDX = start ms (no stack argument)
//   0b4d42  e8 <0x0ff8b0>              Patch E: the Run, ECX = record (no stack argument)
// MOV_StopMovie native 0x0b47a0 (ECX = slot, EDX = context, [esp+4] = id, ESI = the task at the script caller; plain
//   RET, the caller pops one word):
//   0b47a0  a1 <owner>                 mov eax,[base+0x1a84b4]               <- T3 entry claimed (5 bytes, operand live)
// Fallback pause 0x0b41b0 (ECX = record; clears flag 2, Pause/Stop, EAX = 1 always), pump 0x0fe7d0 (ECX = media;
// EAX 0 failure, 1 streaming, 2 drained), free wrapper 0x0b4640 (ECX = slot, EDX = context, [esp+4] = id; slot 32
// excludes the requester callback; no reliable result: the glue derives it from a lookup before and after).
// Save: gzwrite through the IAT slot base+0x1503f0, gzclose through base+0x150408 called from the close wrapper at
//   0x10695d (return 0x106963); the save calls the wrapper at 0x0791fc (return 0x079201). At the gzclose import's
//   entry [esp] = base+0x106963, [esp+8] = base+0x079201 for exactly the save's own close.
namespace x3m::fl::music_keep_fl {
namespace core = x3m::music_keep::core;
constexpr std::uint32_t stop_all_rva = 0x0b4210, stop_all_continue_rva = 0x0b4217;
constexpr unsigned stop_all_length = 7, stop_all_owner_offset = 3;
constexpr std::uint32_t a_site_rva = 0x0b423e, a_skip_rva = 0x0b4285, a_next_rva = 0x0b42c4;
constexpr unsigned a_site_length = 7, a_return_slot = 0x0c;
constexpr unsigned char a_site_bytes[a_site_length] = {0x8b, 0x7e, 0x24, 0x83, 0x7f, 0x04, 0x00};
constexpr std::uint32_t c_site_rva = 0x0b4d26, seek_rva = 0x0fe5f0;
constexpr std::uint32_t d_site_rva = 0x0b434a, status_rva = 0x0ff580;
constexpr std::uint32_t e_site_rva = 0x0b4d42, run_rva = 0x0ff8b0;
constexpr std::uint32_t play_rva = 0x0b4c50;
constexpr unsigned play_length = 5;
constexpr unsigned char play_bytes[play_length] = {0x51, 0x53, 0x55, 0x56, 0x57};
constexpr std::uint32_t stop_movie_rva = 0x0b47a0;
constexpr unsigned stop_movie_length = 5, stop_movie_owner_offset = 1;
constexpr std::uint32_t pause_rva = 0x0b41b0, pump_rva = 0x0fe7d0, free_wrapper_rva = 0x0b4640;
constexpr std::uint32_t free_slot = 32; // >= 32: no requester completion on hit or miss (the JGE at 0x0b4680)
constexpr std::uint32_t owner_slot_rva = 0x1a84b4, vm_slot_rva = 0x1aa174, alert_registry_slot_rva = 0x1a82e4;
constexpr std::uint32_t active_flag_rva = 0x1aaf34, input_flags_slot_rva = 0x1a8478;
constexpr std::uint32_t gzwrite_iat_rva = 0x1503f0, gzclose_iat_rva = 0x150408;
constexpr std::uint32_t gzclose_call_rva = 0x10695d, gzclose_return_rva = 0x106963, close_wrapper_rva = 0x106940;
constexpr std::uint32_t save_close_call_rva = 0x0791fc, save_close_return_rva = 0x079201;
// The play routine's FL stack words (dwords from the return address): id, start minutes, seconds, ms.
constexpr unsigned play_arg_id = 1, play_arg_minutes = 2, play_arg_seconds = 3, play_arg_ms = 4;

struct Caller {
    std::uint32_t call_rva, return_rva;
    const char* name;
    std::uint32_t ap; // the AP return of the same role (the policy's key), 0 = none
};
// clang-format off
constexpr Caller stop_callers[] = {   // the six direct callers of stop-all 0x0b4210 (music-keep.md section 2)
    {0x0ec822, 0x0ec827, "alt_tab",       core::stop_callers[0].return_va}, // WndProc WM_ACTIVATE inactive arm
    {0x078a38, 0x078a3d, "save",          core::save_return_va},
    {0x07b7ca, 0x07b7cf, "pause",         core::pause_return_va}, // X2_SetPause native
    {0x079461, 0x079466, "load",          core::load_return_va},
    {0x0b3b31, 0x0b3b36, "p_leave",       core::stop_callers[4].return_va},
    {0x077be2, 0x077be7, "session_start", core::session_start_return_va},
};
constexpr Caller play_callers[] = {   // the four direct callers of play 0x0b4c50
    {0x0b593a, 0x0b593f, "MOV_PlayMovie",     core::play_callers[0].return_va},
    {0x0b5a4a, 0x0b5a4f, "MOV_PlayMovieFrom", core::play_callers[1].return_va},
    {0x11390c, 0x113911, "helper_0x113890",   core::play_callers[2].return_va},
    {0x066313, 0x066318, "selector_0x065030", 0},         // FL only: the sector selector plays directly
};
constexpr Caller stop_movie_callers[] = { // the two direct callers of MOV_StopMovie 0x0b47a0
    {0x0b598e, 0x0b5993, "MOV_StopMovie",     core::stop_movie_native_return_va},
    {0x113954, 0x113959, "helper_0x113890",   core::stop_movie_callers[2].return_va},
};
constexpr Caller free_callers[] = {   // the one direct caller of the free wrapper 0x0b4640 (MOV_FreeMovie)
    {0x0b5975, 0x0b597a, "MOV_FreeMovie", 0},
};
// clang-format on
// Every AP key is read from AP's own tables in music_keep_core.h; these asserts pin each role to the AP entry of the same
// name and mode, so a change in AP's caller tables fails to compile here instead of mapping an FL caller to vanilla.
constexpr bool same_name(const char* a, const char* b) {
    while (*a && *a == *b) ++a, ++b;
    return *a == *b;
}
constexpr bool stop_role_is(unsigned fl, unsigned ap, std::uint32_t return_va, core::StopMode mode) {
    return stop_callers[fl].ap == core::stop_callers[ap].return_va && core::stop_callers[ap].return_va == return_va &&
           core::stop_callers[ap].mode == mode && same_name(stop_callers[fl].name, core::stop_callers[ap].name);
}
static_assert(core::stop_caller_count == 6 && core::play_caller_count == 3 && core::stop_movie_caller_count == 3,
              "AP's caller tables");
static_assert(stop_role_is(0, 0, 0x004d36c2, core::StopMode::skip_all) &&
                  stop_role_is(1, 1, core::save_return_va, core::StopMode::keep_running) &&
                  stop_role_is(2, 2, core::pause_return_va, core::StopMode::keep_running) &&
                  stop_role_is(3, 3, core::load_return_va, core::StopMode::vanilla) &&
                  stop_role_is(4, 4, 0x00497bbb, core::StopMode::vanilla) &&
                  stop_role_is(5, 5, core::session_start_return_va, core::StopMode::vanilla),
              "the AP stop-all role keys");
static_assert(play_callers[0].ap == 0x00499824 && same_name(core::play_callers[0].name, "MOV_PlayMovie") &&
                  play_callers[1].ap == 0x00499987 && same_name(core::play_callers[1].name, "MOV_PlayMovieFrom") &&
                  play_callers[2].ap == 0x004f666d && same_name(core::play_callers[2].name, "helper_0x004f6640"),
              "the AP play role keys");
static_assert(stop_movie_callers[0].ap == 0x00499886 && core::stop_movie_callers[0].return_va == 0x00499886 &&
                  same_name(core::stop_movie_callers[0].name, "MOV_StopMovie") &&
                  stop_movie_callers[1].ap == 0x004f66c3 && same_name(core::stop_movie_callers[2].name, "caller_0x004f66be"),
              "the AP MOV_StopMovie role keys");
template <unsigned N> constexpr const Caller* by_return(const Caller (&table)[N], std::uint32_t ret, std::uintptr_t base) {
    for (const Caller& c : table)
        if (ret == std::uint32_t(base + c.return_rva)) return &c;
    return nullptr;
}
template <unsigned N> constexpr std::uint32_t ap_role(const Caller (&table)[N], std::uint32_t ret, std::uintptr_t base) {
    const Caller* c = by_return(table, ret, base);
    return c ? c->ap : 0u;
}
template <unsigned N> constexpr const char* role_name(const Caller (&table)[N], std::uint32_t ret, std::uintptr_t base) {
    const Caller* c = by_return(table, ret, base);
    return c ? c->name : "unknown";
}
}
