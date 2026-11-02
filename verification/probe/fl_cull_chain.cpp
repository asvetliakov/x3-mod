// Original synthetic fixture of the FL cull/LOD selector ports (src/engine/fl/cull_small_parts_fl.cpp,
// terran_station_lod_fl.cpp, lod_occlusion_fl.cpp; docs/verification/cull-small-parts.md "FL port"): no game code, no
// game bytes beyond the windows in cull_small_parts_sites_fl.h and lod_patch_sites_fl.h, no launch. Built with
// -DX3M_EDITION_FL=1 by verification/analysis/test_fl_cull_chain.py (the small-parts module force-includes
// fl_cull_chain_shim.h for the read-back / restore / chain faults) and run natively (WSL interop).
//
// A 64 KB-aligned private image stands in for X3FL.exe at `base`: the three windows at their RVAs (the occlusion fixup
// = base+0x1a9014, its placeholder global readable there), the marker and JET writers, and `jmp rel32` exits at the
// native continuations (0x0961fc size pass, 0x096201 after the cull AND, 0x095ea1, 0x0e56c5, 0x0e5731). A harness
// enters a site with the FL selector's frame (ESP = align_down(EBP,8) - 0x28, s at [ESP+0x10]/[ESP+0x18], camera at
// [EBP+8] and in EBX, node in ESI, live native measure in ECX, garbage EAX/EDX, chosen EFLAGS); the real window bytes
// then run to an exit, which records every register, EFLAGS, ESP/EBP and the locals.
//
// The far-jet hand-off (docs/reverse-engineering/fl/cull-chain.md "Verified 2026-11-03"): the production handler
// src/proxy/engine_far_jets.cpp is linked under the name x3m_engine_far_jet_real (-D at its compile, its own no-SSE
// flags); the recording fake below forwards to it when far_forward is set, so the stub's far block reaches the real
// handler with FL-layout nodes (decoys at the AP offsets) and the copy is turned into a plume record by the consumer's
// own call (engine_effects_core.h far_record with the owned context scale, as motion_output_engine_plumes_inc.h does).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../../src/engine/fl/cull_small_parts_fl.h"
#include "../../src/engine/fl/cull_small_parts_sites_fl.h"
#include "../../src/engine/fl/lod_patch_sites_fl.h"
#include "../../src/engine/fl/lod_patches_fl.h"
#include "../../src/proxy/camera_state.h"
#include "../../src/proxy/cull_census.h"
#include "../../src/proxy/engine_effects_core.h"
#include "../../src/proxy/engine_far_jets.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/fov.h"
namespace csp = x3m::cull_small_parts;
namespace S = x3m::fl::cull_small_parts_fl;
namespace L = x3m::fl::lod_patch_sites_fl;
namespace small = x3m::cull_small_parts::core;
using std::int32_t;
using std::uint32_t;
unsigned checks = 0, failures = 0;
void check(bool value, const char* label, const char* detail = "") {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s %s\n", label, detail);
    }
}
char last_row[1024];
unsigned rows = 0;
namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(last_row, sizeof last_row, format, args);
    va_end(args);
    ++rows;
    std::printf("LOG %s\n", last_row);
}
}
// Fakes of the frame-side collaborators the module calls (camera reader, registry focus, census note).
bool camera_valid = false;
float camera_m00 = 0.75f, camera_m11 = 1.3333334f;
namespace x3m::camera_state {
bool available() {
    return camera_valid;
}
const char* status() {
    return "fixture";
}
bool read(Sample* out) {
    *out = Sample{};
    out->state.m00 = camera_m00;
    out->state.m11 = camera_m11;
    out->state.valid = camera_valid;
    return camera_valid;
}
}
namespace x3m::fov {
std::uint32_t current_focus() {
    return 0x4000;
}
}
int32_t census_threshold = -1, census_upper = -1;
namespace x3m::cull_census {
void note_small_threshold(std::int32_t threshold, bool, std::int32_t upper) {
    census_threshold = threshold;
    census_upper = upper;
}
}
// The seam (fl_cull_chain_shim.h): one-shot faults, otherwise the real calls.
std::uintptr_t fault_read_at = 0;
bool fault_restore = false, fault_push = false;
unsigned faults_taken = 0;
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count) {
    const bool ok = read_code(address, out, count);
    if (ok && fault_read_at && address == fault_read_at && count == 5) { // the jump's read-back
        fault_read_at = 0;
        ++faults_taken;
        out[1] ^= 0xff;
    }
    return ok;
}
bool fixture_restore(Site& site) {
    if (fault_restore) {
        fault_restore = false;
        ++faults_taken;
        site.status = "restore_protect_failed";
        return false;
    }
    return restore(site);
}
void* fixture_push_front(Site& site, void* stub) {
    if (fault_push) {
        fault_push = false;
        ++faults_taken;
        return nullptr;
    }
    return push_front(site, stub);
}
}

extern "C" {
uint32_t in_eax, in_ebx, in_ecx, in_edx, in_esi, in_edi, in_flags, in_ebp_skew, in_entry;
uint32_t in_s, in_m14, in_l1c, in_l24, in_cam, in_ebp_m1c;
uint32_t out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, out_esp, out_flags, out_kind;
uint32_t out_s10, out_m14, out_s18, out_l1c, out_l24, out_cam, out_ebp_m2c;
uint32_t site_esp, site_ebp, saved_esp;
// The far-jet fake: its cdecl arguments, ESP and EFLAGS at entry; it clobbers EAX/ECX/EDX as any cdecl callee may.
uint32_t far_calls = 0, far_node, far_measure, far_view, far_esp, far_flags, far_forward = 0;
void x3m_engine_far_jet_real(uint32_t node, int32_t measure, uint32_t view); // engine_far_jets.cpp, renamed
void flc_run();
void flc_exit_a();
void flc_exit_b();
}
asm(".text\n\t"
    ".globl _flc_run\n_flc_run:\n\t"
    "pushal\n\tpushfl\n\tmovl %esp,_saved_esp\n\t"
    "andl $-16,%esp\n\tsubl $0x400,%esp\n\tleal 0x200(%esp),%ebp\n\taddl _in_ebp_skew,%ebp\n\t"
    "movl %ebp,%eax\n\tandl $-8,%eax\n\tsubl $0x28,%eax\n\t" // the selector's site ESP
    "movl _in_s,%ecx\n\tmovl %ecx,0x10(%eax)\n\tmovl %ecx,0x18(%eax)\n\t"
    "movl _in_m14,%ecx\n\tmovl %ecx,0x14(%eax)\n\tmovl _in_l1c,%ecx\n\tmovl %ecx,0x1c(%eax)\n\t"
    "movl _in_l24,%ecx\n\tmovl %ecx,0x24(%eax)\n\tmovl _in_cam,%ecx\n\tmovl %ecx,8(%ebp)\n\t"
    // The material-submission frame (occlusion runs only, EBP skewed so [EBP-0x2c] is the site ESP's slot 0).
    "movl _in_ebp_m1c,%ecx\n\ttestl %ecx,%ecx\n\tje 2f\n\tmovl %ecx,-0x1c(%ebp)\n\tmovl $0x2c2c2c2c,-0x2c(%ebp)\n"
    "2:\n\t"
    "movl %eax,%esp\n\tmovl %esp,_site_esp\n\tmovl %ebp,_site_ebp\n\t"
    "movl _in_ebx,%ebx\n\tmovl _in_ecx,%ecx\n\tmovl _in_edx,%edx\n\tmovl _in_esi,%esi\n\tmovl _in_edi,%edi\n\t"
    "pushl _in_flags\n\tmovl _in_eax,%eax\n\tpopfl\n\tjmp *_in_entry\n\t"
    ".globl _flc_exit_a\n_flc_exit_a:\n\tmovl $1,_out_kind\n\tjmp 1f\n\t"
    ".globl _flc_exit_b\n_flc_exit_b:\n\tmovl $2,_out_kind\n"
    "1:\n\tmovl %eax,_out_eax\n\tmovl -0x2c(%ebp),%eax\n\tmovl %eax,_out_ebp_m2c\n\t" // before the pushfl below ESP
    "pushfl\n\tpopl _out_flags\n\tcld\n\t"
    "movl %ebx,_out_ebx\n\tmovl %ecx,_out_ecx\n\tmovl %edx,_out_edx\n\tmovl %esi,_out_esi\n\t"
    "movl %edi,_out_edi\n\tmovl %ebp,_out_ebp\n\tmovl %esp,_out_esp\n\t"
    "movl 0x10(%esp),%eax\n\tmovl %eax,_out_s10\n\tmovl 0x14(%esp),%eax\n\tmovl %eax,_out_m14\n\t"
    "movl 0x18(%esp),%eax\n\tmovl %eax,_out_s18\n\tmovl 0x1c(%esp),%eax\n\tmovl %eax,_out_l1c\n\t"
    "movl 0x24(%esp),%eax\n\tmovl %eax,_out_l24\n\tmovl 8(%ebp),%eax\n\tmovl %eax,_out_cam\n\t"
    "movl _saved_esp,%esp\n\tpopfl\n\tpopal\n\tret\n\t"
    ".globl _x3m_engine_far_jet\n_x3m_engine_far_jet:\n\t"
    "movl %esp,_far_esp\n\tpushfl\n\tpopl _far_flags\n\tincl _far_calls\n\t"
    "movl 4(%esp),%eax\n\tmovl %eax,_far_node\n\tmovl 8(%esp),%eax\n\tmovl %eax,_far_measure\n\t"
    "movl 12(%esp),%eax\n\tmovl %eax,_far_view\n\t"
    "cmpl $0,_far_forward\n\tje 3f\n\tjmp _x3m_engine_far_jet_real\n3:\n\t" // the real handler: a cdecl tail jump
    "movl $0xdead0001,%eax\n\tmovl $0xdead0002,%ecx\n\tmovl $0xdead0003,%edx\n\tret\n\t");

std::uintptr_t base = 0;
unsigned char* image = nullptr;
constexpr unsigned image_size = 0x1b0000;
constexpr uint32_t sentinel_error = 0x2a2a1357, placeholder_value = 0x77665544;
constexpr uint32_t arith = 0x8d5, df = 0x400;
uint32_t u32(const void* p) {
    return uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
void writable(bool on) {
    DWORD old = 0;
    VirtualProtect(image, image_size, on ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ, &old);
}
void exit_at(uint32_t rva, void* to) { // jmp rel32
    image[rva] = 0xe9;
    const uint32_t rel = u32(to) - uint32_t(base + rva + 5);
    std::memcpy(image + rva + 1, &rel, 4);
}
void lay_out() {
    writable(true);
    std::memset(image, 0xcc, image_size);
    std::memcpy(image + S::window_rva, S::window, S::window_length);
    exit_at(S::size_pass_rva, reinterpret_cast<void*>(&flc_exit_a));
    exit_at(S::after_cull_rva, reinterpret_cast<void*>(&flc_exit_b));
    std::memcpy(image + S::marker_store_rva, S::marker_store, S::marker_store_length);
    std::memcpy(image + S::marker_or_rva, S::marker_or, S::marker_or_length);
    std::memcpy(image + S::jet_writer_rva, S::jet_writer, S::jet_writer_length);
    std::memcpy(image + L::terran.rva, L::terran.bytes, L::terran.length);
    exit_at(L::terran_exit_rva, reinterpret_cast<void*>(&flc_exit_a));
    std::memcpy(image + L::occlusion.rva, L::occlusion.bytes, L::occlusion.length);
    const uint32_t fixup = uint32_t(base + L::occlusion.fixup_rva);
    std::memcpy(image + L::occlusion.rva + L::occlusion.fixup_offset, &fixup, 4);
    std::memcpy(image + L::occlusion.fixup_rva, &placeholder_value, 4);
    exit_at(L::occlusion_after_rva, reinterpret_cast<void*>(&flc_exit_a));
    exit_at(L::occlusion_placeholder_rva, reinterpret_cast<void*>(&flc_exit_b));
    writable(false);
    FlushInstructionCache(GetCurrentProcess(), image, image_size);
}
unsigned char cull_window_now[S::window_length], terran_now[31], occlusion_now[20];
bool cull_window_original() {
    return !std::memcmp(image + S::window_rva, cull_window_now, S::window_length);
}
bool lod_windows_original() {
    return !std::memcmp(image + L::terran.rva, terran_now, 31) && !std::memcmp(image + L::occlusion.rva, occlusion_now, 20);
}
void poke(uint32_t rva, unsigned char value) {
    writable(true);
    image[rva] = value;
    writable(false);
    FlushInstructionCache(GetCurrentProcess(), image + rva, 1);
}

// Render nodes: node and parent, FL layout.
struct Node {
    alignas(16) unsigned char bytes[0x200];
};
Node node{}, parent{}, lod_node{};
void put(Node& n, unsigned offset, uint32_t value) {
    std::memcpy(n.bytes + offset, &value, 4);
}
uint32_t get(const Node& n, unsigned offset) {
    uint32_t v = 0;
    std::memcpy(&v, n.bytes + offset, 4);
    return v;
}
struct Case {
    const char* label;
    uint32_t s, ecx, f12c, f130, model;
    int32_t own;
    bool has_parent;
    int32_t parent_threshold;
};
enum class Expect { native, culled, dock, exempt, far_jet };
struct Out {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp, flags, kind, s10, m14, s18, l1c, l24, cam, f12c, error;
    uint32_t site_esp, site_ebp;
};
constexpr uint32_t camera_value = 0x00ca3e7a, d_value = 0x0000d00d;
void (*node_fields)(Node& n) = nullptr; // the far-chain cases' further node fields (after the decision fields)
Out run_cull(const Case& c, uint32_t flags, uint32_t skew = 4, uint32_t camera = camera_value) {
    std::memset(&node, 0x5a, sizeof node);
    std::memset(&parent, 0x6b, sizeof parent);
    put(node, S::parent_offset, c.has_parent ? u32(&parent) : 0);
    put(node, S::flags12c_offset, c.f12c);
    put(node, S::flags130_offset, c.f130);
    put(node, S::model_offset, c.model);
    put(node, S::threshold_1d8_offset, uint32_t(c.own));
    put(parent, S::threshold_1d8_offset, uint32_t(c.parent_threshold));
    if (node_fields) node_fields(node);
    in_eax = 0xeaeaeaea, in_edx = 0xedededed, in_ebx = camera, in_ecx = c.ecx, in_esi = u32(&node);
    in_edi = d_value, in_flags = flags, in_ebp_skew = skew, in_entry = uint32_t(base + S::site_rva);
    in_s = c.s, in_m14 = c.ecx, in_l1c = 0x1c1c1c1c, in_l24 = 0x24242424, in_cam = camera, in_ebp_m1c = 0;
    SetLastError(sentinel_error);
    flc_run();
    Out o{out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, out_esp, out_flags, out_kind, out_s10, out_m14,
          out_s18, out_l1c, out_l24, out_cam, get(node, S::flags12c_offset), GetLastError(), site_esp, site_ebp};
    return o;
}
// The frame the native code and the stub must leave: callee-saved registers, ESP/EBP, the locals, DF and LastError.
bool frame_kept(const Out& o, const Case& c) {
    return o.ebx == camera_value && o.esi == u32(&node) && o.edi == d_value && o.esp == o.site_esp &&
           o.ebp == o.site_ebp && o.s10 == c.s && o.s18 == c.s && o.m14 == c.ecx && o.l1c == 0x1c1c1c1c &&
           o.l24 == 0x24242424 && o.cam == camera_value && !(o.flags & df) && o.error == sentinel_error;
}
uint32_t and_flags(uint32_t value) { // the arithmetic flags `and dword [esi+0x12c],~2` leaves (AF masked)
    const uint32_t r = value & ~2u;
    unsigned bits = 0;
    for (unsigned i = 0; i < 8; ++i) bits += (r >> i) & 1;
    return (r == 0 ? 0x40u : 0u) | ((r >> 31) ? 0x80u : 0u) | ((bits & 1) ? 0u : 0x4u);
}
bool same_registers(const Out& a, const Out& b, bool with_ecx) {
    return a.eax == b.eax && a.ebx == b.ebx && (!with_ecx || a.ecx == b.ecx) && a.edx == b.edx && a.esi == b.esi &&
           a.edi == b.edi && a.ebp - a.site_ebp == b.ebp - b.site_ebp && a.esp - a.site_esp == b.esp - b.site_esp &&
           (a.flags & (arith | df)) == (b.flags & (arith | df)) && a.kind == b.kind && a.f12c == b.f12c &&
           a.s10 == b.s10 && a.m14 == b.m14 && a.s18 == b.s18 && a.l1c == b.l1c && a.l24 == b.l24 && a.cam == b.cam;
}

// Thresholds of the decision cases: small T = 10, dock upper U = 30.
constexpr int32_t T = 10, U = 30;
const Case cases[] = {
    // label                 s     ecx   f12c        f130        model      own  parent  parent_thr
    {"small_part", 5, 600, 0x00000004, 0x00000000, 7, 3, true, 4},
    {"small_part_no_parent", 9, 700, 0x80000006, 0x00100000, 9, 0, false, 0},
    {"small_part_zero_flags", 1, 50, 0x00000002, 0x00000000, 11, 0, true, 0},
    {"small_part_parent_bigger", 2, 800, 0x00000004, 0x00000000, 13, 2, true, 9},
    {"big_part", 50, 900, 0x00000004, 0x00000000, 7, 3, true, 4},
    {"big_part_saturated", 0x7000000, 0x7000000, 0x00000004, 0x00000000, 7, 3, false, 0},
    {"at_threshold", 10, 600, 0x00000004, 0x00000000, 7, 3, true, 4},
    {"between_not_dock", 20, 600, 0x00000004, 0x00000000, 901500000, 3, true, 4},
    {"dock_first", 20, 600, 0x00000004, 0x00000000, 901300005, 3, true, 4},
    {"dock_first_last", 29, 600, 0x00000004, 0x00000000, 901499999, 3, true, 4},
    {"dock_second", 15, 600, 0x00000004, 0x00000000, 909800001, 3, false, 0},
    {"dock_at_upper", 30, 600, 0x00000004, 0x00000000, 909800001, 3, false, 0},
    {"dock_below_first", 20, 600, 0x00000004, 0x00000000, 901299999, 3, true, 4},
    {"projectile_small", 5, 600, 0x00000004, 0x20800000, 7, 3, true, 4},
    {"projectile_dock", 20, 600, 0x00000004, 0x20800000, 901300005, 3, true, 4},
    {"far_jet", 2, 640, 0x00000004, 0x04000001, 566, 5, true, 1},
    {"far_jet_rcs_bit26_only", 2, 640, 0x00000004, 0x04000000, 566, 5, true, 1},
    {"far_jet_bit0_only", 2, 640, 0x00000004, 0x00000001, 566, 5, true, 1},
    {"far_jet_projectile", 2, 640, 0x00000004, 0x24800001, 566, 5, true, 1},
    {"native_size_cull", 50, 1, 0x00000004, 0x00000000, 7, 3, true, 4},
};
Expect expect(const Case& c, bool armed, bool projectiles, bool far_on) {
    if (!armed || int32_t(c.s) >= U) return Expect::native;
    const bool marked = projectiles && (c.f130 & small::projectile_flag);
    if (int32_t(c.s) < T) {
        if (marked) return Expect::exempt;
        return far_on && (c.f130 & small::jet_flags) == small::jet_flags ? Expect::far_jet : Expect::culled;
    }
    if (small::dock_model(c.model)) return marked ? Expect::exempt : Expect::dock;
    return Expect::native;
}
Out native_out[sizeof cases / sizeof cases[0]];
uint32_t case_flags(unsigned i) { // a different arithmetic-flag pattern per case (dead at the site), DF clear
    static const uint32_t patterns[] = {0x002, 0x8d7, 0x046, 0x093, 0x803, 0x0c6, 0x015, 0x887};
    return patterns[i % 8];
}
unsigned culled_total = 0, dock_total = 0, exempt_total = 0, far_total = 0, native_total = 0;
void run_decisions(const char* variant, bool armed, bool projectiles, bool far_on) {
    x3m_cull_small_parts_threshold = armed ? T : 0;
    x3m_cull_small_parts_upper = armed ? U : 0;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        const Case& c = cases[i];
        const Expect e = expect(c, armed, projectiles, far_on);
        const uint32_t culled0 = x3m_cull_small_parts_culled, dock0 = x3m_cull_small_parts_dock_culled,
                       exempt0 = x3m_cull_small_parts_exempt, far0 = far_calls;
        const Out o = run_cull(c, case_flags(i), i & 1 ? 4 : 0);
        char label[160], detail[320];
        std::snprintf(label, sizeof label, "%s_%s", variant, c.label);
        std::snprintf(detail, sizeof detail,
                      "kind=%u eax=%08x ecx=%08x edx=%08x flags=%08x f12c=%08x culled=%u dock=%u exempt=%u far=%u",
                      o.kind, o.eax, o.ecx, o.edx, o.flags, o.f12c, x3m_cull_small_parts_culled - culled0,
                      x3m_cull_small_parts_dock_culled - dock0, x3m_cull_small_parts_exempt - exempt0, far_calls - far0);
        bool ok = frame_kept(o, c) && o.ecx == c.ecx;
        if (e == Expect::native || e == Expect::exempt) {
            // The tail: the displaced loads replayed once, then the engine's own verdict, exactly as unpatched.
            ok = ok && same_registers(o, native_out[i], true) && x3m_cull_small_parts_culled == culled0 &&
                 x3m_cull_small_parts_dock_culled == dock0 && far_calls == far0 &&
                 x3m_cull_small_parts_exempt == exempt0 + (e == Expect::exempt);
            ++(e == Expect::exempt ? exempt_total : native_total);
        } else {
            // The engine's cull instruction with EAX/EDX as the native max-threshold block leaves them; ECX kept.
            const uint32_t parent_thr = c.has_parent ? uint32_t(c.parent_threshold) : 0;
            const uint32_t limit = c.has_parent && c.parent_threshold > c.own ? uint32_t(c.parent_threshold) : uint32_t(c.own);
            ok = ok && o.kind == 2 && o.eax == parent_thr && o.edx == limit && o.f12c == (c.f12c & ~2u) &&
                 (o.flags & (arith & ~0x10u)) == and_flags(c.f12c) &&
                 x3m_cull_small_parts_exempt == exempt0 &&
                 x3m_cull_small_parts_culled == culled0 + (e != Expect::dock) &&
                 x3m_cull_small_parts_dock_culled == dock0 + (e == Expect::dock) &&
                 far_calls == far0 + (e == Expect::far_jet);
            if (e == Expect::far_jet)
                ok = ok && far_node == u32(&node) && far_measure == c.ecx && far_view == camera_value &&
                     far_esp == o.site_esp - 20 && !(far_flags & df);
            // The native cull of the same node (its measure below the limit) leaves the same frame, ECX aside.
            if (limit > 0 && int32_t(limit) > 0) {
                Case native = c;
                native.ecx = 0;
                const bool was_armed = x3m_cull_small_parts_upper != 0;
                const int32_t t = x3m_cull_small_parts_threshold, u = x3m_cull_small_parts_upper;
                x3m_cull_small_parts_threshold = 0;
                x3m_cull_small_parts_upper = 0;
                Out n = run_cull(native, case_flags(i), i & 1 ? 4 : 0);
                x3m_cull_small_parts_threshold = t;
                x3m_cull_small_parts_upper = u;
                n.m14 = c.ecx; // the harness wrote the measure copy from ECX
                ok = ok && was_armed && same_registers(o, n, false);
            }
            ++(e == Expect::dock ? dock_total : e == Expect::far_jet ? far_total : culled_total);
        }
        check(ok, label, detail);
    }
}

// The terran and occlusion code paths, run through the image.
Out run_terran(uint32_t f12c, bool has_parent, uint32_t al) {
    std::memset(&node, 0x5a, sizeof node);
    put(node, S::parent_offset, has_parent ? u32(&parent) : 0);
    put(node, S::flags12c_offset, f12c);
    in_eax = 0xeaeaea00 | al, in_edx = 0xedededed, in_ebx = camera_value, in_ecx = 1, in_esi = u32(&node);
    in_edi = d_value, in_flags = 0x002, in_ebp_skew = 0, in_entry = uint32_t(base + L::terran.rva);
    in_s = 0x51, in_m14 = 0x14, in_l1c = 0x1c1c1c1c, in_l24 = 0x24242424, in_cam = camera_value, in_ebp_m1c = 0;
    SetLastError(sentinel_error);
    flc_run();
    return Out{out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, out_esp, out_flags, out_kind, out_s10,
               out_m14, out_s18, out_l1c, out_l24, out_cam, 0, GetLastError(), site_esp, site_ebp};
}
Out run_occlusion(uint32_t lod) {
    std::memset(&lod_node, 0x3c, sizeof lod_node);
    put(lod_node, 0x150, lod);
    in_eax = 0xeaeaeaea, in_edx = 0xedededed, in_ebx = 0xb0b0b0b0, in_ecx = 0xc0c0c0c0, in_esi = 0x51515151;
    in_edi = 0xd1d1d1d1, in_flags = 0x8d7, in_ebp_skew = 4, in_entry = uint32_t(base + L::occlusion.rva);
    in_s = 0x51, in_m14 = 0x14, in_l1c = 0x1c1c1c1c, in_l24 = 0x24242424, in_cam = camera_value,
    in_ebp_m1c = u32(&lod_node);
    SetLastError(sentinel_error);
    flc_run();
    return Out{out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, out_esp, out_flags, out_kind, out_s10,
               out_m14, out_s18, out_l1c, out_l24, out_cam, out_ebp_m2c, GetLastError(), site_esp, site_ebp};
}
void lod_paths(const char* variant, bool terran_patched, bool occlusion_patched) {
    char label[96];
    // Terran: a parentless root with bit 31 (CMP <0, CMOVL taken natively) and one without; a parented node.
    for (unsigned k = 0; k < 3; ++k) {
        const uint32_t f12c = k == 1 ? 0x00000004 : 0x80000004;
        const Out o = run_terran(f12c, k == 2, 0);
        const uint32_t flag = (k == 0 && !terran_patched) ? 1 : 0;
        const bool ok = k == 2 ? (o.kind == 1 && o.l1c == 0x1c1c1c1c && o.l24 == 0x24242424)
                               : (o.kind == 1 && o.eax == flag && o.l1c == flag && (o.l24 & 0xff) == flag &&
                                  (o.l24 >> 8) == 0x242424);
        std::snprintf(label, sizeof label, "%s_terran_%s", variant, k == 0 ? "root_bit31" : k == 1 ? "root_plain" : "parented");
        check(ok && o.ecx == 1 && o.ebx == camera_value && o.esi == u32(&node) && o.edi == d_value &&
                  o.esp == o.site_esp && o.ebp == o.site_ebp && o.error == sentinel_error && !(o.flags & df),
              label);
    }
    // Occlusion: LOD 0 falls through natively; LOD 2 takes the placeholder bind natively, the LOD-0 path patched.
    for (uint32_t lod : {0u, 2u}) {
        const Out o = run_occlusion(lod);
        const bool lod0 = lod == 0 || occlusion_patched;
        std::snprintf(label, sizeof label, "%s_occlusion_lod%u", variant, lod);
        check(o.kind == (lod0 ? 1u : 2u) && o.eax == placeholder_value &&
                  o.f12c == (lod0 ? placeholder_value : 0x2c2c2c2cu) && o.ecx == 0xc0c0c0c0 && o.edx == 0xedededed &&
                  o.ebx == 0xb0b0b0b0 && o.esi == 0x51515151 && o.edi == 0xd1d1d1d1 && o.esp == o.site_esp &&
                  o.ebp == o.site_ebp && o.error == sentinel_error,
              label);
    }
}

// A buffer-backed Ops for the one-byte install/restore sequence's failure paths (the production CodeOps is driven
// through the image above).
struct FakeOps {
    static constexpr std::uint32_t writable = 0x40;
    unsigned char* mem;
    std::uintptr_t origin;
    bool fail_protect_at = false, corrupt_write = false, refuse_rollback_write = false;
    unsigned protect_calls = 0, protect_fail_call = 0, writes = 0;
    bool read(std::uintptr_t at, unsigned char* out, unsigned n) {
        std::memcpy(out, mem + (at - origin), n);
        return true;
    }
    bool protect(std::uintptr_t, unsigned, std::uint32_t, std::uint32_t* previous) {
        ++protect_calls;
        if (protect_fail_call && protect_calls >= protect_fail_call) return false; // sticky from that call on
        *previous = 0x20;
        return true;
    }
    bool write(std::uintptr_t at, const unsigned char* bytes, unsigned n, bool* atomic) {
        ++writes;
        *atomic = true;
        if (refuse_rollback_write && writes == 2) return false;
        std::memcpy(mem + (at - origin), bytes, n);
        if (corrupt_write && writes == 1) mem[at - origin] ^= 0x10;
        return true;
    }
};
void one_byte_sequences() {
    for (const L::Window* w : {&L::terran, &L::occlusion}) {
        const bool t = w == &L::terran;
        const std::uintptr_t fbase = 0x00410000;
        unsigned char mem[32];
        auto fresh = [&]() {
            std::memcpy(mem, w->bytes, w->length);
            if (w->fixup_offset) {
                const uint32_t v = uint32_t(fbase + w->fixup_rva);
                std::memcpy(mem + w->fixup_offset, &v, 4);
            }
        };
        bool live = false, atomic = false;
        std::uint32_t protection = 0;
        char label[96];
        // A read-back mismatch: rolled back, original byte, nothing live.
        fresh();
        FakeOps a{mem, fbase + w->rva};
        a.corrupt_write = true;
        const char* r = L::install(a, *w, fbase, true, &live, &atomic, &protection);
        std::snprintf(label, sizeof label, "%s_readback_rolled_back", t ? "terran" : "occlusion");
        check(!std::strcmp(r, "patch_rolled_back") && !live && mem[w->edit_offset] == w->original, label, r);
        // The rollback store refused: rollback_failed, kept live (registered).
        fresh();
        FakeOps b{mem, fbase + w->rva};
        b.corrupt_write = true;
        b.refuse_rollback_write = true;
        r = L::install(b, *w, fbase, true, &live, &atomic, &protection);
        std::snprintf(label, sizeof label, "%s_rollback_failed_registered", t ? "terran" : "occlusion");
        check(!std::strcmp(r, "rollback_failed") && live, label, r);
        // The re-protect after a good write refused: the byte goes back, the page stays writable.
        fresh();
        FakeOps c{mem, fbase + w->rva};
        c.protect_fail_call = 2;
        r = L::install(c, *w, fbase, true, &live, &atomic, &protection);
        std::snprintf(label, sizeof label, "%s_reprotect_refused", t ? "terran" : "occlusion");
        check(!std::strcmp(r, "rollback_unprotected") && !live && mem[w->edit_offset] == w->original, label, r);
        // The first protect refused: nothing written.
        fresh();
        FakeOps d{mem, fbase + w->rva};
        d.protect_fail_call = 1;
        r = L::install(d, *w, fbase, true, &live, &atomic, &protection);
        std::snprintf(label, sizeof label, "%s_protect_refused", t ? "terran" : "occlusion");
        check(!std::strcmp(r, "protect_failed") && !live && d.writes == 0, label, r);
        // Restore over a foreign byte: not owned, nothing written.
        fresh();
        mem[w->edit_offset] = 0x90;
        FakeOps e{mem, fbase + w->rva};
        unsigned char found = 0;
        bool found_read = false;
        r = L::restore(e, *w, fbase + w->rva + w->edit_offset, 0x20, &found, &found_read);
        std::snprintf(label, sizeof label, "%s_restore_not_owned", t ? "terran" : "occlusion");
        check(!std::strcmp(r, "restore_not_owned") && e.writes == 0 && found == 0x90, label, r);
        // Restore over the patched byte: the original back.
        fresh();
        mem[w->edit_offset] = w->patched;
        FakeOps f{mem, fbase + w->rva};
        r = L::restore(f, *w, fbase + w->rva + w->edit_offset, 0x20, &found, &found_read);
        std::snprintf(label, sizeof label, "%s_restore_owned", t ? "terran" : "occlusion");
        check(!std::strcmp(r, "restored") && mem[w->edit_offset] == w->original, label, r);
    }
}

// ns per pass through the site (harness included: pushal/popal and the frame set-up), for the report only.
double time_case(const Case& c, unsigned n = 200000) {
    run_cull(c, 0x002);
    LARGE_INTEGER f{}, t0{}, t1{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    for (unsigned i = 0; i < n; ++i) flc_run();
    QueryPerformanceCounter(&t1);
    return double(t1.QuadPart - t0.QuadPart) * 1e9 / double(f.QuadPart) / n;
}
double timing[4];
void set_env(const wchar_t* name, const wchar_t* value) {
    SetEnvironmentVariableW(name, value);
}
bool row_has(const char* label, unsigned before, unsigned added, const char* const* needles, unsigned count) {
    bool ok = rows == before + added;
    for (unsigned i = 0; i < count; ++i) ok = ok && std::strstr(last_row, needles[i]);
    check(ok, label, last_row);
    return ok;
}
void dump_stub(const char* variant, bool far_on) {
    const std::uintptr_t at = csp::stub_address();
    std::printf("STUB variant=%s at=%08x cull=%08x upper=%08x threshold=%08x culled=%08x exempt=%08x dock=%08x far=%08x "
                "far_on=%u hex=",
                variant, uint32_t(at), uint32_t(base + S::cull_rva), u32((const void*)&x3m_cull_small_parts_upper),
                u32((const void*)&x3m_cull_small_parts_threshold), u32((const void*)&x3m_cull_small_parts_culled),
                u32((const void*)&x3m_cull_small_parts_exempt), u32((const void*)&x3m_cull_small_parts_dock_culled),
                u32(reinterpret_cast<void*>(&x3m_engine_far_jet)), far_on ? 1u : 0u);
    for (unsigned i = 0; i < S::stub_length; ++i) std::printf("%02x", reinterpret_cast<const unsigned char*>(at)[i]);
    // The continuation slot follows the stub (4-aligned): it must hold the tail.
    std::printf("\n");
}
bool install_variant(const char* label, bool projectiles, bool far_on) {
    SetLastError(sentinel_error);
    const bool ok = csp::install_base(base, projectiles, far_on);
    check(ok && GetLastError() == sentinel_error && !std::strcmp(csp::state(), "ok") &&
              !std::strcmp(csp::site_write(), "atomic") && csp::site_claimed() && csp::projectiles_exempt() == projectiles &&
              csp::far_jets() == far_on && image[S::site_rva] == 0xe9 &&
              !std::memcmp(image + S::window_rva, cull_window_now, S::site_offset) &&
              !std::memcmp(image + S::site_rva + 5, cull_window_now + S::site_offset + 5,
                           S::window_length - S::site_offset - 5),
          label, csp::state());
    return ok;
}
bool shutdown_restores(const char* label) {
    SetLastError(sentinel_error);
    const bool ok = csp::shutdown();
    check(ok && GetLastError() == sentinel_error && cull_window_original() && !csp::site_claimed() &&
              !std::strcmp(csp::state(), "restored") && x3m_cull_small_parts_threshold == 0,
          label, csp::state());
    return ok;
}

// The far-jet hand-off under the FL layout: a camera (0x790-byte block, handle +0x28, context +0x1c), two contexts
// (+0x2c the float scale) and the node fields the handler copies. Decoys at the AP offsets: +0x140 holds the RCS model
// 566 (a handler reading AP's model offset counts the jet as steering and writes nothing) unless a case swaps them.
namespace far_chain {
namespace ee = x3m::engine_effects::core;
namespace fj = x3m::engine_far_jets;
struct alignas(16) Block {
    unsigned char bytes[0x790];
};
Block camera{}, context{}, other_context{};
constexpr uint32_t view_handle = 0x0000cafe, node_handle = 0x00001111, real_model = 1234;
constexpr float scale = 0.01f, other_scale = 0.5f;
uint32_t node_context = 0;   // node +0x1c
bool swap_models = false;    // +0x144 = 566, +0x140 = the real model
void put_block(Block& b, unsigned offset, uint32_t value) {
    std::memcpy(b.bytes + offset, &value, 4);
}
uint32_t bits(float f) {
    uint32_t v = 0;
    std::memcpy(&v, &f, 4);
    return v;
}
void fields(Node& n) {
    put(n, x3m::engine_far_jets::core::model_offset, swap_models ? real_model : 566u); // AP +0x140: the decoy
    put(n, S::model_offset, swap_models ? 566u : real_model);                          // FL +0x144
    put(n, 0x1c, node_context);
    put(n, 0x28, node_handle);
    put(n, 0x70, 200);
    put(n, 0x80, 0x10000);
    put(n, 0x84, 0x10000);
    put(n, 0x88, 0x18000);
    const int32_t position[3] = {100000, -200000, 300000};
    for (unsigned i = 0; i < 3; ++i) {
        put(n, 0xb0 + 4 * i, uint32_t(position[i]));
        put(n, 0xc0 + 4 * i, i == 0 ? 0x10000u : 0u); // basis row 0: model x
        put(n, 0xd0 + 4 * i, i == 1 ? 0x10000u : 0u);
        put(n, 0xe0 + 4 * i, i == 2 ? 0x10000u : 0u); // basis row 2: model z
    }
}
// s = 2 below T: the small path; +0x130 the JET pair; ECX 640 above the native limit (no engine verdict).
const Case jet = {"far_chain_jet", 2, 640, 0x00000004, 0x04000001, 0 /* fields() */, 1, true, 1};
Out run(uint32_t context_of_node, bool swap) {
    node_context = context_of_node;
    swap_models = swap;
    node_fields = &fields;
    far_forward = 1;
    fj::begin_frame();
    const Out o = run_cull(jet, 0x046, 4, u32(&camera));
    far_forward = 0;
    node_fields = nullptr;
    return o;
}
void checks() {
    std::memset(&camera, 0x3c, sizeof camera);
    std::memset(&context, 0x4d, sizeof context);
    std::memset(&other_context, 0x5e, sizeof other_context);
    put_block(camera, 0x28, view_handle);
    put_block(camera, 0x1c, u32(&context));
    put_block(context, 0x2c, bits(scale));
    put_block(other_context, 0x2c, bits(other_scale));
    bool counted = false;
    const int device = 0;
    fj::request(&counted, true, &device);
    x3m_cull_small_parts_threshold = T;
    x3m_cull_small_parts_upper = U;
    // 1. Same context on node and view: one entry, every field from the FL offsets, the owned scale; the stub's frame
    // kept and the node culled exactly as without the handler.
    const uint32_t calls0 = far_calls, culled0 = x3m_cull_small_parts_culled;
    Out o = run(u32(&context), false);
    const fj::core::Raw* raw = fj::entries();
    const fj::core::Stats st = fj::stats();
    char detail[320];
    std::snprintf(detail, sizeof detail, "count=%u written=%u steering=%u engine=%u disarmed=%u model=%u scale=%08x kind=%u",
                  fj::count(), st.written, st.steering, st.engine, st.disarmed, fj::count() ? raw[0].model : 0u,
                  fj::count() ? fj::context_scale_bits()[0] : 0u, o.kind);
    const bool copied =
        fj::count() == 1 && st.written == 1 && raw[0].node == u32(&node) && raw[0].view_handle == view_handle &&
        raw[0].context == u32(&context) && raw[0].parent == u32(&parent) && raw[0].handle == node_handle &&
        raw[0].model == real_model && raw[0].scale70 == 200 && raw[0].scale80 == 0x10000 &&
        raw[0].scale88 == 0x18000 && raw[0].position[0] == 100000 && raw[0].position[1] == -200000 &&
        raw[0].position[2] == 300000 && raw[0].basis_x[0] == 0x10000 && raw[0].basis_z[2] == 0x10000 &&
        fj::context_scale_bits()[0] == bits(scale);
    check(copied && far_calls == calls0 + 1 && x3m_cull_small_parts_culled == culled0 + 1 && o.kind == 2 &&
              o.ebx == u32(&camera) && o.esi == u32(&node) && o.ecx == jet.ecx && o.edi == d_value &&
              o.esp == o.site_esp && o.ebp == o.site_ebp && o.f12c == (jet.f12c & ~2u) && !(o.flags & df) &&
              o.error == sentinel_error && far_view == u32(&camera),
          "far_chain_fl_layout_copied", detail);
    // 2. The consumer's call (motion_output_engine_plumes_inc.h engine_far_append on FL): far_record with the owned
    // scale -> a far plume record at position x 0.01.
    float owned = 0.f;
    std::memcpy(&owned, &fj::context_scale_bits()[0], 4);
    ee::Record record{};
    const ee::FarVerdict verdict = ee::far_record(raw[0], owned, -1, nullptr, 9, &record);
    std::snprintf(detail, sizeof detail, "verdict=%u origin=%.3f,%.3f,%.3f size=%.4f axis_z=%.3f flags=%04x",
                  unsigned(verdict), double(record.origin[0]), double(record.origin[1]), double(record.origin[2]),
                  double(record.size), double(record.axis[2]), unsigned(record.flags));
    const auto close_to = [](float a, float b) { return a - b < 1e-3f && b - a < 1e-3f; };
    check(verdict == ee::FarVerdict::record && close_to(record.origin[0], 1000.f) && close_to(record.origin[1], -2000.f) &&
              close_to(record.origin[2], 3000.f) && close_to(record.size, 2.f) && record.axis[2] == -1.f &&
              (record.flags & ee::flag_far) && record.node_handle == node_handle && record.model == real_model,
          "far_chain_record_reaches_consumer", detail);
    std::printf("FARCHAIN record origin=%.1f,%.1f,%.1f size=%.4f scale=%08x\n", double(record.origin[0]),
                double(record.origin[1]), double(record.origin[2]), double(record.size), fj::context_scale_bits()[0]);
    // 3. The node's context differs from the view's: the entry stays, its owned scale is 0 and the consumer refuses.
    o = run(u32(&other_context), false);
    std::memcpy(&owned, &fj::context_scale_bits()[0], 4);
    check(fj::count() == 1 && fj::context_scale_bits()[0] == 0 &&
              ee::far_record(fj::entries()[0], owned, -1, nullptr, 9, &record) == ee::FarVerdict::invalid && o.kind == 2,
          "far_chain_foreign_context_refused");
    // 4. No node context: the same refusal (no read through a null context).
    o = run(0, false);
    check(fj::count() == 1 && fj::context_scale_bits()[0] == 0 && o.kind == 2, "far_chain_null_context_refused");
    // 5. The AP model offset holds the real id, FL's holds the RCS 566: the handler reads FL's, skips it as steering.
    o = run(u32(&context), true);
    check(fj::count() == 0 && fj::stats().steering == 1 && o.kind == 2, "far_chain_model_is_fl_offset");
    // 6. Disarmed (no device requests the stage): nothing copied, the node still culled.
    fj::request(&counted, false, &device);
    o = run(u32(&context), false);
    check(fj::count() == 0 && fj::stats().disarmed == 1 && o.kind == 2, "far_chain_disarmed");
    x3m_cull_small_parts_threshold = 0;
    x3m_cull_small_parts_upper = 0;
}
} // namespace far_chain

int main() {
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    base = reinterpret_cast<std::uintptr_t>(image);
    check(image && !(base & 0xffff), "image_64k_aligned");
    if (!image) return 1;
    lay_out();
    std::memcpy(cull_window_now, image + S::window_rva, S::window_length);
    std::memcpy(terran_now, image + L::terran.rva, 31);
    std::memcpy(occlusion_now, image + L::occlusion.rva, 20);
    {
        unsigned char masked[20];
        std::memcpy(masked, occlusion_now, 20);
        std::memset(masked + L::occlusion.fixup_offset, 0, 4);
        std::printf("WINDOWS cull_fnv1a=%08x terran_fnv1a=%08x occlusion_fnv1a_masked=%08x marker_store=%08x "
                    "marker_or=%08x jet_writer=%08x base=%08x\n",
                    S::fnv1a(cull_window_now, S::window_length), L::fnv1a(terran_now, 31), L::fnv1a(masked, 20),
                    S::fnv1a(image + S::marker_store_rva, S::marker_store_length),
                    S::fnv1a(image + S::marker_or_rva, S::marker_or_length),
                    S::fnv1a(image + S::jet_writer_rva, S::jet_writer_length), uint32_t(base));
        check(S::fnv1a(cull_window_now, S::window_length) == 0x9df9cf9e && L::fnv1a(terran_now, 31) == 0x0e8e0d62 &&
                  L::fnv1a(masked, 20) == 0x3105ccaa,
              "window_hashes_measured");
    }

    // 1. The native image: every case's reference frame, and the unpatched LOD paths.
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) native_out[i] = run_cull(cases[i], case_flags(i), i & 1 ? 4 : 0);
    check(native_out[19].kind == 2 && native_out[4].kind == 1 && native_out[0].kind == 1, "native_reference_paths");
    timing[0] = time_case(cases[4]);
    lod_paths("native", false, false);

    // 2. Small-parts refusals before any write.
    {
        auto refused = [&](const char* label, std::uintptr_t at, const char* state) {
            SetLastError(sentinel_error);
            const bool ok = csp::install_base(at, true, true);
            check(!ok && GetLastError() == sentinel_error && !std::strcmp(csp::state(), state) && cull_window_original() &&
                      !csp::site_claimed(),
                  label, csp::state());
        };
        refused("cull_invalid_base_zero", 0, "invalid_site");
        refused("cull_invalid_base_unaligned", base + 0x1000, "invalid_site");
        unsigned flips = 0, refused_flips = 0;
        for (unsigned i = 0; i < S::window_length * 8; ++i) {
            unsigned char* p = image + S::window_rva + i / 8;
            writable(true);
            *p ^= static_cast<unsigned char>(1u << (i % 8));
            writable(false);
            unsigned char flipped[S::window_length];
            std::memcpy(flipped, image + S::window_rva, S::window_length);
            SetLastError(sentinel_error);
            const bool ok = csp::install_base(base, true, true);
            refused_flips += !ok && GetLastError() == sentinel_error && !std::strcmp(csp::state(), "bytes_mismatch") &&
                             !std::memcmp(image + S::window_rva, flipped, S::window_length) && !csp::site_claimed();
            writable(true);
            *p ^= static_cast<unsigned char>(1u << (i % 8));
            writable(false);
            ++flips;
        }
        std::printf("FLIPS cull=%u refused=%u\n", flips, refused_flips);
        check(flips == 440 && refused_flips == flips && cull_window_original(), "cull_every_window_bit_refused");
    }

    // 3. Install (all blocks), the decisions, the disarmed frame, then the two reduced variants.
    if (install_variant("cull_installed_atomic", true, true)) {
        dump_stub("full", true);
        check(!csp::install_base(base, true, true) && !std::strcmp(csp::state(), "already_installed"),
              "cull_second_install_refused");
        run_decisions("full", true, true, true);
        run_decisions("disarmed", false, true, true);
        timing[1] = time_case(cases[4]); // disarmed (upper 0)
        x3m_cull_small_parts_threshold = T;
        x3m_cull_small_parts_upper = U;
        timing[2] = time_case(cases[4]); // armed, s >= upper: the engine's compare
        timing[3] = time_case(cases[0]); // armed, culled
        x3m_cull_small_parts_threshold = 0;
        x3m_cull_small_parts_upper = 0;
        std::printf("TIMING native_ns=%.1f disarmed_ns=%.1f armed_continue_ns=%.1f armed_cull_ns=%.1f\n", timing[0],
                    timing[1], timing[2], timing[3]);
        // chain_stub / site_chainable refuse every other consumer on FL (their stubs encode AP's registers).
        const char* status = nullptr;
        check(!csp::chain_stub(base + S::site_rva, base + S::cull_rva, nullptr, nullptr, &status) &&
                  !std::strcmp(status, "edition_fl") &&
                  !csp::site_chainable(base + S::site_rva, base + S::cull_rva, &status) && !std::strcmp(status, "edition_fl"),
              "cull_no_foreign_chain");
        shutdown_restores("cull_shutdown_restores");
        x3m_cull_small_parts_threshold = T;
        x3m_cull_small_parts_upper = U;
        for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
            const Out o = run_cull(cases[i], case_flags(i), i & 1 ? 4 : 0);
            check(same_registers(o, native_out[i], true), "cull_after_shutdown_native", cases[i].label);
        }
    }
    if (install_variant("cull_installed_no_projectiles", false, true)) {
        dump_stub("no_projectiles", true);
        run_decisions("no_projectiles", true, false, true);
        shutdown_restores("cull_shutdown_no_projectiles");
    }
    if (install_variant("cull_installed_no_far", true, false)) {
        dump_stub("no_far", false);
        run_decisions("no_far", true, true, false);
        shutdown_restores("cull_shutdown_no_far");
    }
    std::printf("DECISIONS culled=%u dock=%u exempt=%u far=%u native=%u\n", culled_total, dock_total, exempt_total,
                far_total, native_total);

    // 4. Partial installs (seam faults).
    {
        fault_read_at = base + S::site_rva;
        SetLastError(sentinel_error);
        check(!csp::install_base(base, true, true) && GetLastError() == sentinel_error &&
                  !std::strcmp(csp::state(), "readback_rolled_back") && cull_window_original() && !csp::site_claimed(),
              "cull_readback_rolled_back", csp::state());
        fault_push = true;
        check(!csp::install_base(base, true, true) && !std::strcmp(csp::state(), "chain_failed") && cull_window_original() &&
                  !csp::site_claimed(),
              "cull_chain_failed_rolled_back", csp::state());
        fault_read_at = base + S::site_rva;
        fault_restore = true;
        check(!csp::install_base(base, true, true) && !std::strcmp(csp::state(), "rollback_failed") &&
                  csp::site_claimed() && image[S::site_rva] == 0xe9,
              "cull_rollback_failed_registered", csp::state());
        check(!csp::install_base(base, true, true) && !std::strcmp(csp::state(), "rollback_failed"),
              "cull_registered_refuses_reinstall");
        {
            // The registered jump leads through the dispatcher to the tail: every case still runs natively.
            x3m_cull_small_parts_threshold = T;
            x3m_cull_small_parts_upper = U;
            bool all = true;
            for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i)
                all = all && same_registers(run_cull(cases[i], case_flags(i), i & 1 ? 4 : 0), native_out[i], true);
            check(all, "cull_registered_jump_runs_tail_only");
        }
        shutdown_restores("cull_registered_restored");
        check(faults_taken == 4, "cull_faults_taken");
    }

    // 5. initialize(): rows and refusals; then the full path through the fixture base.
    {
        set_env(L"X3M_ENGINE_EFFECTS", L"plumes");
        set_env(L"X3M_CULL_SMALL_PARTS_PX", nullptr);
        unsigned before = rows;
        check(!csp::initialize() && rows == before && !std::strcmp(csp::state(), "disabled"), "init_unset_no_row");
        struct Init {
            const char* label;
            const wchar_t *px, *projectiles, *dock;
            const char* needle;
        } inits[] = {
            {"init_zero_off", L"0", nullptr, nullptr, "edition=fl state=off reason=disabled"},
            {"init_invalid_px", L"4px", nullptr, nullptr, "state=refused reason=invalid_px"},
            {"init_px_over_band", L"65", nullptr, nullptr, "state=refused reason=invalid_px"},
            {"init_invalid_projectiles", L"4", L"maybe", nullptr, "state=refused reason=invalid_projectiles"},
            {"init_invalid_dock", L"4", nullptr, L"100", "state=refused reason=invalid_dock_px"},
            {"init_executable_mismatch", L"4", nullptr, L"12",
             "state=refused reason=executable_mismatch site=0x00000000 rva=0x0961d3"},
        };
        for (const Init& i : inits) {
            set_env(L"X3M_CULL_SMALL_PARTS_PX", i.px);
            set_env(L"X3M_CULL_SMALL_PARTS_PROJECTILES", i.projectiles);
            set_env(L"X3M_CULL_DOCK_PARTS_PX", i.dock);
            before = rows;
            SetLastError(sentinel_error);
            const bool applied = csp::initialize();
            check(!applied && GetLastError() == sentinel_error && rows == before + 1 &&
                      std::strstr(last_row, "cull_small_parts requested=") && std::strstr(last_row, i.needle) &&
                      std::strstr(last_row, "write=none") && cull_window_original(),
                  i.label, last_row);
        }
        set_env(L"X3M_CULL_SMALL_PARTS_PROJECTILES", nullptr);
        // Through the fixture base: the marker and JET writer pinned; a changed one drops only its block.
        csp::fixture_base(base);
        set_env(L"X3M_CULL_SMALL_PARTS_PX", L"4");
        set_env(L"X3M_CULL_DOCK_PARTS_PX", L"12");
        poke(S::marker_or_rva + 9, 0x31);
        before = rows;
        SetLastError(sentinel_error);
        bool applied = csp::initialize();
        const char* mismatch[] = {"edition=fl state=active reason=ok", "write=atomic", "projectiles=marker_mismatch",
                                  "far_jets=on", "rva=0x0961d3"};
        check(applied && GetLastError() == sentinel_error && !csp::projectiles_exempt() && csp::far_jets(),
              "init_mismatched_marker_drops_exemption", last_row);
        row_has("init_mismatch_row", before, 1, mismatch, 5);
        shutdown_restores("init_mismatch_restored");
        poke(S::marker_or_rva + 9, S::marker_or[9]);
        // A changed JET writer drops only the far block (as on AP: far_jets=writer_mismatch).
        poke(S::jet_writer_rva + 6, 0x02);
        before = rows;
        SetLastError(sentinel_error);
        applied = csp::initialize();
        const char* writer[] = {"edition=fl state=active reason=ok", "projectiles=on", "far_jets=writer_mismatch"};
        check(applied && GetLastError() == sentinel_error && csp::projectiles_exempt() && !csp::far_jets() &&
                  reinterpret_cast<const unsigned char*>(csp::stub_address())[S::stub_far] == 0xeb,
              "init_jet_writer_mismatch_drops_far", last_row);
        row_has("init_writer_mismatch_row", before, 1, writer, 3);
        shutdown_restores("init_writer_mismatch_restored");
        poke(S::jet_writer_rva + 6, S::jet_writer[6]);
        // The far-block gate resolves X3M_ENGINE_EFFECTS as the engine_effects option does (option::resolve_mode):
        // unset or empty = plumes (the default) and plumes request the block; native, off and refused words do not.
        {
            struct Gate {
                const char* label;
                const wchar_t* value;
                bool far_on;
            } gates[] = {
                {"init_far_gate_unset_on", nullptr, true},   {"init_far_gate_empty_on", L"", true},
                {"init_far_gate_plumes_on", L"plumes", true}, {"init_far_gate_native_off", L"native", false},
                {"init_far_gate_off_off", L"off", false},     {"init_far_gate_mixed_case_off", L"Plumes", false},
                {"init_far_gate_invalid_off", L"plumes2", false},
            };
            for (const Gate& g : gates) {
                set_env(L"X3M_ENGINE_EFFECTS", g.value);
                before = rows;
                SetLastError(sentinel_error);
                applied = csp::initialize();
                const char* row[] = {"edition=fl state=active reason=ok", g.far_on ? " far_jets=on" : " far_jets=off"};
                check(applied && GetLastError() == sentinel_error && csp::far_jets() == g.far_on &&
                          reinterpret_cast<const unsigned char*>(csp::stub_address())[S::stub_far] ==
                              (g.far_on ? 0xf7 : 0xeb),
                      g.label, last_row);
                row_has(g.label, before, 1, row, 2);
                shutdown_restores(g.label);
            }
            set_env(L"X3M_ENGINE_EFFECTS", L"plumes");
        }
        before = rows;
        SetLastError(sentinel_error);
        applied = csp::initialize();
        char want[200];
        std::snprintf(want, sizeof want, "site=0x%08lx rva=0x0961d3 cull=0x%08lx write=atomic",
                      static_cast<unsigned long>(base + S::site_rva), static_cast<unsigned long>(base + S::cull_rva));
        const char* active[] = {"cull_small_parts requested=4 px=4 patched=1 edition=fl state=active reason=ok", want,
                                "projectiles=on dock_px=12 dock_requested=12 far_jets=on"};
        // Far jets on FL: plumes requested and the JET writer verified, the stub carries the far block.
        check(applied && GetLastError() == sentinel_error && csp::projectiles_exempt() && csp::far_jets() &&
                  reinterpret_cast<const unsigned char*>(csp::stub_address())[S::stub_far] == 0xf7,
              "init_installs_far_block", last_row);
        row_has("init_active_row", before, 1, active, 3);
        far_chain::checks(); // the live stub's far block into the real handler, then the consumer's record
        check(csp::initialize() && rows == before + 1, "init_again_no_row");
        // The frame side: a vanilla frame (no camera) publishes 0; a valid projection the AP threshold rule.
        csp::set_backbuffer_width(1920);
        camera_valid = false;
        csp::begin_frame();
        check(x3m_cull_small_parts_threshold == 0 && x3m_cull_small_parts_upper == 0, "frame_without_camera_vanilla");
        camera_valid = true;
        before = rows;
        SetLastError(sentinel_error);
        csp::begin_frame();
        const int32_t t = small::threshold_for(4.0, camera_m00, 1920), d = small::threshold_for(12.0, camera_m00, 1920);
        check(GetLastError() == sentinel_error && t > 0 && x3m_cull_small_parts_threshold == t &&
                  x3m_cull_small_parts_upper == small::upper_for(t, d) && census_threshold == t &&
                  census_upper == small::upper_for(t, d) && rows == before + 1 &&
                  std::strstr(last_row, "cull_small_parts_value px=4 ") && std::strstr(last_row, " edition=fl"),
              "frame_threshold_published", last_row);
        x3m_cull_small_parts_culled = 3;
        x3m_cull_small_parts_dock_culled = 2;
        x3m_cull_small_parts_exempt = 1;
        before = rows;
        csp::present(1, 77, true);
        check(rows == before + 1 && std::strstr(last_row, "cull_small_parts_frame device=1 frame=77 px=4 ") &&
                  std::strstr(last_row, " culled=3 ") && std::strstr(last_row, " exempt_bullet=1 ") &&
                  std::strstr(last_row, " dock_culled=2 far_jets=on edition=fl") && x3m_cull_small_parts_culled == 0 &&
                  x3m_cull_small_parts_dock_culled == 0 && x3m_cull_small_parts_exempt == 0,
              "frame_row_and_counters", last_row);
        csp::present(1, 78, false);
        check(rows == before + 1, "frame_uncaptured_no_row");
        csp::after_reset(2560);
        check(x3m_cull_small_parts_threshold == 0 && x3m_cull_small_parts_upper == 0 && census_threshold == 0,
              "reset_disarms");
        shutdown_restores("init_install_restored");
        csp::fixture_base(0);
        // Off = nothing runs per frame: begin_frame/present on an uninstalled module write nothing.
        x3m_cull_small_parts_threshold = 0;
        csp::begin_frame();
        csp::present(1, 79, true);
        check(x3m_cull_small_parts_threshold == 0 && rows == before + 1, "off_frame_side_inert");
    }

    // 6. Terran / occlusion: refusals, flips, install, the patched paths, shutdown, rows.
    {
        namespace tl = x3m::terran_station_lod;
        namespace lo = x3m::lod_occlusion;
        unsigned t_flips = 0, t_refused = 0, o_flips = 0, o_refused = 0, fixup_flips = 0, fixup_refused = 0;
        for (unsigned w = 0; w < 2; ++w) {
            const L::Window& win = w ? L::occlusion : L::terran;
            for (unsigned i = 0; i < win.length * 8; ++i) {
                unsigned char* p = image + win.rva + i / 8;
                writable(true);
                *p ^= static_cast<unsigned char>(1u << (i % 8));
                writable(false);
                SetLastError(sentinel_error);
                const bool ok = w ? lo::install_base(base) : tl::install_base(base);
                const bool pass = !ok && GetLastError() == sentinel_error &&
                                  !std::strcmp(w ? lo::state() : tl::state(), "bytes_mismatch") &&
                                  !std::strcmp(w ? lo::write_path() : tl::write_path(), "none");
                writable(true);
                *p ^= static_cast<unsigned char>(1u << (i % 8));
                writable(false);
                const bool fixup = win.fixup_offset && i / 8 >= win.fixup_offset && i / 8 < win.fixup_offset + 4;
                (w ? o_flips : t_flips) += 1;
                (w ? o_refused : t_refused) += pass;
                fixup_flips += fixup;
                fixup_refused += fixup && pass;
            }
        }
        std::printf("FLIPS terran=%u refused=%u occlusion=%u refused=%u fixup=%u fixup_refused=%u\n", t_flips, t_refused,
                    o_flips, o_refused, fixup_flips, fixup_refused);
        check(t_flips == 248 && t_refused == 248 && o_flips == 160 && o_refused == 160 && fixup_refused == 32 &&
                  lod_windows_original(),
              "lod_every_window_bit_refused");
        // The fixup zeroed (the hashing form) or rebased to another base: refused.
        for (uint32_t value : {0u, uint32_t(base + 0x10000 + L::occlusion.fixup_rva)}) {
            writable(true);
            std::memcpy(image + L::occlusion.rva + L::occlusion.fixup_offset, &value, 4);
            writable(false);
            check(!lo::install_base(base) && !std::strcmp(lo::state(), "bytes_mismatch"),
                  value ? "occlusion_fixup_other_base_refused" : "occlusion_fixup_zeroed_refused");
        }
        writable(true);
        std::memcpy(image + L::occlusion.rva, occlusion_now, 20);
        writable(false);
        check(!L::plan(L::occlusion, occlusion_now, base) && L::plan(L::occlusion, occlusion_now, base + 0x10000) &&
                  !L::plan(L::terran, terran_now, base),
              "occlusion_plan_binds_fixup_to_base");
        check(!tl::install_base(0) && !std::strcmp(tl::state(), "invalid_site") && !lo::install_base(base + 0x1000) &&
                  !std::strcmp(lo::state(), "invalid_site"),
              "lod_invalid_base");
        one_byte_sequences();
        // Rows and install through initialize() (the fixture base).
        set_env(L"X3M_TERRAN_STATION_LOD", L"distance");
        set_env(L"X3M_LOD_OCCLUSION", L"record0");
        unsigned before = rows;
        check(!tl::initialize() && std::strstr(last_row, "edition=fl state=off reason=distance mode=distance") &&
                  !lo::initialize() && std::strstr(last_row, "edition=fl state=off reason=record0 mode=record0") &&
                  rows == before + 2 && lod_windows_original(),
              "lod_off_rows", last_row);
        set_env(L"X3M_TERRAN_STATION_LOD", L"Size");
        set_env(L"X3M_LOD_OCCLUSION", L"allallallallallallallallallallallall");
        check(!tl::initialize() && std::strstr(last_row, "state=refused reason=invalid_setting mode=- setting=Size") &&
                  !lo::initialize() && std::strstr(last_row, "state=refused reason=too_long mode=- setting=?"),
              "lod_refused_rows", last_row);
        set_env(L"X3M_TERRAN_STATION_LOD", nullptr);
        set_env(L"X3M_LOD_OCCLUSION", L"all");
        set_env(L"X3M_LOD_OCCLUSION_DEFAULT", L"1");
        check(!tl::initialize() && std::strstr(last_row, "site=00000000 rva=095e98 edition=fl state=refused "
                                                         "reason=executable_mismatch mode=size setting=- write=none") &&
                  !lo::initialize() && std::strstr(last_row, "site=00000000 rva=0e56c1 edition=fl state=refused "
                                                             "reason=executable_mismatch mode=all setting=all write=none default=1"),
              "lod_executable_mismatch_rows", last_row);
        tl::fixture_base(base);
        lo::fixture_base(base);
        SetLastError(sentinel_error);
        bool t_ok = tl::initialize();
        char want[200];
        std::snprintf(want, sizeof want,
                      "terran_station_lod site=%08lx rva=095e98 edition=fl state=patched reason=ok mode=size setting=- "
                      "write=atomic",
                      static_cast<unsigned long>(base + 0x095e98));
        check(t_ok && GetLastError() == sentinel_error && !std::strcmp(last_row, want) && image[0x095e98] == 0xc0 &&
                  !std::memcmp(image + L::terran.rva, terran_now, L::terran.edit_offset) &&
                  !std::memcmp(image + 0x095e99, terran_now + L::terran.edit_offset + 1, 31 - L::terran.edit_offset - 1),
              "terran_installed_one_byte", last_row);
        bool o_ok = lo::initialize();
        std::snprintf(want, sizeof want,
                      "lod_occlusion site=%08lx rva=0e56c1 edition=fl state=patched reason=ok mode=all setting=all "
                      "write=atomic default=1",
                      static_cast<unsigned long>(base + 0x0e56c1));
        check(o_ok && GetLastError() == sentinel_error && !std::strcmp(last_row, want) && image[0x0e56c1] == 0x00 &&
                  !std::memcmp(image + L::occlusion.rva, occlusion_now, L::occlusion.edit_offset) &&
                  !std::memcmp(image + 0x0e56c2, occlusion_now + L::occlusion.edit_offset + 1, 20 - 17),
              "occlusion_installed_one_byte", last_row);
        check(tl::initialize() && lo::initialize() && !tl::install_base(base) &&
                  !std::strcmp(tl::state(), "already_installed"),
              "lod_second_install_refused");
        lod_paths("patched", true, true);
        SetLastError(sentinel_error);
        check(tl::shutdown() && lo::shutdown() && GetLastError() == sentinel_error && lod_windows_original() &&
                  !std::strcmp(tl::state(), "restored") && !std::strcmp(lo::state(), "restored"),
              "lod_shutdown_restores");
        lod_paths("restored", false, false);
        // A foreign byte at shutdown: not owned, nothing written, still registered.
        check(tl::install_base(base), "terran_reinstalled");
        poke(0x095e98, 0xc3);
        check(!tl::shutdown() && !std::strcmp(tl::state(), "restore_not_owned") && tl::patched() &&
                  image[0x095e98] == 0xc3,
              "terran_restore_not_owned");
        poke(0x095e98, 0xc0);
        check(tl::shutdown() && lod_windows_original(), "terran_restored_after_foreign");
        tl::fixture_base(0);
        lo::fixture_base(0);
        set_env(L"X3M_LOD_OCCLUSION_DEFAULT", nullptr);
    }

    // 7. Late window: after the first Present nothing is claimed by any of the three.
    x3m::engine_patch::close_install_window("fixture_present");
    const unsigned arena_before = x3m::engine_patch::arena_used();
    SetLastError(sentinel_error);
    check(!csp::install_base(base, true, true) && !std::strcmp(csp::state(), "late_claim") &&
              x3m::engine_patch::arena_used() == arena_before && // refused before a stub is emitted
              !x3m::terran_station_lod::install_base(base) &&
              !std::strcmp(x3m::terran_station_lod::state(), "late_claim") && !x3m::lod_occlusion::install_base(base) &&
              !std::strcmp(x3m::lod_occlusion::state(), "late_claim") && GetLastError() == sentinel_error &&
              cull_window_original() && lod_windows_original(),
          "late_claim_refused");

    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
