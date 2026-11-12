// FL 70 A (2026-11-13): the Titan's logged shield hits through the production lathe (src/proxy/weapon_effects_lathe.h):
// map_lathe of the contact (the Run70 path) against map_lathe of lathe_entry (the fix). CPU only, no D3D calls.
// stdin: "z0 z1" (scene units), 32 lines "cx cy hx hy", then per hit "frame x y z dx dy dz" (pos engine units, dir x127).
// stdout per hit: frame, inside, entered, contact z (m), Run70 point (m), fix point (m), each point's distance from the
// projectile's line of flight (m).
#include "proxy/weapon_effects_lathe.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace ws = x3m::weapon_effects::stage;
namespace wc = x3m::weapon_effects::core;

// The walk's exit distance without the half-width guard (the guard's margin on the logged hits), view units.
static float unguarded_exit(const ws::LatheMesh& m, const float p[3], const float d[3]) {
    const float n = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(n > 0.f) || !(ws::lathe_shape(m, p) < 0.f)) return -1.f;
    for (unsigned s = 1; s <= m.march_steps; ++s) {
        const float x[3] = {p[0] - d[0] / n * m.march * float(s), p[1] - d[1] / n * m.march * float(s),
                            p[2] - d[2] / n * m.march * float(s)};
        if (!(ws::lathe_shape(m, x) < 0.f)) return m.march * float(s);
    }
    return -1.f;
}
static double off_line(const float p[3], const float d[3], const float q[3]) {
    double v[3] = {q[0] - p[0], q[1] - p[1], q[2] - p[2]}, n = std::sqrt(double(d[0]) * d[0] + double(d[1]) * d[1] + double(d[2]) * d[2]);
    if (!(n > 0.0)) return -1.0;
    double t = (v[0] * d[0] + v[1] * d[1] + v[2] * d[2]) / n, l2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] - t * t;
    return std::sqrt(l2 > 0.0 ? l2 : 0.0);
}

int main() {
    static wc::ShipProfile p;
    if (std::scanf("%f %f", &p.z0, &p.z1) != 2) return 2;
    for (unsigned i = 0; i < wc::profile_slices; ++i)
        if (std::scanf("%f %f %f %f", &p.slice[i][0], &p.slice[i][1], &p.slice[i][2], &p.slice[i][3]) != 4) return 2;
    const std::int32_t axis[3] = {0x10000, 0x10000, 0x10000};
    static ws::LatheMesh m;
    if (!ws::build_lathe(p, axis, &m)) return 3;
    const double metre = double(wc::engine_units_per_metre) * double(wc::world_scale);
    std::printf("mesh z_first_m=%.1f z_last_m=%.1f dome_m=%.1f,%.1f march_m=%.2f steps=%u\n",
                m.ring_z[ws::lathe_dome_rings] / metre, m.ring_z[ws::lathe_dome_rings + ws::lathe_body_rings - 1] / metre,
                m.dome_depth[0] / metre, m.dome_depth[1] / metre, m.march / metre, m.march_steps);
    unsigned long frame;
    long x, y, z;
    int dx, dy, dz;
    std::vector<float> walk; // contact xyz, travel xyz per hit (the timing loop below)
    while (std::scanf("%lu %ld %ld %ld %d %d %d", &frame, &x, &y, &z, &dx, &dy, &dz) == 7) {
        const float c[3] = {float(double(x) * wc::world_scale), float(double(y) * wc::world_scale),
                            float(double(z) * wc::world_scale)};
        const float d[3] = {float(dx), float(dy), float(dz)};
        walk.insert(walk.end(), {c[0], c[1], c[2], d[0], d[1], d[2]});
        float old_b[3], new_b[3], entry[3], chart[2];
        ws::map_lathe(m, c, old_b, chart);
        const ws::LatheEntry walked = ws::lathe_entry(m, c, d, entry);
        const bool entered = walked == ws::LatheEntry::entered;
        ws::map_lathe(m, entry, new_b, chart);
        std::printf("hit %lu inside=%d entered=%d far=%d contact_m=%.1f,%.1f,%.1f run70_m=%.1f,%.1f,%.1f fix_m=%.1f,%.1f,%.1f "
                    "run70_off_line_m=%.1f fix_off_line_m=%.1f exit_m=%.1f half_width_m=%.1f\n",
                    frame, int(ws::lathe_shape(m, c) < 0.f), int(entered), int(walked == ws::LatheEntry::beyond), c[0] / metre, c[1] / metre, c[2] / metre,
                    old_b[0] / metre, old_b[1] / metre, old_b[2] / metre, new_b[0] / metre, new_b[1] / metre,
                    new_b[2] / metre, off_line(c, d, old_b) / metre, off_line(c, d, new_b) / metre,
                    unguarded_exit(m, c, d) / metre, m.half_width / metre);
    }
    // The entry walk's cost (the stage memoises it: once per record): every hit 200 times, best and mean per hit.
    const unsigned hits = unsigned(walk.size() / 6);
    double best = 1e30, sum = 0.0;
    float sink = 0.f;
    for (unsigned rep = 0; rep < 200 && hits; ++rep) {
        const auto t0 = std::chrono::steady_clock::now();
        for (unsigned h = 0; h < hits; ++h) {
            float e[3];
            ws::lathe_entry(m, &walk[6 * h], &walk[6 * h + 3], e);
            sink += e[2];
        }
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / hits;
        best = us < best ? us : best;
        sum += us;
    }
    std::printf("cost hits=%u best_us_per_hit=%.3f mean_us_per_hit=%.3f sink=%g\n", hits, best, hits ? sum / 200.0 : 0.0,
                double(sink));
    return 0;
}
