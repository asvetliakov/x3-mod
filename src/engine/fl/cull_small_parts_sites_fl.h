#pragma once
#include <cstdint>
#include <cstring>
#include "../../proxy/cull_small_parts_core.h"

// FL edition of the small-parts cull site contract (the AP twin is src/proxy/cull_small_parts_core.h, whose pure rules
// this header reuses: parse_px, valid_px, threshold_for, upper_for, the scene latch, dock_model's ranges, the
// projectile and JET flag words). Verified facts: docs/reverse-engineering/fl/cull-chain.md, "Verified 2026-11-02
// against X3FL.exe" (producer verification/results/fl-cull-chain/produce.py, contract.json). X3FL.exe is
// relocatable: every value below is an RVA added to x3m::fl::module_base(), or a field offset. No Windows dependency
// (the native fixture and the host tests compile it).
//
// Selector [0x095e50, 0x096446): ECX = node -> ESI (0x095e5e), camera [EBP+8] -> EBX (0x0960d5), RET 8.
//
// 0961c5  83 f9 14                 CMP  ECX,0x14                     <- window (55 bytes, no fixup)
// 0961c8  7d 09                    JGE  0x0961d3
// 0961ca  33 c9                    XOR  ECX,ECX                      ; env-map view: native measure zeroed
// 0961cc  83 a6 2c 01 00 00 fd     AND  dword [ESI+0x12c],~2
// 0961d3  8b 46 18                 MOV  EAX,[ESI+0x18]               <- site (3 + 6 bytes displaced)
// 0961d6  8b 96 d8 01 00 00        MOV  EDX,[ESI+0x1d8]
// 0961dc  85 c0                    TEST EAX,EAX                      <- continuation (the tail jumps back here)
// 0961de  74 0b                    JE   0x0961eb
// 0961e0  8b 80 d8 01 00 00        MOV  EAX,[EAX+0x1d8]              ; parent threshold
// 0961e6  3b c2                    CMP  EAX,EDX
// 0961e8  0f 4f d0                 CMOVG EDX,EAX                     ; EDX = max(own, parent)
// 0961eb  85 d2                    TEST EDX,EDX
// 0961ed  7e 0d                    JLE  0x0961fc
// 0961ef  3b ca                    CMP  ECX,EDX                      ; native measure < limit ?
// 0961f1  7d 09                    JGE  0x0961fc
// 0961f3  83 a6 2c 01 00 00 fd     AND  dword [ESI+0x12c],~2         <- cull: the engine's size-cull path
// 0961fa  eb 05                    JMP  0x096201                     ; end of window
//
// Entries to the site: je 0x0961b9, jge 0x0961c8, fall-through from 0x0961cc; nothing enters (0x0961d3, 0x0961dc).
// At the site: ESI = node, ECX = native measure r*W/D (LIVE: read at 0x0961ef/0x0961fc), [ESP+0x10] = s = r*640/D
// (>= 1, or 0x7000000; copy at [ESP+0x18]), EDI = D, EBX = camera = [EBP+8], ESP = align_down(EBP,8) - 0x28. Dead: EAX,
// EDX (the displaced loads write them), EFLAGS (0x0961dc TEST, or the cull AND, write them before any read). The cull
// target 0x0961f3 has no direct entry (fall-through only); from there 0x096201 escapes to LOD selection when +0x12c bit
// 26 is set (as AP 0x47d2d1), else stores and visits the children (0x096217 -> 0x095f24). The five jump bytes lie in
// the aligned qword [0x0961d0, 0x0961d8): one lock cmpxchg8b; the four remaining displaced bytes stay unreachable.
namespace x3m::fl::cull_small_parts_fl {
namespace small = x3m::cull_small_parts::core;
constexpr std::uint32_t selector_rva = 0x095e50, selector_end_rva = 0x096446;
constexpr std::uint32_t window_rva = 0x0961c5, site_rva = 0x0961d3, continue_rva = 0x0961dc, cull_rva = 0x0961f3,
                        after_cull_rva = 0x096201, size_pass_rva = 0x0961fc;
constexpr unsigned window_length = 55, site_offset = 14, site_length = 9, cull_offset = 46, ret_pop = 8;
constexpr std::uint32_t window_fnv1a = 0x9df9cf9e;
// clang-format off
constexpr unsigned char window[window_length] = {
    0x83,0xf9,0x14, 0x7d,0x09, 0x33,0xc9, 0x83,0xa6,0x2c,0x01,0x00,0x00,0xfd,
    0x8b,0x46,0x18, 0x8b,0x96,0xd8,0x01,0x00,0x00, 0x85,0xc0, 0x74,0x0b, 0x8b,0x80,0xd8,0x01,0x00,0x00,
    0x3b,0xc2, 0x0f,0x4f,0xd0, 0x85,0xd2, 0x7e,0x0d, 0x3b,0xca, 0x7d,0x09,
    0x83,0xa6,0x2c,0x01,0x00,0x00,0xfd, 0xeb,0x05};
// clang-format on
constexpr unsigned char site[site_length] = {0x8b, 0x46, 0x18, 0x8b, 0x96, 0xd8, 0x01, 0x00, 0x00};
// Node fields (cull-chain.md "Node layout"): parent, flags and thresholds as on AP; the model id moves +0x140 -> +0x144.
constexpr unsigned parent_offset = 0x18, flags12c_offset = 0x12c, flags130_offset = 0x130, model_offset = 0x144,
                   threshold_1d8_offset = 0x1d8;
constexpr unsigned s_slot = 0x10; // [ESP+n] at the site: s (the pixel-policy metric); no push precedes its read

// The projectile marker (class-0 object creation, the same constant as AP 0x004401ae / 0x0044123b) and the JET writer
// (AP 0x00434708), pinned at install like the AP module (contract.json windows projectile_store, projectile_or,
// jet_writer; no fixup in any of them):
// 049b1d  c7 45 e0 00 00 80 20                MOV dword [EBP-0x20],0x20800000
// 04a807  8b 46 70 / 8b 4d e0 / 09 88 30 01 00 00   MOV EAX,[ESI+0x70]; MOV ECX,[EBP-0x20]; OR [EAX+0x130],ECX
// 03deb3  81 89 30 01 00 00 01 00 00 04       OR dword [ECX+0x130],0x4000001
constexpr std::uint32_t marker_store_rva = 0x049b1d, marker_or_rva = 0x04a807, jet_writer_rva = 0x03deb3;
constexpr unsigned marker_store_length = 7, marker_or_length = 12, jet_writer_length = 10;
constexpr unsigned char marker_store[marker_store_length] = {0xc7, 0x45, 0xe0, 0x00, 0x00, 0x80, 0x20};
constexpr unsigned char marker_or[marker_or_length] = {0x8b, 0x46, 0x70, 0x8b, 0x4d, 0xe0,
                                                       0x09, 0x88, 0x30, 0x01, 0x00, 0x00};
constexpr unsigned char jet_writer[jet_writer_length] = {0x81, 0x89, 0x30, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x04};
constexpr std::uint32_t marker_store_fnv1a = 0x7fd6e34b, marker_or_fnv1a = 0x147c2006, jet_writer_fnv1a = 0x4d34a219;

constexpr std::uint32_t fnv1a(const unsigned char* p, unsigned n) {
    std::uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
    return h;
}
// The install decision on the 55 bytes read at base + window_rva: nullptr when they are exactly the engine's.
inline const char* plan(const unsigned char current[window_length]) {
    return std::memcmp(current, window, window_length) ? "bytes_mismatch" : nullptr;
}

// The stub (176 bytes), entered by the dispatcher's `jmp [entry]` with the site's exact register state and ESP (no
// return address). EAX and EDX are its scratch (dead at the site); ECX, the live native measure, is never written
// except by the push/pop pair around the far-jet call. `upper` = upper_for(threshold, dock threshold):
//    0  83 3d abs32 00                  CMP  dword [upper],0        ; 0 outside an armed frame
//    7  7e 34                           JLE  continue
//    9  a1 abs32                        MOV  EAX,[upper]
//   14  39 44 24 10                     CMP  [ESP+0x10],EAX         ; s - upper
//   18  7d 29                           JGE  continue               ; s >= upper: the engine's own compare
//   20  a1 abs32                        MOV  EAX,[threshold]
//   25  39 44 24 10                     CMP  [ESP+0x10],EAX         ; s - threshold
//   29  7c 38                           JL   small                  ; below the small-parts threshold: any node
//   31  8b 86 44 01 00 00               MOV  EAX,[ESI+0x144]        ; threshold <= s < upper: dock-port ids only
//   37  2d imm32                        SUB  EAX,901300000
//   42  3d imm32                        CMP  EAX,200000
//   47  72 12                           JB   dock
//   49  2d imm32                        SUB  EAX,8500000            ; id - 909800000
//   54  3d imm32                        CMP  EAX,200000
//   59  72 06                           JB   dock
//   61  ff 25 abs32                     JMP  [next]                 ; continue: the tail (displaced 9 bytes, jmp 0x0961dc)
//   67  f7 86 30 01 00 00 00 00 00 20   TEST dword [ESI+0x130],0x20000000   ; dock: projectile marker
//   77  75 59                           JNE  exempt
//   79  ff 05 abs32                     INC  dword [dock_culled]
//   85  eb 34                           JMP  replay
//   87  f7 86 30 01 00 00 00 00 00 20   TEST dword [ESI+0x130],0x20000000   ; small: projectile marker
//   97  75 45                           JNE  exempt
//   99  f7 86 30 01 00 00 00 00 00 04   TEST dword [ESI+0x130],0x4000000    ; far jet: bit 26 ...
//  109  74 16                           JE   count
//  111  f6 86 30 01 00 00 01            TEST byte [ESI+0x130],1             ; ... and bit 0
//  118  74 0d                           JE   count
//  120  51                              PUSH ECX                    ; the live measure, saved
//  121  53 51 56                        PUSH EBX; PUSH ECX; PUSH ESI ; view (camera), measure, node
//  124  e8 rel32                        CALL x3m_engine_far_jet     ; cdecl, integer only, no Win32
//  129  83 c4 0c                        ADD  ESP,12
//  132  59                              POP  ECX
//  133  ff 05 abs32                     INC  dword [culled]         ; count
//  139  8b 46 18                        MOV  EAX,[ESI+0x18]         ; replay: 0x0961d3..0x0961e8, so EAX/EDX arrive
//  142  8b 96 d8 01 00 00               MOV  EDX,[ESI+0x1d8]        ;   at the cull exactly as the engine leaves them
//  148  85 c0                           TEST EAX,EAX
//  150  74 0b                           JE   cull
//  152  8b 80 d8 01 00 00               MOV  EAX,[EAX+0x1d8]
//  158  3b c2                           CMP  EAX,EDX
//  160  0f 4f d0                        CMOVG EDX,EAX
//  163  e9 rel32                        JMP  base+0x0961f3          ; cull: `and [esi+0x12c],~2; jmp 0x096201`
//  168  ff 05 abs32                     INC  dword [exempt]         ; exempt: count, then the vanilla compare
//  174  eb 8d                           JMP  continue
// LastError, x87, MXCSR, XMM and DF are untouched: the only call is the far-jet handler (engine_far_jets.cpp, integer
// code without SSE/MMX or Win32, its own CMake flags), ECX is saved around it, EAX/EDX are dead and rewritten by the
// replay, EBX/ESI/EDI/EBP are callee-saved. ESP and every native local are unchanged on every exit. The only memory the
// stub writes is its three counters and the call's argument area below the site ESP.
//
// Projectiles `off` replaces bytes 67..78 and 87..98 with `eb 0a` (JMP +10) and int3 padding: the marker is not read.
// Far jets off replaces bytes 99..132 with `eb 20` (JMP +32, to the count) and int3 padding: nothing is called.
constexpr unsigned stub_length = 176, stub_continue = 61, stub_dock = 67, stub_dock_count = 79, stub_small = 87,
                   stub_far = 99, stub_far_call = 124, stub_count = 133, stub_replay = 139, stub_cull = 163,
                   stub_exempt = 168;
constexpr unsigned stub_far_length = 34, stub_marker_length = 12;
inline void encode_stub(std::uint32_t at, std::uint32_t threshold, std::uint32_t upper, std::uint32_t culled,
                        std::uint32_t exempt, std::uint32_t dock_culled, std::uint32_t cull_target,
                        std::uint32_t next_slot, unsigned char out[stub_length], bool exempt_projectiles,
                        std::uint32_t far_handler, bool far_jets) {
    unsigned n = 0;
    auto b = [&](unsigned char v) { out[n++] = v; };
    auto d = [&](std::uint32_t v) {
        std::memcpy(out + n, &v, 4);
        n += 4;
    };
    auto rel8 = [&](unsigned target) { b(static_cast<unsigned char>(static_cast<int>(target) - static_cast<int>(n + 1))); };
    auto marker = [&]() {
        b(0xf7); b(0x86); d(flags130_offset); d(small::projectile_flag);
        b(0x75); rel8(stub_exempt);
    };
    // clang-format off
    b(0x83); b(0x3d); d(upper); b(0x00);                    // 0
    b(0x7e); rel8(stub_continue);                            // 7
    b(0xa1); d(upper);                                       // 9
    b(0x39); b(0x44); b(0x24); b(s_slot);                    // 14
    b(0x7d); rel8(stub_continue);                            // 18
    b(0xa1); d(threshold);                                   // 20
    b(0x39); b(0x44); b(0x24); b(s_slot);                    // 25
    b(0x7c); rel8(stub_small);                               // 29
    b(0x8b); b(0x86); d(model_offset);                       // 31
    b(0x2d); d(small::dock_first_base);                      // 37
    b(0x3d); d(small::dock_span);                            // 42
    b(0x72); rel8(stub_dock);                                // 47
    b(0x2d); d(small::dock_second_base - small::dock_first_base); // 49
    b(0x3d); d(small::dock_span);                            // 54
    b(0x72); rel8(stub_dock);                                // 59
    b(0xff); b(0x25); d(next_slot);                          // 61 continue
    marker();                                                // 67 dock
    b(0xff); b(0x05); d(dock_culled);                        // 79
    b(0xeb); rel8(stub_replay);                              // 85
    marker();                                                // 87 small
    b(0xf7); b(0x86); d(flags130_offset); d(small::jet_flag_high); // 99 far jet
    b(0x74); rel8(stub_count);
    b(0xf6); b(0x86); d(flags130_offset); b(static_cast<unsigned char>(small::jet_flag_low));
    b(0x74); rel8(stub_count);
    b(0x51);                                                 // 120 save the measure
    b(0x53); b(0x51); b(0x56);                               // 121 view, measure, node
    b(0xe8); d(far_handler - (at + stub_far_call + 5));      // 124
    b(0x83); b(0xc4); b(0x0c);                               // 129
    b(0x59);                                                 // 132
    b(0xff); b(0x05); d(culled);                             // 133 count
    b(0x8b); b(0x46); b(parent_offset);                      // 139 replay
    b(0x8b); b(0x96); d(threshold_1d8_offset);
    b(0x85); b(0xc0);
    b(0x74); rel8(stub_cull);
    b(0x8b); b(0x80); d(threshold_1d8_offset);
    b(0x3b); b(0xc2);
    b(0x0f); b(0x4f); b(0xd0);
    b(0xe9); d(cull_target - (at + stub_exempt));            // 163 cull
    b(0xff); b(0x05); d(exempt);                             // 168 exempt
    b(0xeb); rel8(stub_continue);                            // 174
    // clang-format on
    if (!exempt_projectiles) {
        const unsigned markers[2] = {stub_dock, stub_small};
        for (unsigned at_marker : markers) {
            out[at_marker] = 0xeb;
            out[at_marker + 1] = static_cast<unsigned char>(stub_marker_length - 2);
            std::memset(out + at_marker + 2, 0xcc, stub_marker_length - 2);
        }
    }
    if (!far_jets) {
        out[stub_far] = 0xeb;
        out[stub_far + 1] = static_cast<unsigned char>(stub_far_length - 2);
        std::memset(out + stub_far + 2, 0xcc, stub_far_length - 2);
    }
}

constexpr bool window_holds_site() {
    for (unsigned i = 0; i < site_length; ++i)
        if (window[site_offset + i] != site[i]) return false;
    return true;
}
static_assert(window_rva + site_offset == site_rva && site_rva + site_length == continue_rva &&
                  window_rva + cull_offset == cull_rva && window_rva + window_length == size_pass_rva,
              "window offsets");
static_assert(window_holds_site(), "the window carries the displaced span");
static_assert(window[cull_offset] == 0x83 && window[cull_offset + 1] == 0xa6 && window[cull_offset + 2] == 0x2c,
              "the cull instruction is the engine's AND [ESI+0x12c],~2");
static_assert(fnv1a(window, window_length) == window_fnv1a, "the verified window identity (contract.json)");
static_assert(fnv1a(marker_store, marker_store_length) == marker_store_fnv1a &&
                  fnv1a(marker_or, marker_or_length) == marker_or_fnv1a &&
                  fnv1a(jet_writer, jet_writer_length) == jet_writer_fnv1a,
              "the verified marker and JET writer identities (contract.json)");
static_assert((site_rva & 7u) + 5u <= 8u, "the five jump bytes lie inside one aligned qword (atomic write)");
static_assert(selector_rva < window_rva && after_cull_rva < selector_end_rva, "inside the selector");
static_assert(int(stub_continue) - int(stub_length) >= -128, "the exempt block's rel8 reaches continue");
static_assert(stub_far + stub_far_length == stub_count && stub_dock + stub_marker_length + 6 + 2 == stub_small,
              "patched-out blocks");
}
