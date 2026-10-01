#pragma once
#include <cstdint>
#include <cstring>

// Portable core of the engine-effects call redirects (docs/reverse-engineering/engine-effects.md section 7,
// docs/architecture/engine-effects-modern.md phase 1b): the two verified windows inside the per-ship engine effect
// routine 0x00414590, the calls they redirect, the stub contract and the X3M_ENGINE_EFFECTS parser. No Windows
// dependency, so the host tests and the site verifier compile it directly.
//
// Child walk of 0x00414590 (EBX = the JET child node, EBP = the routine's frame, [ebp+8] = obj; at the window
// starts [esp+0x10] = k, [esp+0x14] = eff, [esp+0x18] = trail, [esp+0x20..0x2c] = pos):
//
// 004147c4  f7 83 60 02 00 00 00 40 00 00  TEST dword [EBX+0x260],0x4000      <- window A (47 bytes)
// 004147ce  75 23                          JNE  0x004147f3
// 004147d0  8b 55 08                       MOV  EDX,[EBP+0x8]                 ; obj
// 004147d3  6a 00 / 6a 00                  PUSH 0 / PUSH 0
// 004147d7  8d 4c 24 28                    LEA  ECX,[ESP+0x28]                ; &pos
// 004147db  51 / 6a 00 x3 / 52 / 50        PUSH ECX / PUSH 0 x3 / PUSH EDX / PUSH EAX (eff)
// 004147e4  8b 44 24 30                    MOV  EAX,[ESP+0x30]                ; k
// 004147e8  50 / 6a 00                     PUSH EAX / PUSH 0
// 004147eb  e8 b0 00 00 00                 CALL 0x004148a0                    <- site A (cdecl, 10 dwords)
// 004147f0  83 c4 28                       ADD  ESP,0x28                      ; end of window A
// 004147f3  8b 44 24 18 / 85 c0 / 0f 8e .. MOV EAX,[ESP+0x18] / TEST / JLE 0x0041487c (trail > 0)
// 004147ff  f7 83 60 02 00 00 00 20 00 00  TEST dword [EBX+0x260],0x2000      <- window B (50 bytes)
// 00414809  75 71                          JNE  0x0041487c
// 0041480b  8b 0d 34 6f 60 00              MOV  ECX,[0x00606f34]
// 00414811  f7 81 fc 00 00 00 00 00 00 40  TEST dword [ECX+0xfc],0x40000000   ; VideoD3DFlags bit 30
// 0041481b  74 5f                          JE   0x0041487c
// 0041481d  8b 4d 08                       MOV  ECX,[EBP+0x8]                 ; obj
// 00414820  8d 54 24 20 / 52               LEA  EDX,[ESP+0x20] / PUSH EDX     ; &pos
// 00414825  8b 54 24 14                    MOV  EDX,[ESP+0x14]                ; k
// 00414829  51 / 50 / 52                   PUSH ECX / PUSH EAX (trail) / PUSH EDX
// 0041482c  e8 3f e5 ff ff                 CALL 0x00412d70                    <- site B (stdcall, ret 0x10)
// 00414831  eb 49                          JMP  0x0041487c                    ; (after window B)
//
// Each call is redirected (engine_patch::claim_call) to a stub in engine_effects_patch.cpp. At stub entry obj is
// [esp+0x10] (A) or [esp+0x0c] (B). The stub compares the 16-bit class word obj+0x48 with 7 (TShips): a ship
// returns at once (`xor eax,eax; ret` for A, whose caller pops 0x28; `xor eax,eax; ret 0x10` for B, the callee's
// own stdcall pop), which is the engine's `C & 0x4000` / `C & 0x2000` state: no effect instance (emitter sprite,
// lens flare) and no Particles3 trail link. Every other class (10, missiles, in practice) jumps to the original
// callee with the stack, EAX and every other register as the call left them (EFLAGS status bits aside: both
// callees write them before reading, 0x004148a3 `and esp` and 0x00412d88 `test`; DF is untouched). Both return
// values are unused and EAX/ECX/EDX/EFLAGS are dead after either call; EBX and EBP are live and preserved.
//
// obj is read without engine_memory validation: it is the routine's own argument [ebp+8], the pointer the routine
// already dereferenced on this path (`mov eax,[esi+0x70]` at 0x004145ac, `movzx edx,word [esi+0x48]` at 0x0041460c,
// the class dispatch that led to the child walk), pushed by the window from the same slot, on the same thread, with
// no call between the dispatch and the stub that can remove the object (A's callee 0x004148a0 only finds or
// creates an effect instance; B runs after it in the same iteration).
//
// A's five bytes lie inside the aligned qword 0x004147e8..0x004147ef: one lock cmpxchg8b. B's rel32
// 0x0041482d..0x00414830 crosses the qword boundary 0x00414830: claim_call takes the plain copy, which is safe only
// inside the install window (before the first Present; 0x00414590 runs only in the frame loop). No direct branch
// lands strictly inside either window and no image dword points into either (verify_engine_effects_sites.py).
namespace x3m::engine_effects_patch::sites {
constexpr std::uintptr_t routine_va = 0x00414590, routine_end_va = 0x0041489a;
constexpr std::uintptr_t window_a_va = 0x004147c4, a_site_va = 0x004147eb, target_a_va = 0x004148a0;
constexpr std::uintptr_t return_a_va = 0x004147f0, join_a_va = 0x004147f3;
constexpr std::uintptr_t window_b_va = 0x004147ff, b_site_va = 0x0041482c, target_b_va = 0x00412d70;
constexpr std::uintptr_t return_b_va = 0x00414831, join_b_va = 0x0041487c;
constexpr std::uintptr_t d3d_flags_slot_va = 0x00606f34; // VideoD3DFlags block pointer; the word is at +0xfc
constexpr unsigned window_a_length = 47, window_b_length = 50, site_a_offset = 0x27, site_b_offset = 0x2d;
constexpr unsigned call_length = 5;
constexpr unsigned char expected_window_a[window_a_length] = {
    0xf7, 0x83, 0x60, 0x02, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x75, 0x23, 0x8b, 0x55, 0x08, 0x6a,
    0x00, 0x6a, 0x00, 0x8d, 0x4c, 0x24, 0x28, 0x51, 0x6a, 0x00, 0x6a, 0x00, 0x6a, 0x00, 0x52, 0x50,
    0x8b, 0x44, 0x24, 0x30, 0x50, 0x6a, 0x00, 0xe8, 0xb0, 0x00, 0x00, 0x00, 0x83, 0xc4, 0x28};
constexpr unsigned char expected_window_b[window_b_length] = {
    0xf7, 0x83, 0x60, 0x02, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x75, 0x71, 0x8b, 0x0d, 0x34, 0x6f, 0x60,
    0x00, 0xf7, 0x81, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x74, 0x5f, 0x8b, 0x4d, 0x08, 0x8d,
    0x54, 0x24, 0x20, 0x52, 0x8b, 0x54, 0x24, 0x14, 0x51, 0x50, 0x52, 0xe8, 0x3f, 0xe5, 0xff, 0xff};
// The stub contract.
constexpr unsigned obj_arg_a = 0x10, obj_arg_b = 0x0c; // [esp+n] at stub entry
constexpr unsigned caller_pop_a = 0x28;                // A: __cdecl, 10 dwords, plain ret
constexpr unsigned callee_pop_b = 0x10;                // B: __stdcall, 4 dwords, ret 0x10
constexpr unsigned class_offset = 0x48;                // int16 object class
constexpr std::uint16_t ship_class = 7;                // TShips; 10 (missiles) and the rest are forwarded

// X3M_ENGINE_EFFECTS: native = the engine's bytes (unset or empty: the DLL default); off and plumes = both
// redirects (plumes adds the draw-path plumes, which arm only when both redirects are live); anything else
// (1..15 characters) is refused, 16 or more is too_long.
enum class Mode : unsigned char { native = 0, off = 1, plumes = 2 };
constexpr Mode default_mode = Mode::native;
constexpr unsigned setting_capacity = 16;
inline const char* mode_name(Mode m) {
    return m == Mode::off ? "off" : m == Mode::plumes ? "plumes" : "native";
}
// Exactly "native", "off" or "plumes" (lower case, nothing else).
template <class Char> inline bool parse_mode(const Char* text, Mode* out) {
    if (!text) return false;
    auto equals = [text](const char* word) {
        unsigned i = 0;
        for (; word[i]; ++i)
            if (text[i] != Char(word[i])) return false;
        return text[i] == Char(0);
    };
    if (equals("native"))
        *out = Mode::native;
    else if (equals("off"))
        *out = Mode::off;
    else if (equals("plumes"))
        *out = Mode::plumes;
    else
        return false;
    return true;
}
// The install decision on the bytes read at each window: nullptr when it is exactly the engine's, else the
// refusal reason (an already redirected or otherwise changed window is refused).
inline const char* plan_a(const unsigned char current[window_a_length]) {
    return std::memcmp(current, expected_window_a, window_a_length) ? "bytes_mismatch_a" : nullptr;
}
inline const char* plan_b(const unsigned char current[window_b_length]) {
    return std::memcmp(current, expected_window_b, window_b_length) ? "bytes_mismatch_b" : nullptr;
}

constexpr std::uint32_t rel32_at(const unsigned char* bytes) {
    return std::uint32_t(bytes[0]) | std::uint32_t(bytes[1]) << 8 | std::uint32_t(bytes[2]) << 16 |
           std::uint32_t(bytes[3]) << 24;
}
static_assert(window_a_va + site_a_offset == a_site_va && a_site_va + call_length == return_a_va &&
                  window_a_va + window_a_length == join_a_va,
              "window A ends with the call and the caller's add esp,0x28");
static_assert(window_b_va + site_b_offset == b_site_va && b_site_va + call_length == return_b_va &&
                  site_b_offset + call_length == window_b_length,
              "window B ends with the call");
static_assert(expected_window_a[site_a_offset] == 0xe8 &&
                  return_a_va + rel32_at(expected_window_a + site_a_offset + 1) == target_a_va,
              "CALL 0x004148a0");
static_assert(expected_window_b[site_b_offset] == 0xe8 &&
                  std::uint32_t(return_b_va + rel32_at(expected_window_b + site_b_offset + 1)) == target_b_va,
              "CALL 0x00412d70");
static_assert(expected_window_a[site_a_offset + 5] == 0x83 && expected_window_a[site_a_offset + 6] == 0xc4 &&
                  expected_window_a[site_a_offset + 7] == caller_pop_a,
              "the caller pops 0x28 after A");
static_assert(expected_window_a[10] == 0x75 && window_a_va + 12 + expected_window_a[11] == join_a_va,
              "A's guard jne lands on the join after the call");
static_assert(expected_window_b[10] == 0x75 && window_b_va + 12 + expected_window_b[11] == join_b_va &&
                  expected_window_b[28] == 0x74 && window_b_va + 30 + expected_window_b[29] == join_b_va,
              "B's guards land on the join");
static_assert(rel32_at(expected_window_b + 14) == d3d_flags_slot_va, "B reads the VideoD3DFlags block pointer");
static_assert((a_site_va & 7u) + call_length <= 8u, "A's five bytes lie inside one aligned 8-byte word (atomic)");
static_assert(((b_site_va + 1) & 7u) + 4u > 8u, "B's rel32 crosses an aligned 8-byte word (plain write)");
static_assert(routine_va < window_a_va && join_b_va < routine_end_va && window_a_va + window_a_length < window_b_va,
              "two disjoint windows inside 0x00414590");
}
