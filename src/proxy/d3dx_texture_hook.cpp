#include "d3dx_texture_hook.h"
#include "cpu_state.h"
#include "engine_patch.h"
#include "../ownership/application_admission_abi.h"
#include <d3dx9.h>
#include <atomic>
#include <cstring>
#include <type_traits>

// FL material-strength texture hook (d3dx_texture_hook.h; docs/architecture/overlay-material-strength.md, "Farnham's
// Legacy"). One import slot of the main module, one forwarding stub with AP's texture-row semantics: the original
// runs with the caller's CPU state and LastError, the observer then sees the created texture and its bytes, and the
// caller gets the original's result, LastError, x87 and MXCSR state back (CpuCallBoundary). Unmarked textures cost
// the boundary and the 12-byte header compare inside the observer; no behaviour change.

namespace x3m::d3dx_texture_hook {
namespace {
constexpr char target_name[] = "D3DXCreateTextureFromFileInMemoryEx";
constexpr unsigned dll_capacity = 32;

std::atomic<TextureObserver> observer_{nullptr};
std::atomic<void*> original_{nullptr}; // published before the slot goes live; never cleared while the stub may run
PVOID* slot_ = nullptr;
bool installed_ = false;
char dll_[dll_capacity] = "-";
unsigned slot_rva_ = 0;
#ifdef X3M_D3DX_TEXTURE_HOOK_FIXTURE
unsigned fail_protection_restore_ = 0;
void (*protection_restore_probe_)(PVOID* slot) = nullptr; // runs before a failing restore (a writer chaining on top)
#endif

using CreateFn = decltype(&D3DXCreateTextureFromFileInMemoryEx);

HRESULT WINAPI texture_stub(IDirect3DDevice9* d, const void* data, UINT size, UINT width, UINT height, UINT levels,
                            DWORD usage, D3DFORMAT format, D3DPOOL pool, DWORD filter, DWORD mipfilter, D3DCOLOR key,
                            D3DXIMAGE_INFO* info, PALETTEENTRY* palette, IDirect3DTexture9** out) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    const auto original = reinterpret_cast<CreateFn>(original_.load(std::memory_order_acquire));
    cpu.before_original();
    const HRESULT result = original(d, data, size, width, height, levels, usage, format, pool, filter, mipfilter, key,
                                    info, palette, out);
    cpu.after_original();
    observe_created(observer_.load(std::memory_order_acquire), d, data, size, result, out, GetLastError());
    return result;
}
static_assert(std::is_same_v<decltype(&texture_stub), CreateFn>);

// Every byte of [address, address + bytes) committed, readable and inside `owner`'s allocation (the mapped image).
bool readable(const void* address, size_t bytes, HMODULE owner) {
    auto cursor = reinterpret_cast<uintptr_t>(address);
    if (!cursor || bytes == 0 || cursor + bytes < cursor) return false;
    const uintptr_t end = cursor + bytes;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof info) != sizeof info ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            info.AllocationBase != owner)
            return false;
        const DWORD protection = info.Protect & 0xff;
        if (protection != PAGE_READONLY && protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE &&
            protection != PAGE_EXECUTE_WRITECOPY)
            return false;
        cursor = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    }
    return true;
}
struct Image {
    unsigned char* base;
    DWORD size;
    HMODULE module;
    bool range(DWORD rva, DWORD length) const { return length <= size && rva <= size - length; }
    template <typename T> const T* at(DWORD rva) const {
        return range(rva, sizeof(T)) && readable(base + rva, sizeof(T), module) ? reinterpret_cast<const T*>(base + rva)
                                                                                 : nullptr;
    }
    // The NUL-terminated name at `rva` copied into out (capacity bytes), false when it is longer or leaves the image.
    bool name(DWORD rva, char* out, unsigned capacity) const {
        for (unsigned i = 0; i < capacity; ++i) {
            const auto c = at<char>(rva + i);
            if (!c || rva + i < rva) return false;
            out[i] = *c;
            if (!*c) return true;
        }
        return false;
    }
};
// d3dx9_<digits>.dll, any case.
bool d3dx9_dll(const char* name) {
    if (_strnicmp(name, "d3dx9_", 6)) return false;
    const char* p = name + 6;
    if (*p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') ++p;
    return !_stricmp(p, ".dll");
}
bool restore_protection(PVOID* slot, DWORD protection) {
#ifdef X3M_D3DX_TEXTURE_HOOK_FIXTURE
    if (fail_protection_restore_) {
        --fail_protection_restore_;
        if (protection_restore_probe_) protection_restore_probe_(slot);
        SetLastError(ERROR_ACCESS_DENIED);
        return false;
    }
#endif
    DWORD ignored = 0;
    return VirtualProtect(slot, sizeof(PVOID), protection, &ignored) != FALSE;
}
// One aligned dword exchange under a temporary PAGE_READWRITE: expected -> desired. A failed protection restore after
// a successful exchange rolls the exchange back (forward installs only) and reports protect_restore_failed; when the
// undo finds the slot no longer holding `desired` (another hook chained on top of the stub in that window), nothing
// is undone and foreign_chained is reported: the chain stays valid through the stub, which the caller keeps
// forwarding-only.
const char* exchange(PVOID* slot, PVOID expected, PVOID desired, bool forward) {
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(PVOID), PAGE_READWRITE, &protection)) return "slot_not_writable";
    const bool swapped = InterlockedCompareExchangePointer(slot, desired, expected) == expected;
    bool protected_again = restore_protection(slot, protection), rolled_back = false, chained = false;
    if (!protected_again && swapped && forward) {
        rolled_back = InterlockedCompareExchangePointer(slot, expected, desired) == desired;
        chained = !rolled_back;
        protected_again = restore_protection(slot, protection);
    }
    if (!swapped) return "foreign_redirect"; // the slot changed between the check and the exchange
    if (chained) return "foreign_chained";
    if (rolled_back || !protected_again) return "protect_restore_failed";
    return "ok";
}
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&texture_stub), &module) != FALSE &&
           module != nullptr;
}
// Resolves the slot by name: exactly one d3dx9_NN.dll descriptor entry with a name table naming the target.
const char* resolve(HMODULE module, PVOID** slot_out, char* dll_out, unsigned* rva_out) {
    auto* base = reinterpret_cast<unsigned char*>(module);
    if (!module || !readable(base, sizeof(IMAGE_DOS_HEADER), module)) return "bad_image";
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < LONG(sizeof(IMAGE_DOS_HEADER)) ||
        dos->e_lfanew > 0x100000 || !readable(base + dos->e_lfanew, sizeof(IMAGE_NT_HEADERS32), module))
        return "bad_image";
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT ||
        nt->OptionalHeader.SizeOfImage < DWORD(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) ||
        nt->OptionalHeader.SizeOfImage > 0x40000000)
        return "bad_image";
    const Image image{base, nt->OptionalHeader.SizeOfImage, module};
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress || !image.range(directory.VirtualAddress, directory.Size)) return "import_absent";
    unsigned matches = 0;
    for (DWORD pos = 0; pos + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= directory.Size;
         pos += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        const auto* descriptor = image.at<IMAGE_IMPORT_DESCRIPTOR>(directory.VirtualAddress + pos);
        if (!descriptor) return "bad_image";
        if (!descriptor->Name) break;
        char dll[dll_capacity]{};
        if (!image.name(descriptor->Name, dll, dll_capacity) || !d3dx9_dll(dll) || !descriptor->OriginalFirstThunk)
            continue; // names are read from the name table only, never inferred from the IAT
        for (DWORD i = 0;; ++i) {
            const DWORD name_rva = descriptor->OriginalFirstThunk + i * sizeof(IMAGE_THUNK_DATA32),
                        slot_rva = descriptor->FirstThunk + i * sizeof(IMAGE_THUNK_DATA32);
            if (name_rva < descriptor->OriginalFirstThunk || slot_rva < descriptor->FirstThunk) break;
            const auto* name = image.at<IMAGE_THUNK_DATA32>(name_rva);
            const auto* slot = image.at<IMAGE_THUNK_DATA32>(slot_rva);
            if (!name || !slot || !name->u1.AddressOfData) break;
            if (IMAGE_SNAP_BY_ORDINAL32(name->u1.Ordinal)) continue;
            char symbol[sizeof target_name]{};
            if (name->u1.AddressOfData > 0xfffffffdu ||
                !image.name(name->u1.AddressOfData + 2, symbol, sizeof symbol) || std::strcmp(symbol, target_name))
                continue;
            if (++matches == 1) {
                *slot_out = reinterpret_cast<PVOID*>(base + slot_rva);
                std::memcpy(dll_out, dll, dll_capacity);
                *rva_out = slot_rva;
            }
        }
    }
    if (!matches) return "import_absent";
    if (matches > 1) return "ambiguous_import";
    return "ok";
}
} // namespace

const char* install(HMODULE module, TextureObserver observer) {
    const DWORD error = GetLastError();
    const char* reason = "ok";
    PVOID* slot = nullptr;
    char dll[dll_capacity] = "-";
    unsigned rva = 0;
    if (installed_)
        reason = "already_installed";
    else if (!observer)
        reason = "no_observer";
    else if (!engine_patch::install_window_open())
        reason = "late_claim"; // a texture loaded before the hook would draw at the merged strength unmarked
    else
        reason = resolve(module, &slot, dll, &rva);
    if (!std::strcmp(reason, "ok") || std::strcmp(dll, "-")) {
        std::memcpy(dll_, dll, dll_capacity);
        slot_rva_ = rva;
    }
    if (!std::strcmp(reason, "ok")) {
        HMODULE provider = nullptr;
        const PVOID current = *slot;
        if (reinterpret_cast<uintptr_t>(slot) % alignof(PVOID))
            reason = "slot_misaligned";
        else if (!(provider = GetModuleHandleA(dll)))
            reason = "dll_not_loaded";
        else if (!current || current != reinterpret_cast<PVOID>(GetProcAddress(provider, target_name)))
            reason = "foreign_redirect"; // the slot no longer holds the named export (another module redirected it)
        else if (!pin_self())
            reason = "pin_failed";
        else {
            observer_.store(observer, std::memory_order_release);
            original_.store(current, std::memory_order_release);
            reason = exchange(slot, current, reinterpret_cast<PVOID>(&texture_stub), true);
            if (!std::strcmp(reason, "ok")) {
                slot_ = slot;
                installed_ = true;
            } else
                observer_.store(nullptr, std::memory_order_release); // foreign_chained: the stub stays in the chain,
                                                                      // forwarding only (original_ kept)
        }
    }
    SetLastError(error);
    return reason;
}
bool installed() {
    return installed_;
}
const char* dll_name() {
    return dll_;
}
unsigned slot_rva() {
    return slot_rva_;
}
bool restore() {
    if (!installed_ || !slot_) return false;
    const DWORD error = GetLastError();
    observer_.store(nullptr, std::memory_order_release);
    // protect_restore_failed here means the original is back but the page kept PAGE_READWRITE (no rollback).
    const char* reason =
        exchange(slot_, reinterpret_cast<PVOID>(&texture_stub), original_.load(std::memory_order_acquire), false);
    const bool restored = !std::strcmp(reason, "ok") || !std::strcmp(reason, "protect_restore_failed");
    if (restored) installed_ = false;
    SetLastError(error);
    return restored;
}
#ifdef X3M_D3DX_TEXTURE_HOOK_FIXTURE
void fixture_fail_protection_restores(unsigned calls) {
    fail_protection_restore_ = calls;
}
void fixture_protection_restore_probe(void (*probe)(PVOID* slot)) {
    protection_restore_probe_ = probe;
}
void fixture_set_original(void* original) {
    original_.store(original, std::memory_order_release);
}
void* fixture_original() {
    return original_.load(std::memory_order_acquire);
}
void fixture_reset() {
    observer_.store(nullptr, std::memory_order_release);
    slot_ = nullptr;
    installed_ = false;
    std::memcpy(dll_, "-", 2);
    slot_rva_ = 0;
}
#endif
} // namespace x3m::d3dx_texture_hook
