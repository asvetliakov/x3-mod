#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Executable identity of X3AP.exe (docs/reverse-engineering/executable-identity.md):
// structure plus verified sites; file hashes are provenance, never a gate.
//
// Structure: the mapped headers of the main module at the preferred base
// (i386 PE32, link stamp, entry point, SizeOfImage, the four-section table)
// and the file size on disk. IMAGE_FILE_LARGE_ADDRESS_AWARE (0x20) and the
// optional-header CheckSum are not compared, so the image with the "4GB patch"
// applied (NTCore 4gb_patch.exe sets the bit and rewrites CheckSum) is the same
// image. Sites: every hook compares its own whole-instruction bytes before it
// writes (engine_patch::claim/claim_call/verify_bytes, or a module memcmp);
// every engine global the proxy reads is tied to the image by one anchor below,
// a whole instruction of .text whose absolute operand is that global. The
// Python mirror is verification/probe/exe_identity.py (host test
// test_exe_identity compares the two tables). Documented PE/Win32 only.
namespace x3m::executable_identity {
constexpr std::uintptr_t image_base = 0x00400000;
constexpr std::uint32_t image_size = 0x002f5000, entry_point = 0x00112ead, time_date_stamp = 0x5a1d70ad;
constexpr std::uint16_t characteristics_without_laa = 0x0103; // RELOCS_STRIPPED | EXECUTABLE_IMAGE | 32BIT_MACHINE
constexpr std::uint16_t large_address_aware_bit = IMAGE_FILE_LARGE_ADDRESS_AWARE;
constexpr unsigned long long file_size = 2153984;
struct Section { char name[8]; std::uint32_t virtual_size, virtual_address, raw_size, raw_pointer, characteristics; };
constexpr Section sections[] = {
    {{'.','t','e','x','t',0,0,0}, 0x00130630, 0x00001000, 0x00130800, 0x00000400, 0x60000020},
    {{'.','r','d','a','t','a',0,0}, 0x0004074d, 0x00132000, 0x00040800, 0x00130c00, 0x40000040},
    {{'.','d','a','t','a',0,0,0}, 0x000efb58, 0x00173000, 0x0000b000, 0x00171400, 0xc0000040},
    {{'.','r','s','r','c',0,0,0}, 0x00091814, 0x00263000, 0x00091a00, 0x0017c400, 0x40000040}};
constexpr unsigned section_count = sizeof sections / sizeof sections[0];
// {instruction VA, global VA, opcode/ModRM prefix}: the instruction is the
// prefix, the little-endian global (disp32 absolute operand) and an optional
// one-byte immediate suffix. No anchor overlaps a patched site. The
// comment names the modules that read the global.
struct Anchor { std::uint32_t va, global; std::uint8_t prefix_length, prefix[3], suffix_length = 0, suffix = 0; };
constexpr Anchor anchors[] = {
    {0x00401b91, 0x0057fc60, 2, {0x8b, 0x35}},       // pause_key_only
    {0x00433cbe, 0x00587b88, 2, {0x89, 0x15}},       // chase_aim_trace
    {0x004e21c7, 0x00596928, 2, {0xd9, 0x05}},       // collide_memo
    {0x004e27df, 0x00596934, 2, {0x89, 0x35}},       // collide_narrow_census
    {0x0049959b, 0x00596988, 2, {0x8b, 0x2d}},       // resource_reader, loading_probes
    {0x004995a1, 0x0059698c, 1, {0xa1}},             // resource_reader
    {0x0042760b, 0x006069ac, 2, {0x8b, 0x0d}},       // chase_lead
    {0x004275fa, 0x006069b0, 3, {0x0f, 0xbf, 0x05}}, // chase_lead
    {0x004275f3, 0x006069b4, 3, {0x0f, 0xbf, 0x15}}, // chase_lead
    {0x00401c19, 0x00606f34, 1, {0xa1}},             // sector_background, lod_scale, sun_occlusion
    {0x00401bb0, 0x00606f38, 2, {0x8b, 0x0d}},       // chase_camera/aim/fire/lead, sun_occlusion
    {0x0040258b, 0x00606f3c, 1, {0xa1}},             // game_phases
    {0x004971d3, 0x00606f44, 1, {0xa1}},             // music_keep (media record list head; outside the music patch sites)
    {0x0041efd6, 0x00606fc0, 2, {0x8b, 0x35}},       // sector_background
    {0x0041305f, 0x00606fd4, 2, {0x8b, 0x15}},       // chase_aim_trace
    {0x004343e6, 0x00607040, 2, {0x8b, 0x2d}},       // sector_background
    {0x00445a3a, 0x00607ce8, 2, {0x83, 0x3d}, 1, 0x00}, // chase_aim_trace, chase_fire (0x004074de is a chase_aim_trace site)
    {0x00401a0d, 0x00608504, 2, {0x89, 0x1d}},       // chase_*, sector_background, motion_output, sun_occlusion
    {0x00401e69, 0x0060850c, 1, {0xa1}},             // chase_transition
    {0x00403367, 0x00608518, 1, {0xa3}},             // object_trace, object_lifetime, sun_light_poll, submit/residual_phases, cull_census, point_light
    {0x0048a6d2, 0x0060851c, 2, {0xd9, 0x05}},       // collide_memo
    {0x004e234b, 0x00608534, 2, {0x8a, 0x1d}},       // collide_memo
    {0x004e29fd, 0x00608538, 1, {0xa3}},             // collide_memo
    {0x004e2a24, 0x0060853c, 1, {0xa3}},             // collide_memo
    {0x004e246e, 0x00608540, 2, {0x8b, 0x0d}},       // collide_memo
    {0x004e0bff, 0x00608544, 2, {0x89, 0x3d}},       // collide_memo
    {0x004e294c, 0x00608548, 1, {0xa3}},             // collide_memo
    {0x004e0c05, 0x0060854c, 2, {0x89, 0x3d}},       // collide_memo, collide_narrow_census
    {0x00401d6b, 0x006085e4, 2, {0x8b, 0x0d}},       // chase_transition
    {0x00412c88, 0x006085f4, 2, {0x8b, 0x2d}},       // resource_reader
    {0x004ae07f, 0x006085f8, 2, {0x8b, 0x0d}},       // resource_reader
    {0x00413ade, 0x006089f8, 2, {0x01, 0x1d}},       // resource_reader
    {0x004e3f9d, 0x006089fc, 2, {0x8b, 0x0d}},       // resource_reader
    {0x004b9a60, 0x00608a38, 2, {0x89, 0x35}},       // camera_state, object_trace
    {0x004b9a08, 0x00608a40, 2, {0x89, 0x35}},       // camera_state, object_trace
    {0x004b9958, 0x00608a44, 2, {0x89, 0x35}},       // object_trace
    {0x004b99b0, 0x00608a48, 2, {0x89, 0x35}},       // object_trace
    {0x00401e0e, 0x00608adc, 2, {0x89, 0x35}},       // game_phases
    {0x0041d00c, 0x00608dac, 3, {0x0f, 0xbf, 0x0d}}, // chase_lead
    {0x0041d020, 0x00608db0, 2, {0x8b, 0x0d}},       // chase_lead
    {0x00524fa7, 0x006619ec, 1, {0xa3}}};            // collide_memo, collide_box_cull
constexpr unsigned anchor_count = sizeof anchors / sizeof anchors[0];

// Read is bool(std::uintptr_t address, void* out, std::size_t size): a bounded,
// fault-free read of this process (engine_memory::read in production).
template <class Read>
bool nt_headers(std::uintptr_t base, Read read, IMAGE_NT_HEADERS32* nt, std::uint32_t* nt_offset) {
    IMAGE_DOS_HEADER dos{};
    if (!read(base, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew >= 0x1000) return false;
    if (!read(base + std::uint32_t(dos.e_lfanew), nt, sizeof *nt) || nt->Signature != IMAGE_NT_SIGNATURE) return false;
    *nt_offset = std::uint32_t(dos.e_lfanew);
    return true;
}
// The mapped image at image_base has the known headers and section table.
template <class Read>
bool known_structure(Read read) {
    IMAGE_NT_HEADERS32 nt{}; std::uint32_t offset = 0;
    if (!nt_headers(image_base, read, &nt, &offset)) return false;
    const IMAGE_FILE_HEADER& f = nt.FileHeader; const IMAGE_OPTIONAL_HEADER32& o = nt.OptionalHeader;
    if (f.Machine != IMAGE_FILE_MACHINE_I386 || f.NumberOfSections != section_count || f.TimeDateStamp != time_date_stamp ||
        f.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER32) ||
        std::uint16_t(f.Characteristics & ~large_address_aware_bit) != characteristics_without_laa ||
        o.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || o.ImageBase != image_base || o.SizeOfImage != image_size ||
        o.AddressOfEntryPoint != entry_point) return false;
    IMAGE_SECTION_HEADER table[section_count]{};
    if (!read(image_base + offset + sizeof(IMAGE_NT_HEADERS32), table, sizeof table)) return false;
    for (unsigned i = 0; i < section_count; ++i) {
        const Section& s = sections[i]; const IMAGE_SECTION_HEADER& h = table[i];
        if (std::memcmp(h.Name, s.name, 8) || h.Misc.VirtualSize != s.virtual_size || h.VirtualAddress != s.virtual_address ||
            h.SizeOfRawData != s.raw_size || h.PointerToRawData != s.raw_pointer || h.Characteristics != s.characteristics) return false;
    }
    return true;
}
// Every anchor instruction is present byte for byte.
template <class Read>
bool anchors_match(Read read) {
    for (const Anchor& a : anchors) {
        unsigned char expected[8]{}, actual[8]{};
        const unsigned length = a.prefix_length + 4u + a.suffix_length;
        std::memcpy(expected, a.prefix, a.prefix_length);
        std::memcpy(expected + a.prefix_length, &a.global, 4);
        if (a.suffix_length) expected[a.prefix_length + 4] = a.suffix;
        if (!read(a.va, actual, length) || std::memcmp(actual, expected, length)) return false;
    }
    return true;
}
// IMAGE_FILE_LARGE_ADDRESS_AWARE of the image mapped at base, as the loader
// saw it. false when the headers cannot be read.
template <class Read>
bool large_address_aware(std::uintptr_t base, Read read) {
    IMAGE_NT_HEADERS32 nt{}; std::uint32_t offset = 0;
    return nt_headers(base, read, &nt, &offset) && (nt.FileHeader.Characteristics & large_address_aware_bit) != 0;
}
// The module's file on disk has the known size (GetFileSizeEx; no read).
inline bool known_file_size(HMODULE module) {
    wchar_t path[32768];
    const DWORD length = GetModuleFileNameW(module, path, 32768);
    if (!length || length >= 32768) return false;
    HANDLE file = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    const bool ok = GetFileSizeEx(file, &size) && static_cast<unsigned long long>(size.QuadPart) == file_size;
    CloseHandle(file);
    return ok;
}
}
