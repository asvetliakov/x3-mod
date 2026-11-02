// Native fixture of the FL material-strength texture hook (src/proxy/d3dx_texture_hook.cpp;
// docs/architecture/overlay-material-strength.md, "Farnham's Legacy"). MinGW i686, run natively through WSL interop
// by verification/analysis/test_fl_material_strength_hook.py. The executable imports the system d3dx9_43.dll (as
// X3FL.exe does) and d3d9.dll; it creates a hidden-window HAL device (D3D9Ex when another process holds exclusive
// fullscreen) for the real D3DX texture loads (D3DPOOL_SYSTEMMEM).
//   1. Resolution over this executable: the slot the hook finds is the linker's __imp_ entry, named d3dx9_43.dll.
//   2. Refusals before any write: no_observer, foreign_redirect (slot pre-set to a foreign function; untouched).
//   3. Install, then real D3DX through the IAT: a marked DDS reaches the observer (device, bytes, size, texture,
//      strength lane); unmarked and failing calls forward with the same HRESULT, LastError, MXCSR and x87 control as
//      the direct original; the observer's own LastError/MXCSR/x87 work never leaks.
//   4. CPU-state boundary with a probe original: the original sees the caller's LastError, MXCSR, x87 control word
//      and stack top; the caller sees the original's.
//   5. Rollback: restore() puts the original back; a failed protection restore rolls the install back.
//   6. Synthetic images (section views): case-insensitive d3dx9_NN.dll, import_absent, ambiguous_import,
//      dll_not_loaded, slot_misaligned, foreign_redirect, bad_image, and slot_not_writable on a read-only view.
//   7. Late window: refused after engine_patch::close_install_window, slot untouched.
// Prints one RESULT line; failures as FAIL rows.
#include "../../src/proxy/d3dx_texture_hook.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/lod_texture_mark.h"
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace H = x3m::d3dx_texture_hook;
namespace M = x3m::lod_texture_mark;
using CreateFn = decltype(&D3DXCreateTextureFromFileInMemoryEx);
extern "C" PVOID iat_entry asm("__imp__D3DXCreateTextureFromFileInMemoryEx@60");

static unsigned checks = 0, failures = 0;
static void check(bool ok, const char* what, unsigned long a = 0, unsigned long b = 0) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s (%08lx %08lx)\n", what, a, b);
    }
}
static bool same(const char* a, const char* b) {
    return a && b && !std::strcmp(a, b);
}

// Observer: records what it saw, then does API, x87 and MXCSR work of its own that must not reach the caller.
struct Seen {
    unsigned calls = 0, marked = 0;
    IDirect3DDevice9* device = nullptr;
    const void* data = nullptr;
    UINT size = 0;
    IDirect3DTexture9* texture = nullptr;
    unsigned lane = 0;
} seen;
static void observer(IDirect3DDevice9* d, const void* data, UINT size, IDirect3DTexture9* texture) {
    ++seen.calls;
    seen.device = d;
    seen.data = data;
    seen.size = size;
    seen.texture = texture;
    M::Mark mark;
    if (M::parse(data, size, mark)) {
        ++seen.marked;
        seen.lane = M::lane_of(mark);
    }
    SetLastError(5);
    volatile long double x = 1.5L;
    x = x * 3.0L; // x87 arithmetic inside the observer
    const unsigned mx = 0x1f80;
    const unsigned short cw = 0x037f;
    asm volatile("ldmxcsr %0\n\tfldcw %1" ::"m"(mx), "m"(cw));
}

// One call with a chosen caller state; returns what the caller observes afterwards.
struct Outcome {
    HRESULT hr = 0;
    DWORD error = 0;
    unsigned mxcsr = 0;
    unsigned short cw = 0, top = 0;
};
static Outcome call_with_state(CreateFn f, IDirect3DDevice9* d, const void* data, UINT size,
                               IDirect3DTexture9** out, bool push_one) {
    Outcome o;
    unsigned saved_mx = 0;
    unsigned short saved_cw = 0;
    asm volatile("stmxcsr %0\n\tfnstcw %1" : "=m"(saved_mx), "=m"(saved_cw));
    const unsigned mx_in = 0x7f80;      // round toward zero, all masked
    const unsigned short cw_in = 0x0e7f; // truncate, 53-bit, all masked
    asm volatile("fldcw %0\n\tldmxcsr %1" ::"m"(cw_in), "m"(mx_in));
    if (push_one) asm volatile("fld1" ::: "memory");
    SetLastError(0x00c0ffee);
    o.hr = f(d, data, size, 0, 0, 1, 0, D3DFMT_UNKNOWN, D3DPOOL_SYSTEMMEM, D3DX_DEFAULT, D3DX_DEFAULT, 0, nullptr,
             nullptr, out);
    o.error = GetLastError();
    unsigned short sw = 0;
    asm volatile("stmxcsr %0\n\tfnstcw %1\n\tfnstsw %2" : "=m"(o.mxcsr), "=m"(o.cw), "=m"(sw));
    o.top = (sw >> 11) & 7;
    asm volatile("fninit\n\tfldcw %0\n\tldmxcsr %1" ::"m"(saved_cw), "m"(saved_mx));
    return o;
}

// Probe original for the boundary check.
struct ProbeSeen {
    DWORD error = 0;
    unsigned mxcsr = 0;
    unsigned short cw = 0, top = 0;
    unsigned calls = 0;
} probe;
HRESULT probe_result = S_FALSE;
IDirect3DTexture9* const probe_texture = reinterpret_cast<IDirect3DTexture9*>(0x1234560);
static HRESULT WINAPI probe_original(IDirect3DDevice9*, const void*, UINT, UINT, UINT, UINT, DWORD, D3DFORMAT,
                                     D3DPOOL, DWORD, DWORD, D3DCOLOR, D3DXIMAGE_INFO*, PALETTEENTRY*,
                                     IDirect3DTexture9** out) {
    probe.error = GetLastError();
    unsigned short sw = 0;
    asm volatile("stmxcsr %0\n\tfnstcw %1\n\tfnstsw %2" : "=m"(probe.mxcsr), "=m"(probe.cw), "=m"(sw));
    probe.top = (sw >> 11) & 7;
    ++probe.calls;
    if (out) *out = probe_texture;
    const unsigned mx = 0x9fc0;
    const unsigned short cw = 0x037f;
    asm volatile("fldcw %0\n\tfldz\n\tldmxcsr %1" ::"m"(cw), "m"(mx) : "memory");
    SetLastError(0xbeef);
    return probe_result;
}
static HRESULT WINAPI foreign_create(IDirect3DDevice9*, const void*, UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
                                     DWORD, DWORD, D3DCOLOR, D3DXIMAGE_INFO*, PALETTEENTRY*, IDirect3DTexture9**) {
    return E_NOTIMPL;
}
// A hook chaining on top of the stub while the slot page is still writable: keeps the stub as its next target.
static PVOID chained_next = nullptr;
static void chain_on_top(PVOID* slot) {
    chained_next = *slot;
    *slot = reinterpret_cast<PVOID>(&foreign_create);
}
static bool write_slot(PVOID* slot, PVOID value) {
    DWORD old = 0, ignored = 0;
    if (!VirtualProtect(slot, sizeof(PVOID), PAGE_READWRITE, &old)) return false;
    *slot = value;
    return VirtualProtect(slot, sizeof(PVOID), old, &ignored) != FALSE;
}

static std::vector<unsigned char> dds(bool marked) {
    std::vector<unsigned char> b(128 + 64, 0);
    const auto put = [&](unsigned at, DWORD v) { std::memcpy(&b[at], &v, 4); };
    put(0, 0x20534444u);
    put(4, 124);
    put(8, 0x1 | 0x2 | 0x4 | 0x1000);
    put(12, 4);
    put(16, 4);
    put(20, 16);
    if (marked) {
        put(32, M::magic);
        put(36, M::version);
        put(40, 3u | M::lane_w << 8 | M::strength_k << 16 | M::payload_strength << 24);
    }
    put(76, 32);
    put(80, 0x41);
    put(88, 32);
    put(92, 0xff0000);
    put(96, 0xff00);
    put(100, 0xff);
    put(104, 0xff000000u);
    put(108, 0x1000);
    for (unsigned i = 128; i < b.size(); ++i) b[i] = static_cast<unsigned char>(i * 37);
    return b;
}

// Synthetic PE32 image in a pagefile section: descriptors at 0x1000, names at 0x1400, thunk arrays at 0x1800 and
// 0x1c00 (+misalign), hint/name entries at 0x2000. Returns {read-write view, read-only view}.
struct Descriptor {
    const char* dll;
    std::vector<const char*> names;
    std::vector<PVOID> slots;
    bool name_table = true;
    unsigned misalign = 0;
};
struct Views {
    HANDLE section = nullptr;
    unsigned char *rw = nullptr, *ro = nullptr;
    ~Views() {
        if (rw) UnmapViewOfFile(rw);
        if (ro) UnmapViewOfFile(ro);
        if (section) CloseHandle(section);
    }
};
constexpr DWORD image_size = 0x3000;
static bool build_image(Views& v, const std::vector<Descriptor>& descriptors, bool valid_dos = true) {
    v.section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, image_size, nullptr);
    if (!v.section) return false;
    v.rw = static_cast<unsigned char*>(MapViewOfFile(v.section, FILE_MAP_WRITE, 0, 0, image_size));
    v.ro = static_cast<unsigned char*>(MapViewOfFile(v.section, FILE_MAP_READ, 0, 0, image_size));
    if (!v.rw || !v.ro) return false;
    unsigned char* b = v.rw;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(b);
    dos->e_magic = valid_dos ? IMAGE_DOS_SIGNATURE : 0x5a5a;
    dos->e_lfanew = 0x80;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(b + 0x80);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt->OptionalHeader.SizeOfImage = image_size;
    nt->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] = {0x1000,
                                                                      DWORD((descriptors.size() + 1) * 20)};
    DWORD names = 0x1400, oft = 0x1800, iat = 0x1c00, hints = 0x2000;
    for (size_t i = 0; i < descriptors.size(); ++i) {
        const auto& d = descriptors[i];
        auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(b + 0x1000 + i * 20);
        std::strcpy(reinterpret_cast<char*>(b + names), d.dll);
        desc->Name = names;
        names += DWORD(std::strlen(d.dll) + 1);
        iat += d.misalign;
        desc->OriginalFirstThunk = d.name_table ? oft : 0;
        desc->FirstThunk = iat;
        for (size_t n = 0; n < d.names.size(); ++n) {
            std::strcpy(reinterpret_cast<char*>(b + hints + 2), d.names[n]);
            const DWORD entry = hints;
            hints += DWORD(2 + std::strlen(d.names[n]) + 1 + 1) & ~1u;
            std::memcpy(b + oft, &entry, 4);
            std::memcpy(b + iat, &d.slots[n], 4);
            oft += 4;
            iat += 4;
        }
        oft += 4; // terminators
        iat += 4;
    }
    return true;
}

int main() {
    HMODULE self = GetModuleHandleW(nullptr);
    auto* base = reinterpret_cast<unsigned char*>(self);
    PVOID* slot = &iat_entry;
    HMODULE d3dx = GetModuleHandleA("d3dx9_43.dll");
    const PVOID exported = d3dx ? reinterpret_cast<PVOID>(GetProcAddress(d3dx, "D3DXCreateTextureFromFileInMemoryEx"))
                                : nullptr;
    const PVOID mesh = d3dx ? reinterpret_cast<PVOID>(GetProcAddress(d3dx, "D3DXCreateMesh")) : nullptr;
    check(d3dx && exported && *slot == exported, "import slot holds the d3dx9_43 export before install",
          reinterpret_cast<unsigned long>(*slot), reinterpret_cast<unsigned long>(exported));
    const PVOID original = *slot;

    // Device for the real D3DX loads.
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = self;
    wc.lpszClassName = "x3m_d3dx_texture_hook";
    RegisterClassA(&wc);
    HWND window = CreateWindowA(wc.lpszClassName, "x", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, self,
                                nullptr);
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    constexpr DWORD behaviour = D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE;
    IDirect3DDevice9* device = nullptr;
    const HRESULT plain = d3d ? d3d->CreateDevice(0, D3DDEVTYPE_HAL, window, behaviour, &pp, &device) : E_FAIL;
    HRESULT created = plain;
    const char* kind = "d3d9";
    if (FAILED(plain)) {
        // Another process in exclusive fullscreen refuses a plain windowed device (D3DERR_NOTAVAILABLE); a D3D9Ex
        // windowed device is not refused then. The textures use D3DPOOL_SYSTEMMEM, valid on both.
        if (d3d) d3d->Release();
        d3d = nullptr;
        IDirect3D9Ex* ex = nullptr;
        IDirect3DDevice9Ex* device_ex = nullptr;
        created = Direct3DCreate9Ex(D3D_SDK_VERSION, &ex);
        if (SUCCEEDED(created)) created = ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, window, behaviour, &pp, nullptr,
                                                             &device_ex);
        d3d = ex;
        device = device_ex;
        kind = "d3d9ex";
    }
    std::printf("DEVICE kind=%s plain=%08lx created=%08lx\n", kind, static_cast<unsigned long>(plain),
                static_cast<unsigned long>(created));
    check(SUCCEEDED(created) && device, "HAL device for the real D3DX loads", static_cast<unsigned long>(created));
    if (!device) {
        std::printf("RESULT FAIL checks=%u failures=%u\n", checks, failures);
        return 1;
    }
    const auto marked = dds(true), unmarked = dds(false);
    const auto direct = reinterpret_cast<CreateFn>(original);
    // Baselines through the direct original (no hook): the hooked calls must reproduce these exactly. One warm-up call
    // first: D3DX's first load leaves the LastError of its own lazy module load (0x7e measured), later ones do not.
    IDirect3DTexture9* t = nullptr;
    const Outcome warm_up = call_with_state(direct, device, marked.data(), UINT(marked.size()), &t, false);
    std::printf("WARMUP hr=%08lx error=%08lx\n", static_cast<unsigned long>(warm_up.hr),
                static_cast<unsigned long>(warm_up.error));
    if (t) t->Release();
    t = nullptr;
    const Outcome base_marked = call_with_state(direct, device, marked.data(), UINT(marked.size()), &t, false);
    if (t) t->Release();
    t = nullptr;
    const Outcome base_unmarked = call_with_state(direct, device, unmarked.data(), UINT(unmarked.size()), &t, false);
    if (t) t->Release();
    t = nullptr;
    const Outcome base_null = call_with_state(direct, nullptr, unmarked.data(), UINT(unmarked.size()), &t, false);
    check(base_marked.hr == S_OK && base_unmarked.hr == S_OK && base_null.hr == D3DERR_INVALIDCALL,
          "direct baselines", static_cast<unsigned long>(base_marked.hr), static_cast<unsigned long>(base_null.hr));

    // 2. Refusals before any write.
    SetLastError(0x4242);
    check(same(H::install(self, nullptr), "no_observer") && GetLastError() == 0x4242 && *slot == original,
          "no_observer refusal");
    check(write_slot(slot, reinterpret_cast<PVOID>(&foreign_create)), "foreign pointer written");
    SetLastError(0x4243);
    const char* foreign = H::install(self, observer);
    check(same(foreign, "foreign_redirect") && GetLastError() == 0x4243 &&
              *slot == reinterpret_cast<PVOID>(&foreign_create) && !H::installed(),
          "foreign_redirect refusal leaves the slot");
    std::printf("REFUSAL reason=%s\n", foreign);
    check(write_slot(slot, original), "original pointer back");
    H::fixture_reset();

    // 3. Install and real D3DX through the IAT.
    SetLastError(0x4244);
    const char* reason = H::install(self, observer);
    check(same(reason, "ok") && GetLastError() == 0x4244, "install ok, LastError kept");
    check(H::installed() && *slot != original && *slot != nullptr, "slot exchanged",
          reinterpret_cast<unsigned long>(*slot));
    check(H::fixture_original() == original, "stub forwards to the export");
    check(!_stricmp(H::dll_name(), "d3dx9_43.dll"), "resolved dll name");
    const unsigned expected_rva = unsigned(reinterpret_cast<unsigned char*>(slot) - base);
    check(H::slot_rva() == expected_rva, "slot rva is the linker's __imp_ entry", H::slot_rva(), expected_rva);
    std::printf("RESOLVED dll=%s slot_rva=0x%08x imp_rva=0x%08x reason=%s\n", H::dll_name(), H::slot_rva(),
                expected_rva, reason);
    check(same(H::install(self, observer), "already_installed"), "second install refused");
    const auto through_iat = [](IDirect3DDevice9* d, const void* data, UINT size, UINT w, UINT h, UINT l, DWORD u,
                                D3DFORMAT f, D3DPOOL p, DWORD fi, DWORD mf, D3DCOLOR k, D3DXIMAGE_INFO* i,
                                PALETTEENTRY* pal, IDirect3DTexture9** out) -> HRESULT {
        return D3DXCreateTextureFromFileInMemoryEx(d, data, size, w, h, l, u, f, p, fi, mf, k, i, pal, out);
    };
    // A non-capturing lambda is cdecl; wrap the import call in a stdcall function of the right type.
    struct Iat {
        static HRESULT WINAPI call(IDirect3DDevice9* d, const void* data, UINT size, UINT w, UINT h, UINT l, DWORD u,
                                   D3DFORMAT f, D3DPOOL p, DWORD fi, DWORD mf, D3DCOLOR k, D3DXIMAGE_INFO* i,
                                   PALETTEENTRY* pal, IDirect3DTexture9** out) {
            return D3DXCreateTextureFromFileInMemoryEx(d, data, size, w, h, l, u, f, p, fi, mf, k, i, pal, out);
        }
    };
    (void)through_iat;
    seen = {};
    t = nullptr;
    const Outcome hooked_marked = call_with_state(&Iat::call, device, marked.data(), UINT(marked.size()), &t, false);
    check(seen.calls == 1 && seen.marked == 1 && seen.lane == M::lane_strength_alpha, "observer saw the marked atlas",
          seen.calls, seen.marked);
    check(seen.device == device && seen.data == marked.data() && seen.size == marked.size() && seen.texture == t &&
              t != nullptr,
          "observer arguments: device, bytes, size, created texture");
    check(hooked_marked.hr == base_marked.hr && hooked_marked.error == base_marked.error &&
              hooked_marked.mxcsr == base_marked.mxcsr && hooked_marked.cw == base_marked.cw,
          "marked call: result, LastError, MXCSR, x87 CW as direct", hooked_marked.error, base_marked.error);
    std::printf("MARKED hr=%08lx error=%08lx mxcsr=%08x cw=%04x direct_error=%08lx observer_calls=%u lane=%u\n",
                static_cast<unsigned long>(hooked_marked.hr), static_cast<unsigned long>(hooked_marked.error),
                hooked_marked.mxcsr, hooked_marked.cw, static_cast<unsigned long>(base_marked.error), seen.calls,
                seen.lane);
    if (t) t->Release();
    t = nullptr;
    const Outcome hooked_unmarked =
        call_with_state(&Iat::call, device, unmarked.data(), UINT(unmarked.size()), &t, false);
    check(seen.calls == 2 && seen.marked == 1, "unmarked texture: observer ran, no mark", seen.calls, seen.marked);
    check(hooked_unmarked.hr == base_unmarked.hr && hooked_unmarked.error == base_unmarked.error &&
              hooked_unmarked.mxcsr == base_unmarked.mxcsr && hooked_unmarked.cw == base_unmarked.cw && t != nullptr,
          "unmarked call: result, LastError, MXCSR, x87 CW as direct", hooked_unmarked.error, base_unmarked.error);
    std::printf("UNMARKED hr=%08lx error=%08lx direct_error=%08lx\n", static_cast<unsigned long>(hooked_unmarked.hr),
                static_cast<unsigned long>(hooked_unmarked.error), static_cast<unsigned long>(base_unmarked.error));
    if (t) t->Release();
    t = nullptr;
    const Outcome hooked_null = call_with_state(&Iat::call, nullptr, unmarked.data(), UINT(unmarked.size()), &t, false);
    check(seen.calls == 2, "failed call: observer not run", seen.calls);
    check(hooked_null.hr == base_null.hr && hooked_null.error == base_null.error &&
              hooked_null.mxcsr == base_null.mxcsr && hooked_null.cw == base_null.cw,
          "failed call: result, LastError, MXCSR, x87 CW as direct", hooked_null.error, base_null.error);
    std::printf("FAILED hr=%08lx error=%08lx direct_error=%08lx\n", static_cast<unsigned long>(hooked_null.hr),
                static_cast<unsigned long>(hooked_null.error), static_cast<unsigned long>(base_null.error));

    // 4. CPU-state boundary with the probe original.
    H::fixture_set_original(reinterpret_cast<void*>(&probe_original));
    for (const HRESULT result : {S_FALSE, E_FAIL}) {
        probe = {};
        probe_result = result;
        const unsigned before = seen.calls;
        t = nullptr;
        const Outcome o = call_with_state(&Iat::call, device, marked.data(), UINT(marked.size()), &t, true);
        check(probe.calls == 1 && probe.error == 0x00c0ffee && probe.mxcsr == 0x7f80 && probe.cw == 0x0e7f &&
                  probe.top == 7,
              "original entered with the caller's LastError, MXCSR, x87 CW and stack", probe.error, probe.mxcsr);
        check(o.hr == result && o.error == 0xbeef && o.mxcsr == 0x9fc0 && o.cw == 0x037f && o.top == 6,
              "caller left with the original's result, LastError, MXCSR, x87 CW and stack", o.error, o.mxcsr);
        check(seen.calls == before + (SUCCEEDED(result) ? 1u : 0u), "observer only after success", seen.calls,
              before);
        if (SUCCEEDED(result)) check(seen.texture == probe_texture, "observer got the original's texture");
        std::printf("BOUNDARY result=%08lx entry_error=%08lx entry_mxcsr=%08x entry_cw=%04x entry_top=%u "
                    "exit_error=%08lx exit_mxcsr=%08x exit_cw=%04x exit_top=%u\n",
                    static_cast<unsigned long>(result), static_cast<unsigned long>(probe.error), probe.mxcsr,
                    probe.cw, probe.top, static_cast<unsigned long>(o.error), o.mxcsr, o.cw, o.top);
    }
    H::fixture_set_original(original);

    // 5. Rollback.
    const PVOID stub = *slot;
    SetLastError(0x4245);
    check(H::restore() && *slot == original && !H::installed() && GetLastError() == 0x4245, "restore puts the export back");
    const unsigned calls_after_restore = seen.calls;
    t = nullptr;
    call_with_state(&Iat::call, device, marked.data(), UINT(marked.size()), &t, false);
    check(seen.calls == calls_after_restore && t, "restored slot: the observer is out of the path");
    if (t) t->Release();
    // The stub stays callable after restore (a thread may be inside it) and forwards to the export, observer detached.
    t = nullptr;
    const Outcome via_stub = call_with_state(reinterpret_cast<CreateFn>(stub), device, marked.data(),
                                             UINT(marked.size()), &t, false);
    check(via_stub.hr == S_OK && t && seen.calls == calls_after_restore, "restored stub still forwards");
    if (t) t->Release();
    H::fixture_reset();
    for (unsigned failing : {1u, 2u}) {
        H::fixture_fail_protection_restores(failing);
        const char* rolled = H::install(self, observer);
        check(same(rolled, "protect_restore_failed") && *slot == original && !H::installed(),
              "failed protection restore rolls the install back", failing);
        std::printf("ROLLBACK failing_restores=%u reason=%s slot_original=%u\n", failing, rolled,
                    unsigned(*slot == original));
        H::fixture_fail_protection_restores(0);
        H::fixture_reset();
    }
    // A foreign writer chains on top before the failed restore's undo: no undo, foreign_chained, the chain stays valid
    // (the stub it saw forwards to the export, observer detached).
    chained_next = nullptr;
    H::fixture_protection_restore_probe(chain_on_top);
    H::fixture_fail_protection_restores(1);
    const char* chained = H::install(self, observer);
    H::fixture_protection_restore_probe(nullptr);
    H::fixture_fail_protection_restores(0);
    const bool chained_slot = *slot == reinterpret_cast<PVOID>(&foreign_create);
    check(same(chained, "foreign_chained") && chained_slot && !H::installed() && chained_next &&
              chained_next != original && H::fixture_original() == original,
          "foreign writer on top: no undo, foreign_chained");
    const unsigned calls_before_chain = seen.calls;
    t = nullptr;
    const Outcome chain = call_with_state(reinterpret_cast<CreateFn>(chained_next), device, marked.data(),
                                          UINT(marked.size()), &t, false);
    check(chain.hr == S_OK && t && seen.calls == calls_before_chain, "chained stub forwards, observer detached",
          static_cast<unsigned long>(chain.hr), seen.calls - calls_before_chain);
    std::printf("CHAINED reason=%s slot_foreign=%u stub_forwards=%u observer_delta=%u\n", chained,
                unsigned(chained_slot), unsigned(chain.hr == S_OK && t), seen.calls - calls_before_chain);
    if (t) t->Release();
    check(write_slot(slot, original), "original pointer back after the chain case");
    H::fixture_reset();
    check(same(H::install(self, observer), "ok") && *slot != original, "install after rollback");
    check(H::restore() && *slot == original, "restore after reinstall");
    H::fixture_reset();

    // 6. Synthetic images.
    const char* tex = "D3DXCreateTextureFromFileInMemoryEx";
    struct Case {
        const char* label;
        std::vector<Descriptor> descriptors;
        const char* expected;
        bool read_only, valid_dos;
    };
    std::vector<Case> cases = {
        {"upper_case_dll", {{"D3DX9_43.DLL", {"D3DXCreateMesh", tex}, {mesh, exported}}}, "ok", false, true},
        {"read_only_view", {{"d3dx9_43.dll", {tex}, {exported}}}, "slot_not_writable", true, true},
        {"absent", {{"d3dx9_43.dll", {"D3DXCreateMesh"}, {mesh}}}, "import_absent", false, true},
        {"not_d3dx9", {{"d3dx9.dll", {tex}, {exported}}, {"xd3dx9_43.dll", {tex}, {exported}}}, "import_absent", false,
         true},
        {"no_name_table", {{"d3dx9_43.dll", {tex}, {exported}, false}}, "import_absent", false, true},
        {"ambiguous", {{"d3dx9_43.dll", {tex}, {exported}}, {"d3dx9_42.dll", {tex}, {exported}}}, "ambiguous_import",
         false, true},
        {"dll_not_loaded", {{"d3dx9_99.dll", {tex}, {exported}}}, "dll_not_loaded", false, true},
        {"misaligned", {{"d3dx9_43.dll", {tex}, {exported}, true, 2}}, "slot_misaligned", false, true},
        {"foreign", {{"d3dx9_43.dll", {tex}, {reinterpret_cast<PVOID>(&foreign_create)}}}, "foreign_redirect", false,
         true},
        {"bad_image", {{"d3dx9_43.dll", {tex}, {exported}}}, "bad_image", false, false},
    };
    for (const auto& c : cases) {
        Views v;
        if (!build_image(v, c.descriptors, c.valid_dos)) {
            check(false, c.label);
            continue;
        }
        auto* module = reinterpret_cast<HMODULE>(c.read_only ? v.ro : v.rw);
        unsigned char before[image_size];
        std::memcpy(before, v.rw, image_size);
        SetLastError(0x4246);
        const char* got = H::install(module, observer);
        const bool kept = GetLastError() == 0x4246;
        std::printf("SYNTHETIC case=%s reason=%s dll=%s\n", c.label, got, H::dll_name());
        check(same(got, c.expected) && kept, c.label);
        if (same(got, "ok")) {
            // The synthetic slot reaches the stub, which forwards a failing call to the real export.
            const unsigned descriptor_iat = 0x1c00 + 4;
            auto* synthetic_slot = reinterpret_cast<PVOID*>(v.rw + descriptor_iat);
            check(H::slot_rva() == descriptor_iat && *synthetic_slot != exported, "synthetic slot exchanged",
                  H::slot_rva());
            t = nullptr;
            const Outcome o = call_with_state(reinterpret_cast<CreateFn>(*synthetic_slot), nullptr, unmarked.data(),
                                              UINT(unmarked.size()), &t, false);
            check(o.hr == base_null.hr && o.error == base_null.error, "synthetic stub forwards");
            check(H::restore() && *synthetic_slot == exported, "synthetic restore");
            check(!std::memcmp(before, v.rw, image_size), "synthetic image identical after restore");
        } else
            check(!std::memcmp(before, v.rw, image_size), "refused image unchanged");
        H::fixture_reset();
    }

    // 7. Late window.
    x3m::engine_patch::close_install_window("fixture");
    const char* late = H::install(self, observer);
    check(same(late, "late_claim") && *slot == original && !H::installed(), "late window refused");
    std::printf("LATE reason=%s\n", late);

    device->Release();
    if (d3d) d3d->Release();
    DestroyWindow(window);
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
