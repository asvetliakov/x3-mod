#pragma once
#include <cstdint>
#include <cstring>

// The two death sites of step 3, the death explosion (docs/archive/effects-look-2026-11-08/explosion-look.md section 1,
// docs/reverse-engineering/weapon-effect-spawn.md sections 4, 6 and 8.1; X3AP.exe SHA-256 fdbf3418...f8ab, preferred
// VAs). No Windows dependency: the host tests and the hook fixture compile it. Both windows are compared whole before
// any write (fail closed); the bytes are the installed EXE's (verification/probe/build_weapon_effects_hook.py reads them
// from /mnt/c/X3/X3AP.exe and records the match).
//
// Inside the kill routine 0x0044abd0 (thiscall, EBP its frame: [ebp+8] the reason word, [ebp+0xc] the killer,
// [ebp+0x10] arg4; EDI = the dying object), after the dying bit and the parent-is-a-sector test:
// the area-bullet path (any object with obj+0x40 & 0x40000000 whose TBullets[obj+0x4a] has flag 0x100; EBX = 0 from
// 0x0044ac37, read at 0x0044acf6 after the join):
// 0044ac5b  8b 4d 10           MOV ECX,[EBP+0x10]              ; arg4
// 0044ac5e  8b 55 0c           MOV EDX,[EBP+0xc]               ; the killer
// 0044ac61  51 52 57           PUSH ECX / PUSH EDX / PUSH EDI  ; arg4, killer, the dying object
// 0044ac64  e8 a7 89 fc ff     CALL 0x00413610                 <- the area site (cdecl, 3 dwords, plain ret)
// 0044ac69  83 c4 0c           ADD ESP,0xc
// 0044ac6c  8b c7              MOV EAX,EDI                     <- end of the window (19 bytes); EAX overwritten: the
//                                                                 return value is dead (then CALL 0x00450310)
// every other death (the same sequence at 0x0044aca9; EBX cleared again at 0x0044accc after it):
// 0044acb2  e8 59 89 fc ff     CALL 0x00413610                 <- the other site
// The area call's five bytes 0x0044ac64..68 straddle the aligned qword 0x0044ac68: claim_call's plain copy, inside the
// install window only (the kill routine runs only in the frame loop); the other's lie inside one qword (lock cmpxchg8b).
// No direct branch lands inside either call and no image dword points into them
// (verification/results/weapon-effect-spawn/weapon_effect_sites_out.txt). The stubs forward with every register, the
// flags and the stack as the call left them; the skip path returns `xor eax,eax; ret` (EAX, ECX, EDX and EFLAGS are dead
// after the call: the caller pops its three arguments and reloads EAX).
namespace x3m::weapon_effects::death_sites {
constexpr unsigned call_length = 5;
constexpr std::uintptr_t target_va = 0x00413610;
constexpr unsigned window_length = 19, site_offset = 9;
constexpr std::uintptr_t area_window_va = 0x0044ac5b, area_site_va = 0x0044ac64, area_return_va = 0x0044ac69,
                         area_window_end_va = 0x0044ac6e;
constexpr std::uintptr_t other_window_va = 0x0044aca9, other_site_va = 0x0044acb2, other_return_va = 0x0044acb7,
                         other_window_end_va = 0x0044acbc;
constexpr unsigned char expected_area_window[window_length] = {0x8b, 0x4d, 0x10, 0x8b, 0x55, 0x0c, 0x51, 0x52, 0x57, 0xe8,
                                                               0xa7, 0x89, 0xfc, 0xff, 0x83, 0xc4, 0x0c, 0x8b, 0xc7};
constexpr unsigned char expected_other_window[window_length] = {0x8b, 0x4d, 0x10, 0x8b, 0x55, 0x0c, 0x51, 0x52, 0x57, 0xe8,
                                                                0x59, 0x89, 0xfc, 0xff, 0x83, 0xc4, 0x0c, 0x8b, 0xc7};
// The stub contract (stack slots at stub entry, in dwords; the caller's EBP frame's reason word).
constexpr unsigned object_slot = 1, killer_slot = 2, arg4_slot = 3;

constexpr std::uint32_t rel32_at(const unsigned char* b) {
    return std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 | std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
}
inline const char* plan(const unsigned char* current, const unsigned char* expected) {
    return std::memcmp(current, expected, window_length) ? "bytes_mismatch" : nullptr;
}

static_assert(area_window_va + site_offset == area_site_va && area_site_va + call_length == area_return_va &&
                  area_window_va + window_length == area_window_end_va,
              "the area window holds the loads, the three pushes, the call, the pop and the EAX reload");
static_assert(other_window_va + site_offset == other_site_va && other_site_va + call_length == other_return_va &&
                  other_window_va + window_length == other_window_end_va,
              "the other window likewise");
static_assert(expected_area_window[0] == 0x8b && expected_area_window[1] == 0x4d && expected_area_window[2] == 0x10 &&
                  expected_area_window[3] == 0x8b && expected_area_window[4] == 0x55 && expected_area_window[5] == 0x0c,
              "mov ecx,[ebp+0x10]; mov edx,[ebp+0xc]: EBP is the kill routine's frame");
static_assert(expected_area_window[6] == 0x51 && expected_area_window[7] == 0x52 && expected_area_window[8] == 0x57,
              "push ecx (arg4); push edx (killer); push edi (the dying object, slot 1)");
static_assert(expected_area_window[site_offset] == 0xe8 &&
                  std::uint32_t(area_return_va + rel32_at(expected_area_window + site_offset + 1)) == target_va &&
                  expected_other_window[site_offset] == 0xe8 &&
                  std::uint32_t(other_return_va + rel32_at(expected_other_window + site_offset + 1)) == target_va,
              "CALL 0x00413610 at both sites");
static_assert(expected_area_window[14] == 0x83 && expected_area_window[15] == 0xc4 && expected_area_window[16] == 0x0c &&
                  expected_area_window[17] == 0x8b && expected_area_window[18] == 0xc7,
              "the caller pops three arguments (cdecl) and reloads EAX from EDI: the return value is dead");
static_assert(((area_site_va & 7u) + call_length) > 8u, "the area call straddles a qword (plain write, install window)");
static_assert(((other_site_va & 7u) + call_length) <= 8u, "the other call lies inside one aligned qword (atomic)");
} // namespace x3m::weapon_effects::death_sites
