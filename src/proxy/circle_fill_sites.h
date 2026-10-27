#pragma once
#include <cstdint>

// Portable core of the oversized-disc fix (docs/reverse-engineering/sector-map-scanner-range.md sections 2, 3 and 5;
// docs/architecture/text-density.md, "Oversized disc"): the claimed entry of the engine's filled-ellipse rasteriser
// 0x004ef620, the code regions the port mirrors or calls (compared by FNV-1a 64 before the claim), the B quotient, the
// leaf selection and the rasteriser itself with 64-bit decision variables. No Windows dependency: the host test and the
// site verifier compile it directly.
//
// 0x004ef620 (only caller 0x0048c076, from B3D_TexCircleFill's 0x0048bee0 after the text-density `x N` scale block):
// cdecl (cx, cy, r, colour, aspect 16.16), caller cleans 0x14 bytes, return value unused, EDI = the surface object
// (register argument). Its midpoint decision variables and products are 32-bit and grow as about 4 r^3: from r = 814
// texels a value wraps, from 826 the spans differ, from 1291 the disc collapses. The claim displaces
// `sub esp,0x4c; mov eax,[esp+0x58]` (7 bytes, two whole instructions, continuation 0x004ef627 `push ebx`); EAX, ECX,
// EDX and EFLAGS are dead at entry. The thunk reads the radius as the routine does (movsx of the word at [esp+0xc]):
// r <= 813 (every decision value fits an int32; negative included) jumps to the tail, which runs the displaced pair
// and the stock routine. Larger radii run x3m_circle_fill_wide, which repeats the routine with 64-bit d, A, B and
// products (identical to the engine up to r = 825, the exact midpoint disc above): the 16-bit coordinates, the per-span
// clip and the unsigned 16-bit row test of the engine, the same surface lock (0x004dcd30 when +0 is null, +0x10 |= 2),
// the same leaf (0x004ee6e0 with ECX = surface, EAX = 0, then the class table of 0x004ef6c5) and the same leaf calls
// in the same order; then the thunk returns (`ret`). The C side declines (the stock routine runs from its first
// instruction) when the aspect gives no B in [0, max_b] or the class has no leaf (the stock path prints its
// "Unknown Blittype" line).
namespace x3m::circle_fill::sites {
// ---- the site ----
constexpr std::uintptr_t site_va = 0x004ef620, continue_va = 0x004ef627, caller_va = 0x0048c076, caller_return_va = 0x0048c07b;
constexpr unsigned site_length = 7, window_length = 18;
constexpr unsigned char expected_window[window_length] = {
    0x83, 0xec, 0x4c,             // sub esp,0x4c            <- the claim (7 bytes)
    0x8b, 0x44, 0x24, 0x58,       // mov eax,[esp+0x58]      (r)
    0x53, 0x55, 0x56,             // push ebx; push ebp; push esi   <- continuation 0x004ef627
    0x0f, 0xbf, 0xf0,             // movsx esi,ax            (r as int16)
    0x8b, 0xde,                   // mov ebx,esi
    0x0f, 0xb7, 0xc8};            // movzx ecx,ax
constexpr std::int32_t stock_radius_max = 813; // r <= 813: max |d| 2,109,152,079 < 2^31 (measured for every r)
// ---- the engine pieces the port calls or mirrors; each compared as a whole by FNV-1a 64 before the claim ----
constexpr std::uintptr_t lock_va = 0x004dcd30;  // cdecl (IDirect3DSurface9*, out, 0) -> bits; writes out+0x18 (pitch)
constexpr std::uintptr_t class_va = 0x004ee6e0; // EAX, ECX = objects (0 = the other) -> AX pixel class; keeps EBX/ESI/EDI/EBP
constexpr std::uintptr_t leaf_class0_va = 0x004b32f0, leaf_class5_va = 0x004b3680, leaf_class10_va = 0x004b3580,
                         leaf_class15_va = 0x004b3340, leaf_class24_va = 0x004b3620; // cdecl (surface, x0, y, x1, colour)
constexpr unsigned lock_out_bytes = 0x1c;
struct Region {
    std::uintptr_t va;
    unsigned length;
    std::uint64_t fnv1a64;
};
constexpr unsigned region_count = 8;
constexpr Region regions[region_count] = {
    {0x004ef620, 0x44d, 0xd8a312f431d326e5ull}, // the rasteriser 0x004ef620..0x004efa3b with its jump and class tables
    {0x004ee6e0, 0x16b, 0xd587068fc4e3f803ull}, // the pixel-class function (pure: no call, no store)
    {0x004dcd30, 0xe4, 0xcee30de245e267afull},  // the surface lock
    {0x004b32f0, 0x4e, 0x8dca471db145a525ull},  // leaf, class 0
    {0x004b3340, 0x231, 0xecf78d52cc9ff86cull}, // leaf, class 15
    {0x004b3580, 0x9a, 0x196c6aaf3f319ca7ull},  // leaf, class 10 (24-bit)
    {0x004b3620, 0x5b, 0xb6eba8f5d7f57f30ull},  // leaf, class 24 (32-bit, stores the converted colour)
    {0x004b3680, 0x74, 0x557a17b0503fe354ull}}; // leaf, class 5 (16-bit)
constexpr std::uint64_t fnv1a64(const unsigned char* p, unsigned n, std::uint64_t h = 0xcbf29ce484222325ull) {
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}
// ---- the surface object (0x004f3950's 100-byte texture object) ----
constexpr unsigned object_bits = 0, object_width = 4, object_height = 6, object_flags = 0x10, object_backing = 0x30;
constexpr std::uint32_t flag_no_draw = 0x200000, flag_locked = 2;
// ---- the thunk frame (pushfd then pushad, so from ESP: EDI first, EAX at 7, then EFLAGS, the return address, the
// five arguments) ----
constexpr unsigned frame_edi = 0, frame_eax = 7, frame_return = 9, frame_args = 10;
constexpr unsigned arg_cx = 0, arg_cy = 1, arg_r = 2, arg_colour = 3, arg_aspect = 4;

inline std::int32_t s16(std::int64_t v) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(static_cast<std::uint64_t>(v) & 0xffffu));
}
inline std::int32_t u16(std::int64_t v) {
    return static_cast<std::int32_t>(static_cast<std::uint64_t>(v) & 0xffffu);
}
// The class table of 0x004ef6bc..0x004ef702 (class index bytes at 0x004efa54, jump table 0x004efa3c): 0 when the class
// has no leaf (the stock routine prints "FilledCircle: Unknown Blittype").
inline std::uintptr_t leaf_for_class(std::int32_t pixel_class) {
    switch (pixel_class) {
    case 0: return leaf_class0_va;
    case 5: return leaf_class5_va;
    case 10: return leaf_class10_va;
    case 15: return leaf_class15_va;
    case 24: return leaf_class24_va;
    default: return 0;
    }
}
// B = A / (q * 2^-16) with q the low 32 bits of (aspect^2 + 0x8000) >> 16 (`imul; add; adc; shrd`, 0x004ef703). The
// engine divides on the x87 and truncates (0x0052b5d0); here the exact integer quotient (B = A for the scripts'
// aspect 65536). false (the stock routine runs) when q <= 0 or B > max_b: with B <= 32767^2 (B = A for every int16
// radius at aspect 65536) the ellipse's x semi-axis stays inside int16 and every decision term below stays under 2^62.
constexpr std::int64_t max_b = std::int64_t(32767) * 32767;
inline bool semi_axis(std::int64_t A, std::int32_t aspect, std::int64_t* B) {
    const std::int64_t square = std::int64_t(aspect) * aspect;
    const std::int32_t q = static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(square + 0x8000) >> 16));
    if (q <= 0 || A < 0) return false;
    const std::int64_t b = (A << 16) / q;
    if (b > max_b) return false;
    *B = b;
    return true;
}
// The rasteriser of 0x004ef620 with 64-bit d, A, B and products (the emulation's unbounded variant). `leaf(x0, y, x1)`
// receives each span as the engine's leaf would (the low 16 bits, sign-extended; the leaves read words only), in the
// engine's order: the row cy + y, then cy - y. `width`/`height` are the surface's +4/+6. The engine sets its clip flag
// from cx +- r and cy +- r only; the x semi-axis is sqrt(B), so with B > A (aspect < 65536) an unclipped span could run
// past the row: the port clips then too (no pixel changes at the scripts' aspect 65536, where B = A).
template <class Leaf>
inline void fill(std::int32_t cx, std::int32_t cy, std::int32_t r, std::int64_t B, std::int32_t width, std::int32_t height, Leaf&& leaf) {
    const std::int64_t rr = s16(r), A = rr * rr;
    const std::int32_t W = s16(width), H = s16(height);
    const bool clip = s16(cx) + rr >= W || s16(cx) - rr < 0 || s16(cy) + rr >= H || s16(cy) - rr < 0 || B > A;
    auto rows = [&](std::int32_t left, std::int32_t right, std::int32_t y) {
        if (clip) { // per span: left to 0, right to width - 1 (signed 16), empty skipped, each row unsigned 16 vs height
            const std::int32_t x0 = s16(left) < 0 ? 0 : s16(left);
            const std::int32_t x1 = s16(right) >= W ? s16(W - 1) : s16(right);
            if (x0 > x1) return;
            if (u16(cy + y) < u16(H)) leaf(x0, s16(cy + y), x1);
            if (u16(cy - y) < u16(H)) leaf(x0, s16(cy - y), x1);
        } else {
            leaf(s16(left), s16(cy + y), s16(right));
            leaf(s16(left), s16(cy - y), s16(right));
        }
    };
    std::int32_t Y = u16(r), X = 0; // the engine's [esp+0x10] / [esp+0x14] (read through s16)
    // Region 1 (the caps): x from 0 up while y B > x A.
    std::int64_t d = (1 - B) * 2 * A + (rr - 1) * rr * 2 * B + B;
    if (rr * B > 0) {
        std::int32_t left = u16(cx), right = u16(cx);
        std::int64_t xs = 0;
        for (;;) {
            if (d >= 0) {
                rows(left, right, Y);
                --Y;
                d -= std::int64_t(s16(Y)) * 4 * B;
            }
            d += (2 * xs + 3) * 2 * A;
            --left;
            ++X;
            xs = s16(X);
            ++right;
            if (!(std::int64_t(s16(Y)) * B > xs * A)) break;
        }
    }
    // Region 2 (the sides): y down to 0.
    const std::int64_t Xs = s16(X), Ys = s16(Y);
    d = ((Ys - 2) * Ys + 1) * 2 * B + (Xs + 1) * Xs * 2 * A + (1 - 2 * B) * A;
    if (Ys + 1 == 0) return;
    std::int32_t left = u16(cx - X), right = u16(cx + X);
    for (;;) {
        rows(left, right, Y);
        if (d <= 0) {
            ++X;
            d += std::int64_t(s16(X)) * 4 * A;
            --left;
            ++right;
        }
        --Y;
        d += (3 - 2 * std::int64_t(s16(Y))) * 2 * B;
        if (s16(Y) + 1 == 0) break;
    }
}
// X3M_CIRCLE_FILL_FIX (ini circle_fill_fix): on = the claim (the DLL default when unset; the launcher sends on),
// off = the engine's bytes.
enum class Mode : unsigned char { off = 0, on = 1 };
constexpr Mode default_mode = Mode::on;
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
// The engine_patch claim (two whole instructions, no relative branch, plain `ret`).
struct ClaimSpec {
    const char* name;
    std::uintptr_t address;
    unsigned char expected[site_length];
    unsigned length, ret_pop, rel32_offset;
};
constexpr ClaimSpec claim_spec = {"circle_fill_wide", 0x004ef620, {0x83, 0xec, 0x4c, 0x8b, 0x44, 0x24, 0x58}, 7, 0, 0};

constexpr bool claim_matches_window() {
    for (unsigned i = 0; i < site_length; ++i)
        if (claim_spec.expected[i] != expected_window[i]) return false;
    return true;
}
static_assert(claim_matches_window() && claim_spec.address == site_va && claim_spec.length == site_length &&
                  site_va + site_length == continue_va && regions[0].va == site_va,
              "the claim is the window's first seven bytes; the continuation follows them");
static_assert((site_va & ~std::uintptr_t(7)) == ((site_va + 4) & ~std::uintptr_t(7)),
              "the five patch bytes lie in one aligned 8-byte word (atomic write)");
static_assert(caller_va + 5 == caller_return_va, "the one caller is a five-byte call");
}
