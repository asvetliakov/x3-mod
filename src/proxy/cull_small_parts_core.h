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
// successors read on the same node (parent link, own and parent threshold),
// and +0x130 (the projectile marker below), which the pass itself rewrites on
// the same node at 0x0047cfed.
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
// Scope of the cull (X3M_CULL_SMALL_PARTS_SCOPE): `all` (the default since
// 2026-09-19, also when the variable is unset) culls every node below the
// threshold; `bodies` culls only nodes without a parent link
// (`[node+0x18] == 0`, the test the displaced instruction performs: whole
// objects), which saves almost nothing because nearly every small node has a
// parent (docs/verification/cull-small-parts.md, run 43 B). Anything else is
// refused.
enum class Scope : unsigned char { bodies = 0, all = 1 };
inline bool parse_scope(const char* text, Scope* out) {
    if (!text || !*text || !std::strcmp(text, "all")) { *out = Scope::all; return true; }
    if (!std::strcmp(text, "bodies")) { *out = Scope::bodies; return true; }
    return false;
}
inline const char* scope_name(Scope scope) { return scope == Scope::all ? "all" : "bodies"; }
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

// Projectile exemption (X3M_CULL_SMALL_PARTS_PROJECTILES, default on;
// docs/reverse-engineering/lod-selection.md, "Projectile nodes"). The engine's
// object creation 0x0043fxxx..0x004412xx gives every class-0 object (TBullets:
// bolts, beams, flak, whatever type a mod adds to the table) one root node and
// ORs 0x20800000 into its +0x130 (0x004401ae stores the value, 0x00441242 ORs
// it on the class-0 node path only); the engine itself tests +0x130 &
// 0x20000000 to keep such nodes out of the script occluder list (0x00488b00).
// The field is per node and read by the pass on the same node at its entry
// (0x0047cfed `and [edi+0x130],...`). No other object class sets the bit, and a
// false result either way only restores the vanilla compare or the current cull.
// Missiles (class 10) take the generic path (no marker, a multi-node scene) and
// are not exempt; they are large enough to rarely fall under a few pixels.
constexpr unsigned flags130_offset = 0x130;
constexpr std::uint32_t projectile_flag = 0x20000000;
// The two engine instructions that establish the marker, pinned at install:
// 004401ae  c7 44 24 20 00 00 80 20   MOV dword [ESP+0x20],0x20800000   ; class-0 case
// 0044123b  8b 45 70                  MOV EAX,[EBP+0x70]                ; the object's root node
// 0044123e  8b 54 24 20               MOV EDX,[ESP+0x20]
// 00441242  09 90 30 01 00 00         OR  [EAX+0x130],EDX
constexpr std::uintptr_t marker_store_va = 0x004401ae, marker_or_va = 0x0044123b;
constexpr unsigned marker_store_length = 8, marker_or_length = 13;
constexpr unsigned char marker_store[marker_store_length] = {0xc7,0x44,0x24,0x20, 0x00,0x00,0x80,0x20};
constexpr unsigned char marker_or[marker_or_length] = {0x8b,0x45,0x70, 0x8b,0x54,0x24,0x20, 0x09,0x90,0x30,0x01,0x00,0x00};
// `on` (also unset or empty) exempts marked nodes; `off` culls them like any node; anything else is refused.
inline bool parse_projectiles(const char* text, bool* exempt) {
    if (!text || !*text || !std::strcmp(text, "on")) { *exempt = true; return true; }
    if (!std::strcmp(text, "off")) { *exempt = false; return true; }
    return false;
}

// The stub (82 bytes), entered by the dispatcher's `jmp [entry]` with the
// site's exact register state and ESP (no return address):
//    0  83 3d abs32 00      CMP  dword [threshold],0    ; off (0) outside an armed frame
//    7  7e 43               JLE  continue
//    9  50                  PUSH EAX                    ; dead at the site; preserved anyway
//   10  a1 abs32            MOV  EAX,[threshold]
//   15  39 44 24 30         CMP  [ESP+0x30],EAX         ; s (site [ESP+0x2c]) - threshold
//   19  58                  POP  EAX
//   20  7d 36               JGE  continue               ; s >= threshold: the engine's own compare
//   22  f7 87 30 01 00 00 00 00 00 20   TEST dword [EDI+0x130],0x20000000   ; projectile marker
//   32  75 24               JNE  exempt
//   34  8b 4f 18            MOV  ECX,[EDI+0x18]         ; 0x0047d2a2..0x0047d2b9 replayed so ECX/EAX
//   37  85 c9               TEST ECX,ECX                ;   arrive at the cull exactly as the engine
//   39  8b 87 d8 01 00 00   MOV  EAX,[EDI+0x1d8]        ;   leaves them (both dead there anyway)
//   45  74 0c               JE   cull
//   47  8b 89 d8 01 00 00   MOV  ECX,[ECX+0x1d8]
//   53  3b c8               CMP  ECX,EAX
//   55  7e 02               JLE  cull
//   57  8b c1               MOV  EAX,ECX
//   59  ff 05 abs32         INC  dword [culled]         ; cull: per-frame count, render thread only
//   65  e9 rel32            JMP  0x0047d2c3             ; the engine's `and [edi+0x12c],~2; jmp 0x0047d2d1`
//   70  ff 05 abs32         INC  dword [exempt]         ; exempt: per-frame count, then the vanilla compare
//   76  ff 25 abs32         JMP  [next]                 ; continue: the tail (displaced MOV+TEST, jump back to 0x0047d2a7)
// No call, no Win32, no floating point: LastError and the x87 stack are
// untouched by construction; EFLAGS are dead on every exit (the tail's
// displaced TEST regenerates them, the cull AND overwrites them). The marker
// test reads one word of the node and writes no register; it runs only on a
// node already below the threshold.
//
// Projectiles `off` replaces bytes 22..33 with `eb 0a` (JMP 34) and int3
// padding: the marker is not read and the exempt block is unreachable.
//
// Scope `bodies` keeps the layout and replaces bytes 39..58: the replayed
// parent test decides, a parented node continues, a parentless one is culled
// with EAX/ECX exactly as the engine's JE path leaves them (ECX = 0, EAX = own):
//   34  8b 4f 18            MOV  ECX,[EDI+0x18]
//   37  85 c9               TEST ECX,ECX
//   39  75 23               JNE  continue               ; has a parent: the engine's own compare. The tail
//                                                       ;   re-executes the displaced MOV+TEST, so ECX and
//                                                       ;   EFLAGS reach 0x0047d2a7 as native; EAX untouched
//   41  8b 87 d8 01 00 00   MOV  EAX,[EDI+0x1d8]
//   47  eb 0a               JMP  cull
//   49  cc * 10
// One extra taken-or-not branch on nodes already below the threshold only.
// The projectile test precedes the scope's parent test, so it applies to both scopes.
constexpr unsigned stub_length = 82, stub_projectile = 22, stub_replay = 34, stub_cull = 59, stub_exempt = 70, stub_continue = 76, stub_scope_branch = 39;
inline void encode_stub(std::uint32_t at, std::uint32_t threshold, std::uint32_t culled, std::uint32_t exempt, std::uint32_t cull_target, std::uint32_t next_slot,
                        unsigned char out[stub_length], Scope scope, bool exempt_projectiles) {
    out[0] = 0x83; out[1] = 0x3d; std::memcpy(out + 2, &threshold, 4); out[6] = 0x00;
    out[7] = 0x7e; out[8] = static_cast<unsigned char>(stub_continue - 9);
    out[9] = 0x50;
    out[10] = 0xa1; std::memcpy(out + 11, &threshold, 4);
    out[15] = 0x39; out[16] = 0x44; out[17] = 0x24; out[18] = 0x30;
    out[19] = 0x58;
    out[20] = 0x7d; out[21] = static_cast<unsigned char>(stub_continue - 22);
    out[22] = 0xf7; out[23] = 0x87; out[24] = flags130_offset & 0xff; out[25] = flags130_offset >> 8; out[26] = 0x00; out[27] = 0x00;
    std::memcpy(out + 28, &projectile_flag, 4);
    out[32] = 0x75; out[33] = static_cast<unsigned char>(stub_exempt - 34);
    out[34] = 0x8b; out[35] = 0x4f; out[36] = 0x18;
    out[37] = 0x85; out[38] = 0xc9;
    out[39] = 0x8b; out[40] = 0x87; out[41] = 0xd8; out[42] = 0x01; out[43] = 0x00; out[44] = 0x00;
    out[45] = 0x74; out[46] = static_cast<unsigned char>(stub_cull - 47);
    out[47] = 0x8b; out[48] = 0x89; out[49] = 0xd8; out[50] = 0x01; out[51] = 0x00; out[52] = 0x00;
    out[53] = 0x3b; out[54] = 0xc8;
    out[55] = 0x7e; out[56] = static_cast<unsigned char>(stub_cull - 57);
    out[57] = 0x8b; out[58] = 0xc1;
    out[59] = 0xff; out[60] = 0x05; std::memcpy(out + 61, &culled, 4);
    out[65] = 0xe9; const std::uint32_t rel = cull_target - (at + stub_exempt); std::memcpy(out + 66, &rel, 4);
    out[70] = 0xff; out[71] = 0x05; std::memcpy(out + 72, &exempt, 4);
    out[76] = 0xff; out[77] = 0x25; std::memcpy(out + 78, &next_slot, 4);
    if (!exempt_projectiles) {
        out[22] = 0xeb; out[23] = static_cast<unsigned char>(stub_replay - 24);
        std::memset(out + 24, 0xcc, stub_replay - 24);
    }
    if (scope == Scope::bodies) {
        out[39] = 0x75; out[40] = static_cast<unsigned char>(stub_continue - 41);
        out[41] = 0x8b; out[42] = 0x87; out[43] = 0xd8; out[44] = 0x01; out[45] = 0x00; out[46] = 0x00;
        out[47] = 0xeb; out[48] = static_cast<unsigned char>(stub_cull - 49);
        std::memset(out + 49, 0xcc, stub_cull - 49);
    }
}
}
