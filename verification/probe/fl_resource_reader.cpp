// Native fixture of the FL resource reader and DAT handle pool (src/engine/fl/resource_reader_fl.cpp over the shared
// core src/proxy/resource_reader_core.cpp; docs/reverse-engineering/fl/resource-reader.md, "Port (2026-11-03)"). Built
// with -DX3M_EDITION_FL=1 -DX3M_RESOURCE_READER_FL_FIXTURE by verification/analysis/test_fl_loading.py and run natively
// through WSL interop: ./fl_resource_reader.exe <X3FL.exe> <zlib1.dll copy>. It maps and relocates the installed X3FL.exe
// to a private 64 KB-aligned base (fl_music_fixture.h), binds the reader's three inflate import slots to the game's
// zlib1.dll, and points the static CRT's fread/fseek/ftell/fgetc/malloc/free/memset/fopen/fclose entries at an
// in-memory CRT with FL's FILE layout (flag word +0x0c, EOF 0x08, error 0x10). The real relocated reader
// (base+0x105f40) is the native route; the production install claims the real entry and the eighteen call sites.
// Cases: every reader/pool window change refused; claim + read-back of the entry and all eighteen call sites (the CRT's exit-time fcloseall call left native); the
// keep's close_wrapper window; reads through the stub byte-identical to the native route (bytes, both size globals,
// all four counters, cursor, stream position) over a synthetic catalogue .dat (sequential, out-of-order seeking, the
// last record ending at EOF, a stream cut before its end) and loose files; fallbacks; register / EFLAGS / XMM7 / x87
// CW / MXCSR / LastError / errno preservation on the handled and the fallback path (the caller's EAX restored before
// the original); verify mode agreeing; the close wrapper and the destructor (engine code) pooling, the pool close
// reached through each of the seventeen redirected calls' targets pooling or releasing; reuse across reopen;
// partial-install rollback at every fault point;
// the late-window refusal; a per-read timing. The X3FL.exe and zlib1.dll bytes are never copied into the repository.
#include "fl_music_fixture.h"
#include "../../src/engine/fl/code_window_fl.h"
#include "../../src/engine/fl/resource_reader_fl.h"
#include "../../src/engine/fl/resource_reader_sites_fl.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/resource_reader.h"
#include <cerrno>
#include <string>
#include <vector>

namespace R = x3m::resource_reader;
namespace F = x3m::fl::resource_reader_fl;
namespace W = x3m::fl::loading_windows;
using std::uint32_t;

namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    fx::row(format, args);
    va_end(args);
}
}

// ---- in-memory CRT with FL's FILE prefix ----
namespace {
struct FakeFile {
    char* ptr;           // +0x00
    char* base;          // +0x04
    int cnt;             // +0x08
    volatile LONG flags; // +0x0c: EOF 0x08, error 0x10 (FL's values)
    int fd;              // +0x10
    const std::vector<unsigned char>* data;
    unsigned pos;
    bool open;
    std::string path;
};
static_assert(offsetof(FakeFile, flags) == F::file_flag_offset);
struct Named {
    std::string path;
    std::vector<unsigned char> bytes;
};
std::vector<Named> disk;
FakeFile files[64];
unsigned crt_opens = 0, crt_closes = 0, crt_dead_use = 0, crt_mallocs = 0, crt_frees = 0;
FakeFile* live(void* f) {
    auto* p = static_cast<FakeFile*>(f);
    if (!p || !p->open) ++crt_dead_use;
    return p;
}
extern "C" void* __cdecl crt_fopen(const char* path, const char* mode) {
    for (auto& d : disk)
        if (d.path == path) {
            for (auto& f : files)
                if (!f.open) {
                    f = FakeFile{};
                    f.open = true;
                    f.data = &d.bytes;
                    f.path = path;
                    f.fd = int(&f - files);
                    ++crt_opens;
                    (void)mode;
                    return &f;
                }
            return nullptr;
        }
    return nullptr;
}
extern "C" int __cdecl crt_fclose(void* file) {
    FakeFile* f = live(file);
    if (!f->open) return -1;
    f->open = false;
    ++crt_closes;
    return 0;
}
extern "C" size_t __cdecl crt_fread(void* out, size_t size, size_t count, void* file) {
    FakeFile* f = live(file);
    if (f->flags & 0x10) return 0;
    const size_t want = size * count, have = f->pos < f->data->size() ? f->data->size() - f->pos : 0;
    const size_t n = want < have ? want : have;
    auto* o = static_cast<unsigned char*>(out);
    for (size_t i = 0; i < n; ++i) o[i] = (*f->data)[f->pos + i];
    f->pos += unsigned(n);
    if (n < want) f->flags |= 0x08;
    return size ? n / size : 0;
}
extern "C" int __cdecl crt_fseek(void* file, long offset, int whence) {
    FakeFile* f = live(file);
    const long from = whence == 0 ? 0 : whence == 1 ? long(f->pos) : long(f->data->size());
    if (from + offset < 0) return -1;
    f->pos = unsigned(from + offset);
    f->flags &= ~0x08;
    return 0;
}
extern "C" long __cdecl crt_ftell(void* file) {
    return long(live(file)->pos);
}
extern "C" int __cdecl crt_fgetc(void* file) {
    FakeFile* f = live(file);
    if (f->pos >= f->data->size()) {
        f->flags |= 0x08;
        return -1;
    }
    return (*f->data)[f->pos++];
}
extern "C" void* __cdecl crt_malloc(size_t n) {
    ++crt_mallocs;
    return HeapAlloc(GetProcessHeap(), 0, n ? n : 1);
}
extern "C" void __cdecl crt_free(void* p) {
    if (p) ++crt_frees, HeapFree(GetProcessHeap(), 0, p);
}
extern "C" void* __cdecl crt_memset(void* p, int v, size_t n) {
    auto* b = static_cast<volatile unsigned char*>(p);
    for (size_t i = 0; i < n; ++i) b[i] = static_cast<unsigned char>(v);
    return p;
}
// The CRT entries the reader, the allocator and the pool reach (fgetc and memset are not production bindings).
struct Crt {
    uint32_t rva;
    const void* fn;
};
const Crt crt[] = {{F::fread_rva, reinterpret_cast<const void*>(&crt_fread)},
                   {F::fseek_rva, reinterpret_cast<const void*>(&crt_fseek)},
                   {F::ftell_rva, reinterpret_cast<const void*>(&crt_ftell)},
                   {0x12fd2c, reinterpret_cast<const void*>(&crt_fgetc)},
                   {F::malloc_rva, reinterpret_cast<const void*>(&crt_malloc)},
                   {F::free_rva, reinterpret_cast<const void*>(&crt_free)},
                   {0x121920, reinterpret_cast<const void*>(&crt_memset)},
                   {F::fopen_rva, reinterpret_cast<const void*>(&crt_fopen)},
                   {F::fclose_rva, reinterpret_cast<const void*>(&crt_fclose)}};
void crt_route(bool on) {
    for (const Crt& c : crt) {
        if (on) {
            fx::image[c.rva] = 0xe9;
            fx::put32(c.rva + 1, fx::u32(c.fn) - fx::va(c.rva + 5));
        } else
            std::memcpy(fx::image + c.rva, fx::pristine.data() + c.rva, 5);
    }
    fx::flush();
}

// ---- zlib1.dll: payloads ----
using DeflateInit2 = int(__cdecl*)(void*, int, int, int, int, int, const char*, int);
using Deflate = int(__cdecl*)(void*, int);
using DeflateEnd = int(__cdecl*)(void*);
using Crc32 = unsigned long(__cdecl*)(unsigned long, const unsigned char*, unsigned);
DeflateInit2 z_deflate_init;
Deflate z_deflate;
DeflateEnd z_deflate_end;
Crc32 z_crc32;
struct ZStream {
    const unsigned char* next_in;
    uint32_t avail_in, total_in;
    unsigned char* next_out;
    uint32_t avail_out, total_out;
    const char* msg;
    void *state, *zalloc, *zfree, *opaque;
    int data_type;
    uint32_t adler, reserved;
};
std::vector<unsigned char> raw_deflate(const std::vector<unsigned char>& in, int level) {
    ZStream z{};
    z_deflate_init(&z, level, 8, -15, 8, 0, "1.2.3", int(sizeof z));
    std::vector<unsigned char> out(in.size() + in.size() / 8 + 256);
    z.next_in = in.data();
    z.avail_in = uint32_t(in.size());
    z.next_out = out.data();
    z.avail_out = uint32_t(out.size());
    z_deflate(&z, 4);
    out.resize(z.total_out);
    z_deflate_end(&z);
    return out;
}
uint32_t rng = 0x12345678;
uint32_t rnd() {
    rng = rng * 1103515245u + 12345u;
    return rng >> 8;
}
std::vector<unsigned char> payload(size_t n, bool text) {
    std::vector<unsigned char> v(n);
    static const char words[] = "the quick brown fox jumps over the lazy dog shipyard sector trade ";
    for (size_t i = 0; i < n; ++i) v[i] = text ? words[(i * 7 + (i / 61)) % (sizeof words - 1)] : (unsigned char)rnd();
    return v;
}
// A gzip member with optional FEXTRA/FNAME/FCOMMENT/FHCRC and an optional cut of the deflate data (trailer kept).
std::vector<unsigned char> gzip(const std::vector<unsigned char>& data, unsigned flags, unsigned cut = 0, int level = 6) {
    std::vector<unsigned char> g = {0x1f, 0x8b, 8, (unsigned char)flags, 0, 0, 0, 0, 0, 11};
    if (flags & 4) {
        g.push_back(5), g.push_back(0);
        for (int i = 0; i < 5; ++i) g.push_back((unsigned char)('a' + i));
    }
    if (flags & 8)
        for (const char* p = "name.txt"; *p || (g.push_back(0), false); ++p) g.push_back((unsigned char)*p);
    if (flags & 0x10)
        for (const char* p = "comment"; *p || (g.push_back(0), false); ++p) g.push_back((unsigned char)*p);
    if (flags & 2) g.push_back(0x12), g.push_back(0x34);
    std::vector<unsigned char> d = raw_deflate(data, level);
    if (cut) d.resize(d.size() - cut);
    g.insert(g.end(), d.begin(), d.end());
    const uint32_t crc = uint32_t(z_crc32(0, data.data(), unsigned(data.size()))), isize = uint32_t(data.size());
    for (int i = 0; i < 4; ++i) g.push_back((unsigned char)(crc >> (8 * i)));
    for (int i = 0; i < 4; ++i) g.push_back((unsigned char)(isize >> (8 * i)));
    return g;
}
std::vector<unsigned char> scramble(const std::vector<unsigned char>& g, unsigned char key) {
    std::vector<unsigned char> s(1, (unsigned char)(key ^ 0xc8));
    for (unsigned char b : g) s.push_back((unsigned char)(b ^ key));
    return s;
}
unsigned header_length(unsigned flags) {
    return 10 + (flags & 4 ? 7 : 0) + (flags & 8 ? 9 : 0) + (flags & 0x10 ? 8 : 0) + (flags & 2 ? 2 : 0);
}
// A payload whose last deflate byte opens a chunk of its own (the trailer-tail class: the record cursor stops short).
std::vector<unsigned char> short_cursor_payload(bool scrambled, std::vector<unsigned char>& stored) {
    for (unsigned n = 2000; n < 40000; n += 7) {
        auto data = payload(n, false);
        auto g = gzip(data, 0);
        const unsigned start = scrambled ? 1 : 0, len = unsigned(g.size()) + start, h = header_length(0);
        const unsigned consumed = len - start - h - 8, final_ = start + h + ((consumed - 1) / 1024 + 1) * 1024;
        if (final_ < len) {
            stored = scrambled ? scramble(g, 0x5a) : g;
            return data;
        }
    }
    return {};
}

// ---- cases ----
struct Case {
    const char* name;
    std::string path;
    bool catalogue;
    uint32_t offset, length;
    bool handled; // the fast path decodes it (otherwise a fallback to the native route)
};
std::vector<Case> cases;
std::vector<unsigned char> dat;
void add_record(const char* name, const std::vector<unsigned char>& stored, bool handled) {
    Case c{name, "data\\01.dat", true, uint32_t(dat.size()), uint32_t(stored.size()), handled};
    for (unsigned char b : stored) dat.push_back((unsigned char)(b ^ 0x33));
    cases.push_back(c);
}
void add_loose(const char* name, const std::vector<unsigned char>& stored, bool handled) {
    disk.push_back({std::string("data\\") + name + ".pck", stored});
    cases.push_back(Case{name, disk.back().path, false, 0, 0, handled});
}
// The FL file object (0x64 bytes: the 0x14-byte prefix the core reads, three SSO strings the destructor resets).
struct Object {
    R::FileObject head;
    unsigned char rest[0x64 - sizeof(R::FileObject)];
};
static_assert(sizeof(Object) == 0x64);
void init_object(Object& o) {
    std::memset(&o, 0, sizeof o);
    for (uint32_t cap : {0x28u, 0x40u, 0x60u}) std::memcpy(reinterpret_cast<unsigned char*>(&o) + cap, "\x0f\0\0\0", 4);
}
// The DAT open's effect (0x105ec7..0x105ee3): flags |= 3, offset/length stored, cursor 0, fseek to the record.
void open_object(Object& o, const Case& c, void* file) {
    init_object(o);
    o.head.file = file;
    o.head.flags = c.catalogue ? 3 : 1;
    o.head.offset = int32_t(c.offset);
    o.head.length = int32_t(c.length);
    crt_fseek(file, long(c.offset), 0);
}
struct Outcome {
    std::vector<unsigned char> bytes;
    bool null = true;
    uint32_t globals[2]{}, deltas[4]{}, mallocs = 0;
    int32_t cursor = 0;
    long position = 0;
    DWORD error = 0;
};
uint32_t* global(uint32_t rva) {
    return reinterpret_cast<uint32_t*>(fx::image + rva);
}
// One reader call through base+0x105f40 (native, or the claimed entry -> stub) on an opened object.
Outcome run(const Case& c, Object& o) {
    uint32_t before[4];
    for (unsigned i = 0; i < 4; ++i) before[i] = *global(F::counter_rva[i]);
    *global(F::size_global_rva[0]) = *global(F::size_global_rva[1]) = 0xdeadbeef;
    const unsigned m0 = crt_mallocs;
    fx_df = 0;
    const fx::Call call = fx::enter(fx::va(F::reader_rva), {0x1111, fx::u32(&o), 0x3333, 0x4444, 0x5555, 0x6666, 0x7777}, {});
    Outcome r;
    r.error = call.error;
    r.mallocs = crt_mallocs - m0;
    for (unsigned i = 0; i < 2; ++i) r.globals[i] = *global(F::size_global_rva[i]);
    for (unsigned i = 0; i < 4; ++i) r.deltas[i] = *global(F::counter_rva[i]) - before[i];
    r.cursor = o.head.cursor;
    r.position = crt_ftell(o.head.file);
    r.null = fx_out[0] == 0;
    if (!r.null && r.globals[0] != 0xdeadbeef) {
        auto* p = reinterpret_cast<const unsigned char*>(std::uintptr_t(fx_out[0]));
        r.bytes.assign(p, p + r.globals[0]);
        crt_free(const_cast<unsigned char*>(p));
    }
    (void)c;
    return r;
}
bool same(const Outcome& a, const Outcome& b) {
    return a.null == b.null && a.bytes == b.bytes && !std::memcmp(a.globals, b.globals, sizeof a.globals) &&
           !std::memcmp(a.deltas, b.deltas, sizeof a.deltas) && a.cursor == b.cursor && a.position == b.position;
}
Outcome run_case(const Case& c) {
    void* f = crt_fopen(c.path.c_str(), "rb");
    Object o;
    open_object(o, c, f);
    Outcome r = run(c, o);
    crt_fclose(f);
    return r;
}
// The claim spans back to the mapped original: the entry; the eighteen pool calls.
bool reader_pristine() {
    return fx::same_as_pristine(F::reader_rva, F::reader_length);
}
bool pool_pristine() {
    bool ok = fx::same_as_pristine(F::fopen_site_rva, 5);
    for (uint32_t s : F::close_site_rva) ok = ok && fx::same_as_pristine(s, 5);
    return ok;
}
bool spans_pristine() {
    return reader_pristine() && pool_pristine();
}
uint32_t call_target(uint32_t rva) {
    return fx::va(rva + 5) + fx::get32(rva + 1);
}
void install(const wchar_t* mode, bool pool) {
    F::fixture_reset();
    crt_route(false);
    SetEnvironmentVariableW(L"X3M_RESOURCE_READ", mode);
    SetEnvironmentVariableW(L"X3M_DAT_HANDLES", pool ? L"1" : nullptr);
    F::fixture_bind(fx::base);
    R::initialize();
    crt_route(true);
}
using OpenFn = void*(__cdecl*)(const char*, const char*);
using CloseFn = int(__cdecl*)(void*);
// The fallback probe: records the registers the original receives, then continues to it.
extern "C" {
uint32_t probe_regs[7], probe_flags = 0, probe_next = 0;
void fl_rr_probe();
}
asm(".text\n\t.globl _fl_rr_probe\n_fl_rr_probe:\n\t"
    "movl %eax,_probe_regs\n\tmovl %ecx,_probe_regs+4\n\tmovl %edx,_probe_regs+8\n\tmovl %ebx,_probe_regs+12\n\t"
    "movl %esi,_probe_regs+16\n\tmovl %edi,_probe_regs+20\n\tmovl %ebp,_probe_regs+24\n\t"
    "pushfl\n\tpopl _probe_flags\n\tjmp *_probe_next\n\t");
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3 || !fx::map_exe(argv[1])) {
        std::printf("FAIL map_exe\n");
        return 2;
    }
    HMODULE zlib = LoadLibraryA(argv[2]);
    if (!zlib || GetModuleHandleW(L"zlib1.dll") != zlib) {
        std::printf("FAIL zlib\n");
        return 2;
    }
    std::printf("IMAGE base=%08x size=%08x\n", fx::u32(fx::image), fx::image_size);
    z_deflate_init = reinterpret_cast<DeflateInit2>(GetProcAddress(zlib, "deflateInit2_"));
    z_deflate = reinterpret_cast<Deflate>(GetProcAddress(zlib, "deflate"));
    z_deflate_end = reinterpret_cast<DeflateEnd>(GetProcAddress(zlib, "deflateEnd"));
    z_crc32 = reinterpret_cast<Crc32>(GetProcAddress(zlib, "crc32"));
    fx::put32(F::inflate_init_slot_rva, fx::u32(reinterpret_cast<void*>(GetProcAddress(zlib, "inflateInit2_"))));
    fx::put32(F::inflate_slot_rva, fx::u32(reinterpret_cast<void*>(GetProcAddress(zlib, "inflate"))));
    fx::put32(F::inflate_end_slot_rva, fx::u32(reinterpret_cast<void*>(GetProcAddress(zlib, "inflateEnd"))));
    for (uint32_t rva : {F::inflate_init_slot_rva, F::inflate_slot_rva, F::inflate_end_slot_rva})
        std::memcpy(fx::pristine.data() + rva, fx::image + rva, 4);
    if (!z_deflate_init || !z_crc32) {
        std::printf("FAIL zlib_exports\n");
        return 2;
    }

    // ---- the synthetic catalogue .dat and loose files ----
    std::vector<unsigned char> st;
    add_record("cat_text_100", gzip(payload(100, true), 0), true);
    add_record("cat_text_5000_scrambled", scramble(gzip(payload(5000, true), 8), 0x41), true);
    add_record("cat_random_1024_fname", gzip(payload(1024, false), 8), true);
    add_record("cat_random_70000_extra_comment_hcrc", scramble(gzip(payload(70000, false), 4 | 0x10 | 2), 0x77), true);
    short_cursor_payload(false, st);
    add_record("cat_cursor_short", st, true);
    short_cursor_payload(true, st);
    add_record("cat_cursor_short_scrambled", st, true);
    add_record("cat_not_gzip", payload(3000, true), false);
    add_record("cat_empty", gzip({}, 0), false);
    add_record("cat_cut_stream", gzip(payload(9000, false), 0, 300), false);
    add_record("cat_text_200000_scrambled", scramble(gzip(payload(200000, true), 0, 0, 9), 0x13), true);
    add_record("cat_eof_last", gzip(payload(3000, true), 0), true); // ends exactly at the .dat end
    disk.push_back({"data\\01.dat", dat});
    add_loose("loose_text_4000", gzip(payload(4000, true), 0), true);
    add_loose("loose_random_30000_scrambled", scramble(gzip(payload(30000, false), 8), 0x99), true);
    add_loose("loose_to_eof_cut", gzip(payload(5000, false), 0, 100), false);
    std::printf("DATA records=%u dat_bytes=%u loose=3\n", unsigned(cases.size() - 3), unsigned(dat.size()));

    // (1) every reader/pool window change refused, nothing written.
    {
        unsigned flips = 0, refused = 0;
        for (int table = 0; table < 2; ++table) {
            const W::CodeWindow* ws = table ? W::pool_windows : W::reader_windows;
            const unsigned n = table ? sizeof W::pool_windows / sizeof W::pool_windows[0]
                                     : sizeof W::reader_windows / sizeof W::reader_windows[0];
            for (unsigned k = 0; k < n; ++k)
                for (uint32_t at : {ws[k].rva, ws[k].rva + ws[k].length / 2, ws[k].rva + ws[k].length - 1}) {
                    F::fixture_reset();
                    fx::image[at] ^= 0x01;
                    SetEnvironmentVariableW(L"X3M_RESOURCE_READ", L"fast");
                    SetEnvironmentVariableW(L"X3M_DAT_HANDLES", L"1");
                    F::fixture_bind(fx::base);
                    R::initialize();
                    fx::image[at] ^= 0x01; // the refusing transaction wrote nothing there (checked next)
                    ++flips;
                    const bool r = table ? !R::pool_installed() && !std::strcmp(F::pool_status(), "bytes_mismatch") &&
                                               pool_pristine()
                                         : !R::installed() && !std::strcmp(R::status(), "bytes_mismatch") &&
                                               reader_pristine();
                    refused += r;
                    F::fixture_reset();
                }
        }
        fx::check(flips && flips == refused, "every_window_change_refused");
        std::printf("RRFLIPS windows=%u cases=%u refused=%u\n",
                    unsigned(sizeof W::reader_windows / sizeof W::reader_windows[0] +
                             sizeof W::pool_windows / sizeof W::pool_windows[0]),
                    flips, refused);
    }

    // (2) the native route: the real relocated reader through the in-memory CRT.
    F::fixture_reset();
    crt_route(true);
    std::vector<Outcome> native;
    for (const Case& c : cases) native.push_back(run_case(c));
    unsigned native_ok = 0;
    for (unsigned i = 0; i < cases.size(); ++i) native_ok += !native[i].null;
    std::printf("NATIVE cases=%u nonnull=%u\n", unsigned(cases.size()), native_ok);
    fx::check(native_ok >= 12, "native_route_runs");

    // (3) install fast + pool: every write read back; the keep's window holds everywhere but the claimed rel32.
    const x3m::fl::music_windows::CodeWindow* keep_close = nullptr;
    for (const auto& w : x3m::fl::music_windows::keep_windows)
        if (!std::strcmp(w.name, "close_wrapper")) keep_close = &w;
    auto keep_window_ok = [&] {
        unsigned char now[0x40];
        std::memcpy(now, fx::image + keep_close->rva, keep_close->length);
        return x3m::fl::music_windows::matches(*keep_close, x3m::fl::music_windows::keep_fixups, now, fx::base);
    };
    fx::check(keep_close && keep_window_ok(), "keep_close_wrapper_verifies_before_install");
    SetLastError(0x1357);
    install(L"fast", true);
    fx::check(GetLastError() == 0x1357, "install_lasterror_kept");
    fx::check(R::installed() && !std::strcmp(R::status(), "active"), "reader_installed", R::status());
    fx::check(R::pool_installed() && !std::strcmp(F::pool_status(), "ok"), "pool_installed", F::pool_status());
    fx::check(fx::image[F::reader_rva] == 0xe9 && fx::same_as_pristine(F::reader_rva + 5, 1), "entry_jump_read_back");
    unsigned redirected = 0;
    for (uint32_t s : F::close_site_rva)
        redirected += fx::image[s] == 0xe8 && call_target(s) == fx::u32(reinterpret_cast<void*>(&R::x3m_pool_fclose));
    fx::check(redirected == F::close_site_count, "all_17_close_sites_redirected");
    fx::check(call_target(F::exit_close_rva) == fx::va(F::fclose_rva) && fx::same_as_pristine(F::exit_close_rva, 5),
              "exit_fcloseall_call_left_native");
    fx::check(call_target(F::fopen_site_rva) == fx::u32(reinterpret_cast<void*>(&R::x3m_pool_fopen)), "fopen_site_redirected");
    {
        // The keep's window: identical except the four rel32 bytes of the call at +0x2c (keep and gz_buffer check it
        // before this install; a re-check after it refuses, so the reader and pool claim last).
        bool rest_same = true;
        for (uint32_t at = keep_close->rva; at < keep_close->rva + keep_close->length; ++at)
            if (at < 0x10696d || at > 0x106970) rest_same = rest_same && fx::same_as_pristine(at, 1);
        fx::check(rest_same, "keep_close_wrapper_untouched_but_rel32");
        fx::check(!keep_window_ok(), "keep_close_wrapper_recheck_after_install_refuses");
    }
    std::printf("INSTALL arena_used=%u redirected=%u\n", x3m::engine_patch::arena_used(), redirected);

    // (4) fast: every case byte-identical to the native route; fallbacks run it unchanged.
    R::Statistics s0 = R::statistics();
    unsigned equal = 0, handled_expected = 0;
    std::vector<Outcome> fast;
    for (unsigned i = 0; i < cases.size(); ++i) {
        fast.push_back(run_case(cases[i]));
        const bool e = same(fast[i], native[i]);
        equal += e;
        handled_expected += cases[i].handled;
        if (!e) std::printf("FAIL fast_vs_native %s\n", cases[i].name);
        fx::check(fast[i].error == fx::sentinel_error, "fast_lasterror_kept", cases[i].name);
        if (cases[i].handled) fx::check(fast[i].mallocs == 1, "fast_one_game_allocation", cases[i].name);
    }
    R::Statistics s1 = R::statistics();
    fx::check(equal == cases.size(), "fast_identical_to_native");
    fx::check(s1.handled - s0.handled == handled_expected, "fast_handled_count");
    fx::check(s1.fallbacks - s0.fallbacks == cases.size() - handled_expected, "fast_fallback_count");
    fx::check(s1.cursor_short - s0.cursor_short >= 2, "fast_cursor_short_class");
    fx::check(s1.reasons[unsigned(R::Reason::NotGzip)] > s0.reasons[unsigned(R::Reason::NotGzip)] &&
                  s1.reasons[unsigned(R::Reason::Empty)] > s0.reasons[unsigned(R::Reason::Empty)] &&
                  s1.reasons[unsigned(R::Reason::Inflate)] > s0.reasons[unsigned(R::Reason::Inflate)],
              "fast_fallback_reasons");
    std::printf("FAST cases=%u equal=%u handled=%llu fallbacks=%llu cursor_short=%llu\n", unsigned(cases.size()), equal,
                s1.handled - s0.handled, s1.fallbacks - s0.fallbacks, s1.cursor_short - s0.cursor_short);
    // Sequential and out-of-order reads on one handle (each open seeks to its record, as the DAT open does).
    {
        void* f = crt_fopen("data\\01.dat", "rb");
        unsigned seq_equal = 0, n = 0;
        const unsigned order[] = {0, 1, 2, 3, 4, 5, 9, 10, 3, 0, 10, 5, 1, 9, 4, 2};
        for (unsigned i : order) {
            Object o;
            open_object(o, cases[i], f);
            seq_equal += same(run(cases[i], o), native[i]);
            ++n;
        }
        crt_fclose(f);
        fx::check(seq_equal == n, "sequential_and_seeking_identical");
        std::printf("SEEKING reads=%u equal=%u\n", n, seq_equal);
    }

    // (5) registers, EFLAGS, XMM7, x87 CW, MXCSR, LastError, errno on the handled path (DF set by the caller).
    {
        void* f = crt_fopen("data\\01.dat", "rb");
        Object o;
        open_object(o, cases[0], f);
        errno = 0x2d;
        fx_df = 1;
        fx::enter(fx::va(F::reader_rva), {0x1111, fx::u32(&o), 0x3333, 0x4444, 0x5555, 0x6666, 0x7777}, {});
        const DWORD e = GetLastError();
        fx::check(fx_out[0] != 0 && fx::kept({fx::ebx, fx::esi, fx::edi, fx::ebp}), "handled_nonvolatiles_kept");
        fx::check(fx_out_esp == fx_esp_before + 4, "handled_plain_ret");
        fx::check(fx::cpu_kept(), "handled_df_xmm7_fcw_mxcsr_kept");
        fx::check(e == fx::sentinel_error && errno == 0x2d, "handled_lasterror_errno_kept");
        crt_free(reinterpret_cast<void*>(std::uintptr_t(fx_out[0])));
        // The fallback path (an unopened object): the original receives the caller's EAX and ECX and every other
        // register and flag; it returns zero.
        void** next = F::fixture_next_slot();
        probe_next = fx::u32(*next);
        x3m::engine_patch::store_pointer(next, reinterpret_cast<void*>(&fl_rr_probe));
        init_object(o);
        fx_df = 1;
        fx::enter(fx::va(F::reader_rva), {0xa1a1a1a1, fx::u32(&o), 0x3333, 0x4444, 0x5555, 0x6666, 0x7777}, {});
        const DWORD e2 = GetLastError();
        x3m::engine_patch::store_pointer(next, reinterpret_cast<void*>(std::uintptr_t(probe_next)));
        fx::check(probe_regs[0] == 0xa1a1a1a1 && probe_regs[1] == fx::u32(&o) && probe_regs[2] == 0x3333 &&
                      probe_regs[3] == 0x4444 && probe_regs[4] == 0x5555 && probe_regs[5] == 0x6666 &&
                      probe_regs[6] == 0x7777 && (probe_flags & 0x400),
                  "fallback_original_gets_caller_eax_ecx_df");
        fx::check(fx_out[0] == 0 && fx::kept({fx::ebx, fx::esi, fx::edi, fx::ebp}) && fx::cpu_kept(),
                  "fallback_native_result_and_state");
        fx::check(e2 == fx::sentinel_error && errno == 0x2d, "fallback_lasterror_errno_kept");
        fx_df = 0;
        crt_fclose(f);
    }

    // (6) verify mode: the core decodes into scratch, calls the original through the ECX bridge, compares.
    {
        F::fixture_mode(R::Mode::Verify);
        const R::Statistics v0 = R::statistics();
        unsigned eq = 0;
        for (unsigned i = 0; i < cases.size(); ++i) eq += same(run_case(cases[i]), native[i]);
        const R::Statistics v1 = R::statistics();
        fx::check(eq == cases.size(), "verify_results_identical_to_native");
        fx::check(v1.verify_files - v0.verify_files == handled_expected &&
                      v1.verify_equal - v0.verify_equal == handled_expected && v1.verify_mismatched == v0.verify_mismatched,
                  "verify_agrees_with_fast");
        std::printf("VERIFY files=%llu equal=%llu mismatched=%llu\n", v1.verify_files - v0.verify_files,
                    v1.verify_equal - v0.verify_equal, v1.verify_mismatched - v0.verify_mismatched);
        F::fixture_mode(R::Mode::Fast);
    }

    // (7) pool: every close route pools the DAT handle or releases it; reuse across reopen.
    {
        const auto open = reinterpret_cast<OpenFn>(std::uintptr_t(call_target(F::fopen_site_rva)));
        const unsigned o0 = crt_opens, c0 = crt_closes;
        void* f1 = open("data\\01.dat", "rb");
        fx::check(f1 && crt_opens == o0 + 1, "pool_first_open_real");
        Object o;
        open_object(o, cases[1], f1);
        fx::check(same(run(cases[1], o), native[1]), "pool_read_identical");
        // The close wrapper (engine code, its fclose at 0x10696c): kept.
        fx_df = 0;
        fx::enter(fx::va(F::close_wrapper_rva), {0, fx::u32(&o), 0, 0, 0, 0, 0}, {});
        fx::check(crt_closes == c0 && !(o.head.flags & 1) && fx_out[0] == 1, "close_wrapper_pools");
        void* again = open("data\\01.dat", "rb");
        fx::check(again == f1 && crt_opens == o0 + 1, "reopen_reuses_handle");
        open_object(o, cases[9], again);
        fx::check(same(run(cases[9], o), native[9]), "reused_handle_read_identical");
        void* f2 = open("data\\01.dat", "rb");
        fx::check(f2 && f2 != f1 && crt_opens == o0 + 2, "concurrent_open_distinct_handle");
        // The destructor (engine code, its fclose at 0x1049c5): kept.
        Object o2;
        open_object(o2, cases[0], f2);
        fx::enter(fx::va(F::destructor_rva), {0, fx::u32(&o2), 0, 0, 0, 0, 0}, {});
        fx::check(crt_closes == c0 && !(o2.head.flags & 1), "destructor_pools");
        // f1 is still in use: closed through each redirected call's target in turn (all seventeen decode to the same
        // pool close, so this exercises that function once per site; the per-site redirection itself is the install
        // read-back above), reopened between.
        unsigned pooled = 0, released = 0;
        void* in_use = f1;
        for (uint32_t s : F::close_site_rva) {
            const auto close = reinterpret_cast<CloseFn>(std::uintptr_t(call_target(s)));
            errno = 0x2d;
            SetLastError(0x2468);
            const unsigned before = crt_closes;
            close(in_use);
            pooled += crt_closes == before && GetLastError() == 0x2468 && errno == 0x2d;
            in_use = open("data\\01.dat", "rb");
            // an unpooled handle (another open site) through the same call: released to the CRT.
            void* other = crt_fopen("data\\01.dat", "rb");
            const unsigned b2 = crt_closes;
            close(other);
            released += crt_closes == b2 + 1;
        }
        fx::check(pooled == F::close_site_count && released == F::close_site_count, "every_close_site_pools_or_releases");
        std::printf("CLOSES site_targets=%u pool_close_calls=%u pooled=%u released=%u\n", F::close_site_count,
                    2 * F::close_site_count, pooled, released);
        // EOF flag alone: still pooled; FL's error flag 0x10: released and the entry freed.
        const auto close = reinterpret_cast<CloseFn>(std::uintptr_t(call_target(F::close_site_rva[0])));
        static_cast<FakeFile*>(in_use)->flags |= 0x08;
        unsigned b = crt_closes;
        close(in_use);
        fx::check(crt_closes == b, "eof_flag_pooled");
        in_use = open("data\\01.dat", "rb");
        static_cast<FakeFile*>(in_use)->flags |= F::file_error_flag;
        b = crt_closes;
        close(in_use);
        fx::check(crt_closes == b + 1, "error_flag_0x10_released");
        const unsigned real_before = crt_opens;
        void* fresh = open("data\\01.dat", "rb");
        void* fresh2 = open("data\\01.dat", "rb"); // the one kept handle (f2) is reused; the errored one is gone
        fx::check(crt_opens == real_before + 1, "errored_handle_not_reused");
        close(fresh);
        close(fresh2);
        // A non-"rb" open through the pooled site passes through and is never kept.
        void* text = open("data\\01.dat", "r");
        b = crt_closes;
        close(text);
        fx::check(crt_closes == b + 1, "non_rb_open_not_pooled");
        const R::PoolStatistics p = R::pool_statistics();
        std::printf("POOL opens=%llu reused=%llu real_opens=%llu closes=%llu kept=%llu real_closes=%llu errors=%llu held=%u\n",
                    p.opens, p.reused, p.real_opens, p.closes, p.kept, p.real_closes, p.errors, p.held);
        fx::check(p.errors == 1 && p.reused >= 20, "pool_statistics");
        R::report();
    }

    // (8) per-read cost: the native route vs the stub (fast): 200000-byte text, 70000-byte random (scrambled), 5000-byte
    // text (scrambled) and 100-byte records.
    for (unsigned which : {9u, 3u, 1u, 0u}) {
        LARGE_INTEGER fq, t0, t1, t2, t3;
        QueryPerformanceFrequency(&fq);
        const unsigned reps = which == 0 ? 400 : 40;
        for (unsigned r = 0; r < 4; ++r) run_case(cases[which]); // warm both routes alike
        const unsigned m0 = crt_mallocs;
        const R::Statistics p0 = R::statistics();
        QueryPerformanceCounter(&t0);
        for (unsigned r = 0; r < reps; ++r) run_case(cases[which]);
        QueryPerformanceCounter(&t1);
        const R::Statistics p1 = R::statistics();
        const unsigned fast_allocs = crt_mallocs - m0;
        auto phase = [&](uint64_t a, uint64_t b) { return double(b - a) * 1e6 / double(fq.QuadPart) / reps; };
        // native: the claimed entry put back for the timing loop, then claimed again.
        F::fixture_reset();
        crt_route(true);
        for (unsigned r = 0; r < 4; ++r) run_case(cases[which]);
        QueryPerformanceCounter(&t2);
        for (unsigned r = 0; r < reps; ++r) run_case(cases[which]);
        QueryPerformanceCounter(&t3);
        const double fast_us = double(t1.QuadPart - t0.QuadPart) * 1e6 / double(fq.QuadPart) / reps;
        const double native_us = double(t3.QuadPart - t2.QuadPart) * 1e6 / double(fq.QuadPart) / reps;
        std::printf("PERF case=%s bytes=%u fast_us=%.2f native_us=%.2f decode_us=%.2f read_us=%.2f scan_us=%.2f "
                    "alloc_us=%.2f inflate_us=%.2f game_allocs_per_read=%.2f\n",
                    cases[which].name, unsigned(native[which].bytes.size()), fast_us, native_us,
                    phase(p0.ticks, p1.ticks), phase(p0.read_ticks, p1.read_ticks), phase(p0.scan_ticks, p1.scan_ticks),
                    phase(p0.alloc_ticks, p1.alloc_ticks), phase(p0.inflate_ticks, p1.inflate_ticks),
                    double(fast_allocs) / reps);
        install(L"fast", true);
    }
    fx::check(R::installed() && R::pool_installed(), "reinstall_after_timing");

    // (9) partial install: each fault point rolls back every write of its transaction; the other stays.
    {
        unsigned steps = 0, rolled = 0;
        for (unsigned k = 1; k <= 2 + 1 + F::close_site_count; ++k) {
            F::fixture_reset();
            crt_route(false);
            F::fixture_fail_step = k;
            SetEnvironmentVariableW(L"X3M_RESOURCE_READ", L"fast");
            SetEnvironmentVariableW(L"X3M_DAT_HANDLES", L"1");
            F::fixture_bind(fx::base);
            SetLastError(0x77);
            R::initialize();
            const bool error_kept = GetLastError() == 0x77;
            bool ok;
            if (k <= 2)
                ok = !R::installed() && fx::same_as_pristine(F::reader_rva, F::reader_length) && R::pool_installed();
            else {
                bool pool_pristine = fx::same_as_pristine(F::fopen_site_rva, 5);
                for (uint32_t s : F::close_site_rva) pool_pristine = pool_pristine && fx::same_as_pristine(s, 5);
                ok = R::installed() && !R::pool_installed() && pool_pristine &&
                     !std::strcmp(F::pool_status(), "readback_failed");
            }
            ++steps;
            rolled += ok && error_kept;
            if (!ok) std::printf("FAIL rollback step=%u reader=%s pool=%s\n", k, R::status(), F::pool_status());
        }
        fx::check(steps == rolled, "every_fault_point_rolls_back");
        std::printf("ROLLBACK steps=%u rolled_back=%u\n", steps, rolled);
    }

    // (10) late window: refused, nothing written (the window stays closed for the rest of the process).
    F::fixture_reset();
    crt_route(false);
    x3m::engine_patch::close_install_window("fixture");
    SetEnvironmentVariableW(L"X3M_RESOURCE_READ", L"fast");
    SetEnvironmentVariableW(L"X3M_DAT_HANDLES", L"1");
    F::fixture_bind(fx::base);
    R::initialize();
    fx::check(!R::installed() && !R::pool_installed() && !std::strcmp(R::status(), "late_claim") &&
                  !std::strcmp(F::pool_status(), "late_claim") && spans_pristine(),
              "late_window_refused");
    fx::check(crt_dead_use == 0, "no_closed_handle_used");
    unsigned open_now = 0;
    for (const auto& f : files) open_now += f.open;
    fx::check(open_now == 0, "every_handle_released_after_drain");
    std::printf("CRT opens=%u closes=%u open_now=%u dead_use=%u\n", crt_opens, crt_closes, open_now, crt_dead_use);
    return fx::result();
}
