#pragma once
#include <cstdint>
#include <cstring>

// FL edition of the oversized-disc fix (X3M_CIRCLE_FILL_FIX; the AP twin is src/proxy/circle_fill_sites.h, whose
// parser and mode names this edition reuses unchanged). Facts: docs/reverse-engineering/fl/circle-fill.md, sections
// "Call path, site and candidate stub", "Surface, helpers, lifetime and reentrancy", "Independent deep review" and
// "Port (2026-11-03)"; verifier verification/probe/verify_circle_fill_site_fl.py (it checks every constant below
// against X3FL.exe). X3FL.exe is relocatable: every value is an RVA added to x3m::fl::module_base(), or a field
// offset. Windows are identified by FNV-1a 32 of their bytes with every base-relocation HIGHLOW dword zeroed; at
// install each such dword must equal base + its target RVA. No Windows dependency (the native fixture and the host
// tests compile it).
//
// Rasterizer 0x10c940 (only direct caller 0x0a7b19 `call; add esp,0x10`; ESI live after the return): ECX = surface,
// EDX = cx, stack +4 cy, +8 r, +0xc colour, +0x10 aspect (16.16), plain `ret` at both exits (0x10cd88, 0x10cd9c).
// The claim displaces `push ebp; mov ebp,esp; and esp,-8` (six bytes, three whole instructions, no relative operand,
// no relocation, no entry into 0x10c941..45); the tail replays all three and resumes at 0x10c946 `sub esp,0x64`.
// Its midpoint products are 32-bit like AP's: identical ordered spans to the exact recurrence up to r = 825 at aspect
// 65536, changed from 826, collapsed from 1291 (the scanner disc).
//
// The stub (circle_fill_fl.cpp) reads r as the routine does (movsx of the word at [esp+8]); r <= 0, or r <= 813 at
// aspect 65536, jumps to the tail with ECX/EDX untouched (EAX and the arithmetic flags are dead: the prologue's AND
// writes them, the first EAX write is 0x10c949). Every other call saves EFLAGS and the GPRs, clears DF and runs
// route(): an integer-only decision taken before any native helper (no lock, class call, floating instruction or
// diagnostic precedes it). stock -> every register back, the stock routine from its first instruction. wide -> the
// native order: no-draw checks, lock 0x0f5a90 (ECX = backing, EDX = out[0x1c], one stack word 0, caller pops it) when
// +0 is null (result stored at +0, +0x10 |= 2), class 0x10ba70 (ECX = 0, EDX = surface, AX), the unsupported-class
// diagnostic 0x075110(base+0x190fc0) with the class call's ECX, then the native SSE2 B conversion (0x10ca29..58,
// caller MXCSR in force), then the spans through the native leaf (cdecl surface, x0, y, x1, colour) with 64-bit
// decision variables, the pre-lock clip flag, width read once per emitted pair and height before each span.
namespace x3m::fl::circle_fill_fl {
// ---- the site ----
constexpr std::uint32_t entry_rva = 0x10c940, continue_rva = 0x10c946, caller_rva = 0x0a7b19,
                        caller_return_rva = 0x0a7b1e;
constexpr unsigned site_length = 6;
constexpr unsigned char expected_entry[site_length] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8}; // push ebp; mov ebp,esp; and esp,-8
// ---- the native pieces the wide path calls ----
constexpr std::uint32_t lock_rva = 0x0f5a90, class_rva = 0x10ba70, diagnostic_rva = 0x075110,
                        message_rva = 0x190fc0, scale_rva = 0x191994, jump_table_rva = 0x10cda0,
                        class_index_rva = 0x10cdb8;
constexpr std::uint32_t leaf_class0_rva = 0x0d42a0, leaf_class5_rva = 0x0d45e0, leaf_class10_rva = 0x0d44f0,
                        leaf_class15_rva = 0x0d42f0, leaf_class24_rva = 0x0d4580;
constexpr unsigned lock_out_bytes = 0x1c;
constexpr std::int32_t stock_radius_max = 813;          // r <= 813 at aspect 65536: every decision value fits an int32
constexpr std::int32_t unit_aspect = 65536;
constexpr std::int64_t max_b = std::int64_t(32767) * 32767; // every 64-bit decision term stays under 2^62
// ---- the surface object ----
constexpr unsigned object_bits = 0, object_width = 4, object_height = 6, object_flags = 0x10, object_backing = 0x30;
constexpr std::uint32_t flag_no_draw = 0x200000, flag_locked = 2;
// ---- the stub frame (pushfd then pushad: EDI first, EDX at 5, ECX at 6, EAX at 7, EFLAGS 8, the return 9, the four
// stack arguments from 10) ----
constexpr unsigned frame_edx = 5, frame_ecx = 6, frame_eax = 7, frame_flags = 8, frame_return = 9, frame_args = 10;
constexpr unsigned arg_cy = 0, arg_r = 1, arg_colour = 2, arg_aspect = 3;

// ---- identity windows ----
constexpr unsigned max_relocs = 16;
struct Window {
    const char* name; // the refusal reason when it differs
    std::uint32_t rva;
    unsigned length;
    std::uint32_t fnv; // FNV-1a 32 of the bytes with every relocated dword zeroed
    unsigned reloc_count;
    unsigned reloc_offset[max_relocs];      // dword offsets inside the window
    std::uint32_t reloc_target[max_relocs]; // at the live base each dword must equal base + this RVA
};
// The rasterizer with its jump table and class index table (the leaf movs, the B constant, the diagnostic message and
// the six jump-table entries are the fifteen relocations); the class function (pure: no call, no store); the lock;
// the five leaves; the diagnostic entry; the caller's argument block, call and cleanup (ends before 0x0a7b30, the text
// density's block-blit claim); the float 2^-16.
constexpr Window windows[] = {
    {"routine_mismatch", entry_rva, 0x491, 0x2c12c5f0u, 15,
     {0xb8, 0xbf, 0xc4, 0xcb, 0xd2, 0xd9, 0xe0, 0x10d, 0x44a, 0x460, 0x464, 0x468, 0x46c, 0x470, 0x474},
     {class_index_rva, jump_table_rva, leaf_class0_rva, leaf_class5_rva, leaf_class10_rva, leaf_class24_rva,
      leaf_class15_rva, scale_rva, message_rva, 0x10ca03, 0x10ca0a, 0x10ca11, 0x10ca1f, 0x10ca18, 0x10cd89}},
    {"class_mismatch", class_rva, 0x199, 0x2ac0b97du, 0, {}, {}},
    {"lock_mismatch", lock_rva, 0xdc, 0x0a44ad4fu, 5, {0x27, 0x66, 0x6e, 0x74, 0x84},
     {0x1a82f8, 0x22de10, 0x1aae24, 0x22de10, 0x22de10}},
    {"leaf0_mismatch", leaf_class0_rva, 0x4b, 0xdf8d3607u, 0, {}, {}},
    {"leaf15_mismatch", leaf_class15_rva, 0x200, 0xff15ebfeu, 4, {0x81, 0xd2, 0xf1, 0x165}, {0x17c000, 0x17b080, 0x17c000, 0x17c000}},
    {"leaf10_mismatch", leaf_class10_rva, 0x8f, 0x7bc9e402u, 0, {}, {}},
    {"leaf24_mismatch", leaf_class24_rva, 0x60, 0x33de1b09u, 0, {}, {}},
    {"leaf5_mismatch", leaf_class5_rva, 0x70, 0xaecc0586u, 0, {}, {}},
    {"diagnostic_mismatch", diagnostic_rva, 0x1f, 0xe6fb2212u, 0, {}, {}},
    {"caller_mismatch", 0x0a7b0c, 0x23, 0xab78945du, 0, {}, {}},
    {"scale_mismatch", scale_rva, 4, 0x13586a00u, 0, {}, {}},
};
constexpr unsigned window_count = sizeof windows / sizeof windows[0];
constexpr unsigned max_window = 0x491;

inline std::uint32_t u32_at(const unsigned char* p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
constexpr std::uint32_t fnv1a(const unsigned char* p, unsigned n, std::uint32_t h = 0x811c9dc5u) {
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
    return h;
}
// The install decision on the bytes read at base + w.rva (`current`, w.length bytes; modified in place): true when
// every relocated dword equals base + its target and the zeroed bytes hash to w.fnv.
inline bool window_matches(const Window& w, unsigned char* current, std::uintptr_t base) {
    for (unsigned k = 0; k < w.reloc_count; ++k) {
        if (w.reloc_offset[k] + 4 > w.length) return false;
        if (u32_at(current + w.reloc_offset[k]) != std::uint32_t(base + w.reloc_target[k])) return false;
        std::memset(current + w.reloc_offset[k], 0, 4);
    }
    return fnv1a(current, w.length) == w.fnv;
}

// ---- the arithmetic ----
inline std::int32_t s16(std::int64_t v) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(static_cast<std::uint64_t>(v) & 0xffffu));
}
inline std::int32_t u16(std::int64_t v) {
    return static_cast<std::int32_t>(static_cast<std::uint64_t>(v) & 0xffffu);
}
// q of 0x10ca24..3b: the low signed 32 bits of (aspect^2 + 0x8000) >> 16 (`imul; add; adc; shrd`).
inline std::int32_t quotient(std::int32_t aspect) {
    const std::uint64_t square = static_cast<std::uint64_t>(std::int64_t(aspect) * aspect);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>((square + 0x8000u) >> 16));
}
// The native clip flag (0x10c975..a9), taken before the lock from signed 16-bit centre, radius and dimensions; no B.
inline bool native_clip(std::int32_t cx, std::int32_t cy, std::int32_t r, std::int32_t width, std::int32_t height) {
    const std::int32_t R = s16(r);
    return s16(cx) + R >= s16(width) || s16(cx) - R < 0 || s16(cy) + R >= s16(height) || s16(cy) - R < 0;
}
// The class table of 0x10c9ec..0x10ca1f (index bytes 0x10cdb8, jump table 0x10cda0): the leaf RVA, 0 = unsupported
// (the native diagnostic, then return).
inline std::uint32_t leaf_for_class(std::int32_t pixel_class) {
    switch (pixel_class) {
    case 0: return leaf_class0_rva;
    case 5: return leaf_class5_rva;
    case 10: return leaf_class10_rva;
    case 15: return leaf_class15_rva;
    case 24: return leaf_class24_rva;
    default: return 0;
    }
}
// The recurrence of 0x10ca87..0x10cd7c with 64-bit d, A, B and products (0 < r <= 32767, 0 <= B <= max_b). `pair(left,
// right, y)` receives each emitted pair before clipping (left/right as the signed low words the leaves read, y the
// native EDI); returning false stops at once (the extent check). Region 1 (the caps): x from 0 while yB > xA; region 2
// (the sides): y down to -1.
template <class Pair> inline bool traverse(std::int32_t cx, std::int32_t r, std::int64_t B, Pair&& pair) {
    const std::int64_t R = s16(r), A = R * R;
    std::int32_t Y = u16(r), X = 0;
    std::int64_t d = (1 - B) * 2 * A + (R - 1) * R * 2 * B + B;
    if (R * B > 0) {
        std::int32_t left = u16(cx), right = u16(cx);
        std::int64_t xs = 0;
        for (;;) {
            if (d >= 0) {
                if (!pair(s16(left), s16(right), Y)) return false;
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
    const std::int64_t Xs = s16(X), Ys = s16(Y);
    d = ((Ys - 2) * Ys + 1) * 2 * B + (Xs + 1) * Xs * 2 * A + (1 - 2 * B) * A;
    if (Ys + 1 == 0) return true;
    std::int32_t left = u16(std::int64_t(cx) - X), right = u16(std::int64_t(cx) + X);
    for (;;) {
        if (!pair(s16(left), s16(right), Y)) return false;
        if (d <= 0) {
            ++X;
            d += std::int64_t(s16(X)) * 4 * A;
            --left;
            ++right;
        }
        --Y;
        d += (3 - 2 * std::int64_t(s16(Y))) * 2 * B;
        if (s16(Y) + 1 == 0) return true;
    }
}
// The native row emitter: with the clip flag the width word is read once per emitted pair (0x10cb1d, 0x10ccb2) and the
// height word before each span (0x10cb4a/72, 0x10ccd6/f8), so a leaf callback that changes them is seen as the stock
// routine sees it; x0 = max(left, 0), x1 = right < W ? right : W - 1 (signed low words), x0 > x1 skipped, each row's
// low word unsigned against the height. Without the flag both spans go out unchecked. Rows: cy + y, then cy - y.
template <class Width, class Height, class Leaf>
inline void emit(std::int32_t cx, std::int32_t cy, std::int32_t r, std::int64_t B, bool clip, Width&& width,
                 Height&& height, Leaf&& leaf) {
    traverse(cx, r, B, [&](std::int32_t left, std::int32_t right, std::int32_t y) {
        const std::int64_t up = std::int64_t(cy) + y, down = std::int64_t(cy) - y;
        if (clip) {
            const std::int32_t w = u16(width());
            const std::int32_t x0 = left < 0 ? 0 : left;
            const std::int32_t x1 = right < s16(w) ? right : s16(w - 1);
            if (x0 > x1) return true;
            if (u16(up) < u16(height())) leaf(x0, s16(up), x1);
            if (u16(down) < u16(height())) leaf(x0, s16(down), x1);
        } else {
            leaf(left, s16(up), right);
            leaf(left, s16(down), right);
        }
        return true;
    });
}

// ---- the routing decision (integer only; before any native helper) ----
enum class Route : unsigned char { stock_fast, stock_quotient, stock_axis, stock_extent, wide };
inline const char* route_name(Route r) {
    switch (r) {
    case Route::stock_fast: return "stock_fast";
    case Route::stock_quotient: return "stock_quotient";
    case Route::stock_axis: return "stock_axis";
    case Route::stock_extent: return "stock_extent";
    default: return "wide";
    }
}
// r <= 0, or r <= 813 at aspect 65536: stock (no value wraps). q <= 0: stock. q >= 65536: wide (the float32 of q is
// >= 65536 under every rounding mode, so the native B lies in [0, A] and the radius box covers the x extent). Else the
// native B is exactly (A << 16) / q in every rounding mode (docs/reverse-engineering/fl/circle-fill.md, "Routing and
// clipping policy", step 3): B > 32767^2 -> stock; unclipped with B > A -> wide only when every span of the exact
// recurrence lies inside the surface (a no-write walk that stops at the first escaping span), else stock.
inline Route route(std::int32_t cx, std::int32_t cy, std::int32_t r_word, std::int32_t aspect, std::int32_t width,
                   std::int32_t height) {
    const std::int32_t r = s16(r_word);
    if (r <= 0 || (r <= stock_radius_max && aspect == unit_aspect)) return Route::stock_fast;
    const std::int32_t q = quotient(aspect);
    if (q <= 0) return Route::stock_quotient;
    if (q >= 65536) return Route::wide;
    const std::int64_t A = std::int64_t(r) * r, B = (A << 16) / q;
    if (B > max_b) return Route::stock_axis;
    if (!native_clip(cx, cy, r, width, height) && B > A) {
        const std::int32_t W = s16(width), H = s16(height);
        const bool inside = traverse(cx, r, B, [&](std::int32_t left, std::int32_t right, std::int32_t y) {
            const std::int32_t up = s16(std::int64_t(cy) + y), down = s16(std::int64_t(cy) - y);
            return left >= 0 && left <= right && right < W && up >= 0 && up < H && down >= 0 && down < H;
        });
        if (!inside) return Route::stock_extent;
    }
    return Route::wide;
}
static_assert((entry_rva & 7) + 5 <= 8, "the five patch bytes lie in one aligned 8-byte word at any 64 KB base");
static_assert(entry_rva + site_length == continue_rva, "the continuation follows the displaced prologue");
static_assert(caller_rva + 5 == caller_return_rva, "the one caller is a five-byte call");
}
