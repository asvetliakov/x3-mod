#pragma once
#include "../renderer/weapon_effects_pass.h"
#include "weapon_effects_bubble.h"
#include "weapon_effects_core.h"
#include "sse_scalar.h"
#include <cstdint>
#include <cstring>

// The lathe bubble (docs/architecture/weapon-effects.md 5.1.1 "Placement", user decision 2026-11-12 on FL run 68:
// capitals get the bubble; the box-fitted ellipsoid misfits long hulls; FL 69 A, the same day: every ship). A ship
// whose TShips row has an offline profile (core::ShipProfiles, weapon_effects.json schema 3) draws its shield hits on a
// lathe built from that profile, whatever its tier; a ship without a usable one keeps the game's native effect (the
// impact hook's core::ship_profile_verdict forwards it). Windows-free; reads go through the caller's reader; no
// allocation, no libm, SSE2 scalars only.
//
// The type (resolve_lathe, per owner and stage call, six engine reads): the object's class word obj+0x48 must be a ship
// (7); its subtype obj+0x4a (int16) is the TShips index (the row read_tier reads its class from); the type table
// [ship_table_slot] with its count [ship_count_slot] give the row's class id (+0xc8, TShips col 52); the profile of that
// index must carry the same class (the generator's col 52 token through core::ship_class_id_of), else the ship falls
// back (LatheRefusal::class_mismatch: a table generated for another game tree or edition). The scene-to-object scale: the
// root node's axis scales [node+0x80 + 4 i] (16.16), the factor the engine applies to its children's offsets (FL 67 A's
// Titan fit: 0x10000, scene units = owner-frame engine units), each in (0, core::profile_axis_scale_max], 256x.
//
// The mesh (build_lathe, once per type and scale in a LatheCache of lathe_cache_entries, the least recently used not
// drawn this frame evicted): weapon_lathe_rings rings of weapon_lathe_columns vertices in rising z (owner frame, view
// units = scene x axis scale / 65536 x world_scale). The 32 body rings sit at the slices' centres, each an ellipse
// (cx + hx sin phi, cy + hy cos phi), phi = -pi + 2 pi j / 24 (the seam at -y), its half-widths the slice's (an empty
// slice takes its neighbours' interpolation; each floored at core::bubble_floor of the largest) x core::bubble_scale. Each
// end closes with a half ellipsoid on its end ring (semi-axes hx, hy and the dome depth d = half a slice + (bubble_scale -
// 1) x the end ring's mean half-width, at least 0.35 of it, so the bow and stern are rounded, not sawn off):
// four rings at 22.5, 45, 67.5 and 90 degrees (the last the tip). The chart: u the arc length along the lathe's mean
// outline (per ring, its step the mean of its columns' distances from the previous ring; the middle ring's z at the
// middle ring; a step between slices keeps its cells' length on average, and no column shears), v the
// vertex's perimeter fraction in [-0.5, 0.5] (the ring polygon's cumulative edge length, the seam at -y) x the ring's
// scale (1, or its perimeter over lathe_min_width of the median: a narrow ring spans that share of the period, so the
// cells' shrink across the hull stops at lathe_min_width near the nose, the tail and the dome tips); the stage scales v
// by the chart's period (lathe_period: the median body ring's perimeter rounded to whole lattice periods, so the hex
// lattice closes around a ring of scale 1). A hit inside the lathe first moves back along its projectile's travel to
// where it entered the lathe (lathe_entry, FL 70 A; the engine's contact can lie deep in its slice). It then maps to
// the lathe (map_lathe) along its slice:
// the hit's z picks the body ring pair, their (cx, cy, hx, hy) interpolated, and the hit is projected laterally onto that
// ellipse in its normalised space ((x - cx) / hx, (y - cy) / hy), not radially from the centre; past the end rings onto
// the dome's ellipsoid radially in its normalised space.
namespace x3m::weapon_effects::stage {
namespace core = x3m::weapon_effects::core;
constexpr unsigned lathe_body_rings = core::profile_slices, lathe_dome_rings = 4;
constexpr unsigned lathe_rings = lathe_body_rings + 2 * lathe_dome_rings;
constexpr unsigned lathe_columns = renderer::weapon_lathe_columns, lathe_segments = renderer::weapon_lathe_segments;
static_assert(lathe_rings == renderer::weapon_lathe_rings, "the pass's lathe ring count");
static_assert(lathe_segments == 24, "the ring's sine table");
constexpr unsigned lathe_cache_entries = 32;
// A ring narrower than this share of the median body ring's perimeter spans that share of the chart period (the cells'
// across-hull shrink toward the nose, the tail and the dome tips stops at it; 2026-11-12 look fix).
constexpr float lathe_min_width = 0.6f;
static_assert(lathe_cache_entries >= core::max_owners, "every owner a frame draws keeps its mesh through the frame");
// sin and cos of 15 j degrees, j = 0..24 (the ring's columns, exact to float).
constexpr float lathe_sin[lathe_columns] = {0.f, 0.25881905f, 0.5f, 0.70710678f, 0.8660254f, 0.96592583f, 1.f,
                                            0.96592583f, 0.8660254f, 0.70710678f, 0.5f, 0.25881905f, 0.f, -0.25881905f,
                                            -0.5f, -0.70710678f, -0.8660254f, -0.96592583f, -1.f, -0.96592583f,
                                            -0.8660254f, -0.70710678f, -0.5f, -0.25881905f, 0.f};
// The dome rings' sin and cos (22.5, 45, 67.5, 90 degrees).
constexpr float dome_sin[lathe_dome_rings] = {0.38268343f, 0.70710678f, 0.92387953f, 1.f};
constexpr float dome_cos[lathe_dome_rings] = {0.92387953f, 0.70710678f, 0.38268343f, 0.f};
// atan2(y, x) in radians without libm (|error| < 1.5e-5 rad): the octant's ratio through a minimax polynomial.
inline float atan2_sse(float y, float x) noexcept {
    const float ax = x < 0.f ? -x : x, ay = y < 0.f ? -y : y;
    const float mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    if (!(mx > 0.f)) return 0.f;
    const float a = mn / mx, s = a * a;
    float r = a * (0.99997726f + s * (-0.33262347f + s * (0.19354346f + s * (-0.11643287f + s * (0.05265332f +
                                                                                              s * -0.01172120f)))));
    if (ay > ax) r = 1.57079633f - r;
    if (x < 0.f) r = 3.14159265f - r;
    return y < 0.f ? -r : r;
}
// The type's mesh and the data the hit mapping reads (owner frame, view units). About 22 KB.
struct LatheMesh {
    renderer::WeaponLatheVertex vertex[lathe_rings * lathe_columns];
    float ring[lathe_rings][4];  // cx, cy, hx, hy (inflated; a dome ring scaled by its cosine)
    float ring_z[lathe_rings], ring_u[lathe_rings];         // ring_u: placeholder z until the column pass
    float ring_umin[lathe_rings], ring_umax[lathe_rings];   // the ring's chart u range over its columns (rising)
    float ring_scale[lathe_rings];                          // the ring's chart v range over the period (<= 1)
    float ring_lo[lathe_rings][3], ring_hi[lathe_rings][3]; // each ring's bounds (the band's box)
    float dome_depth[2] = {0.f, 0.f};                       // the stern's and the bow's d
    float perimeter = 0.f;                                  // the median body ring's perimeter
    float hull_lo[3]{}, hull_hi[3]{};                       // the profile's own bounds (before the inflation)
    float march = 0.f;          // lathe_entry's step: a quarter of the thinnest ring half-axis or dome depth
    unsigned march_steps = 0;   // its steps across the lathe's bounds (the diagonal), at most lathe_march_max
    float half_width = 0.f;     // the largest body ring half-axis (inflated): lathe_entry's bound
    std::uint32_t serial = 0;   // the LatheCache build that made it (0: built outside a cache)
};
constexpr unsigned lathe_march_max = 512;
// The scene-to-view factors of the root's axis scales (16.16); false outside (0, core::profile_axis_scale_max].
inline bool lathe_factors(const std::int32_t axis[3], float f[3]) noexcept {
    for (unsigned i = 0; i < 3; ++i) {
        if (axis[i] <= 0 || axis[i] > core::profile_axis_scale_max) return false;
        f[i] = float(double(axis[i]) / 65536.0 * double(core::world_scale));
    }
    return true;
}
// One ring's vertices, bounds and perimeter fractions from its (cx, cy, hx, hy) at z with chart u; fracs (when not
// null) give the columns' v instead of this ring's own (the dome rings keep their end ring's). Returns the perimeter.
inline float lathe_ring(LatheMesh& m, unsigned r, const float e[4], float z, float u, const float* fracs) noexcept {
    renderer::WeaponLatheVertex* v = m.vertex + r * lathe_columns;
    for (unsigned k = 0; k < 4; ++k) m.ring[r][k] = e[k];
    m.ring_z[r] = z;
    m.ring_u[r] = u;
    float s = 0.f;
    for (unsigned j = 0; j < lathe_columns; ++j) {
        // phi_j = -180 + 15 j degrees: sin(-180 + a) = -sin a, cos(-180 + a) = -cos a = -sin(a + 90)
        const unsigned q = (j + 6u) % lathe_segments;
        const float sp = -lathe_sin[j], cp = -lathe_sin[q];
        v[j].pos[0] = e[0] + e[2] * sp;
        v[j].pos[1] = e[1] + e[3] * cp;
        v[j].pos[2] = z;
        v[j].u = u;
        if (j) {
            const float dx = v[j].pos[0] - v[j - 1].pos[0], dy = v[j].pos[1] - v[j - 1].pos[1];
            s += scalar::sqrt(dx * dx + dy * dy);
        }
        v[j].v = s; // the cumulative length, normalised below
    }
    for (unsigned j = 0; j < lathe_columns; ++j)
        v[j].v = fracs ? fracs[j] : (s > 0.f ? v[j].v / s - 0.5f : float(j) / float(lathe_segments) - 0.5f);
    for (unsigned i = 0; i < 3; ++i) {
        m.ring_lo[r][i] = 1e30f;
        m.ring_hi[r][i] = -1e30f;
    }
    for (unsigned j = 0; j < lathe_columns; ++j)
        for (unsigned i = 0; i < 3; ++i) {
            m.ring_lo[r][i] = v[j].pos[i] < m.ring_lo[r][i] ? v[j].pos[i] : m.ring_lo[r][i];
            m.ring_hi[r][i] = v[j].pos[i] > m.ring_hi[r][i] ? v[j].pos[i] : m.ring_hi[r][i];
        }
    return s;
}
// The mesh of profile p at the root's axis scales (see the header); false for a degenerate profile or scale.
inline bool build_lathe(const core::ShipProfile& p, const std::int32_t axis[3], LatheMesh* m) noexcept {
    float f[3];
    if (!lathe_factors(axis, f) || !(p.z1 > p.z0)) return false;
    constexpr unsigned S = core::profile_slices;
    float e[S][4];
    bool have[S];
    float widest = 0.f;
    for (unsigned i = 0; i < S; ++i) {
        const float* s = p.slice[i];
        have[i] = s[2] > 0.f && s[3] > 0.f;
        e[i][0] = s[0] * f[0];
        e[i][1] = s[1] * f[1];
        e[i][2] = s[2] * f[0];
        e[i][3] = s[3] * f[1];
        if (have[i]) {
            widest = e[i][2] > widest ? e[i][2] : widest;
            widest = e[i][3] > widest ? e[i][3] : widest;
        }
    }
    if (!(widest > 0.f) || !(widest < 1e20f)) return false;
    for (unsigned i = 0; i < S; ++i) { // an empty slice: its nearest filled neighbours' interpolation
        if (have[i]) continue;
        int a = int(i) - 1, b = int(i) + 1;
        while (a >= 0 && !have[a]) --a;
        while (b < int(S) && !have[b]) ++b;
        const float* lo = a >= 0 ? e[a] : e[b];
        const float* hi = b < int(S) ? e[b] : e[a];
        const float w = a >= 0 && b < int(S) ? float(int(i) - a) / float(b - a) : 0.f;
        for (unsigned k = 0; k < 4; ++k) e[i][k] = lo[k] + (hi[k] - lo[k]) * w;
    }
    const float floor_w = widest * core::bubble_floor;
    for (unsigned i = 0; i < 3; ++i) {
        m->hull_lo[i] = 1e30f;
        m->hull_hi[i] = -1e30f;
    }
    for (unsigned i = 0; i < S; ++i) {
        const float x0 = e[i][0] - e[i][2], x1 = e[i][0] + e[i][2], y0 = e[i][1] - e[i][3], y1 = e[i][1] + e[i][3];
        m->hull_lo[0] = x0 < m->hull_lo[0] ? x0 : m->hull_lo[0];
        m->hull_hi[0] = x1 > m->hull_hi[0] ? x1 : m->hull_hi[0];
        m->hull_lo[1] = y0 < m->hull_lo[1] ? y0 : m->hull_lo[1];
        m->hull_hi[1] = y1 > m->hull_hi[1] ? y1 : m->hull_hi[1];
        for (unsigned k = 2; k < 4; ++k)
            e[i][k] = (e[i][k] > floor_w ? e[i][k] : floor_w) * core::bubble_scale;
    }
    const float z0 = p.z0 * f[2], z1 = p.z1 * f[2], dz = (z1 - z0) / float(S);
    m->hull_lo[2] = z0;
    m->hull_hi[2] = z1;
    float perimeter[lathe_rings];
    for (unsigned i = 0; i < S; ++i) {
        const float z = z0 + (float(i) + 0.5f) * dz;
        perimeter[lathe_dome_rings + i] = lathe_ring(*m, lathe_dome_rings + i, e[i], z, z, nullptr);
    }
    // The domes: the stern's rings below its end ring (tip first), the bow's above.
    for (unsigned end = 0; end < 2; ++end) {
        const unsigned er = end ? lathe_dome_rings + S - 1 : lathe_dome_rings;
        const float* ee = e[end ? S - 1 : 0];
        const float sign = end ? 1.f : -1.f, mean = 0.5f * (ee[2] + ee[3]);
        // past the end slice's half by the inflation of the end ring's own size (not of the ship's length: on the
        // Titan's 4 m nose that made a 192 m needle), at least 0.35 of its mean half-width (rounded, not sawn off)
        float d = 0.5f * dz + (core::bubble_scale - 1.f) * mean;
        d = d > 0.35f * mean ? d : 0.35f * mean;
        m->dome_depth[end] = d;
        float fr[lathe_columns];
        for (unsigned j = 0; j < lathe_columns; ++j) fr[j] = m->vertex[er * lathe_columns + j].v;
        for (unsigned k = 0; k < lathe_dome_rings; ++k) {
            const float de[4] = {ee[0], ee[1], ee[2] * dome_cos[k], ee[3] * dome_cos[k]};
            const unsigned r = end ? er + 1 + k : er - 1 - k;
            const float zz = m->ring_z[er] + sign * d * dome_sin[k];
            lathe_ring(*m, r, de, zz, zz, fr);
            perimeter[r] = perimeter[er] * dome_cos[k];
        }
    }
    float sorted[S];
    std::memcpy(sorted, perimeter + lathe_dome_rings, sizeof sorted);
    for (unsigned i = 1; i < S; ++i) // insertion sort of 32 values, once per type
        for (unsigned j = i; j > 0 && sorted[j] < sorted[j - 1]; --j) {
            const float t = sorted[j];
            sorted[j] = sorted[j - 1];
            sorted[j - 1] = t;
        }
    m->perimeter = 0.5f * (sorted[S / 2 - 1] + sorted[S / 2]);
    if (!(m->perimeter > 0.f)) return false;
    // The chart's u (2026-11-12 look fix): per column the arc length along the lathe's outline at that angle (the
    // cumulative distance between the column's vertices ring to ring), 0 at the middle body ring's z, so the cells keep
    // their length over a step between slices (u = z stretched them there). The chart's v: the perimeter fraction x
    // ring_scale, a ring narrower than lathe_min_width of the median perimeter spanning that share of the period, so its
    // cells shrink across the hull by at most lathe_min_width toward the nose, the tail and the dome tips.
    // u per ring (the mean outline): its step from the previous ring is the mean of its 24 columns' distances, so a
    // step between slices keeps the cells' length on average and no column shears against its neighbours (a per-column
    // arc length accumulated up to 188 m of u between neighbouring columns past the Titan's fin slices: sheared cells).
    constexpr unsigned mid = lathe_dome_rings + S / 2;
    float step[lathe_rings] = {};
    for (unsigned r = 1; r < lathe_rings; ++r) {
        float sum = 0.f;
        for (unsigned j = 0; j < lathe_segments; ++j) {
            const renderer::WeaponLatheVertex &a = m->vertex[(r - 1) * lathe_columns + j], &b = m->vertex[r * lathe_columns + j];
            const float dx = b.pos[0] - a.pos[0], dy = b.pos[1] - a.pos[1], dzz = b.pos[2] - a.pos[2];
            sum += scalar::sqrt(dx * dx + dy * dy + dzz * dzz);
        }
        step[r] = sum / float(lathe_segments);
    }
    float ring_u[lathe_rings];
    ring_u[mid] = m->ring_z[mid];
    for (unsigned r = mid + 1; r < lathe_rings; ++r) ring_u[r] = ring_u[r - 1] + step[r];
    for (unsigned r = mid; r-- > 0;) ring_u[r] = ring_u[r + 1] - step[r + 1];
    for (unsigned r = 0; r < lathe_rings; ++r)
        for (unsigned j = 0; j < lathe_columns; ++j) m->vertex[r * lathe_columns + j].u = ring_u[r];
    for (unsigned r = 0; r < lathe_rings; ++r) {
        const float share = perimeter[r] / (m->perimeter * lathe_min_width);
        m->ring_scale[r] = share < 1.f ? (share > 0.f ? share : 0.f) : 1.f;
        m->ring_umin[r] = 1e30f;
        m->ring_umax[r] = -1e30f;
        for (unsigned j = 0; j < lathe_columns; ++j) {
            renderer::WeaponLatheVertex& v = m->vertex[r * lathe_columns + j];
            v.v *= m->ring_scale[r];
            m->ring_umin[r] = v.u < m->ring_umin[r] ? v.u : m->ring_umin[r];
            m->ring_umax[r] = v.u > m->ring_umax[r] ? v.u : m->ring_umax[r];
        }
        m->ring_u[r] = 0.5f * (m->ring_umin[r] + m->ring_umax[r]);
    }
    // lathe_entry's walk: a step of a quarter of the thinnest ring half-axis or dome depth, its count across the
    // lathe's bounding diagonal (a walk from any inside point leaves the lathe within it).
    float thin = m->dome_depth[0] < m->dome_depth[1] ? m->dome_depth[0] : m->dome_depth[1], wide = 0.f;
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (unsigned r = 0; r < lathe_rings; ++r) {
        if (r >= lathe_dome_rings && r < lathe_dome_rings + S) {
            thin = m->ring[r][2] < thin ? m->ring[r][2] : thin;
            thin = m->ring[r][3] < thin ? m->ring[r][3] : thin;
            wide = m->ring[r][2] > wide ? m->ring[r][2] : wide;
            wide = m->ring[r][3] > wide ? m->ring[r][3] : wide;
        }
        for (unsigned i = 0; i < 3; ++i) {
            lo[i] = m->ring_lo[r][i] < lo[i] ? m->ring_lo[r][i] : lo[i];
            hi[i] = m->ring_hi[r][i] > hi[i] ? m->ring_hi[r][i] : hi[i];
        }
    }
    const float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dzz = hi[2] - lo[2];
    const float diag = scalar::sqrt(dx * dx + dy * dy + dzz * dzz);
    m->march = 0.25f * thin;
    m->half_width = wide;
    if (!(m->march > 0.f) || !(diag < 1e30f)) return false;
    const float steps = diag / m->march + 1.f;
    m->march_steps = steps < float(lathe_march_max) ? unsigned(steps) + 1u : lathe_march_max;
    m->serial = 0;
    return true;
}
// The chart period for a lattice of cell (view units): the median perimeter rounded to whole lattice periods (sqrt 3
// cells, two key rows), at least two; *rows the key rows of one period.
inline float lathe_period(const LatheMesh& m, float cell, float* rows) noexcept {
    const float unit = 1.7320508f * cell;
    float n = float(scalar::floor(double(m.perimeter / unit) + 0.5));
    n = n < 2.f ? 2.f : n;
    *rows = 2.f * n;
    return n * unit;
}
// Ring r's chart (u, v) at the hit's angle phi (radians, -pi..pi: the column pair's linear interpolation).
inline void lathe_uv(const LatheMesh& m, unsigned r, float phi, float uv[2]) noexcept {
    float t = (phi + 3.14159265f) * (float(lathe_segments) / 6.28318531f);
    t = t < 0.f ? 0.f : t > float(lathe_segments) ? float(lathe_segments) : t;
    unsigned j = unsigned(t);
    j = j >= lathe_segments ? lathe_segments - 1 : j;
    const float w = t - float(j);
    const renderer::WeaponLatheVertex &a = m.vertex[r * lathe_columns + j], &b = m.vertex[r * lathe_columns + j + 1];
    uv[0] = a.u + (b.u - a.u) * w;
    uv[1] = a.v + (b.v - a.v) * w;
}
// The chart at weight w between rings ra and rb (angle phi), and their chart scale.
inline void lathe_chart(const LatheMesh& m, unsigned ra, unsigned rb, float w, float phi, float chart[2],
                        float* scale) noexcept {
    float a[2], b[2];
    lathe_uv(m, ra, phi, a);
    lathe_uv(m, rb, phi, b);
    chart[0] = a[0] + (b[0] - a[0]) * w;
    chart[1] = a[1] + (b[1] - a[1]) * w;
    if (scale) *scale = m.ring_scale[ra] + (m.ring_scale[rb] - m.ring_scale[ra]) * w;
}
// Hit point p (owner frame, view units) onto the lathe (see the header): out the lathe point, chart (u, v') the chart
// coordinates (v' the perimeter fraction x the ring's scale: x lathe_period for the lattice), *scale (when given) the
// ring's chart scale there (its seam lies at +-scale / 2).
inline void map_lathe(const LatheMesh& m, const float p[3], float out[3], float chart[2],
                      float* scale = nullptr) noexcept {
    constexpr unsigned first = lathe_dome_rings, last = lathe_dome_rings + lathe_body_rings - 1;
    const float zf = m.ring_z[first], zl = m.ring_z[last];
    if (p[2] >= zf && p[2] <= zl) { // the body: lateral onto the slice's ellipse
        const float t = (p[2] - zf) / (zl - zf) * float(last - first);
        unsigned i = unsigned(t);
        i = i >= last - first ? last - first - 1 : i;
        const float w = t - float(i);
        const unsigned r = first + i;
        float e[4];
        for (unsigned k = 0; k < 4; ++k) e[k] = m.ring[r][k] + (m.ring[r + 1][k] - m.ring[r][k]) * w;
        float dx = (p[0] - e[0]) / e[2], dy = (p[1] - e[1]) / e[3];
        const float n2 = dx * dx + dy * dy;
        if (!(n2 > 1e-12f) || !(n2 < 1e30f)) {
            dx = 0.f;
            dy = 1.f;
        } else {
            const float inv = 1.f / scalar::sqrt(n2);
            dx *= inv;
            dy *= inv;
        }
        out[0] = e[0] + e[2] * dx;
        out[1] = e[1] + e[3] * dy;
        out[2] = p[2];
        lathe_chart(m, r, r + 1, w, atan2_sse(dx, dy), chart, scale);
        return;
    }
    const bool bow = p[2] > zl;
    const unsigned er = bow ? last : first;
    const float* e = m.ring[er];
    const float d = m.dome_depth[bow ? 1 : 0], sign = bow ? 1.f : -1.f;
    float q[3] = {(p[0] - e[0]) / e[2], (p[1] - e[1]) / e[3], sign * (p[2] - m.ring_z[er]) / d};
    const float n2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
    if (!(n2 > 1e-12f) || !(n2 < 1e30f)) {
        q[0] = q[1] = 0.f;
        q[2] = 1.f;
    } else {
        const float inv = 1.f / scalar::sqrt(n2);
        for (float& v : q) v *= inv;
    }
    out[0] = e[0] + e[2] * q[0];
    out[1] = e[1] + e[3] * q[1];
    out[2] = m.ring_z[er] + sign * d * q[2];
    // along the dome: its rings at 22.5 k degrees (k = 0 the end ring), the meridian u between them
    const float lateral = scalar::sqrt(q[0] * q[0] + q[1] * q[1]);
    float k = atan2_sse(q[2], lateral) * (float(lathe_dome_rings) / 1.57079633f);
    k = k < 0.f ? 0.f : k > float(lathe_dome_rings) ? float(lathe_dome_rings) : k;
    unsigned a = unsigned(k);
    a = a >= lathe_dome_rings ? lathe_dome_rings - 1 : a;
    const unsigned ra = bow ? er + a : er - a, rb = bow ? ra + 1 : ra - 1;
    lathe_chart(m, ra, rb, k - float(a), lateral > 1e-6f ? atan2_sse(q[0], q[1]) : 0.f, chart, scale);
}
// The lathe's shape at owner-frame point x (view units): its normalised squared radius less one, < 0 inside, > 0
// outside; the body (between the end rings' z) the slice pair's interpolated ellipse ((x - cx) / hx, (y - cy) / hy),
// past an end ring its dome's half ellipsoid (the third axis (z - z_end) / d). A NaN point reads as outside.
inline float lathe_shape(const LatheMesh& m, const float x[3]) noexcept {
    constexpr unsigned first = lathe_dome_rings, last = lathe_dome_rings + lathe_body_rings - 1;
    const float zf = m.ring_z[first], zl = m.ring_z[last];
    if (x[2] >= zf && x[2] <= zl) {
        const float t = (x[2] - zf) / (zl - zf) * float(last - first);
        unsigned i = unsigned(t);
        i = i >= last - first ? last - first - 1 : i;
        const float w = t - float(i);
        const float *a = m.ring[first + i], *b = m.ring[first + i + 1];
        const float dx = (x[0] - (a[0] + (b[0] - a[0]) * w)) / (a[2] + (b[2] - a[2]) * w);
        const float dy = (x[1] - (a[1] + (b[1] - a[1]) * w)) / (a[3] + (b[3] - a[3]) * w);
        return dx * dx + dy * dy - 1.f;
    }
    const bool bow = x[2] > zl;
    const unsigned er = bow ? last : first;
    const float* e = m.ring[er];
    const float dx = (x[0] - e[0]) / e[2], dy = (x[1] - e[1]) / e[3];
    const float dz = (x[2] - m.ring_z[er]) / m.dome_depth[bow ? 1 : 0];
    const float g = dx * dx + dy * dy + dz * dz - 1.f;
    return g == g ? g : 1.f;
}
// Where a hit's projectile entered the lathe (FL 70 A, 2026-11-13): the engine's contact p (owner frame, view units)
// lies on the hull, inside the shield, and away from a ring's rim it can lie deep in its slice (the Titan's nose hits
// registered 158 to 255 m behind z1 within 130 m of the axis), where map_lathe's lateral projection throws it onto
// the slice's rim at a near-random angle, a band of clusters around the hull where the bolts did not strike. The entry
// is the first point of the shape's sign change walking from p against the travel (the event's dir: the projectile's
// local z in the owner frame, any length), in steps of m.march, then bisected eight times (march / 256). The entry
// must lie within lathe_entry_reach x the hull's half-width (m.half_width, the largest inflated ring half-axis; the
// 550 logged Titan hits walk at most 1.08 of it, 376 m) of p: a travel that does not point into the hull (a shooter
// inside the inflated lathe, a projectile node whose z is not its flight) would cross the hull and exit on its far side
// (FL 70 A review; a crossing shorter than the reach, across a narrow section, is not caught). LatheEntry::none, *out
// = p, when the travel is zero, p is not inside the lathe (a mast past a dome keeps map_lathe's radial dome; a graze
// outside its slice the lateral projection) or the walk finds no exit within m.march_steps; LatheEntry::beyond, *out =
// p, past the reach. O(steps) shape reads; the stage memoises it per record (build's lathe memo).
constexpr float lathe_entry_reach = 1.25f;
enum class LatheEntry : std::uint8_t { none, entered, beyond }; // not "far": a Windows macro
inline LatheEntry lathe_entry(const LatheMesh& m, const float p[3], const float travel[3], float out[3]) noexcept {
    for (unsigned i = 0; i < 3; ++i) out[i] = p[i];
    const float n2 = travel[0] * travel[0] + travel[1] * travel[1] + travel[2] * travel[2];
    if (!(n2 > 1e-12f) || !(n2 < 1e30f) || !(m.march > 0.f) || !(lathe_shape(m, p) < 0.f)) return LatheEntry::none;
    const float k = m.march / scalar::sqrt(n2);
    const float step[3] = {-travel[0] * k, -travel[1] * k, -travel[2] * k};
    float in = 0.f, x[3];
    for (unsigned s = 1; s <= m.march_steps; ++s) {
        for (unsigned i = 0; i < 3; ++i) x[i] = p[i] + step[i] * float(s);
        if (lathe_shape(m, x) < 0.f) {
            in = float(s);
            continue;
        }
        float lo = in, hi = float(s);
        for (unsigned b = 0; b < 8; ++b) {
            const float mid = 0.5f * (lo + hi);
            for (unsigned i = 0; i < 3; ++i) x[i] = p[i] + step[i] * mid;
            if (lathe_shape(m, x) < 0.f) lo = mid;
            else hi = mid;
        }
        const float at = 0.5f * (lo + hi);
        if (at * m.march > lathe_entry_reach * m.half_width) return LatheEntry::beyond; // *out stays p
        for (unsigned i = 0; i < 3; ++i) out[i] = p[i] + step[i] * at;
        return LatheEntry::entered;
    }
    return LatheEntry::none;
}
// The rings and columns whose quads hold chart box [u0, u1] x [v0, v1] (v in perimeter fractions; a box past the seam
// or wider than the chart takes every column), with one column of margin; and the band's bounds (owner frame).
inline void lathe_band(const LatheMesh& m, float u0, float u1, float v0, float v1, unsigned* r0, unsigned* r1,
                       unsigned* c0, unsigned* c1, float lo[3], float hi[3]) noexcept {
    // every column's u rises ring to ring: from the last ring wholly below u0 to the first wholly above u1
    unsigned a = 0, b = lathe_rings - 1;
    while (a + 1 < lathe_rings && m.ring_umax[a + 1] <= u0) ++a;
    while (b > a + 1 && m.ring_umin[b - 1] >= u1) --b;
    *r0 = a;
    *r1 = b;
    *c0 = 0;
    *c1 = lathe_segments;
    if (v0 > -0.5f && v1 < 0.5f && v1 >= v0) {
        unsigned s0 = lathe_segments, s1 = 0;
        for (unsigned r = a; r <= b; ++r) {
            const renderer::WeaponLatheVertex* v = m.vertex + r * lathe_columns;
            unsigned j0 = 0, j1 = lathe_segments;
            while (j0 + 1 < lathe_columns && v[j0 + 1].v <= v0) ++j0;
            while (j1 > j0 + 1 && v[j1 - 1].v >= v1) --j1;
            s0 = j0 < s0 ? j0 : s0;
            s1 = j1 > s1 ? j1 : s1;
        }
        *c0 = s0 > 0 ? s0 - 1 : 0;
        *c1 = s1 + 1 < lathe_segments ? s1 + 1 : lathe_segments;
    }
    for (unsigned i = 0; i < 3; ++i) {
        lo[i] = 1e30f;
        hi[i] = -1e30f;
    }
    for (unsigned r = a; r <= b; ++r)
        for (unsigned i = 0; i < 3; ++i) {
            lo[i] = m.ring_lo[r][i] < lo[i] ? m.ring_lo[r][i] : lo[i];
            hi[i] = m.ring_hi[r][i] > hi[i] ? m.ring_hi[r][i] : hi[i];
        }
}
// Why an owner with profiles loaded draws no lathe (the weapon_effects_frame row's lathe_* counters): not a ship (or no
// profiles; counted with no_profile), an engine read refused, no profile for its row, the profile's class differs from
// the type record's, the root's axis scales outside (0, 256x], the profile degenerate, every cache entry drawn this
// frame, the frame's mesh builds spent (deferred: the owner's records stay in the ring and draw on a later frame).
enum class LatheRefusal : std::uint8_t {
    none, not_ship, unreadable, no_profile, class_mismatch, scale, degenerate, cache, deferred
};
// Mesh builds per stage call (review F5: a sector entry with many new types builds two a frame; one Titan mesh's cold
// build is measured by the native fixture's LATHE case=build_cost row).
constexpr unsigned lathe_builds_per_frame = 2;
struct LatheType {
    const core::ShipProfile* profile = nullptr;
    std::int32_t row = -1, class_id = -1, axis[3] = {0, 0, 0};
};
// The owner's type (see the header): LatheRefusal::none and *out filled, else the refusal.
inline LatheRefusal resolve_lathe(Reader rd, const core::ShipProfiles* profiles, std::uintptr_t ship_table_slot,
                                  std::uintptr_t ship_count_slot, std::uint32_t owner, std::uint32_t node,
                                  LatheType* out) noexcept {
    if (!profiles || !profiles->count || !rd || !ship_table_slot || !ship_count_slot) return LatheRefusal::not_ship;
    std::uint16_t cls = 0;
    std::int16_t sub = 0;
    if (!rd(owner + core::layout::obj_class, &cls, 2)) return LatheRefusal::unreadable;
    if (cls != core::layout::class_ship) return LatheRefusal::not_ship;
    std::uint32_t table = 0;
    std::int32_t count = 0, class_id = 0;
    if (!rd(owner + core::layout::obj_subtype, &sub, 2) || !rd(ship_table_slot, &table, 4) ||
        !rd(ship_count_slot, &count, 4))
        return LatheRefusal::unreadable;
    const core::ShipProfile* p = profiles->find(sub);
    if (!p) return LatheRefusal::no_profile;
    if (!table || sub < 0 || sub >= count) return LatheRefusal::unreadable;
    if (!rd(table + std::uint32_t(sub) * core::layout::type_stride + core::layout::ship_class_id, &class_id, 4))
        return LatheRefusal::unreadable;
    if (class_id != p->class_id) return LatheRefusal::class_mismatch;
    if (!rd(node + core::layout::node_axis_scale, out->axis, 12)) return LatheRefusal::unreadable;
    for (unsigned i = 0; i < 3; ++i)
        if (out->axis[i] <= 0 || out->axis[i] > core::profile_axis_scale_max) return LatheRefusal::scale;
    out->profile = p;
    out->row = sub;
    out->class_id = class_id;
    return LatheRefusal::none;
}
// The meshes by type and scale: lathe_cache_entries entries, built on first use; a miss takes an empty entry, else the
// least recently used one not drawn in this stage call (frame()). About 0.7 MB; the production instance is static.
struct LatheCache {
    struct Entry {
        std::int32_t row = -1, axis[3] = {0, 0, 0};
        std::uint32_t used = 0, frame = 0;
        std::uint32_t serial = 0; // the build that made this mesh (builds after it): a rebuilt slot is a new mesh
        bool valid = false;
        LatheMesh mesh;
    };
    Entry entries[lathe_cache_entries];
    std::uint32_t clock = 0, frame_id = 0;
    std::uint32_t builds = 0, hits = 0, refused = 0, deferred = 0; // running counters (the tests, diagnostics)
    void frame() noexcept { ++frame_id; }
    void clear() noexcept {
        for (Entry& e : entries) e.valid = false;
    }
    // budget (nullable): the builds the caller may still run; at 0 a miss is refused as deferred (nothing evicted).
    const LatheMesh* get(const LatheType& t, LatheRefusal* why, unsigned* budget = nullptr) noexcept {
        Entry* free = nullptr;
        for (Entry& e : entries) {
            if (e.valid && e.row == t.row && e.axis[0] == t.axis[0] && e.axis[1] == t.axis[1] && e.axis[2] == t.axis[2]) {
                e.used = ++clock;
                e.frame = frame_id;
                ++hits;
                return &e.mesh;
            }
            if (!e.valid) {
                if (!free || free->valid) free = &e;
            } else if (e.frame != frame_id && (!free || (free->valid && e.used < free->used))) {
                free = &e;
            }
        }
        if (!free) {
            ++refused;
            *why = LatheRefusal::cache;
            return nullptr;
        }
        if (budget) {
            if (!*budget) {
                ++deferred;
                *why = LatheRefusal::deferred;
                return nullptr;
            }
            --*budget;
        }
        free->valid = false;
        if (!build_lathe(*t.profile, t.axis, &free->mesh)) {
            ++refused;
            *why = LatheRefusal::degenerate;
            return nullptr;
        }
        free->valid = true;
        free->row = t.row;
        for (unsigned i = 0; i < 3; ++i) free->axis[i] = t.axis[i];
        free->used = ++clock;
        free->frame = frame_id;
        free->serial = ++builds;
        free->mesh.serial = free->serial;
        return &free->mesh;
    }
};
} // namespace x3m::weapon_effects::stage
