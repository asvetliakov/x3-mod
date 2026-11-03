// Original synthetic fixture of the FL text density (src/engine/fl/text_density_fl.cpp, text_density_sites_fl.h;
// docs/reverse-engineering/fl/text-density.md, "Verified 2026-11-03"): no game code beyond the identity windows in
// text_density_sites_fl.h, no game data, no launch. Built with -DX3M_EDITION_FL=1 -DX3M_TEXT_DENSITY_FL_FIXTURE by
// verification/analysis/test_fl_text_density.py (the module force-includes fl_text_density_shim.h for read-back / chain /
// restore faults and the write-order witness) and run natively (WSL interop).
//
// A 64 KB-aligned private image stands in for X3FL.exe at `base`: every window at base + RVA with its fixups relocated to
// base, the engine's data cells (config slot, Materials rows and table, named count, KC VM slot, recorder, image slots)
// written by the fixture, decoys at the AP layout's offsets (an AP-RVA config slot and Materials table, cfg+0x784). The
// six engine helpers (lookup, alloc, free, plain copy, colour and alpha wrappers) are `jmp` to fakes with the FL register
// ABIs while vectors run. The harness enters a site with chosen GPRs, EFLAGS (DF included where no callee runs), XMM0-7,
// three x87 values with a non-default CW, a non-default MXCSR, LastError and sixteen stack words, and a `push imm32; ret`
// trap at the stop RVA (or the caller's return address) brings it back; every register, EFLAGS, ESP, the stack words,
// XMM0-7, the FNSAVE image, MXCSR and LastError are compared with the vanilla span on the same image or with the
// documented outputs.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdarg>
#include <initializer_list>
#include <cwchar>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../../src/engine/fl/text_density_fl.h"
#include "../../src/engine/fl/text_density_sites_fl.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/log_tiers.h"
namespace td = x3m::text_density;
namespace S = x3m::fl::text_density_fl;
namespace sites = x3m::text_density::sites;
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
char rows_seen[64][512];
unsigned row_count = 0;
namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(last_row, sizeof last_row, format, args);
    va_end(args);
    std::snprintf(rows_seen[row_count % 64], sizeof rows_seen[0], "%s", last_row);
    ++row_count;
    std::printf("LOG %s\n", last_row);
}
}
bool row_has(const char* needle) {
    return std::strstr(last_row, needle) != nullptr;
}
bool any_row_since(unsigned since, const char* a, const char* b = "") {
    for (unsigned i = since; i < row_count; ++i)
        if (std::strstr(rows_seen[i % 64], a) && std::strstr(rows_seen[i % 64], b)) return true;
    return false;
}

constexpr unsigned image_size = 0x220000;
std::uintptr_t base = 0;
unsigned char* image = nullptr;
unsigned char* pristine = nullptr;
alignas(16) unsigned char config[0x7a0], decoy_config[0x7a0];
uint32_t u32(const void* p) {
    return uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
void put32(uint32_t rva, uint32_t v) {
    std::memcpy(image + rva, &v, 4);
}
uint32_t cfg(unsigned offset) {
    uint32_t v = 0;
    std::memcpy(&v, config + offset, 4);
    return v;
}
void set_cfg(unsigned offset, uint32_t v) {
    std::memcpy(config + offset, &v, 4);
}

// ---- the seam (fl_text_density_shim.h): one-shot faults, the write witness, otherwise the real calls ----
std::uintptr_t fault_read_at = 0, fault_restore_at = 0, fault_push_at = 0;
unsigned fault_read_count = 0, fault_read_skip = 0, faults_taken = 0;
// A read that fails outright (as an unreadable page): the next `fail_read_times` reads of (fail_read_at, fail_read_count).
std::uintptr_t fail_read_at = 0;
unsigned fail_read_count = 0, fail_read_times = 0;
struct Write {
    uint32_t rva;
    unsigned char value;
    uint32_t field; // cfg+0x774 at the moment of the write
};
Write writes[64];
unsigned write_count = 0;
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count) {
    if (fail_read_times && address == fail_read_at && count == fail_read_count) {
        --fail_read_times;
        ++faults_taken;
        return false;
    }
    const bool ok = read_code(address, out, count);
    if (ok && fault_read_at && address == fault_read_at && count == fault_read_count) {
        if (fault_read_skip) {
            --fault_read_skip;
        } else {
            fault_read_at = 0;
            ++faults_taken;
            out[count - 1] ^= 0xff;
        }
    }
    return ok;
}
bool fixture_write_code(std::uintptr_t address, const unsigned char* bytes, unsigned n) {
    if (n == 1 && write_count < 64) writes[write_count++] = Write{uint32_t(address - base), bytes[0], cfg(S::config_density_offset)};
    return write_code(address, bytes, n);
}
bool fixture_restore(Site& site) {
    if (fault_restore_at && site.spec.address == fault_restore_at) {
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

// ---- the harness ----
extern "C" {
uint32_t in_eax, in_ebx, in_ecx, in_edx, in_esi, in_edi, in_ebp, in_flags;
uint32_t in_stack[16];
alignas(16) unsigned char in_xmm[128];
double in_st[3];
uint16_t in_fcw = 0x027f, def_fcw = 0;
uint32_t in_mxcsr = 0x3f80, def_mxcsr = 0;
uint32_t site_entry = 0, saved_esp = 0, esp_before = 0;
uint32_t out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, out_esp, out_flags, out_stack[32];
alignas(16) unsigned char out_xmm[128];
unsigned char out_fpu[108];
uint32_t out_mxcsr;
void fl_td_call();
void fl_td_return();
// The fake engine helpers (FL register ABIs).
void fake_lookup();
void fake_alloc();
void fake_free();
void fake_colour();
void fake_alpha();
void* __cdecl fake_lookup_c(uint32_t ecx);
void* __cdecl fake_alloc_c(uint32_t ecx, uint32_t edx, uint32_t bpp, uint32_t flags);
int __cdecl fake_free_c(void** object);
void __cdecl fake_colour_c(void* src, void* dst, int sx, int sy, int dx, int dy, int w, int h, int colour);
void __cdecl fake_alpha_c(void* src, void* dst, int sx, int sy, int dx, int dy, int w, int h, int colour);
void __cdecl fake_copy(void* src, void* dst, int sx, int sy, int dx, int dy, int w, int h);
}
asm(".text\n\t"
    ".globl _fl_td_call\n_fl_td_call:\n\t"
    "pushal\n\tpushfl\n\tmovl %esp,_saved_esp\n\tfnstcw _def_fcw\n\tstmxcsr _def_mxcsr\n\t"
    "subl $512,%esp\n\tandl $-16,%esp\n\t"
    "movl %esp,%edi\n\tleal _in_stack,%esi\n\tmovl $16,%ecx\n\tcld\n\trep movsl\n\t"
    "movdqu _in_xmm,%xmm0\n\tmovdqu _in_xmm+16,%xmm1\n\tmovdqu _in_xmm+32,%xmm2\n\tmovdqu _in_xmm+48,%xmm3\n\t"
    "movdqu _in_xmm+64,%xmm4\n\tmovdqu _in_xmm+80,%xmm5\n\tmovdqu _in_xmm+96,%xmm6\n\tmovdqu _in_xmm+112,%xmm7\n\t"
    "fninit\n\tfldcw _in_fcw\n\tfldl _in_st+16\n\tfldl _in_st+8\n\tfldl _in_st\n\tldmxcsr _in_mxcsr\n\t"
    "movl %esp,_esp_before\n\t"
    "movl _in_eax,%eax\n\tmovl _in_ebx,%ebx\n\tmovl _in_ecx,%ecx\n\tmovl _in_edx,%edx\n\tmovl _in_esi,%esi\n\t"
    "movl _in_edi,%edi\n\tmovl _in_ebp,%ebp\n\tpushl _in_flags\n\tpopfl\n\tjmp *_site_entry\n\t"
    ".globl _fl_td_return\n_fl_td_return:\n\t"
    "pushfl\n\tpopl _out_flags\n\tcld\n\tmovl %eax,_out_eax\n\tmovl %ebx,_out_ebx\n\tmovl %ecx,_out_ecx\n\t"
    "movl %edx,_out_edx\n\tmovl %esi,_out_esi\n\tmovl %edi,_out_edi\n\tmovl %ebp,_out_ebp\n\tmovl %esp,_out_esp\n\t"
    "movl %esp,%esi\n\tleal _out_stack,%edi\n\tmovl $32,%ecx\n\trep movsl\n\t"
    "movdqu %xmm0,_out_xmm\n\tmovdqu %xmm1,_out_xmm+16\n\tmovdqu %xmm2,_out_xmm+32\n\tmovdqu %xmm3,_out_xmm+48\n\t"
    "movdqu %xmm4,_out_xmm+64\n\tmovdqu %xmm5,_out_xmm+80\n\tmovdqu %xmm6,_out_xmm+96\n\tmovdqu %xmm7,_out_xmm+112\n\t"
    "fnsave _out_fpu\n\tstmxcsr _out_mxcsr\n\tfninit\n\tfldcw _def_fcw\n\tldmxcsr _def_mxcsr\n\t"
    "movl _saved_esp,%esp\n\tpopfl\n\tpopal\n\tret\n\t"
    ".globl _fake_lookup\n_fake_lookup:\n\tpushl %ecx\n\tcall _fake_lookup_c\n\taddl $4,%esp\n\tret\n\t"
    ".globl _fake_alloc\n_fake_alloc:\n\tpushl 8(%esp)\n\tpushl 8(%esp)\n\tpushl %edx\n\tpushl %ecx\n\tcall _fake_alloc_c\n\t"
    "addl $16,%esp\n\tret\n\t"
    ".globl _fake_free\n_fake_free:\n\tpushl %ecx\n\tcall _fake_free_c\n\taddl $4,%esp\n\tret\n\t"
    // The wrappers' seven caller-cleaned words are passed as copies (a cdecl callee may reuse its argument slots).
    ".globl _fake_colour\n_fake_colour:\n\tpushl 28(%esp)\n\tpushl 28(%esp)\n\tpushl 28(%esp)\n\tpushl 28(%esp)\n\t"
    "pushl 28(%esp)\n\tpushl 28(%esp)\n\tpushl 28(%esp)\n\tpushl %edx\n\tpushl %ecx\n\tcall _fake_colour_c\n\taddl $36,%esp\n\tret\n\t"
    ".globl _fake_alpha\n_fake_alpha:\n\tpushl 28(%esp)\n\tpushl 28(%esp)\n\tpushl 28(%esp)\n\tpushl 28(%esp)\n\t"
    "pushl 28(%esp)\n\tpushl 28(%esp)\n\tpushl 28(%esp)\n\tpushl %edx\n\tpushl %ecx\n\tcall _fake_alpha_c\n\taddl $36,%esp\n\tret\n\t");

constexpr uint32_t sentinel_error = 0x2a2a1357, flag_mask = 0xcd5; // CF PF AF ZF SF DF OF
struct Out {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp_delta, flags, stack[32];
    unsigned char xmm[128], fpu[108];
    uint32_t mxcsr, error;
};
struct In {
    uint32_t eax = 0x11111111, ebx = 0x22222222, ecx = 0x33333333, edx = 0x44444444, esi = 0x55555555, edi = 0x66666666,
             ebp = 0x77777777, flags = 0x0893;
    uint32_t stack[16] = {};
};
// Places a `push imm32; ret` back into the harness at `stop` (0 = none: the site returns to the stacked address).
void trap(uint32_t stop, bool on) {
    if (!stop) return;
    if (on) {
        image[stop] = 0x68;
        put32(stop + 1, u32(reinterpret_cast<void*>(&fl_td_return)));
        image[stop + 5] = 0xc3;
    } else {
        std::memcpy(image + stop, pristine + stop, 6);
    }
    FlushInstructionCache(GetCurrentProcess(), image + stop, 6);
}
Out run(std::uintptr_t entry, uint32_t stop, const In& in) {
    in_eax = in.eax;
    in_ebx = in.ebx;
    in_ecx = in.ecx;
    in_edx = in.edx;
    in_esi = in.esi;
    in_edi = in.edi;
    in_ebp = in.ebp;
    in_flags = in.flags;
    std::memcpy(in_stack, in.stack, sizeof in_stack);
    for (unsigned i = 0; i < 128; ++i) in_xmm[i] = static_cast<unsigned char>(0x30 + i * 7);
    in_st[0] = 1.25;
    in_st[1] = -3.5e10;
    in_st[2] = 1.0 / 3.0;
    site_entry = uint32_t(entry);
    trap(stop, true);
    SetLastError(sentinel_error);
    fl_td_call();
    const DWORD error = GetLastError();
    trap(stop, false);
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
    std::memcpy(o.xmm, out_xmm, 128);
    std::memcpy(o.fpu, out_fpu, 108);
    o.mxcsr = out_mxcsr;
    o.error = error;
    return o;
}
Out run_rva(uint32_t rva, uint32_t stop, const In& in) {
    return run(base + rva, stop, in);
}
// The caller's own state (the harness straight into its return): what a transparent path must hand back.
Out reference(const In& in) {
    return run(reinterpret_cast<std::uintptr_t>(&fl_td_return), 0, in);
}
// x87: CW, SW, TW and the eight registers (the FNSAVE image without the instruction/operand pointers).
bool fpu_equal(const Out& a, const Out& b) {
    return !std::memcmp(a.fpu, b.fpu, 2) && !std::memcmp(a.fpu + 4, b.fpu + 4, 2) && !std::memcmp(a.fpu + 8, b.fpu + 8, 2) &&
           !std::memcmp(a.fpu + 28, b.fpu + 28, 80);
}
// The caller-visible float/error state: XMM0-7, the x87 image, MXCSR, LastError.
bool state_equal(const Out& a, const Out& b) {
    return !std::memcmp(a.xmm, b.xmm, 128) && fpu_equal(a, b) && a.mxcsr == b.mxcsr && a.error == b.error;
}
// Every register, EFLAGS, ESP and the 32 stack words, except `locals` uninitialised words from `from` (the function's own
// `sub esp,n` area, which the thunk's frame below the caller's ESP leaves different).
bool all_equal(const Out& a, const Out& b, unsigned from = 0, unsigned locals = 0) {
    bool stack = true;
    for (unsigned i = 0; i < 32; ++i) stack = stack && ((i >= from && i < from + locals) || a.stack[i] == b.stack[i]);
    return a.eax == b.eax && a.ebx == b.ebx && a.ecx == b.ecx && a.edx == b.edx && a.esi == b.esi && a.edi == b.edi && a.ebp == b.ebp &&
           a.esp_delta == b.esp_delta && (a.flags & flag_mask) == (b.flags & flag_mask) && stack && state_equal(a, b);
}
void describe(const Out& a, const Out& b, char* out, unsigned n) {
    std::snprintf(out, n, "eax %08lx/%08lx ecx %08lx/%08lx edx %08lx/%08lx esi %08lx/%08lx esp %lu/%lu flags %03lx/%03lx xmm %d fpu %d mxcsr %lx/%lx err %lx/%lx",
                  (unsigned long)a.eax, (unsigned long)b.eax, (unsigned long)a.ecx, (unsigned long)b.ecx, (unsigned long)a.edx,
                  (unsigned long)b.edx, (unsigned long)a.esi, (unsigned long)b.esi, (unsigned long)a.esp_delta, (unsigned long)b.esp_delta,
                  (unsigned long)(a.flags & flag_mask), (unsigned long)(b.flags & flag_mask), std::memcmp(a.xmm, b.xmm, 128) != 0,
                  !fpu_equal(a, b), (unsigned long)a.mxcsr, (unsigned long)b.mxcsr, (unsigned long)a.error, (unsigned long)b.error);
}

// ---- engine objects and fakes ----
struct Object {
    alignas(16) unsigned char bytes[0x64];
    uint32_t* pixels;
    bool shadow; // allocated by the module (through the fake alloc)
};
constexpr unsigned object_pool = 48;
Object objects[object_pool];
unsigned object_count = 0;
Object* new_object(int w, int h, uint32_t flags, uint32_t seed) {
    Object* o = &objects[object_count++];
    std::memset(o->bytes, 0, sizeof o->bytes);
    o->shadow = false;
    o->pixels = static_cast<uint32_t*>(std::calloc(size_t(w) * size_t(h) + 1, 4));
    for (int i = 0; i < w * h; ++i) o->pixels[i] = seed + uint32_t(i);
    const int16_t w16 = int16_t(w), h16 = int16_t(h);
    const uint32_t p = u32(o->pixels);
    std::memcpy(o->bytes + 0, &p, 4);
    std::memcpy(o->bytes + 4, &w16, 2);
    std::memcpy(o->bytes + 6, &h16, 2);
    const uint16_t bpp = 4;
    std::memcpy(o->bytes + 0xc, &bpp, 2);
    std::memcpy(o->bytes + 0x10, &flags, 4);
    const uint32_t texture = 0x7e000000u + object_count * 0x100u; // a stand-in IDirect3DTexture9 pointer
    std::memcpy(o->bytes + S::object_texture_offset, &texture, 4);
    return o;
}
Object* from(void* p) {
    for (unsigned i = 0; i < object_count; ++i)
        if (objects[i].bytes == p) return &objects[i];
    return nullptr;
}
int width(const Object* o) {
    int16_t v = 0;
    std::memcpy(&v, o->bytes + 4, 2);
    return v;
}
int height(const Object* o) {
    int16_t v = 0;
    std::memcpy(&v, o->bytes + 6, 2);
    return v;
}
uint32_t pixel(const Object* o, int x, int y) {
    return o->pixels[y * width(o) + x];
}
constexpr int max_ids = 64;
Object* by_id[max_ids];
struct Call {
    int kind; // 0 plain copy, 1 colour, 2 alpha
    void *src, *dst;
    int sx, sy, dx, dy, w, h, colour;
};
Call calls[256];
unsigned call_count = 0, oob = 0, allocs = 0, frees = 0, lookups = 0;
uint32_t last_alloc_flags = 0, last_alloc_bpp = 0;
void do_copy(void* src, void* dst, int sx, int sy, int dx, int dy, int w, int h) {
    Object *s = from(src), *d = from(dst);
    if (!s || !d || w <= 0 || h <= 0) return;
    if (sx < 0 || sy < 0 || dx < 0 || dy < 0 || sx + w > width(s) || sy + h > height(s) || dx + w > width(d) || dy + h > height(d)) {
        ++oob;
        return;
    }
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) d->pixels[(dy + j) * width(d) + dx + i] = s->pixels[(sy + j) * width(s) + sx + i];
}
extern "C" {
void* __cdecl fake_lookup_c(uint32_t ecx) {
    ++lookups;
    const int16_t id = int16_t(ecx & 0xffff);
    return id >= 0 && id < max_ids && by_id[id] ? by_id[id]->bytes : nullptr;
}
void* __cdecl fake_alloc_c(uint32_t ecx, uint32_t edx, uint32_t bpp, uint32_t flags) {
    ++allocs;
    last_alloc_flags = flags;
    last_alloc_bpp = bpp;
    Object* o = new_object(int16_t(ecx & 0xffff), int16_t(edx & 0xffff), flags | 0x2000, 0);
    o->shadow = true;
    return o->bytes;
}
int __cdecl fake_free_c(void** object) {
    ++frees;
    *object = nullptr;
    return 0;
}
void record(int kind, void* src, void* dst, int sx, int sy, int dx, int dy, int w, int h, int colour) {
    if (call_count < 256) calls[call_count++] = Call{kind, src, dst, sx, sy, dx, dy, w, h, colour};
    do_copy(src, dst, sx, sy, dx, dy, w, h);
}
void __cdecl fake_colour_c(void* src, void* dst, int sx, int sy, int dx, int dy, int w, int h, int colour) {
    record(1, src, dst, sx, sy, dx, dy, w, h, colour);
}
void __cdecl fake_alpha_c(void* src, void* dst, int sx, int sy, int dx, int dy, int w, int h, int colour) {
    record(2, src, dst, sx, sy, dx, dy, w, h, colour);
}
void __cdecl fake_copy(void* src, void* dst, int sx, int sy, int dx, int dy, int w, int h) {
    const Object* d = from(dst);
    if (d && !d->shadow) record(0, src, dst, sx, sy, dx, dy, w, h, -1);
    else do_copy(src, dst, sx, sy, dx, dy, w, h); // building a shadow: the column copies and the row replication
}
}
const uint32_t helper_rvas[6] = {S::lookup_rva, S::alloc_rva, S::free_rva, S::copy_rva, S::colour_rva, S::alpha_rva};
void fakes(bool on) {
    void* const targets[6] = {reinterpret_cast<void*>(&fake_lookup), reinterpret_cast<void*>(&fake_alloc), reinterpret_cast<void*>(&fake_free),
                              reinterpret_cast<void*>(&fake_copy), reinterpret_cast<void*>(&fake_colour), reinterpret_cast<void*>(&fake_alpha)};
    for (unsigned i = 0; i < 6; ++i) {
        const uint32_t rva = helper_rvas[i];
        if (on) {
            image[rva] = 0xe9;
            put32(rva + 1, u32(targets[i]) - uint32_t(base + rva + 5));
        } else {
            std::memcpy(image + rva, pristine + rva, 5);
        }
    }
    FlushInstructionCache(GetCurrentProcess(), image, image_size);
}

// ---- the image ----
// AP-layout decoys: AP's VAs as RVAs (0x606f34 config slot, 0x608dac/0x608db0 rows, 0x6069ac table, 0x6069b4 named).
constexpr uint32_t ap_config_slot = 0x206f34, ap_rows_count = 0x208dac, ap_rows_slot = 0x208db0, ap_table_slot = 0x2069ac,
                   ap_named_count = 0x2069b4;
constexpr int rows_n = 40;
alignas(16) unsigned char rows_mem[rows_n * 0x3c], table_mem[(rows_n + 8) * 0x10], decoy_rows[rows_n * 0x3c], decoy_table[rows_n * 0x10];
uint32_t row_flags_before[rows_n];
uint32_t row_flag(int id) {
    uint32_t v = 0;
    std::memcpy(&v, rows_mem + id * 0x3c + 0x10, 4);
    return v;
}
uint32_t entry_flag(int id) {
    uint32_t v = 0;
    std::memcpy(&v, table_mem + id * 0x10 + 4, 4);
    return v;
}
void set_row(unsigned char* rows, unsigned char* table, int id, uint32_t flags, int16_t w, int16_t h) {
    const int16_t valid = int16_t(id + 1);
    std::memcpy(rows + id * 0x3c + 0xc, &valid, 2);
    std::memcpy(rows + id * 0x3c + 0x10, &flags, 4);
    std::memcpy(rows + id * 0x3c + 0x14, &w, 2);
    std::memcpy(rows + id * 0x3c + 0x16, &h, 2);
    const uint32_t entry = flags | 0x20000000u; // an engine bit the edit must keep
    std::memcpy(table + id * 0x10 + 4, &entry, 4);
}
// The ids the vectors use.
constexpr int id_text = 3, id_text2 = 4, id_static = 10, id_fontscale_file = 12, id_plain = 20, id_named = rows_n + 2;
void lay_tables() {
    std::memset(rows_mem, 0, sizeof rows_mem);
    std::memset(table_mem, 0, sizeof table_mem);
    for (int id = 0; id < rows_n; ++id) {
        uint32_t f = 0;
        if (id % 5 == 3) f = sites::mpf_generated | sites::mpf_writeable | (id % 2 ? sites::mpf_nofiltering : 0u); // text targets
        else if (id % 7 == 5) f = sites::mpf_fontscale;                                                          // file-backed FONTSCALE
        else if (id % 11 == 9) f = sites::mpf_generated;                                                         // generated read-only
        else f = 0x2u | (id % 3 ? sites::mpf_nofiltering : 0u);                                                  // ordinary files
        if (id == id_text) f = sites::mpf_generated | sites::mpf_writeable | sites::mpf_nofiltering;
        if (id == id_text2) f = sites::mpf_generated | sites::mpf_writeable;
        if (id == id_static) f = 0x2u;
        if (id == id_fontscale_file) f = sites::mpf_fontscale;
        const int16_t h = id == 8 ? 2048 : 64;
        set_row(rows_mem, table_mem, id, f, int16_t(id == 8 ? 64 : 256), h);
        if (id == 8) set_row(rows_mem, table_mem, id, sites::mpf_generated | sites::mpf_writeable, 64, 2048);
        row_flags_before[id] = row_flag(id);
        set_row(decoy_rows, decoy_table, id, 0x5a000000u | uint32_t(id), 16, 16);
    }
    const uint32_t named = sites::mpf_generated * 0; // a named file atlas: GENERATED never set
    std::memcpy(table_mem + id_named * 0x10 + 4, &named, 4);
}
// The Materials load as the engine leaves it (count, rows, table, named count); zero = before the load.
void materials(bool loaded) {
    const int16_t count = loaded ? int16_t(rows_n) : 0, named = loaded ? 4 : 0;
    std::memcpy(image + S::rows_count_rva, &count, 2);
    put32(S::rows_slot_rva, loaded ? u32(rows_mem) : 0);
    put32(S::table_slot_rva, loaded ? u32(table_mem) : 0);
    std::memcpy(image + S::named_count_rva, &named, 2);
}
void lay_data() {
    std::memset(config, 0, sizeof config);
    set_cfg(S::config_density_offset, 1);
    set_cfg(0x784, 0x5a5a5a5a); // the AP field: never written on FL
    set_cfg(S::config_language_offset, 44);
    std::memset(decoy_config, 0, sizeof decoy_config);
    std::memcpy(decoy_config + 0x774, "\x07\x00\x00\x00", 4);
    std::memcpy(decoy_config + 0x784, "\x07\x00\x00\x00", 4);
    put32(S::config_slot_rva, u32(config));
    put32(S::vm_slot_rva, 0);
    put32(S::recorder_slot_rva, 0);
    put32(S::image_object_slot_rva, 0);
    put32(S::image_path_slot_rva, 0);
    materials(false);
    put32(ap_config_slot, u32(decoy_config));
    const int16_t n = rows_n;
    std::memcpy(image + ap_rows_count, &n, 2);
    put32(ap_rows_slot, u32(decoy_rows));
    put32(ap_table_slot, u32(decoy_table));
    std::memcpy(image + ap_named_count, &n, 2);
}
void lay_out() {
    std::memset(image, 0xcc, image_size);
    for (const S::Window& w : S::windows) {
        std::memcpy(image + w.rva, w.bytes, w.length);
        for (unsigned k = 0; k < w.reloc_count; ++k) put32(w.rva + w.reloc_offset[k], uint32_t(base + w.reloc_target[k]));
    }
    lay_data();
    FlushInstructionCache(GetCurrentProcess(), image, image_size);
}
// The code image equals the pristine layout (data cells excluded): every window, except the first five bytes of the
// claimed spans (when `claimed`), the materials call's five bytes and the edits listed in `edited`.
// `image_retained`: the image block's claim stays in place (inert) while every other byte is as `claimed` says.
bool image_is(bool claimed, unsigned d = 1, bool style = false, bool image_retained = false) {
    const uint32_t image_site = S::claims[S::BltImage].rva;
    if (image_retained && image[image_site] != 0xe9) return false;
    for (const S::Window& w : S::windows)
        for (unsigned i = 0; i < w.length; ++i) {
            const uint32_t at = w.rva + i;
            bool skip = image_retained && at >= image_site && at < image_site + 5;
            for (const S::Claim& c : S::claims) skip = skip || (claimed && at >= c.rva && at < c.rva + 5 && (!c.diagnostic || claimed));
            skip = skip || (claimed && at >= S::materials_call_rva && at < S::materials_call_rva + 5);
            if (skip) continue;
            unsigned char want = pristine[at];
            for (unsigned e = 0; e < S::edit_count; ++e)
                if (at == S::edits[e].rva && (e < S::cockpit_edits ? d > 1 : style)) want = S::edit_value(S::edits[e], d);
            if (image[at] != want) return false;
        }
    return true;
}
bool claims_in(bool diagnostics) {
    for (const S::Claim& c : S::claims)
        if ((image[c.rva] == 0xe9) != (!c.diagnostic || diagnostics)) return false;
    return image[S::materials_call_rva] == 0xe8 &&
           std::memcmp(image + S::materials_call_rva, pristine + S::materials_call_rva, 5) != 0;
}
// The loose fonts under <exe dir>\f.
wchar_t font_dir[MAX_PATH];
void font_file(const char* name, bool present) {
    wchar_t path[MAX_PATH];
    unsigned n = 0;
    while (font_dir[n] && n < MAX_PATH - 2) {
        path[n] = font_dir[n];
        ++n;
    }
    path[n++] = L'\\';
    for (unsigned i = 0; name[i] && n < MAX_PATH - 1; ++i) path[n++] = wchar_t(static_cast<unsigned char>(name[i]));
    path[n] = 0;
    if (present) {
        const HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    } else {
        DeleteFileW(path);
    }
}
void font_set(unsigned d, bool present) {
    for (const sites::FontFamily& f : sites::font_families) {
        char name[48];
        std::snprintf(name, sizeof name, "%s%u.abc", f.name, f.size * d);
        font_file(name, present);
        std::snprintf(name, sizeof name, "%s%u.tga", f.name, f.size * d);
        font_file(name, present);
    }
}
void set_env(const wchar_t* name, const wchar_t* value) {
    SetEnvironmentVariableW(name, value);
}
template <class F> bool keeps_error(F f) {
    SetLastError(sentinel_error);
    f();
    return GetLastError() == sentinel_error;
}
void fakes(bool on);
void materials(bool loaded);
// The KC VM slot (the init routine creates the VM at 0x077561, before CreateDevice: a realistic device step sets it).
void vm(bool exists) {
    put32(S::vm_slot_rva, exists ? 0x0c0ffee0u : 0u);
}
// Back to the backend-load state (no VM, no rows): a retained image claim can go back; then nothing is registered.
void release() {
    vm(false);
    materials(false);
    td::shutdown();
}
bool init(const wchar_t* setting) {
    fakes(false); // the windows are compared at initialize: the helpers' own bytes
    release();
    td::fixture_reset();
    set_env(L"X3M_TEXT_DENSITY", setting);
    bool ok = true;
    check(keeps_error([] { td::initialize(); }), "initialize LastError");
    ok = row_has("status=pending");
    return ok;
}
// A fake device whose GetDeviceCaps (slot 7) reports the given texture limits.
uint32_t caps_w = 0, caps_h = 0;
HRESULT __stdcall fake_caps(void*, D3DCAPS9* caps) {
    std::memset(caps, 0, sizeof *caps);
    caps->MaxTextureWidth = caps_w;
    caps->MaxTextureHeight = caps_h;
    return S_OK;
}
void* fake_vtable[8] = {};
void* fake_device_object[1] = {fake_vtable};
IDirect3DDevice9* fake_device() {
    fake_vtable[7] = reinterpret_cast<void*>(&fake_caps);
    return reinterpret_cast<IDirect3DDevice9*>(fake_device_object);
}
unsigned long sampler[16];
long __stdcall sampler_get(IDirect3DDevice9*, unsigned long, int type, unsigned long* v) {
    *v = sampler[type];
    return 0;
}
long __stdcall sampler_set(IDirect3DDevice9*, unsigned long, int type, unsigned long v) {
    sampler[type] = v;
    return 0;
}
bool device(double s, IDirect3DDevice9* dev = nullptr) {
    bool ok = false;
    check(keeps_error([&] { ok = td::device_created(s, dev); }), "device_created LastError");
    return ok;
}
// The Materials seam through the claimed call (vanilla: the call into the callee); compared with vanilla.
Out materials_vector(const char* tag, bool expect_equal_to = true) {
    In in;
    in.stack[0] = 0x0badc0de;
    const Out o = run_rva(S::materials_call_rva, S::materials_callee_rva, in);
    char label[96], detail[256];
    std::snprintf(label, sizeof label, "%s materials seam: stack and state", tag);
    In ref_in = in;
    Out ref = reference(ref_in);
    // At the callee: the return address base+0x0a6a00 on top, everything else the caller's.
    describe(o, ref, detail, sizeof detail);
    check(expect_equal_to && o.stack[0] == uint32_t(base + S::materials_return_rva) && o.stack[1] == 0x0badc0de && o.esp_delta == 4 &&
              o.eax == in.eax && o.ebx == in.ebx && o.ecx == in.ecx && o.edx == in.edx && o.esi == in.esi && o.edi == in.edi &&
              o.ebp == in.ebp && (o.flags & flag_mask) == (in.flags & flag_mask) && state_equal(o, ref),
          label, detail);
    return o;
}

// ---- the vectors ----
char tahoma[] = "Tahoma";
char zekton[] = "Zekton";
// The font entry, stopped after the displaced span (`push edi` at +7): name ECX, cell EDX, size/flags/yoff on the stack.
void font_vector(const char* tag, char* name, unsigned size, unsigned cell, unsigned yoff, unsigned scale_by, uint32_t flags = 0x0893) {
    In in;
    in.ecx = u32(name);
    in.edx = cell;
    in.flags = flags;
    in.stack[0] = 0x0c0de000;
    in.stack[1] = size;
    in.stack[2] = 0x21;
    in.stack[3] = yoff;
    in.stack[4] = 0xfeedf00d;
    const Out patched = run_rva(S::claims[S::Font].rva, S::claims[S::Font].rva + S::claims[S::Font].length, in);
    Out want = patched;
    // The model: the vanilla span on the pristine bytes with the scaled request.
    In model = in;
    model.edx = cell * scale_by;
    model.stack[1] = size * scale_by;
    model.stack[3] = yoff * scale_by;
    unsigned char saved[8];
    std::memcpy(saved, image + S::claims[S::Font].rva, 8);
    std::memcpy(image + S::claims[S::Font].rva, pristine + S::claims[S::Font].rva, 8);
    FlushInstructionCache(GetCurrentProcess(), image + S::claims[S::Font].rva, 8);
    want = run_rva(S::claims[S::Font].rva, S::claims[S::Font].rva + S::claims[S::Font].length, model);
    std::memcpy(image + S::claims[S::Font].rva, saved, 8);
    FlushInstructionCache(GetCurrentProcess(), image + S::claims[S::Font].rva, 8);
    char label[96], detail[256];
    std::snprintf(label, sizeof label, "%s font %s%u x%u", tag, name, size, scale_by);
    describe(patched, want, detail, sizeof detail);
    check(all_equal(patched, want) && patched.esi == size * scale_by && patched.edx == cell * scale_by, label, detail);
}
// A block/alpha blit stopped after the span (pass-through), or returning to the stacked address (handled).
struct BlitArgs {
    int32_t src, dst, sx, sy, dx, dy, w, h, colour, clip;
};
In blit_in(const BlitArgs& a, uint32_t flags = 0x0893) {
    In in;
    in.ecx = uint32_t(a.src);
    in.edx = uint32_t(a.dst);
    in.flags = flags;
    in.stack[0] = u32(reinterpret_cast<void*>(&fl_td_return));
    const int32_t words[8] = {a.sx, a.sy, a.dx, a.dy, a.w, a.h, a.colour, a.clip};
    for (unsigned i = 0; i < 8; ++i) in.stack[1 + i] = uint32_t(words[i]);
    in.stack[9] = 0x0d0d0d0d;
    return in;
}
// Pass-through: the claimed path equals the vanilla span (same image with the span's pristine bytes) at the span end.
void blit_pass(const char* tag, unsigned claim, const BlitArgs& a, uint32_t flags = 0x0893) {
    const S::Claim& c = S::claims[claim];
    const In in = blit_in(a, flags);
    const unsigned calls_before = call_count;
    const Out patched = run_rva(c.rva, c.rva + c.length, in);
    unsigned char saved[8];
    std::memcpy(saved, image + c.rva, 8);
    std::memcpy(image + c.rva, pristine + c.rva, 8);
    FlushInstructionCache(GetCurrentProcess(), image + c.rva, 8);
    const Out vanilla = run_rva(c.rva, c.rva + c.length, in);
    std::memcpy(image + c.rva, saved, 8);
    FlushInstructionCache(GetCurrentProcess(), image + c.rva, 8);
    char label[96], detail[256];
    std::snprintf(label, sizeof label, "%s %s pass-through src=%ld dst=%ld", tag, claim == S::BltAlpha ? "alpha" : "block", long(a.src),
                  long(a.dst));
    describe(patched, vanilla, detail, sizeof detail);
    check(all_equal(patched, vanilla, 3, 13) && patched.stack[16] == in.stack[0] && patched.stack[17] == uint32_t(a.sx) &&
              patched.stack[24] == uint32_t(a.clip) && call_count == calls_before,
          label, detail);
}
// Handled: the C side drew from the shadow; `ret` to the caller with EAX = the destination object.
void blit_handled(const char* tag, unsigned claim, const BlitArgs& a, unsigned d, int expect_kind, const int* expect_coords) {
    const S::Claim& c = S::claims[claim];
    const In in = blit_in(a);
    const Out ref = reference(in);
    const unsigned calls_before = call_count;
    const Out o = run_rva(c.rva, 0, in);
    char label[112], detail[256];
    std::snprintf(label, sizeof label, "%s %s handled src=%ld dst=%ld d=%u", tag, claim == S::BltAlpha ? "alpha" : "block", long(a.src),
                  long(a.dst), d);
    describe(o, ref, detail, sizeof detail);
    check(o.eax == u32(by_id[a.dst]->bytes) && o.ebx == in.ebx && o.ecx == in.ecx && o.edx == in.edx && o.esi == in.esi &&
              o.edi == in.edi && o.ebp == in.ebp && o.esp_delta == uint32_t(-4) && (o.flags & flag_mask) == (in.flags & flag_mask) &&
              state_equal(o, ref) && !std::memcmp(o.stack, in.stack + 1, 15 * 4),
          label, detail);
    std::snprintf(label, sizeof label, "%s %s leaf call", tag, claim == S::BltAlpha ? "alpha" : "block");
    bool ok = call_count == calls_before + 1;
    if (ok) {
        const Call& k = calls[call_count - 1];
        Object* shadow = from(k.src);
        ok = k.kind == expect_kind && k.dst == by_id[a.dst]->bytes && shadow && shadow != by_id[a.src] &&
             width(shadow) == width(by_id[a.src]) * int(d) && height(shadow) == height(by_id[a.src]) * int(d) && k.sx == expect_coords[0] &&
             k.sy == expect_coords[1] && k.dx == expect_coords[2] && k.dy == expect_coords[3] && k.w == expect_coords[4] &&
             k.h == expect_coords[5] && (expect_kind == 0 || k.colour == a.colour);
        // The shadow is the d x nearest-neighbour copy of the source.
        for (int y = 0; ok && y < height(shadow); ++y)
            for (int x = 0; ok && x < width(shadow); ++x) ok = pixel(shadow, x, y) == pixel(by_id[a.src], x / int(d), y / int(d));
    }
    check(ok, label);
}

// A fault inside a vector: one row with the address (image-relative when inside the image) and the registers.
LONG WINAPI witness(EXCEPTION_POINTERS* e) {
    const CONTEXT* c = e->ContextRecord;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e->ExceptionRecord->ExceptionAddress);
    std::printf("FAIL exception code=%08lx at=%08lx image_rva=%06lx eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx "
                "ebp=%08lx esp=%08lx info=%08lx\n",
                static_cast<unsigned long>(e->ExceptionRecord->ExceptionCode), static_cast<unsigned long>(at),
                static_cast<unsigned long>(at >= base && at < base + image_size ? at - base : 0), c->Eax, c->Ebx, c->Ecx, c->Edx,
                c->Esi, c->Edi, c->Ebp, c->Esp,
                static_cast<unsigned long>(e->ExceptionRecord->NumberParameters > 1 ? e->ExceptionRecord->ExceptionInformation[1] : 0));
    std::fflush(stdout);
    return EXCEPTION_CONTINUE_SEARCH;
}
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetUnhandledExceptionFilter(&witness);
    const bool late = argc > 1 && !std::strcmp(argv[1], "late");
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    base = reinterpret_cast<std::uintptr_t>(image);
    check(image && !(base & 0xffff), "image 64 KB aligned");
    if (!image) return 2;
    lay_out();
    pristine = static_cast<unsigned char*>(std::malloc(image_size));
    std::memcpy(pristine, image, image_size);
    for (const S::Window& w : S::windows) check(S::fnv1a(w.bytes, w.length) == w.fnv, "window fnv", w.name);
    lay_tables();
    {
        wchar_t exe[MAX_PATH];
        const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        DWORD cut = n;
        while (cut && exe[cut - 1] != L'\\' && exe[cut - 1] != L'/') --cut;
        exe[cut] = 0;
        wcscpy(font_dir, exe);
        wcscat(font_dir, L"f");
        CreateDirectoryW(font_dir, nullptr);
    }
    font_set(2, true);
    font_set(3, true);
    set_env(L"X3M_DEBUG", nullptr);
    x3m::log_tier::cached_debug = true; // the debug-tier rows (the module reads the cached flag on its hot paths)
    td::fixture_base(base);

    if (argc > 1 && !std::strcmp(argv[1], "timing")) {
        // Diagnostic cost (not game FPS): one block blit through the harness, vanilla span, the inactive fast path, the
        // active pass-through (flagged source: the envelope and the C side's row checks) and the handled draw (shadow
        // cached, the colour leaf is the fake); the differences are the thunk's cost. Production tier: no debug rows.
        x3m::log_tier::cached_debug = false;
        by_id[id_text] = new_object(64, 32, sites::generated_surface_flags | sites::surface_depth_32, 0x1000);
        by_id[id_text2] = new_object(32, 16, sites::generated_surface_flags | sites::surface_depth_32, 0x2000);
        by_id[id_static] = new_object(8, 4, sites::generated_surface_flags | sites::surface_depth_32, 0x3000);
        LARGE_INTEGER f{}, t0{}, t1{};
        QueryPerformanceFrequency(&f);
        const unsigned n = 200000;
        const S::Claim& c = S::claims[S::BltBlock];
        const In pass = blit_in({id_text2, id_text, 0, 0, 0, 0, 4, 4, 1, 0});
        const In draw = blit_in({id_static, id_text, 1, 1, 2, 2, 4, 2, 0x00ffffff, 0});
        double ns[5] = {};
        auto time = [&](unsigned slot, const In& in, uint32_t stop) {
            QueryPerformanceCounter(&t0);
            for (unsigned k = 0; k < n; ++k) run_rva(c.rva, stop, in);
            QueryPerformanceCounter(&t1);
            ns[slot] = double(t1.QuadPart - t0.QuadPart) * 1e9 / double(f.QuadPart) / n;
        };
        fakes(true);
        time(0, pass, c.rva + c.length);
        check(init(L"auto"), "timing install");
        fakes(true);
        time(1, pass, c.rva + c.length);
        check(device(1.5), "timing device");
        materials(true);
        fakes(true);
        materials_vector("timing");
        time(2, pass, c.rva + c.length);
        call_count = 0;
        time(3, draw, 0);
        QueryPerformanceCounter(&t0); // the harness alone (no trap: the handled draw returns with `ret`)
        for (unsigned k = 0; k < n; ++k) reference(draw);
        QueryPerformanceCounter(&t1);
        ns[4] = double(t1.QuadPart - t0.QuadPart) * 1e9 / double(f.QuadPart) / n;
        fakes(false);
        td::shutdown();
        std::printf("TIMING calls=%u vanilla_ns=%.1f inactive_ns=%.1f active_pass_ns=%.1f (with the stop trap) handled_ns=%.1f "
                    "harness_only_ns=%.1f (no trap)\n",
                    n, ns[0], ns[1], ns[2], ns[3], ns[4]);
        std::printf("RESULT checks=%u failures=%u\n", checks, failures);
        return failures ? 1 : 0;
    }
    if (late) {
        // The install window closes between the claims and the device: nothing is edited, every claim goes back; a
        // later initialize claims nothing.
        check(init(L"auto"), "late: claims in", last_row);
        engine_patch::close_install_window("fixture");
        vm(true);
        check(!device(1.5), "late: device refused");
        check(row_has("status=refused reason=late_claim") && row_has("edition=fl") && row_has("config=1 ") &&
                  row_has("image_claim=retained_inert"),
              "late: device row", last_row);
        check(!td::patched() && image_is(false, 1, false, true) && cfg(S::config_density_offset) == 1,
              "late: nothing written, the image claim kept inert (the VM exists)");
        fakes(true);
        {
            // The retained claim is inert: the image block equals the vanilla span.
            static unsigned char frame[0x80], record[0x30];
            In in;
            in.esi = u32(record);
            in.ebp = u32(frame + 0x80);
            in.edx = 2;
            in.flags = 0x0cd7;
            const S::Claim& c = S::claims[S::BltImage];
            const Out patched = run_rva(c.rva, c.rva + c.length, in);
            unsigned char saved[8];
            std::memcpy(saved, image + c.rva, 8);
            std::memcpy(image + c.rva, pristine + c.rva, 8);
            const Out vanilla = run_rva(c.rva, c.rva + c.length, in);
            std::memcpy(image + c.rva, saved, 8);
            FlushInstructionCache(GetCurrentProcess(), image + c.rva, 8);
            char detail[256];
            describe(patched, vanilla, detail, sizeof detail);
            check(all_equal(patched, vanilla), "late: retained image claim equals the vanilla span", detail);
        }
        fakes(false);
        release();
        check(!td::patched() && image_is(false) && row_has("image_claim=none"), "late: released once the witness holds", last_row);
        td::fixture_reset();
        set_env(L"X3M_TEXT_DENSITY", L"auto");
        td::initialize();
        check(row_has("status=refused reason=late_claim") && row_has("failed_site=text_density_font_fl"), "late: initialize refused", last_row);
        check(!td::patched() && image_is(false), "late: initialize wrote nothing");
        std::printf("RESULT checks=%u failures=%u\n", checks, failures);
        return failures ? 1 : 0;
    }

    // ---- refusals at initialize (nothing claimed) ----
    struct Refusal {
        const wchar_t* setting;
        const char* row;
    };
    const Refusal refusals[] = {{L"1", "status=off reason=off"},
                                {L"abc", "status=refused reason=invalid_setting"},
                                {L"4", "status=refused reason=invalid_setting"},
                                {L"autoauto", "status=refused reason=too_long"}};
    for (const Refusal& r : refusals) {
        td::fixture_reset();
        set_env(L"X3M_TEXT_DENSITY", r.setting);
        td::initialize();
        check(row_has(r.row) && row_has("edition=fl"), "refusal row", last_row);
        check(!td::device_created(1.5, nullptr) && image_is(false) && !td::patched(), "refusal claims nothing");
    }
    td::fixture_reset();
    td::fixture_base(0); // the fixture executable is not X3FL.exe: fl::module_base() is 0
    set_env(L"X3M_TEXT_DENSITY", nullptr);
    td::initialize();
    check(row_has("status=refused reason=executable_mismatch") && row_has("setting=- "), "executable mismatch (unset = auto)", last_row);
    td::fixture_base(base);
    // Every window: one flipped byte refuses before anything is claimed, naming the window.
    for (const S::Window& w : S::windows) {
        unsigned at = 0;
        auto in_reloc = [&](unsigned k) {
            for (unsigned r = 0; r < w.reloc_count; ++r)
                if (k >= w.reloc_offset[r] && k < w.reloc_offset[r] + 4) return true;
            return false;
        };
        while (in_reloc(at) && at + 1 < w.length) ++at;
        image[w.rva + at] ^= 0x01;
        td::fixture_reset();
        set_env(L"X3M_TEXT_DENSITY", L"auto");
        td::initialize();
        char want[64];
        std::snprintf(want, sizeof want, "window=%s ", w.name);
        check(row_has("status=refused") && row_has(want) && row_has(in_reloc(at) ? "reason=slot_mismatch" : "reason=window_mismatch"),
              "window flip refused", last_row);
        check(!td::patched(), "window flip claims nothing", w.name);
        image[w.rva + at] ^= 0x01;
        if (w.reloc_count) {
            put32(w.rva + w.reloc_offset[0], uint32_t(base + w.reloc_target[0] + 0x10000));
            td::fixture_reset();
            td::initialize();
            check(row_has("reason=slot_mismatch") && row_has(want), "fixup at another base", last_row);
            put32(w.rva + w.reloc_offset[0], uint32_t(base + w.reloc_target[0]));
        }
    }
    check(image_is(false), "image pristine after the window refusals");
    // The order witness: the KC VM or the Materials rows already exist -> the crossing image write is refused and the
    // atomic claims go back.
    for (unsigned which = 0; which < 3; ++which) {
        if (which == 0) put32(S::vm_slot_rva, 0x0c0ffee0);
        if (which == 1) materials(true);
        if (which == 2) {
            const int16_t one = 1;
            std::memcpy(image + S::rows_count_rva, &one, 2);
        }
        td::fixture_reset();
        set_env(L"X3M_TEXT_DENSITY", L"auto");
        td::initialize();
        check(row_has("status=refused reason=late_window") && row_has("failed_site=text_density_blt_image_fl") && image_is(false) &&
                  !td::patched(),
              "late window refused", last_row);
        put32(S::vm_slot_rva, 0);
        materials(false);
    }

    // ---- vanilla reference rows (DF set where no callee runs) ----
    fakes(true);
    by_id[id_text] = new_object(64, 32, sites::generated_surface_flags | sites::surface_depth_32, 0x1000);
    by_id[id_text2] = new_object(32, 16, sites::generated_surface_flags | sites::surface_depth_32, 0x2000);
    by_id[id_static] = new_object(8, 4, sites::generated_surface_flags | sites::surface_depth_32, 0x3000);
    by_id[id_plain] = new_object(16, 16, sites::generated_surface_flags | sites::surface_depth_32, 0x4000);
    // Before the density (claims in, inactive): every thunk equals the vanilla span.
    set_env(L"X3M_DEBUG", L"1");
    check(init(L"auto"), "install auto", last_row);
    set_env(L"X3M_DEBUG", nullptr);
    check(row_has("writes=atomic,atomic,atomic,plain,atomic,atomic,atomic") && row_has("diagnostic=1") && row_has("edition=fl"), "claims row",
          last_row);
    check(claims_in(true), "claimed image");
    fakes(true);
    font_vector("inactive", tahoma, 13, 16, 1, 1);
    font_vector("inactive DF", tahoma, 13, 16, 1, 1, 0x0cd7);
    blit_pass("inactive", S::BltBlock, {id_static, id_text, 1, 1, 2, 2, 4, 2, 0x00ffffff, 0});
    blit_pass("inactive DF", S::BltAlpha, {id_static, id_text, 1, 1, 2, 2, 4, 2, 0x00ffffff, 1}, 0x0cd7);
    {
        // The image block before the density: the fast path to the tail (stopped after the displaced loads).
        unsigned char frame[0x80] = {};
        const uint32_t src = u32(by_id[id_static]->bytes), dst = u32(by_id[id_text]->bytes);
        std::memcpy(frame + 0x80 - 0x50, &src, 4);
        std::memcpy(frame + 0x80 - 0x20, &dst, 4);
        unsigned char record[0x30] = {};
        In in;
        in.esi = u32(record);
        in.ebp = u32(frame + 0x80);
        in.edx = 1;
        in.flags = 0x0cd7;
        const S::Claim& c = S::claims[S::BltImage];
        const Out patched = run_rva(c.rva, c.rva + c.length, in);
        unsigned char saved[8];
        std::memcpy(saved, image + c.rva, 8);
        std::memcpy(image + c.rva, pristine + c.rva, 8);
        FlushInstructionCache(GetCurrentProcess(), image + c.rva, 8);
        const Out vanilla = run_rva(c.rva, c.rva + c.length, in);
        std::memcpy(image + c.rva, saved, 8);
        FlushInstructionCache(GetCurrentProcess(), image + c.rva, 8);
        char detail[256];
        describe(patched, vanilla, detail, sizeof detail);
        check(all_equal(patched, vanilla), "inactive image block pass-through", detail);
    }
    td::shutdown();
    fakes(false);
    check(image_is(false), "image pristine before the auto install");

    // ---- auto at s = 1.5: d = 2 ----
    write_count = 0;
    set_env(L"X3M_DEBUG", L"1");
    check(init(L"auto"), "install auto", last_row);
    check(claims_in(true), "claimed image (debug)");
    vm(true); // CreateDevice runs after the init routine created the KC VM (0x077561 < 0x07765a)
    check(device(1.5), "device d=2");
    check(row_has("density=2 scale=1.5000 status=patched reason=ok mode=auto config_before=1 config=2 language=44 cockpit=patched style=stock") &&
              row_has("fonts=Tahoma26,Zekton52,ZektonES52,Harrier48") && row_has("rows=pending") && row_has("edition=fl"),
          "install row d=2", last_row);
    check(cfg(S::config_density_offset) == 2 && cfg(0x784) == 0x5a5a5a5a && cfg(S::config_language_offset) == 44 &&
              !std::memcmp(decoy_config + 0x774, "\x07\0\0\0", 4) && !std::memcmp(decoy_config + 0x784, "\x07\0\0\0", 4),
          "config +0x774 = 2, decoys at +0x784 and the AP slot untouched");
    check(image_is(true, 2, false) && claims_in(true), "cockpit 1a/02/20/1a, style stock");
    {
        bool order = write_count == 4;
        for (unsigned i = 0; i < write_count; ++i) order = order && writes[i].field == 2 && writes[i].rva == S::edits[i].rva;
        check(order, "the four cockpit bytes written after the field");
    }
    fakes(true);
    // Fonts: scaled where the pair exists, unchanged (one `missing` row) where it does not.
    font_vector("d2", tahoma, 13, 16, 1, 2);
    check(row_has("text_density_font name=Tahoma size=13 density=2 status=scaled file=f\\Tahoma26.abc cell_width=32 y_offset=2 edition=fl"),
          "font row", last_row);
    font_vector("d2 DF", zekton, 26, 22, 8, 2, 0x0cd7);
    font_vector("d2 missing", zekton, 30, 22, 8, 1);
    check(row_has("status=missing file=f\\Zekton60.abc"), "missing font row", last_row);
    // The Materials load, then its seam: the row edit, the decoy tables untouched.
    materials(true);
    materials_vector("d2");
    check(row_has("text_density_rows status=applied reason=ok density=2") && row_has("edition=fl"), "rows row", last_row);
    {
        unsigned flagged = 0, unflagged = 0, nofilter = 0;
        bool ok = true;
        for (int id = 0; id < rows_n; ++id) {
            const sites::RowPlan plan = sites::plan_row(row_flags_before[id], true);
            ok = ok && row_flag(id) == plan.flags && (entry_flag(id) & ~0x20000000u) == plan.flags && (entry_flag(id) & 0x20000000u);
            if (plan.edit == sites::RowEdit::flag) {
                ++flagged;
                if ((row_flags_before[id] ^ plan.flags) & sites::mpf_nofiltering) ++nofilter;
            } else if (plan.edit == sites::RowEdit::unflag) {
                ++unflagged;
            }
            uint32_t decoy = 0;
            std::memcpy(&decoy, decoy_rows + id * 0x3c + 0x10, 4);
            ok = ok && decoy == (0x5a000000u | uint32_t(id));
        }
        char want[96];
        std::snprintf(want, sizeof want, "flagged=%u unflagged=%u nofilter_cleared=%u", flagged, unflagged, nofilter);
        check(ok && row_has(want) && flagged >= 8 && unflagged >= 4, "rows edited per the plan, decoys untouched", want);
        check(row_flag(id_text) & sites::mpf_fontscale && !(row_flag(id_fontscale_file) & sites::mpf_fontscale) &&
                  !(row_flag(id_static) & sites::mpf_fontscale),
              "text row flagged, file-backed FONTSCALE row unflagged, static row untouched");
    }
    // The blits: a static source into a flagged destination from the 2x shadow; everything else passes through.
    {
        const int c1[6] = {2, 2, 4, 4, 8, 4};
        blit_handled("d2", S::BltBlock, {id_static, id_text, 1, 1, 2, 2, 4, 2, 0x00ffffff, 0}, 2, 1, c1);
        check(allocs == 1 && last_alloc_bpp == 32 && last_alloc_flags == S::generated_surface_flags, "shadow allocated 32 bpp 0x402c");
        check(any_row_since(row_count - 3, "text_density_shadow src=10 status=built reason=ok size=8x4 density=2", "edition=fl"), "shadow row",
              last_row);
        const int c2[6] = {0, 0, 0, 0, 16, 8};
        blit_handled("d2 plain", S::BltBlock, {id_static, id_text, 0, 0, 0, 0, 8, 4, -1, 0}, 2, 0, c2);
        const int c3[6] = {2, 0, 0, 2, 6, 6};
        blit_handled("d2 alpha clip", S::BltAlpha, {id_static, id_text, 0, 0, -1, 1, 4, 3, 0x7f0000ff, 1}, 2, 2, c3);
        check(allocs == 1, "one shadow per source id (cached)");
        // Clip model: the engine's destination clip in layout units (sites::clip_destination) against 64/2 x 32/2.
        int sx = 0, sy = 0, dx = 30, dy = 14, w = 4, h = 4;
        const bool draw = sites::clip_destination(32, 16, &sx, &sy, &dx, &dy, &w, &h);
        const int c4[6] = {sx * 2, sy * 2, dx * 2, dy * 2, w * 2, h * 2};
        check(draw && w == 2 && h == 2, "clip model at the far edge");
        blit_handled("d2 clip far", S::BltBlock, {id_static, id_text, 0, 0, 30, 14, 4, 4, 5, 1}, 2, 1, c4);
        // Pass-through: flagged source, unflagged destination, generated source, a named atlas source, negative ids.
        blit_pass("d2", S::BltBlock, {id_text2, id_text, 0, 0, 0, 0, 4, 4, 1, 0});
        blit_pass("d2", S::BltBlock, {id_static, id_plain, 0, 0, 0, 0, 4, 4, 1, 0}, 0x0cd7);
        blit_pass("d2", S::BltAlpha, {9, id_text, 0, 0, 0, 0, 4, 4, 1, 0});
        blit_pass("d2", S::BltAlpha, {-3, id_text, 0, 0, 0, 0, 4, 4, 1, 0});
        blit_pass("d2", S::BltBlock, {id_static, -1, 0, 0, 0, 0, 4, 4, 1, 0});
        blit_pass("d2", S::BltBlock, {id_named + 9, id_text, 0, 0, 0, 0, 4, 4, 1, 0});
        // The FL blit recorder on: the blit is left to the engine (its recording stays complete).
        static unsigned char recorder[0x40] = {};
        recorder[S::recorder_flags_offset] = 1;
        put32(S::recorder_slot_rva, u32(recorder));
        blit_pass("d2 recording", S::BltBlock, {id_static, id_text, 1, 1, 2, 2, 4, 2, 0x00ffffff, 0});
        blit_pass("d2 recording", S::BltAlpha, {id_static, id_text, 1, 1, 2, 2, 4, 2, 0x00ffffff, 0}, 0x0cd7);
        recorder[S::recorder_flags_offset] = 0;
        td::present(77);
        check(row_has("text_density_recorder first_pass frame=77 recorded_passes=2 edition=fl"), "recorder row at the next Present", last_row);
        td::present(78);
        check(!row_has("frame=78"), "recorder row once");
        const int c5[6] = {2, 2, 4, 4, 8, 4};
        blit_handled("d2 recorder idle", S::BltBlock, {id_static, id_text, 1, 1, 2, 2, 4, 2, 0x00ffffff, 0}, 2, 1, c5);
        put32(S::recorder_slot_rva, 0);
    }
    // KC's image block: N = 1 (unflagged destination) runs the unscaled, clamped copy itself; N = d with a 32-bit image
    // draws from the image's d x shadow; a null image or a factor that is neither 1 nor d leaves the engine's block.
    {
        static unsigned char frame[0x80];
        static unsigned char record[0x30];
        static char path[] = "textures/kc/icon.dds";
        put32(S::image_path_slot_rva, u32(path));
        auto set_record = [&](int32_t dst, const uint16_t words[6]) {
            std::memset(record, 0xee, sizeof record);
            std::memcpy(record + 1, &dst, 4);
            for (unsigned i = 0; i < 6; ++i) std::memcpy(record + 0xb + 5 * i, &words[i], 2);
        };
        auto set_frame = [&](Object* src, Object* dst) {
            std::memset(frame, 0, sizeof frame);
            const uint32_t s = src ? u32(src->bytes) : 0, d = dst ? u32(dst->bytes) : 0;
            std::memcpy(frame + 0x80 - 0x50, &s, 4);
            std::memcpy(frame + 0x80 - 0x20, &d, 4);
        };
        Object* icon = new_object(6, 5, sites::generated_surface_flags | sites::surface_depth_32, 0x5000);
        const uint16_t words[6] = {1, 1, 3, 2, 4, 3};
        // N = 1: vanilla (the engine's block into the fake copy) and patched agree on the copy's arguments.
        set_record(id_plain, words);
        set_frame(icon, by_id[id_plain]);
        In in;
        in.esi = u32(record);
        in.ebp = u32(frame + 0x80);
        in.edx = 1;
        const S::Claim& c = S::claims[S::BltImage];
        const Out ref = reference(in);
        unsigned before = call_count;
        const Out patched = run_rva(c.rva, S::image_return_rva, in);
        const Call handled = calls[call_count - 1];
        char detail[256];
        describe(patched, ref, detail, sizeof detail);
        check(call_count == before + 1 && patched.esp_delta == 0x20 && patched.ebx == in.ebx && patched.esi == in.esi &&
                  patched.edi == in.edi && patched.ebp == in.ebp && (patched.flags & flag_mask) == (in.flags & flag_mask) &&
                  state_equal(patched, ref),
              "image N=1 handled: the skip path's registers and state", detail);
        unsigned char saved[8];
        std::memcpy(saved, image + c.rva, 8);
        std::memcpy(image + c.rva, pristine + c.rva, 8);
        before = call_count;
        const Out vanilla = run_rva(c.rva, S::image_return_rva, in);
        std::memcpy(image + c.rva, saved, 8);
        FlushInstructionCache(GetCurrentProcess(), image + c.rva, 8);
        const Call native = calls[call_count - 1];
        check(call_count == before + 1 && vanilla.esp_delta == 0x20 && native.src == handled.src && native.dst == handled.dst &&
                  native.sx == handled.sx && native.sy == handled.sy && native.dx == handled.dx && native.dy == handled.dy &&
                  native.w == handled.w && native.h == handled.h && handled.src == icon->bytes,
              "image N=1: the engine's own copy arguments");
        // N = d into a flagged row: the image's 2x shadow, every coordinate x2, clamped.
        set_record(id_text, words);
        set_frame(icon, by_id[id_text]);
        in.edx = 2;
        before = call_count;
        const unsigned allocs_before = allocs;
        run_rva(c.rva, S::image_return_rva, in);
        const Call scaled = calls[call_count - 1];
        Object* shadow = from(scaled.src);
        check(call_count == before + 1 && allocs == allocs_before + 1 && shadow && shadow != icon && width(shadow) == 12 &&
                  height(shadow) == 10 && scaled.sx == 2 && scaled.sy == 2 && scaled.dx == 6 && scaled.dy == 4 && scaled.w == 8 &&
                  scaled.h == 6 && oob == 0,
              "image N=d: the 2x shadow, coordinates x2");
        check(any_row_since(row_count > 3 ? row_count - 3 : 0, "image=textures/kc/icon.dds", "status=built"), "image shadow row", last_row);
        run_rva(c.rva, S::image_return_rva, in);
        check(allocs == allocs_before + 1, "image shadow reused for the same path");
        // An oversized record: clamped to both surfaces, never out of bounds.
        const uint16_t big[6] = {4, 3, 30, 14, 40, 40};
        set_record(id_text, big);
        run_rva(c.rva, S::image_return_rva, in);
        int raw[6] = {4, 3, 30, 14, 40, 40};
        const sites::ImagePlan plan = sites::plan_image_blit(2, 2, true, 6, 5, 64, 32, raw);
        const Call clamped = calls[call_count - 1];
        check(oob == 0 && plan.draw && clamped.w == plan.w && clamped.h == plan.h && clamped.dx == plan.dx, "image clamp model");
        // Pass-through: no image object, or a factor that is neither 1 nor d.
        set_frame(nullptr, by_id[id_text]);
        in.edx = 2;
        in.flags = 0x0cd7;
        const Out p1 = run_rva(c.rva, c.rva + c.length, in);
        std::memcpy(image + c.rva, pristine + c.rva, 8);
        const Out v1 = run_rva(c.rva, c.rva + c.length, in);
        std::memcpy(image + c.rva, saved, 8);
        FlushInstructionCache(GetCurrentProcess(), image + c.rva, 8);
        describe(p1, v1, detail, sizeof detail);
        check(all_equal(p1, v1), "image null object pass-through", detail);
        set_frame(icon, by_id[id_text]);
        in.edx = 5;
        const Out p2 = run_rva(c.rva, c.rva + c.length, in);
        std::memcpy(image + c.rva, pristine + c.rva, 8);
        const Out v2 = run_rva(c.rva, c.rva + c.length, in);
        std::memcpy(image + c.rva, saved, 8);
        FlushInstructionCache(GetCurrentProcess(), image + c.rva, 8);
        describe(p2, v2, detail, sizeof detail);
        check(all_equal(p2, v2), "image factor 5 pass-through", detail);
    }
    // The diagnostic entries (debug): one row per (font, dst), the engine's state untouched.
    {
        for (unsigned which : {unsigned(S::TextLine), unsigned(S::RectFill)}) {
            const S::Claim& c = S::claims[which];
            In in;
            in.ecx = id_text;
            in.edx = 0x1234;
            in.flags = 0x0cd7;
            in.stack[0] = 0xabcdef01;
            in.stack[1] = 2; // font slot
            in.stack[2] = 17; // x
            const unsigned since = row_count;
            const Out patched = run_rva(c.rva, c.rva + c.length, in);
            unsigned char saved[8];
            std::memcpy(saved, image + c.rva, 8);
            std::memcpy(image + c.rva, pristine + c.rva, 8);
            const Out vanilla = run_rva(c.rva, c.rva + c.length, in);
            std::memcpy(image + c.rva, saved, 8);
            FlushInstructionCache(GetCurrentProcess(), image + c.rva, 8);
            char detail[256];
            describe(patched, vanilla, detail, sizeof detail);
            const unsigned locals = which == S::TextLine ? 5 : 11;
            check(all_equal(patched, vanilla, 3, locals) && patched.stack[3 + locals] == in.stack[0] &&
                      patched.stack[4 + locals] == in.stack[1],
                  which == S::TextLine ? "text line state" : "rect fill state", detail);
            check(which == S::TextLine ? any_row_since(since, "text_density_draw fn=text_line font=2 x=17 dst=3 dst_flagged=1", "edition=fl")
                                       : any_row_since(since, "text_density_draw fn=rect_fill src=-1 dst=3 dst_flagged=1", "edition=fl"),
                  "diagnostic row", last_row);
        }
    }
    // The on-screen filter (s != d): the flagged rows' textures from the table's objects (+0x34), LINEAR on a draw that
    // samples one, the previous values back after it.
    {
        const uint32_t object = u32(by_id[id_text]->bytes);
        std::memcpy(table_mem + id_text * 0x10 + 8, &object, 4);
        td::present(300);
        check(td::filter_hooks_wanted() && row_has("text_density_filter_frame frame=300 textures=1") && row_has("edition=fl"), "filter set",
              last_row);
        sampler[D3DSAMP_MINFILTER] = 1;
        sampler[D3DSAMP_MAGFILTER] = 1;
        uint32_t texture = 0;
        std::memcpy(&texture, by_id[id_text]->bytes + S::object_texture_offset, 4);
        td::bound_texture(0, reinterpret_cast<IDirect3DBaseTexture9*>(std::uintptr_t(texture)));
        IDirect3DDevice9* dev = fake_device();
        const bool raised = td::draw_filter_override(dev, sampler_get, sampler_set);
        check(raised && sampler[D3DSAMP_MINFILTER] == 2 && sampler[D3DSAMP_MAGFILTER] == 2 && row_has("raised=both") && row_has("edition=fl"),
              "filter raised", last_row);
        td::draw_filter_restore(dev, sampler_set);
        check(sampler[D3DSAMP_MINFILTER] == 1 && sampler[D3DSAMP_MAGFILTER] == 1, "filter restored");
        td::bound_texture(0, nullptr);
        check(!td::draw_filter_override(dev, sampler_get, sampler_set), "other texture untouched");
        std::memset(table_mem + id_text * 0x10 + 8, 0, 4);
    }
    // A Reset frees the shadows through the engine's free.
    {
        const unsigned before = frees;
        check(keeps_error([] { td::before_reset(); }), "before_reset LastError");
        check(frees == before + 2 && row_has("text_density_reset shadows=2") && row_has("edition=fl"), "reset frees the shadows", last_row);
    }
    // Shutdown: the cockpit bytes, then the field; every claim; image pristine.
    write_count = 0;
    fakes(false);
    check(keeps_error([] { td::shutdown(); }), "shutdown LastError");
    check(row_has("text_density_restore status=restored registered=0 image_claim=retained_inert edition=fl") && !td::patched() &&
              image_is(false, 1, false, true) && cfg(S::config_density_offset) == 1,
          "shutdown restores; the image claim stays in place, inert, after the VM and the rows exist", last_row);
    {
        bool order = write_count == 4;
        for (unsigned i = 0; i < write_count; ++i) order = order && writes[i].field == 2;
        check(order, "the cockpit bytes restored before the field");
    }
    release();
    check(row_has("image_claim=none") && image_is(false), "the image claim goes back once the order witness holds", last_row);
    set_env(L"X3M_DEBUG", nullptr);
    materials(false);
    lay_tables();

    // ---- fixed 3: the style byte after the field, restored before it ----
    write_count = 0;
    check(init(L"3"), "install fixed 3", last_row);
    check(claims_in(false), "claimed image (no diagnostics)");
    vm(true);
    check(device(1.25), "device d=3");
    check(row_has("density=3 scale=1.2500 status=patched reason=ok mode=fixed config_before=1 config=3") && row_has("cockpit=patched style=patched") &&
              row_has("fonts=Tahoma39,Zekton78,ZektonES78,Harrier72"),
          "install row d=3", last_row);
    check(image_is(true, 3, true) && cfg(S::config_density_offset) == 3, "cockpit 27/03/30/27, style 02");
    {
        bool order = write_count == 5 && writes[4].rva == S::edits[S::Style].rva && writes[4].value == 0x02;
        for (unsigned i = 0; i < write_count; ++i) order = order && writes[i].field == 3;
        check(order, "style byte written while the field already holds 3");
    }
    fakes(true);
    font_vector("d3", tahoma, 13, 16, 1, 3);
    materials(true);
    materials_vector("d3");
    check(row_has("status=applied") && row_has("density=3"), "rows d=3", last_row);
    write_count = 0;
    fakes(false);
    td::shutdown();
    {
        bool order = write_count == 5 && writes[0].rva == S::edits[S::Style].rva && writes[0].value == 0x01 && writes[0].field == 3;
        for (unsigned i = 0; i < write_count; ++i) order = order && writes[i].field == 3;
        check(order && image_is(false, 1, false, true) && cfg(S::config_density_offset) == 1,
              "style byte restored first, while the field is still 3");
    }
    release();
    materials(false);
    lay_tables();

    // ---- texture limits lower 3 to 2 at the Materials seam: the style byte goes first, the cockpit bytes follow ----
    write_count = 0;
    check(init(L"3"), "install fixed 3 (caps)", last_row);
    caps_w = 4096;
    caps_h = 4096; // the 64x2048 row 8 is 6144 high at d = 3
    vm(true);
    check(device(1.25, fake_device()), "device d=3 with caps");
    check(row_has("max_texture=4096x4096"), "caps read", last_row);
    write_count = 0;
    materials(true);
    fakes(true);
    materials_vector("caps");
    check(row_has("status=applied") && row_has("density=2") && row_has("caps_limited_d=2") && row_has("largest_row=256x2048"), "caps row",
          last_row);
    {
        bool order = write_count == 5 && writes[0].rva == S::edits[S::Style].rva && writes[0].value == 0x01 && writes[0].field == 3;
        for (unsigned i = 1; i < write_count; ++i) order = order && writes[i].field == 2 && writes[i].value == S::edits[i - 1].d2;
        fakes(false);
        check(order && image_is(true, 2, false) && cfg(S::config_density_offset) == 2, "caps: style back at 3, field 2, cockpit d2");
    }
    td::shutdown();
    check(image_is(false, 1, false, true) && cfg(S::config_density_offset) == 1, "caps: restored (image claim inert)");
    release();
    lay_tables();

    // ---- the same fall from 3 to 2 with the style byte refusing to go back twice (an unreadable page): the field stays 3
    // (factor 3, as the byte says), the cockpit bytes and claims go back; shutdown later restores the byte, then the field ----
    check(init(L"3"), "install fixed 3 (style double failure)", last_row);
    vm(true);
    check(device(1.25, fake_device()), "device d=3 with caps (double failure)");
    materials(true);
    fakes(true);
    faults_taken = 0;
    fail_read_at = base + S::edits[S::Style].rva;
    fail_read_count = 1;
    fail_read_times = 2;
    materials_vector("style double failure");
    fakes(false);
    check(faults_taken == 2 && row_has("status=refused reason=style_failed") && row_has("image_claim=retained_inert"), "double failure row",
          last_row);
    check(cfg(S::config_density_offset) == 3 && image[S::edits[S::Style].rva] == 0x02 && td::patched(),
          "the field stays 3 while the style byte is still ours");
    {
        bool cockpit = true;
        for (unsigned e = 0; e < S::cockpit_edits; ++e) cockpit = cockpit && image[S::edits[e].rva] == S::edits[e].original;
        check(cockpit && image[S::claims[S::Font].rva] == pristine[S::claims[S::Font].rva] &&
                  image[S::materials_call_rva + 1] == pristine[S::materials_call_rva + 1],
              "cockpit bytes and claims back");
    }
    write_count = 0;
    td::shutdown();
    check(write_count == 1 && writes[0].rva == S::edits[S::Style].rva && writes[0].value == 0x01 && writes[0].field == 3 &&
              cfg(S::config_density_offset) == 1 && image_is(false, 1, false, true) && !td::patched(),
          "shutdown: the style byte, then the field");
    release();
    caps_w = caps_h = 0;
    lay_tables();

    // ---- refusals at the device ----
    {
        font_file("Zekton52.tga", false);
        check(init(L"auto"), "install (missing font)");
        vm(true);
        check(!device(1.5) && row_has("status=refused reason=fonts_missing") && row_has("missing=Zekton52") && row_has("config=1 ") &&
                  row_has("image_claim=retained_inert") && image_is(false, 1, false, true) && !td::patched(),
              "missing font refused", last_row);
        font_file("Zekton52.tga", true);
        // -L081 without UmePlusCLP46: refused; with the pair: applied.
        set_cfg(S::config_language_offset, 81);
        check(init(L"auto"), "install (-L081)");
        vm(true);
        check(!device(1.5) && row_has("status=refused reason=umeplusclp_missing") && row_has("missing=UmePlusCLP46") && row_has("language=81") &&
                  image_is(false, 1, false, true) && !td::patched(),
              "-L081 refused", last_row);
        font_file("UmePlusCLP46.abc", true);
        font_file("UmePlusCLP46.tga", true);
        check(init(L"auto"), "install (-L081 with UmePlusCLP46)");
        check(device(1.5) && row_has("status=patched") && row_has("language=81"), "-L081 with the pair applied", last_row);
        td::shutdown();
        font_file("UmePlusCLP46.abc", false);
        font_file("UmePlusCLP46.tga", false);
        set_cfg(S::config_language_offset, 44);
        // auto at s = 1: nothing changes, the claims go back.
        check(init(L"auto"), "install (s=1)");
        vm(true); // the default launch (auto, no UI scale): the image claim stays in place, inert
        check(!device(1.0) && row_has("status=off reason=density_1") && row_has("image_claim=retained_inert") &&
                  image_is(false, 1, false, true) && !td::patched(),
              "density 1 off", last_row);
        release();
    }

    // ---- all or none: read-back and chain faults in the claims group ----
    {
        struct Fault {
            unsigned claim; // claim_count = the Materials call
            int kind;       // 0 read-back, 1 chain
            const char* reason;
            const char* site;
        };
        const Fault faults[] = {{S::Font, 0, "reason=readback_failed ", "failed_site=text_density_font_fl "},
                                {S::BltAlpha, 1, "reason=chain_failed ", "failed_site=text_density_blt_alpha_fl "},
                                {S::claim_count, 0, "reason=readback_failed ", "failed_site=text_density_materials_fl "},
                                {S::BltImage, 0, "reason=readback_failed ", "failed_site=text_density_blt_image_fl "},
                                {S::BltImage, 1, "reason=chain_failed ", "failed_site=text_density_blt_image_fl "},
                                {S::RectFill, 0, "reason=readback_failed ", "failed_site=text_density_rect_fill_fl "}};
        set_env(L"X3M_DEBUG", L"1");
        for (const Fault& f : faults) {
            faults_taken = 0;
            const uint32_t rva = f.claim == S::claim_count ? S::materials_call_rva : S::claims[f.claim].rva;
            if (f.kind == 0) {
                fault_read_at = base + rva;
                fault_read_count = f.claim == S::claim_count ? 5 : S::claims[f.claim].length;
                fault_read_skip = 0;
            } else {
                fault_push_at = base + rva;
            }
            init(L"auto");
            check(faults_taken == 1 && row_has("status=refused") && row_has(f.reason) && row_has(f.site) && !td::patched() && image_is(false),
                  "claims rolled back", last_row);
        }
        // A rollback whose restore fails keeps that claim registered; shutdown takes it back later.
        faults_taken = 0;
        fault_push_at = base + S::claims[S::BltImage].rva;
        fault_restore_at = base + S::claims[S::Font].rva;
        init(L"auto");
        check(faults_taken == 2 && row_has("status=refused reason=chain_failed") && td::patched() && image[S::claims[S::Font].rva] == 0xe9,
              "failed restore stays registered", last_row);
        td::shutdown();
        check(!td::patched() && image_is(false) && row_has("status=restored"), "registered claim restored at shutdown", last_row);
        set_env(L"X3M_DEBUG", nullptr);
    }
    // ---- all or none: read-back faults in the edits group (field, a cockpit byte, the style byte) ----
    {
        struct Fault {
            std::uintptr_t at;
            unsigned count, skip;
            const wchar_t* setting;
            const char* reason;
            const char* site;
        };
        const Fault faults[] = {
            {u32(config) + S::config_density_offset, 4, 0, L"auto", "reason=config_unwritable ", "edit_failed=- "},
            {base + S::edits[S::CockpitNameSize].rva, 1, 0, L"auto", "reason=cockpit_failed ", "edit_failed=cockpit_name_size "},
            {base + S::edits[S::CockpitCellWidth].rva, 1, 1, L"auto", "reason=cockpit_readback_failed ", "edit_failed=cockpit_cell_width "},
            {base + S::edits[S::CockpitSize].rva, 1, 1, L"3", "reason=cockpit_readback_failed ", "edit_failed=cockpit_size "},
            {base + S::edits[S::Style].rva, 1, 1, L"3", "reason=style_failed ", "edit_failed=style_factor "}};
        for (const Fault& f : faults) {
            faults_taken = 0;
            init(f.setting);
            fault_read_at = f.at;
            fault_read_count = f.count;
            fault_read_skip = f.skip;
            write_count = 0;
            check(!device(1.5), "edit fault refused");
            check(faults_taken == 1 && row_has("status=refused") && row_has(f.reason) && row_has(f.site) && !td::patched() &&
                      image_is(false) && cfg(S::config_density_offset) == 1,
                  "edits rolled back with the claims", last_row);
            // The style byte (when written) went back while the field still held 3.
            for (unsigned i = 0; i < write_count; ++i)
                if (writes[i].rva == S::edits[S::Style].rva) check(writes[i].field == 3, "style restore order under a fault");
            fault_read_at = 0;
        }
    }
    check(image_is(false), "image pristine at the end");
    std::printf("RESULT checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
