#pragma once
#include <cstdint>
#include "loading_windows_fl.h"

// FL sites of the gz read-ahead buffer (the AP twin: loading_trace.cpp's six gz_buffer rows; the buffer itself,
// src/proxy/gz_buffer.cpp, is shared unchanged). Verified facts: docs/reverse-engineering/fl/loading-hooks.md,
// "Port (2026-11-03)" (verifier verification/probe/verify_gz_buffer_sites_fl.py). X3FL.exe is relocatable: every value
// is an RVA added to x3m::fl::module_base().
//
// The six zlib1.dll named imports the buffer owns, in claim order (gzclose first: a buffered handle can only exist once
// gzopen, the last write, is live; then every call on it already reaches the buffer). Each IAT slot is a four-byte
// pointer; no instruction is displaced. gzwrite (base+0x1503f0) is not claimed: write handles pass through, and the
// slot is the music keep's (music_keep_sites_fl.h). gzclose (base+0x150408) is shared with the keep
// (gz_close_share_fl.h).
// Witnesses (loading_windows_fl_inc.h, table gz): gzopen 0x106627, gzread 0x1067b0, gzseek 0x106926, gztell 0x106077,
// gzgetc 0x1066ec plus the three EDX loads 0x106174/0x1061d0/0x106234 (`mov edx,[IAT]`, then `call edx`: routed when
// the slot is written before the game's CRT runs), gzclose's eleven `call [IAT]` (inlined closers included) and the
// file object's close wrapper 0x106940; each operand is a HIGHLOW that must hold base + the slot RVA.
namespace x3m::fl::gz_buffer_fl {
namespace W = x3m::fl::loading_windows;
struct Import {
    const char* name;
    std::uint32_t slot_rva;
};
constexpr unsigned import_count = 6;
constexpr Import imports[import_count] = {{"gzclose", 0x150408}, {"gztell", 0x1503ec}, {"gzseek", 0x1503e8},
                                          {"gzgetc", 0x1503f4},  {"gzread", 0x1503f8}, {"gzopen", 0x1503e4}};
enum Index : unsigned { close_index, tell_index, seek_index, getc_index, read_index, open_index };
constexpr std::uint32_t gzwrite_slot_rva = 0x1503f0;
constexpr std::uint32_t close_wrapper_rva = 0x106940;

// The slots are the operands of the generated witnesses (offset 2 of each `ff 15` / `8b 15`, 0x1f in the wrapper).
static_assert(W::fixup_target(W::gz_windows, W::gz_fixups, "gzopen_call", 2) == imports[open_index].slot_rva);
static_assert(W::fixup_target(W::gz_windows, W::gz_fixups, "gzread_call", 2) == imports[read_index].slot_rva);
static_assert(W::fixup_target(W::gz_windows, W::gz_fixups, "gzseek_call", 2) == imports[seek_index].slot_rva);
static_assert(W::fixup_target(W::gz_windows, W::gz_fixups, "gztell_call", 2) == imports[tell_index].slot_rva);
static_assert(W::fixup_target(W::gz_windows, W::gz_fixups, "gzgetc_call", 2) == imports[getc_index].slot_rva);
static_assert(W::fixup_target(W::gz_windows, W::gz_fixups, "gzgetc_load_a", 2) == imports[getc_index].slot_rva);
static_assert(W::fixup_target(W::gz_windows, W::gz_fixups, "gzgetc_load_b", 2) == imports[getc_index].slot_rva);
static_assert(W::fixup_target(W::gz_windows, W::gz_fixups, "gzgetc_load_c", 2) == imports[getc_index].slot_rva);
static_assert(W::fixup_target(W::gz_windows, W::gz_fixups, "close_wrapper", 0x1f) == imports[close_index].slot_rva);
static_assert(W::fixup_target(W::gz_windows, W::gz_fixups, "gzclose_call_10695d", 2) == imports[close_index].slot_rva);
static_assert(sizeof W::gz_windows / sizeof W::gz_windows[0] == 20);
}
