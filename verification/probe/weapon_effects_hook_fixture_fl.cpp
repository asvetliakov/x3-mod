// Native fixture of the weapon-effects FL hooks (src/proxy/weapon_effects_patch.cpp through the force-included fault
// seam, src/proxy/engine_patch.cpp and src/proxy/weapon_effects_hook.cpp unchanged, X3M_EDITION_FL). Never launches the
// game; built and run by verification/probe/build_weapon_effects_hook_fl.py / run_weapon_effects.py, MinGW i686,
// natively through WSL interop (the FL engine-effects fixture's harness: a 64 KB-aligned private image stands in for
// X3FL.exe at `base`, the seven windows at base + RVA with the two relocated operands rebased to that base).
//
// Each site is entered through fl_enter: it sets EAX/EBX/ECX/EDX/ESI/EDI/EBP and pushes in_stack[0..n) as the frame the
// FL shim reads, then `std` (DF set) and jumps to the site's CALL (now the stub's). The stub runs the handler and
// forwards to a planted fake callee (plain RET, records every register/EFLAGS/ESP and the stack) or skips
// (`xor eax,eax; ret`); a planted jump at the site's return RVA lands both on fl_return, which records the caller-side
// state and restores. So every FL shim is checked against the FL register/stack contract (sections 2 and 4 of
// docs/reverse-engineering/fl/weapon-effect-spawn.md), with CPU flags, DF and LastError preserved.
#include "../../src/proxy/weapon_effects_patch.h"
#include "../../src/proxy/weapon_effects_core.h"
#include "../../src/proxy/weapon_effects_bolt.h"
#include "../../src/proxy/weapon_effects_bubble.h"
#include "../../src/engine/fl/weapon_effects_sites_fl.h"
#include "../../src/proxy/engine_patch.h"
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace we = x3m::weapon_effects;
namespace core = x3m::weapon_effects::core;
namespace S = x3m::fl::weapon_effects_fl;
namespace engine_patch = x3m::engine_patch;
namespace stage = x3m::weapon_effects::stage;

namespace {
unsigned checks = 0, failures = 0;
std::vector<std::string> log_lines;
std::uintptr_t fault_read_at = 0, fault_restore_at = 0;
bool fault_protect = false;
unsigned faults_taken = 0;
std::uintptr_t fixture_module_base = 0; // what fl::module_base() returns to the module (the fixture image base)
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
// The FL identity the module queries: the fixture image base, so install_base runs on the fixture's own windows.
namespace x3m::fl {
bool verified() {
    return fixture_module_base != 0;
}
std::uintptr_t module_base() {
    return fixture_module_base;
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
    if (ok && fault_read_at && address == fault_read_at && count == S::call_length) {
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

// ---------------------------------------------------------------------------- the site trampoline
extern "C" {
std::uint32_t in_eax, in_ebx, in_ecx, in_edx, in_esi, in_edi, in_ebp;
std::uint32_t in_nstack, in_stack[12], fl_site, saved_esp, esp_before_call, entry_flags;
std::uint32_t out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, out_esp, out_flags;
// The fake callee's record: entry registers, EFLAGS, ESP and nine stack dwords from [esp]; one counter.
std::uint32_t t_eax, t_ebx, t_ecx, t_edx, t_esi, t_edi, t_ebp, t_esp, t_flags, t_stack[9], t_calls;
void fl_enter();
void fl_return();
void fl_fake_callee();
}
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _fl_enter
_fl_enter:
    pushad
    mov dword ptr [_saved_esp], esp
    mov ecx, dword ptr [_in_nstack]
    lea esi, [_in_stack + ecx*4]
.Lpush:
    test ecx, ecx
    jz .Ldone
    sub esi, 4
    push dword ptr [esi]
    dec ecx
    jmp .Lpush
.Ldone:
    mov eax, dword ptr [_in_eax]
    mov ebx, dword ptr [_in_ebx]
    mov edx, dword ptr [_in_edx]
    mov esi, dword ptr [_in_esi]
    mov edi, dword ptr [_in_edi]
    mov ebp, dword ptr [_in_ebp]
    mov ecx, dword ptr [_in_ecx]
    mov dword ptr [_esp_before_call], esp
    push dword ptr [_entry_flags]
    popfd
    jmp dword ptr [_fl_site]
    .globl _fl_return
_fl_return:
    pushfd
    pop dword ptr [_out_flags]
    cld
    mov dword ptr [_out_eax], eax
    mov dword ptr [_out_ebx], ebx
    mov dword ptr [_out_ecx], ecx
    mov dword ptr [_out_edx], edx
    mov dword ptr [_out_esi], esi
    mov dword ptr [_out_edi], edi
    mov dword ptr [_out_ebp], ebp
    mov dword ptr [_out_esp], esp
    mov esp, dword ptr [_saved_esp]
    popad
    ret
    .globl _fl_fake_callee
_fl_fake_callee:
    mov dword ptr [_t_eax], eax
    mov dword ptr [_t_ebx], ebx
    mov dword ptr [_t_ecx], ecx
    mov dword ptr [_t_edx], edx
    mov dword ptr [_t_esi], esi
    mov dword ptr [_t_edi], edi
    mov dword ptr [_t_ebp], ebp
    mov dword ptr [_t_esp], esp
    pushfd
    pop dword ptr [_t_flags]
    mov eax, dword ptr [esp]
    mov dword ptr [_t_stack], eax
    mov eax, dword ptr [esp+4]
    mov dword ptr [_t_stack+4], eax
    mov eax, dword ptr [esp+8]
    mov dword ptr [_t_stack+8], eax
    mov eax, dword ptr [esp+12]
    mov dword ptr [_t_stack+12], eax
    mov eax, dword ptr [esp+16]
    mov dword ptr [_t_stack+16], eax
    mov eax, dword ptr [esp+20]
    mov dword ptr [_t_stack+20], eax
    mov eax, dword ptr [esp+24]
    mov dword ptr [_t_stack+24], eax
    mov eax, dword ptr [esp+28]
    mov dword ptr [_t_stack+28], eax
    mov eax, dword ptr [esp+32]
    mov dword ptr [_t_stack+32], eax
    inc dword ptr [_t_calls]
    mov eax, 0xca11ee00
    mov ecx, 0xc1c1c1c1
    mov edx, 0xd1d1d1d1
    ret
    .att_syntax
)");

namespace {
constexpr std::uint32_t sentinel_error = 0x2a2a1357;
constexpr std::uint32_t df_bit = 0x400, flag_mask = 0xcd5; // CF PF AF ZF SF DF OF
unsigned char* image = nullptr;
constexpr unsigned image_span = 0x00210000; // reaches every FL site and the body cache RVA 0x1aa100
std::uintptr_t base = 0;
std::uint32_t u32(const void* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
std::uint32_t u32f(void (*p)()) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
void writable(bool on) {
    DWORD old = 0;
    VirtualProtect(image, image_span, on ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ, &old);
}
void jump_at(std::uint32_t rva, void (*to)()) { // push imm32; ret
    unsigned char* p = image + rva;
    p[0] = 0x68;
    const std::uint32_t v = u32f(to);
    std::memcpy(p + 1, &v, 4);
    p[5] = 0xc3;
}
void lay_out() {
    // Phase 1 (identity, refusal, install, rollback): the seven windows pristine at base + RVA (the loader's relocation
    // applied to the two fixups), the fake callees at their targets (outside every window), and the frame witnesses.
    writable(true);
    std::memset(image, 0xcc, image_span);
    for (unsigned i = 0; i < S::site_count; ++i) {
        const S::Site& s = S::sites[i];
        S::live_bytes(s.window, base, image + s.window.rva);
        jump_at(s.target_rva, fl_fake_callee);
    }
    for (const S::Window& w : S::impact_witnesses) S::live_bytes(w, base, image + w.rva);
    for (const S::Window& w : S::muzzle_witnesses) S::live_bytes(w, base, image + w.rva);
    for (const S::Window& w : S::death_witnesses) S::live_bytes(w, base, image + w.rva);
    writable(false);
    FlushInstructionCache(GetCurrentProcess(), image, image_span);
}
// Every window back to its pristine engine bytes (the loader's relocation applied): undoes the planted returns and any
// test corruption, so install_base compares the engine bytes again.
void restore_windows() {
    writable(true);
    for (unsigned i = 0; i < S::site_count; ++i) S::live_bytes(S::sites[i].window, base, image + S::sites[i].window.rva);
    writable(false);
    FlushInstructionCache(GetCurrentProcess(), image, image_span);
}
// Phase 2 (ABI execution): a planted return jump right after each call, so a run executes only the five call bytes and
// returns to fl_return. This overwrites the window bytes past the call, so no window-identity check follows.
void plant_returns() {
    writable(true);
    for (unsigned i = 0; i < S::site_count; ++i)
        jump_at(S::sites[i].window.rva + S::sites[i].call_offset + S::call_length, fl_return);
    writable(false);
    FlushInstructionCache(GetCurrentProcess(), image, image_span);
}
std::uint32_t call_target(std::uint32_t site_rva) {
    std::int32_t rel = 0;
    std::memcpy(&rel, image + site_rva + 1, 4);
    return std::uint32_t(base + site_rva + 5 + std::uint32_t(rel));
}
bool site_redirected(unsigned i, void (*stub)()) {
    return call_target(S::sites[i].window.rva + S::sites[i].call_offset) == u32f(stub);
}
bool windows_original() {
    for (unsigned i = 0; i < S::site_count; ++i) {
        unsigned char now[S::max_window];
        std::memcpy(now, image + S::sites[i].window.rva, S::sites[i].window.length);
        if (!S::matches(S::sites[i].window, now, base)) return false;
    }
    return true;
}
void set_setting(const wchar_t* value) {
    SetEnvironmentVariableW(L"X3M_WEAPON_EFFECTS", value);
}

// ---------------------------------------------------------------------------- the world (FL globals)
struct alignas(16) Node {
    unsigned char bytes[0x280];
};
struct alignas(16) Obj {
    unsigned char bytes[0x100];
};
Node victim_node, source_node;
Obj victim_obj, source_obj;
std::uint32_t cfg_slot, bullet_t, bullet_c, ship_t, ship_c, shield_t, shield_c, missile_t, missile_c, body_root;
unsigned char config_block[0x800];
std::vector<unsigned char> bullet_table(16 * core::layout::type_stride);
std::vector<unsigned char> ship_table(8 * core::layout::type_stride);
std::vector<unsigned char> shield_table(8 * core::layout::type_stride);
// The victim's shields (spawn note 9): one chained node, then the sentinel.
alignas(16) unsigned char shield_ext_block[0x100];
alignas(16) unsigned char shield_node[4][0x60];
alignas(16) unsigned char shield_energy_block[4][0x10];
void w32(void* b, unsigned off, std::uint32_t v) {
    std::memcpy(static_cast<unsigned char*>(b) + off, &v, 4);
}
void w16(void* b, unsigned off, std::uint16_t v) {
    std::memcpy(static_cast<unsigned char*>(b) + off, &v, 2);
}
// Binds the handler's globals at the fixture's slots (install_base writes base + RVA; here we point those RVAs at heap).
void bind_globals() {
    core::Globals& g = we::hook_state().globals;
    g.cfg_slot = u32(&cfg_slot);
    g.bullet_table_slot = u32(&bullet_t);
    g.bullet_count_slot = u32(&bullet_c);
    g.ship_table_slot = u32(&ship_t);
    g.ship_count_slot = u32(&ship_c);
    g.shield_table_slot = u32(&shield_t);
    g.shield_count_slot = u32(&shield_c);
    g.missile_table_slot = u32(&missile_t);
    g.missile_count_slot = u32(&missile_c);
    g.body_cache_slot = u32(&body_root);
    we::bolt_hook_state().globals = g;
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
    w32(&source_obj, core::layout::obj_ext, 0x00000010u); // unmapped: [source+0x50] must not be followed
    for (Node* n : {&victim_node, &source_node}) {
        w32(n, core::layout::node_live, 1);
        w32(n, core::layout::node_handle, 0xd00d);
        for (unsigned k = 0; k < 3; ++k) w32(n, core::layout::node_rot + 16 * k + 4 * k, 0x10000);
    }
    w32(&source_node, core::layout::node_pos + 0, 7000);
    w32(&source_node, core::layout::node_pos + 8, 1500);
    w32(&source_node, core::layout::node_contact + 0, 0x8000);
    std::fill(bullet_table.begin(), bullet_table.end(), (unsigned char)0);
    unsigned char* rec = bullet_table.data() + std::size_t(subtype) * core::layout::type_stride;
    w32(rec, core::layout::bullet_flags, flags);
    w32(rec, core::layout::bullet_light + 0, 230);
    w32(rec, core::layout::bullet_light + 4, 60);
    w32(rec, core::layout::bullet_light + 8, 255);
    w32(rec, core::layout::bullet_shield_damage, 1200);
    w32(rec, core::layout::bullet_hull_damage, 300);
    std::memset(config_block, 0, sizeof config_block);
    w32(config_block, core::layout::cfg_game_ms, game_ms);
    cfg_slot = u32(config_block);
    bullet_t = u32(bullet_table.data());
    bullet_c = 16;
    std::fill(ship_table.begin(), ship_table.end(), (unsigned char)0);
    w32(ship_table.data() + 4 * core::layout::type_stride, core::layout::ship_class_id, 2023); // M3
    ship_t = u32(ship_table.data());
    ship_c = 8;
    missile_t = u32(bullet_table.data());
    missile_c = 16;
    shield_t = 0;
    shield_c = 0;
    bind_globals();
}
void set_shields(std::int32_t cap, std::int32_t energy) {
    std::memset(shield_ext_block, 0, sizeof shield_ext_block);
    std::memset(shield_node, 0, sizeof shield_node);
    std::memset(shield_energy_block, 0, sizeof shield_energy_block);
    std::fill(shield_table.begin(), shield_table.end(), (unsigned char)0);
    w32(shield_node[0], core::layout::shield_next, u32(shield_node[1]));
    w16(shield_node[0], core::layout::shield_index, 0);
    w32(shield_node[0], core::layout::shield_ext, u32(shield_energy_block[0]));
    w32(shield_energy_block[0], core::layout::shield_energy, std::uint32_t(energy));
    w32(shield_table.data(), core::layout::shield_capacity, std::uint32_t(cap));
    w16(shield_node[1], core::layout::shield_index, 0x7fff);
    w32(shield_node[1], core::layout::shield_ext, 0x00000010u);
    w32(shield_ext_block, core::layout::ext_shield_list, u32(shield_node[0]));
    w32(&victim_obj, core::layout::obj_ext, u32(shield_ext_block));
    shield_t = u32(shield_table.data());
    shield_c = 8;
}

// ---------------------------------------------------------------------------- running a site
struct Entry {
    std::uint32_t eax = 0xa1a1a1a1, ebx = 0x5b5b5b5b, ecx = 0xc1c1c1c1, edx = 0xd1d1d1d1, esi = 0x5e5e5e5e,
                  edi = 0xed1ed1ed, ebp = 0xeb9eb9eb, df = 1;
    std::uint32_t stack[12]{};
    unsigned nstack = 0;
};
void run_site(unsigned i, const Entry& e) {
    in_eax = e.eax;
    in_ebx = e.ebx;
    in_ecx = e.ecx;
    in_edx = e.edx;
    in_esi = e.esi;
    in_edi = e.edi;
    in_ebp = e.ebp;
    in_nstack = e.nstack;
    for (unsigned k = 0; k < e.nstack; ++k) in_stack[k] = e.stack[k];
    entry_flags = 0x8c5u | (e.df ? df_bit : 0u); // CF PF ZF SF OF, DF per case
    fl_site = std::uint32_t(base + S::sites[i].window.rva + S::sites[i].call_offset);
    t_calls = 0;
    SetLastError(sentinel_error);
    fl_enter();
}
bool newest_record(std::uint32_t owner, core::Event* out) {
    const core::Ring& r = we::hook_state().ring;
    std::uint32_t best = 0;
    for (unsigned k = 0; k < core::ring_capacity; ++k)
        if (r.slots[k].sequence > best && r.slots[k].owner == owner) {
            best = r.slots[k].sequence;
            *out = r.slots[k];
        }
    return best != 0;
}
}

namespace {
void (*fl_stub(unsigned i))() {
    switch (i) {
    case 0: return x3m_weapon_effects_stub_impact_fl;
    case 1:
    case 2: return x3m_weapon_effects_stub_tap_fl;
    case 3: return x3m_weapon_effects_stub_bolt_tap_fl;
    case 4: return x3m_weapon_effects_stub_muzzle_fl;
    case 5: return x3m_weapon_effects_stub_flight_fl;
    default: return x3m_weapon_effects_stub_death_fl;
    }
}
// The stub preserved the caller's state: EBX/ESI/EDI/EBP (the stub saves only EAX/ECX/EDX), DF and LastError. ESP is
// not compared here (the planted return skips the engine caller's own argument pop; the forward checks prove the callee
// saw [esp] = the return address, and the skip checks EAX = 0).
bool caller_kept(const Entry& e) {
    return GetLastError() == sentinel_error && out_ebx == e.ebx && out_esi == e.esi && out_edi == e.edi &&
           out_ebp == e.ebp && (out_flags & df_bit) == df_bit;
}
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_span, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    base = reinterpret_cast<std::uintptr_t>(image);
    fixture_module_base = base;
    check(image && !(base & 0xffff), "image_64k_aligned");
    if (!image) return 1;
    lay_out();

    bool fnv_ok = true, call_ok = true;
    for (unsigned i = 0; i < S::site_count; ++i) {
        const S::Site& s = S::sites[i];
        fnv_ok = fnv_ok && S::fnv1a(s.window.bytes, s.window.length) == s.window.fnv;
        call_ok = call_ok && call_target(s.window.rva + s.call_offset) == std::uint32_t(base + s.target_rva);
    }
    check(fnv_ok, "window_hashes_measured");
    check(call_ok, "window_calls_reach_callees");

    check(core::fl_absorbed(1, 1200, 1200, 0, 0) == 1, "absorbed_esi1_total_matches");
    check(core::fl_absorbed(0, 1200, 1200, 0, 0) == 0, "absorbed_esi0");
    check(core::fl_absorbed(1, 1300, 1200, 0, 0) == 0, "absorbed_hull_total_differs");
    check(core::fl_absorbed(1, 1200, 1200, 5, 0) == 0, "absorbed_remaining_nonzero");
    check(core::fl_absorbed(1, 50000, 60000, 0, core::fl_obj_piloted) == 0, "absorbed_piloted_clamp_ambiguous_native");
    check(core::fl_absorbed(1, 40000, 40000, 0, core::fl_obj_piloted) == 1, "absorbed_piloted_below_clamp");

    build_world(7, 0, 4, 0x0, 100000);
    check(core::fl_death_site(u32(&victim_obj), we::hook_state().globals) == 1, "death_site_tag_other");
    w32(&victim_obj, core::layout::obj_flags, core::obj_area_candidate);
    w32(bullet_table.data() + 4u * core::layout::type_stride, core::layout::bullet_flags, core::bullet_area_flag);
    check(core::fl_death_site(u32(&victim_obj), we::hook_state().globals) == 0, "death_site_tag_area");

    // A mode that uses each window (taps only in observe, the bolts in a bolts mode, death in an explosions mode): a
    // flipped window that the mode does not use would be skipped by the compare, so each is flipped under a using mode.
    const auto mode_for = [](unsigned i) {
        return i == 0 ? core::mode_shields
               : i < 3 ? core::mode_observe
               : i < 6 ? core::mode_shields_hull_bolts
                       : core::mode_shields_hull_explosions;
    };
    unsigned flips = 0, refused = 0, fixup_flips = 0, fixup_refused = 0;
    for (unsigned i = 0; i < S::site_count; ++i) {
        const S::Site& s = S::sites[i];
        for (unsigned bit = 0; bit < s.window.length * 8; ++bit) {
            writable(true);
            image[s.window.rva + bit / 8] ^= (unsigned char)(1u << (bit % 8));
            writable(false);
            SetLastError(sentinel_error);
            const bool ok = we::install_base(base, mode_for(i));
            const bool fixup = s.window.fixup_offset != S::no_fixup && bit / 8 >= s.window.fixup_offset &&
                               bit / 8 < s.window.fixup_offset + 4;
            const bool pass = !ok && GetLastError() == sentinel_error && !we::installed();
            writable(true);
            image[s.window.rva + bit / 8] ^= (unsigned char)(1u << (bit % 8));
            writable(false);
            ++flips;
            refused += pass;
            fixup_flips += fixup;
            fixup_refused += fixup && pass;
        }
    }
    std::printf("FLIPS total=%u refused=%u fixup=%u fixup_refused=%u\n", flips, refused, fixup_flips, fixup_refused);
    check(refused == flips && fixup_refused == fixup_flips && fixup_flips == 64, "every_window_bit_refused");
    check(windows_original(), "windows_restored_after_flips");

    // A rebased fixup (the object tap's gate 0x1a82ec, the muzzle's table 0x1a83c8 at another base) is refused, and the
    // install writes nothing (the corrupted window stays as I left it).
    for (unsigned i : {3u, 4u}) {
        const S::Site& s = S::sites[i];
        const std::uint32_t other = std::uint32_t(base + 0x10000 + s.window.fixup_rva);
        writable(true);
        std::memcpy(image + s.window.rva + s.window.fixup_offset, &other, 4);
        writable(false);
        SetLastError(sentinel_error);
        const bool refused_ok = !we::install_base(base, core::mode_shields_hull_bolts_explosions) && !we::installed() &&
                                GetLastError() == sentinel_error &&
                                !std::strcmp(we::state(), i == 3 ? "bytes_mismatch_bolt_tap" : "bytes_mismatch_muzzle") &&
                                call_target(s.window.rva + s.call_offset) == std::uint32_t(base + s.target_rva);
        check(refused_ok, i == 3 ? "bolt_tap_fixup_other_base_refused" : "muzzle_fixup_other_base_refused", we::state());
        writable(true);
        S::live_bytes(s.window, base, image + s.window.rva);
        writable(false);
    }

    // Install every family (shields,hull,bolts,explosions): the used sites (impact, object tap, muzzle, flight, death)
    // active and redirected, the taps not used; globals bound at base + RVA; write paths atomic except the object tap.
    const auto used_here = [](unsigned i) { return i == 0 || i >= 3; };
    SetLastError(sentinel_error);
    const bool ok = we::install_base(base, core::mode_shields_hull_bolts_explosions);
    bool all_active = ok && we::installed() && GetLastError() == sentinel_error && !std::strcmp(we::state(), "ok");
    for (unsigned i = 0; i < S::site_count; ++i)
        all_active = all_active && (used_here(i) ? (!std::strcmp(we::site_state(i), "active") && site_redirected(i, fl_stub(i)))
                                                 : !std::strcmp(we::site_state(i), "not_used"));
    check(all_active, "installed_all_sites_active", we::state());
    const core::Globals& g = we::hook_state().globals;
    check(g.cfg_slot == base + S::cfg_slot_rva && g.bullet_table_slot == base + S::type_table_rva &&
              g.bullet_count_slot == base + S::type_count_rva && g.ship_table_slot == base + S::type_table_rva + 4 * 7 &&
              g.shield_count_slot == base + S::type_count_rva + 4 * 9 && g.body_cache_slot == base + S::body_cache_rva,
          "globals_bound_base_plus_rva");
    bool writes_ok = true;
    for (unsigned i = 0; i < S::site_count; ++i)
        writes_ok = writes_ok && !std::strcmp(we::write_path(i), !used_here(i) ? "none" : i == 3 ? "plain" : "atomic");
    check(writes_ok, "write_paths_atomic_except_object_tap");

    // Shutdown restores every site.
    check(we::shutdown() && windows_original() && !we::installed(), "shutdown_restores_all");

    // Rollback: a refused read-back at the object tap (the last claimed) restores every earlier site.
    fault_read_at = base + S::sites[3].window.rva + S::sites[3].call_offset;
    SetLastError(sentinel_error);
    const bool rb = we::install_base(base, core::mode_shields_hull_bolts_explosions);
    check(!rb && GetLastError() == sentinel_error && !we::installed() && windows_original(), "rollback_restores_all");
    fault_read_at = 0;

    // initialize() rows: native (nothing claimed), then shields (one weapon_effects_patch row per site, edition=fl).
    set_setting(L"native");
    log_lines.clear();
    SetLastError(sentinel_error);
    check(!we::initialize() && GetLastError() == sentinel_error && !we::installed(), "init_native");
    set_setting(L"shields");
    log_lines.clear();
    check(we::initialize() && we::installed() && we::mode() == core::mode_shields, "init_shields_installs");
    unsigned fl_rows = 0;
    for (const std::string& l : log_lines)
        if (l.find("weapon_effects_patch site=") != std::string::npos && l.find(" edition=fl ") != std::string::npos) ++fl_rows;
    check(fl_rows == S::site_count, "init_logs_one_row_per_site");
    we::shutdown();

    // Phase 2a (observe: the impact and the two taps): install, plant the return jumps (they overwrite the window bytes
    // past each call), bind the globals at the fixture's heap tables, and drive the impact and tap shims.
    restore_windows();
    SetLastError(sentinel_error);
    check(we::install_base(base, core::mode_observe) && we::installed() && we::mode() == core::mode_observe,
          "reinstall_observe_for_runs");
    plant_returns();
    build_world(7, 0, 4, 0x0, 100000); // override the garbage image slots with controlled heap tables
    core::HookState& hs = we::hook_state();
    core::BoltHookState& bs = we::bolt_hook_state();
    bs.armed = 1;

    // Impact forward (observe, no stage): ECX/EDX/EBP/ESI published, forwards to the callee with the registers/stack/
    // flags/DF and LastError kept; one absorbed shield-hit record.
    hs.owned_next = 0;
    set_shields(1000000, core::layout::shield_full);
    {
        Entry e;
        e.ecx = u32(&victim_obj);
        e.edx = u32(&source_obj);
        e.ebp = 1200;
        e.esi = 1;
        e.nstack = 7;
        e.stack[0] = 0xdddddddd;
        e.stack[5] = 1200;
        e.stack[6] = 0;
        run_site(0, e);
        check(caller_kept(e) && out_eax == 0xca11ee00u, "impact_forward_caller_kept");
        check(t_calls == 1 && t_ecx == u32(&victim_obj) && t_edx == u32(&source_obj) &&
                  t_stack[0] == std::uint32_t(base + S::sites[0].window.rva + S::sites[0].call_offset + 5),
              "impact_forward_reaches_callee_ecx_edx_return");
        check((t_flags & flag_mask) == (entry_flags & flag_mask), "impact_forward_callee_eflags");
        core::Event rec{};
        check(newest_record(u32(&victim_obj), &rec) && rec.role == core::role_shield_hit && rec.damage == 1200 &&
                  (rec.flags & core::flag_absorbed) && (rec.flags & core::flag_forwarded),
              "impact_forward_records_shield_hit_absorbed");
    }
    // Taps (observe forwards always): [esp+8] hit, [esp+0xc] &pos, ECX/EDX forwarded.
    hs.owned_next = 0;
    for (unsigned i : {1u, 2u}) {
        Entry e;
        e.nstack = 4;
        e.stack[0] = 0xeeeeeeee;
        e.stack[1] = u32(&victim_obj);
        e.stack[2] = 0;
        run_site(i, e);
        check(caller_kept(e) && t_calls == 1 && t_ecx == e.ecx && t_edx == e.edx,
              i == 1 ? "tap1_forwards_ecx_edx" : "tap2_forwards_ecx_edx");
    }

    // Phase 2b (shields,hull,bolts,explosions: the impact skip, the object tap, the muzzle, the flight, the death):
    // shut the observe install, reinstall the family windows, plant the returns, and rebind the globals.
    we::shutdown();
    restore_windows();
    SetLastError(sentinel_error);
    check(we::install_base(base, core::mode_shields_hull_bolts_explosions) && we::installed(), "reinstall_family_for_runs");
    plant_returns();
    build_world(7, 0, 4, 0x0, 100000);
    bs.armed = 1;
    set_shields(1000000, core::layout::shield_full);
    // Impact skip (staged shield hit): EAX = 0, the callee never entered.
    core::ring_clear(hs.ring);
    hs.owned_next = 1;
    hs.armed = core::family_shields | core::family_hull | core::family_bolts | core::family_boom;
    hs.suppress_native = 1;
    {
        Entry e;
        e.ecx = u32(&victim_obj);
        e.edx = u32(&source_obj);
        e.ebp = 1200;
        e.esi = 1;
        e.nstack = 7;
        e.stack[5] = 1200;
        run_site(0, e);
        check(caller_kept(e) && out_eax == 0 && t_calls == 0, "impact_skip_no_callee_eax_zero");
        core::Event rec{};
        check(newest_record(u32(&victim_obj), &rec) && (rec.flags & core::flag_stage) && !(rec.flags & core::flag_forwarded),
              "impact_skip_record_staged");
    }


    // Object tap (bolt_tap): ECX = object, forwarded. The bolt handler follows the bullet extension, so give it a null
    // one (the impact no-follow test's 0x10 is unmapped).
    w32(&source_obj, core::layout::obj_ext, 0);
    {
        Entry e;
        e.ecx = u32(&source_obj);
        e.nstack = 0;
        run_site(3, e);
        check(caller_kept(e) && t_calls == 1 && t_ecx == u32(&source_obj), "object_tap_forwards_ecx");
    }

    // Muzzle: EDX slot, [esp+8] ship, [esp+0x18] &pos, [esp+0x20] &orient, ESI record offset; forward / skip.
    static std::int32_t pos_block[4] = {1, 2, 3, 0};
    static std::int32_t orient_block[12] = {0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x10000, 0, 0, 0};
    for (int owned = 0; owned < 2; ++owned) {
        bs.muzzle_owned = std::uint8_t(owned);
        bs.suppress_muzzle = 1;
        Entry e;
        e.edx = 2;
        e.esi = 4u * core::layout::type_stride;
        e.nstack = 9;
        e.stack[1] = u32(&victim_obj);
        e.stack[5] = u32(pos_block);
        e.stack[7] = u32(orient_block);
        run_site(4, e);
        if (owned)
            check(caller_kept(e) && out_eax == 0 && t_calls == 0, "muzzle_skip_no_callee");
        else
            check(caller_kept(e) && t_calls == 1 && t_stack[2] == u32(&victim_obj), "muzzle_forward_reaches_callee_ship");
    }

    // Flight: [esp+8] bullet; forward / skip.
    for (int owned = 0; owned < 2; ++owned) {
        bs.owned = std::uint8_t(owned);
        bs.suppress_flight = 1;
        Entry e;
        e.nstack = 2;
        e.stack[1] = u32(&source_obj);
        run_site(5, e);
        if (owned)
            check(caller_kept(e) && out_eax == 0 && t_calls == 0, "flight_skip_no_callee");
        else
            check(caller_kept(e) && t_calls == 1 && t_stack[2] == u32(&source_obj), "flight_forward_reaches_callee_bullet");
    }

    // Death (one FL site, both roles): ECX obj, EDX killer, [esp+4] arg4, reason from [EBP+8]; census when not owned.
    build_world(7, 0, 4, 0x0, 100000);
    static std::uint32_t kill_frame[8] = {0, 0, 0x002a, 0xdeadbeef, 0x44332211};
    hs.owned_next = 0;
    {
        Entry e;
        e.ecx = u32(&victim_obj);
        e.edx = 0xdeadbeef;
        e.ebp = u32(kill_frame);
        e.nstack = 1;
        e.stack[0] = 0x44332211;
        run_site(6, e);
        check(caller_kept(e) && t_calls == 1 && t_ecx == u32(&victim_obj) && t_edx == 0xdeadbeef &&
                  t_stack[1] == 0x44332211u,
              "death_forward_reaches_callee_obj_killer_arg4");
        core::Event rec{};
        check(newest_record(u32(&victim_obj), &rec) && rec.role == core::role_explosion && rec.tint_source == 1 &&
                  rec.shield_q == 0x2a,
              "death_forward_records_other_tag_reason");
    }

    // The ship bubble's hull walk on FL: a root with one part whose body id sits at node +0x144, reachable through the
    // body-cache hash; hull_bounds must read the FL node body offset and succeed.
    {
        alignas(16) static unsigned char root[0x280], child[0x280], sentinel[0x280], hash_obj[0x20];
        static std::uint32_t entry3[3], buckets[4], hashhead[2], lod_ptrs[1], root_slot, body_cache_slot;
        alignas(16) static unsigned char model[0x40], lod[0x40];
        std::memset(root, 0, sizeof root);
        std::memset(child, 0, sizeof child);
        std::memset(sentinel, 0, sizeof sentinel);
        std::memset(hash_obj, 0, sizeof hash_obj);
        std::memset(buckets, 0, sizeof buckets);
        std::memset(model, 0, sizeof model);
        std::memset(lod, 0, sizeof lod);
        w32(root, core::layout::node_children, u32(child));
        w32(child, 0, u32(sentinel)); // child.next -> sentinel (non-zero: a real node)
        w32(sentinel, 0, 0);          // sentinel.next = 0 : the list ends
        w32(child, core::layout::node_body, 5); // the body id at FL's +0x144
        for (unsigned k = 0; k < 3; ++k) w32(child, core::layout::node_basis + 0x10 * k + 4 * k, 0x10000);
        w32(child, core::layout::node_scale, 0x10000);
        for (unsigned k = 0; k < 3; ++k) w32(child, core::layout::node_axis_scale + 4 * k, 0x10000);
        for (unsigned k = 0; k < 3; ++k) w32(lod, core::layout::lod_extent + 4 * k, 0x1000);
        lod_ptrs[0] = u32(lod);
        w32(model, core::layout::model_lods, u32(lod_ptrs));
        w16(model, 0x10, 1);
        entry3[0] = 0;
        entry3[1] = 5 + 1;
        entry3[2] = u32(model);
        buckets[6u & 3u] = u32(entry3);
        hashhead[0] = u32(buckets);
        hashhead[1] = 4;
        w32(hash_obj, core::layout::body_cache_hash, u32(hashhead));
        root_slot = u32(hash_obj);
        body_cache_slot = u32(&root_slot);
        stage::HullBox box;
        bool extent = false;
        auto rd = [](std::uintptr_t a, void* out, std::size_t n) { return x3m::engine_memory::read(a, out, n); };
        const bool walked = stage::hull_bounds(rd, body_cache_slot, u32(root), &box, &extent);
        check(walked && box.parts == 1 && !extent, "fl_bubble_walk_reads_node_body_0x144");
        check(core::layout::node_body == 0x144 && core::layout::node_walk_bytes == 0x148, "fl_node_body_offset");
    }

    // The late window, last (closing it is permanent; the planted returns leave the windows non-pristine, so only the
    // state is checked).
    we::shutdown();
    engine_patch::close_install_window("fl-fixture-late");
    SetLastError(sentinel_error);
    const bool late = we::install_base(base, core::mode_shields_hull_bolts_explosions);
    check(!late && GetLastError() == sentinel_error && !we::installed() && !std::strcmp(we::state(), "late_claim") &&
              !std::strcmp(we::site_state(0), "late_claim"),
          "late_claim_refused");



    std::printf("STACK_DEPTH ok=1\n");
    std::printf("RESULT %s checks=%u failures=%u faults=%u\n", failures ? "FAIL" : "PASS", checks, failures, faults_taken);
    return failures ? 1 : 0;
}
