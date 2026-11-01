// Host driver of the own sun's pure part (src/proxy/own_sun_core.h, docs/architecture/own-sun-pass.md): the production
// header, no device, no Wine. verification/analysis/test_own_sun.py runs one command per call and parses key=value.
//   project px py pz r0..r8 t0 t1 t2 m00 m11 m20 m21 width height radius_u jx jy
//   radiance c0 c1 c2 k fog_t
//   fog on extinction start cap k0 k1 k2
//   answer active eligible
//   toggle <0/1 string>            the key state per frame; prints on / edges per frame
//   frame_active configured key_on stage_ready
//   quad x y half_x half_y
//   constants width height d0 d1 d2 c0 c1 c2
//   pixel c0 c1 c2 k fog_t distance open_taps exp_term
//   bench frames                   the stage's CPU side per frame (projection, fog scalar, radiance, quad, constants)
#include "../../src/proxy/own_sun_core.h"
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
        std::printf("black=%u disc=%.7f,%.7f,%.7f corona=%.7f,%.7f,%.7f colour=%.7f,%.7f,%.7f\n", unsigned(r.black),
                    double(r.disc[0]), double(r.disc[1]), double(r.disc[2]), double(r.corona[0]), double(r.corona[1]),
                    double(r.corona[2]), double(r.colour[0]), double(r.colour[1]), double(r.colour[2]));
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
    if (!std::strcmp(cmd, "toggle") && argc == 3) {
        core::Toggle t;
        std::printf("initial=%u on=", unsigned(t.on));
        unsigned edges = 0;
        for (const char* k = argv[2]; *k; ++k) {
            edges += t.step(*k == '1');
            std::printf("%u", unsigned(t.on));
        }
        std::printf(" edges=%u\n", edges);
        return 0;
    }
    if (!std::strcmp(cmd, "frame_active") && argc == 5) {
        std::printf("active=%u\n",
                    unsigned(core::frame_active(std::atoi(argv[2]) != 0, std::atoi(argv[3]) != 0, std::atoi(argv[4]) != 0)));
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
    if (!std::strcmp(cmd, "constants") && argc == 10) {
        core::Radiance r;
        for (int i = 0; i < 3; ++i) {
            r.disc[i] = f(argv[4 + i]);
            r.corona[i] = f(argv[7 + i]);
        }
        float c[core::pixel_registers * 4];
        core::pixel_constants(unsigned(std::atoi(argv[2])), unsigned(std::atoi(argv[3])), r, c);
        std::printf("c=");
        for (unsigned i = 0; i < core::pixel_registers * 4; ++i) std::printf("%s%.9g", i ? "," : "", double(c[i]));
        std::printf("\n");
        return 0;
    }
    if (!std::strcmp(cmd, "pixel") && argc == 10) {
        const std::int32_t c[3] = {i32(argv[2]), i32(argv[3]), i32(argv[4])};
        core::Radiance r;
        core::radiance(c, f(argv[5]), f(argv[6]), &r);
        core::Pixel p;
        core::reference_pixel(r, f(argv[7]), unsigned(std::atoi(argv[8])), f(argv[9]), &p);
        std::printf("rgb=%.7f,%.7f,%.7f\n", double(p.rgb[0]), double(p.rgb[1]), double(p.rgb[2]));
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
            core::pixel_constants(5120, 1440, r, k);
            sink = sink + v[3].x + k[12];
        }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count();
        std::printf("frames=%u ns_per_frame=%.1f sink=%.3f\n", frames, ns / double(frames ? frames : 1), double(sink));
        return 0;
    }
    return 2;
}
