#pragma once
#include <cstdint>
#include <cstring>
#include "../../proxy/pause_key_only_core.h"

// FL edition of the pause-key-only site (the AP twin is src/proxy/pause_key_only_core.h, whose key rule, default key
// and X3M_PAUSE_KEY parser the FL module reuses). Facts: docs/reverse-engineering/fl/pause-background.md, section 1 and
// "Port (2026-11-03)" (verifier verification/probe/verify_pause_key_only_sites_fl.py). X3FL.exe is relocatable: every
// value below is an RVA added to x3m::fl::module_base(). No Windows dependency (the native fixture compiles it).
//
// The in-flight input function RVA 0x078690 (one direct caller, 0x078150) waits in the loop below while bit 0 of
// [*ctx + 0x4a0] is set; the exit at 0x07885e is the only bit-0 clearer after construction. Entry (window `entry`):
//   078699  push ebx ; movzx ebx,word [base+0x22ddf0] ; push ebp/esi/edi ; mov [esp+0x14],ebx   ; previous key, zero-extended
//   0786a8  call 0x0eccd0 (key reader, AX) ; ... ; movzx esi,ax ; ... ; test cl,1 ; jne 0x0787ee
// The wait loop (window `loop`, 136 bytes):
//   0787ee  mov eax,[base+0x1a8478]; ECX=EBX=[eax+0x454] mouse, EBP=EDI=[eax+0x450] joystick; [esp+0x10]=ECX
//   078807  66 85 f6              test si,si
//   07880a  74 0d                 je   0x078819                   ; no key
//   07880c  8b c6                 mov  eax,esi                    <- site (13 bytes, four whole instructions)
//   07880e  33 44 24 14           xor  eax,[esp+0x14]
//   078812  a9 ff 0f 00 00        test eax,0xfff
//   078817  75 45                 jne  0x07885e                   ; exit: the key differs from the previous read
//   078819  mov eax,ebx; or eax,ecx; cmp eax,ebx; jne 0x07885e    ; exit: a new mouse button
//   078821  mov eax,edi; or eax,ebp; cmp eax,edi; jne 0x07885e    ; exit: a new joystick button (FL only)
//   078829  call 0x0ec620 (pump) ; reload buttons ; [esp+0x14] = movzx si ; call 0x0eccd0 ; movzx esi,ax ; jmp 0x078807
//   07885e  mov ecx,[base+0x2243cc]; [ecx+0x4a0] = ([ecx+0x4a0] & ~1) | 2    ; leave the pause
// The site becomes  cmp si,KEY ; jne 0x078819 ; cmp esi,[esp+0x14] ; jne 0x07885e  (66 81 fe KK KK 75 06 3b 74 24 14
// 75 45): only KEY, newly pressed, leaves by key; every other key falls into the unchanged mouse and joystick tests.
// ESI and [esp+0x14] are both zero-extended 16-bit keys (movzx at 0x0786b3/0x078859 and 0x078699/0x078845), so the
// DWORD compare is AP's exact 16-bit compare. AP's `cmp si,bx` is wrong here: FL's EBX is the mouse snapshot. The
// original wrote EAX (dead on both successors: the mouse test and the exit both load EAX first) and EFLAGS (consumed by
// the two JNE); the replacement writes only EFLAGS. ECX (live for the mouse test) is untouched. No edge enters the span
// or its first byte; no relocation lies in it; its last two bytes are unchanged.
namespace x3m::fl::pause_key_only_fl {
constexpr unsigned max_relocs = 3;
struct Window {
    const char* name;
    std::uint32_t rva;
    unsigned length;
    const unsigned char* bytes; // relocated dwords zeroed
    std::uint32_t fnv;          // FNV-1a of `bytes`
    unsigned reloc_count;
    unsigned reloc_offset[max_relocs];      // dword offsets inside the window
    std::uint32_t reloc_target[max_relocs]; // at the live base each dword must equal base + this RVA
};
constexpr unsigned window_count = 2, max_window = 136;
// clang-format off
inline constexpr unsigned char entry_window[60] = {
    0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x08,0x53,0x0f,0xb7,0x1d,0x00,0x00,0x00,0x00,0x55,0x56,0x57,
    0x89,0x5c,0x24,0x14,0xe8,0x23,0x46,0x07,0x00,0x8b,0x15,0x00,0x00,0x00,0x00,0x0f,0xb7,0xf0,0x66,0x89,
    0x35,0x00,0x00,0x00,0x00,0x8b,0x8a,0xa0,0x04,0x00,0x00,0xf6,0xc1,0x01,0x0f,0x85,0x22,0x01,0x00,0x00};
inline constexpr unsigned char loop_window[136] = {
    0xa1,0x00,0x00,0x00,0x00,0x8b,0x88,0x54,0x04,0x00,0x00,0x8b,0xd9,0x8b,0xa8,0x50,0x04,0x00,0x00,0x8b,
    0xfd,0x89,0x4c,0x24,0x10,0x66,0x85,0xf6,0x74,0x0d,0x8b,0xc6,0x33,0x44,0x24,0x14,0xa9,0xff,0x0f,0x00,
    0x00,0x75,0x45,0x8b,0xc3,0x0b,0xc1,0x3b,0xc3,0x75,0x3d,0x8b,0xc7,0x0b,0xc5,0x3b,0xc7,0x75,0x35,0xe8,
    0xf2,0x3d,0x07,0x00,0xa1,0x00,0x00,0x00,0x00,0x8b,0xfd,0x8b,0x5c,0x24,0x10,0x8b,0x88,0x54,0x04,0x00,
    0x00,0x8b,0xa8,0x50,0x04,0x00,0x00,0x0f,0xb7,0xc6,0x89,0x4c,0x24,0x10,0x89,0x44,0x24,0x14,0xe8,0x7b,
    0x44,0x07,0x00,0x8b,0x4c,0x24,0x10,0x0f,0xb7,0xf0,0xeb,0xa9,0x8b,0x0d,0x00,0x00,0x00,0x00,0x8b,0x81,
    0xa0,0x04,0x00,0x00,0x83,0xe0,0xfe,0x83,0xc8,0x02,0x89,0x81,0xa0,0x04,0x00,0x00};
// clang-format on
constexpr Window windows[window_count] = {
    {"entry", 0x078690, 60, entry_window, 0x87e58492u, 3, {0x0d, 0x1f, 0x29}, {0x22ddf0, 0x2243cc, 0x22ddf0}},
    {"loop", 0x0787ee, 136, loop_window, 0x8180c9b7u, 3, {0x01, 0x41, 0x72}, {0x1a8478, 0x1a8478, 0x2243cc}},
};
enum WindowIndex { w_entry, w_loop };

constexpr std::uint32_t function_rva = 0x078690, caller_rva = 0x078150, loop_rva = 0x078807, site_rva = 0x07880c,
                        mouse_test_rva = 0x078819, joystick_test_rva = 0x078821, pump_call_rva = 0x078829,
                        key_call_rva = 0x078850, exit_rva = 0x07885e, after_exit_rva = 0x078876;
constexpr std::uint32_t pump_rva = 0x0ec620, key_reader_rva = 0x0eccd0;
constexpr std::uint32_t input_slot_rva = 0x1a8478, context_slot_rva = 0x2243cc, previous_key_rva = 0x22ddf0;
constexpr unsigned input_mouse_offset = 0x454, input_joystick_offset = 0x450, context_flags_offset = 0x4a0;
constexpr unsigned site_length = 13, previous_key_slot = 0x14; // [esp+0x14] inside the loop
inline constexpr unsigned char original[site_length] = {0x8b, 0xc6, 0x33, 0x44, 0x24, 0x14, 0xa9,
                                                        0xff, 0x0f, 0x00, 0x00, 0x75, 0x45};

constexpr std::uint32_t fnv1a(const unsigned char* p, unsigned n) {
    std::uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
    return h;
}
constexpr std::uint32_t le32(const unsigned char* b) {
    return std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 | std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
}
// The replacement for `key` (AP's key rule, pause_key_only_core.h key_valid). Both rel8 are relative to the site, so the
// bytes do not depend on the base. False for an invalid key.
inline bool encode_site(std::uint32_t key, unsigned char out[site_length]) {
    if (!x3m::pause_key_only::core::key_valid(key)) return false;
    // clang-format off
    const unsigned char bytes[site_length] = {0x66,0x81,0xfe, static_cast<unsigned char>(key & 0xff),
                                              static_cast<unsigned char>((key >> 8) & 0xff), 0x75,0x06,
                                              0x3b,0x74,0x24,0x14, 0x75,0x45};
    // clang-format on
    std::memcpy(out, bytes, site_length);
    return true;
}
// The install decision: every window read at base + RVA through `read(address, out, size)` must equal the expected
// bytes with each relocated dword equal to base + its target RVA (the site's original bytes are inside `loop`, so an
// already patched or otherwise changed site is refused). nullptr when both match, else the refusal; *which names the
// first refused window.
template <class Read> const char* plan(std::uintptr_t base, Read read, const char** which = nullptr) {
    if (!base || (base & 0xffff)) return "invalid_site";
    for (const Window& w : windows) {
        if (which) *which = w.name;
        unsigned char now[max_window]{};
        if (!read(base + w.rva, now, w.length)) return "unreadable";
        for (unsigned r = 0; r < w.reloc_count; ++r) {
            if (le32(now + w.reloc_offset[r]) != std::uint32_t(base + w.reloc_target[r])) return "bytes_mismatch";
            std::memset(now + w.reloc_offset[r], 0, 4);
        }
        if (std::memcmp(now, w.bytes, w.length)) return "bytes_mismatch";
    }
    if (which) *which = "-";
    return nullptr;
}

constexpr bool windows_hash() {
    for (const Window& w : windows)
        if (w.length > max_window || fnv1a(w.bytes, w.length) != w.fnv) return false;
    return true;
}
static_assert(windows_hash(), "every window is the measured one");
constexpr bool site_in_loop() {
    for (unsigned i = 0; i < site_length; ++i)
        if (loop_window[site_rva - 0x0787ee + i] != original[i]) return false;
    return true;
}
static_assert(windows[w_loop].rva == 0x0787ee && site_in_loop(), "the loop window carries the original site bytes");
static_assert(site_rva + site_length == mouse_test_rva, "the mouse test follows the site");
static_assert(site_rva + site_length + std::int8_t(original[12]) == exit_rva, "the site's last jne goes to the exit");
static_assert(site_rva + 7 + 6 == mouse_test_rva && site_rva + 13 + 0x45 == exit_rva,
              "replacement: jne +6 -> the mouse test, jne +0x45 -> the exit");
static_assert(loop_window[loop_rva - 0x0787ee] == 0x66 && loop_window[loop_rva - 0x0787ee + 3] == 0x74 &&
                  loop_rva + 5 + loop_window[loop_rva - 0x0787ee + 4] == mouse_test_rva,
              "test si,si ; je the mouse test");
static_assert(entry_window[0x14] == 0x89 && entry_window[0x15] == 0x5c && entry_window[0x17] == previous_key_slot,
              "mov [esp+0x14],ebx: the previous key at entry");
}
