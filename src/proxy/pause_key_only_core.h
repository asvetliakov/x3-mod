#pragma once
#include <cstdint>
#include <cstring>

// Portable core of the pause-key-only patch
// (docs/reverse-engineering/pause-dialog-input.md, section 4.1): the verified
// byte window of the flight-pause wait loop in 0x00404280, the 12-byte site,
// the replacement encoder and the X3M_PAUSE_KEY parser. No Windows dependency
// so the host tests and the site verifier compile it directly.
//
// While bit 0 of [*0x0057fc60 + 0x4a0] is set, 0x00404280 spins in the loop
// below until a key read differs from the previous read (low 12 bits) or a
// DirectInput mouse button appears; the exit at 0x004043d8 is the only place
// that clears the bit. The site's four instructions become
//   cmp si,KEY ; jne 0x004043b1 (mouse test) ; cmp si,bx ; jne 0x004043d8 (exit)
// so only KEY (DIK_PAUSE = 0x1b5 by default), newly pressed, leaves the pause
// by key; every other key is read, dropped and falls into the unchanged mouse
// test. Only si/bx are read and only EFLAGS written (consumed by the two
// jne); ECX, which the original wrote, is dead on both successors. The span
// is entered only by fall-through from 0x004043a3; no branch or dword in the
// image points inside it (verify_pause_sites.py).
//
// 004043a0  66 85 f6              TEST SI,SI                     <- window
// 004043a3  74 0c                 JE   0x004043b1
// 004043a5  8b ce                 MOV  ECX,ESI                   <- site (12 bytes, four whole instructions)
// 004043a7  33 cb                 XOR  ECX,EBX
// 004043a9  f7 c1 ff 0f 00 00     TEST ECX,0xfff
// 004043af  75 27                 JNE  0x004043d8                ; exit: key differs from the previous read
// 004043b1  8b d5 0b d7 3b d5     MOV EDX,EBP; OR EDX,EDI; CMP EDX,EBP
// 004043b7  75 1f                 JNE  0x004043d8                ; exit: a mouse button appeared
// 004043b9  e8 f2 f0 0c 00        CALL 0x004d34b0                ; message pump + DirectInput poll
// 004043be  a1 3c 6f 60 00 8b ef 8b b8 54 04 00 00                ; EBP = EDI; EDI = [ctx+0x454]
// 004043cb  0f b7 de              MOVZX EBX,SI                   ; previous key
// 004043ce  e8 8d f7 0c 00        CALL 0x004d3b60                ; next key
// 004043d3  0f b7 f0 eb c8        MOVZX ESI,AX; JMP 0x004043a0
// 004043d8  a1 60 fc 57 00        MOV EAX,[0x0057fc60]           ; exit
// 004043dd  8b 88 a0 04 00 00 83 e1 fe 83 c9 02 89 88 a0 04 00 00 ; [obj+0x4a0] = (x & ~1) | 2
namespace x3m::pause_key_only::core {
constexpr std::uintptr_t window_va = 0x004043a0, site_va = 0x004043a5;
constexpr std::uintptr_t mouse_test_va = 0x004043b1, exit_va = 0x004043d8;
constexpr unsigned site_offset = 5, site_length = 12, window_length = 79;
constexpr unsigned char window[window_length] = {
    0x66,0x85,0xf6, 0x74,0x0c,
    0x8b,0xce, 0x33,0xcb, 0xf7,0xc1,0xff,0x0f,0x00,0x00, 0x75,0x27,
    0x8b,0xd5, 0x0b,0xd7, 0x3b,0xd5, 0x75,0x1f,
    0xe8,0xf2,0xf0,0x0c,0x00,
    0xa1,0x3c,0x6f,0x60,0x00, 0x8b,0xef, 0x8b,0xb8,0x54,0x04,0x00,0x00,
    0x0f,0xb7,0xde,
    0xe8,0x8d,0xf7,0x0c,0x00,
    0x0f,0xb7,0xf0, 0xeb,0xc8,
    0xa1,0x60,0xfc,0x57,0x00,
    0x8b,0x88,0xa0,0x04,0x00,0x00, 0x83,0xe1,0xfe, 0x83,0xc9,0x02, 0x89,0x88,0xa0,0x04,0x00,0x00};
constexpr unsigned char original[site_length] = {0x8b,0xce, 0x33,0xcb, 0xf7,0xc1,0xff,0x0f,0x00,0x00, 0x75,0x27};
// Engine key codes: the reader's 12-bit code, | 0x1000 while a Shift key is
// held (0x004d3b60). DIK_PAUSE maps to 0x1b5 (0x004d6b30). The 12-bit part
// must not be 0: 0 is "no key" (the loop never reaches the site with it) and
// 0x1000 would be Shift alone, which the reader never produces as a key.
constexpr std::uint32_t default_key = 0x1b5, max_key = 0x1fff;
inline bool key_valid(std::uint32_t key) { return key >= 1 && key <= max_key && (key & 0xfff) != 0; }
// The replacement: 66 81 fe KK KK (cmp si,imm16) 75 05 (jne +5 -> 0x004043b1)
// 66 39 de (cmp si,bx) 75 27 (jne +0x27 -> 0x004043d8; the original last
// instruction, unchanged). Both rel8 are relative to the site, so the bytes do
// not depend on where the window sits. False for an invalid key.
inline bool encode_site(std::uint32_t key, unsigned char out[site_length]) {
    if (!key_valid(key)) return false;
    const unsigned char bytes[site_length] = {0x66,0x81,0xfe, static_cast<unsigned char>(key & 0xff), static_cast<unsigned char>((key >> 8) & 0xff),
                                              0x75,0x05, 0x66,0x39,0xde, 0x75,0x27};
    std::memcpy(out, bytes, site_length);
    return true;
}
// X3M_PAUSE_KEY: "0x" hex or decimal digits, nothing else, in [1, max_key] with a non-zero 12-bit part.
template<class Char>
inline bool parse_key(const Char* text, std::uint32_t* key) {
    if (!text || !*text) return false;
    unsigned base = 10;
    if (text[0] == Char('0') && (text[1] == Char('x') || text[1] == Char('X'))) { base = 16; text += 2; if (!*text) return false; }
    std::uint32_t value = 0;
    for (; *text; ++text) {
        const Char c = *text;
        unsigned digit;
        if (c >= Char('0') && c <= Char('9')) digit = unsigned(c - Char('0'));
        else if (base == 16 && c >= Char('a') && c <= Char('f')) digit = unsigned(c - Char('a')) + 10;
        else if (base == 16 && c >= Char('A') && c <= Char('F')) digit = unsigned(c - Char('A')) + 10;
        else return false;
        value = value * base + digit;
        if (value > max_key) return false;
    }
    if (!key_valid(value)) return false;
    *key = value;
    return true;
}
// The install decision on the bytes read at the window: nullptr and the
// replacement in `out` when the window is exactly the engine's (an already
// patched or otherwise changed window is refused), else the refusal reason.
inline const char* plan(const unsigned char current[window_length], std::uint32_t key, unsigned char out[site_length]) {
    if (!key_valid(key)) return "bad_key";
    if (std::memcmp(current, window, window_length)) return "bytes_mismatch";
    return encode_site(key, out) ? nullptr : "bad_key";
}
constexpr bool window_holds_original() {
    for (unsigned i = 0; i < site_length; ++i) if (window[site_offset + i] != original[i]) return false;
    return true;
}
static_assert(window_va + site_offset == site_va, "site offset");
static_assert(window_holds_original(), "the window carries the original site bytes");
static_assert(site_va + site_length == mouse_test_va, "the mouse test follows the site");
}
