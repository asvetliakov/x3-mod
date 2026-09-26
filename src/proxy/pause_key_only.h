#pragma once
#include <cstdint>

// Pause key only (X3M_PAUSE_KEY_ONLY=1; unset = vanilla, nothing patched):
// the flight pause's wait loop in 0x00404280 leaves the pause on any new key
// or any new mouse button (docs/reverse-engineering/pause-dialog-input.md);
// the 12 bytes at 0x004043a5 are rewritten in place so only the configured
// key (X3M_PAUSE_KEY, engine key code, default 0x1b5 = DIK_PAUSE) or a mouse
// button ends it. Every other key, including the Alt/Command press that
// begins an alt-tab, is read and dropped. Pure instruction bytes: no stub,
// no pointer into this module, nothing per frame.
// Written on the backend-load path inside the engine_patch install window
// (before the first Present, so the main loop cannot be inside the loop)
// after the exact-executable check and a 79-byte window compare (fail
// closed); VirtualProtect/FlushInstructionCache, read-back compare,
// rollback judged by a read-back of the original bytes. Device Reset does
// not touch it (no device state); shutdown() puts the original bytes back on
// a dynamic unload.
// Lifetime: the module holds no stub and does not pin the DLL. It relies on
// the process not unloading the DLL while the main thread spins in the wait
// loop: shutdown() restoring the bytes while that thread sits inside the span
// would move the instruction boundaries (new a5/aa/ac/af, old a5/a7/a9/af)
// under it. In practice the DLL is never unloaded: every module that emits a
// stub pins it (GetModuleHandleExW(..._PIN) in collide_box_cull,
// collide_sat_sse2 and collide_memo, the latter two on by default on a modded
// launch), which makes the FreeLibrary path unreachable.
namespace x3m::pause_key_only {
bool initialize(); // backend-load path only; logs one pause_key_only line when X3M_PAUSE_KEY_ONLY is set
bool shutdown();   // restores the 12 original bytes (dynamic-unload detach only); true when nothing is installed
// Verifies the window at `window` (the engine's 0x004043a0, or a fixture's
// synthetic copy) and rewrites the site with `key`. Returns whether the
// patch is live; state() carries the reason either way.
bool install_at(std::uintptr_t window, std::uint32_t key);
const char* state();
const char* write_path(); // none|atomic|plain: which engine_patch::write_code path wrote the site
std::uint32_t key();      // the key in the live patch (0 when none)
}
