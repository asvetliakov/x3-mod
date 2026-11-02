// Original synthetic fixture of the FL chase lead marker and central HUD (src/engine/fl/chase_lead_fl.cpp built with
// X3M_EDITION_FL=1, src/engine/fl/chase_lead_sites_fl.h; docs/reverse-engineering/fl/chase-lead.md, "Verified
// 2026-11-02"): no game code beyond the twelve identity windows of the sites header, no launch. Built with
// -DX3M_EDITION_FL=1 -DX3M_CHASE_LEAD_FL_FIXTURE by verification/analysis/test_fl_chase_lead.py and run natively (WSL
// interop).
//
// A 64 KB-aligned private image stands in for X3FL.exe at `base`: the twelve windows at base + RVA with their
// relocated dwords = base + target, and the image globals (registry, defaults, texture tables) pointing at a synthetic
// cockpit, overlay, cameras, nodes, ship, target, sector and scene. After the install, `push imm32; ret` recorders
// replace each seam's continuation and branch target, and the detacher the native hide helper calls (base + 0x0a5740).
// A harness enters each seam on a private stack (ESP = the overlay's local base P, the solver point at P+0xe0 and a
// decoy at AP's P+0xc0) with the seam's register ABI, sentinels elsewhere, all arithmetic flags and DF set, eight XMM
// sentinels, a loaded x87 stack with a non-default control word, a non-default MXCSR, a LastError sentinel and every
// 4-byte ESP skew; the recorders capture what the native continuation would see.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../../src/proxy/chase_lead.h"
#include "../../src/proxy/chase_camera_math.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/engine/fl/chase_lead_sites_fl.h"
namespace cl = x3m::chase_lead;
namespace S = x3m::fl::chase_lead_fl;
using std::uint32_t;
unsigned checks = 0, failures = 0;
void check(bool value, const char* label, const char* detail = "") {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s %s\n", label, detail);
    }
}
char last_row[2048];
unsigned rows = 0;
bool quiet = false;
bool camera_installed = true;
namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(last_row, sizeof last_row, format, args);
    va_end(args);
    ++rows;
    if (!quiet) std::printf("LOG %s\n", last_row);
}
namespace telemetry {
bool enabled() {
    return true;
}
}
namespace chase_camera {
bool installed() {
    return camera_installed;
}
}
}

extern "C" {
uint32_t in_eax, in_ebx, in_ecx, in_edx, in_esi, in_edi, in_ebp, in_esp, in_flags, site_addr, saved_esp, esp_at_jump,
    ref_cmp_flags;
uint32_t in_mxcsr, host_mxcsr, out_mxcsr;
std::uint16_t in_fpucw, out_fpucw, out_fpusw;
double in_st0, out_st0;
alignas(16) unsigned char in_xmm[128], out_xmm[128];
uint32_t out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, out_esp, out_flags, out_branch;
uint32_t detach_ecx, detach_arg, detach_calls;
void fl_cl_enter();
void fl_cl_fall();
void fl_cl_taken();
void fl_cl_detach();
}
#define XMM_IN(i) "movups _in_xmm+" #i "*16,%xmm" #i "\n\t"
#define XMM_OUT(i) "movups %xmm" #i ",_out_xmm+" #i "*16\n\t"
asm(".text\n\t"
    ".globl _fl_cl_enter\n_fl_cl_enter:\n\t"
    "pushal\n\tpushfl\n\tmovl %esp,_saved_esp\n\t"
    "stmxcsr _host_mxcsr\n\tfninit\n\tfldcw _in_fpucw\n\tfldl _in_st0\n\tldmxcsr _in_mxcsr\n\t"
    XMM_IN(0) XMM_IN(1) XMM_IN(2) XMM_IN(3) XMM_IN(4) XMM_IN(5) XMM_IN(6) XMM_IN(7)
    "movl _in_esp,%esp\n\t"
    "movl _in_ebx,%ebx\n\tmovl _in_ecx,%ecx\n\tmovl _in_edx,%edx\n\tmovl _in_esi,%esi\n\tmovl _in_edi,%edi\n\t"
    "movl _in_ebp,%ebp\n\t"
    "pushl _in_flags\n\tpopfl\n\t"
    // The final-FOV tail's compare on the same memory and flags (the reference for that seam).
    "pushfl\n\tcmpl %esi,0x240(%ebx)\n\tpushfl\n\tpopl _ref_cmp_flags\n\tpopfl\n\t"
    "movl _in_eax,%eax\n\tmovl %esp,_esp_at_jump\n\t"
    "jmp *_site_addr\n\t"
    ".globl _fl_cl_fall\n_fl_cl_fall:\n\t"
    "pushfl\n\tpopl _out_flags\n\tmovl $1,_out_branch\n\tjmp .Lfl_cl_record\n\t"
    ".globl _fl_cl_taken\n_fl_cl_taken:\n\t"
    "pushfl\n\tpopl _out_flags\n\tmovl $2,_out_branch\n"
    ".Lfl_cl_record:\n\t"
    "movl %eax,_out_eax\n\tmovl %ebx,_out_ebx\n\tmovl %ecx,_out_ecx\n\tmovl %edx,_out_edx\n\tmovl %esi,_out_esi\n\t"
    "movl %edi,_out_edi\n\tmovl %ebp,_out_ebp\n\tmovl %esp,_out_esp\n\t"
    XMM_OUT(0) XMM_OUT(1) XMM_OUT(2) XMM_OUT(3) XMM_OUT(4) XMM_OUT(5) XMM_OUT(6) XMM_OUT(7)
    "stmxcsr _out_mxcsr\n\tfnstcw _out_fpucw\n\tfnstsw _out_fpusw\n\tfstpl _out_st0\n\t"
    "fninit\n\tldmxcsr _host_mxcsr\n\tcld\n\t"
    "movl _saved_esp,%esp\n\tpopfl\n\tpopal\n\tret\n\t"
    // The detacher the native hide helper calls: ECX = node, one stack argument (the node's scene), RET 4.
    ".globl _fl_cl_detach\n_fl_cl_detach:\n\t"
    "movl %ecx,_detach_ecx\n\tmovl 4(%esp),%eax\n\tmovl %eax,_detach_arg\n\tincl _detach_calls\n\tret $4\n\t");

constexpr std::size_t image_size = 0x230000;
unsigned char* image = nullptr;
std::uintptr_t base = 0;
unsigned char* stack = nullptr;
uint32_t addr(const void* p) {
    return uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
void put32(unsigned char* p, unsigned off, uint32_t v) {
    std::memcpy(p + off, &v, 4);
}
void put16(unsigned char* p, unsigned off, std::uint16_t v) {
    std::memcpy(p + off, &v, 2);
}
uint32_t get32(const unsigned char* p, unsigned off) {
    uint32_t v;
    std::memcpy(&v, p + off, 4);
    return v;
}
std::int32_t gets(const unsigned char* p, unsigned off) {
    return std::int32_t(get32(p, off));
}
const S::Seam* const seams[4] = {&S::gate, &S::publish, &S::final_fov, &S::hud_gate};
unsigned char original_seam[4][6];
bool seams_original() {
    for (unsigned i = 0; i < 4; ++i)
        if (std::memcmp(image + seams[i]->rva, original_seam[i], 6)) return false;
    return true;
}
void lay_window(const S::Window& w) {
    std::memcpy(image + w.rva, w.bytes, w.length);
    for (unsigned r = 0; r < w.reloc_count; ++r) put32(image, w.rva + w.reloc_offset[r], uint32_t(base + w.reloc_target[r]));
}
void lay_out() {
    for (const auto& w : S::windows) lay_window(w);
}
void recorder(std::uint32_t rva, void (*target)()) {
    unsigned char* p = image + rva;
    p[0] = 0x68;
    put32(p, 1, addr(reinterpret_cast<void*>(target)));
    p[5] = 0xc3;
    FlushInstructionCache(GetCurrentProcess(), p, 6);
}

// The synthetic engine objects.
struct Scene {
    unsigned char *cockpit, *overlay, *camera, *hud, *viewport, *defaults, *screen, *ship, *target, *sector, *scene,
        *marker, *nodes[5], *registry, *map, *buckets, *link, *textures;
} sc{};
constexpr std::int32_t cam_pos[3] = {1000, 2000, 3000};
constexpr std::int32_t point[3] = {1100, 2050, 4000};  // camera-relative (100, 50, 1000)
constexpr std::int32_t decoy[3] = {1000, 2000, 3500};  // AP's slot P+0xc0: would project to (0, 0)
constexpr std::int32_t behind[3] = {1100, 2050, 2000}; // behind the camera: projection fails
void put_identity(unsigned char* p, unsigned off) {
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = 0; c < 4; ++c) put32(p, off + 16 * r + 4 * c, c == 3 ? 0x77u : (r == c ? 0x10000u : 0u));
}
void build_scene() {
    auto* b = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    std::memset(b, 0, 0x10000);
    sc.cockpit = b;
    sc.overlay = b + S::overlay;
    sc.camera = b + 0x1000;
    sc.hud = b + 0x1400;
    sc.viewport = b + 0x1800;
    sc.defaults = b + 0x1840;
    sc.screen = b + 0x1880;
    sc.ship = b + 0x2000;
    sc.target = b + 0x2100;
    sc.sector = b + 0x2200;
    sc.scene = b + 0x2300;
    sc.marker = b + 0x3000;
    for (unsigned i = 0; i < 5; ++i) sc.nodes[i] = b + 0x3200 + 0x200 * i;
    sc.registry = b + 0x4000;
    sc.map = b + 0x4040;
    sc.buckets = b + 0x4080;
    sc.link = b + 0x40c0;
    sc.textures = b + 0x5000;
    unsigned char* k = sc.cockpit;
    put32(k, 4, addr(sc.scene));
    put32(k, 8, addr(sc.hud));
    put32(k, 0xc, addr(sc.ship));
    put32(k, 0x10, addr(sc.ship));
    put32(k, 0x54, addr(sc.sector));
    put32(k, 0x58, addr(sc.camera));
    put32(k, 0x150, 258);
    put32(k, 0x1e0, addr(sc.target));
    put16(k, 0x1e4, 2);
    put32(k, S::cockpit_cached_fov, 0x2000);
    put32(k, S::cockpit_draw2d, 1);
    put32(k, S::cockpit_display, 1);
    unsigned char* o = sc.overlay;
    put32(o, S::overlay_bodies, 1);
    put32(o, S::overlay_cockpit, addr(sc.cockpit));
    put32(o, S::lead_entry, 1);
    put32(o, S::lead_entry + 4, addr(sc.marker));
    for (unsigned i = 0; i < 5; ++i) {
        put32(o, S::hud_entries[i], 1);
        put32(o, S::hud_entries[i] + 4, addr(sc.nodes[i]));
    }
    for (unsigned char* n : {sc.marker, sc.nodes[0], sc.nodes[1], sc.nodes[2], sc.nodes[3], sc.nodes[4]}) {
        put32(n, 0x1c, addr(sc.scene));
        put32(n, 0x130, 0x200);
    }
    put32(sc.scene, 0x18, 1);
    for (unsigned char* c : {sc.camera, sc.hud}) {
        put32(c, 0x278, addr(sc.viewport));
        put32(c, 0x288, 0);
        put32(c, 0x28c, 0x10000);
        put32(c, 0x290, 0);
        put32(c, 0x294, 0x10000);
        put32(c, 0x300, 0x10000);
        put32(c, 0x304, 0x10000);
        put_identity(c, 0x40);
        put32(c, 0x298, 0x2000);
    }
    for (unsigned i = 0; i < 3; ++i) put32(sc.camera, 0x30 + 4 * i, uint32_t(cam_pos[i]));
    put32(sc.viewport, 0, 0);
    put32(sc.viewport, 4, 0);
    put32(sc.viewport, 8, 1920);
    put32(sc.viewport, 12, 1080);
    put32(sc.defaults, 0, addr(sc.screen));
    put32(sc.defaults, 0x28, 0x10000);
    put32(sc.defaults, 0x2c, 0x10000);
    put16(sc.screen, 4, 1920);
    put16(sc.screen, 6, 1080);
    put32(sc.ship, 8, 4242);
    put16(sc.ship, 0x48, 7);
    put32(sc.ship, 0x54, addr(sc.sector));
    put32(sc.target, 8, 5151);
    put32(sc.target, 0x54, addr(sc.sector));
    put16(sc.sector, 0x48, 1);
    // Registry: map {buckets, 4}, active handle 5 -> bucket 1 -> {next 0, key 5, value cockpit}.
    put32(sc.registry, 0, addr(sc.map));
    put32(sc.registry, 0x10, 5);
    put32(sc.map, 0, addr(sc.buckets));
    put32(sc.map, 4, 4);
    put32(sc.buckets, 4, addr(sc.link));
    put32(sc.link, 4, 5);
    put32(sc.link, 8, addr(sc.cockpit));
    put32(sc.textures, 15 * 0x10 + 8, 0x1234);
    // Image globals.
    put32(image, S::registry_slot_rva, addr(sc.registry));
    put32(image, S::defaults_slot_rva, addr(sc.defaults));
    put16(image, S::texture_meta_count_rva, 10);
    put16(image, S::texture_count_a_rva, 10);
    put16(image, S::texture_count_b_rva, 10);
    put32(image, S::texture_rows_rva, addr(sc.textures));
}

void set_anchor(const wchar_t* value) {
    SetEnvironmentVariableW(L"X3M_CHASE_HUD_ANCHOR", value);
}
bool row_has(const char* text) {
    return std::strstr(last_row, text) != nullptr;
}
// A fresh initialize() that must refuse with `status` before any write.
void refused(const char* label, std::uintptr_t at, const char* status) {
    cl::fixture_reset();
    cl::fixture_base(at);
    const unsigned before = rows;
    const DWORD sentinel = 0x2bad0001;
    SetLastError(sentinel);
    const bool ok = cl::initialize();
    char want[96];
    std::snprintf(want, sizeof want, "chase_lead installed=0 status=%s ", status);
    check(!ok && !cl::installed() && !cl::hud_installed() && GetLastError() == sentinel && rows == before + 3 &&
              row_has(want) && row_has(" edition=fl ") && seams_original(),
          label, last_row);
}

// One seam visit through the harness.
struct Visit {
    uint32_t branch;
    bool regs, flags, xmm, x87, mxcsr, last_error, esp;
};
const DWORD visit_error = 0x1234abcd;
constexpr uint32_t all_flags = 0x8d5 | 0x400; // CF PF AF ZF SF OF and DF
struct Entry {
    uint32_t eax = 0xa0a0a0a0, ebx = 0, ecx = 0xc0c0c0c0, edx = 0xd0d0d0d0, esi = 0x51515151, edi = 0xd1d1d1d1,
             ebp = 0xb0b0b0b0, flags = all_flags;
};
// Expected deltas: which of ECX/EDX change and to what, and the flags the continuation must see.
struct Expect {
    uint32_t branch = 1, flags = all_flags;
    bool ecx_changes = false, edx_changes = false;
    uint32_t ecx = 0, edx = 0;
    bool cmp_flags = false; // the final-FOV tail's compare decides the flags
};
uint32_t local_base(uint32_t skew) { // the overlay's P for this skew (16-aligned minus skew)
    return addr(stack + 0x30000) - skew;
}
Visit visit(const S::Seam& seam, const Entry& in, uint32_t skew, const Expect& want) {
    in_eax = in.eax;
    in_ebx = in.ebx;
    in_ecx = in.ecx;
    in_edx = in.edx;
    in_esi = in.esi;
    in_edi = in.edi;
    in_ebp = in.ebp;
    in_flags = in.flags;
    in_esp = local_base(skew);
    in_fpucw = 0x007f;
    in_st0 = 1234.5678125;
    in_mxcsr = 0xbfe0;
    for (unsigned i = 0; i < sizeof in_xmm; ++i) in_xmm[i] = static_cast<unsigned char>(0x11 * (i / 16 + 1) + i);
    std::memset(out_xmm, 0, sizeof out_xmm);
    out_branch = 0;
    site_addr = uint32_t(base + seam.rva);
    SetLastError(visit_error);
    fl_cl_enter();
    const DWORD error = GetLastError();
    Visit v{};
    v.branch = out_branch;
    v.regs = out_eax == in_eax && out_ebx == in_ebx && out_esi == in_esi && out_edi == in_edi && out_ebp == in_ebp &&
             out_ecx == (want.ecx_changes ? want.ecx : in_ecx) && out_edx == (want.edx_changes ? want.edx : in_edx);
    v.esp = out_esp == esp_at_jump && esp_at_jump == in_esp;
    v.flags = (out_flags & 0xcd5) == ((want.cmp_flags ? ref_cmp_flags : want.flags) & 0xcd5);
    v.xmm = !std::memcmp(out_xmm, in_xmm, sizeof in_xmm);
    v.x87 = out_fpucw == in_fpucw && out_st0 == in_st0 && ((out_fpusw >> 11) & 7) == 7;
    v.mxcsr = out_mxcsr == in_mxcsr;
    v.last_error = error == visit_error;
    return v;
}
void report_visit(const char* label, const Visit& v, uint32_t branch) {
    char detail[256];
    std::snprintf(detail, sizeof detail,
                  "branch=%u want=%u regs=%u flags=%u(%08x) xmm=%u x87=%u mxcsr=%u error=%u esp=%u ecx=%08x edx=%08x",
                  v.branch, branch, v.regs, v.flags, out_flags, v.xmm, v.x87, v.mxcsr, v.last_error, v.esp, out_ecx,
                  out_edx);
    check(v.branch == branch && v.regs && v.flags && v.xmm && v.x87 && v.mxcsr && v.last_error && v.esp, label, detail);
}
void put_point(uint32_t skew, const std::int32_t* p, const std::int32_t* d) {
    auto* local = reinterpret_cast<unsigned char*>(std::uintptr_t(local_base(skew)));
    for (unsigned i = 0; i < 3; ++i) {
        put32(local, S::solver_point_slot + 4 * i, uint32_t(p[i]));
        put32(local, 0xc0 + 4 * i, uint32_t(d[i]));
    }
}
// Closed-form pixel of a camera-relative vector (identity basis, plane 1.0, 1920x1080, binary-angle FOV).
void pixel(double x, double y, double z, std::int32_t fov, std::int32_t& px, std::int32_t& py) {
    const double t = std::tan(3.14159265358979323846 * fov / 65536.0);
    px = std::int32_t(960.0 * x / (z * t));
    py = std::int32_t(-540.0 * y / (z * t));
}
x3m::chase::Mat3 view_rel_rotation(double a) {
    x3m::chase::Mat3 r;
    r.m[0][0] = std::cos(a);
    r.m[0][2] = -std::sin(a);
    r.m[2][0] = std::sin(a);
    r.m[2][2] = std::cos(a);
    return r;
}
const x3m::chase::Mat3 identity{};
const x3m::chase::Mat3 view_rel = view_rel_rotation(0.1);
void pose() { // what the chase camera's pose seam does on an applied bound visit
    cl::invalidate_pose();
    cl::camera_context(addr(sc.cockpit), addr(sc.ship), addr(sc.camera), true, &identity, &view_rel);
}
Entry gate_entry(uint32_t frame) {
    Entry e;
    e.esi = addr(sc.cockpit);
    e.ebx = addr(sc.overlay);
    e.ebp = frame;
    return e;
}
Entry hud_entry(uint32_t frame) {
    Entry e;
    e.eax = addr(sc.cockpit);
    e.ebx = addr(sc.overlay);
    e.ebp = frame;
    return e;
}
Entry publish_entry(uint32_t frame) {
    Entry e;
    e.eax = addr(sc.marker);
    e.ebx = addr(sc.overlay);
    e.ecx = 0x11110001; // native x
    e.edx = 0x22220002; // native y
    e.ebp = frame;
    return e;
}
Entry fov_entry() {
    Entry e;
    e.ebx = addr(sc.cockpit);
    e.esi = 0x2000; // the native final FOV: equal to the cached value, so the native je is taken
    return e;
}
constexpr std::int32_t native_hud[5][2] = {{0, 9}, {-70, -10}, {70, -10}, {0, 40}, {1, -25}};
void reset_nodes() {
    for (unsigned i = 0; i < 5; ++i) {
        put32(sc.nodes[i], 0x30, uint32_t(native_hud[i][0]));
        put32(sc.nodes[i], 0x34, uint32_t(native_hud[i][1]));
        put32(sc.overlay, S::hud_entries[i], 1);
    }
    put32(sc.marker, 0x30, 0x0bad);
    put32(sc.marker, 0x34, 0x0bad);
    put32(sc.overlay, S::lead_entry, 1);
    put32(sc.overlay, S::overlay_cached, 0x0bad);
    put32(sc.overlay, S::overlay_cached + 4, 0x0bad);
}

int main() {
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    stack = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x40000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    base = reinterpret_cast<std::uintptr_t>(image);
    check(image && stack && !(base & 0xffff), "image_64k_aligned");
    if (!image || !stack) return 1;
    std::memset(image, 0xcc, image_size);
    lay_out();
    build_scene();
    for (unsigned i = 0; i < 4; ++i) std::memcpy(original_seam[i], image + seams[i]->rva, 6);
    unsigned total = 0;
    for (const auto& w : S::windows) total += w.length;
    std::printf("WINDOWS count=%u bytes=%u gate_mod8=%u publish_mod8=%u fov_mod8=%u hud_mod8=%u\n", S::window_count,
                total, unsigned((base + S::gate.rva) & 7), unsigned((base + S::publish.rva) & 7),
                unsigned((base + S::final_fov.rva) & 7), unsigned((base + S::hud_gate.rva) & 7));

    // Refusals before any write.
    set_anchor(nullptr);
    refused("executable_mismatch", 0, "executable_mismatch");
    camera_installed = false;
    refused("chase_camera_unavailable", base, "chase_camera_unavailable");
    camera_installed = true;
    refused("invalid_base", base + 0x1000, "invalid_base");
    // Every single-bit change of every window, relocated dwords included, is refused before any write.
    unsigned flips = 0, refusals = 0, slot = 0;
    quiet = true;
    for (const auto& w : S::windows)
        for (unsigned bit = 0; bit < w.length * 8; ++bit) {
            image[w.rva + bit / 8] ^= static_cast<unsigned char>(1u << (bit % 8));
            bool reloc = false;
            for (unsigned r = 0; r < w.reloc_count; ++r)
                reloc |= bit / 8 >= w.reloc_offset[r] && bit / 8 < w.reloc_offset[r] + 4;
            cl::fixture_reset();
            cl::fixture_base(base);
            const bool ok = cl::initialize();
            const bool status = row_has(reloc ? "status=slot_mismatch " : "status=window_mismatch ");
            char window[64];
            std::snprintf(window, sizeof window, " window=%s ", w.name);
            image[w.rva + bit / 8] ^= static_cast<unsigned char>(1u << (bit % 8));
            ++flips;
            slot += reloc;
            if (!ok && !cl::installed() && status && row_has(window) && seams_original()) ++refusals;
        }
    quiet = false;
    std::printf("FLIPS bits=%u refused=%u reloc_bits=%u\n", flips, refusals, slot);
    check(flips == refusals && flips == total * 8, "every_flip_refused");

    // Partial install, both-or-neither: a foreign write into seam k between its claim and read-back. The marker's
    // three seams go back together; a HUD seam fault leaves the marker installed.
    for (unsigned k = 0; k < 3; ++k) {
        cl::fixture_reset();
        cl::fixture_base(base);
        cl::fixture_fault_readback(k, 1);
        const unsigned before = rows;
        const bool ok = cl::initialize();
        char label[48];
        std::snprintf(label, sizeof label, "lead_rollback_site%u", k);
        check(!ok && !cl::installed() && !cl::hud_installed() && rows == before + 3 && seams_original() &&
                  row_has("chase_lead installed=0 status=readback_mismatch ") && row_has(" rollback=restored "),
              label, last_row);
    }
    cl::fixture_reset();
    cl::fixture_base(base);
    cl::fixture_fault_readback(3, 2);
    {
        quiet = true;
        const bool ok = cl::initialize();
        quiet = false;
        check(ok && cl::installed() && !cl::hud_installed() && row_has("chase_lead installed=1 status=active ") &&
                  !std::memcmp(image + S::hud_gate.rva, original_seam[3], 6) && image[S::gate.rva] == 0xe9,
              "hud_rollback_keeps_lead", last_row);
        cl::shutdown();
        check(seams_original(), "shutdown_after_hud_rollback");
    }
    // A fault the forced restore cannot undo (inside the jump and beyond it): rollback=failed, the site stays
    // registered and inert (enabled false).
    cl::fixture_reset();
    cl::fixture_base(base);
    cl::fixture_fault_readback(1, 1, 5);
    {
        const bool ok = cl::initialize();
        check(!ok && !cl::installed() && row_has("status=readback_mismatch ") && row_has(" rollback=failed "),
              "lead_rollback_failed_reported", last_row);
        image[S::publish.rva + 5] ^= 0xff;
        check(seams_original(), "foreign_byte_undone");
    }

    // The install.
    set_anchor(L"forward");
    cl::fixture_reset();
    cl::fixture_base(base);
    const DWORD sentinel = 0x2bad0002;
    SetLastError(sentinel);
    unsigned before = rows;
    const bool installed = cl::initialize();
    check(installed && cl::installed() && cl::hud_installed() && GetLastError() == sentinel && rows == before + 3 &&
              row_has("chase_lead installed=1 status=active ") && row_has(" edition=fl ") &&
              row_has(" rva=0x02c7b9,0x02cbe7,0x022cf5 window=- rollback=none update=pose_seam"),
          "install_row", last_row);
    for (unsigned i = 0; i < 4; ++i) {
        char label[32];
        std::snprintf(label, sizeof label, "jump_written_%u", i);
        check(image[seams[i]->rva] == 0xe9 && image[seams[i]->rva + 5] == original_seam[i][5], label);
    }
    if (!installed) {
        std::printf("RESULT FAIL checks=%u failures=%u\n", checks, failures);
        return 1;
    }
    recorder(S::gate.rva + 6, &fl_cl_fall);
    recorder(S::gate_target_rva, &fl_cl_taken);
    recorder(S::publish.rva + 6, &fl_cl_fall);
    recorder(S::final_fov.rva + 6, &fl_cl_fall);
    recorder(S::hud_gate.rva + 6, &fl_cl_fall);
    recorder(S::hud_target_rva, &fl_cl_taken);
    recorder(S::detach_rva, &fl_cl_detach);

    // Expected pixels.
    std::int32_t lx = 0, ly = 0, fx = 0, fy = 0;
    pixel(100, 50, 1000, 0x2000, lx, ly);
    std::int32_t forward[3]{};
    {
        auto ship = x3m::chase::mul(x3m::chase::transpose(view_rel), identity);
        std::int32_t rows12[12]{};
        x3m::chase::orthonormalize(ship);
        x3m::chase::to_fixed(ship, rows12);
        forward[0] = rows12[8];
        forward[1] = rows12[9];
        forward[2] = rows12[10];
    }
    pixel(forward[0], forward[1], forward[2], 0x2000, fx, fy);
    std::printf("EXPECT lead=%d,%d forward=%d,%d,%d anchor=%d,%d\n", lx, ly, forward[0], forward[1], forward[2], fx, fy);

    // Four applied updates, one per ESP skew: pose, gate, publish, HUD gate, final FOV (unchanged projection).
    for (uint32_t skew = 0; skew < 16; skew += 4) {
        char label[64];
        const uint32_t frame = 0xb0b0b000 + skew;
        reset_nodes();
        pose();
        const auto c0 = cl::fixture_counts();
        Expect admit;
        admit.flags = all_flags & ~0x40u;
        Visit v = visit(S::gate, gate_entry(frame), skew, admit);
        std::snprintf(label, sizeof label, "gate_admitted_skew%u", skew);
        report_visit(label, v, 1);
        put_point(skew, point, decoy);
        Expect pub;
        pub.ecx_changes = pub.edx_changes = true;
        pub.ecx = uint32_t(lx);
        pub.edx = uint32_t(ly);
        v = visit(S::publish, publish_entry(frame), skew, pub);
        std::snprintf(label, sizeof label, "publish_skew%u", skew);
        report_visit(label, v, 1);
        std::snprintf(label, sizeof label, "publish_writes_skew%u", skew);
        check(gets(sc.marker, 0x30) == lx && gets(sc.marker, 0x34) == ly && gets(sc.overlay, S::overlay_cached) == lx,
              label);
        v = visit(S::hud_gate, hud_entry(frame), skew, admit);
        std::snprintf(label, sizeof label, "hud_gate_admitted_skew%u", skew);
        report_visit(label, v, 1);
        Expect fov;
        fov.cmp_flags = true;
        v = visit(S::final_fov, fov_entry(), skew, fov);
        std::snprintf(label, sizeof label, "final_fov_skew%u", skew);
        report_visit(label, v, 1);
        const auto c1 = cl::fixture_counts();
        bool anchored = true;
        for (unsigned i = 0; i < 5; ++i)
            anchored &= gets(sc.nodes[i], 0x30) == native_hud[i][0] + fx && gets(sc.nodes[i], 0x34) == native_hud[i][1] + fy;
        std::snprintf(label, sizeof label, "counts_skew%u", skew);
        check(c1.lead[0] == c0.lead[0] + 1 && c1.lead[1] == c0.lead[1] + 1 && c1.lead[11] == c0.lead[11] + 1 &&
                  c1.hud[0] == c0.hud[0] + 1 && c1.anchor_applied == c0.anchor_applied + 1 && !c1.pose_open,
              label);
        std::snprintf(label, sizeof label, "anchor_nodes_skew%u", skew);
        check(anchored && c1.anchor_x == fx && c1.anchor_y == fy, label);
    }
    std::printf("NODES crosshair=%d,%d distance=%d,%d marker=%d,%d cached=%d\n", gets(sc.nodes[0], 0x30),
                gets(sc.nodes[0], 0x34), gets(sc.nodes[3], 0x30), gets(sc.nodes[3], 0x34), gets(sc.marker, 0x30),
                gets(sc.marker, 0x34), gets(sc.overlay, S::overlay_cached));

    // The pose is closed after the final FOV: a later gate without a pose visit is refused (native branch taken).
    {
        const auto c0 = cl::fixture_counts();
        Visit v = visit(S::gate, gate_entry(0xb0b0c000), 0, Expect{2, all_flags});
        report_visit("gate_after_close_not_admitted", v, 2);
        v = visit(S::hud_gate, hud_entry(0xb0b0c000), 0, Expect{2, all_flags});
        report_visit("hud_after_close_not_admitted", v, 2);
        const auto c1 = cl::fixture_counts();
        check(c1.lead[3] == c0.lead[3] + 1 && c1.hud[3] == c0.hud[3] + 1, "no_pose_counted");
    }
    // Native bit set (ZF clear: first-person view): untouched, not counted.
    {
        pose();
        const auto c0 = cl::fixture_counts();
        Entry e = gate_entry(0xb0b0c100);
        e.flags = all_flags & ~0x40u;
        Visit v = visit(S::gate, e, 4, Expect{1, all_flags & ~0x40u});
        report_visit("gate_internal_view_untouched", v, 1);
        const auto c1 = cl::fixture_counts();
        check(c1.lead[0] == c0.lead[0] && c1.lead[3] == c0.lead[3], "internal_view_not_counted");
        Visit f = visit(S::final_fov, fov_entry(), 4, Expect{1, 0, false, false, 0, 0, true});
        report_visit("final_fov_closes", f, 1);
    }
    // Final FOV with a changed camera FOV (zoom between publish and the final FOV): marker and cached pair finalized.
    {
        reset_nodes();
        pose();
        Expect admit;
        admit.flags = all_flags & ~0x40u;
        Visit v = visit(S::gate, gate_entry(0xb0b0c200), 8, admit);
        report_visit("zoom_gate", v, 1);
        put_point(8, point, decoy);
        Expect pub;
        pub.ecx_changes = pub.edx_changes = true;
        pub.ecx = uint32_t(lx);
        pub.edx = uint32_t(ly);
        v = visit(S::publish, publish_entry(0xb0b0c200), 8, pub);
        report_visit("zoom_publish", v, 1);
        put32(sc.camera, 0x298, 0x1800);
        put32(sc.hud, 0x298, 0x1800);
        std::int32_t zx = 0, zy = 0;
        pixel(100, 50, 1000, 0x1800, zx, zy);
        const auto c0 = cl::fixture_counts();
        Expect fov;
        fov.cmp_flags = true;
        v = visit(S::final_fov, fov_entry(), 8, fov);
        report_visit("zoom_final_fov", v, 1);
        const auto c1 = cl::fixture_counts();
        char detail[96];
        std::snprintf(detail, sizeof detail, "marker=%d,%d want=%d,%d", gets(sc.marker, 0x30), gets(sc.marker, 0x34), zx,
                      zy);
        check(c1.lead[2] == c0.lead[2] + 1 && gets(sc.marker, 0x30) == zx && gets(sc.marker, 0x34) == zy &&
                  gets(sc.overlay, S::overlay_cached) == zx && gets(sc.overlay, S::overlay_cached + 4) == zy,
              "zoom_finalized", detail);
        put32(sc.camera, 0x298, 0x2000);
        put32(sc.hud, 0x298, 0x2000);
    }
    // Failed projection at publication (point behind the camera): the native helper hides the owned marker through the
    // detacher (ECX = marker, argument = its scene), ECX/EDX = -1, the cached pair -1.
    {
        reset_nodes();
        pose();
        Expect admit;
        admit.flags = all_flags & ~0x40u;
        Visit v = visit(S::gate, gate_entry(0xb0b0c300), 12, admit);
        report_visit("hide_gate", v, 1);
        put_point(12, behind, decoy);
        const uint32_t calls = detach_calls;
        Expect pub;
        pub.ecx_changes = pub.edx_changes = true;
        pub.ecx = pub.edx = 0xffffffffu;
        v = visit(S::publish, publish_entry(0xb0b0c300), 12, pub);
        report_visit("hide_publish", v, 1);
        check(detach_calls == calls + 1 && detach_ecx == addr(sc.marker) && detach_arg == addr(sc.scene) &&
                  get32(sc.overlay, S::lead_entry) == 0 && gets(sc.overlay, S::overlay_cached) == -1 &&
                  gets(sc.overlay, S::overlay_cached + 4) == -1,
              "hide_through_native_helper");
        Visit f = visit(S::final_fov, fov_entry(), 12, Expect{1, 0, false, false, 0, 0, true});
        report_visit("hide_final_fov", f, 1);
    }
    // Central HUD refusals: an extra entry active (native cleanup first), texture 15 not cached, the 2D draw disabled.
    {
        struct Case {
            const char* label;
            unsigned char* at;
            unsigned offset;
            uint32_t value;
            unsigned reason;
        } cases[] = {{"hud_extra_entry_active", sc.overlay, S::extra_entries[1], 1, 9},
                     {"hud_texture_missing", sc.textures, 15 * 0x10 + 8, 0, 8},
                     {"hud_draw2d_off", sc.cockpit, S::cockpit_draw2d, 0, 6},
                     {"hud_backpointer_foreign", sc.overlay, S::overlay_cockpit, 0x10, 7}};
        for (const auto& c : cases) {
            const uint32_t old = get32(c.at, c.offset);
            put32(c.at, c.offset, c.value);
            pose();
            const auto c0 = cl::fixture_counts();
            Visit v = visit(S::hud_gate, hud_entry(0xb0b0c400), 0, Expect{2, all_flags});
            report_visit(c.label, v, 2);
            const auto c1 = cl::fixture_counts();
            check(c1.hud[c.reason] == c0.hud[c.reason] + 1 && c1.hud[0] == c0.hud[0], c.label, "reason");
            put32(c.at, c.offset, old);
            visit(S::final_fov, fov_entry(), 0, Expect{1, 0, false, false, 0, 0, true});
        }
    }
    // A foreign cockpit (another cockpit's overlay visit after this pose) and a wrong ESI are not admitted.
    {
        pose();
        Entry e = gate_entry(0xb0b0c500);
        e.esi = addr(sc.cockpit) + 0x10;
        Visit v = visit(S::gate, e, 0, Expect{2, all_flags});
        report_visit("gate_foreign_cockpit", v, 2);
        visit(S::final_fov, fov_entry(), 0, Expect{1, 0, false, false, 0, 0, true});
    }
    cl::report(300);
    check(row_has("chase_lead_sample ") || row_has("chase_lead_window "), "report_rows", last_row);

    // Disabled flags: the stub's byte test forwards before its CPU envelope; the native branch and every register,
    // flag and FP state come through unchanged, and nothing is counted.
    {
        pose();
        const auto c0 = cl::fixture_counts();
        cl::fixture_enable(false, false);
        Visit v = visit(S::gate, gate_entry(0xb0b0c600), 4, Expect{2, all_flags});
        report_visit("disabled_gate_forwards", v, 2);
        v = visit(S::hud_gate, hud_entry(0xb0b0c600), 8, Expect{2, all_flags});
        report_visit("disabled_hud_forwards", v, 2);
        Visit f = visit(S::final_fov, fov_entry(), 12, Expect{1, 0, false, false, 0, 0, true});
        report_visit("disabled_final_fov_forwards", f, 1);
        const auto c1 = cl::fixture_counts();
        check(std::memcmp(c0.lead, c1.lead, sizeof c0.lead) == 0 && std::memcmp(c0.hud, c1.hud, sizeof c0.hud) == 0 &&
                  c1.pose_open,
              "disabled_not_counted");
        cl::fixture_enable(true, true);
        visit(S::final_fov, fov_entry(), 0, Expect{1, 0, false, false, 0, 0, true});
    }

    // Cost (diagnostic, this host, not game FPS): mean per call over n calls of the harness itself (site = the
    // recorder, no stub), a final-FOV visit of another cockpit with the module enabled (what every non-chase cockpit of
    // the registry walk pays), the same visit with the flags off (the byte test only), and one admitted update (pose,
    // gate, publish, HUD gate, final FOV with the anchor).
    {
        constexpr unsigned n = 2000;
        LARGE_INTEGER f{}, t0{}, t1{};
        QueryPerformanceFrequency(&f);
        const double us = 1e6 / double(f.QuadPart);
        unsigned char* other = sc.cockpit + 0x7000;
        Entry other_fov = fov_entry();
        other_fov.ebx = addr(other);
        auto mean = [&](auto&& body) {
            QueryPerformanceCounter(&t0);
            for (unsigned i = 0; i < n; ++i) body(i);
            QueryPerformanceCounter(&t1);
            return double(t1.QuadPart - t0.QuadPart) * us / n;
        };
        const S::Seam harness = {"harness", 0, {0}, 0, S::w_fov};
        recorder(0x1000, &fl_cl_fall); // a bare continuation: the harness cost alone
        Expect plain{1, 0, false, false, 0, 0, true};
        const double base_us = mean([&](unsigned) {
            S::Seam h = harness;
            h.rva = 0x1000;
            visit(h, other_fov, 0, plain);
        });
        const double other_us = mean([&](unsigned) { visit(S::final_fov, other_fov, 0, plain); });
        cl::fixture_enable(false, false);
        const double off_us = mean([&](unsigned) { visit(S::final_fov, other_fov, 0, plain); });
        cl::fixture_enable(true, true);
        Expect admit;
        admit.flags = all_flags & ~0x40u;
        Expect pub;
        pub.ecx_changes = pub.edx_changes = true;
        pub.ecx = uint32_t(lx);
        pub.edx = uint32_t(ly);
        reset_nodes();
        put_point(0, point, decoy);
        const auto c0 = cl::fixture_counts();
        const double update_us = mean([&](unsigned i) {
            pose();
            visit(S::gate, gate_entry(0xb0b0d000 + 16 * i), 0, admit);
            visit(S::publish, publish_entry(0xb0b0d000 + 16 * i), 0, pub);
            visit(S::hud_gate, hud_entry(0xb0b0d000 + 16 * i), 0, admit);
            visit(S::final_fov, fov_entry(), 0, plain);
        });
        const auto c1 = cl::fixture_counts();
        check(c1.lead[1] == c0.lead[1] + n && c1.hud[0] == c0.hud[0] + n && c1.anchor_applied == c0.anchor_applied + n,
              "timed_updates_admitted");
        std::printf("TIMING n=%u harness_us=%.3f other_cockpit_final_fov_us=%.3f disabled_final_fov_us=%.3f "
                    "admitted_update_us=%.3f (harness included: 1 call, 1 call, 1 call, 4 calls + pose)\n",
                    n, base_us, other_us, off_us, update_us);
    }

    // Explicit teardown: shutdown() puts the four spans back (the recorders sit outside them); a second shutdown is a
    // no-op; with the windows laid out again a fresh install claims and restores again.
    cl::shutdown();
    check(!cl::installed() && !cl::hud_installed() && seams_original(), "shutdown_restores_four_spans");
    cl::shutdown();
    check(seams_original(), "second_shutdown_noop");
    lay_out();
    cl::fixture_reset();
    cl::fixture_base(base);
    quiet = true;
    check(cl::initialize() && cl::installed() && cl::hud_installed(), "reinstall_after_shutdown", last_row);
    quiet = false;
    cl::shutdown();
    check(seams_original(), "second_restore");

    // Late window: after the first Present nothing is claimed.
    x3m::engine_patch::close_install_window("fixture_present");
    refused("late_claim", base, "late_claim");
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
