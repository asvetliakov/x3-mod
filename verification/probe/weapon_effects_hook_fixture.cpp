// Native fixture of the weapon-effects shield-hit hook (src/proxy/weapon_effects_patch.cpp through a force-included
// fault seam, src/proxy/engine_patch.cpp and src/proxy/weapon_effects_hook.cpp unchanged). Never launches the game;
// built and run by verification/probe/run_weapon_effects.py / test_weapon_effects_hook, MinGW i686, natively through
// WSL interop (the FL redirect fixture's harness: the engine pages are reserved at the game's VAs at run time, not
// placed as fixed link-time sections, so the real Windows loader accepts the image).
//
// The fixture reserves the engine pages at their X3AP.exe VAs with VirtualAlloc and writes into them: the damage
// routine's prologue 0x00449a60 and its four frame witnesses, the impact window 0x00449d83..0x00449dab (push edi; push
// esi; call 0x00413c40; the add esp, the 0x00450310 / 0x00417250 calls and the epilogue ret 0x10), a `jmp` at
// 0x00450310 to a stand-in that returns 0 (so the je skips 0x00417250), the impact callee's stand-in at 0x00413c40
// (records registers, return and its two arguments), the two inner tap windows 0x00413da1 / 0x00413e72 and the instance
// callee's stand-in at 0x00414a70, the PROPAGATE stand-in at 0x00414410, and the engine config and TBullets pointers
// at 0x00606f34 / 0x00606fb8 / 0x00607038. So the window's absolute operands and every rel32 reach the stand-ins
// exactly as in X3AP.exe. we_enter builds the damage routine's frame (the two call arguments, the four saved
// registers, the 0xc locals with the total at [esp_window+0x10] and the absorbed flag at +0x18, the planted return
// address and the ret 0x10 arguments), loads a hostile MXCSR, x87 control word, sentinels in EAX/ECX/EDX/EBX/EBP and a
// chosen EFLAGS (arithmetic flags and DF), sets EDI = source and ESI = victim and jumps to the window; the window runs
// the hook and the epilogue and returns through the planted address to we_exit_body, which records the registers and
// the FPU state. The callee stand-in records every register, EFLAGS, ESP and its return address, so the forward path is
// checked against the state the call left (review F7).
#include "../../src/proxy/weapon_effects_patch.h"
#include "../../src/proxy/weapon_effects_sites.h"
#include "../../src/proxy/weapon_effects_bolt_sites.h"
#include "../../src/proxy/weapon_effects_death_sites.h"
#include "../../src/proxy/weapon_effects_table.h"
#include "../../src/proxy/engine_patch.h"
#include <windows.h>
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace we = x3m::weapon_effects;
namespace core = x3m::weapon_effects::core;
namespace sites = x3m::weapon_effects::sites;
namespace engine_patch = x3m::engine_patch;

namespace {
std::vector<std::string> log_lines;
bool executable_ok = true;
std::uintptr_t fault_read_at = 0, fault_restore_at = 0;
bool fault_protect = false;
unsigned faults_taken = 0;
unsigned checks = 0, failures = 0;
void check(bool ok, const char* name, const char* detail = "") {
    ++checks;
    if (!ok) ++failures;
    std::printf("CHECK %s %s%s%s\n", ok ? "pass" : "FAIL", name, !ok && *detail ? " " : "", ok ? "" : detail);
}
}
namespace x3m {
void log(const char* format, ...) {
    char text[512];
    std::va_list a;
    va_start(a, format);
    std::vsnprintf(text, sizeof text, format, a);
    va_end(a);
    log_lines.emplace_back(text);
    std::printf("LOG %s\n", text);
}
}
namespace x3m::object_trace {
bool executable_verified() {
    return executable_ok;
}
}
namespace x3m::engine_memory {
bool read(std::uintptr_t address, void* out, std::size_t size) {
    if (!address) return false;
    std::memcpy(out, reinterpret_cast<const void*>(address), size);
    return true;
}
}
namespace x3m::weapon_effects_fixture {
BOOL WINAPI virtual_protect(LPVOID address, SIZE_T size, DWORD protection, PDWORD previous) {
    if (fault_protect) {
        fault_protect = false;
        ++faults_taken;
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return VirtualProtect(address, size, protection, previous);
}
}
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count) {
    const bool ok = read_code(address, out, count);
    if (ok && fault_read_at && address == fault_read_at && count == sites::call_length) {
        fault_read_at = 0;
        ++faults_taken;
        out[1] ^= 0xff;
    }
    return ok;
}
bool fixture_restore_call(CallSite& site) {
    if (fault_restore_at && site.address == fault_restore_at) {
        fault_restore_at = 0;
        ++faults_taken;
        site.status = "restore_protect_failed";
        return false;
    }
    return restore_call(site);
}
}

// The enter/exit and stand-in bodies live in the fixture's own .text (any address). we_enter reads the window address
// from we_window_ptr (set at run time) and jumps to it; the window returns through the planted we_exit_body.
extern "C" {
std::uint32_t we_in[12];
std::uint32_t we_out[13];
std::uint32_t we_ca[16];
std::uint32_t we_tap_ca[12];
std::uint32_t we_saved_esp, we_saved_mxcsr, we_saved_fcw, we_frame_esp, we_window_ptr, we_tap_window_ptr;
unsigned char we_frame_buffer[0x800]; // the synthetic frame at +0x600; below it the stub's and handler's stack
void __cdecl we_enter();
void we_impact_callee_body();
void we_follow_body();
void we_tap_callee_body();
void we_prop_body();
void we_exit_body();
void we_tap_back();
void __cdecl we_tap_enter(const std::uint32_t* args, std::uint32_t ebx);
}
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _we_enter
_we_enter:
    push ebp
    push ebx
    push esi
    push edi
    mov dword ptr [_we_saved_esp], esp
    stmxcsr dword ptr [_we_saved_mxcsr]
    fnstcw word ptr [_we_saved_fcw]
    lea edi, [_we_frame_buffer]
    add edi, 0x600
    mov dword ptr [_we_frame_esp], edi
    mov eax, dword ptr [_we_in+8]
    mov dword ptr [edi+0x10], eax                      # total at F+0x10
    mov eax, dword ptr [_we_in+12]
    mov dword ptr [edi+0x18], eax                      # absorbed at F+0x18
    mov dword ptr [edi+0x00], 0xed1ed1ed               # saved edi
    mov dword ptr [edi+0x04], 0xe51e51e5               # saved esi
    mov dword ptr [edi+0x08], 0xeb0eb0eb               # saved ebp
    mov dword ptr [edi+0x0c], 0xeb1eb1eb               # saved ebx
    lea eax, [_we_exit_body]
    mov dword ptr [edi+0x1c], eax                      # the planted return address
    ldmxcsr dword ptr [_we_in+16]
    fldcw word ptr [_we_in+20]
    fld qword ptr [_we_x87_a]
    fld qword ptr [_we_x87_b]
    mov ebx, 0x5b5b5b5b
    mov ebp, 0xeb9eb9eb
    mov edi, dword ptr [_we_in+4]                      # source
    mov esi, dword ptr [_we_in+0]                      # victim
    mov eax, 0xa1a1a1a1                                # the caller-dead registers' sentinels
    mov ecx, 0xc1c1c1c1
    mov edx, 0xd1d1d1d1
    push dword ptr [_we_in+28]                         # the entry EFLAGS (CF PF ZF SF OF, DF per case)
    popfd
    mov esp, dword ptr [_we_frame_esp]
    jmp dword ptr [_we_window_ptr]

    .globl _we_exit_body
_we_exit_body:
    mov dword ptr [_we_out+0], eax
    mov dword ptr [_we_out+4], ebx
    mov dword ptr [_we_out+8], ebp
    mov dword ptr [_we_out+12], esi
    mov dword ptr [_we_out+16], edi
    pushfd
    pop dword ptr [_we_out+20]
    cld
    stmxcsr dword ptr [_we_out+24]
    fnstcw word ptr [_we_out+28]
    fstp qword ptr [_we_out+32]
    fstp qword ptr [_we_out+40]
    mov eax, esp
    sub eax, dword ptr [_we_frame_esp]
    mov dword ptr [_we_out+48], eax
    ldmxcsr dword ptr [_we_saved_mxcsr]
    fldcw word ptr [_we_saved_fcw]
    mov esp, dword ptr [_we_saved_esp]
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

    .globl _we_tap_enter
_we_tap_enter:                                          # (args, ebx)
    push ebp
    push ebx
    push esi
    push edi
    mov dword ptr [_we_saved_esp], esp
    mov edx, dword ptr [esp+0x14]                       # args
    mov ebx, dword ptr [esp+0x18]                       # ebx ([ebx+8] = projectile id)
    mov edi, dword ptr [edx+8]                           # args[2] = hit, into EDI
    mov esi, dword ptr [edx+4]                           # args[1] = eff, into ESI
    push dword ptr [edx+0x14]                            # the four 0x00414410 leftovers
    push dword ptr [edx+0x14]
    push dword ptr [edx+0x14]
    push dword ptr [edx+0x14]
    push dword ptr [edx+0x10]                            # arg5 = 0
    push dword ptr [edx+0xc]                             # arg4 = &pos
    jmp dword ptr [_we_tap_window_ptr]
    .globl _we_tap_back
_we_tap_back:                                            # the tap window's add esp,0x24 lands here (planted return)
    mov esp, dword ptr [_we_saved_esp]
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

    .globl _we_impact_callee_body
_we_impact_callee_body:                                 # 0x00413c40 stand-in: cdecl 2, records
    mov dword ptr [_we_ca+0], eax
    mov dword ptr [_we_ca+4], ecx
    mov dword ptr [_we_ca+8], edx
    mov dword ptr [_we_ca+12], ebx
    mov dword ptr [_we_ca+16], ebp
    mov dword ptr [_we_ca+20], esi
    mov dword ptr [_we_ca+24], edi
    pushfd
    pop dword ptr [_we_ca+28]
    mov eax, dword ptr [esp]
    mov dword ptr [_we_ca+32], eax
    mov eax, dword ptr [esp+4]
    mov dword ptr [_we_ca+36], eax
    mov eax, dword ptr [esp+8]
    mov dword ptr [_we_ca+40], eax
    inc dword ptr [_we_ca+44]
    mov dword ptr [_we_ca+48], esp
    mov eax, 0x1
    ret

    .globl _we_follow_body
_we_follow_body:                                        # 0x00450310 stand-in: returns 0 so the je skips 0x00417250
    xor eax, eax
    ret

    .globl _we_tap_callee_body
_we_tap_callee_body:                                    # 0x00414a70 stand-in: cdecl 5, records, plants the return
    mov dword ptr [_we_tap_ca+0], eax
    mov dword ptr [_we_tap_ca+4], ecx
    mov dword ptr [_we_tap_ca+8], edx
    mov dword ptr [_we_tap_ca+12], ebx
    mov dword ptr [_we_tap_ca+16], ebp
    mov dword ptr [_we_tap_ca+20], esi
    mov dword ptr [_we_tap_ca+24], edi
    mov eax, dword ptr [esp]
    mov dword ptr [_we_tap_ca+28], eax
    mov eax, dword ptr [esp+0xc]                         # hit
    mov dword ptr [_we_tap_ca+32], eax
    mov eax, dword ptr [esp+0x10]                        # &pos
    mov dword ptr [_we_tap_ca+36], eax
    inc dword ptr [_we_tap_ca+40]
    lea eax, [_we_tap_back]
    mov dword ptr [esp], eax
    mov eax, 0x1
    ret

    .globl _we_prop_body
_we_prop_body:                                          # 0x00414410 stand-in
    xor eax, eax
    ret

    .section .rodata
    .balign 8
_we_x87_a:
    .double 1.25
_we_x87_b:
    .double -3.5e10
    .text
    .att_syntax
)");

namespace {
constexpr std::uintptr_t dmg_page = 0x00449000, eff_page = 0x00413000, data_page = 0x00606000;
constexpr DWORD sentinel_error = 0x1234abcd;
constexpr std::uint32_t hostile_mxcsr = 0x7f80, hostile_fcw = 0x0c7f;
constexpr std::uint32_t entry_flags = 0x8c5;   // CF PF ZF SF OF (DF 0x400 added per case)
constexpr std::uint32_t flag_mask = 0xcd5;     // CF PF AF ZF SF DF OF
constexpr std::uint32_t tap_projectile_id = 0x0b1d0b1du;
std::uint32_t u32(const void* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
std::uint32_t u32f(void (*p)()) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
struct alignas(16) Node {
    unsigned char bytes[0x200];
};
struct alignas(16) Obj {
    unsigned char bytes[0x100];
};
Node victim_node, source_node;
Obj victim_obj, source_obj;
unsigned char* region = nullptr;        // one relocatable block; engine VA v -> region + (v - eff_page)
unsigned char* bullet_table = nullptr;
unsigned char* config_block = nullptr;
std::uint32_t cfg_ptr_slot, bullet_ptr_slot, count_slot; // the fixture's own engine-config slots (heap)
std::uint32_t ship_ptr_slot, ship_count_slot, shield_ptr_slot, shield_count_slot; // TShips / TShields (5.1.1)
// The synthetic shield list of the victim (spawn note 9): extension block, chain nodes (next +0, TShields index +0x4a,
// shield extension +0x50), the shields' extensions (energy +4) and a TShields table (capacity +0x48).
constexpr unsigned shield_nodes_max = 70, shield_rows = 8;
alignas(16) unsigned char shield_ext_block[0x100];
alignas(16) unsigned char shield_node[shield_nodes_max][0x60];
alignas(16) unsigned char shield_energy_block[shield_nodes_max][0x10];
std::vector<unsigned char> shield_table(shield_rows * core::layout::type_stride);
std::vector<unsigned char> ship_table(8 * core::layout::type_stride);
std::uintptr_t at(std::uintptr_t va) { return reinterpret_cast<std::uintptr_t>(region) + (va - eff_page); }
std::uintptr_t impact_site() { return at(sites::impact_site_va); }
void w32(void* base, unsigned off, std::uint32_t v) {
    std::memcpy(static_cast<unsigned char*>(base) + off, &v, 4);
}
void w16(void* base, unsigned off, std::uint16_t v) {
    std::memcpy(static_cast<unsigned char*>(base) + off, &v, 2);
}
void write_jmp(std::uintptr_t where, void (*target)()) {
    auto* p = reinterpret_cast<unsigned char*>(where);
    p[0] = 0xe9;
    const std::uint32_t rel = u32f(target) - std::uint32_t(where + 5);
    std::memcpy(p + 1, &rel, 4);
}
bool build_pages() {
    // One relocatable region (VirtualAlloc picks a free base: the engine VAs 0x00413000.. are pre-reserved in a native
    // process, and the window's rel32 calls are position-independent, so the layout works at any base). Engine VA v
    // maps to region + (v - eff_page); the handler's config/table slots are three heap dwords (hook_state().globals).
    constexpr SIZE_T span = 0x00608000 - eff_page;
    region = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!region) return false;
    std::memset(region, 0xcc, span);
    std::memcpy(reinterpret_cast<void*>(at(sites::prologue_va)), sites::expected_prologue, sites::prologue_length);
    for (unsigned i = 0; i < sites::witness_count; ++i)
        std::memcpy(reinterpret_cast<void*>(at(sites::witnesses[i].va)), sites::witnesses[i].bytes, sites::witnesses[i].length);
    std::memcpy(reinterpret_cast<void*>(at(sites::impact_window_va)), sites::expected_impact_window, sites::impact_window_length);
    write_jmp(at(sites::impact_follow_a_va), we_follow_body); // 0x00450310
    std::memcpy(reinterpret_cast<void*>(at(sites::tap_window_va[0])), sites::expected_tap_window[0], sites::tap_window_length);
    std::memcpy(reinterpret_cast<void*>(at(sites::tap_window_va[1])), sites::expected_tap_window[1], sites::tap_window_length);
    write_jmp(at(sites::impact_target_va), we_impact_callee_body); // 0x00413c40
    write_jmp(at(sites::tap_target_va), we_tap_callee_body);       // 0x00414a70
    write_jmp(at(0x00414410), we_prop_body);
    // Step 4: the per-bullet tap, the muzzle and the flight-effect windows (weapon_effects_bolt_sites.h).
    namespace bs = x3m::weapon_effects::bolt_sites;
    std::memcpy(reinterpret_cast<void*>(at(bs::tap_window_va)), bs::expected_tap_window, bs::tap_window_length);
    std::memcpy(reinterpret_cast<void*>(at(bs::muzzle_window_va)), bs::expected_muzzle_window, bs::muzzle_window_length);
    std::memcpy(reinterpret_cast<void*>(at(bs::flight_window_va)), bs::expected_flight_window, bs::flight_window_length);
    FlushInstructionCache(GetCurrentProcess(), region, span);
    config_block = reinterpret_cast<unsigned char*>(at(data_page) + 0x1000);
    we_window_ptr = std::uint32_t(at(sites::impact_window_va));
    we_tap_window_ptr = 0;
    // The handler's engine-config slots point at the fixture's own dwords.
    core::HookState& hs = we::hook_state();
    hs.globals.cfg_slot = reinterpret_cast<std::uintptr_t>(&cfg_ptr_slot);
    hs.globals.bullet_table_slot = reinterpret_cast<std::uintptr_t>(&bullet_ptr_slot);
    hs.globals.bullet_count_slot = reinterpret_cast<std::uintptr_t>(&count_slot);
    hs.globals.ship_table_slot = reinterpret_cast<std::uintptr_t>(&ship_ptr_slot);
    hs.globals.ship_count_slot = reinterpret_cast<std::uintptr_t>(&ship_count_slot);
    hs.globals.shield_table_slot = reinterpret_cast<std::uintptr_t>(&shield_ptr_slot);
    hs.globals.shield_count_slot = reinterpret_cast<std::uintptr_t>(&shield_count_slot);
    return true;
}
void build_world(std::uint16_t victim_class, std::uint16_t source_class, std::uint16_t subtype, std::uint32_t flags,
                 std::uint32_t game_ms) {
    std::memset(&victim_node, 0, sizeof victim_node);
    std::memset(&source_node, 0, sizeof source_node);
    std::memset(&victim_obj, 0, sizeof victim_obj);
    std::memset(&source_obj, 0, sizeof source_obj);
    w16(&victim_obj, core::layout::obj_class, victim_class);
    w16(&victim_obj, core::layout::obj_subtype, subtype);
    w32(&victim_obj, core::layout::obj_node, u32(&victim_node));
    w32(&victim_obj, core::layout::obj_radius, 20000);
    w16(&source_obj, core::layout::obj_class, source_class);
    w16(&source_obj, core::layout::obj_subtype, subtype);
    w32(&source_obj, core::layout::obj_node, u32(&source_node));
    w32(&source_obj, core::layout::obj_radius, 50);
    // The bullet extension pointer is an unmapped address: the handler must not follow [source+0x50] (review F9); a read
    // would fault the fixture.
    w32(&source_obj, core::layout::obj_ext, 0x00000010u);
    for (Node* n : {&victim_node, &source_node}) {
        w32(n, core::layout::node_live, 1);
        w32(n, core::layout::node_handle, 0xd00d);
        for (unsigned k = 0; k < 3; ++k) w32(n, core::layout::node_rot + 16 * k + 4 * k, 0x10000);
    }
    w32(&source_node, core::layout::node_pos + 0, 7000);
    w32(&source_node, core::layout::node_pos + 4, 0xfffff448u); // -3000
    w32(&source_node, core::layout::node_pos + 8, 1500);
    w32(&source_node, core::layout::node_contact + 0, 0x8000);
    static std::vector<unsigned char> table(16 * core::layout::type_stride);
    std::fill(table.begin(), table.end(), 0);
    bullet_table = table.data();
    unsigned char* rec = bullet_table + std::size_t(subtype) * core::layout::type_stride;
    w32(rec, core::layout::bullet_flags, flags);
    w32(rec, core::layout::bullet_light + 0, 230);
    w32(rec, core::layout::bullet_light + 4, 60);
    w32(rec, core::layout::bullet_light + 8, 255);
    w32(rec, core::layout::bullet_shield_damage, 1200);
    w32(rec, core::layout::bullet_hull_damage, 300);
    std::memset(config_block, 0, 0x800);
    w32(config_block, core::layout::cfg_game_ms, game_ms);
    cfg_ptr_slot = u32(config_block);
    bullet_ptr_slot = u32(bullet_table);
    count_slot = 16;
    // TShips: subtype 4 is an M3 (OBJ_SHIP_M3 2023); no TShields rows until set_shields.
    std::fill(ship_table.begin(), ship_table.end(), 0);
    w32(ship_table.data() + 4 * core::layout::type_stride, core::layout::ship_class_id, 2023);
    ship_ptr_slot = u32(ship_table.data());
    ship_count_slot = 8;
    shield_ptr_slot = 0;
    shield_count_slot = 0;
}
// The victim's shields: n chained nodes (row k of TShields with capacity caps[k], energy energies[k]) and the sentinel
// node n, whose fields are poison (an index past the count, an unmapped extension): the read must stop at it.
void set_shields(unsigned n, const std::int32_t* caps, const std::int32_t* energies) {
    std::memset(shield_ext_block, 0, sizeof shield_ext_block);
    std::memset(shield_node, 0, sizeof shield_node);
    std::memset(shield_energy_block, 0, sizeof shield_energy_block);
    std::fill(shield_table.begin(), shield_table.end(), 0);
    for (unsigned k = 0; k < n && k < shield_nodes_max - 1; ++k) {
        const unsigned row = k % shield_rows;
        w32(shield_node[k], core::layout::shield_next, u32(shield_node[k + 1]));
        w16(shield_node[k], core::layout::shield_index, std::uint16_t(row));
        w32(shield_node[k], core::layout::shield_ext, u32(shield_energy_block[k]));
        w32(shield_energy_block[k], core::layout::shield_energy, std::uint32_t(energies[k]));
        w32(shield_table.data() + row * core::layout::type_stride, core::layout::shield_capacity, std::uint32_t(caps[k]));
    }
    w16(shield_node[n], core::layout::shield_index, 0x7fff);
    w32(shield_node[n], core::layout::shield_ext, 0x00000010u);
    w32(shield_ext_block, core::layout::ext_shield_list, u32(shield_node[0]));
    w32(&victim_obj, core::layout::obj_ext, u32(shield_ext_block));
    shield_ptr_slot = u32(shield_table.data());
    shield_count_slot = shield_rows;
}
// The engine's own current / maximum (0x0044ecd0 / 0x0044ee10: per shield energy x capacity / 0x3e80000, truncated) as
// a fraction, the reference the handler's 2^-15 approximation is compared with.
double hud_fraction(unsigned n, const std::int32_t* caps, const std::int32_t* energies) {
    long long cur = 0, max = 0;
    for (unsigned k = 0; k < n; ++k) {
        cur += (long long)energies[k] * caps[k] / 0x3e80000;
        max += caps[k];
    }
    return max ? double(cur) / double(max) : 0.0;
}
void expected_pos(std::int32_t out[3]) {
    const auto* rot = reinterpret_cast<std::int32_t*>(source_node.bytes + core::layout::node_rot);
    const auto* off = reinterpret_cast<std::int32_t*>(source_node.bytes + core::layout::node_contact);
    const auto* spos = reinterpret_cast<std::int32_t*>(source_node.bytes + core::layout::node_pos);
    const auto* vpos = reinterpret_cast<std::int32_t*>(victim_node.bytes + core::layout::node_pos);
    const auto* vrot = reinterpret_cast<std::int32_t*>(victim_node.bytes + core::layout::node_rot);
    std::int32_t p[3], d[3];
    core::rotate_out(rot, off, p);
    for (unsigned i = 0; i < 3; ++i) p[i] = core::wrap_add(p[i], spos[i]);
    for (unsigned i = 0; i < 3; ++i) d[i] = core::wrap_sub(p[i], vpos[i]);
    core::rotate_in(vrot, d, out);
}
constexpr unsigned frame_offset = 0x600; // we_enter's F inside we_frame_buffer
// 50 events on a full 1024-record ring (each scans the ring for its victim), median of 21: the bound set from the native
// measurement of 2026-11-07 (docs/verification/weapon-effects.md, "Ring 1024").
constexpr double event_cost_full_us = 150.0; // 57.9, 59.9 and 85.6 measured (17.9 on an empty ring)
constexpr unsigned char stack_fill = 0x5a;
unsigned max_stack_depth = 0;            // bytes below F the stub and the handler wrote, over every run
struct RunResult {
    DWORD error;
    std::uint32_t flags_in;
    std::uint32_t out[13], ca[16];
};
RunResult run_window(std::uint32_t absorbed, std::uint32_t total, std::uint32_t df) {
    std::memset(we_out, 0, sizeof we_out);
    std::memset(we_ca, 0, sizeof we_ca);
    we_in[0] = u32(&victim_obj);
    we_in[1] = u32(&source_obj);
    we_in[2] = total;
    we_in[3] = absorbed;
    we_in[4] = hostile_mxcsr;
    we_in[5] = hostile_fcw;
    we_in[6] = df;
    we_in[7] = entry_flags | (df ? 0x400u : 0u);
    std::memset(we_frame_buffer, stack_fill, frame_offset); // the stack below F, to measure how deep the hook went
    SetLastError(sentinel_error);
    we_enter();
    unsigned lowest = 0;
    while (lowest < frame_offset && we_frame_buffer[lowest] == stack_fill) ++lowest;
    if (frame_offset - lowest > max_stack_depth) max_stack_depth = frame_offset - lowest;
    RunResult r{};
    r.error = GetLastError();
    r.flags_in = we_in[7];
    std::memcpy(r.out, we_out, sizeof r.out);
    std::memcpy(r.ca, we_ca, sizeof r.ca);
    return r;
}
bool frame_kept(const RunResult& r) {
    double st0 = 0, st1 = 0;
    std::memcpy(&st0, r.out + 8, 8);
    std::memcpy(&st1, r.out + 10, 8);
    return r.out[1] == 0xeb1eb1ebu && r.out[2] == 0xeb0eb0ebu && r.out[3] == 0xe51e51e5u && r.out[4] == 0xed1ed1edu &&
           (r.out[5] & 0x400u) == (r.flags_in & 0x400u) && r.out[6] == hostile_mxcsr && (r.out[7] & 0xffff) == hostile_fcw && st0 == -3.5e10 &&
           st1 == 1.25 && r.error == sentinel_error && std::int32_t(r.out[12]) == 0x30;
}
bool read5(std::uintptr_t at, unsigned char out[5]) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(at), out, 5, &got) && got == 5;
}
std::uintptr_t call_target(std::uintptr_t site) {
    unsigned char now[5]{};
    if (!read5(site, now) || now[0] != 0xe8) return 0;
    std::int32_t rel = 0;
    std::memcpy(&rel, now + 1, 4);
    return site + 5 + std::uintptr_t(rel);
}
DWORD protection(std::uintptr_t at) {
    MEMORY_BASIC_INFORMATION m{};
    return VirtualQuery(reinterpret_cast<const void*>(at), &m, sizeof m) ? m.Protect : 0;
}
bool impact_redirected() {
    return call_target(impact_site()) == u32(reinterpret_cast<void*>(&x3m_weapon_effects_stub_impact));
}
void set_setting(const wchar_t* value) {
    SetEnvironmentVariableW(L"X3M_WEAPON_EFFECTS", value);
}
we::Addresses addresses() {
    return we::Addresses{at(sites::impact_window_va), at(sites::impact_target_va), at(sites::prologue_va),
                         {at(sites::tap_window_va[0]), at(sites::tap_window_va[1])}, at(sites::tap_target_va)};
}
unsigned ring_live(const core::Ring& r) {
    unsigned n = 0;
    for (unsigned i = 0; i < core::ring_capacity; ++i) n += r.slots[i].sequence != 0;
    return n;
}
// The newest record of an owner (by sequence); false when none.
bool newest_record(std::uint32_t owner, core::Event* out) {
    const core::Ring& r = we::hook_state().ring;
    std::uint32_t best = 0;
    for (unsigned i = 0; i < core::ring_capacity; ++i)
        if (r.slots[i].sequence > best && r.slots[i].owner == owner) {
            best = r.slots[i].sequence;
            *out = r.slots[i];
        }
    return best != 0;
}
bool window_is(std::uintptr_t va, const unsigned char* expected, unsigned length) {
    unsigned char now[64]{};
    return engine_patch::read_code(at(va), now, length) && !std::memcmp(now, expected, length);
}
bool all_windows_original() {
    return window_is(sites::impact_window_va, sites::expected_impact_window, sites::impact_window_length) &&
           window_is(sites::tap_window_va[0], sites::expected_tap_window[0], sites::tap_window_length) &&
           window_is(sites::tap_window_va[1], sites::expected_tap_window[1], sites::tap_window_length);
}
void flip(std::uintptr_t va) {
    *reinterpret_cast<unsigned char*>(at(va)) ^= 0xff;
}
void core_checks() {
    check(core::role_from(1, core::layout::class_bullet, 0) == core::role_shield_hit, "rule_absorbed_bullet_shield");
    check(core::role_from(1, core::layout::class_bullet, core::bullet_hull_through_shields) == core::role_hull_hit,
          "rule_absorbed_bullet_flag20_hull");
    check(core::role_from(1, core::layout::class_missile, core::bullet_hull_through_shields) == core::role_shield_hit,
          "rule_absorbed_missile_flag20_ignored");
    check(core::role_from(0, core::layout::class_bullet, 0) == core::role_hull_hit, "rule_not_absorbed_hull");
    check(core::style_from(0x2, core::shape_slug) == core::shape_beam, "style_beam_bit");
    check(core::style_from(0x8, core::shape_slug) == core::shape_wave, "style_wave_bit");
    check(core::style_from(0x0, core::shape_from_elongation(18.5f, true)) == core::shape_tracer, "style_tracer_elongation");
    check((core::style_from(0x1000, core::shape_from_elongation(1.9f, true)) & core::style_burst) &&
              (core::style_from(0x1000, core::shape_from_elongation(1.9f, true)) & core::style_shape_mask) == core::shape_slug,
          "style_slug_burst");
    check(core::light_saturated(230, 60, 255) && !core::light_saturated(48, 48, 48) && !core::light_saturated(20, 0, 0),
          "tint_saturation");
    check(core::tier_from(220) == core::tier_light && core::tier_from(7845) == core::tier_medium &&
              core::tier_from(63778) == core::tier_heavy,
          "calibre_bands");
    core::Ring ring;
    core::ring_clear(ring);
    for (unsigned i = 0; i < core::ring_capacity; ++i) {
        core::Event* e = core::ring_acquire(ring);
        e->role = (i == 5) ? core::role_shield_hit : core::role_hull_hit;
        e->flags = (i == 5) ? std::uint8_t(core::flag_stage | core::flag_forwarded) : std::uint8_t(core::flag_forwarded);
        e->owner = 0x1000 + (i & 3);
    }
    const std::uint32_t oldest_shield = ring.slots[5].sequence;
    core::Event* over = core::ring_acquire(ring);
    check(ring.overflow == 1 && over && over->sequence != 0, "overflow_counts");
    bool shield_survived = false;
    for (unsigned i = 0; i < core::ring_capacity; ++i)
        if (ring.slots[i].sequence == oldest_shield) shield_survived = true;
    check(shield_survived, "overflow_keeps_owned_shield");
    // Review 2: a staged hull hit is no cheaper than a staged shield hit (its native spawn was skipped): with census
    // records present the oldest census record goes; with only staged records the oldest of them, whatever its role.
    core::ring_clear(ring);
    for (unsigned i = 0; i < core::ring_capacity; ++i) {
        core::Event* e = core::ring_acquire(ring);
        e->role = (i & 1) ? core::role_hull_hit : core::role_shield_hit;
        e->flags = i == 200 ? std::uint8_t(core::flag_forwarded) : std::uint8_t(core::flag_stage);
    }
    const unsigned census_slot = core::ring_victim(ring);
    const bool census_first = ring.slots[census_slot].flags == core::flag_forwarded;
    ring.slots[census_slot].flags = core::flag_stage;
    const unsigned oldest_slot = core::ring_victim(ring);
    std::uint32_t oldest_seq = 0xffffffffu;
    for (unsigned i = 0; i < core::ring_capacity; ++i) oldest_seq = std::min(oldest_seq, ring.slots[i].sequence);
    check(census_first && ring.slots[oldest_slot].sequence == oldest_seq &&
              ring.slots[oldest_slot].role == core::role_shield_hit,
          "overflow_census_first_then_oldest_staged");
    core::ring_clear(ring);
    for (unsigned i = 0; i < 6; ++i) {
        core::Event* e = core::ring_acquire(ring);
        e->owner = (i < 4) ? 0x2000u : 0x3000u;
    }
    const unsigned retired = core::ring_retire_owner(ring, 0x2000u);
    unsigned live = 0;
    for (unsigned i = 0; i < core::ring_capacity; ++i) live += ring.slots[i].sequence != 0;
    check(retired == 4 && live == 2, "nested_death_retires_owner");
    core::Event e{};
    e.t0_game_ms = 1000;
    e.lifetime_ms = 600;
    check(core::age_ms(1100, e.t0_game_ms) == 100 && !core::expired(1100, e) && core::expired(1700, e) &&
              core::expired(500, e),
          "age_law_game_time");
}
// Step 4 hook cases (moved here from weapon_effects_bolt_cases.h when the look layer was scratched, 2026-11-08): the
// per-bullet tap 0x0045ac7d, the muzzle 0x00446268 and the flight-effect skip 0x004146c0, run through the real windows
// the fixture reserved in the engine pages, inside this file's anonymous namespace (at()/w32()/check()/the globals);
// the asm harness and the callee stand-ins carry unique symbols. No stage latches the bolts any more (present_bolts
// keeps owned and muzzle_owned 0); the skip paths are driven by setting the latches directly. The handler
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

    // The generation stamp: Present advances it, so the next frame's list starts empty (a reader takes exactly one
    // generation); it latches nothing (no in-place draw since the look layer was scratched).
    b.owned = 1;
    x3m::weapon_effects::present_bolts(1, false);
    we_bolt_obj = u32(&bolt_bullet);
    std::memset(we_bolt_ca, 0, sizeof we_bolt_ca);
    run_bolt(we_bolt_tap_enter, at(bolt_sites::tap_site_va) - 1, at(bolt_sites::tap_window_end_va));
    const core::BoltList& l1 = b.lists[b.generation & 1u];
    check(b.generation == gen + 1 && l1.generation == b.generation && l1.count == 1 && b.owned == 0,
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
    // Present clears both latches every frame (no stage draws the muzzles since the look layer was scratched).
    b.muzzle_owned = 1;
    x3m::weapon_effects::present_bolts(7, false);
    const bool latched_off = b.muzzle_owned == 0 && b.owned == 0;
    check(fwd && latched_off && bolt_muzzle_frame_kept(), "muzzle_forwards_and_not_ours_without_latch");
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
// Step 3 hook cases (moved here from weapon_effects_death_cases.h when the look layer was scratched, 2026-11-08; the
// sites in src/proxy/weapon_effects_death_sites.h): the death sites 0x0044ac64 (area) and 0x0044acb2 (other) ->
// 0x00413610, run through the real windows the fixture reserved in the engine pages, inside this file's anonymous
// namespace (at() / w32() / check() / write_jmp() and the globals); the asm harness and the callee stand-in carry unique
// symbols. The handler (core::death through x3m_weapon_death_area / _other) is the production object the audit proved
// holds no call and no x87.
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

    // death_decision_seam: boom::decide is the one place a tier is ours or native (fighters and missiles ours; the big
    // tiers in boom::tier_native since their clone row R' left with the look, the asteroid and any other class native).
    check(core::boom::decide(core::boom::fighter) == core::boom::decide_ours &&
              core::boom::decide(core::boom::missile) == core::boom::decide_ours &&
              core::boom::decide(core::boom::corvette) == core::boom::decide_native &&
              core::boom::decide(core::boom::capital) == core::boom::decide_native &&
              core::boom::decide(core::boom::station) == core::boom::decide_native && core::boom::tier_native == 14 &&
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
    // death_forwards_big_tier_census: an M6 (corvette) and a factory (station) forward byte-exact while owned (their
    // tiers are native), each recorded census-only with its tier, row and cause.
    std::memset(we_death_ca, 0, sizeof we_death_ca);
    err = run_death(1, &death_obj[2]);
    core::Event rc{}, rs{};
    const bool corvette = death_forward_exact(1, &death_obj[2], 1) && death_frame_kept(err, &death_obj[2]) &&
                          death_record(&death_obj[2], &rc) && (rc.flags & core::flag_forwarded) &&
                          rc.style == core::boom::corvette && rc.row == 40 && rc.tier == core::boom::cause_tier_native &&
                          !(rc.flags & core::flag_stage) && rc.lifetime_ms == 0;
    std::memset(we_death_ca, 0, sizeof we_death_ca);
    err = run_death(1, &death_obj[5]);
    const bool station = death_forward_exact(1, &death_obj[5], 1) && death_frame_kept(err, &death_obj[5]) &&
                         death_record(&death_obj[5], &rs) && (rs.flags & core::flag_forwarded) &&
                         rs.style == core::boom::station && rs.row == 201 && rs.table == 6 &&
                         rs.tier == core::boom::cause_tier_native;
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
    // their life plus the stage's floor, then releases them; it never latches the death family (no stage draws it).
    hs.boom_floor_ms = 100;
    x3m::weapon_effects::StageFrame sf{};
    x3m::weapon_effects::present(300, false, true, sf);
    const bool census_gone = !death_record(&death_obj[2], &rec);
    const bool ours_kept = death_record(&death_obj[0], &rec);
    const bool latched = hs.boom_ready == 0 && hs.owned_next == 1;
    w32(config_block, core::layout::cfg_game_ms, 61000 + core::boom::record_life_ms[core::boom::fighter] + 50);
    x3m::weapon_effects::present(301, false, true, sf);
    const bool floor_kept = death_record(&death_obj[0], &rec);
    w32(config_block, core::layout::cfg_game_ms, 61000 + core::boom::record_life_ms[core::boom::fighter] + 200);
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
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    check(build_pages(), "engine_pages_reserved");
    core_checks();

    unsigned char now[64]{};
    check(engine_patch::read_code(at(sites::impact_window_va), now, sites::impact_window_length) &&
              !std::memcmp(now, sites::expected_impact_window, sites::impact_window_length),
          "impact_window_is_header");

    build_world(7, 0, 4, 0x0, 123456);

    set_setting(L"observe");
    executable_ok = true;
    const bool observe_ok = we::install_at(addresses(), core::mode_observe);
    check(observe_ok && we::installed() && we::mode() == core::mode_observe && impact_redirected(), "observe_installed");

    core::HookState& hs = we::hook_state();
    hs.owned_next = 0;
    RunResult fwd = run_window(1, 1200, 1);
    check(fwd.ca[11] == 1 && fwd.ca[9] == u32(&victim_obj) && fwd.ca[10] == u32(&source_obj), "forward_reaches_callee_args");
    check(fwd.ca[5] == u32(&victim_obj) && fwd.ca[6] == u32(&source_obj), "forward_callee_esi_edi");
    // The forward path reaches 0x00413c40 with the caller-dead registers, EBX/EBP, the flags, the return address and the
    // stack exactly as the call left them (EAX/ECX/EDX are saved by the stub around the handler).
    std::printf("FORWARD_STATE eflags_entry=%08lx eflags_callee=%08lx esp_frame=%08lx esp_callee=%08lx return=%08lx site=%08lx\n",
                static_cast<unsigned long>(fwd.flags_in), static_cast<unsigned long>(fwd.ca[7]),
                static_cast<unsigned long>(we_frame_esp), static_cast<unsigned long>(fwd.ca[12]),
                static_cast<unsigned long>(fwd.ca[8]), static_cast<unsigned long>(impact_site()));
    check(fwd.ca[0] == 0xa1a1a1a1u && fwd.ca[1] == 0xc1c1c1c1u && fwd.ca[2] == 0xd1d1d1d1u && fwd.ca[3] == 0x5b5b5b5bu &&
              fwd.ca[4] == 0xeb9eb9ebu,
          "forward_callee_eax_ecx_edx_ebx_ebp");
    check(fwd.ca[8] == std::uint32_t(impact_site() + sites::call_length), "forward_callee_return_address");
    check(fwd.ca[12] == we_frame_esp - 12u, "forward_callee_esp");
    check((fwd.ca[7] & flag_mask) == (fwd.flags_in & flag_mask), "forward_callee_eflags");
    check(frame_kept(fwd), "forward_frame_kept");
    bool found = false;
    core::Event rec{};
    for (unsigned i = 0; i < core::ring_capacity; ++i)
        if (hs.ring.slots[i].sequence && hs.ring.slots[i].owner == u32(&victim_obj)) {
            rec = hs.ring.slots[i];
            found = true;
        }
    std::int32_t want[3];
    expected_pos(want);
    check(found && rec.role == core::role_shield_hit && rec.damage == 1200 && (rec.flags & core::flag_absorbed) &&
              (rec.flags & core::flag_forwarded) && rec.table == core::table_bullets && rec.row == 4,
          "forward_records_shield_hit");
    check(found && rec.pos[0] == want[0] && rec.pos[1] == want[1] && rec.pos[2] == want[2], "handler_contact_point");
    check(found && rec.tint_source == core::tint_light && (rec.tint_rgb & 0xff) == 230, "classifier_tint_light");
    check(found && !(rec.flags & core::flag_nested) && !(rec.flags & core::flag_stage), "no_bullet_extension_read");
    // The default world: no shield extension (unread), an M3 by its TShips class id.
    check(found && !(rec.flags & core::flag_shield_read) && rec.shield_q == 0 && rec.patch_tier == core::patch_fighter &&
              rec.tier_source == core::tier_by_ship_class && rec.lifetime_ms == core::patch_lifetime_ms,
          "record_shield_unread_tier_ship_class");

    we::shutdown();
    set_setting(L"shields");
    const bool shields_ok = we::install_at(addresses(), core::mode_shields);
    check(shields_ok && we::mode() == core::mode_shields, "shields_installed");
    core::HookState& hs2 = we::hook_state();
    // Coordinator decision 2026-11-06: a stage-owned shield hit skips the engine's spawn by default (the patch alone):
    // the record is the stage's and not forwarded, the callee never entered, the forwarded count unchanged, and the
    // frame as the caller expects (registers, flags, MXCSR, x87, LastError, stack: frame_kept); a hull hit still
    // forwards and is counted.
    check(core::suppress_native_shield_hit && hs2.suppress_native == 1, "shields_default_skips_native");
    {
        hs2.owned_next = 1;
        std::memset(we_ca, 0, sizeof we_ca);
        const std::uint32_t forwarded = hs2.counters.forwarded, skipped = hs2.counters.skipped;
        const RunResult skipped_run = run_window(1, 1200, 1);
        core::Event rec{};
        const bool shield_skipped = skipped_run.ca[11] == 0 && hs2.counters.forwarded == forwarded &&
                                    hs2.counters.skipped == skipped + 1 && newest_record(u32(&victim_obj), &rec) &&
                                    (rec.flags & core::flag_stage) && !(rec.flags & core::flag_forwarded) &&
                                    frame_kept(skipped_run);
        build_world(7, 0, 4, core::bullet_hull_through_shields, 123456);
        hs2.owned_next = 1;
        std::memset(we_ca, 0, sizeof we_ca);
        const std::uint32_t forwarded_hull = hs2.counters.forwarded;
        const RunResult hull_run = run_window(1, 1200, 1);
        const bool hull_forwarded = hull_run.ca[11] == 1 && hs2.counters.forwarded == forwarded_hull + 1 &&
                                    frame_kept(hull_run);
        std::printf("SUPPRESS shield_callee=%u shield_forwarded_delta=%lu shield_skipped_delta=%lu hull_callee=%u hull_forwarded_delta=%lu\n",
                    unsigned(skipped_run.ca[11]), static_cast<unsigned long>(hs2.counters.forwarded - forwarded - 1),
                    static_cast<unsigned long>(hs2.counters.skipped - skipped), unsigned(hull_run.ca[11]),
                    static_cast<unsigned long>(hs2.counters.forwarded - forwarded_hull));
        check(shield_skipped && hull_forwarded, "shield_hit_skips_callee_hull_forwards");
        build_world(7, 0, 4, 0x0, 123456);
    }
    // The forward path of a stage-owned shield hit (suppress_native cleared): the callee with the call's state.
    hs2.suppress_native = 0;
    hs2.owned_next = 1;
    std::memset(we_ca, 0, sizeof we_ca);
    RunResult staged = run_window(1, 1200, 1);
    core::Event staged_rec{};
    check(staged.ca[11] == 1 && staged.ca[9] == u32(&victim_obj) && staged.ca[10] == u32(&source_obj) &&
              newest_record(u32(&victim_obj), &staged_rec) && (staged_rec.flags & core::flag_stage) &&
              (staged_rec.flags & core::flag_forwarded) && frame_kept(staged),
          "shield_hit_stage_and_forward");
    check((staged.ca[7] & flag_mask) == (staged.flags_in & flag_mask) && staged.ca[12] == we_frame_esp - 12u &&
              staged.ca[8] == std::uint32_t(impact_site() + sites::call_length),
          "shield_hit_forward_callee_state");
    // The skip path (the default): no callee, the frame as the caller expects.
    hs2.suppress_native = 1;
    hs2.owned_next = 1;
    std::memset(we_ca, 0, sizeof we_ca);
    RunResult skip = run_window(1, 1200, 1);
    check(skip.ca[11] == 0 && newest_record(u32(&victim_obj), &staged_rec) && (staged_rec.flags & core::flag_stage) &&
              !(staged_rec.flags & core::flag_forwarded),
          "skip_no_callee");
    check(frame_kept(skip) && protection(at(dmg_page)) == PAGE_EXECUTE_READWRITE, "skip_frame_kept");
    hs2.owned_next = 1;
    build_world(7, 0, 4, core::bullet_hull_through_shields, 123456);
    std::memset(we_ca, 0, sizeof we_ca);
    RunResult hull = run_window(1, 1200, 0);
    check(hull.ca[11] == 1, "hull_forwards_in_shields");
    // No projectile on either side (class 7 against class 7): no record, forwarded (the engine spawns nothing either).
    build_world(7, 7, 4, 0x0, 123456);
    hs2.owned_next = 1;
    const unsigned live_before = ring_live(hs2.ring), no_projectile_before = hs2.counters.no_projectile;
    std::memset(we_ca, 0, sizeof we_ca);
    RunResult none = run_window(1, 1200, 1);
    check(none.ca[11] == 1 && ring_live(hs2.ring) == live_before && hs2.counters.no_projectile == no_projectile_before + 1 &&
              frame_kept(none),
          "no_projectile_class_forwards_unrecorded");
    // Inside a kill (note 2.7): a dying projectile (the blast's source) is still the stage's for a live victim (and
    // skips under suppress_native) and is marked nested; a dying hit object is native only (the stage would retire it
    // unseen) and marked nested.
    build_world(7, 0, 4, 0x0, 123456);
    w32(&source_obj, core::layout::obj_flags, core::layout::obj_dying);
    hs2.suppress_native = 0;
    hs2.owned_next = 1;
    std::memset(we_ca, 0, sizeof we_ca);
    RunResult blast = run_window(1, 1200, 1);
    core::Event nested_rec{};
    bool blast_ok = blast.ca[11] == 1 && newest_record(u32(&victim_obj), &nested_rec) &&
                    (nested_rec.flags & core::flag_nested) && (nested_rec.flags & core::flag_stage) &&
                    (nested_rec.flags & core::flag_forwarded) && frame_kept(blast);
    hs2.suppress_native = 1;
    hs2.owned_next = 1;
    std::memset(we_ca, 0, sizeof we_ca);
    blast = run_window(1, 1200, 1);
    blast_ok = blast_ok && blast.ca[11] == 0 && newest_record(u32(&victim_obj), &nested_rec) &&
               (nested_rec.flags & core::flag_nested) && !(nested_rec.flags & core::flag_forwarded) && frame_kept(blast);
    check(blast_ok, "nested_dying_projectile_staged_marked");
    build_world(7, 0, 4, 0x0, 123456);
    w32(&victim_obj, core::layout::obj_flags, core::layout::obj_dying);
    hs2.owned_next = 1;
    hs2.suppress_native = 1; // even with the skip selected, a dying victim stays native
    std::memset(we_ca, 0, sizeof we_ca);
    RunResult dying = run_window(1, 1200, 1);
    check(dying.ca[11] == 1 && newest_record(u32(&victim_obj), &nested_rec) && (nested_rec.flags & core::flag_nested) &&
              (nested_rec.flags & core::flag_forwarded) && !(nested_rec.flags & core::flag_stage) && frame_kept(dying),
          "nested_dying_victim_forwards_marked");

    // The shield read (spawn note 9) and the patch tier (5.1.1), on the stage path (skipped by default, flag_stage).
    {
        auto hit = [&](core::Event* out) {
            hs2.owned_next = 1;
            std::memset(we_ca, 0, sizeof we_ca);
            const RunResult r = run_window(1, 1200, 0);
            return newest_record(u32(&victim_obj), out) && r.ca[11] == 0 && frame_kept(r);
        };
        core::Event e{};
        char detail[160];
        // The projectile's travel (Run235 Flight B fix, Event::dir): the source (bullet) node's row 2 (local z in world,
        // 16.16) in the hit object's frame (its rows, as pos) as int8 x 127 rounded. The victim's rows turned (local x ->
        // world -z, y -> y, z -> world +x): the bullet's identity row (0, 0, 1) gives (-127, 0, 0); the row (0.6, -0.8,
        // 0) is local (0, -0.8, 0.6) and gives (0, -102, 76) (the independent rounding below); a zero row records (0, 0,
        // 0) and counts dir_fallback; a dead bullet node records nothing (no_node: the callee's own test returns before
        // any effect) and forwards.
        {
            build_world(7, 0, 4, 0x0, 123456);
            const std::int32_t turned[3][3] = {{0, 0, -0x10000}, {0, 0x10000, 0}, {0x10000, 0, 0}};
            for (unsigned r = 0; r < 3; ++r)
                for (unsigned c = 0; c < 3; ++c)
                    w32(&victim_node, core::layout::node_rot + 16 * r + 4 * c, std::uint32_t(turned[r][c]));
            bool ok = hit(&e);
            const bool identity = ok && e.dir[0] == -127 && e.dir[1] == 0 && e.dir[2] == 0;
            const std::int32_t row[3] = {39322, -52429, 0};
            for (unsigned k = 0; k < 3; ++k) w32(&source_node, core::layout::node_rot + 32 + 4 * k, std::uint32_t(row[k]));
            ok = hit(&e);
            auto rounded = [](std::int32_t v) { // floor(v x 127 / 65536 + 1/2), integer only
                const std::int64_t n = std::int64_t(v) * 127 + 32768;
                return int(n >= 0 ? n / 65536 : -((-n + 65535) / 65536));
            };
            const bool known = ok && e.dir[0] == 0 && e.dir[1] == rounded(row[1]) && e.dir[2] == rounded(row[0]) &&
                               rounded(row[0]) == 76 && rounded(row[1]) == -102;
            std::printf("DIR case=known row=%ld,%ld,%ld dir=%d,%d,%d want=0,%d,%d identity_row=%u\n", long(row[0]),
                        long(row[1]), long(row[2]), int(e.dir[0]), int(e.dir[1]), int(e.dir[2]), rounded(row[1]),
                        rounded(row[0]), unsigned(identity));
            check(identity && known, "travel_dir_from_bullet_row2");
            const std::uint32_t fallback = hs2.counters.dir_fallback;
            for (unsigned k = 0; k < 3; ++k) w32(&source_node, core::layout::node_rot + 32 + 4 * k, 0);
            ok = hit(&e);
            const bool zero = ok && !e.dir[0] && !e.dir[1] && !e.dir[2] && hs2.counters.dir_fallback == fallback + 1;
            w32(&source_node, core::layout::node_live, 0);
            const unsigned live_before = ring_live(hs2.ring);
            const std::uint32_t no_node = hs2.counters.no_node, fallback_dead = hs2.counters.dir_fallback;
            hs2.owned_next = 1;
            std::memset(we_ca, 0, sizeof we_ca);
            const RunResult dead = run_window(1, 1200, 0);
            const bool dead_ok = dead.ca[11] == 1 && frame_kept(dead) && ring_live(hs2.ring) == live_before &&
                                 hs2.counters.no_node == no_node + 1 && hs2.counters.dir_fallback == fallback_dead;
            std::printf("DIR case=fallback zero_row=%u dir_fallback=%lu dead_node_recorded=%u no_node=%lu\n",
                        unsigned(zero), static_cast<unsigned long>(hs2.counters.dir_fallback - fallback),
                        unsigned(ring_live(hs2.ring) != live_before), static_cast<unsigned long>(hs2.counters.no_node - no_node));
            check(zero && dead_ok, "travel_dir_fallback_zero_row_dead_node");
        }
        // Three shields: capacity-weighted (the HUD bar), not the mean of the fractions.
        const std::int32_t caps[3] = {20000, 50000, 125000};
        const std::int32_t energy[3] = {core::layout::shield_full, core::layout::shield_full / 2,
                                        std::int32_t(0.1234 * double(core::layout::shield_full))};
        build_world(7, 0, 4, 0x0, 123456);
        set_shields(3, caps, energy);
        bool ok = hit(&e);
        const double want = hud_fraction(3, caps, energy), got = double(e.shield_q) / 65535.0;
        std::snprintf(detail, sizeof detail, "q=%u got=%.6f hud=%.6f", unsigned(e.shield_q), got, want);
        std::printf("SHIELD case=weighted q=%u fraction=%.6f hud=%.6f error=%.2e mean_of_fractions=%.6f\n",
                    unsigned(e.shield_q), got, want, got - want, (1.0 + 0.5 + 0.1234) / 3.0);
        check(ok && (e.flags & core::flag_shield_read) && (e.flags & core::flag_stage) && got - want < 4e-5 &&
                  want - got < 4e-5,
              "shield_read_capacity_weighted", detail);
        // Two shields of 2e9 points (no 32-bit overflow), one full, one at a quarter.
        const std::int32_t big_caps[2] = {2000000000, 2000000000};
        const std::int32_t big_energy[2] = {core::layout::shield_full, core::layout::shield_full / 4};
        build_world(7, 0, 4, 0x0, 123456);
        set_shields(2, big_caps, big_energy);
        ok = hit(&e);
        const double big = double(e.shield_q) / 65535.0;
        std::printf("SHIELD case=large q=%u fraction=%.6f hud=%.6f\n", unsigned(e.shield_q), big,
                    hud_fraction(2, big_caps, big_energy));
        check(ok && (e.flags & core::flag_shield_read) && big > 0.62495 && big < 0.62505, "shield_read_large_capacity");
        // Energy above full (recharge clamps; a corrupt value) and below 0 are clamped.
        const std::int32_t odd_caps[2] = {1000, 1000};
        const std::int32_t odd_energy[2] = {0x7fffffff, -5};
        build_world(7, 0, 4, 0x0, 123456);
        set_shields(2, odd_caps, odd_energy);
        ok = hit(&e);
        check(ok && (e.flags & core::flag_shield_read) && e.shield_q >= 32766 && e.shield_q <= 32768,
              "shield_read_energy_clamped");
        // The sentinel ends the list: its poison fields are not read; a head that is the sentinel has no capacity.
        build_world(7, 0, 4, 0x0, 123456);
        set_shields(0, caps, energy);
        ok = hit(&e);
        check(ok && !(e.flags & core::flag_shield_read) && e.shield_q == 0, "shield_sentinel_only_unread");
        build_world(7, 0, 4, 0x0, 123456);
        set_shields(1, caps, energy);
        ok = hit(&e);
        check(ok && (e.flags & core::flag_shield_read) && e.shield_q == 0xffff, "shield_sentinel_ends_list");
        // A TShields index at the count, a negative index, a null shield extension: the whole read is refused.
        build_world(7, 0, 4, 0x0, 123456);
        set_shields(3, caps, energy);
        w16(shield_node[1], core::layout::shield_index, std::uint16_t(shield_rows));
        ok = hit(&e);
        bool refused = ok && !(e.flags & core::flag_shield_read) && e.shield_q == 0;
        w16(shield_node[1], core::layout::shield_index, 0x8000);
        ok = hit(&e);
        refused = refused && ok && !(e.flags & core::flag_shield_read);
        check(refused, "shield_index_out_of_range_refused");
        w16(shield_node[1], core::layout::shield_index, 1);
        w32(shield_node[1], core::layout::shield_ext, 0);
        ok = hit(&e);
        check(ok && !(e.flags & core::flag_shield_read), "shield_null_shield_extension_refused");
        // A null object extension, a null list head, a null TShields table.
        build_world(7, 0, 4, 0x0, 123456);
        set_shields(3, caps, energy);
        w32(&victim_obj, core::layout::obj_ext, 0);
        ok = hit(&e);
        refused = ok && !(e.flags & core::flag_shield_read);
        w32(&victim_obj, core::layout::obj_ext, u32(shield_ext_block));
        w32(shield_ext_block, core::layout::ext_shield_list, 0);
        ok = hit(&e);
        refused = refused && ok && !(e.flags & core::flag_shield_read);
        w32(shield_ext_block, core::layout::ext_shield_list, u32(shield_node[0]));
        shield_ptr_slot = 0;
        ok = hit(&e);
        refused = refused && ok && !(e.flags & core::flag_shield_read);
        check(refused, "shield_null_extension_refused");
        // 64 shields and the sentinel are read; 65 nodes without a sentinel within 64 are refused.
        std::int32_t many_caps[shield_nodes_max], many_energy[shield_nodes_max];
        for (unsigned k = 0; k < shield_nodes_max; ++k) {
            many_caps[k] = 1000;
            many_energy[k] = core::layout::shield_full / 2;
        }
        build_world(7, 0, 4, 0x0, 123456);
        set_shields(64, many_caps, many_energy);
        ok = hit(&e);
        const bool at64 = ok && (e.flags & core::flag_shield_read) && e.shield_q >= 32766 && e.shield_q <= 32768;
        build_world(7, 0, 4, 0x0, 123456);
        set_shields(65, many_caps, many_energy);
        ok = hit(&e);
        check(at64 && ok && !(e.flags & core::flag_shield_read), "shield_list_64_node_limit");
        // An unshielded class (17, an asteroid, no bit 20) is not walked even with a list present; bit 20 on a dock is.
        build_world(17, 0, 4, 0x0, 123456);
        set_shields(3, caps, energy);
        ok = hit(&e);
        const bool unshielded = ok && !(e.flags & core::flag_shield_read) && e.patch_tier == core::patch_corvette &&
                                e.tier_source == core::tier_by_radius; // radius 20000: the corvette band
        build_world(5, 0, 4, 0x0, 123456);
        set_shields(3, caps, energy);
        w32(&victim_obj, core::layout::obj_flags, core::layout::obj_shielded);
        ok = hit(&e);
        check(unshielded && ok && (e.flags & core::flag_shield_read) && e.patch_tier == core::patch_capital &&
                  e.tier_source == core::tier_by_station,
              "shield_unshielded_class_skipped_bit20_read");
        // Tier: an M6 by its class id; an unknown id and a subtype past the count fall back to the radius bands.
        build_world(7, 0, 4, 0x0, 123456);
        w32(ship_table.data() + 4 * core::layout::type_stride, core::layout::ship_class_id, 2026);
        ok = hit(&e);
        bool tier_ok = ok && e.patch_tier == core::patch_corvette && e.tier_source == core::tier_by_ship_class;
        w32(ship_table.data() + 4 * core::layout::type_stride, core::layout::ship_class_id, 2022);
        ok = hit(&e);
        tier_ok = tier_ok && ok && e.patch_tier == core::patch_capital && e.tier_source == core::tier_by_ship_class;
        w32(ship_table.data() + 4 * core::layout::type_stride, core::layout::ship_class_id, 9999);
        w32(&victim_obj, core::layout::obj_radius, 5000);
        ok = hit(&e);
        tier_ok = tier_ok && ok && e.patch_tier == core::patch_fighter && e.tier_source == core::tier_by_radius;
        ship_count_slot = 4; // subtype 4 is now past the count
        w32(ship_table.data() + 4 * core::layout::type_stride, core::layout::ship_class_id, 2026);
        w32(&victim_obj, core::layout::obj_radius, 90000);
        ok = hit(&e);
        tier_ok = tier_ok && ok && e.patch_tier == core::patch_capital && e.tier_source == core::tier_by_radius;
        check(tier_ok, "patch_tier_class_id_and_radius_fallback");
        // The drained shield (core::drained_shield_q, user report after Run237 Flight D): an absorbed hit whose read
        // post-hit fraction is below 0.5 % is a hull hit: forwarded (the callee entered: the native impact), recorded
        // role_hull_hit with detail_drained and absorbed, not staged, counted drained; at 400 / 65535 it stays a shield
        // hit (skipped, staged); an unread shield (no extension) keeps the engine's rule (skipped, staged). The frame
        // as the caller expects on every path.
        {
            const std::int32_t one_cap[1] = {1000};
            auto drained_run = [&](std::int32_t energy, core::Event* out, RunResult* run) {
                build_world(7, 0, 4, 0x0, 123456);
                if (energy >= 0) {
                    const std::int32_t e1[1] = {energy};
                    set_shields(1, one_cap, e1);
                }
                hs2.owned_next = 1;
                std::memset(we_ca, 0, sizeof we_ca);
                *run = run_window(1, 1200, 0);
                return newest_record(u32(&victim_obj), out);
            };
            const std::uint32_t drained_before = hs2.counters.drained, forwarded_before = hs2.counters.forwarded,
                                skipped_before = hs2.counters.skipped;
            core::Event low{}, high{}, unread{};
            RunResult low_run{}, high_run{}, unread_run{};
            const bool low_ok = drained_run(std::int32_t(std::int64_t(core::layout::shield_full) * 200 / 65535), &low, &low_run);
            const std::uint32_t drained_low = hs2.counters.drained - drained_before;
            const std::uint32_t forwarded_low = hs2.counters.forwarded - forwarded_before;
            const bool high_ok = drained_run(std::int32_t(std::int64_t(core::layout::shield_full) * 400 / 65535), &high, &high_run);
            const bool unread_ok = drained_run(-1, &unread, &unread_run);
            const std::uint32_t skipped = hs2.counters.skipped - skipped_before;
            std::printf("DRAINED case=low q=%u read=%u role=%s detail=%u absorbed=%u forwarded=%u stage=%u callee=%u "
                        "drained_delta=%lu forwarded_delta=%lu threshold=%u\n",
                        unsigned(low.shield_q), unsigned((low.flags & core::flag_shield_read) != 0), core::role_name(low.role),
                        unsigned(low.detail), unsigned((low.flags & core::flag_absorbed) != 0),
                        unsigned((low.flags & core::flag_forwarded) != 0), unsigned((low.flags & core::flag_stage) != 0),
                        unsigned(low_run.ca[11]), static_cast<unsigned long>(drained_low),
                        static_cast<unsigned long>(forwarded_low), unsigned(core::drained_shield_q));
            std::printf("DRAINED case=high q=%u read=%u role=%s detail=%u stage=%u callee=%u\n", unsigned(high.shield_q),
                        unsigned((high.flags & core::flag_shield_read) != 0), core::role_name(high.role),
                        unsigned(high.detail), unsigned((high.flags & core::flag_stage) != 0), unsigned(high_run.ca[11]));
            std::printf("DRAINED case=unread read=%u role=%s detail=%u stage=%u callee=%u drained_total_delta=%lu "
                        "skipped_delta=%lu\n",
                        unsigned((unread.flags & core::flag_shield_read) != 0), core::role_name(unread.role),
                        unsigned(unread.detail), unsigned((unread.flags & core::flag_stage) != 0),
                        unsigned(unread_run.ca[11]), static_cast<unsigned long>(hs2.counters.drained - drained_before),
                        static_cast<unsigned long>(skipped));
            check(low_ok && (low.flags & core::flag_shield_read) && low.shield_q < core::drained_shield_q &&
                      low.role == core::role_hull_hit && (low.detail & 0x0f) == core::detail_drained &&
                      core::hull_class_of(low) == core::class_energy &&
                      (low.flags & core::flag_absorbed) && (low.flags & core::flag_forwarded) &&
                      !(low.flags & core::flag_stage) && low_run.ca[11] == 1 && drained_low == 1 && forwarded_low == 1 &&
                      frame_kept(low_run),
                  "drained_shield_hit_forwards_as_hull_hit");
            check(high_ok && (high.flags & core::flag_shield_read) && high.shield_q > core::drained_shield_q &&
                      high.role == core::role_shield_hit && !(high.detail & 0x0f) &&
                      core::hull_class_of(high) == core::class_energy && (high.flags & core::flag_stage) &&
                      !(high.flags & core::flag_forwarded) && high_run.ca[11] == 0 && frame_kept(high_run),
                  "shield_hit_above_drained_threshold_staged");
            check(unread_ok && !(unread.flags & core::flag_shield_read) && unread.role == core::role_shield_hit &&
                      !(unread.detail & 0x0f) && core::hull_class_of(unread) == core::class_energy &&
                      (unread.flags & core::flag_stage) && unread_run.ca[11] == 0 &&
                      hs2.counters.drained - drained_before == 1 && skipped == 2 && frame_kept(unread_run),
                  "unread_shield_keeps_the_engine_rule");
            // The boundary: one shield of 1000 points at energy 327000 / 328000 (16.16 thousandths) reads 326 / 327 of
            // 65535 through the handler's shifted division; 326 is drained (forwarded), 327 is not (staged, skipped).
            core::Event b326{}, b327{};
            RunResult r326{}, r327{};
            const bool ok326 = drained_run(327000, &b326, &r326);
            const bool ok327 = drained_run(328000, &b327, &r327);
            std::printf("DRAINED case=boundary q_below=%u role_below=%s detail_below=%u callee_below=%u q_at=%u role_at=%s "
                        "detail_at=%u stage_at=%u callee_at=%u threshold=%u\n",
                        unsigned(b326.shield_q), core::role_name(b326.role), unsigned(b326.detail), unsigned(r326.ca[11]),
                        unsigned(b327.shield_q), core::role_name(b327.role), unsigned(b327.detail),
                        unsigned((b327.flags & core::flag_stage) != 0), unsigned(r327.ca[11]),
                        unsigned(core::drained_shield_q));
            check(ok326 && ok327 && core::drained_shield_q == 327 && b326.shield_q == 326 &&
                      b326.role == core::role_hull_hit && (b326.detail & core::detail_drained) && r326.ca[11] == 1 &&
                      b327.shield_q == 327 && b327.role == core::role_shield_hit && !(b327.detail & 0x0f) &&
                      (b327.detail >> core::detail_class_shift) == core::class_energy &&
                      (b326.detail >> core::detail_class_shift) == core::class_energy &&
                      (b327.flags & core::flag_stage) && r327.ca[11] == 0 && frame_kept(r326) && frame_kept(r327),
                  "drained_threshold_boundary_326_327");
        }
        // The per-event cost: 50 stage-path shield hits on a six-shield ship through the whole window (the fixture's
        // entry, the stub, the handler, the forward to the callee stand-in and the epilogue), best and median of 21.
        const std::int32_t six_caps[6] = {25000, 25000, 25000, 25000, 25000, 25000};
        const std::int32_t six_energy[6] = {core::layout::shield_full, core::layout::shield_full,
                                            core::layout::shield_full / 2, core::layout::shield_full / 2,
                                            core::layout::shield_full / 3, 0};
        build_world(7, 0, 4, 0x0, 123456);
        set_shields(6, six_caps, six_energy);
        LARGE_INTEGER freq{}, a{}, b{};
        QueryPerformanceFrequency(&freq);
        double runs[21];
        for (double& us : runs) {
            core::ring_clear(hs2.ring);
            QueryPerformanceCounter(&a);
            for (unsigned i = 0; i < 50; ++i) {
                hs2.owned_next = 1;
                run_window(1, 1200, 0);
            }
            QueryPerformanceCounter(&b);
            us = double(b.QuadPart - a.QuadPart) * 1e6 / double(freq.QuadPart);
        }
        std::sort(runs, runs + 21);
        std::printf("EVENT_COST events=50 shields=6 best_us=%.2f median_us=%.2f worst_us=%.2f target_us=30\n", runs[0],
                    runs[10], runs[20]);
        check(runs[10] <= 30.0, "event_cost_50_within_30us");
        // The overflow order at ring_capacity (1024) through the installed stub and handler (2026-11-07): the ring full of
        // stage-owned shield hits on 64 owners, the stage's stamp (core::ring_stamp_hidden) with every owner drawn but
        // one (its 16 records hidden). Each hit evicts the oldest hidden record first (the others stay); with none hidden
        // left the oldest drawn record goes, never a record acquired since the stamp.
        {
            core::ring_clear(hs2.ring);
            const std::uint32_t base = hs2.ring.next_sequence; // record i's sequence is base + i
            for (unsigned i = 0; i < core::ring_capacity; ++i) {
                core::Event* e = core::ring_acquire(hs2.ring);
                e->role = core::role_shield_hit;
                e->flags = core::flag_stage;
                e->owner = 0x4000u + (i & 63u);
            }
            const std::uint32_t hidden_owner = 0x4005u;
            core::VisibleOwners drawn;
            for (unsigned k = 0; k < 64; ++k)
                if (0x4000u + k != hidden_owner) drawn.add(0x4000u + k);
            core::ring_stamp_hidden(hs2.ring, drawn);
            auto count_owner = [&](std::uint32_t owner, bool match) {
                unsigned n = 0;
                for (unsigned i = 0; i < core::ring_capacity; ++i)
                    n += hs2.ring.slots[i].sequence && (hs2.ring.slots[i].owner == owner) == match &&
                         hs2.ring.slots[i].owner != u32(&victim_obj);
                return n;
            };
            auto live_seq = [&](std::uint32_t seq) {
                for (unsigned i = 0; i < core::ring_capacity; ++i)
                    if (hs2.ring.slots[i].sequence == seq) return true;
                return false;
            };
            unsigned stamped = 0;
            for (unsigned i = 0; i < core::ring_capacity; ++i) stamped += hs2.ring.hidden[i] & core::hidden_undrawn;
            const std::uint32_t overflow0 = hs2.ring.overflow, evicted_visible0 = hs2.ring.evicted_visible;
            const std::uint32_t oldest_hidden = base + 5, second_hidden = base + 69; // owner 0x4005's first two
            hs2.owned_next = 1;
            const RunResult first = run_window(1, 1200, 0);
            const bool first_ok = !live_seq(oldest_hidden) && live_seq(second_hidden) &&
                                  count_owner(hidden_owner, true) == 15 && count_owner(hidden_owner, false) == 1008 &&
                                  hs2.ring.overflow == overflow0 + 1 && frame_kept(first);
            for (unsigned k = 1; k < 16; ++k) {
                hs2.owned_next = 1;
                run_window(1, 1200, 0);
            }
            const bool hidden_gone = count_owner(hidden_owner, true) == 0 && count_owner(hidden_owner, false) == 1008 &&
                                     live_seq(base);
            hs2.owned_next = 1;
            const RunResult next = run_window(1, 1200, 0);
            const bool oldest_drawn = !live_seq(base) && live_seq(base + 1) && count_owner(hidden_owner, false) == 1007 &&
                                      ring_live(hs2.ring) == core::ring_capacity && hs2.ring.overflow == overflow0 + 17 &&
                                      frame_kept(next);
            // the frame row's evicted_visible= (Ring::evicted_visible): the 16 hidden evictions do not count, the 17th does
            const std::uint32_t evicted_visible = hs2.ring.evicted_visible - evicted_visible0;
            std::printf("OVERFLOW capacity=%u stamped_hidden=%u first_evicts_oldest_hidden=%u hidden_after_16=%u "
                        "drawn_kept_after_16=%u evicted_after=%s overflow=%lu evicted_visible=%lu\n",
                        core::ring_capacity, stamped, unsigned(first_ok), count_owner(hidden_owner, true),
                        unsigned(hidden_gone), oldest_drawn ? "oldest_drawn" : "other",
                        static_cast<unsigned long>(hs2.ring.overflow - overflow0), static_cast<unsigned long>(evicted_visible));
            check(stamped == 16 && first_ok && hidden_gone && oldest_drawn, "overflow_1024_hidden_owner_first_then_oldest");
            check(evicted_visible == 1, "overflow_counts_evicted_visible");
            // The handler's cost on a full ring: every event scans the 1024 slots for its victim (50 events, best and
            // median of 21, the same window as event_cost).
            double full[21];
            for (double& us : full) {
                QueryPerformanceCounter(&a);
                for (unsigned i = 0; i < 50; ++i) {
                    hs2.owned_next = 1;
                    run_window(1, 1200, 0);
                }
                QueryPerformanceCounter(&b);
                us = double(b.QuadPart - a.QuadPart) * 1e6 / double(freq.QuadPart);
            }
            std::sort(full, full + 21);
            std::printf("EVENT_COST_FULL events=50 capacity=%u best_us=%.2f median_us=%.2f worst_us=%.2f target_us=%.0f\n",
                        core::ring_capacity, full[0], full[10], full[20], event_cost_full_us);
            check(ring_live(hs2.ring) == core::ring_capacity && full[10] <= event_cost_full_us,
                  "event_cost_50_full_ring_1024");
        }
        core::ring_clear(hs2.ring);
        build_world(7, 0, 4, 0x0, 123456);
    }
    // The ring's scope at Present (note 3.1): the first known sector key and an unknown read keep the stage-owned
    // records; a different key, a load, a different load epoch clear them.
    {
        auto owned_hit = [&] {
            build_world(7, 0, 4, 0x0, 123456);
            hs2.owned_next = 1;
            std::memset(we_ca, 0, sizeof we_ca);
            run_window(1, 1200, 0);
            return we_ca[11] == 0 && core::ring_owned_live(hs2.ring); // the stage's record, the native spawn skipped
        };
        we::StageFrame idle{};
        bool keep = owned_hit();
        we::present(1, false, true, idle, core::Scope{0x111, 0, false});
        keep = keep && core::ring_owned_live(hs2.ring);
        we::present(2, false, true, idle, core::Scope{0, 0, false});
        keep = keep && core::ring_owned_live(hs2.ring);
        check(keep, "scope_first_key_and_unknown_keep");
        we::present(3, false, true, idle, core::Scope{0x222, 0, false});
        check(!core::ring_owned_live(hs2.ring), "scope_sector_change_clears_owned");
        const bool hit2 = owned_hit();
        we::present(4, false, true, idle, core::Scope{0x222, 0, true});
        check(hit2 && !core::ring_owned_live(hs2.ring), "scope_loading_clears_owned");
        const bool hit3 = owned_hit();
        we::present(5, false, true, idle, core::Scope{0x222, 7, false});
        const bool kept3 = core::ring_owned_live(hs2.ring);
        we::present(6, false, true, idle, core::Scope{0x222, 8, false});
        check(hit3 && kept3 && !core::ring_owned_live(hs2.ring), "scope_load_epoch_clears_owned");
    }

    // Step 2 (docs/archive/effects-look-2026-11-08/weapon-look.md 3.1): shields,hull. A stage-owned hull hit (the hull-through-shield row
    // absorbed, an unabsorbed hit, a drained shield, a missile source) is the stage's and skips the callee
    // (suppress_native_hull) with the frame as the caller expects; a shield hit is staged as in shields mode; a frame the
    // stage did not own and a dying hit object forward; suppress_native_hull cleared, the staged hull hit reaches the
    // callee with the call's state; the variants by the victim (asteroid) and by the table (repair); Present keeps a
    // staged hull hit life_floor_ms past its lifetime and releases it after.
    we::shutdown();
    set_setting(L"shields,hull");
    {
        // The setting (2026-11-08, the look layer scratched): native, observe or shields; the scratched families' names
        // beside shields in any order are ignored (reported in the mask: bit 0 hull, 1 bolts, 2 explosions) and the mode
        // is shields; a family name without shields, a repeated, empty or unknown token is refused. The family modes
        // themselves stay reachable through install_at only (the cases below).
        auto parses = [&](const char* text, core::Mode want, unsigned want_ignored) {
            core::Mode m = core::mode_native;
            unsigned ignored = 99;
            return core::parse_mode(text, std::strlen(text), &m, &ignored) && m == want && ignored == want_ignored;
        };
        auto refused = [&](const char* text) {
            core::Mode m = core::mode_native;
            return !core::parse_mode(text, std::strlen(text), &m);
        };
        const bool parse_ok = parses("shields,hull", core::mode_shields, 1) && parses("hull,shields", core::mode_shields, 1) &&
                              parses(" shields , hull ", core::mode_shields, 1) && parses("hull, shields", core::mode_shields, 1) &&
                              parses("shields", core::mode_shields, 0) && parses(" observe", core::mode_observe, 0) &&
                              parses("native", core::mode_native, 0) &&
                              parses("shields,hull,bolts", core::mode_shields, 3) &&
                              parses("bolts,hull,shields", core::mode_shields, 3) &&
                              parses("shields,hull,explosions", core::mode_shields, 5) &&
                              parses("explosions,bolts,hull,shields", core::mode_shields, 7) &&
                              parses("shields,bolts", core::mode_shields, 2) && refused("hull") &&
                              refused("bolts") && refused("explosions") && refused("hull,explosions") &&
                              refused("shields,shields") && refused("shields,hull,hull") && refused("shields,") &&
                              refused(",hull") && refused("observe,hull") && refused("native,bolts") &&
                              refused("Shields,hull") && refused("shields,beams") && refused("") &&
                              core::stage_mode(core::mode_shields_hull) && core::stage_mode(core::mode_shields) &&
                              !core::stage_mode(core::mode_observe) &&
                              !std::strcmp(core::mode_name(core::mode_shields_hull), "shields,hull") &&
                              !std::strcmp(core::ignored_name(0), "hull") && !std::strcmp(core::ignored_name(2), "explosions");
        check(parse_ok, "mode_parse_ignores_scratched_families");
        build_world(7, 0, 4, core::bullet_hull_through_shields, 123456);
        const bool hull_ok = we::install_at(addresses(), core::mode_shields_hull);
        core::HookState& h3 = we::hook_state();
        check(hull_ok && we::mode() == core::mode_shields_hull && impact_redirected() &&
                  h3.armed == (core::family_shields | core::family_hull) && h3.suppress_native_hull == 1 &&
                  h3.hull_lifetime_ms == core::hull_lifetime_ms && core::suppress_native_hull_hit,
              "hull_mode_installed");
        auto staged_run = [&](std::uint32_t absorbed, core::Event* out, RunResult* run) {
            h3.owned_next = 1;
            h3.hull_ready = 1;
            std::memset(we_ca, 0, sizeof we_ca);
            *run = run_window(absorbed, 1200, absorbed);
            return newest_record(u32(&victim_obj), out);
        };
        core::Event rec{};
        RunResult run{};
        const std::uint32_t skipped0 = h3.counters.skipped, forwarded0 = h3.counters.forwarded;
        bool ok = staged_run(1, &rec, &run);
        const bool through = ok && rec.role == core::role_hull_hit && (rec.flags & core::flag_stage) &&
                             !(rec.flags & core::flag_forwarded) && rec.lifetime_ms == core::hull_lifetime_ms &&
                             run.ca[11] == 0 && frame_kept(run);
        ok = staged_run(0, &rec, &run);
        const bool unabsorbed = ok && rec.role == core::role_hull_hit && (rec.flags & core::flag_stage) &&
                                !(rec.flags & core::flag_forwarded) && run.ca[11] == 0 && frame_kept(run);
        std::printf("HULL_MODE case=skip through=%u unabsorbed=%u skipped_delta=%lu forwarded_delta=%lu lifetime_ms=%u\n",
                    unsigned(through), unsigned(unabsorbed), static_cast<unsigned long>(h3.counters.skipped - skipped0),
                    static_cast<unsigned long>(h3.counters.forwarded - forwarded0), unsigned(rec.lifetime_ms));
        check(through && unabsorbed && h3.counters.skipped - skipped0 == 2 && h3.counters.forwarded == forwarded0,
              "hull_hit_skips_native_when_staged");
        check(protection(at(dmg_page)) == PAGE_EXECUTE_READWRITE && (run.out[5] & 0x400u) == (run.flags_in & 0x400u),
              "hull_skip_frame_kept");
        // A shield hit in this mode: staged and skipped as in shields mode, the patch's lifetime.
        build_world(7, 0, 4, 0x0, 123456);
        ok = staged_run(1, &rec, &run);
        check(ok && rec.role == core::role_shield_hit && (rec.flags & core::flag_stage) && run.ca[11] == 0 &&
                  rec.lifetime_ms == core::patch_lifetime_ms && frame_kept(run),
              "hull_mode_shield_hit_staged");
        // Not owned: forwarded, census only.
        build_world(7, 0, 4, core::bullet_hull_through_shields, 123456);
        h3.owned_next = 0;
        std::memset(we_ca, 0, sizeof we_ca);
        run = run_window(0, 1200, 1);
        ok = newest_record(u32(&victim_obj), &rec);
        check(ok && rec.role == core::role_hull_hit && !(rec.flags & core::flag_stage) && (rec.flags & core::flag_forwarded) &&
                  run.ca[11] == 1 && run.ca[9] == u32(&victim_obj) && run.ca[10] == u32(&source_obj) && frame_kept(run),
              "hull_hit_forwards_when_not_owned");
        // The pass without the hull programs (Present's hull_ready 0, review 5): the stage owns the frame for the shields
        // but the hull hit forwards.
        h3.owned_next = 1;
        h3.hull_ready = 0;
        std::memset(we_ca, 0, sizeof we_ca);
        run = run_window(0, 1200, 0);
        ok = newest_record(u32(&victim_obj), &rec);
        check(ok && rec.role == core::role_hull_hit && !(rec.flags & core::flag_stage) && (rec.flags & core::flag_forwarded) &&
                  run.ca[11] == 1 && frame_kept(run),
              "hull_hit_forwards_without_hull_programs");
        // A dying hit object forwards (the stage would retire it unseen).
        w32(&victim_obj, core::layout::obj_flags, core::layout::obj_dying);
        ok = staged_run(0, &rec, &run);
        check(ok && rec.role == core::role_hull_hit && !(rec.flags & core::flag_stage) && (rec.flags & core::flag_forwarded) &&
                  (rec.flags & core::flag_nested) && run.ca[11] == 1 && frame_kept(run),
              "hull_hit_dying_object_forwards");
        // The drained shield: a hull hit, now staged and skipped (drained_forwards_as_hull becomes staged).
        build_world(7, 0, 4, 0x0, 123456);
        {
            const std::int32_t one_cap[1] = {1000}, low[1] = {std::int32_t(std::int64_t(core::layout::shield_full) * 200 / 65535)};
            set_shields(1, one_cap, low);
        }
        ok = staged_run(1, &rec, &run);
        check(ok && rec.role == core::role_hull_hit && (rec.detail & core::detail_drained) && (rec.flags & core::flag_stage) &&
                  !(rec.flags & core::flag_forwarded) && (rec.flags & core::flag_absorbed) && run.ca[11] == 0 &&
                  frame_kept(run),
              "drained_hull_hit_staged");
        // suppress_native_hull cleared: staged and forwarded, the callee with the call's state.
        build_world(7, 0, 4, core::bullet_hull_through_shields, 123456);
        h3.suppress_native_hull = 0;
        ok = staged_run(0, &rec, &run);
        check(ok && (rec.flags & core::flag_stage) && (rec.flags & core::flag_forwarded) && run.ca[11] == 1 &&
                  run.ca[9] == u32(&victim_obj) && run.ca[10] == u32(&source_obj) &&
                  (run.ca[7] & flag_mask) == (run.flags_in & flag_mask) && run.ca[12] == we_frame_esp - 12u &&
                  run.ca[8] == std::uint32_t(impact_site() + sites::call_length) && frame_kept(run),
              "hull_hit_stage_and_forward");
        h3.suppress_native_hull = 1;
        // A missile source (class 10): staged, skipped, the missile table and style.
        build_world(7, 10, 4, 0x0, 123456);
        ok = staged_run(0, &rec, &run);
        check(ok && rec.role == core::role_hull_hit && rec.table == core::table_missiles &&
                  rec.style == core::shape_missile_hit && (rec.flags & core::flag_stage) && run.ca[11] == 0 && frame_kept(run),
              "hull_missile_source_staged");
        // The asteroid (class 17 victim): detail_asteroid.
        build_world(17, 0, 4, 0x0, 123456);
        ok = staged_run(0, &rec, &run);
        check(ok && rec.role == core::role_hull_hit && (rec.detail & core::detail_asteroid) &&
                  (rec.flags & core::flag_stage) && run.ca[11] == 0 && frame_kept(run),
              "hull_asteroid_variant");
        // The repair laser by the table's override (row_hull_repair; the tug likewise row_hull_tug).
        build_world(7, 0, 4, core::bullet_hull_through_shields, 123456);
        static core::Table override_table;
        override_table = core::Table{};
        override_table.rows = core::rd32(h3.globals.bullet_count_slot);
        override_table.bits[4] = std::uint8_t(core::row_present | core::row_hull_repair);
        const core::Table* saved_table = h3.table;
        h3.table = &override_table;
        ok = staged_run(1, &rec, &run);
        const bool repair = ok && (rec.detail & core::detail_repair) && !(rec.detail & core::detail_tug) &&
                            (rec.flags & core::flag_stage) && run.ca[11] == 0 && frame_kept(run);
        override_table.bits[4] = std::uint8_t(core::row_present | core::row_hull_tug);
        ok = staged_run(1, &rec, &run);
        const bool tug = ok && (rec.detail & core::detail_tug) && !(rec.detail & core::detail_repair) && frame_kept(run);
        h3.table = saved_table;
        check(repair && tug, "hull_repair_tug_variant_by_override");
        // The hull-hit class (docs/architecture/hull-impact-classes.md section 2) at the hook, into Event::detail bits 4-7:
        // one live record per cue of the rule (the flag words and damage of the stock rows it names), the round trip
        // through the ring and the variant bits beside it; then the table: an orb body, a class pin over the rule, a
        // pin over the repair bit, an out-of-range pin. Every run through the production stub (frame kept).
        {
            // shot: the bullet record's per-shot damage (+0x44 shield, +0x80 hull: the class's band); total: the applied
            // total the hook sees (a shields-down hull hit: its hull damage; 0 on a target that takes none)
            struct Cue {
                const char* name;
                std::uint32_t flags;
                std::int32_t shot_shield, shot_hull;
                std::uint32_t total;
                unsigned want;
                std::uint8_t variant;
            };
            const Cue cues[] = {{"tug", 0x2, 0, 0, 0, core::class_beam, core::detail_tug},
                                {"repair", 0xa2, 0, 40, 40, core::class_beam, core::detail_repair},
                                {"beam_pal", 0x2, 3969, 3969, 3969, core::class_beam, 0},
                                {"beam_invulnerable_target", 0x2, 3969, 3969, 0, core::class_beam, 0},
                                {"beam_iond", 0x214, 1575, 1575, 1575, core::class_beam, 0},
                                {"ion_esr", 0x400, 2671, 1335, 1335, core::class_ion, 0},
                                {"ion_ipg_frag", 0x300, 12563, 6281, 6281, core::class_ion, 0},
                                {"ion_ionc", 0x2600, 119148, 59574, 59574, core::class_ion, 0},
                                {"ballistic_mass", 0x60, 45, 45, 45, core::class_ballistic, 0},
                                {"ballistic_mam", 0x140, 8350, 4175, 4175, core::class_ballistic, 0},
                                {"heavy_gauss_shield_total", 0x40, 28540, 7135, 28540, core::class_heavy, 0},
                                {"heavy_gauss_hull_total", 0x40, 28540, 7135, 7135, core::class_heavy, 0},
                                {"plasma_wave_psg", 0x8, 1996, 998, 998, core::class_plasma, 0},
                                {"heavy_ppc_shield_total", 0x2000, 63778, 15944, 63778, core::class_heavy, 0},
                                {"heavy_ppc_hull_total", 0x2000, 63778, 15944, 15944, core::class_heavy, 0},
                                {"energy_ire", 0x0, 220, 110, 110, core::class_energy, 0},
                                {"energy_flak", 0x2100, 13860, 6930, 6930, core::class_energy, 0},
                                {"heavy_band_edge", 0x0, 20000, 5000, 5000, core::class_heavy, 0},
                                {"heavy_band_edge_hull", 0x0, 5000, 20000, 5000, core::class_heavy, 0},
                                {"energy_below_band", 0x0, 19999, 19999, 19999, core::class_energy, 0}};
            unsigned passed = 0, total_cues = 0;
            auto classed = [&](std::uint32_t total, core::Event* out) {
                h3.owned_next = 1;
                h3.hull_ready = 1;
                std::memset(we_ca, 0, sizeof we_ca);
                const RunResult r = run_window(0, total, 0);
                return newest_record(u32(&victim_obj), out) && frame_kept(r);
            };
            for (const Cue& c : cues) {
                build_world(7, 0, 4, c.flags, 123456);
                unsigned char* rec = bullet_table + 4 * core::layout::type_stride;
                w32(rec, core::layout::bullet_shield_damage, std::uint32_t(c.shot_shield));
                w32(rec, core::layout::bullet_hull_damage, std::uint32_t(c.shot_hull));
                core::Event e{};
                const bool ok = classed(c.total, &e) && core::hull_class_of(e) == c.want &&
                                (e.detail & (core::detail_repair | core::detail_tug)) == c.variant &&
                                (e.detail >> core::detail_class_shift) == c.want && e.role == core::role_hull_hit;
                passed += ok;
                ++total_cues;
                std::printf("HULL_CLASS case=%s flags=%#lx shot=%ld,%ld total=%lu class=%s want=%s detail=%#x ok=%u\n",
                            c.name, static_cast<unsigned long>(c.flags), static_cast<long>(c.shot_shield),
                            static_cast<long>(c.shot_hull), static_cast<unsigned long>(c.total),
                            core::hull_class_name(core::hull_class_of(e)), core::hull_class_name(c.want), unsigned(e.detail),
                            unsigned(ok));
            }
            check(passed == total_cues, "hull_class_from_record");
            // The table: an orb body (the elongation's shape) is plasma; a pin replaces the rule (MD: flags 0 -> ballistic;
            // a 0x40 row pinned ion); a class pin clears the rule's repair variant; an out-of-range pin reads energy; a
            // missile source keeps the nibble 0 (its style is missile_hit).
            static core::Table class_table;
            class_table = core::Table{};
            class_table.rows = core::rd32(h3.globals.bullet_count_slot);
            h3.table = &class_table;
            core::Event e{};
            build_world(7, 0, 4, 0x0, 123456);
            class_table.bits[4] = core::row_present;
            class_table.shape[4] = core::shape_orb;
            const bool orb = classed(5539, &e) && core::hull_class_of(e) == core::class_plasma;
            class_table.bits[4] = std::uint8_t(core::row_present | core::row_hull_class);
            class_table.shape[4] = core::shape_tracer;
            class_table.hull_class[4] = core::class_ballistic;
            const bool pin_md = classed(1716, &e) && core::hull_class_of(e) == core::class_ballistic &&
                                (e.flags & core::flag_override_applied);
            build_world(7, 0, 4, 0x40, 123456);
            class_table.hull_class[4] = core::class_ion;
            const bool pin_over_rule = classed(1716, &e) && core::hull_class_of(e) == core::class_ion;
            build_world(7, 0, 4, 0x80, 123456);
            class_table.hull_class[4] = core::class_energy;
            const bool pin_clears_repair = classed(40, &e) && core::hull_class_of(e) == core::class_energy &&
                                           !(e.detail & core::detail_repair);
            class_table.bits[4] = std::uint8_t(core::row_present | core::row_hull_class | core::row_hull_repair);
            const bool repair_pin = classed(40, &e) && (e.detail & core::detail_repair) &&
                                    core::hull_class_of(e) == core::class_energy;
            build_world(7, 0, 4, 0x0, 123456);
            class_table.bits[4] = std::uint8_t(core::row_present | core::row_hull_class);
            class_table.hull_class[4] = 9;
            const bool out_of_range = classed(1716, &e) && core::hull_class_of(e) == core::class_energy &&
                                      (e.detail >> core::detail_class_shift) == core::class_energy;
            build_world(7, 10, 4, 0x0, 123456);
            const bool missile = classed(30000, &e) && e.table == core::table_missiles && core::hull_class_of(e) == 0 &&
                                 (e.detail >> core::detail_class_shift) == 0;
            h3.table = saved_table;
            std::printf("HULL_CLASS case=table orb=%u pin_md=%u pin_over_rule=%u pin_clears_repair=%u repair_pin=%u "
                        "out_of_range=%u missile=%u\n",
                        unsigned(orb), unsigned(pin_md), unsigned(pin_over_rule), unsigned(pin_clears_repair),
                        unsigned(repair_pin), unsigned(out_of_range), unsigned(missile));
            check(orb && pin_md && pin_over_rule && pin_clears_repair && repair_pin && out_of_range && missile,
                  "hull_class_pins_and_body");
        }
        // Present's upkeep: a staged hull hit stays life_floor_ms past its lifetime, then leaves.
        build_world(7, 0, 4, core::bullet_hull_through_shields, 200000);
        ok = staged_run(0, &rec, &run);
        const std::uint32_t seq = rec.sequence;
        auto alive = [&]() {
            for (unsigned i = 0; i < core::ring_capacity; ++i)
                if (h3.ring.slots[i].sequence == seq) return true;
            return false;
        };
        we::StageFrame idle{};
        h3.life_floor_ms = 400;
        w32(config_block, core::layout::cfg_game_ms, 200000 + core::hull_lifetime_ms + 300);
        we::present(10, false, true, idle);
        const bool kept = alive();
        w32(config_block, core::layout::cfg_game_ms, 200000 + core::hull_lifetime_ms + 500);
        we::present(11, false, true, idle);
        const bool released = !alive();
        h3.life_floor_ms = 0;
        std::printf("HULL_MODE case=present kept=%u released=%u lifetime_ms=%u floor_ms=400\n", unsigned(kept),
                    unsigned(released), unsigned(core::hull_lifetime_ms));
        check(ok && kept && released, "hull_present_keeps_floor_then_releases");
    }
    // Observe-mode inner tap: the engine's &pos equals the handler's.
    we::shutdown();
    build_world(7, 0, 4, 0x0, 123456);
    set_setting(L"observe");
    we::install_at(addresses(), core::mode_observe);
    core::HookState& hs3 = we::hook_state();
    hs3.owned_next = 0;
    std::memset(we_ca, 0, sizeof we_ca);
    run_window(1, 1200, 0);
    std::int32_t engine_pos[3];
    expected_pos(engine_pos);
    we_tap_window_ptr = std::uint32_t(at(sites::tap_window_va[0]));
    std::uint32_t tap_args[6] = {3, 0xeff, u32(&victim_obj), u32(engine_pos), 0, 0};
    w32(&source_obj, 8, tap_projectile_id); // [ebx+8]: the projectile id the window loads into ESI before the call
    std::memset(we_tap_ca, 0, sizeof we_tap_ca);
    we_tap_enter(tap_args, u32(&source_obj));
    check(we_tap_ca[10] == 1 && we_tap_ca[8] == u32(&victim_obj) && we_tap_ca[9] == u32(engine_pos), "tap_reaches_callee");
    check(we_tap_ca[5] == tap_projectile_id && we_tap_ca[3] == u32(&source_obj) && we_tap_ca[6] == u32(&victim_obj) &&
              we_tap_ca[7] == std::uint32_t(at(sites::tap_site_va[0]) + sites::call_length),
          "tap_callee_esi_ebx_edi_return");
    check(hs3.check.compared == 1 && hs3.check.max_error == 0, "weapon_pos_check_zero_error");
    we::shutdown();

    set_setting(L"shields");
    auto* copy = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    check(copy != nullptr, "private_page");
    if (copy) {
        std::memcpy(copy, reinterpret_cast<const void*>(at(sites::impact_window_va & ~std::uintptr_t(0xfff))), 4096);
        const std::uintptr_t base = u32(copy);
        copy[(sites::impact_window_va & 0xfff) + 1] ^= 0xff; // the push esi
        we::Addresses on_copy = addresses();
        on_copy.impact_window = base + (sites::impact_window_va & 0xfff);
        SetLastError(sentinel_error);
        const bool refused = we::install_at(on_copy, core::mode_shields);
        check(!refused && !std::strcmp(we::state(), "bytes_mismatch_impact") && GetLastError() == sentinel_error &&
                  !we::installed(),
              "changed_byte_refused", we::state());
        VirtualFree(copy, 0, MEM_RELEASE);
    }

    // A changed frame witness or prologue byte refuses before any write (fail closed), LastError kept.
    flip(sites::witnesses[1].va);
    SetLastError(sentinel_error);
    bool refused_frame = !we::install_at(addresses(), core::mode_shields) && !std::strcmp(we::state(), "frame_mismatch");
    flip(sites::witnesses[1].va);
    check(refused_frame && GetLastError() == sentinel_error && !we::installed() && all_windows_original(),
          "witness_mismatch_refused", we::state());
    flip(sites::prologue_va + 5);
    refused_frame = !we::install_at(addresses(), core::mode_shields) && !std::strcmp(we::state(), "frame_mismatch");
    flip(sites::prologue_va + 5);
    check(refused_frame && !we::installed() && all_windows_original(), "prologue_mismatch_refused", we::state());
    // A changed tap byte refuses observe before any write.
    flip(sites::tap_window_va[1] + 2);
    SetLastError(sentinel_error);
    const bool refused_tap = !we::install_at(addresses(), core::mode_observe) && !std::strcmp(we::state(), "bytes_mismatch_tap");
    flip(sites::tap_window_va[1] + 2);
    check(refused_tap && GetLastError() == sentinel_error && !we::installed() && all_windows_original() &&
              !std::strcmp(we::site_state(2), "bytes_mismatch"),
          "tap_byte_mismatch_refused", we::state());
    // Observe claims three sites (tap1, tap2, then the impact): a failed read-back at the last restores the two before
    // it; one at the second restores the first and never reaches the impact.
    fault_read_at = impact_site();
    SetLastError(sentinel_error);
    bool multi = !we::install_at(addresses(), core::mode_observe);
    check(multi && fault_read_at == 0 && !we::installed() && all_windows_original() && GetLastError() == sentinel_error &&
              !std::strcmp(we::site_state(0), "readback_rolled_back") && !std::strcmp(we::site_state(1), "rolled_back") &&
              !std::strcmp(we::site_state(2), "rolled_back") && !std::strcmp(we::state(), "readback_rolled_back"),
          "observe_rollback_three_sites", we::site_state(0));
    fault_read_at = at(sites::tap_site_va[1]);
    multi = !we::install_at(addresses(), core::mode_observe);
    check(multi && fault_read_at == 0 && !we::installed() && all_windows_original() &&
              !std::strcmp(we::site_state(2), "readback_rolled_back") && !std::strcmp(we::site_state(1), "rolled_back") &&
              !std::strcmp(we::site_state(0), "not_attempted"),
          "observe_rollback_second_site", we::site_state(2));
    fault_read_at = 0;

    // Rollback on a failed read-back (the seam flips a byte of the post-write read): the site restored.
    fault_read_at = impact_site();
    SetLastError(sentinel_error);
    const bool rolled = we::install_at(addresses(), core::mode_shields);
    check(engine_patch::read_code(at(sites::impact_window_va), now, sites::impact_window_length) &&
              !std::memcmp(now, sites::expected_impact_window, sites::impact_window_length) && !rolled &&
              !we::installed() && faults_taken > 0 && GetLastError() == sentinel_error &&
              !std::strcmp(we::site_state(0), "readback_rolled_back"),
          "rollback_on_failed_readback", we::site_state(0));
    fault_read_at = 0;

    bolt_checks(); // step 4: the per-bullet tap, the muzzle and the flight-effect skip (while the window is still open)
    death_checks(); // step 3: the two death sites (while the window is still open)

    engine_patch::close_install_window("fixture");
    SetLastError(sentinel_error);
    const bool late = we::install_at(addresses(), core::mode_shields);
    check(!late && !std::strcmp(we::state(), "late_claim") && GetLastError() == sentinel_error && !we::installed(),
          "late_claim_refused");

    // The fixture's own stack margin: the stub and the handler stayed inside the 0x600 bytes below the frame (a deeper
    // write would corrupt the fixture's .bss, not the engine's: production runs on the game thread's stack).
    std::printf("STACK_DEPTH bytes=%u margin=%u\n", max_stack_depth, frame_offset);
    check(max_stack_depth > 0 && max_stack_depth + 0x100 <= frame_offset, "fixture_stack_margin");
    std::printf("RESULT %s checks=%u failures=%u faults=%u\n", failures ? "FAIL" : "PASS", checks, failures, faults_taken);
    return failures ? 1 : 0;
}
