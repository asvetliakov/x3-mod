#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Executable identity of X3FL.exe (X3: Farnham's Legacy; docs/architecture/farnhams-legacy-build.md),
// the FL counterpart of src/proxy/executable_identity.h. FL edition only.
//
// X3FL.exe is ASLR-relocatable (DllCharacteristics 0x8140, a .reloc section): the identity, and every
// FL address after it, is module-base relative (an RVA added to module_base()), never 0x400000 based.
// Structure: the mapped headers of the main module at its actual base (i386 PE32, link stamp, entry
// point RVA, SizeOfImage, the eight-section table) and the file size on disk. The in-memory
// OptionalHeader.ImageBase is not compared (the loader may rewrite it on relocation);
// IMAGE_FILE_LARGE_ADDRESS_AWARE and the CheckSum are free, as for AP. One anchor: the entry point's
// first bytes (call rel32; jmp rel32, no base relocation touches them), read at base + RVA. The
// constants are generated from the file (executable_identity_fl_inc.h, tools/analysis/
// fl_identity_constants.py); the host test is verification/analysis/test_fl_identity.py.
// Documented PE/Win32 only.
namespace x3m::fl {
struct Section {
    char name[8];
    std::uint32_t virtual_size, virtual_address, raw_size, raw_pointer, characteristics;
};
#include "executable_identity_fl_inc.h"
constexpr unsigned section_count = sizeof sections / sizeof sections[0];
constexpr std::uint16_t large_address_aware_bit = IMAGE_FILE_LARGE_ADDRESS_AWARE;

// Read is bool(std::uintptr_t address, void* out, std::size_t size): a bounded, fault-free read of
// this process. Returns nullptr when the image mapped at base has the known headers, section table
// and entry anchor, else the first mismatching field (a static string).
template <class Read> const char* structure_mismatch(std::uintptr_t base, Read read) {
    IMAGE_DOS_HEADER dos{};
    if (!base || !read(base, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
        dos.e_lfanew >= 0x1000)
        return "dos_header";
    IMAGE_NT_HEADERS32 nt{};
    const std::uint32_t offset = std::uint32_t(dos.e_lfanew);
    if (!read(base + offset, &nt, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE) return "nt_header";
    const IMAGE_FILE_HEADER& f = nt.FileHeader;
    const IMAGE_OPTIONAL_HEADER32& o = nt.OptionalHeader;
    if (f.Machine != IMAGE_FILE_MACHINE_I386 || f.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER32) ||
        o.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return "machine";
    if (f.TimeDateStamp != time_date_stamp) return "timestamp";
    if (o.SizeOfImage != image_size) return "image_size";
    if (o.AddressOfEntryPoint != entry_point) return "entry_point";
    if (std::uint16_t(f.Characteristics & ~large_address_aware_bit) != characteristics_without_laa)
        return "characteristics";
    if (f.NumberOfSections != section_count) return "section_count";
    IMAGE_SECTION_HEADER table[section_count]{};
    // The section table lies inside the mapped headers: e_lfanew + 0xF8 + 8 * 40 <= SizeOfHeaders.
    if (o.SizeOfHeaders < offset + sizeof(IMAGE_NT_HEADERS32) + sizeof table) return "headers";
    if (!read(base + offset + sizeof(IMAGE_NT_HEADERS32), table, sizeof table)) return "section_table";
    for (unsigned i = 0; i < section_count; ++i) {
        const Section& s = sections[i];
        const IMAGE_SECTION_HEADER& h = table[i];
        if (std::memcmp(h.Name, s.name, 8) || h.Misc.VirtualSize != s.virtual_size ||
            h.VirtualAddress != s.virtual_address || h.SizeOfRawData != s.raw_size ||
            h.PointerToRawData != s.raw_pointer || h.Characteristics != s.characteristics)
            return "section_table";
    }
    unsigned char actual[sizeof entry_anchor]{};
    if (!read(base + entry_anchor_rva, actual, sizeof actual) || std::memcmp(actual, entry_anchor, sizeof actual))
        return "entry_anchor";
    return nullptr;
}
// The module's file on disk has the known size (GetFileSizeEx; no read).
inline bool known_file_size(HMODULE module) {
    wchar_t path[32768];
    const DWORD length = GetModuleFileNameW(module, path, 32768);
    if (!length || length >= 32768) return false;
    HANDLE file = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    const bool ok = GetFileSizeEx(file, &size) && static_cast<unsigned long long>(size.QuadPart) == file_size;
    CloseHandle(file);
    return ok;
}

// The main module (GetModuleHandleW(nullptr)) is X3FL.exe: structure, entry anchor and file size.
// Evaluated once per process and cached; LastError preserved. The first call belongs outside DllMain
// (the loader calls it from load_backend).
bool verified();
// The main module's base when verified(), else 0: the base every FL RVA is added to.
std::uintptr_t module_base();
// nullptr when verified(), else the first mismatching field: dos_header, nt_header, machine,
// timestamp, image_size, entry_point, characteristics, section_count, headers, section_table, entry_anchor,
// file_size.
const char* mismatch_reason();
}
