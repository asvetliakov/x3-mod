#include "managed_upload_contract.h"
#include <windows.h>
#include <wincrypt.h>
#include <cstring>
#include <type_traits>

namespace x3m::ownership::managed_upload {
namespace {
static_assert(sizeof(void*) == 4, "Pinned Preview managed upload contract is PE32 only");

struct PreserveState {
    DWORD error = GetLastError();
    unsigned char x87[108];
    DWORD mxcsr;
    // Complete legacy register/tag transport, not x87 arithmetic. The pinned
    // Preview execution path did not round-trip a live x87 value/tag through
    // FXSAVE/FXRSTOR; FNSAVE+immediate FRSTOR also preserves the incoming state
    // while the helper executes. Volatile XMM registers follow the normal ABI.
    PreserveState() noexcept {
        asm volatile("fnsave %0\n\tfrstor %0\n\tstmxcsr %1" : "=m"(x87), "=m"(mxcsr)::"memory");
    }
    ~PreserveState() {
        asm volatile("frstor %0\n\tldmxcsr %1" ::"m"(x87), "m"(mxcsr) : "memory");
        SetLastError(error);
    }
};
SRWLOCK runtime_lock = SRWLOCK_INIT;
HMODULE d3d9_module = nullptr, wined3d_module = nullptr;
constexpr std::uint64_t contract_generation = 1; // Immutable pinned backend + policy v1.
constexpr unsigned char d3d9_sha[32] = {0x58, 0xcc, 0x36, 0xcf, 0x74, 0x12, 0x8a, 0xe4, 0xb6, 0x21, 0x11,
                                        0x00, 0x43, 0x0d, 0x14, 0x6c, 0x36, 0x92, 0x80, 0x81, 0x46, 0xd8,
                                        0xd2, 0x07, 0x5e, 0x6c, 0x5d, 0x84, 0x61, 0x62, 0xf8, 0xcf};
constexpr unsigned char wined3d_sha[32] = {0xf4, 0x99, 0x7b, 0xc0, 0x46, 0x5d, 0xe7, 0xe8, 0x7b, 0xac, 0x99,
                                           0x21, 0xbf, 0x02, 0x74, 0xdb, 0x00, 0xac, 0x3b, 0x3b, 0xa0, 0x75,
                                           0x4f, 0xa0, 0x3f, 0x1f, 0x33, 0xe3, 0x09, 0xa8, 0xe8, 0x63};

// Structural accessibility only. Mapping coherence comes from exact backend
// identity, native layout and heap-pointer association, never VirtualQuery alone.
bool accessible(const void* pointer, std::uint64_t size, HMODULE owner = nullptr, bool executable = false,
                bool writable = false) noexcept {
    const auto first = reinterpret_cast<std::uintptr_t>(pointer);
    const std::uint64_t end = std::uint64_t(first) + size;
    if (!first || !size || end > (std::uint64_t(1) << 32)) return false;
    std::uint64_t cursor = first;
    for (unsigned region = 0; cursor < end && region < 64; ++region) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<const void*>(std::uintptr_t(cursor)), &info, sizeof info) != sizeof info ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            (owner && info.AllocationBase != owner))
            return false;
        const DWORD protection = info.Protect & 0xff;
        const bool rw = protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
                        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        const bool rx = protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
                        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        if ((executable && !rx) || (writable && !rw) ||
            (!executable && !rw && protection != PAGE_READONLY && protection != PAGE_EXECUTE_READ))
            return false;
        const auto base = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        const std::uint64_t next = std::uint64_t(base) + info.RegionSize;
        if (base > cursor || next <= cursor) return false;
        cursor = next;
    }
    return cursor >= end;
}
std::uint32_t word(const void* base, unsigned offset) noexcept {
    std::uint32_t value;
    std::memcpy(&value, static_cast<const unsigned char*>(base) + offset, sizeof value);
    return value;
}
const void* at(HMODULE module, unsigned rva) noexcept {
    return reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(module) + rva);
}
bool pe32(HMODULE module, DWORD image_size) noexcept {
    if (!accessible(module, sizeof(IMAGE_DOS_HEADER), module)) return false;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) return false;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(at(module, unsigned(dos->e_lfanew)));
    return accessible(nt, sizeof *nt, module) && nt->Signature == IMAGE_NT_SIGNATURE &&
           nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
           nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC && nt->OptionalHeader.SizeOfImage == image_size;
}
bool digest_matches(HMODULE module, const unsigned char expected[32]) noexcept {
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(module, path, 32768);
    if (!length || length >= 32768) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    bool good = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 64 * 1024 * 1024 &&
                CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
                CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash);
    unsigned char bytes[16384];
    DWORD count = 0;
    std::uint64_t total = 0;
    while (good && total < std::uint64_t(size.QuadPart)) {
        if (!ReadFile(file, bytes, sizeof bytes, &count, nullptr) || !count) {
            good = false;
            break;
        }
        total += count;
        good = CryptHashData(hash, bytes, count, 0) != FALSE;
    }
    unsigned char digest[32]{};
    DWORD digest_size = sizeof digest;
    good = good && total == std::uint64_t(size.QuadPart) &&
           CryptGetHashParam(hash, HP_HASHVAL, digest, &digest_size, 0) && digest_size == sizeof digest &&
           !std::memcmp(digest, expected, sizeof digest);
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    CloseHandle(file);
    return good;
}
bool imports_match(HMODULE d3d9, HMODULE wined3d) noexcept {
    constexpr unsigned imports[] = {0x23460, 0x235b8, 0x235c4, 0x235ac, 0x23560, 0x23564};
    constexpr unsigned exports[] = {0x1ac00, 0x7b690, 0x7b700, 0x7b630, 0xc4260, 0xc4280};
    const char* names[] = {"wined3d_buffer_get_resource", "wined3d_resource_map", "wined3d_resource_unmap",
                           "wined3d_resource_get_desc",   "wined3d_mutex_lock",   "wined3d_mutex_unlock"};
    for (unsigned i = 0; i < 6; ++i) {
        const void* slot = at(d3d9, imports[i]);
        const void* target = at(wined3d, exports[i]);
        if (!accessible(slot, sizeof(void*), d3d9) || word(slot, 0) != reinterpret_cast<std::uintptr_t>(target) ||
            reinterpret_cast<const void*>(GetProcAddress(wined3d, names[i])) != target ||
            !accessible(target, 1, wined3d, true))
            return false;
    }
    return true;
}
bool runtime_for(const void* unlock, HMODULE& d3d9, HMODULE& wined3d) noexcept {
    HMODULE candidate = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCSTR>(unlock), &candidate))
        return false;
    AcquireSRWLockExclusive(&runtime_lock);
    if (!d3d9_module) {
        HMODULE wine = nullptr, pinned_d3d9 = nullptr, pinned_wine = nullptr;
        bool good = pe32(candidate, 0x2c000) && digest_matches(candidate, d3d9_sha) &&
                    accessible(at(candidate, 0x235c4), sizeof(void*), candidate);
        if (good)
            good = GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                      reinterpret_cast<LPCSTR>(word(at(candidate, 0x235c4), 0)), &wine) != FALSE;
        good = good && pe32(wine, 0x2d0000) && digest_matches(wine, wined3d_sha) && imports_match(candidate, wine);
        if (good)
            good = GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                      reinterpret_cast<LPCSTR>(at(wine, 0x7b700)), &pinned_wine) != FALSE;
        if (good)
            good = GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                      reinterpret_cast<LPCSTR>(unlock), &pinned_d3d9) != FALSE;
        if (good) {
            d3d9_module = pinned_d3d9;
            wined3d_module = pinned_wine;
        }
        if (wine) FreeLibrary(wine);
    }
    d3d9 = d3d9_module;
    wined3d = wined3d_module;
    const bool good = d3d9 && candidate == d3d9 && imports_match(d3d9, wined3d);
    ReleaseSRWLockExclusive(&runtime_lock);
    FreeLibrary(candidate);
    return good;
}
template <class Buffer> bool inspect_impl(Buffer* native, BufferContract& result) noexcept {
    constexpr bool vertex = std::is_same_v<Buffer, IDirect3DVertexBuffer9>;
    if (!accessible(native, 0x24)) return false;
    const auto table = reinterpret_cast<const void*>(word(native, 0));
    if (!accessible(table, 14 * sizeof(void*))) return false;
    const auto unlock = reinterpret_cast<const void*>(word(table, 12 * 4));
    HMODULE d3d9 = nullptr, wined3d = nullptr;
    if (!runtime_for(unlock, d3d9, wined3d)) return false;
    constexpr unsigned slots[] = {4, 5, 6, 11, 12, 13};
    const unsigned endpoints[] = {vertex ? 0x1ae0u : 0x25c0u, vertex ? 0x1c00u : 0x26e0u, vertex ? 0x1d10u : 0x27f0u,
                                  vertex ? 0x1f90u : 0x2a70u, vertex ? 0x2060u : 0x2b40u, vertex ? 0x20c0u : 0x2ba0u};
    for (unsigned i = 0; i < 6; ++i) {
        const void* expected = at(d3d9, endpoints[i]);
        if (word(table, slots[i] * 4) != reinterpret_cast<std::uintptr_t>(expected) ||
            !accessible(expected, 1, d3d9, true))
            return false;
    }
    std::conditional_t<vertex, D3DVERTEXBUFFER_DESC, D3DINDEXBUFFER_DESC> desc{};
    if (native->GetDesc(&desc) != S_OK || desc.Pool != D3DPOOL_MANAGED || desc.Usage != D3DUSAGE_WRITEONLY ||
        !desc.Size || desc.Size > maximum_buffer_bytes ||
        desc.Type != (vertex ? D3DRTYPE_VERTEXBUFFER : D3DRTYPE_INDEXBUFFER) ||
        (vertex ? desc.Format != D3DFMT_VERTEXDATA : (desc.Format != D3DFMT_INDEX16 && desc.Format != D3DFMT_INDEX32)))
        return false;
    const auto resource = reinterpret_cast<const void*>(word(native, 0x10));
    if (!accessible(resource, 0xbc) || word(resource, 0x14) != 1 || word(resource, 0x4c) != desc.Size ||
        !(word(resource, 0x30) & 0x20000000) || !(word(resource, 0x5c) & 1) || word(resource, 0xb8) ||
        word(resource, 0x94) != reinterpret_cast<std::uintptr_t>(at(wined3d, 0x1e7c90)))
        return false;
    const auto heap = reinterpret_cast<const void*>(word(resource, 0x58));
    if (!accessible(heap, desc.Size, nullptr, false, true)) return false;
    result = {contract_generation, desc.Size, desc.Type, desc.Format, native, resource, heap};
    return true;
}
bool same_live_contract(const BufferContract& previous, BufferContract& current) noexcept {
    if (previous.generation != contract_generation || !previous.borrowed_native) return false;
    bool good = false;
    if (previous.type == D3DRTYPE_VERTEXBUFFER)
        good = inspect_impl(static_cast<IDirect3DVertexBuffer9*>(previous.borrowed_native), current);
    else if (previous.type == D3DRTYPE_INDEXBUFFER)
        good = inspect_impl(static_cast<IDirect3DIndexBuffer9*>(previous.borrowed_native), current);
    return good && current.size == previous.size && current.format == previous.format &&
           current.backend_resource == previous.backend_resource && current.heap_data == previous.heap_data;
}
}
bool inspect(IDirect3DVertexBuffer9* native, BufferContract* out) noexcept {
    PreserveState guard;
    if (!out) return false;
    *out = {};
    return inspect_impl(native, *out);
}
bool inspect(IDirect3DIndexBuffer9* native, BufferContract* out) noexcept {
    PreserveState guard;
    if (!out) return false;
    *out = {};
    return inspect_impl(native, *out);
}
bool validate_window(const BufferContract& contract, UINT offset, UINT size, DWORD flags, const void* pointer,
                     Window* out) noexcept {
    PreserveState guard;
    if (!out) return false;
    *out = {};
    if (flags != 0 && flags != D3DLOCK_NOSYSLOCK) return false;
    if ((!size && offset) || offset > contract.size) return false;
    const UINT length = size ? size : contract.size;
    if (!length || length > contract.size - offset) return false;
    BufferContract current{};
    if (!same_live_contract(contract, current) || word(current.backend_resource, 8) != 1 ||
        !(word(current.backend_resource, 0xb4) & 2))
        return false;
    const std::uint64_t address = reinterpret_cast<std::uintptr_t>(current.heap_data) + std::uint64_t(offset);
    if (address >= (std::uint64_t(1) << 32) || pointer != reinterpret_cast<const void*>(std::uintptr_t(address)) ||
        !accessible(pointer, length, nullptr, false, true))
        return false;
    *out = {current.generation, offset, length};
    return true;
}
bool validate_closed(const BufferContract& contract) noexcept {
    PreserveState guard;
    BufferContract current{};
    return same_live_contract(contract, current) && word(current.backend_resource, 8) == 0;
}
}
