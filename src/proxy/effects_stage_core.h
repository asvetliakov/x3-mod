#pragma once
#include "sse_scalar.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

// Effects stage, phase 1 (docs/architecture/effects-modernisation-opus.md,
// sections 2, 3.1, 3.3, 8 and 9): the CPU side of the proxy-owned effects
// stage. Records taken from recognised game effect draws, the upload-time
// texture key (the sparse content hash the ownership layer computes at the
// first level-0 Unlock and the offline tool mirrors), the shipped key table
// (effect_keys.json, a flat JSON document parsed here without allocation),
// the arming rule with its 64-frame disarm after a failed stage, the bolt
// instances derived from the locked-prefix copy of the bullet buffer with
// their nearest-centroid velocity match, the shield-hit association with the
// ellipsoid boxes and the four-slot policy, the node-age map and the vertex
// data of the stage's draws.
//
// Free of Windows and D3D; single precision only, no <cmath> calls (the MinGW
// inlines of fabs/floor are x87; sse_scalar.h and the bit tricks below are
// SSE), nothing here allocates. Host-tested by
// verification/analysis/test_effects_stage_core.py through a compiled harness.
namespace x3m::effects_stage {

constexpr unsigned max_records = 256;      // records per frame; overflow draws natively and is counted
constexpr unsigned max_bolts = 256;        // bolt instances per frame (bolt_footprint::max_instances)
constexpr unsigned max_hits = 64;          // shield-hit records per frame
constexpr unsigned max_shells = 16;        // hit ships drawn per frame
constexpr unsigned hit_slots = 4;          // active hits per ship (section 3.3)
constexpr unsigned max_boxes = 64;         // owner boxes the association searches
constexpr unsigned disarm_frames = 64;     // frames the stage stays off after a failure (section 2.1)
constexpr unsigned age_map_capacity = 512; // node-age map (open addressing, power of two)
constexpr unsigned key_table_capacity = 96;
constexpr float default_min_width_px = 3.f;    // W_min in render pixels (bolt-footprint rule)
constexpr float default_min_length_px = 12.f;  // L_min at 1080 lines
constexpr float default_stretch = 1.f;         // k_stretch: the quad covers the full previous position
constexpr float default_core_intensity = 6.f;  // I_core, engine units (a few units above E 1)
constexpr float default_halo_intensity = 1.5f; // I_halo before the 0.37-0.66 resolve cut
constexpr float default_halo_width = 3.f;      // halo radius as a multiple of the core radius
constexpr float default_soft = 0.5f;           // SOFT: soft radius as a multiple of the effect radius
constexpr float default_match_eps = 0.35f;     // perpendicular tolerance as a fraction of the travel
constexpr float default_match_s_max = 4000.f;  // world units per frame the match will follow (~1,000 m/s at 4 fps)
constexpr float shell_inflate = 1.2f;          // the ellipsoid: box half-extents x 1.2
constexpr float shell_owner_radii = 1.5f;      // no owner within 1.5 radii: the ripple decal
constexpr float shell_ripple_seconds = 0.6f;   // the ripple timeline
constexpr float shell_flash_seconds = 0.1f;
constexpr std::uint32_t age_expire_frames = 120; // an entry not seen for this long is dropped
constexpr float w_epsilon = 1e-3f;

#define X3M_ES_INLINE inline __attribute__((always_inline))
X3M_ES_INLINE float fabs_f(float x) noexcept {
    std::uint32_t bits; std::memcpy(&bits, &x, sizeof bits);
    bits &= 0x7fffffffu;
    std::memcpy(&x, &bits, sizeof x);
    return x;
}
X3M_ES_INLINE bool finite_f(float x) noexcept { return fabs_f(x) <= 3.4028235e38f; }
X3M_ES_INLINE float dot3(const float* a, const float* b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
X3M_ES_INLINE void cross3(const float* a, const float* b, float* out) noexcept {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
X3M_ES_INLINE float length3(const float* a) noexcept { return scalar::sqrt(dot3(a, a)); }
X3M_ES_INLINE float min_f(float a, float b) noexcept { return a < b ? a : b; }
X3M_ES_INLINE float max_f(float a, float b) noexcept { return a > b ? a : b; }
X3M_ES_INLINE float clamp_f(float x, float lo, float hi) noexcept { return x < lo ? lo : x > hi ? hi : x; }

// ---------------------------------------------------------------------------
// The upload-time texture key (section 8.2): FNV-1a 64 over the level-0
// width, height and D3DFORMAT value, then 16 runs of at most 256 bytes taken
// at fixed rows and columns of the level-0 image. Rows are block rows for the
// DXT formats (the caller passes rows and row_bytes as the format dictates,
// see level0_layout); the pitch is the mapping's. Row k of 16 is
// (k * rows) / 16, its column start (k * 2654435761 mod 2^32) mod (row_bytes -
// run + 1), so two images that differ only in bytes outside the runs share a
// key: about 4 KB per texture is enough to tell archive textures apart.
// tools/effects/effect_keys.py computes the same key from the archive DDS.
constexpr std::uint64_t fnv_offset = 0xcbf29ce484222325ull, fnv_prime = 0x100000001b3ull;
constexpr unsigned key_runs = 16, key_run_bytes = 256;
X3M_ES_INLINE std::uint64_t fnv_bytes(std::uint64_t h, const unsigned char* p, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= fnv_prime; }
    return h;
}
X3M_ES_INLINE std::uint64_t fnv_u32(std::uint64_t h, std::uint32_t v) noexcept {
    unsigned char b[4] = {(unsigned char)(v & 255u), (unsigned char)((v >> 8) & 255u), (unsigned char)((v >> 16) & 255u), (unsigned char)((v >> 24) & 255u)};
    return fnv_bytes(h, b, 4);
}
// D3DFORMAT values the key layout understands (the numeric D3DFORMAT enum
// and the DXT FOURCCs), kept here so the host test compiles without d3d9.h.
constexpr std::uint32_t fmt_a8r8g8b8 = 21, fmt_x8r8g8b8 = 22, fmt_r5g6b5 = 23, fmt_a1r5g5b5 = 25, fmt_a4r4g4b4 = 26, fmt_a8 = 28, fmt_l8 = 50, fmt_a8l8 = 51;
constexpr std::uint32_t fmt_dxt1 = 0x31545844u, fmt_dxt2 = 0x32545844u, fmt_dxt3 = 0x33545844u, fmt_dxt4 = 0x34545844u, fmt_dxt5 = 0x35545844u;
// Rows and bytes per row of a level-0 image; false for a format the key does
// not cover (the texture then has no key and every draw with it stays native).
inline bool level0_layout(std::uint32_t width, std::uint32_t height, std::uint32_t format, std::uint32_t* rows, std::uint32_t* row_bytes) noexcept {
    if (!width || !height || !rows || !row_bytes) return false;
    switch (format) {
    case fmt_dxt1: *rows = (height + 3u) / 4u; *row_bytes = ((width + 3u) / 4u) * 8u; return true;
    case fmt_dxt2: case fmt_dxt3: case fmt_dxt4: case fmt_dxt5: *rows = (height + 3u) / 4u; *row_bytes = ((width + 3u) / 4u) * 16u; return true;
    case fmt_a8r8g8b8: case fmt_x8r8g8b8: *rows = height; *row_bytes = width * 4u; return true;
    case fmt_r5g6b5: case fmt_a1r5g5b5: case fmt_a4r4g4b4: case fmt_a8l8: *rows = height; *row_bytes = width * 2u; return true;
    case fmt_a8: case fmt_l8: *rows = height; *row_bytes = width; return true;
    default: return false;
    }
}
// The key of a mapped level 0 (base + row * pitch). Never reads outside
// rows x row_bytes; pitch >= row_bytes is the caller's contract.
inline std::uint64_t sparse_key(std::uint32_t width, std::uint32_t height, std::uint32_t format, std::uint32_t rows, std::uint32_t row_bytes,
                                const unsigned char* base, std::uint32_t pitch) noexcept {
    std::uint64_t h = fnv_offset;
    h = fnv_u32(h, width); h = fnv_u32(h, height); h = fnv_u32(h, format);
    if (!base || !rows || !row_bytes) return h;
    const std::uint32_t run = row_bytes < key_run_bytes ? row_bytes : key_run_bytes;
    const std::uint32_t span = row_bytes - run + 1u;
    for (std::uint32_t k = 0; k < key_runs; ++k) {
        const std::uint32_t row = std::uint32_t((std::uint64_t(k) * rows) / key_runs);
        const std::uint32_t column = std::uint32_t((k * 2654435761u) % span);
        h = fnv_bytes(h, base + std::size_t(row) * pitch + column, run);
    }
    return h;
}

// ---------------------------------------------------------------------------
// Effect classes and the key table.
enum class Class : unsigned char { Unknown = 0, Bolt = 1, ShieldHit = 2, HullHit = 3, Explosion = 4, Beam = 5, EngineGlow = 6, Count = 7 };
inline const char* class_name(Class c) noexcept {
    static constexpr const char* names[] = {"unknown", "bolt", "shield_hit", "hull_hit", "explosion", "beam", "engine_glow"};
    const unsigned i = unsigned(c);
    return i < unsigned(Class::Count) ? names[i] : "invalid";
}
inline Class class_from_name(const char* name, std::size_t n) noexcept {
    for (unsigned i = 1; i < unsigned(Class::Count); ++i) {
        const char* c = class_name(Class(i));
        if (std::strlen(c) == n && !std::memcmp(c, name, n)) return Class(i);
    }
    return Class::Unknown;
}
struct KeyEntry {
    std::uint64_t key = 0;
    Class cls = Class::Unknown;
    float extent[3]{};   // the body's local half-extent (world units at scale 1)
    float axis[3]{};     // the body's local axis
    float tint[3]{1.f, 1.f, 1.f};
    char name[40]{};
};
struct KeyTable {
    KeyEntry entries[key_table_capacity]{};
    unsigned count = 0;
    unsigned duplicates = 0, refused = 0; // parse statistics
    const KeyEntry* find(std::uint64_t key) const noexcept {
        if (!key) return nullptr;
        for (unsigned i = 0; i < count; ++i) if (entries[i].key == key) return entries + i;
        return nullptr;
    }
    bool add(const KeyEntry& e) noexcept {
        if (!e.key || e.cls == Class::Unknown) { ++refused; return false; }
        if (find(e.key)) { ++duplicates; return false; }
        if (count >= key_table_capacity) { ++refused; return false; }
        entries[count++] = e;
        return true;
    }
};

// A minimal JSON reader for the flat table the tool writes:
// {"schema":1,"hash":"sparse16x256-fnv1a64","entries":[{"key":"0123456789abcdef","class":"shield_hit",
//  "name":"exp_PL_imp_diff","extent":[1,1,0],"axis":[0,0,1],"tint":[1,0.8,0.6]}, ...]}
// Unknown fields are skipped; a malformed document yields what parsed before
// the fault and reports it. No recursion beyond the depth the table needs.
namespace json {
struct Cursor {
    const char* p; const char* end;
    bool ok = true;
    bool at_end() const noexcept { return p >= end; }
    void skip_ws() noexcept { while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p; }
    bool accept(char c) noexcept { skip_ws(); if (p < end && *p == c) { ++p; return true; } return false; }
    bool expect(char c) noexcept { if (!accept(c)) ok = false; return ok; }
    // A string's bytes without unescaping (keys and names carry no escapes).
    bool string(const char** s, std::size_t* n) noexcept {
        skip_ws();
        if (p >= end || *p != '"') { ok = false; return false; }
        const char* start = ++p;
        while (p < end && *p != '"') { if (*p == '\\' && p + 1 < end) ++p; ++p; }
        if (p >= end) { ok = false; return false; }
        *s = start; *n = std::size_t(p - start); ++p;
        return true;
    }
    bool number(float* out) noexcept {
        skip_ws();
        const char* start = p;
        bool negative = false;
        if (p < end && (*p == '-' || *p == '+')) { negative = *p == '-'; ++p; }
        float value = 0.f; bool digits = false;
        while (p < end && *p >= '0' && *p <= '9') { value = value * 10.f + float(*p - '0'); ++p; digits = true; }
        if (p < end && *p == '.') { ++p; float scale = 0.1f; while (p < end && *p >= '0' && *p <= '9') { value += float(*p - '0') * scale; scale *= 0.1f; ++p; digits = true; } }
        if (p < end && (*p == 'e' || *p == 'E')) {
            ++p; bool eneg = false; if (p < end && (*p == '-' || *p == '+')) { eneg = *p == '-'; ++p; }
            int e = 0; bool ed = false; while (p < end && *p >= '0' && *p <= '9') { e = e * 10 + (*p - '0'); ++p; ed = true; if (e > 60) break; }
            if (!ed) { ok = false; return false; }
            float f = 1.f; for (int i = 0; i < e; ++i) f *= 10.f;
            value = eneg ? value / f : value * f;
        }
        if (!digits) { p = start; ok = false; return false; }
        *out = negative ? -value : value;
        return true;
    }
    // Skips one value of any kind (bounded nesting).
    bool skip_value(unsigned depth = 0) noexcept {
        skip_ws();
        if (p >= end || depth > 8) { ok = false; return false; }
        if (*p == '"') { const char* s; std::size_t n; return string(&s, &n); }
        if (*p == '{' || *p == '[') {
            const char close = *p == '{' ? '}' : ']'; ++p;
            if (accept(close)) return true;
            for (;;) {
                if (close == '}') { const char* s; std::size_t n; if (!string(&s, &n) || !expect(':')) return false; }
                if (!skip_value(depth + 1)) return false;
                if (accept(close)) return true;
                if (!expect(',')) return false;
            }
        }
        while (p < end && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') ++p;
        return true;
    }
    bool vector3(float* out) noexcept {
        if (!expect('[')) return false;
        for (unsigned i = 0; i < 3; ++i) { float v = 0.f; if (!number(&v)) return false; if (!finite_f(v)) { ok = false; return false; } out[i] = v; if (i < 2 && !expect(',')) return false; }
        return expect(']');
    }
};
inline bool parse_hex64(const char* s, std::size_t n, std::uint64_t* out) noexcept {
    if (n != 16) return false;
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const char c = s[i]; unsigned d;
        if (c >= '0' && c <= '9') d = unsigned(c - '0'); else if (c >= 'a' && c <= 'f') d = unsigned(c - 'a' + 10); else if (c >= 'A' && c <= 'F') d = unsigned(c - 'A' + 10); else return false;
        v = (v << 4) | d;
    }
    *out = v; return true;
}
} // namespace json
// Parses the table; returns false on a malformed document (entries parsed
// before the fault are kept, *fault names the position of the fault).
inline bool parse_key_table(const char* text, std::size_t length, KeyTable* out, std::size_t* fault = nullptr) noexcept {
    if (fault) *fault = 0;
    if (!out) return false;
    *out = KeyTable{};
    if (!text) return false;
    json::Cursor c{text, text + length};
    // A malformed document keeps nothing (fail closed: every effect draw stays native), and names the fault offset.
    auto fail = [&]() { if (fault) *fault = std::size_t(c.p - text); *out = KeyTable{}; return false; };
    if (!c.expect('{')) return fail();
    if (c.accept('}')) return true;
    bool schema_ok = false;
    for (;;) {
        const char* name; std::size_t n;
        if (!c.string(&name, &n) || !c.expect(':')) return fail();
        if (n == 6 && !std::memcmp(name, "schema", 6)) { float v = 0.f; if (!c.number(&v)) return fail(); schema_ok = v == 1.f; }
        else if (n == 7 && !std::memcmp(name, "entries", 7)) {
            if (!c.expect('[')) return fail();
            if (!c.accept(']')) for (;;) {
                if (!c.expect('{')) return fail();
                KeyEntry e{}; bool have_key = false;
                if (!c.accept('}')) for (;;) {
                    const char* f; std::size_t fn;
                    if (!c.string(&f, &fn) || !c.expect(':')) return fail();
                    if (fn == 3 && !std::memcmp(f, "key", 3)) { const char* s; std::size_t sn; if (!c.string(&s, &sn)) return fail(); have_key = json::parse_hex64(s, sn, &e.key); }
                    else if (fn == 5 && !std::memcmp(f, "class", 5)) { const char* s; std::size_t sn; if (!c.string(&s, &sn)) return fail(); e.cls = class_from_name(s, sn); }
                    else if (fn == 4 && !std::memcmp(f, "name", 4)) { const char* s; std::size_t sn; if (!c.string(&s, &sn)) return fail(); const std::size_t m = sn < sizeof e.name - 1 ? sn : sizeof e.name - 1; std::memcpy(e.name, s, m); e.name[m] = '\0'; }
                    else if (fn == 6 && !std::memcmp(f, "extent", 6)) { if (!c.vector3(e.extent)) return fail(); }
                    else if (fn == 4 && !std::memcmp(f, "axis", 4)) { if (!c.vector3(e.axis)) return fail(); }
                    else if (fn == 4 && !std::memcmp(f, "tint", 4)) { if (!c.vector3(e.tint)) return fail(); }
                    else if (!c.skip_value()) return fail();
                    if (c.accept('}')) break;
                    if (!c.expect(',')) return fail();
                }
                if (!have_key) e.key = 0;
                out->add(e);
                if (c.accept(']')) break;
                if (!c.expect(',')) return fail();
            }
        } else if (!c.skip_value()) return fail();
        if (c.accept('}')) break;
        if (!c.expect(',')) return fail();
    }
    if (!schema_ok) return fail();
    c.skip_ws();
    if (!c.at_end()) return fail(); // trailing bytes after the document: not the table the tool wrote
    return true;
}

// ---------------------------------------------------------------------------
// Arming (section 2.1): the stage takes records only while armed; a failed
// stage disarms it for disarm_frames frames. `resources`, `caps` and `hdr`
// are the caller's prerequisites of this frame.
struct Arming {
    std::uint64_t disarmed_until = 0; // frame number below which the stage is off
    std::uint32_t failures = 0;
    bool armed(std::uint64_t frame, bool resources, bool caps, bool hdr) const noexcept {
        return resources && caps && hdr && frame >= disarmed_until;
    }
    void fail(std::uint64_t frame) noexcept { disarmed_until = frame + disarm_frames; ++failures; }
    void reset() noexcept { disarmed_until = 0; }
};

// ---------------------------------------------------------------------------
// Records (section 2.1). A record is one recognised effect draw of this
// frame; bolts carry their instances separately (BoltInstance).
struct Record {
    Class cls = Class::Unknown;
    std::uint64_t key = 0;
    std::uint64_t identity = 0; // node serial when scoped, else the spatial identity
    float origin[3]{};          // world origin (c4-6 .w)
    float scale = 1.f;          // row norm of the world rows
    float alpha = 1.f;
    float tint[3]{1.f, 1.f, 1.f};
    std::uint32_t primitives = 0;
    bool scoped = false;
};
// The world rows of the DEFAULT effect VS: c4-6, each a row (m0 m1 m2 t).
// origin = the translation column, scale = the mean row norm; false when any
// value is not finite or the scale is zero.
inline bool world_rows_origin(const float rows[12], float* origin, float* scale) noexcept {
    for (unsigned i = 0; i < 12; ++i) if (!finite_f(rows[i])) return false;
    origin[0] = rows[3]; origin[1] = rows[7]; origin[2] = rows[11];
    float s = 0.f;
    for (unsigned r = 0; r < 3; ++r) s += length3(rows + r * 4);
    s *= (1.f / 3.f);
    if (!(s > 0.f) || !finite_f(s)) return false;
    *scale = s;
    return true;
}
// The object -> world rows of a routed draw from its clip rows (WVP in the
// column-vector form of bolt_footprint: clip_x = rows[0..3] . p, clip_w =
// rows[12..15] . p) and the camera latch: the view rows through P^-1 (view_z
// = the w row; view_x = (x row - m20 w row) / m00; view_y = (y row - m21 w
// row) / m11), then world_k = sum_j wv[k*3+j] (view_j - t_j) with the camera's
// world-from-view basis. false when m00 or m11 is not positive or a value is
// not finite.
inline bool object_to_world_from_clip(const float clip[16], float m00, float m11, float m20, float m21, const float wv[9], const float t[3], float out[12]) noexcept {
    if (!clip || !wv || !t || !out || !(m00 > 0.f) || !(m11 > 0.f) || !finite_f(m00) || !finite_f(m11) || !finite_f(m20) || !finite_f(m21)) return false;
    for (unsigned i = 0; i < 16; ++i) if (!finite_f(clip[i])) return false;
    float view[12];
    for (unsigned i = 0; i < 4; ++i) {
        view[i] = (clip[i] - m20 * clip[12 + i]) / m00;
        view[4 + i] = (clip[4 + i] - m21 * clip[12 + i]) / m11;
        view[8 + i] = clip[12 + i];
    }
    for (unsigned k = 0; k < 3; ++k) {
        for (unsigned i = 0; i < 3; ++i) out[k * 4 + i] = wv[k * 3] * view[i] + wv[k * 3 + 1] * view[4 + i] + wv[k * 3 + 2] * view[8 + i];
        out[k * 4 + 3] = wv[k * 3] * (view[3] - t[0]) + wv[k * 3 + 1] * (view[7] - t[1]) + wv[k * 3 + 2] * (view[11] - t[2]);
    }
    for (unsigned i = 0; i < 12; ++i) if (!finite_f(out[i])) return false;
    return true;
}
// The spatial identity of an unscoped record: the key with the origin
// quantised to `cell` world units, so a sprite that stays in place across
// frames keeps its age.
inline std::uint64_t spatial_identity(std::uint64_t key, const float* origin, float cell) noexcept {
    std::uint64_t h = fnv_offset ^ key;
    for (unsigned i = 0; i < 3; ++i) {
        const float q = origin[i] / (cell > 0.f ? cell : 1.f);
        const float r = q >= 0.f ? q + 0.5f : q - 0.5f;
        const std::int32_t c = r > 2147483520.f ? 2147483520 : r < -2147483520.f ? -2147483520 : std::int32_t(r);
        h = fnv_u32(h, std::uint32_t(c));
    }
    return h ? h : 1u;
}

// ---------------------------------------------------------------------------
// The node-age map (section 2.5): identity -> first frame seen, last frame
// seen. Open addressing over a power-of-two table; an entry not seen for
// age_expire_frames is reclaimed by the next insertion that probes it.
struct AgeEntry { std::uint64_t identity = 0; std::uint64_t first = 0, last = 0; std::uint64_t key = 0; };
struct AgeMap {
    AgeEntry entries[age_map_capacity]{};
    unsigned used = 0;
    static_assert((age_map_capacity & (age_map_capacity - 1)) == 0, "power of two");
    static unsigned slot_of(std::uint64_t identity) noexcept { return unsigned((identity * 0x9e3779b97f4a7c15ull) >> 55) & (age_map_capacity - 1); }
    // The entry's first frame; inserted at `frame` when new. 0 when the map is full.
    std::uint64_t touch(std::uint64_t identity, std::uint64_t key, std::uint64_t frame) noexcept {
        if (!identity) return 0;
        unsigned i = slot_of(identity);
        for (unsigned probe = 0; probe < age_map_capacity; ++probe, i = (i + 1) & (age_map_capacity - 1)) {
            AgeEntry& e = entries[i];
            const bool expired = e.identity && frame > e.last && frame - e.last > age_expire_frames;
            if (e.identity == identity && !expired) { e.last = frame; return e.first; }
            if (!e.identity || expired) { // free, or an entry not seen for its fade time (its own identity starts a new age)
                if (!e.identity) ++used;
                e = AgeEntry{identity, frame, frame, key};
                return frame;
            }
        }
        return 0;
    }
    void clear() noexcept { for (auto& e : entries) e = AgeEntry{}; used = 0; }
};

// ---------------------------------------------------------------------------
// Bolt instances (section 3.1). Derived from the locked-prefix copy of the
// drawn bullet buffer (positions: 3 floats per vertex; extras: the UV and
// colour words) grouped by the period bolt_footprint::detect_period found.
struct BoltInstance {
    float centre[3]{};      // world centroid
    float axis[3]{};        // unit world axis (the elongation direction)
    float half_length = 0;  // world half-extent along the axis
    float half_width = 0;   // world half-extent across it
    float uv[2]{};          // the instance's UV centroid (tint sample)
    float alpha = 1.f;      // vertex colour alpha
    float velocity[3]{};    // world units per frame when matched
    std::uint32_t period = 0;
    bool matched = false;
};
// The major eigenvector of the 3x3 covariance (c = xx, xy, xz, yy, yz, zz) by
// power iteration; false when the covariance is zero or not finite.
X3M_ES_INLINE bool major_axis(const float* c, float* v) noexcept {
    const float m[9] = {c[0], c[1], c[2], c[1], c[3], c[4], c[2], c[4], c[5]};
    unsigned k = 0;
    if (c[3] > c[0]) k = 1;
    if (c[5] > m[k * 4]) k = 2;
    float x[3] = {m[k], m[3 + k], m[6 + k]};
    for (unsigned it = 0; it < 16; ++it) {
        const float y[3] = {dot3(m, x), dot3(m + 3, x), dot3(m + 6, x)};
        const float yy = dot3(y, y);
        if (!(yy > 1e-30f) || !finite_f(yy)) return false;
        const float inv = 1.f / scalar::sqrt(yy);
        x[0] = y[0] * inv; x[1] = y[1] * inv; x[2] = y[2] * inv;
    }
    v[0] = x[0]; v[1] = x[1]; v[2] = x[2];
    return true;
}
// One instance from its `period` vertices. false: a non-finite vertex.
inline bool derive_instance(const float* positions, const std::uint32_t* extras, std::uint32_t period, BoltInstance* out) noexcept {
    *out = BoltInstance{};
    if (!positions || !extras || period < 3) return false;
    float c[3] = {0.f, 0.f, 0.f}, uv[2] = {0.f, 0.f};
    std::uint32_t alpha_sum = 0;
    for (std::uint32_t v = 0; v < period; ++v) {
        const float* p = positions + std::size_t(v) * 3u;
        for (unsigned i = 0; i < 3; ++i) { if (!finite_f(p[i])) return false; c[i] += p[i]; }
        float u, w; std::memcpy(&u, extras + std::size_t(v) * 3u, 4); std::memcpy(&w, extras + std::size_t(v) * 3u + 1, 4);
        if (finite_f(u) && finite_f(w)) { uv[0] += u; uv[1] += w; }
        alpha_sum += extras[std::size_t(v) * 3u + 2] >> 24;
    }
    const float inv = 1.f / float(period);
    for (unsigned i = 0; i < 3; ++i) c[i] *= inv;
    out->uv[0] = uv[0] * inv; out->uv[1] = uv[1] * inv;
    out->alpha = float(alpha_sum) * inv * (1.f / 255.f);
    out->period = period;
    std::memcpy(out->centre, c, sizeof c);
    float cov[6] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    for (std::uint32_t v = 0; v < period; ++v) {
        const float* p = positions + std::size_t(v) * 3u;
        const float d[3] = {p[0] - c[0], p[1] - c[1], p[2] - c[2]};
        cov[0] += d[0] * d[0]; cov[1] += d[0] * d[1]; cov[2] += d[0] * d[2]; cov[3] += d[1] * d[1]; cov[4] += d[1] * d[2]; cov[5] += d[2] * d[2];
    }
    float axis[3];
    if (!major_axis(cov, axis)) { out->axis[0] = 1.f; out->half_length = out->half_width = 0.f; return true; }
    std::memcpy(out->axis, axis, sizeof axis);
    float along = 0.f, across = 0.f;
    for (std::uint32_t v = 0; v < period; ++v) {
        const float* p = positions + std::size_t(v) * 3u;
        const float d[3] = {p[0] - c[0], p[1] - c[1], p[2] - c[2]};
        const float a = dot3(d, axis);
        const float perp[3] = {d[0] - a * axis[0], d[1] - a * axis[1], d[2] - a * axis[2]};
        along = max_f(along, fabs_f(a)); across = max_f(across, length3(perp));
    }
    out->half_length = along; out->half_width = across;
    return finite_f(along) && finite_f(across);
}
// Every instance of a drawn prefix of `count` vertices with the period p.
// Returns the instances written (at most capacity); instances that fail
// derive_instance are skipped and counted in *refused.
inline unsigned derive_instances(const float* positions, const std::uint32_t* extras, std::uint32_t count, std::uint32_t period,
                                 BoltInstance* out, unsigned capacity, unsigned* refused) noexcept {
    if (refused) *refused = 0;
    if (!positions || !extras || !out || !period || count % period) return 0;
    const std::uint32_t instances = count / period;
    unsigned n = 0;
    for (std::uint32_t i = 0; i < instances && n < capacity; ++i) {
        if (derive_instance(positions + std::size_t(i) * period * 3u, extras + std::size_t(i) * period * 3u, period, out + n)) ++n;
        else if (refused) ++*refused;
    }
    return n;
}
// Velocity by nearest-centroid matching against the previous frame's
// instances on the same UV period (section 3.1): the match is the previous
// centroid nearest on the backward axis line c - s a, s in (0, s_max], with a
// perpendicular distance below eps x s (eps the fraction of the travel). It is
// refused when the two nearest candidates are closer than 2 |v| (ambiguous
// spacing), when a cut occurred, or when the draw has no period. The
// matched instance takes velocity = c - c_prev.
struct MatchStats { unsigned instances = 0, matched = 0, refused_ambiguous = 0, refused_none = 0, refused_cut = 0; float median_speed = 0.f; };
inline void match_instances(const BoltInstance* previous, unsigned previous_count, BoltInstance* current, unsigned current_count,
                            float s_max, float eps, bool cut, MatchStats* stats) noexcept {
    MatchStats s{}; s.instances = current_count;
    float speeds[max_bolts]; unsigned speed_count = 0;
    for (unsigned i = 0; i < current_count; ++i) {
        BoltInstance& c = current[i];
        c.matched = false; c.velocity[0] = c.velocity[1] = c.velocity[2] = 0.f;
        if (cut) { ++s.refused_cut; continue; }
        if (!c.period || !previous) { ++s.refused_none; continue; }
        float best = 3.4e38f, second = 3.4e38f; int best_index = -1;
        for (unsigned j = 0; j < previous_count; ++j) {
            const BoltInstance& p = previous[j];
            if (p.period != c.period) continue;
            const float d[3] = {p.centre[0] - c.centre[0], p.centre[1] - c.centre[1], p.centre[2] - c.centre[2]};
            const float along = -dot3(d, c.axis); // s: how far behind the current centre along the axis (either sign of the axis)
            const float s_abs = fabs_f(along);
            if (!(s_abs > 0.f) || s_abs > s_max) continue;
            const float perp[3] = {d[0] + along * c.axis[0], d[1] + along * c.axis[1], d[2] + along * c.axis[2]};
            if (length3(perp) > eps * s_abs) continue;
            if (s_abs < best) { second = best; best = s_abs; best_index = int(j); }
            else if (s_abs < second) second = s_abs;
        }
        if (best_index < 0) { ++s.refused_none; continue; }
        if (second < 3.4e38f && second - best < 2.f * best) { ++s.refused_ambiguous; continue; }
        const BoltInstance& p = previous[unsigned(best_index)];
        for (unsigned k = 0; k < 3; ++k) c.velocity[k] = c.centre[k] - p.centre[k];
        c.matched = true; ++s.matched;
        if (speed_count < max_bolts) speeds[speed_count++] = best;
    }
    // The median speed by insertion sort (n <= 256, firing frames only).
    for (unsigned i = 1; i < speed_count; ++i) { const float v = speeds[i]; unsigned j = i; while (j > 0 && speeds[j - 1] > v) { speeds[j] = speeds[j - 1]; --j; } speeds[j] = v; }
    s.median_speed = speed_count ? speeds[speed_count / 2] : 0.f;
    if (stats) *stats = s;
}

// The bolt draw's vertices (stride 64, four per instance): the vertex program
// builds the camera-facing streak from the instance data and the corner.
//   POSITION  float4: centre.xyz, uv.y
//   TEXCOORD0 float4: axis.xyz, half_length
//   TEXCOORD1 float4: half_width, corner.x (-1/1 along), corner.y (-1/1 across), alpha
//   TEXCOORD2 float4: velocity.xyz (world per frame), uv.x
struct BoltVertex { float centre[3], uv_y; float axis[3], half_length; float half_width, corner_x, corner_y, alpha; float velocity[3], uv_x; };
static_assert(sizeof(BoltVertex) == 64, "stride 64");
constexpr unsigned bolt_vertices_per_instance = 4, bolt_primitives_per_instance = 2;
// Writes 4 vertices per instance (TRIANGLELIST through the shared quad index
// buffer: 0 1 2, 2 1 3). Returns the instances written.
inline unsigned write_bolt_vertices(const BoltInstance* instances, unsigned count, BoltVertex* out, unsigned capacity) noexcept {
    if (!instances || !out) return 0;
    unsigned n = 0;
    static constexpr float corners[4][2] = {{-1.f, -1.f}, {1.f, -1.f}, {-1.f, 1.f}, {1.f, 1.f}};
    for (unsigned i = 0; i < count && n < capacity; ++i) {
        const BoltInstance& b = instances[i];
        for (unsigned k = 0; k < 4; ++k) {
            BoltVertex& v = out[n * 4 + k];
            std::memcpy(v.centre, b.centre, sizeof v.centre); v.uv_y = b.uv[1];
            std::memcpy(v.axis, b.axis, sizeof v.axis); v.half_length = b.half_length;
            v.half_width = b.half_width; v.corner_x = corners[k][0]; v.corner_y = corners[k][1]; v.alpha = b.alpha;
            std::memcpy(v.velocity, b.velocity, sizeof v.velocity); v.uv_x = b.uv[0];
        }
        ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Shield hits (section 3.3). Owner boxes come from the caster-candidate route
// (object-space half-extents plus object -> world rows) or, without rows,
// from a node radius. The ellipsoid is the box half-extents x shell_inflate
// through its rows; the association picks the owner whose ellipsoid-normalised
// distance to the hit point is smallest and below shell_owner_radii.
struct OwnerBox {
    std::uint64_t identity = 0; // node serial or another stable id
    float rows[12]{};           // object -> world: world = R p + t, rows (r0 t0)(r1 t1)(r2 t2)
    float half[3]{};            // object-space half-extents
    float radius = 0.f;         // sphere fallback (rows_valid false)
    bool rows_valid = false;
};
enum class HitKind : unsigned char { None = 0, Shell = 1, Sphere = 2, Decal = 3 };
struct Association { HitKind kind = HitKind::None; int index = -1; float distance = 3.4e38f; float local[3]{}; };
// The normalised distance |E^-1 (P - c)| of P to the box's inflated ellipsoid
// and the unit-sphere direction of P (the local direction the shell's hit slot
// stores). false when the box is degenerate.
inline bool ellipsoid_distance(const OwnerBox& box, const float* P, float* distance, float* local) noexcept {
    if (box.rows_valid) {
        const float c[3] = {box.rows[3], box.rows[7], box.rows[11]};
        const float d[3] = {P[0] - c[0], P[1] - c[1], P[2] - c[2]};
        // Object axes: columns of R scaled by the inflated half-extents; the
        // rows are orthogonal up to the engine's fixed-point error, so the
        // object coordinate is d . column / |column|^2.
        float q[3];
        for (unsigned k = 0; k < 3; ++k) {
            const float col[3] = {box.rows[k], box.rows[4 + k], box.rows[8 + k]};
            const float cc = dot3(col, col), h = box.half[k] * shell_inflate;
            if (!(cc > 1e-20f) || !(h > 0.f)) return false;
            q[k] = dot3(d, col) / (cc * h);
        }
        const float n = length3(q);
        if (!finite_f(n)) return false;
        *distance = n;
        const float inv = n > 1e-12f ? 1.f / n : 0.f;
        for (unsigned k = 0; k < 3; ++k) local[k] = q[k] * inv;
        return true;
    }
    if (!(box.radius > 0.f)) return false;
    const float c[3] = {box.rows[3], box.rows[7], box.rows[11]};
    const float d[3] = {P[0] - c[0], P[1] - c[1], P[2] - c[2]};
    const float n = length3(d);
    if (!finite_f(n)) return false;
    *distance = n / (box.radius * shell_inflate);
    const float inv = n > 1e-12f ? 1.f / n : 0.f;
    for (unsigned k = 0; k < 3; ++k) local[k] = d[k] * inv;
    return true;
}
inline Association associate_hit(const float* P, const OwnerBox* boxes, unsigned count) noexcept {
    Association a{};
    if (!P || !boxes) { a.kind = HitKind::Decal; return a; }
    for (unsigned i = 0; i < count; ++i) {
        float d, local[3];
        if (!ellipsoid_distance(boxes[i], P, &d, local)) continue;
        if (d < a.distance) { a.distance = d; a.index = int(i); std::memcpy(a.local, local, sizeof a.local); a.kind = boxes[i].rows_valid ? HitKind::Shell : HitKind::Sphere; }
    }
    if (a.index < 0 || a.distance > shell_owner_radii) { a.kind = HitKind::Decal; a.index = -1; }
    return a;
}
// The four hit slots of one ship: a new hit takes an empty slot, else the
// oldest; the same identity refreshes its slot.
struct HitSlot { std::uint64_t identity = 0; float local[3]{}; std::uint64_t first_frame = 0; bool live = false; };
struct ShipHits {
    std::uint64_t owner = 0;
    HitSlot slots[hit_slots]{};
    float tint[3]{1.f, 1.f, 1.f}; // the last recorded hit's tint (kept on frames without a new record)
    unsigned live() const noexcept { unsigned n = 0; for (const auto& s : slots) n += s.live; return n; }
    // Returns the slot index taken.
    unsigned insert(std::uint64_t identity, const float* local, std::uint64_t first_frame) noexcept {
        for (unsigned i = 0; i < hit_slots; ++i) if (slots[i].live && slots[i].identity == identity) { std::memcpy(slots[i].local, local, 12); return i; }
        unsigned pick = hit_slots;
        for (unsigned i = 0; i < hit_slots; ++i) if (!slots[i].live) { pick = i; break; }
        if (pick == hit_slots) { pick = 0; for (unsigned i = 1; i < hit_slots; ++i) if (slots[i].first_frame < slots[pick].first_frame) pick = i; }
        slots[pick] = HitSlot{identity, {local[0], local[1], local[2]}, first_frame, true};
        return pick;
    }
    // Drops slots older than `frames` at `frame`.
    void expire(std::uint64_t frame, std::uint64_t frames) noexcept { for (auto& s : slots) if (s.live && frame >= s.first_frame + frames) s.live = false; }
};

// The shell draw's per-ship constants (uploaded as vertex/pixel constants) and
// the decal vertices (stride 32: POSITION float4 world P + radius, TEXCOORD0
// float4 corner.xy, age, alpha).
struct ShellInstance {
    std::uint64_t owner = 0;
    float centre[3]{};
    float axes[9]{};      // world axes of the inflated ellipsoid (columns scaled)
    float tint[3]{1.f, 1.f, 1.f};
    float hits[hit_slots][4]{}; // local direction xyz, age in seconds
    unsigned hit_count = 0;
    float alpha = 1.f;
};
struct DecalVertex { float p[3], radius; float corner_x, corner_y, age, alpha; };
static_assert(sizeof(DecalVertex) == 32, "stride 32");
inline bool shell_from_box(const OwnerBox& box, ShellInstance* out) noexcept {
    if (!box.rows_valid) {
        if (!(box.radius > 0.f)) return false;
        *out = ShellInstance{}; out->owner = box.identity;
        out->centre[0] = box.rows[3]; out->centre[1] = box.rows[7]; out->centre[2] = box.rows[11];
        const float r = box.radius * shell_inflate;
        out->axes[0] = r; out->axes[4] = r; out->axes[8] = r;
        return true;
    }
    *out = ShellInstance{}; out->owner = box.identity;
    out->centre[0] = box.rows[3]; out->centre[1] = box.rows[7]; out->centre[2] = box.rows[11];
    for (unsigned k = 0; k < 3; ++k) {
        const float h = box.half[k] * shell_inflate;
        if (!(h > 0.f)) return false;
        for (unsigned r = 0; r < 3; ++r) out->axes[k * 3 + r] = box.rows[r * 4 + k] * h; // column k of R scaled: world axis k
    }
    return true;
}
inline void write_decal_vertices(const float* P, float radius, float age, float alpha, DecalVertex* out) noexcept {
    static constexpr float corners[4][2] = {{-1.f, -1.f}, {1.f, -1.f}, {-1.f, 1.f}, {1.f, 1.f}};
    for (unsigned k = 0; k < 4; ++k) { out[k] = DecalVertex{{P[0], P[1], P[2]}, radius, corners[k][0], corners[k][1], age, alpha}; }
}

// ---------------------------------------------------------------------------
// Per-frame counters of the stage (the effects_stage_frame row).
struct FrameCounters {
    std::uint32_t recognised = 0;   // effect-pair draws seen
    std::uint32_t recorded = 0;     // records taken (the native draw goes through as well: phase 1 suppresses nothing)
    std::uint32_t forwarded = 0;    // recognised draws not recorded (disarmed, overflow, view gate, unknown class)
    std::uint32_t unknown_keys = 0; // effect-pair draws whose stage-0 texture has no key or no table entry
    std::uint32_t overflow = 0;
    std::uint32_t bolts = 0, shells = 0, decals = 0; // drawn by the stage
    std::uint32_t bolt_draws = 0;   // bullet draws recorded
    std::uint32_t hits = 0;         // hit records
    void clear() noexcept { *this = FrameCounters{}; }
};

#undef X3M_ES_INLINE
} // namespace x3m::effects_stage
