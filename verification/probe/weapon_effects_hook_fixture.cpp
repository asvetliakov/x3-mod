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
                      low.role == core::role_hull_hit && (low.detail & core::detail_drained) &&
                      (low.flags & core::flag_absorbed) && (low.flags & core::flag_forwarded) &&
                      !(low.flags & core::flag_stage) && low_run.ca[11] == 1 && drained_low == 1 && forwarded_low == 1 &&
                      frame_kept(low_run),
                  "drained_shield_hit_forwards_as_hull_hit");
            check(high_ok && (high.flags & core::flag_shield_read) && high.shield_q > core::drained_shield_q &&
                      high.role == core::role_shield_hit && !high.detail && (high.flags & core::flag_stage) &&
                      !(high.flags & core::flag_forwarded) && high_run.ca[11] == 0 && frame_kept(high_run),
                  "shield_hit_above_drained_threshold_staged");
            check(unread_ok && !(unread.flags & core::flag_shield_read) && unread.role == core::role_shield_hit &&
                      !unread.detail && (unread.flags & core::flag_stage) && unread_run.ca[11] == 0 &&
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
                      b327.shield_q == 327 && b327.role == core::role_shield_hit && !b327.detail &&
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
