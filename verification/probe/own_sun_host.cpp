// Host driver of the own sun's pure part (src/proxy/own_sun_core.h, docs/architecture/own-sun-pass.md): the production
// header, no device, no Wine. verification/analysis/test_own_sun.py runs one command per call and parses key=value.
//   project px py pz r0..r8 t0 t1 t2 m00 m11 m20 m21 width height radius_u jx jy
//   radiance c0 c1 c2 k fog_t
//   fog on extinction start cap k0 k1 k2
//   answer active eligible
//   frame_active configured stage_ready
//   resolve latch hidden_before_resolve   the late-probe rule (latch_after_resolve)
//   quad x y half_x half_y
//   constants width height x y radius_px rays core0 core1 core2 glow0 glow1 glow2
//   pixel c0 c1 c2 k fog_t rays px py open_taps visible   the program's law (own_sun_core.h reference_pixel) at
//                                  offset (px, py) core radii, with the constants pixel_constants() uploads
//   field c0 c1 c2 k rays step reach   the glow terms on a grid of `step` core radii out to `reach`: maxima, the hue
//                                  ratio of the glow, the rays per spoke (length, peak) and their mirror symmetry
//   seam c0 c1 c2 k fog_t rays     the largest jump of the ray term across a spoke boundary (r 0.6..16): the program's
//                                  two-spoke sum, and the nearest spoke alone for comparison
//   bench frames                   the stage's CPU side per frame (projection, fog scalar, radiance, quad, constants)
#define X3M_OWN_SUN_HOST_REFERENCE
#include "../../src/proxy/own_sun_core.h"
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace core = x3m::own_sun::core;
static float f(const char* s) {
    return float(std::strtod(s, nullptr));
}
static std::int32_t i32(const char* s) {
    return std::int32_t(std::strtol(s, nullptr, 0));
}
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const char* cmd = argv[1];
    if (!std::strcmp(cmd, "project") && argc == 26) {
        const std::int32_t p[3] = {i32(argv[2]), i32(argv[3]), i32(argv[4])};
        core::Camera c;
        for (int i = 0; i < 9; ++i) c.r[i] = f(argv[5 + i]);
        for (int i = 0; i < 3; ++i) c.t[i] = f(argv[14 + i]);
        c.m00 = f(argv[17]);
        c.m11 = f(argv[18]);
        c.m20 = f(argv[19]);
        c.m21 = f(argv[20]);
        const core::Disc d = core::project(p, c, unsigned(std::atoi(argv[21])), unsigned(std::atoi(argv[22])), f(argv[23]),
                                           f(argv[24]), f(argv[25]));
        std::printf("skip=%s u=%.7f v=%.7f x=%.7f y=%.7f half_x=%.7f half_y=%.7f radius_px=%.5f view_z=%.3f\n",
                    core::skip_name(d.skip), double(d.u), double(d.v), double(d.x), double(d.y), double(d.half_x),
                    double(d.half_y), double(d.radius_px), double(d.view_z));
        return 0;
    }
    if (!std::strcmp(cmd, "radiance") && argc == 7) {
        const std::int32_t c[3] = {i32(argv[2]), i32(argv[3]), i32(argv[4])};
        core::Radiance r;
        core::radiance(c, f(argv[5]), f(argv[6]), &r);
        std::printf("black=%u core=%.7f,%.7f,%.7f glow=%.7f,%.7f,%.7f colour=%.7f,%.7f,%.7f\n", unsigned(r.black),
                    double(r.core[0]), double(r.core[1]), double(r.core[2]), double(r.glow[0]), double(r.glow[1]),
                    double(r.glow[2]), double(r.colour[0]), double(r.colour[1]), double(r.colour[2]));
        return 0;
    }
    if (!std::strcmp(cmd, "fog") && argc == 9) {
        x3m::renderer::FogTransmittanceLaw law;
        law.on = std::atoi(argv[2]) != 0;
        law.extinction = f(argv[3]);
        law.start = f(argv[4]);
        law.cap = f(argv[5]);
        for (int i = 0; i < 3; ++i) law.k[i] = f(argv[6 + i]);
        float t = -1.f;
        core::fog_scalar(law, &t);
        std::printf("t=%.7f\n", double(t));
        return 0;
    }
    if (!std::strcmp(cmd, "answer") && argc == 4) {
        std::printf("answer=%d\n", core::answer(std::atoi(argv[2]) != 0, std::atoi(argv[3]) != 0));
        return 0;
    }
    // The late-probe rule over one frame: the latch set at the previous Present, a hidden answer before the resolve or
    // not; prints the latch for the rest of the frame (after the resolve) and for the next frame (Present sets it again).
    if (!std::strcmp(cmd, "resolve") && argc == 4) {
        const bool latch = std::atoi(argv[2]) != 0, hidden = std::atoi(argv[3]) != 0;
        const bool after = core::latch_after_resolve(latch, hidden);
        std::printf("after_resolve=%u next_frame=%u\n", unsigned(after), unsigned(core::frame_active(true, true)));
        return 0;
    }
    if (!std::strcmp(cmd, "frame_active") && argc == 4) {
        std::printf("active=%u\n", unsigned(core::frame_active(std::atoi(argv[2]) != 0, std::atoi(argv[3]) != 0)));
        return 0;
    }
    if (!std::strcmp(cmd, "quad") && argc == 6) {
        core::Disc d;
        d.x = f(argv[2]);
        d.y = f(argv[3]);
        d.half_x = f(argv[4]);
        d.half_y = f(argv[5]);
        core::Vertex v[4];
        core::quad(d, v);
        for (int k = 0; k < 4; ++k)
            std::printf("v%d=%.6f,%.6f,%.6f,%.6f,%.6f,%.6f%s", k, double(v[k].x), double(v[k].y), double(v[k].z),
                        double(v[k].w), double(v[k].lx), double(v[k].ly), k == 3 ? "\n" : " ");
        return 0;
    }
    if (!std::strcmp(cmd, "constants") && argc == 14) {
        core::Disc d;
        d.x = f(argv[4]);
        d.y = f(argv[5]);
        d.radius_px = f(argv[6]);
        core::Radiance r;
        for (int i = 0; i < 3; ++i) {
            r.core[i] = f(argv[8 + i]);
            r.glow[i] = f(argv[11 + i]);
        }
        float c[core::pixel_registers * 4];
        core::pixel_constants(unsigned(std::atoi(argv[2])), unsigned(std::atoi(argv[3])), d, r, f(argv[7]), c);
        std::printf("c=");
        for (unsigned i = 0; i < core::pixel_registers * 4; ++i) std::printf("%s%.9g", i ? "," : "", double(c[i]));
        std::printf("\n");
        return 0;
    }
    // The constants of a centred sun of 17 px at 1440 rows for a colour, K, fog and rays.
    auto frame_constants = [](const char* const* a, float rays, float* c, core::Radiance* r) {
        const std::int32_t colour[3] = {i32(a[0]), i32(a[1]), i32(a[2])};
        core::radiance(colour, f(a[3]), f(a[4]), r);
        core::Disc d;
        d.radius_px = core::radius_default_u * 1440.f;
        core::pixel_constants(5120, 1440, d, *r, rays, c);
    };
    if (!std::strcmp(cmd, "pixel") && argc == 12) {
        float c[core::pixel_registers * 4];
        core::Radiance r;
        frame_constants(argv + 2, f(argv[7]), c, &r);
        core::Pixel p;
        core::reference_pixel(c, f(argv[8]), f(argv[9]), unsigned(std::atoi(argv[10])), f(argv[11]), &p);
        std::printf("rgb=%.9g,%.9g,%.9g corona=%.7f halo=%.7f rays=%.7f core=%.7f\n", double(p.rgb[0]), double(p.rgb[1]),
                    double(p.rgb[2]), double(p.corona), double(p.halo), double(p.rays), double(p.core));
        return 0;
    }
    if (!std::strcmp(cmd, "seam") && argc == 8) {
        float c[core::pixel_registers * 4];
        core::Radiance r;
        frame_constants(argv + 2, f(argv[7]), c, &r);
        float summed = 0.f, single = 0.f, where = 0.f;
        for (unsigned k = 0; k < core::ray_count; ++k) {
            // The boundary between spoke k and k + 1 (t = k + 0.5).
            const float theta = (float(k) + .5f - core::ray_offset) * core::two_pi / float(core::ray_count);
            for (float rr = .6f; rr <= core::glow_radius; rr += .01f) {
                core::Pixel a, b;
                const float e = 2e-4f;
                core::reference_pixel(c, rr * std::cos(theta - e), rr * std::sin(theta - e), 9, 1.f, &a);
                core::reference_pixel(c, rr * std::cos(theta + e), rr * std::sin(theta + e), 9, 1.f, &b);
                const float d = 1.f - a.core;
                const float jump = std::fabs(a.rays - b.rays) * d;
                if (jump > summed) {
                    summed = jump;
                    where = rr;
                }
                const float one = std::fabs(core::reference_spoke(c, float(k), .5f, rr) -
                                            core::reference_spoke(c, float(k + 1), -.5f, rr)) * c[22] * d;
                single = one > single ? one : single;
            }
        }
        std::printf("summed=%.6f at=%.2f single=%.6f\n", double(summed), double(where), double(single));
        return 0;
    }
    if (!std::strcmp(cmd, "field") && argc == 9) {
        float c[core::pixel_registers * 4];
        core::Radiance r;
        frame_constants(argv + 2, f(argv[7]), c, &r);
        const float step = f(argv[8]);
        // The glow outside the core (D = 0; r >= 1 core radius): its largest radiance (under 1.0 engine: the meter's guard
        // and AgX contrast), the hue ratio's largest deviation from the polled colour, and its largest rise outward along
        // 72 radial lines (monotone: corona, halo and the rays' along-profile all fall with r).
        float glow_max = 0.f, hue_error = 0.f, halo_rise = 0.f;
        for (float y = -core::quad_extent_y; y <= core::quad_extent_y; y += step)
            for (float x = -core::quad_extent_x; x <= core::quad_extent_x; x += step) {
                core::Pixel p;
                core::reference_pixel(c, x, y, 9, 1.f, &p);
                if (p.core > 0.f) continue;
                const float g = p.corona + p.halo + p.rays;
                glow_max = g > glow_max ? g : glow_max;
                for (int i = 0; i < 3 && g > 1e-6f; ++i) {
                    const float want = r.glow[i] * g, e = std::fabs(p.rgb[i] - want) / (want > 1e-6f ? want : 1e-6f);
                    hue_error = e > hue_error ? e : hue_error;
                }
            }
        for (unsigned line = 0; line < 72; ++line) {
            const float theta = float(line) * core::two_pi / 72.f;
            float previous = 1e30f;
            for (float rr = 1.f; rr <= core::glow_radius; rr += step / 4.f) {
                core::Pixel p;
                core::reference_pixel(c, rr * std::cos(theta), rr * std::sin(theta), 9, 1.f, &p);
                const float g = p.corona + p.halo + p.rays, rise = g - previous;
                halo_rise = rise > halo_rise ? rise : halo_rise;
                previous = g;
            }
        }
        // Per spoke: its centre angle, the ray's reach (the last radius above 1 % of the gain) and peak, and the
        // mirror symmetry of the profile about the spoke's axis at 3 core radii.
        std::printf("glow_max=%.5f hue_error=%.3g glow_rise=%.3g spokes=", double(glow_max), double(hue_error),
                    double(halo_rise));
        float asymmetry = 0.f;
        for (unsigned k = 0; k < core::ray_count; ++k) {
            const float theta = (float(k) - core::ray_offset) * core::two_pi / float(core::ray_count);
            float reach = 0.f, peak = 0.f;
            for (float rr = 1.f; rr <= core::glow_radius; rr += .05f) {
                core::Pixel p;
                core::reference_pixel(c, rr * std::cos(theta), rr * std::sin(theta), 9, 1.f, &p);
                if (p.rays > .01f * core::ray_gain) reach = rr;
                peak = p.rays > peak ? p.rays : peak;
            }
            for (float delta = .002f; delta < .03f; delta += .004f) {
                core::Pixel a, b;
                core::reference_pixel(c, 3.f * std::cos(theta + delta), 3.f * std::sin(theta + delta), 9, 1.f, &a);
                core::reference_pixel(c, 3.f * std::cos(theta - delta), 3.f * std::sin(theta - delta), 9, 1.f, &b);
                const float e = std::fabs(a.rays - b.rays);
                asymmetry = e > asymmetry ? e : asymmetry;
            }
            std::printf("%s%.2f:%.4f", k ? "," : "", double(reach), double(peak));
        }
        std::printf(" asymmetry=%.3g\n", double(asymmetry));
        return 0;
    }
    if (!std::strcmp(cmd, "bench") && argc == 3) {
        // A sun ahead of a rotating camera; the inputs vary per frame so nothing is hoisted out of the loop.
        const unsigned frames = unsigned(std::atoi(argv[2]));
        core::Camera c;
        c.r[0] = c.r[4] = c.r[8] = 1.f;
        c.m00 = 1.f;
        c.m11 = 1.7777778f;
        x3m::renderer::FogTransmittanceLaw law;
        law.on = true;
        law.extinction = 2e-5f;
        const std::int32_t colour[3] = {255, 230, 200};
        volatile float sink = 0.f;
        const auto begin = std::chrono::steady_clock::now();
        for (unsigned n = 0; n < frames; ++n) {
            const std::int32_t p[3] = {std::int32_t(n % 1000) * 1000, 2000, 1000000000};
            c.t[0] = float(n % 7);
            float t = 1.f;
            core::fog_scalar(law, &t);
            const core::Disc d = core::project(p, c, 5120, 1440, core::radius_default_u, .25f, -.25f);
            core::Radiance r;
            core::radiance(colour, core::radiance_default, t, &r);
            core::Vertex v[4];
            core::quad(d, v);
            float k[core::pixel_registers * 4];
            core::pixel_constants(5120, 1440, d, r, core::rays_default, k);
            sink = sink + v[3].x + k[12];
        }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count();
        std::printf("frames=%u ns_per_frame=%.1f sink=%.3f\n", frames, ns / double(frames ? frames : 1), double(sink));
        return 0;
    }
    return 2;
}
