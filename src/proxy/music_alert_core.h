#pragma once
#include <cstdint>

// Combat-music trigger (X3M_MUSIC_COMBAT_TRIGGER=engaged, X3M_MUSIC_COMBAT_HOLD_S): the engine site, the byte
// windows the installer compares and the "engaged" rule as pure templates the host test compiles against a fake
// memory (docs/reverse-engineering/music-script-driver.md sections 2, 5 and 9; docs/architecture/
// music-combat-trigger.md). No Windows header, no allocation, no floating point, no engine call.
//
// Site: the alert routine 0x0042c890 (stdcall, R = the cockpit registry *0x00608504) ends its walk at 0x0042c974 with
// `mov ecx,[esi+8]; mov eax,[esi+4]` (ESI = R, EDI = P the player ship or 0, [R+8] = this frame's level, [R+4] = the
// committed level; EAX/ECX/EDX and the flags are dead). The thunk may rewrite [R+8] before the two moves replay.
namespace x3m::music_alert::core {
// ---- Engine globals and layouts (music-script-driver.md section 9) ----
constexpr std::uint32_t registry_va = 0x00608504;    // R; [R+4] committed level, [R+8] this frame's level, [R+0x10]
                                                     // the active cockpit's handle
constexpr std::uint32_t player_root_va = 0x0060850c; // M; [M+0x38] = P, the player ship; counters below
constexpr std::uint32_t cfg_va = 0x00606f34;         // cfg; +0x718 game clock (ms), +0x714 the frame step
constexpr unsigned registry_committed = 0x4, registry_level = 0x8, registry_cockpit_handle = 0x10;
constexpr unsigned root_player_ship = 0x38, root_laser_fired = 0x40, root_missile_fired = 0x44, root_laser_hits = 0x48,
                   root_missile_hits = 0x4c;
constexpr unsigned cfg_clock = 0x718, cfg_step = 0x714, cfg_seta_a = 0xcc, cfg_seta_b = 0xd0; // SETA factor
                                                                                             // (a*b)>>16, section 9.4
constexpr unsigned obj_next = 0x0, obj_group = 0x3c, obj_flags1 = 0x40, obj_flags2 = 0x44, obj_class = 0x48,
                   obj_data = 0x50, obj_parent = 0x54, obj_render = 0x70;
constexpr unsigned render_position = 0x30;                      // three int32: x, y, z
constexpr unsigned sector_lists = 0x50, list_stride = 12;       // 32 per-class lists; [list] = first node; node+0 = next
constexpr unsigned flight_target = 0x224, flight_order = 0x258; // flight = [ship+0x50]
constexpr unsigned flight_friend_mask = 0x1cc, flight_foe_mask = 0x1d0, flight_enemy_of_enabled = 0x1d4;
constexpr unsigned missile_launcher = 0xc; // [missile+0x50]+0xc
constexpr unsigned cockpit_target = 0x1e0; // the player's selected target
constexpr std::uint32_t flags2_hit = 0x80000, flags2_player_hit = 0x40, flags2_neutral = 0x40000000,
                        flags1_missile_hostile = 0x1000000;
constexpr std::uint16_t class_sector = 1, class_5 = 5, class_6 = 6, class_ship = 7, class_missile = 10;
constexpr std::uint16_t player_group = 10, order_attack = 0x10, order_attack_big = 0x11;
constexpr std::int32_t alert_range = 2500000; // the routine's own first-level range (0x0042c928)
constexpr unsigned walked_classes[] = {class_5, class_6, class_ship, class_missile};
constexpr unsigned list_walk_limit = 4096;   // nodes visited per list at most (a cut walk is counted; the vanilla walk
                                             // is unbounded)
constexpr unsigned cockpit_chain_limit = 32; // registry hash chain links followed at most (chase_fire::active_cockpit)
constexpr std::uint32_t default_hold_ms = 20000;
constexpr std::uint32_t min_hold_s = 1, max_hold_s = 600;

// ---- The site and the windows the installer compares (exact bytes of the installed X3AP.exe) ----
constexpr std::uint32_t alert_va = 0x0042c890, site_va = 0x0042c974, continue_va = 0x0042c97a,
                        site_branch_va = 0x0042c96f, walk_va = 0x0041cde0, walk_caller_va = 0x0041cdea,
                        main_loop_alert_va = 0x00403f2f, relation_va = 0x00450890, stance_va = 0x00450810,
                        distance_va = 0x00419280;
constexpr unsigned site_length = 6; // mov ecx,[esi+8]; mov eax,[esi+4]
constexpr unsigned char site_bytes[site_length] = {0x8b, 0x4e, 0x08, 0x8b, 0x46, 0x04};
// clang-format off
// 0x0042c890: mov eax,[0x60850c]; sub esp,8; push ebx/ebp/esi/edi; mov edi,[eax+0x38]
constexpr unsigned char alert_head[15] = {0xa1, 0x0c, 0x85, 0x60, 0x00, 0x83, 0xec, 0x08, 0x53, 0x55, 0x56, 0x57, 0x8b, 0x78, 0x38};
// 0x0042c915: the range test: [R+4]==0 ? dist < 2,500,000 : dist < 5,000,000, both through 0x00419280; mov ebp,1
constexpr unsigned char range_window[43] = {
    0x8b, 0x54, 0x24, 0x1c, 0x83, 0x7a, 0x04, 0x00, 0x8b, 0xc6, 0x8b, 0xd7, 0x75, 0x0c, 0xe8, 0x58, 0xc9, 0xfe, 0xff,
    0x3d, 0xa0, 0x25, 0x26, 0x00, 0xeb, 0x0a, 0xe8, 0x4c, 0xc9, 0xfe, 0xff, 0x3d, 0x40, 0x4b, 0x4c, 0x00, 0x7d, 0x05,
    0xbd, 0x01, 0x00, 0x00, 0x00};
// 0x0042c965: mov esi,[esp+0x1c]; movsx eax,bp; cmp eax,[esi+8]; jle 0x42c974; mov [esi+8],eax; <site>; cmp ecx,eax;
// je 0x42ca50
constexpr unsigned char site_window[29] = {
    0x8b, 0x74, 0x24, 0x1c, 0x0f, 0xbf, 0xc5, 0x3b, 0x46, 0x08, 0x7e, 0x03, 0x89, 0x46, 0x08, 0x8b, 0x4e, 0x08, 0x8b,
    0x46, 0x04, 0x3b, 0xc8, 0x0f, 0x84, 0xce, 0x00, 0x00, 0x00};
constexpr unsigned site_window_va = 0x0042c965, site_window_offset = site_va - site_window_va;
// 0x0042ca4a: mov eax,[esi+8]; mov [esi+4],eax; pop edi; mov [esi+8],0; pop esi/ebp/ebx; add esp,8; ret 4
constexpr unsigned char commit_window[23] = {
    0x8b, 0x46, 0x08, 0x89, 0x46, 0x04, 0x5f, 0xc7, 0x46, 0x08, 0x00, 0x00, 0x00, 0x00, 0x5e, 0x5d, 0x5b, 0x83, 0xc4,
    0x08, 0xc2, 0x04, 0x00};
// 0x00450890 whole: the relation test (EAX = O, EDI = P; AL = hostile), SA_IsEnemy's routine
constexpr unsigned char relation_body[97] = {
    0x56, 0x8b, 0xf0, 0x85, 0xf6, 0x75, 0x0b, 0x33, 0xc9, 0x85, 0xc0, 0x0f, 0x9c, 0xc1, 0x8a, 0xc1, 0x5e, 0xc3, 0x0f,
    0xb7, 0x47, 0x48, 0x66, 0x3d, 0x07, 0x00, 0x74, 0x0c, 0x66, 0x3d, 0x05, 0x00, 0x74, 0x06, 0x66, 0x3d, 0x06, 0x00,
    0x75, 0x25, 0x8b, 0x47, 0x50, 0x83, 0xb8, 0xd4, 0x01, 0x00, 0x00, 0x00, 0x74, 0x19, 0x8b, 0xc7, 0xe8, 0x45, 0xff,
    0xff, 0xff, 0x85, 0xc0, 0x7d, 0x0e, 0x83, 0xc8, 0xff, 0x33, 0xc9, 0x85, 0xc0, 0x0f, 0x9c, 0xc1, 0x8a, 0xc1, 0x5e,
    0xc3, 0x8b, 0xc6, 0x8b, 0xf7, 0xe8, 0x2a, 0xff, 0xff, 0xff, 0x33, 0xc9, 0x85, 0xc0, 0x0f, 0x9c, 0xc1, 0x8a, 0xc1,
    0x5e, 0xc3};
// 0x00450810 whole: the stance (ESI = subject, EAX = other; -1/0/+1)
constexpr unsigned char stance_body[122] = {
    0x85, 0xc0, 0x74, 0x73, 0x66, 0x83, 0x78, 0x48, 0x0a, 0x75, 0x1f, 0xf7, 0x40, 0x40, 0x00, 0x00, 0x00, 0x01, 0x74,
    0x16, 0x8b, 0x48, 0x50, 0x39, 0x71, 0x0c, 0x74, 0x0e, 0x66, 0x8b, 0x50, 0x3c, 0x66, 0x3b, 0x56, 0x3c, 0x74, 0x04,
    0x83, 0xc8, 0xff, 0xc3, 0xf7, 0x46, 0x44, 0x00, 0x00, 0x00, 0x40, 0x75, 0x44, 0x0f, 0xb7, 0x4e, 0x48, 0x66, 0x83,
    0xf9, 0x07, 0x74, 0x0c, 0x66, 0x83, 0xf9, 0x05, 0x74, 0x06, 0x66, 0x83, 0xf9, 0x06, 0x75, 0x2e, 0x0f, 0xb7, 0x48,
    0x3c, 0x66, 0x85, 0xc9, 0x7c, 0x25, 0x8b, 0x46, 0x50, 0xba, 0x01, 0x00, 0x00, 0x00, 0xd3, 0xe2, 0x85, 0x90, 0xcc,
    0x01, 0x00, 0x00, 0x74, 0x06, 0xb8, 0x01, 0x00, 0x00, 0x00, 0xc3, 0x8b, 0x80, 0xd0, 0x01, 0x00, 0x00, 0x23, 0xc2,
    0xf7, 0xd8, 0x1b, 0xc0, 0xc3, 0x33, 0xc0, 0xc3};
// 0x00419280 whole: Chebyshev distance over [obj+0x70]+0x30..0x38 (EAX = O, EDX = P)
constexpr unsigned char distance_body[60] = {
    0x8b, 0x48, 0x70, 0x8b, 0x52, 0x70, 0x8b, 0x42, 0x30, 0x83, 0xc2, 0x30, 0x83, 0xc1, 0x30, 0x2b, 0x01, 0x79, 0x02,
    0xf7, 0xd8, 0x56, 0x8b, 0x72, 0x04, 0x2b, 0x71, 0x04, 0x79, 0x02, 0xf7, 0xde, 0x8b, 0x52, 0x08, 0x2b, 0x51, 0x08,
    0x79, 0x02, 0xf7, 0xda, 0x3b, 0xc6, 0x7e, 0x04, 0x3b, 0xc2, 0xeb, 0x04, 0x3b, 0xf2, 0x8b, 0xc6, 0x7f, 0x02, 0x8b,
    0xc2, 0x5e, 0xc3};
// Witnesses of the fields the rule reads (section 9): the collision pass clearing Flags2 0x80000/0x40 (0x0045d280);
// the player-shot counters M+0x40 (bullet, 0x004463b0) and M+0x44 (missile, 0x00449312); the hit counters M+0x48/0x4c
// and the victim bits 0x40 / 0x80000 (0x0045e473); SA_AttackObject's order 0x10 at flight+0x258 with its target at
// flight+0x224 (0x0046151a); the cockpit's target cockpit+0x1e0 (0x00445255); the clock cfg+0x718 (0x00446578); the
// player group word compared with 10 (0x00450730); the death fix-up of flight+0x224 (0x0044a88a); the SA_ getters of
// the four counters (0x00464797).
constexpr unsigned char collide_clear[14] = {0x81, 0x60, 0x40, 0xff, 0xcf, 0x9f, 0xff, 0x81, 0x60, 0x44, 0xbc, 0xff, 0xf7, 0xff};
constexpr unsigned char bullet_counter[33] = {
    0x83, 0x7d, 0xc8, 0x00, 0x8b, 0x0d, 0x0c, 0x85, 0x60, 0x00, 0xba, 0x01, 0x00, 0x00, 0x00, 0x89, 0x95, 0x64, 0xff,
    0xff, 0xff, 0x74, 0x0a, 0x01, 0x51, 0x40, 0x81, 0x48, 0x44, 0x00, 0x01, 0x00, 0x00};
constexpr unsigned char missile_counter[17] = {0xf6, 0x45, 0x18, 0x02, 0x74, 0x0b, 0x83, 0x40, 0x44, 0x01, 0x81, 0x4b, 0x44, 0x00, 0x01, 0x00, 0x00};
constexpr unsigned char hit_counters[79] = {
    0x8b, 0x43, 0x44, 0xa9, 0x00, 0x01, 0x00, 0x00, 0x8b, 0x0d, 0x0c, 0x85, 0x60, 0x00, 0x74, 0x32, 0xa9, 0x00, 0x08,
    0x00, 0x00, 0x75, 0x2b, 0x0f, 0xb7, 0x43, 0x48, 0x66, 0x85, 0xc0, 0x75, 0x06, 0x83, 0x41, 0x48, 0x01, 0xeb, 0x11,
    0x66, 0x3d, 0x0a, 0x00, 0x75, 0x0b, 0x66, 0x83, 0x7e, 0x48, 0x00, 0x74, 0x04, 0x83, 0x41, 0x4c, 0x01, 0x83, 0x4e,
    0x44, 0x40, 0x81, 0x4b, 0x44, 0x00, 0x08, 0x00, 0x00, 0x81, 0x4e, 0x44, 0x00, 0x00, 0x08, 0x00, 0x8b, 0x75, 0x0c,
    0x0f, 0xb7, 0x46};
constexpr unsigned char attack_order[18] = {0x66, 0xc7, 0x81, 0x58, 0x02, 0x00, 0x00, 0x10, 0x00, 0x8b, 0x56, 0x50, 0x89, 0x82, 0x24, 0x02, 0x00, 0x00};
constexpr unsigned char cockpit_target_witness[9] = {0x8b, 0x8e, 0xe0, 0x01, 0x00, 0x00, 0x89, 0x4b, 0x0c};
constexpr unsigned char clock_witness[56] = {
    0x8b, 0x0d, 0x34, 0x6f, 0x60, 0x00, 0x8b, 0x75, 0x88, 0x89, 0x47, 0x10, 0x89, 0x47, 0x14, 0x8b, 0x91, 0x18, 0x07,
    0x00, 0x00, 0x03, 0x56, 0x0c, 0x8b, 0x47, 0x50, 0x89, 0x10, 0x8b, 0x4f, 0x50, 0x8b, 0x43, 0x08, 0x89, 0x41, 0x04,
    0x66, 0x8b, 0x50, 0x3c, 0x8b, 0x4f, 0x50, 0x8b, 0x45, 0xe8, 0x66, 0x89, 0x57, 0x3c, 0x89, 0x41, 0x70, 0x8b};
constexpr unsigned char group10_witness[11] = {0x66, 0x3d, 0x0a, 0x00, 0x56, 0x8b, 0xf1, 0x66, 0x89, 0x46, 0x3c};
constexpr unsigned char death_fixup_target[17] = {0x8b, 0x47, 0x50, 0x39, 0x98, 0x24, 0x02, 0x00, 0x00, 0x75, 0x06, 0x89, 0xa8, 0x24, 0x02, 0x00, 0x00};
constexpr unsigned char sa_counters[60] = {
    0x8b, 0x15, 0x0c, 0x85, 0x60, 0x00, 0x8b, 0x42, 0x40, 0x50, 0xe9, 0x4c, 0xe6, 0xff, 0xff, 0x8b, 0x15, 0x0c, 0x85,
    0x60, 0x00, 0x8b, 0x42, 0x44, 0x50, 0xe9, 0x3d, 0xe6, 0xff, 0xff, 0x8b, 0x15, 0x0c, 0x85, 0x60, 0x00, 0x8b, 0x42,
    0x48, 0x50, 0xe9, 0x2e, 0xe6, 0xff, 0xff, 0x8b, 0x15, 0x0c, 0x85, 0x60, 0x00, 0x8b, 0x42, 0x4c, 0x50, 0xe9, 0x1f,
    0xe6, 0xff, 0xff};
constexpr unsigned char walk_caller[5] = {0xe8, 0xa1, 0xfa, 0x00, 0x00}; // 0x0041cdea: call 0x0042c890
// The main loop's call of the registry walk, 0x00403f2f `call 0x0041cde0`, is checked only by the verifier on the
// unmodified image: the game-phase group (X3M_GAME_PHASES=1 / the draw trace, game_phase_sites.h "game_phase_cockpits")
// legitimately redirects that call inside the same install window, and the site is reached from 0x0042c890 whichever
// caller enters the walk.
struct Window {
    const char* name;
    std::uint32_t va;
    const unsigned char* bytes;
    unsigned length;
};
// Every window the installer compares before the one write; the same table drives verify_music_alert_sites.py.
constexpr Window windows[] = {
    {"alert_head", 0x0042c890, alert_head, sizeof alert_head},
    {"range_window", 0x0042c915, range_window, sizeof range_window},
    {"site_window", 0x0042c965, site_window, sizeof site_window},
    {"commit_window", 0x0042ca4a, commit_window, sizeof commit_window},
    {"relation_body", 0x00450890, relation_body, sizeof relation_body},
    {"stance_body", 0x00450810, stance_body, sizeof stance_body},
    {"distance_body", 0x00419280, distance_body, sizeof distance_body},
    {"collide_clear", 0x0045d280, collide_clear, sizeof collide_clear},
    {"bullet_counter", 0x004463b0, bullet_counter, sizeof bullet_counter},
    {"missile_counter", 0x00449312, missile_counter, sizeof missile_counter},
    {"hit_counters", 0x0045e473, hit_counters, sizeof hit_counters},
    {"attack_order", 0x0046151a, attack_order, sizeof attack_order},
    {"cockpit_target", 0x00445255, cockpit_target_witness, sizeof cockpit_target_witness},
    {"clock_witness", 0x00446578, clock_witness, sizeof clock_witness},
    {"group10", 0x00450730, group10_witness, sizeof group10_witness},
    {"death_fixup_target", 0x0044a88a, death_fixup_target, sizeof death_fixup_target},
    {"sa_counters", 0x00464797, sa_counters, sizeof sa_counters},
    {"walk_caller", 0x0041cdea, walk_caller, sizeof walk_caller},
};
constexpr unsigned window_count = sizeof windows / sizeof windows[0];
// clang-format on

// ---- The install check: every window in table order, then the six-byte site, each compared over its whole length.
// Not engine_patch::verify_bytes: that caps a compare at max_prologue (16) bytes and refused 13 of these windows (the
// Run 189 bytes_mismatch). Raw, no claim overlay: no other engine patch claims a byte of any window
// (test_music_alert applies every collected claim to a copy of the EXE and reruns this check); the walk_caller window
// pins `call 0x0042c890` byte for byte. `read(at, out, n)` reads n code bytes, false when unreadable. ----
constexpr unsigned compare_capacity = 128, mismatch_shown = 8;
constexpr bool windows_fit() {
    for (const Window& w : windows)
        if (w.length > compare_capacity) return false;
    return site_length <= compare_capacity;
}
static_assert(windows_fit(), "a window exceeds the compare buffer");
struct Mismatch {
    const char* name = nullptr; // nullptr: every window matched
    std::uint32_t va = 0;
    bool readable = false;
    unsigned shown = 0; // leading bytes of the window kept in expected/got (min(length, mismatch_shown))
    unsigned char expected[mismatch_shown]{}, got[mismatch_shown]{};
};
template <class Read>
inline bool window_equal(Read& read, const char* name, std::uint32_t va, const unsigned char* bytes, unsigned length,
                         Mismatch& out) {
    unsigned char actual[compare_capacity]{};
    const bool readable = length <= compare_capacity && read(va, actual, length);
    bool equal = readable;
    for (unsigned i = 0; equal && i < length; ++i) equal = actual[i] == bytes[i];
    if (equal) return true;
    out.name = name;
    out.va = va;
    out.readable = readable;
    out.shown = length < mismatch_shown ? length : mismatch_shown;
    for (unsigned i = 0; i < out.shown; ++i) {
        out.expected[i] = bytes[i];
        out.got[i] = readable ? actual[i] : 0;
    }
    return false;
}
// True when every window and the site match; otherwise `out` names the first that does not.
template <class Read> inline bool install_windows_match(Read&& read, Mismatch& out) {
    out = Mismatch{};
    for (const Window& w : windows)
        if (!window_equal(read, w.name, w.va, w.bytes, w.length, out)) return false;
    return window_equal(read, "site", site_va, site_bytes, site_length, out);
}

// ---- The rule (pure; Mem supplies `bool read(uint32_t at, void* out, unsigned n)` and `void write32(uint32_t at,
// uint32_t v)`; every read is bounds-checked by Mem, a failed read ends the decision with no write) ----
template <class Mem> bool u32(Mem& m, std::uint32_t base, unsigned offset, std::uint32_t& out) {
    if (!base || offset > 0xffffffffu - base) return false;
    return m.read(base + offset, &out, 4);
}
template <class Mem> bool u16(Mem& m, std::uint32_t base, unsigned offset, std::uint16_t& out) {
    if (!base || offset > 0xffffffffu - base) return false;
    return m.read(base + offset, &out, 2);
}
inline bool relation_class(std::uint16_t c) {
    return c == class_ship || c == class_5 || c == class_6;
}
// 0x00450810: the subject's stance towards `other` (-1 hostile, 0 neutral, +1 friend); 0 on any unreadable field.
template <class Mem> int stance(Mem& m, std::uint32_t subject, std::uint32_t other) {
    if (!other) return 0;
    std::uint16_t oc = 0, og = 0, sg = 0, sc = 0;
    std::uint32_t of1 = 0, od = 0, launcher = 0, sf2 = 0, flight = 0, mask = 0;
    if (!u16(m, other, obj_class, oc)) return 0;
    if (oc == class_missile) {
        if (!u32(m, other, obj_flags1, of1)) return 0;
        if (of1 & flags1_missile_hostile) {
            if (!u32(m, other, obj_data, od) || !u32(m, od, missile_launcher, launcher) ||
                !u16(m, other, obj_group, og) || !u16(m, subject, obj_group, sg))
                return 0;
            if (launcher != subject && og != sg) return -1;
        }
    }
    if (!u32(m, subject, obj_flags2, sf2)) return 0;
    if (sf2 & flags2_neutral) return 0;
    if (!u16(m, subject, obj_class, sc) || !relation_class(sc)) return 0;
    if (!u16(m, other, obj_group, og) || std::int16_t(og) < 0) return 0;
    const std::uint32_t bit = std::uint32_t(1) << (og & 31);
    if (!u32(m, subject, obj_data, flight) || !u32(m, flight, flight_friend_mask, mask)) return 0;
    if (mask & bit) return 1;
    if (!u32(m, flight, flight_foe_mask, mask)) return 0;
    return (mask & bit) ? -1 : 0;
}
// 0x00450890 (SA_IsEnemy): whether `object` is hostile to the player ship `player`; false on any unreadable field.
template <class Mem> bool is_enemy(Mem& m, std::uint32_t object, std::uint32_t player) {
    if (!object || !player) return false;
    std::uint16_t pc = 0;
    std::uint32_t flight = 0, enabled = 0;
    if (!u16(m, player, obj_class, pc)) return false;
    if (relation_class(pc)) {
        if (!u32(m, player, obj_data, flight) || !u32(m, flight, flight_enemy_of_enabled, enabled)) return false;
        if (enabled && stance(m, object, player) < 0) return true;
    }
    return stance(m, player, object) < 0;
}
// 0x00419280: max(|dx|,|dy|,|dz|) with the engine's own two's-complement negation (an INT_MIN difference stays
// negative, i.e. "in range", as the engine computes it).
template <class Mem> bool chebyshev(Mem& m, std::uint32_t object, std::uint32_t player, std::int32_t& out) {
    std::uint32_t ro = 0, rp = 0, po[3]{}, pp[3]{};
    if (!u32(m, object, obj_render, ro) || !u32(m, player, obj_render, rp)) return false;
    for (unsigned i = 0; i < 3; ++i)
        if (!u32(m, ro, render_position + 4 * i, po[i]) || !u32(m, rp, render_position + 4 * i, pp[i])) return false;
    std::int32_t best = 0;
    for (unsigned i = 0; i < 3; ++i) {
        std::int32_t d = std::int32_t(pp[i] - po[i]);
        if (d < 0) d = std::int32_t(0u - std::uint32_t(d));
        if (i == 0 || d > best) best = d;
    }
    out = best;
    return true;
}
// The active cockpit: the registry's hash lookup of [R+0x10] (chase_fire.cpp::active_cockpit, bounded).
template <class Mem> bool active_cockpit(Mem& m, std::uint32_t registry, std::uint32_t& cockpit) {
    std::uint32_t table = 0, handle = 0, bucket[2]{}, link = 0;
    if (!u32(m, registry, 0, table) || !u32(m, registry, registry_cockpit_handle, handle) || !table ||
        !m.read(table, bucket, 8))
        return false;
    const std::uint32_t n = bucket[1];
    if (!n || n > 65536 || (n & (n - 1)) || !u32(m, bucket[0], 4 * ((n - 1) & handle), link)) return false;
    for (unsigned i = 0; link && i < cockpit_chain_limit; ++i) {
        std::uint32_t row[3]{};
        if (!m.read(link, row, 12)) return false;
        if (row[1] == handle) {
            cockpit = row[2];
            return cockpit && !(cockpit & 3);
        }
        link = row[0];
    }
    return false;
}
// Reason bits (the row letters): a fired at a hostile selected target; b the player ship hit; c an owned ship near P
// hit or attacking a hostile; d the vanilla level is 2 (a hostile missile homing on P); e the player's hit counters
// rose and a hostile carries the player-hit bit; f a hostile ship's attack order targets P or an owned ship near P.
constexpr unsigned reason_fire = 1, reason_player_hit = 2, reason_owned = 4, reason_missile = 8, reason_hit = 16,
                   reason_hostile_order = 32;
inline bool is_attack_order(std::uint16_t order) {
    return order == order_attack || order == order_attack_big;
}
struct Walk {
    std::uint32_t target = 0;  // the selected target to find (0 = not wanted); rule (a)
    bool want_events = false;  // rules (c) and (f): frames with a nonzero vanilla level
    bool want_hits = false;    // rule (e): frames where a player hit counter rose
    bool target_found = false, target_hostile = false, owned_hit = false, owned_attack = false, hostile_order = false,
         hit_victim = false;
    unsigned visited = 0, owned_near = 0, hostile_orders = 0, cuts = 0;
};
// Lists 5, 6, 7 and 10 of the sector, at most list_walk_limit nodes each, stopping as soon as every wanted answer is
// known. Per node: the next link; with want_hits its Flags2; with want_events its group word, and for a node that is
// not P: owned (group 10) within alert_range -> Flags2 and, for a class-7 ship, its order and target (one relation test
// on the target); not owned and class 7 -> its order, and for an attack order on P or on an owned ship within
// alert_range one relation test on the node itself.
template <class Mem> void walk_lists(Mem& m, std::uint32_t sector, std::uint32_t player, Walk& w) {
    for (unsigned c : walked_classes) {
        std::uint32_t node = 0;
        if (!u32(m, sector, sector_lists + c * list_stride, node)) return;
        unsigned i = 0;
        for (; i < list_walk_limit && node; ++i) {
            std::uint32_t next = 0;
            if (!u32(m, node, obj_next, next) || !next) break; // the last node is the terminator
            ++w.visited;
            if (w.target && node == w.target) {
                w.target_found = true;
                w.target_hostile = is_enemy(m, node, player);
                w.target = 0;
            }
            if (w.want_hits && node != player) {
                std::uint32_t flags2 = 0;
                if (u32(m, node, obj_flags2, flags2) && (flags2 & flags2_player_hit) && is_enemy(m, node, player)) {
                    w.hit_victim = true;
                    w.want_hits = false;
                }
            }
            if (w.want_events && node != player) {
                std::uint16_t group = 0, cls = 0, order = 0;
                std::uint32_t flags2 = 0, flight = 0, target = 0;
                std::int32_t distance = 0;
                if (u16(m, node, obj_group, group) && u16(m, node, obj_class, cls)) {
                    if (group == player_group) {
                        if (chebyshev(m, node, player, distance) && distance < alert_range) {
                            ++w.owned_near;
                            if (u32(m, node, obj_flags2, flags2) && (flags2 & flags2_hit))
                                w.owned_hit = true;
                            else if (cls == class_ship && u32(m, node, obj_data, flight) &&
                                     u16(m, flight, flight_order, order) && is_attack_order(order) &&
                                     u32(m, flight, flight_target, target) && target && is_enemy(m, target, player))
                                w.owned_attack = true;
                        }
                    } else if (cls == class_ship && u32(m, node, obj_data, flight) &&
                               u16(m, flight, flight_order, order) && is_attack_order(order) &&
                               u32(m, flight, flight_target, target) && target) {
                        std::uint16_t target_group = 0;
                        const bool on_player = target == player;
                        const bool on_owned = !on_player && u16(m, target, obj_group, target_group) &&
                                              target_group == player_group &&
                                              chebyshev(m, target, player, distance) && distance < alert_range;
                        if (on_player || on_owned) {
                            ++w.hostile_orders;
                            if (is_enemy(m, node, player)) w.hostile_order = true;
                        }
                    }
                }
                if (w.owned_hit || w.owned_attack || w.hostile_order) w.want_events = false;
            }
            if (!w.target && !w.want_events && !w.want_hits) return;
            node = next;
        }
        if (i == list_walk_limit) ++w.cuts;
    }
}
struct State {
    bool stamped = false, baseline = false, episode = false;
    bool rebaseline = false; // set by the caller on a save-load event: the next frame's counters are a baseline
    std::uint32_t last_engaged = 0, laser = 0, missile = 0, laser_hits = 0, missile_hits = 0, player_prev = 0,
                  now_prev = 0;
    unsigned last_reason = 0, episode_frames = 0;
    std::uint32_t filtered_prev = 0;
};
enum class Cause : unsigned { none, stamp, held, expired, newer, range };
struct Frame {
    bool ok = false; // the inputs were readable; false = nothing written, vanilla behaviour
    bool in_sector = false, fired = false, hit = false, rebaselined = false, engaged = false, wrote_zero = false;
    std::uint32_t vanilla = 0, committed = 0, filtered = 0, from = 0, now = 0, step = 0, seta = 0, age = 0;
    unsigned reasons = 0;      // stamps taken this frame (reason bits)
    Cause cause = Cause::none; // why `engaged` is what it is
    bool edge = false, suppressed_start = false, suppressed_end = false;
    unsigned episode_frames = 0; // with suppressed_end: the episode's length
    Walk walk;
};
// One frame at the site. The vanilla level stays the precondition: [R+8] is only ever lowered, to 0, and only when
// the hold has run out; stamps are taken every frame the inputs allow.
template <class Mem>
Frame frame(Mem& m, State& s, std::uint32_t registry, std::uint32_t player, std::uint32_t hold_ms) {
    Frame f;
    std::uint32_t cfg = 0, root = 0, laser = 0, missile = 0, laser_hits = 0, missile_hits = 0, seta_a = 0, seta_b = 0;
    if (!u32(m, registry, registry_level, f.vanilla) || !u32(m, registry, registry_committed, f.committed) ||
        !u32(m, cfg_va, 0, cfg) || !u32(m, cfg, cfg_clock, f.now) || !u32(m, cfg, cfg_step, f.step) ||
        !u32(m, player_root_va, 0, root) || !u32(m, root, root_laser_fired, laser) ||
        !u32(m, root, root_missile_fired, missile) || !u32(m, root, root_laser_hits, laser_hits) ||
        !u32(m, root, root_missile_hits, missile_hits))
        return f;
    if (u32(m, cfg, cfg_seta_a, seta_a) && u32(m, cfg, cfg_seta_b, seta_b))
        f.seta = std::uint32_t((std::uint64_t(seta_a) * seta_b) >> 16);
    f.ok = true;
    // Counters: statistics restored by a save load. The first sample, a save-load event (rebaseline), a changed
    // player ship pointer (a load rebuilds the universe; a ship change is harmless) and a clock that went backwards
    // are a new baseline, not a shot; so is a decrease of a counter.
    f.rebaselined = !s.baseline || s.rebaseline || player != s.player_prev || f.now < s.now_prev;
    f.fired = !f.rebaselined && (laser > s.laser || missile > s.missile);
    f.hit = !f.rebaselined && (laser_hits > s.laser_hits || missile_hits > s.missile_hits);
    s.laser = laser;
    s.missile = missile;
    s.laser_hits = laser_hits;
    s.missile_hits = missile_hits;
    s.player_prev = player;
    s.now_prev = f.now;
    s.baseline = true;
    s.rebaseline = false;
    std::uint32_t sector = 0, flags2 = 0;
    std::uint16_t sector_class = 0;
    f.in_sector = player && u32(m, player, obj_parent, sector) && sector && u16(m, sector, obj_class, sector_class) &&
                  sector_class == class_sector;
    // (b) the player ship was hit this frame; (d) the walk or the missile homing update raised level 2 (a hostile
    // missile on the player).
    if (player && u32(m, player, obj_flags2, flags2) && (flags2 & flags2_hit)) f.reasons |= reason_player_hit;
    if (f.vanilla == 2) f.reasons |= reason_missile;
    // One bounded walk when wanted: (a) the player fired at a selected hostile target (the target is looked up in
    // the lists, never dereferenced from the cockpit pointer alone); (e) a player hit counter rose (the victim with
    // the player-hit bit must be hostile: asteroids and friends do not count); (c) and (f) only while the vanilla
    // level is nonzero.
    if (f.in_sector) {
        std::uint32_t cockpit = 0, target = 0;
        if (f.fired && active_cockpit(m, registry, cockpit) && u32(m, cockpit, cockpit_target, target) && target &&
            target != player)
            f.walk.target = target;
        f.walk.want_events = f.vanilla != 0;
        f.walk.want_hits = f.hit;
        if (f.walk.target || f.walk.want_events || f.walk.want_hits) walk_lists(m, sector, player, f.walk);
        if (f.walk.target_found && f.walk.target_hostile) f.reasons |= reason_fire;
        if (f.walk.owned_hit || f.walk.owned_attack) f.reasons |= reason_owned;
        if (f.walk.hit_victim) f.reasons |= reason_hit;
        if (f.walk.hostile_order) f.reasons |= reason_hostile_order;
    }
    if (f.reasons) {
        s.stamped = true;
        s.last_engaged = f.now;
        s.last_reason = f.reasons;
    }
    // The hold, in game ms (scales with SETA); a stamp newer than now (TI_SetAbsTime rewound the clock) is expired.
    if (!s.stamped) {
        f.cause = Cause::none;
    } else if (s.last_engaged > f.now) {
        f.cause = Cause::newer;
    } else {
        f.age = f.now - s.last_engaged;
        f.engaged = f.age < hold_ms;
        f.cause = f.reasons ? Cause::stamp : f.engaged ? Cause::held : Cause::expired;
    }
    f.filtered = f.engaged ? f.vanilla : 0;
    if (f.vanilla && !f.engaged) {
        m.write32(registry + registry_level, 0);
        f.wrote_zero = true;
    }
    if (!f.vanilla && f.cause != Cause::newer && f.cause != Cause::none) f.cause = Cause::range;
    f.from = s.filtered_prev;
    f.edge = f.filtered != s.filtered_prev;
    s.filtered_prev = f.filtered;
    const bool suppressed = f.vanilla != 0 && f.filtered == 0;
    if (suppressed && !s.episode) {
        f.suppressed_start = true;
        s.episode_frames = 0;
    }
    if (suppressed) ++s.episode_frames;
    if (!suppressed && s.episode) {
        f.suppressed_end = true;
        f.episode_frames = s.episode_frames;
    }
    s.episode = suppressed;
    return f;
}
inline const char* cause_name(Cause c) {
    switch (c) {
    case Cause::stamp: return "stamp";
    case Cause::held: return "held";
    case Cause::expired: return "expired";
    case Cause::newer: return "newer_than_now";
    case Cause::range: return "out_of_range";
    default: return "none";
    }
}
// "a".."f" or combinations ("ab"), "-" for none; `out` holds at least reason_text_size bytes.
constexpr unsigned reason_text_size = 8;
inline const char* reason_text(unsigned reasons, char* out) {
    unsigned n = 0;
    if (reasons & reason_fire) out[n++] = 'a';
    if (reasons & reason_player_hit) out[n++] = 'b';
    if (reasons & reason_owned) out[n++] = 'c';
    if (reasons & reason_missile) out[n++] = 'd';
    if (reasons & reason_hit) out[n++] = 'e';
    if (reasons & reason_hostile_order) out[n++] = 'f';
    if (!n) out[n++] = '-';
    out[n] = 0;
    return out;
}
// X3M_MUSIC_COMBAT_HOLD_S: decimal 1..600 -> ms; false (and the default) otherwise.
inline bool parse_hold_s(const wchar_t* text, unsigned length, std::uint32_t& hold_ms) {
    hold_ms = default_hold_ms;
    if (!length || length > 3) return false;
    std::uint32_t v = 0;
    for (unsigned i = 0; i < length; ++i) {
        if (text[i] < L'0' || text[i] > L'9') return false;
        v = v * 10 + unsigned(text[i] - L'0');
    }
    if (v < min_hold_s || v > max_hold_s) return false;
    hold_ms = v * 1000;
    return true;
}
enum class Trigger : unsigned { engaged, vanilla, invalid };
// X3M_MUSIC_COMBAT_TRIGGER: unset or empty = engaged (the schema default), `engaged`, `vanilla`; anything else invalid.
inline Trigger parse_trigger(const wchar_t* text, unsigned length) {
    if (!length) return Trigger::engaged;
    const wchar_t* const engaged = L"engaged";
    const wchar_t* const vanilla = L"vanilla";
    bool e = length == 7, v = length == 7;
    for (unsigned i = 0; i < length && i < 7; ++i) {
        e = e && text[i] == engaged[i];
        v = v && text[i] == vanilla[i];
    }
    return e ? Trigger::engaged : v ? Trigger::vanilla : Trigger::invalid;
}
}
