#pragma once
#include "music_keep_core.h"
#include <cstdint>

// Sector-music keep and trace (docs/reverse-engineering/music-restart.md,
// "Implementation"; ledger docs/verification/music-keep.md). Both are off
// unless their variable is exactly `1`; each fails closed to vanilla on any
// byte mismatch, closed install window or failed claim, and each rolls its
// own partial install back. They are independent: a trace-only flight runs
// with X3M_MUSIC_TRACE=1 alone, and either may install first beside the other
// (the shared entry sites below are claimed by the first and chained by the
// second; the second's byte windows see through the first's live claim).
//
// X3M_MUSIC_KEEP=1 (two claims, both inside the engine_patch install window):
//   A  0x004982db  six-byte trampoline inside the stop-all 0x004982b0 (two whole
//      instructions `mov edi,[esi+0x24]; cmp [edi+4],ebx`). The stub classifies
//      the stop-all's caller by its return address ([esp+0x10] at the site). Alt-tab
//      (skip_all): a music record is left untouched by jumping to the loop's continue
//      0x00498359 (no Pause/Stop, flag 2 kept, no completion, context kept), so the
//      script is not woken, never replays, and the pump keeps servicing the record
//      (the main loop runs through an alt-tab, Run 72 B); the record is held with the
//      skip_all kind for Patch D. Only with Patch D live; else paused. Save and pause
//      (keep_running) skip only the IMediaControl::Pause / DirectSound
//      Stop of a music record by jumping to the engine's own bookkeeping label
//      0x00498322 (flag 2 cleared, completion (ctx,1), context cleared, as vanilla);
//      paused and every other caller replay the displaced instructions
//      through the claim tail. A music record that passed save or pause is
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
//   D  0x004983d9  `call 0x004d14e0` (the media update's status query, its only
//      caller) redirected to a thunk: for a music record holding a skip_all hold,
//      while the engine's active flag [0x00608adc] is 0 and the RunInBackground
//      bit 0x4000 of [*0x00606f3c] is clear (the query would answer "ended"
//      without looking and the update would end the record), answer 1 = playing;
//      every other call is forwarded unchanged. Optional: when D's windows or
//      claim fail, the keep installs without it and alt-tab takes mode paused
//      (logged `status_gate=<reason> alt_tab_mode=paused`). With the keep armed,
//      Present samples the active flag and logs each change (music_keep_active).
//   plus two shared entry sites (claimed once, stubs chained per feature):
//   0x004982b0  stop-all entry: a keep-running hold the script never replayed
//      (still linked, flag 2 clear, context 0) is paused with 0x004d1810 on any
//      stop-all, so a track never plays on through an alt-tab or a load;
//   0x00498810  MOV_StopMovie native (EAX = id): the id's holds are dropped, so
//      a script stop followed by a replay restarts from 0 as vanilla does.
//   Music through the save (music-restart.md section 7): with the save pump bound
//      (Patch E live, the gzwrite import row of loading_trace live on IAT slot
//      0x005323e0) the save's stop-all keeps a DirectSound-path music record running
//      (keep_running) and arms one pump target (record, id, media, thread); while the
//      save writes, the gzwrite observer calls the engine's pump 0x004d0700 (EAX =
//      media) at most every 250 ms after checking the record (linked, same id and
//      media, music + 0x40, flag 2 clear, [m+0x8c] & 0x48 == 0x40, buffer state 2/4);
//      pumping stops at the save's own gzclose (wrapper 0x004e9360 called from
//      0x00404a6f), a 0/2 return, a refused check or 120 s. Every early stop (all but
//      the save's gzclose followed by the replay within 2.5 s of the last pump) pauses
//      the record through 0x004d1810: the flown paused mode, resumed by the replay.
//   E  0x00498d71  `call 0x004d1870` (the play routine's Run) redirected to a thunk:
//      right after Patch C skipped the replay's seek of that record, and with its
//      buffer still playing, answer 1 without the Run (no re-arm: the ring lead
//      survives); every other call goes to 0x004d1870. The episode ends at the first
//      music play, MOV_StopMovie of the id or the next stop-all; one
//      music_keep_save_pump line per save. Optional: when E's windows or claim fail
//      or the import row is not bound, the save keeps the paused mode.
//   Resume after combat (music-script-driver.md section 4): at MOV_StopMovie (native
//      caller 0x00499886) of a playing music record with a non-combat id (not
//      1001..1042) the keep remembers (id, position ms), read as the engine's reader
//      0x004d0600 does; combat plays keep it; the first start-0 play of that id is
//      seeked to the position (Patch C rewrites the seek's start argument); another
//      non-combat id, a vanilla stop-all or the one use clears it (music_keep_resume).
//   Track queue (X3M_MUSIC_QUEUE=1, only with the keep; docs/architecture/music-queue.md,
//      music-script-driver.md section 8.3 as amended by 8.4): at the MOV_StopMovie entry a stop
//      from PlayMusicWithFade (task IP 0x0db54) of a playing non-combat music record, with the
//      LoopMusic-from-ChangeTrackMusic frame cells and the alert at 0/0, re-keys the record to a
//      private id, keeps it playing, delivers the two vanilla completions itself and returns
//      (ORPHANED); the new task's first MOV_PlayMovie at the shared play entry 0x00498c90 is
//      consumed and the old record's completion context becomes that task (PARKED), so the
//      engine's own natural end wakes it and its KC replay plays the new id through vanilla,
//      where the orphan is freed (0x004986b0). A further deferrable stop collapses to the
//      newest id; combat, any other music stop or play (any caller of 0x00498c90), a stop-all
//      other than alt-tab under skip_all (alt-tab under paused frees at the next Present) and an
//      alert edge at Present end the queue (wake, free, vanilla). The prerequisite KC patch
//      (150::WarpToSector's StopMusic(500) fork at CODE 0x161c3 -> jmp, build 1462141839) is
//      maintained from Present and re-applied after every completed story reload (the load gate:
//      KcPatch in music_keep_core.h).
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
bool initialize();   // backend-load path only; logs one music_keep line and one music_trace line when the variables are
                     // set
bool shutdown();     // restores every live site (dynamic-unload detach only); true when nothing is installed
const char* state(); // keep: ok / disabled / <refusal>
const char* trace_state(); // trace: ok / disabled / <refusal>
// Stores the Present frame counter for the lines (no other per-frame work).
void present(unsigned long long device, unsigned long long frame, bool captured);
// The save_load_complete marker (Present path): the track queue's KC patch may be written at the next Present's check.
void loaded();
// After loading_trace::initialize(): one music_keep_save_pump_hook row (the save pump bound, or its refusal) when
// X3M_MUSIC_KEEP is set.
void save_pump_report();
}
extern "C" {
// Called by the Patch A stub: ESI = record, the stop-all's return address. Returns the StopMode as unsigned (1 =
// keep_running: jump to 0x00498322; 3 = skip_all: jump to 0x00498359).
unsigned __cdecl x3m_music_keep_stop_all(std::uint32_t record, std::uint32_t return_va);
// Called by the Patch D thunk: ECX = record. Returns 1 to answer "playing" (the thunk returns AX = 1), 0 to forward to
// 0x004d14e0.
unsigned __cdecl x3m_music_keep_status(std::uint32_t record);
// Called by the Patch C thunk: EAX = record, [esp+4] = start ms. Returns the SeekAction (0 vanilla, 1 skip, 2 pause
// x3m_music_keep_pause_record then vanilla), plus seek_resume_bit (4) when the thunk replaces the start argument with
// x3m_music_keep_resume_ms before the seek.
unsigned __cdecl x3m_music_keep_seek(std::uint32_t record, std::int32_t start_ms);
// Called by the Patch E thunk: EAX = record. Returns 1 to answer the Run with 1 without calling it, 0 to forward.
unsigned __cdecl x3m_music_keep_run(std::uint32_t record);
// The save-write observer (loading_trace's gzwrite / gzclose rows, while x3m_music_keep_save_pumping is nonzero).
void __cdecl x3m_music_keep_gz_write();
void __cdecl x3m_music_keep_gz_close(std::uint32_t return_va, std::uint32_t caller_return_va);
// Integer-only shim: calls the engine pump 0x004d0700 with EAX = media and returns AX zero-extended.
std::uint32_t __cdecl x3m_music_keep_call_pump(std::uint32_t media);
// Integer-only shim: calls the engine's pause 0x004d1810 with EAX = record (the save pump's fallback).
std::uint32_t __cdecl x3m_music_keep_call_pause(std::uint32_t record);
extern volatile std::uint32_t x3m_music_keep_save_pumping; // 1 while the save pump may run
extern std::uint32_t x3m_music_keep_resume_ms;             // written by x3m_music_keep_seek before returning bit 4
// Block handlers; `block` points at the stub's saved-register block: [0] edx, [1] ecx, [2] eax, [3] eflags, [4] return
// address, [5..] stack arguments.
std::uint32_t __cdecl x3m_music_keep_stop_all_entry(const std::uint32_t* block); // next orphaned keep-running record to
                                                                                 // pause, 0 when none
// MOV_StopMovie entry: drops the holds of the id in EAX and runs the track queue (task = ESI, slot = EDI at the entry).
// Returns 1 when the queue consumed the stop (the stub returns to the caller without the engine's body), else 0.
unsigned __cdecl x3m_music_keep_stop_movie(const std::uint32_t* block, std::uint32_t task, std::uint32_t slot);
// The queue's play-entry handler (shared site 0x00498c90; block[5..7] = slot, task id, id; task = ESI): 1 = consume
// (the task suspends without a play; its wake is the orphan's natural end), 0 = vanilla.
unsigned __cdecl x3m_music_queue_play(const std::uint32_t* block, std::uint32_t task);
// Integer-only shims: a completion (context, status) through a slot-table function (cdecl), and the engine's
// delete-by-id 0x004986b0 (ECX = key; returns 1 when a record was freed).
void __cdecl x3m_music_queue_call_completion(std::uint32_t function, std::uint32_t context, std::uint32_t status);
std::uint32_t __cdecl x3m_music_queue_call_free(std::uint32_t key);
void __cdecl x3m_music_trace_stop(const std::uint32_t* block);
void __cdecl x3m_music_trace_play(const std::uint32_t* block, std::uint32_t task);       // task = ESI at the entry
void __cdecl x3m_music_trace_stop_movie(const std::uint32_t* block, std::uint32_t task); // task = ESI at the entry
extern std::uint32_t x3m_music_keep_pause_record; // written by x3m_music_keep_seek before returning 2
}
