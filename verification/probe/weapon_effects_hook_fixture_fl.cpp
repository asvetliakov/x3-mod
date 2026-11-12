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
// docs/reverse-engineering/fl/weapon-effect-spawn.md), with CPU flags, DF and LastError preserved. The projectile
// stage's batch submit 0x097af8 (docs/reverse-engineering/fl/bullet-draw-batch.md section 5) is driven with a fake batch
// record: skipped (EAX 1, no callee) when its body id is in the enabled set while the latch is on, forwarded with every
// register, the stack and EFLAGS otherwise; its read-back fault rolls every site back; the object tap registers a
// bullet of an enabled body into the projectile pool.
#include "../../src/proxy/weapon_effects_patch.h"
#include "../../src/proxy/weapon_effects_core.h"
#include "../../src/proxy/weapon_effects_bolt.h"
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
// The private stack every run executes on (filled with stack_fill before a run, to measure how deep the stub and the
// handler wrote below the call; the AP fixture's STACK_DEPTH).
alignas(16) unsigned char fl_stack[0x1000];
std::uint32_t fl_stack_top;
std::uint32_t out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, out_esp, out_flags;
// The fake callee's record: entry registers, EFLAGS, ESP and nine stack dwords from [esp]; one counter.
std::uint32_t t_eax, t_ebx, t_ecx, t_edx, t_esi, t_edi, t_ebp, t_esp, t_flags, t_stack[9], t_calls;
std::uint32_t t_scope; // x3m_bullet_scope as the callee saw it (the bullet re-shade's scope stub)
std::uint32_t t_body;  // x3m_bullet_body as the callee saw it (the batch's body id the scope stub published)
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
    mov esp, dword ptr [_fl_stack_top]
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
    mov eax, dword ptr [_x3m_bullet_scope]
    mov dword ptr [_t_scope], eax
    mov eax, dword ptr [_x3m_bullet_body]
    mov dword ptr [_t_body], eax
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
std::uint32_t cfg_slot, bullet_t, bullet_c, ship_t, ship_c, shield_t, shield_c, missile_t, missile_c;
// The ship profiles the shield stage needs since the walk's removal (5.1.1 "Placement", user decision 2026-11-12 on FL
// 69 A: a ship without a usable profile keeps the native effect): TShips row 4 (the default victim, an M3) with its
// class, the generator's row count the fixture's eight; bound with the globals.
core::ShipProfiles fixture_profiles;
void bind_profiles() {
    fixture_profiles.clear();
    fixture_profiles.tships_rows = 8;
    core::ShipProfile& p = fixture_profiles.entries[0];
    p.row = 4;
    p.class_id = 2023;
    p.z0 = -1000.f;
    p.z1 = 1000.f;
    for (auto& slice : p.slice) {
        slice[0] = slice[1] = 0.f;
        slice[2] = slice[3] = 500.f;
    }
    fixture_profiles.slot_of[4] = 1;
    fixture_profiles.count = 1;
    we::hook_state().profiles = &fixture_profiles;
}
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
    we::bolt_hook_state().globals = g;
    bind_profiles();
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
        for (unsigned k = 0; k < 3; ++k) w32(n, core::layout::node_axis_scale + 4 * k, 0x10000);
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
constexpr unsigned char stack_fill = 0x5a;
unsigned max_stack_depth = 0, deepest_site = 0;
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
    std::memset(fl_stack, stack_fill, sizeof fl_stack);
    fl_stack_top = u32(fl_stack + 0xc00);
    fl_enter();
    const unsigned char* lowest = fl_stack;
    while (lowest < fl_stack + 0xc00 && *lowest == stack_fill) ++lowest;
    const unsigned depth = unsigned(esp_before_call - u32(lowest)); // bytes below the call the stub and handler wrote
    if (depth > max_stack_depth) {
        max_stack_depth = depth;
        deepest_site = i;
    }
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
    case 6: return x3m_weapon_effects_stub_death_fl;
    default: return x3m_weapon_effects_stub_batch_fl;
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
    // A piloted victim: EBP is T clamped to 50,000 (cmovg 0x0532fd), so T >= 50,000 still reads absorbed (the PSP/IONC/
    // PPC/IBL shots, shield damage >= 50,000); an unclamped EBP above 50,000 cannot be the piloted absorbed path, and
    // the hull path's T + 1 below the clamp is refused.
    check(core::fl_absorbed(1, 50000, 60000, 0, core::fl_obj_piloted) == 1, "absorbed_piloted_clamped_total");
    check(core::fl_absorbed(1, 60000, 60000, 0, core::fl_obj_piloted) == 0, "absorbed_piloted_unclamped_refused");
    check(core::fl_absorbed(1, 40001, 40000, 0, core::fl_obj_piloted) == 0, "absorbed_piloted_hull_term_refused");
    check(core::fl_absorbed(1, 40000, 40000, 0, core::fl_obj_piloted) == 1, "absorbed_piloted_below_clamp");

    build_world(7, 0, 4, 0x0, 100000);
    check(core::fl_death_site(u32(&victim_obj), we::hook_state().globals) == 1, "death_site_tag_other");
    w32(&victim_obj, core::layout::obj_flags, core::obj_area_candidate);
    w32(bullet_table.data() + 4u * core::layout::type_stride, core::layout::bullet_flags, core::bullet_area_flag);
    check(core::fl_death_site(u32(&victim_obj), we::hook_state().globals) == 0, "death_site_tag_area");

    // A mode that uses each window (taps only in observe, the bolts in a bolts mode, death in an explosions mode): a
    // flipped window that the mode does not use would be skipped by the compare, so each is flipped under a using mode.
    const auto mode_for = [](unsigned i) {
        return i == 0 || i == 7 ? core::mode_shields
               : i < 3          ? core::mode_observe
               : i < 6          ? core::mode_shields_hull_bolts
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
              g.shield_count_slot == base + S::type_count_rva + 4 * 9,
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
    // The batch site's read-back refused (claimed after the death, before the impact and the object tap): every site
    // back, the pool disarmed, LastError kept.
    fault_read_at = base + S::sites[7].window.rva + S::sites[7].call_offset;
    SetLastError(sentinel_error);
    const bool rb7 = we::install_base(base, core::mode_shields_hull_bolts_explosions);
    check(!rb7 && GetLastError() == sentinel_error && !we::installed() && windows_original() &&
              !x3m_projectile_pool.armed && !x3m_projectile_pool.skip_on && std::strcmp(we::site_state(7), "active"),
          "batch_rollback_restores_all", we::site_state(7));
    fault_read_at = 0;
    // Shields mode (the production setting) claims the object tap, the muzzle and the batch site for the projectile
    // stage (the muzzle: muzzle-flash-look.md section 4), never the flight site.
    SetLastError(sentinel_error);
    {
        const bool ok_s = we::install_base(base, core::mode_shields);
        check(ok_s && !std::strcmp(we::site_state(7), "active") && !std::strcmp(we::site_state(3), "active") &&
                  site_redirected(7, fl_stub(7)) && !std::strcmp(we::write_path(7), "atomic") &&
                  !std::strcmp(we::site_state(5), "not_used") && x3m_projectile_pool.armed,
              "shields_mode_claims_batch_and_object_tap", we::state());
        check(ok_s && !std::strcmp(we::site_state(4), "active") && site_redirected(4, fl_stub(4)) &&
                  !std::strcmp(we::write_path(4), "atomic"),
              "shields_mode_claims_muzzle", we::site_state(4));
        we::shutdown();
        check(windows_original() && !x3m_projectile_pool.armed, "shields_mode_shutdown_restores_batch");
    }
    // The muzzle's frame witnesses guard the shields mode too: a changed gun-slot writer refuses the install, nothing
    // written, LastError kept.
    {
        const S::Window& w = S::muzzle_witnesses[0];
        writable(true);
        image[w.rva] ^= 0x01;
        writable(false);
        SetLastError(sentinel_error);
        const bool refused_w = !we::install_base(base, core::mode_shields) && !we::installed() &&
                               GetLastError() == sentinel_error && !std::strcmp(we::state(), "frame_mismatch_muzzle") &&
                               windows_original();
        writable(true);
        image[w.rva] ^= 0x01;
        writable(false);
        check(refused_w, "shields_mode_muzzle_witness_refused", we::state());
    }
    // A refused read-back at the muzzle (claimed first in shields mode) leaves nothing claimed.
    fault_read_at = base + S::sites[4].window.rva + S::sites[4].call_offset;
    SetLastError(sentinel_error);
    {
        const bool rb4 = we::install_base(base, core::mode_shields);
        check(!rb4 && GetLastError() == sentinel_error && !we::installed() && windows_original() &&
                  !x3m_projectile_pool.armed && std::strcmp(we::site_state(4), "active"),
              "shields_mode_muzzle_rollback_restores_all", we::site_state(4));
    }
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
    unsigned projectile_rows = 0;
    for (const std::string& l : log_lines)
        if (l.find("projectile_site state=claimed rva=097af8") != std::string::npos) ++projectile_rows;
    check(projectile_rows == 1, "init_logs_projectile_site_claimed_once");
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
    // Taps (observe forwards always): ECX/EDX forwarded, and the handler reads [esp+8] hit and [esp+0xc] &pos: after an
    // observe impact the right hit and the record's pos compare at error 0, a pos 1000 units off reads error 1000, and a
    // wrong hit is unmatched.
    const auto observe_impact = [&]() {
        Entry e;
        e.ecx = u32(&victim_obj);
        e.edx = u32(&source_obj);
        e.ebp = 1200;
        e.esi = 1;
        e.nstack = 7;
        e.stack[5] = 1200;
        run_site(0, e);
    };
    static std::int32_t tap_pos[3];
    const auto run_tap = [&](unsigned i, std::uint32_t hit, std::int32_t off) {
        observe_impact();
        core::Event rec{};
        newest_record(u32(&victim_obj), &rec);
        for (unsigned k = 0; k < 3; ++k) tap_pos[k] = rec.pos[k] + (k == 0 ? off : 0);
        Entry e;
        e.nstack = 4;
        e.stack[0] = 0xeeeeeeee; // eff
        e.stack[1] = hit;        // [esp+8]
        e.stack[2] = u32(tap_pos); // [esp+0xc]
        run_site(i, e);
        return caller_kept(e) && t_calls == 1 && t_ecx == e.ecx && t_edx == e.edx;
    };
    hs.owned_next = 0;
    hs.check.pending = false; // the impact-forward run above left its comparison pending (no tap followed it)
    for (unsigned i : {1u, 2u}) {
        const std::uint32_t compared = hs.check.compared, unmatched = hs.check.unmatched;
        hs.check.max_error = 0;
        const bool fwd = run_tap(i, u32(&victim_obj), 0);
        check(fwd && hs.check.compared == compared + 1 && hs.check.unmatched == unmatched && hs.check.max_error == 0,
              i == 1 ? "tap1_hit_pos_slots_match" : "tap2_hit_pos_slots_match");
        const bool off = run_tap(i, u32(&victim_obj), 1000);
        check(off && hs.check.max_error == 1000, i == 1 ? "tap1_pos_slot_read" : "tap2_pos_slot_read");
        const std::uint32_t before = hs.check.unmatched;
        const bool wrong = run_tap(i, u32(&source_obj), 0);
        check(wrong && hs.check.unmatched == before + 1, i == 1 ? "tap1_hit_slot_read" : "tap2_hit_slot_read");
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
    // A piloted victim (obj+0x40 & 2) hit with T = 60,000: EBP is the clamped 50,000, ESI = 1, remaining 0. With
    // ECX = the ship it is absorbed (a staged shield hit, the callee skipped); with ECX/EDX swapped the victim flags read
    // are the bullet's (not piloted), EBP != T, so the same hit records a hull hit and forwards: the shim reads ECX as
    // the victim and EDX as the source.
    const auto piloted_hit = [&](bool swapped) {
        core::ring_clear(hs.ring);
        Entry e;
        e.ecx = swapped ? u32(&source_obj) : u32(&victim_obj);
        e.edx = swapped ? u32(&victim_obj) : u32(&source_obj);
        e.ebp = 50000;
        e.esi = 1;
        e.nstack = 7;
        e.stack[5] = 60000;
        run_site(0, e);
        return caller_kept(e);
    };
    w32(&victim_obj, core::layout::obj_flags, core::fl_obj_piloted);
    {
        const bool kept = piloted_hit(false);
        core::Event rec{};
        check(kept && t_calls == 0 && out_eax == 0 && newest_record(u32(&victim_obj), &rec) &&
                  rec.role == core::role_shield_hit && (rec.flags & core::flag_absorbed) && rec.damage == 50000,
              "impact_piloted_clamped_absorbed_staged");
        const bool kept_swapped = piloted_hit(true);
        core::Event swapped{};
        check(kept_swapped && t_calls == 1 && newest_record(u32(&victim_obj), &swapped) &&
                  swapped.role == core::role_hull_hit && !(swapped.flags & core::flag_absorbed),
              "impact_ecx_edx_swapped_differs");
        // The residual case: the same registers after a hull stage that drained every shield to 0 (0x053155): read
        // absorbed, then the drained-shield rule records a hull hit and forwards.
        set_shields(1000000, 0);
        const bool kept_drained = piloted_hit(false);
        core::Event drained{};
        check(kept_drained && t_calls == 1 && newest_record(u32(&victim_obj), &drained) &&
                  drained.role == core::role_hull_hit && (drained.flags & core::flag_absorbed) &&
                  (drained.detail & core::detail_drained),
              "impact_piloted_drained_shields_hull_hit");
        set_shields(1000000, core::layout::shield_full);
    }
    w32(&victim_obj, core::layout::obj_flags, 0);


    // Object tap (bolt_tap): ECX = object, forwarded. The bolt handler follows the bullet extension, so give it a null
    // one (the impact no-follow test's 0x10 is unmapped).
    w32(&source_obj, core::layout::obj_ext, 0);
    {
        Entry e;
        e.ecx = u32(&source_obj);
        e.nstack = 0;
        run_site(3, e);
        check(caller_kept(e) && t_calls == 1 && t_ecx == u32(&source_obj), "object_tap_forwards_ecx");
        const core::BoltList& l = core::bolt_list_current(bs);
        bool listed = false;
        for (unsigned k = 0; k < core::bolt_list_count(bs, l); ++k) listed = listed || l.records[k].obj == u32(&source_obj);
        check(listed, "object_tap_handler_records_ecx_object");
    }

    // Muzzle (docs/architecture/muzzle-flash-look.md section 4): ECX 4, EDX the gun slot, [esp+4] eff, [esp+8] ship,
    // [esp+0x18] &pos, [esp+0x20] &orient, ESI the TBullets record offset. A shot of an enabled body (word +0 of the
    // record) while the batch latch is on: skipped (no callee, EAX 0), its (ship, slot) record ours, relit (shots 2);
    // the latch off, or another body: forwarded with EAX, ECX, EDX, EBX, ESI, EDI, EBP, the stack and EFLAGS as the call
    // left them.
    {
        core::ProjectilePool& pool = x3m_projectile_pool;
        static std::int32_t pos_block[4] = {1, 2, 3, 0};
        static std::int32_t orient_block[12] = {0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x10000, 0, 0, 0};
        const std::uint16_t body = 0x0b17;
        w16(bullet_table.data() + 4u * core::layout::type_stride, core::projectile::type_body, body);
        w16(bullet_table.data() + 5u * core::layout::type_stride, core::projectile::type_body, std::uint16_t(body + 1));
        pool.globals = we::hook_state().globals;
        pool.resolved = 1;
        pool.stale = 0;
        pool.engine_rows = 16;
        pool.body_count = 1;
        pool.bodies[0].id = body;
        for (std::uint32_t& w : pool.bits) w = 0;
        pool.bits[body >> 5] |= 1u << (body & 31u);
        const auto run_muzzle = [&](std::uint32_t row, std::uint32_t latch, std::uint32_t now, bool* skipped) {
            w32(config_block, core::layout::cfg_game_ms, now);
            pool.skip_on = latch;
            const std::uint32_t before = pool.counters.muzzle_skipped;
            Entry e;
            e.eax = 0x0000007c; // the col-21 row the window loaded
            e.ecx = 4;
            e.edx = 2;
            e.esi = row * core::layout::type_stride;
            e.nstack = 9;
            e.stack[0] = 0x7c;
            e.stack[1] = u32(&victim_obj);
            e.stack[5] = u32(pos_block);
            e.stack[7] = u32(orient_block);
            run_site(4, e);
            *skipped = pool.counters.muzzle_skipped == before + 1;
            bool stack_same = true;
            for (unsigned k = 0; k < 8; ++k) stack_same = stack_same && t_stack[1 + k] == e.stack[k];
            const bool forwarded = t_calls == 1 && t_eax == e.eax && t_ecx == e.ecx && t_edx == e.edx && t_ebx == e.ebx &&
                                   t_esi == e.esi && t_edi == e.edi && t_ebp == e.ebp && stack_same &&
                                   t_stack[0] == std::uint32_t(base + S::sites[4].window.rva + S::sites[4].call_offset + 5) &&
                                   (t_flags & flag_mask) == (entry_flags & flag_mask) && t_esp == esp_before_call - 4;
            const bool skip = t_calls == 0 && out_eax == 0;
            return caller_kept(e) && (*skipped ? skip : forwarded);
        };
        const auto record = [&]() -> const core::MuzzleRecord* {
            for (const core::MuzzleRecord& r : pool.muzzles)
                if (r.sequence && r.ship == u32(&victim_obj) && r.slot == 2) return &r;
            return nullptr;
        };
        bool skipped = false;
        const std::uint32_t unskipped0 = pool.counters.muzzle_unskipped;
        check(run_muzzle(4, 0, 1000, &skipped) && !skipped && pool.counters.muzzle_unskipped == unskipped0 + 1 &&
                  record() && !record()->ours,
              "muzzle_enabled_body_latch_off_forwards_registers_stack_flags");
        check(run_muzzle(5, 1, 1075, &skipped) && !skipped && pool.counters.muzzle_unskipped == unskipped0 + 1 &&
                  record() && record()->t0 == 1000,
              "muzzle_other_body_forwards_registers_stack_flags");
        check(run_muzzle(4, 1, 1150, &skipped) && skipped, "muzzle_skip_no_callee");
        const core::MuzzleRecord* m = record();
        check(m && m->ours && m->row == 4 && m->t0 == 1150 && m->shots == 2,
              "muzzle_handler_slot_row_pos_orient", "the (ship, slot) record: row, t0, shots");
        // the object tap matches a bullet born at the shot (ext+4 the ship, ext+8 = t0) to the ours record: pending; born
        // at another ms, unmatched; registered three steps (17 ms) after its birth, stale
        w32(config_block, core::projectile::cfg_step_ms, 17);
        alignas(16) static unsigned char shot[3][0x100], shot_node[3][0x200], shot_ext[3][0x80];
        const std::uint32_t births[3] = {1150, 1149, 1150}, taps[3] = {1150, 1150, 1201};
        std::uint8_t states[3]{};
        core::projectile_clear(pool);
        pool.armed = 1;
        for (unsigned i = 0; i < 3; ++i) {
            std::memset(shot[i], 0, sizeof shot[i]);
            std::memset(shot_node[i], 0, sizeof shot_node[i]);
            std::memset(shot_ext[i], 0, sizeof shot_ext[i]);
            w16(shot[i], core::layout::obj_class, core::layout::class_bullet);
            w32(shot[i], core::layout::obj_node, u32(shot_node[i]));
            w32(shot[i], core::layout::obj_ext, u32(shot_ext[i]));
            w32(shot_node[i], core::layout::node_live, 1);
            w32(shot_node[i], core::layout::node_handle, 0x300 + i);
            w32(shot_node[i], core::layout::node_body, body);
            w32(shot_ext[i], core::projectile::ext_expiry, 5000);
            w32(shot_ext[i], core::projectile::ext_owner, u32(&victim_obj));
            w32(shot_ext[i], core::projectile::ext_created, births[i]);
            w32(config_block, core::layout::cfg_game_ms, taps[i]);
            core::projectile_tap(u32(shot[i]), pool);
            const unsigned s = core::projectile_slot(pool, u32(shot_node[i]));
            if (s < core::projectile::slot_count) {
                states[i] = pool.entries[pool.slots[s] - 1u].flash;
            }
        }
        std::printf("MUZZLE skipped=%lu unskipped=%lu matched=%lu unmatched=%lu stale=%lu states=%u/%u/%u\n",
                    static_cast<unsigned long>(pool.counters.muzzle_skipped),
                    static_cast<unsigned long>(pool.counters.muzzle_unskipped),
                    static_cast<unsigned long>(pool.counters.flash_matched),
                    static_cast<unsigned long>(pool.counters.flash_unmatched),
                    static_cast<unsigned long>(pool.counters.flash_stale),
                    unsigned(states[0]), unsigned(states[1]), unsigned(states[2]));
        check(states[0] == core::projectile::flash_pending && states[1] == core::projectile::flash_none &&
                  states[2] == core::projectile::flash_none && pool.counters.flash_matched >= 1 &&
                  pool.counters.flash_unmatched >= 1 && pool.counters.flash_stale >= 1,
              "object_tap_matches_flash_to_ours_record");
        core::projectile_clear(pool);
        pool.skip_on = 0;
        pool.armed = 0;
        w32(config_block, core::layout::cfg_game_ms, 100000);
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
                  rec.shield_q == 0x2a && std::uint32_t(rec.damage) == 0xdeadbeefu,
              "death_forward_records_other_tag_reason_killer");
    }
    // The death skip (cmp eax,1; je; xor eax,eax; ret): a fighter death while the stage latched its death programs is
    // ours, the callee is not entered and EAX is 0.
    hs.owned_next = 1;
    hs.boom_ready = 1;
    {
        Entry e;
        e.ecx = u32(&victim_obj);
        e.edx = 0x0b0b0b0b;
        e.ebp = u32(kill_frame);
        e.nstack = 1;
        e.stack[0] = 0x44332211;
        run_site(6, e);
        core::Event rec{};
        check(caller_kept(e) && t_calls == 0 && out_eax == 0 && newest_record(u32(&victim_obj), &rec) &&
                  (rec.flags & core::flag_stage) && std::uint32_t(rec.damage) == 0x0b0b0b0bu,
              "death_skip_no_callee_eax_zero");
    }
    hs.owned_next = 0;
    hs.boom_ready = 0;

    // FL's node body id offset (+0x144; the projectile pool's read). The ship bubble's hull walk that also read it was
    // removed on 2026-11-12.
    check(core::layout::node_body == 0x144, "fl_node_body_offset");

    // The projectile stage's batch submit (shields mode): a fake batch record whose body id sits at +0x0c, ECX the scene,
    // [esp+4] the material, [esp+8] the view. Forward: the callee sees every register, the stack and the flags; skip
    // (latch on, body enabled): no callee, EAX 1, the rest as the caller left them; the stub's skip count advances.
    we::shutdown();
    restore_windows();
    SetLastError(sentinel_error);
    check(we::install_base(base, core::mode_shields) && we::installed(), "reinstall_shields_for_batch_runs");
    plant_returns();
    {
        core::ProjectilePool& pool = x3m_projectile_pool;
        alignas(16) static unsigned char batch[0x40];
        std::memset(batch, 0, sizeof batch);
        const std::uint32_t body = 0x1234;
        pool.bits[body >> 5] |= 1u << (body & 31u);
        const auto run_batch = [&](std::uint32_t id, std::uint32_t latch, bool* skipped) {
            w32(batch, S::batch_body, id);
            pool.skip_on = latch;
            const std::uint32_t before = pool.skipped;
            Entry e;
            e.ecx = 0x5ce4e000;
            e.edx = u32(batch);
            e.nstack = 2;
            e.stack[0] = 0x3a7e0001; // material
            e.stack[1] = 0x71e70002; // view
            run_site(7, e);
            *skipped = pool.skipped == before + 1;
            const bool forwarded = t_calls == 1 && t_eax == e.eax && t_ecx == e.ecx && t_edx == e.edx && t_ebx == e.ebx &&
                                   t_esi == e.esi && t_edi == e.edi && t_ebp == e.ebp && t_stack[1] == e.stack[0] &&
                                   t_stack[2] == e.stack[1] &&
                                   t_stack[0] == std::uint32_t(base + S::sites[7].window.rva + S::sites[7].call_offset + 5) &&
                                   (t_flags & flag_mask) == (entry_flags & flag_mask) && t_esp == esp_before_call - 4;
            const bool skip = t_calls == 0 && out_eax == 1 && out_ecx == e.ecx && out_edx == e.edx &&
                              (out_flags & flag_mask) == (entry_flags & flag_mask) && out_esp == esp_before_call;
            return caller_kept(e) && (*skipped ? skip : forwarded && !*skipped);
        };
        bool skipped = false;
        check(run_batch(body, 0, &skipped) && !skipped, "batch_latch_off_forwards_registers_stack_flags");
        check(run_batch(body + 1, 1, &skipped) && !skipped, "batch_other_body_forwards_registers_stack_flags");
        check(run_batch(0x10000 | body, 1, &skipped) && !skipped, "batch_body_above_word_forwards");
        check(run_batch(body, 1, &skipped) && skipped, "batch_enabled_body_skipped_eax1_no_callee");
        std::printf("BATCH skipped=%lu body=%04lx\n", static_cast<unsigned long>(pool.skipped),
                    static_cast<unsigned long>(body));
        // The object tap registers a bullet of an enabled body (its node +0x144) once, refreshes it on the next pass,
        // and ignores a bullet of another body.
        pool.skip_on = 0;
        pool.resolved = 1;
        pool.body_count = 1;
        bind_globals(); // the tap's game-clock read: the fixture's heap cfg instead of the image's 0xcc slots
        pool.globals = we::hook_state().globals;
        pool.bodies[0].id = body;
        core::projectile_clear(pool);
        alignas(16) static unsigned char bullet[0x100], node[0x200], ext[0x80];
        std::memset(bullet, 0, sizeof bullet);
        std::memset(node, 0, sizeof node);
        std::memset(ext, 0, sizeof ext);
        w16(bullet, core::layout::obj_class, core::layout::class_bullet);
        w32(bullet, core::layout::obj_node, u32(node));
        w32(bullet, core::layout::obj_ext, u32(ext));
        w32(node, core::layout::node_live, 1);
        w32(node, core::layout::node_handle, 0x77);
        w32(node, core::layout::node_body, body);
        w32(ext, core::projectile::ext_expiry, 5000);
        w32(ext, core::projectile::ext_created, 3800);
        Entry e;
        e.ecx = u32(bullet);
        run_site(3, e);
        run_site(3, e);
        const bool registered = pool.count == 1 && pool.entries[0].node == u32(node) && pool.entries[0].obj == u32(bullet) &&
                                pool.entries[0].handle == 0x77 && pool.entries[0].expiry == 5000 &&
                                pool.entries[0].created == 3800 && core::projectile_known(pool, u32(node));
        w32(node, core::layout::node_body, body + 1);
        alignas(16) static unsigned char node2[0x200];
        std::memcpy(node2, node, sizeof node2);
        w32(bullet, core::layout::obj_node, u32(node2));
        run_site(3, e);
        check(caller_kept(e) && t_calls == 1 && registered && pool.count == 1, "object_tap_registers_enabled_body_once");
        core::projectile_remove(pool, 0);
        check(pool.count == 0 && !core::projectile_known(pool, u32(node)), "projectile_remove_empties_slot");
    }

    // Reshade mode (bullet-draw-batch.md section 6): the impact and the batch call claimed, the batch with the scope stub;
    // no object tap, muzzle, flight or death site, the projectile pool disarmed. The scope stub forwards every submit as
    // a call: the callee sees ECX, EDX, EAX, the callee-saved registers, EFLAGS and the two stack words as the caller
    // left them, its stack at the original call's alignment modulo 16, the scope raised to 1 and x3m_bullet_body the
    // batch's body id [EDX+0x0c] (two batches, two ids); after it the scope is
    // 0 again, the callee's EAX is returned, the callee-saved registers, DF and LastError are the caller's, and the stack
    // is the caller's (it pops its own two words).
    we::shutdown();
    restore_windows();
    SetLastError(sentinel_error);
    {
        const bool ok = we::install_base(base, core::mode_reshade);
        check(ok && we::installed() && we::mode() == core::mode_reshade && GetLastError() == sentinel_error &&
                  !std::strcmp(we::site_state(0), "active") && !std::strcmp(we::site_state(7), "active") &&
                  !std::strcmp(we::site_state(3), "not_used") && !std::strcmp(we::site_state(4), "not_used") &&
                  !std::strcmp(we::site_state(5), "not_used") && !std::strcmp(we::site_state(6), "not_used") &&
                  x3m_bullet_reshade_armed == 1 && x3m_projectile_pool.armed == 0 &&
                  site_redirected(7, x3m_weapon_effects_stub_batch_scope_fl) &&
                  we::hook_state().armed == core::family_shields,
              "reshade_installs_impact_and_batch_scope_only");
        plant_returns();
        alignas(16) static unsigned char batch[0x40];
        std::memset(batch, 0, sizeof batch);
        x3m_projectile_pool.skip_on = 1; // a stale latch never skips in reshade mode: the scope stub reads no pool
        const std::uint32_t calls_before = x3m_bullet_scope_calls;
        bool all = true;
        for (unsigned df = 0; df < 2; ++df) {
            const std::uint32_t body_id = df ? 0x0206u : 0x01e1u;
            std::memcpy(batch + 0x0c, &body_id, 4);
            Entry e;
            e.df = df;
            e.ecx = 0x5ce4e000;
            e.edx = u32(batch);
            e.nstack = 2;
            e.stack[0] = 0x3a7e0001; // material
            e.stack[1] = 0x71e70002; // view
            t_scope = 0;
            t_body = 0;
            x3m_bullet_body = 0xdeadbeefu;
            run_site(7, e);
            const bool forwarded = t_calls == 1 && t_eax == e.eax && t_ecx == e.ecx && t_edx == e.edx && t_ebx == e.ebx &&
                                   t_esi == e.esi && t_edi == e.edi && t_ebp == e.ebp && t_stack[1] == e.stack[0] &&
                                   t_stack[2] == e.stack[1] && (t_flags & flag_mask) == (entry_flags & flag_mask) &&
                                   ((t_esp - (esp_before_call - 4)) & 15u) == 0 && t_esp < esp_before_call - 4 &&
                                   t_scope == 1 && t_body == body_id;
            const bool returned = out_eax == 0xca11ee00 && out_esp == esp_before_call && x3m_bullet_scope == 0 &&
                                  GetLastError() == sentinel_error && out_ebx == e.ebx && out_esi == e.esi &&
                                  out_edi == e.edi && out_ebp == e.ebp && (out_flags & df_bit) == (df ? df_bit : 0u);
            all = all && forwarded && returned;
        }
        check(all && x3m_bullet_scope_calls == calls_before + 2, "reshade_scope_stub_forwards_and_scopes_the_submit");
        std::printf("RESHADE scope_calls=%lu callee_esp_offset=%lu\n",
                    static_cast<unsigned long>(x3m_bullet_scope_calls - calls_before),
                    static_cast<unsigned long>(esp_before_call - t_esp));
        x3m_projectile_pool.skip_on = 0;
        we::shutdown();
        check(!we::installed() && x3m_bullet_reshade_armed == 0, "reshade_shutdown_disarms");
        restore_windows();
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



    // The deepest run (the impact's: the stub's return, EFLAGS and three saves, four inputs, the entry pointer and the
    // handler call's return = 0x2c, then the handler's own frame) stays well inside the private stack's 0xc00; the
    // runner gates it against the audited handler frame (run_weapon_effects.py run_hook_fl).
    std::printf("STACK_DEPTH bytes=%u margin=%u site=%u\n", max_stack_depth, 0xc00u, deepest_site);
    check(max_stack_depth > 0x2c && max_stack_depth < 0x400 && deepest_site == 0, "fixture_stack_margin");
    std::printf("RESULT %s checks=%u failures=%u faults=%u\n", failures ? "FAIL" : "PASS", checks, failures, faults_taken);
    return failures ? 1 : 0;
}
