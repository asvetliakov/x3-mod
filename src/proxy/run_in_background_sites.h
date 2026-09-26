#pragma once
#include <cstdint>
#include <cstring>

// Portable core of the run-in-background setting (docs/reverse-engineering/run-in-background.md): the
// verified 60-byte window at the end of the game's command-line handling in the init routine 0x00402780, the
// call it redirects, the flag it sets and the X3M_RUN_IN_BACKGROUND parser. No Windows dependency, so the host
// tests and the site verifier compile it directly.
//
// `-runinbg` / `/runinbg` store 1 and `-noruninbg` / `/noruninbg` store 0 into the init routine's local
// [esp+0x14] (0x00402d16 / 0x00402d09; -1 = neither, 0x004027ba). The window below is its only reader: it
// sets or clears bit 0x4000 (RunInBackground) of the input flags word [*0x00606f3c], reading the registry
// value RunInBackground (0x004b8510, HKCU, read only) when neither argument was given. The message pump
// 0x004d34b0 blocks in GetMessageA while the window is inactive ([0x00608adc] == 0) unless that bit is set.
//
// 00403392  8b 0d 3c 6f 60 00        MOV  ECX,[00606f3c]           ; input block       <- window
// 00403398  8b 44 24 14              MOV  EAX,[ESP+0x14]           ; the argument's local
// 0040339c  81 09 00 10 00 00        OR   dword [ECX],0x1000
// 004033a2  83 f8 ff                 CMP  EAX,-1
// 004033a5  75 10                    JNE  0x004033b7
// 004033a7  be a8 56 55 00           MOV  ESI,0x005556a8           ; "RunInBackground"
// 004033ac  e8 5f 51 0b 00           CALL 0x004b8510               ; the registry read
// 004033b1  8b 0d 3c 6f 60 00        MOV  ECX,[00606f3c]
// 004033b7  85 c0                    TEST EAX,EAX
// 004033b9  74 08                    JE   0x004033c3
// 004033bb  81 09 00 40 00 00        OR   dword [ECX],0x4000       ; RunInBackground on
// 004033c1  eb 06                    JMP  0x004033c9
// 004033c3  81 21 ff bf ff ff        AND  dword [ECX],0xffffbfff   ; RunInBackground off
// 004033c9  e8 b2 f1 0c 00           CALL 0x004d2580               <- site (the rel32 is redirected)
// 004033ce                                                         ; (after the window)
//
// The redirected call enters a thunk that saves every register and EFLAGS, sets the bit once and jumps to
// 0x004d2580 with the caller's return address still on the stack, so the callee runs exactly as before. The
// callee takes no argument (its `push ecx` reserves a slot), has this call as its only caller, and the site
// executes once per process. The rel32 span 0x004033ca..0x004033cd lies inside the aligned 8-byte word
// 0x004033c8..0x004033cf, so engine_patch::claim_call writes the five bytes with one lock cmpxchg8b.
namespace x3m::run_in_background::sites {
constexpr std::uintptr_t window_va = 0x00403392, site_va = 0x004033c9, target_va = 0x004d2580, return_va = 0x004033ce;
constexpr std::uintptr_t input_block_slot_va = 0x00606f3c; // the input block pointer; the flags word is at +0
constexpr std::uint32_t run_in_background_bit = 0x4000;
// The argument parser (documentation and the host verifier; the DLL does not touch it).
constexpr std::uintptr_t local_init_va = 0x004027ba, noruninbg_store_va = 0x00402d09, runinbg_store_va = 0x00402d16;
constexpr std::uintptr_t local_read_va = 0x00403398, registry_read_va = 0x004b8510, pump_va = 0x004d34b0;
constexpr unsigned window_length = 60, site_offset = 0x37, call_length = 5;
constexpr unsigned char expected_window[window_length] = {
    0x8b, 0x0d, 0x3c, 0x6f, 0x60, 0x00, 0x8b, 0x44, 0x24, 0x14, 0x81, 0x09, 0x00, 0x10, 0x00,
    0x00, 0x83, 0xf8, 0xff, 0x75, 0x10, 0xbe, 0xa8, 0x56, 0x55, 0x00, 0xe8, 0x5f, 0x51, 0x0b,
    0x00, 0x8b, 0x0d, 0x3c, 0x6f, 0x60, 0x00, 0x85, 0xc0, 0x74, 0x08, 0x81, 0x09, 0x00, 0x40,
    0x00, 0x00, 0xeb, 0x06, 0x81, 0x21, 0xff, 0xbf, 0xff, 0xff, 0xe8, 0xb2, 0xf1, 0x0c, 0x00};
constexpr unsigned set_bit_offset = 0x29, clear_bit_offset = 0x31; // the OR / AND of the flag

// X3M_RUN_IN_BACKGROUND: exactly "1" = on, "0" = off; unset or empty = off (the schema default 1 reaches the
// DLL through the settings resolver; X3M_CONFIG=bare leaves it unset); anything else is refused.
enum class Setting : unsigned char { off = 0, on = 1, invalid = 2 };
template <class Char> inline Setting parse_setting(const Char* text, unsigned length) {
    if (!text || length == 0) return Setting::off;
    if (length == 1 && text[0] == Char('1')) return Setting::on;
    if (length == 1 && text[0] == Char('0')) return Setting::off;
    return Setting::invalid;
}
// The install decision on the bytes read at the window: nullptr when the window is exactly the engine's, else
// the refusal reason (an already redirected or otherwise changed window is refused).
inline const char* plan(const unsigned char current[window_length]) {
    return std::memcmp(current, expected_window, window_length) ? "bytes_mismatch" : nullptr;
}
// What the thunk does to the flags word the game has just written: set the bit when it is clear (the state
// `-runinbg` produces), nothing when the argument or the registry value already set it.
struct Outcome {
    const char* status; // patched | already
    std::uint32_t before, after;
    bool write;
};
inline Outcome apply(std::uint32_t flags) {
    if (flags & run_in_background_bit) return Outcome{"already", flags, flags, false};
    return Outcome{"patched", flags, flags | run_in_background_bit, true};
}

constexpr std::uint32_t window_rel32() {
    return std::uint32_t(expected_window[site_offset + 1]) | std::uint32_t(expected_window[site_offset + 2]) << 8 |
           std::uint32_t(expected_window[site_offset + 3]) << 16 |
           std::uint32_t(expected_window[site_offset + 4]) << 24;
}
constexpr bool window_names_slot(unsigned at) {
    return expected_window[at] == (input_block_slot_va & 0xff) &&
           expected_window[at + 1] == ((input_block_slot_va >> 8) & 0xff) &&
           expected_window[at + 2] == ((input_block_slot_va >> 16) & 0xff) &&
           expected_window[at + 3] == ((input_block_slot_va >> 24) & 0xff);
}
static_assert(window_va + site_offset == site_va && site_va + call_length == return_va &&
                  site_offset + call_length == window_length,
              "the call closes the window");
static_assert(expected_window[site_offset] == 0xe8 && return_va + window_rel32() == target_va, "CALL 0x004d2580");
static_assert(window_names_slot(2) && window_names_slot(0x21), "both loads read [0x00606f3c]");
static_assert(expected_window[set_bit_offset] == 0x81 && expected_window[set_bit_offset + 1] == 0x09 &&
                  expected_window[set_bit_offset + 3] == 0x40 && expected_window[clear_bit_offset] == 0x81 &&
                  expected_window[clear_bit_offset + 1] == 0x21 && expected_window[clear_bit_offset + 3] == 0xbf,
              "OR [ecx],0x4000 / AND [ecx],0xffffbfff");
static_assert(window_va + 6 == local_read_va, "the local's only reader opens the window's second instruction");
static_assert(((site_va + 1) & 7u) + 4 <= 8u && (site_va & ~std::uintptr_t(7)) + 8 >= return_va,
              "the call's five bytes lie inside one aligned 8-byte word (atomic write)");
}
