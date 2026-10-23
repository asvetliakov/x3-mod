#pragma once
#include "story_detect.h"
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
constexpr unsigned record_id = 0x10, record_context = 0x14, record_slot = 0x18, record_media = 0x24,
                   record_flags = 0x2c;
constexpr std::uint32_t flag_loop = 1, flag_playing = 2, flag_directsound = 0x40, flag_music = 0x80;
constexpr unsigned list_walk_limit = 4096; // nodes visited at most (the engine's own walks are unbounded; a cut walk is
                                           // logged); walked only on a music play or stop

// ---- Patch A: the stop-all classifier, 0x004982db (two whole instructions) ----
constexpr std::uint32_t stop_all_va = 0x004982b0, stop_all_end_va = 0x00498367;
constexpr std::uint32_t a_site_va = 0x004982db, a_next_va = 0x004982e1, a_skip_va = 0x00498322;
constexpr unsigned a_site_length = 6, a_return_slot = 0x10; // four pushes since entry: the caller's return address is
                                                            // at [esp+0x10]
// From the second push: push ebp; mov ebp,[eax]; cmp [ebp],ebx; je; push esi; push edi; mov edi,edi; mov esi,ebp;
// test [esi+0x2c],2; mov ebp,[ebp]; je; <site>; je; mov eax,[edi+0x74]. With `push ebx` at 0x004982b5 (stop_all_head)
// these are the four pushes that put the caller's return address at [esp+0x10] at the site.
constexpr std::uint32_t a_window_va = 0x004982c0;
constexpr unsigned a_window_length = 38, a_site_offset = a_site_va - a_window_va;
constexpr unsigned char a_window[a_window_length] = {
    0x55, 0x8b, 0x28, 0x39, 0x5d, 0x00, 0x0f, 0x84, 0x98, 0x00, 0x00, 0x00, 0x56, 0x57, 0x8b, 0xff, 0x8b, 0xf5, 0xf6,
    0x46, 0x2c, 0x02, 0x8b, 0x6d, 0x00, 0x74, 0x7e, 0x8b, 0x7e, 0x24, 0x39, 0x5f, 0x04, 0x74, 0x13, 0x8b, 0x47, 0x74};
constexpr unsigned a_skip_length = 15; // mov ecx,[0x6085e4]; and [esi+0x2c],~2; cmp ecx,ebx; mov eax,[esi+0x18]
constexpr unsigned char a_skip_window[a_skip_length] = {0x8b, 0x0d, 0xe4, 0x85, 0x60, 0x00, 0x83, 0x66,
                                                        0x2c, 0xfd, 0x3b, 0xcb, 0x8b, 0x46, 0x18};
// The per-record loop's continue: `cmp [ebp],ebx; jne 0x004982d0` then the epilogue (pop edi/esi/ebp/ebx; ret). It is
// the target of the loop's own `je 0x00498359` at 0x004982d9 (in a_window: `74 7e`), taken for a record without flag 2,
// so the skip_all exit reaches it in exactly that state: EBP = next node (loaded at 0x004982d6, before the site),
// EBX = 0, the stack four pushes deep; ESI/EDI/EAX/ECX/EDX and the flags are dead there (rewritten before any read).
constexpr std::uint32_t a_next_record_va = 0x00498359, a_loop_head_va = 0x004982d0, a_not_playing_je_va = 0x004982d9;
constexpr unsigned a_next_record_length = 14;
constexpr unsigned char a_next_record_window[a_next_record_length] = {0x39, 0x5d, 0x00, 0x0f, 0x85, 0x6e, 0xff,
                                                                      0xff, 0xff, 0x5f, 0x5e, 0x5d, 0x5b, 0xc3};

// ---- Patch C: the seek call of the play routine, 0x00498d54 -> 0x004d0430 ----
constexpr std::uint32_t play_va = 0x00498c90, play_end_va = 0x00498e28;
constexpr std::uint32_t c_site_va = 0x00498d54, c_target_va = 0x004d0430, c_return_va = 0x00498d59;
constexpr std::uint32_t c_pre_va = 0x00498d4d, c_post_va = 0x00498d59, c_run_call_va = 0x00498d71, run_va = 0x004d1870,
                        pause_va = 0x004d1810;
constexpr unsigned call_length = 5;
constexpr unsigned c_pre_length = 7; // add edi,[esp+0x2c]; mov eax,esi; push edi
constexpr unsigned char c_pre_window[c_pre_length] = {0x03, 0x7c, 0x24, 0x2c, 0x8b, 0xc6, 0x57};
constexpr unsigned c_post_length = 29; // add esp,4; test eax,eax; je; xor ebp,ebp; cmp ebx,ebp; jle; mov
                                       // eax,[esi+0x24]; mov [eax+0x94],ebx; mov eax,esi; call run
constexpr unsigned char c_post_window[c_post_length] = {0x83, 0xc4, 0x04, 0x85, 0xc0, 0x74, 0x7e, 0x33, 0xed, 0x3b,
                                                        0xdd, 0x7e, 0x09, 0x8b, 0x46, 0x24, 0x89, 0x98, 0x94, 0x00,
                                                        0x00, 0x00, 0x8b, 0xc6, 0xe8, 0xfa, 0x8a, 0x03, 0x00};
constexpr std::uint32_t play_set_playing_va = 0x00498d8c; // or ecx,2; or [esi+0x2c],ecx
constexpr unsigned play_set_playing_length = 6;
constexpr unsigned char play_set_playing_window[play_set_playing_length] = {0x83, 0xc9, 0x02, 0x09, 0x4e, 0x2c};
constexpr unsigned seek_head_length = 26; // 0x004d0430: push ebp; mov ebp,esp; and esp,-0x40; sub esp,0x34; push ebx;
                                          // push esi; mov esi,[eax+0x24]; ...
constexpr unsigned char seek_head[seek_head_length] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xc0, 0x83, 0xec, 0x34,
                                                       0x53, 0x56, 0x8b, 0x70, 0x24, 0xbb, 0x01, 0x00, 0x00,
                                                       0x00, 0x39, 0x5e, 0x48, 0x57, 0x8b, 0x7d, 0x08};
constexpr unsigned pause_body_length = 0x57; // 0x004d1810 whole: EAX = record; Pause + DirectSound Stop; clobbers
                                             // EAX/ECX/EDX only
// clang-format off
constexpr unsigned char pause_body[pause_body_length] = {
    0x56, 0x8b, 0x70, 0x24, 0x83, 0x7e, 0x04, 0x00, 0x74, 0x13, 0x8b, 0x46, 0x74, 0x85, 0xc0, 0x74,
    0x0c, 0x8b, 0x08, 0x8b, 0x51, 0x20, 0x50, 0xff, 0xd2, 0x85, 0xc0, 0x7c, 0x21, 0x8b, 0x86, 0x8c,
    0x00, 0x00, 0x00, 0xa8, 0x08, 0x75, 0x29, 0xa8, 0x40, 0x74, 0x25, 0x8b, 0x46, 0x44, 0x85, 0xc0,
    0x74, 0x10, 0x8b, 0x08, 0x8b, 0x51, 0x48, 0x50, 0xff, 0xd2, 0x85, 0xc0, 0x7d, 0x04, 0x33, 0xc0,
    0x5e, 0xc3, 0xc7, 0x46, 0x48, 0x01, 0x00, 0x00, 0x00, 0xc7, 0x46, 0x64, 0x01, 0x00, 0x00, 0x00,
    0xb8, 0x01, 0x00, 0x00, 0x00, 0x5e, 0xc3};
// clang-format on
constexpr unsigned run_head_length = 14; // 0x004d1870: sub esp,0x70; push ebx; mov ebx,[eax+0x24]; test byte
                                         // [ebx+0x8c],0x10
constexpr unsigned char run_head[run_head_length] = {0x83, 0xec, 0x70, 0x53, 0x8b, 0x58, 0x24,
                                                     0xf6, 0x83, 0x8c, 0x00, 0x00, 0x00, 0x10};

// ---- Patch D: the status query call of the media update, 0x004983d9 -> 0x004d14e0 ----
// The update 0x00498370 asks 0x004d14e0 (its only caller; ECX = record, EAX = [rec+0x28]; returns AX: 1 playing,
// 2 ended; preserves EBX/EBP/ESI/EDI; `ret`, no stack arguments) for every flag-2 record. While the engine's active
// flag [0x00608adc] is 0 and the RunInBackground bit 0x4000 of the input flags word [*0x00606f3c] is clear, the query
// answers 2 without looking (0x004d14f2..0x004d1500 -> 0x004d15a0), and 2 ends the record (0x0049842b: flag 2 cleared,
// completion (ctx,1); a looped record re-seeks at 0x0049840a). A skip_all record keeps flag 2 through the alt-tab, so
// the thunk answers 1 (playing: the caller only reads the position into a discarded local, 0x004983ed) for it in
// exactly that state and forwards every other call unchanged.
constexpr std::uint32_t d_site_va = 0x004983d9, d_target_va = 0x004d14e0, d_return_va = 0x004983de;
constexpr std::uint32_t active_flag_va = 0x00608adc, input_flags_ptr_va = 0x00606f3c, run_in_background_bit = 0x4000;
constexpr std::uint32_t d_caller_va = 0x004983d4, status_ended_va = 0x004d15a0, status_normal_va = 0x004d1506;
constexpr unsigned d_caller_length = 41; // mov eax,[edi+0x28]; mov ecx,edi; call; movzx eax,ax; cmp ax,1; jne; lea
                                         // esi,[esp+0x10]; mov eax,edi; call 0x4d0600; jmp; cmp ax,2; jne
constexpr unsigned char d_caller_window[d_caller_length] = {
    0x8b, 0x47, 0x28, 0x8b, 0xcf, 0xe8, 0x02, 0x91, 0x03, 0x00, 0x0f, 0xb7, 0xc0, 0x66,
    0x3d, 0x01, 0x00, 0x75, 0x10, 0x8d, 0x74, 0x24, 0x10, 0x8b, 0xc7, 0xe8, 0x0e, 0x82,
    0x03, 0x00, 0xe9, 0xbe, 0x00, 0x00, 0x00, 0x66, 0x3d, 0x02, 0x00, 0x75, 0x71};
constexpr unsigned status_head_length = 38; // sub esp,0x24; push ebx/ebp/esi; mov esi,[ecx+0x24]; xor ebp,ebp; cmp
                                            // [0x608adc],ebp; push edi; jne; mov edx,[0x606f3c]; test [edx],0x4000; je
                                            // 0x4d15a0
constexpr unsigned char status_head[status_head_length] = {
    0x83, 0xec, 0x24, 0x53, 0x55, 0x56, 0x8b, 0x71, 0x24, 0x33, 0xed, 0x39, 0x2d, 0xdc, 0x8a, 0x60, 0x00, 0x57, 0x75,
    0x12, 0x8b, 0x15, 0x3c, 0x6f, 0x60, 0x00, 0xf7, 0x02, 0x00, 0x40, 0x00, 0x00, 0x0f, 0x84, 0x9a, 0x00, 0x00, 0x00};
constexpr unsigned status_ended_length = 12; // mov ax,2; pop edi/esi/ebp/ebx; add esp,0x24; ret
constexpr unsigned char status_ended_window[status_ended_length] = {0x66, 0xb8, 0x02, 0x00, 0x5f, 0x5e,
                                                                    0x5d, 0x5b, 0x83, 0xc4, 0x24, 0xc3};

// ---- Music through the save (music-restart.md section 7): the save pump and Patch E ----
// The save's stop-all (caller save_return_va) keeps a DirectSound-path music record running (keep_running: the vanilla
// bookkeeping runs, the Pause and the buffer Stop are skipped), and the gzwrite import hook calls the engine's streaming
// pump 0x004d0700 on it while the save blocks the main loop. Pump contract (section 7.2): EAX = media object, no stack
// argument, plain `ret`; preserves EBX/EBP/ESI/EDI (pushed at +3..+6, popped on all exits), returns AX = 0 (failure),
// 1 (streaming), 2 (ended and stopped); no x87; its only engine entry is the status query's DirectSound tail
// (0x004d17b5: [m+0x8c] & 8 clear and & 0x40 set, then EAX = ESI = media, `jmp 0x004d0700`).
constexpr std::uint32_t save_return_va = 0x00404561; // the save routine's stop-all call returns here (stop_callers)
constexpr std::uint32_t pump_va = 0x004d0700, pump_end_va = 0x004d0c0c, status_ds_tail_va = 0x004d17b5;
constexpr unsigned pump_head_length = 55; // sub esp,0x44; push ebx/ebp/esi/edi; mov esi,eax; mov edi,1; the re-arm
                                          // ([esi+0x48]==1 && [esi+0x64]==1: Stop, cursors 0, SetCurrentPosition(0))
constexpr unsigned char pump_head[pump_head_length] = {
    0x83, 0xec, 0x44, 0x53, 0x55, 0x56, 0x57, 0x8b, 0xf0, 0xbf, 0x01, 0x00, 0x00, 0x00, 0x39, 0x7e, 0x48, 0x75, 0x24,
    0x39, 0x7e, 0x64, 0x75, 0x1f, 0x8b, 0x46, 0x44, 0x8b, 0x08, 0x8b, 0x51, 0x48, 0x50, 0xff, 0xd2, 0x8b, 0x46, 0x44,
    0x33, 0xc9, 0x89, 0x4e, 0x4c, 0x89, 0x4e, 0x50, 0x8b, 0x10, 0x51, 0x50, 0x8b, 0x42, 0x34, 0xff, 0xd0};
constexpr unsigned pump_end_length = 37; // the drain end: Stop, [esi+0x48]=1, wpos 0, pop edi/esi/ebp, mov ax,2; pop
                                         // ebx; add esp,0x44; ret
constexpr unsigned char pump_end[pump_end_length] = {
    0x8b, 0x46, 0x44, 0x8b, 0x08, 0x8b, 0x51, 0x48, 0x50, 0xff, 0xd2, 0x5f, 0xc7, 0x46, 0x48, 0x01, 0x00, 0x00, 0x00,
    0xc7, 0x46, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x5e, 0x5d, 0x66, 0xb8, 0x02, 0x00, 0x5b, 0x83, 0xc4, 0x44, 0xc3};
constexpr unsigned status_ds_tail_length = 28; // mov eax,[esi+0x8c]; test al,8; jne; test al,0x40; je; mov eax,esi; pops;
                                               // add esp,0x24; jmp 0x004d0700
constexpr unsigned char status_ds_tail[status_ds_tail_length] = {
    0x8b, 0x86, 0x8c, 0x00, 0x00, 0x00, 0xa8, 0x08, 0x75, 0x45, 0xa8, 0x40, 0x74, 0x0e,
    0x8b, 0xc6, 0x5f, 0x5e, 0x5d, 0x5b, 0x83, 0xc4, 0x24, 0xe9, 0x2f, 0xef, 0xff, 0xff};
// Media-object fields the pump gate reads (section 7.1): +0x8c flags (0x40 DirectSound path, 8 excluded, 0x20 stream
// clock), +0x48 buffer state (1 stopped / needs first fill, 2 playing, 4 draining), +0x04 the multimedia stream,
// +0x70 the position interface.
constexpr unsigned media_stream = 0x04, media_buffer_state = 0x48, media_position = 0x70, media_flags = 0x8c;
constexpr std::uint32_t media_ds_mask = 0x48, media_ds_value = 0x40, media_stream_clock = 0x20;
constexpr std::uint32_t buffer_playing = 2, buffer_draining = 4;
// The import the save writes through: `jmp [0x005323e0]` at 0x004fae46 (zlib1.dll!gzwrite; 52 direct callers plus the
// dispatcher 0x004e92e0). The proxy's import writer (loading_trace) owns the slot; the keep only observes it.
constexpr std::uint32_t gzwrite_iat_va = 0x005323e0, gzwrite_thunk_va = 0x004fae46;
constexpr unsigned gzwrite_thunk_length = 6;
constexpr unsigned char gzwrite_thunk[gzwrite_thunk_length] = {0xff, 0x25, 0xe0, 0x23, 0x53, 0x00};
// The save's own gzclose: the save calls the close wrapper 0x004e9360 at 0x00404a6f (return 0x00404a74); the wrapper
// calls the gzclose thunk 0x004fae52 (`jmp [0x005323f0]`) at 0x004e937f (return 0x004e9384). At the import hook's entry
// [esp] = 0x004e9384, [esp+4] = the file, [esp+8] = 0x00404a74. Only that pair ends pumping; any other gzclose is ignored.
constexpr std::uint32_t save_gzclose_call_va = 0x00404a6f, save_gzclose_return_va = 0x00404a74;
constexpr std::uint32_t gzclose_wrapper_va = 0x004e9360, gzclose_wrapper_return_va = 0x004e9384;
constexpr std::uint32_t gzclose_thunk_va = 0x004fae52, gzclose_iat_va = 0x005323f0;
constexpr unsigned gzclose_wrapper_length = 39; // test [ecx+4],1; jne; xor al,al; ret; ...; mov eax,[ecx]; push eax; call
                                                // 0x004fae52; add esp,4; neg eax
constexpr unsigned char gzclose_wrapper[gzclose_wrapper_length] = {
    0xf6, 0x41, 0x04, 0x01, 0x75, 0x03, 0x32, 0xc0, 0xc3, 0x8b, 0x41, 0x04, 0x8b, 0xd0, 0x83, 0xe2, 0x05, 0x83, 0xe0, 0xfe,
    0x80, 0xfa, 0x05, 0x89, 0x41, 0x04, 0x75, 0x13, 0x8b, 0x01, 0x50, 0xe8, 0xce, 0x1a, 0x01, 0x00, 0x83, 0xc4, 0x04};
constexpr unsigned gzclose_thunk_length = 6;
constexpr unsigned char gzclose_thunk[gzclose_thunk_length] = {0xff, 0x25, 0xf0, 0x23, 0x53, 0x00};
// The replay's Run is skipped only when the last pump (or the arm) is less than run_skip_bound_ms of wall time old: the
// 5 s ring minus one 2 s sample leaves a 3 s minimum lead, less a margin. Past it, Patch E forwards the Run (the buffer
// re-arms), and an episode still waiting for its replay past it is paused through 0x004d1810 at the next Present.
constexpr unsigned run_skip_bound_ms = 2500;
// Cadence: at most one pump call per 250 ms of wall time; the clock is read every pump_poll_stride gzwrite calls (one
// load, one decrement and a branch otherwise); pumping stops on its own after pump_bound_ms.
constexpr unsigned pump_interval_ms = 250, pump_poll_stride = 32, pump_bound_ms = 120000;
// Patch E: the play routine's Run call at 0x00498d71 (`e8 fa 8a 03 00` = call 0x004d1870, one whole instruction
// between `mov eax,esi` at 0x00498d6f and `test eax,eax; je 0x00498dde`; & 7 = 1, one lock cmpxchg8b). In: EAX = ESI =
// record; the callee preserves EBX/EBP/ESI/EDI and returns 1 on this path; ECX/EDX/flags are dead after it (test eax;
// 0x00498d7a reloads EAX, xor ecx,ecx; EDX is loaded at 0x00498d96 / 0x00498dce before any read). The thunk answers
// `mov eax,1; ret` (no Run, no re-arm of the running buffer) for the record the save kept running, right after Patch C
// skipped its seek; every other call goes to 0x004d1870 unchanged. The second Run caller 0x00498f76 stays vanilla.
constexpr std::uint32_t e_site_va = c_run_call_va, e_target_va = run_va, e_return_va = 0x00498d76;
constexpr unsigned e_post_length = 35; // test eax,eax; je; mov eax,[esi+0x18]; xor ecx,ecx; cmp [esp+0x3c],ebp; mov
                                       // [esi+0x1c],edi; setne cl; mov [esi+0x20],ebx; or ecx,2; or [esi+0x2c],ecx;
                                       // cmp eax,ebp; je; mov edx,[esi+0x14]
constexpr unsigned char e_post_window[e_post_length] = {
    0x85, 0xc0, 0x74, 0x64, 0x8b, 0x46, 0x18, 0x33, 0xc9, 0x39, 0x6c, 0x24, 0x3c, 0x89, 0x7e, 0x1c, 0x0f, 0x95,
    0xc1, 0x89, 0x5e, 0x20, 0x83, 0xc9, 0x02, 0x09, 0x4e, 0x2c, 0x3b, 0xc5, 0x74, 0x34, 0x8b, 0x56, 0x14};

// ---- Resume the sector track after combat (music-script-driver.md section 4) ----
// The story stops (MOV_StopMovie, native caller 0x00499886) and frees the sector record when combat music starts and
// replays the sector id from 0 when the alert ends. At the shared MOV_StopMovie entry the keep remembers (id, position)
// of a playing music record stopped on the alert's rising edge; the first later start-0 play of that id at Patch C, with
// the alert off, is seeked to
// the position (the stack argument of 0x004d0430 rewritten in place: the path MOV_PlayMovieFrom takes; EDI, hence
// [rec+0x1c], keeps 0). The position is read as the engine's reader 0x004d0600 does: [m+0x8c] & 0x20 ->
// IMultiMediaStream::GetTime on [m+4] (vtbl +0x20, 100 ns units / 10000), else IMediaPosition::get_CurrentPosition on
// [m+0x70] (vtbl +0x24, REFTIME seconds * 1000.0 at 0x00565550). Combat is the alert, not an id list: 150::NotifyAlert
// runs synchronously inside the engine's commit of the cockpit alert record R = *0x00608504 ([R+4] committed level,
// [R+8] this frame's level), so the script's stop of the sector track at a combat start arrives with [R+4] = 0 and
// [R+8] != 0 (the rising edge, Run 189: 0/1), the combat track plays while the level is nonzero, and the sector track's
// replay after the falling edge (1/0) plays with both 0 (music-script-driver.md section 8.4).
constexpr std::uint32_t stop_movie_native_return_va = 0x00499886;
constexpr std::uint32_t position_reader_va = 0x004d0600;
constexpr unsigned position_reader_length = 120; // the whole reader through its last `ret` at 0x004d0677
// clang-format off
constexpr unsigned char position_reader[position_reader_length] = {
    0x8b, 0x40, 0x24, 0x83, 0xec, 0x0c, 0xf6, 0x80, 0x8c, 0x00, 0x00, 0x00, 0x20, 0x75, 0x2c, 0x8b,
    0x40, 0x70, 0x8b, 0x08, 0x8d, 0x14, 0x24, 0x52, 0x50, 0x8b, 0x41, 0x24, 0xff, 0xd0, 0x85, 0xc0,
    0x7c, 0x2c, 0xdd, 0x04, 0x24, 0xdc, 0x0d, 0x50, 0x55, 0x56, 0x00, 0xe8, 0xa0, 0xaf, 0x05, 0x00,
    0x89, 0x06, 0xb8, 0x01, 0x00, 0x00, 0x00, 0x83, 0xc4, 0x0c, 0xc3, 0x8b, 0x40, 0x04, 0x8b, 0x08,
    0x8d, 0x14, 0x24, 0x52, 0x50, 0x8b, 0x41, 0x20, 0xff, 0xd0, 0x85, 0xc0, 0x7d, 0x06, 0x33, 0xc0,
    0x83, 0xc4, 0x0c, 0xc3, 0x8b, 0x4c, 0x24, 0x04, 0x8b, 0x14, 0x24, 0x6a, 0x00, 0x68, 0x10, 0x27,
    0x00, 0x00, 0x51, 0x52, 0xe8, 0xf7, 0x87, 0x04, 0x00, 0x89, 0x06, 0x89, 0x54, 0x24, 0x04, 0xb8,
    0x01, 0x00, 0x00, 0x00, 0x83, 0xc4, 0x0c, 0xc3};
// clang-format on
constexpr unsigned position_vtbl_get_current = 0x24, stream_vtbl_get_time = 0x20;
constexpr std::uint32_t position_limit_ms = 24u * 3600u * 1000u; // a larger reading is refused (no memory)
// Patch C's answer: SeekAction in the low bits, plus seek_resume_bit when the start argument is replaced with the
// remembered position before the seek.
constexpr unsigned seek_resume_bit = 4;

// ---- Trace sites: three entry trampolines ----
constexpr std::uint32_t t_stop_all_site_va = stop_all_va; // mov eax,[0x606f44] (5 bytes, absolute operand)
constexpr unsigned t_stop_all_length = 5, stop_all_head_length = 16;
constexpr unsigned char stop_all_head[stop_all_head_length] = {0xa1, 0x44, 0x6f, 0x60, 0x00, 0x53, 0x33, 0xdb,
                                                               0x3b, 0xc3, 0x0f, 0x84, 0xa5, 0x00, 0x00, 0x00};
constexpr std::uint32_t t_play_site_va = play_va; // push ecx; push ebx; mov ebx,[esp+0x14] (6 bytes; ESP-relative,
                                                  // replayed at the entry ESP)
constexpr unsigned t_play_length = 6, play_head_length = 18;
constexpr unsigned char play_head[play_head_length] = {0x51, 0x53, 0x8b, 0x5c, 0x24, 0x14, 0x55, 0x8b, 0x6c,
                                                       0x24, 0x28, 0x56, 0x57, 0xbf, 0x02, 0x00, 0x00, 0x00};
constexpr std::uint32_t t_stop_movie_site_va = 0x00498810; // mov ecx,[0x606f44] (6 bytes); EAX = id, [esp+4] = context,
                                                           // EDI = slot
constexpr unsigned t_stop_movie_length = 6, stop_movie_head_length = 14;
constexpr unsigned char stop_movie_head[stop_movie_head_length] = {0x8b, 0x0d, 0x44, 0x6f, 0x60, 0x00, 0x55,
                                                                   0x8b, 0x6c, 0x24, 0x08, 0x56, 0x8b, 0x31};
// Trace of the KC task at T2/T3 (music-script-driver.md section 8.2): ESI = the calling task; +0x08 task id, +0x10
// stack capacity (cells), +0x14 cell stack base (5-byte cells: tag byte + dword), +0x18 stack index k, +0x1c IP. At the
// T3 stop from PlayMusicWithFade (IP 0x0db54) the cells k+6 (tag 3, return into LoopMusic), k+8 (the new id), k+10
// (loop) and k+12 (tag 3, return into ChangeTrackMusic's fork) are logged raw; read only when base != 0 and
// k + 12 < capacity <= trace_cell_capacity_limit, through the fault-free engine reader. R = *0x00608504 is the alert
// record ([R+4] committed level, [R+8] this frame's level).
constexpr unsigned task_id = 0x08, task_capacity = 0x10, task_cells = 0x14, task_index = 0x18, task_ip = 0x1c;
constexpr unsigned cell_size = 5, trace_cell_capacity_limit = 0x100000;
constexpr unsigned trace_cells[] = {6, 8, 10, 12};
constexpr std::uint32_t alert_record_ptr_va = 0x00608504;
// Play routine arguments at its entry frame (dwords from the return address): a1 slot, a2 context, a3 id, a4 minutes,
// a5 seconds, a6 ms, a7..a9 end, a10 loop.
constexpr unsigned play_arg_id = 3, play_arg_minutes = 4, play_arg_seconds = 5, play_arg_ms = 6;
constexpr unsigned trace_line_cap = 1000;
constexpr unsigned queue_line_cap = 200; // music_queue rows (transitions and refusals): a cap of their own, so trace rows never crowd them out

// ---- Stop-all callers (return addresses) and their policy ----
// skip_all: the record is left untouched (no Pause/Stop, flag 2 kept, no completion, context kept): the stub leaves for
// a_next_record_va. The alt-tab caller only: the main loop keeps running through it (Run 72 B), so the pump keeps
// servicing the record; save and pause block the loop, so they pause (keep_running / paused). skip_all needs Patch D
// (the false "ended" of the status query while the active flag is 0); without it alt-tab falls back to paused.
enum class StopMode : unsigned { vanilla = 0, keep_running = 1, paused = 2, skip_all = 3 };
struct StopCaller {
    std::uint32_t call_va, return_va;
    const char* name;
    StopMode mode;
};
// clang-format off
constexpr StopCaller stop_callers[] = {
    {0x004d36bd, 0x004d36c2, "alt_tab", StopMode::skip_all},        // WndProc WM_ACTIVATE inactive arm
    {0x0040455c, 0x00404561, "save", StopMode::keep_running},       // save routine 0x00404530, first statement
    {0x00407064, 0x00407069, "pause", StopMode::keep_running},      // X2_SetPause
    {0x00404ced, 0x00404cf2, "load", StopMode::vanilla},            // load 0x00404cc0
    {0x00497bb6, 0x00497bbb, "p_leave", StopMode::vanilla},         // P_Leave
    {0x00403878, 0x0040387d, "session_start", StopMode::vanilla},   // main-loop entry 0x00403840
};
// clang-format on
constexpr unsigned stop_caller_count = sizeof stop_callers / sizeof stop_callers[0];
struct PlayCaller {
    std::uint32_t call_va, return_va;
    const char* name;
};
constexpr PlayCaller play_callers[] = {
    {0x0049981f, 0x00499824, "MOV_PlayMovie"},
    {0x00499982, 0x00499987, "MOV_PlayMovieFrom"},
    {0x004f6668, 0x004f666d, "helper_0x004f6640"},
};
constexpr unsigned play_caller_count = sizeof play_callers / sizeof play_callers[0];
constexpr PlayCaller stop_movie_callers[] = {
    // the three direct callers of the MOV_StopMovie native 0x00498810 (EAX = id)
    {0x00499881, 0x00499886, "MOV_StopMovie"},
    {0x0045c27d, 0x0045c282, "selector_0x0045b720"}, // the per-sector object pass (station screens),
                                                     // sector-post-pass.md
    {0x004f66be, 0x004f66c3, "caller_0x004f66be"},
};
constexpr unsigned stop_movie_caller_count = sizeof stop_movie_callers / sizeof stop_movie_callers[0];
inline const StopCaller* stop_caller(std::uint32_t return_va) {
    for (const auto& c : stop_callers)
        if (c.return_va == return_va) return &c;
    return nullptr;
}
inline const char* stop_caller_name(std::uint32_t return_va) {
    const StopCaller* c = stop_caller(return_va);
    return c ? c->name : "unknown";
}
inline StopMode stop_mode(std::uint32_t return_va) {
    const StopCaller* c = stop_caller(return_va);
    return c ? c->mode : StopMode::vanilla;
}
inline const char* play_caller_name(std::uint32_t return_va) {
    for (const auto& c : play_callers)
        if (c.return_va == return_va) return c.name;
    return "unknown";
}
inline const char* stop_movie_caller_name(std::uint32_t return_va) {
    for (const auto& c : stop_movie_callers)
        if (c.return_va == return_va) return c.name;
    return "unknown";
}
inline const char* mode_name(StopMode m) {
    return m == StopMode::keep_running ? "keep_running"
           : m == StopMode::paused     ? "paused"
           : m == StopMode::skip_all   ? "skip_all"
                                       : "vanilla";
}

// ---- Holds ----
// A hold is a music record the classifier let through a preserve caller:
// (record, id, media) identify it without dereferencing the record later;
// mode says whether its graph is still running (keep_running) or paused.
struct Hold {
    std::uint32_t record, id, media;
    StopMode mode;
};
constexpr unsigned hold_capacity = 4;
struct Holds {
    Hold slots[hold_capacity]{};
    unsigned count = 0, next = 0; // next: the slot replaced when full (oldest first)
    void clear() {
        count = 0;
        next = 0;
    }
    const Hold* find(std::uint32_t record, std::uint32_t id, std::uint32_t media) const {
        for (unsigned i = 0; i < count; ++i)
            if (slots[i].record == record && slots[i].id == id && slots[i].media == media) return &slots[i];
        return nullptr;
    }
    // The fallback pause of a kept-running record: its hold becomes a paused one (F2 and pause_then_vanilla skip it).
    bool set_mode(std::uint32_t record, std::uint32_t id, std::uint32_t media, StopMode mode) {
        for (unsigned i = 0; i < count; ++i)
            if (slots[i].record == record && slots[i].id == id && slots[i].media == media) {
                slots[i].mode = mode;
                return true;
            }
        return false;
    }
    void add(const Hold& h) {
        for (unsigned i = 0; i < count; ++i)
            if (slots[i].record == h.record) {
                slots[i] = h;
                return;
            }
        if (count < hold_capacity) {
            slots[count++] = h;
            return;
        }
        slots[next] = h;
        next = (next + 1) % hold_capacity;
    }
};

// What a live-list lookup reports for a held record: its flags and completion context.
struct LiveRecord {
    std::uint32_t flags, context;
};

// Patch A decision. In: the record's flags, the stop-all caller's return
// address. Out: the mode; the stub falls through to the vanilla Pause on
// vanilla/paused, jumps to a_skip_va on keep_running and to a_next_record_va
// on skip_all. A vanilla caller (load, P_Leave, session start, unknown) drops
// every hold; a non-music record is never held and takes the vanilla path
// whatever the caller. A DirectSound-path record (flag 0x40) is paused, not
// kept running, under save and pause too: the pump services flag-2 records
// only and the save's file write blocks the loop, so a running buffer would
// starve. Alt-tab (skip_all, only when Patch D is live: `skip_all_available`):
// the record stays registered as playing with its completion context, the
// script is not woken and never replays; the record is held with the skip_all
// kind, which only Patch D reads (decide_status) and the seek rule ignores (a
// skip_all hold must not make the next start-0 play after the natural end skip
// the seek). Without Patch D alt-tab takes the flown paused mode.
// Music through the save (`save_pump_free`: the save pump is bound, Patch E is
// live and no other record is armed in this stop-all): a DirectSound-path music
// record under the save caller keeps running instead (`pump` set: the caller
// arms the save pump on it); pause keeps the paused mode.
struct StopDecision {
    StopMode mode;
    bool held;
    bool pump = false;
};
inline StopDecision decide_stop(Holds& holds, std::uint32_t record, std::uint32_t flags, std::uint32_t id,
                                std::uint32_t media, std::uint32_t return_va, bool skip_all_available,
                                bool save_pump_free = false) {
    StopMode mode = stop_mode(return_va);
    if (mode == StopMode::vanilla) {
        holds.clear();
        return {StopMode::vanilla, false};
    }
    if (!(flags & flag_music)) return {StopMode::vanilla, false};
    if (mode == StopMode::skip_all && !skip_all_available) mode = StopMode::paused;
    bool pump = false;
    if (mode == StopMode::keep_running && (flags & flag_directsound)) {
        if (save_pump_free && return_va == save_return_va)
            pump = true;
        else
            mode = StopMode::paused;
    }
    holds.add(Hold{record, id, media, mode});
    return {mode, true, pump};
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
template <class Lookup> inline std::uint32_t stale_hold_to_pause(Holds& holds, Lookup&& live) {
    for (unsigned i = 0; i < holds.count; ++i) {
        const Hold h = holds.slots[i];
        if (h.mode != StopMode::keep_running) continue;
        for (unsigned j = i + 1; j < holds.count; ++j) holds.slots[j - 1] = holds.slots[j];
        --holds.count;
        holds.next = 0;
        LiveRecord r{};
        if (live(h.record, h.id, h.media, r) && !(r.flags & flag_playing) && r.context == 0 && (r.flags & flag_music))
            return h.record;
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
        if (holds.slots[i].id != id) {
            ++i;
            continue;
        }
        for (unsigned j = i + 1; j < holds.count; ++j) holds.slots[j - 1] = holds.slots[j];
        --holds.count;
        holds.next = 0;
        ++dropped;
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
struct SeekDecision {
    SeekAction action;
    std::uint32_t pause_record;
    std::uint32_t hold_id;
};
template <class Lookup>
inline SeekDecision decide_seek(Holds& holds, std::uint32_t record, std::uint32_t flags, std::uint32_t id,
                                std::uint32_t media, std::int32_t start_ms, Lookup&& live) {
    if (!(flags & flag_music)) return {SeekAction::vanilla, 0, 0};
    const Hold* mine = holds.find(record, id, media);
    if (mine && mine->mode == StopMode::skip_all) mine = nullptr; // flag 2 alone decides for a skip_all record
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
// Patch D decision. In: the visited record's flags, id and media, the engine's
// active flag and the input flags word. True (answer 1 = playing, do not ask
// the engine) only for a music record still marked playing that holds a
// skip_all hold, while the active flag is 0 and the RunInBackground bit is
// clear: exactly the state in which the query would answer "ended" without
// looking. With the bit set the engine's own answer is genuine and forwarded.
inline bool decide_status(const Holds& holds, std::uint32_t record, std::uint32_t flags, std::uint32_t id,
                          std::uint32_t media, std::uint32_t active, std::uint32_t input_flags) {
    if (active != 0 || (input_flags & run_in_background_bit)) return false;
    if ((flags & (flag_music | flag_playing)) != (flag_music | flag_playing)) return false;
    const Hold* h = holds.find(record, id, media);
    return h && h->mode == StopMode::skip_all;
}

// ---- The save pump episode (music-restart.md section 7.5) ----
// One target per save: armed by Patch A for the DirectSound music record the
// save kept running; `open` until the episode's line is written; `pumping` while the pump may run (from that stop-all to
// gzclose, a failed or ended pump call, a refused check or the wall bound);
// `kept` while the replay's Run may be skipped (until the first music play);
// `run_marker` from Patch C's seek skip of exactly that record to the Run call
// that follows it (Patch E, one shot). The episode ends (`open` cleared) at the
// first music play (or at Patch E after the replay), at MOV_StopMovie of the id
// and at the next stop-all; the caller then writes one line from the fields
// (kept until reset) and resets. A stopped pump (gzclose, 0/2, a refused check,
// the bound) leaves the episode open for that line. Every early stop of the
// kept-running state (all but gzclose followed by a timely replay) cancels the
// Run skip and asks the caller to pause the record (cancel_skip / note_fallback).
enum class PumpEnd : unsigned {
    none = 0,
    gzclose,
    replay,
    play,
    stop_movie,
    stop_all,
    ended,
    failed,
    refused,
    bound,
    overdue, // gzclose came, the replay did not within run_skip_bound_ms
    stale,   // Patch E found the last pump older than run_skip_bound_ms
};
// What happened to a record the save kept running once the Run skip was cancelled early: paused through 0x004d1810 (the
// flown paused mode: the replay's seek skip and Run resume it), or nothing to pause (no longer linked as the target).
enum class Fallback : unsigned { none = 0, paused, unlinked, pending };
inline const char* fallback_name(Fallback f) {
    return f == Fallback::paused ? "paused" : f == Fallback::unlinked ? "unlinked" : f == Fallback::pending ? "pending" : "none";
}
inline const char* pump_end_name(PumpEnd e) {
    switch (e) {
    case PumpEnd::gzclose: return "gzclose";
    case PumpEnd::replay: return "replay";
    case PumpEnd::play: return "play";
    case PumpEnd::stop_movie: return "stop_movie";
    case PumpEnd::stop_all: return "stop_all";
    case PumpEnd::ended: return "ended";
    case PumpEnd::failed: return "failed";
    case PumpEnd::refused: return "refused";
    case PumpEnd::bound: return "bound";
    case PumpEnd::overdue: return "overdue";
    case PumpEnd::stale: return "stale";
    default: return "none";
    }
}
inline std::uint64_t ticks_to_ms(std::uint64_t ticks, std::uint64_t frequency) {
    return frequency ? ticks * 1000u / frequency : 0;
}
struct SavePump {
    std::uint32_t record = 0, id = 0, media = 0, thread = 0;
    bool open = false, pumping = false, kept = false, run_marker = false;
    std::uint64_t frequency = 0, interval = 0, bound = 0, skip_bound = 0;
    std::uint64_t armed_at = 0, first_call_at = 0, last_at = 0, pump_end_at = 0, end_at = 0, max_gap = 0;
    std::uint32_t writes = 0, samples = 0, calls = 0, returns[4]{}; // returns[3]: any other value (failure)
    unsigned countdown = 0;
    PumpEnd pump_end = PumpEnd::none, end = PumpEnd::none, fallback_reason = PumpEnd::none;
    Fallback fallback = Fallback::none;
    std::uint64_t fallback_at = 0;

    bool active() const { return open; }
    void reset() { *this = SavePump{}; }
    void arm(std::uint32_t r, std::uint32_t i, std::uint32_t m, std::uint32_t t, std::uint64_t now, std::uint64_t f) {
        reset();
        record = r;
        id = i;
        media = m;
        thread = t;
        frequency = f;
        interval = f * pump_interval_ms / 1000u;
        bound = f * pump_bound_ms / 1000u;
        skip_bound = f * run_skip_bound_ms / 1000u;
        armed_at = last_at = now;
        open = pumping = kept = true;
        countdown = 1; // the first gzwrite samples the clock (and pumps): the stop-all's lead is the reserve
    }
    // Every gzwrite while pumping: true when this call reads the clock.
    bool sample_due() {
        ++writes;
        if (--countdown) return false;
        countdown = pump_poll_stride;
        ++samples;
        return true;
    }
    void stop(PumpEnd why, std::uint64_t now) {
        if (!pumping) return;
        pumping = false;
        pump_end = why;
        pump_end_at = now;
        if (now - last_at > max_gap) max_gap = now - last_at; // the unpumped span up to the end
    }
    // On a sampled gzwrite: whether to call the pump now (on the armed thread, past the interval since the last call;
    // the first sample always pumps). Past the wall bound pumping stops.
    bool pump_due(std::uint64_t now, std::uint32_t current_thread) {
        if (!pumping || current_thread != thread) return false;
        if (now - armed_at >= bound) {
            cancel_skip(PumpEnd::bound, now);
            return false;
        }
        return calls == 0 || now - last_at >= interval;
    }
    // An early end of the kept-running state (a refused check, a 0/2/other return, the wall bound, an overdue replay):
    // pumping stops, the Run skip is cancelled and the record must be paused (Fallback::pending until the caller
    // reports what it did through note_fallback). Once per episode.
    void cancel_skip(PumpEnd why, std::uint64_t now) {
        stop(why, now);
        if (!kept) return;
        kept = false;
        fallback = Fallback::pending;
        fallback_reason = why;
        fallback_at = now;
    }
    bool fallback_due() const { return fallback == Fallback::pending; }
    void note_fallback(bool paused) { fallback = paused ? Fallback::paused : Fallback::unlinked; }
    // The checks before a call failed (record unlinked, other id or media, not DirectSound, flag 2 set, buffer not
    // running): no call, no Run skip.
    void refuse(std::uint64_t now) { cancel_skip(PumpEnd::refused, now); }
    // After gzclose: the replay has not come within run_skip_bound_ms of the last pump (checked at Present, on the
    // armed thread).
    bool replay_overdue(std::uint64_t now, std::uint32_t current_thread) const {
        return open && kept && !pumping && !run_marker && current_thread == thread && now - last_at >= skip_bound;
    }
    // After a call: 1 keeps streaming; 0, 2 or anything else stops pumping and the replay's Run stays vanilla (a 2:
    // the track ended during the save, the replay re-arms at end of stream and ends as after any natural end).
    void note_call(std::uint32_t result, std::uint64_t before, std::uint64_t after) {
        if (before - last_at > max_gap) max_gap = before - last_at;
        if (!calls) first_call_at = before;
        last_at = after;
        ++calls;
        ++returns[result <= 2 ? result : 3];
        if (result != 1) cancel_skip(result == 2 ? PumpEnd::ended : PumpEnd::failed, after);
    }
    // Patch C, any music play (record, id, media of the played record; `skipped`: the seek was skipped). Returns true
    // when the episode ends here (the caller logs and resets); false when nothing was active or the replay of the kept
    // record set the Run marker (Patch E logs).
    bool on_play(std::uint32_t r, std::uint32_t i, std::uint32_t m, bool skipped, std::uint64_t now) {
        if (!active()) return false;
        const bool replay = kept && skipped && r == record && i == id && m == media;
        stop(replay ? PumpEnd::replay : PumpEnd::play, now);
        kept = false;
        run_marker = replay;
        open = replay; // a replay stays open until Patch E
        end = replay ? PumpEnd::replay : PumpEnd::play;
        end_at = now;
        return !replay;
    }
    // Patch E (the Run call right after the seek): true = skip the Run. Only for the marked record whose buffer is
    // still playing and whose last pump (or arm) is younger than run_skip_bound_ms (else the ring may have run dry:
    // the vanilla Run re-arms it, recorded as PumpEnd::stale); the marker is consumed by any Run call.
    bool skip_run(std::uint32_t r, std::uint32_t m, std::uint32_t buffer_state, std::uint64_t now) {
        const bool mine = run_marker && r == record && m == media;
        if (run_marker) open = false;
        run_marker = false;
        if (!mine || buffer_state != buffer_playing) return false;
        if (now - last_at >= skip_bound) {
            fallback_reason = PumpEnd::stale;
            return false;
        }
        return true;
    }
    // MOV_StopMovie of the id, or the next stop-all entry: the episode ends (true when one was active).
    bool end_now(PumpEnd why, std::uint64_t now) {
        if (!active()) return false;
        stop(why, now);
        open = kept = run_marker = false;
        end = why;
        end_at = now;
        return true;
    }
};

// ---- The resume memory (music-script-driver.md section 4) ----
// The alert record as a handler reads it: `ok` false when R or its levels are unreadable.
struct Alert {
    bool ok = false;
    std::uint32_t committed = 0, current = 0; // [R+4], [R+8]
};
// The combat start: NotifyAlert(1) inside the commit (committed 0, this frame's level nonzero).
inline bool alert_rising(const Alert& a) {
    return a.ok && a.committed == 0 && a.current != 0;
}
// A combat track's play: the alert is on (either level nonzero).
inline bool alert_on(const Alert& a) {
    return a.ok && (a.committed != 0 || a.current != 0);
}
struct ResumeMemory {
    std::uint32_t id = 0, ms = 0;
    bool valid = false;
};
enum class ResumeAction : unsigned {
    none = 0,
    remember,       // a sector track stopped by the script: (id, position) kept
    forget,         // a sector track stopped without a readable position: the old memory goes
    keep,           // a music play while the alert is on (a combat track): the memory stays
    seek,           // the first start-0 play of the remembered id: seek to the position, memory used
    clear_sector,   // another non-combat id plays (sector change, dock): memory cleared
    clear_played,   // the remembered id plays positioned or still playing (seek skipped): memory cleared
    clear_stop_all, // a vanilla stop-all (load, session start, P_Leave, unknown): memory cleared
};
inline const char* resume_action_name(ResumeAction a) {
    switch (a) {
    case ResumeAction::remember: return "remember";
    case ResumeAction::forget: return "forget";
    case ResumeAction::keep: return "keep";
    case ResumeAction::seek: return "seek";
    case ResumeAction::clear_sector: return "clear_sector";
    case ResumeAction::clear_played: return "clear_played";
    case ResumeAction::clear_stop_all: return "clear_stop_all";
    default: return "none";
    }
}
// MOV_StopMovie native entry: `caller` the native's return address, the found record's flags (0 when absent),
// `position_ok`/`ms` the reading taken before the engine pauses it, `a` the alert at the stop. Only the script's own
// stop (caller stop_movie_native_return_va) of a playing music record on the alert's rising edge counts.
inline ResumeAction resume_on_stop(ResumeMemory& m, std::uint32_t caller, std::uint32_t id, std::uint32_t flags,
                                   bool position_ok, std::uint32_t ms, const Alert& a) {
    if (caller != stop_movie_native_return_va || !alert_rising(a)) return ResumeAction::none;
    if ((flags & (flag_music | flag_playing)) != (flag_music | flag_playing)) return ResumeAction::none;
    if (!position_ok || ms == 0 || ms >= position_limit_ms) {
        m = ResumeMemory{};
        return ResumeAction::forget;
    }
    m = ResumeMemory{id, ms, true};
    return ResumeAction::remember;
}
// Patch C, a music play (after decide_seek: `base` its action; `a` the alert at the play). Out: `seek_ms` for
// ResumeAction::seek.
inline ResumeAction resume_on_play(ResumeMemory& m, std::uint32_t id, std::int32_t start_ms, SeekAction base,
                                   std::uint32_t& seek_ms, const Alert& a) {
    if (!m.valid) return ResumeAction::none;
    if (alert_on(a)) return ResumeAction::keep;
    if (id != m.id) {
        m = ResumeMemory{};
        return ResumeAction::clear_sector;
    }
    if (start_ms == 0 && base != SeekAction::skip) {
        seek_ms = m.ms;
        m = ResumeMemory{};
        return ResumeAction::seek;
    }
    m = ResumeMemory{};
    return ResumeAction::clear_played;
}
// Stop-all entry: a vanilla caller (load, session start, P_Leave, unknown) clears the memory; save, pause and alt-tab
// keep it.
inline ResumeAction resume_on_stop_all(ResumeMemory& m, std::uint32_t return_va) {
    if (!m.valid || stop_mode(return_va) != StopMode::vanilla) return ResumeAction::none;
    m = ResumeMemory{};
    return ResumeAction::clear_stop_all;
}

// ---- The track queue (music-script-driver.md section 8.3 as amended by section 8.4; docs/architecture/music-queue.md) ----
// X3M_MUSIC_QUEUE=1 under the keep: when the story's new track task C stops the playing sector track at the T3 site from
// PlayMusicWithFade (KC IP 0x0db54), the old record O is re-keyed to a private id P, keeps flag 2 and is not paused; T_old
// and C get the two vanilla completions (ORPHANED). C's first MOV_PlayMovie(N) at T2 is consumed: O's completion context
// and slot become C's (PARKED), so the engine's own natural end of O wakes C, whose KC replay plays N through the vanilla
// path; that replay frees P (IDLE). A further deferrable stop of N while PARKED collapses to the newest id. Every other
// music stop of N, any stop-all but alt-tab under the keep's skip_all, an alert edge and any other music play (every
// caller of the play entry) end the queue at once: C is woken through O's slot and P is freed, then vanilla continues.
// Script owner / VM `*0x006085e4` (gate-queue-script.md section 1, script-task-scheduler.md): +0x08 the CODE buffer
// (0x0049d34c), +0x10 its size (0x0049d332), +0x143c the story build id (0x0049d2a7), +0x04 the slot table guard and
// +0x34 + 0x18 * slot the completion function (the MOV_StopMovie body 0x00498864..0x0049888e, cdecl (ctx, status), slot < 32).
constexpr std::uint32_t vm_ptr_va = 0x006085e4;
constexpr unsigned vm_slot_guard = 0x04, vm_code = 0x08, vm_code_size = 0x10, vm_build = 0x143c;
constexpr unsigned slot_stride = 0x18, slot_completion = 0x34, slot_count = 0x20;
constexpr std::uint32_t completion_window_va = 0x00498864;
constexpr unsigned completion_window_length = 42; // mov eax,[0x6085e4]; test eax,eax; mov ecx,[esi+0x18]; je; cmp [eax+4],0; je;
                                                  // cmp ecx,0x20; jge; lea edx,[ecx*3]; mov ecx,[eax+edx*8+0x34]; test; je;
                                                  // mov eax,[esi+0x14]; push 1; push eax; call ecx
constexpr unsigned char completion_window[completion_window_length] = {
    0xa1, 0xe4, 0x85, 0x60, 0x00, 0x85, 0xc0, 0x8b, 0x4e, 0x18, 0x74, 0x26, 0x83, 0x78, 0x04, 0x00, 0x74, 0x20, 0x83, 0xf9, 0x20,
    0x7d, 0x1b, 0x8d, 0x14, 0x49, 0x8b, 0x4c, 0xd0, 0x34, 0x85, 0xc9, 0x74, 0x10, 0x8b, 0x46, 0x14, 0x6a, 0x01, 0x50, 0xff, 0xd1};
// The engine's delete-by-id 0x004986b0 (MOV_FreeMovie's body, its only caller 0x004986f0): ECX = id; frees the first
// linked record with that id and media != 0 (0x004984d0: teardown, unlink, free) and returns EAX 1, else 0; pushes and
// pops ESI, clobbers EAX/ECX/EDX only. The queue's one free, called only from T2/T3, from T1 for the non-alt-tab callers
// and from Present, never inside the manager's walk.
constexpr std::uint32_t free_by_id_va = 0x004986b0, free_by_id_caller_va = 0x004986f0;
constexpr unsigned free_by_id_length = 51;
// clang-format off
constexpr unsigned char free_by_id_body[free_by_id_length] = {
    0xa1, 0x44, 0x6f, 0x60, 0x00, 0x56, 0x8b, 0x30, 0x8b, 0x06, 0x85, 0xc0, 0x74, 0x15, 0x8b, 0xff,
    0x83, 0x7e, 0x24, 0x00, 0x74, 0x05, 0x39, 0x4e, 0x10, 0x74, 0x0c, 0x8b, 0xf0, 0x8b, 0x06, 0x85,
    0xc0, 0x75, 0xed, 0x33, 0xc0, 0x5e, 0xc3, 0xe8, 0xf4, 0xfd, 0xff, 0xff, 0xb8, 0x01, 0x00, 0x00,
    0x00, 0x5e, 0xc3};
// clang-format on
// KC layout witnesses compared at install: the jmp handler 0x004a317a (`mov edi,[edi]; mov edx,[esp+0x18]; add
// edi,[edx+8]`: a jump operand is an absolute CODE offset added to [VM+8]), the loader's CODE size and buffer stores
// 0x0049d332..0x0049d34e (`mov [ebx+0x10],eax` ... `mov [ebx+8],eax`) and its build id store 0x0049d2a7.
constexpr std::uint32_t kc_jump_handler_va = 0x004a317a, kc_loader_code_store_va = 0x0049d332,
                        kc_loader_build_store_va = 0x0049d2a7;
constexpr unsigned kc_jump_handler_length = 9, kc_loader_code_store_length = 29, kc_loader_build_store_length = 6;
constexpr unsigned char kc_jump_handler[kc_jump_handler_length] = {0x8b, 0x3f, 0x8b, 0x54, 0x24, 0x18, 0x03, 0x7a, 0x08};
constexpr unsigned char kc_loader_code_store[kc_loader_code_store_length] = {
    0x89, 0x43, 0x10, 0x0f, 0x87, 0xe8, 0x0b, 0x00, 0x00, 0x53, 0xbf, 0x0c, 0x00, 0x00, 0x00,
    0xe8, 0xca, 0xf5, 0xff, 0xff, 0x8b, 0x4b, 0x10, 0x51, 0x55, 0x50, 0x89, 0x43, 0x08};
constexpr unsigned char kc_loader_build_store[kc_loader_build_store_length] = {0x89, 0x83, 0x3c, 0x14, 0x00, 0x00};
// The KC patch (section 8.4 "Prerequisite"): 150::WarpToSector forks a child that fades the old track out at the gate
// (StopMusic(500), MusicFadeOut's stop). The window is detected at run time in the loaded object (story_detect.h
// detect_queue; Mayhem CODE 0x161c3 -> 0x161d7, stock AP 0x1639c -> 0x163b0): its 6 bytes `op6f; jt <continue>` become
// `jmp <continue>` plus one filler byte that is never reached (push0, the padding the gate-queue options use). Jump
// operands are byte-swapped to little endian at load (0x0049e1a0). Detected once per VM key after the load gate opened;
// applied (and re-applied after every story reload, which happens on each save load) from Present when the window
// holds exactly the detected vanilla bytes; any other content refuses with the bytes seen. An object where the sites
// are not found (x3config, x3intro, a story whose signatures do not match uniquely) is no_sites, naming the method.
using story::warp_window_length;
constexpr unsigned char kc_op_fork = 0x6f, kc_op_jt = 0x33, kc_op_jmp = 0x32, kc_op_filler = 0x01;
constexpr unsigned kc_check_stride_frames = 8; // the fields and the window are re-read every 8th Present (and on a VM change)
// The deferral site, detected with the window: the T3 stop from PlayMusicWithFade (kc_play_stop_ip; Mayhem 0x0db54),
// the fade stop (kc_fade_stop_ip, never deferred) and the T2 play from the same method (kc_play_ip). Frame cells of the
// ChangeTrackMusic fork child at its stop, by absolute index from the end of the cell buffer ([ESI+0x14]; valid cells
// -capacity..-1; section 8.4): the stack index is -queue_stack_depth (19), the cells -queue_cell_depths[i]
// (old 18, loop return 13, new 11, fade 10, loop 9, change return 7): constants (story_detect.h), checked at each stop
// against the cell tags and the detected return offsets.
constexpr unsigned tag_int = 1, tag_return = 3;
using story::queue_cell_count;
// Ids never deferred as the new track: death, landing and boarding (the same literals in both builds,
// verify_music_queue_kc.py). A combat track is never deferred by the alert test instead (deferrable_shape).
constexpr std::uint32_t id_death = 13, id_landing = 14, boarding_id_first = 8233, boarding_id_last = 8237;
// Private keys for the orphan: outside 101..299 and every script id; one per episode.
constexpr std::uint32_t queue_key_base = 0x7f000000, queue_key_mask = 0xffff;
// A cell index is valid when -capacity <= index <= -1 (the buffer end is [ESI+0x14]; the bottom cell is -1).
inline bool cell_index_ok(std::int32_t index, std::uint32_t capacity) {
    return capacity != 0 && capacity <= trace_cell_capacity_limit && index < 0 && std::int64_t(index) >= -std::int64_t(capacity);
}
// Byte offset of cell `index` from the buffer end (only after cell_index_ok).
inline std::int32_t cell_offset(std::int32_t index) {
    return index * std::int32_t(cell_size);
}
// A new id N must be > 0 (signed) to be deferred: 0 (leave, StopMusic) and every negative id play at once.
inline bool immediate_id(std::uint32_t id) {
    return std::int32_t(id) <= 0 || id == id_death || id == id_landing || (id >= boarding_id_first && id <= boarding_id_last);
}
inline std::uint32_t queue_key(std::uint32_t episode) {
    return queue_key_base + (episode & queue_key_mask);
}
enum class QueueState : unsigned { idle = 0, orphaned = 1, parked = 2 };
inline const char* queue_state_name(QueueState s) {
    return s == QueueState::orphaned ? "orphaned" : s == QueueState::parked ? "parked" : "idle";
}
enum class QueueEvent : unsigned { none = 0, defer, park, collapse, release, end, refuse };
inline const char* queue_event_name(QueueEvent e) {
    switch (e) {
    case QueueEvent::defer: return "defer";
    case QueueEvent::park: return "park";
    case QueueEvent::collapse: return "collapse";
    case QueueEvent::release: return "release";
    case QueueEvent::end: return "end";
    case QueueEvent::refuse: return "refuse";
    default: return "none";
    }
}
enum class QueueReason : unsigned {
    none = 0,
    // refusals of a PlayMusicWithFade stop (one row each)
    not_armed,    // the queue is not armed (refused at install or music_keep off)
    no_task,      // ESI unreadable
    cells,        // the frame cells do not carry the LoopMusic-from-ChangeTrackMusic shape (values logged)
    immediate_id, // the new id is <= 0 (signed), death, landing or boarding
    alert,        // [R+4] or [R+8] nonzero, or R unreadable
    combat_old,   // the stopped id is the track the last music play started with the alert on
    not_playing,  // the record is absent, not music + playing, loop-flagged, unowned or has no media
    same_id,      // new id == old id
    // ends
    natural_end,       // the replay after O's natural end: P freed, vanilla plays N from 0
    lost,              // O no longer linked under P (the manager's error path freed it)
    ended_before_park, // O ended while C was still waiting: no park, N plays now
    stop,              // a non-deferrable stop of N (combat, StopMusic, fade) or of another music record
    other_play,        // another music play while the queue was active
    stop_all,          // a stop-all other than alt-tab
    alert_edge,        // the committed alert level became nonzero while the queue was active
    stale,             // O ended (flag 2 clear, context 0) with nobody left to wake
    claimed,           // O carries a context the queue did not write, or C's slot is out of range
};
inline const char* queue_reason_name(QueueReason r) {
    switch (r) {
    case QueueReason::not_armed: return "not_armed";
    case QueueReason::no_task: return "no_task";
    case QueueReason::cells: return "cells";
    case QueueReason::immediate_id: return "immediate_id";
    case QueueReason::alert: return "alert";
    case QueueReason::combat_old: return "combat_old";
    case QueueReason::not_playing: return "not_playing";
    case QueueReason::same_id: return "same_id";
    case QueueReason::natural_end: return "natural_end";
    case QueueReason::lost: return "lost";
    case QueueReason::ended_before_park: return "ended_before_park";
    case QueueReason::stop: return "stop";
    case QueueReason::other_play: return "other_play";
    case QueueReason::stop_all: return "stop_all";
    case QueueReason::alert_edge: return "alert_edge";
    case QueueReason::stale: return "stale";
    case QueueReason::claimed: return "claimed";
    default: return "none";
    }
}
// The orphan O as the handler finds it now: linked under the key with the same media (`live`), its flags and context.
struct OrphanView {
    bool live = false;
    std::uint32_t flags = 0, context = 0;
};
// One frame cell: tag byte and value.
struct Cell {
    unsigned tag = 0;
    std::uint32_t value = 0;
};
// The T3 entry (MOV_StopMovie native) as the handler reads it.
struct QueueStopView {
    std::uint32_t return_va = 0, id = 0, task_id = 0, slot = 0; // caller return, EAX, [esp+4], EDI
    bool task_ok = false;                                       // ESI readable
    std::uint32_t task_ip = 0, capacity = 0;
    std::int32_t index = 0;                                     // k, signed
    bool cells_ok = false;                                      // all six cells inside the buffer
    Cell cells[queue_cell_count]{};                             // old, loop return, new, fade, loop, change return
    bool alert_ok = false;
    std::uint32_t alert_old = 0, alert_new = 0;
    bool found = false;                                         // the first record with `id`
    std::uint32_t flags = 0, context = 0, rec_slot = 0, record = 0, media = 0;
    OrphanView orphan;                                          // when the queue is active
    bool combat_old = false;                                    // id is the last play's id and that play had the alert on
    const story::QueueSites* story = nullptr;                   // the detection for the VM's object (null: none or failed)
};
// The T2 entry (play routine) as the handler reads it.
struct QueuePlayView {
    std::uint32_t return_va = 0, slot = 0, task_id = 0, id = 0; // caller return, a1, a2, a3
    bool task_ok = false;
    std::uint32_t task_ip = 0;
    bool found = false; // the first record with `id` (MOV_LoadMovie created it)
    std::uint32_t flags = 0;
    OrphanView orphan;
    const story::QueueSites* story = nullptr; // the detection for the VM's object (null: none or failed)
};
// What the handler does, in this order: rekey O (`rekey`), wake the task O names (`wake`: deliver (wake_ctx, 1) through
// wake_slot, then zero O's +0x14/+0x18), complete the calling task (`complete`: (complete_ctx, 1) through complete_slot),
// park C on O (`park`: O's +0x14/+0x18 := park_ctx/park_slot), free P (`free_key`), and consume the site (`consume`: the
// stub returns instead of running the engine's body).
struct QueueOutcome {
    QueueEvent event = QueueEvent::none;
    QueueReason reason = QueueReason::none;
    bool consume = false, rekey = false, wake = false, complete = false, park = false, free_key = false;
    bool clear = false;       // zero O's +0x14/+0x18 before the free without a wake (O still names the calling task)
    bool free_later = false;  // free P at the next Present instead of now (alt-tab under paused: T1 may run inside the walk)
    bool resume_candidate = true; // false: the keep's combat-resume memory must not remember this stop
    std::uint32_t record = 0, key = 0, wake_ctx = 0, wake_slot = 0, complete_ctx = 0, complete_slot = 0, park_ctx = 0,
                  park_slot = 0;
    std::uint32_t old_id = 0, new_id = 0, task = 0, stop_task = 0;
    unsigned long long frames = 0; // since the defer
};
struct TrackQueue {
    QueueState state = QueueState::idle;
    std::uint32_t record = 0, media = 0, key = 0, old_id = 0, new_id = 0, task = 0, slot = 0; // O, its media, P, old, N, C, C's slot
    std::uint32_t episode = 0;
    unsigned long long defer_frame = 0;
    bool active() const { return state != QueueState::idle; }
    void reset() {
        const std::uint32_t e = episode;
        *this = TrackQueue{};
        episode = e;
    }
};
// Is this stop the LoopMusic-from-ChangeTrackMusic shape with a deferrable new id? (the cells and the alert; only with
// the story entry v.story, which the callers check)
inline bool deferrable_shape(const QueueStopView& v, QueueReason& why) {
    if (!v.task_ok || !v.story) {
        why = QueueReason::no_task;
        return false;
    }
    const story::QueueSites& s = *v.story;
    if (!v.cells_ok || v.index != -std::int32_t(s.queue_stack_depth) || v.cells[0].tag != tag_int || v.cells[0].value != v.id ||
        v.cells[1].tag != tag_return || v.cells[1].value != s.kc_loop_music_return || v.cells[2].tag != tag_int ||
        v.cells[3].tag != tag_int || v.cells[3].value != 0 || v.cells[4].tag != tag_int || v.cells[4].value != 1 ||
        v.cells[5].tag != tag_return || v.cells[5].value != s.kc_change_track_return) {
        why = QueueReason::cells;
        return false;
    }
    const std::uint32_t new_id = v.cells[2].value;
    if (immediate_id(new_id)) {
        why = QueueReason::immediate_id;
        return false;
    }
    if (new_id == v.id) {
        why = QueueReason::same_id;
        return false;
    }
    if (!v.alert_ok || v.alert_old != 0 || v.alert_new != 0) {
        why = QueueReason::alert;
        return false;
    }
    return true;
}
inline QueueOutcome queue_end(TrackQueue& q, QueueReason why, std::uint32_t caller_task, unsigned long long frame,
                              const OrphanView& o) {
    QueueOutcome out;
    out.event = QueueEvent::end;
    out.reason = why;
    out.record = q.record;
    out.key = q.key;
    out.old_id = q.old_id;
    out.new_id = q.new_id;
    out.task = q.task;
    out.stop_task = caller_task;
    out.frames = frame - q.defer_frame;
    // Wake C only while O still names it (the manager's own end already zeroed the context and woke C) and never the
    // calling task itself (its +0x20 must stay clear so the native suspends it as vanilla does). When the wake is
    // suppressed for that reason, O's context and slot are zeroed before the free all the same, so the teardown 0x004984d0
    // cannot deliver (C, 1) and set C's +0x20.
    if (q.state == QueueState::parked && o.live && o.context == q.task) {
        if (q.task != caller_task) {
            out.wake = true;
            out.wake_ctx = q.task;
            out.wake_slot = q.slot;
        } else
            out.clear = true;
    }
    out.free_key = o.live;
    q.reset();
    return out;
}
// T3, the MOV_StopMovie native entry (only the native caller 0x00499886 matters; the other two callers stay vanilla).
// Without a story entry (no story loaded, or an unknown build) no stop is PlayMusicWithFade's: nothing defers.
inline QueueOutcome queue_on_stop(TrackQueue& q, const QueueStopView& v, bool armed, unsigned long long frame) {
    QueueOutcome out;
    if (v.return_va != stop_movie_native_return_va) return out;
    const bool from_play = v.story && v.task_ok && v.task_ip == v.story->kc_play_stop_ip;
    if (q.active()) {
        if (!v.orphan.live) return queue_end(q, QueueReason::lost, v.task_id, frame, v.orphan);
        if (!(v.orphan.flags & flag_playing) && v.orphan.context == 0)
            return queue_end(q, QueueReason::stale, v.task_id, frame, v.orphan);
        const bool music = v.found && (v.flags & flag_music);
        if (v.id != q.new_id && v.id != q.key && !music) return out; // speech, video: not the queue's business
        if (q.state == QueueState::parked && v.id == q.new_id && from_play && v.task_id != q.task) {
            QueueReason why = QueueReason::none;
            if (deferrable_shape(v, why)) { // a further jump: collapse to the newest id
                out.event = QueueEvent::collapse;
                out.consume = out.wake = out.complete = true;
                out.record = q.record;
                out.key = q.key;
                out.old_id = q.new_id;
                out.new_id = v.cells[2].value;
                out.task = v.task_id;
                out.stop_task = v.task_id;
                out.wake_ctx = q.task;
                out.wake_slot = q.slot;
                out.complete_ctx = v.task_id;
                out.complete_slot = v.slot;
                out.frames = frame - q.defer_frame;
                out.resume_candidate = false;
                q.state = QueueState::orphaned;
                q.new_id = out.new_id;
                q.task = v.task_id;
                q.slot = 0;
                return out;
            }
        }
        out = queue_end(q, QueueReason::stop, v.task_id, frame, v.orphan);
        out.resume_candidate = v.id != out.new_id && v.id != out.key; // N never played; O is the proxy's
        return out;
    }
    if (!from_play) return out;
    out.event = QueueEvent::refuse;
    out.old_id = v.id;
    out.stop_task = v.task_id;
    if (v.cells_ok) out.new_id = v.cells[2].value;
    if (!armed) {
        out.reason = QueueReason::not_armed;
        return out;
    }
    QueueReason why = QueueReason::none;
    if (!deferrable_shape(v, why)) {
        out.reason = why;
        return out;
    }
    if (v.combat_old) {
        out.reason = QueueReason::combat_old;
        return out;
    }
    if (!v.found || (v.flags & (flag_music | flag_playing | flag_loop)) != (flag_music | flag_playing) || v.context == 0 ||
        v.media == 0 || v.context == v.task_id || v.rec_slot >= slot_count) {
        out.reason = QueueReason::not_playing;
        return out;
    }
    // Defer: re-key O, wake T_old as the engine would, complete C as the engine would, keep flag 2 and skip the pause.
    q.state = QueueState::orphaned;
    q.record = v.record;
    q.media = v.media;
    q.key = queue_key(++q.episode);
    q.old_id = v.id;
    q.new_id = v.cells[2].value;
    q.task = v.task_id;
    q.slot = 0;
    q.defer_frame = frame;
    out.event = QueueEvent::defer;
    out.reason = QueueReason::none;
    out.consume = out.rekey = out.wake = out.complete = true;
    out.resume_candidate = false;
    out.record = v.record;
    out.key = q.key;
    out.wake_ctx = v.context;  // T_old, through the record's own slot
    out.wake_slot = v.rec_slot;
    out.complete_ctx = v.task_id;
    out.complete_slot = v.slot;
    out.new_id = q.new_id;
    out.task = v.task_id;
    return out;
}
// T2, the play routine entry 0x00498c90, every caller (MOV_PlayMovie, MOV_PlayMovieFrom, the helper 0x004f6640): any play
// of a music record (flag 0x80) ends the queue unless it is C's own MOV_PlayMovie(N) (park or release).
inline QueueOutcome queue_on_play(TrackQueue& q, const QueuePlayView& v, unsigned long long frame) {
    QueueOutcome out;
    if (!q.active()) return out;
    if (!v.found || !(v.flags & flag_music)) return out; // speech, video
    const bool mine = v.return_va == play_callers[0].return_va && v.story && v.task_ok && v.task_ip == v.story->kc_play_ip &&
                      v.task_id == q.task && v.id == q.new_id;
    if (!v.orphan.live) return queue_end(q, QueueReason::lost, v.task_id, frame, v.orphan);
    if (q.state == QueueState::orphaned) {
        if (!mine) return queue_end(q, QueueReason::other_play, v.task_id, frame, v.orphan);
        if (!(v.orphan.flags & flag_playing)) return queue_end(q, QueueReason::ended_before_park, v.task_id, frame, v.orphan);
        if (v.orphan.context != 0 || v.slot >= slot_count) return queue_end(q, QueueReason::claimed, v.task_id, frame, v.orphan);
        out.event = QueueEvent::park;
        out.consume = out.park = true;
        out.park_ctx = v.task_id;
        out.park_slot = v.slot;
        out.record = q.record;
        out.key = q.key;
        out.old_id = q.old_id;
        out.new_id = q.new_id;
        out.task = q.task;
        out.frames = frame - q.defer_frame;
        q.state = QueueState::parked;
        q.slot = v.slot;
        return out;
    }
    if (mine) { // the replay after O's natural end: vanilla continues, N seeks to 0 and runs
        out = queue_end(q, QueueReason::natural_end, v.task_id, frame, v.orphan);
        out.event = QueueEvent::release;
        return out;
    }
    return queue_end(q, QueueReason::other_play, v.task_id, frame, v.orphan);
}
// T1, the stop-all entry. Alt-tab leaves the queue alone only when the keep's alt-tab mode in effect is skip_all
// (`skip_all_available`, the keep's status gate: skip_all leaves O alone too). Under paused (no status gate) alt-tab ends
// the queue like every other caller, but P is freed at the next Present: alt-tab's T1 can run inside a COM call of the
// manager's walk (media-record-lifetime.md, boundary 1), so it never frees there.
inline QueueOutcome queue_on_stop_all(TrackQueue& q, std::uint32_t return_va, bool skip_all_available, const OrphanView& o,
                                      unsigned long long frame) {
    if (!q.active()) return QueueOutcome{};
    const StopMode mode = stop_mode(return_va);
    if (mode == StopMode::skip_all && skip_all_available) return QueueOutcome{};
    QueueOutcome out = queue_end(q, QueueReason::stop_all, 0, frame, o);
    if (mode == StopMode::skip_all) { // alt-tab under paused
        out.free_later = out.free_key;
        out.free_key = false;
    }
    return out;
}
// The story reload's own stop-alls: the session function's (0x0040387d, before it calls the loader 0x00404cc0 at
// 0x004038ba) and the loader's (0x00404cf2, before the KC VM restore 0x004a0880).
constexpr std::uint32_t session_start_return_va = 0x0040387d, load_return_va = 0x00404cf2;
inline bool story_load_caller(std::uint32_t return_va) {
    return return_va == session_start_return_va || return_va == load_return_va;
}
// Present: the committed alert level [R+4] became nonzero while the queue was active (a combat start the script dropped
// while C was waiting, or one whose stop the queue did not see).
inline QueueOutcome queue_on_alert(TrackQueue& q, std::uint32_t level, const OrphanView& o, unsigned long long frame) {
    if (!q.active() || level == 0) return QueueOutcome{};
    return queue_end(q, QueueReason::alert_edge, 0, frame, o);
}
// The KC patch state (Present): one row per change of (vm, code, result) and per apply.
enum class KcResult : unsigned {
    none = 0,
    applied,
    already,
    no_vm,
    unreadable,
    no_sites, // the queue's sites were not detected in the loaded object: refused (the row names the method and reason)
    out_of_range,
    bytes_mismatch,
    unwritable,
    waiting // no write yet: a story load may still be in progress (the load gate), or the sites were detected only now
};
inline const char* kc_result_name(KcResult r) {
    switch (r) {
    case KcResult::applied: return "applied";
    case KcResult::already: return "already";
    case KcResult::no_vm: return "no_vm";
    case KcResult::unreadable: return "unreadable";
    case KcResult::no_sites: return "no_sites";
    case KcResult::out_of_range: return "out_of_range";
    case KcResult::bytes_mismatch: return "bytes_mismatch";
    case KcResult::unwritable: return "unwritable";
    case KcResult::waiting: return "waiting";
    default: return "none";
    }
}
// What one due check read: the VM pointer, its CODE buffer, size and build, and the window bytes.
struct KcObservation {
    std::uint32_t vm = 0, code = 0, size = 0, build = 0;
    bool window_ok = false;
    unsigned char window[warp_window_length]{};
    bool operator==(const KcObservation& o) const {
        if (vm != o.vm || code != o.code || size != o.size || build != o.build || window_ok != o.window_ok) return false;
        for (unsigned i = 0; i < warp_window_length; ++i)
            if (window[i] != o.window[i]) return false;
        return true;
    }
};
struct KcPatch {
    std::uint32_t vm = 0, code = 0, size = 0, build = 0;
    const story::QueueSites* story = nullptr;   // the detection for (vm, code, size, build) (null: not detected yet)
    const story::QueueSites* applied_story = nullptr; // the detection the last apply wrote (shutdown restores its window)
    std::uint32_t noted_build = 0;         // the build of the last row (a no_sites row for each new build)
    KcResult result = KcResult::none;
    unsigned countdown = 1; // the first Present is due
    unsigned applied = 0, refused = 0;
    // The load gate: the patch is written only once a story load has completed, never while one may be in progress.
    // A load begins at the stop-all of the session function or of the loader (story_load_caller), both before the KC VM
    // restore 0x004a0880 reloads the story object; load_begin() closes the gate. It opens for the first due check after
    // the save_load_complete marker (load_complete(); the marker is emitted once per process, for the first save load),
    // or, for every load without a marker (the initial story load at session start, every later save load), once two
    // consecutive due checks kc_check_stride_frames apart after the last load_begin() read the same observation
    // (VM, CODE buffer, size, build and window bytes).
    KcObservation last{};
    bool have_last = false, marker = false;
    unsigned load_begins = 0, markers = 0;
    void load_begin() {
        have_last = false;
        marker = false;
        ++load_begins;
    }
    void load_complete() {
        marker = true;
        ++markers;
        countdown = 1; // the next Present is due
    }
    // Every due check: true when the gate is open for a write at this check.
    bool settle(const KcObservation& now) {
        const bool same = have_last && now == last;
        last = now;
        have_last = true;
        if (marker) {
            marker = false;
            return true;
        }
        return same;
    }
    // Whether this Present reads the VM's fields and the window: on a change of the VM pointer (one image load per
    // frame otherwise) and every kc_check_stride_frames, so a reloaded story object (the same VM, a new CODE buffer) is
    // patched again within the stride.
    bool due(std::uint32_t vm_now) {
        const bool changed = vm_now != vm;
        vm = vm_now;
        if (changed) {
            countdown = kc_check_stride_frames;
            return true;
        }
        if (--countdown) return false;
        countdown = kc_check_stride_frames;
        return true;
    }
    // The decision over the window bytes: true = write the patch; `r` the result either way.
    bool decide(bool vm_ok, bool fields_ok, const unsigned char* window, bool window_ok, KcResult& r) const {
        if (!vm_ok) {
            r = KcResult::no_vm;
            return false;
        }
        if (!fields_ok) {
            r = KcResult::unreadable;
            return false;
        }
        if (!story || story->key.code != code || story->key.size != size || story->key.build != build) {
            r = KcResult::waiting; // not detected for this object yet
            return false;
        }
        if (!story->outcome.ok()) {
            r = KcResult::no_sites;
            return false;
        }
        if (code == 0 || size < story->warp_fork_offset + warp_window_length) {
            r = KcResult::out_of_range;
            return false;
        }
        if (!window_ok) {
            r = KcResult::unreadable;
            return false;
        }
        bool vanilla = true, patched = true;
        for (unsigned i = 0; i < warp_window_length; ++i) {
            if (window[i] != story->warp_window_memory[i]) vanilla = false;
            if (window[i] != story->warp_patch_memory[i]) patched = false;
        }
        if (patched) {
            r = KcResult::already;
            return false;
        }
        if (!vanilla) {
            r = KcResult::bytes_mismatch;
            return false;
        }
        r = KcResult::applied;
        return true;
    }
    // A change of result (or a fresh apply) is a row; the `already` that follows an apply is the same state, no row. A
    // no_sites result is a row again for each new build id it sees (the config, then the intro).
    bool note(KcResult r) {
        const bool per_build = r == KcResult::no_sites && r == result && build != noted_build;
        const bool row = r == KcResult::applied || per_build ||
                         (r != result && !(r == KcResult::already && result == KcResult::applied));
        result = r;
        if (row) noted_build = build;
        if (r == KcResult::applied) {
            ++applied;
            applied_story = story;
        } else if (r != KcResult::already && r != KcResult::none && r != KcResult::waiting)
            ++refused;
        return row;
    }
};

// ---- Shared entry sites (the stop-all entry 0x004982b0 and the MOV_StopMovie entry 0x00498810) ----
// Both features patch the same two heads. The first to install claims the site (the claim writes `e9 rel32`
// over its first five bytes); every feature chains its own stub in front of the current chain head. The
// window checks of the second feature must therefore see through the first feature's live claim: a read
// that overlaps a live claim of ours whose bytes are still exactly the claim's own patch is overlaid with
// the claim's original bytes; any other byte there (a foreign write) stays and fails the comparison.
struct LiveClaim {
    std::uint32_t site;
    const unsigned char* original;
    const unsigned char* patched;
    unsigned patch_length;
};
// Returns false when an overlapping claim's bytes are not its own patch (every other claim is still overlaid).
inline bool overlay_claims(std::uint32_t at, unsigned char* bytes, unsigned length, const LiveClaim* claims,
                           unsigned count) {
    bool all_own = true;
    for (unsigned c = 0; c < count; ++c) {
        const LiveClaim& k = claims[c];
        if (k.site + k.patch_length <= at || k.site >= at + length) continue;
        bool own = true;
        for (unsigned i = 0; i < k.patch_length; ++i) {
            const std::uint32_t va = k.site + i;
            if (va >= at && va < at + length && bytes[va - at] != k.patched[i]) own = false;
        }
        if (!own) {
            all_own = false;
            continue;
        } // not our patch: leave the bytes as read (the comparison fails)
        for (unsigned i = 0; i < k.patch_length; ++i) {
            const std::uint32_t va = k.site + i;
            if (va >= at && va < at + length) bytes[va - at] = k.original[i];
        }
    }
    return all_own;
}
// Whether [at, at+length) holds `expected` in the unpatched image. `read(at, out, n)` reads code bytes.
template <class Read>
inline bool window_matches(Read&& read, std::uint32_t at, const unsigned char* expected, unsigned length,
                           const LiveClaim* claims, unsigned count) {
    unsigned char actual[128]{};
    if (length > sizeof actual || !read(at, actual, length)) return false;
    if (!overlay_claims(at, actual, length, claims, count))
        return false; // a foreign write over a live claim: fail closed
    for (unsigned i = 0; i < length; ++i)
        if (actual[i] != expected[i]) return false;
    return true;
}
// The shared claim: `Ops` supplies claim(site, spec) -> bool, status(site) -> const char*, push(site, stub,
// continuation) -> bool (stores the current head into continuation, then makes the stub the head; false
// leaves the chain untouched), restore(site) -> bool, live(site) -> bool. The first user claims, every user
// chains; a push that fails on a fresh claim restores it. A feature that later fails its install releases
// the site: the last user restores the bytes; while another user remains, the failed feature's stub stays
// chained and its armed flag keeps it inert. Independent of which feature comes first.
template <class Site> struct SharedSite {
    Site site{};
    unsigned users = 0;
};
template <class Ops, class Site, class Spec, class Stub, class Slot>
inline bool acquire_shared(Ops& ops, SharedSite<Site>& s, const Spec& spec, Stub stub, Slot& continuation,
                           const char** reason) {
    if (!s.users) {
        if (ops.live(s.site)) {
            *reason = "site_live";
            return false;
        } // an unrestored claim (failed rollback): refuse, never forget it
        s.site = Site{};
        if (!ops.claim(s.site, spec)) {
            *reason = ops.status(s.site);
            return false;
        }
    }
    if (!ops.push(s.site, stub, continuation)) {
        *reason = "chain_failed";
        if (!s.users && !ops.restore(s.site)) *reason = "rollback_failed";
        return false;
    }
    ++s.users;
    return true;
}
template <class Ops, class Site> inline bool release_shared(Ops& ops, SharedSite<Site>& s) {
    if (!s.users)
        return !ops.live(s.site) || ops.restore(s.site); // a claim whose push failed: already restored by
                                                         // acquire_shared
    if (--s.users) return true;
    return ops.restore(s.site);
}
}
