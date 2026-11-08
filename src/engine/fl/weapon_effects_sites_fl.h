#pragma once
#include <cstdint>
#include <cstring>

// FL edition of the weapon-effects call sites (the AP twins: src/proxy/weapon_effects_sites.h, weapon_effects_bolt_sites.h,
// weapon_effects_death_sites.h). Verified facts: docs/reverse-engineering/fl/weapon-effect-spawn.md sections 1, 2 and 4
// (producer verification/results/fl-weapon-effect-spawn/fl_weapon_effect_sites.py; X3FL.exe SHA-256 e7f3aad8...dfbb8).
// X3FL.exe is relocatable: every value below is an RVA added to the running module base, or a field offset. No Windows
// dependency: the host tests and the FL hook fixture compile it.
//
// Each window is whole instructions; its relocated absolute operand (object: the gate slot, muzzle: the TBullets slot)
// is zeroed here and must equal base + its target RVA at install, the rest must equal these bytes (fail closed before
// any write). Every rel32 is unrelocated, no direct branch lands in any call's bytes 1..4 and no image dword points into
// them. Six calls lie inside one aligned qword (lock cmpxchg8b: the module base is 64 KB aligned, so the RVA's
// alignment is the live address's); the object tap's crosses 0x0645c0 (the plain copy, install window only).
//
// The ABI at stub entry ([esp] the return address; section 2):
//   impact  0x053390 -> 0x014440  ECX victim, EDX source, [esp+4] a dummy push, [esp+0x18] the shield-walk total T,
//                                 [esp+0x1c] the remaining shield-stage damage, EBP the running total, ESI 1 on the
//                                 absorbed path; plain ret, the caller pops the dummy.
//   tap 1/2 0x014583 / 0x014650 -> 0x015270  ECX 3, EDX the projectile id, [esp+4] eff, [esp+8] hit, [esp+0xc] &pos,
//                                 [esp+0x10] 0; the return is tested: observe-only, always forwards.
//   object  0x0645bd -> 0x014d90  ECX the object, no stack argument.
//   muzzle  0x04f639 -> 0x0150e0  ECX 4, EDX the gun slot ([ebp-0x88]), [esp+4] eff (col 21), [esp+8] the ship,
//                                 [esp+0x18] &pos, [esp+0x20] &orientation, ESI the TBullets record offset (index x
//                                 0xdb8, loaded inside the window from [ebp-0x24] and used there for col 21).
//   flight  0x014eaa -> 0x0150e0  ECX 0, EDX 0, [esp+4] eff (col 19), [esp+8] the bullet.
//   death   0x0543b3 -> 0x013ed0  ECX the dying object, EDX the killer, [esp+4] arg4, word [ebp+8] the reason (EBP the
//                                 kill routine's frame); the one FL call serves both AP death sites.
// Every callee returns with a plain ret and every return value outside the taps is dead: the skip return is
// `xor eax,eax; ret` at the impact, muzzle, flight and death sites.
namespace x3m::fl::weapon_effects_fl {
constexpr unsigned call_length = 5;
constexpr unsigned no_fixup = 0xffffffffu;
struct Window {
    const char* name;
    std::uint32_t rva;
    unsigned length;
    const unsigned char* bytes; // the relocated dword zeroed
    unsigned fixup_offset;      // no_fixup: none
    std::uint32_t fixup_rva;    // the dword holds base + fixup_rva
    std::uint32_t fnv;          // FNV-1a 32 of bytes (the producer's value)
};
struct Site {
    Window window;
    unsigned call_offset;
    std::uint32_t target_rva;
};
// ---- the windows (bytes from the producer's output; fnv pinned below)
constexpr unsigned char impact_bytes[43] = {
    0x51, 0x8b, 0xd3, 0x8b, 0xcf, 0xe8, 0xab, 0x10, 0xfc, 0xff, 0x83, 0xc4, 0x04, 0x8b, 0xcf, 0xe8, 0xe1, 0x69, 0x00, 0x00, 0x85, 0xc0,
    0x74, 0x09, 0x8b, 0xd3, 0x8b, 0xcf, 0xe8, 0x94, 0x46, 0xfc, 0xff, 0x5f, 0x5e, 0x5d, 0x5b, 0x83, 0xc4, 0x08, 0xc2, 0x0c, 0x00};
constexpr unsigned char tap1_bytes[27] = {0x8b, 0x57, 0x08, 0x8d, 0x44, 0x24, 0x18, 0x6a, 0x00, 0x50, 0x53, 0x56, 0xb9, 0x03,
                                          0x00, 0x00, 0x00, 0xe8, 0xe8, 0x0c, 0x00, 0x00, 0x8b, 0xc8, 0x83, 0xc4, 0x18};
constexpr unsigned char tap2_bytes[27] = {0x8b, 0x53, 0x08, 0x8d, 0x44, 0x24, 0x18, 0x6a, 0x00, 0x50, 0x57, 0x56, 0xb9, 0x03,
                                          0x00, 0x00, 0x00, 0xe8, 0x1b, 0x0c, 0x00, 0x00, 0x8b, 0xc8, 0x83, 0xc4, 0x18};
constexpr unsigned char object_bytes[31] = {0xa1, 0x00, 0x00, 0x00, 0x00, 0x83, 0x38, 0x00, 0x0f, 0x84, 0x7f,
                                            0x07, 0x00, 0x00, 0x8b, 0xcb, 0xe8, 0xce, 0x07, 0xfb, 0xff, 0x0f,
                                            0xb7, 0x73, 0x48, 0x0f, 0xbf, 0xc6, 0x83, 0xf8, 0x0a};
constexpr unsigned char muzzle_bytes[62] = {
    0xa1, 0x00, 0x00, 0x00, 0x00, 0x83, 0xc4, 0x08, 0x8b, 0x75, 0xdc, 0x8b, 0x44, 0x06, 0x7c, 0x85, 0xc0, 0x74, 0x2b, 0x8b, 0x95,
    0x78, 0xff, 0xff, 0xff, 0x8d, 0x8d, 0x20, 0xfd, 0xff, 0xff, 0x51, 0x6a, 0x00, 0x8d, 0x8d, 0xe0, 0xfe, 0xff, 0xff, 0x51, 0x6a,
    0x00, 0x6a, 0x00, 0x6a, 0x00, 0x57, 0x50, 0xb9, 0x04, 0x00, 0x00, 0x00, 0xe8, 0xa2, 0x5a, 0xfc, 0xff, 0x83, 0xc4, 0x20};
constexpr unsigned char flight_bytes[29] = {0x6a, 0x00, 0x6a, 0x00, 0x8d, 0x44, 0x24, 0x48, 0x33, 0xc9, 0x50, 0x6a, 0x00, 0x6a, 0x00,
                                            0x6a, 0x00, 0x57, 0x52, 0x33, 0xd2, 0xe8, 0x31, 0x02, 0x00, 0x00, 0x83, 0xc4, 0x20};
constexpr unsigned char death_bytes[22] = {0xff, 0x75, 0x10, 0x8b, 0xd3, 0x8b, 0xce, 0xe8, 0x18, 0xfb, 0xfb,
                                           0xff, 0x83, 0xc4, 0x04, 0x8b, 0xce, 0xe8, 0xbe, 0x59, 0x00, 0x00};
// ---- the frame witnesses (read only)
constexpr unsigned char prologue_bytes[25] = {0x83, 0xec, 0x08, 0x53, 0x55, 0x8b, 0x6c, 0x24, 0x18, 0x33, 0xdb, 0x56, 0x57,
                                              0x8b, 0xf9, 0x89, 0x6c, 0x24, 0x14, 0x33, 0xf6, 0x89, 0x5c, 0x24, 0x10};
constexpr unsigned char absorb_total_bytes[4] = {0x89, 0x5c, 0x24, 0x10};
constexpr unsigned char absorbed_esi_bytes[9] = {0x33, 0xed, 0x89, 0x6c, 0x24, 0x14, 0x8d, 0x75, 0x01};
constexpr unsigned char source_test_bytes[53] = {
    0x8b, 0x5c, 0x24, 0x1c, 0x0f, 0xb7, 0x53, 0x48, 0x66, 0x85, 0xd2, 0x75, 0x1e, 0x0f, 0xbf, 0x43, 0x4a, 0x8b,
    0x0d, 0x00, 0x00, 0x00, 0x00, 0x69, 0xc0, 0xb8, 0x0d, 0x00, 0x00, 0xf6, 0x44, 0x08, 0x58, 0x20, 0x74, 0x07,
    0xb9, 0x01, 0x00, 0x00, 0x00, 0xeb, 0x0a, 0x33, 0xc9, 0x85, 0xf6, 0x0f, 0x85, 0x0d, 0x01, 0x00, 0x00};
constexpr unsigned char hull_add_bytes[2] = {0x03, 0xee};
constexpr unsigned char clamp_bytes[29] = {0x8b, 0x6c, 0x24, 0x10, 0x8b, 0x47, 0x40, 0xd1, 0xe8, 0xa8, 0x01, 0x74, 0x50, 0x8b, 0x87,
                                           0xa0, 0x00, 0x00, 0x00, 0xb9, 0x50, 0xc3, 0x00, 0x00, 0x3b, 0xe9, 0x0f, 0x4f, 0xe9};
constexpr unsigned char gun_slot_bytes[10] = {0x0f, 0xbf, 0x40, 0x04, 0x89, 0x85, 0x78, 0xff, 0xff, 0xff};
constexpr unsigned char bullet_index_bytes[11] = {0x0f, 0xb7, 0x4c, 0x02, 0x4c, 0x89, 0x8d, 0x74, 0xff, 0xff, 0xff};
constexpr unsigned char row_chooser_bytes[14] = {0xff, 0x76, 0x40, 0x0f, 0xb7, 0x56, 0x4a, 0x8b, 0xcf, 0xe8, 0x95, 0xf9, 0xff, 0xff};

// The relocated globals (sections 3 and 5): the engine config slot, the type tables and counts per class (count =
// table - 0x98, not AP's table + 0x80), the body cache root, the object tap's gate slot.
constexpr std::uint32_t cfg_slot_rva = 0x1a83b0, type_table_rva = 0x1a83c8, type_count_rva = 0x1a8330;
constexpr std::uint32_t body_cache_rva = 0x1aa100, gate_slot_rva = 0x1a82ec;

// Site indices follow the module's rows: 0 impact, 1 tap1, 2 tap2, 3 bolt_tap (object), 4 muzzle, 5 flight, 6 death.
constexpr unsigned site_count = 7;
constexpr Site sites[site_count] = {
    {{"impact", 0x05338b, 43, impact_bytes, no_fixup, 0, 0xde6f8702}, 5, 0x014440},
    {{"tap1", 0x014572, 27, tap1_bytes, no_fixup, 0, 0xc5cdd97d}, 17, 0x015270},
    {{"tap2", 0x01463f, 27, tap2_bytes, no_fixup, 0, 0x73ecdfa4}, 17, 0x015270},
    {{"bolt_tap", 0x0645ad, 31, object_bytes, 1, gate_slot_rva, 0xd392394d}, 16, 0x014d90},
    {{"muzzle", 0x04f603, 62, muzzle_bytes, 1, type_table_rva, 0x35569ad2}, 54, 0x0150e0},
    {{"flight", 0x014e95, 29, flight_bytes, no_fixup, 0, 0x38c0ec0e}, 21, 0x0150e0},
    {{"death", 0x0543ac, 22, death_bytes, no_fixup, 0, 0x6452f5c6}, 7, 0x013ed0},
};
// The frame witnesses by the family that relies on them: the impact (the damage routine's frame: T, the remaining
// shield-stage damage, ESI = 1 on the absorbed path, the hull add into EBP, the total reload and the 50,000 clamp), the
// muzzle (the gun slot's and bullet index's writers) and the death (the row chooser call of the forwarded callee).
constexpr unsigned impact_witness_count = 6, muzzle_witness_count = 2, death_witness_count = 1;
constexpr Window impact_witnesses[impact_witness_count] = {
    {"prologue", 0x053070, 25, prologue_bytes, no_fixup, 0, 0xbc88bb15},
    {"absorb_total", 0x05318d, 4, absorb_total_bytes, no_fixup, 0, 0xf219b05c},
    {"absorbed_esi", 0x053194, 9, absorbed_esi_bytes, no_fixup, 0, 0x50eb691d},
    {"source_test", 0x0531a1, 53, source_test_bytes, 19, type_table_rva, 0xdb3502e9},
    {"hull_add", 0x0532a9, 2, hull_add_bytes, no_fixup, 0, 0xe1788ad4},
    {"clamp", 0x0532e3, 29, clamp_bytes, no_fixup, 0, 0x8b9c0d87},
};
constexpr Window muzzle_witnesses[muzzle_witness_count] = {
    {"gun_slot", 0x04e4a4, 10, gun_slot_bytes, no_fixup, 0, 0x147dd4e6},
    {"bullet_index", 0x04e4f9, 11, bullet_index_bytes, no_fixup, 0, 0x5a1c3936},
};
constexpr Window death_witnesses[death_witness_count] = {
    {"row_chooser", 0x013f3d, 14, row_chooser_bytes, no_fixup, 0, 0x8410e620},
};
// The impact frame at stub entry, in dwords: T and the remaining shield-stage damage (the routine's [esp+0x10] /
// [esp+0x14] past the return address and the dummy push).
constexpr unsigned impact_total_slot = 6, impact_remaining_slot = 7;
// The inner tap callee's stack (dwords at stub entry): hit, &pos.
constexpr unsigned tap_hit_slot = 2, tap_pos_slot = 3;
// The spawner's stack at the flight and muzzle sites: obj [esp+8], &pos [esp+0x18], &orientation [esp+0x20].
constexpr unsigned spawner_object_slot = 2, spawner_pos_slot = 6, spawner_orient_slot = 8;
// The death callee's stack: arg4 [esp+4].
constexpr unsigned death_arg4_slot = 1;
constexpr unsigned max_window = 64;

constexpr std::uint32_t fnv1a(const unsigned char* p, unsigned n) {
    std::uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
    return h;
}
constexpr std::uint32_t rel32_at(const unsigned char* b) {
    return std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 | std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
}
// The install decision on bytes read at base + w.rva: true when the relocated dword (if any) is base + its target and
// every other byte is the engine's.
inline bool matches(const Window& w, const unsigned char* current, std::uintptr_t base) {
    if (w.length > max_window) return false;
    unsigned char masked[max_window];
    std::memcpy(masked, current, w.length);
    if (w.fixup_offset != no_fixup) {
        if (w.fixup_offset + 4 > w.length || rel32_at(masked + w.fixup_offset) != std::uint32_t(base + w.fixup_rva))
            return false;
        std::memset(masked + w.fixup_offset, 0, 4);
    }
    return !std::memcmp(masked, w.bytes, w.length);
}
// The live bytes of a window at base (the loader's relocation applied; the fixture lays its image out with it).
inline void live_bytes(const Window& w, std::uintptr_t base, unsigned char* out) {
    std::memcpy(out, w.bytes, w.length);
    if (w.fixup_offset != no_fixup) {
        const std::uint32_t v = std::uint32_t(base + w.fixup_rva);
        std::memcpy(out + w.fixup_offset, &v, 4);
    }
}

constexpr bool window_ok(const Window& w) {
    return w.length <= max_window && fnv1a(w.bytes, w.length) == w.fnv &&
           (w.fixup_offset == no_fixup || (w.fixup_offset + 4 <= w.length && rel32_at(w.bytes + w.fixup_offset) == 0));
}
constexpr bool site_ok(const Site& s) {
    return window_ok(s.window) && s.call_offset + call_length <= s.window.length && s.window.bytes[s.call_offset] == 0xe8 &&
           std::uint32_t(s.window.rva + s.call_offset + call_length + rel32_at(s.window.bytes + s.call_offset + 1)) ==
               s.target_rva &&
           (s.window.fixup_offset == no_fixup || s.window.fixup_offset + 4 <= s.call_offset ||
            s.window.fixup_offset >= s.call_offset + call_length);
}
constexpr bool in_one_qword(const Site& s) {
    return ((s.window.rva + s.call_offset) & 7u) + call_length <= 8u;
}
static_assert(site_ok(sites[0]) && site_ok(sites[1]) && site_ok(sites[2]) && site_ok(sites[3]) && site_ok(sites[4]) &&
                  site_ok(sites[5]) && site_ok(sites[6]),
              "every window is the measured one and its call reaches the callee");
static_assert(window_ok(impact_witnesses[0]) && window_ok(impact_witnesses[1]) && window_ok(impact_witnesses[2]) &&
                  window_ok(impact_witnesses[3]) && window_ok(impact_witnesses[4]) && window_ok(impact_witnesses[5]) &&
                  window_ok(muzzle_witnesses[0]) && window_ok(muzzle_witnesses[1]) && window_ok(death_witnesses[0]),
              "every witness is the measured one");
static_assert(sites[0].window.rva + sites[0].call_offset == 0x053390 && sites[1].window.rva + sites[1].call_offset == 0x014583 &&
                  sites[2].window.rva + sites[2].call_offset == 0x014650 && sites[3].window.rva + sites[3].call_offset == 0x0645bd &&
                  sites[4].window.rva + sites[4].call_offset == 0x04f639 && sites[5].window.rva + sites[5].call_offset == 0x014eaa &&
                  sites[6].window.rva + sites[6].call_offset == 0x0543b3,
              "the call RVAs of the note's section 1");
static_assert(in_one_qword(sites[0]) && in_one_qword(sites[1]) && in_one_qword(sites[2]) && !in_one_qword(sites[3]) &&
                  in_one_qword(sites[4]) && in_one_qword(sites[5]) && in_one_qword(sites[6]),
              "six calls in one aligned qword (atomic), the object tap straddles (plain, install window)");
// The impact window: push ecx (the dummy); mov edx,ebx (source); mov ecx,edi (victim); call; add esp,4; ... the four
// pops, add esp,8 (the two locals) and ret 0xc (three stack arguments): the frame the slots are derived from.
static_assert(impact_bytes[0] == 0x51 && impact_bytes[1] == 0x8b && impact_bytes[2] == 0xd3 && impact_bytes[3] == 0x8b &&
                  impact_bytes[4] == 0xcf && impact_bytes[10] == 0x83 && impact_bytes[12] == 0x04 && impact_bytes[33] == 0x5f &&
                  impact_bytes[36] == 0x5b && impact_bytes[37] == 0x83 && impact_bytes[39] == 0x08 && impact_bytes[40] == 0xc2 &&
                  impact_bytes[41] == 0x0c,
              "the impact ABI and the routine's frame");
static_assert(prologue_bytes[0] == 0x83 && prologue_bytes[1] == 0xec && prologue_bytes[2] == 0x08 &&
                  (impact_total_slot - 2) * 4 == 0x10 && (impact_remaining_slot - 2) * 4 == 0x14 &&
                  absorb_total_bytes[3] == 0x10 && absorbed_esi_bytes[5] == 0x14 && clamp_bytes[3] == 0x10 &&
                  rel32_at(clamp_bytes + 20) == 50000,
              "slot = return address + the dummy + the routine's [esp+n] (its four pushes and two locals)");
static_assert(tap1_bytes[0] == 0x8b && tap1_bytes[1] == 0x57 && tap1_bytes[2] == 0x08 && tap1_bytes[10] == 0x53 &&
                  tap1_bytes[11] == 0x56 && tap1_bytes[12] == 0xb9 && tap1_bytes[13] == 0x03 && tap2_bytes[1] == 0x53 &&
                  tap2_bytes[10] == 0x57,
              "mov edx,[proj+8] (the id); push hit; push eff; mov ecx,3");
static_assert(object_bytes[14] == 0x8b && object_bytes[15] == 0xcb, "mov ecx,ebx: the object in ECX, no stack argument");
static_assert(muzzle_bytes[8] == 0x8b && muzzle_bytes[9] == 0x75 && muzzle_bytes[10] == 0xdc && muzzle_bytes[19] == 0x8b &&
                  muzzle_bytes[20] == 0x95 && rel32_at(muzzle_bytes + 21) == std::uint32_t(-0x88) && muzzle_bytes[47] == 0x57 &&
                  muzzle_bytes[48] == 0x50 && muzzle_bytes[49] == 0xb9 && muzzle_bytes[50] == 0x04,
              "mov esi,[ebp-0x24] (the record offset); mov edx,[ebp-0x88] (the gun slot); push edi (ship); push eax; mov ecx,4");
static_assert(flight_bytes[17] == 0x57 && flight_bytes[18] == 0x52 && flight_bytes[19] == 0x33 && flight_bytes[20] == 0xd2,
              "push edi (the bullet, [esp+8]); push edx (eff); xor edx,edx");
static_assert(death_bytes[0] == 0xff && death_bytes[1] == 0x75 && death_bytes[2] == 0x10 && death_bytes[3] == 0x8b &&
                  death_bytes[4] == 0xd3 && death_bytes[5] == 0x8b && death_bytes[6] == 0xce && death_bytes[12] == 0x83 &&
                  death_bytes[14] == 0x04,
              "push [ebp+0x10] (arg4); mov edx,ebx (killer); mov ecx,esi (obj); the caller pops one dword");
} // namespace x3m::fl::weapon_effects_fl
