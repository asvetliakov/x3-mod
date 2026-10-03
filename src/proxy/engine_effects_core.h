#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "sse_scalar.h"
#include "lens_flare_cull_core.h"
#include "engine_effects_option.h"
#include "engine_far_jets_core.h"

// Portable core of the engine-effects phase 1a (docs/architecture/engine-effects-modern.md sections 1, 2 and 5;
// docs/reverse-engineering/engine-effects.md sections 2-4): the glow-jet recogniser's truth table, the throttle and
// geometry of one recognised draw, its 64-byte record and the per-frame ring, the engine_bodies.json parser (schema 1
// of tools/effects/engine_bodies.py) and the name -> id resolver over the engine's body table (the lens_flare_cull
// recipe: its table header, slot and name helpers, generalised to a loaded name list of any length). No Windows
// dependency: the host tests compile it. No x87: float work only through SSE scalars, no float returned by value from
// a function that may stay out of line (i686 returns those in st(0)).
namespace x3m::engine_effects::core {
namespace lfc = x3m::lens_flare_cull::core;
namespace census = x3m::cull_census::core;

// --------------------------------------------------------------------------- option
// X3M_ENGINE_EFFECTS=native|off|plumes (ini engine_effects), read once at load. native = the game's draws; off = a
// recognised glow-jet draw is recorded and not forwarded; plumes = off in this phase (the stage arrives in phase 2).
// The enum, names and the exact-lowercase parser (mixed case refused) live in engine_effects_option.h, shared with
// the redirect module (engine_effects_sites.h).
using option::Mode;
using option::mode_name;
using option::parse_mode;
inline bool suppresses(Mode m) noexcept {
    return m != Mode::native;
}

// --------------------------------------------------------------------------- recogniser
// node+0x130 & 0x4000001: set on every SBTYPE_JET body at 0x00434708 (and on v/00566 through the SMALLJET loop).
constexpr std::uint32_t jet_flags = 0x4000001u;
// The effects pair of engine.fx / effects.fx (DEFAULT VS d5e1c753 or its variant 89193868, PS 8360f422).
constexpr std::uint64_t effect_vs_a = 0xd5e1c75351ed3f04ull, effect_vs_b = 0x89193868c61c3846ull,
                        effect_ps = 0x8360f422de08b5bdull;
inline bool effect_pair(std::uint64_t vs, std::uint64_t ps) noexcept {
    return ps == effect_ps && (vs == effect_vs_a || vs == effect_vs_b);
}
constexpr std::uint32_t steering_model = 566; // v/00566: JET + SMALLJET (+0x1d8 = 5), the RCS nozzle body

// The draw's bound state from the proxy's shadow (no device call): the effect pair, Z-write and blending.
struct DrawState {
    bool pair = false;
    bool zwrite_known = false;
    std::uint32_t zwrite = 0;
    bool blend_known = false;
    std::uint32_t blend = 0;
};
// A candidate is a draw worth a scope read: the effect pair, or any pair blending with Z-write off. Everything else
// (every opaque draw included) costs these few compares and stays native.
inline bool candidate(const DrawState& s) noexcept {
    return s.pair || (s.blend_known && s.blend != 0 && s.zwrite_known && s.zwrite == 0);
}
enum class Verdict : std::uint8_t {
    none,       // not a candidate, or a blended non-pair draw outside an object scope: native, not counted
    not_jet,    // scoped candidate without the JET flags (every other transparent ship part): native
    suppressed, // recorded, not forwarded
    unscoped,   // forwarded_unscoped: the effect pair outside an object scope
    snapshot,   // forwarded_snapshot: the scope's node could not be read (fail closed)
    opaque,     // forwarded_opaque: a JET node drawn with Z-write on (nozzle geometry) - effect pair only
    state,      // forwarded_state: Z-write unknown in the shadow - effect pair only
    overflow,   // forwarded_overflow: the frame's ring is full
    native,     // forwarded_native: mode native (the census counts what off would suppress)
    patch_missing, // forwarded_patch_missing: off|plumes without both call redirects live (engine_effects_patch.h):
                   // the glow is never hidden while the native sprites, flares and trails stay
    stage_off      // forwarded_stage_off: plumes whose stage is not attached on this device (refused at attach or
                   // creation, failed until Reset, or within its 64-frame disarm): the game's glow rather than nothing;
                   // the sprite and trail redirects stay (load-time)
};
constexpr unsigned forward_reasons = 8; // unscoped .. stage_off, in this order
inline unsigned forward_index(Verdict v) noexcept {
    return v >= Verdict::unscoped ? unsigned(v) - unsigned(Verdict::unscoped) : forward_reasons;
}
inline const char* verdict_name(Verdict v) noexcept {
    static const char* const names[] = {"none", "not_jet", "suppressed", "forwarded_unscoped", "forwarded_snapshot",
                                        "forwarded_opaque", "forwarded_state", "forwarded_overflow", "forwarded_native",
                                        "forwarded_patch_missing", "forwarded_stage_off"};
    return unsigned(v) < sizeof names / sizeof names[0] ? names[unsigned(v)] : "none";
}
// What the hot path learned about the draw, in the order it learns it (each later field is read only when the
// earlier ones did not decide): scope present, node snapshot readable, the node's +0x130, the mode, the two call
// redirects, the plume stage, the ring.
struct Facts {
    bool scoped = false;
    bool snapshot = false;
    std::uint32_t flags130 = 0;
    bool suppress = false;
    bool redirects = false; // engine_effects_patch::installed(): both call redirects live
    bool stage_off = false; // plumes requested, the stage not attached on this device (refused or disarmed this frame)
    bool ring_full = false;
};
inline Verdict classify(const DrawState& s, const Facts& f) noexcept {
    if (!candidate(s)) return Verdict::none;
    if (!f.scoped) return s.pair ? Verdict::unscoped : Verdict::none;
    if (!f.snapshot) return Verdict::snapshot;
    if ((f.flags130 & jet_flags) != jet_flags) return Verdict::not_jet;
    if (!s.zwrite_known) return Verdict::state;
    if (s.zwrite) return Verdict::opaque;
    if (!f.suppress) return Verdict::native;
    if (!f.redirects) return Verdict::patch_missing;
    if (f.stage_off) return Verdict::stage_off;
    if (f.ring_full) return Verdict::overflow;
    return Verdict::suppressed;
}

// --------------------------------------------------------------------------- throttle
// z = node+0x88 / 65536, the engine's per-frame z-scale (0.25 + 1.75 clamp(speed / vmax) for a main jet, rate-limited;
// brake and steering bits push it up to 9.0); s = clamp((z - 0.25) / 1.75, 0, 1).
inline void throttle(std::uint32_t scale3, float* z, float* s) noexcept {
    const float zz = float(std::int32_t(scale3)) * (1.f / 65536.f);
    float t = (zz - .25f) * (1.f / 1.75f);
    t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
    *z = zz;
    *s = t;
}

// --------------------------------------------------------------------------- geometry from c4-6
// The draw's world rows c4-6 (12 floats: c4.xyzw, c5.xyzw, c6.xyzw). Order a (dp4 rows, the vertex program's
// world.x = dot(c4, pos)): model axis k = (c4[k], c5[k], c6[k]); order b (one register per model axis): model x =
// c4.xyz, model z = c6.xyz. The translation is the .w column in both. Which one the engine uploads is unconfirmed
// (engine-effects.md, "Unknown"): the order whose |z| / |x| length ratio equals the node's z-scale is taken; both
// matching (z = 1, or an axis-aligned basis) is ambiguous and keeps the pinned order; neither matching is a mismatch
// (counted, the pinned order used).
enum class Order : std::uint8_t { a, b };
enum class Match : std::uint8_t { a, b, ambiguous, mismatch, invalid };
constexpr unsigned match_count = 5;
inline const char* match_name(Match m) noexcept {
    static const char* const names[match_count] = {"a", "b", "ambiguous", "mismatch", "invalid"};
    return unsigned(m) < match_count ? names[unsigned(m)] : "invalid";
}
constexpr float ratio_tolerance = 1e-3f; // relative; float rows of a 16.16 basis agree to about 1e-5
struct Geometry {
    float origin[3]{}, axis[3]{}; // axis = -(model z), unit length
    float size = 0.f;             // |model x| of the chosen order: the body's LOD-0 value x the context scale
    float ratio = 0.f;            // |z| / |x| of the chosen order
    float ratio_a = 0.f, ratio_b = 0.f;
    float z_a[3]{}, z_b[3]{};     // both orders' unit model-z axes (the census's cross-check against the node basis)
    Order order = Order::a;
    Match match = Match::invalid;
};
inline bool finite_f(float v) noexcept {
    return v == v && v - v == 0.f;
}
inline void length3(const float* v, float* out) noexcept {
    *out = x3m::scalar::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}
// Relative agreement of a ratio with z.
inline bool ratio_matches(float ratio, float z) noexcept {
    const float d = ratio > z ? ratio - z : z - ratio;
    const float m = z > 1.f ? z : 1.f;
    return d <= ratio_tolerance * m;
}
inline void geometry(const float rows[12], float z, Order pinned, Geometry* g) noexcept {
    *g = Geometry{};
    g->order = pinned;
    for (unsigned i = 0; i < 12; ++i)
        if (!finite_f(rows[i])) return; // match invalid
    g->origin[0] = rows[3];
    g->origin[1] = rows[7];
    g->origin[2] = rows[11];
    const float xa[3] = {rows[0], rows[4], rows[8]}, za[3] = {rows[2], rows[6], rows[10]};
    const float xb[3] = {rows[0], rows[1], rows[2]}, zb[3] = {rows[8], rows[9], rows[10]};
    float lxa, lza, lxb, lzb;
    length3(xa, &lxa);
    length3(za, &lza);
    length3(xb, &lxb);
    length3(zb, &lzb);
    if (!(lxa > 0.f) || !(lza > 0.f) || !(lxb > 0.f) || !(lzb > 0.f) || !finite_f(lxa) || !finite_f(lza) ||
        !finite_f(lxb) || !finite_f(lzb))
        return; // invalid: a degenerate world (no axis to take)
    g->ratio_a = lza / lxa;
    g->ratio_b = lzb / lxb;
    for (unsigned i = 0; i < 3; ++i) {
        g->z_a[i] = za[i] / lza;
        g->z_b[i] = zb[i] / lzb;
    }
    const bool a = ratio_matches(g->ratio_a, z), b = ratio_matches(g->ratio_b, z);
    g->match = a && b ? Match::ambiguous : a ? Match::a : b ? Match::b : Match::mismatch;
    g->order = g->match == Match::a ? Order::a : g->match == Match::b ? Order::b : pinned;
    const bool use_a = g->order == Order::a;
    const float* zu = use_a ? g->z_a : g->z_b;
    for (unsigned i = 0; i < 3; ++i) g->axis[i] = -zu[i];
    g->size = use_a ? lxa : lxb;
    g->ratio = use_a ? g->ratio_a : g->ratio_b;
}

// --------------------------------------------------------------------------- record and ring
enum RecordFlag : std::uint16_t {
    flag_steering = 1u << 0,     // v/00566 (by model id or a SMALLJET table entry): an RCS jet
    flag_brake = 1u << 1,        // z above 2.0 on a main jet: brake (+4.0) or steering (+1.0) bits pushed it
    flag_unknown_body = 1u << 2, // the model is not in the shipped table: body -1, the default cluster
    flag_order_b = 1u << 3,      // the geometry used order b
    flag_order_mismatch = 1u << 4,
    flag_serial = 1u << 5,       // serial is the object_lifetime node serial (else 0)
    flag_effect_pair = 1u << 6,  // the effects pair was bound (c4-6 are its world rows)
    flag_rows_unknown = 1u << 7, // no geometry: not the effects pair, c4-6 not shadowed, or degenerate rows
    flag_additive = 1u << 8,     // DESTBLEND ONE (else the screen law ONE/INVSRCCOLOR or unknown)
    flag_far = 1u << 9,          // a far jet: built from the node the small-parts cull culled (far_record), no draw
};
constexpr unsigned cluster_shift = 12; // bits 12..15: the tint cluster (core::Cluster)
// One recognised glow-jet draw: a per-frame fact, 64 bytes, no pointer.
struct Record {
    std::uint64_t serial;   // object_lifetime node serial, 0 when unknown (flag_serial)
    float origin[3];        // c4-6 translation (the vertex program's world space)
    float axis[3];          // -(model z), unit: the plume direction
    float size;             // |model x|: the base size (LOD-0 value x context scale)
    float s;                // throttle 0..1
    float z;                // raw node+0x88 / 65536 (brake flare beyond 2.0)
    float ratio;            // |z| / |x| of the chosen c4-6 order
    std::uint32_t node_handle; // node+0x28, with model: the identity when the serial is unknown
    std::uint32_t model;    // node+0x140, the body id
    std::int16_t body;      // engine_bodies.json entry, -1 unknown
    std::uint16_t flags;    // RecordFlag | cluster << cluster_shift
    std::uint32_t frame;    // low 32 bits of the proxy frame number
};
static_assert(sizeof(Record) == 64, "the fixed 64-byte record");
constexpr unsigned ring_capacity = 1024;
struct Ring {
    Record records[ring_capacity];
    // Beside each record (the record stays 64 bytes): the object scope's camera handle (0 unknown) and whether the draw
    // came in the scene phase of the scene-boundary selector (the plume stage draws only the scene view's records), and
    // whether the jet node belongs to the own ship (its parent +0x18 is the own ship's root node, or it is the root:
    // the scene view is the camera the own ship's jets were recorded under, engine_plumes_core.h scene_view_camera).
    std::uint32_t camera[ring_capacity];
    std::uint8_t scene[ring_capacity];
    std::uint8_t own[ring_capacity];
    // The jet node's parent, node+0x18: the ship's root node for every engine part (docs/reverse-engineering/
    // engine-effects.md); 0 when unreadable. The own-ship tag and the key of the radius read below.
    std::uint32_t parent[ring_capacity];
    // The ship's radius in the record's units (parent_radius_in_record): the root node's cached subtree radius
    // (+0xa4) x the record's size / its node's x scale; 0 unknown. engine_plumes_core.h build(): the plume floor.
    float parent_radius[ring_capacity];
    unsigned count = 0;
    void clear() noexcept { count = 0; }
    bool full() const noexcept { return count >= ring_capacity; }
    Record* push() noexcept { return count < ring_capacity ? &records[count++] : nullptr; }
};
// The ship's radius for the plume floor (docs/reverse-engineering/engine-effects.md, "Ship radius"): the root node's
// +0xa4, the subtree radius 0x00488170 caches (-1 dirty): the maximum over its children and the three axes of
// |child offset| + the child's own subtree radius, starting from the root's own +0xa0 (the TShips column-0 body's
// value: 47 for body 0). Units: those of node+0x70 (the LOD-0 value), as the jet's own +0x70; the record's size is
// |model x| = +0x70 x (+0x80 / 65536) x the context scale, so the radius in the record's units is
// radius x size / (+0x70 x +0x80 / 65536). 0 when the radius is not positive (dirty, unread) or the scales are not,
// and when it exceeds parent_radius_max_ratio x size: no ship is 10,000 of its own nozzles across (the Mayhem fleet's
// largest main nozzle / R is about 0.09; a capital's smallest jets near 1/2,000, so 2,000 would clip real data), so a larger value is a garbage read
// and takes no floor (counted floor_unknown by the stage) rather than the 4x cap.
constexpr unsigned parent_radius_offset = 0xa4;
constexpr float parent_radius_max_ratio = 10000.f;
inline void parent_radius_in_record(std::int32_t radius, std::uint32_t scale70, std::uint32_t scale80, float size,
                                    float* out) noexcept {
    *out = 0.f;
    if (radius <= 0 || std::int32_t(scale70) <= 0 || std::int32_t(scale80) <= 0 || !finite_f(size) || !(size > 0.f)) return;
    const float base = float(std::int32_t(scale70)) * (float(std::int32_t(scale80)) * (1.f / 65536.f));
    const float r = float(radius) * (size / base);
    if (finite_f(r) && r > 0.f && r <= parent_radius_max_ratio * size) *out = r;
}
// The frame's census counts (engine_frame row).
struct FrameCounts {
    std::uint32_t candidates = 0, not_jet = 0, records = 0, suppressed = 0, unknown_body = 0, steering = 0,
                  rows_unknown = 0;
    std::uint32_t forwarded[forward_reasons]{};
    std::uint32_t match[match_count]{};
};

// --------------------------------------------------------------------------- body table (engine_bodies.json)
// The tint clusters of tools/effects/engine_bodies.py (CLUSTERS, then LEGACY_CLUSTER); an unknown body or a null
// cluster takes default_cluster.
enum Cluster : std::uint8_t {
    darkblue, lightblue, red, green, yellow, orange, magenta, purple, white, peach, lime, cyan, grey, cluster_count
};
constexpr std::uint8_t default_cluster = white;
inline const char* cluster_name(unsigned c) noexcept {
    static const char* const names[cluster_count] = {"darkblue", "lightblue", "red",  "green", "yellow", "orange", "magenta",
                                                     "purple",   "white",     "peach", "lime", "cyan",   "grey"};
    return c < cluster_count ? names[c] : "default";
}
enum Extent : std::uint8_t { extent_none, extent_negative, extent_positive, extent_both };
enum ListBit : std::uint8_t { list_jet = 1, list_smalljet = 2 };
constexpr unsigned body_capacity = 1024, body_name_capacity = 96; // a name of 95 bytes at most (+ NUL)
struct Body {
    char name[body_name_capacity];
    std::int32_t id;           // the fixed id of a `v\NNNNN` token, -1 for a name (dynamic slot, resolved at run time)
    float value;               // LOD-0 value (body units)
    float z_min, z_max;        // visible extent in body units (0 when null)
    float half_width[2];       // x, y (0 when null)
    std::uint8_t cluster;      // Cluster
    std::uint8_t extent;       // Extent
    std::uint8_t lists;        // ListBit
    std::uint8_t colour;       // 1: mean and peak below carry the table's mean_linear / peak_linear (phase 2's tint)
    float mean[3], peak[3];    // linear colours, each divided by its largest channel at parse (white when that is 0)
};
struct BodyTable {
    Body bodies[body_capacity];
    unsigned count = 0;
    unsigned refused = 0; // entries dropped: name too long or not printable ASCII, capacity
};

namespace json {
struct Cursor {
    const char* p;
    const char* end;
    bool ok = true;
    void skip_ws() noexcept {
        while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
    }
    bool accept(char c) noexcept {
        skip_ws();
        if (p < end && *p == c) {
            ++p;
            return true;
        }
        return false;
    }
    bool expect(char c) noexcept {
        if (!accept(c)) ok = false;
        return ok;
    }
    bool literal(const char* word) noexcept {
        skip_ws();
        const std::size_t n = std::strlen(word);
        if (std::size_t(end - p) >= n && !std::memcmp(p, word, n)) {
            p += n;
            return true;
        }
        return false;
    }
    // A string, unescaped into out (cap bytes with the NUL). *fits = false when it does not fit or carries a byte
    // outside printable ASCII or a \u escape beyond 0x7e; the string is consumed either way. false = malformed.
    bool string(char* out, std::size_t cap, bool* fits) noexcept {
        skip_ws();
        *fits = true;
        if (p >= end || *p != '"') return ok = false;
        ++p;
        std::size_t n = 0;
        auto put = [&](unsigned c) {
            if (c < 0x20 || c > 0x7e || n + 1 >= cap) *fits = false;
            else out[n++] = char(c);
        };
        while (p < end && *p != '"') {
            unsigned char c = static_cast<unsigned char>(*p++);
            if (c == '\\') {
                if (p >= end) return ok = false;
                const char e = *p++;
                if (e == '"' || e == '\\' || e == '/') put(unsigned(e));
                else if (e == 'n' || e == 't' || e == 'r' || e == 'b' || e == 'f') *fits = false;
                else if (e == 'u') {
                    if (end - p < 4) return ok = false;
                    unsigned v = 0;
                    for (unsigned i = 0; i < 4; ++i) {
                        const char h = *p++;
                        const unsigned d = h >= '0' && h <= '9'   ? unsigned(h - '0')
                                           : h >= 'a' && h <= 'f' ? unsigned(h - 'a' + 10)
                                           : h >= 'A' && h <= 'F' ? unsigned(h - 'A' + 10)
                                                                  : 16u;
                        if (d > 15) return ok = false;
                        v = v * 16 + d;
                    }
                    put(v);
                } else
                    return ok = false;
            } else if (c < 0x20)
                return ok = false;
            else
                put(c);
        }
        if (p >= end) return ok = false;
        ++p;
        if (cap) out[n < cap ? n : cap - 1] = 0;
        return true;
    }
    bool skip_string() noexcept {
        char scratch[2];
        bool fits;
        return string(scratch, sizeof scratch, &fits);
    }
    // A JSON number into a float (decimal digits, fraction, exponent up to 38); false = not a number.
    bool number(float* out) noexcept {
        skip_ws();
        const char* start = p;
        bool negative = false;
        if (p < end && *p == '-') {
            negative = true;
            ++p;
        }
        float value = 0.f;
        bool digits = false;
        while (p < end && *p >= '0' && *p <= '9') {
            value = value * 10.f + float(*p - '0');
            ++p;
            digits = true;
        }
        if (p < end && *p == '.') {
            ++p;
            float scale = .1f;
            while (p < end && *p >= '0' && *p <= '9') {
                value += float(*p - '0') * scale;
                scale *= .1f;
                ++p;
                digits = true;
            }
        }
        if (digits && p < end && (*p == 'e' || *p == 'E')) {
            ++p;
            bool eneg = false;
            if (p < end && (*p == '-' || *p == '+')) {
                eneg = *p == '-';
                ++p;
            }
            int e = 0;
            bool ed = false;
            while (p < end && *p >= '0' && *p <= '9') {
                e = e * 10 + (*p - '0');
                ++p;
                ed = true;
                if (e > 38) return ok = false;
            }
            if (!ed) return ok = false;
            float f = 1.f;
            for (int i = 0; i < e; ++i) f *= 10.f;
            value = eneg ? value / f : value * f;
        }
        if (!digits || !finite_f(value)) {
            p = start;
            return ok = false;
        }
        *out = negative ? -value : value;
        return true;
    }
    // A number or null (*present = false for null).
    bool number_or_null(float* out, bool* present) noexcept {
        if (literal("null")) {
            *present = false;
            return true;
        }
        *present = true;
        return number(out);
    }
    // Skips one value of any kind (bounded nesting).
    bool skip_value(unsigned depth = 0) noexcept {
        skip_ws();
        if (p >= end || depth > 16) return ok = false;
        if (*p == '"') return skip_string();
        if (*p == '{' || *p == '[') {
            const char close = *p == '{' ? '}' : ']';
            ++p;
            if (accept(close)) return true;
            for (;;) {
                if (close == '}' && (!skip_string() || !expect(':'))) return false;
                if (!skip_value(depth + 1)) return false;
                if (accept(close)) return true;
                if (!expect(',')) return false;
            }
        }
        if (literal("null") || literal("true") || literal("false")) return true;
        float v;
        return number(&v);
    }
};
} // namespace json

inline std::uint8_t cluster_from(const char* s) noexcept {
    for (unsigned c = 0; c < cluster_count; ++c)
        if (!std::strcmp(s, cluster_name(c))) return std::uint8_t(c);
    return default_cluster;
}
inline std::uint8_t extent_from(const char* s) noexcept {
    return std::uint8_t(!std::strcmp(s, "negative") ? extent_negative
                        : !std::strcmp(s, "positive") ? extent_positive
                        : !std::strcmp(s, "both")     ? extent_both
                                                      : extent_none);
}
// A colour: [r, g, b] (three numbers) or null; true with *present when parsed, false on malformed input.
inline bool parse_colour(json::Cursor& c, float out[3], bool* present) noexcept {
    *present = false;
    if (c.literal("null")) return true;
    if (!c.expect('[')) return false;
    for (unsigned i = 0; i < 3; ++i) {
        if (!c.number(&out[i])) return false;
        if (i < 2 && !c.expect(',')) return false;
    }
    if (!c.expect(']')) return false;
    *present = true;
    return true;
}
// Divides a colour by its largest channel (negative or non-finite channels read as 0); an all-zero colour is white.
inline void normalise_colour(float c[3]) noexcept {
    float m = 0.f;
    for (unsigned i = 0; i < 3; ++i) {
        if (!(c[i] > 0.f) || !finite_f(c[i])) c[i] = 0.f;
        if (c[i] > m) m = c[i];
    }
    for (unsigned i = 0; i < 3; ++i) c[i] = m > 0.f ? c[i] / m : 1.f;
}
// One body object: {"id": N|null, "lists": [...], "value": N, "z_min": N|null, "z_max": N|null, "z_extent": "...",
// "half_width": [x, y]|null, "cluster": "..."|null, "mean_linear": [r, g, b]|null, "peak_linear": [r, g, b]|null, ...};
// other fields skipped. The colours count (colour = 1) only when both are present; they are normalised by the caller.
inline bool parse_body(json::Cursor& c, Body* b) noexcept {
    if (!c.expect('{')) return false;
    if (c.accept('}')) return true;
    for (;;) {
        char key[24];
        bool fits;
        if (!c.string(key, sizeof key, &fits) || !c.expect(':')) return false;
        if (!fits) {
            if (!c.skip_value()) return false;
        } else if (!std::strcmp(key, "id")) {
            float v = 0.f;
            bool present;
            if (!c.number_or_null(&v, &present)) return false;
            b->id = present && v >= 0.f && v < 2147483520.f ? std::int32_t(v) : -1;
        } else if (!std::strcmp(key, "value") || !std::strcmp(key, "z_min") || !std::strcmp(key, "z_max")) {
            float v = 0.f;
            bool present;
            if (!c.number_or_null(&v, &present)) return false;
            float* field = key[0] == 'v' ? &b->value : key[2] == 'm' && key[3] == 'i' ? &b->z_min : &b->z_max;
            *field = present ? v : 0.f;
        } else if (!std::strcmp(key, "half_width")) {
            if (!c.literal("null")) {
                if (!c.expect('[')) return false;
                for (unsigned i = 0; i < 2; ++i) {
                    if (!c.number(&b->half_width[i])) return false;
                    if (i == 0 && !c.expect(',')) return false;
                }
                if (!c.expect(']')) return false;
            }
        } else if (!std::strcmp(key, "lists")) {
            if (!c.expect('[')) return false;
            if (!c.accept(']'))
                for (;;) {
                    char list[16];
                    bool list_fits;
                    if (!c.string(list, sizeof list, &list_fits)) return false;
                    if (list_fits && !std::strcmp(list, "jet")) b->lists |= list_jet;
                    if (list_fits && !std::strcmp(list, "smalljet")) b->lists |= list_smalljet;
                    if (c.accept(']')) break;
                    if (!c.expect(',')) return false;
                }
        } else if (!std::strcmp(key, "mean_linear") || !std::strcmp(key, "peak_linear")) {
            bool present = false;
            if (!parse_colour(c, key[0] == 'm' ? b->mean : b->peak, &present)) return false;
            b->colour = std::uint8_t(b->colour | (present ? (key[0] == 'm' ? 1u : 2u) : 0u));
        } else if (!std::strcmp(key, "cluster") || !std::strcmp(key, "z_extent")) {
            if (c.literal("null")) {
                if (key[0] == 'c') b->cluster = default_cluster;
            } else {
                char word[16];
                bool word_fits;
                if (!c.string(word, sizeof word, &word_fits)) return false;
                if (key[0] == 'c') b->cluster = word_fits ? cluster_from(word) : default_cluster;
                else b->extent = word_fits ? extent_from(word) : std::uint8_t(extent_none);
            }
        } else if (!c.skip_value())
            return false;
        if (c.accept('}')) return true;
        if (!c.expect(',')) return false;
    }
}
// Parses engine_bodies.json. true with `count` bodies (0 is valid); false on a malformed document, a schema other than
// 1 or no "bodies" object, with the table cleared (count 0: every record unknown_body, one log row by the caller) and
// *fault the byte offset. An entry whose name does not fit or is not printable ASCII is dropped and counted (refused).
inline bool parse_body_table(const char* text, std::size_t length, BodyTable* out, std::size_t* fault = nullptr) noexcept {
    if (fault) *fault = 0;
    if (!out) return false;
    out->count = out->refused = 0;
    if (!text) return false;
    json::Cursor c{text, text + length};
    auto fail = [&]() {
        if (fault) *fault = std::size_t(c.p - text);
        out->count = out->refused = 0;
        return false;
    };
    bool schema_ok = false, bodies_seen = false;
    if (!c.expect('{')) return fail();
    if (!c.accept('}'))
        for (;;) {
            char key[16];
            bool fits;
            if (!c.string(key, sizeof key, &fits) || !c.expect(':')) return fail();
            if (fits && !std::strcmp(key, "schema")) {
                float v = 0.f;
                if (!c.number(&v)) return fail();
                schema_ok = v == 1.f;
            } else if (fits && !std::strcmp(key, "bodies")) {
                bodies_seen = true;
                if (!c.expect('{')) return fail();
                if (!c.accept('}'))
                    for (;;) {
                        Body b{};
                        b.id = -1;
                        b.cluster = default_cluster;
                        bool name_fits;
                        if (!c.string(b.name, sizeof b.name, &name_fits) || !c.expect(':') || !parse_body(c, &b))
                            return fail();
                        b.colour = b.colour == 3u ? 1u : 0u; // both colours or none
                        normalise_colour(b.mean);
                        normalise_colour(b.peak);
                        if (!name_fits || !b.name[0] || out->count >= body_capacity)
                            ++out->refused;
                        else
                            out->bodies[out->count++] = b;
                        if (c.accept('}')) break;
                        if (!c.expect(',')) return fail();
                    }
            } else if (!c.skip_value())
                return fail();
            if (c.accept('}')) break;
            if (!c.expect(',')) return fail();
        }
    if (!c.ok || !schema_ok || !bodies_seen) return fail();
    c.skip_ws();
    if (c.p != c.end) return fail(); // trailing bytes: not the document the tool wrote
    return true;
}

// --------------------------------------------------------------------------- names -> ids
// ASCII-folded FNV-1a of a name (the engine's _stricmp folding, `\` and `/` distinct).
inline std::uint32_t name_hash(const char* s) noexcept {
    std::uint32_t h = 2166136261u;
    for (; *s; ++s) {
        unsigned char c = static_cast<unsigned char>(*s);
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c + ('a' - 'A'));
        h = (h ^ c) * 16777619u;
    }
    return h;
}
constexpr unsigned index_slots = 2048; // open addressing, at most half full
static_assert(index_slots >= 2 * body_capacity, "load factor");
struct NameIndex {
    std::uint16_t slots[index_slots]; // entry + 1, 0 empty
    void build(const BodyTable& t) noexcept {
        std::memset(slots, 0, sizeof slots);
        for (unsigned i = 0; i < t.count; ++i) {
            if (find(t, t.bodies[i].name) >= 0) continue; // a duplicate under the folding: the first one stands
            for (std::uint32_t h = name_hash(t.bodies[i].name);; ++h)
                if (!slots[h & (index_slots - 1)]) {
                    slots[h & (index_slots - 1)] = std::uint16_t(i + 1);
                    break;
                }
        }
    }
    int find(const BodyTable& t, const char* name) const noexcept {
        for (std::uint32_t h = name_hash(name);; ++h) {
            const unsigned e = slots[h & (index_slots - 1)];
            if (!e) return -1;
            if (lfc::name_equal(t.bodies[e - 1].name, name)) return int(e - 1);
        }
    }
};
// Up to cap bytes at p in page-bounded pieces; false without a NUL inside cap bytes (lfc::read_short_name for any cap).
inline bool read_name(lfc::Reader read, std::uint32_t p, char* out, unsigned cap) noexcept {
    unsigned have = 0;
    while (have < cap) {
        const std::uintptr_t at = std::uintptr_t(p) + have;
        unsigned chunk = unsigned(0x1000 - (at & 0xfff));
        if (chunk > cap - have) chunk = cap - have;
        if (!read(at, out + have, chunk)) return false;
        for (unsigned i = have; i < have + chunk; ++i)
            if (!out[i]) return true;
        have += chunk;
    }
    return false;
}
// The slot's name as the engine prints it (its +0x0c string, or `v\%05d` of its id when null).
inline bool body_slot_name(lfc::Reader read, const lfc::Table& t, std::uint32_t slot, char out[body_name_capacity]) noexcept {
    std::uint32_t p = 0;
    if (!lfc::slot_name_pointer(read, t, slot, &p)) return false;
    if (!p) {
        census::body_default_name(lfc::slot_id(slot, t.fixed), out);
        return true;
    }
    return read_name(read, p, out, body_name_capacity);
}
// The resolution state: id -> entry (the per-draw lookup), the entries found, the dynamic-slot mappings that a game
// load can re-bind (checked round-robin per frame).
struct Resolver {
    std::int16_t id_entry[lfc::id_span]; // -1 unknown
    bool found[body_capacity];
    struct Mapping {
        std::uint32_t slot;
        std::uint16_t entry;
    } dynamic[body_capacity];
    unsigned dynamic_count = 0, check_cursor = 0;
    std::uint32_t resolved = 0, mapped = 0, scanned = 0;
    void clear() noexcept {
        std::memset(id_entry, 0xff, sizeof id_entry);
        std::memset(found, 0, sizeof found);
        dynamic_count = check_cursor = 0;
        resolved = mapped = scanned = 0;
    }
    int entry(std::uint32_t model) const noexcept { return model < lfc::id_span ? id_entry[model] : -1; }
};
// Resolves the entries not found yet: first by the id of a default-form name (`v\00566` is slot 566 when that slot's
// name is null or equal), then by scanning [slot_from, slot_to) by name through the index. Bounded by the reader.
inline void resolve(lfc::Reader read, const lfc::Table& t, const BodyTable& bodies, const NameIndex& index, Resolver* r,
                    std::uint32_t slot_from, std::uint32_t slot_to) noexcept {
    if (!t.valid) return;
    const std::uint32_t slot_count = std::uint32_t(t.fixed) + std::uint32_t(t.dynamic);
    char name[body_name_capacity];
    unsigned missing = 0;
    auto take = [&](unsigned e, std::uint32_t slot, std::int32_t id) {
        r->found[e] = true;
        ++r->resolved;
        if (id >= 0 && std::uint32_t(id) < lfc::id_span) {
            r->id_entry[id] = std::int16_t(e);
            ++r->mapped;
        }
        if (slot >= std::uint32_t(t.fixed) && r->dynamic_count < body_capacity)
            r->dynamic[r->dynamic_count++] = Resolver::Mapping{slot, std::uint16_t(e)};
    };
    for (unsigned e = 0; e < bodies.count; ++e) {
        if (r->found[e]) continue;
        std::int32_t id = 0;
        std::uint32_t slot = 0;
        if (lfc::default_name_id(bodies.bodies[e].name, &id) && census::body_slot(id, t.fixed, t.dynamic, &slot) &&
            body_slot_name(read, t, slot, name) && lfc::name_equal(name, bodies.bodies[e].name))
            take(e, slot, id);
        else
            ++missing;
    }
    if (slot_to > slot_count) slot_to = slot_count;
    for (std::uint32_t slot = slot_from; missing && slot < slot_to; ++slot) {
        std::uint32_t p = 0;
        if (!lfc::slot_name_pointer(read, t, slot, &p) || !p || !read_name(read, p, name, body_name_capacity)) continue;
        ++r->scanned;
        const int e = index.find(bodies, name);
        if (e < 0 || r->found[e]) continue;
        take(unsigned(e), slot, lfc::slot_id(slot, t.fixed));
        --missing;
    }
}
// Up to `budget` dynamic mappings re-read from the cursor on (round-robin): false when one lies beyond the table, is
// unreadable or carries another name (the caller restarts the resolution).
inline bool mappings_hold(lfc::Reader read, const lfc::Table& t, const BodyTable& bodies, Resolver* r,
                          unsigned budget) noexcept {
    char name[body_name_capacity];
    const std::uint32_t slot_count = std::uint32_t(t.fixed) + std::uint32_t(t.dynamic);
    for (unsigned n = 0; n < budget && n < r->dynamic_count; ++n) {
        if (r->check_cursor >= r->dynamic_count) r->check_cursor = 0;
        const auto& m = r->dynamic[r->check_cursor++];
        if (m.slot >= slot_count || !body_slot_name(read, t, m.slot, name) || !lfc::name_equal(name, bodies.bodies[m.entry].name))
            return false;
    }
    return true;
}

// --------------------------------------------------------------------------- one record
// Fills a record from the throttle word, the c4-6 rows (null when not the effect pair or not shadowed), the identity
// and the table entry; counts the geometry match in `counts`. `pinned` is updated by a decisive match.
struct RecordInput {
    std::uint32_t scale3 = 0, model = 0, node_handle = 0;
    std::uint64_t serial = 0; // 0 unknown
    const float* rows = nullptr;
    bool pair = false, additive = false;
    int body = -1;
    const Body* entry = nullptr;
    std::uint64_t frame = 0;
};
inline void fill_record(const RecordInput& in, Order* pinned, Record* r, Geometry* g, FrameCounts* counts) noexcept {
    float z, s;
    throttle(in.scale3, &z, &s);
    std::uint16_t flags = 0;
    *g = Geometry{};
    g->order = *pinned;
    if (in.rows) geometry(in.rows, z, *pinned, g);
    if (g->match == Match::a || g->match == Match::b) *pinned = g->order;
    if (g->match == Match::invalid) flags |= flag_rows_unknown;
    if (g->match == Match::mismatch) flags |= flag_order_mismatch;
    if (g->order == Order::b && g->match != Match::invalid) flags |= flag_order_b;
    if (counts) {
        ++counts->match[unsigned(g->match)];
        if (g->match == Match::invalid) ++counts->rows_unknown;
    }
    const bool steering = in.model == steering_model || (in.entry && (in.entry->lists & list_smalljet));
    if (steering) flags |= flag_steering;
    else if (z > 2.f + ratio_tolerance) flags |= flag_brake;
    if (!in.entry) flags |= flag_unknown_body;
    if (in.serial) flags |= flag_serial;
    if (in.pair) flags |= flag_effect_pair;
    if (in.additive) flags |= flag_additive;
    const unsigned cluster = in.entry ? in.entry->cluster : default_cluster;
    flags |= std::uint16_t((cluster & 15u) << cluster_shift);
    r->serial = in.serial;
    for (unsigned i = 0; i < 3; ++i) {
        r->origin[i] = g->match == Match::invalid ? 0.f : g->origin[i];
        r->axis[i] = g->match == Match::invalid ? 0.f : g->axis[i];
    }
    r->size = g->match == Match::invalid ? 0.f : g->size;
    r->ratio = g->match == Match::invalid ? 0.f : g->ratio;
    r->s = s;
    r->z = z;
    r->node_handle = in.node_handle;
    r->model = in.model;
    r->body = std::int16_t(in.entry ? in.body : -1);
    r->flags = flags;
    r->frame = std::uint32_t(in.frame);
}
// --------------------------------------------------------------------------- far jets
// A far jet's record (engine_far_jets_core.h Raw: the node fields the small-parts cull stub's far block copied for a JET
// node it culled; docs/architecture/engine-effects-modern.md "After flight E") in the draw path's terms, from the same
// construction the engine's world rows use (0x004bdee0, docs/reverse-engineering/engine-effects.md section 4): row k of
// the 3x3 is basis row k (+0xc0 / +0xd0 / +0xe0, 16.16) x +0x70 x (+0x80, +0x84, +0x88)[k] / 65536 x the context scale,
// the translation +0xb0 x the context scale; c4-6 order a (flight A: 117,442 records, model z = basis row 2 at cos 1.0)
// reads model axis k from row k. So origin = +0xb0 x scale, axis = -(basis row 2) normalised, size = |model x| = |basis
// row 0| / 65536 x +0x70 x +0x80 / 65536 x scale, ratio = |z| / |x| = (|row 2| x +0x88) / (|row 0| x +0x80), z and s from
// +0x88 (throttle). The origin is computed in double (the engine rounds pos x scale once). `context_scale` is the
// view's float at context +0x2c (0.01 in every gameplay view). steering: an RCS jet (v/00566 or a SMALLJET table entry,
// the recogniser's rule; the stub's handler already skips 566); invalid: a context scale outside (0, 1), a zero basis
// row or a non-positive scale: no record.
enum class FarVerdict : std::uint8_t { record, steering, invalid };
inline FarVerdict far_record(const x3m::engine_far_jets::core::Raw& raw, float context_scale, int body, const Body* entry,
                             std::uint64_t frame, Record* r) noexcept {
    if (raw.model == steering_model || (entry && (entry->lists & list_smalljet))) return FarVerdict::steering;
    if (!finite_f(context_scale) || !(context_scale > 0.f) || !(context_scale < 1.f) || std::int32_t(raw.scale70) <= 0 ||
        std::int32_t(raw.scale80) <= 0)
        return FarVerdict::invalid;
    float bx[3], bz[3];
    for (unsigned i = 0; i < 3; ++i) {
        bx[i] = float(raw.basis_x[i]) * (1.f / 65536.f);
        bz[i] = float(raw.basis_z[i]) * (1.f / 65536.f);
    }
    float lx = 0.f, lz = 0.f;
    length3(bx, &lx);
    length3(bz, &lz);
    if (!(lx > 0.f) || !(lz > 0.f) || !finite_f(lx) || !finite_f(lz)) return FarVerdict::invalid;
    float z = 0.f, s = 0.f;
    throttle(raw.scale88, &z, &s);
    const float sx = float(std::int32_t(raw.scale80)) * (1.f / 65536.f);
    *r = Record{};
    for (unsigned i = 0; i < 3; ++i) {
        r->origin[i] = float(double(raw.position[i]) * double(context_scale));
        r->axis[i] = -bz[i] / lz;
    }
    r->size = lx * float(std::int32_t(raw.scale70)) * sx * context_scale;
    r->ratio = (lz * z) / (lx * sx);
    if (!finite_f(r->size) || !(r->size > 0.f)) return FarVerdict::invalid;
    r->s = s;
    r->z = z;
    r->node_handle = raw.handle;
    r->model = raw.model;
    r->body = std::int16_t(entry ? body : -1);
    std::uint16_t flags = flag_far;
    if (z > 2.f + ratio_tolerance) flags |= flag_brake;
    if (!entry) flags |= flag_unknown_body;
    const unsigned cluster = entry ? entry->cluster : default_cluster;
    flags |= std::uint16_t((cluster & 15u) << cluster_shift);
    r->flags = flags;
    r->serial = 0;
    r->frame = std::uint32_t(frame);
    return FarVerdict::record;
}
} // namespace x3m::engine_effects::core
