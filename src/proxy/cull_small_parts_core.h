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
constexpr std::uintptr_t window_va = 0x0047d294, site_va = 0x0047d2a2, next_va = 0x0047d2a7, je_va = 0x0047d2ad,
                         cull_va = 0x0047d2c3, after_cull_va = 0x0047d2d1;
constexpr unsigned window_length = 56, site_offset = 14, site_length = 5, cull_offset = 47;
// clang-format off
constexpr unsigned char window[window_length] = {
    0x83,0xfe,0x14, 0x7d,0x09, 0x33,0xf6, 0x83,0xa7,0x2c,0x01,0x00,0x00,0xfd,
    0x8b,0x4f,0x18, 0x85,0xc9, 0x8b,0x87,0xd8,0x01,0x00,0x00, 0x74,0x0c, 0x8b,0x89,0xd8,0x01,0x00,0x00,
    0x3b,0xc8, 0x7e,0x02, 0x8b,0xc1, 0x85,0xc0, 0x7e,0x0d, 0x3b,0xf0, 0x7d,0x09,
    0x83,0xa7,0x2c,0x01,0x00,0x00,0xfd, 0xeb,0x05};
// clang-format on
constexpr unsigned char site[site_length] = {0x8b, 0x4f, 0x18, 0x85, 0xc9};
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
    double value = 0;
    unsigned digits = 0;
    for (; *p >= '0' && *p <= '9'; ++p, ++digits) value = value * 10.0 + (*p - '0');
    if (*p == '.') {
        double scale = 0.1;
        for (++p; *p >= '0' && *p <= '9'; ++p, ++digits) {
            value += (*p - '0') * scale;
            scale *= 0.1;
        }
    }
    if (*p != '\0' || digits == 0) return false;
    *out = value;
    return true;
}
inline bool valid_px(double px) {
    return std::isfinite(px) && px > px_min && px <= px_max;
}
// The pixel scale of `s`: px = s * m00 * width / 1280 * focus / 0x4000
// (s is the projected radius at a 640-wide reference, m00 the projection's
// P[0], width the back buffer's). The engine computes s = r*640/D' with
// D' = D * focus / 0x4000 (0x0047d1ce, focus = the view camera's +0x298, the
// base FOV divided by the cockpit zoom, 0x4000 = the game's default;
// docs/reverse-engineering/field-of-view.md section 6), so the true pixel
// radius r/D * m00 * width/2 carries the factor focus/0x4000; at the default
// the factor is exactly 1. The threshold is the smallest integer t with
// t * px_per_s >= px, so `s < t` is exactly the class
// tools/analysis/cull_census.py buckets as `s * px_per_s < px` when it is
// given the same focus (it derives it from the logged projection rows the
// same way, else from the cull_small_parts_value row). 0 when the inputs are
// unusable.
constexpr std::uint32_t focus_default = 0x4000, focus_min = 0x106, focus_max = 0x8000;
// The view's focus from the live projection, no engine read: m11 = cot(F/2)/H
// and m00 = cot(F/2)/W with the default view plane H = 0.75, W = 0.75*w/h for
// displays at least as wide as 4:3 and W = 1, H = h/w for narrower ones
// (field-of-view.md section 1). m00/m11 = h/w, so H = max(0.75, m00/m11) and
// cot(F/2) = max(0.75*m11, m00); F = 65536/pi * atan(1 / cot(F/2)), rounded.
// This is the camera's own +0x298, zoom included. 0 when either term is
// unusable or F falls outside [focus_min, focus_max]. The engine's own
// projection is about 1e-4 off (run309 vanilla: m00 0.3750374, m11 1.333461
// give F = 16383.0), about one unit of F, so a value within focus_snap of
// the default is the default (factor exactly 1); the menu's 1-degree steps
// are 182 units apart and every other F keeps its nearest integer.
constexpr double focus_snap = 2.0;
inline std::uint32_t focus_from_projection(float m00, float m11) {
    if (!std::isfinite(m00) || !std::isfinite(m11) || !(m00 > 0.0f) || !(m11 > 0.0f)) return 0;
    const double cot = std::fmax(0.75 * static_cast<double>(m11), static_cast<double>(m00));
    const double exact = 65536.0 / 3.14159265358979323846 * std::atan(1.0 / cot);
    if (std::fabs(exact - static_cast<double>(focus_default)) <= focus_snap) return focus_default;
    const double focus = std::floor(exact + 0.5);
    if (!(focus >= focus_min) || !(focus <= focus_max)) return 0;
    return static_cast<std::uint32_t>(focus);
}
// Whose projection the frame's threshold uses (Run 81 A launch 1, run309,
// docs/verification/field-of-view.md). begin_frame runs in the Present hook,
// where the engine's buffer holds the view the frame just presented
// activated last (a cockpit/HUD view whose
// F stays 0x4000 whatever --fov or the menu set), so P[0]/P[5] come from the
// scene view as the motion route latches them at the scene phase's Clear
// (the read behind the camera_state rows; the cull/LOD pass runs for that
// view right after its activation), at most scene_max_age frames old. Without
// a usable latch (the route off, a Reset, a menu without a scene phase) the
// registry base F is the fallback, applied to the live projection's aspect.
constexpr unsigned scene_max_age = 8;
enum class Source : unsigned char { registry = 0, scene = 1 };
inline const char* source_name(Source source) {
    return source == Source::scene ? "scene" : "registry";
}
// Why the registry: no_scene = nothing latched since install (the motion
// output is off, or its selector never reached the scene phase, e.g. a
// multisampled main target), reset = nothing since a Reset dropped the
// latch, aged = no scene Clear for more than scene_max_age frames.
enum class Fallback : unsigned char { none = 0, no_scene = 1, reset = 2, aged = 3 };
inline const char* fallback_name(Fallback fallback) {
    return fallback == Fallback::no_scene ? "no_scene"
           : fallback == Fallback::reset  ? "reset"
           : fallback == Fallback::aged   ? "aged"
                                          : "none";
}
struct SceneLatch {
    float m00 = 0, m11 = 0;
    std::uint32_t focus = 0;             // focus_from_projection of the latched terms; 0 = nothing latched
    unsigned age = scene_max_age + 1;    // begin_frame calls since the latch
    Fallback empty = Fallback::no_scene; // why focus is 0
    // A projection whose focus is unusable leaves the previous latch (it ages out).
    void note(float p00, float p11) {
        const std::uint32_t f = focus_from_projection(p00, p11);
        if (!f) return;
        m00 = p00;
        m11 = p11;
        focus = f;
        age = 0;
    }
    bool usable() const { return focus != 0 && age <= scene_max_age; }
    Fallback fallback() const { return usable() ? Fallback::none : focus == 0 ? empty : Fallback::aged; }
    void advance() {
        if (age <= scene_max_age) ++age;
    }
    void clear() {
        *this = SceneLatch{};
        empty = Fallback::reset;
    }
};
// The registry fallback's P[0]: the live projection rescaled to the base F,
// cot(F/2)/W with W = cot_live/m00_live (cot_live = max(0.75*m11, m00), as in
// focus_from_projection), so only the aspect is taken from the live view. The
// live P[0] unchanged when its P[5] is unusable or F is outside the band.
inline float fallback_m00(float live_m00, float live_m11, std::uint32_t focus) {
    if (!std::isfinite(live_m00) || !std::isfinite(live_m11) || !(live_m00 > 0.0f) || !(live_m11 > 0.0f) ||
        focus < focus_min || focus > focus_max)
        return live_m00;
    const double cot_live = std::fmax(0.75 * static_cast<double>(live_m11), static_cast<double>(live_m00));
    const double cot = 1.0 / std::tan(static_cast<double>(focus) * 3.14159265358979323846 / 65536.0);
    return static_cast<float>(static_cast<double>(live_m00) * cot / cot_live);
}
// The frame's P[0] and F from a valid live projection: the scene latch when
// usable, else registry_focus (read by the caller only in that case) applied
// to the live projection.
struct Choice {
    float m00;
    std::uint32_t focus;
    Source source;
    Fallback fallback;
};
inline Choice choose(const SceneLatch& scene, float live_m00, float live_m11, std::uint32_t registry_focus) {
    if (scene.usable()) return Choice{scene.m00, scene.focus, Source::scene, Fallback::none};
    return Choice{fallback_m00(live_m00, live_m11, registry_focus), registry_focus, Source::registry, scene.fallback()};
}
inline std::int32_t threshold_for(double px, float m00, unsigned width, std::uint32_t focus = focus_default) {
    if (!valid_px(px) || !std::isfinite(m00) || !(m00 > 0.05f) || !(m00 < 20.0f) || width < 64 || width > 16384)
        return 0;
    if (focus < focus_min || focus > focus_max) return 0;
    const double px_per_s = static_cast<double>(m00) * static_cast<double>(width) / 1280.0 *
                            (static_cast<double>(focus) / static_cast<double>(focus_default));
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
constexpr unsigned char marker_store[marker_store_length] = {0xc7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x80, 0x20};
constexpr unsigned char marker_or[marker_or_length] = {0x8b, 0x45, 0x70, 0x8b, 0x54, 0x24, 0x20,
                                                       0x09, 0x90, 0x30, 0x01, 0x00, 0x00};
// `on` (also unset or empty) exempts marked nodes; `off` culls them like any node; anything else is refused.
inline bool parse_projectiles(const char* text, bool* exempt) {
    if (!text || !*text || !std::strcmp(text, "on")) {
        *exempt = true;
        return true;
    }
    if (!std::strcmp(text, "off")) {
        *exempt = false;
        return true;
    }
    return false;
}

// Carrier dock-port parts (X3M_CULL_DOCK_PARTS_PX; docs/reverse-engineering/ship-scene-parts.md, 2026-09-29):
// the inline bodies of the stock dock cut scenes 9013/9014 (DockCarrier_quicklaunch_scene / DockCarrier_scene)
// and 9098/9099 (the M6 variants) become render nodes with model id local + (cut - 1) * 100000 (0x004920ec..
// 0x00492105 text, 0x00491521 binary CUT1), local = 1000nn, i.e. ids 901300000..901499999 and
// 909800000..909999999. The id is node+0x140, read by the pass itself on the same node at 0x0047d19b before
// the site. Such a node is culled below a second, larger threshold (its own pixel setting through
// threshold_for); every other node keeps the X3M_CULL_SMALL_PARTS_PX rule. Two unsigned range compares:
// id - base < span.
constexpr unsigned model_offset = 0x140;
constexpr std::uint32_t dock_first_base = 901300000, dock_second_base = 909800000, dock_span = 200000;
inline bool dock_model(std::uint32_t id) {
    return id - dock_first_base < dock_span || id - dock_second_base < dock_span;
}
// The word the stub compares first: the larger of the two thresholds, 0 when the small-parts threshold is
// 0 (a vanilla frame disarms both). A dock threshold at or below the small one adds nothing.
inline std::int32_t upper_for(std::int32_t small, std::int32_t dock) {
    if (small <= 0) return 0;
    return dock > small ? dock : small;
}

// The stub (147 bytes), entered by the dispatcher's `jmp [entry]` with the
// site's exact register state and ESP (no return address). `upper` =
// upper_for(threshold, dock threshold):
//    0  83 3d abs32 00      CMP  dword [upper],0        ; off (0) outside an armed frame
//    7  7e 36               JLE  continue
//    9  50                  PUSH EAX                    ; dead at the site; preserved anyway
//   10  a1 abs32            MOV  EAX,[upper]
//   15  39 44 24 30         CMP  [ESP+0x30],EAX         ; s (site [ESP+0x2c]) - upper
//   19  7d 29               JGE  pop_continue           ; s >= upper: the engine's own compare
//   21  a1 abs32            MOV  EAX,[threshold]
//   26  39 44 24 30         CMP  [ESP+0x30],EAX         ; s - threshold
//   30  7c 3a               JL   pop_small              ; below the small-parts threshold: any node
//   32  8b 87 40 01 00 00   MOV  EAX,[EDI+0x140]        ; threshold <= s < upper: dock-port ids only
//   38  2d imm32            SUB  EAX,901300000
//   43  3d imm32            CMP  EAX,200000
//   48  72 13               JB   pop_dock
//   50  2d imm32            SUB  EAX,8500000            ; id - 909800000
//   55  3d imm32            CMP  EAX,200000
//   60  72 07               JB   pop_dock
//   62  58                  POP  EAX                    ; pop_continue
//   63  ff 25 abs32         JMP  [next]                 ; continue: the tail (displaced MOV+TEST, jump back to 0x0047d2a7)
//   69  58                  POP  EAX                    ; pop_dock
//   70  f7 87 30 01 00 00 00 00 00 20   TEST dword [EDI+0x130],0x20000000   ; projectile marker
//   80  75 39               JNE  exempt
//   82  ff 05 abs32         INC  dword [dock_culled]    ; per-frame count, render thread only
//   88  eb 13               JMP  replay
//   90  58                  POP  EAX                    ; pop_small
//   91  f7 87 30 01 00 00 00 00 00 20   TEST dword [EDI+0x130],0x20000000   ; projectile marker
//  101  75 24               JNE  exempt
//  103  ff 05 abs32         INC  dword [culled]         ; per-frame count, render thread only
//  109  8b 4f 18            MOV  ECX,[EDI+0x18]         ; replay: 0x0047d2a2..0x0047d2b9 replayed so ECX/EAX
//  112  85 c9               TEST ECX,ECX                ;   arrive at the cull exactly as the engine
//  114  8b 87 d8 01 00 00   MOV  EAX,[EDI+0x1d8]        ;   leaves them (both dead there anyway)
//  120  74 0c               JE   cull
//  122  8b 89 d8 01 00 00   MOV  ECX,[ECX+0x1d8]
//  128  3b c8               CMP  ECX,EAX
//  130  7e 02               JLE  cull
//  132  8b c1               MOV  EAX,ECX
//  134  e9 rel32            JMP  0x0047d2c3             ; cull: the engine's `and [edi+0x12c],~2; jmp 0x0047d2d1`
//  139  ff 05 abs32         INC  dword [exempt]         ; exempt: per-frame count, then the vanilla compare
//  145  eb ac               JMP  continue
// No call, no Win32, no floating point: LastError and the x87 stack are
// untouched by construction; EFLAGS are dead on every exit (the tail's
// displaced TEST regenerates them, the cull AND overwrites them); EAX is
// restored on every path that leaves through the tail and rewritten by the
// replay on every cull. The marker test and the id read touch one word of
// the node each and run only on a node already below `upper`. With the dock
// rule off (upper == threshold) the path of a node at or above the threshold
// is the same eight instructions as before the dock rule; a node below the
// small threshold takes three more (MOV, CMP, JL); only a node between the
// two thresholds runs the id compares.
//
// Projectiles `off` replaces bytes 70..81 and 91..102 with `eb 0a` (JMP +10)
// and int3 padding: the marker is not read and the exempt block is unreachable.
//
constexpr unsigned stub_length = 147, stub_continue = 63, stub_pop_continue = 62, stub_dock = 69, stub_dock_projectile = 70,
                   stub_dock_count = 82, stub_small = 90, stub_projectile = 91, stub_count = 103, stub_replay = 109,
                   stub_cull = 134, stub_exempt = 139;
constexpr unsigned stub_marker_length = 12; // TEST (10) + JNE (2)
inline void encode_stub(std::uint32_t at, std::uint32_t threshold, std::uint32_t upper, std::uint32_t culled,
                        std::uint32_t exempt, std::uint32_t dock_culled, std::uint32_t cull_target,
                        std::uint32_t next_slot, unsigned char out[stub_length], bool exempt_projectiles) {
    unsigned n = 0;
    auto b = [&](unsigned char v) { out[n++] = v; };
    auto d = [&](std::uint32_t v) {
        std::memcpy(out + n, &v, 4);
        n += 4;
    };
    auto rel8 = [&](unsigned target) { b(static_cast<unsigned char>(static_cast<int>(target) - static_cast<int>(n + 1))); };
    auto marker = [&]() {
        b(0xf7);
        b(0x87);
        d(flags130_offset);
        d(projectile_flag);
        b(0x75);
        rel8(stub_exempt);
    };
    // clang-format off
    b(0x83); b(0x3d); d(upper); b(0x00); // 0
    b(0x7e); rel8(stub_continue);         // 7
    b(0x50);                              // 9
    b(0xa1); d(upper);                    // 10
    b(0x39); b(0x44); b(0x24); b(0x30);   // 15
    b(0x7d); rel8(stub_pop_continue);     // 19
    b(0xa1); d(threshold);                // 21
    b(0x39); b(0x44); b(0x24); b(0x30);   // 26
    b(0x7c); rel8(stub_small);            // 30
    b(0x8b); b(0x87); d(model_offset);    // 32
    b(0x2d); d(dock_first_base);          // 38
    b(0x3d); d(dock_span);                // 43
    b(0x72); rel8(stub_dock);             // 48
    b(0x2d); d(dock_second_base - dock_first_base); // 50
    b(0x3d); d(dock_span);                // 55
    b(0x72); rel8(stub_dock);             // 60
    b(0x58);                              // 62 pop_continue
    b(0xff); b(0x25); d(next_slot);       // 63 continue
    b(0x58);                              // 69 pop_dock
    marker();                             // 70
    b(0xff); b(0x05); d(dock_culled);     // 82
    b(0xeb); rel8(stub_replay);           // 88
    b(0x58);                              // 90 pop_small
    marker();                             // 91
    b(0xff); b(0x05); d(culled);          // 103
    b(0x8b); b(0x4f); b(0x18);            // 109 replay
    b(0x85); b(0xc9);
    b(0x8b); b(0x87); d(threshold_1d8_offset);
    b(0x74); rel8(stub_cull);
    b(0x8b); b(0x89); d(threshold_1d8_offset);
    b(0x3b); b(0xc8);
    b(0x7e); rel8(stub_cull);
    b(0x8b); b(0xc1);
    b(0xe9); d(cull_target - (at + stub_exempt)); // 134 cull
    b(0xff); b(0x05); d(exempt);                  // 139 exempt
    b(0xeb); rel8(stub_continue);                 // 145
    // clang-format on
    if (!exempt_projectiles) {
        const unsigned markers[2] = {stub_dock_projectile, stub_projectile};
        for (unsigned at_marker : markers) {
            out[at_marker] = 0xeb;
            out[at_marker + 1] = static_cast<unsigned char>(stub_marker_length - 2);
            std::memset(out + at_marker + 2, 0xcc, stub_marker_length - 2);
        }
    }
}
}
