// Native fixture of the FL oversized-disc port (src/engine/fl/circle_fill_fl.cpp, circle_fill_sites_fl.h;
// docs/reverse-engineering/fl/circle-fill.md, "Port (2026-11-03)"). Built with -DX3M_EDITION_FL=1
// -DX3M_CIRCLE_FILL_FL_FIXTURE by verification/analysis/test_fl_circle_fill.py (the module force-includes
// fl_circle_fill_shim.h for the read-back / chain / restore faults) and run natively (WSL interop). No game launch.
//
// argv[1] is X3FL.exe (read only). Its sections are mapped into a 64 KB-aligned private image at `base` (not 0x400000)
// with every HIGHLOW relocation applied, so the real rasterizer, class function, lock, class-0 leaf and CRT fill run
// relocated. Fakes stand in for the rest: a COM backing object (vtable +0x30 GetDesc, +0x34 LockRect, scripted
// failures), the recovery callback slot base+0x1aae24, recording leaves (`push imm32; ret` at the five leaf entries,
// after the install checked their bytes), a counting wrapper around a copy of the real class function, and for one
// test a class function answering an unsupported class plus a recording diagnostic. The harness enters the routine with
// the FL convention (ECX = surface, EDX = cx, four caller-cleaned stack words), sentinels in EAX/EBX/ESI/EDI/EBP, DF,
// XMM7, the x87 control word, MXCSR and LastError set, at four incoming stack alignments.
//
// Output: one `RUN kind=native|patched ...` line per call (records = ordered leaf calls, hash = FNV-1a 64 over class
// and the three signed words), checked by the Python test against its own model of the instruction stream; `FAIL`
// lines and a final `RESULT PASS|FAIL checks=N failures=M`.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../../src/engine/fl/circle_fill_fl.h"
#include "../../src/engine/fl/circle_fill_sites_fl.h"
#include "../../src/proxy/engine_patch.h"
namespace cf = x3m::circle_fill;
namespace F = x3m::fl::circle_fill_fl;
namespace engine_patch = x3m::engine_patch;
using std::int32_t;
using std::uint32_t;
using std::uint64_t;

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
    SetLastError(0x0badf00d); // the module must put LastError back
    std::printf("LOG %s\n", last_row);
}
}
// ---- the seam (fl_circle_fill_shim.h): one-shot faults, otherwise the real calls ----
std::uintptr_t fault_read_at = 0;
unsigned fault_read_count = 0;
std::uintptr_t fault_restore_at = 0, fault_push_at = 0;
unsigned faults_taken = 0;
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count) {
    const bool ok = read_code(address, out, count);
    if (ok && fault_read_at && address == fault_read_at && count == fault_read_count) {
        fault_read_at = 0;
        ++faults_taken;
        return false;
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

// ---- the harness ----
extern "C" {
uint32_t in_eax, in_ebx, in_ecx, in_edx, in_esi, in_edi, in_ebp, in_df, in_pad;
uint32_t in_cy, in_r, in_colour, in_aspect;
uint32_t call_target, saved_esp, esp_before;
uint32_t out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, out_esp, out_flags, out_args[4];
alignas(16) unsigned char in_xmm[16], out_xmm[16];
uint16_t in_fcw = 0x027f, out_fcw = 0, def_fcw = 0;
uint32_t in_mxcsr = 0x1f80, out_mxcsr = 0, def_mxcsr = 0;
uint32_t class_calls = 0, diag_calls = 0, diag_ecx = 0, diag_arg = 0, class_copy = 0, fake_class_value = 0,
         fake_class_ecx = 0;
void fl_cf_call();
void fl_cf_class_counter();
void fl_cf_class_fake();
void fl_cf_diag_fake();
}
asm(".text\n\t"
    ".globl _fl_cf_call\n_fl_cf_call:\n\t"
    "pushal\n\tpushfl\n\tmovl %esp,_saved_esp\n\tfnstcw _def_fcw\n\tstmxcsr _def_mxcsr\n\t"
    "andl $-16,%esp\n\tsubl _in_pad,%esp\n\t"
    "pushl _in_aspect\n\tpushl _in_colour\n\tpushl _in_r\n\tpushl _in_cy\n\t"
    "movl %esp,_esp_before\n\t"
    "movdqu _in_xmm,%xmm7\n\tfldcw _in_fcw\n\tldmxcsr _in_mxcsr\n\t"
    "movl _in_eax,%eax\n\tmovl _in_ebx,%ebx\n\tmovl _in_ecx,%ecx\n\tmovl _in_edx,%edx\n\t"
    "movl _in_esi,%esi\n\tmovl _in_edi,%edi\n\tmovl _in_ebp,%ebp\n\t"
    "cld\n\tcmpl $0,_in_df\n\tje 1f\n\tstd\n"
    "1:\n\tcall *_call_target\n\t"
    "pushfl\n\tpopl _out_flags\n\tcld\n\t"
    "movl %eax,_out_eax\n\tmovl %ebx,_out_ebx\n\tmovl %ecx,_out_ecx\n\tmovl %edx,_out_edx\n\t"
    "movl %esi,_out_esi\n\tmovl %edi,_out_edi\n\tmovl %ebp,_out_ebp\n\tmovl %esp,_out_esp\n\t"
    "movdqu %xmm7,_out_xmm\n\tfnstcw _out_fcw\n\tstmxcsr _out_mxcsr\n\tfldcw _def_fcw\n\tldmxcsr _def_mxcsr\n\t"
    "movl (%esp),%eax\n\tmovl %eax,_out_args\n\tmovl 4(%esp),%eax\n\tmovl %eax,_out_args+4\n\t"
    "movl 8(%esp),%eax\n\tmovl %eax,_out_args+8\n\tmovl 12(%esp),%eax\n\tmovl %eax,_out_args+12\n\t"
    "movl _saved_esp,%esp\n\tpopfl\n\tpopal\n\tret\n\t"
    // A counting wrapper around the copy of the real class function (ECX/EDX pass through).
    ".globl _fl_cf_class_counter\n_fl_cf_class_counter:\n\tincl _class_calls\n\tjmp *_class_copy\n\t"
    // An unsupported class: AX = the value, ECX = a marker (the native diagnostic call keeps the class call's ECX).
    ".globl _fl_cf_class_fake\n_fl_cf_class_fake:\n\tincl _class_calls\n\tmovl _fake_class_ecx,%ecx\n\t"
    "movl _fake_class_value,%eax\n\tret\n\t"
    // The diagnostic 0x075110: records ECX and its one stack argument; the caller pops it.
    ".globl _fl_cf_diag_fake\n_fl_cf_diag_fake:\n\tincl _diag_calls\n\tmovl %ecx,_diag_ecx\n\t"
    "movl 4(%esp),%eax\n\tmovl %eax,_diag_arg\n\tret\n\t");

// ---- the image ----
std::uintptr_t base = 0;
unsigned char* image = nullptr;
unsigned char* pristine = nullptr;
uint32_t image_size = 0, preferred_base = 0;
uint32_t u32(const void* p) {
    return uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
uint32_t get32(uint32_t rva) {
    uint32_t v;
    std::memcpy(&v, image + rva, 4);
    return v;
}
void put32(uint32_t rva, uint32_t v) {
    std::memcpy(image + rva, &v, 4);
}
void flush() {
    FlushInstructionCache(GetCurrentProcess(), image, image_size);
}
bool map_image(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    unsigned char* file = static_cast<unsigned char*>(std::malloc(size_t(size)));
    const bool read = file && std::fread(file, 1, size_t(size), f) == size_t(size);
    std::fclose(f);
    if (!read) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file + dos->e_lfanew);
    image_size = nt->OptionalHeader.SizeOfImage;
    preferred_base = nt->OptionalHeader.ImageBase;
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    pristine = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!image || !pristine) return false;
    base = reinterpret_cast<std::uintptr_t>(image);
    std::memcpy(image, file, nt->OptionalHeader.SizeOfHeaders);
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const uint32_t n = section[i].SizeOfRawData < section[i].Misc.VirtualSize || !section[i].Misc.VirtualSize
                               ? section[i].SizeOfRawData
                               : section[i].Misc.VirtualSize;
        std::memcpy(image + section[i].VirtualAddress, file + section[i].PointerToRawData, n);
    }
    const IMAGE_DATA_DIRECTORY reloc = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    const uint32_t delta = uint32_t(base) - preferred_base;
    for (uint32_t o = reloc.VirtualAddress; o < reloc.VirtualAddress + reloc.Size;) {
        const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(image + o);
        if (block->SizeOfBlock < 8) break;
        const auto* entries = reinterpret_cast<const std::uint16_t*>(block + 1);
        for (unsigned k = 0; k < (block->SizeOfBlock - 8) / 2; ++k)
            if (entries[k] >> 12 == IMAGE_REL_BASED_HIGHLOW)
                put32(block->VirtualAddress + (entries[k] & 0xfff), get32(block->VirtualAddress + (entries[k] & 0xfff)) + delta);
        o += block->SizeOfBlock;
    }
    std::free(file);
    std::memcpy(pristine, image, image_size);
    flush();
    return true;
}
void restore_bytes(uint32_t rva, unsigned n) {
    std::memcpy(image + rva, pristine + rva, n);
}
void jump_at(uint32_t rva, const void* to) { // push imm32; ret
    image[rva] = 0x68;
    put32(rva + 1, u32(to));
    image[rva + 5] = 0xc3;
}

// ---- fakes ----
constexpr uint32_t sentinel_error = 0x2a2a1357;
constexpr uint32_t hr_outofmemory = 0x8007000e, hr_fail = 0x80004005;
unsigned getdesc_calls = 0, lockrect_calls = 0, recovery_calls = 0, guard_seen = 0, df_seen = 0, leaf_calls = 0;
unsigned lock_fail_left = 0;
uint32_t lock_fail_hr = hr_outofmemory, lock_flags_seen = 0;
uint32_t locked_bits = 0;
unsigned mutate_at = 0; // the leaf call (1-based) after which the surface dimensions change
int mutate_w = 0, mutate_h = 0;
bool recovery_mutates = false;
alignas(16) unsigned char surface[0x64];
unsigned char dummy_pixels[64];
uint32_t fake_cfg[16];
uint32_t df_now() {
    uint32_t f;
    __asm__ __volatile__("pushfl\n\tpopl %0" : "=r"(f));
    return f & 0x400;
}
void set16(unsigned off, int v) {
    const std::int16_t w = static_cast<std::int16_t>(v);
    std::memcpy(surface + off, &w, 2);
}
uint32_t sget32(unsigned off) {
    uint32_t v;
    std::memcpy(&v, surface + off, 4);
    return v;
}
void sput32(unsigned off, uint32_t v) {
    std::memcpy(surface + off, &v, 4);
}
long __stdcall fake_get_desc(void*, uint32_t* desc) {
    ++getdesc_calls;
    if (df_now()) ++df_seen;
    volatile uint32_t* d = desc;
    for (unsigned i = 0; i < 8; ++i) d[i] = 0; // Usage 0: never the render-target early-out
    return 0;
}
long __stdcall fake_lock_rect(void*, uint32_t* lr, const void*, uint32_t flags) {
    ++lockrect_calls;
    if (df_now()) ++df_seen;
    lock_flags_seen = flags;
    if (lock_fail_left) {
        --lock_fail_left;
        return long(lock_fail_hr);
    }
    volatile uint32_t* l = lr;
    l[0] = 0x480;
    l[1] = locked_bits;
    return 0;
}
void* fake_vtable[32];
struct FakeBacking {
    void** vtbl;
} backing{fake_vtable};
void __cdecl fake_recovery() {
    ++recovery_calls;
    if (get32(0x22de10) == 1) ++guard_seen;
    if (recovery_mutates) {
        set16(F::object_width, mutate_w);
        set16(F::object_height, mutate_h);
    }
}
// ---- recording leaves ----
struct Record {
    unsigned char cls;
    std::int16_t x0, y, x1;
};
constexpr unsigned record_capacity = 400000;
Record records[record_capacity];
unsigned record_count = 0, bad_colour = 0, bad_surface = 0, overflow = 0;
uint32_t expect_colour = 0;
void record(unsigned char cls, void* s, int32_t x0, int32_t y, int32_t x1, uint32_t colour) {
    if (df_now()) ++df_seen;
    if (s != surface) ++bad_surface;
    if (colour != expect_colour) ++bad_colour;
    if (record_count < record_capacity) {
        Record& r = records[record_count];
        r.cls = cls;
        r.x0 = static_cast<std::int16_t>(x0);
        r.y = static_cast<std::int16_t>(y);
        r.x1 = static_cast<std::int16_t>(x1);
    } else {
        ++overflow;
    }
    ++record_count;
    if (mutate_at && ++leaf_calls == mutate_at) {
        set16(F::object_width, mutate_w);
        set16(F::object_height, mutate_h);
    }
}
void __cdecl leaf0(void* s, int32_t a, int32_t b, int32_t c, uint32_t d) { record(0, s, a, b, c, d); }
void __cdecl leaf5(void* s, int32_t a, int32_t b, int32_t c, uint32_t d) { record(5, s, a, b, c, d); }
void __cdecl leaf10(void* s, int32_t a, int32_t b, int32_t c, uint32_t d) { record(10, s, a, b, c, d); }
void __cdecl leaf15(void* s, int32_t a, int32_t b, int32_t c, uint32_t d) { record(15, s, a, b, c, d); }
void __cdecl leaf24(void* s, int32_t a, int32_t b, int32_t c, uint32_t d) { record(24, s, a, b, c, d); }
unsigned char* class_copy_code = nullptr;
// The recording leaves, the counting class wrapper; real = the pristine bytes (the install checks them).
void fakes(bool on) {
    const uint32_t leaves[5] = {F::leaf_class0_rva, F::leaf_class5_rva, F::leaf_class10_rva, F::leaf_class15_rva,
                                F::leaf_class24_rva};
    void (*const fns[5])(void*, int32_t, int32_t, int32_t, uint32_t) = {leaf0, leaf5, leaf10, leaf15, leaf24};
    for (unsigned i = 0; i < 5; ++i) {
        if (on) jump_at(leaves[i], reinterpret_cast<const void*>(fns[i]));
        else restore_bytes(leaves[i], 6);
    }
    if (on) jump_at(F::class_rva, reinterpret_cast<const void*>(&fl_cf_class_counter));
    else restore_bytes(F::class_rva, 6);
    restore_bytes(F::diagnostic_rva, 6);
    flush();
}
uint64_t hash_records() {
    uint64_t h = 0xcbf29ce484222325ull;
    const unsigned n = record_count < record_capacity ? record_count : record_capacity;
    for (unsigned i = 0; i < n; ++i) {
        const unsigned char b[7] = {records[i].cls,
                                    static_cast<unsigned char>(records[i].x0 & 0xff),
                                    static_cast<unsigned char>((records[i].x0 >> 8) & 0xff),
                                    static_cast<unsigned char>(records[i].y & 0xff),
                                    static_cast<unsigned char>((records[i].y >> 8) & 0xff),
                                    static_cast<unsigned char>(records[i].x1 & 0xff),
                                    static_cast<unsigned char>((records[i].x1 >> 8) & 0xff)};
        for (unsigned char c : b) h = (h ^ c) * 0x100000001b3ull;
    }
    return h;
}

// ---- one call ----
struct Setup {
    int32_t r = 0, aspect = 65536, cx = 0, cy = 0;
    int w = 1024, h = 768;
    uint32_t flags = 4, colour = 0x3c;
    bool locked = true, has_backing = true, df = false;
    unsigned pad = 0;
    uint32_t mxcsr = 0x1f80;
    unsigned char* bits = dummy_pixels;
    int stride = 0;
};
struct Effects {
    unsigned records = 0;
    uint64_t hash = 0;
    unsigned getdesc = 0, lockrect = 0, recovery = 0, guard = 0, classes = 0, diags = 0, df = 0;
    uint32_t diag_ecx = 0, diag_arg = 0, bits = 0, flags = 0, mxcsr = 0, last_error = 0, pitch_field = 0;
    bool regs = false;
    bool same(const Effects& o) const {
        return records == o.records && hash == o.hash && getdesc == o.getdesc && lockrect == o.lockrect &&
               recovery == o.recovery && guard == o.guard && classes == o.classes && diags == o.diags &&
               diag_ecx == o.diag_ecx && diag_arg == o.diag_arg && bits == o.bits && flags == o.flags &&
               mxcsr == o.mxcsr && regs == o.regs;
    }
};
std::uintptr_t native_target() {
    return cf::patched() ? x3m_circle_fill_fl_continue : base + F::entry_rva;
}
Effects run(std::uintptr_t target, const Setup& s) {
    std::memset(surface, 0, sizeof surface);
    sput32(F::object_bits, s.locked ? u32(s.bits) : 0);
    set16(F::object_width, s.w);
    set16(F::object_height, s.h);
    sput32(F::object_flags, s.flags);
    set16(0x16, s.stride);
    sput32(F::object_backing, s.has_backing ? u32(&backing) : 0);
    locked_bits = u32(s.bits);
    record_count = bad_colour = bad_surface = overflow = leaf_calls = 0;
    getdesc_calls = lockrect_calls = recovery_calls = guard_seen = df_seen = 0;
    class_calls = diag_calls = diag_ecx = diag_arg = 0;
    expect_colour = s.colour;
    in_eax = 0xeaeaeaea;
    in_ebx = 0xb1b1b1b1;
    in_esi = 0x51515151;
    in_edi = 0xd1d1d1d1;
    in_ebp = 0xbebebebe;
    in_ecx = u32(surface);
    in_edx = uint32_t(s.cx);
    in_cy = uint32_t(s.cy);
    in_r = uint32_t(s.r);
    in_colour = s.colour;
    in_aspect = uint32_t(s.aspect);
    in_df = s.df;
    in_pad = s.pad;
    in_mxcsr = s.mxcsr;
    in_fcw = 0x027f;
    for (unsigned i = 0; i < 16; ++i) in_xmm[i] = static_cast<unsigned char>(0x70 + i);
    call_target = uint32_t(target);
    SetLastError(sentinel_error);
    fl_cf_call();
    Effects e;
    e.last_error = GetLastError();
    e.records = record_count;
    e.hash = hash_records();
    e.getdesc = getdesc_calls;
    e.lockrect = lockrect_calls;
    e.recovery = recovery_calls;
    e.guard = guard_seen;
    e.classes = class_calls;
    e.diags = diag_calls;
    e.diag_ecx = diag_ecx;
    e.diag_arg = diag_arg;
    e.df = df_seen;
    e.bits = sget32(F::object_bits);
    e.flags = sget32(F::object_flags);
    e.mxcsr = out_mxcsr;
    const uint32_t args[4] = {in_cy, in_r, in_colour, in_aspect};
    e.regs = out_ebx == in_ebx && out_esi == in_esi && out_edi == in_edi && out_ebp == in_ebp &&
             out_esp == esp_before && !std::memcmp(out_args, args, sizeof args) && ((out_flags & 0x400) != 0) == s.df &&
             out_fcw == in_fcw && !std::memcmp(out_xmm, in_xmm, 16) && (out_mxcsr & ~0x3fu) == (s.mxcsr & ~0x3fu) &&
             e.last_error == sentinel_error && !bad_colour && !bad_surface && !overflow;
    return e;
}
unsigned run_lines = 0, records_compared = 0;
const char* run_tag = "plain"; // plain = the Python model applies; else checked in patched/native pairs
void print(const char* kind, unsigned id, const Setup& s, const char* route, const Effects& e) {
    ++run_lines;
    records_compared += e.records;
    std::printf("RUN kind=%s id=%u r=%ld aspect=%ld cx=%ld cy=%ld w=%d h=%d mxcsr=%04lx route=%s records=%u "
                "hash=%016llx getdesc=%u lockrect=%u recovery=%u classes=%u diags=%u bits=%s flags=%08lx "
                "mxcsr_out=%04lx regs=%s tag=%s\n",
                kind, id, long(s.r), long(s.aspect), long(s.cx), long(s.cy), s.w, s.h, (unsigned long)s.mxcsr, route,
                e.records, (unsigned long long)e.hash, e.getdesc, e.lockrect, e.recovery, e.classes, e.diags,
                e.bits ? "set" : "null", (unsigned long)e.flags, (unsigned long)e.mxcsr, e.regs ? "ok" : "bad", run_tag);
}
const char* route_of(const Setup& s) {
    return F::route_name(F::route(s.cx, s.cy, s.r, s.aspect, s.w, s.h));
}
// Whether the stock routine is known to stop on this input (aspect 65536 at any radius, B <= 0 or the int32
// overflow value, or a radius below every non-unit onset): only those run natively.
bool native_safe(const Setup& s) {
    const int32_t r = F::s16(s.r);
    if (r <= 0 || s.aspect == 65536) return true;
    const int32_t q = F::quotient(s.aspect);
    if (q <= 0) return true;
    const int64_t A = int64_t(r) * r;
    if (q < 65536 && (A << 16) / q > 0x7fffffffll) return true;
    return r <= 518;
}
unsigned case_id = 0;
// Runs one input patched (and natively when safe); prints both. Returns the patched effects.
Effects both(Setup s, bool native = true) {
    const unsigned id = ++case_id;
    const char* route = route_of(s);
    Effects p = run(base + F::entry_rva, s);
    print("patched", id, s, route, p);
    check(p.regs, "patched_cpu_state", route);
    if (native && native_safe(s)) {
        Effects n = run(native_target(), s);
        print("native", id, s, route, n);
        check(n.regs, "native_cpu_state", route);
    }
    return p;
}

// ---- install helpers ----
bool entry_original() {
    return !std::memcmp(image + F::entry_rva, pristine + F::entry_rva, 16);
}
void set_setting(const wchar_t* value) {
    SetEnvironmentVariableW(L"X3M_CIRCLE_FILL_FIX", value);
}
bool row_has(const char* a, const char* b = nullptr, const char* c = nullptr) {
    return std::strstr(last_row, a) && (!b || std::strstr(last_row, b)) && (!c || std::strstr(last_row, c));
}
bool shutdown_clean(const char* label) {
    SetLastError(sentinel_error);
    const bool ok = cf::shutdown();
    const bool pass = ok && GetLastError() == sentinel_error && entry_original() && !cf::patched() &&
                      row_has("circle_fill_restore ", " edition=fl status=restored registered=0");
    check(pass, label, last_row);
    return pass;
}
bool install_ok(const char* label) {
    SetLastError(sentinel_error);
    const bool ok = cf::install_base(base);
    unsigned char now[16];
    std::memcpy(now, image + F::entry_rva, 16);
    const bool pass = ok && GetLastError() == sentinel_error && cf::patched() && !std::strcmp(cf::state(), "ok") &&
                      now[0] == 0xe9 && now[5] == 0xf8 && !std::memcmp(now + 6, pristine + F::entry_rva + 6, 10) &&
                      x3m_circle_fill_fl_continue != 0;
    check(pass, label, cf::state());
    return pass;
}
// A refused install: the entry bytes as they were before the attempt, nothing registered, LastError kept; the arena
// unchanged unless the claim itself ran (a post-claim fault rolls back but keeps its tail block).
void refused(const char* label, const char* state, bool claim_ran = false) {
    const unsigned used = engine_patch::arena_used();
    unsigned char before[16];
    std::memcpy(before, image + F::entry_rva, 16);
    SetLastError(sentinel_error);
    const bool ok = cf::install_base(base);
    const bool pass = !ok && GetLastError() == sentinel_error && !std::strcmp(cf::state(), state) &&
                      !std::memcmp(before, image + F::entry_rva, 16) && !cf::patched() &&
                      (claim_ran || engine_patch::arena_used() == used);
    char detail[160];
    std::snprintf(detail, sizeof detail, "want=%s got=%s", state, cf::state());
    check(pass, label, detail);
}

int main(int argc, char** argv) {
    if (argc < 2 || !map_image(argv[1])) {
        std::printf("RESULT FAIL cannot map %s\n", argc > 1 ? argv[1] : "(no path)");
        return 2;
    }
    check(!(base & 0xffff) && base != preferred_base, "image_64k_aligned_relocated");
    std::printf("IMAGE base=%08lx preferred=%08lx size=%08lx\n", (unsigned long)base, (unsigned long)preferred_base,
                (unsigned long)image_size);
    // The process-wide data the lock reads: the recovery callback, the render-target config object; the guard word
    // (base+0x22de10) starts 0.
    fake_vtable[12] = reinterpret_cast<void*>(&fake_get_desc);
    fake_vtable[13] = reinterpret_cast<void*>(&fake_lock_rect);
    put32(0x1aae24, u32(reinterpret_cast<void*>(&fake_recovery)));
    put32(0x1a82f8, u32(fake_cfg));
    std::memcpy(pristine + 0x1aae24, image + 0x1aae24, 4);
    std::memcpy(pristine + 0x1a82f8, image + 0x1a82f8, 4);
    class_copy_code = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    std::memcpy(class_copy_code, image + F::class_rva, 0x199);
    class_copy = u32(class_copy_code);

    // ---- 1. windows: every window matches at this base; every bit flip and every rebound relocation refused ----
    {
        unsigned matched = 0;
        static unsigned char buf[F::max_window];
        char line[1024];
        int n = std::snprintf(line, sizeof line, "WINDOWS");
        for (const F::Window& w : F::windows) {
            std::memcpy(buf, image + w.rva, w.length);
            matched += F::window_matches(w, buf, base);
            n += std::snprintf(line + n, sizeof line - n, " %s=%08lx", w.name, (unsigned long)F::fnv1a(buf, w.length));
        }
        std::printf("%s\n", line);
        check(matched == F::window_count, "every_window_matches_relocated");
        const unsigned used = engine_patch::arena_used();
        unsigned bits = 0, refused_bits = 0, relocs = 0, refused_relocs = 0;
        for (const F::Window& w : F::windows) {
            for (unsigned i = 0; i < w.length; ++i)
                for (unsigned b = 0; b < 8; ++b) {
                    image[w.rva + i] ^= static_cast<unsigned char>(1u << b);
                    ++bits;
                    const bool ok = cf::install_base(base);
                    if (!ok && !std::strcmp(cf::state(), w.name) && !cf::patched()) ++refused_bits;
                    else if (ok) cf::shutdown();
                    image[w.rva + i] ^= static_cast<unsigned char>(1u << b);
                }
            for (unsigned k = 0; k < w.reloc_count; ++k) {
                const uint32_t at = w.rva + w.reloc_offset[k];
                put32(at, get32(at) + 0x10000); // bound to another base
                ++relocs;
                const bool ok = cf::install_base(base);
                if (!ok && !std::strcmp(cf::state(), w.name)) ++refused_relocs;
                else if (ok) cf::shutdown();
                restore_bytes(at, 4);
            }
        }
        std::printf("FLIPS windows=%u bits=%u refused=%u relocs=%u relocs_refused=%u\n", F::window_count, bits,
                    refused_bits, relocs, refused_relocs);
        check(bits == refused_bits && relocs == refused_relocs && entry_original() && engine_patch::arena_used() == used,
              "every_window_bit_and_relocation_refused");
        // Named mutation guards: ret 16 at an exit, the caller's EDX/ECX convention, the AND prologue, a stale AP base.
        struct Named {
            const char* label;
            uint32_t rva;
            unsigned char bytes[3];
            unsigned n;
            const char* state;
        } named[] = {{"ret16_exit", 0x10cd88, {0xc2, 0x10, 0x00}, 3, "routine_mismatch"},
                     {"caller_edx_from_ecx", 0x0a7b17, {0x8b, 0xd1}, 2, "caller_mismatch"},
                     {"caller_ecx_from_edx", 0x0a7b10, {0x8b, 0x54}, 2, "caller_mismatch"},
                     {"and_prologue_16", 0x10c945, {0xf0}, 1, "routine_mismatch"},
                     {"lock_one_attempt", 0x0f5b21, {0x83, 0xff, 0x00}, 3, "lock_mismatch"},
                     {"class_pure_body", 0x10ba85, {0x90}, 1, "class_mismatch"}};
        for (const Named& m : named) {
            std::memcpy(image + m.rva, m.bytes, m.n);
            refused(m.label, m.state);
            restore_bytes(m.rva, m.n);
        }
        // A base that is not the image (64 KB off: a stale or preferred-base address) and a misaligned base.
        {
            SetLastError(sentinel_error);
            const bool stale = cf::install_base(base + 0x10000);
            check(!stale && std::strcmp(cf::state(), "ok") && entry_original() && GetLastError() == sentinel_error,
                  "stale_base_refused", cf::state());
            const bool odd = cf::install_base(base + 0x1000);
            check(!odd && !std::strcmp(cf::state(), "invalid_site"), "misaligned_base_refused", cf::state());
        }
        // A window that cannot be read.
        fault_read_at = base + F::windows[2].rva;
        fault_read_count = F::windows[2].length;
        refused("unreadable_window", "unreadable");
    }

    // ---- 2. claim, read-back, rollback, shutdown ----
    {
        if (install_ok("install")) {
            check(!cf::install_base(base) && !std::strcmp(cf::state(), "already_installed") && cf::patched(),
                  "second_install_refused");
            shutdown_clean("shutdown_restores");
            check(cf::shutdown(), "shutdown_idempotent");
        }
        fault_push_at = base + F::entry_rva;
        refused("chain_fault_rolled_back", "chain_failed", true);
        fault_read_at = base + F::entry_rva;
        fault_read_count = F::site_length;
        refused("readback_fault_rolled_back", "readback_failed", true);
        // A chain fault whose rollback cannot complete: registered, the jump still in; shutdown then restores.
        fault_push_at = fault_restore_at = base + F::entry_rva;
        SetLastError(sentinel_error);
        const bool ok = cf::install_base(base);
        check(!ok && !std::strcmp(cf::state(), "rollback_failed") && cf::patched() && image[F::entry_rva] == 0xe9 &&
                  GetLastError() == sentinel_error,
              "rollback_failure_stays_registered", cf::state());
        shutdown_clean("shutdown_after_rollback_failure");
        // Someone else's bytes over our jump: shutdown writes nothing and stays registered.
        if (install_ok("install_again")) {
            unsigned char ours[5];
            std::memcpy(ours, image + F::entry_rva, 5);
            image[F::entry_rva] = 0xcc;
            SetLastError(sentinel_error);
            const bool clean = cf::shutdown();
            check(!clean && cf::patched() && image[F::entry_rva] == 0xcc && row_has("status=restore_not_owned registered=1") &&
                      GetLastError() == sentinel_error,
                  "restore_not_owned", last_row);
            std::memcpy(image + F::entry_rva, ours, 5);
            shutdown_clean("shutdown_after_not_owned");
        }
        std::printf("FAULTS taken=%u\n", faults_taken);
        check(faults_taken == 5, "faults_taken");
    }

    // ---- 3. initialize(): the rows ----
    {
        struct RowCase {
            const char* label;
            const wchar_t* value;
            std::uintptr_t at;
            const char* a;
            const char* b;
            bool applied;
        } cases[] = {
            {"row_off", L"off", base, "status=off reason=off mode=off setting=off write=none", nullptr, false},
            {"row_invalid", L"On", base, "status=refused reason=invalid_setting mode=- setting=On", nullptr, false},
            {"row_too_long", L"onononononononononononononononononon", base, "status=refused reason=too_long", "setting=?", false},
            {"row_no_executable", nullptr, 0, "status=refused reason=executable_mismatch mode=on setting=-", nullptr, false},
            {"row_patched_default", nullptr, base, "status=patched reason=ok mode=on setting=- write=atomic threshold=813",
             "rva=10c940 edition=fl", true},
            {"row_patched_on", L"on", base, "status=patched reason=ok mode=on setting=on write=atomic", nullptr, true},
        };
        for (const RowCase& c : cases) {
            set_setting(c.value);
            cf::fixture_base(c.at);
            const unsigned before = rows;
            SetLastError(sentinel_error);
            const bool applied = cf::initialize();
            const bool pass = applied == c.applied && rows == before + 1 && GetLastError() == sentinel_error &&
                              row_has("circle_fill site=", " edition=fl ", c.a) && (!c.b || row_has(c.b)) &&
                              cf::patched() == c.applied && (c.applied || entry_original());
            check(pass, c.label, last_row);
            if (c.applied) shutdown_clean("row_shutdown");
        }
        cf::fixture_base(0);
        set_setting(nullptr);
    }

    // ---- 4. native validation before the claim: the real routine, aspect 65536 ----
    fakes(true);
    {
        const int32_t radii[] = {1, 2, 100, 813, 814, 825, 826, 1290, 1291, 32767};
        for (int32_t r : radii) {
            Setup s;
            s.r = r;
            s.cx = 512;
            s.cy = 384;
            const unsigned id = ++case_id;
            Effects n = run(base + F::entry_rva, s);
            print("native", id, s, "unpatched", n);
            check(n.regs, "unpatched_cpu_state");
        }
    }

    // ---- 5. the patched routine ----
    fakes(false);
    if (!install_ok("install_for_runs")) {
        std::printf("RESULT FAIL checks=%u failures=%u\n", checks, failures);
        return 1;
    }
    fakes(true);
    struct Place {
        int cx, cy, w, h;
    };
    // Placements: inside a surface sized to the disc, the centred one and four single-edge placements on 1024x768.
    auto places = [](int32_t r, Place* out) {
        unsigned n = 0;
        const int32_t R = F::s16(r) > 0 ? F::s16(r) : 1;
        if (2 * R + 64 <= 32767) out[n++] = {R + 32, R + 32, 2 * R + 64, 2 * R + 64};
        out[n++] = {512, 384, 1024, 768};
        out[n++] = {-100, 300, 1024, 768};
        out[n++] = {1100, 300, 1024, 768};
        out[n++] = {500, -50, 1024, 768};
        out[n++] = {500, 800, 1024, 768};
        // Large radii: the caps (small x) on the surface. Near y = 0 a span of x > 32767 - |cx| wraps its 16-bit end
        // in the engine's coordinates, so the port, like the routine, emits nothing there.
        if (R >= 1000) out[n++] = {512, 400 - R, 1024, 768};
        if (R >= 1000 && 368 + R <= 32767) out[n++] = {512, 368 + R, 1024, 768};
        return n;
    };
    {
        // aspect 65536 at every listed radius and placement.
        const int32_t radii[] = {1, 2, 100, 500, 813, 814, 825, 826, 1290, 1291, 32767};
        for (int32_t r : radii) {
            Place p[8];
            const unsigned n = places(r, p);
            for (unsigned i = 0; i < n; ++i) {
                Setup s;
                s.r = r;
                s.cx = p[i].cx;
                s.cy = p[i].cy;
                s.w = p[i].w;
                s.h = p[i].h;
                both(s);
            }
        }
        // Non-unit and invalid aspects: a 32767-square surface for the unclipped placement (room for B > A).
        const int32_t aspects[] = {32768, 98304, 131072, 1051682, -65536, 65535, 65537, 46341, 0, 2147483647, 256};
        const int32_t aradii[] = {100, 518, 813, 814, 900, 1291, 32767};
        for (int32_t aspect : aspects)
            for (int32_t r : aradii) {
                Place p[8];
                const unsigned n = places(r, p);
                p[0] = {16383, 16383, 32767, 32767};
                for (unsigned i = 0; i < n; ++i) {
                    Setup s;
                    s.r = r;
                    s.aspect = aspect;
                    s.cx = p[i].cx;
                    s.cy = p[i].cy;
                    s.w = p[i].w;
                    s.h = p[i].h;
                    const F::Route route = F::route(s.cx, s.cy, s.r, s.aspect, s.w, s.h);
                    if (route != F::Route::wide && !native_safe(s)) continue; // the stock routine might not stop
                    both(s);
                }
            }
        // The routing witnesses of the note: native out-of-bounds spans forwarded (stock_extent), a safe unclipped
        // ellipse admitted, and the radius words: zero, negative, -32768, high bits ignored.
        Setup s;
        s.r = 300;
        s.aspect = 32768;
        s.cx = s.cy = 400;
        s.w = s.h = 800;
        check(both(s).records == 602 && !std::strcmp(route_of(s), "stock_extent"), "native_extent_forwarded");
        s.r = 900;
        s.cx = s.cy = 2000;
        s.w = s.h = 4000;
        check(!std::strcmp(route_of(s), "wide"), "unclipped_ellipse_admitted");
        both(s, false);
        const int32_t words[] = {0, -5, int32_t(0xffff8000u), 0x00010000, 0x12340100, int32_t(0xfffe0335u)};
        for (int32_t w : words) {
            Setup t;
            t.r = w;
            t.cx = 512;
            t.cy = 384;
            both(t);
        }
    }
    // ---- 6. MXCSR modes: the native conversion under the caller's rounding/FTZ/DAZ; control bits kept ----
    run_tag = "mxcsr";
    {
        const uint32_t modes[] = {0x1f80, 0x3f80, 0x5f80, 0x7f80, 0x9fc0};
        struct In {
            int32_t r, aspect;
            int cx, cy, w, h;
        } inputs[] = {{300, 1051682, 512, 384, 1024, 768}, {300, 1051682, 16383, 16383, 32767, 32767},
                      {1000, 131072, 512, 384, 1024, 768}, {820, 65536, 512, 384, 1024, 768},
                      {518, 32768, 512, 384, 1024, 768}, {900, 98304, 2000, 2000, 4000, 4000}};
        unsigned same = 0, total = 0;
        for (uint32_t m : modes)
            for (const In& in : inputs) {
                Setup s;
                s.r = in.r;
                s.aspect = in.aspect;
                s.cx = in.cx;
                s.cy = in.cy;
                s.w = in.w;
                s.h = in.h;
                s.mxcsr = m;
                const unsigned id = ++case_id;
                Effects p = run(base + F::entry_rva, s);
                Effects n = run(native_target(), s);
                print("patched", id, s, route_of(s), p);
                print("native", id, s, route_of(s), n);
                ++total;
                same += p.same(n);
            }
        std::printf("MXCSR modes=5 inputs=6 same=%u/%u\n", same, total);
        check(same == total, "mxcsr_modes_patched_equals_native");
    }
    // ---- 7. lock, recovery, no-draw, live dimensions, DF, stack alignment, unsupported class ----
    run_tag = "scripted";
    {
        struct LockCase {
            const char* label;
            unsigned fails;
            uint32_t hr;
            bool backing;
            uint32_t flags;
            unsigned lockrects, recoveries;
            bool drawn;
        } locks[] = {{"lock_ok", 0, 0, true, 4, 1, 0, true},
                     {"lock_oom_then_ok", 1, hr_outofmemory, true, 4, 2, 1, true},
                     {"lock_oom_twice", 2, hr_outofmemory, true, 4, 2, 2, false},
                     {"lock_vidmem_twice", 2, 0x8876017c, true, 4, 2, 2, false},
                     {"lock_fail_twice", 2, hr_fail, true, 4, 2, 0, false},
                     {"no_backing", 0, 0, false, 4, 0, 0, false},
                     {"no_draw_flag", 0, 0, true, 4 | F::flag_no_draw, 0, 0, false}};
        const int32_t lradii[] = {500, 820, 1291};
        for (const LockCase& c : locks)
            for (int32_t r : lradii) {
                Setup s;
                s.r = r;
                s.cx = 512;
                s.cy = 384;
                s.locked = false;
                s.has_backing = c.backing;
                s.flags = c.flags;
                lock_fail_left = c.fails;
                lock_fail_hr = c.hr;
                const unsigned id = ++case_id;
                Effects p = run(base + F::entry_rva, s);
                print("patched", id, s, route_of(s), p);
                lock_fail_left = c.fails;
                Effects n = run(native_target(), s);
                print("native", id, s, route_of(s), n);
                const bool counts = p.lockrect == c.lockrects && p.recovery == c.recoveries && p.guard == c.recoveries &&
                                    p.getdesc == (c.backing && !(c.flags & F::flag_no_draw) ? 1u : 0u) &&
                                    (p.records != 0) == c.drawn && (p.bits != 0) == c.drawn &&
                                    ((p.flags & F::flag_locked) != 0) == c.drawn && p.classes == (c.drawn ? 1u : 0u);
                const bool equal = r > 825 ? (p.getdesc == n.getdesc && p.lockrect == n.lockrect &&
                                              p.recovery == n.recovery && p.classes == n.classes && p.bits == n.bits &&
                                              p.flags == n.flags)
                                           : p.same(n);
                check(counts && equal && p.regs && lock_flags_seen == 0 && get32(0x22de10) == 0, c.label);
            }
        lock_fail_left = 0;
        // Dimensions changed by a leaf callback or by the recovery callback between the clip decision and the spans.
        struct Mut {
            const char* label;
            int32_t r, aspect;
            unsigned at;
            bool recovery;
            int w, h;
        } muts[] = {{"leaf_shrinks_height", 820, 65536, 7, false, 1024, 300},
                    {"leaf_shrinks_width", 820, 65536, 3, false, 400, 768},
                    {"leaf_grows_both", 820, 65536, 11, false, 2000, 1500},
                    {"leaf_shrinks_ellipse", 300, 1051682, 5, false, 600, 200},
                    {"recovery_shrinks", 820, 65536, 0, true, 300, 200},
                    {"recovery_grows_unclipped", 300, 65536, 0, true, 2000, 2000}};
        for (unsigned k = 0; k < sizeof muts / sizeof muts[0]; ++k) {
            const Mut& m = muts[k];
            Setup s;
            s.r = m.r;
            s.aspect = m.aspect;
            s.cx = 512;
            s.cy = 384;
            if (k == 5) s.cx = s.cy = 400, s.w = s.h = 1000; // unclipped before the recovery grows the surface
            s.locked = !m.recovery;
            mutate_at = m.at;
            mutate_w = m.w;
            mutate_h = m.h;
            recovery_mutates = m.recovery;
            lock_fail_hr = hr_outofmemory;
            lock_fail_left = m.recovery ? 1 : 0;
            const unsigned id = ++case_id;
            Effects p = run(base + F::entry_rva, s);
            print("patched", id, s, route_of(s), p);
            lock_fail_left = m.recovery ? 1 : 0;
            Effects n = run(native_target(), s);
            print("native", id, s, route_of(s), n);
            check(p.same(n) && p.records > 0 && !std::strcmp(route_of(s), k == 5 ? "stock_fast" : "wide"), m.label);
        }
        mutate_at = 0;
        recovery_mutates = false;
        lock_fail_left = 0;
        // DF: entry DF=1 kept on every path; the C side and the helpers it calls see DF=0.
        {
            Setup s;
            s.r = 820;
            s.cx = 512;
            s.cy = 384;
            s.df = true;
            s.locked = false;
            Effects p = run(base + F::entry_rva, s);
            check(p.regs && p.records > 0 && p.df == 0 && p.getdesc == 1, "df_set_wide_draw_cleared_inside");
            s.has_backing = false;
            Effects q = run(base + F::entry_rva, s);
            Effects n = run(native_target(), s);
            check(q.regs && n.regs && q.same(n), "df_set_no_draw_kept");
            s.r = 500;
            s.has_backing = true;
            Effects f = run(base + F::entry_rva, s); // the fast path: the stock routine sees the caller's DF
            check(f.regs && f.df > 0, "df_fast_path_untouched");
        }
        // Four incoming stack alignments: same spans, every register kept.
        {
            uint64_t first = 0;
            unsigned good = 0;
            for (unsigned pad = 0; pad < 16; pad += 4) {
                Setup s;
                s.r = 1291;
                s.cx = 512;
                s.cy = 384;
                s.pad = pad;
                Effects p = run(base + F::entry_rva, s);
                if (!pad) first = p.hash;
                good += p.regs && p.hash == first && p.records > 0;
            }
            check(good == 4, "four_incoming_alignments");
        }
        // An unsupported class: the lock first, the diagnostic once with the class call's ECX and the message, no
        // span, handled; the same effects as the stock routine.
        {
            const int32_t values[] = {3, 25, -1, 0x7fff};
            for (int32_t v : values) {
                fake_class_value = uint32_t(v);
                fake_class_ecx = 0x5a5a0000u | uint32_t(v & 0xff);
                jump_at(F::class_rva, reinterpret_cast<const void*>(&fl_cf_class_fake));
                jump_at(F::diagnostic_rva, reinterpret_cast<const void*>(&fl_cf_diag_fake));
                flush();
                Setup s;
                s.r = 820;
                s.cx = 512;
                s.cy = 384;
                s.locked = false;
                const unsigned id = ++case_id;
                Effects p = run(base + F::entry_rva, s);
                print("patched", id, s, route_of(s), p);
                Effects n = run(native_target(), s);
                print("native", id, s, route_of(s), n);
                check(p.same(n) && p.diags == 1 && p.diag_ecx == fake_class_ecx && p.diag_arg == base + F::message_rva &&
                          p.records == 0 && p.lockrect == 1 && p.classes == 1 && p.regs,
                      "unsupported_class_diagnostic");
            }
            fakes(true);
        }
        // Every class: the leaf the table names.
        {
            const uint32_t flags[] = {4, 4 | 0x100, 4 | 0x200, 0, 4 | 0x2000};
            const unsigned char want[] = {0, 5, 10, 15, 24};
            for (unsigned i = 0; i < 5; ++i) {
                Setup s;
                s.r = 1291;
                s.cx = 512;
                s.cy = 384;
                s.flags = flags[i];
                Effects p = run(base + F::entry_rva, s);
                check(p.records > 0 && records[0].cls == want[i] && p.classes == 1, "class_leaf");
            }
        }
    }
    // ---- 8. canary-backed writes through the real class-0 leaf and CRT fill ----
    {
        constexpr int W = 1024, H = 768, padx = 64, pady = 16, stride = W + 2 * padx;
        static unsigned char pixels[(H + 2 * pady) * stride];
        static unsigned char expect[H * W];
        struct In {
            int32_t r, aspect;
            int cx, cy;
        } inputs[] = {{813, 65536, 512, 384},   {814, 65536, 512, 384},   {825, 65536, -100, 300},
                      {826, 65536, 1100, 300},  {1290, 65536, 500, -50},  {1291, 65536, 500, 800},
                      {1291, 65536, 512, 384},  {32767, 65536, 512, -32000}, {32767, 65536, -100, 32700},
                      {600, 32768, 512, 384},   {300, 1051682, 512, 384}, {2000, 131072, 1000, 700}};
        unsigned good = 0, written = 0;
        for (const In& in : inputs) {
            Setup s;
            s.r = in.r;
            s.aspect = in.aspect;
            s.cx = in.cx;
            s.cy = in.cy;
            s.w = W;
            s.h = H;
            s.stride = stride;
            s.colour = 0x3c;
            s.bits = pixels + pady * stride + padx;
            Effects rec = run(base + F::entry_rva, s); // the recording leaves: the coverage
            std::memset(expect, 0, sizeof expect);
            bool inside = true;
            for (unsigned i = 0; i < rec.records && i < record_capacity; ++i) {
                const Record& q = records[i];
                if (q.y < 0 || q.y >= H || q.x0 < 0 || q.x1 >= W || q.x0 > q.x1) {
                    inside = false;
                    continue;
                }
                std::memset(expect + q.y * W + q.x0, 1, size_t(q.x1 - q.x0 + 1));
            }
            std::memset(pixels, 0xa5, sizeof pixels);
            restore_bytes(F::leaf_class0_rva, 6); // the real class-0 leaf
            flush();
            Effects real = run(base + F::entry_rva, s);
            fakes(true);
            unsigned bad = 0, set = 0;
            for (int y = -pady; y < H + pady; ++y)
                for (int x = -padx; x < W + padx; ++x) {
                    const unsigned char v = pixels[(y + pady) * stride + x + padx];
                    const bool in_surface = y >= 0 && y < H && x >= 0 && x < W;
                    const bool want = in_surface && expect[y * W + x];
                    if (v != (want ? 0x3c : 0xa5)) ++bad;
                    set += v == 0x3c;
                }
            written += set;
            const bool pass = inside && !bad && real.regs && rec.regs && rec.records > 0;
            good += pass;
            char detail[128];
            std::snprintf(detail, sizeof detail, "r=%ld aspect=%ld cx=%d bad=%u inside=%d regs=%d/%d records=%u", long(in.r),
                          long(in.aspect), in.cx, bad, int(inside), int(rec.regs), int(real.regs), rec.records);
            check(pass, "canary_leaf_writes", detail);
        }
        std::printf("CANARY cases=%u good=%u pixels=%u\n", unsigned(sizeof inputs / sizeof inputs[0]), good, written);
    }
    // ---- 9. the first-wide row, the late window, shutdown ----
    {
        fakes(false);
        engine_patch::close_install_window("fixture");
        shutdown_clean("shutdown_after_window_closed");
        refused("late_claim", "late_claim");
        set_setting(nullptr);
        cf::fixture_base(base);
        SetLastError(sentinel_error);
        const bool applied = cf::initialize();
        check(!applied && row_has("status=refused reason=late_claim", " edition=fl ") && entry_original() &&
                  GetLastError() == sentinel_error,
              "row_late_claim", last_row);
    }
    std::printf("SUMMARY runs=%u records=%u cases=%u\n", run_lines, records_compared, case_id);
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
