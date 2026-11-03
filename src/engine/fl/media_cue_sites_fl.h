#pragma once
#include <cstdint>
#include "../../proxy/media_cue_core.h"
#include "code_window_fl.h"

// FL edition of the media-cue gate sites (the AP twin is src/proxy/media_cue_sites.h; the value-only policy
// media_cue_core.h is shared unchanged: the immutable ID2 rule, the negative cache, the pending stack, the trace ring,
// the 300-frame window). Facts: docs/reverse-engineering/fl/media-cue.md sections 1 to 4 and "Port (2026-11-03)";
// verifier verification/probe/verify_media_cue_sites_fl.py. Every value is an RVA added to x3m::fl::module_base().
//
// Allocator `record* 0x0b4080(ECX = id, EDX = constructor flags)`, no stack argument, plain RET at both exits
// (0x0b40f6 failure, 0x0b41a3 success); EBX/ESI/EDI saved and restored, EBP untouched, ECX/EDX/EFLAGS scratch.
//   0b4080  53 / 56 / 57 / 8b fa   push ebx; push esi; push edi; mov edi,edx   <- claimed (5 bytes, four whole
//                                  instructions, no relative operand, no relocation; aligned: one lock cmpxchg8b)
//   0b4085  8b d9                  mov ebx,ecx                                  ; the claim tail resumes here
// The one direct caller is the notification wrapper 0x0b46b0 (call at 0x0b46be, return 0x0b46c3); at allocator entry
// [esp] = 0x0b46c3, [esp+4]/[esp+8] the wrapper's saved EDI/ESI, [esp+0xc] the wrapper caller's return, [esp+0x10] the
// wrapper's id argument, [esp+0x14] its flags argument, [esp+0x18] its notify argument. The wrapper's six callers
// classify the cue; the sector selector (call 0x066126, return 0x06612b) assigns kind 0x5a after creation (0x066303),
// so the selector's kind is the statically proven constant, never a stack word.
//
// The video consumer 0x0fed10..0x0ff578 (exclusive) locks its destination surface at 0x0fedf0 (return 0x0fedf3) and
// unlocks it at 0x0ff556 (return 0x0ff559): the optional surface-lock witness classifies its caller by that range.
namespace x3m::fl::media_cue_fl {
namespace W = x3m::fl::music_windows;
namespace detail = x3m::media_cue::detail;
constexpr std::uint32_t allocator_rva = 0x0b4080, continue_rva = 0x0b4085;
constexpr unsigned entry_length = 5;
constexpr unsigned char entry_bytes[entry_length] = {0x53, 0x56, 0x57, 0x8b, 0xfa};
constexpr std::uint32_t wrapper_rva = 0x0b46b0, wrapper_call_rva = 0x0b46be, wrapper_return_rva = 0x0b46c3;
struct WrapperCaller {
    std::uint32_t call_rva, return_rva;
    detail::Caller caller;
};
constexpr WrapperCaller wrapper_callers[] = {
    {0x0b475d, 0x0b4762, detail::query},    // query/create-and-notify 0x0b4730 (VM 0x0b5913)
    {0x0b4b45, 0x0b4b4a, detail::savegame}, // savegame MOVI restore 0x0b4a50 (load 0x07985c)
    {0x0b4ca4, 0x0b4ca9, detail::script},   // script play-by-id 0x0b4c50
    {0x0b4ee2, 0x0b4ee7, detail::speech},   // speech 0x0b4e10
    {0x1138bc, 0x1138c1, detail::other},    // other media helper 0x113890
    {0x066126, 0x06612b, detail::selector}, // the sector selector 0x065030, the old helper inlined
};
constexpr unsigned wrapper_caller_count = sizeof wrapper_callers / sizeof wrapper_callers[0];
constexpr std::uint32_t selector_kind = 0x5a;
constexpr std::uint32_t video_begin_rva = 0x0fed10, video_end_rva = 0x0ff578;
constexpr std::uint32_t owner_slot_rva = 0x1a84b4; // the media owner (the allocator's link, fixup at +0xcb)

// The game's stack as the FL gate's handler sees it (the emitter in media_cue_fl.cpp; 0xac bytes).
struct EnterFrame {
    unsigned char xmm[8][16];
    std::uint32_t edx, ecx, eax, eflags; // EDX = constructor flags, ECX = id
    std::uint32_t ret;                   // [esp]: the wrapper's return 0x0b46c3 (rewritten on an observed PASS)
    std::uint32_t saved_edi, saved_esi;  // [esp+4], [esp+8]
    std::uint32_t caller;                // [esp+0xc]: the wrapper caller's return
    std::uint32_t id_copy, flags_copy;   // [esp+0x10], [esp+0x14]
};
struct ReturnFrame {
    unsigned char xmm[8][16];
    std::uint32_t edx, ecx, eax, eflags;
    std::uint32_t slot; // the reserved slot the original return address goes back into
};
static_assert(sizeof(EnterFrame) == 0xa8, "FL gate frame layout");
static_assert(sizeof(ReturnFrame) == 0x94, "FL return trampoline frame layout");

// The cue's class: only the allocator's own wrapper return is classified, by the wrapper caller's return; anything
// else (a foreign entry) is `other`.
inline detail::Caller classify(std::uint32_t ret, std::uint32_t caller, std::uintptr_t base) {
    if (ret != std::uint32_t(base + wrapper_return_rva)) return detail::other;
    for (const WrapperCaller& c : wrapper_callers)
        if (caller == std::uint32_t(base + c.return_rva)) return c.caller;
    return detail::other;
}
inline std::uint32_t kind_of(detail::Caller c) {
    return c == detail::selector ? selector_kind : 0u;
}

// ---- The gate stub and its return trampoline (one arena block, 299 bytes) ----
// Entry: pushfd; push eax/ecx/edx; cld; sub esp,0x80; 8 movups saves; push esp; call enter; add esp,4; test eax,eax;
// jz refuse. PASS (start+64): 8 movups loads; add esp,0x80; pop edx/ecx/eax; popfd; jmp [next] (next = start+120, the
// claim tail: the displaced pushes at the game's exact ESP). REFUSE (start+124): the same restore, then xor eax,eax;
// ret (the failed allocator's own exit: no stack argument, ZF=PF=1, CF=OF=SF=0). Trampoline (start+177): lea
// esp,[esp-4] (the slot, flags untouched: the allocator's outgoing EFLAGS are what pushfd saves); the same saves;
// push esp; call ret_handler (returns the original return address); add esp,4; mov [esp+0x90],eax; the same restore;
// ret. `at` is the block's 4-aligned address; `enter`/`ret_handler` absolute targets. Returns the bytes written.
constexpr unsigned gate_length = 299, gate_pass = 64, gate_next = 120, gate_refuse = 124, gate_trampoline = 177;
inline unsigned encode_gate(std::uint32_t at, std::uint32_t enter, std::uint32_t ret_handler, unsigned char out[gate_length]) {
    unsigned n = 0;
    const auto put = [&](unsigned char b) { out[n++] = b; };
    const auto put32 = [&](std::uint32_t v) {
        for (unsigned k = 0; k < 4; ++k) put(static_cast<unsigned char>(v >> (8 * k)));
    };
    const auto save = [&] {
        put(0x9c), put(0x50), put(0x51), put(0x52), put(0xfc), put(0x81), put(0xec), put32(0x80);
        for (unsigned i = 0; i < 8; ++i)
            put(0x0f), put(0x11), put(static_cast<unsigned char>(0x44 | (i << 3))), put(0x24),
                put(static_cast<unsigned char>(i * 16));
    };
    const auto restore = [&] {
        for (unsigned i = 0; i < 8; ++i)
            put(0x0f), put(0x10), put(static_cast<unsigned char>(0x44 | (i << 3))), put(0x24),
                put(static_cast<unsigned char>(i * 16));
        put(0x81), put(0xc4), put32(0x80), put(0x5a), put(0x59), put(0x58), put(0x9d);
    };
    save();
    put(0x54), put(0xe8), put32(enter - (at + n + 4));
    put(0x83), put(0xc4), put(4), put(0x85), put(0xc0), put(0x74), put(gate_refuse - gate_pass);
    restore();
    put(0xff), put(0x25), put32(at + gate_next);
    while (n < gate_next) put(0xcc);
    put32(0);
    restore();
    put(0x33), put(0xc0), put(0xc3);
    put(0x8d), put(0x64), put(0x24), put(0xfc); // lea esp,[esp-4]
    save();
    put(0x54), put(0xe8), put32(ret_handler - (at + n + 4));
    put(0x83), put(0xc4), put(4), put(0x89), put(0x84), put(0x24), put32(0x90);
    restore();
    put(0xc3);
    return n; // gate_length when every offset above held
}
}
