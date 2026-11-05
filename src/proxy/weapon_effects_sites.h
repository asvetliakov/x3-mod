#pragma once
#include <cstdint>
#include <cstring>

// The weapon-effects call sites of step 1 (docs/architecture/weapon-effects.md 2.2, docs/reverse-engineering/
// weapon-effect-spawn.md section 6; X3AP.exe SHA-256 fdbf3418...f8ab, preferred VAs). No Windows dependency: the host
// tests and the hook fixture compile it.
//
// Impact window (inside the damage routine 0x00449a60, after the shield and hull stages; ESI = victim, EDI = source):
// 00449d83  57                 PUSH EDI                        ; source
// 00449d84  56                 PUSH ESI                        ; victim
// 00449d85  e8 b6 9e fc ff     CALL 0x00413c40                 <- the impact site (cdecl, 2 dwords, plain ret)
// 00449d8a  83 c4 08           ADD  ESP,0x8
// 00449d8d  8b c6 / e8 ..      MOV  EAX,ESI / CALL 0x00450310  ; the return value of 0x00413c40 is dead
// 00449d94  85 c0 / 74 09      TEST / JE 0x00449da1
// 00449d98  8b c7 / 8b ce / e8 MOV EAX,EDI / MOV ECX,ESI / CALL 0x00417250 (the impact sound)
// 00449da1  5f 5e 5d 5b        POP EDI / ESI / EBP / EBX       ; the routine's four pushes
// 00449da5  83 c4 0c           ADD  ESP,0xc                    ; its 0xc of locals
// 00449da8  c2 10 00           RET  0x10                       <- end of the window (40 bytes)
// The epilogue pins the frame: at the stub's entry [esp+4] victim, [esp+8] source, then the four saved registers
// (+0xc..+0x18), then the locals: [esp+0x1c] the running damage total (routine [esp+0x10]), [esp+0x24] the absorbed
// flag (routine [esp+0x18]). The prologue witness (0x00449a60, 39 bytes: sub esp,0xc; ... mov [esp+0x10],ebx;
// mov [esp+0x18],esi) and the writers' bytes (0x00449b95 absorbed = 1, 0x00449b9d / 0x00449cd4 the total,
// 0x00449cf9 the 50,000 clamp) are compared at install too: a changed frame layout refuses the site (fail closed).
// The five bytes 0x00449d85..89 straddle the aligned qword 0x00449d88: claim_call's plain copy, inside the install
// window only (the damage routine runs only in the frame loop). No direct branch lands inside the call and no image
// dword points into it (verification/results/weapon-effect-spawn/weapon_effect_sites_out.txt).
//
// Inner taps (observe mode only: the engine's own &pos of the impact instance, inside 0x00413c40):
// 00413da1  57 56              PUSH EDI (hit) / PUSH ESI (eff)
// 00413da3  8b 73 08           MOV  ESI,[EBX+0x8]              ; the projectile id (the callee's ESI input)
// 00413da6  6a 03              PUSH 3
// 00413da8  e8 c3 0c 00 00     CALL 0x00414a70                 <- tap 1 (cdecl, 5 dwords; [esp+0xc] hit, [esp+0x10] &pos)
// 00413dad  83 c4 24           ADD  ESP,0x24                   ; with 0x00414410's four
// 00413e72  53 56 8b 77 08 6a 03 e8 f2 0b 00 00 83 c4 24  the same with EBX = hit, EDI = projectile <- tap 2 0x00413e79
// Both taps' five bytes lie inside one aligned qword (lock cmpxchg8b). The tap forwards always, every register and the
// stack as the call left them (ESI is the callee's input).
namespace x3m::weapon_effects::sites {
constexpr unsigned call_length = 5;
// ---- impact
constexpr std::uintptr_t impact_window_va = 0x00449d83, impact_site_va = 0x00449d85, impact_target_va = 0x00413c40;
constexpr std::uintptr_t impact_return_va = 0x00449d8a, impact_window_end_va = 0x00449dab;
constexpr std::uintptr_t impact_follow_a_va = 0x00450310, impact_follow_b_va = 0x00417250; // the window's other calls
constexpr unsigned impact_window_length = 40, impact_site_offset = 2;
constexpr unsigned char expected_impact_window[impact_window_length] = {
    0x57, 0x56, 0xe8, 0xb6, 0x9e, 0xfc, 0xff, 0x83, 0xc4, 0x08, 0x8b, 0xc6, 0xe8, 0x7c, 0x65, 0x00, 0x00, 0x85, 0xc0, 0x74,
    0x09, 0x8b, 0xc7, 0x8b, 0xce, 0xe8, 0xaf, 0xd4, 0xfc, 0xff, 0x5f, 0x5e, 0x5d, 0x5b, 0x83, 0xc4, 0x0c, 0xc2, 0x10, 0x00};
// ---- the frame witnesses (read only)
constexpr std::uintptr_t prologue_va = 0x00449a60;
constexpr unsigned prologue_length = 39;
constexpr unsigned char expected_prologue[prologue_length] = {
    0x83, 0xec, 0x0c, 0x8b, 0x4c, 0x24, 0x10, 0x0f, 0xb7, 0x41, 0x48, 0x53, 0x55, 0x8b, 0x6c, 0x24, 0x20, 0x56, 0x33, 0xf6,
    0x33, 0xdb, 0x66, 0x3d, 0x07, 0x00, 0x57, 0x89, 0x6c, 0x24, 0x14, 0x89, 0x5c, 0x24, 0x10, 0x89, 0x74, 0x24, 0x18};
struct Witness {
    std::uintptr_t va;
    unsigned length;
    unsigned char bytes[8];
};
constexpr unsigned witness_count = 4;
constexpr Witness witnesses[witness_count] = {
    {0x00449b95, 8, {0xc7, 0x44, 0x24, 0x18, 0x01, 0x00, 0x00, 0x00}}, // mov [esp+0x18],1   absorbed
    {0x00449b9d, 4, {0x89, 0x5c, 0x24, 0x10}},                         // mov [esp+0x10],ebx the total (shield walk)
    {0x00449cd4, 4, {0x89, 0x5c, 0x24, 0x10}},                         // mov [esp+0x10],ebx the total (hull stage)
    {0x00449cf9, 8, {0xc7, 0x44, 0x24, 0x10, 0x50, 0xc3, 0x00, 0x00}}, // mov [esp+0x10],50000 the piloted clamp
};
// The stub contract at the impact site (stack slots at stub entry, in dwords).
constexpr unsigned victim_slot = 1, source_slot = 2, total_slot = 7, absorbed_slot = 9;
constexpr unsigned routine_pushes = 4, routine_locals = 0xc;
constexpr unsigned total_local = 0x10, absorbed_local = 0x18; // the routine's [esp+n] after its pushes
// ---- inner taps
constexpr std::uintptr_t tap_window_va[2] = {0x00413da1, 0x00413e72};
constexpr std::uintptr_t tap_site_va[2] = {0x00413da8, 0x00413e79};
constexpr std::uintptr_t tap_return_va[2] = {0x00413dad, 0x00413e7e};
constexpr std::uintptr_t tap_target_va = 0x00414a70;
constexpr unsigned tap_window_length = 15, tap_site_offset = 7;
constexpr unsigned char expected_tap_window[2][tap_window_length] = {
    {0x57, 0x56, 0x8b, 0x73, 0x08, 0x6a, 0x03, 0xe8, 0xc3, 0x0c, 0x00, 0x00, 0x83, 0xc4, 0x24},
    {0x53, 0x56, 0x8b, 0x77, 0x08, 0x6a, 0x03, 0xe8, 0xf2, 0x0b, 0x00, 0x00, 0x83, 0xc4, 0x24}};
constexpr unsigned tap_hit_slot = 3, tap_pos_slot = 4;

constexpr std::uint32_t rel32_at(const unsigned char* b) {
    return std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 | std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
}
// The install decision on bytes read at a window: nullptr when exactly the engine's, else the refusal reason.
inline const char* plan_impact(const unsigned char current[impact_window_length]) {
    return std::memcmp(current, expected_impact_window, impact_window_length) ? "bytes_mismatch_impact" : nullptr;
}
inline const char* plan_tap(unsigned i, const unsigned char current[tap_window_length]) {
    return std::memcmp(current, expected_tap_window[i], tap_window_length) ? "bytes_mismatch_tap" : nullptr;
}

static_assert(impact_window_va + impact_site_offset == impact_site_va && impact_site_va + call_length == impact_return_va &&
                  impact_window_va + impact_window_length == impact_window_end_va,
              "the window holds the two pushes, the call and runs to the routine's ret 0x10");
static_assert(expected_impact_window[0] == 0x57 && expected_impact_window[1] == 0x56, "push edi (source); push esi (victim)");
static_assert(expected_impact_window[impact_site_offset] == 0xe8 &&
                  std::uint32_t(impact_return_va + rel32_at(expected_impact_window + impact_site_offset + 1)) == impact_target_va,
              "CALL 0x00413c40");
static_assert(expected_impact_window[7] == 0x83 && expected_impact_window[8] == 0xc4 && expected_impact_window[9] == 0x08,
              "the caller pops the two arguments (cdecl)");
static_assert(expected_impact_window[10] == 0x8b && expected_impact_window[11] == 0xc6,
              "mov eax,esi: the callee's EAX is dead after the call");
static_assert(expected_impact_window[12] == 0xe8 &&
                  std::uint32_t(impact_window_va + 17 + rel32_at(expected_impact_window + 13)) == impact_follow_a_va &&
                  expected_impact_window[25] == 0xe8 &&
                  std::uint32_t(impact_window_va + 30 + rel32_at(expected_impact_window + 26)) == impact_follow_b_va,
              "the window's two later calls");
static_assert(expected_impact_window[30] == 0x5f && expected_impact_window[31] == 0x5e && expected_impact_window[32] == 0x5d &&
                  expected_impact_window[33] == 0x5b && expected_impact_window[34] == 0x83 && expected_impact_window[35] == 0xc4 &&
                  expected_impact_window[36] == routine_locals && expected_impact_window[37] == 0xc2,
              "four pops and 0xc of locals: the frame the slots are derived from");
static_assert(total_slot * 4 == 4 + 8 + total_local && absorbed_slot * 4 == 4 + 8 + absorbed_local &&
                  total_local >= routine_pushes * 4 && absorbed_local < routine_pushes * 4 + routine_locals,
              "slot = return address + two arguments + the routine's [esp+n] (n counts its four pushes)");
static_assert(expected_prologue[0] == 0x83 && expected_prologue[1] == 0xec && expected_prologue[2] == routine_locals &&
                  expected_prologue[34] == total_local && expected_prologue[38] == absorbed_local,
              "the prologue allocates 0xc and initialises the total and the absorbed flag at their offsets");
static_assert(witnesses[0].bytes[3] == absorbed_local && witnesses[0].bytes[4] == 1 && witnesses[1].bytes[3] == total_local &&
                  witnesses[2].bytes[3] == total_local && witnesses[3].bytes[3] == total_local,
              "the writers address the two locals");
static_assert(((impact_site_va & 7u) + call_length) > 8u, "the impact call straddles a qword (plain write, install window)");
static_assert(((tap_site_va[0] & 7u) + call_length) <= 8u && ((tap_site_va[1] & 7u) + call_length) <= 8u,
              "both taps lie inside one aligned qword (atomic)");
static_assert(tap_window_va[0] + tap_site_offset == tap_site_va[0] && tap_window_va[1] + tap_site_offset == tap_site_va[1] &&
                  tap_site_va[0] + call_length == tap_return_va[0] && tap_site_va[1] + call_length == tap_return_va[1],
              "the taps' windows");
static_assert(expected_tap_window[0][tap_site_offset] == 0xe8 && expected_tap_window[1][tap_site_offset] == 0xe8 &&
                  std::uint32_t(tap_return_va[0] + rel32_at(expected_tap_window[0] + tap_site_offset + 1)) == tap_target_va &&
                  std::uint32_t(tap_return_va[1] + rel32_at(expected_tap_window[1] + tap_site_offset + 1)) == tap_target_va,
              "CALL 0x00414a70");
static_assert(expected_tap_window[0][12] == 0x83 && expected_tap_window[0][14] == 0x24 && expected_tap_window[1][14] == 0x24,
              "the caller pops 0x24 after the tap");
} // namespace x3m::weapon_effects::sites
