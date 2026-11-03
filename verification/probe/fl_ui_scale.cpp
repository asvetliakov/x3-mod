// Original synthetic fixture of the FL UI scale (src/engine/fl/ui_scale_fl.cpp, ui_scale_sites_fl.h;
// docs/reverse-engineering/fl/ui-scale.md, "Verified 2026-11-03"): no game code, no game bytes beyond the identity
// windows in ui_scale_sites_fl.h, no launch. Built with -DX3M_EDITION_FL=1 -DX3M_UI_SCALE_FL_FIXTURE by
// verification/analysis/test_fl_ui_scale.py (the module force-includes fl_ui_scale_shim.h for the read-back / chain /
// restore faults) and run natively (WSL interop).
//
// A 64 KB-aligned private image stands in for X3FL.exe at `base`: every window at base + RVA with its fixups relocated to
// base. The real span bytes run: the harness enters each site's window at a start RVA with chosen registers, stack,
// XMM1/XMM4/XMM7, x87 CW, MXCSR, DF and arithmetic flags, and a `push imm32; ret` trap at the stop RVA (placed only
// while vectors run, never during an install) returns to the harness, which records every register, EFLAGS, ESP, the
// twelve stack dwords on top, XMM1/XMM4/XMM7, the CW and MXCSR. Each vector runs on the vanilla image and on the
// patched one (s = 1.5, s = 2, and the identity cells after a Reset to 1080 lines) and the outputs are compared with
// the models of ui_scale_sites.h / ui_scale_sites_fl.h and with the vanilla registers.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../../src/engine/fl/ui_scale_fl.h"
#include "../../src/engine/fl/ui_scale_sites_fl.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/ui_scale_sites.h"
namespace ui = x3m::ui_scale;
namespace S = x3m::fl::ui_scale_fl;
namespace sites = x3m::ui_scale::sites;
namespace engine_patch = x3m::engine_patch;
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
bool row_has(const char* needle) {
    return std::strstr(last_row, needle) != nullptr;
}
// The seam (fl_ui_scale_shim.h): one-shot faults, otherwise the real calls.
std::uintptr_t fault_read_at = 0, fault_restore_at = 0, fault_push_at = 0;
unsigned fault_read_count = 0, faults_taken = 0;
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count) {
    const bool ok = read_code(address, out, count);
    if (ok && fault_read_at && address == fault_read_at && count == fault_read_count) {
        fault_read_at = 0;
        ++faults_taken;
        out[count - 1] ^= 0xff;
    }
    return ok;
}
bool fixture_restore(Site& site) {
    if (fault_restore_at && site.spec.address == fault_restore_at) { // as a refused VirtualProtect: still registered
        fault_restore_at = 0;
        ++faults_taken;
        site.status = "restore_protect_failed";
        return false;
    }
    return restore(site);
}
void* fixture_push_front(Site& site, void* stub) {
    if (fault_push_at && site.spec.address == fault_push_at) {
        fault_push_at = 0;
        ++faults_taken;
        return nullptr;
    }
    return push_front(site, stub);
}
}

extern "C" {
uint32_t in_eax = 0, in_ebx = 0, in_ecx = 0, in_edx = 0, in_esi = 0, in_edi = 0, in_ebp = 0, in_flags = 0xcd7;
uint32_t site_entry = 0, saved_esp = 0, esp_before = 0;
uint32_t out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, out_esp, out_flags, out_stack[12];
alignas(16) unsigned char in_xmm1[16], in_xmm4[16], in_xmm7[16], out_xmm1[16], out_xmm4[16], out_xmm7[16];
uint16_t in_fcw = 0x027f, out_fcw = 0, def_fcw = 0;
uint32_t in_mxcsr = 0x1fa0, out_mxcsr = 0, def_mxcsr = 0;
void fl_ui_call();
void fl_ui_return();
}
asm(".text\n\t"
    ".globl _fl_ui_call\n_fl_ui_call:\n\t"
    "pushal\n\tpushfl\n\tmovl %esp,_saved_esp\n\tfnstcw _def_fcw\n\tstmxcsr _def_mxcsr\n\t"
    "movdqu _in_xmm1,%xmm1\n\tmovdqu _in_xmm4,%xmm4\n\tmovdqu _in_xmm7,%xmm7\n\tfldcw _in_fcw\n\tldmxcsr _in_mxcsr\n\t"
    "subl $64,%esp\n\t"
    "movl _in_eax,%eax\n\tmovl _in_ebx,%ebx\n\tmovl _in_ecx,%ecx\n\tmovl _in_edx,%edx\n\tmovl _in_esi,%esi\n\t"
    "movl _in_edi,%edi\n\tmovl _in_ebp,%ebp\n\tmovl %esp,_esp_before\n\tpushl _in_flags\n\tpopfl\n\tjmp *_site_entry\n\t"
    ".globl _fl_ui_return\n_fl_ui_return:\n\t"
    "pushfl\n\tpopl _out_flags\n\tcld\n\tmovl %eax,_out_eax\n\tmovl %ebx,_out_ebx\n\tmovl %ecx,_out_ecx\n\t"
    "movl %edx,_out_edx\n\tmovl %esi,_out_esi\n\tmovl %edi,_out_edi\n\tmovl %ebp,_out_ebp\n\tmovl %esp,_out_esp\n\t"
    "movl (%esp),%eax\n\tmovl %eax,_out_stack\n\tmovl 4(%esp),%eax\n\tmovl %eax,_out_stack+4\n\t"
    "movl 8(%esp),%eax\n\tmovl %eax,_out_stack+8\n\tmovl 12(%esp),%eax\n\tmovl %eax,_out_stack+12\n\t"
    "movl 16(%esp),%eax\n\tmovl %eax,_out_stack+16\n\tmovl 20(%esp),%eax\n\tmovl %eax,_out_stack+20\n\t"
    "movl 24(%esp),%eax\n\tmovl %eax,_out_stack+24\n\tmovl 28(%esp),%eax\n\tmovl %eax,_out_stack+28\n\t"
    "movl 32(%esp),%eax\n\tmovl %eax,_out_stack+32\n\tmovl 36(%esp),%eax\n\tmovl %eax,_out_stack+36\n\t"
    "movl 40(%esp),%eax\n\tmovl %eax,_out_stack+40\n\tmovl 44(%esp),%eax\n\tmovl %eax,_out_stack+44\n\t"
    "movdqu %xmm1,_out_xmm1\n\tmovdqu %xmm4,_out_xmm4\n\tmovdqu %xmm7,_out_xmm7\n\tfnstcw _out_fcw\n\tstmxcsr _out_mxcsr\n\t"
    "fldcw _def_fcw\n\tldmxcsr _def_mxcsr\n\tmovl _saved_esp,%esp\n\tpopfl\n\tpopal\n\tret\n\t");

constexpr unsigned image_size = 0x220000;
constexpr uint32_t sentinel_error = 0x2a2a1357, flag_mask = 0xcd5; // CF PF AF ZF SF DF OF
std::uintptr_t base = 0;
unsigned char* image = nullptr;
unsigned char* pristine = nullptr;
// The stop RVAs (a `push imm32; ret` trap each, outside every claimed span).
constexpr uint32_t stops[] = {S::projection_epilogue_rva, S::width_call_rva, S::height_call_rva, S::direct_call_rva,
                              S::mapped_call_rva, S::cursor_end_rva, S::overlay_call_rva, S::aim_compare_rva,
                              S::label_next_rva, S::pick_call_rva};

uint32_t u32(const void* p) {
    return uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
// The engine code writes its data cells (the cursor and mapped-sum cells) inside the image: it stays read-write-execute
// (engine_patch's own VirtualProtect brackets restore that). The calls mark where the fixture itself edits bytes.
void writable(bool) {}
void put32(uint32_t rva, uint32_t v) {
    std::memcpy(image + rva, &v, 4);
}
uint32_t get32(uint32_t rva) {
    uint32_t v = 0;
    std::memcpy(&v, image + rva, 4);
    return v;
}
void lay_out() {
    writable(true);
    std::memset(image, 0xcc, image_size);
    for (const S::Window& w : S::windows) {
        std::memcpy(image + w.rva, w.bytes, w.length);
        for (unsigned k = 0; k < w.reloc_count; ++k) put32(w.rva + w.reloc_offset[k], uint32_t(base + w.reloc_target[k]));
    }
    writable(false);
    FlushInstructionCache(GetCurrentProcess(), image, image_size);
}
void traps(bool on) {
    writable(true);
    for (uint32_t rva : stops) {
        if (on) {
            image[rva] = 0x68;
            put32(rva + 1, u32(reinterpret_cast<void*>(&fl_ui_return)));
            image[rva + 5] = 0xc3;
        } else {
            std::memcpy(image + rva, pristine + rva, 6);
        }
    }
    writable(false);
    FlushInstructionCache(GetCurrentProcess(), image, image_size);
}
// Whether the image equals the pristine layout everywhere except the first five bytes of the claimed spans (when
// `claimed`) and the trap bytes (never present during this test).
bool image_is(bool claimed) {
    for (const S::Window& w : S::windows)
        for (unsigned i = 0; i < w.length; ++i) {
            const uint32_t at = w.rva + i;
            bool in_site = false;
            for (const S::Site& s : S::site_table) in_site = in_site || (at >= s.rva && at < s.rva + 5);
            if (claimed && in_site) continue;
            if (image[at] != pristine[at]) return false;
        }
    if (claimed)
        for (const S::Site& s : S::site_table)
            if (image[s.rva] != 0xe9) return false;
    return true;
}
struct Out {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp_delta, flags, stack[12];
    unsigned char xmm1[16], xmm4[16], xmm7[16];
    uint16_t fcw;
    uint32_t mxcsr;
};
struct In {
    uint32_t eax = 0x11111111, ebx = 0x22222222, ecx = 0x33333333, edx = 0x44444444, esi = 0x55555555, edi = 0x66666666,
             ebp = 0x77777777;
    double xmm1 = 0, xmm4 = 0;
};
Out run(uint32_t rva, const In& in) {
    in_eax = in.eax;
    in_ebx = in.ebx;
    in_ecx = in.ecx;
    in_edx = in.edx;
    in_esi = in.esi;
    in_edi = in.edi;
    in_ebp = in.ebp;
    std::memset(in_xmm1, 0x5a, 16);
    std::memset(in_xmm4, 0xa5, 16);
    std::memcpy(in_xmm1, &in.xmm1, 8);
    std::memcpy(in_xmm4, &in.xmm4, 8);
    for (unsigned i = 0; i < 16; ++i) in_xmm7[i] = static_cast<unsigned char>(0x70 + i);
    site_entry = uint32_t(base + rva);
    if (std::getenv("FL_UI_SCALE_TRACE")) std::printf("RUN %06lx\n", static_cast<unsigned long>(rva));
    fl_ui_call();
    Out o{};
    o.eax = out_eax;
    o.ebx = out_ebx;
    o.ecx = out_ecx;
    o.edx = out_edx;
    o.esi = out_esi;
    o.edi = out_edi;
    o.ebp = out_ebp;
    o.esp_delta = esp_before - out_esp;
    o.flags = out_flags;
    std::memcpy(o.stack, out_stack, sizeof o.stack);
    std::memcpy(o.xmm1, out_xmm1, 16);
    std::memcpy(o.xmm4, out_xmm4, 16);
    std::memcpy(o.xmm7, out_xmm7, 16);
    o.fcw = out_fcw;
    o.mxcsr = out_mxcsr;
    return o;
}

// ---- engine objects ----
alignas(16) float P[16];
alignas(16) unsigned char instance[0x200], display[16], record[0x20], context[0x420], owner[0x500];
alignas(16) uint32_t registry[8], table[2], buckets[4], row[3], cockpit[4], camera[16];
constexpr uint32_t handle = 0x1235, scene = 0x0badf00c;
void set_record(int32_t x, int32_t y) {
    std::memset(record, 0xee, sizeof record);
    std::memcpy(record + 6, &x, 4);
    std::memcpy(record + 0xb, &y, 4);
}
// X2_UpdateCursorSteering's record: +1 mode, +6 active, +0xb x, +0x10 y.
void set_cursor_record(int32_t x, int32_t y) {
    std::memset(record, 0xee, sizeof record);
    std::memcpy(record + 0xb, &x, 4);
    std::memcpy(record + 0x10, &y, 4);
}
// ---- the vectors: one call each on the current image; `s` = 0 means vanilla (no scaling expected) ----
const uint32_t anchor_flags[] = {0x200, 0x1200, 0x2200, 0x3200, 0x4200, 0x8200, 0xc200, 0x5200, 0xa200, 0xf200};
struct Vectors {
    double s;            // the scale the cells hold (1 = identity cells)
    bool vanilla;        // the image is unpatched
    const char* tag;
};
// A: P after the site; the camera in EBX.
void vector_projection(const Vectors& v, uint32_t camera_value, bool expect_excluded) {
    char label[96];
    for (uint32_t flags : anchor_flags) {
        float p[16];
        sites::engine_projection(p, 3413, 960, 37, -21, flags);
        p[14] = 0.75f;
        std::memcpy(P, p, sizeof P);
        P[1] = P[2] = P[3] = 9.0f;
        P[15] = 3.0f;
        std::memcpy(instance + S::flags_offset, &flags, 4);
        In in;
        in.esi = u32(P);
        in.edi = u32(instance);
        in.ebx = camera_value;
        in.xmm1 = 1.25;
        const unsigned before_scaled = ui::scaled_count(), before_excluded = ui::excluded_count();
        const Out o = run(S::site_table[S::Projection].rva, in);
        float expect[16];
        std::memcpy(expect, p, sizeof expect);
        if (!v.vanilla && !expect_excluded) sites::scale_projection(expect, flags, float(v.s));
        bool ok = true;
        for (unsigned i : {0u, 5u, 12u, 13u}) {
            if (v.vanilla || expect_excluded || v.s != 1.0) {
                ok = ok && P[i] == expect[i];
            } else { // identity cells: (P - a) * 1 + a, within one ulp of the engine's
                ok = ok && std::fabs(P[i] - p[i]) <= std::fabs(p[i]) * 1.2e-7f + 1e-30f;
            }
        }
        ok = ok && P[14] == 0.75f && P[15] == 1.0f && P[1] == 0.0f && P[2] == 0.0f && P[3] == 0.0f;
        std::snprintf(label, sizeof label, "%s projection flags=%04x", v.tag, flags);
        check(ok, label, "P");
        std::snprintf(label, sizeof label, "%s projection flags=%04x registers", v.tag, flags);
        // Everything but EAX (= 1 from the tail) is the caller's: GPRs, EFLAGS, XMM, CW, MXCSR.
        check(o.eax == 1 && o.ebx == in.ebx && o.ecx == in.ecx && o.edx == in.edx && o.esi == in.esi && o.edi == in.edi &&
                  o.ebp == in.ebp && o.esp_delta == 0 && (o.flags & flag_mask) == (in_flags & flag_mask) &&
                  !std::memcmp(o.xmm1, in_xmm1, 16) && !std::memcmp(o.xmm4, in_xmm4, 16) && !std::memcmp(o.xmm7, in_xmm7, 16) &&
                  o.fcw == in_fcw && o.mxcsr == in_mxcsr,
              label);
        if (!v.vanilla) {
            std::snprintf(label, sizeof label, "%s projection flags=%04x counters", v.tag, flags);
            check(expect_excluded ? ui::excluded_count() == before_excluded + 1 && ui::scaled_count() == before_scaled
                                  : ui::scaled_count() == before_scaled + 1 && ui::excluded_count() == before_excluded,
                  label);
        }
    }
}
void vectors(const Vectors& v) {
    char label[96];
    const uint32_t inv = v.vanilla ? sites::identity_inverse16 : sites::inverse16(v.s);
    const uint32_t fixed = v.vanilla ? sites::identity_fixed256 : sites::fixed256(v.s);
    // B, C
    for (int16_t w : {int16_t(1), int16_t(1080), int16_t(1440), int16_t(2560), int16_t(5120), int16_t(7680), int16_t(32767)}) {
        for (unsigned which = 0; which < 2; ++which) {
            std::memset(display, 0, sizeof display);
            std::memcpy(display + (which ? 6 : 4), &w, 2);
            In in;
            in.eax = u32(display);
            const Out o = run(S::site_table[which ? S::Height : S::Width].rva, in);
            const uint32_t expect = v.vanilla ? uint32_t(w) : sites::virtual_size(uint32_t(w), inv);
            std::snprintf(label, sizeof label, "%s size %s=%d", v.tag, which ? "height" : "width", w);
            check(o.esp_delta == 8 && o.stack[1] == expect && o.stack[0] == in.edi && o.eax == expect && o.ecx == in.ecx &&
                      o.edx == in.edx && o.edi == in.edi && o.ebx == in.ebx && o.esi == in.esi && o.ebp == in.ebp &&
                      !std::memcmp(o.xmm1, in_xmm1, 16) && o.mxcsr == in_mxcsr && (o.flags & 0x400),
                  label);
        }
    }
    // D1 (direct, int16 deltas) and D2 (mapped sums, any dword): the remainder carries across calls.
    {
        int32_t acc[4] = {sites::accumulator_start, sites::accumulator_start, sites::accumulator_start, sites::accumulator_start};
        const int32_t direct[][2] = {{1, 0}, {1, 1}, {1, -1}, {1, 1}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
                                     {-1, 0}, {-1, 0}, {7, -7}, {32767, -32768}, {-32768, 32767}, {0, 3}};
        for (const auto& d : direct) {
            int16_t dx = int16_t(d[0]), dy = int16_t(d[1]);
            std::memset(context, 0, sizeof context);
            std::memcpy(context + 0x414, &dx, 2);
            std::memcpy(context + 0x416, &dy, 2);
            std::memset(owner, 0, sizeof owner);
            const uint32_t klass = 150, ctx = 0x0c0c0c0c;
            std::memcpy(owner + 0x4dc, &klass, 4);
            std::memcpy(owner + 0x418, &ctx, 4);
            In in;
            in.edx = u32(context);
            in.ebx = u32(owner);
            const Out o = run(0x077fc6, in);
            const int32_t ex = v.vanilla ? d[0] : S::callback_step(d[0], inv, &acc[0]);
            const int32_t ey = v.vanilla ? d[1] : S::callback_step(d[1], inv, &acc[1]);
            std::snprintf(label, sizeof label, "%s delta_direct %d,%d", v.tag, d[0], d[1]);
            check(o.esp_delta == 32 && int32_t(o.stack[6]) == ex && int32_t(o.stack[7]) == ey && o.stack[5] == 2 &&
                      o.stack[4] == 1 && o.stack[3] == uint32_t(base + S::input_delta_name_rva) && o.stack[2] == klass &&
                      o.stack[1] == 0 && o.stack[0] == ctx && !std::memcmp(context + 0x414, &dx, 2) &&
                      !std::memcmp(context + 0x416, &dy, 2) && o.ecx == in.ecx && o.ebx == in.ebx && o.esi == in.esi &&
                      o.edi == in.edi && o.ebp == in.ebp && !std::memcmp(o.xmm1, in_xmm1, 16) && o.mxcsr == in_mxcsr &&
                      (o.flags & 0x400),
                  label);
        }
        const int32_t mapped[][2] = {{1, 1}, {1, 1}, {-1, -1}, {100000, -100000}, {-2000000, 2000000}, {3, 0}, {0, -3}};
        for (const auto& d : mapped) {
            put32(S::mapped_x_rva, uint32_t(d[0]));
            put32(S::mapped_y_rva, uint32_t(d[1]));
            In in;
            const Out o = run(0x0d22e8, in);
            const int32_t ex = v.vanilla ? d[0] : S::callback_step(d[0], inv, &acc[2]);
            const int32_t ey = v.vanilla ? d[1] : S::callback_step(d[1], inv, &acc[3]);
            std::snprintf(label, sizeof label, "%s delta_mapped %d,%d", v.tag, d[0], d[1]);
            check(o.esp_delta == 32 && int32_t(o.stack[6]) == ex && int32_t(o.stack[7]) == ey && o.stack[5] == 2 &&
                      o.stack[4] == 1 && o.stack[3] == uint32_t(base + S::input_delta_name_rva) && o.stack[2] == 0x96 &&
                      o.stack[1] == 0 && o.stack[0] == 0 && get32(S::mapped_x_rva) == uint32_t(d[0]) &&
                      get32(S::mapped_y_rva) == uint32_t(d[1]) && o.ecx == in.ecx && o.ebx == in.ebx && o.esi == in.esi &&
                      o.edi == in.edi && o.ebp == in.ebp && !std::memcmp(o.xmm1, in_xmm1, 16) && o.mxcsr == in_mxcsr &&
                      (o.flags & 0x400),
                  label);
        }
    }
    const int32_t points[][2] = {{0, 0}, {1, -1}, {2559, 1439}, {3413, 959}, {-5, 7}, {100000, -100000}, {1706, 480}};
    for (const auto& pt : points) {
        const int32_t ex = v.vanilla ? pt[0] : sites::cursor_real(pt[0], fixed);
        const int32_t ey = v.vanilla ? pt[1] : sites::cursor_real(pt[1], fixed);
        // E: both cells through EAX.
        {
            set_cursor_record(pt[0], pt[1]);
            In in;
            in.ecx = u32(record);
            const Out o = run(0x07bc4d, in);
            std::snprintf(label, sizeof label, "%s cursor %d,%d", v.tag, pt[0], pt[1]);
            check(int32_t(get32(S::cursor_x_rva)) == ex && int32_t(get32(S::cursor_y_rva)) == ey && int32_t(o.eax) == ey &&
                      o.ecx == in.ecx && o.edx == in.edx && o.edi == in.edi && o.esp_delta == 0 &&
                      o.ebx == in.ebx && o.esi == in.esi && o.ebp == in.ebp && !std::memcmp(o.xmm1, in_xmm1, 16) &&
                      o.mxcsr == in_mxcsr && (o.flags & 0x400),
                  label);
        }
        // G: y*s, x*s pushed, ECX = cockpit + 0x3bc, the record untouched.
        {
            set_record(pt[0], pt[1]);
            unsigned char copy[sizeof record];
            std::memcpy(copy, record, sizeof copy);
            In in;
            in.esi = u32(record);
            in.eax = 0x0c0c0000;
            const Out o = run(S::site_table[S::OverlayIcon].rva, in);
            std::snprintf(label, sizeof label, "%s overlay %d,%d", v.tag, pt[0], pt[1]);
            check(o.esp_delta == 8 && int32_t(o.stack[0]) == ex && int32_t(o.stack[1]) == ey && o.ecx == in.eax + S::overlay_offset &&
                      o.eax == in.eax && o.edx == in.edx && o.edi == in.edi && o.ebx == in.ebx && o.esi == in.esi &&
                      o.ebp == in.ebp && !std::memcmp(copy, record, sizeof copy) && !std::memcmp(o.xmm1, in_xmm1, 16) &&
                      o.mxcsr == in_mxcsr && (o.flags & 0x400),
                  label);
        }
        // H: EDX = x*s, EDI = y*s, ECX (the argument count) untouched.
        {
            set_record(pt[0], pt[1]);
            In in;
            in.esi = u32(record);
            in.ecx = 4;
            in.eax = 0x0c0c0000;
            const Out o = run(S::site_table[S::CursorAim].rva, in);
            std::snprintf(label, sizeof label, "%s aim %d,%d", v.tag, pt[0], pt[1]);
            check(o.esp_delta == 0 && int32_t(o.edx) == ex && int32_t(o.edi) == ey && o.ecx == 4 && o.eax == in.eax &&
                      o.ebx == in.ebx && o.esi == in.esi && o.ebp == in.ebp && !std::memcmp(o.xmm1, in_xmm1, 16) &&
                      o.mxcsr == in_mxcsr && (o.flags & 0x400),
                  label);
        }
        // J: y*s, x*s, the scene pushed; ECX and EAX kept.
        {
            set_record(pt[0], pt[1]);
            In in;
            in.esi = u32(record);
            in.eax = 0x5ce0e000;
            const Out o = run(S::site_table[S::MapPick].rva, in);
            std::snprintf(label, sizeof label, "%s pick %d,%d", v.tag, pt[0], pt[1]);
            check(o.esp_delta == 12 && o.stack[0] == in.eax && int32_t(o.stack[1]) == ex && int32_t(o.stack[2]) == ey &&
                      o.eax == in.eax && o.ecx == in.ecx && o.edx == in.edx && o.edi == in.edi && o.ebx == in.ebx &&
                      o.esi == in.esi && o.ebp == in.ebp && !std::memcmp(o.xmm1, in_xmm1, 16) && o.mxcsr == in_mxcsr &&
                      (o.flags & 0x400),
                  label);
        }
    }
    // I: the truncated ring offsets divided by s, a tie away from zero; XMM1/XMM4 untouched.
    const double offsets[] = {0.0, 0.4, -0.4, 1.5, -1.5, 3.0, -3.0, 123.9, -123.9, 2559.5, -2559.5, 1e6, -1e6};
    for (double y : offsets)
        for (double x : {0.0, 7.0, -7.0, 1706.9}) {
            In in;
            in.xmm1 = y;
            in.xmm4 = x;
            const Out o = run(S::site_table[S::MapLabel].rva, in);
            const int32_t ty = int32_t(y), tx = int32_t(x);
            const int32_t ey = v.vanilla ? ty : sites::map_offset(ty, inv), ex = v.vanilla ? tx : sites::map_offset(tx, inv);
            std::snprintf(label, sizeof label, "%s label %g,%g", v.tag, x, y);
            check(o.esp_delta == 8 && int32_t(o.stack[0]) == ex && int32_t(o.stack[1]) == ey && o.ecx == in.ecx &&
                      o.ebx == in.ebx && o.esi == in.esi && o.edi == in.edi && o.ebp == in.ebp &&
                      !std::memcmp(o.xmm1, in_xmm1, 16) && !std::memcmp(o.xmm4, in_xmm4, 16) &&
                      !std::memcmp(o.xmm7, in_xmm7, 16) && o.mxcsr == in_mxcsr && (o.flags & 0x400),
                  label);
        }
    vector_projection(v, 0x0cafe000, false);
}
// The fake registry the excluded-camera walk reads (base + 0x1a82e4 -> registry).
void lay_registry(bool attached) {
    std::memset(registry, 0, sizeof registry);
    registry[0] = u32(table);
    registry[4] = handle;
    table[0] = u32(buckets);
    table[1] = 4;
    std::memset(buckets, 0, sizeof buckets);
    buckets[handle & 3] = u32(row);
    row[0] = 0;
    row[1] = handle;
    row[2] = u32(cockpit);
    cockpit[1] = scene;
    cockpit[2] = u32(camera);
    std::memset(camera, 0, sizeof camera);
    camera[S::camera_scene_offset / 4] = attached ? scene : 0;
    writable(true);
    put32(S::registry_slot_rva, u32(registry));
    writable(false);
}
void clear_registry() {
    writable(true);
    put32(S::registry_slot_rva, 0);
    writable(false);
}
void set_env(const wchar_t* name, const wchar_t* value) {
    SetEnvironmentVariableW(name, value);
}
// One entry point with LastError set to a sentinel before; true when it is the same after.
template <class F> bool keeps_error(F f) {
    SetLastError(sentinel_error);
    f();
    return GetLastError() == sentinel_error;
}
bool install(const wchar_t* setting, unsigned width, unsigned height) {
    ui::fixture_reset();
    set_env(L"X3M_UI_SCALE", setting);
    check(keeps_error([] { ui::initialize(); }), "initialize LastError");
    bool ok = false;
    check(keeps_error([&] { ok = ui::device_created(width, height); }), "device_created LastError");
    return ok;
}

// `stubs`: every encoder's bytes with distinct operand sentinels, for the host test's Capstone decode.
void print_stub(const char* name, const unsigned char* b, unsigned n, unsigned length) {
    std::printf("STUB %s %u ", name, n == length ? 1u : 0u);
    for (unsigned i = 0; i < n; ++i) std::printf("%02x", b[i]);
    std::printf("\n");
}
int print_stubs() {
    unsigned char b[S::max_stub_length + 16];
    const uint32_t inv = 0x10101010, fixed = 0x20202020, acc_x = 0x30303030, acc_y = 0x30303034, slot = 0x40404040;
    print_stub("size4", b, S::encode_size_stub(4, inv, slot, b), S::size_stub_length);
    print_stub("size6", b, S::encode_size_stub(6, inv, slot, b), S::size_stub_length);
    print_stub("delta", b, S::encode_delta_stub(inv, acc_x, acc_y, slot, b), S::delta_stub_length);
    print_stub("cursor", b, S::encode_cursor_stub(fixed, slot, b), S::cursor_stub_length);
    print_stub("overlay", b, S::encode_overlay_stub(fixed, slot, b), S::overlay_stub_length);
    print_stub("aim", b, S::encode_aim_stub(fixed, slot, b), S::aim_stub_length);
    print_stub("map_label", b, S::encode_map_label_stub(inv, slot, b), S::map_label_stub_length);
    print_stub("map_pick", b, S::encode_map_pick_stub(fixed, slot, b), S::map_pick_stub_length);
    print_stub("projection", b,
               S::encode_projection_stub(0x50505050, 0x60606060, 0x70707070, 0x70707074, 0x80808080, 0x90909090, slot, b),
               S::projection_stub_length);
    return 0;
}
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // every row reaches the pipe even if a vector faults
    if (argc > 1 && !std::strcmp(argv[1], "stubs")) return print_stubs();
    const bool late = argc > 1 && !std::strcmp(argv[1], "late");
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    base = reinterpret_cast<std::uintptr_t>(image);
    check(image && !(base & 0xffff), "image 64 KB aligned");
    if (!image) return 2;
    lay_out();
    pristine = static_cast<unsigned char*>(std::malloc(image_size));
    std::memcpy(pristine, image, image_size);
    for (const S::Window& w : S::windows) check(S::fnv1a(w.bytes, w.length) == w.fnv, "window fnv", w.name);
    set_env(L"X3M_DEBUG", nullptr);
    ui::fixture_base(base);

    if (argc > 1 && !std::strcmp(argv[1], "timing")) {
        // Diagnostic cost (not game FPS): the projection site per 2D instance and one delta send, vanilla against
        // patched at s = 1.5, through the same harness; the difference is the stub.
        LARGE_INTEGER f{}, t0{}, t1{};
        QueryPerformanceFrequency(&f);
        const unsigned n = 200000;
        const uint32_t flags = 0x5200;
        std::memcpy(instance + S::flags_offset, &flags, 4);
        In proj;
        proj.esi = u32(P);
        proj.edi = u32(instance);
        proj.ebx = 0x0cafe000;
        std::memset(context, 0, sizeof context);
        context[0x414] = 3;
        In delta;
        delta.edx = u32(context);
        delta.ebx = u32(owner);
        double ns[2][2] = {};
        for (unsigned patched = 0; patched < 2; ++patched) {
            if (patched) check(install(L"auto", 5120, 1440), "timing install");
            traps(true);
            for (unsigned which = 0; which < 2; ++which) {
                QueryPerformanceCounter(&t0);
                for (unsigned k = 0; k < n; ++k) {
                    if (!which) sites::engine_projection(P, 3413, 960, 37, -21, flags);
                    run(which ? 0x077fc6 : S::site_table[S::Projection].rva, which ? delta : proj);
                }
                QueryPerformanceCounter(&t1);
                ns[patched][which] = double(t1.QuadPart - t0.QuadPart) * 1e9 / double(f.QuadPart) / n;
            }
            traps(false);
        }
        ui::shutdown();
        std::printf("TIMING calls=%u projection_vanilla_ns=%.1f projection_patched_ns=%.1f delta_vanilla_ns=%.1f "
                    "delta_patched_ns=%.1f\n",
                    n, ns[0][0], ns[1][0], ns[0][1], ns[1][1]);
        std::printf("RESULT checks=%u failures=%u\n", checks, failures);
        return failures ? 1 : 0;
    }
    if (late) {
        // The install window closes before the device: the claim is refused, nothing written.
        engine_patch::close_install_window("fixture");
        check(!install(L"auto", 5120, 1440), "late refused");
        check(row_has("status=refused reason=late_claim") && row_has("edition=fl"), "late row", last_row);
        check(!ui::patched() && image_is(false) && ui::scale() == 1.0, "late nothing written");
        std::printf("RESULT checks=%u failures=%u\n", checks, failures);
        return failures ? 1 : 0;
    }

    // ---- refusals at initialize (nothing claimed) ----
    struct Refusal {
        const wchar_t* setting;
        const char* row;
    };
    const Refusal refusals[] = {{nullptr, "status=off reason=off"},
                                {L"1", "status=off reason=off"},
                                {L"abc", "status=refused reason=invalid_setting"},
                                {L"3.5", "status=refused reason=out_of_range"},
                                {L"0.5", "status=refused reason=out_of_range"},
                                {L"1234567890123456789012345678901234", "status=refused reason=too_long"}};
    for (const Refusal& r : refusals) {
        ui::fixture_reset();
        set_env(L"X3M_UI_SCALE", r.setting);
        ui::initialize();
        check(row_has(r.row) && row_has("edition=fl"), "refusal row", last_row);
        check(!ui::device_created(5120, 1440) && image_is(false), "refusal claims nothing");
    }
    ui::fixture_reset();
    ui::fixture_base(0); // the fixture executable is not X3FL.exe: fl::module_base() is 0
    set_env(L"X3M_UI_SCALE", L"auto");
    ui::initialize();
    check(row_has("status=refused reason=executable_mismatch"), "executable mismatch", last_row);
    ui::fixture_base(base);
    // Every window: one flipped byte refuses before anything is claimed, naming the window.
    for (const S::Window& w : S::windows) {
        unsigned at = 0;
        while (true) { // a byte outside the relocated dwords
            bool in_reloc = false;
            for (unsigned r = 0; r < w.reloc_count; ++r) in_reloc = in_reloc || (at >= w.reloc_offset[r] && at < w.reloc_offset[r] + 4);
            if (!in_reloc || at + 1 >= w.length) break;
            ++at;
        }
        writable(true);
        image[w.rva + at] ^= 0x01;
        writable(false);
        ui::fixture_reset();
        ui::initialize();
        char want[64];
        std::snprintf(want, sizeof want, "window=%s ", w.name);
        bool in_reloc = false;
        for (unsigned r = 0; r < w.reloc_count; ++r) in_reloc = in_reloc || (at >= w.reloc_offset[r] && at < w.reloc_offset[r] + 4);
        check(row_has("status=refused") && row_has(want) && row_has(in_reloc ? "reason=slot_mismatch" : "reason=window_mismatch"),
              "window flip refused", last_row);
        writable(true);
        image[w.rva + at] ^= 0x01;
        writable(false);
    }
    // A relocated dword bound to another base.
    {
        const S::Window& w = S::windows[S::w_cursor];
        writable(true);
        put32(w.rva + w.reloc_offset[0], get32(w.rva + w.reloc_offset[0]) + 0x10000);
        writable(false);
        ui::fixture_reset();
        ui::initialize();
        check(row_has("reason=slot_mismatch") && row_has("window=cursor "), "fixup mismatch", last_row);
        writable(true);
        std::memcpy(image + w.rva, pristine + w.rva, w.length);
        writable(false);
    }
    check(image_is(false), "image pristine after the refusals");

    // ---- vanilla vectors ----
    traps(true);
    vectors({1.0, true, "vanilla"});
    traps(false);

    // ---- auto at 1440 lines: s = 1.5 ----
    set_env(L"X3M_DEBUG", L"1");
    check(install(L"auto", 5120, 1440), "install auto 1440");
    set_env(L"X3M_DEBUG", nullptr);
    check(row_has("status=patched reason=ok") && row_has("scale=1.5000") && row_has("virtual=3413x960") &&
              row_has("inverse16=43691 fixed256=384") &&
              row_has("writes=atomic,atomic,atomic,atomic,atomic,atomic,atomic,atomic,atomic,atomic,atomic") &&
              row_has("edition=fl"),
          "install row", last_row);
    check(ui::patched() && ui::scale() == 1.5 && !ui::diagnostic_patched() && image_is(true), "installed image");
    traps(true);
    vectors({1.5, false, "s1.5"});
    // The excluded camera: published by present() while attached to the cockpit's HUD scene.
    lay_registry(true);
    check(keeps_error([] { ui::present(1); }), "present LastError");
    check(ui::excluded_camera() == u32(camera) && row_has("ui_scale_camera frame=1") && row_has("edition=fl"), "camera published",
          last_row);
    vector_projection({1.5, false, "s1.5 excluded"}, u32(camera), true);
    vector_projection({1.5, false, "s1.5 other camera"}, u32(camera) + 0x40, false);
    camera[S::camera_scene_offset / 4] = 0; // detached (the teardown path): nothing excluded
    ui::present(2);
    check(ui::excluded_camera() == 0 && row_has("excluded_camera=00000000"), "detached camera not excluded", last_row);
    lay_registry(true);
    row[2] = 0; // the active handle's row maps to no cockpit
    ui::present(3);
    check(ui::excluded_camera() == 0, "no cockpit for the handle");
    clear_registry();
    ui::present(3);
    check(ui::excluded_camera() == 0, "no registry");
    {
        const unsigned scaled = ui::scaled_count();
        ui::present(300);
        char want[48];
        std::snprintf(want, sizeof want, "scaled=%u ", scaled);
        check(row_has("ui_scale_frame frame=300") && row_has(want) && row_has("cameras=- edition=fl") && ui::scaled_count() == 0,
              "debug frame row", last_row);
    }
    // A Reset to 1080 lines under auto: the identity cells, every stub reproduces the engine.
    check(keeps_error([] { ui::after_reset(1920, 1080); }), "after_reset LastError");
    check(row_has("ui_scale_reset width=1920 height=1080 scale=1.0000") && row_has("inverse16=65536 fixed256=256"), "reset row",
          last_row);
    vectors({1.0, false, "identity"});
    ui::after_reset(3840, 2160);
    check(row_has("scale=2.0000") && row_has("virtual=1920x1080"), "reset 2160 row", last_row);
    vectors({2.0, false, "s2"});
    traps(false);
    check(keeps_error([] { ui::shutdown(); }), "shutdown LastError");
    check(row_has("ui_scale_restore status=restored registered=0") && !ui::patched() && image_is(false), "shutdown restores",
          last_row);

    // ---- a fixed scale ignores the height; auto at 1080 lines patches nothing ----
    check(install(L"1.25", 1920, 1080), "install fixed 1.25");
    check(row_has("status=patched") && row_has("mode=fixed") && row_has("scale=1.2500") && row_has("fixed256=320"), "fixed row",
          last_row);
    traps(true);
    vectors({1.25, false, "s1.25"});
    traps(false);
    ui::shutdown();
    check(!install(L"auto", 2560, 1080), "auto 1080 off");
    check(row_has("status=off reason=off_auto") && image_is(false), "off_auto row", last_row);

    // ---- all or none: a fault at any step rolls back every earlier claim ----
    struct Fault {
        unsigned site;
        int kind; // 0 read-back, 1 chain
        const char* reason;
    };
    const Fault faults[] = {{S::Width, 1, "reason=chain_failed failed_site=ui_scale_width_fl"},
                            {S::CursorY, 0, "reason=readback_failed failed_site=ui_scale_cursor_y_fl"},
                            {S::MapPick, 0, "reason=readback_failed failed_site=ui_scale_map_pick_fl"},
                            {S::Projection, 1, "reason=chain_failed failed_site=ui_scale_projection_fl"}};
    for (const Fault& f : faults) {
        const S::Site& s = S::site_table[f.site];
        faults_taken = 0;
        if (f.kind == 0) {
            fault_read_at = base + s.rva;
            fault_read_count = s.length;
        } else {
            fault_push_at = base + s.rva;
        }
        check(!install(L"auto", 5120, 1440), "fault refused");
        check(faults_taken == 1 && row_has("status=refused") && row_has(f.reason) && !ui::patched() && image_is(false) &&
                  ui::scale() == 1.0,
              "fault rolled back", last_row);
    }
    // A rollback whose restore fails keeps that site registered at the identity cells (patched_unverified); shutdown
    // takes it back later.
    faults_taken = 0;
    fault_push_at = base + S::site_table[S::Projection].rva;
    fault_restore_at = base + S::site_table[S::CursorX].rva;
    check(!install(L"auto", 5120, 1440), "fault with failed restore");
    check(faults_taken == 2 && row_has("status=patched_unverified") && ui::patched() && image[S::site_table[S::CursorX].rva] == 0xe9,
          "patched_unverified row", last_row);
    traps(true);
    {
        set_cursor_record(3413, 959);
        In in;
        in.ecx = u32(record);
        run(0x07bc4d, in);
        check(get32(S::cursor_x_rva) == 3413 && get32(S::cursor_y_rva) == 959, "registered stub at the identity cells");
    }
    traps(false);
    ui::shutdown();
    check(!ui::patched() && image_is(false) && row_has("status=restored"), "unverified site restored", last_row);
    // The window checked again at the device: a byte changed after initialize refuses before any claim.
    ui::fixture_reset();
    set_env(L"X3M_UI_SCALE", L"auto");
    ui::initialize();
    writable(true);
    image[S::windows[S::w_pick_callee].rva + 1] ^= 0x10;
    writable(false);
    check(!ui::device_created(5120, 1440) && row_has("reason=window_mismatch") && row_has("window=pick_callee") &&
              row_has("writes=none,none"),
          "window rechecked at the device", last_row);
    writable(true);
    image[S::windows[S::w_pick_callee].rva + 1] ^= 0x10;
    writable(false);
    check(image_is(false), "image pristine at the end");
    std::printf("RESULT checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
