#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

// Portable core of the projected-size cull of small parts (X3M_CULL_SMALL_PARTS_PX;
// docs/reverse-engineering/lod-selection.md, "Cull small parts site";
// docs/architecture/engine-frame-time.md 2.3): the verified byte window of the
// cull/LOD pass 0x0047cfe0 around the effective-limit computation, the
// five-byte trampoline site, the stub encoding, the pixel-to-`s` threshold
// rule and the setting parser. No Windows dependency so the host tests compile
// it directly.
namespace x3m::cull_small_parts::core {
// The window (56 bytes) pins the env-map zeroing, the effective-limit
// computation, the size compare and the engine's own cull instruction:
//
// 0047d294  83 fe 14                 CMP  ESI,0x14
// 0047d297  7d 09                    JGE  0x0047d2a2
// 0047d299  33 f6                    XOR  ESI,ESI                  ; env-map view: measure zeroed
// 0047d29b  83 a7 2c 01 00 00 fd     AND  dword [EDI+0x12c],~2
// 0047d2a2  8b 4f 18                 MOV  ECX,[EDI+0x18]           <- site (3 + 2 bytes, displaced)
// 0047d2a5  85 c9                    TEST ECX,ECX                  ; flag writer inside the displaced span
// 0047d2a7  8b 87 d8 01 00 00        MOV  EAX,[EDI+0x1d8]          ; next: own threshold
// 0047d2ad  74 0c                    JE   0x0047d2bb               ; consumes the TEST flags after the tail's jump back
// 0047d2af  8b 89 d8 01 00 00        MOV  ECX,[ECX+0x1d8]          ; parent threshold
// 0047d2b5  3b c8                    CMP  ECX,EAX
// 0047d2b7  7e 02                    JLE  0x0047d2bb
// 0047d2b9  8b c1                    MOV  EAX,ECX                  ; EAX = max(own, parent)
// 0047d2bb  85 c0                    TEST EAX,EAX
// 0047d2bd  7e 0d                    JLE  0x0047d2cc
// 0047d2bf  3b f0                    CMP  ESI,EAX                  ; measure < limit ?
// 0047d2c1  7d 09                    JGE  0x0047d2cc
// 0047d2c3  83 a7 2c 01 00 00 fd     AND  dword [EDI+0x12c],~2     <- cull: the engine's size-cull path
// 0047d2ca  eb 05                    JMP  0x0047d2d1
//
// Reached from 0x0047d28c (je) and 0x0047d297 (jge) and by fall-through;
// nothing branches into 0x0047d2a3..0x0047d2a6. Live at the site and read by
// the stub: EDI = node, [ESP+0x2c] = s = r*640/D (>= 1, or 0x7000000). Dead:
// EAX (written at 0x0047d2a7), ECX (written by the displaced MOV), EFLAGS (the
// displaced TEST regenerates them for the JE, the cull AND overwrites them),
// x87 stack empty (fld/fstp at 0x0047d0f2/0x0047d0fa balanced). EDX, EBX,
// EBP, ESI and ESP are untouched by the stub.
constexpr std::uintptr_t function_va = 0x0047cfe0, function_end_va = 0x0047d552;
constexpr std::uintptr_t window_va = 0x0047d294, site_va = 0x0047d2a2, next_va = 0x0047d2a7, je_va = 0x0047d2ad, cull_va = 0x0047d2c3, after_cull_va = 0x0047d2d1;
constexpr unsigned window_length = 56, site_offset = 14, site_length = 5, cull_offset = 47;
constexpr unsigned char window[window_length] = {
    0x83,0xfe,0x14, 0x7d,0x09, 0x33,0xf6, 0x83,0xa7,0x2c,0x01,0x00,0x00,0xfd,
    0x8b,0x4f,0x18, 0x85,0xc9, 0x8b,0x87,0xd8,0x01,0x00,0x00, 0x74,0x0c, 0x8b,0x89,0xd8,0x01,0x00,0x00,
    0x3b,0xc8, 0x7e,0x02, 0x8b,0xc1, 0x85,0xc0, 0x7e,0x0d, 0x3b,0xf0, 0x7d,0x09,
    0x83,0xa7,0x2c,0x01,0x00,0x00,0xfd, 0xeb,0x05};
constexpr unsigned char site[site_length] = {0x8b,0x4f,0x18, 0x85,0xc9};
constexpr unsigned ret_pop = 8;
// Node fields the stub reads: the same ones the displaced span and its
// successors read on the same node (parent link, own and parent threshold).
constexpr unsigned parent_offset = 0x18, threshold_1d8_offset = 0x1d8;
// Setting band: pixels of projected radius below which a node is culled;
// 0 or unset = off. 64 px is a guard against a typo, not a measurement.
constexpr double px_min = 0.0, px_max = 64.0;
// The largest threshold the stub will publish (s is >= 1, saturates at 0x7000000).
constexpr std::int32_t threshold_max = 0x1000000;

// Locale-independent decimal parse of X3M_CULL_SMALL_PARTS_PX:
// `[+]digits[.digits]` or `.digits`, nothing else.
inline bool parse_px(const char* text, double* out) {
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
inline bool valid_px(double px) { return std::isfinite(px) && px > px_min && px <= px_max; }
// The pixel scale of `s`: px = s * m00 * width / 1280 (tools/analysis/cull_census.py,
// the census bucket rule; s is the projected radius at a 640-wide reference,
// m00 the projection's P[0], width the back buffer's). The threshold is the
// smallest integer t with t * px_per_s >= px, so `s < t` is exactly the
// summariser's `s * px_per_s < px` class; 0 when the inputs are unusable.
inline std::int32_t threshold_for(double px, float m00, unsigned width) {
    if (!valid_px(px) || !std::isfinite(m00) || !(m00 > 0.05f) || !(m00 < 20.0f) || width < 64 || width > 16384) return 0;
    const double px_per_s = static_cast<double>(m00) * static_cast<double>(width) / 1280.0;
    double t = std::ceil(px / px_per_s);
    while (t > 1.0 && (t - 1.0) * px_per_s >= px) t -= 1.0;
    while (t * px_per_s < px) t += 1.0;
    if (!(t >= 1.0)) return 0;
    if (t > static_cast<double>(threshold_max)) return threshold_max;
    return static_cast<std::int32_t>(t);
}

// The stub (64 bytes), entered by the dispatcher's `jmp [entry]` with the
// site's exact register state and ESP (no return address):
//    0  83 3d abs32 00      CMP  dword [threshold],0    ; off (0) outside an armed frame
//    7  7e 31               JLE  continue
//    9  50                  PUSH EAX                    ; dead at the site; preserved anyway
//   10  a1 abs32            MOV  EAX,[threshold]
//   15  39 44 24 30         CMP  [ESP+0x30],EAX         ; s (site [ESP+0x2c]) - threshold
//   19  58                  POP  EAX
//   20  7d 24               JGE  continue               ; s >= threshold: the engine's own compare
//   22  8b 4f 18            MOV  ECX,[EDI+0x18]         ; 0x0047d2a2..0x0047d2b9 replayed so ECX/EAX
//   25  85 c9               TEST ECX,ECX                ;   arrive at the cull exactly as the engine
//   27  8b 87 d8 01 00 00   MOV  EAX,[EDI+0x1d8]        ;   leaves them (both dead there anyway)
//   33  74 0c               JE   cull
//   35  8b 89 d8 01 00 00   MOV  ECX,[ECX+0x1d8]
//   41  3b c8               CMP  ECX,EAX
//   43  7e 02               JLE  cull
//   45  8b c1               MOV  EAX,ECX
//   47  ff 05 abs32         INC  dword [culled]         ; cull: per-frame count, render thread only
//   53  e9 rel32            JMP  0x0047d2c3             ; the engine's `and [edi+0x12c],~2; jmp 0x0047d2d1`
//   58  ff 25 abs32         JMP  [next]                 ; continue: the tail (displaced MOV+TEST, jump back to 0x0047d2a7)
// No call, no Win32, no floating point: LastError and the x87 stack are
// untouched by construction; EFLAGS are dead on both exits.
constexpr unsigned stub_length = 64, stub_cull = 47, stub_continue = 58;
inline void encode_stub(std::uint32_t at, std::uint32_t threshold, std::uint32_t culled, std::uint32_t cull_target, std::uint32_t next_slot, unsigned char out[stub_length]) {
    out[0] = 0x83; out[1] = 0x3d; std::memcpy(out + 2, &threshold, 4); out[6] = 0x00;
    out[7] = 0x7e; out[8] = static_cast<unsigned char>(stub_continue - 9);
    out[9] = 0x50;
    out[10] = 0xa1; std::memcpy(out + 11, &threshold, 4);
    out[15] = 0x39; out[16] = 0x44; out[17] = 0x24; out[18] = 0x30;
    out[19] = 0x58;
    out[20] = 0x7d; out[21] = static_cast<unsigned char>(stub_continue - 22);
    out[22] = 0x8b; out[23] = 0x4f; out[24] = 0x18;
    out[25] = 0x85; out[26] = 0xc9;
    out[27] = 0x8b; out[28] = 0x87; out[29] = 0xd8; out[30] = 0x01; out[31] = 0x00; out[32] = 0x00;
    out[33] = 0x74; out[34] = static_cast<unsigned char>(stub_cull - 35);
    out[35] = 0x8b; out[36] = 0x89; out[37] = 0xd8; out[38] = 0x01; out[39] = 0x00; out[40] = 0x00;
    out[41] = 0x3b; out[42] = 0xc8;
    out[43] = 0x7e; out[44] = static_cast<unsigned char>(stub_cull - 45);
    out[45] = 0x8b; out[46] = 0xc1;
    out[47] = 0xff; out[48] = 0x05; std::memcpy(out + 49, &culled, 4);
    out[53] = 0xe9; const std::uint32_t rel = cull_target - (at + 58); std::memcpy(out + 54, &rel, 4);
    out[58] = 0xff; out[59] = 0x25; std::memcpy(out + 60, &next_slot, 4);
}
}
