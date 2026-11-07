#pragma once
#include <cstdint>
#include <cstring>

// The three call sites of step 4, the bolts and the muzzle (docs/architecture/bolt-muzzle-look.md, weapon-look.md 3.3,
// docs/reverse-engineering/weapon-effect-spawn.md sections 2, 6 and 8.2; X3AP.exe SHA-256 fdbf3418...f8ab, preferred
// VAs). No Windows dependency: the host tests and the hook fixture compile it. Each window is compared whole before any
// write (fail closed); bytes read from the installed EXE with verification/results/weapon-effects-bolt/bolt_sites.py.
//
// The per-bullet tap (the sector update 0x004596e0, its per-object branch; EBX = obj):
// 0045ac6d  8b 15 0c 85 60 00  MOV EDX,[0x0060850c]
// 0045ac73  83 3a 00           CMP [EDX],0
// 0045ac76  0f 84 3b 07 00 00  JE 0x0045b3b7                  ; the gate *(*0x0060850c) != 0
// 0045ac7c  53                 PUSH EBX                        ; obj
// 0045ac7d  e8 0e 99 fb ff     CALL 0x00414590                 <- the tap (cdecl, 1 dword, plain ret; no register input)
// 0045ac82  0f b7 73 48        MOVZX ESI,[EBX+0x48]            ; ESI reloaded, EBX live
// 0045ac86  0f bf c6           MOVSX EAX,SI                    ; EAX overwritten: the return value is dead
// 0045ac89  83 c4 04           ADD ESP,0x4                     <- end of the window (31 bytes)
// The five bytes 0x0045ac7d..81 straddle the aligned qword 0x0045ac80: claim_call's plain copy, inside the install
// window only (the sector update runs only in the frame loop). The stub always forwards with every register, the flags
// and the stack as the call left them.
//
// The muzzle (the fire routine 0x00445170, per shot, only when no live beam is refreshed; EBP = its frame):
// 0044625d  8d 55 90           LEA EDX,[EBP-0x70]              ; &pos (16 bytes, ship-relative [i])
// 00446260  52                 PUSH EDX
// 00446261  50                 PUSH EAX                        ; gun slot k ([ebp-0x58])
// 00446262  8b 45 84           MOV EAX,[EBP-0x7c]              ; the TBullets index (the callee's EAX input)
// 00446265  51                 PUSH ECX                        ; the ship ([ebx+8])
// 00446266  8b d6              MOV EDX,ESI                     ; &orientation (12 dwords, [ebp-0x200]; EDX input)
// 00446268  e8 93 d9 fc ff     CALL 0x00413c00                 <- the muzzle site (cdecl, 3 dwords, plain ret)
// 0044626d  8b 85 7c ff ff ff  MOV EAX,[EBP-0x84]              ; EAX reloaded: the return value is dead
// 00446273  33 f6              XOR ESI,ESI
// 00446275  83 c4 0c           ADD ESP,0xc                     <- end of the window (27 bytes)
// The five bytes lie inside one aligned qword (lock cmpxchg8b). The stub forwards EAX and EDX unchanged; the skip path
// returns `xor eax,eax; ret` (EAX, ECX, EDX, ESI and EFLAGS are dead after the call).
//
// The flight effect (0x00414590's class-0 branch, a bullet whose col 19 is non-zero; ESI = the bullet, EBX = 0):
// 004146b2  53 53 8d 54 24 28 52 53 53 53 56 51 53 53  ten pushes: 0, 0, &zero, 0, 0, 0, ESI, ECX (eff), 0 (k), 0 (kind)
// 004146c0  e8 db 01 00 00     CALL 0x004148a0                 <- the flight site (cdecl, 10 dwords, return not tested)
// 004146c5  83 c4 28           ADD ESP,0x28                    <- end of the window (22 bytes)
// The five bytes lie inside one aligned qword. The skip path returns `xor eax,eax; ret`. No direct branch lands inside
// any of the three calls and no image dword points into them
// (verification/results/weapon-effect-spawn/weapon_effect_sites_out.txt: object, muzzle, flight).
namespace x3m::weapon_effects::bolt_sites {
constexpr unsigned call_length = 5;
// ---- the tap
constexpr std::uintptr_t tap_window_va = 0x0045ac6d, tap_site_va = 0x0045ac7d, tap_target_va = 0x00414590;
constexpr std::uintptr_t tap_return_va = 0x0045ac82, tap_window_end_va = 0x0045ac8c;
constexpr unsigned tap_window_length = 31, tap_site_offset = 16;
constexpr unsigned char expected_tap_window[tap_window_length] = {
    0x8b, 0x15, 0x0c, 0x85, 0x60, 0x00, 0x83, 0x3a, 0x00, 0x0f, 0x84, 0x3b, 0x07, 0x00, 0x00, 0x53,
    0xe8, 0x0e, 0x99, 0xfb, 0xff, 0x0f, 0xb7, 0x73, 0x48, 0x0f, 0xbf, 0xc6, 0x83, 0xc4, 0x04};
constexpr unsigned tap_object_slot = 1; // [esp+4] at stub entry
// ---- the muzzle
constexpr std::uintptr_t muzzle_window_va = 0x0044625d, muzzle_site_va = 0x00446268, muzzle_target_va = 0x00413c00;
constexpr std::uintptr_t muzzle_return_va = 0x0044626d, muzzle_window_end_va = 0x00446278;
constexpr unsigned muzzle_window_length = 27, muzzle_site_offset = 11;
constexpr unsigned char expected_muzzle_window[muzzle_window_length] = {
    0x8d, 0x55, 0x90, 0x52, 0x50, 0x8b, 0x45, 0x84, 0x51, 0x8b, 0xd6, 0xe8, 0x93, 0xd9,
    0xfc, 0xff, 0x8b, 0x85, 0x7c, 0xff, 0xff, 0xff, 0x33, 0xf6, 0x83, 0xc4, 0x0c};
constexpr unsigned muzzle_ship_slot = 1, muzzle_slot_slot = 2, muzzle_pos_slot = 3;
// ---- the flight effect
constexpr std::uintptr_t flight_window_va = 0x004146b2, flight_site_va = 0x004146c0, flight_target_va = 0x004148a0;
constexpr std::uintptr_t flight_return_va = 0x004146c5, flight_window_end_va = 0x004146c8;
constexpr unsigned flight_window_length = 22, flight_site_offset = 14;
constexpr unsigned char expected_flight_window[flight_window_length] = {
    0x53, 0x53, 0x8d, 0x54, 0x24, 0x28, 0x52, 0x53, 0x53, 0x53, 0x56, 0x51, 0x53, 0x53,
    0xe8, 0xdb, 0x01, 0x00, 0x00, 0x83, 0xc4, 0x28};
constexpr unsigned flight_kind_slot = 1, flight_eff_slot = 3, flight_object_slot = 4;

constexpr std::uint32_t rel32_at(const unsigned char* b) {
    return std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 | std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
}
inline const char* plan(const unsigned char* current, const unsigned char* expected, unsigned length) {
    return std::memcmp(current, expected, length) ? "bytes_mismatch" : nullptr;
}

static_assert(tap_window_va + tap_site_offset == tap_site_va && tap_site_va + call_length == tap_return_va &&
                  tap_window_va + tap_window_length == tap_window_end_va,
              "the tap window holds the gate, the push, the call and the caller's pop");
static_assert(expected_tap_window[15] == 0x53, "push ebx: the object is the call's one argument");
static_assert(expected_tap_window[tap_site_offset] == 0xe8 &&
                  std::uint32_t(tap_return_va + rel32_at(expected_tap_window + tap_site_offset + 1)) == tap_target_va,
              "CALL 0x00414590");
static_assert(expected_tap_window[21] == 0x0f && expected_tap_window[22] == 0xb7 && expected_tap_window[23] == 0x73 &&
                  expected_tap_window[24] == 0x48 && expected_tap_window[25] == 0x0f && expected_tap_window[26] == 0xbf &&
                  expected_tap_window[27] == 0xc6,
              "movzx esi,[ebx+0x48]; movsx eax,si: EBX live, ESI and EAX overwritten after the call");
static_assert(expected_tap_window[28] == 0x83 && expected_tap_window[29] == 0xc4 && expected_tap_window[30] == 0x04,
              "the caller pops the argument (cdecl)");
static_assert(((tap_site_va & 7u) + call_length) > 8u, "the tap call straddles a qword (plain write, install window)");

static_assert(muzzle_window_va + muzzle_site_offset == muzzle_site_va && muzzle_site_va + call_length == muzzle_return_va &&
                  muzzle_window_va + muzzle_window_length == muzzle_window_end_va,
              "the muzzle window holds the three pushes, the two register inputs, the call and the pop");
static_assert(expected_muzzle_window[3] == 0x52 && expected_muzzle_window[4] == 0x50 && expected_muzzle_window[8] == 0x51,
              "push edx (&pos); push eax (slot); push ecx (ship)");
static_assert(expected_muzzle_window[5] == 0x8b && expected_muzzle_window[6] == 0x45 && expected_muzzle_window[7] == 0x84 &&
                  expected_muzzle_window[9] == 0x8b && expected_muzzle_window[10] == 0xd6,
              "mov eax,[ebp-0x7c] (the bullet index); mov edx,esi (&orientation): the callee's register inputs");
static_assert(expected_muzzle_window[muzzle_site_offset] == 0xe8 &&
                  std::uint32_t(muzzle_return_va + rel32_at(expected_muzzle_window + muzzle_site_offset + 1)) ==
                      muzzle_target_va,
              "CALL 0x00413c00");
static_assert(expected_muzzle_window[16] == 0x8b && expected_muzzle_window[17] == 0x85 && expected_muzzle_window[22] == 0x33 &&
                  expected_muzzle_window[23] == 0xf6 && expected_muzzle_window[26] == 0x0c,
              "EAX reloaded and ESI cleared after the call; the caller pops 0xc");
static_assert(((muzzle_site_va & 7u) + call_length) <= 8u, "the muzzle call lies inside one aligned qword (atomic)");

static_assert(flight_window_va + flight_site_offset == flight_site_va && flight_site_va + call_length == flight_return_va &&
                  flight_window_va + flight_window_length == flight_window_end_va,
              "the flight window holds the ten pushes, the call and the pop");
static_assert(expected_flight_window[10] == 0x56 && expected_flight_window[11] == 0x51 && expected_flight_window[12] == 0x53 &&
                  expected_flight_window[13] == 0x53,
              "push esi (the bullet, slot 4); push ecx (eff, slot 3); push ebx; push ebx (k, kind)");
static_assert(expected_flight_window[flight_site_offset] == 0xe8 &&
                  std::uint32_t(flight_return_va + rel32_at(expected_flight_window + flight_site_offset + 1)) ==
                      flight_target_va,
              "CALL 0x004148a0");
static_assert(expected_flight_window[19] == 0x83 && expected_flight_window[20] == 0xc4 && expected_flight_window[21] == 0x28,
              "the caller pops ten arguments");
static_assert(((flight_site_va & 7u) + call_length) <= 8u, "the flight call lies inside one aligned qword (atomic)");
} // namespace x3m::weapon_effects::bolt_sites
