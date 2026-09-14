#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

// Portable core of the LOD threshold scale (docs/architecture/lod-scale.md,
// docs/reverse-engineering/lod-selection.md option 1): the verified read site,
// the same-length replacement encoding and the mirror rule. No Windows
// dependency so the host tests compile it directly.
namespace x3m::lod_scale::core {
// 0047d440  8b 03                 MOV   EAX,[EBX]
// 0047d442  db 40 34              FILD  dword [EAX+0x34]
// 0047d445  8b 0d 34 6f 60 00     MOV   ECX,[0x00606f34]
// 0047d44b  d8 89 60 07 00 00     FMUL  dword [ECX+0x760]     <- replaced
// 0047d451  e8 7a e1 0a 00        CALL  0x0052b5d0            (ftol, unchanged)
constexpr std::uintptr_t window_va = 0x0047d440;
constexpr std::uintptr_t site_va = 0x0047d44b;
constexpr std::uintptr_t next_va = 0x0047d451;
constexpr std::uintptr_t config_pointer_va = 0x00606f34;
constexpr std::uint32_t config_scale_offset = 0x760;
constexpr unsigned window_length = 17, site_length = 6;
constexpr unsigned char expected_window[window_length] = {
    0x8b,0x03, 0xdb,0x40,0x34, 0x8b,0x0d,0x34,0x6f,0x60,0x00, 0xd8,0x89,0x60,0x07,0x00,0x00};
constexpr unsigned char expected_site[site_length] = {0xd8,0x89,0x60,0x07,0x00,0x00};
// Factor band (X3M_LOD_SCALE): 1 = vanilla ladder, 4 = the cap. T_i = (int)(LODrec[+0x34] * f)
// is integer-truncated and the record values are unread, so larger factors
// could collapse small thresholds to 0 (lod-selection.md, "Unknown").
constexpr double factor_min = 1.0, factor_max = 4.0;
// The engine writes one of {1.0, 1.15, 1.2, 1.3, 1.4} (shader-quality mapping).
constexpr float game_value_min = 1.0f, game_value_max = 1.4f;

// Locale-independent decimal parse of X3M_LOD_SCALE: `[+]digits[.digits]` or
// `.digits`, nothing else (no exponent, no whitespace, no locale separator).
inline bool parse_factor(const char* text, double* out) {
    if (!text || !*text) return false;
    const char* p = text;
    if (*p == '+') ++p;
    double value = 0; unsigned digits = 0;
    for (; *p >= '0' && *p <= '9'; ++p, ++digits) value = value * 10.0 + (*p - '0');
    if (*p == '.') {
        double scale = 0.1;
        for (++p; *p >= '0' && *p <= '9'; ++p, ++digits) { value += (*p - '0') * scale; scale *= 0.1; }
    }
    if (*p != '\0' || digits == 0) return false;
    *out = value; return true;
}
inline bool valid_factor(double f) { return std::isfinite(f) && f >= factor_min && f <= factor_max; }
inline bool valid_game_value(float v) { return std::isfinite(v) && v >= game_value_min && v <= game_value_max; }
inline float bits_to_float(std::uint32_t bits) { float v; std::memcpy(&v, &bits, 4); return v; }
inline std::uint32_t float_to_bits(float v) { std::uint32_t b; std::memcpy(&b, &v, 4); return b; }
// The mirror the patched FMUL reads: game_value / factor when the game value is
// in band (switch distance is proportional to 1/f), otherwise the game's own
// bits unchanged, so an out-of-band or unwritten value yields exactly the
// vanilla multiply. Returns whether the scale was applied.
inline bool mirror_bits(std::uint32_t game_bits, double factor, std::uint32_t* out) {
    const float v = bits_to_float(game_bits);
    if (!valid_factor(factor) || !valid_game_value(v)) { *out = game_bits; return false; }
    *out = float_to_bits(static_cast<float>(static_cast<double>(v) / factor));
    return true;
}
// FMUL m32fp with a 32-bit absolute address: opcode D8, ModRM /1 with mod=00
// r/m=101 (0x0d), disp32 little-endian. Six bytes, like the original.
inline void encode_replacement(std::uint32_t mirror_address, unsigned char out[site_length]) {
    out[0] = 0xd8; out[1] = 0x0d;
    out[2] = static_cast<unsigned char>(mirror_address);
    out[3] = static_cast<unsigned char>(mirror_address >> 8);
    out[4] = static_cast<unsigned char>(mirror_address >> 16);
    out[5] = static_cast<unsigned char>(mirror_address >> 24);
}
}
