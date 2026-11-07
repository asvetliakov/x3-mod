#pragma once
// Step 3 hook cases (docs/architecture/explosion-look.md sections 1 and 9 "hook record", weapon-look.md 3.2; the sites
// in src/proxy/weapon_effects_death_sites.h): the death sites 0x0044ac64 (area) and 0x0044acb2 (other) -> 0x00413610,
// run through the real windows the fixture reserved in the engine pages. Textually included inside
// weapon_effects_hook_fixture.cpp's anonymous namespace (after weapon_effects_bolt_cases.h), so it shares at() / w32() /
// check() / write_jmp() and the globals; the asm harness and the callee stand-in carry unique symbols. The handler
// (core::death through x3m_weapon_death_area / _other) is the production object the audit proved holds no call and
// no x87.
//
// Each case enters its window with the kill routine's contract (EDI = the dying object, EBP its frame: the reason word
// at +8, the killer at +0xc, arg4 at +0x10; EBX / ESI / EAX / ECX / EDX sentinels, hostile MXCSR and x87 control word,
// two x87 values, LastError set), the stub runs, and the window returns through a planted jmp at its end to
// we_death_back, which records the exit state. The stand-in at 0x00413610 records the registers, the return address,
// its three arguments and ESP; it can run nested deaths through the other window while it is entered (an area blast's
// kills inside one outer call: the harness's saved-ESP stack is re-entrant).
extern "C" {
void we_death_enter();
void we_death_back();
void we_death_callee();
void __cdecl we_death_nested();
std::uint32_t we_death_saved_esp[4], we_death_depth, we_death_window_ptr, we_death_obj, we_death_ebp, we_death_flags_in;
std::uint32_t we_death_mxcsr_in, we_death_fcw_in, we_death_saved_mxcsr, we_death_saved_fcw;
// exit: eax, ebx, ebp, esi, edi, eflags, esp delta, mxcsr, fcw, st0 (2 dwords), st1 (2 dwords)
std::uint32_t we_death_out[16];
// callee: eax, ecx, edx, ebx, ebp, esi, edi, eflags, [esp] (the return), the three arguments, entries, esp
std::uint32_t we_death_ca[16];
}
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _we_death_enter
_we_death_enter:
    push ebp
    push ebx
    push esi
    push edi
    mov ecx, dword ptr [_we_death_depth]
    mov dword ptr [_we_death_saved_esp+ecx*4], esp
    inc dword ptr [_we_death_depth]
    test ecx, ecx
    jnz 1f
    stmxcsr dword ptr [_we_death_saved_mxcsr]
    fnstcw word ptr [_we_death_saved_fcw]
    ldmxcsr dword ptr [_we_death_mxcsr_in]
    fldcw word ptr [_we_death_fcw_in]
    fld qword ptr [_we_death_x87_a]
    fld qword ptr [_we_death_x87_b]
1:
    mov edi, dword ptr [_we_death_obj]          # the dying object (the window's push edi)
    mov ebp, dword ptr [_we_death_ebp]          # the kill routine's frame
    mov ebx, 0x5b5b5b5b
    mov esi, 0xe51e51e5
    mov eax, 0xa1a1a1a1
    mov ecx, 0xc1c1c1c1
    mov edx, 0xd1d1d1d1
    push dword ptr [_we_death_flags_in]
    popfd
    jmp dword ptr [_we_death_window_ptr]

    .p2align 4
    .globl _we_death_back
_we_death_back:                                  # the window's end jumps here (planted jmp)
    mov dword ptr [_we_death_out+0], eax
    mov dword ptr [_we_death_out+4], ebx
    mov dword ptr [_we_death_out+8], ebp
    mov dword ptr [_we_death_out+12], esi
    mov dword ptr [_we_death_out+16], edi
    pushfd
    pop dword ptr [_we_death_out+20]
    cld
    dec dword ptr [_we_death_depth]
    mov ecx, dword ptr [_we_death_depth]
    mov eax, dword ptr [_we_death_saved_esp+ecx*4]
    sub eax, esp
    mov dword ptr [_we_death_out+24], eax        # 0 when the window balanced the stack
    test ecx, ecx
    jnz 2f
    stmxcsr dword ptr [_we_death_out+28]
    fnstcw word ptr [_we_death_out+32]
    fstp qword ptr [_we_death_out+36]
    fstp qword ptr [_we_death_out+44]
    ldmxcsr dword ptr [_we_death_saved_mxcsr]
    fldcw word ptr [_we_death_saved_fcw]
2:
    mov ecx, dword ptr [_we_death_depth]
    mov esp, dword ptr [_we_death_saved_esp+ecx*4]
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

    .p2align 4
    .globl _we_death_callee
_we_death_callee:                                # 0x00413610 stand-in: cdecl 3, records, runs the armed nested deaths
    mov dword ptr [_we_death_ca+0], eax
    mov dword ptr [_we_death_ca+4], ecx
    mov dword ptr [_we_death_ca+8], edx
    mov dword ptr [_we_death_ca+12], ebx
    mov dword ptr [_we_death_ca+16], ebp
    mov dword ptr [_we_death_ca+20], esi
    mov dword ptr [_we_death_ca+24], edi
    pushfd
    pop dword ptr [_we_death_ca+28]
    mov eax, dword ptr [esp]
    mov dword ptr [_we_death_ca+32], eax
    mov eax, dword ptr [esp+4]
    mov dword ptr [_we_death_ca+36], eax
    mov eax, dword ptr [esp+8]
    mov dword ptr [_we_death_ca+40], eax
    mov eax, dword ptr [esp+12]
    mov dword ptr [_we_death_ca+44], eax
    inc dword ptr [_we_death_ca+48]
    mov dword ptr [_we_death_ca+52], esp
    call _we_death_nested
    mov eax, 0x1
    ret

    .section .rodata
    .balign 8
_we_death_x87_a:
    .double 1.25
_we_death_x87_b:
    .double -3.5e10
    .text
    .att_syntax
)");

namespace death_sites = x3m::weapon_effects::death_sites;
alignas(16) Obj death_obj[6];
alignas(16) Node death_node[6];
alignas(16) unsigned char death_frame[0x100]; // the kill routine's EBP frame: reason +8, killer +0xc, arg4 +0x10
std::vector<unsigned char> death_missile_table(4 * core::layout::type_stride);
std::vector<unsigned char> death_station_table(4 * core::layout::type_stride);
std::vector<unsigned char> death_asteroid_table(4 * core::layout::type_stride);
std::uint32_t death_missile_slot, death_missile_count, death_dock_slot, death_dock_count, death_factory_slot,
    death_factory_count, death_asteroid_slot, death_asteroid_count;
// The nested deaths the stand-in runs while it is entered (objects through the other window), then disarmed.
unsigned death_nest_count = 0;
std::uint32_t death_nest_objects[4];
constexpr std::uint32_t death_killer = 0x00c0ffeeu, death_arg4 = 0x00a4a4a4u, death_reason = 7;
constexpr std::uint32_t death_entry_flags = 0x8c5; // CF PF ZF SF OF (DF clear)

void death_object(unsigned i, std::uint16_t cls, std::uint16_t subtype, std::uint32_t flags, std::int32_t radius,
                  std::int32_t x) {
    std::memset(&death_obj[i], 0, sizeof death_obj[i]);
    std::memset(&death_node[i], 0, sizeof death_node[i]);
    w16(&death_obj[i], core::layout::obj_class, cls);
    w16(&death_obj[i], core::layout::obj_subtype, subtype);
    w32(&death_obj[i], core::layout::obj_flags, flags | core::layout::obj_dying);
    w32(&death_obj[i], core::layout::obj_node, u32(&death_node[i]));
    w32(&death_obj[i], core::layout::obj_radius, std::uint32_t(radius));
    w32(&death_node[i], core::layout::node_live, 1);
    w32(&death_node[i], core::layout::node_handle, 0xdead0000u + i);
    w32(&death_node[i], core::layout::node_pos + 0, std::uint32_t(x));
    w32(&death_node[i], core::layout::node_pos + 4, 2000u);
    w32(&death_node[i], core::layout::node_pos + 8, 0xfffff000u);
    for (unsigned k = 0; k < 3; ++k) w32(&death_node[i], core::layout::node_rot + 16 * k + 4 * k, 0x10000);
}
std::uintptr_t death_window(unsigned site) {
    return at(site ? death_sites::other_window_va : death_sites::area_window_va);
}
std::uintptr_t death_window_end(unsigned site) {
    return at(site ? death_sites::other_window_end_va : death_sites::area_window_end_va);
}
std::uintptr_t death_site_at(unsigned site) {
    return at(site ? death_sites::other_site_va : death_sites::area_site_va);
}
// One death through a window (site 0 area, 1 other); LastError is set before and read after.
DWORD run_death(unsigned site, const Obj* obj) {
    std::memset(we_death_out, 0, sizeof we_death_out);
    we_death_window_ptr = std::uint32_t(death_window(site));
    we_death_obj = u32(obj);
    we_death_ebp = u32(death_frame);
    we_death_flags_in = death_entry_flags;
    we_death_mxcsr_in = hostile_mxcsr;
    we_death_fcw_in = hostile_fcw;
    SetLastError(sentinel_error);
    we_death_enter();
    return GetLastError();
}
// The frame the kill routine expects after the call: EBX / ESI / EBP sentinels, EDI the object (and EAX = EDI from the
// window's reload), DF clear, the flags the call left (the caller tests none), MXCSR, the x87 control word and stack,
// LastError, the stack balanced.
bool death_frame_kept(DWORD error, const Obj* obj) {
    double st0 = 0, st1 = 0;
    std::memcpy(&st0, we_death_out + 9, 8);
    std::memcpy(&st1, we_death_out + 11, 8);
    return we_death_out[1] == 0x5b5b5b5bu && we_death_out[2] == u32(death_frame) && we_death_out[3] == 0xe51e51e5u &&
           we_death_out[4] == u32(obj) && we_death_out[0] == u32(obj) && (we_death_out[5] & 0x400u) == 0 &&
           we_death_out[6] == 0 && we_death_out[7] == hostile_mxcsr && (we_death_out[8] & 0xffffu) == hostile_fcw &&
           st0 == -3.5e10 && st1 == 1.25 && error == sentinel_error;
}
// The forward path: the stand-in entered once with the call's registers (EAX / ECX / EDX as the window left them: ECX
// arg4, EDX the killer), EBX / ESI / EDI / EBP, the flags, the return address after the site, the three arguments and
// ESP three dwords below the frame.
bool death_forward_exact(unsigned site, const Obj* obj, std::uint32_t entries) {
    return we_death_ca[12] == entries && we_death_ca[0] == 0xa1a1a1a1u && we_death_ca[1] == death_arg4 &&
           we_death_ca[2] == death_killer && we_death_ca[3] == 0x5b5b5b5bu && we_death_ca[4] == u32(death_frame) &&
           we_death_ca[5] == 0xe51e51e5u && we_death_ca[6] == u32(obj) &&
           (we_death_ca[7] & flag_mask) == (death_entry_flags & flag_mask) &&
           we_death_ca[8] == std::uint32_t(death_site_at(site) + death_sites::call_length) && we_death_ca[9] == u32(obj) &&
           we_death_ca[10] == death_killer && we_death_ca[11] == death_arg4 &&
           we_death_ca[13] == we_death_saved_esp[0] - 16u;
}
// The newest death record of an object.
bool death_record(const Obj* obj, core::Event* out) {
    core::Event e{};
    return newest_record(u32(obj), &e) && e.role == core::role_explosion && (*out = e, true);
}
we::Addresses death_addresses() {
    we::Addresses a = bolt_addresses();
    a.death_area_window = at(death_sites::area_window_va);
    a.death_other_window = at(death_sites::other_window_va);
    a.death_target = at(death_sites::target_va);
    return a;
}
bool death_windows_original() {
    return window_is(death_sites::area_window_va, death_sites::expected_area_window, death_sites::window_length) &&
           window_is(death_sites::other_window_va, death_sites::expected_other_window, death_sites::window_length);
}
} // namespace (the nested hook is called from the stand-in's asm)
extern "C" void __cdecl we_death_nested() {
    if (!death_nest_count) return;
    const unsigned n = death_nest_count;
    death_nest_count = 0;
    const std::uint32_t window = we_death_window_ptr, obj = we_death_obj;
    for (unsigned k = 0; k < n; ++k) {
        we_death_window_ptr = std::uint32_t(death_window(1));
        we_death_obj = death_nest_objects[k];
        we_death_enter();
    }
    we_death_window_ptr = window;
    we_death_obj = obj;
}
namespace {
void death_checks() {
    core::HookState& hs = we::hook_state();
    // The two death windows in the fixture's engine pages (the engine's bytes, weapon_effects_death_sites.h).
    std::memcpy(reinterpret_cast<void*>(at(death_sites::area_window_va)), death_sites::expected_area_window,
                death_sites::window_length);
    std::memcpy(reinterpret_cast<void*>(at(death_sites::other_window_va)), death_sites::expected_other_window,
                death_sites::window_length);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at(0x00413000)), 0x00608000 - 0x00413000);
    // The tables the handler reads at a death (the fixture's own dwords): TShips rows 4 M3 (2023, row 35), 5 M6
    // (2026, row 40), 6 M2 (2022, row 37); TMissiles row 2 (row 39); TFactories row 1 (row 201); TAsteroids row 3 (59).
    w32(ship_table.data() + 4 * core::layout::type_stride, core::layout::ship_death_row, 35);
    w32(ship_table.data() + 5 * core::layout::type_stride, core::layout::ship_class_id, 2026);
    w32(ship_table.data() + 5 * core::layout::type_stride, core::layout::ship_death_row, 40);
    w32(ship_table.data() + 6 * core::layout::type_stride, core::layout::ship_class_id, 2022);
    w32(ship_table.data() + 6 * core::layout::type_stride, core::layout::ship_death_row, 37);
    w32(death_missile_table.data() + 2 * core::layout::type_stride, core::layout::missile_death_row, 39);
    w32(death_station_table.data() + 1 * core::layout::type_stride, core::layout::station_death_row, 201);
    w32(death_asteroid_table.data() + 3 * core::layout::type_stride, core::layout::asteroid_death_row, 59);
    death_missile_slot = u32(death_missile_table.data());
    death_dock_slot = death_factory_slot = u32(death_station_table.data());
    death_asteroid_slot = u32(death_asteroid_table.data());
    death_missile_count = death_dock_count = death_factory_count = death_asteroid_count = 4;
    hs.globals.missile_table_slot = reinterpret_cast<std::uintptr_t>(&death_missile_slot);
    hs.globals.missile_count_slot = reinterpret_cast<std::uintptr_t>(&death_missile_count);
    hs.globals.dock_table_slot = reinterpret_cast<std::uintptr_t>(&death_dock_slot);
    hs.globals.dock_count_slot = reinterpret_cast<std::uintptr_t>(&death_dock_count);
    hs.globals.factory_table_slot = reinterpret_cast<std::uintptr_t>(&death_factory_slot);
    hs.globals.factory_count_slot = reinterpret_cast<std::uintptr_t>(&death_factory_count);
    hs.globals.asteroid_table_slot = reinterpret_cast<std::uintptr_t>(&death_asteroid_slot);
    hs.globals.asteroid_count_slot = reinterpret_cast<std::uintptr_t>(&death_asteroid_count);
    std::memset(death_frame, 0, sizeof death_frame);
    w16(death_frame, core::layout::caller_reason, std::uint16_t(death_reason));
    w32(death_frame, 0xc, death_killer);
    w32(death_frame, 0x10, death_arg4);
    w32(config_block, core::layout::cfg_game_ms, 50000);

    // death_mode_parse: the token in any order, refused without the families below it.
    core::Mode m = core::mode_native;
    const auto parse = [&](const char* s) {
        m = core::mode_native;
        return core::parse_mode(s, std::strlen(s), &m);
    };
    const bool p1 = parse("shields,hull,explosions") && m == core::mode_shields_hull_explosions;
    const bool p2 = parse(" explosions , hull,shields") && m == core::mode_shields_hull_explosions;
    const bool p3 = parse("shields,hull,bolts,explosions") && m == core::mode_shields_hull_bolts_explosions;
    const bool p4 = parse("explosions,bolts,hull,shields") && m == core::mode_shields_hull_bolts_explosions;
    const bool refused = !parse("explosions") && !parse("shields,explosions") && !parse("hull,explosions") &&
                         !parse("shields,hull,explosions,explosions") && !parse("shields,hull,explosion");
    check(p1 && p2 && p3 && p4 && refused && core::explosions_mode(core::mode_shields_hull_bolts_explosions) &&
              core::hull_mode(core::mode_shields_hull_explosions) && core::stage_mode(core::mode_shields_hull_explosions) &&
              !core::bolts_mode(core::mode_shields_hull_explosions) && !core::explosions_mode(core::mode_shields_hull_bolts),
          "death_mode_parse");
    // death_decision_seam: boom::decide is the one place a tier is ours or native (fighters and missiles ours; the big
    // tiers in boom::tier_native, the asteroid and any other class native).
    check(core::boom::decide(core::boom::fighter) == core::boom::decide_ours &&
              core::boom::decide(core::boom::missile) == core::boom::decide_ours &&
              core::boom::decide(core::boom::corvette) == core::boom::decide_native &&
              core::boom::decide(core::boom::capital) == core::boom::decide_native &&
              core::boom::decide(core::boom::station) == core::boom::decide_native &&
              core::boom::decide(core::boom::asteroid) == core::boom::decide_native &&
              core::boom::decide(core::boom::other) == core::boom::decide_native,
          "death_decision_seam");

    // Install shields,hull,explosions: the impact and both death sites (the area one plain, the other atomic).
    set_setting(L"shields,hull,explosions");
    executable_ok = true;
    const bool installed = we::install_at(death_addresses(), core::mode_shields_hull_explosions);
    const void* stub_area = reinterpret_cast<const void*>(&x3m_weapon_effects_stub_death_area);
    const void* stub_other = reinterpret_cast<const void*>(&x3m_weapon_effects_stub_death_other);
    check(installed && we::installed() && we::mode() == core::mode_shields_hull_explosions &&
              (hs.armed & core::family_boom) && call_target(death_site_at(0)) == u32(stub_area) &&
              call_target(death_site_at(1)) == u32(stub_other) && !std::strcmp(we::write_path(6), "plain") &&
              !std::strcmp(we::write_path(7), "atomic") && !std::strcmp(we::site_state(3), "not_used") &&
              impact_redirected(),
          "death_installed", we::state());
    write_jmp(at(death_sites::target_va), we_death_callee);
    write_jmp(death_window_end(0), we_death_back);
    write_jmp(death_window_end(1), we_death_back);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at(0x00413000)), 0x00608000 - 0x00413000);

    // Objects: 0 an M3 fighter, 1 a missile, 2 an M6 corvette, 3 an asteroid, 4 a rule-(b) area missile, 5 a factory.
    death_object(0, core::layout::class_ship, 4, 0, 14 * 500, 100000);
    death_object(1, core::layout::class_missile, 2, 0, 3 * 500, 200000);
    death_object(2, core::layout::class_ship, 5, 0, 87 * 500, 300000);
    death_object(3, core::layout::class_asteroid, 3, 0, 60 * 500, 400000);
    death_object(4, core::layout::class_missile, 2, core::layout::obj_missile_area, 3 * 500, 500000);
    death_object(5, core::layout::class_factory, 1, 0, 1800 * 500, 600000);

    // death_forwards_when_not_owned: a fighter in a frame the stage did not own forwards byte-exact and is census
    // (flag_forwarded, no life); the frame as the caller expects.
    hs.owned_next = 0;
    hs.boom_ready = 0;
    std::memset(we_death_ca, 0, sizeof we_death_ca);
    DWORD err = run_death(1, &death_obj[0]);
    core::Event rec{};
    bool rec_ok = death_record(&death_obj[0], &rec);
    check(death_forward_exact(1, &death_obj[0], 1) && death_frame_kept(err, &death_obj[0]) && rec_ok &&
              (rec.flags & core::flag_forwarded) && !(rec.flags & core::flag_stage) && rec.style == core::boom::fighter &&
              rec.lifetime_ms == 0,
          "death_forwards_when_not_owned");
    std::printf("DEATH_FORWARD_STATE eflags_entry=%08lx eflags_callee=%08lx esp_frame=%08lx esp_callee=%08lx return=%08lx "
                "site=%08lx ecx=%08lx edx=%08lx\n",
                static_cast<unsigned long>(death_entry_flags), static_cast<unsigned long>(we_death_ca[7]),
                static_cast<unsigned long>(we_death_saved_esp[0]), static_cast<unsigned long>(we_death_ca[13]),
                static_cast<unsigned long>(we_death_ca[8]), static_cast<unsigned long>(death_site_at(1)),
                static_cast<unsigned long>(we_death_ca[1]), static_cast<unsigned long>(we_death_ca[2]));

    // death_skips_fighter_when_owned: the stage owned the frame and holds the death programs: the stand-in is never
    // entered, the record is the stage's (flag_stage, the fighter's life, the class, the death row, the reason, the
    // killer, the world position), the frame kept.
    hs.owned_next = 1;
    hs.boom_ready = 1;
    hs.hull_ready = 1;
    std::memset(we_death_ca, 0, sizeof we_death_ca);
    const std::uint32_t ours0 = hs.counters.death_ours;
    err = run_death(1, &death_obj[0]);
    rec_ok = death_record(&death_obj[0], &rec);
    check(we_death_ca[12] == 0 && death_frame_kept(err, &death_obj[0]) && rec_ok && (rec.flags & core::flag_stage) &&
              !(rec.flags & core::flag_forwarded) && rec.style == core::boom::fighter && rec.table == 7 && rec.row == 35 &&
              rec.tint_rgb == 2023 && rec.shield_q == death_reason && std::uint32_t(rec.damage) == death_killer &&
              rec.pos[0] == 100000 && rec.pos[1] == 2000 && rec.pos[2] == -4096 && rec.radius == 7000 &&
              rec.lifetime_ms == core::boom::record_life_ms[core::boom::fighter] && rec.tint_source == 1 &&
              hs.counters.death_ours == ours0 + 1,
          "death_skips_fighter_when_owned");
    // death_skips_missile_when_owned: a missile without the area flag is ours too (its own tier and life).
    std::memset(we_death_ca, 0, sizeof we_death_ca);
    err = run_death(1, &death_obj[1]);
    rec_ok = death_record(&death_obj[1], &rec);
    check(we_death_ca[12] == 0 && death_frame_kept(err, &death_obj[1]) && rec_ok && (rec.flags & core::flag_stage) &&
              rec.style == core::boom::missile && rec.row == 39 && rec.table == 10 &&
              rec.lifetime_ms == core::boom::record_life_ms[core::boom::missile],
          "death_skips_missile_when_owned");
    // death_forwards_big_tier_census: an M6 (corvette, boom::tier_native) and a factory (station) forward byte-exact
    // while owned, each recorded census-only with its tier and row.
    std::memset(we_death_ca, 0, sizeof we_death_ca);
    err = run_death(1, &death_obj[2]);
    core::Event rc{}, rs{};
    const bool corvette = death_forward_exact(1, &death_obj[2], 1) && death_frame_kept(err, &death_obj[2]) &&
                          death_record(&death_obj[2], &rc) && (rc.flags & core::flag_forwarded) &&
                          rc.style == core::boom::corvette && rc.row == 40;
    std::memset(we_death_ca, 0, sizeof we_death_ca);
    err = run_death(1, &death_obj[5]);
    const bool station = death_forward_exact(1, &death_obj[5], 1) && death_frame_kept(err, &death_obj[5]) &&
                         death_record(&death_obj[5], &rs) && (rs.flags & core::flag_forwarded) &&
                         rs.style == core::boom::station && rs.row == 201 && rs.table == 6;
    check(corvette && station, "death_forwards_big_tier_census");
    // death_forwards_asteroid: an asteroid forwards byte-exact (rock geometry stays the engine's), census.
    std::memset(we_death_ca, 0, sizeof we_death_ca);
    err = run_death(1, &death_obj[3]);
    rec_ok = death_record(&death_obj[3], &rec);
    check(death_forward_exact(1, &death_obj[3], 1) && death_frame_kept(err, &death_obj[3]) && rec_ok &&
              (rec.flags & core::flag_forwarded) && rec.style == core::boom::asteroid && rec.row == 59,
          "death_forwards_asteroid");
    // death_forwards_rule_b_missile: the area missile with a killer forwards byte-exact with no record (the callee
    // itself makes no explosion; its area damage follows in the kill routine).
    std::memset(we_death_ca, 0, sizeof we_death_ca);
    const std::uint32_t rule_b0 = hs.counters.death_rule_b, records0 = hs.counters.death_records;
    err = run_death(1, &death_obj[4]);
    check(death_forward_exact(1, &death_obj[4], 1) && death_frame_kept(err, &death_obj[4]) &&
              !death_record(&death_obj[4], &rec) && hs.counters.death_rule_b == rule_b0 + 1 &&
              hs.counters.death_records == records0,
          "death_forwards_rule_b_missile");
    // death_area_bullet_forwards: at the area site a class-0 bullet forwards byte-exact unrecorded (step 7); a ship
    // that reaches the area site (the bullet flag on another class: the site tests the class first) is classified as
    // at the other site (a fighter: skipped, ours).
    death_object(4, core::layout::class_bullet, 3, 0x40004801u, 50, 510000);
    std::memset(we_death_ca, 0, sizeof we_death_ca);
    const std::uint32_t bullets0 = hs.counters.death_bullet;
    err = run_death(0, &death_obj[4]);
    const bool bullet = death_forward_exact(0, &death_obj[4], 1) && death_frame_kept(err, &death_obj[4]) &&
                        !death_record(&death_obj[4], &rec) && hs.counters.death_bullet == bullets0 + 1;
    death_object(0, core::layout::class_ship, 4, 0x40000000u, 14 * 500, 110000);
    std::memset(we_death_ca, 0, sizeof we_death_ca);
    err = run_death(0, &death_obj[0]);
    const bool ship = we_death_ca[12] == 0 && death_frame_kept(err, &death_obj[0]) && death_record(&death_obj[0], &rec) &&
                      (rec.flags & core::flag_stage) && rec.style == core::boom::fighter && rec.tint_source == 0;
    check(bullet && ship, "death_area_bullet_forwards");
    // death_retires_owner_hits: the dying object's live shield hit leaves with the death record (a staged hull hit
    // stays: its sparks outlive the owner).
    core::Event* hit = core::ring_acquire(hs.ring);
    hit->role = core::role_shield_hit;
    hit->flags = core::flag_stage;
    hit->owner = u32(&death_obj[1]);
    core::Event* hull = core::ring_acquire(hs.ring);
    hull->role = core::role_hull_hit;
    hull->flags = core::flag_stage;
    hull->owner = u32(&death_obj[1]);
    const std::uint32_t hit_seq = hit->sequence, hull_seq = hull->sequence;
    death_object(1, core::layout::class_missile, 2, 0, 3 * 500, 210000);
    run_death(1, &death_obj[1]);
    bool hit_left = true, hull_kept = false;
    for (unsigned i = 0; i < core::ring_capacity; ++i) {
        if (hs.ring.slots[i].sequence == hit_seq) hit_left = false;
        if (hs.ring.slots[i].sequence == hull_seq) hull_kept = true;
    }
    check(hit_left && hull_kept && death_record(&death_obj[1], &rec), "death_retires_owner_hits");
    // death_nested_stack: a corvette forwarded natively whose callee runs two fighter kills through the other window
    // while it is entered (an area blast inside one outer call): three records, each its own stub; the two nested ones
    // skipped (the stand-in entered once, by the outer), marked nested (the same game ms); the outer's frame kept.
    death_object(2, core::layout::class_ship, 5, 0, 87 * 500, 310000);
    death_object(0, core::layout::class_ship, 4, 0, 14 * 500, 120000);
    death_object(4, core::layout::class_ship, 4, 0, 12 * 500, 130000);
    w32(config_block, core::layout::cfg_game_ms, 61000);
    death_nest_objects[0] = u32(&death_obj[0]);
    death_nest_objects[1] = u32(&death_obj[4]);
    death_nest_count = 2;
    std::memset(we_death_ca, 0, sizeof we_death_ca);
    const std::uint32_t nested0 = hs.counters.death_nested, recs0 = hs.counters.death_records;
    err = run_death(1, &death_obj[2]);
    core::Event r0{}, r1{}, r2{};
    const bool three = death_record(&death_obj[2], &r2) && death_record(&death_obj[0], &r0) &&
                       death_record(&death_obj[4], &r1) && hs.counters.death_records == recs0 + 3;
    const bool kinds = (r2.flags & core::flag_forwarded) && (r0.flags & core::flag_stage) && (r1.flags & core::flag_stage) &&
                       (r0.flags & core::flag_nested) && (r1.flags & core::flag_nested) && r0.sequence > r2.sequence &&
                       r1.sequence > r0.sequence && hs.counters.death_nested == nested0 + 2 && we_death_depth == 0;
    // the outer: the stand-in entered once (with the corvette), its frame kept after both nested windows returned
    check(three && kinds && we_death_ca[12] == 1 && we_death_ca[9] == u32(&death_obj[2]) &&
              death_frame_kept(err, &death_obj[2]),
          "death_nested_stack");
    std::printf("DEATH_NESTED records=%lu nested=%lu outer_entries=%lu outer_seq=%lu inner_seq=%lu,%lu\n",
                static_cast<unsigned long>(hs.counters.death_records - recs0),
                static_cast<unsigned long>(hs.counters.death_nested - nested0), static_cast<unsigned long>(we_death_ca[12]),
                static_cast<unsigned long>(r2.sequence), static_cast<unsigned long>(r0.sequence),
                static_cast<unsigned long>(r1.sequence));
    // death_present_keeps_ours_releases_census: Present releases the census records once logged, keeps the stage's to
    // their life plus the stage's floor, then releases them.
    hs.boom_floor_ms = 100;
    x3m::weapon_effects::StageFrame sf{};
    sf.hull_ready = true;
    sf.boom_ready = true;
    x3m::weapon_effects::present(300, false, true, sf);
    const bool census_gone = !death_record(&death_obj[2], &rec);
    const bool ours_kept = death_record(&death_obj[0], &rec);
    const bool latched = hs.boom_ready == 1 && hs.owned_next == 1;
    w32(config_block, core::layout::cfg_game_ms, 61000 + core::boom::record_life_ms[core::boom::fighter] + 50);
    x3m::weapon_effects::present(301, false, true, sf);
    const bool floor_kept = death_record(&death_obj[0], &rec);
    w32(config_block, core::layout::cfg_game_ms, 61000 + core::boom::record_life_ms[core::boom::fighter] + 200);
    sf.boom_ready = false;
    x3m::weapon_effects::present(302, false, true, sf);
    const bool released = !death_record(&death_obj[0], &rec) && hs.boom_ready == 0;
    check(census_gone && ours_kept && latched && floor_kept && released, "death_present_keeps_ours_releases_census");
    hs.boom_floor_ms = 0;
    // death_nested_after_return: the engine's own order (spawn note 6: the kill routine's area damage 0x0045eeb0 runs
    // after 0x00413610 has returned, at 0x0044ad18): an area missile's own death goes through the area window (forwarded
    // byte-exact: a rule-(b) missile, unrecorded), then, after the callee returned, the blast kills two fighters through
    // the other window in the same game millisecond: two records, both nested, both skipped, the stand-in entered once
    // (by the missile's forward), each window's frame kept.
    {
        hs.owned_next = 1; // the stage owns the frame and holds its death programs (Present's last latch cleared them)
        hs.boom_ready = 1;
        death_object(4, core::layout::class_missile, 2, core::layout::obj_missile_area, 3 * 500, 520000);
        death_object(3, core::layout::class_ship, 4, 0, 14 * 500, 150000);
        death_object(5, core::layout::class_ship, 4, 0, 12 * 500, 160000);
        w32(config_block, core::layout::cfg_game_ms, 62000);
        std::memset(we_death_ca, 0, sizeof we_death_ca);
        const std::uint32_t recs_a = hs.counters.death_records, nested_a = hs.counters.death_nested;
        const DWORD e0 = run_death(0, &death_obj[4]); // the outer kill's own call (area: the missile's flag)
        const bool outer = death_forward_exact(0, &death_obj[4], 1) && death_frame_kept(e0, &death_obj[4]);
        const DWORD e1 = run_death(1, &death_obj[3]); // after the return: the blast's kills
        const bool k1 = death_frame_kept(e1, &death_obj[3]);
        const DWORD e2 = run_death(1, &death_obj[5]);
        const bool k2 = death_frame_kept(e2, &death_obj[5]);
        core::Event a{}, b{};
        const bool recs = death_record(&death_obj[3], &a) && death_record(&death_obj[5], &b) &&
                          !death_record(&death_obj[4], &rec) && hs.counters.death_records == recs_a + 2;
        // the first blast kill follows the corvette's earlier records at another ms: nested only from the second
        check(outer && k1 && k2 && recs && we_death_ca[12] == 1 && (a.flags & core::flag_stage) &&
                  (b.flags & core::flag_stage) && (b.flags & core::flag_nested) &&
                  hs.counters.death_nested == nested_a + 1u + ((a.flags & core::flag_nested) ? 1u : 0u),
              "death_nested_after_return");
        std::printf("DEATH_NESTED_AFTER records=%lu nested=%lu outer_entries=%lu\n",
                    static_cast<unsigned long>(hs.counters.death_records - recs_a),
                    static_cast<unsigned long>(hs.counters.death_nested - nested_a), static_cast<unsigned long>(we_death_ca[12]));
    }
    // death_forwards_without_boom_programs: the latch clear (the pass refused the death programs) forwards a fighter.
    hs.owned_next = 1;
    hs.boom_ready = 0;
    death_object(0, core::layout::class_ship, 4, 0, 14 * 500, 140000);
    std::memset(we_death_ca, 0, sizeof we_death_ca);
    err = run_death(1, &death_obj[0]);
    check(death_forward_exact(1, &death_obj[0], 1) && death_frame_kept(err, &death_obj[0]) &&
              death_record(&death_obj[0], &rec) && (rec.flags & core::flag_forwarded),
          "death_forwards_without_boom_programs");

    // death_shutdown_restores_both: both windows (and the impact) back to the engine's bytes.
    we::shutdown();
    check(!we::installed() && all_windows_original() && death_windows_original(), "death_shutdown_restores_both");
    // death_mode_without_token_leaves_sites: shields,hull does not touch the death sites.
    const bool hull_only = we::install_at(death_addresses(), core::mode_shields_hull);
    check(hull_only && death_windows_original() && !std::strcmp(we::site_state(6), "not_used") &&
              !std::strcmp(we::site_state(7), "not_used"),
          "death_mode_without_token_leaves_sites");
    we::shutdown();

    // death_byte_mismatch_refused: a changed byte in either window refuses before any write (LastError kept).
    bool refused_both = true;
    for (unsigned site = 0; site < 2; ++site) {
        const std::uintptr_t va = (site ? death_sites::other_window_va : death_sites::area_window_va) + 3;
        flip(va);
        SetLastError(sentinel_error);
        const bool r = !we::install_at(death_addresses(), core::mode_shields_hull_explosions) &&
                       !std::strcmp(we::state(), site ? "bytes_mismatch_death_other" : "bytes_mismatch_death_area") &&
                       GetLastError() == sentinel_error;
        flip(va);
        refused_both = refused_both && r && !we::installed() && all_windows_original() && death_windows_original() &&
                       !std::strcmp(we::site_state(6 + site), "bytes_mismatch");
    }
    check(refused_both, "death_byte_mismatch_refused", we::state());
    // death_rollback_restores_both: a faulted read-back at the area site (the last plain copy before the impact) restores
    // the other death site claimed before it; one at the impact restores both death sites.
    fault_read_at = death_site_at(0);
    SetLastError(sentinel_error);
    bool rolled = !we::install_at(death_addresses(), core::mode_shields_hull_explosions);
    const bool area_rolled = rolled && fault_read_at == 0 && !we::installed() && all_windows_original() &&
                             death_windows_original() && GetLastError() == sentinel_error &&
                             !std::strcmp(we::site_state(6), "readback_rolled_back") &&
                             !std::strcmp(we::site_state(7), "rolled_back") &&
                             !std::strcmp(we::site_state(0), "not_attempted");
    fault_read_at = impact_site();
    rolled = !we::install_at(death_addresses(), core::mode_shields_hull_explosions);
    const bool impact_rolled = rolled && fault_read_at == 0 && !we::installed() && all_windows_original() &&
                               death_windows_original() && !std::strcmp(we::site_state(0), "readback_rolled_back") &&
                               !std::strcmp(we::site_state(6), "rolled_back") &&
                               !std::strcmp(we::site_state(7), "rolled_back");
    fault_read_at = 0;
    check(area_rolled && impact_rolled, "death_rollback_restores_both", we::site_state(6));
    std::printf("DEATH_STATE deaths=%lu records=%lu ours=%lu no_node=%lu bullet=%lu rule_b=%lu nested=%lu\n",
                static_cast<unsigned long>(hs.counters.deaths), static_cast<unsigned long>(hs.counters.death_records),
                static_cast<unsigned long>(hs.counters.death_ours), static_cast<unsigned long>(hs.counters.death_no_node),
                static_cast<unsigned long>(hs.counters.death_bullet), static_cast<unsigned long>(hs.counters.death_rule_b),
                static_cast<unsigned long>(hs.counters.death_nested));
}
