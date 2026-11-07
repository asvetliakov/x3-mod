#pragma once
// Step 4 hook cases (docs/architecture/bolt-muzzle-look.md section 7, weapon-look.md 3.3): the per-bullet tap
// 0x0045ac7d, the muzzle 0x00446268 and the flight-effect skip 0x004146c0, run through the real windows the fixture
// reserved in the engine pages. Textually included inside weapon_effects_hook_fixture.cpp's anonymous namespace, so it
// shares at()/w32()/check()/the globals; the asm harness and the callee stand-ins carry unique symbols. The handler
// (weapon_effects_bolt.h) is the production object the audit proved holds no call and no x87; these cases drive it.
//
// Each case jumps to its window with the engine's own register and stack contract, the stub runs, and the window
// returns through a planted jmp at its end to we_bolt_back. The stand-ins at 0x00414590 / 0x00413c00 / 0x004148a0
// record the registers and arguments they received, so the forward path is checked against the state the call left and
// the skip path proves the callee was never entered.
extern "C" {
void we_bolt_tap_enter();
void we_bolt_muzzle_enter();
void we_bolt_flight_enter();
void we_bolt_tap_callee();
void we_bolt_muzzle_callee();
void we_bolt_flight_callee();
void we_bolt_back();
std::uint32_t we_bolt_saved_esp, we_bolt_window_ptr, we_bolt_obj, we_bolt_eax, we_bolt_ecx, we_bolt_esi, we_bolt_ebp_frame;
std::uint32_t we_bolt_flags_in;
std::uint32_t we_bolt_out[8];   // exit ebx, ebp, esi, edi, eflags, esp-delta (below the frame), unused
std::uint32_t we_bolt_ca[16];   // callee: eax, ecx, edx, ebx, ebp, esi, edi, eflags, [esp+0..+8], hit-count at +11
}
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _we_bolt_tap_enter
_we_bolt_tap_enter:
    push ebp
    push ebx
    push esi
    push edi
    mov dword ptr [_we_bolt_saved_esp], esp
    mov ebx, dword ptr [_we_bolt_obj]          # the object: the window's push ebx, and movzx esi,[ebx+0x48]
    mov ebp, 0xeb9eb9eb
    mov edi, 0xed1ed1ed
    mov esi, 0xe51e51e5
    mov eax, 0xa1a1a1a1
    mov ecx, 0xc1c1c1c1
    mov edx, 0xd1d1d1d1
    push dword ptr [_we_bolt_flags_in]
    popfd
    jmp dword ptr [_we_bolt_window_ptr]        # 0x0045ac7c, the push ebx (the gate above it is skipped)

    .p2align 4
    .globl _we_bolt_muzzle_enter
_we_bolt_muzzle_enter:
    push ebp
    push ebx
    push esi
    push edi
    mov dword ptr [_we_bolt_saved_esp], esp
    mov eax, dword ptr [_we_bolt_eax]          # the gun slot ([ebp-0x58], loaded before the window)
    mov ecx, dword ptr [_we_bolt_ecx]          # the ship
    mov esi, dword ptr [_we_bolt_esi]          # &orientation
    mov ebp, dword ptr [_we_bolt_ebp_frame]    # the fire routine's frame base
    mov ebx, 0x5b5b5b5b
    mov edi, 0xed1ed1ed
    push dword ptr [_we_bolt_flags_in]
    popfd
    jmp dword ptr [_we_bolt_window_ptr]        # 0x0044625d

    .p2align 4
    .globl _we_bolt_flight_enter
_we_bolt_flight_enter:
    push ebp
    push ebx
    push esi
    push edi
    mov dword ptr [_we_bolt_saved_esp], esp
    mov esi, dword ptr [_we_bolt_obj]          # the bullet (arg4; pushed by the window)
    mov ecx, dword ptr [_we_bolt_ecx]          # eff (arg3)
    xor ebx, ebx                               # the window's zero pushes
    mov ebp, 0xeb9eb9eb
    mov edi, 0xed1ed1ed
    push dword ptr [_we_bolt_flags_in]
    popfd
    jmp dword ptr [_we_bolt_window_ptr]        # 0x004146b2

    .p2align 4
    .globl _we_bolt_back
_we_bolt_back:                                  # the window's end jumps here (planted jmp)
    mov dword ptr [_we_bolt_out+0], ebx
    mov dword ptr [_we_bolt_out+4], ebp
    mov dword ptr [_we_bolt_out+8], esi
    mov dword ptr [_we_bolt_out+12], edi
    pushfd
    pop dword ptr [_we_bolt_out+16]
    mov eax, dword ptr [_we_bolt_saved_esp]
    sub eax, esp
    mov dword ptr [_we_bolt_out+20], eax        # esp delta (0 when the window balanced the stack)
    mov esp, dword ptr [_we_bolt_saved_esp]
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

    .p2align 4
    .globl _we_bolt_tap_callee
_we_bolt_tap_callee:                            # 0x00414590 stand-in: cdecl 1, records
    mov dword ptr [_we_bolt_ca+0], eax
    mov dword ptr [_we_bolt_ca+4], ecx
    mov dword ptr [_we_bolt_ca+8], edx
    mov dword ptr [_we_bolt_ca+12], ebx
    mov dword ptr [_we_bolt_ca+16], ebp
    mov dword ptr [_we_bolt_ca+20], esi
    mov dword ptr [_we_bolt_ca+24], edi
    pushfd
    pop dword ptr [_we_bolt_ca+28]
    mov eax, dword ptr [esp+4]
    mov dword ptr [_we_bolt_ca+32], eax
    inc dword ptr [_we_bolt_ca+44]
    mov eax, 0x1
    ret

    .p2align 4
    .globl _we_bolt_muzzle_callee
_we_bolt_muzzle_callee:                         # 0x00413c00 stand-in: cdecl 3, records eax/edx and the three args
    mov dword ptr [_we_bolt_ca+0], eax
    mov dword ptr [_we_bolt_ca+8], edx
    mov dword ptr [_we_bolt_ca+12], ebx
    mov dword ptr [_we_bolt_ca+16], ebp
    mov dword ptr [_we_bolt_ca+20], esi
    mov dword ptr [_we_bolt_ca+24], edi
    mov eax, dword ptr [esp+4]
    mov dword ptr [_we_bolt_ca+32], eax
    mov eax, dword ptr [esp+8]
    mov dword ptr [_we_bolt_ca+36], eax
    mov eax, dword ptr [esp+12]
    mov dword ptr [_we_bolt_ca+40], eax
    inc dword ptr [_we_bolt_ca+44]
    mov eax, 0x1
    ret

    .p2align 4
    .globl _we_bolt_flight_callee
_we_bolt_flight_callee:                         # 0x004148a0 stand-in: cdecl 10, records arg4 (the bullet)
    mov eax, dword ptr [esp+16]
    mov dword ptr [_we_bolt_ca+32], eax
    inc dword ptr [_we_bolt_ca+44]
    mov eax, 0x1
    ret
    .att_syntax
)");

// The bolt world: a class-0 bullet with a live node and an extension (owner / created / expiry / length), a second
// bullet, a class-7 ship (the muzzle shooter) with a live node, and the fire routine's &pos / &orientation / frame.
namespace bolt_sites = x3m::weapon_effects::bolt_sites;
alignas(16) Obj bolt_bullet, bolt_bullet2, bolt_ship;
alignas(16) Node bolt_bullet_node, bolt_bullet2_node, bolt_ship_node;
alignas(16) unsigned char bolt_ext[0x100], bolt_ext2[0x100];
alignas(16) std::int32_t bolt_pos[4], bolt_orient[12];
alignas(16) unsigned char bolt_frame_buffer[0x400]; // the fire routine's EBP frame
constexpr std::uint32_t bolt_eff_id = 0x0eff0eff;

void build_bolt_bullet(Obj& obj, Node& node, unsigned char* ext, std::int32_t x, std::uint32_t owner, std::uint16_t row,
                       std::uint32_t created, std::uint32_t expiry, std::int32_t length) {
    std::memset(&obj, 0, sizeof obj);
    std::memset(&node, 0, sizeof node);
    std::memset(ext, 0, 0x100);
    w16(&obj, core::layout::obj_class, core::layout::class_bullet);
    w16(&obj, core::layout::obj_subtype, row);
    w32(&obj, core::layout::obj_node, u32(&node));
    w32(&obj, core::layout::obj_ext, u32(ext));
    w32(&node, core::layout::node_live, 1);
    w32(&node, core::layout::node_handle, 0xb01d);
    w32(&node, core::layout::node_pos + 0, std::uint32_t(x));
    w32(&node, core::layout::node_pos + 4, 0);
    w32(&node, core::layout::node_pos + 8, 0);
    w32(&node, x3m::weapon_effects::core::bolt_layout::node_velocity + 0, 0x10000); // a forward velocity
    w32(&node, x3m::weapon_effects::core::bolt_layout::node_zrow + 0, 0x10000);     // the rotation's z row
    w32(ext, x3m::weapon_effects::core::bolt_layout::ext_expiry, expiry);
    w32(ext, x3m::weapon_effects::core::bolt_layout::ext_owner, owner);
    w32(ext, x3m::weapon_effects::core::bolt_layout::ext_created, created);
    w32(ext, x3m::weapon_effects::core::bolt_layout::ext_length, std::uint32_t(length));
}
we::Addresses bolt_addresses() {
    return we::Addresses{at(sites::impact_window_va),
                         at(sites::impact_target_va),
                         at(sites::prologue_va),
                         {at(sites::tap_window_va[0]), at(sites::tap_window_va[1])},
                         at(sites::tap_target_va),
                         at(bolt_sites::tap_window_va),
                         at(bolt_sites::tap_target_va),
                         at(bolt_sites::muzzle_window_va),
                         at(bolt_sites::muzzle_target_va),
                         at(bolt_sites::flight_window_va),
                         at(bolt_sites::flight_target_va)};
}
void run_bolt(void (*enter)(), std::uintptr_t window_start, std::uintptr_t window_end) {
    std::memset(we_bolt_out, 0, sizeof we_bolt_out);
    we_bolt_window_ptr = std::uint32_t(window_start);
    we_bolt_flags_in = 0x8c5; // CF PF ZF SF OF, DF clear
    write_jmp(window_end, we_bolt_back);
    enter();
}
bool bolt_frame_kept() { // EBP / EDI sentinels, DF clear, the stack balanced
    return we_bolt_out[1] == 0xeb9eb9ebu && we_bolt_out[3] == 0xed1ed1edu && (we_bolt_out[4] & 0x400u) == 0 &&
           we_bolt_out[5] == 0;
}
bool bolt_muzzle_frame_kept() { // EBX / EDI sentinels, EBP the fire-routine frame, DF clear, the stack balanced
    return we_bolt_out[0] == 0x5b5b5b5bu && we_bolt_out[1] == we_bolt_ebp_frame && we_bolt_out[3] == 0xed1ed1edu &&
           (we_bolt_out[4] & 0x400u) == 0 && we_bolt_out[5] == 0;
}
// bolt_tap_overflow_1024_o1 (review 2026-11-07, finding 5): 1024 live bullets (8 of them the own ship's) through the
// production handler, two update passes a frame: the list holds 512 distinct objects, every own bullet among them, its
// records nearer than the bullets' median distance on the whole (the cursor's far candidate goes first), no duplicate
// on the second pass (the tombstoned keys), and the two passes' CPU within the bound (median of 21 frames).
struct BoltSwarm {
    Obj obj;
    Node node;
    unsigned char ext[0x100];
};
void bolt_overflow_case(core::BoltHookState& b) {
    constexpr unsigned live = 1024, own = 8;
    static BoltSwarm swarm[live];
    const std::uint32_t own_ship = 0x5107;
    std::uint64_t all_d = 0;
    for (unsigned i = 0; i < live; ++i) {
        // distances spread 1 .. 1024 km (engine units: 500 a metre) in a scrambled order
        const std::int32_t x = std::int32_t(((i * 389u) % live + 1u) * 1000u * 500u / 8u);
        build_bolt_bullet(swarm[i].obj, swarm[i].node, swarm[i].ext, x, i < own ? own_ship : 0x9001u, 11, 1000, 900000, 0);
        all_d += std::uint64_t(x);
    }
    b.own_owner = own_ship;
    for (unsigned i = 0; i < 3; ++i) b.eye[i] = 0;
    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    double runs[21];
    const core::BoltCounters c0 = b.counters;
    for (double& us : runs) {
        ++b.generation;
        QueryPerformanceCounter(&t0);
        for (unsigned pass = 0; pass < 2; ++pass)
            for (unsigned i = 0; i < live; ++i) {
                // the engine order: own bullets last in each pass (they enter by replacing a candidate)
                const unsigned k = (i + own) % live;
                const std::uint32_t entry[2] = {0, u32(&swarm[k].obj)};
                x3m_weapon_bolt_tap(entry);
            }
        QueryPerformanceCounter(&t1);
        us = double(t1.QuadPart - t0.QuadPart) * 1e6 / double(freq.QuadPart);
    }
    std::sort(runs, runs + 21);
    const core::BoltList& l = b.lists[b.generation & 1u];
    unsigned owns = 0, dup = 0;
    std::uint64_t listed_d = 0;
    for (unsigned i = 0; i < l.count; ++i) {
        owns += l.records[i].own;
        listed_d += std::uint64_t(l.records[i].pos[0]);
        for (unsigned j = 0; j < i; ++j) dup += l.records[j].obj == l.records[i].obj;
    }
    const double mean_listed = l.count ? double(listed_d) / l.count : 0, mean_all = double(all_d) / live;
    const std::uint32_t frames = 21;
    const double overflow = double(b.counters.tap_overflow - c0.tap_overflow) / frames,
                 replaced = double(b.counters.tap_replaced - c0.tap_replaced) / frames,
                 refreshed = double(b.counters.tap_refreshed - c0.tap_refreshed) / frames,
                 unkeyed = double(b.counters.tap_unkeyed - c0.tap_unkeyed) / frames;
    constexpr double bound_us = 150.0; // 3x the measured 47 us median (RTX host, 2026-11-07)
    std::printf("BOLT_TAP_CPU live=%u passes=2 listed=%u own_listed=%u duplicates=%u mean_listed_m=%.0f mean_all_m=%.0f "
                "overflow=%.0f replaced=%.0f refreshed=%.0f unkeyed=%.1f best_us=%.1f median_us=%.1f worst_us=%.1f "
                "bound_us=%.0f\n",
                live, l.count, owns, dup, mean_listed / 500.0, mean_all / 500.0, overflow, replaced, refreshed, unkeyed,
                runs[0], runs[10], runs[20], bound_us);
    check(l.count == core::bolt::list_capacity && owns == own && dup == 0 && mean_listed < 0.75 * mean_all &&
              runs[10] <= bound_us,
          "bolt_tap_overflow_1024_o1");
    b.own_owner = 0;
}
void bolt_checks() {
    core::BoltHookState& b = we::bolt_hook_state();
    // Install shields,hull,bolts: the impact, the bolt tap (plain write), the muzzle and the flight effect (atomic).
    set_setting(L"shields,hull,bolts");
    executable_ok = true;
    const bool installed = we::install_at(bolt_addresses(), core::mode_shields_hull_bolts);
    check(installed && we::installed() && we::mode() == core::mode_shields_hull_bolts && b.armed == 1,
          "bolts_installed", we::state());
    check(call_target(at(bolt_sites::tap_site_va)) == u32f(reinterpret_cast<void (*)()>(&x3m_weapon_effects_stub_bolt_tap)) &&
              call_target(at(bolt_sites::muzzle_site_va)) == u32f(reinterpret_cast<void (*)()>(&x3m_weapon_effects_stub_muzzle)) &&
              call_target(at(bolt_sites::flight_site_va)) == u32f(reinterpret_cast<void (*)()>(&x3m_weapon_effects_stub_flight)),
          "bolt_sites_redirected");
    check(!std::strcmp(we::write_path(3), "plain") && !std::strcmp(we::write_path(4), "atomic") &&
              !std::strcmp(we::write_path(5), "atomic"),
          "bolt_write_paths");
    // The three stand-ins and the bolt world.
    write_jmp(at(bolt_sites::tap_target_va), we_bolt_tap_callee);
    write_jmp(at(bolt_sites::muzzle_target_va), we_bolt_muzzle_callee);
    write_jmp(at(bolt_sites::flight_target_va), we_bolt_flight_callee);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at(0x00413000)), 0x00608000 - 0x00413000);
    const std::uint32_t own_ship = 0x5107, other_ship = 0x9001;
    b.own_owner = own_ship;
    for (unsigned i = 0; i < 3; ++i) b.eye[i] = 0;

    // bolt_tap_record_per_live_bullet: a live class-0 bullet is recorded once per pass; a second pass refreshes it; a
    // second bullet adds a record; always forwards (the stand-in was entered); EBP/EDI and the stack as the call left.
    build_bolt_bullet(bolt_bullet, bolt_bullet_node, bolt_ext, 100000, other_ship, 11, 1000, 9000, 4000);
    std::memset(we_bolt_ca, 0, sizeof we_bolt_ca);
    const std::uint32_t gen = b.generation;
    we_bolt_obj = u32(&bolt_bullet);
    // the window start is the PUSH EBX right before the call (the gate above it is skipped)
    run_bolt(we_bolt_tap_enter, at(bolt_sites::tap_site_va) - 1, at(bolt_sites::tap_window_end_va));
    const core::BoltList& l0 = b.lists[b.generation & 1u];
    bool rec_ok = we_bolt_ca[11] >= 1 && l0.generation == gen && l0.count == 1 && l0.records[0].obj == u32(&bolt_bullet) &&
                  l0.records[0].row == 11 && l0.records[0].owner == other_ship && l0.records[0].created == 1000 &&
                  l0.records[0].expiry == 9000 && l0.records[0].length == 4000 && !l0.records[0].own;
    const std::uint32_t refreshed0 = b.counters.tap_refreshed;
    run_bolt(we_bolt_tap_enter, at(bolt_sites::tap_site_va) - 1, at(bolt_sites::tap_window_end_va));
    rec_ok = rec_ok && l0.count == 1 && b.counters.tap_refreshed == refreshed0 + 1; // same bullet: refreshed in place
    build_bolt_bullet(bolt_bullet2, bolt_bullet2_node, bolt_ext2, 50000, own_ship, 12, 1000, 9000, 0);
    we_bolt_obj = u32(&bolt_bullet2);
    run_bolt(we_bolt_tap_enter, at(bolt_sites::tap_site_va) - 1, at(bolt_sites::tap_window_end_va));
    rec_ok = rec_ok && l0.count == 2 && l0.records[1].obj == u32(&bolt_bullet2) && l0.records[1].own; // own ship's bolt
    check(rec_ok && bolt_frame_kept() && we_bolt_out[0] == u32(&bolt_bullet2), "bolt_tap_record_per_live_bullet");

    // The generation stamp: Present advances it, so the next frame's list starts empty (the parity rule's basis: the
    // in-place draw reads exactly one generation).
    x3m::weapon_effects::BoltFrame bf{};
    bf.drew = true;
    x3m::weapon_effects::present_bolts(1, false, bf);
    we_bolt_obj = u32(&bolt_bullet);
    std::memset(we_bolt_ca, 0, sizeof we_bolt_ca);
    run_bolt(we_bolt_tap_enter, at(bolt_sites::tap_site_va) - 1, at(bolt_sites::tap_window_end_va));
    const core::BoltList& l1 = b.lists[b.generation & 1u];
    check(b.generation == gen + 1 && l1.generation == b.generation && l1.count == 1 && b.owned == 1,
          "bolt_tap_generation_stamped_per_present");

    // A non-class-0 object forwards unrecorded (the stand-in runs, nothing recorded).
    std::memset(we_bolt_ca, 0, sizeof we_bolt_ca);
    const std::uint32_t before = b.lists[b.generation & 1u].count;
    we_bolt_obj = u32(&bolt_ship); // class 0 not set yet; set it a ship
    std::memset(&bolt_ship, 0, sizeof bolt_ship);
    std::memset(&bolt_ship_node, 0, sizeof bolt_ship_node);
    w16(&bolt_ship, core::layout::obj_class, core::layout::class_ship);
    w32(&bolt_ship, core::layout::obj_node, u32(&bolt_ship_node));
    w32(&bolt_ship_node, core::layout::node_live, 1);
    run_bolt(we_bolt_tap_enter, at(bolt_sites::tap_site_va) - 1, at(bolt_sites::tap_window_end_va));
    check(we_bolt_ca[11] >= 1 && b.lists[b.generation & 1u].count == before, "bolt_tap_non_bullet_forwards");

    // muzzle_stub_forwards_eax_edx: the shooter's &pos / &orientation / slot reach the callee, EAX the bullet index,
    // EDX &orientation, unchanged; forwards while not owned.
    for (unsigned i = 0; i < 4; ++i) bolt_pos[i] = 1000 + std::int32_t(i);
    for (unsigned i = 0; i < 12; ++i) bolt_orient[i] = (i % 5 == 0) ? 0x10000 : 0;
    w16(&bolt_ship, core::layout::obj_subtype, 0);
    unsigned char* frame = bolt_frame_buffer + 0x100;
    std::memset(bolt_frame_buffer, 0, sizeof bolt_frame_buffer);
    w32(frame, 0u - 0x7c, 7);                       // [ebp-0x7c] = the bullet index (EAX input)
    std::memcpy(frame - 0x70, bolt_pos, 16);        // [ebp-0x70] = &pos's data (&pos = ebp-0x70)
    b.owned = 0;
    std::memset(we_bolt_ca, 0, sizeof we_bolt_ca);
    we_bolt_eax = 5;                                 // the gun slot
    we_bolt_ecx = u32(&bolt_ship);
    we_bolt_esi = u32(bolt_orient);
    we_bolt_ebp_frame = u32(frame);
    run_bolt(we_bolt_muzzle_enter, at(bolt_sites::muzzle_window_va), at(bolt_sites::muzzle_window_end_va));
    check(we_bolt_ca[11] == 1 && we_bolt_ca[0] == 7 && we_bolt_ca[2] == u32(bolt_orient) &&
              we_bolt_ca[8] == u32(&bolt_ship) && we_bolt_ca[9] == 5 && we_bolt_ca[10] == u32(frame - 0x70) &&
              bolt_muzzle_frame_kept(),
          "muzzle_stub_forwards_eax_edx");

    // muzzle_record_per_shot_per_slot: one record per (ship, slot), re-lit (t0 and seed reset, shots advanced, prev_t0
    // kept); a second slot is a second record.
    w32(config_block, core::layout::cfg_game_ms, 2000);
    run_bolt(we_bolt_muzzle_enter, at(bolt_sites::muzzle_window_va), at(bolt_sites::muzzle_window_end_va));
    w32(config_block, core::layout::cfg_game_ms, 2030);
    run_bolt(we_bolt_muzzle_enter, at(bolt_sites::muzzle_window_va), at(bolt_sites::muzzle_window_end_va));
    const core::MuzzleRecord* m5 = nullptr;
    for (unsigned i = 0; i < core::muzzle::record_capacity; ++i)
        if (b.muzzles[i].sequence && b.muzzles[i].ship == u32(&bolt_ship) && b.muzzles[i].slot == 5) m5 = &b.muzzles[i];
    const bool relit = m5 && m5->t0 == 2030 && m5->prev_t0 == 2000 && m5->shots >= 2 && m5->row == 7;
    we_bolt_eax = 9;
    we_bolt_window_ptr = 0; // reset guard
    run_bolt(we_bolt_muzzle_enter, at(bolt_sites::muzzle_window_va), at(bolt_sites::muzzle_window_end_va));
    unsigned slots = 0;
    for (unsigned i = 0; i < core::muzzle::record_capacity; ++i)
        if (b.muzzles[i].sequence && b.muzzles[i].ship == u32(&bolt_ship)) ++slots;
    check(relit && slots == 2 && m5->seed != 0, "muzzle_record_per_shot_per_slot");

    // muzzle_skips_native_when_staged: the muzzle latch is set (the stage drew the previous frame's muzzles with room)
    // and suppress_muzzle is set, so the stub returns 1, the callee is never entered and the shot is marked ours (the
    // stage builds it); the frame is still as the caller left it. The bolt latch (owned) plays no part.
    auto slot5 = [&]() -> const core::MuzzleRecord* {
        for (unsigned i = 0; i < core::muzzle::record_capacity; ++i)
            if (b.muzzles[i].sequence && b.muzzles[i].ship == u32(&bolt_ship) && b.muzzles[i].slot == 5) return &b.muzzles[i];
        return nullptr;
    };
    we_bolt_eax = 5;
    b.owned = 0;
    b.muzzle_owned = 1;
    b.suppress_muzzle = 1;
    std::memset(we_bolt_ca, 0, sizeof we_bolt_ca);
    const std::uint32_t skipped0 = b.counters.muzzle_skipped;
    run_bolt(we_bolt_muzzle_enter, at(bolt_sites::muzzle_window_va), at(bolt_sites::muzzle_window_end_va));
    check(we_bolt_ca[11] == 0 && b.counters.muzzle_skipped == skipped0 + 1 && bolt_muzzle_frame_kept() && slot5() &&
              slot5()->ours == 1,
          "muzzle_skips_native_when_staged");
    // muzzle_forwards_and_not_ours_without_latch (review 2026-11-07, finding 2): the latch clear (the bolt draw refused:
    // disarmed, so Present latched 0; or the stage's batch was full) forwards the native muzzle and marks the shot not
    // ours, even while the bolt latch says the bolts drew (decoupled): the stage does not build it (never two flashes).
    b.owned = 1;
    b.muzzle_owned = 0;
    std::memset(we_bolt_ca, 0, sizeof we_bolt_ca);
    run_bolt(we_bolt_muzzle_enter, at(bolt_sites::muzzle_window_va), at(bolt_sites::muzzle_window_end_va));
    const bool fwd = we_bolt_ca[11] == 1 && b.counters.muzzle_skipped == skipped0 + 1 && slot5() && slot5()->ours == 0;
    // Present latches the muzzle from the BoltFrame (stage::muzzle_latch): a frame with muzzle_owned false clears it
    x3m::weapon_effects::BoltFrame mf{};
    mf.muzzle_owned = false;
    mf.drew = true;
    b.muzzle_owned = 1;
    x3m::weapon_effects::present_bolts(7, false, mf);
    const bool latched_off = b.muzzle_owned == 0 && b.owned == 1;
    mf.muzzle_owned = true;
    x3m::weapon_effects::present_bolts(8, false, mf);
    const bool latched_on = b.muzzle_owned == 1;
    check(fwd && latched_off && latched_on && bolt_muzzle_frame_kept(), "muzzle_forwards_and_not_ours_without_latch");
    b.owned = 0;
    b.muzzle_owned = 0;
    // muzzle_record_restarts_on_new_handle (note 10): the same object and slot with another node handle (a new ship at a
    // reused object) starts the record over: prev_t0 0, one shot.
    {
        const std::uint32_t restarted0 = b.counters.muzzle_restarted;
        w32(config_block, core::layout::cfg_game_ms, 2100);
        run_bolt(we_bolt_muzzle_enter, at(bolt_sites::muzzle_window_va), at(bolt_sites::muzzle_window_end_va));
        const bool relit5 = slot5() && slot5()->prev_t0 != 0 && slot5()->shots >= 2;
        w32(&bolt_ship_node, core::layout::node_handle, 0x5eed);
        w32(config_block, core::layout::cfg_game_ms, 2200);
        run_bolt(we_bolt_muzzle_enter, at(bolt_sites::muzzle_window_va), at(bolt_sites::muzzle_window_end_va));
        const core::MuzzleRecord* r = slot5();
        check(relit5 && r && r->prev_t0 == 0 && r->shots == 1 && r->handle == 0x5eed && r->t0 == 2200 &&
                  b.counters.muzzle_restarted == restarted0 + 1,
              "muzzle_record_restarts_on_new_handle");
    }
    // muzzle_shooter_class_counted (unknown 13): the shooter's object class per shot (7 ship; 5 dock, 6 factory, other)
    {
        const core::BoltCounters c0 = b.counters;
        run_bolt(we_bolt_muzzle_enter, at(bolt_sites::muzzle_window_va), at(bolt_sites::muzzle_window_end_va));
        w16(&bolt_ship, core::layout::obj_class, core::layout::class_dock);
        run_bolt(we_bolt_muzzle_enter, at(bolt_sites::muzzle_window_va), at(bolt_sites::muzzle_window_end_va));
        w16(&bolt_ship, core::layout::obj_class, 9);
        run_bolt(we_bolt_muzzle_enter, at(bolt_sites::muzzle_window_va), at(bolt_sites::muzzle_window_end_va));
        w16(&bolt_ship, core::layout::obj_class, core::layout::class_ship);
        check(b.counters.shooter_ship == c0.shooter_ship + 1 && b.counters.shooter_dock == c0.shooter_dock + 1 &&
                  b.counters.shooter_other == c0.shooter_other + 1 && b.shooter_last_other == 9,
              "muzzle_shooter_class_counted");
    }

    // The flight effect: skipped (the col 19 sprite that would double the capsule) while owned, forwarded otherwise.
    we_bolt_obj = u32(&bolt_bullet);
    we_bolt_ecx = bolt_eff_id;
    b.owned = 1;
    std::memset(we_bolt_ca, 0, sizeof we_bolt_ca);
    run_bolt(we_bolt_flight_enter, at(bolt_sites::flight_window_va), at(bolt_sites::flight_window_end_va));
    const bool flight_skipped = we_bolt_ca[11] == 0 && bolt_frame_kept();
    b.owned = 0;
    std::memset(we_bolt_ca, 0, sizeof we_bolt_ca);
    run_bolt(we_bolt_flight_enter, at(bolt_sites::flight_window_va), at(bolt_sites::flight_window_end_va));
    const bool flight_forwarded = we_bolt_ca[11] == 1 && we_bolt_ca[8] == u32(&bolt_bullet);
    check(flight_skipped && flight_forwarded, "flight_skip_when_staged_else_forward");

    bolt_overflow_case(b);

    std::printf("BOLT_STATE tap_bullets=%lu tap_overflow=%lu tap_replaced=%lu muzzle_relit=%lu muzzle_skipped=%lu "
                "flight_skipped=%lu generation=%lu\n",
                static_cast<unsigned long>(b.counters.tap_bullets), static_cast<unsigned long>(b.counters.tap_overflow),
                static_cast<unsigned long>(b.counters.tap_replaced), static_cast<unsigned long>(b.counters.muzzle_relit),
                static_cast<unsigned long>(b.counters.muzzle_skipped),
                static_cast<unsigned long>(b.counters.flight_skipped), static_cast<unsigned long>(b.generation));
    we::shutdown();
    check(!we::installed() && all_windows_original() &&
              window_is(bolt_sites::tap_window_va, bolt_sites::expected_tap_window, bolt_sites::tap_window_length) &&
              window_is(bolt_sites::muzzle_window_va, bolt_sites::expected_muzzle_window, bolt_sites::muzzle_window_length) &&
              window_is(bolt_sites::flight_window_va, bolt_sites::expected_flight_window, bolt_sites::flight_window_length),
          "bolt_shutdown_restores_all");

    // A changed bolt-tap byte refuses before any write; a muzzle byte likewise.
    executable_ok = true;
    *reinterpret_cast<unsigned char*>(at(bolt_sites::tap_site_va + 1)) ^= 0xff;
    SetLastError(sentinel_error);
    bool refused = !we::install_at(bolt_addresses(), core::mode_shields_hull_bolts) &&
                   !std::strcmp(we::state(), "bytes_mismatch_bolt_tap");
    *reinterpret_cast<unsigned char*>(at(bolt_sites::tap_site_va + 1)) ^= 0xff;
    check(refused && GetLastError() == sentinel_error && !we::installed() && all_windows_original() &&
              !std::strcmp(we::site_state(3), "bytes_mismatch"),
          "bolt_tap_byte_mismatch_refused", we::state());
    *reinterpret_cast<unsigned char*>(at(bolt_sites::muzzle_site_va + 1)) ^= 0xff;
    refused = !we::install_at(bolt_addresses(), core::mode_shields_hull_bolts) &&
              !std::strcmp(we::state(), "bytes_mismatch_muzzle");
    *reinterpret_cast<unsigned char*>(at(bolt_sites::muzzle_site_va + 1)) ^= 0xff;
    check(refused && !we::installed() && all_windows_original(), "muzzle_byte_mismatch_refused", we::state());

    // Rollback: a faulted read-back at the bolt tap (the last plain write) restores every site claimed before it.
    fault_read_at = at(bolt_sites::tap_site_va);
    SetLastError(sentinel_error);
    const bool rolled = !we::install_at(bolt_addresses(), core::mode_shields_hull_bolts);
    check(rolled && fault_read_at == 0 && !we::installed() && all_windows_original() &&
              window_is(bolt_sites::tap_window_va, bolt_sites::expected_tap_window, bolt_sites::tap_window_length) &&
              window_is(bolt_sites::muzzle_window_va, bolt_sites::expected_muzzle_window, bolt_sites::muzzle_window_length) &&
              GetLastError() == sentinel_error && !std::strcmp(we::site_state(3), "readback_rolled_back"),
          "bolt_rollback_restores_all", we::site_state(3));
    fault_read_at = 0;
}
