#pragma once
#include <cstdint>
#include "loading_windows_fl.h"

// FL sites of the fast resource reader and the DAT handle pool (the AP twin: src/proxy/resource_reader.cpp's constants;
// the decoder and the pool themselves, src/proxy/resource_reader_core.cpp, are shared unchanged). Verified facts:
// docs/reverse-engineering/fl/resource-reader.md, "Port (2026-11-03)" (verifier
// verification/probe/verify_resource_reader_sites_fl.py). X3FL.exe is relocatable: every value is an RVA added to
// x3m::fl::module_base().
//
// Reader: entry base+0x105f40, `push ebp; mov ebp,esp; and esp,-8` (six bytes, no relative or relocated operand). The
// file object arrives in ECX (moved to EDI at 0x105f61; EAX is written by `lea eax` at 0x105f54 before any read), the
// buffer or zero returns in EAX, plain ret; EBX/ESI/EDI/EBP preserved. Witnesses: loading_windows_fl_inc.h table
// `reader` (the whole reader in two halves, its dispatcher/seek/byte reader, the allocator, the five CRT heads).
// Pool: the extension open's DAT `call fopen` at base+0x105eb0 (window dat_open: the "rb" push at 0x105eaa, the
// record fseek after it) and seventeen of the eighteen direct `call fclose` sites (every E8 to fclose in .text but the
// CRT's exit-time fcloseall loop, below); FL's FILE keeps its
// flag word at +0x0c with error 0x10 (fread's `lock or`, window file_error_flag), not AP's 0x20.
namespace x3m::fl::resource_reader_fl {
namespace W = x3m::fl::loading_windows;
constexpr std::uint32_t reader_rva = 0x105f40;
constexpr unsigned reader_length = 6;
constexpr unsigned char reader_bytes[reader_length] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8};
// Core bindings (resource_reader::Environment order): the static CRT's fread/fseek/ftell, raw malloc (not the
// allocator 0x0d8fc0, which would double-count and zero-fill) and free.
constexpr std::uint32_t fread_rva = 0x132c9e, fseek_rva = 0x13074e, ftell_rva = 0x131fb0, malloc_rva = 0x131395,
                        free_rva = 0x12f4e5;
// The allocator's four counters in the core's order (the live pair, cumulative bytes, allocation count; EDX=0 picks
// the category-0 slot of the base+0x1aaa18 array) and the two result-size globals.
constexpr std::uint32_t counter_rva[4] = {0x1aae20, 0x1aaa18, 0x1aaa14, 0x1aae1c};
constexpr std::uint32_t size_global_rva[2] = {0x1ab3e8, 0x1ab3ec};
// zlib1.dll's inflate imports the reader calls through (the core resolves the same exports by name, like gz_buffer_fl).
constexpr std::uint32_t inflate_init_slot_rva = 0x1503fc, inflate_slot_rva = 0x150400, inflate_end_slot_rva = 0x150404;
// Pool.
constexpr std::uint32_t fopen_rva = 0x132087, fclose_rva = 0x12fb6e;
constexpr std::uint32_t fopen_site_rva = 0x105eb0, rb_string_rva = 0x18bb9c;
constexpr unsigned file_flag_offset = 0x0c, file_error_flag = 0x10;
// Seventeen of the eighteen direct fclose calls. The eighteenth, base+0x1397e4, is the static CRT's own fcloseall loop
// (0x13979c, called only from the exit-time stdio teardown 0x12ee72, reached only through the .rdata pointer at
// 0x150480): it closes every open stream at process exit and frees its slot. It is not redirected: through the pool a
// kept handle would be closed there yet stay listed as kept, so a DAT open racing exit could be handed a closed FILE;
// left native, it closes kept handles (the pool never reuses them after exit begins: no DAT open follows teardown).
constexpr unsigned close_site_count = 17;
constexpr std::uint32_t close_site_rva[close_site_count] = {
    0x079da6, 0x0819af, 0x0c29e7, 0x0c2f12, 0x1048fa, 0x1049c5, 0x105e50, 0x106410, 0x106606,
    0x10696c, 0x10a512, 0x10a6e5, 0x10a787, 0x1149ee, 0x114a23, 0x114a2f, 0x11d9be};
constexpr std::uint32_t exit_close_rva = 0x1397e4, exit_fcloseall_rva = 0x13979c, exit_teardown_rva = 0x12ee72,
                        exit_teardown_pointer_rva = 0x150480;
// Real close routes a fixture can execute: the file object's close wrapper (its fclose at 0x10696c lies inside the
// music keep's and gz_buffer's close_wrapper window, which both check before this pool claims) and the destructor
// (fclose at 0x1049c5).
constexpr std::uint32_t close_wrapper_rva = 0x106940, destructor_rva = 0x1049a0;

// The generated witnesses pin every address above: the counters and globals are the operands of the allocator's and the
// reader's stores, the inflate slots the reader's calls, "rb" the DAT open's push; the window tables cover the claims.
static_assert(W::fixup_target(W::reader_windows, W::reader_fixups, "allocator", 0xd0) == counter_rva[0]);
static_assert(W::fixup_target(W::reader_windows, W::reader_fixups, "allocator", 0xe3) == counter_rva[1]);
static_assert(W::fixup_target(W::reader_windows, W::reader_fixups, "allocator", 0xd6) == counter_rva[2]);
static_assert(W::fixup_target(W::reader_windows, W::reader_fixups, "allocator", 0xe9) == counter_rva[3]);
static_assert(W::fixup_target(W::reader_windows, W::reader_fixups, "reader_b", 0x1ec) == size_global_rva[0]);
static_assert(W::fixup_target(W::reader_windows, W::reader_fixups, "reader_b", 0x1e7) == size_global_rva[1]);
static_assert(W::fixup_target(W::reader_windows, W::reader_fixups, "reader_b", 0x20a) == counter_rva[0]);
static_assert(W::fixup_target(W::reader_windows, W::reader_fixups, "reader_b", 0x213) == counter_rva[1]);
static_assert(W::fixup_target(W::reader_windows, W::reader_fixups, "reader_b", 0x151) == inflate_init_slot_rva);
static_assert(W::fixup_target(W::reader_windows, W::reader_fixups, "reader_b", 0x162) == inflate_slot_rva);
static_assert(W::fixup_target(W::reader_windows, W::reader_fixups, "reader_b", 0x1d7) == inflate_end_slot_rva);
static_assert(W::fixup_target(W::pool_windows, W::pool_fixups, "dat_open", 0x03) == rb_string_rva);
static_assert(W::reader_windows[0].rva == reader_rva && W::reader_windows[0].count == 6);
static_assert(sizeof W::reader_windows / sizeof W::reader_windows[0] == 11);
static_assert(sizeof W::pool_windows / sizeof W::pool_windows[0] == 5 + close_site_count);
constexpr bool close_sites_are_windows() {
    for (unsigned i = 0; i < close_site_count; ++i)
        if (W::pool_windows[5 + i].rva != close_site_rva[i] || W::pool_windows[5 + i].length != 5 ||
            W::pool_windows[5 + i].count != 0)
            return false;
    return true;
}
static_assert(close_sites_are_windows());
static_assert(W::pool_windows[0].rva + 8 == fopen_site_rva); // dat_open: mov esi,eax; push "rb"; push esi; call fopen
}
