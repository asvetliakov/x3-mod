// Native fixture of the FL loading quintet's claimed sites (src/engine/fl/gz_buffer_fl.cpp, crypt_cache_fl.cpp;
// docs/reverse-engineering/fl/loading-hooks.md, "Port (2026-11-03)"). Built with -DX3M_EDITION_FL=1
// -DX3M_GZ_BUFFER_FL_FIXTURE -DX3M_CRYPT_CACHE_FL_FIXTURE -DX3M_LOADING_FL_FIXTURE by
// verification/analysis/test_fl_loading.py (no force-include; the modules take a fixture base and fake exports) and run
// natively through WSL interop: ./fl_loading.exe <X3FL.exe>. It maps and relocates the installed X3FL.exe to a private
// 64 KB-aligned base (fl_music_fixture.h), binds the six zlib and four CryptoAPI entries to fakes, and exercises:
// gz_buffer claim / read-back / rollback, LastError kept across install, a buffered read byte-identical to the fake
// stream, nonvolatiles / ESP / XMM7 / x87 CW / MXCSR / LastError kept across a read, a buffered hit and a close entered
// from the register harness, the gzclose share in both install orders (the keep modelled by a hand-written forward stub;
// the real keep's share code in music_keep_fl.cpp does not run here); crypt_cache admission from the checker's
// return sites (hit / miss, an unadmitted caller forwarding, the sibling signer retiring the cache), LastError
// preservation. The X3FL.exe bytes are never copied into the repository.
#include "fl_music_fixture.h"
#include "../../src/engine/fl/crypt_cache_fl.h"
#include "../../src/engine/fl/crypt_cache_sites_fl.h"
#include "../../src/engine/fl/gz_buffer_fl.h"
#include "../../src/engine/fl/gz_buffer_sites_fl.h"
#include "../../src/engine/fl/gz_close_share_fl.h"
#include "../../src/engine/fl/import_slots_fl.h"
#include "../../src/proxy/crypt_cache.h"
#include "../../src/proxy/gz_buffer.h"
#include <wincrypt.h>

namespace GZ = x3m::fl::gz_buffer_fl;
namespace CC = x3m::fl::crypt_cache_fl;
namespace share = x3m::fl::gz_close_share;
namespace slots = x3m::fl::import_slots;
using std::uint32_t;

namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    fx::row(format, args);
    va_end(args);
}
HANDLE log_handle() noexcept {
    return reinterpret_cast<HANDLE>(std::uintptr_t(4));
}
namespace session_log {
void vlog(const char* format, va_list args) noexcept {
    fx::row(format, args);
}
}
}

// ---- fake zlib: a backing byte stream exercised by the buffer ----
namespace {
struct FakeFile {
    const unsigned char* data;
    unsigned size, pos, max_read;
    bool write;
};
FakeFile fakes[4];
unsigned fake_count = 0;
unsigned char stream_bytes[4096];
unsigned real_opens = 0, real_reads = 0, real_closes = 0;
extern "C" void* __cdecl fake_gzopen(const char* path, const char* mode) {
    ++real_opens;
    if (fake_count >= 4) return nullptr;
    FakeFile* f = &fakes[fake_count++];
    f->data = stream_bytes;
    f->size = sizeof stream_bytes;
    f->pos = 0;
    f->max_read = 400; // short real reads like a chunked source
    f->write = mode && mode[0] == 'w';
    (void)path;
    return f;
}
extern "C" int __cdecl fake_gzread(void* file, void* out, unsigned n) {
    ++real_reads;
    FakeFile* f = static_cast<FakeFile*>(file);
    unsigned left = f->size - f->pos;
    unsigned take = n < left ? n : left;
    if (take > f->max_read) take = f->max_read;
    std::memcpy(out, f->data + f->pos, take);
    f->pos += take;
    return int(take);
}
extern "C" LONG __cdecl fake_gzseek(void* file, LONG off, int whence) {
    FakeFile* f = static_cast<FakeFile*>(file);
    LONG base = whence == SEEK_CUR ? LONG(f->pos) : 0;
    f->pos = unsigned(base + off);
    return LONG(f->pos);
}
extern "C" LONG __cdecl fake_gztell(void* file) {
    return LONG(static_cast<FakeFile*>(file)->pos);
}
extern "C" int __cdecl fake_gzgetc(void* file) {
    FakeFile* f = static_cast<FakeFile*>(file);
    return f->pos < f->size ? f->data[f->pos++] : -1;
}
unsigned fake_close_calls = 0;
extern "C" int __cdecl fake_gzclose(void* file) {
    ++real_closes;
    ++fake_close_calls;
    (void)file;
    return 0;
}
void* zlib_exports[7] = {reinterpret_cast<void*>(&fake_gzopen),  reinterpret_cast<void*>(&fake_gzread),
                         reinterpret_cast<void*>(&fake_gzseek),  reinterpret_cast<void*>(&fake_gzgetc),
                         reinterpret_cast<void*>(&fake_gztell),  reinterpret_cast<void*>(&fake_gzclose),
                         nullptr};
uint32_t addr(const void* p) {
    return uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
// The gz slots as the loader binds them: each IAT slot holds its zlib export.
void bind_gz_slots() {
    using namespace x3m::fl::gz_buffer_fl;
    fx::put32(imports[open_index].slot_rva, addr(zlib_exports[0]));
    fx::put32(imports[read_index].slot_rva, addr(zlib_exports[1]));
    fx::put32(imports[seek_index].slot_rva, addr(zlib_exports[2]));
    fx::put32(imports[getc_index].slot_rva, addr(zlib_exports[3]));
    fx::put32(imports[tell_index].slot_rva, addr(zlib_exports[4]));
    fx::put32(imports[close_index].slot_rva, addr(zlib_exports[5]));
    fx::put32(gzwrite_slot_rva, addr(reinterpret_cast<void*>(&fake_gzclose)));
}
}

// ---- fake CryptoAPI ----
namespace {
unsigned native_acquire = 0, native_release = 0, native_import = 0, native_destroy = 0;
HCRYPTPROV next_prov = 0x5000;
HCRYPTKEY next_key = 0x6000;
extern "C" BOOL WINAPI fake_acquire(HCRYPTPROV* out, LPCSTR, LPCSTR, DWORD, DWORD flags) {
    ++native_acquire;
    if (flags == CRYPT_DELETEKEYSET) {
        SetLastError(0);
        return TRUE;
    }
    if (out) *out = ++next_prov;
    SetLastError(0);
    return TRUE;
}
extern "C" BOOL WINAPI fake_release(HCRYPTPROV, DWORD) {
    ++native_release;
    return TRUE;
}
extern "C" BOOL WINAPI fake_import(HCRYPTPROV, const BYTE*, DWORD, HCRYPTKEY, DWORD, HCRYPTKEY* out) {
    ++native_import;
    if (out) *out = ++next_key;
    return TRUE;
}
extern "C" BOOL WINAPI fake_destroy(HCRYPTKEY) {
    ++native_destroy;
    return TRUE;
}
void* crypt_exports[4] = {reinterpret_cast<void*>(&fake_acquire), reinterpret_cast<void*>(&fake_release),
                          reinterpret_cast<void*>(&fake_import), reinterpret_cast<void*>(&fake_destroy)};
void bind_crypt_slots() {
    using namespace x3m::fl::crypt_cache_fl;
    fx::put32(imports[acquire_index].slot_rva, addr(crypt_exports[0]));
    fx::put32(imports[release_index].slot_rva, addr(crypt_exports[1]));
    fx::put32(imports[import_index].slot_rva, addr(crypt_exports[2]));
    fx::put32(imports[destroy_index].slot_rva, addr(crypt_exports[3]));
}
// A 276-byte RSA-2048 PUBLICKEYBLOB the cache admits.
BYTE blob[276];
void make_blob() {
    std::memset(blob, 0, sizeof blob);
    blob[0] = PUBLICKEYBLOB;
    blob[1] = CUR_BLOB_VERSION;
    blob[5] = BYTE(CALG_RSA_KEYX >> 8);
    blob[8] = 'R';
    blob[9] = 'S';
    blob[10] = 'A';
    blob[11] = '1';
    blob[13] = 8;
}
// The four cache entries now sitting in the slots after install (resolved by RVA).
void* slot_entry(unsigned idx) {
    return reinterpret_cast<void*>(std::uintptr_t(fx::get32(x3m::fl::crypt_cache_fl::imports[idx].slot_rva)));
}
using ImpShim = BOOL(WINAPI*)(HCRYPTPROV, const BYTE*, DWORD, HCRYPTKEY, DWORD, HCRYPTKEY*);
using DesShim = BOOL(WINAPI*)(HCRYPTKEY);
using RelShim = BOOL(WINAPI*)(HCRYPTPROV, DWORD);
using AcqEntry = BOOL(WINAPI*)(HCRYPTPROV*, LPCSTR, LPCSTR, DWORD, DWORD);
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2 || !fx::map_exe(argv[1])) {
        std::printf("FAIL map_exe\n");
        return 2;
    }
    std::printf("IMAGE base=%08x size=%08x\n", fx::u32(fx::image), fx::image_size);
    for (unsigned i = 0; i < sizeof stream_bytes; ++i) stream_bytes[i] = (unsigned char)((i * 37 + 11) & 0xff);
    make_blob();

    // ================= gz_buffer =================
    SetEnvironmentVariableW(L"X3M_GZ_BUFFER", L"1");
    GZ::fixture_bind(fx::base, zlib_exports);
    namespace G = x3m::fl::gz_buffer_fl;

    // (1) every window change refused, then every named-import slot mismatch refused.
    {
        unsigned flips = 0, refused = 0;
        for (const auto& w : x3m::fl::loading_windows::gz_windows) {
            for (uint32_t at : {w.rva, w.rva + w.length / 2, w.rva + w.length - 1}) {
                unsigned char save = fx::image[at];
                fx::image[at] ^= 0x01;
                fx::flush();
                bind_gz_slots();
                G::fixture_reset();
                GZ::initialize();
                ++flips;
                refused += !std::strcmp(G::state(), "bytes_mismatch");
                fx::image[at] = save;
                fx::flush();
            }
        }
        fx::check(flips && flips == refused, "gz_every_window_change_refused");
        std::printf("GZFLIPS windows=%u cases=%u refused=%u\n",
                    unsigned(sizeof x3m::fl::loading_windows::gz_windows / sizeof(x3m::fl::loading_windows::gz_windows[0])),
                    flips, refused);
    }

    // (2) buffer-first install: all six slots written, read back; share globals published; LastError preserved.
    bind_gz_slots();
    G::fixture_reset();
    SetLastError(0x1357);
    bool ok = GZ::initialize();
    fx::check(ok && !std::strcmp(G::state(), "ok"), "gz_install_ok", G::state());
    fx::check(GetLastError() == 0x1357, "gz_install_lasterror_kept");
    fx::check(fx::get32(G::imports[G::open_index].slot_rva) == addr(reinterpret_cast<void*>(&x3m::gz_buffer::open)),
              "gz_open_slot_written");
    fx::check(fx::get32(G::imports[G::close_index].slot_rva) == addr(reinterpret_cast<void*>(&x3m::gz_buffer::close)),
              "gz_close_slot_written");
    fx::check(share::buffer_holds_slot == 1 && share::buffer_forward == addr(reinterpret_cast<void*>(&fake_gzclose)),
              "gz_close_share_published");

    // (3) a buffered read is byte-identical to the fake stream through the real import entries.
    {
        auto open = reinterpret_cast<x3m::gz_buffer::OpenFn>(std::uintptr_t(fx::get32(G::imports[G::open_index].slot_rva)));
        auto read = reinterpret_cast<x3m::gz_buffer::ReadFn>(std::uintptr_t(fx::get32(G::imports[G::read_index].slot_rva)));
        auto close = reinterpret_cast<x3m::gz_buffer::CloseFn>(std::uintptr_t(fx::get32(G::imports[G::close_index].slot_rva)));
        void* h = open("res.pck", "rb");
        unsigned char got[1500];
        unsigned total = 0;
        for (;;) {
            int n = read(h, got + total, 7); // tiny reads, served from the buffer
            if (n <= 0) break;
            total += unsigned(n);
            if (total + 7 > sizeof got) break;
        }
        bool identical = total > 0 && !std::memcmp(got, stream_bytes, total);
        fx::check(identical, "gz_buffered_read_byte_identical");
        fx::check(real_reads >= 1, "gz_real_read_chunked");
        close(h);
        fx::check(fake_close_calls >= 1, "gz_close_forwarded");
        // The slot entries entered from the register harness (cdecl: DF clear on entry): nonvolatiles, ESP, XMM7, the
        // x87 control word, MXCSR and the caller's LastError kept across a buffered read and a close.
        auto state_kept = [] {
            return fx::kept({fx::ebx, fx::esi, fx::edi, fx::ebp}) && fx_out_esp == fx_esp_before + 4 &&
                   !std::memcmp(fx_in_xmm, fx_out_xmm, 16) && fx_out_fcw == fx_in_fcw && fx_out_mxcsr == fx_in_mxcsr;
        };
        void* h2 = open("res2.pck", "rb");
        static unsigned char small[32];
        fx_df = 0;
        fx::Call c = fx::enter(addr(reinterpret_cast<void*>(read)), {0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666,
                                                                     0x7777},
                               {addr(h2), addr(small), 16});
        fx::check(fx_out[0] == 16 && !std::memcmp(small, stream_bytes, 16) && state_kept() &&
                      c.error == fx::sentinel_error,
                  "gz_read_registers_lasterror_kept");
        c = fx::enter(addr(reinterpret_cast<void*>(read)), {0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666, 0x7777},
                      {addr(h2), addr(small), 16});
        fx::check(fx_out[0] == 16 && !std::memcmp(small, stream_bytes + 16, 16) && state_kept() &&
                      c.error == fx::sentinel_error,
                  "gz_buffered_hit_registers_lasterror_kept");
        const unsigned closes = fake_close_calls;
        c = fx::enter(addr(reinterpret_cast<void*>(close)), {0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666, 0x7777},
                      {addr(h2)});
        fx::check(fake_close_calls == closes + 1 && state_kept() && c.error == fx::sentinel_error,
                  "gz_close_registers_lasterror_kept");
    }

    // (4) keep-first install: the keep holds the close slot (its stub forwards to zlib's gzclose through a word);
    //     gz_buffer detects route=keep and chains behind the keep's forward word; a close reaches gz_buffer then zlib.
    static uint32_t keep_forward_word = addr(reinterpret_cast<void*>(&fake_gzclose));
    static uint32_t keep_stub_target = 0;
    // a tiny "keep stub": jmp [keep_forward_word]
    {
        unsigned char* stub = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        stub[0] = 0xff; // jmp dword ptr [abs]
        stub[1] = 0x25;
        uint32_t p = addr(&keep_forward_word);
        std::memcpy(stub + 2, &p, 4);
        FlushInstructionCache(GetCurrentProcess(), stub, 6);
        keep_stub_target = addr(stub);
    }
    bind_gz_slots();
    fx::put32(G::imports[G::close_index].slot_rva, keep_stub_target); // the keep owns the close slot
    share::keep_stub = keep_stub_target;
    share::keep_forward = &keep_forward_word;
    G::fixture_reset();
    ok = GZ::initialize();
    fx::check(ok && !std::strcmp(G::state(), "ok"), "gz_keep_first_install_ok", G::state());
    fx::check(fx::get32(G::imports[G::close_index].slot_rva) == keep_stub_target, "gz_keep_first_slot_untouched");
    fx::check(keep_forward_word == addr(reinterpret_cast<void*>(&x3m::gz_buffer::close)), "gz_keep_first_chained");
    {
        auto open = reinterpret_cast<x3m::gz_buffer::OpenFn>(std::uintptr_t(fx::get32(G::imports[G::open_index].slot_rva)));
        unsigned before = fake_close_calls;
        void* h = open("a.pck", "rb");
        // close through the keep stub -> gz_buffer::close -> fake_gzclose
        reinterpret_cast<x3m::gz_buffer::CloseFn>(std::uintptr_t(keep_stub_target))(h);
        fx::check(fake_close_calls == before + 1, "gz_keep_first_close_through_chain");
    }
    share::keep_stub = 0;
    share::keep_forward = nullptr;

    // (5) rollback: a slot write fails midway; every earlier write is put back, state refuses.
    bind_gz_slots();
    G::fixture_reset();
    slots::fixture_fail_exchange = 3; // the third claim (gzseek) fails
    ok = GZ::initialize();
    fx::check(!ok && std::strcmp(G::state(), "ok"), "gz_partial_install_refused", G::state());
    bool restored = true;
    for (const auto& im : G::imports)
        if (im.slot_rva != G::gzwrite_slot_rva) {}
    restored = fx::get32(G::imports[G::close_index].slot_rva) == addr(reinterpret_cast<void*>(&fake_gzclose)) &&
               fx::get32(G::imports[G::tell_index].slot_rva) == addr(zlib_exports[4]) &&
               fx::get32(G::imports[G::open_index].slot_rva) == addr(zlib_exports[0]);
    fx::check(restored, "gz_rollback_restored_slots");
    slots::fixture_fail_exchange = 0;

    // ================= crypt_cache =================
    SetEnvironmentVariableW(L"X3M_CRYPT_CACHE", L"1");
    CC::fixture_bind(fx::base, crypt_exports, x3m::fl::crypt_cache_fl::container_string);
    namespace C = x3m::fl::crypt_cache_fl;

    bind_crypt_slots();
    C::fixture_reset();
    SetLastError(0x2468);
    ok = CC::initialize();
    fx::check(ok && !std::strcmp(C::state(), "ok"), "crypt_install_ok", C::state());
    fx::check(GetLastError() == 0x2468, "crypt_install_lasterror_kept");

    // The cache entries sit in the slots; the admitted caller RVA is set through a fixture seam (crypt_cache_fl's
    // admitted() substitutes it for __builtin_return_address in a fixture build), so each call is as if it returned to
    // that checker site. Production uses the real return address; this seam only chooses the compared RVA.
    const char* provider = "Microsoft Base Cryptographic Provider v1.0";
    const char* container = x3m::fl::crypt_cache_fl::container_string;
    auto acq = reinterpret_cast<AcqEntry>(slot_entry(C::acquire_index));
    auto imp = reinterpret_cast<ImpShim>(slot_entry(C::import_index));
    auto des = reinterpret_cast<DesShim>(slot_entry(C::destroy_index));
    auto rel = reinterpret_cast<RelShim>(slot_entry(C::release_index));
    auto at = [](uint32_t rva) { CC::fixture_call_site(rva); };

    // First signature pass (miss): acquire new keyset, import, destroy, release, final delete.
    HCRYPTPROV p1 = 0;
    HCRYPTKEY k1 = 0;
    unsigned a0 = native_acquire, i0 = native_import;
    at(0x0f8bdb);
    acq(&p1, container, provider, PROV_RSA_FULL, CRYPT_NEWKEYSET);
    at(0x0f8c08);
    imp(p1, blob, 276, 0, 0, &k1);
    at(0x0f8db1);
    des(k1);
    at(0x0f8dbd);
    rel(p1, 0);
    at(0x0f8dd6);
    acq(&p1, container, provider, PROV_RSA_FULL, CRYPT_DELETEKEYSET);
    bool first_native = native_acquire > a0 && native_import > i0;
    fx::check(first_native, "crypt_first_pass_native");

    // Second pass (hit): the acquire and import are served from the cache; no native calls.
    unsigned a1 = native_acquire, i1 = native_import;
    HCRYPTPROV p2 = 0;
    HCRYPTKEY k2 = 0;
    SetLastError(0x5555); // a cache hit preserves the caller's incoming LastError (no native call)
    at(0x0f8bdb);
    acq(&p2, container, provider, PROV_RSA_FULL, CRYPT_NEWKEYSET);
    fx::check(GetLastError() == 0x5555, "crypt_hit_lasterror_kept");
    at(0x0f8c08);
    imp(p2, blob, 276, 0, 0, &k2);
    fx::check(p2 == p1 && k2 == k1 && native_acquire == a1 && native_import == i1, "crypt_second_pass_hit");
    at(0x0f8db1);
    des(k2);
    at(0x0f8dbd);
    rel(p2, 0);
    at(0x0f8dd6);
    acq(&p2, container, provider, PROV_RSA_FULL, CRYPT_DELETEKEYSET);

    // An unadmitted acquire (no gate RVA) naming the scratch container retires the cache; the next admitted pass misses.
    at(0);
    acq(&p2, container, provider, PROV_RSA_FULL, CRYPT_DELETEKEYSET);
    fx::check(CC::fixture_foreign_acquires() >= 1, "crypt_foreign_acquire_counted");
    // Sampled after the unadmitted acquire (which reaches the native function itself): the admitted acquire must now
    // reach it too, with a fresh provider, where the second pass above was served from the cache.
    const unsigned a2 = native_acquire;
    HCRYPTPROV p3 = 0;
    at(0x0f8bdb);
    acq(&p3, container, provider, PROV_RSA_FULL, CRYPT_NEWKEYSET);
    fx::check(native_acquire == a2 + 1 && p3 != p1, "crypt_after_foreign_misses");
    at(0x0f8dbd);
    rel(p3, 0);
    at(0);

    CC::report("session");
    return fx::result();
}
