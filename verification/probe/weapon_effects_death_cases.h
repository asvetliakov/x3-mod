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
//
// The big tiers (explosion-look.md section 1, spawn note 10): the fixture's engine model holds an Effects row table
// (Globals::effects_rows: rows 40 / 37 / 201 with the census elements of 10.3) and a malloc stand-in at the region's
// 0x005112c4 with _malloc's first five bytes (Globals::malloc_va), so Present builds the clone rows R' as in the game;
// the stand-in callee snapshots the watched death columns at every entry (we_death_nested), so a case sees what the
// forwarded 0x00413610 read and what the stub restored after it.
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
// The engine model of the big tiers: the row table, the source rows' element arrays, the EXE malloc's pool; the death
// columns the callee snapshots at each entry.
alignas(16) unsigned char death_rows[core::layout::row_ids * core::layout::row_stride];
alignas(16) unsigned char death_source[3][16 * core::layout::element_stride];
alignas(16) unsigned char death_pool[64 * 1024];
unsigned death_pool_used = 0, death_mallocs = 0;
std::uint32_t death_malloc_size[16];
std::uintptr_t death_watch[3];
std::uint32_t death_seen[8][3];
unsigned death_seen_n = 0;
void* __cdecl death_malloc(std::size_t n) {
    if (death_pool_used + n > sizeof death_pool) return nullptr;
    void* p = death_pool + death_pool_used;
    death_pool_used += unsigned((n + 15) & ~std::size_t(15));
    death_malloc_size[death_mallocs++ & 15u] = std::uint32_t(n);
    return p;
}
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
// One element of a source row (spawn note 10.2): body, EEDF flags, start, timeout (+ the PROPAGATE child fields).
struct DeathEl {
    std::int32_t body;
    std::uint32_t flags, start, timeout;
};
void death_source_row(unsigned id, unsigned k, std::uint32_t row_flags, const DeathEl* els, unsigned n) {
    namespace L = core::layout;
    unsigned char* a = death_source[k];
    std::memset(a, 0, sizeof death_source[k]);
    for (unsigned i = 0; i < n; ++i) {
        unsigned char* e = a + i * L::element_stride;
        w32(e, L::el_body, std::uint32_t(els[i].body));
        w32(e, L::el_flags, els[i].flags);
        w32(e, L::el_start, els[i].start);
        w32(e, L::el_timeout, els[i].timeout);
        w32(e, 0x08, 0x10000u + i); // size a: the copies keep every other field
        if (els[i].flags & L::eedf_propagate) {
            w32(e, 0x44, 149);
            w32(e, 0x48, 30);
            w32(e, 0x4c, 30);
        }
    }
    unsigned char* row = death_rows + id * L::row_stride;
    w32(row, L::row_count, n);
    w32(row, L::row_flags, row_flags);
    w32(row, L::row_8, 30000);
    w32(row, L::row_c, 0x10000);
    w32(row, L::row_elements, u32(a));
}
std::uint32_t death_row_u32(unsigned id, unsigned off) {
    std::uint32_t v;
    std::memcpy(&v, death_rows + id * core::layout::row_stride + off, 4);
    return v;
}
std::uint32_t el_u32(std::uint32_t array, unsigned i, unsigned off) {
    std::uint32_t v;
    std::memcpy(&v, reinterpret_cast<const unsigned char*>(std::uintptr_t(array)) + i * core::layout::element_stride + off, 4);
    return v;
}
bool logged(const char* needle, std::size_t from = 0) {
    for (std::size_t i = from; i < log_lines.size(); ++i)
        if (log_lines[i].find(needle) != std::string::npos) return true;
    return false;
}
// A clone slot is no row the data references (the fixture's references: 995 a death column, 994 a PROPAGATE child).
bool row_skips(unsigned slot) {
    return slot && slot != 995 && slot != 994;
}
const core::CloneRow* death_clone(unsigned source) {
    const core::HookState& hs = we::hook_state();
    for (unsigned k = 0; k < hs.clone_count; ++k)
        if (hs.clones[k].source == source && hs.clones[k].ready) return &hs.clones[k];
    return nullptr;
}
} // namespace (the nested hook is called from the stand-in's asm)
extern "C" void __cdecl we_death_nested() {
    if (death_seen_n < 8) { // what the forwarded call read in the watched death columns
        for (unsigned k = 0; k < 3; ++k)
            death_seen[death_seen_n][k] = death_watch[k] ? *reinterpret_cast<const std::uint32_t*>(death_watch[k]) : 0u;
        ++death_seen_n;
    }
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
    // the engine model of the big tiers: an empty row table (no source row loaded yet) and the malloc stand-in at the
    // region's 0x005112c4: push ebp; mov ebp,[esp+8] (_malloc's first five bytes); push ebp; call death_malloc;
    // add esp,4; pop ebp; ret
    std::memset(death_rows, 0, sizeof death_rows);
    hs.globals.effects_rows = u32(death_rows);
    {
        unsigned char* m = reinterpret_cast<unsigned char*>(at(0x005112c4));
        const unsigned char code[] = {0x55, 0x8b, 0x6c, 0x24, 0x08, 0x55, 0xe8, 0, 0, 0, 0, 0x83, 0xc4, 0x04, 0x5d, 0xc3};
        std::memcpy(m, code, sizeof code);
        const std::uint32_t rel = u32f(reinterpret_cast<void (*)()>(&death_malloc)) - std::uint32_t(u32(m) + 11);
        std::memcpy(m + 7, &rel, 4);
        hs.globals.malloc_va = u32(m);
    }
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
    // the big tiers swap the death column to R' (tier_swap; none native since 2026-11-07)
    check(core::boom::decide(core::boom::fighter) == core::boom::decide_ours &&
              core::boom::decide(core::boom::missile) == core::boom::decide_ours &&
              core::boom::decide(core::boom::corvette) == core::boom::decide_swap &&
              core::boom::decide(core::boom::capital) == core::boom::decide_swap &&
              core::boom::decide(core::boom::station) == core::boom::decide_swap && core::boom::tier_native == 0 &&
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
    // death_forwards_big_tier_census: an M6 (corvette) and a factory (station) forward byte-exact while owned when their
    // clone row R' is not ready (none built: no source row loaded), each recorded census-only with its tier, row and
    // cause (the tier stays native for that death).
    std::memset(we_death_ca, 0, sizeof we_death_ca);
    err = run_death(1, &death_obj[2]);
    core::Event rc{}, rs{};
    const bool corvette = death_forward_exact(1, &death_obj[2], 1) && death_frame_kept(err, &death_obj[2]) &&
                          death_record(&death_obj[2], &rc) && (rc.flags & core::flag_forwarded) &&
                          rc.style == core::boom::corvette && rc.row == 40 && rc.tier == core::boom::cause_clone_not_ready &&
                          !(rc.flags & core::flag_stage) && rc.lifetime_ms == 0;
    std::memset(we_death_ca, 0, sizeof we_death_ca);
    err = run_death(1, &death_obj[5]);
    const bool station = death_forward_exact(1, &death_obj[5], 1) && death_frame_kept(err, &death_obj[5]) &&
                         death_record(&death_obj[5], &rs) && (rs.flags & core::flag_forwarded) &&
                         rs.style == core::boom::station && rs.row == 201 && rs.table == 6 &&
                         rs.tier == core::boom::cause_clone_not_ready;
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

    // ---- The big tiers' clone row R' (explosion-look.md section 1; spawn note 10.5 (b)). The source rows as the census
    // has them (10.3): 40 (the corvette's: four elements, WRECK on one), 37 and 201 (the capital's and the station's:
    // PROPAGATE first, FADEOUTBODY at start 0, WRECK, the 22 s debris scene; EDF_PROPAGATE on the row).
    {
        namespace L = core::layout;
        namespace bm = core::boom;
        const DeathEl row40[] = {{10670, L::eedf_timeout, 10, 8000},
                                 {10670, L::eedf_timeout | 0x400u, 30, 9000},
                                 {11998, L::eedf_timeout | L::eedf_wreck | 0x10000000u, 0, 3110},
                                 {4264, L::eedf_timeout | 0x20u, 0, 1900}};
        const DeathEl row37[] = {{-1, L::eedf_propagate, 0, 0},
                                 {4262, L::eedf_timeout | 0x20u, 0, 3840},
                                 {4262, L::eedf_timeout | 0x20u | L::eedf_wreck, 25, 3840},
                                 {11999, L::eedf_timeout | 0x10000000u, 700, 2080},
                                 {11994, L::eedf_timeout, 780, 3600},
                                 {10664, L::eedf_timeout | L::eedf_fadeoutbody, 0, 7000},
                                 {10670, L::eedf_timeout, 1400, 9900},
                                 {-1, L::eedf_timeout, 0, 0},
                                 {4264, L::eedf_timeout | 0x20u, 1800, 1900},
                                 {4264, L::eedf_timeout | 0x20u, 1858, 1900},
                                 {7000, L::eedf_timeout | 0x20u | 0x400000u | 0x800000u, 120, 22000}};
        death_source_row(40, 0, 0, row40, 4);
        death_source_row(37, 1, L::edf_propagate, row37, 11);
        death_source_row(201, 2, L::edf_propagate, row37, 11);
        hs.owned_next = 1;
        hs.boom_ready = 1;
        x3m::weapon_effects::StageFrame sf{};
        sf.hull_ready = true;
        sf.boom_ready = true;
        // death_clone_malloc_prologue_refused: the allocator's first bytes changed: no clone built, one refusal row; the
        // bytes back: every pair built at the next scan.
        unsigned char* mbytes = reinterpret_cast<unsigned char*>(std::uintptr_t(hs.globals.malloc_va));
        mbytes[1] ^= 0xff;
        const std::size_t log0 = log_lines.size();
        x3m::weapon_effects::present(600, false, true, sf);
        const bool refused = !death_clone(40) && !death_clone(37) && !death_clone(201) && death_mallocs == 0 &&
                             logged("weapon_boom_rows event=refused", log0) && logged("reason=malloc_prologue", log0);
        mbytes[1] ^= 0xff;
        check(refused, "death_clone_malloc_prologue_refused");
        // death_clone_row_built: Present builds R' for the three (row, pre-roll) pairs of the type records we own (TShips
        // 5 M6 row 40 at P 0, 6 M2 row 37 at P 1500, TFactories 1 row 201 at P 3000; the station records' row 0 is not
        // loaded: source_missing), each in the highest free slot with an array from the EXE malloc: every source
        // element but PROPAGATE copied DISTANT (FADEOUTBODY and WRECK cleared, start clamped to P), one empty FADEOUTBODY
        // node from P for 1000 ms and one empty TIMEOUT node 0 .. P + 1000; the row's EDF_PROPAGATE cleared, +8 / +0xc
        // copied, the source rows untouched.
        const std::size_t log1 = log_lines.size();
        x3m::weapon_effects::present(900, false, true, sf);
        const core::CloneRow* c40 = death_clone(40);
        const core::CloneRow* c37 = death_clone(37);
        const core::CloneRow* c201 = death_clone(201);
        // the scan's order: TShips 5 (row 40) and 6 (row 37), then the stations (row 201): slots 999, 998, 997
        bool built = c40 && c37 && c201 && death_mallocs == 3 && c40->slot == bm::clone_slot_hi &&
                     c37->slot == bm::clone_slot_hi - 1 && c201->slot == bm::clone_slot_hi - 2 &&
                     c40->preroll_ms == 0 && c37->preroll_ms == 1500 && c201->preroll_ms == 3000 &&
                     logged("weapon_boom_rows event=built", log1) && logged("source=37 slot=998", log1) &&
                     logged("kept=2 masked=10 dropped=1", log1) && hs.clone_count == 3; // no pair for an unloaded row
        unsigned distant = 0, propagate = 0, fade_start = 0, hold_end = 0, starts_over = 0;
        if (built) {
            const unsigned slot = c37->slot;
            built = built && death_row_u32(slot, L::row_count) == 12 && death_row_u32(slot, L::row_flags) == 0 &&
                    death_row_u32(slot, L::row_8) == 30000 && death_row_u32(slot, L::row_c) == 0x10000 &&
                    death_row_u32(slot, L::row_elements) == c37->elements && c37->count == 12 &&
                    death_malloc_size[1] == 12 * L::element_stride && death_row_u32(37, L::row_count) == 11 &&
                    death_row_u32(37, L::row_flags) == L::edf_propagate;
            for (unsigned i = 0; i < 12; ++i) {
                const std::uint32_t f = el_u32(c37->elements, i, L::el_flags), st = el_u32(c37->elements, i, L::el_start);
                distant += (f & L::eedf_distant) != 0;
                propagate += (f & L::eedf_propagate) != 0;
                starts_over += st > 1500;
                if (i < 10) built = built && (f & L::eedf_distant) && !(f & (L::eedf_fadeoutbody | L::eedf_wreck)) &&
                                    el_u32(c37->elements, i, 0x08) == 0x10000u + i + 1u;
            }
            fade_start = el_u32(c37->elements, 10, L::el_start);
            hold_end = el_u32(c37->elements, 11, L::el_start) + el_u32(c37->elements, 11, L::el_timeout);
            built = built && distant == 10 && propagate == 0 && starts_over == 0 &&
                    el_u32(c37->elements, 10, L::el_flags) == (L::eedf_fadeoutbody | L::eedf_timeout) &&
                    el_u32(c37->elements, 10, L::el_body) == 0 && fade_start == 1500 &&
                    el_u32(c37->elements, 10, L::el_timeout) == 1000 &&
                    el_u32(c37->elements, 11, L::el_flags) == L::eedf_timeout && el_u32(c37->elements, 11, L::el_body) == 0 &&
                    hold_end == 2500;
            // the corvette's clone: P 0 (the hull fades over the body's first second), every start clamped to 0
            built = built && c40->count == 6 && el_u32(c40->elements, 0, L::el_start) == 0 &&
                    el_u32(c40->elements, 1, L::el_start) == 0 && el_u32(c40->elements, 4, L::el_start) == 0 &&
                    el_u32(c40->elements, 5, L::el_timeout) == 1000 &&
                    !(el_u32(c40->elements, 2, L::el_flags) & L::eedf_wreck) && c201->count == 12 &&
                    el_u32(c201->elements, 10, L::el_start) == 3000;
            std::printf("DEATH_CLONE source=37 slot=%u count=%lu masked=10 kept=2 dropped=1 row_flags=%lu distant=%u "
                        "propagate=%u fade_start=%u hold_end=%u mallocs=%u corvette_slot=%u corvette_count=%lu "
                        "station_slot=%u station_fade_start=%lu\n",
                        slot, static_cast<unsigned long>(c37->count),
                        static_cast<unsigned long>(death_row_u32(slot, L::row_flags)), distant, propagate, fade_start,
                        hold_end, death_mallocs, unsigned(c40->slot), static_cast<unsigned long>(c40->count),
                        unsigned(c201->slot), static_cast<unsigned long>(el_u32(c201->elements, 10, L::el_start)));
        }
        check(built, "death_clone_row_built");
        // The objects: 0 the M6 (TShips 5, row 40), 1 the M2 (TShips 6, row 37), 2 the factory (TFactories 1, row 201),
        // 3 a second M2, 4 a second M6.
        death_object(0, core::layout::class_ship, 5, 0, 87 * 500, 700000);
        death_object(1, core::layout::class_ship, 6, 0, 1288 * 500, 710000);
        death_object(2, core::layout::class_factory, 1, 0, 1800 * 500, 720000);
        death_object(3, core::layout::class_ship, 6, 0, 1288 * 500, 730000);
        death_object(4, core::layout::class_ship, 5, 0, 87 * 500, 740000);
        const std::uintptr_t col_m6 = u32(ship_table.data() + 5 * core::layout::type_stride + core::layout::ship_death_row);
        const std::uintptr_t col_m2 = u32(ship_table.data() + 6 * core::layout::type_stride + core::layout::ship_death_row);
        const std::uintptr_t col_fac =
            u32(death_station_table.data() + 1 * core::layout::type_stride + core::layout::station_death_row);
        const auto col = [](std::uintptr_t a) { return *reinterpret_cast<const std::uint32_t*>(a); };
        death_watch[0] = col_m6;
        death_watch[1] = col_m2;
        death_watch[2] = col_fac;
        // death_swaps_death_column_big_tiers: the corvette, the capital and the station, each through the other window:
        // the stand-in entered once with its type record's death column reading R' (the other two untouched), the
        // original back after the return; the record the stage's with the native spawn forwarded (flag_swapped), its
        // life; the caller's frame kept (registers, flags, MXCSR, the x87 control word and stack, LastError, ESP).
        bool swaps = built;
        const std::uint32_t restores0 = hs.counters.swap_restores, swapped0 = hs.counters.death_swapped;
        struct Big {
            unsigned obj, watch, tier;
            const core::CloneRow* clone;
            std::uint32_t original;
        } bigs[3] = {{0, 0, bm::corvette, c40, 40}, {1, 1, bm::capital, c37, 37}, {2, 2, bm::station, c201, 201}};
        std::uint32_t swap_ca[16] = {};
        DWORD swap_err = 0;
        for (const Big& b : bigs) {
            if (!b.clone) {
                swaps = false;
                continue;
            }
            std::memset(we_death_ca, 0, sizeof we_death_ca);
            death_seen_n = 0;
            const DWORD err2 = run_death(1, &death_obj[b.obj]);
            core::Event r{};
            const bool rec = death_record(&death_obj[b.obj], &r);
            const bool seen = death_seen_n == 1 && death_seen[0][b.watch] == b.clone->slot;
            bool others = true;
            for (unsigned k = 0; k < 3; ++k)
                if (k != b.watch) others = others && death_seen[0][k] == bigs[k].original;
            swaps = swaps && we_death_ca[12] == 1 && seen && others && col(death_watch[b.watch]) == b.original &&
                    death_frame_kept(err2, &death_obj[b.obj]) && rec && (r.flags & core::flag_stage) &&
                    (r.flags & core::flag_forwarded) && (r.flags & core::flag_swapped) && r.style == b.tier &&
                    r.tier == bm::cause_swap && r.row == b.original && r.lifetime_ms == bm::record_life_ms[b.tier] &&
                    hs.swap_depth == 0;
            if (b.tier == bm::capital) {
                std::memcpy(swap_ca, we_death_ca, sizeof swap_ca);
                swap_err = err2;
            }
        }
        check(swaps && hs.counters.swap_restores == restores0 + 3 && hs.counters.death_swapped == swapped0 + 3,
              "death_swaps_death_column_big_tiers");
        // death_swap_callee_state: the swap path's forwarded call (the capital's): the callee's EAX / ECX (arg4) / EDX (the
        // killer) as the window left them, EBX / ESI / EDI / EBP, the flags, the three arguments, its return inside the
        // stub and ESP 16 bytes below the plain forward's (the stub's three copies and its call).
        const std::uint32_t stub_other = u32f(reinterpret_cast<void (*)()>(&x3m_weapon_effects_stub_death_other));
        const bool callee_state = swap_ca[12] == 1 && swap_ca[0] == 0xa1a1a1a1u && swap_ca[1] == death_arg4 &&
                                  swap_ca[2] == death_killer && swap_ca[3] == 0x5b5b5b5bu && swap_ca[4] == u32(death_frame) &&
                                  swap_ca[5] == 0xe51e51e5u && swap_ca[6] == u32(&death_obj[1]) &&
                                  (swap_ca[7] & flag_mask) == (death_entry_flags & flag_mask) && swap_ca[8] > stub_other &&
                                  swap_ca[8] < stub_other + 0x80 && swap_ca[9] == u32(&death_obj[1]) &&
                                  swap_ca[10] == death_killer && swap_ca[11] == death_arg4 &&
                                  swap_ca[13] == we_death_saved_esp[0] - 32u && swap_err == sentinel_error;
        check(callee_state, "death_swap_callee_state");
        std::printf("DEATH_SWAP_STATE esp_frame=%08lx esp_callee=%08lx return=%08lx stub=%08lx eflags_entry=%08lx "
                    "eflags_callee=%08lx ecx=%08lx edx=%08lx\n",
                    static_cast<unsigned long>(we_death_saved_esp[0]), static_cast<unsigned long>(swap_ca[13]),
                    static_cast<unsigned long>(swap_ca[8]), static_cast<unsigned long>(stub_other),
                    static_cast<unsigned long>(death_entry_flags), static_cast<unsigned long>(swap_ca[7]),
                    static_cast<unsigned long>(swap_ca[1]), static_cast<unsigned long>(swap_ca[2]));
        // death_nested_lifo_restore: a capital whose forwarded call kills a corvette (another type: its own record) and a
        // second capital (the same type: the column already reads R', written again and restored to R') through the other
        // window while it is entered: three entries, each seeing its own R' (and the outer's), every column original
        // after the outer returns, the stack empty, three restores; the outer's frame kept.
        death_nest_objects[0] = u32(&death_obj[4]);
        death_nest_objects[1] = u32(&death_obj[3]);
        death_nest_count = 2;
        death_seen_n = 0;
        std::memset(we_death_ca, 0, sizeof we_death_ca);
        const std::uint32_t restores1 = hs.counters.swap_restores;
        const DWORD e_out = run_death(1, &death_obj[1]);
        core::Event n0{}, n1{}, n2{};
        const bool lifo = we_death_ca[12] == 3 && death_seen_n == 3 && death_seen[0][1] == c37->slot &&
                          death_seen[0][0] == 40 && death_seen[1][0] == c40->slot && death_seen[1][1] == c37->slot &&
                          death_seen[2][1] == c37->slot && death_seen[2][0] == 40 && col(col_m2) == 37 && col(col_m6) == 40 &&
                          col(col_fac) == 201 && hs.swap_depth == 0 && hs.counters.swap_restores == restores1 + 3 &&
                          death_frame_kept(e_out, &death_obj[1]) && death_record(&death_obj[1], &n0) &&
                          death_record(&death_obj[4], &n1) && death_record(&death_obj[3], &n2) &&
                          (n1.flags & core::flag_swapped) && (n2.flags & core::flag_swapped) && (n2.flags & core::flag_nested);
        check(lifo, "death_nested_lifo_restore");
        std::printf("DEATH_NESTED_SWAP entries=%lu seen=%u outer_m2=%lu inner_m6=%lu inner_m2=%lu after_m2=%lu after_m6=%lu "
                    "depth=%lu restores=%lu\n",
                    static_cast<unsigned long>(we_death_ca[12]), death_seen_n, static_cast<unsigned long>(death_seen[0][1]),
                    static_cast<unsigned long>(death_seen[1][0]), static_cast<unsigned long>(death_seen[2][1]),
                    static_cast<unsigned long>(col(col_m2)), static_cast<unsigned long>(col(col_m6)),
                    static_cast<unsigned long>(hs.swap_depth),
                    static_cast<unsigned long>(hs.counters.swap_restores - restores1));
        // death_nested_after_return_swap: the engine's order (spawn note 6): a capital's call returns, then its blast
        // kills a corvette and a capital through the other window in the same game millisecond: each swaps and restores
        // on its own (one entry each), the columns original after each.
        {
            bool after = true;
            for (unsigned o : {1u, 4u, 3u}) {
                std::memset(we_death_ca, 0, sizeof we_death_ca);
                death_seen_n = 0;
                const DWORD e1 = run_death(1, &death_obj[o]);
                const unsigned w = o == 4 ? 0u : 1u;
                after = after && we_death_ca[12] == 1 && death_seen[0][w] == (w ? c37->slot : c40->slot) &&
                        col(death_watch[w]) == (w ? 37u : 40u) && death_frame_kept(e1, &death_obj[o]) && hs.swap_depth == 0;
            }
            check(after, "death_nested_after_return_swap");
        }
        // death_clone_not_ready_forwards_exact: a reload zeroed the capital's clone slot (count and array 0): the capital
        // forwards byte-exact with its column untouched, census (cause clone_not_ready: the native look for that death);
        // a full swap stack likewise forwards (cause swap_full).
        const unsigned slot37 = c37->slot;
        std::memset(death_rows + slot37 * L::row_stride, 0, L::row_stride);
        std::memset(we_death_ca, 0, sizeof we_death_ca);
        death_seen_n = 0;
        DWORD e2 = run_death(1, &death_obj[1]);
        core::Event nr{};
        const bool not_ready = death_forward_exact(1, &death_obj[1], 1) && death_frame_kept(e2, &death_obj[1]) &&
                               death_seen[0][1] == 37 && col(col_m2) == 37 && death_record(&death_obj[1], &nr) &&
                               (nr.flags & core::flag_forwarded) && !(nr.flags & core::flag_stage) &&
                               nr.tier == bm::cause_clone_not_ready && nr.lifetime_ms == 0;
        hs.swap_depth = bm::swap_depth_max; // the stack full (a deep nest): the corvette forwards
        std::memset(we_death_ca, 0, sizeof we_death_ca);
        e2 = run_death(1, &death_obj[0]);
        const bool full = death_forward_exact(1, &death_obj[0], 1) && death_frame_kept(e2, &death_obj[0]) &&
                          col(col_m6) == 40 && death_record(&death_obj[0], &nr) && nr.tier == bm::cause_swap_full &&
                          hs.swap_depth == bm::swap_depth_max;
        hs.swap_depth = 0;
        check(not_ready && full, "death_clone_not_ready_forwards_exact");
        // death_clone_rebuilt_after_reload: the next Present finds the slot zeroed (invalidated reason=zeroed), rebuilds R'
        // in it from a fresh allocation (the reset freed the old array: never reused) and the capital swaps again.
        const std::size_t log2 = log_lines.size();
        const unsigned mallocs0 = death_mallocs;
        const std::uint32_t builds0 = hs.counters.clone_builds;
        x3m::weapon_effects::present(901, false, true, sf);
        const core::CloneRow* r37 = death_clone(37);
        std::memset(we_death_ca, 0, sizeof we_death_ca);
        death_seen_n = 0;
        e2 = run_death(1, &death_obj[1]);
        const bool rebuilt = r37 && r37->slot == slot37 && death_mallocs == mallocs0 + 1 &&
                             hs.counters.clone_builds == builds0 + 1 && death_row_u32(slot37, L::row_count) == 12 &&
                             logged("reason=zeroed", log2) && logged("weapon_boom_rows event=rebuilt", log2) &&
                             we_death_ca[12] == 1 && death_seen[0][1] == slot37 && col(col_m2) == 37 &&
                             death_frame_kept(e2, &death_obj[1]);
        check(rebuilt, "death_clone_rebuilt_after_reload");
        // death_clone_slot_taken_refused: data loaded into the corvette's clone slot (another count and array): the
        // corvette forwards native, the next Present gives the slot up (reason=taken; the data untouched) and builds R'
        // in another free slot; the corvette swaps to it.
        const unsigned slot40 = c40->slot;
        alignas(16) static unsigned char foreign[3 * L::element_stride];
        w32(death_rows + slot40 * L::row_stride, L::row_count, 3);
        w32(death_rows + slot40 * L::row_stride, L::row_elements, u32(foreign));
        std::memset(we_death_ca, 0, sizeof we_death_ca);
        e2 = run_death(1, &death_obj[0]);
        const bool native = death_forward_exact(1, &death_obj[0], 1) && col(col_m6) == 40;
        const std::size_t log3 = log_lines.size();
        x3m::weapon_effects::present(902, false, true, sf);
        const core::CloneRow* n40 = death_clone(40);
        std::memset(we_death_ca, 0, sizeof we_death_ca);
        death_seen_n = 0;
        e2 = run_death(1, &death_obj[0]);
        const bool moved = native && n40 && n40->slot != slot40 && logged("reason=taken", log3) &&
                           death_row_u32(slot40, L::row_count) == 3 && death_row_u32(slot40, L::row_elements) == u32(foreign) &&
                           death_seen[0][0] == n40->slot && col(col_m6) == 40 && death_frame_kept(e2, &death_obj[0]);
        check(moved, "death_clone_slot_taken_refused");
        // death_swap_leak_restored_at_present: a swap whose call never came back through the stub (an unwind): the
        // handler's swap left on the stack and the column at R'; the next Present writes the saved row back and empties
        // the stack (swap_leaked, one leak_restored row).
        {
            std::uint32_t entry[4] = {0, u32(&death_obj[1]), death_killer, death_arg4};
            const std::uint32_t leaked0 = hs.counters.swap_leaked;
            const int d = x3m_weapon_death_other(entry, u32(death_frame));
            const bool at_clone = d == core::death_swap && hs.swap_depth == 1 && col(col_m2) == slot37;
            const std::size_t log4 = log_lines.size();
            x3m::weapon_effects::present(903, false, true, sf);
            check(at_clone && hs.swap_depth == 0 && col(col_m2) == 37 && hs.counters.swap_leaked == leaked0 + 1 &&
                      logged("event=leak_restored", log4),
                  "death_swap_leak_restored_at_present");
        }
        // ---- Review 2026-11-07 (A-C, L): pairs added only by a scan outside loading, dropped with their source; a changed
        // source abandons its slot (never refilled in place) and builds in another; free slots skip referenced rows.
        // References: TDocks/TFactories record 3's death column 995 (row 995 not loaded), row 201's PROPAGATE child 994.
        w32(death_station_table.data() + 3 * core::layout::type_stride, core::layout::station_death_row, 995);
        w32(death_station_table.data() + 2 * core::layout::type_stride, core::layout::station_death_row, 650);
        death_source_row(650, 2, L::edf_propagate, row37, 11); // row 650 loaded (the station array's copy)
        w32(death_source[2], 0x44, 994);                       // row 201 / 650's PROPAGATE element now names row 994
        {
            x3m::weapon_effects::StageFrame sfl = sf;
            core::Scope loading{};
            loading.loading = true;
            const unsigned pairs0 = hs.clone_count, mallocs1 = death_mallocs;
            x3m::weapon_effects::present(1300, false, true, sfl, loading); // a scan due, but the scope loads
            const bool none = hs.clone_count == pairs0 && death_mallocs == mallocs1 && !death_clone(650);
            x3m::weapon_effects::present(1301, false, true, sfl); // the scan after the load
            const core::CloneRow* c650 = death_clone(650);
            check(none && c650 && c650->preroll_ms == 3000 && c650->slot != 995 && c650->slot != 994 &&
                      hs.clone_count == pairs0 + 1,
                  "death_clone_scan_refused_while_loading");
            std::printf("DEATH_SCAN loading_pairs=%u after_pairs=%u slot650=%u\n", pairs0, unsigned(hs.clone_count),
                        c650 ? unsigned(c650->slot) : 0u);
            // death_clone_source_vanished_drops: row 650 unloaded: its pair drops, its slot left as it is (abandoned);
            // a station of that record forwards native
            const unsigned slot650 = c650 ? c650->slot : 0u;
            const std::uint32_t arr650 = c650 ? c650->elements : 0u;
            const unsigned ab0 = we::clone_abandoned_slots();
            std::memset(death_rows + 650 * L::row_stride, 0, L::row_stride);
            const std::size_t logv = log_lines.size();
            x3m::weapon_effects::present(1302, false, true, sfl);
            death_object(2, core::layout::class_factory, 2, 0, 1800 * 500, 750000);
            std::memset(we_death_ca, 0, sizeof we_death_ca);
            const DWORD ev = run_death(1, &death_obj[2]);
            core::Event rv{};
            const bool vanished = slot650 && !death_clone(650) && hs.clone_count == pairs0 &&
                                  death_row_u32(slot650, L::row_count) == 12 && death_row_u32(slot650, L::row_elements) == arr650 &&
                                  we::clone_abandoned_slots() == ab0 + 1 && logged("reason=source_vanished", logv) &&
                                  logged("weapon_boom_rows event=abandoned", logv) && death_forward_exact(1, &death_obj[2], 1) &&
                                  death_frame_kept(ev, &death_obj[2]) && death_record(&death_obj[2], &rv) &&
                                  rv.tier == bm::cause_clone_not_ready;
            check(vanished, "death_clone_source_vanished_drops");
            // death_clone_source_changed_new_slot: row 37's array moved (a copy): its pair drops, the old slot is left
            // holding the old clone (abandoned, never refilled), R' is built again in another free slot (not 995 / 994:
            // referenced), and the capital swaps to it
            const core::CloneRow* o37 = death_clone(37);
            const unsigned old37 = o37 ? o37->slot : 0u;
            const std::uint32_t oldarr = o37 ? o37->elements : 0u;
            alignas(16) static unsigned char moved[16 * L::element_stride];
            std::memcpy(moved, death_source[1], sizeof moved);
            w32(death_rows + 37 * L::row_stride, L::row_elements, u32(moved));
            const std::size_t logc = log_lines.size();
            x3m::weapon_effects::present(1303, false, true, sfl);
            const core::CloneRow* n37 = death_clone(37);
            std::memset(we_death_ca, 0, sizeof we_death_ca);
            death_seen_n = 0;
            const DWORD ec = run_death(1, &death_obj[1]);
            const bool changed = old37 && n37 && n37->slot != old37 && n37->slot != 995 && n37->slot != 994 &&
                                 death_row_u32(old37, L::row_count) == 12 && death_row_u32(old37, L::row_elements) == oldarr &&
                                 logged("reason=source_changed", logc) && death_seen[0][1] == n37->slot &&
                                 col(col_m2) == 37 && death_frame_kept(ec, &death_obj[1]);
            check(changed, "death_clone_source_changed_new_slot");
            check(row_skips(n37 ? n37->slot : 0u) && row_skips(slot650), "death_clone_free_slot_skips_referenced");
            std::printf("DEATH_SLOTS slot650=%u old37=%u new37=%u abandoned=%u referenced=995,994\n", slot650, old37,
                        n37 ? unsigned(n37->slot) : 0u, we::clone_abandoned_slots());
        }
        // death_clone_scan_time (L): the type-table scan over the installed tables' record counts (Mayhem 3: TShips 521,
        // TDocks 76, TFactories 910, TMissiles 36, TAsteroids 10; the fixture's engine memory), median of 15 scans
        {
            static std::vector<unsigned char> ships(521 * core::layout::type_stride), docks(76 * core::layout::type_stride),
                facts(910 * core::layout::type_stride), miss(36 * core::layout::type_stride), rocks(10 * core::layout::type_stride);
            const std::int32_t classes[6] = {2023, 2024, 2026, 2030, 2022, 2027}; // M3 M4 M6 TS M2 M7
            for (unsigned i = 0; i < 521; ++i) {
                w32(ships.data() + i * core::layout::type_stride, core::layout::ship_class_id, std::uint32_t(classes[i % 6]));
                w32(ships.data() + i * core::layout::type_stride, core::layout::ship_death_row, i % 6 < 2 ? 600 : i % 6 < 4 ? 601 : 602);
            }
            for (unsigned i = 0; i < 76; ++i) w32(docks.data() + i * core::layout::type_stride, core::layout::station_death_row, 602);
            for (unsigned i = 0; i < 910; ++i) w32(facts.data() + i * core::layout::type_stride, core::layout::station_death_row, 602);
            for (unsigned i = 0; i < 36; ++i) w32(miss.data() + i * core::layout::type_stride, core::layout::missile_death_row, 48);
            for (unsigned i = 0; i < 10; ++i) w32(rocks.data() + i * core::layout::type_stride, core::layout::asteroid_death_row, 59 + i);
            death_source_row(600, 0, 0, row40, 4);
            death_source_row(601, 0, 0, row40, 4);
            death_source_row(602, 1, L::edf_propagate, row37, 11);
            static std::uint32_t sp, sc, dp, dc, fp, fc, mp, mc, ap, ac;
            sp = u32(ships.data()); sc = 521; dp = u32(docks.data()); dc = 76; fp = u32(facts.data()); fc = 910;
            mp = u32(miss.data()); mc = 36; ap = u32(rocks.data()); ac = 10;
            const core::Globals saved = hs.globals;
            hs.globals.ship_table_slot = u32(&sp); hs.globals.ship_count_slot = u32(&sc);
            hs.globals.dock_table_slot = u32(&dp); hs.globals.dock_count_slot = u32(&dc);
            hs.globals.factory_table_slot = u32(&fp); hs.globals.factory_count_slot = u32(&fc);
            hs.globals.missile_table_slot = u32(&mp); hs.globals.missile_count_slot = u32(&mc);
            hs.globals.asteroid_table_slot = u32(&ap); hs.globals.asteroid_count_slot = u32(&ac);
            std::vector<double> us;
            for (unsigned r = 0; r < 15; ++r) {
                x3m::weapon_effects::present(2000 + 300ull * r, false, true, sf);
                us.push_back(double(we::clone_scan_us()));
            }
            std::sort(us.begin(), us.end());
            const double med = us[us.size() / 2];
            const unsigned pairs = hs.clone_count;
            hs.globals = saved;
            std::printf("DEATH_SCAN_TIMING records=1553 pairs=%u median_us=%.1f max_us=%.1f bound_us=250\n", pairs, med, us.back());
            check(med <= 250.0 && pairs == 3, "death_clone_scan_time");
        }
        std::printf("DEATH_SWAP swapped=%lu restores=%lu changed=%lu underflow=%lu leaked=%lu clone_missing=%lu swap_full=%lu "
                    "builds=%lu invalidated=%lu clones=%lu mallocs=%u\n",
                    static_cast<unsigned long>(hs.counters.death_swapped), static_cast<unsigned long>(hs.counters.swap_restores),
                    static_cast<unsigned long>(hs.counters.swap_changed), static_cast<unsigned long>(hs.counters.swap_underflow),
                    static_cast<unsigned long>(hs.counters.swap_leaked),
                    static_cast<unsigned long>(hs.counters.death_clone_missing),
                    static_cast<unsigned long>(hs.counters.death_swap_full), static_cast<unsigned long>(hs.counters.clone_builds),
                    static_cast<unsigned long>(hs.counters.clone_invalidated), static_cast<unsigned long>(hs.clone_count),
                    death_mallocs);
        death_watch[0] = death_watch[1] = death_watch[2] = 0;
    }

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
