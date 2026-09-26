#pragma once
#include <cstdint>
#include <cstring>

// Portable core of the lens-flare collector fix (docs/reverse-engineering/
// field-of-view.md sections 9 and 9.1): the verified window around the
// horizontal off-screen test of the lens collector's gate, the claimed span,
// the saturating stub and the X3M_SUN_FLARE_FIX parser. No Windows dependency
// so the host test and the site verifier compile it directly.
//
// The gate (0x0047e315..0x0047e402, inside the render visit 0x0047d9c0, run for
// TSuns lens sources) computes the horizontal bound as FixMul(W, FixMul(tan,
// z/2)) and keeps only the low 32 bits of the shifted 64-bit product, so for
// W*tan(F/2)*z/2 >= 2^31 (32:9 displays at wide F and far suns) the bound turns
// negative and the sun is declared off-screen:
//
// 0047e365  8b 44 24 1c           MOV  EAX,[ESP+0x1c]      ; tan(F/2) 16.16    <- window
// 0047e369  8b 54 24 10           MOV  EDX,[ESP+0x10]      ; z/2
// 0047e36d  f7 ea                 IMUL EDX
// 0047e36f  05 00 80 00 00        ADD  EAX,0x8000
// 0047e374  83 d2 00              ADC  EDX,0
// 0047e377  0f ac d0 10           SHRD EAX,EDX,16
// 0047e37b  89 44 24 10           MOV  [ESP+0x10],EAX      ; t1
// 0047e37f  8b 44 24 18           MOV  EAX,[ESP+0x18]      ; W 16.16
// 0047e383  8b 54 24 10           MOV  EDX,[ESP+0x10]
// 0047e387  f7 ea                 IMUL EDX                 ; EDX:EAX = W*t1 (exact)
// 0047e389  05 00 80 00 00        ADD  EAX,0x8000
// 0047e38e  83 d2 00              ADC  EDX,0
// 0047e391  0f ac d0 10           SHRD EAX,EDX,16          <- site (claimed with the CMP: 6 bytes)
// 0047e395  3b c8                 CMP  ECX,EAX             ; ECX = |x|/2
// 0047e397  0f 8d 19 02 00 00     JGE  0x0047e5b6          ; off-screen
// 0047e39d                        (the vertical test)       <- window end
//
// The claim displaces SHRD and CMP into the engine_patch tail (whole
// instructions, no relative branch; the first five bytes lie in the aligned
// qword 0x0047e390, one lock cmpxchg8b; byte 0x0047e396 is left and never
// executed) and the stub runs first: when (EDX:EAX)>>16 >= 2^31 (EDX >= 0x8000
// signed) it sets EDX:EAX = 0x00007fff:ffffffff so the displaced SHRD yields
// 0x7fffffff; otherwise it changes nothing. Since |x|/2 <= 2^30 an overflowing
// bound always means "inside horizontally", so saturation gives the exact
// answer, and no non-overflowing input changes. Limit: the first FixMul
// (t1 = FixMul(tan, z/2), 0x0047e36d..0x0047e37b) is not covered and still wraps
// when tan(F/2) >= 2 (F >= 126.9 deg); the FOV options stay below that (--fov N,
// N 50..130 in game units, remaps to F <= ~116 deg; the menu's 100 is F = 100 deg
// under --fov game), so only script cameras can reach it. The stub touches only EAX/EDX
// (the values it corrects) and EFLAGS, which are dead at the site (SHRD
// rewrites them, CMP rewrites them again before the JGE reads them); it pushes
// and calls nothing, so ESP, the ESP-relative locals, EBX/ESI/EDI/EBP, ECX,
// the FPU/SSE state and LastError are untouched. The original CMP runs in the
// tail and the original JGE reads its flags; JMP [slot] preserves them. No
// byte-pattern branch and no absolute reference lands in the window
// (verify_sun_flare_site.py).
namespace x3m::sun_flare_fix::sites {
constexpr std::uintptr_t gate_va = 0x0047e315, gate_end_va = 0x0047e402; // the collector's gate; 0x0047e402 = on-screen
                                                                         // path
constexpr std::uintptr_t window_va = 0x0047e365, site_va = 0x0047e391, jge_va = 0x0047e397, y_test_va = 0x0047e39d;
constexpr std::uintptr_t off_screen_va = 0x0047e5b6; // the JGE's target: record+0x30 stays 0
constexpr unsigned window_length = 56, site_offset = 0x2c, site_length = 6;
constexpr unsigned char expected_window[window_length] = {
    0x8b, 0x44, 0x24, 0x1c, 0x8b, 0x54, 0x24, 0x10, 0xf7, 0xea, 0x05, 0x00, 0x80, 0x00, 0x00, 0x83, 0xd2, 0x00, 0x0f,
    0xac, 0xd0, 0x10, 0x89, 0x44, 0x24, 0x10, 0x8b, 0x44, 0x24, 0x18, 0x8b, 0x54, 0x24, 0x10, 0xf7, 0xea, 0x05, 0x00,
    0x80, 0x00, 0x00, 0x83, 0xd2, 0x00, 0x0f, 0xac, 0xd0, 0x10, 0x3b, 0xc8, 0x0f, 0x8d, 0x19, 0x02, 0x00, 0x00};
constexpr unsigned char expected_site[site_length] = {0x0f, 0xac, 0xd0,
                                                      0x10, 0x3b, 0xc8}; // SHRD EAX,EDX,16; CMP ECX,EAX

// The stub pushed in front of the chain: 20 bytes, then the abs32 of its
// continuation slot (the previous chain head, i.e. the tail).
//   +00 81 fa 00 80 00 00   CMP EDX,0x8000       ; (EDX:EAX)>>16 >= 2^31 ?
//   +06 7c 0a               JL  +0x0a (-> +12)   ; no overflow (or a negative product): unchanged
//   +08 ba ff 7f 00 00      MOV EDX,0x7fff
//   +0d b8 ff ff ff ff      MOV EAX,0xffffffff   ; the displaced SHRD then yields 0x7fffffff
//   +12 ff 25 <slot>        JMP [slot]           ; tail: SHRD; CMP; JMP 0x0047e397
constexpr unsigned stub_code_length = 20, stub_length = 24;
constexpr unsigned char stub_code[stub_code_length] = {0x81, 0xfa, 0x00, 0x80, 0x00, 0x00, 0x7c, 0x0a, 0xba, 0xff,
                                                       0x7f, 0x00, 0x00, 0xb8, 0xff, 0xff, 0xff, 0xff, 0xff, 0x25};
inline void encode_stub(std::uint32_t slot, unsigned char out[stub_length]) {
    std::memcpy(out, stub_code, stub_code_length);
    for (unsigned i = 0; i < 4; ++i) out[stub_code_length + i] = static_cast<unsigned char>(slot >> (8 * i));
}
// What the stub followed by the displaced SHRD leaves in EAX for the product
// EDX:EAX after the ADD/ADC rounding (the host test's model; the Wine fixture
// executes the bytes).
inline std::int32_t fixed_bound(std::int64_t rounded_product) {
    std::uint32_t hi = static_cast<std::uint32_t>(static_cast<std::uint64_t>(rounded_product) >> 32),
                  lo = static_cast<std::uint32_t>(rounded_product);
    if (static_cast<std::int32_t>(hi) >= 0x8000) {
        hi = 0x7fff;
        lo = 0xffffffffu;
    }
    return static_cast<std::int32_t>((lo >> 16) | (hi << 16));
}
inline std::int32_t vanilla_bound(std::int64_t rounded_product) {
    const std::uint32_t hi = static_cast<std::uint32_t>(static_cast<std::uint64_t>(rounded_product) >> 32),
                        lo = static_cast<std::uint32_t>(rounded_product);
    return static_cast<std::int32_t>((lo >> 16) | (hi << 16));
}

// X3M_SUN_FLARE_FIX: off = the engine's bytes (the DLL default when the
// variable is unset), on = the stub. The launcher sends on by default.
enum class Mode : unsigned char { off = 0, on = 1 };
constexpr Mode default_mode = Mode::off;
constexpr unsigned setting_capacity = 32; // 1..31 characters; 32 or more is too_long
inline const char* mode_name(Mode m) {
    return m == Mode::on ? "on" : "off";
}
// Exactly "on" or "off" (lower case, nothing else).
template <class Char> inline bool parse_mode(const Char* text, Mode* out) {
    if (!text) return false;
    auto equals = [text](const char* word) {
        unsigned i = 0;
        for (; word[i]; ++i)
            if (text[i] != Char(word[i])) return false;
        return text[i] == Char(0);
    };
    if (equals("on")) {
        *out = Mode::on;
        return true;
    }
    if (equals("off")) {
        *out = Mode::off;
        return true;
    }
    return false;
}
// The install decision on the bytes read at the window: nullptr when the
// window is exactly the engine's, else the refusal reason (a patched or
// otherwise changed window is refused).
inline const char* plan(const unsigned char current[window_length]) {
    return std::memcmp(current, expected_window, window_length) ? "bytes_mismatch" : nullptr;
}
// The engine_patch claim of the span (whole instructions, no relative branch, ret_pop 0).
struct ClaimSpec {
    const char* name;
    std::uintptr_t address;
    unsigned char expected[site_length];
    unsigned length, ret_pop, rel32_offset;
};
constexpr ClaimSpec claim_spec = {"lens_collector_x_bound", 0x0047e391, {0x0f, 0xac, 0xd0, 0x10, 0x3b, 0xc8}, 6, 0, 0};

constexpr bool window_holds_site() {
    for (unsigned i = 0; i < site_length; ++i)
        if (expected_window[site_offset + i] != expected_site[i] || claim_spec.expected[i] != expected_site[i])
            return false;
    return true;
}
static_assert(window_va + site_offset == site_va && site_va + site_length == jge_va && jge_va + 6 == y_test_va &&
                  window_va + window_length == y_test_va,
              "offsets");
static_assert(window_holds_site() && claim_spec.address == site_va && claim_spec.length == site_length,
              "the window carries the claimed bytes");
static_assert((site_va & ~std::uintptr_t(7)) == ((site_va + 4) & ~std::uintptr_t(7)),
              "the five patch bytes lie in one aligned 8-byte word (atomic write)");
static_assert(jge_va + 6 + 0x219 == off_screen_va && expected_window[window_length - 4] == 0x19 &&
                  expected_window[window_length - 3] == 0x02,
              "JGE rel32 0x219 reaches the off-screen path");
static_assert(gate_va < window_va && y_test_va < gate_end_va && gate_end_va < off_screen_va,
              "inside the collector's gate");
static_assert(stub_code[6] == 0x7c && 8 + stub_code[7] == stub_code_length - 2, "JL skips to the JMP [slot]");
}
