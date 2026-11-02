#pragma once
#include <cstdint>
#include <cstring>

// FL edition of the engine-effects call redirects' site contract (the AP twin is src/proxy/engine_effects_sites.h).
// Verified facts: docs/reverse-engineering/fl/engine-effects.md, "Verified 2026-11-02 against X3FL.exe" (producer
// verification/results/fl-engine-effects/produce.py, contract.json). X3FL.exe is relocatable: every value below is an
// RVA added to x3m::fl::module_base(), or a field offset. No Windows dependency (the native fixture compiles it).
//
// Per-ship engine effect routine RVA 0x014d90 (obj ECX -> EDI, class word obj+0x48 dispatched at 0x014e07; only
// classes 7 and 10 reach the windows). At both windows EBX = k, ESI = the JET child node, EDI = obj.
//
// 015054  f7 86 60 02 00 00 00 40 00 00  TEST dword [ESI+0x260],0x4000     <- window A (41 bytes, no fixup)
// 01505e  75 1d                          JNE  join_a 0x01507d
// 015060  6a 00 / 6a 00                  PUSH 0 / PUSH 0
// 015064  8d 4c 24 48                    LEA  ECX,[ESP+0x48]               ; &pos
// 015068  8b d3                          MOV  EDX,EBX                      ; k
// 01506a  51 / 6a 00 x3 / 57 / 50        PUSH ECX / PUSH 0 x3 / PUSH EDI (obj) / PUSH EAX (eff)
// 015073  33 c9                          XOR  ECX,ECX
// 015075  e8 66 00 00 00                 CALL 0x0150e0                     <- site A: ECX, EDX, 8 dwords, plain RET
// 01507a  83 c4 20                       ADD  ESP,0x20                     ; caller pop; end of window A
// 015085  f7 86 60 02 00 00 00 20 00 00  TEST dword [ESI+0x260],0x2000     <- window B (42 bytes)
// 01508f  75 24                          JNE  join_b 0x0150b5
// 015091  a1 <0x5a83b0>                  MOV  EAX,[cfg slot]               ; the one fixup, at window offset +0xd
// 015096  f7 80 ec 00 00 00 00 00 00 40  TEST dword [EAX+0xec],0x40000000  ; VideoD3DFlags bit 30
// 0150a0  74 13                          JE   join_b
// 0150a2  8d 44 24 40 / 50 / 57 / 51 / 53  LEA EAX,[ESP+0x40] / PUSH EAX (&pos) / PUSH EDI (obj) / PUSH ECX / PUSH EBX
// 0150aa  e8 91 e5 ff ff                 CALL 0x013640                     <- site B: 4 dwords, callee RET 0x10
//
// A's callee reads ECX and EDX (its instance key): a forward must leave both untouched. At stub entry obj is
// [esp+8] (A) or [esp+0xc] (B). EAX/ECX/EDX/flags/XMM0 are dead after either call; EBX/ESI/EDI/EBP are live.
// A's five bytes cross the aligned qword at 0x015078 (plain copy, install window only); B's lie inside one qword
// (one lock cmpxchg8b). Both callees have other direct callers (A 10, B 3): call-site claims only.
namespace x3m::fl::engine_effects_fl {
constexpr std::uint32_t routine_rva = 0x014d90, class_dispatch_rva = 0x014e07;
constexpr std::uint32_t window_a_rva = 0x015054, a_site_rva = 0x015075, target_a_rva = 0x0150e0;
constexpr std::uint32_t return_a_rva = 0x01507a, join_a_rva = 0x01507d;
constexpr std::uint32_t window_b_rva = 0x015085, b_site_rva = 0x0150aa, target_b_rva = 0x013640;
constexpr std::uint32_t return_b_rva = 0x0150af, join_b_rva = 0x0150b5;
constexpr std::uint32_t config_slot_rva = 0x1a83b0; // the relocated operand of window B
constexpr unsigned window_a_length = 41, window_b_length = 42, site_a_offset = 0x21, site_b_offset = 0x25;
constexpr unsigned window_b_fixup_offset = 0x0d, call_length = 5;
constexpr std::uint32_t window_a_fnv1a = 0x5a69143c, window_b_fnv1a = 0x601f528c; // B with the fixup zeroed
constexpr unsigned char expected_window_a[window_a_length] = {
    0xf7, 0x86, 0x60, 0x02, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x75, 0x1d, 0x6a, 0x00, 0x6a, 0x00, 0x8d,
    0x4c, 0x24, 0x48, 0x8b, 0xd3, 0x51, 0x6a, 0x00, 0x6a, 0x00, 0x6a, 0x00, 0x57, 0x50, 0x33, 0xc9, 0xe8,
    0x66, 0x00, 0x00, 0x00, 0x83, 0xc4, 0x20};
// The fixup dword (+0xd) is zeroed here; at the live base it must equal base + config_slot_rva.
constexpr unsigned char expected_window_b[window_b_length] = {
    0xf7, 0x86, 0x60, 0x02, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x75, 0x24, 0xa1, 0x00, 0x00, 0x00, 0x00,
    0xf7, 0x80, 0xec, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x74, 0x13, 0x8d, 0x44, 0x24, 0x40, 0x50,
    0x57, 0x51, 0x53, 0xe8, 0x91, 0xe5, 0xff, 0xff};
// The stub contract.
constexpr unsigned obj_arg_a = 0x08, obj_arg_b = 0x0c; // [esp+n] at stub entry
constexpr unsigned caller_pop_a = 0x20;                // A: eight dwords, the caller's add esp,0x20, plain RET
constexpr unsigned callee_pop_b = 0x10;                // B: four dwords, RET 0x10
constexpr unsigned class_offset = 0x48;                // int16 object class (movzx at 0x014e07)
constexpr std::uint16_t ship_class = 7;                // skipped; 10 (missiles) forwarded

// The engine's body manager slot (the same slot object_trace_fl binds) and the SETA tick's reads (12 bytes at
// seta_site_rva: mov eax,[ecx+0xbc]; imul dword [ecx+0xc0]; no fixup), cfg = *(base + config_slot_rva).
constexpr std::uint32_t body_slot_rva = 0x1aa100;
constexpr std::uint32_t seta_site_rva = 0x0fff26, seta_warp_offset = 0xbc;
constexpr unsigned seta_site_length = 12;
constexpr unsigned char expected_seta_site[seta_site_length] = {0x8b, 0x81, 0xbc, 0x00, 0x00, 0x00,
                                                                0xf7, 0xa9, 0xc0, 0x00, 0x00, 0x00};
constexpr std::uint32_t seta_site_fnv1a = 0x625a39c5;

constexpr std::uint32_t fnv1a(const unsigned char* p, unsigned n) {
    std::uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
    return h;
}
constexpr std::uint32_t rel32_at(const unsigned char* bytes) {
    return std::uint32_t(bytes[0]) | std::uint32_t(bytes[1]) << 8 | std::uint32_t(bytes[2]) << 16 |
           std::uint32_t(bytes[3]) << 24;
}
// The install decision on the bytes read at base + window_?_rva: nullptr when they are exactly the engine's (B's fixup
// equal to base + config_slot_rva, the other 38 bytes as listed), else the refusal.
inline const char* plan_a(const unsigned char current[window_a_length]) {
    return std::memcmp(current, expected_window_a, window_a_length) ? "bytes_mismatch_a" : nullptr;
}
inline const char* plan_b(const unsigned char current[window_b_length], std::uintptr_t base) {
    if (rel32_at(current + window_b_fixup_offset) != std::uint32_t(base + config_slot_rva)) return "bytes_mismatch_b";
    unsigned char masked[window_b_length];
    std::memcpy(masked, current, window_b_length);
    std::memset(masked + window_b_fixup_offset, 0, 4);
    return std::memcmp(masked, expected_window_b, window_b_length) ? "bytes_mismatch_b" : nullptr;
}

static_assert(fnv1a(expected_window_a, window_a_length) == window_a_fnv1a, "window A is the measured one");
static_assert(fnv1a(expected_window_b, window_b_length) == window_b_fnv1a, "window B (fixup zeroed) is the measured one");
static_assert(fnv1a(expected_seta_site, seta_site_length) == seta_site_fnv1a, "the SETA reads are the measured ones");
static_assert(window_a_rva + site_a_offset == a_site_rva && a_site_rva + call_length == return_a_rva &&
                  window_a_rva + window_a_length == join_a_rva,
              "window A ends with the call and the caller's add esp,0x20");
static_assert(window_b_rva + site_b_offset == b_site_rva && b_site_rva + call_length == return_b_rva &&
                  site_b_offset + call_length == window_b_length,
              "window B ends with the call");
static_assert(expected_window_a[site_a_offset] == 0xe8 &&
                  return_a_rva + rel32_at(expected_window_a + site_a_offset + 1) == target_a_rva,
              "CALL 0x0150e0");
static_assert(expected_window_b[site_b_offset] == 0xe8 &&
                  std::uint32_t(return_b_rva + rel32_at(expected_window_b + site_b_offset + 1)) == target_b_rva,
              "CALL 0x013640");
static_assert(expected_window_a[site_a_offset - 2] == 0x33 && expected_window_a[site_a_offset - 1] == 0xc9,
              "xor ecx,ecx before A: ECX is an argument");
static_assert(expected_window_a[site_a_offset + 5] == 0x83 && expected_window_a[site_a_offset + 6] == 0xc4 &&
                  expected_window_a[site_a_offset + 7] == caller_pop_a,
              "the caller pops 0x20 after A");
static_assert(expected_window_a[10] == 0x75 && window_a_rva + 12 + expected_window_a[11] == join_a_rva,
              "A's guard jne lands on the join after the caller pop");
static_assert(expected_window_b[10] == 0x75 && window_b_rva + 12 + expected_window_b[11] == join_b_rva &&
                  expected_window_b[27] == 0x74 && window_b_rva + 29 + expected_window_b[28] == join_b_rva,
              "B's guards land on the join");
static_assert(expected_window_b[window_b_fixup_offset - 1] == 0xa1, "B's fixup is mov eax,[moffs32]");
// The module base is 64 KB aligned, so a site's qword geometry is its RVA's.
static_assert((a_site_rva & 7u) + call_length > 8u, "A's five bytes cross an aligned 8-byte word (plain write)");
static_assert((b_site_rva & 7u) + call_length <= 8u, "B's five bytes lie inside one aligned 8-byte word (atomic)");
static_assert(routine_rva < window_a_rva && join_a_rva <= window_b_rva && return_b_rva < target_a_rva,
              "two disjoint windows inside the routine, before A's callee");
}
