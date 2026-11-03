#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>

// Named import slots of X3FL.exe for the FL loading quintet (gz_buffer_fl.cpp, crypt_cache_fl.cpp; loading_trace.cpp
// keeps its own walk for the mesh rows): the slot the image at `base` binds for dll!name, found by walking the import
// directory's OriginalFirstThunk names (never inferred from IAT contents), and the one write every owner uses on a
// slot: compare-exchange of the expected pointer under a temporary PAGE_READWRITE, the protection put back and the
// value read back. Documented PE / Win32 only.
namespace x3m::fl::import_slots {
// RVA of the IAT slot for the named import dll!name (case-insensitive DLL, exact symbol), 0 when absent, by ordinal
// only, duplicated or outside the image. Read is bool(std::uintptr_t address, void* out, std::size_t size).
template <class Read> std::uint32_t named_slot(std::uintptr_t base, const char* dll, const char* name, Read&& read) {
    IMAGE_DOS_HEADER dos{};
    if (!base || !read(base, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
        dos.e_lfanew >= 0x1000)
        return 0;
    IMAGE_NT_HEADERS32 nt{};
    if (!read(base + std::uint32_t(dos.e_lfanew), &nt, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT)
        return 0;
    const std::uint32_t size = nt.OptionalHeader.SizeOfImage;
    const IMAGE_DATA_DIRECTORY directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress || directory.VirtualAddress >= size || directory.Size > size - directory.VirtualAddress)
        return 0;
    auto text = [&](std::uint32_t rva, char* out, unsigned capacity) {
        if (rva >= size || capacity > size - rva || !read(base + rva, out, capacity)) return false;
        return std::memchr(out, 0, capacity) != nullptr;
    };
    std::uint32_t found = 0;
    for (std::uint32_t at = 0; at + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= directory.Size;
         at += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        IMAGE_IMPORT_DESCRIPTOR d{};
        if (!read(base + directory.VirtualAddress + at, &d, sizeof d)) return 0;
        if (!d.Name) break;
        char module[64]{};
        if (!d.OriginalFirstThunk || !text(d.Name, module, sizeof module - 1) || lstrcmpiA(module, dll)) continue;
        for (std::uint32_t i = 0; i < 4096; ++i) {
            IMAGE_THUNK_DATA32 lookup{};
            const std::uint32_t entry = d.OriginalFirstThunk + 4 * i, slot = d.FirstThunk + 4 * i;
            if (entry >= size - 4 || slot >= size - 4 || !read(base + entry, &lookup, 4)) return 0;
            if (!lookup.u1.AddressOfData) break;
            if (IMAGE_SNAP_BY_ORDINAL32(lookup.u1.Ordinal)) continue;
            char symbol[64]{};
            if (!text(lookup.u1.AddressOfData + 2, symbol, sizeof symbol - 1) || std::strcmp(symbol, name)) continue;
            if (found) return 0; // two slots for one name: not the qualified image
            found = slot;
        }
    }
    return found;
}
#ifdef X3M_LOADING_FL_FIXTURE
inline unsigned fixture_fail_exchange = 0; // the Nth exchange from now fails before writing (fixture faults)
#endif
// One slot write: true when the slot held `expected`, now holds `desired` (read back) and its protection is restored.
// A failed protection restore after the swap puts `expected` back and reports false.
inline bool exchange(std::uintptr_t at, std::uint32_t expected, std::uint32_t desired) {
#ifdef X3M_LOADING_FL_FIXTURE
    if (fixture_fail_exchange && !--fixture_fail_exchange) return false;
#endif
    DWORD old = 0, unused = 0;
    if (!at || (at & 3) || !VirtualProtect(reinterpret_cast<void*>(at), 4, PAGE_READWRITE, &old)) return false;
    auto* slot = reinterpret_cast<volatile LONG*>(at);
    const LONG seen = InterlockedCompareExchange(slot, LONG(desired), LONG(expected));
    bool protected_again = VirtualProtect(reinterpret_cast<void*>(at), 4, old, &unused) != FALSE;
    if (std::uint32_t(seen) == expected && !protected_again) {
        if (VirtualProtect(reinterpret_cast<void*>(at), 4, PAGE_READWRITE, &unused)) {
            InterlockedCompareExchange(slot, LONG(expected), LONG(desired));
            VirtualProtect(reinterpret_cast<void*>(at), 4, old, &unused);
        }
        return false;
    }
    return std::uint32_t(seen) == expected && protected_again && std::uint32_t(*slot) == desired;
}
}
