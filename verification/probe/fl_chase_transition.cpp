// Original synthetic fixture of the FL chase view restore (src/proxy/chase_transition.cpp built with X3M_EDITION_FL=1,
// src/engine/fl/chase_transition_sites_fl.h; docs/reverse-engineering/fl/chase-transition.md, "Verified 2026-11-02"):
// no game code, no game bytes beyond the identity windows of the sites header, no launch. Built with
// -DX3M_EDITION_FL=1 -DX3M_CHASE_TRANSITION_FL_FIXTURE by verification/analysis/test_fl_chase_transition.py and run
// natively (WSL interop). The production source is included so the scenarios can read the module's state.
//
// A 64 KB-aligned private image stands in for X3FL.exe at `base`: the 26 windows at their RVAs (fixups relocated to
// the base), the three root slots pointing at fixture-owned registries, game object, ship and cockpits. After the
// install, `jmp ct_record` recorders replace the bytes at each span's continuation and a `jmp fake_script_call` the
// named dispatcher's entry. A harness enters a site with sentinels in every GPR, all arithmetic flags and DF set (or
// all clear), eight XMM sentinels, a loaded x87 stack with a non-default control word, a non-default MXCSR, a LastError
// sentinel and every 4-byte ESP skew mod 16; the recorder captures what the native continuation would see. The same
// input runs once more through a reference copy of the original span (plain bytes + jmp ct_record): registers, flags,
// ESP, XMM, x87, MXCSR, LastError and the stack/cockpit words the span writes must be identical.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../../src/proxy/chase_transition.cpp"
#include "../../src/proxy/engine_patch.cpp" // in this unit: the fixture rewinds the arena between installs
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
namespace S = x3m::fl::chase_transition_fl;
namespace T = x3m::chase_transition;
namespace D = x3m::chase_transition::detail;
using std::uint32_t;
unsigned checks = 0, failures = 0;
void check(bool value, const char* label, const char* detail = "") {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s %s\n", label, detail);
    }
}
char last_row[4096], outcome_row[4096], transition_row[4096], restore_row[4096];
unsigned rows = 0, outcome_rows = 0, rows_under_lock = 0;
bool quiet = false;
namespace x3m {
void log(const char* format, ...) {
    if (TryAcquireSRWLockExclusive(&x3m::chase_transition::lock))
        ReleaseSRWLockExclusive(&x3m::chase_transition::lock);
    else
        ++rows_under_lock;
    va_list args;
    va_start(args, format);
    std::vsnprintf(last_row, sizeof last_row, format, args);
    va_end(args);
    ++rows;
    if (!std::strncmp(last_row, "chase_transition installed=", 27)) std::memcpy(transition_row, last_row, sizeof transition_row);
    if (!std::strncmp(last_row, "chase_view_restore installed=", 29)) std::memcpy(restore_row, last_row, sizeof restore_row);
    if (!std::strncmp(last_row, "chase_view_restore result=", 26)) {
        std::memcpy(outcome_row, last_row, sizeof outcome_row);
        ++outcome_rows;
    }
    if (!quiet) std::printf("LOG %s\n", last_row);
}
namespace telemetry {
bool enabled() {
    return false;
}
}
namespace chase_camera {
bool wanted() {
    return true;
}
bool installed() {
    return true;
}
}
namespace chase_lead {
unsigned invalidations = 0;
void native_timing_invalidate(std::uintptr_t, std::uint32_t) noexcept {
    ++invalidations;
}
}
namespace fl {
std::uintptr_t module_base() {
    return 0; // the fixture binds through fixture_base_
}
}
}

// ---------------- harness ----------------
extern "C" {
uint32_t in_eax, in_ebx, in_ecx, in_edx, in_esi, in_edi, in_ebp, in_esp, in_flags, site_addr, saved_esp;
uint32_t in_mxcsr, host_mxcsr, out_mxcsr;
std::uint16_t in_fpucw, out_fpucw;
double in_st0, out_st0;
alignas(16) unsigned char in_xmm[128], out_xmm[128];
uint32_t out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, out_esp, out_flags;
void ct_enter();
void ct_record();
uint32_t __cdecl fake_script_call(uint32_t callback, uint32_t token, uint32_t key, const char* name, uint32_t kind,
                                  uint32_t argc, uint32_t value);
}
#define XMM_IN(i) "movups _in_xmm+" #i "*16,%xmm" #i "\n\t"
#define XMM_OUT(i) "movups %xmm" #i ",_out_xmm+" #i "*16\n\t"
asm(".text\n\t"
    ".globl _ct_enter\n_ct_enter:\n\t"
    "pushal\n\tpushfl\n\tmovl %esp,_saved_esp\n\t"
    "stmxcsr _host_mxcsr\n\tfninit\n\tfldcw _in_fpucw\n\tfldl _in_st0\n\tldmxcsr _in_mxcsr\n\t"
    XMM_IN(0) XMM_IN(1) XMM_IN(2) XMM_IN(3) XMM_IN(4) XMM_IN(5) XMM_IN(6) XMM_IN(7)
    "pushl _in_flags\n\tpopfl\n\t"
    "movl _in_esp,%esp\n\t"
    "movl _in_eax,%eax\n\tmovl _in_ebx,%ebx\n\tmovl _in_ecx,%ecx\n\tmovl _in_edx,%edx\n\tmovl _in_esi,%esi\n\t"
    "movl _in_edi,%edi\n\tmovl _in_ebp,%ebp\n\t"
    "jmp *_site_addr\n\t"
    ".globl _ct_record\n_ct_record:\n\t"
    "movl %eax,_out_eax\n\tmovl %ebx,_out_ebx\n\tmovl %ecx,_out_ecx\n\tmovl %edx,_out_edx\n\tmovl %esi,_out_esi\n\t"
    "movl %edi,_out_edi\n\tmovl %ebp,_out_ebp\n\tmovl %esp,_out_esp\n\t"
    "pushfl\n\tpopl _out_flags\n\t"
    XMM_OUT(0) XMM_OUT(1) XMM_OUT(2) XMM_OUT(3) XMM_OUT(4) XMM_OUT(5) XMM_OUT(6) XMM_OUT(7)
    "stmxcsr _out_mxcsr\n\tfnstcw _out_fpucw\n\tfstpl _out_st0\n\t"
    "fninit\n\tldmxcsr _host_mxcsr\n\tcld\n\t"
    "movl _saved_esp,%esp\n\tpopfl\n\tpopal\n\tret\n\t");

struct Cpu {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp, flags, mxcsr, last_error;
    std::uint16_t fpucw;
    double st0;
    unsigned char xmm[128];
};
// The harness globals, for a nested entry from inside an engine call (the fake dispatcher's mode store).
struct HarnessState {
    uint32_t r[18];
    std::uint16_t cw[2];
    double st[2];
    unsigned char x[256];
};
HarnessState save_harness() {
    HarnessState h{};
    const uint32_t r[18] = {in_eax, in_ebx, in_ecx, in_edx, in_esi, in_edi, in_ebp, in_esp, in_flags, site_addr,
                            saved_esp, in_mxcsr, host_mxcsr, out_mxcsr, out_eax, out_esp, out_flags, out_ebp};
    std::memcpy(h.r, r, sizeof r);
    h.cw[0] = in_fpucw;
    h.cw[1] = out_fpucw;
    h.st[0] = in_st0;
    h.st[1] = out_st0;
    std::memcpy(h.x, in_xmm, 128);
    std::memcpy(h.x + 128, out_xmm, 128);
    return h;
}
void load_harness(const HarnessState& h) {
    in_eax = h.r[0], in_ebx = h.r[1], in_ecx = h.r[2], in_edx = h.r[3], in_esi = h.r[4], in_edi = h.r[5];
    in_ebp = h.r[6], in_esp = h.r[7], in_flags = h.r[8], site_addr = h.r[9], saved_esp = h.r[10], in_mxcsr = h.r[11];
    host_mxcsr = h.r[12], out_mxcsr = h.r[13], out_eax = h.r[14], out_esp = h.r[15], out_flags = h.r[16];
    out_ebp = h.r[17];
    in_fpucw = h.cw[0], out_fpucw = h.cw[1], in_st0 = h.st[0], out_st0 = h.st[1];
    std::memcpy(in_xmm, h.x, 128);
    std::memcpy(out_xmm, h.x + 128, 128);
}
// One entry: `in` supplies every GPR, ESP, EFLAGS and the hostile FP/LastError image.
Cpu enter(uint32_t site, const Cpu& in) {
    in_eax = in.eax, in_ebx = in.ebx, in_ecx = in.ecx, in_edx = in.edx, in_esi = in.esi, in_edi = in.edi;
    in_ebp = in.ebp, in_esp = in.esp, in_flags = in.flags, in_mxcsr = in.mxcsr, in_fpucw = in.fpucw, in_st0 = in.st0;
    std::memcpy(in_xmm, in.xmm, 128);
    site_addr = site;
    SetLastError(in.last_error);
    ct_enter();
    Cpu out{};
    out.last_error = GetLastError();
    out.eax = out_eax, out.ebx = out_ebx, out.ecx = out_ecx, out.edx = out_edx, out.esi = out_esi, out.edi = out_edi;
    out.ebp = out_ebp, out.esp = out_esp, out.flags = out_flags, out.mxcsr = out_mxcsr, out.fpucw = out_fpucw;
    out.st0 = out_st0;
    std::memcpy(out.xmm, out_xmm, 128);
    return out;
}

// ---------------- the synthetic image and engine objects ----------------
unsigned char* image = nullptr;
std::uintptr_t base = 0;
unsigned char* world = nullptr; // registries, game, ship, cockpits, VM task/contexts, frames
unsigned char* stack_mem = nullptr;
unsigned char* reference = nullptr; // executable copies of the original spans
uint32_t addr(const void* p) {
    return uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
void put32(std::uintptr_t at, uint32_t v) {
    std::memcpy(reinterpret_cast<void*>(at), &v, 4);
}
uint32_t get32(std::uintptr_t at) {
    uint32_t v;
    std::memcpy(&v, reinterpret_cast<const void*>(at), 4);
    return v;
}
void lay_out() {
    std::memset(image, 0xcc, S::image_size);
    for (const S::Window& w : S::windows) {
        std::memcpy(image + w.rva, w.bytes, w.length);
        for (unsigned f = 0; f < w.fixups; ++f)
            put32(base + w.rva + w.fixup[f].offset, uint32_t(base + w.fixup[f].target_rva));
    }
}
bool span_original(const S::SiteRow& s) {
    unsigned char want[8];
    S::expected(s, base, want);
    return !std::memcmp(image + s.rva, want, s.length);
}
bool all_original() {
    for (const auto& s : S::sites)
        if (!span_original(s)) return false;
    for (const auto& s : S::restore_sites)
        if (!span_original(s)) return false;
    return true;
}
void jump(std::uintptr_t from, const void* to) {
    auto* p = reinterpret_cast<unsigned char*>(from);
    const uint32_t rel = uint32_t(reinterpret_cast<std::uintptr_t>(to) - (from + 5));
    p[0] = 0xe9;
    std::memcpy(p + 1, &rel, 4);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
}
// World layout (offsets in `world`).
constexpr unsigned w_cockpit_registry = 0x0000, w_cockpit_table = 0x0040, w_cockpit_buckets = 0x0080,
                   w_cockpit_rows = 0x0100, w_sim = 0x0400, w_sim_table = 0x0440, w_sim_buckets = 0x0480,
                   w_sim_rows = 0x0500, w_game = 0x0800, w_ship = 0x1000, w_ship2 = 0x1400, w_cockpit_a = 0x2000,
                   w_cockpit_b = 0x3000, w_cockpit_c = 0x4000, w_task = 0x5000, w_task_stack = 0x5400,
                   w_monitor_ctx = 0x6000, w_manager_ctx = 0x6040, w_monitor_desc = 0x6080, w_manager_desc = 0x60c0,
                   w_frame = 0x6100, w_frame_bad = 0x6140, w_world_size = 0x8000;
constexpr uint32_t ship_native = 0x1234, ship2_native = 0x5678, handle_id = 0x77, controller_id = 0x2b,
                   monitor_id = 0xfffffde8u, monitor_class = 0x25e, manager_id = 0xfffffde0u, manager_class = 0x25d;
std::uintptr_t W(unsigned off) {
    return reinterpret_cast<std::uintptr_t>(world) + off;
}
void set_active(std::uintptr_t cockpit) { // cockpit registry: one row {next, key=handle, value=cockpit}
    put32(W(w_cockpit_rows) + 8, uint32_t(cockpit));
}
void set_ship_live(unsigned ship_off, bool live) { // simulation registry: rows keyed by ship+8
    const unsigned row = ship_off == w_ship ? 0 : 12;
    put32(W(w_sim_rows) + row + 8, live ? uint32_t(W(ship_off)) : 0);
}
void build_world() {
    std::memset(world, 0, w_world_size);
    // Cockpit registry: [root] -> registry {+0 table, +0x10 active handle}; table {buckets, 1}.
    put32(base + S::cockpit_registry_rva, uint32_t(W(w_cockpit_registry)));
    put32(W(w_cockpit_registry), uint32_t(W(w_cockpit_table)));
    put32(W(w_cockpit_registry) + S::registry_active_handle, handle_id);
    put32(W(w_cockpit_table), uint32_t(W(w_cockpit_buckets)));
    put32(W(w_cockpit_table) + 4, 1);
    put32(W(w_cockpit_buckets), uint32_t(W(w_cockpit_rows)));
    put32(W(w_cockpit_rows) + 4, handle_id);
    // Simulation registry: [root] -> manager {+0x14 map}; map {buckets, 2}; two rows keyed by the ships' +8.
    put32(base + S::sim_registry_rva, uint32_t(W(w_sim)));
    put32(W(w_sim) + S::sim_map, uint32_t(W(w_sim_table)));
    put32(W(w_sim_table), uint32_t(W(w_sim_buckets)));
    put32(W(w_sim_table) + 4, 2);
    put32(W(w_sim_buckets) + 4 * (ship_native & 1), uint32_t(W(w_sim_rows)));
    put32(W(w_sim_buckets) + 4 * (ship2_native & 1), uint32_t(W(w_sim_rows)));
    put32(W(w_sim_rows), uint32_t(W(w_sim_rows) + 12)); // chain: ship row -> ship2 row
    put32(W(w_sim_rows) + 4, ship_native);
    put32(W(w_sim_rows) + 16, ship2_native);
    set_ship_live(w_ship, true);
    set_ship_live(w_ship2, true);
    put32(W(w_ship) + S::ship_native_id, ship_native);
    put32(W(w_ship) + S::ship_script_id, 0x00c0ffee);
    put32(W(w_ship2) + S::ship_native_id, ship2_native);
    put32(W(w_ship2) + S::ship_script_id, 0x00c0ffef);
    // Game object: controller and pause bits.
    put32(base + S::game_root_rva, uint32_t(W(w_game)));
    put32(W(w_game) + S::game_controller, controller_id);
    // VM task: +0x3c current context (the storing monitor), +0x10 capacity, +0x14 stack base, +0x18 signed index.
    put32(W(w_task) + 0x3c, uint32_t(W(w_monitor_ctx)));
    put32(W(w_monitor_ctx), monitor_id);
    put32(W(w_monitor_ctx) + 8, uint32_t(W(w_monitor_desc)));
    put32(W(w_monitor_desc), monitor_class);
    put32(W(w_manager_ctx), manager_id);
    put32(W(w_manager_ctx) + 8, uint32_t(W(w_manager_desc)));
    put32(W(w_manager_desc), manager_class);
    // The dispatcher frame at the mode store: [EBX+4] native return, [EBX+0xc] task, [EBX+0x10] command 0x30.
    put32(W(w_frame) + S::frame_return, uint32_t(base + S::native_return_rva));
    put32(W(w_frame) + S::frame_task, uint32_t(W(w_task)));
    put32(W(w_frame) + S::frame_command, S::mode_command);
    put32(W(w_frame_bad) + S::frame_return, uint32_t(base + S::native_return_rva + 1)); // not the interpreter
    put32(W(w_frame_bad) + S::frame_task, uint32_t(W(w_task)));
    put32(W(w_frame_bad) + S::frame_command, S::mode_command);
}
// Task stack (5-byte cells, top at base - 5*used): with_manager places the manager's call record above the monitor's
// own (StartMonitor's self call), else only the bottom pair (no caller: hud_refresh=skipped).
void build_task(bool with_manager) {
    unsigned char cells[16][5]{};
    unsigned n = 0;
    auto cell = [&](unsigned char tag, uint32_t v) {
        cells[n][0] = tag;
        std::memcpy(cells[n] + 1, &v, 4);
        ++n;
    };
    cell(1, 1);                                  // argc (the native's top cell)
    cell(10, uint32_t(W(w_monitor_ctx)));        // self call record (passed)
    cell(3, 0x100);
    if (with_manager) {
        cell(1, 7);
        cell(10, uint32_t(W(w_manager_ctx)));    // the caller frame: the manager
        cell(3, 0x200);
    }
    cell(10, 0);
    cell(3, 0); // bottom
    const std::uintptr_t stack_base = W(w_task_stack) + 0x200;
    std::memcpy(reinterpret_cast<void*>(stack_base - 5 * n), cells, 5 * n);
    put32(W(w_task) + 0x10, 64);
    put32(W(w_task) + 0x14, uint32_t(stack_base));
    put32(W(w_task) + 0x18, uint32_t(-std::int32_t(n)));
}
void cockpit_fields(unsigned off, uint32_t mode, uint32_t ref, uint32_t view, uint32_t sector) {
    put32(W(off) + 0x150, mode);
    put32(W(off) + 0x1c0, 0);
    put32(W(off) + 0x1a0, 0);
    put32(W(off) + 0x0c, ref);
    put32(W(off) + 0x10, view);
    put32(W(off) + 0x1fc, sector);
    put32(W(off) + 0x54, sector ^ 0x5a5a0000u);
}

// ---------------- CPU inputs ----------------
Cpu hostile(unsigned skew, bool flags_set) {
    Cpu c{};
    c.eax = 0xa1a1a1a1, c.ebx = 0xb2b2b2b2, c.ecx = 0xc3c3c3c3, c.edx = 0xd4d4d4d4, c.esi = 0xe5e5e5e5;
    c.edi = 0xf6f6f6f6, c.ebp = 0x17171717;
    c.esp = addr(stack_mem) + 0x8000 - 0x800 - skew;
    c.flags = flags_set ? 0x8d5 | 0x400 : 0x2; // CF PF AF ZF SF OF and DF, or all clear
    c.mxcsr = 0xffc0;                          // FZ, RC=11, DAZ, all masked
    c.fpucw = 0x0e7f;                          // round to zero, 53-bit
    c.st0 = 3.25;
    c.last_error = 0x1234abcd;
    for (unsigned i = 0; i < 128; ++i) c.xmm[i] = static_cast<unsigned char>(0x5a ^ (i * 7));
    return c;
}
bool same_cpu(const Cpu& a, const Cpu& b, char* why, unsigned n) {
    const char* field = nullptr;
    if (a.eax != b.eax) field = "eax";
    else if (a.ebx != b.ebx) field = "ebx";
    else if (a.ecx != b.ecx) field = "ecx";
    else if (a.edx != b.edx) field = "edx";
    else if (a.esi != b.esi) field = "esi";
    else if (a.edi != b.edi) field = "edi";
    else if (a.ebp != b.ebp) field = "ebp";
    else if (a.esp != b.esp) field = "esp";
    else if ((a.flags & 0xcd5) != (b.flags & 0xcd5)) field = "eflags";
    else if (a.mxcsr != b.mxcsr) field = "mxcsr";
    else if (a.fpucw != b.fpucw) field = "fpucw";
    else if (std::memcmp(&a.st0, &b.st0, 8)) field = "st0";
    else if (a.last_error != b.last_error) field = "last_error";
    else if (std::memcmp(a.xmm, b.xmm, 128)) field = "xmm";
    std::snprintf(why, n, "%s", field ? field : "");
    return !field;
}
// Per-span stack words the original instructions write below the entry ESP (pushes), checked like registers.
struct SpanInput {
    unsigned kind;
    bool restore;
    std::uint32_t ebp_off, ebx_off; // EBP/EBX relative to the entry ESP (frame spans), 0 = keep the sentinel
    int pushed[2];
    unsigned npushed;
};
const S::SiteRow& row(const SpanInput& s) {
    return s.restore ? S::restore_sites[s.kind] : S::sites[s.kind];
}
void* reference_span(const S::SiteRow& s) {
    static unsigned used = 0;
    unsigned char* p = reference + used;
    used += 32;
    S::expected(s, base, p);
    const std::uintptr_t after = reinterpret_cast<std::uintptr_t>(p) + s.length;
    jump(after, reinterpret_cast<const void*>(&ct_record));
    return p;
}
Cpu prepare(const SpanInput& s, Cpu c) {
    // A recognisable synthetic stack above and below ESP, a return word and one argument.
    for (unsigned i = 0; i < 0x400; i += 4) put32(c.esp - 0x200 + i, 0x51000000u + i);
    if (s.ebp_off) c.ebp = c.esp + s.ebp_off;
    if (s.ebx_off) c.ebx = c.esp + s.ebx_off;
    return c;
}
struct StackWords {
    uint32_t words[0x100];
};
StackWords stack_above(uint32_t esp) {
    StackWords w{};
    std::memcpy(w.words, reinterpret_cast<const void*>(esp), sizeof w.words);
    return w;
}

// ---------------- the fake named dispatcher ----------------
struct FakeCall {
    uint32_t callback, token, key, kind, argc, value;
    char name[24];
};
FakeCall fake_log[64];
unsigned fake_calls = 0;
uint32_t fake_select_al = 1, fake_store = 258, fake_refresh_al = 1, fake_cockpit = 0;
uint32_t fake_entry_mxcsr = 0, fake_entry_last_error = 0;
std::uint16_t fake_entry_fpucw = 0;
bool fake_nested_ok = true;
uint32_t __cdecl fake_script_call(uint32_t callback, uint32_t token, uint32_t key, const char* name, uint32_t kind,
                                  uint32_t argc, uint32_t value) {
    // The engine image at entry (the handler restored the incoming one before the call).
    unsigned mx = 0;
    std::uint16_t cw = 0;
    asm volatile("stmxcsr %0" : "=m"(mx));
    asm volatile("fnstcw %0" : "=m"(cw));
    fake_entry_mxcsr = mx;
    fake_entry_fpucw = cw;
    fake_entry_last_error = GetLastError();
    FakeCall& c = fake_log[fake_calls++ & 63];
    c = {callback, token, key, kind, argc, argc ? value : 0, {}};
    std::snprintf(c.name, sizeof c.name, "%s", name ? name : "");
    uint32_t al = fake_refresh_al;
    if (name && !std::strcmp(name, "SelectMode")) {
        al = fake_select_al;
        if (fake_store && fake_cockpit) {
            // The script's own store into the bound cockpit, through the patched mode-store site (nested entry).
            const HarnessState keep = save_harness();
            Cpu in = hostile(4, false);
            in.esp = addr(stack_mem) + 0x3000;
            in.eax = fake_cockpit;
            in.ecx = fake_store;
            in.ebx = uint32_t(W(w_frame));
            const Cpu out = enter(uint32_t(base + S::sites[6].rva), in);
            fake_nested_ok &= out.eax == fake_cockpit && out.ebx == in.ebx && out.ecx == fake_store;
            load_harness(keep);
        }
    }
    // Hostile engine: clobbers what a callee may (XMM, LastError), and leaves a different FP control image.
    SetLastError(0x0badf00d);
    asm volatile("xorps %%xmm0,%%xmm0\n\txorps %%xmm1,%%xmm1\n\txorps %%xmm7,%%xmm7" ::: "xmm0", "xmm1", "xmm7");
    const unsigned other = 0x1f80;
    asm volatile("ldmxcsr %0" ::"m"(other));
    return al | 0xabcdef00u; // only AL is the result
}

// ---------------- module control ----------------
void reset_module() {
    T::initialized = false;
    T::enabled.store(false);
    T::restore_enabled.store(false);
    T::diagnostic.store(false);
    for (auto& s : T::sites) s = x3m::engine_patch::Site{};
    for (auto& s : T::restore_sites) s = x3m::engine_patch::Site{};
    T::inject_live = false;
    T::inject_chase = false;
    T::state = D::State{};
    T::restore = D::RestoreState{};
    T::restore_epoch.store(1);
    T::restore_inject_filter.store(0);
    T::restore_eh_bumps.store(0);
    T::fl_base = 0;
    T::fl_native_return = 0;
    T::fl_refusal = "not_initialized";
    T::fl_failed_window = S::window_count;
    T::fixture_base_ = base;
    T::fixture_claims_ = 0;
    T::fixture_fault_claim_ = ~0u;
    T::native_registry_root = T::cockpit_registry_root = T::game_root = 0;
    T::script_call_address = 0;
    // Every earlier install's spans are rewritten by lay_out() before the next one, so no live code reaches the
    // earlier stubs: the fixture-only arena rewind keeps dozens of installs inside the production arena size.
    x3m::engine_patch::arena_cursor = 0;
}
bool row_has(const char* row, const char* text) {
    return std::strstr(row, text) != nullptr;
}
// After a successful install: recorders at every continuation, the fake at the named dispatcher's entry.
void arm_recorders() {
    for (const auto& s : S::sites) jump(base + s.rva + s.length, reinterpret_cast<const void*>(&ct_record));
    for (const auto& s : S::restore_sites) jump(base + s.rva + s.length, reinterpret_cast<const void*>(&ct_record));
    jump(base + S::script_call_rva, reinterpret_cast<const void*>(&fake_script_call));
}
bool install(bool restore_wanted, const char* retries = nullptr) {
    reset_module();
    lay_out();
    build_world();
    SetEnvironmentVariableW(L"X3M_CHASE_VIEW_RESTORE", restore_wanted ? L"1" : L"0");
    wchar_t value[8]{};
    for (unsigned i = 0; retries && retries[i] && i < 7; ++i) value[i] = wchar_t(retries[i]);
    SetEnvironmentVariableW(L"X3M_CHASE_VIEW_RESTORE_RETRIES", retries ? value : nullptr);
    return T::initialize();
}

// ---------------- CPU contract over every thunk ----------------
const SpanInput spans[] = {
    {0, false, 0, 0, {-4}, 1},       // construct: push ebx
    {1, false, 0x40, 0x80, {}, 0},   // complete: pop esi; mov esp,ebp; pop ebp; mov esp,ebx
    {2, false, 0, 0, {-4}, 1},       // destroy wrapper: push ebp
    {3, false, 0, 0, {-4}, 1},       // update begin: push ebp
    {4, false, 0x100, 0, {}, 0},     // internal exit: pops; mov esp,ebp
    {5, false, 0x100, 0, {}, 0},     // exit
    {6, false, 0, 0, {}, 0},         // script mode store (EAX = cockpit C)
    {7, false, 0, 0, {}, 0},         // load mode store (EDI = cockpit C)
    {0, true, 0, 0, {-4}, 1},        // inject: push ebp
    {1, true, 0, 0, {-0x10, -0x14}, 2}, // VM clear: sub esp,0xc; push ebx; push ebp
    {2, true, 0, 0, {-4, -8}, 2},    // VM load: push ebp; mov ebp,esp; push -1
    {3, true, 0, 0, {}, 0},          // EH adapter: mov eax,imm32
};
unsigned contract_runs = 0;
void cpu_contract(const char* phase) {
    static void* refs[12]{};
    for (unsigned i = 0; i < 12; ++i)
        if (!refs[i]) refs[i] = reference_span(row(spans[i]));
    for (unsigned i = 0; i < 12; ++i) {
        const SpanInput& s = spans[i];
        for (unsigned skew = 0; skew < 16; skew += 4)
            for (int f = 0; f < 2; ++f) {
                Cpu in = prepare(s, hostile(skew, f == 0));
                if (s.kind == 6 && !s.restore) in.eax = uint32_t(W(w_cockpit_c)), in.ecx = 7, in.ebx = uint32_t(W(w_frame_bad));
                if (s.kind == 7 && !s.restore) in.edi = uint32_t(W(w_cockpit_c)), in.edx = 9;
                if (s.kind == 0 && s.restore) in.ecx = uint32_t(W(w_ship2)); // not the bound ship: no call
                const StackWords before = stack_above(in.esp);
                put32(W(w_cockpit_c) + 0x150, 0);
                const Cpu hooked = enter(uint32_t(base + row(s).rva), in);
                const StackWords after_hooked = stack_above(in.esp);
                uint32_t pushed_hooked[2]{}, cockpit_hooked = get32(W(w_cockpit_c) + 0x150);
                for (unsigned k = 0; k < s.npushed; ++k) pushed_hooked[k] = get32(in.esp + s.pushed[k]);
                std::memcpy(reinterpret_cast<void*>(in.esp), before.words, sizeof before.words);
                put32(W(w_cockpit_c) + 0x150, 0);
                const Cpu ref = enter(addr(refs[i]), in);
                const StackWords after_ref = stack_above(in.esp);
                uint32_t pushed_ref[2]{}, cockpit_ref = get32(W(w_cockpit_c) + 0x150);
                for (unsigned k = 0; k < s.npushed; ++k) pushed_ref[k] = get32(in.esp + s.pushed[k]);
                char why[32], label[96], words[96];
                std::snprintf(label, sizeof label, "%s cpu %s skew=%u flags=%d", phase, row(s).name, skew, f == 0);
                check(same_cpu(hooked, ref, why, sizeof why), label, why);
                unsigned differ = 0x100;
                for (unsigned k = 0; k < 0x100 && differ == 0x100; ++k)
                    if (after_hooked.words[k] != after_ref.words[k]) differ = k;
                std::snprintf(words, sizeof words, "stack word +0x%x %08lx/%08lx pushed %08lx/%08lx cockpit %lu/%lu",
                              differ * 4, differ < 0x100 ? (unsigned long)after_hooked.words[differ] : 0ul,
                              differ < 0x100 ? (unsigned long)after_ref.words[differ] : 0ul,
                              (unsigned long)pushed_hooked[0], (unsigned long)pushed_ref[0],
                              (unsigned long)cockpit_hooked, (unsigned long)cockpit_ref);
                check(differ == 0x100 && !std::memcmp(pushed_hooked, pushed_ref, sizeof pushed_ref) &&
                          cockpit_hooked == cockpit_ref,
                      label, words);
                check(hooked.last_error == 0x1234abcd && hooked.mxcsr == 0xffc0 && hooked.fpucw == 0x0e7f &&
                          (hooked.flags & 0x400) == (f == 0 ? 0x400u : 0u),
                      label, "sentinels");
                ++contract_runs;
            }
    }
}

// ---------------- semantic drives through the thunks ----------------
uint32_t ret_va(uint32_t preferred_va) {
    return uint32_t(base + preferred_va - S::preferred_base);
}
void call_site(uint32_t rva, Cpu in) {
    enter(uint32_t(base + rva), in);
}
Cpu at_stack(uint32_t ret, uint32_t arg = 0) {
    Cpu c = hostile(8, true);
    put32(c.esp, ret);
    put32(c.esp + 4, arg);
    return c;
}
void construct(unsigned off, uint32_t caller_va) {
    Cpu c = at_stack(ret_va(caller_va));
    c.ecx = uint32_t(W(off));
    call_site(S::sites[0].rva, c);
    Cpu d = hostile(8, false);
    d.eax = uint32_t(W(off));
    d.ebx = d.esp + 0x80;
    d.ebp = d.esp + 0x40;
    call_site(S::sites[1].rva, d);
}
void destroy(unsigned off, uint32_t caller_va) {
    Cpu c = at_stack(ret_va(caller_va), 1);
    c.ecx = uint32_t(W(off));
    call_site(S::sites[2].rva, c);
}
// One cockpit update: begin, the chase hook's observation of the frame, end.
void update(unsigned off) {
    Cpu c = at_stack(0x401234);
    c.ecx = uint32_t(W(off));
    const uint32_t entry = c.esp;
    call_site(S::sites[3].rva, c);
    T::AppliedFrame f;
    f.cockpit = W(off);
    f.mode = get32(W(off) + 0x150);
    f.connect = get32(W(off) + 0x1c0);
    f.flags = get32(W(off) + 0x1a0);
    f.ref = get32(W(off) + 0x0c);
    f.view = get32(W(off) + 0x10);
    f.sector = get32(W(off) + 0x1fc);
    f.sector54 = get32(W(off) + 0x54);
    T::restore_observe_frame(f);
    Cpu e = hostile(8, false);
    e.ebp = entry - 4; // the updater's EBP: entry ESP = EBP + 4
    put32(e.esp, 0), put32(e.esp + 4, 0), put32(e.esp + 8, 0);
    call_site(S::sites[5].rva, e);
}
void mode_store(unsigned off, uint32_t mode, bool frame_ok = true) {
    Cpu c = hostile(4, true);
    c.eax = uint32_t(W(off));
    c.ecx = mode;
    c.ebx = uint32_t(W(frame_ok ? w_frame : w_frame_bad));
    call_site(S::sites[6].rva, c);
}
void load_store(unsigned off, uint32_t mode) {
    Cpu c = hostile(4, true);
    c.edi = uint32_t(W(off));
    c.edx = mode;
    call_site(S::sites[7].rva, c);
}
void inject(unsigned ship_off) {
    Cpu c = at_stack(0x401111, 1);
    c.ecx = uint32_t(W(ship_off));
    const Cpu in = c;
    const Cpu out = enter(uint32_t(base + S::restore_sites[0].rva), c);
    check(out.ecx == in.ecx && out.esp == ((in.esp - 4) & ~15u) && out.ebp == in.esp - 4 &&
              out.last_error == in.last_error && out.mxcsr == in.mxcsr && out.fpucw == in.fpucw &&
              !std::memcmp(out.xmm, in.xmm, 128) && out.ebx == in.ebx && out.esi == in.esi && out.edi == in.edi,
          "inject continuation keeps the engine's CPU image");
}
void vm_site(unsigned kind) {
    Cpu c = at_stack(0x402222, 0);
    call_site(S::restore_sites[kind].rva, c);
}
const char* field_of(const char* row, const char* key, char* out, unsigned n) {
    const char* p = std::strstr(row, key);
    out[0] = 0;
    if (!p) return out;
    p += std::strlen(key);
    unsigned i = 0;
    while (p[i] && p[i] != ' ' && i + 1 < n) out[i] = p[i], ++i;
    out[i] = 0;
    return out;
}
// Gate transit / dock: the rear frame arms on cockpit A, the free command destroys it, the alloc command builds B
// whose monitor stores mode 1, two eligible updates make it due, the input-worker entry consumes it.
void transit(bool manager, unsigned ship_off = w_ship) {
    construct(w_cockpit_a, S::alloc_return_va);
    cockpit_fields(w_cockpit_a, 258, uint32_t(W(w_ship)), uint32_t(W(w_ship)), 0x11);
    set_active(W(w_cockpit_a));
    update(w_cockpit_a);
    update(w_cockpit_a);
    destroy(w_cockpit_a, S::free_return_va);
    construct(w_cockpit_b, S::alloc_return_va);
    cockpit_fields(w_cockpit_b, 0, uint32_t(W(ship_off)), uint32_t(W(ship_off)), 0x22);
    set_active(W(w_cockpit_b));
    build_task(manager);
    mode_store(w_cockpit_b, 1);
    put32(W(w_cockpit_b) + 0x150, 1);
}
void scenarios() {
    char v[64];
    // 1. Gate transit: restored, then UpdateVisibility on the captured manager.
    check(install(true), "install with the restore", last_row);
    arm_recorders();
    fake_calls = 0;
    outcome_rows = 0;
    fake_cockpit = uint32_t(W(w_cockpit_b));
    fake_store = 258;
    transit(true);
    check(T::restore.phase == D::phase_pending && T::restore.monitor == monitor_id &&
              T::restore.manager == manager_id && T::restore.monitor_class == monitor_class,
          "gate: pending with the monitor and manager captured");
    update(w_cockpit_b);
    update(w_cockpit_b);
    check(T::restore.phase == D::phase_due && T::restore_inject_filter.load() == 1, "gate: due after two updates");
    inject(w_ship2); // another controlled object: skipped, stays due
    check(fake_calls == 0 && T::restore.phase == D::phase_due && T::restore.injections_skipped == 1,
          "gate: a foreign controlled object is skipped");
    put32(W(w_game) + S::game_pause_bits, 1);
    inject(w_ship);
    check(fake_calls == 0 && T::restore.injections_skipped == 2, "gate: pause bit 0 skips the injection");
    put32(W(w_game) + S::game_pause_bits, 0);
    inject(w_ship);
    check(fake_calls == 2 && fake_nested_ok, "gate: SelectMode then UpdateVisibility");
    const FakeCall& a = fake_log[0];
    const FakeCall& b = fake_log[1];
    check(a.callback == 32 && a.token == 0 && a.key == monitor_id && !std::strcmp(a.name, "SelectMode") &&
              a.kind == 1 && a.argc == 1 && a.value == 258,
          "gate: SelectMode(258) on the monitor with selector 32");
    check(b.callback == 32 && b.token == 0 && b.key == manager_id && !std::strcmp(b.name, "UpdateVisibility") &&
              b.kind == 1 && b.argc == 0,
          "gate: UpdateVisibility() on the manager with selector 32");
    check(fake_entry_mxcsr == 0xffc0 && fake_entry_fpucw == 0x0e7f && fake_entry_last_error == 0x1234abcd,
          "gate: the engine call runs under the engine's incoming FP/LastError image");
    check(outcome_rows == 1 && !std::strcmp(field_of(outcome_row, "result=", v, sizeof v), "restored") &&
              !std::strcmp(field_of(outcome_row, "hud_refresh=", v, sizeof v), "ok") &&
              !std::strcmp(field_of(outcome_row, "mode_seen=", v, sizeof v), "258") && T::restore.phase == D::phase_idle,
          "gate: outcome restored, hud_refresh=ok", outcome_row);
    check(rows_under_lock == 0, "no row is logged under the module lock");
    std::printf("SCENARIO gate %s\n", outcome_row);

    // 2. Docking: the same cockpit replacement at the dock reset; no manager frame (bottom) -> hud_refresh=skipped.
    install(true);
    arm_recorders();
    fake_calls = outcome_rows = 0;
    transit(false);
    update(w_cockpit_b);
    update(w_cockpit_b);
    inject(w_ship);
    check(fake_calls == 1 && outcome_rows == 1 && !std::strcmp(field_of(outcome_row, "result=", v, sizeof v), "restored") &&
              !std::strcmp(field_of(outcome_row, "hud_refresh=", v, sizeof v), "skipped") &&
              !std::strcmp(field_of(outcome_row, "manager_scan=", v, sizeof v), "bottom"),
          "dock: restored, no manager: hud_refresh=skipped", outcome_row);
    std::printf("SCENARIO dock %s\n", outcome_row);

    // 3. Undock: no cockpit recreation and no mode store: the armed ticket stays armed, no call, no row.
    install(true);
    arm_recorders();
    fake_calls = outcome_rows = 0;
    construct(w_cockpit_a, S::alloc_return_va);
    cockpit_fields(w_cockpit_a, 258, uint32_t(W(w_ship)), uint32_t(W(w_ship)), 0x11);
    set_active(W(w_cockpit_a));
    for (int i = 0; i < 80; ++i) update(w_cockpit_a);
    inject(w_ship);
    check(T::restore.phase == D::phase_armed && fake_calls == 0 && outcome_rows == 0 && T::restore.arms == 1,
          "undock: armed, nothing called, no row");

    // 4. Jump drive (ship change, run78 shape): B's view object stays 0 for 36 updates, due only after it binds.
    install(true);
    arm_recorders();
    fake_calls = outcome_rows = 0;
    fake_cockpit = uint32_t(W(w_cockpit_b));
    transit(true, w_ship2);
    put32(W(w_cockpit_b) + 0x10, 0);
    for (int i = 0; i < 36; ++i) update(w_cockpit_b);
    check(T::restore.phase == D::phase_pending && T::restore.soft_counts[D::soft_connect_flags_view] == 36,
          "jump: an unbound fresh cockpit waits (soft refusal)");
    put32(W(w_cockpit_b) + 0x10, uint32_t(W(w_ship2)));
    update(w_cockpit_b);
    update(w_cockpit_b);
    inject(w_ship2);
    check(outcome_rows == 1 && !std::strcmp(field_of(outcome_row, "result=", v, sizeof v), "restored") &&
              !std::strcmp(field_of(outcome_row, "ship_id_to=", v, sizeof v), "0x00c0ffef"),
          "jump: restored on the new ship", outcome_row);
    std::printf("SCENARIO jump %s\n", outcome_row);

    // 5. Retries exhausted: SelectMode accepted (AL 1) but no store: no_effect, spaced >= 60 updates, then retries.
    install(true, "2");
    arm_recorders();
    fake_calls = outcome_rows = 0;
    fake_store = 0;
    transit(true);
    unsigned updates = 0;
    while (outcome_rows == 0 && updates < 2000) {
        update(w_cockpit_b);
        ++updates;
        if (T::restore.phase == D::phase_due) inject(w_ship);
    }
    check(outcome_rows == 1 && fake_calls == 3 && !std::strcmp(field_of(outcome_row, "result=", v, sizeof v), "no_effect") &&
              !std::strcmp(field_of(outcome_row, "cancel=", v, sizeof v), "retries") &&
              !std::strcmp(field_of(outcome_row, "attempts=", v, sizeof v), "3") && updates > 120,
          "retries: three attempts (1 + retries 2), spaced, then cancel=retries", outcome_row);
    std::printf("SCENARIO retries updates=%u %s\n", updates, outcome_row);
    fake_store = 258;

    // 6. Refused call (receiver or method missing: AL 0): call_refused, no refresh.
    install(true);
    arm_recorders();
    fake_calls = outcome_rows = 0;
    fake_select_al = 0;
    transit(true);
    update(w_cockpit_b);
    update(w_cockpit_b);
    inject(w_ship);
    check(fake_calls == 1 && outcome_rows == 1 &&
              !std::strcmp(field_of(outcome_row, "result=", v, sizeof v), "call_refused"),
          "refused: AL 0 is call_refused", outcome_row);
    fake_select_al = 1;

    // 7. Cancels through the thunks: the load store (kind 7), the VM clear and the EH adapter epoch, a load-chain
    // constructor, a mode store outside the interpreter frame captures nothing (no monitor -> cancel at injection).
    const struct {
        const char* label;
        int how;
        const char* cancel;
    } cancels[] = {{"load store", 0, "load"},       {"VM clear", 1, "epoch"},        {"VM load", 2, "epoch"},
                   {"EH adapter", 3, "epoch"},      {"load constructor", 4, "lifetime"}, {"foreign frame", 5, "no_monitor"}};
    for (const auto& k : cancels) {
        install(true);
        arm_recorders();
        fake_calls = outcome_rows = 0;
        if (k.how == 5) {
            construct(w_cockpit_a, S::alloc_return_va);
            cockpit_fields(w_cockpit_a, 258, uint32_t(W(w_ship)), uint32_t(W(w_ship)), 0x11);
            set_active(W(w_cockpit_a));
            update(w_cockpit_a);
            destroy(w_cockpit_a, S::free_return_va);
            construct(w_cockpit_b, S::alloc_return_va);
            cockpit_fields(w_cockpit_b, 1, uint32_t(W(w_ship)), uint32_t(W(w_ship)), 0x22);
            set_active(W(w_cockpit_b));
            build_task(true);
            mode_store(w_cockpit_b, 1, false);
        } else
            transit(true);
        if (k.how == 0) load_store(w_cockpit_c, 1);
        if (k.how >= 1 && k.how <= 3) vm_site(unsigned(k.how));
        if (k.how == 4) construct(w_cockpit_c, S::load_return_va);
        update(w_cockpit_b);
        update(w_cockpit_b);
        inject(w_ship);
        char label[64];
        std::snprintf(label, sizeof label, "cancel: %s -> %s", k.label, k.cancel);
        check(fake_calls == 0 && outcome_rows == 1 && !std::strcmp(field_of(outcome_row, "cancel=", v, sizeof v), k.cancel),
              label, outcome_row);
    }
    check(T::restore_eh_bumps.load() == 0, "the last scenario did not pass the EH adapter");

    // 8. Lifetime witnesses: generations, the update token (begin ECX, exit EBP+4) and normalized callers.
    install(true);
    arm_recorders();
    construct(w_cockpit_a, S::alloc_return_va);
    const auto* life = T::state.find(W(w_cockpit_a));
    check(life && life->complete && life->constructor_caller == S::alloc_return_va && T::generation(W(w_cockpit_a)),
          "lifetime: complete generation, caller normalized to 0x4305ab");
    {
        Cpu c = at_stack(0x401234);
        c.ecx = uint32_t(W(w_cockpit_a));
        const uint32_t entry = c.esp;
        call_site(S::sites[3].rva, c);
        const T::Update u = T::current_update(W(w_cockpit_a));
        check(u.generation == T::generation(W(w_cockpit_a)) && u.serial && u.thread == GetCurrentThreadId(),
              "lifetime: the update token is live between begin and exit");
        Cpu e = hostile(4, true);
        e.ebp = entry - 4;
        call_site(S::sites[4].rva, e); // the internal exit ends it too
        check(!T::current_update(W(w_cockpit_a)).generation, "lifetime: the internal exit revokes the token");
    }
    set_active(W(w_cockpit_a));
    check(T::active_control_cockpit(W(w_cockpit_a)) && !T::active_control_cockpit(W(w_cockpit_b)),
          "lifetime: active-control proof through the FL cockpit registry");
    destroy(w_cockpit_a, S::free_return_va);
    check(!T::generation(W(w_cockpit_a)) && !T::state.find(W(w_cockpit_a)), "lifetime: the wrapper destroys it");
    check(x3m::chase_lead::invalidations > 0, "lifetime: the lead timing revocations still run");
}

// ---------------- install refusals and rollback ----------------
void refusals() {
    // A changed byte in any window refuses before any write.
    unsigned flips = 0, refused = 0;
    quiet = true;
    for (unsigned w = 0; w < S::window_count; ++w)
        for (unsigned bit : {0u, 13u}) {
            const unsigned at = (bit * 7) % S::windows[w].length;
            reset_module();
            lay_out();
            image[S::windows[w].rva + at] ^= 0x01;
            const bool ok = T::initialize();
            ++flips;
            if (!ok && !T::installed() && row_has(transition_row, "installed=0") &&
                (row_has(transition_row, "status=window_mismatch") || row_has(transition_row, "status=fixup_mismatch")))
                ++refused;
            image[S::windows[w].rva + at] ^= 0x01;
            check(all_original(), "window flip leaves the spans", transition_row);
        }
    quiet = false;
    std::printf("FLIPS windows=%u refused=%u\n", flips, refused);
    check(flips == refused && flips == 2 * S::window_count, "every window flip refuses");
    // The relocated EH immediate: a stale (unrelocated) value is a fixup mismatch.
    reset_module();
    lay_out();
    put32(base + S::restore_sites[3].rva + 1, 0x59e520);
    check(!T::initialize() && row_has(transition_row, "status=fixup_mismatch") &&
              row_has(transition_row, "window=eh_adapter"),
          "an unrelocated EH immediate refuses", transition_row);
    // No admitted executable.
    reset_module();
    lay_out();
    T::fixture_base_ = 0;
    check(!T::initialize() && row_has(transition_row, "status=executable_mismatch") && all_original(),
          "no FL base: executable_mismatch", transition_row);
    // Read-back faults: mandatory group (the third claim), observers (the seventh), restore group (the tenth).
    const struct {
        unsigned claim;
        bool mandatory;
        const char* label;
        const char* status; // the chase_view_restore row's status
    } faults[] = {{2, false, "mandatory fault", "status=claim_failed "},
                  {6, true, "observer fault", "status=observers_failed "},
                  {9, true, "restore-site fault", "status=readback_restored "}};
    for (const auto& f : faults) {
        reset_module();
        lay_out();
        build_world();
        SetEnvironmentVariableW(L"X3M_CHASE_VIEW_RESTORE", L"1");
        T::fixture_fault_claim_ = f.claim;
        const DWORD sentinel = 0x2bad0002;
        SetLastError(sentinel);
        const bool ok = T::initialize();
        bool mandatory_patched = true, mandatory_original = true, others_original = true;
        for (unsigned i = 0; i < S::mandatory; ++i) {
            mandatory_patched &= T::sites[i].patched_in && image[S::sites[i].rva] == 0xe9;
            mandatory_original &= span_original(S::sites[i]) && !T::sites[i].patched_in;
        }
        for (unsigned i = S::mandatory; i < S::site_count; ++i)
            others_original &= span_original(S::sites[i]) && !T::sites[i].patched_in;
        for (const auto& s : S::restore_sites) others_original &= span_original(s);
        for (const auto& s : T::restore_sites) others_original &= !s.patched_in;
        char label[64];
        std::snprintf(label, sizeof label, "rollback %s", f.label);
        check(ok == f.mandatory && GetLastError() == sentinel && !T::restore_installed() && others_original &&
                  (f.mandatory ? mandatory_patched : mandatory_original) && row_has(restore_row, f.status) &&
                  row_has(restore_row, "installed=0 ") && row_has(transition_row, "observers=0 "),
              label, restore_row);
    }
    // Restore requested off: mandatory only, the observers and restore sites untouched.
    install(false);
    bool rest = true;
    for (unsigned i = S::mandatory; i < S::site_count; ++i) rest &= span_original(S::sites[i]);
    for (const auto& s : S::restore_sites) rest &= span_original(s);
    check(T::installed() && !T::restore_installed() && rest, "restore off: mandatory witnesses only");
    // Invalid retries refuse the restore (fail closed), mandatory stays.
    install(true, "9");
    check(T::installed() && !T::restore_installed(), "retries=9 refuses the restore", last_row);
}

int main() {
    // The synthetic engine stack lives in this frame (the thread's own stack, above every callee frame).
    volatile unsigned char frame_area[0x10000];
    frame_area[0] = 0;
    image = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, S::image_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    world = static_cast<unsigned char*>(VirtualAlloc(nullptr, w_world_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    stack_mem = const_cast<unsigned char*>(frame_area);
    reference = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    base = reinterpret_cast<std::uintptr_t>(image);
    if (!image || !world || !stack_mem || !reference || (base & 0xffff)) {
        std::printf("RESULT FAIL setup\n");
        return 2;
    }
    std::printf("WINDOWS count=%u base_aligned=%u\n", unsigned(S::window_count), unsigned((base & 0xffff) == 0));
    // Install and the CPU contract with every module path live (no ticket), then with a due ticket at the inject site.
    check(install(true), "install", last_row);
    check(row_has(last_row, "chase_view_restore_site index=3 ") && row_has(last_row, "status=active edition=fl"),
          "restore site rows", last_row);
    unsigned atomic = 0;
    for (const auto& s : T::sites) atomic += s.atomic_write;
    for (const auto& s : T::restore_sites) atomic += s.atomic_write;
    std::printf("ATOMIC cmpxchg8b=%u plain=%u\n", atomic, 12 - atomic);
    std::printf("ARENA used=%u capacity=%u\n", x3m::engine_patch::arena_used(), x3m::engine_patch::arena_capacity());
    check(atomic == 9, "nine spans by lock cmpxchg8b, three plain (qword crossings)");
    arm_recorders();
    build_world();
    cpu_contract("idle");
    // A due ticket: the inject thunk now calls the (fake) engine dispatcher inside the span's CPU envelope.
    fake_cockpit = uint32_t(W(w_cockpit_b));
    transit(true);
    update(w_cockpit_b);
    update(w_cockpit_b);
    {
        static void* ref = reference_span(S::restore_sites[0]);
        for (unsigned skew = 0; skew < 16; skew += 4) {
            if (T::restore.phase != D::phase_due) {
                transit(true);
                update(w_cockpit_b);
                update(w_cockpit_b);
            }
            Cpu in = prepare(spans[8], hostile(skew, skew & 4));
            in.ecx = uint32_t(W(w_ship));
            const unsigned calls_before = fake_calls;
            const Cpu hooked = enter(uint32_t(base + S::restore_sites[0].rva), in);
            const uint32_t pushed_hooked = get32(in.esp - 4);
            const Cpu reference_out = enter(addr(ref), in);
            char why[32];
            check(fake_calls == calls_before + 2 && same_cpu(hooked, reference_out, why, sizeof why) &&
                      pushed_hooked == get32(in.esp - 4),
                  "inject with an engine call keeps the CPU envelope", why);
        }
    }
    scenarios();
    refusals();
    // Late window: after the first Present nothing is claimed.
    reset_module();
    lay_out();
    x3m::engine_patch::close_install_window("fixture_present");
    check(!T::initialize() && row_has(transition_row, "status=late_claim") &&
              row_has(transition_row, "window_closed_by=fixture_present") && all_original(),
          "late window refuses with nothing written", transition_row);
    std::printf("CONTRACT runs=%u\n", contract_runs);
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
