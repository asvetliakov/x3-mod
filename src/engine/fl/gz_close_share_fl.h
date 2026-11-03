#pragma once
#include <cstdint>

// The one shared import slot of the FL loading port: X3FL.exe's gzclose (base+0x150408). Two FL owners want it: the
// music keep (music_keep_fl.cpp, its save pump observes the save's own close) and gz_buffer (gz_buffer_fl.cpp, the
// read-ahead must unregister a buffered handle before zlib frees it). Neither exchanges a slot the other holds; the
// later one chains, so in either install order the slot runs the keep's stub first and gz_buffer's close behind it:
//   keep first:     slot = keep stub; gz_buffer compare-exchanges the keep's forward word (zlib's gzclose -> its entry);
//   gz_buffer first: slot = gz_buffer's entry; the keep takes the slot (entry -> its stub) and forwards to that entry.
// The words below are each owner's published state (written inside the engine_patch install window, before the game's
// CRT runs; header-only so either module links alone). docs/reverse-engineering/fl/music-keep.md section 3 and
// loading-hooks.md, "Port (2026-11-03)".
namespace x3m::fl::gz_close_share {
// gz_buffer: its gzclose entry while it is active (0 otherwise), the zlib export behind it, and whether it holds the
// slot itself (1) or sits behind the keep's forward word (0).
inline volatile std::uint32_t buffer_entry = 0, buffer_forward = 0, buffer_holds_slot = 0;
// The keep: its gzclose stub while its close slot is live (0 otherwise), and the address of the word the stub jumps
// through (holding zlib's gzclose, or gz_buffer's entry once chained).
inline volatile std::uint32_t keep_stub = 0;
inline volatile std::uint32_t* volatile keep_forward = nullptr;
}
