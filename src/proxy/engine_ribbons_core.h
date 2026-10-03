#pragma once
#include <cstddef>
#include <cstdint>
#include "sse_scalar.h"
#include "engine_effects_core.h"
#include "engine_plumes_core.h"
#include "../renderer/fog_transmittance.h"

// Portable core of the engine ribbons, phase 3 (docs/architecture/engine-effects-modern.md sections 3-5): the ring
// buffers of nozzle positions, the length law and the CPU builder of the stage's second indexed draw. No Windows
// dependency (the host tests compile it); no x87 (SSE scalars, no float returned by value from an out-of-line
// function).
//
// Pool: 256 ribbons (cap; a new nozzle at the cap takes the slot of the oldest fading ribbon, evicted early; with
// none fading it draws its plume only and counts as overflow), each 16 samples
// of world position + half-width + time, keyed by the record's identity (the object_lifetime node serial when the
// record carries one, else node handle + model) through a 512-slot open-addressed map rebuilt at each update.
// - Distance sampling: a sample is appended when the nozzle moved at least
//   max(1.5 m, 0.02 value, L / 15) since the newest sample (1.5 m = 7.5 render units at 5 units per metre: the fog
//   look's and the motes' scale, fog-dust-motes.md; L the ribbon's target length, so the 16 samples always cover it).
//   A frame's displacement never enters except through that distance, so SETA advances the samples and stretches
//   nothing.
// - Length: L = T(value) x preset x v_est x s, T 0.5 s below value 2,000, 0.8 s to 20,000, 1.2 s above; v_est is the
//   path length from the nozzle back through the samples of the last 0.1 s or more (never across a gap of more than
//   0.25 s between samples, the newest sample always) over their age.
// - Lifetime: a ribbon whose record is missing fades out over 0.3 s at its last positions, then is evicted; every
//   update first evicts the ribbons not seen for 0.3 s, so a stage gap of 0.3 s (a load screen, a disarm) empties the
//   pool by itself. Cleared whole on the resolve's camera cut, on a change of the object_lifetime load epoch and on
//   Reset (the pass's before_reset); a head that moved more than 8 value since the last update restarts its ribbon (a
//   jump with an unchanged identity: no bridge).
// View: the plumes' scene-view filter (engine_plumes_core.h ViewFilter) applies to the records first: a record of
// another view (a target monitor) takes no ribbon and does not keep one alive.
// Depth: every strip point carries its own centre-line view z (shape[1]), the per-vertex form of the plumes' corrected
// nearest-axis depth (a ribbon point lies on its own centre line, so no axis offset applies).
// Geometry: a camera-facing strip through the nozzle (the live head, so there is never a gap at the nozzle) and the
// samples, newest first, cut at L (the last point interpolated); half-width 0.6 x the nozzle's half-width (0.5 value)
// at the nozzle tapering linearly to 0 at the tail, never under 1.5 px (a 3 px strip); radiance
// I_ribbon(s) = lerp(0.2, 0.9, s) x preset x tint x (1 - u) (u 0 at the nozzle, 1 at the tail) x the plumes' distance
// law (after flight E: engine_plumes_core.h distance_weight on the plume's projected nozzle width at the head, the
// plume's nozzle being Look::nozzle_width x its floored value); the plume's near-camera
// rule per point (the projected width held to 0.12 H, radiance 1 -> 0.5 over the last 20 %); the nozzle's fog
// transmittance on the colour. Occlusion in the pixel program: the plume halo's soft lane test (SOFT 1.0 value) at the
// centre line's view depth.
namespace x3m::engine_ribbons {
namespace ee = x3m::engine_effects::core;
namespace ep = x3m::engine_plumes;

constexpr unsigned max_ribbons = 256;
constexpr unsigned samples_per_ribbon = 16;
constexpr unsigned map_slots = 512;           // power of two, 2 x the pool
constexpr unsigned points_per_ribbon = samples_per_ribbon + 1; // the head and the samples
constexpr unsigned vertices_per_ribbon = 2 * points_per_ribbon; // 34
constexpr unsigned triangles_per_ribbon = 2 * (points_per_ribbon - 1); // 32
constexpr unsigned indices_per_ribbon = 3 * triangles_per_ribbon;      // 96
constexpr unsigned max_vertices = max_ribbons * vertices_per_ribbon;   // 8,704: 16-bit indices
static_assert(max_vertices <= 65536u, "16-bit indices");
constexpr float units_per_metre = 5.f;       // 5 render units = 1 m (the fog look's 5,000 per km)
constexpr float min_spacing = 1.5f * units_per_metre;
constexpr float spacing_value = .02f;        // x value
constexpr float fade_seconds = .3f;
constexpr float jump_value = 8.f;            // x value per update: a jump, not flight
constexpr float speed_window = .1f, speed_gap = .25f; // seconds
constexpr float width_scale = .6f;           // x the nozzle's half-width (0.5 value)
constexpr float nozzle_half_width = .5f;     // x value
constexpr float min_width_px = 3.f;          // the strip's full projected width floor
constexpr float radiance_low = .2f, radiance_high = .9f; // I_ribbon(s) = lerp(0.2, 0.9, s)
constexpr float soft = 1.f;                  // SOFT x value (the halo's)
constexpr float cull_length_px = 3.f;        // a ribbon projecting shorter is not drawn
constexpr float value_short = 2000.f, value_long = 20000.f; // T's value thresholds
constexpr float t_short = .5f, t_mid = .8f, t_long = 1.2f;  // seconds
constexpr float time_rebase = 1024.f;        // seconds: the pool's clock is re-based past this (float precision)

// T(value) in seconds.
inline void trail_seconds(float value, float* out) noexcept {
    *out = value < value_short ? t_short : value <= value_long ? t_mid : t_long;
}
// The identity: bit 63 set for a node serial, clear for node handle + model (never 0: 0 marks a free map slot).
inline std::uint64_t record_key(const ee::Record& r) noexcept {
    if ((r.flags & ee::flag_serial) && r.serial) return r.serial | (std::uint64_t(1) << 63);
    const std::uint64_t k = (std::uint64_t(r.model & 0x7fffffffu) << 32) | r.node_handle;
    return k ? k : 1u;
}
inline unsigned key_slot(std::uint64_t key) noexcept {
    return ep::hash32(std::uint32_t(key) ^ ep::hash32(std::uint32_t(key >> 32))) & (map_slots - 1);
}

struct Sample {
    float p[3];       // world position (the record's c4-6 origin)
    float half_width; // world units, at the nozzle when appended
    float time;       // seconds on the pool's clock
};
struct Ribbon {
    std::uint64_t key = 0;
    Sample samples[samples_per_ribbon]{};
    unsigned newest = 0, count = 0; // ring: samples[newest] is the latest, count valid going back
    float head[3]{};                // the nozzle at its last update
    float last_seen = 0.f;          // pool clock
    float value = 0.f, throttle = 0.f;
    float nozzle = 0.f;              // the plume's nozzle width (world): Look::nozzle_width x the floored value
    float speed = 0.f, length = 0.f; // v_est (units / s) and the target length at the last update
    float tint[3]{1.f, 1.f, 1.f};    // the record's mean colour (largest channel 1)
    std::uint32_t seen = 0;          // update serial of the last record
    bool live = false;
    const Sample& at(unsigned back) const noexcept {
        return samples[(newest + samples_per_ribbon - back) % samples_per_ribbon];
    }
};
struct UpdateStats {
    unsigned records = 0, matched = 0, created = 0, overflow = 0, duplicates = 0, skipped = 0, skipped_other_view = 0;
    unsigned appended = 0, evicted = 0, jumps = 0, live = 0, fading = 0;
    bool cut_clear = false, load_clear = false;
};
struct Pool {
    Ribbon ribbons[max_ribbons];
    std::uint16_t map[map_slots]{}; // ribbon index + 1, 0 free
    double origin = 0.;             // the clock's origin (caller seconds)
    bool clock_set = false;
    std::uint64_t load_epoch = 0;
    bool load_epoch_set = false;
    std::uint32_t serial = 0;
    unsigned live = 0;
    // Session totals (census).
    std::uint64_t evictions = 0, cut_clears = 0, load_clears = 0, reset_clears = 0, overflows = 0, jumps = 0;
    void clear() noexcept {
        for (auto& r : ribbons) {
            r.live = false;
            r.count = 0;
            r.key = 0;
        }
        for (auto& m : map) m = 0;
        live = 0;
        clock_set = false;
    }
};

namespace detail {
inline void distance(const float a[3], const float b[3], float* out) noexcept {
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    *out = x3m::scalar::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}
inline bool finite3(const float* v) noexcept {
    return ee::finite_f(v[0]) && ee::finite_f(v[1]) && ee::finite_f(v[2]);
}
inline void rebuild_map(Pool& pool) noexcept {
    for (auto& m : pool.map) m = 0;
    for (unsigned i = 0; i < max_ribbons; ++i) {
        if (!pool.ribbons[i].live) continue;
        unsigned slot = key_slot(pool.ribbons[i].key);
        while (pool.map[slot]) slot = (slot + 1) & (map_slots - 1);
        pool.map[slot] = std::uint16_t(i + 1);
    }
}
inline int find(const Pool& pool, std::uint64_t key) noexcept {
    unsigned slot = key_slot(key);
    for (unsigned probe = 0; probe < map_slots && pool.map[slot]; ++probe, slot = (slot + 1) & (map_slots - 1))
        if (pool.ribbons[pool.map[slot] - 1].key == key) return int(pool.map[slot] - 1);
    return -1;
}
inline void insert(Pool& pool, std::uint64_t key, unsigned index) noexcept {
    unsigned slot = key_slot(key);
    while (pool.map[slot]) slot = (slot + 1) & (map_slots - 1);
    pool.map[slot] = std::uint16_t(index + 1);
}
// Removes ribbon `index` from the map (linear probing: backward-shift deletion keeps every other key findable).
inline void erase(Pool& pool, unsigned index) noexcept {
    constexpr unsigned mask = map_slots - 1;
    unsigned hole = key_slot(pool.ribbons[index].key);
    for (unsigned probe = 0; probe < map_slots && pool.map[hole] != index + 1; ++probe) hole = (hole + 1) & mask;
    if (pool.map[hole] != index + 1) return;
    pool.map[hole] = 0;
    for (unsigned next = (hole + 1) & mask; pool.map[next]; next = (next + 1) & mask) {
        const unsigned home = key_slot(pool.ribbons[pool.map[next] - 1].key);
        // The entry at `next` may fill the hole when its home is not cyclically within (hole, next].
        const bool stays = hole <= next ? (hole < home && home <= next) : (hole < home || home <= next);
        if (stays) continue;
        pool.map[hole] = pool.map[next];
        pool.map[next] = 0;
        hole = next;
    }
}
// The fading ribbon (live, not seen in this update) seen longest ago, or -1.
inline int oldest_fading(const Pool& pool) noexcept {
    int best = -1;
    for (unsigned i = 0; i < max_ribbons; ++i) {
        const Ribbon& r = pool.ribbons[i];
        if (r.live && r.seen != pool.serial && (best < 0 || r.last_seen < pool.ribbons[best].last_seen)) best = int(i);
    }
    return best;
}
} // namespace detail

// v_est: the path from `head` (at time `now`) back through the samples, over their age (0 without a sample older
// than now).
inline void estimate_speed(const Ribbon& r, const float head[3], float now, float* out) noexcept {
    float path = 0.f, span = 0.f, previous_time = now;
    const float* previous = head;
    for (unsigned back = 0; back < r.count; ++back) {
        const Sample& s = r.at(back);
        if (back > 0 && previous_time - s.time > speed_gap) break; // a pause: the older samples are another motion
        float d = 0.f;
        detail::distance(previous, s.p, &d);
        path += d;
        span = now - s.time;
        previous = s.p;
        previous_time = s.time;
        if (span >= speed_window) break;
    }
    *out = span > 1e-4f ? path / span : 0.f;
}

// The frame's records into the pool. `now` in seconds (any epoch, monotonic), `cut` the resolve's camera cut,
// `load_epoch` the object_lifetime load epoch (0 when unknown: never a change), `preset_scale` 0.6 / 1 / 1.5 on T,
// `filter` (null: every record) the plumes' scene-view filter: a record of another view takes no ribbon; `look` (null:
// default_look) and `radii` (null: no floor) the plumes' look and ship radii, for the plume's nozzle width (the
// distance law's input).
using BodyLookup = ep::BodyLookup;
inline void update(Pool& pool, const ee::Record* records, unsigned count, double now_seconds, bool cut,
                   std::uint64_t load_epoch, BodyLookup body, float preset_scale, UpdateStats* stats,
                   const ep::ViewFilter* filter = nullptr, const ep::Look* look = nullptr,
                   const float* radii = nullptr) noexcept {
    const ep::Look& k = look ? *look : ep::default_look;
    UpdateStats local{};
    UpdateStats& st = stats ? *stats : local;
    st = UpdateStats{};
    if (cut) {
        if (pool.live) {
            st.cut_clear = true;
            ++pool.cut_clears;
        }
        pool.clear();
    }
    if (load_epoch) {
        if (pool.load_epoch_set && load_epoch != pool.load_epoch) {
            if (pool.live) {
                st.load_clear = true;
                ++pool.load_clears;
            }
            pool.clear();
        }
        pool.load_epoch = load_epoch;
        pool.load_epoch_set = true;
    }
    if (pool.clock_set && !(now_seconds >= pool.origin)) pool.clear(); // the clock went back: start over
    if (!pool.clock_set) {
        pool.origin = now_seconds;
        pool.clock_set = true;
    }
    float now = float(now_seconds - pool.origin);
    if (now > time_rebase) {
        // Re-base the clock (float precision): every live time moves with it; a ribbon older than the fade is evicted
        // below anyway.
        const float shift = now - 16.f;
        pool.origin += double(shift);
        now -= shift;
        for (auto& r : pool.ribbons) {
            if (!r.live) continue;
            r.last_seen -= shift;
            for (auto& s : r.samples) s.time -= shift;
        }
    }
    ++pool.serial;
    // Eviction first: a ribbon unseen for the fade is gone before any record can match it.
    for (auto& r : pool.ribbons)
        if (r.live && now - r.last_seen >= fade_seconds) {
            r.live = false;
            r.count = 0;
            --pool.live;
            ++st.evicted;
            ++pool.evictions;
        }
    detail::rebuild_map(pool);
    unsigned free_hint = 0;
    bool no_fading = false;
    for (unsigned i = 0; i < count && records; ++i) {
        const ee::Record& rec = records[i];
        ++st.records;
        if (filter && (!filter->scene[i] || filter->camera[i] != filter->handle)) {
            ++st.skipped_other_view;
            continue;
        }
        if ((rec.flags & (ee::flag_rows_unknown | ee::flag_steering)) || !detail::finite3(rec.origin) ||
            !ee::finite_f(rec.size) || !(rec.size > 0.f) || !ee::finite_f(rec.s)) {
            ++st.skipped;
            continue;
        }
        const std::uint64_t key = record_key(rec);
        int index = detail::find(pool, key);
        if (index >= 0 && pool.ribbons[index].seen == pool.serial) {
            ++st.duplicates; // two records of one identity in a frame: the first owns the ribbon
            continue;
        }
        if (index < 0) {
            while (free_hint < max_ribbons && pool.ribbons[free_hint].live) ++free_hint;
            if (free_hint < max_ribbons)
                index = int(free_hint);
            else {
                // Full: a live nozzle takes the slot of the oldest fading ribbon (evicted early) before it is refused.
                index = no_fading ? -1 : detail::oldest_fading(pool);
                if (index < 0) {
                    no_fading = true; // nothing fades this update: later identities are refused without a scan
                    ++st.overflow;
                    ++pool.overflows;
                    continue;
                }
                detail::erase(pool, unsigned(index));
                pool.ribbons[index].live = false;
                pool.ribbons[index].count = 0;
                --pool.live;
                ++st.evicted;
                ++pool.evictions;
            }
            Ribbon& fresh = pool.ribbons[index];
            fresh.key = key;
            fresh.count = 0;
            fresh.newest = 0;
            fresh.live = true;
            fresh.speed = fresh.length = 0.f;
            ++pool.live;
            detail::insert(pool, key, unsigned(index));
            ++st.created;
        } else
            ++st.matched;
        Ribbon& r = pool.ribbons[index];
        r.seen = pool.serial;
        const float value = rec.size;
        if (r.count) {
            float moved = 0.f;
            detail::distance(rec.origin, r.head, &moved);
            if (moved > jump_value * value) {
                r.count = 0; // a jump: the old samples would bridge it
                ++st.jumps;
                ++pool.jumps;
            }
        }
        r.value = value;
        float plume_value = value;
        if (radii && k.floor_scale > 0.f) ep::floored_value(k, rec, radii[i], &plume_value);
        r.nozzle = k.nozzle_width * plume_value;
        r.throttle = rec.s < 0.f ? 0.f : rec.s > 1.f ? 1.f : rec.s;
        float speed = 0.f;
        estimate_speed(r, rec.origin, now, &speed);
        float t = 0.f;
        trail_seconds(value, &t);
        const float length = t * preset_scale * speed * r.throttle;
        float spacing = spacing_value * value;
        spacing = spacing > min_spacing ? spacing : min_spacing;
        spacing = length / float(samples_per_ribbon - 1) > spacing ? length / float(samples_per_ribbon - 1) : spacing;
        bool append = r.count == 0;
        if (!append) {
            float moved = 0.f;
            detail::distance(rec.origin, r.at(0).p, &moved);
            append = moved >= spacing;
        }
        if (append) {
            r.newest = r.count ? (r.newest + 1) % samples_per_ribbon : 0;
            r.count = r.count < samples_per_ribbon ? r.count + 1 : samples_per_ribbon;
            Sample& s = r.samples[r.newest];
            for (unsigned j = 0; j < 3; ++j) s.p[j] = rec.origin[j];
            s.half_width = width_scale * nozzle_half_width * value;
            s.time = now;
            ++st.appended;
        }
        for (unsigned j = 0; j < 3; ++j) r.head[j] = rec.origin[j];
        r.last_seen = now;
        r.speed = speed;
        r.length = length;
        float mean[3], peak[3];
        const ee::Body* b = body && rec.body >= 0 ? body(rec.body) : nullptr;
        ep::record_tint(rec, b, mean, peak);
        for (unsigned j = 0; j < 3; ++j) r.tint[j] = mean[j];
    }
    for (const auto& r : pool.ribbons)
        if (r.live && r.seen != pool.serial) ++st.fading;
    st.live = pool.live;
}

// ----------------------------------------------------------------------------------------------- the strip's vertices
// 48 bytes (FLOAT3, 2 x FLOAT4, D3DCOLOR).
struct Vertex {
    float position[3]; // view space
    float strip[4];    // u (0 nozzle .. 1 tail), across (-1 .. 1), unused x2
    float shape[4];    // radiance (I x preset x (1 - u) x fade x near weight), centre-line view depth, value, unused
    std::uint32_t tint; // 0xAARRGGBB of the mean colour x the fog transmittance
};
static_assert(sizeof(Vertex) == 48, "the ribbon stride");
struct BuildStats {
    unsigned ribbons = 0, points = 0, samples = 0, vertices = 0;
    unsigned fading = 0, capped = 0, floored = 0, culled_short = 0, culled_behind = 0, culled_capacity = 0;
    unsigned fogged = 0;
    unsigned far_ribbons = 0; // ribbons whose radiance the distance law scaled
};
// The static index list: per ribbon 16 segments x 2 triangles over its 34 vertices (point k: vertices 2k, 2k + 1).
inline void write_indices(std::uint16_t* out, unsigned ribbons) noexcept {
    for (unsigned r = 0; r < ribbons; ++r)
        for (unsigned k = 0; k + 1 < points_per_ribbon; ++k) {
            const std::uint16_t b = std::uint16_t(r * vertices_per_ribbon + 2 * k);
            std::uint16_t* o = out + (r * (points_per_ribbon - 1) + k) * 6;
            o[0] = b;
            o[1] = std::uint16_t(b + 1);
            o[2] = std::uint16_t(b + 2);
            o[3] = std::uint16_t(b + 2);
            o[4] = std::uint16_t(b + 1);
            o[5] = std::uint16_t(b + 3);
        }
}

namespace detail {
inline void cross(const float a[3], const float b[3], float out[3]) noexcept {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
inline void normalise(float d[3], float* length) noexcept {
    const float n = x3m::scalar::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    *length = n;
    if (n > 0.f)
        for (unsigned i = 0; i < 3; ++i) d[i] /= n;
}
inline void to_view(const ep::View& v, const float p[3], float out[3]) noexcept {
    for (unsigned j = 0; j < 3; ++j)
        out[j] = p[0] * v.rows[j * 4] + p[1] * v.rows[j * 4 + 1] + p[2] * v.rows[j * 4 + 2] + v.rows[j * 4 + 3];
}
} // namespace detail

// One ribbon's 34 vertices (`seen`: its record was in the latest update, else it fades); false: not drawn.
inline bool build_ribbon(const Ribbon& r, bool seen, float now, const ep::View& view, float preset_scale, Vertex* out,
                         BuildStats* stats, const ep::Look& look = ep::default_look) noexcept {
    float fade = 1.f;
    if (!seen) {
        fade = 1.f - (now - r.last_seen) * (1.f / fade_seconds);
        if (!(fade > 0.f)) {
            ++stats->culled_short;
            return false;
        }
        ++stats->fading;
    }
    const float L = r.length;
    if (!r.live || !(L > 0.f) || !(r.value > 0.f) || !ee::finite_f(L) || !r.count) {
        ++stats->culled_short;
        return false;
    }
    // The polyline: the head, then the samples newest first, cut at L (the last point interpolated).
    float world[points_per_ribbon][3], width[points_per_ribbon], along[points_per_ribbon];
    const float head_width = width_scale * nozzle_half_width * r.value;
    for (unsigned j = 0; j < 3; ++j) world[0][j] = r.head[j];
    width[0] = head_width;
    along[0] = 0.f;
    unsigned n = 1;
    float total = 0.f;
    for (unsigned back = 0; back < r.count && n < points_per_ribbon && total < L; ++back) {
        const Sample& s = r.at(back);
        float d = 0.f;
        detail::distance(s.p, world[n - 1], &d);
        if (!(d > 1e-4f * r.value)) continue; // the sample at the head (appended this update)
        if (total + d >= L) {
            const float f = (L - total) / d;
            for (unsigned j = 0; j < 3; ++j) world[n][j] = world[n - 1][j] + (s.p[j] - world[n - 1][j]) * f;
            width[n] = width[n - 1] + (s.half_width - width[n - 1]) * f;
            total = L;
        } else {
            for (unsigned j = 0; j < 3; ++j) world[n][j] = s.p[j];
            width[n] = s.half_width;
            total += d;
        }
        along[n] = total;
        ++n;
    }
    if (n < 2 || !(total > 0.f)) {
        ++stats->culled_short;
        return false;
    }
    float v[points_per_ribbon][3];
    bool any_front = false;
    for (unsigned k = 0; k < n; ++k) {
        detail::to_view(view, world[k], v[k]);
        any_front = any_front || v[k][2] > view.near_z;
    }
    if (!any_front) {
        ++stats->culled_behind;
        return false;
    }
    const float focal = view.m11 * view.height * .5f, cap = ep::chase_cap * view.height;
    {
        const float z = v[0][2] > view.near_z ? v[0][2] : view.near_z;
        if (total * focal / z < cull_length_px) {
            ++stats->culled_short;
            return false;
        }
    }
    float colour[3] = {r.tint[0], r.tint[1], r.tint[2]};
    if (view.fog.on) {
        float t[3];
        x3m::renderer::fog_transmittance(view.fog, x3m::scalar::sqrt(v[0][0] * v[0][0] + v[0][1] * v[0][1] + v[0][2] * v[0][2]), t);
        for (unsigned j = 0; j < 3; ++j) colour[j] *= t[j];
        ++stats->fogged;
    }
    const std::uint32_t tint = ep::pack_colour(colour);
    // The plumes' distance law on the nozzle's projected width at the head (the plume's own factor for a live nozzle).
    float far_weight = 1.f;
    {
        const float z = v[0][2] > view.near_z ? v[0][2] : view.near_z;
        ep::distance_weight(look, r.nozzle * focal / z, &far_weight);
        if (far_weight < 1.f) ++stats->far_ribbons;
    }
    const float i0 = (radiance_low + (radiance_high - radiance_low) * r.throttle) * preset_scale * fade * far_weight;
    float previous_side[3] = {0.f, 0.f, 0.f};
    for (unsigned k = 0; k < n; ++k) {
        const float u = along[k] / total;
        float hw = width[k] * (1.f - u);
        const float z = v[k][2] > view.near_z ? v[k][2] : view.near_z;
        const float ppu = focal / z;
        if (hw * ppu < .5f * min_width_px) {
            hw = .5f * min_width_px / ppu;
            ++stats->floored;
        }
        // The plume's near-camera rule: the projected width held to 0.12 H, radiance 1 -> 0.5 over the last 20 %.
        float near_weight = 1.f;
        const float q = 2.f * hw * ppu / cap;
        if (q > 1.f - ep::chase_fade_band) {
            float t = (q - (1.f - ep::chase_fade_band)) * (1.f / ep::chase_fade_band);
            t = t > 1.f ? 1.f : t;
            near_weight = 1.f - (1.f - ep::chase_fade_floor) * t;
        }
        if (q > 1.f) {
            hw = .5f * cap / ppu;
            ++stats->capped;
        }
        // Side: across the local tangent and the line of sight; kept on the previous point's side (no twist).
        const unsigned a = k + 1 < n ? k + 1 : k, b = k > 0 ? k - 1 : k;
        float tangent[3] = {v[b][0] - v[a][0], v[b][1] - v[a][1], v[b][2] - v[a][2]};
        float e[3] = {-v[k][0], -v[k][1], -v[k][2]}, tl = 0.f, el = 0.f, sl = 0.f;
        detail::normalise(tangent, &tl);
        detail::normalise(e, &el);
        if (!(el > 0.f)) {
            e[0] = e[1] = 0.f;
            e[2] = -1.f;
        }
        float side[3];
        detail::cross(tangent, e, side);
        detail::normalise(side, &sl);
        if (!(sl > 1e-4f)) {
            if (k > 0)
                for (unsigned j = 0; j < 3; ++j) side[j] = previous_side[j];
            else {
                side[0] = 1.f;
                side[1] = side[2] = 0.f;
            }
        } else if (k > 0 && side[0] * previous_side[0] + side[1] * previous_side[1] + side[2] * previous_side[2] < 0.f)
            for (float& c : side) c = -c;
        for (unsigned j = 0; j < 3; ++j) previous_side[j] = side[j];
        const float radiance = i0 * (1.f - u) * near_weight;
        for (unsigned edge = 0; edge < 2; ++edge) {
            Vertex& x = out[2 * k + edge];
            const float sign = edge ? 1.f : -1.f;
            for (unsigned j = 0; j < 3; ++j) x.position[j] = v[k][j] + side[j] * hw * sign;
            x.strip[0] = u;
            x.strip[1] = sign;
            x.strip[2] = x.strip[3] = 0.f;
            x.shape[0] = radiance;
            x.shape[1] = v[k][2];
            x.shape[2] = r.value;
            x.shape[3] = 0.f;
            x.tint = tint;
        }
    }
    // The unused points repeat the last one: zero-area triangles.
    for (unsigned k = n; k < points_per_ribbon; ++k) {
        out[2 * k] = out[2 * (n - 1)];
        out[2 * k + 1] = out[2 * (n - 1) + 1];
    }
    stats->points += n;
    stats->samples += n - 1;
    return true;
}

// The pool's drawable ribbons into `out` (capacity in ribbons, 34 vertices each); `now_seconds` the update's clock.
inline unsigned build(const Pool& pool, const ep::View& view, ep::Preset preset, double now_seconds, Vertex* out,
                      unsigned capacity, BuildStats* stats, const ep::Look* look = nullptr) noexcept {
    const ep::Look& k = look ? *look : ep::default_look;
    BuildStats local{};
    BuildStats& st = stats ? *stats : local;
    st = BuildStats{};
    if (!out || !(view.m11 > 0.f) || !(view.height > 0.f) || !(view.near_z > 0.f) || !pool.clock_set) return 0;
    for (unsigned i = 0; i < 12; ++i)
        if (!ee::finite_f(view.rows[i])) return 0;
    float scale = 1.f;
    ep::preset_scale(preset, &scale);
    const float now = float(now_seconds - pool.origin);
    unsigned written = 0;
    for (const Ribbon& r : pool.ribbons) {
        if (!r.live) continue;
        if (written >= capacity || written >= max_ribbons) {
            ++st.culled_capacity;
            continue;
        }
        if (build_ribbon(r, r.seen == pool.serial, now, view, scale, out + written * vertices_per_ribbon, &st, k)) ++written;
    }
    st.ribbons = written;
    st.vertices = written * vertices_per_ribbon;
    return written;
}
} // namespace x3m::engine_ribbons
