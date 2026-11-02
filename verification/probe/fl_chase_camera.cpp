// Original synthetic fixture of the FL chase camera pose seam (src/proxy/chase_camera.cpp built with X3M_EDITION_FL=1,
// src/engine/fl/chase_camera_sites_fl.h; docs/reverse-engineering/fl/chase-camera.md, "Verified 2026-11-02"): no game
// code, no game bytes beyond the three identity windows of the sites header, no launch. Built with
// -DX3M_EDITION_FL=1 -DX3M_CHASE_CAMERA_FL_FIXTURE by verification/analysis/test_fl_chase_camera.py and run natively
// (WSL interop).
//
// A 64 KB-aligned private image stands in for X3FL.exe at `base`: the seam window at base+0x0227a4 (the ten-byte span at
// base+0x0227ad), the branch-target window at base+0x022928, the default-plane window at base+0x0dec8c with its fixup =
// base+0x1a82f8, and the slot itself. After the install, `push imm32; ret` recorders replace the first bytes at the
// continuation (base+0x0227b7) and the branch target, so the relocated `cmp [ebx+0x54],0; je` in the tail decides which
// recorder runs. A harness enters the site with EBX = a synthetic cockpit, sentinels in every other register, all
// arithmetic flags set, eight XMM sentinels, a loaded x87 stack with a non-default control word, a non-default MXCSR,
// a LastError sentinel and every 4-byte ESP skew mod 16; the recorders capture the CPU state the native code would see.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../../src/proxy/chase_camera.h"
#include "../../src/proxy/chase_transition.h"
#include "../../src/proxy/engine_memory.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/engine/fl/chase_camera_sites_fl.h"
namespace cc = x3m::chase_camera;
namespace S = x3m::fl::chase_camera_fl;
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
bool quiet = false; // the bit-flip sweep logs hundreds of refusal rows: counted, not printed
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
    return false;
}
}
// The view restore's observer (src/proxy/chase_transition.cpp, FL branch): the handler hands it every readable visit.
namespace chase_transition {
unsigned observed_frames = 0, control_queries = 0;
std::uint32_t observed_mode = 0;
bool active_control = false; // the fixture's answer to the unbound-phase proof
std::uintptr_t control_cockpit = 0;
bool active_control_cockpit(std::uintptr_t cockpit) noexcept {
    ++control_queries;
    control_cockpit = cockpit;
    return active_control;
}
void restore_observe_frame(const AppliedFrame& frame) noexcept {
    ++observed_frames;
    observed_mode = frame.mode;
}
// The lifetime/update token the linked FL lead module asks for (src/engine/fl/chase_lead_fl.cpp): reported as not
// installed, so the lead keeps its pose-seam binding here (fl_chase_lead.cpp covers both bindings).
bool installed() {
    return false;
}
std::uint64_t generation(std::uintptr_t) noexcept {
    return 0;
}
Update current_update(std::uintptr_t) noexcept {
    return {};
}
}
}

extern "C" {
uint32_t in_eax, in_ebx, in_ecx, in_edx, in_esi, in_edi, in_ebp, esp_skew, site_addr, saved_esp, esp_at_jump, ref_flags;
uint32_t in_mxcsr, host_mxcsr, out_mxcsr;
std::uint16_t in_fpucw, out_fpucw, out_fpusw;
double in_st0, out_st0;
alignas(16) unsigned char in_xmm[128], out_xmm[128];
uint32_t out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, out_esp, out_flags, out_branch;
void fl_cc_enter();
void fl_cc_fallthrough();
void fl_cc_taken();
}
#define XMM_IN(i) "movups _in_xmm+" #i "*16,%xmm" #i "\n\t"
#define XMM_OUT(i) "movups %xmm" #i ",_out_xmm+" #i "*16\n\t"
asm(".text\n\t"
    ".globl _fl_cc_enter\n_fl_cc_enter:\n\t"
    "pushal\n\tpushfl\n\tmovl %esp,_saved_esp\n\t"
    "stmxcsr _host_mxcsr\n\tfninit\n\tfldcw _in_fpucw\n\tfldl _in_st0\n\tldmxcsr _in_mxcsr\n\t"
    XMM_IN(0) XMM_IN(1) XMM_IN(2) XMM_IN(3) XMM_IN(4) XMM_IN(5) XMM_IN(6) XMM_IN(7)
    "subl _esp_skew,%esp\n\t"
    "movl _in_ebx,%ebx\n\tmovl _in_ecx,%ecx\n\tmovl _in_edx,%edx\n\tmovl _in_esi,%esi\n\tmovl _in_edi,%edi\n\t"
    "movl _in_ebp,%ebp\n\t"
    // Every arithmetic flag set on entry; the reference is the native compare on the same memory and flags.
    "pushfl\n\torl $0x8d5,(%esp)\n\tpopfl\n\t"
    "pushfl\n\tcmpl $0,0x54(%ebx)\n\tpushfl\n\tpopl _ref_flags\n\tpopfl\n\t"
    "movl _in_eax,%eax\n\tmovl %esp,_esp_at_jump\n\t"
    "jmp *_site_addr\n\t"
    ".globl _fl_cc_fallthrough\n_fl_cc_fallthrough:\n\t"
    "pushfl\n\tpopl _out_flags\n\tmovl $1,_out_branch\n\tjmp .Lfl_cc_record\n\t"
    ".globl _fl_cc_taken\n_fl_cc_taken:\n\t"
    "pushfl\n\tpopl _out_flags\n\tmovl $2,_out_branch\n"
    ".Lfl_cc_record:\n\t"
    "movl %eax,_out_eax\n\tmovl %ebx,_out_ebx\n\tmovl %ecx,_out_ecx\n\tmovl %edx,_out_edx\n\tmovl %esi,_out_esi\n\t"
    "movl %edi,_out_edi\n\tmovl %ebp,_out_ebp\n\tmovl %esp,_out_esp\n\t"
    XMM_OUT(0) XMM_OUT(1) XMM_OUT(2) XMM_OUT(3) XMM_OUT(4) XMM_OUT(5) XMM_OUT(6) XMM_OUT(7)
    "stmxcsr _out_mxcsr\n\tfnstcw _out_fpucw\n\tfnstsw _out_fpusw\n\tfstpl _out_st0\n\t"
    "fninit\n\tldmxcsr _host_mxcsr\n\tcld\n\t"
    "movl _saved_esp,%esp\n\tpopfl\n\tpopal\n\tret\n\t");

constexpr std::size_t image_size = 0x1b0000;
unsigned char* image = nullptr;
std::uintptr_t base = 0;
unsigned char original_site[S::site_length], seam_now[S::seam_window_length], target_now[S::target_window_length],
    plane_now[S::plane_window_length];
bool site_original() {
    return !std::memcmp(image + S::site_rva, original_site, S::site_length);
}
void lay_out() {
    std::memset(image, 0xcc, image_size);
    std::memcpy(image + S::seam_window_rva, S::expected_seam_window, S::seam_window_length);
    std::memcpy(image + S::target_window_rva, S::expected_target_window, S::target_window_length);
    std::memcpy(image + S::plane_window_rva, S::expected_plane_window, S::plane_window_length);
    const uint32_t slot = uint32_t(base + S::default_view_plane_slot_rva);
    std::memcpy(image + S::plane_window_rva + S::plane_window_fixup_offset, &slot, 4);
}
void recorder(std::uint32_t rva, void (*target)()) {
    unsigned char* p = image + rva;
    const uint32_t to = uint32_t(reinterpret_cast<std::uintptr_t>(target));
    p[0] = 0x68;
    std::memcpy(p + 1, &to, 4);
    p[5] = 0xc3;
    FlushInstructionCache(GetCurrentProcess(), p, 6);
}

// The synthetic scene: cockpit, sector camera, reference ship with its node and simulation data, default plane.
struct Scene {
    unsigned char* cockpit;
    unsigned char* camera;
    unsigned char* ship;
    unsigned char* node;
    unsigned char* data;
    unsigned char* plane;
};
Scene scene{};
void put32(unsigned char* p, unsigned off, uint32_t v) {
    std::memcpy(p + off, &v, 4);
}
uint32_t get32(const unsigned char* p, unsigned off) {
    uint32_t v;
    std::memcpy(&v, p + off, 4);
    return v;
}
void put_rows(unsigned char* p, unsigned off, uint32_t pad) { // identity, 16.16, 16-byte rows with a fourth word
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = 0; c < 4; ++c) put32(p, off + 16 * r + 4 * c, c == 3 ? pad : (r == c ? 0x10000u : 0u));
}
uint32_t addr(const void* p) {
    return uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
void build_scene() {
    auto* block = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    scene = {block, block + 0x1000, block + 0x2000, block + 0x3000, block + 0x4000, block + 0x6000};
    std::memset(block, 0, 0x10000);
    unsigned char* k = scene.cockpit;
    put32(k, 0x0c, addr(scene.ship));
    put32(k, 0x10, addr(scene.ship));
    put32(k, 0x54, 0x00a0b0c0);
    put32(k, 0x58, addr(scene.camera));
    put_rows(k, 0xf0, 0x5a5a0f00);
    put32(k, 0x150, 258);
    put32(k, 0x1e0, 0xfffff000); // an unreadable tracked object: the handler's bounded read refuses it (no lock)
    put32(k, 0x1fc, 7);
    unsigned char* c = scene.camera;
    put32(c, 0x30, 0);
    put32(c, 0x34, 200);
    put32(c, 0x38, uint32_t(-2000));
    put_rows(c, 0x40, 0xa5a50400);
    put32(c, 0x298, 10430); // binary angle: half-FOV 0.5 rad
    put32(c, 0x300, 0x10000);
    put32(c, 0x304, 0xc000);
    unsigned char* s = scene.ship;
    std::uint16_t type = 7;
    std::memcpy(s + 0x48, &type, 2);
    put32(s, 0x50, addr(scene.data));
    put32(s, 0x54, 0); // no owner: the render-domain anchor (node+0xb0, node+0xc0)
    put32(s, 0x70, addr(scene.node));
    put_rows(scene.node, 0xc0, 0);
    put_rows(scene.node, 0x40, 0);
    put_rows(scene.data, 0x870, 0);
    put32(scene.plane, 0x28, 0x10000);
    put32(scene.plane, 0x2c, 0xc000);
    put32(image, S::default_view_plane_slot_rva, addr(scene.plane));
}

void set_camera(const wchar_t* value) {
    SetEnvironmentVariableW(L"X3M_CAMERA", value);
}
bool row_has(const char* text) {
    return std::strstr(last_row, text) != nullptr;
}
// A fresh initialize() at `at` that must refuse with `status` and leave the site's ten bytes as they were.
void refused(const char* label, std::uintptr_t at, const char* status) {
    cc::fixture_reset();
    cc::fixture_base(at);
    const unsigned before = rows;
    const DWORD sentinel = 0x2bad0001;
    SetLastError(sentinel);
    const bool ok = cc::initialize();
    char want[96];
    std::snprintf(want, sizeof want, "chase_camera requested=1 installed=0 status=%s ", status);
    check(!ok && !cc::installed() && GetLastError() == sentinel && rows == before + 1 && row_has(want) &&
              row_has(" edition=fl ") && site_original(),
          label, last_row);
}

struct Visit {
    uint32_t branch;
    bool regs, flags, xmm, x87, mxcsr, last_error, esp;
};
const DWORD visit_error = 0x1234abcd;
Visit visit(unsigned char* cockpit_ebx, uint32_t skew) {
    in_eax = 0xa0a0a0a0;
    in_ebx = addr(cockpit_ebx);
    in_ecx = 0xc0c0c0c0;
    in_edx = 0xd0d0d0d0;
    in_esi = 0x51515151;
    in_edi = 0xd1d1d1d1;
    in_ebp = 0xb0b0b0b0;
    esp_skew = skew;
    in_fpucw = 0x007f; // single precision, all masked: the D3D-style engine control word, not the CRT default
    in_st0 = 1234.5678125;
    in_mxcsr = 0xbfe0; // FTZ, DAZ, round down, a sticky precision flag
    for (unsigned i = 0; i < sizeof in_xmm; ++i) in_xmm[i] = static_cast<unsigned char>(0x11 * (i / 16 + 1) + i);
    std::memset(out_xmm, 0, sizeof out_xmm);
    out_branch = 0;
    SetLastError(visit_error);
    fl_cc_enter();
    const DWORD error = GetLastError();
    Visit v{};
    v.branch = out_branch;
    v.regs = out_eax == in_eax && out_ebx == in_ebx && out_ecx == in_ecx && out_edx == in_edx && out_esi == in_esi &&
             out_edi == in_edi && out_ebp == in_ebp;
    v.esp = out_esp == esp_at_jump && ((saved_esp - esp_at_jump) == skew);
    v.flags = out_flags == ref_flags;
    v.xmm = !std::memcmp(out_xmm, in_xmm, sizeof in_xmm);
    v.x87 = out_fpucw == in_fpucw && out_st0 == in_st0 && ((out_fpusw >> 11) & 7) == 7;
    v.mxcsr = out_mxcsr == in_mxcsr;
    v.last_error = error == visit_error;
    return v;
}
bool all_state(const Visit& v) {
    return v.regs && v.flags && v.xmm && v.x87 && v.mxcsr && v.last_error && v.esp;
}
void report_visit(const char* label, const Visit& v, uint32_t branch) {
    char detail[160];
    std::snprintf(detail, sizeof detail, "branch=%u want=%u regs=%u flags=%u(%08x/%08x) xmm=%u x87=%u mxcsr=%u error=%u esp=%u",
                  v.branch, branch, v.regs, v.flags, out_flags, ref_flags, v.xmm, v.x87, v.mxcsr, v.last_error, v.esp);
    check(v.branch == branch && all_state(v), label, detail);
}

int main() {
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    base = reinterpret_cast<std::uintptr_t>(image);
    check(image && !(base & 0xffff), "image_64k_aligned");
    if (!image) return 1;
    lay_out();
    build_scene();
    std::memcpy(original_site, image + S::site_rva, S::site_length);
    std::memcpy(seam_now, image + S::seam_window_rva, S::seam_window_length);
    std::memcpy(target_now, image + S::target_window_rva, S::target_window_length);
    std::memcpy(plane_now, image + S::plane_window_rva, S::plane_window_length);
    unsigned char masked[S::plane_window_length];
    std::memcpy(masked, plane_now, sizeof masked);
    std::memset(masked + S::plane_window_fixup_offset, 0, 4);
    std::printf("WINDOWS seam_fnv1a=%08x target_fnv1a=%08x plane_fnv1a_masked=%08x site_mod8=%u\n",
                S::fnv1a(seam_now, S::seam_window_length), S::fnv1a(target_now, S::target_window_length),
                S::fnv1a(masked, S::plane_window_length), unsigned((base + S::site_rva) & 7));

    // Unset: nothing requested, no row, nothing written.
    set_camera(nullptr);
    cc::fixture_reset();
    cc::fixture_base(base);
    unsigned before = rows;
    check(!cc::initialize() && rows == before && site_original() && !std::strcmp(cc::status(), "disabled"), "unset");
    set_camera(L"chase");
    // The fixture's own main module is not X3FL.exe (fl::module_base() == 0).
    refused("executable_mismatch", 0, "executable_mismatch");
    refused("invalid_base", base + 0x1000, "invalid_base");
    SetEnvironmentVariableW(L"X3M_CHASE_ROT_TAU", L"fast");
    refused("invalid_tunables", base, "invalid_tunables");
    SetEnvironmentVariableW(L"X3M_CHASE_ROT_TAU", nullptr);

    // Every single-bit change of each window (the plane fixup included) is refused before any write.
    struct Sweep {
        const char* name;
        std::uint32_t rva;
        unsigned length;
        const char* status;
    } sweeps[] = {{"seam", S::seam_window_rva, S::seam_window_length, "seam_mismatch"},
                  {"target", S::target_window_rva, S::target_window_length, "target_mismatch"},
                  {"plane", S::plane_window_rva, S::plane_window_length, nullptr}};
    unsigned flips[3] = {0, 0, 0}, refusals[3] = {0, 0, 0};
    quiet = true;
    for (unsigned w = 0; w < 3; ++w) {
        for (unsigned bit = 0; bit < sweeps[w].length * 8; ++bit) {
            image[sweeps[w].rva + bit / 8] ^= static_cast<unsigned char>(1u << (bit % 8));
            const bool fixup = w == 2 && bit / 8 >= S::plane_window_fixup_offset && bit / 8 < S::plane_window_fixup_offset + 4;
            const char* status = w < 2 ? sweeps[w].status : fixup ? "plane_slot_mismatch" : "plane_window_mismatch";
            cc::fixture_reset();
            cc::fixture_base(base);
            const bool ok = cc::initialize();
            char want[96];
            std::snprintf(want, sizeof want, "status=%s ", status);
            image[sweeps[w].rva + bit / 8] ^= static_cast<unsigned char>(1u << (bit % 8));
            ++flips[w];
            if (!ok && !cc::installed() && row_has(want) && site_original()) ++refusals[w];
        }
    }
    quiet = false;
    std::printf("FLIPS seam=%u refused=%u target=%u refused=%u plane=%u refused=%u\n", flips[0], refusals[0], flips[1],
                refusals[1], flips[2], refusals[2]);
    check(flips[0] == refusals[0] && flips[1] == refusals[1] && flips[2] == refusals[2], "every_flip_refused");
    check(!std::memcmp(image + S::seam_window_rva, seam_now, S::seam_window_length), "windows_restored_after_sweep");

    // Partial install: the claim writes the jump, a foreign write changes a byte of it in memory before the read-back,
    // and the original ten bytes go back over it (engine_patch::restore alone would refuse: not owned).
    cc::fixture_reset();
    cc::fixture_base(base);
    cc::fixture_fault_readback(1);
    before = rows;
    check(!cc::initialize() && !cc::installed() && rows == before + 1 && site_original() &&
              row_has("installed=0 status=readback_mismatch ") && row_has(" rollback=restored "),
          "readback_rollback", last_row);
    // Failed rollback: foreign bytes inside the jump (+2) and beyond it (+6, the displaced je's rel32): the forced
    // five-byte restore cannot make all ten bytes original, so the site stays registered and inert (no stub chained),
    // and the row and installed() say so.
    cc::fixture_reset();
    cc::fixture_base(base);
    cc::fixture_fault_readback(2, 6);
    before = rows;
    const bool failed_install = cc::initialize();
    check(!failed_install && cc::installed() && rows == before + 1 &&
              row_has("installed=1 status=readback_mismatch ") && row_has(" rollback=failed ") &&
              !std::strcmp(cc::status(), "readback_mismatch") &&
              !std::memcmp(image + S::site_rva, original_site, 5),
          "readback_rollback_failed_registered", last_row);
    image[S::site_rva + 6] ^= 0xff; // the fixture undoes the foreign byte before the next case
    check(site_original(), "foreign_bytes_undone");

    // The install.
    cc::fixture_reset();
    cc::fixture_base(base);
    const DWORD sentinel = 0x2bad0002;
    SetLastError(sentinel);
    before = rows;
    const bool installed = cc::initialize();
    char site_text[32];
    std::snprintf(site_text, sizeof site_text, "site=0x%08lx ", static_cast<unsigned long>(base + S::site_rva));
    check(installed && cc::installed() && GetLastError() == sentinel && rows == before + 1 &&
              row_has("chase_camera requested=1 installed=1 status=active ") && row_has(site_text) &&
              row_has(" length=10 rel32_offset=6 atomic_write=0 ") && row_has(" edition=fl rva=0x0227ad rollback=none "),
          "install_row", last_row);
    check(image[S::site_rva] == 0xe9 && !std::memcmp(image + S::site_rva + 5, original_site + 5, 5), "jump_written_tail_kept");
    if (!installed) {
        std::printf("RESULT FAIL checks=%u failures=%u\n", checks, failures);
        return 1;
    }
    // The tail: the displaced compare and branch, the branch's rel32 re-based to the same absolute target, the jump back.
    const unsigned char* jump = image + S::site_rva;
    const std::uintptr_t dispatcher = base + S::site_rva + 5 + std::uintptr_t(std::int32_t(get32(jump, 1)));
    const auto* dispatch = reinterpret_cast<const unsigned char*>(dispatcher);
    void** entry = reinterpret_cast<void**>(get32(dispatch, 2));
    const auto* stub = static_cast<const unsigned char*>(*entry);
    std::printf("TRAMPOLINE dispatcher=%08x stub=%08x\n", uint32_t(dispatcher), addr(stub));
    check(dispatch[0] == 0xff && dispatch[1] == 0x25 && stub[0] == 0x9c && stub[1] == 0x60, "dispatcher_and_stub");

    recorder(S::continuation_rva, &fl_cc_fallthrough);
    recorder(S::branch_target_rva, &fl_cc_taken);
    site_addr = uint32_t(base + S::site_rva);

    // Bound cockpit, both outcomes of the replayed compare, every 4-byte ESP skew.
    unsigned char vanilla[0x70];
    std::memcpy(vanilla, scene.camera, sizeof vanilla);
    for (uint32_t skew = 0; skew < 16; skew += 4) {
        char label[64];
        put32(scene.cockpit, 0x54, 0x00a0b0c0);
        std::memcpy(scene.camera, vanilla, sizeof vanilla); // the engine re-poses the camera every frame
        const auto s0 = cc::stats();
        Visit v = visit(scene.cockpit, skew);
        std::snprintf(label, sizeof label, "bound_fallthrough_skew%u", skew);
        report_visit(label, v, 1);
        const auto s1 = cc::stats();
        std::snprintf(label, sizeof label, "bound_fallthrough_applied_skew%u", skew);
        check(s1.frames == s0.frames + 1 && s1.applied == s0.applied + 1 && s1.inactive == s0.inactive, label);
        put32(scene.cockpit, 0x54, 0);
        std::memcpy(scene.camera, vanilla, sizeof vanilla);
        v = visit(scene.cockpit, skew);
        std::snprintf(label, sizeof label, "bound_taken_skew%u", skew);
        report_visit(label, v, 2);
        const auto s2 = cc::stats();
        std::snprintf(label, sizeof label, "bound_taken_applied_skew%u", skew);
        check(s2.frames == s1.frames + 1 && s2.applied == s1.applied + 1, label);
    }
    // The handler found the camera through EBX: the pose (position and basis) was written, the fourth words kept.
    bool moved = std::memcmp(scene.camera + 0x30, vanilla + 0x30, 12) != 0;
    bool pads = get32(scene.camera, 0x4c) == 0xa5a50400 && get32(scene.camera, 0x5c) == 0xa5a50400 &&
                get32(scene.camera, 0x6c) == 0xa5a50400 && get32(scene.cockpit, 0xfc) == 0x5a5a0f00 &&
                get32(scene.cockpit, 0x10c) == 0x5a5a0f00 && get32(scene.cockpit, 0x11c) == 0x5a5a0f00;
    std::printf("POSE vanilla=0,200,-2000 written=%d,%d,%d\n", int(get32(scene.camera, 0x30)), int(get32(scene.camera, 0x34)),
                int(get32(scene.camera, 0x38)));
    check(moved && pads, "pose_written_via_ebx");
    // LastError: engine_memory::read keeps it around its own VirtualQuery, and the handler's other calls succeed, so
    // the preserved value above shows no leak on this path; it does not exercise PreserveCpuState's restore.

    // Inactive (view object != reference object) and unbound (+0x10 == 0) without the active-control proof: counted,
    // nothing written, the native branch still replayed; the proof is asked only for the +0x10 == 0 visit.
    std::memcpy(scene.camera, vanilla, sizeof vanilla);
    put32(scene.cockpit, 0x54, 0x00a0b0c0);
    put32(scene.cockpit, 0x10, addr(scene.data));
    auto s0 = cc::stats();
    Visit v = visit(scene.cockpit, 4);
    report_visit("inactive_view_object", v, 1);
    check(x3m::chase_transition::control_queries == 0, "proof_not_asked_for_a_bound_or_inactive_view");
    put32(scene.cockpit, 0x10, 0);
    v = visit(scene.cockpit, 8);
    report_visit("unbound_view_object", v, 1);
    auto s1 = cc::stats();
    check(s1.inactive == s0.inactive + 2 && s1.unbound == s0.unbound && s1.frames == s0.frames &&
              !std::memcmp(scene.camera, vanilla, sizeof vanilla) && x3m::chase_transition::control_queries == 1 &&
              x3m::chase_transition::control_cockpit == addr(scene.cockpit),
          "inactive_and_unbound_not_written");
    // Unbound with the active-control proof (the run78 pose gap after a restore): admitted, the pose written.
    x3m::chase_transition::active_control = true;
    v = visit(scene.cockpit, 12);
    report_visit("unbound_with_control", v, 1);
    auto s1b = cc::stats();
    check(s1b.unbound == s1.unbound + 1 && s1b.frames == s1.frames + 1 && s1b.applied == s1.applied + 1 &&
              s1b.inactive == s1.inactive && std::memcmp(scene.camera, vanilla, sizeof vanilla) != 0 &&
              x3m::chase_transition::control_queries == 2,
          "unbound_with_control_admitted_and_written");
    x3m::chase_transition::active_control = false;
    s1 = s1b;
    put32(scene.cockpit, 0x10, addr(scene.ship));
    // An unaligned EBX: refused (verdict 100) before any read; the compare still runs on the native address.
    v = visit(scene.cockpit + 1, 12);
    report_visit("unaligned_cockpit", v, get32(scene.cockpit + 1, 0x54) ? 1u : 2u);
    auto s2 = cc::stats();
    check(s2.refused == s1.refused + 1 && s2.last_verdict == 100, "unaligned_cockpit_refused");
    const auto st = cc::stats();
    std::printf("STATS frames=%llu applied=%llu refused=%llu inactive=%llu unbound=%llu snaps=%llu\n",
                static_cast<unsigned long long>(st.frames), static_cast<unsigned long long>(st.applied),
                static_cast<unsigned long long>(st.refused), static_cast<unsigned long long>(st.inactive),
                static_cast<unsigned long long>(st.unbound), static_cast<unsigned long long>(st.snaps));
    cc::report(300);
    check(cc::pose_write_count() == 9, "pose_write_count");

    // Explicit teardown: the recorders (fixture-only) go first, then shutdown() puts the original ten bytes back.
    std::memcpy(image + S::continuation_rva, seam_now + (S::continuation_rva - S::seam_window_rva), 6);
    std::memcpy(image + S::branch_target_rva, target_now, 6);
    cc::shutdown();
    check(!cc::installed() && site_original() && row_has("chase_camera_shutdown restored=1 status=restored") &&
              !std::memcmp(image + S::seam_window_rva, seam_now, S::seam_window_length),
          "shutdown_restores_ten_bytes", last_row);
    // A second shutdown is a no-op; a re-install over the restored bytes claims again and restores again.
    cc::shutdown();
    cc::fixture_reset();
    cc::fixture_base(base);
    check(cc::initialize() && cc::installed() && image[S::site_rva] == 0xe9, "reinstall_after_shutdown", last_row);
    cc::shutdown();
    check(!cc::installed() && site_original(), "second_restore");

    // Late window: after the first Present nothing is claimed.
    x3m::engine_patch::close_install_window("fixture_present");
    refused("late_claim", base, "late_claim");
    check(row_has("window_closed_by=fixture_present"), "late_claim_reason", last_row);
    std::printf("OBSERVED frames=%u mode=%u\n", x3m::chase_transition::observed_frames,
                unsigned(x3m::chase_transition::observed_mode));
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
