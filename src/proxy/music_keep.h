#pragma once
#include "music_keep_core.h"
#include <cstdint>

// Sector-music keep and trace (docs/reverse-engineering/music-restart.md,
// "Implementation"; ledger docs/verification/music-keep.md). Both are off
// unless their variable is exactly `1`; each fails closed to vanilla on any
// byte mismatch, closed install window or failed claim, and each rolls its
// own partial install back. They are independent: a trace-only flight runs
// with X3M_MUSIC_TRACE=1 alone.
//
// X3M_MUSIC_KEEP=1 (two claims, both inside the engine_patch install window):
//   A  0x004982db  six-byte trampoline inside the stop-all 0x004982b0 (two whole
//      instructions `mov edi,[esi+0x24]; cmp [edi+4],ebx`). The stub classifies
//      the stop-all's caller by its return address ([esp+0x10] at the site): save
//      and pause (keep_running) skip only the IMediaControl::Pause / DirectSound
//      Stop of a music record by jumping to the engine's own bookkeeping label
//      0x00498322 (flag 2 cleared, completion (ctx,1), context cleared, as vanilla);
//      alt-tab (paused) and every other caller replay the displaced instructions
//      through the claim tail. A music record that passed a preserve caller is
//      recorded as a hold (record, id, media, mode).
//      A DirectSound-path record (flag 0x40) is paused under save and pause
//      as well (the pump services flag-2 records only; a running buffer would
//      starve while the save's file write blocks the loop).
//   C  0x00498d54  `call 0x004d0430` (the play routine's seek) redirected to a
//      thunk. A music record, requested start 0, held or still playing: return 1
//      without seeking, so the following Run resumes (alt-tab) or is a no-op
//      (save, pause) and the track keeps its position. Another id while a
//      keep-running hold is still linked, unclaimed and not playing: the engine's
//      0x004d1810 pauses it first (else two tracks overlap), then the vanilla
//      seek. Everything else: the vanilla seek. Every music play drops the holds.
//   plus two shared entry sites (claimed once, stubs chained per feature):
//   0x004982b0  stop-all entry: a keep-running hold the script never replayed
//      (still linked, flag 2 clear, context 0) is paused with 0x004d1810 on any
//      stop-all, so a track never plays on through an alt-tab or a load;
//   0x00498810  MOV_StopMovie native (EAX = id): the id's holds are dropped, so
//      a script stop followed by a replay restarts from 0 as vanilla does.
// X3M_MUSIC_TRACE=1 (three entry trampolines): 0x004982b0 (stop-all; caller
//   return address), 0x00498c90 (play; id, start ms, caller) and 0x00498810
//   (MOV_StopMovie native; id, caller). One line each, written synchronously
//   through the session log's handle (like media_cue_enter) with the Present
//   frame counter, a sequence number and QPC; capped at 1,000 lines per session.
//   With the keep on, its own decisions add music_keep_stop / music_keep_seek
//   lines under the same cap.
//
// The stubs are integer-only assembly; the handlers run under
// LightCallBoundary (MXCSR + LastError) and hold no x87 code; the line
// formatter runs under call_preserved (FNSAVE/FRSTOR), indirectly, so the
// handlers stay in check_no_x87.py's audited graph. Threads: the stop-all
// runs on the WndProc/main-loop thread and the play inside the script step
// of the main loop; the hold table is touched only by those handlers.
namespace x3m::music_keep {
bool initialize();   // backend-load path only; logs one music_keep line and one music_trace line when the variables are set
bool shutdown();     // restores every live site (dynamic-unload detach only); true when nothing is installed
const char* state();          // keep: ok / disabled / <refusal>
const char* trace_state();    // trace: ok / disabled / <refusal>
// Stores the Present frame counter for the lines (no other per-frame work).
void present(unsigned long long device, unsigned long long frame, bool captured);
}
extern "C" {
// Called by the Patch A stub: ESI = record, the stop-all's return address. Returns the StopMode as unsigned (1 = keep_running: jump to 0x00498322).
unsigned __cdecl x3m_music_keep_stop_all(std::uint32_t record, std::uint32_t return_va);
// Called by the Patch C thunk: EAX = record, [esp+4] = start ms. Returns the SeekAction (0 vanilla, 1 skip, 2 pause x3m_music_keep_pause_record then vanilla).
unsigned __cdecl x3m_music_keep_seek(std::uint32_t record, std::int32_t start_ms);
// Block handlers; `block` points at the stub's saved-register block: [0] edx, [1] ecx, [2] eax, [3] eflags, [4] return address, [5..] stack arguments.
std::uint32_t __cdecl x3m_music_keep_stop_all_entry(const std::uint32_t* block);   // next orphaned keep-running record to pause, 0 when none
void __cdecl x3m_music_keep_stop_movie(const std::uint32_t* block);               // drops the holds of the id in EAX
void __cdecl x3m_music_trace_stop(const std::uint32_t* block);
void __cdecl x3m_music_trace_play(const std::uint32_t* block);
void __cdecl x3m_music_trace_stop_movie(const std::uint32_t* block);
extern std::uint32_t x3m_music_keep_pause_record;   // written by x3m_music_keep_seek before returning 2
}
