#pragma once
#include <cstdint>
#include <cstring>

// FL edition of the run-in-background site (the AP twin is src/proxy/run_in_background_sites.h, whose
// X3M_RUN_IN_BACKGROUND parser the FL module reuses). Facts: docs/reverse-engineering/fl/pause-background.md, section 2
// and "Port (2026-11-03)" (verifier verification/probe/verify_run_in_background_sites_fl.py). X3FL.exe is relocatable:
// every value below is an RVA added to x3m::fl::module_base(). No Windows dependency (the native fixture compiles it).
//
// Startup RVA 0x076bc0 sets the choice local [ebp+0x60] = -1 (0x076bfd); `-runinbg` / `/runinbg` store 1 (0x077124),
// `-noruninbg` / `/noruninbg` store 0 (0x077118). After the input block is created (call 0x0ebef0 at 0x0776b1, slot
// base+0x1a8478, flags word at +0) the decision window below is the local's only reader:
//   0776c2  a1 <0x5a8478>         mov  eax,[input slot]                         <- window `decision` (63 bytes)
//   0776c7  8b 4d 60              mov  ecx,[ebp+0x60]
//   0776ca  81 08 00 10 00 00     or   dword [eax],0x1000
//   0776d0  83 f9 ff 75 15        cmp ecx,-1 ; jne 0x0776ea
//   0776d5  b9 <0x58b9e8> e8 ..   mov ecx,"RunInBackground" ; call 0x0d8850   ; HKCU read only, DWORD in EAX
//   0776df  89 45 60 a1 <..> 8b 4d 60                                         ; store, reload input and choice
//   0776ea  85 c9                 test ecx,ecx
//   0776ec  74 08                 je   0x0776f6                     <- site (2 bytes): 90 90
//   0776ee  81 08 00 40 00 00     or   dword [eax],0x4000           ; RunInBackground on
//   0776f4  eb 06                 jmp  0x0776fc
//   0776f6  81 20 ff bf ff ff     and  dword [eax],0xffffbfff       ; RunInBackground off (only from the site)
//   0776fc  e8 .. (0x100590)      call input setup                  ; its first call's callee opens with sub esp
// With the JE replaced by two NOPs every choice takes the existing OR path: the bit is set as `-runinbg` sets it. The
// registers, the stack, the registry read and every call stay native; the NOPs write no flag, and the arithmetic flags
// the OR leaves (the AND left others) have no reader before 0x100480's `sub esp,0x410`. No handler runs. The two bytes
// lie in the aligned qword 0x0776e8: one lock cmpxchg8b. No edge enters 0x0776ed; no relocation lies in the span.
//
// The consumer (window `pump_gate`, the message pump RVA 0x0ec620): while the window is inactive ([base+0x1aaf34] == 0)
// the pump blocks in GetMessageA unless the bit is set, in which case it takes the PeekMessageA path at 0x0ec6f8:
//   0ec632  8b 0d <0x5aaf34> 85 c9 0f 85 <0x0ec6f8>                 ; active: the non-blocking path
//   0ec640  a1 <0x5a8478> f7 00 00 40 00 00 0f 85 <0x0ec6f8>        ; bit 0x4000: the non-blocking path
//   0ec651                                                          ; the blocking GetMessageA path
namespace x3m::fl::run_in_background_fl {
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
constexpr unsigned window_count = 2, max_window = 63;
// clang-format off
inline constexpr unsigned char decision_window[63] = {
    0xa1,0x00,0x00,0x00,0x00,0x8b,0x4d,0x60,0x81,0x08,0x00,0x10,0x00,0x00,0x83,0xf9,0xff,0x75,0x15,0xb9,
    0x00,0x00,0x00,0x00,0xe8,0x71,0x11,0x06,0x00,0x89,0x45,0x60,0xa1,0x00,0x00,0x00,0x00,0x8b,0x4d,0x60,
    0x85,0xc9,0x74,0x08,0x81,0x08,0x00,0x40,0x00,0x00,0xeb,0x06,0x81,0x20,0xff,0xbf,0xff,0xff,0xe8,0x8f,
    0x8e,0x08,0x00};
inline constexpr unsigned char pump_gate_window[31] = {
    0x8b,0x0d,0x00,0x00,0x00,0x00,0x85,0xc9,0x0f,0x85,0xb8,0x00,0x00,0x00,0xa1,0x00,0x00,0x00,0x00,0xf7,
    0x00,0x00,0x40,0x00,0x00,0x0f,0x85,0xa7,0x00,0x00,0x00};
// clang-format on
constexpr Window windows[window_count] = {
    {"decision", 0x0776c2, 63, decision_window, 0xd9802844u, 3, {0x01, 0x14, 0x21}, {0x1a8478, 0x18b9e8, 0x1a8478}},
    {"pump_gate", 0x0ec632, 31, pump_gate_window, 0xf620f8e2u, 2, {0x02, 0x0f}, {0x1aaf34, 0x1a8478}},
};
enum WindowIndex { w_decision, w_pump_gate };

constexpr std::uint32_t startup_rva = 0x076bc0, input_create_call_rva = 0x0776b1, site_rva = 0x0776ec,
                        set_bit_rva = 0x0776ee, clear_bit_rva = 0x0776f6, setup_call_rva = 0x0776fc,
                        after_window_rva = 0x077701, setup_rva = 0x100590, registry_read_rva = 0x0d8850;
constexpr std::uint32_t local_init_rva = 0x076bfd, noruninbg_store_rva = 0x077118, runinbg_store_rva = 0x077124;
constexpr std::uint32_t pump_rva = 0x0ec620, pump_blocking_rva = 0x0ec651, pump_nonblocking_rva = 0x0ec6f8;
constexpr std::uint32_t input_slot_rva = 0x1a8478, active_rva = 0x1aaf34, value_name_rva = 0x18b9e8;
constexpr unsigned choice_offset = 0x60; // [ebp+0x60]
constexpr std::uint32_t run_in_background_bit = 0x4000;
constexpr unsigned site_length = 2;
inline constexpr unsigned char original[site_length] = {0x74, 0x08}, patched[site_length] = {0x90, 0x90};

constexpr std::uint32_t fnv1a(const unsigned char* p, unsigned n) {
    std::uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
    return h;
}
constexpr std::uint32_t le32(const unsigned char* b) {
    return std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 | std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
}
// The install decision: both windows read at base + RVA through `read(address, out, size)` must equal the expected
// bytes with each relocated dword equal to base + its target RVA (the site's JE is inside `decision`, so an already
// patched site is refused). nullptr when both match, else the refusal; *which names the first refused window.
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
// What the decision leaves in the flags word for a seed word and a choice (the registry value when the choice is -1):
// vanilla follows the choice, patched always sets the bit; 0x1000 is set either way. The fixture's expectation.
inline std::uint32_t decide(std::uint32_t seed, std::int32_t choice, std::uint32_t registry, bool patched_site) {
    const std::uint32_t word = seed | 0x1000;
    const std::uint32_t effective = choice == -1 ? registry : std::uint32_t(choice);
    return (effective || patched_site) ? (word | run_in_background_bit) : (word & ~run_in_background_bit);
}

constexpr bool windows_hash() {
    for (const Window& w : windows)
        if (w.length > max_window || fnv1a(w.bytes, w.length) != w.fnv) return false;
    return true;
}
static_assert(windows_hash(), "every window is the measured one");
static_assert(decision_window[site_rva - 0x0776c2] == original[0] && decision_window[site_rva - 0x0776c2 + 1] == original[1],
              "the decision window carries the original JE");
static_assert(site_rva + 2 + original[1] == clear_bit_rva && site_rva + 2 == set_bit_rva, "je 0x0776f6 over the OR");
static_assert(decision_window[set_bit_rva - 0x0776c2] == 0x81 && decision_window[set_bit_rva - 0x0776c2 + 1] == 0x08 &&
                  le32(decision_window + (set_bit_rva - 0x0776c2) + 2) == run_in_background_bit,
              "or dword [eax],0x4000");
static_assert(decision_window[clear_bit_rva - 0x0776c2] == 0x81 && decision_window[clear_bit_rva - 0x0776c2 + 1] == 0x20 &&
                  le32(decision_window + (clear_bit_rva - 0x0776c2) + 2) == ~run_in_background_bit,
              "and dword [eax],0xffffbfff");
static_assert(decision_window[setup_call_rva - 0x0776c2] == 0xe8 &&
                  after_window_rva + le32(decision_window + (setup_call_rva - 0x0776c2) + 1) == setup_rva &&
                  0x0776c2 + 63 == after_window_rva,
              "call 0x100590 closes the window");
static_assert(decision_window[6] == 0x4d && decision_window[7] == choice_offset, "mov ecx,[ebp+0x60]");
static_assert((site_rva & 7u) + site_length <= 8u, "the two bytes lie in one aligned qword (atomic write)");
static_assert(0x0ec632 + 14 + le32(pump_gate_window + 10) == pump_nonblocking_rva &&
                  0x0ec632 + 31 + le32(pump_gate_window + 27) == pump_nonblocking_rva &&
                  0x0ec632 + 31 == pump_blocking_rva && le32(pump_gate_window + 21) == run_in_background_bit,
              "inactive and the bit clear: the blocking path; else the non-blocking one");
}
