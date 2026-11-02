// Original synthetic fixture of the FL chase cursor fire (src/engine/fl/chase_fire_fl.cpp built with X3M_EDITION_FL=1,
// src/engine/fl/chase_fire_sites_fl.h; docs/reverse-engineering/fl/chase-fire-aim.md, "Verified 2026-11-03"): no game
// code beyond the five identity windows of the sites header, no launch. Built with -DX3M_EDITION_FL=1
// -DX3M_CHASE_FIRE_FL_FIXTURE by verification/analysis/test_fl_chase_fire.py and run natively (WSL interop).
//
// A 64 KB-aligned private image stands in for X3FL.exe at `base`: the five windows at base + RVA with their relocated
// dwords = base + target, the image globals (cursor cells x/active/y, steering, registry and defaults slots) pointing at a
// synthetic cockpit, camera, ship, registry and screen record. After the install, `push imm32; ret` recorders replace
// the seam's continuation (base + 0x04ecb3) and its branch target (base + 0x04ef9f). A harness enters at the native
// `cmp [cursor active],0` (base + 0x04eca6, inside the admission window) with the fire function's register ABI
// (EBX = the stack-argument frame: +0xc group, +0x10 flags; EBP = the aligned frame: -0x3c ship), sentinels elsewhere,
// all arithmetic flags and DF set, eight XMM sentinels, a loaded x87 stack with a non-default control word, a
// non-default MXCSR, a LastError sentinel and every 4-byte ESP skew; the recorders capture what the native continuation
// would see.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../../src/proxy/chase_fire.h"
#include "../../src/proxy/chase_fire_core.h"
#include "../../src/proxy/chase_lead_core.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/engine/fl/chase_fire_sites_fl.h"
namespace cf = x3m::chase_fire;
namespace S = x3m::fl::chase_fire_fl;
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
bool camera_installed = true, telemetry_on = false;
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
    return telemetry_on;
}
}
namespace chase_camera {
bool installed() {
    return camera_installed;
}
}
}

extern "C" {
uint32_t in_eax, in_ebx, in_ecx, in_edx, in_esi, in_edi, in_ebp, in_flags, esp_skew, site_addr, active_addr, saved_esp,
    esp_at_jump, ref_flags;
uint32_t in_mxcsr, host_mxcsr, out_mxcsr;
std::uint16_t in_fpucw, out_fpucw, out_fpusw;
double in_st0, out_st0;
alignas(16) unsigned char in_xmm[128], out_xmm[128];
uint32_t out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, out_esp, out_flags, out_branch;
void fl_cf_enter();
void fl_cf_fall();
void fl_cf_taken();
}
#define XMM_IN(i) "movups _in_xmm+" #i "*16,%xmm" #i "\n\t"
#define XMM_OUT(i) "movups %xmm" #i ",_out_xmm+" #i "*16\n\t"
asm(".text\n\t"
    ".globl _fl_cf_enter\n_fl_cf_enter:\n\t"
    "pushal\n\tpushfl\n\tmovl %esp,_saved_esp\n\t"
    "stmxcsr _host_mxcsr\n\tfninit\n\tfldcw _in_fpucw\n\tfldl _in_st0\n\tldmxcsr _in_mxcsr\n\t"
    XMM_IN(0) XMM_IN(1) XMM_IN(2) XMM_IN(3) XMM_IN(4) XMM_IN(5) XMM_IN(6) XMM_IN(7)
    "subl _esp_skew,%esp\n\t"
    "movl _in_ebx,%ebx\n\tmovl _in_ecx,%ecx\n\tmovl _in_edx,%edx\n\tmovl _in_esi,%esi\n\tmovl _in_edi,%edi\n\t"
    "movl _in_ebp,%ebp\n\t"
    "pushl _in_flags\n\tpopfl\n\t"
    // The reference: the native compare on the same cell and incoming flags (what the unpatched JE would consume).
    "pushfl\n\tmovl _active_addr,%eax\n\tcmpl $0,(%eax)\n\tpushfl\n\tpopl _ref_flags\n\tpopfl\n\t"
    "movl _in_eax,%eax\n\tmovl %esp,_esp_at_jump\n\t"
    "jmp *_site_addr\n\t"
    ".globl _fl_cf_fall\n_fl_cf_fall:\n\t"
    "pushfl\n\tpopl _out_flags\n\tmovl $1,_out_branch\n\tjmp .Lfl_cf_record\n\t"
    ".globl _fl_cf_taken\n_fl_cf_taken:\n\t"
    "pushfl\n\tpopl _out_flags\n\tmovl $2,_out_branch\n"
    ".Lfl_cf_record:\n\t"
    "movl %eax,_out_eax\n\tmovl %ebx,_out_ebx\n\tmovl %ecx,_out_ecx\n\tmovl %edx,_out_edx\n\tmovl %esi,_out_esi\n\t"
    "movl %edi,_out_edi\n\tmovl %ebp,_out_ebp\n\tmovl %esp,_out_esp\n\t"
    XMM_OUT(0) XMM_OUT(1) XMM_OUT(2) XMM_OUT(3) XMM_OUT(4) XMM_OUT(5) XMM_OUT(6) XMM_OUT(7)
    "stmxcsr _out_mxcsr\n\tfnstcw _out_fpucw\n\tfnstsw _out_fpusw\n\tfstpl _out_st0\n\t"
    "fninit\n\tldmxcsr _host_mxcsr\n\tcld\n\t"
    "movl _saved_esp,%esp\n\tpopfl\n\tpopal\n\tret\n\t");

constexpr std::size_t image_size = 0x1b0000;
unsigned char* image = nullptr;
std::uintptr_t base = 0;
unsigned char original_seam[6];
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
bool seam_original() {
    return !std::memcmp(image + S::admission.rva, original_seam, 6);
}
void lay_out() {
    for (const auto& w : S::windows) {
        std::memcpy(image + w.rva, w.bytes, w.length);
        for (unsigned r = 0; r < w.reloc_count; ++r) put32(image, w.rva + w.reloc_offset[r], uint32_t(base + w.reloc_target[r]));
    }
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
    unsigned char *cockpit, *camera, *ship, *other, *defaults, *screen, *registry, *map, *buckets, *link, *args, *frame;
} sc{};
constexpr unsigned frame_offset = 0x7000; // EBP; the ship slot is EBP - 0x3c
void put_cursor(std::int32_t x, std::int32_t y, std::int32_t active = 0) {
    put32(image, S::cursor_x_rva, uint32_t(x));
    put32(image, S::cursor_active_rva, uint32_t(active));
    put32(image, S::cursor_y_rva, uint32_t(y));
}
void build_scene() {
    auto* b = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    std::memset(b, 0, 0x10000);
    sc.cockpit = b;
    sc.camera = b + 0x1000;
    sc.defaults = b + 0x1840;
    sc.screen = b + 0x1880;
    sc.ship = b + 0x2000;
    sc.other = b + 0x2100;
    sc.registry = b + 0x4000;
    sc.map = b + 0x4040;
    sc.buckets = b + 0x4080;
    sc.link = b + 0x40c0;
    sc.args = b + 0x6000;
    sc.frame = b + frame_offset;
    unsigned char* k = sc.cockpit;
    put32(k, 0xc, addr(sc.ship));
    put32(k, 0x10, addr(sc.ship));
    put32(k, 0x58, addr(sc.camera));
    put32(k, 0x150, 258);
    put32(sc.camera, 0x288, 0);
    put32(sc.camera, 0x28c, 0x10000);
    put32(sc.camera, 0x290, 0);
    put32(sc.camera, 0x294, 0x10000);
    put32(sc.camera, 0x298, 0x2000);
    put32(sc.camera, 0x300, 0x10000);
    put32(sc.camera, 0x304, 0x10000);
    put32(sc.ship, 8, 4242);
    put32(sc.other, 8, 4343);
    put32(sc.defaults, 0, addr(sc.screen));
    put32(sc.defaults, 0x28, 0x10000);
    put32(sc.defaults, 0x2c, 0x10000);
    put16(sc.screen, 4, 1920);
    put16(sc.screen, 6, 1080);
    // Registry: map {buckets, 4}, active handle 5 -> bucket 1 -> {next 0, key 5, value cockpit}.
    put32(sc.registry, 0, addr(sc.map));
    put32(sc.registry, 0x10, 5);
    put32(sc.map, 0, addr(sc.buckets));
    put32(sc.map, 4, 4);
    put32(sc.buckets, 4, addr(sc.link));
    put32(sc.link, 4, 5);
    put32(sc.link, 8, addr(sc.cockpit));
    // The fire call: target (the AP ship slot: the player ship here, a decoy), group 0, flags 0x2a, mask -1; ship at
    // [EBP-0x3c].
    put32(sc.args, S::arg_target, addr(sc.ship));
    put32(sc.args, S::arg_group, 0);
    put32(sc.args, S::arg_flags, 0x2a);
    put32(sc.args, S::arg_mask, 0xffffffffu);
    put32(sc.frame - S::ship_below_ebp, 0, addr(sc.ship));
    // Image globals.
    put32(image, S::registry_slot_rva, addr(sc.registry));
    put32(image, S::defaults_slot_rva, addr(sc.defaults));
    put32(image, S::steering_rva, 0x19283746u);
    put_cursor(1440, 270);
}

bool row_has(const char* text) {
    return std::strstr(last_row, text) != nullptr;
}
// A fresh initialize() that must refuse with `status` before any write.
void refused(const char* label, std::uintptr_t at, const char* status) {
    cf::fixture_reset();
    cf::fixture_base(at);
    const unsigned before = rows;
    const DWORD sentinel = 0x2bad0001;
    SetLastError(sentinel);
    const bool ok = cf::initialize();
    char want[96];
    std::snprintf(want, sizeof want, "chase_fire installed=0 status=%s ", status);
    check(!ok && !cf::installed() && GetLastError() == sentinel && rows == before + 1 && row_has(want) &&
              row_has(" edition=fl ") && seam_original(),
          label, last_row);
}

// One seam visit through the harness.
struct Visit {
    uint32_t branch;
    bool regs, flags, xmm, x87, mxcsr, last_error, esp;
};
const DWORD visit_error = 0x1234abcd;
constexpr uint32_t all_flags = 0x8d5 | 0x400; // CF PF AF ZF SF OF and DF
Visit visit(uint32_t skew, bool zf_cleared) {
    in_eax = 0xa0a0a0a0;
    in_ebx = addr(sc.args);
    in_ecx = 0x2a; // the native flags word (mov ecx,[ebx+0x10] at 0x04ec82)
    in_edx = 0xd0d0d0d0;
    in_esi = 0x51515151;
    in_edi = 0xd1d1d1d1;
    in_ebp = addr(sc.frame);
    in_flags = all_flags;
    esp_skew = skew;
    in_fpucw = 0x007f;
    in_st0 = 1234.5678125;
    in_mxcsr = 0xbfe0;
    for (unsigned i = 0; i < sizeof in_xmm; ++i) in_xmm[i] = static_cast<unsigned char>(0x11 * (i / 16 + 1) + i);
    std::memset(out_xmm, 0, sizeof out_xmm);
    out_branch = 0;
    active_addr = uint32_t(base + S::cursor_active_rva);
    site_addr = uint32_t(base + S::admission.rva - 7); // the native cmp [cursor active],0
    SetLastError(visit_error);
    fl_cf_enter();
    const DWORD error = GetLastError();
    Visit v{};
    v.branch = out_branch;
    v.regs = out_eax == in_eax && out_ebx == in_ebx && out_ecx == in_ecx && out_edx == in_edx && out_esi == in_esi &&
             out_edi == in_edi && out_ebp == in_ebp;
    v.esp = out_esp == esp_at_jump;
    const uint32_t want = zf_cleared ? ref_flags & ~0x40u : ref_flags;
    v.flags = (out_flags & 0xcd5) == (want & 0xcd5);
    v.xmm = !std::memcmp(out_xmm, in_xmm, sizeof in_xmm);
    v.x87 = out_fpucw == in_fpucw && out_st0 == in_st0 && ((out_fpusw >> 11) & 7) == 7;
    v.mxcsr = out_mxcsr == in_mxcsr;
    v.last_error = error == visit_error;
    return v;
}
void report_visit(const char* label, const Visit& v, uint32_t branch) {
    char detail[256];
    std::snprintf(detail, sizeof detail, "branch=%u want=%u regs=%u flags=%u(%08x ref %08x) xmm=%u x87=%u mxcsr=%u error=%u esp=%u",
                  v.branch, branch, v.regs, v.flags, out_flags, ref_flags, v.xmm, v.x87, v.mxcsr, v.last_error, v.esp);
    check(v.branch == branch && v.regs && v.flags && v.xmm && v.x87 && v.mxcsr && v.last_error && v.esp, label, detail);
}
LARGE_INTEGER qpc() {
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return t;
}
void publish() { // what the chase camera's pose seam does on an applied bound written pose
    cf::camera_context(addr(sc.cockpit), addr(sc.ship), addr(sc.camera), 258, 0, 0, true,
                       std::uint64_t(qpc().QuadPart));
}
// The native branch with the current inputs: admitted (fallthrough, ZF cleared) or refused (the original JE taken).
void admitted(const char* label, uint32_t skew = 0) {
    report_visit(label, visit(skew, true), 1);
}
void refusal(const char* label, uint32_t skew = 0) {
    report_visit(label, visit(skew, false), 2);
}
std::uint64_t overrides() {
    return cf::fixture_counts().result[0];
}
// The native unprojection's first stage, decoded at base + 0x0a51ef..0x0a52a5 (reference only): the cursor pixel as a
// 16.16 screen fraction (signed IDIV by the signed screen short) minus the viewport centre (x0+x1)/2 resp. (y0+y1)/2.
void native_offset(std::int32_t x, std::int32_t y, std::int32_t& ox, std::int32_t& oy) {
    const std::int32_t* vp = reinterpret_cast<const std::int32_t*>(sc.camera + 0x288);
    const std::int16_t w = std::int16_t(get32(sc.screen, 4) & 0xffff), h = std::int16_t(get32(sc.screen, 6) & 0xffff);
    ox = std::int32_t((std::int64_t(x) << 16) / w) - (vp[2] + vp[3]) / 2;
    oy = std::int32_t((std::int64_t(y) << 16) / h) - (vp[0] + vp[1]) / 2;
}
bool native_inside(std::int32_t x, std::int32_t y) {
    const std::int32_t* vp = reinterpret_cast<const std::int32_t*>(sc.camera + 0x288);
    std::int32_t ox = 0, oy = 0;
    native_offset(x, y, ox, oy);
    const std::int32_t cx = (vp[2] + vp[3]) / 2, cy = (vp[0] + vp[1]) / 2;
    return x >= 0 && y >= 0 && x < 1920 && y < 1080 && ox + cx >= vp[2] && ox + cx <= vp[3] && oy + cy >= vp[0] &&
           oy + cy <= vp[1];
}
void put_basis(const x3m::chase::Mat3& m) {
    std::int32_t rows12[12]{};
    x3m::chase::to_fixed(m, rows12);
    for (unsigned i = 0; i < 12; ++i) put32(sc.camera, 0x40 + 4 * i, uint32_t(rows12[i]));
}
int main() {
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    base = reinterpret_cast<std::uintptr_t>(image);
    check(image && !(base & 0xffff), "image_64k_aligned");
    if (!image) return 1;
    std::memset(image, 0xcc, image_size);
    lay_out();
    build_scene();
    std::memcpy(original_seam, image + S::admission.rva, 6);
    unsigned total = 0;
    for (const auto& w : S::windows) total += w.length;
    std::printf("WINDOWS count=%u bytes=%u seam_mod8=%u\n", S::window_count, total,
                unsigned((base + S::admission.rva) & 7));

    // Refusals before any write.
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
            cf::fixture_reset();
            cf::fixture_base(base);
            const bool ok = cf::initialize();
            const bool status = row_has(reloc ? "status=slot_mismatch " : "status=window_mismatch ");
            char window[64];
            std::snprintf(window, sizeof window, " window=%s ", w.name);
            image[w.rva + bit / 8] ^= static_cast<unsigned char>(1u << (bit % 8));
            ++flips;
            slot += reloc;
            if (!ok && !cf::installed() && status && row_has(window) && seam_original()) ++refusals;
        }
    quiet = false;
    std::printf("FLIPS bits=%u refused=%u reloc_bits=%u\n", flips, refusals, slot);
    check(flips == refusals && flips == total * 8, "every_flip_refused");

    // All or nothing: a foreign write into the seam between its claim and the read-back is undone (rollback=restored);
    // one the forced restore cannot undo (the sixth byte too) leaves the module disabled and reports rollback=failed.
    cf::fixture_reset();
    cf::fixture_base(base);
    cf::fixture_fault_readback(1);
    check(!cf::initialize() && !cf::installed() && seam_original() &&
              row_has("chase_fire installed=0 status=readback_mismatch ") && row_has(" rollback=restored"),
          "readback_rollback_restored", last_row);
    cf::fixture_reset();
    cf::fixture_base(base);
    cf::fixture_fault_readback(1, 5);
    check(!cf::initialize() && !cf::installed() && row_has("status=readback_mismatch ") && row_has(" rollback=failed"),
          "readback_rollback_failed_reported", last_row);
    image[S::admission.rva + 5] ^= 0xff;
    check(seam_original(), "foreign_byte_undone");

    // The install.
    cf::fixture_reset();
    cf::fixture_base(base);
    const DWORD sentinel = 0x2bad0002;
    SetLastError(sentinel);
    unsigned before = rows;
    const bool installed = cf::initialize();
    check(installed && cf::installed() && GetLastError() == sentinel && rows == before + 1 &&
              row_has("chase_fire installed=1 status=active scope=applied_chase_258_player_main_cursor ") &&
              row_has(" edition=fl ") && row_has(" rva=0x04ecad window=- rollback=none"),
          "install_row", last_row);
    check(image[S::admission.rva] == 0xe9 && image[S::admission.rva + 5] == original_seam[5], "jump_written");
    check(cf::initialize() && rows == before + 1, "second_initialize_idempotent");
    if (!installed) {
        std::printf("RESULT FAIL checks=%u failures=%u\n", checks, failures);
        return 1;
    }
    recorder(S::admission.rva + 6, &fl_cf_fall);
    recorder(S::admission_target_rva, &fl_cf_taken);

    // No witness: the native JE is taken, nothing counted (the player filter leaves before any read).
    refusal("no_witness_native_taken");
    check(overrides() == 0, "no_witness_not_counted");
    // Admission at every ESP skew: fallthrough with only ZF cleared; the cursor cells and steering are not written.
    publish();
    for (uint32_t skew = 0; skew < 16; skew += 4) {
        char label[48];
        std::snprintf(label, sizeof label, "admitted_skew%u", skew);
        admitted(label, skew);
    }
    check(overrides() == 4, "override_counted");
    check(get32(image, S::cursor_x_rva) == 1440 && get32(image, S::cursor_active_rva) == 0 &&
              get32(image, S::cursor_y_rva) == 270 && get32(image, S::steering_rva) == 0x19283746u &&
              get32(sc.args, S::arg_target) == addr(sc.ship) && get32(sc.args, S::arg_flags) == 0x2a,
          "no_native_input_written");
    // The native active cursor (ZF clear): the original fallthrough, untouched, not counted (the stub's ZF test).
    put_cursor(1440, 270, 1);
    for (uint32_t skew = 0; skew < 16; skew += 4) {
        char label[48];
        std::snprintf(label, sizeof label, "native_active_skew%u", skew);
        report_visit(label, visit(skew, false), 1);
    }
    put_cursor(1440, 270);
    check(overrides() == 4 && cf::fixture_counts().native_active == 4, "native_active_counted_by_stub_only");

    // The fire call's own filters (no lock, no QPC): the stack words and the [EBP-0x3c] ship, not AP's EBX+8.
    put32(sc.args, S::arg_flags, 0x02);
    refusal("boresight_flags_refused");
    put32(sc.args, S::arg_flags, 0x28);
    refusal("nonfire_flags_refused");
    put32(sc.args, S::arg_flags, 0x2a);
    put32(sc.args, S::arg_group, 1);
    refusal("turret_group_refused");
    put32(sc.args, S::arg_group, 0);
    put32(sc.frame - S::ship_below_ebp, 0, addr(sc.other));
    refusal("other_ship_refused_target_slot_decoy"); // [EBX+8] still holds the player ship
    put32(sc.frame - S::ship_below_ebp, 0, addr(sc.ship));
    // The witness and the live scope (shared policy, FL reads).
    auto refused_live = [&](const char* label, auto&& change, auto&& undo) {
        publish();
        change();
        refusal(label, 8);
        undo();
        publish();
        admitted("readmitted_after_undo", 12);
    };
    unsigned char* k = sc.cockpit;
    refused_live("internal_view_refused", [&] { put32(k, 0x150, 1); }, [&] { put32(k, 0x150, 258); });
    refused_live("other_external_view_refused", [&] { put32(k, 0x150, 257); }, [&] { put32(k, 0x150, 258); });
    refused_live("scripted_connect_refused", [&] { put32(k, 0x1c0, 1); }, [&] { put32(k, 0x1c0, 0); });
    refused_live("verbatim_camera_flag_refused", [&] { put32(k, 0x1a0, 4); }, [&] { put32(k, 0x1a0, 0); });
    refused_live("camera_mismatch_refused", [&] { put32(k, 0x58, addr(sc.camera) + 4); },
                 [&] { put32(k, 0x58, addr(sc.camera)); });
    refused_live("reference_mismatch_refused", [&] { put32(k, 0xc, addr(sc.other)); },
                 [&] { put32(k, 0xc, addr(sc.ship)); });
    refused_live("view_object_mismatch_refused", [&] { put32(k, 0x10, 0); }, [&] { put32(k, 0x10, addr(sc.ship)); });
    refused_live("registry_cockpit_mismatch_refused", [&] { put32(sc.link, 8, addr(k) + 0x100); },
                 [&] { put32(sc.link, 8, addr(k)); });
    refused_live("zero_active_handle_refused", [&] {
        put32(sc.registry, 0x10, 0);
        put32(sc.link, 4, 0);
    }, [&] {
        put32(sc.registry, 0x10, 5);
        put32(sc.link, 4, 5);
    });
    refused_live("same_address_new_ship_id_refused", [&] { put32(sc.ship, 8, 4243); }, [&] { put32(sc.ship, 8, 4242); });
    const auto freq = std::int64_t(cf::fixture_counts().frequency);
    refused_live("stale_witness_refused", [&] { cf::fixture_age_witness(-3 * freq); }, [] {});
    refused_live("future_witness_refused", [&] { cf::fixture_age_witness(freq); }, [] {});
    refused_live("other_thread_witness_refused", [] { cf::fixture_shift_thread(); }, [] {});
    refused_live("cursor_off_screen_refused", [] { put_cursor(1920, 270); }, [] { put_cursor(1440, 270); });
    refused_live("cursor_negative_refused", [] { put_cursor(1440, -1); }, [] { put_cursor(1440, 270); });
    refused_live("cursor_outside_viewport_refused", [&] { put32(sc.camera, 0x290, 50000); },
                 [&] { put32(sc.camera, 0x290, 0); });
    refused_live("invalid_screen_refused", [&] { put16(sc.screen, 6, 0); }, [&] { put16(sc.screen, 6, 1080); });
    refused_live("unreadable_screen_refused", [&] { put32(sc.defaults, 0, 0); },
                 [&] { put32(sc.defaults, 0, addr(sc.screen)); });
    {
        const auto c = cf::fixture_counts();
        std::printf("COUNTS override=%llu stale=%llu identity=%llu view=%llu cursor=%llu read=%llu\n",
                    (unsigned long long)c.result[0], (unsigned long long)c.result[1], (unsigned long long)c.result[2],
                    (unsigned long long)c.result[3], (unsigned long long)c.result[4], (unsigned long long)c.result[5]);
        check(c.result[1] == 3 && c.result[2] == 5 && c.result[3] == 4 && c.result[4] == 4 && c.result[5] == 2,
              "refusal_causes_counted");
    }
    // Witness lifetime: an unapplied or internal update clears it; an unrelated cockpit's invalidation keeps it, the
    // same cockpit's clears it.
    publish();
    cf::camera_context(addr(sc.cockpit), addr(sc.ship), addr(sc.camera), 1, 0, 0, false,
                       std::uint64_t(qpc().QuadPart));
    refusal("internal_update_clears_witness");
    publish();
    cf::camera_context(addr(sc.cockpit), addr(sc.ship), addr(sc.camera), 258, 0, 0, false,
                       std::uint64_t(qpc().QuadPart));
    refusal("refused_write_clears_witness");
    publish();
    cf::camera_context(addr(sc.cockpit), addr(sc.ship), addr(sc.camera), 258, 0, 0, true, 0);
    refusal("zero_stamp_fails_closed");
    publish();
    cf::invalidate_camera(addr(sc.cockpit) + 0x100);
    admitted("unrelated_cockpit_keeps_witness");
    cf::invalidate_camera(addr(sc.cockpit));
    refusal("same_cockpit_invalidation_clears");
    check(cf::fixture_counts().player == 0, "player_cleared");
    {
        const auto c = cf::fixture_counts();
        std::printf("EARLY native_active=%u no_witness=%u group=%u flags=%u other_ship=%u read=%u\n", c.native_active,
                    c.early[0], c.early[1], c.early[2], c.early[3], c.early[4]);
        check(c.native_active == 4 && c.early[0] == 5 && c.early[1] == 1 && c.early[2] == 2 && c.early[3] == 1 &&
                  c.early[4] == 0,
              "early_filters_counted_by_reason");
    }

    // The aim: (1) the admitted cursor set equals the native unprojection's in-viewport set, swept through the thunk
    // across both axes with an off-centre viewport (x and y differ in screen size and bounds, so a swapped x/active/y
    // permutation fails); (2) a target off-centre in a rotated chase camera, projected through the production HUD
    // projection (chase_lead_core::project), puts the cursor where the native first stage plus a pinhole inverse
    // (model) gives a ray from the camera through the target.
    put32(sc.camera, 0x288, 6553);   // y0 0.1
    put32(sc.camera, 0x28c, 58982);  // y1 0.9
    put32(sc.camera, 0x290, 13107);  // x0 0.2
    put32(sc.camera, 0x294, 52428);  // x1 0.8
    unsigned swept = 0, agreed = 0, inside = 0;
    quiet = true;
    for (std::int32_t x = -16; x <= 1936; x += 8)
        for (std::int32_t y : {-4, 0, 107, 108, 540, 971, 972, 1079, 1080}) {
            put_cursor(x, y);
            publish();
            const bool want = native_inside(x, y);
            const auto o0 = overrides();
            const Visit v = visit(4 * ((unsigned(x) >> 3) & 3), want);
            const bool got = overrides() == o0 + 1;
            ++swept;
            inside += want;
            agreed += got == want && v.branch == (want ? 1u : 2u) && v.regs && v.flags && v.xmm && v.x87 && v.mxcsr &&
                      v.last_error && v.esp;
        }
    for (std::int32_t y = -16; y <= 1096; y += 4)
        for (std::int32_t x : {383, 384, 960, 1535, 1536}) {
            put_cursor(x, y);
            publish();
            const bool want = native_inside(x, y);
            const auto o0 = overrides();
            const Visit v = visit(4 * ((unsigned(y) >> 2) & 3), want);
            const bool got = overrides() == o0 + 1;
            ++swept;
            inside += want;
            agreed += got == want && v.branch == (want ? 1u : 2u) && v.regs && v.flags && v.xmm && v.x87 && v.mxcsr &&
                      v.last_error && v.esp;
        }
    quiet = false;
    std::printf("SWEEP cursors=%u inside=%u agreed=%u\n", swept, inside, agreed);
    check(swept == agreed && inside > 0 && inside < swept, "admission_matches_native_viewport");
    {
        using namespace x3m::chase;
        const Mat3 basis = exp_rotation(Vec3{0.2, -0.35, 0.1});
        put_basis(basis);
        const std::int32_t cam[3] = {1000, -2000, 3000};
        for (unsigned i = 0; i < 3; ++i) put32(sc.camera, 0x30 + 4 * i, uint32_t(cam[i]));
        x3m::chase_lead::core::Projection p;
        std::memcpy(p.position, cam, sizeof cam);
        std::memcpy(p.basis, sc.camera + 0x40, sizeof p.basis);
        p.fov = 0x2000;
        p.plane[0] = p.plane[1] = 0x10000;
        std::memcpy(p.viewport, sc.camera + 0x288, sizeof p.viewport);
        p.screen[0] = 1920;
        p.screen[1] = 1080;
        // A target 4000 units ahead, offset right and up in camera space (well off the reticle at the centre).
        const Vec3 local{900, 500, 4000};
        const Vec3 world = Vec3{double(cam[0]), double(cam[1]), double(cam[2])} + mul(local, basis);
        const std::int32_t target[3] = {std::int32_t(std::lround(world.x)), std::int32_t(std::lround(world.y)),
                                        std::int32_t(std::lround(world.z))};
        x3m::chase_lead::core::Pixel px;
        const bool projected = x3m::chase_lead::core::project(p, target, px);
        const std::int32_t cx = std::int32_t(std::int64_t(1920) * (p.viewport[2] + p.viewport[3]) / 2 / 65536);
        const std::int32_t cy = std::int32_t(std::int64_t(1080) * (p.viewport[0] + p.viewport[1]) / 2 / 65536);
        const std::int32_t cursor_x = cx + px.x, cursor_y = cy + px.y;
        put_cursor(cursor_x, cursor_y);
        publish();
        const auto o0 = overrides();
        const Visit v = visit(8, true);
        report_visit("aim_cursor_admitted", v, 1);
        check(projected && overrides() == o0 + 1, "aim_target_projected_and_admitted");
        // Model: native offsets (16.16 of the screen) -> camera-space slope over the viewport half extent.
        std::int32_t ox = 0, oy = 0;
        native_offset(cursor_x, cursor_y, ox, oy);
        const double t = std::tan(pi * p.fov / 65536.0);
        const double hx = (p.viewport[3] - p.viewport[2]) / 2.0, hy = (p.viewport[1] - p.viewport[0]) / 2.0;
        const Vec3 ray_local{ox / hx * t, -oy / hy * t, 1.0};
        const Vec3 ray = mul(ray_local, basis), want = world - Vec3{double(cam[0]), double(cam[1]), double(cam[2])};
        const double angle = std::acos(std::fmin(1.0, dot(ray, want) / (length(ray) * length(want)))) * 180.0 / pi;
        const double pixel_deg = 2.0 * std::atan(t) * 180.0 / pi / (1920.0 * (hx * 2 / 65536.0));
        const double boresight = std::acos(dot(row(basis, 2), want) / length(want)) * 180.0 / pi;
        std::printf("AIM cursor=%d,%d offset_16_16=%d,%d ray_to_target_deg=%.4f pixel_deg=%.4f boresight_deg=%.2f\n",
                    cursor_x, cursor_y, ox, oy, angle, pixel_deg, boresight);
        // Tolerance: integer cursor pixels; the HUD projection and the viewport centre each truncate (up to ~2 px).
        check(angle < 3.0 * pixel_deg && boresight > 10.0, "aim_ray_through_target_off_centre");
    }
    put32(sc.camera, 0x288, 0);
    put32(sc.camera, 0x28c, 0x10000);
    put32(sc.camera, 0x290, 0);
    put32(sc.camera, 0x294, 0x10000);
    put_cursor(1440, 270);

    // The timed path (telemetry on): CPU contract unchanged, the window row drains the counters.
    cf::fixture_timing(true);
    publish();
    for (uint32_t skew = 0; skew < 16; skew += 4) {
        char label[48];
        std::snprintf(label, sizeof label, "timed_admitted_skew%u", skew);
        admitted(label, skew);
    }
    check(cf::fixture_counts().timed == 4, "timed_calls_counted");
    cf::report(300);
    check(row_has("chase_fire_window frame=300 native_inactive_override=") && row_has(" native_active=") &&
              row_has(" early_other_ship=") && row_has(" edition=fl") && cf::fixture_counts().native_active == 0 &&
              cf::fixture_counts().result[0] == 0,
          "report_row_drains", last_row);

    // Cost (diagnostic, this host, not game FPS): mean per call of the harness through the recorder alone (no stub),
    // the hooked seam with a natively active cursor (stub ZF test only), a non-player call (two argument reads), and an
    // admitted call (lock, QPC, registry walk, fields).
    {
        cf::fixture_timing(false);
        constexpr unsigned n = 5000;
        LARGE_INTEGER f{}, t0{}, t1{};
        QueryPerformanceFrequency(&f);
        const double us = 1e6 / double(f.QuadPart);
        auto mean = [&](auto&& body) {
            t0 = qpc();
            for (unsigned i = 0; i < n; ++i) body();
            t1 = qpc();
            return double(t1.QuadPart - t0.QuadPart) * us / n;
        };
        recorder(0x1000, &fl_cf_fall);
        const uint32_t seam_site = uint32_t(base + S::admission.rva - 7);
        double bare_us = 0;
        { // the harness pointed at a bare recorder: its own cost
            t0 = qpc();
            for (unsigned i = 0; i < n; ++i) {
                in_flags = all_flags;
                active_addr = uint32_t(base + S::cursor_active_rva);
                site_addr = uint32_t(base + 0x1000);
                SetLastError(visit_error);
                fl_cf_enter();
            }
            t1 = qpc();
            bare_us = double(t1.QuadPart - t0.QuadPart) * us / n;
            site_addr = seam_site;
        }
        put_cursor(1440, 270, 1);
        const double active_us = mean([&] { visit(0, false); });
        put_cursor(1440, 270);
        publish();
        put32(sc.frame - S::ship_below_ebp, 0, addr(sc.other));
        const double other_us = mean([&] { visit(0, false); });
        put32(sc.frame - S::ship_below_ebp, 0, addr(sc.ship));
        const auto o0 = overrides();
        const double admitted_us = mean([&] { visit(0, true); });
        check(overrides() == o0 + n, "timed_loop_admitted");
        std::printf("TIMING n=%u harness_us=%.3f native_active_us=%.3f non_player_us=%.3f admitted_us=%.3f "
                    "(harness included in each)\n",
                    n, bare_us, active_us, other_us, admitted_us);
    }

    // Disabled after shutdown: the original six bytes are back and the native JE decides again.
    cf::shutdown();
    check(!cf::installed() && seam_original(), "shutdown_restores_seam", last_row);
    recorder(S::admission.rva + 6, &fl_cf_fall); // the continuation recorder sits outside the six bytes
    publish();
    refusal("after_shutdown_native_taken");
    cf::shutdown();
    check(seam_original(), "second_shutdown_noop");
    lay_out();
    cf::fixture_reset();
    cf::fixture_base(base);
    quiet = true;
    check(cf::initialize() && cf::installed(), "reinstall_after_shutdown", last_row);
    quiet = false;
    cf::shutdown();
    check(seam_original(), "second_restore");

    // Late window: after the first Present nothing is claimed.
    x3m::engine_patch::close_install_window("fixture_present");
    refused("late_claim", base, "late_claim");
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
