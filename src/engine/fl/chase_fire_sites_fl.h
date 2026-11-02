#pragma once
#include <cstdint>
#include <cstring>

// FL edition of the chase cursor-fire admission site (the AP site is the spec of src/proxy/chase_fire.cpp). Facts:
// docs/reverse-engineering/fl/chase-fire-aim.md (offline study, then "Verified 2026-11-03 against X3FL.exe"; verifier
// verification/probe/verify_chase_fire_sites_fl.py). X3FL.exe is relocatable: every value below is an RVA added to
// x3m::fl::module_base(), or a field offset. No Windows dependency (the native fixture compiles it).
//
// The seam is one six-byte whole instruction inside the native fire function RVA 0x04e310 (ECX = ship, four
// callee-cleaned stack words target, gun group, fire flags, mask; RET 0x10):
//   0x04eca0  0f 85 rel32  jne 0x04ef9f       (flags & 0x22) != 0x22: no cursor ray
//   0x04eca6  83 3d [0x1aa184] 00  cmp [cursor active],0
//   0x04ecad  0f 84 rel32  je 0x04ef9f        <- the seam: the cursor is natively inactive, skip the cursor ray
// Entry sets EBX = entry ESP - 4 (0x04e311) and EBP = an aligned frame (0x04e324); neither is written again before the
// epilogue. So at the seam [EBX+0xc] = gun group, [EBX+0x10] = fire flags (never written in the function), and
// [EBP-0x3c] = the ship (sole writer 0x04e350, straight-line prologue, no inbound edge before it).
// Cursor cells: x 0x1aa180, active 0x1aa184, y 0x1aa188 (the writer 0x07bc3a stores active, x, y; x precedes active).
// Defaults slot 0x1a82f8 -> +0 screen record (+4/+6 signed width/height), as the native unprojection 0x0a50f0 reads it
// with the camera's normalized viewport +0x288..+0x294 in y0,y1,x0,x1 order. Registry slot 0x1a82e4 (resolver 0x01e420).
namespace x3m::fl::chase_fire_fl {
constexpr unsigned max_relocs = 4;
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
constexpr unsigned window_count = 5, max_window = 182;
inline constexpr unsigned char prologue_window[75] = {
    0x53, 0x8b, 0xdc, 0x83, 0xec, 0x08, 0x83, 0xe4, 0xf0, 0x83, 0xc4, 0x04, 0x55, 0x8b, 0x6b, 0x04,
    0x89, 0x6c, 0x24, 0x04, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x00, 0x00, 0x00, 0x00, 0x64, 0xa1, 0x00,
    0x00, 0x00, 0x00, 0x50, 0x64, 0x89, 0x25, 0x00, 0x00, 0x00, 0x00, 0x53, 0x81, 0xec, 0x38, 0x03,
    0x00, 0x00, 0x56, 0x8b, 0x73, 0x0c, 0xc7, 0x45, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x57, 0x8b, 0xf9,
    0x89, 0x7d, 0xc4, 0x85, 0xf6, 0x0f, 0x88, 0xd1, 0x1b, 0x00, 0x00,
};
inline constexpr unsigned char admission_window[142] = {
    0x8b, 0x4b, 0x10, 0x89, 0x85, 0x48, 0xfe, 0xff, 0xff, 0x8b, 0xc1, 0x0f, 0x28, 0x85, 0x40, 0xfe,
    0xff, 0xff, 0x83, 0xe0, 0x22, 0x0f, 0x29, 0x85, 0xe0, 0xfd, 0xff, 0xff, 0x3c, 0x22, 0x0f, 0x85,
    0xf9, 0x02, 0x00, 0x00, 0x83, 0x3d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x84, 0xec, 0x02, 0x00,
    0x00, 0x8b, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x85, 0xc9, 0x0f, 0x84, 0xd9, 0x02, 0x00, 0x00, 0xff,
    0x71, 0x10, 0xe8, 0x57, 0xf7, 0xfc, 0xff, 0x8b, 0x7d, 0xc4, 0x8b, 0xf0, 0x89, 0x75, 0xec, 0x85,
    0xf6, 0x0f, 0x84, 0xcb, 0x02, 0x00, 0x00, 0x39, 0x7e, 0x10, 0x0f, 0x85, 0xc2, 0x02, 0x00, 0x00,
    0x8b, 0x96, 0xd8, 0x01, 0x00, 0x00, 0x85, 0xd2, 0x0f, 0x88, 0xb4, 0x02, 0x00, 0x00, 0x8b, 0xcf,
    0xe8, 0x39, 0xbc, 0x00, 0x00, 0x3b, 0xd0, 0x0f, 0x8d, 0xa5, 0x02, 0x00, 0x00, 0x52, 0x8b, 0xcf,
    0xe8, 0xd9, 0xbc, 0x00, 0x00, 0x3b, 0x43, 0x0c, 0x0f, 0x85, 0x94, 0x02, 0x00, 0x00,
};
inline constexpr unsigned char unproject_call_window[42] = {
    0x8b, 0x4d, 0xec, 0x81, 0xfe, 0x88, 0x13, 0x00, 0x00, 0xb8, 0x88, 0x13, 0x00, 0x00, 0x0f, 0x4c,
    0xf0, 0x8d, 0x45, 0xa0, 0x50, 0x8b, 0x49, 0x58, 0x56, 0xff, 0x35, 0x00, 0x00, 0x00, 0x00, 0xff,
    0x35, 0x00, 0x00, 0x00, 0x00, 0xe8, 0xf4, 0x62, 0x05, 0x00,
};
inline constexpr unsigned char cursor_writer_window[40] = {
    0x8b, 0x4d, 0x18, 0x8b, 0x41, 0x01, 0xa3, 0x00, 0x00, 0x00, 0x00, 0x8b, 0x41, 0x06, 0xa3, 0x00,
    0x00, 0x00, 0x00, 0x8b, 0x41, 0x0b, 0xa3, 0x00, 0x00, 0x00, 0x00, 0x8b, 0x41, 0x10, 0xa3, 0x00,
    0x00, 0x00, 0x00, 0xe9, 0xbc, 0xfa, 0xff, 0xff,
};
inline constexpr unsigned char screen_window[182] = {
    0x8b, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x89, 0x44, 0x24, 0x14, 0x8b, 0x0b, 0x0f, 0xb7, 0x51, 0x04,
    0x0f, 0xbf, 0xca, 0x89, 0x4c, 0x24, 0x10, 0x66, 0x85, 0xd2, 0x75, 0x04, 0x33, 0xf6, 0xeb, 0x1b,
    0x8b, 0x54, 0x24, 0x20, 0x8b, 0x4c, 0x24, 0x10, 0x33, 0xc0, 0x0f, 0xac, 0xd0, 0x10, 0xc1, 0xfa,
    0x10, 0xf7, 0xf9, 0x8b, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x8b, 0xf0, 0x8b, 0x87, 0x90, 0x02, 0x00,
    0x00, 0x03, 0x87, 0x94, 0x02, 0x00, 0x00, 0x99, 0x2b, 0xc2, 0xd1, 0xf8, 0x2b, 0xf0, 0x8b, 0x44,
    0x24, 0x14, 0xf7, 0xed, 0x8b, 0xc8, 0x8b, 0xc6, 0x81, 0xc1, 0x00, 0x80, 0x00, 0x00, 0x83, 0xd2,
    0x00, 0x0f, 0xac, 0xd1, 0x10, 0xc1, 0xfa, 0x10, 0xf7, 0xe9, 0x05, 0x00, 0x80, 0x00, 0x00, 0x83,
    0xd2, 0x00, 0x0f, 0xac, 0xd0, 0x10, 0x03, 0xc0, 0xc1, 0xfa, 0x10, 0x89, 0x44, 0x24, 0x10, 0x8b,
    0x03, 0x0f, 0xb7, 0x48, 0x06, 0x0f, 0xbf, 0xc1, 0x89, 0x44, 0x24, 0x20, 0x66, 0x85, 0xc9, 0x75,
    0x04, 0x33, 0xf6, 0xeb, 0x15, 0x8b, 0x54, 0x24, 0x24, 0x8b, 0x4c, 0x24, 0x20, 0x33, 0xc0, 0x0f,
    0xac, 0xd0, 0x10, 0xc1, 0xfa, 0x10, 0xf7, 0xf9, 0x8b, 0xf0, 0x8b, 0x87, 0x88, 0x02, 0x00, 0x00,
    0x03, 0x87, 0x8c, 0x02, 0x00, 0x00,
};
constexpr Window windows[window_count] = {
    {"prologue", 0x04e310, 75, prologue_window, 0x5347c1f5u, 1, {0x19}, {0x14d3b5}},
    {"admission", 0x04ec82, 142, admission_window, 0x56a65ba8u, 2, {0x26, 0x33}, {0x1aa184, 0x1a82e4}},
    {"unproject_call", 0x04edd2, 42, unproject_call_window, 0xf6c4c372u, 2, {0x1b, 0x21}, {0x1aa188, 0x1aa180}},
    {"cursor_writer", 0x07bc3a, 40, cursor_writer_window, 0xe849e23au, 4, {0x07, 0x0f, 0x17, 0x1f}, {0x1aaa10, 0x1aa184, 0x1aa180, 0x1aa188}},
    {"screen", 0x0a51ef, 182, screen_window, 0xc013bc5cu, 2, {0x02, 0x35}, {0x1a82f8, 0x1a82f8}},
};
enum WindowIndex { w_prologue, w_admission, w_unproject_call, w_cursor_writer, w_screen };

// The seam: RVA, the six expected bytes (je rel32), its window and its branch target.
struct Seam {
    const char* name;
    std::uint32_t rva;
    unsigned char bytes[6];
    unsigned rel32_offset;
    WindowIndex window;
};
constexpr Seam admission = {"chase_cursor_admission_fl", 0x04ecad, {0x0f, 0x84, 0xec, 0x02, 0x00, 0x00}, 2, w_admission};
constexpr std::uint32_t admission_target_rva = 0x04ef9f, fire_rva = 0x04e310, fire_end_rva = 0x04ff46;

// Stack/frame facts at the seam (prologue window): EBX = entry ESP - 4, so the stack words are at EBX + 8 + 4 * i.
constexpr unsigned arg_target = 0x8, arg_group = 0xc, arg_flags = 0x10, arg_mask = 0x14;
constexpr unsigned ship_below_ebp = 0x3c; // [EBP - 0x3c] = the ship (ECX at entry)
// Globals (RVAs; each is a relocated dword of a window, checked against the live base).
constexpr std::uint32_t cursor_x_rva = 0x1aa180, cursor_active_rva = 0x1aa184, cursor_y_rva = 0x1aa188,
                        steering_rva = 0x1aaa10;
constexpr std::uint32_t registry_slot_rva = 0x1a82e4; // -> registry: +0 map {buckets, count}, +0x10 active handle
constexpr std::uint32_t defaults_slot_rva = 0x1a82f8; // -> +0 screen record (+4/+6 width/height)

constexpr std::uint32_t fnv1a(const unsigned char* p, unsigned n) {
    std::uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
    return h;
}
constexpr std::uint32_t le32(const unsigned char* b) {
    return std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 | std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
}
// The install decision: every window read at base + RVA through `read(address, out, size)` must equal the expected
// bytes with each relocated dword equal to base + its target RVA. nullptr when all five match, else the refusal; *which
// names the first refused window.
template <class Read> const char* plan(std::uintptr_t base, Read read, const char** which = nullptr) {
    if (!base || (base & 0xffff)) return "invalid_base";
    for (const Window& w : windows) {
        if (which) *which = w.name;
        unsigned char now[max_window]{};
        if (!read(base + w.rva, now, w.length)) return "unreadable";
        for (unsigned r = 0; r < w.reloc_count; ++r) {
            if (le32(now + w.reloc_offset[r]) != std::uint32_t(base + w.reloc_target[r])) return "slot_mismatch";
            std::memset(now + w.reloc_offset[r], 0, 4);
        }
        if (std::memcmp(now, w.bytes, w.length)) return "window_mismatch";
    }
    if (which) *which = "-";
    return nullptr;
}

// Compile-time consistency: the measured hashes, the seam inside its window, the branch target and the operands the
// handler relies on.
constexpr bool windows_hash() {
    for (const Window& w : windows)
        if (w.length > max_window || fnv1a(w.bytes, w.length) != w.fnv) return false;
    return true;
}
static_assert(windows_hash(), "every window is the measured one");
constexpr bool seam_in_window(const Seam& s) {
    const Window& w = windows[s.window];
    if (s.rva < w.rva || s.rva + 6 > w.rva + w.length) return false;
    for (unsigned i = 0; i < 6; ++i)
        if (w.bytes[s.rva - w.rva + i] != s.bytes[i]) return false;
    return true;
}
static_assert(seam_in_window(admission), "the seam's six bytes sit in the admission window");
static_assert(admission.rva + 6 + le32(admission.bytes + 2) == admission_target_rva, "je rel32 target");
static_assert(admission_window[0] == 0x8b && admission_window[2] == arg_flags && admission_window[0x12] == 0x83 &&
                  admission_window[0x14] == 0x22 && admission_window[0x1c] == 0x3c && admission_window[0x1d] == 0x22,
              "mov ecx,[ebx+0x10]; and eax,0x22; cmp al,0x22 before the seam");
static_assert(admission_window[0x24] == 0x83 && admission_window[0x25] == 0x3d && admission_window[0x2a] == 0 &&
                  windows[w_admission].reloc_offset[0] == 0x26 && windows[w_admission].reloc_target[0] == cursor_active_rva &&
                  windows[w_admission].rva + 0x2b == admission.rva,
              "cmp dword [cursor active],0 immediately before the seam");
static_assert(admission_window[0x48] == 0x7d && admission_window[0x49] == std::uint8_t(-int(ship_below_ebp)) &&
                  admission_window[0x86] == 0x43 && admission_window[0x87] == arg_group,
              "mov edi,[ebp-0x3c] after the resolver; cmp eax,[ebx+0xc] (group mapping)");
static_assert(prologue_window[1] == 0x8b && prologue_window[2] == 0xdc && prologue_window[0x14] == 0x8b &&
                  prologue_window[0x15] == 0xec && prologue_window[0x40] == 0x89 && prologue_window[0x41] == 0x7d &&
                  prologue_window[0x42] == std::uint8_t(-int(ship_below_ebp)) && prologue_window[0x3e] == 0x8b &&
                  prologue_window[0x3f] == 0xf9,
              "mov ebx,esp; mov ebp,esp; mov edi,ecx; mov [ebp-0x3c],edi in the prologue");
static_assert(cursor_writer_window[5] == 1 && windows[w_cursor_writer].reloc_target[0] == steering_rva &&
                  cursor_writer_window[0x0d] == 6 && windows[w_cursor_writer].reloc_target[1] == cursor_active_rva &&
                  cursor_writer_window[0x15] == 0xb && windows[w_cursor_writer].reloc_target[2] == cursor_x_rva &&
                  cursor_writer_window[0x1d] == 0x10 && windows[w_cursor_writer].reloc_target[3] == cursor_y_rva,
              "the writer stores record +1 steering, +6 active, +0xb x, +0x10 y");
static_assert(cursor_x_rva + 4 == cursor_active_rva && cursor_active_rva + 4 == cursor_y_rva,
              "one 12-byte block x, active, y");
static_assert(windows[w_unproject_call].reloc_target[0] == cursor_y_rva &&
                  windows[w_unproject_call].reloc_target[1] == cursor_x_rva && unproject_call_window[0x16] == 0x49 &&
                  unproject_call_window[0x17] == 0x58,
              "the cursor ray pushes y then x and the camera is cockpit+0x58");
static_assert(screen_window[0x0f] == 4 && screen_window[0x3d] == 0x90 && screen_window[0x43] == 0x94 &&
                  screen_window[0x84] == 6 && screen_window[0xac] == 0x88 && screen_window[0xb2] == 0x8c &&
                  windows[w_screen].reloc_target[0] == defaults_slot_rva,
              "unprojection: width +4 with x0,x1 = +0x290/+0x294, height +6 with y0,y1 = +0x288/+0x28c");
// The module base is 64 KB aligned, so the seam's qword geometry is its RVA's: the five-byte jump crosses an aligned
// qword (plain copy, install window only).
static_assert((admission.rva & 7u) + 5u > 8u, "qword geometry of the five-byte jump");
}
