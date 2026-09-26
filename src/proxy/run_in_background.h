#pragma once
#include <cstdint>

// Run in background (X3M_RUN_IN_BACKGROUND=1|0; the schema default 1, unset under X3M_CONFIG=bare = off): the
// game behaves as if `-runinbg` had been given (docs/reverse-engineering/run-in-background.md). Without the
// argument the RunInBackground bit 0x4000 of the input flags word [*0x00606f3c] stays clear (unless the registry
// value RunInBackground is set), and the message pump 0x004d34b0 blocks the whole game loop in GetMessageA while
// the window is inactive, which the CrossOver shortcut (no arguments) turns into an alt-tab stall.
//
// Mechanism: the init routine's `call 0x004d2580` at 0x004033c9, right after the game wrote the bit from the
// argument, is redirected (engine_patch::claim_call, one lock cmpxchg8b) to x3m_run_in_background_thunk. The
// thunk saves every register and EFLAGS, sets the bit once when it is clear (InterlockedOr, one write; nothing
// when the argument or the registry already set it), writes one run_in_background row, restores LastError and
// jumps to 0x004d2580 with the game's return address on the stack. Installed on the backend-load path inside
// the engine_patch install window (Direct3DCreate9 at 0x00402edc runs before 0x004033c9) after the structural
// executable check and a 60-byte window compare (fail closed). No allocation, no per-frame work, no device
// state: Reset and recovery are not involved. A later X2_SetRunInBackground script call still changes the bit
// as it would with the argument, and the music keep keeps reading the real bit.
//
// Rows: at install `run_in_background site=0x004033c9 status=armed|off|refused reason= setting= ...`; when the
// site runs `run_in_background site=0x004033c9 status=patched|already|refused reason= setting=1 value_before=
// value_after= flags_before= flags_after=` (value_* = the bit, flags_* = the whole word).
namespace x3m::run_in_background {
struct Addresses {
    std::uintptr_t window, target, slot; // the 60-byte window, the call's expected target, the input block pointer
};
bool initialize(); // backend-load path only; logs one run_in_background row
bool shutdown();   // restores the call (dynamic-unload detach only); true when nothing stays installed
// Verifies the window at a.window (the engine's 0x00403392 or a fixture's copy) and redirects its call. Arms the
// one-shot write for the slot a.slot. Returns whether the redirect is live; state() carries the reason.
bool install_at(const Addresses& a);
const char* state();
// The thunk's outcome: nullptr until the site ran, then patched | already | refused.
const char* outcome();
std::uint32_t flags_before();
std::uint32_t flags_after();
}
extern "C" {
// The redirected call's target: saves everything, applies the setting once, then `jmp` to the original callee.
void x3m_run_in_background_thunk();
void __cdecl x3m_run_in_background_apply();
extern std::uintptr_t x3m_run_in_background_continue; // the original callee (0x004d2580), set by install_at
}
