// Host-side reproduction of the 2026-10 native CreatePixelShader refusal of the
// hull emissive widening variants: builds, for each captured original ps_<hash>.bin,
// the gained variant and the widened variant with the flight's parameters
// (fill 0.01, gain 2, depth 1, far-fade dynamic gain, K = 4, B = 4; plus the static-gain
// widened variant, -swiden) through
// linear_material_hull_lightmap_gain_pixel_variant, and writes both to the
// output directory (game-derived bytes: keep the output untracked).
// usage: gen_widen_variants <capture dir> <out dir> <hash>...
#include "../../../src/renderer/linear_material.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace x3m::renderer;
static bool load(const std::string& path, std::vector<std::uint32_t>& w) {
    std::ifstream s(path, std::ios::binary);
    if (!s) return false;
    std::vector<char> b((std::istreambuf_iterator<char>(s)), {});
    w.resize(b.size() / 4);
    std::memcpy(w.data(), b.data(), w.size() * 4);
    return true;
}
static void save(const std::string& path, const std::vector<std::uint32_t>& w) {
    std::ofstream s(path, std::ios::binary);
    s.write(reinterpret_cast<const char*>(w.data()), std::streamsize(w.size() * 4));
}
int main(int argc, char** argv) {
    if (argc < 4) return 2;
    const std::string in = argv[1], out = argv[2];
    const HullLightmapWiden widen{4.0f, 4.0f};
    for (int a = 3; a < argc; ++a) {
        const std::string h = argv[a];
        std::vector<std::uint32_t> o, g, w;
        if (!load(in + "/ps_" + h + ".bin", o)) { std::printf("%s missing\n", h.c_str()); continue; }
        bool fa = false, ga = false, wf = false, wg = false, wa = false;
        const auto gr = linear_material_hull_lightmap_gain_pixel_variant(o.data(), o.size(), 0.01f, 2.0f, g, true, fa, ga, true);
        const auto wr = linear_material_hull_lightmap_gain_pixel_variant(o.data(), o.size(), 0.01f, 2.0f, w, true, wf, wg, true,
                                                                         &widen, &wa);
        std::printf("%s original=%zu gain_result=%u gain_words=%zu gain_applied=%d widen_result=%u widen_words=%zu widen_applied=%d\n",
                    h.c_str(), o.size(), unsigned(gr), g.size(), ga, unsigned(wr), w.size(), wa);
        save(out + "/" + h + "-gain.bin", g);
        save(out + "/" + h + "-widen.bin", w);
        // The static-gain widened variant (light-map far fade off: DEF c223, c223.x).
        std::vector<std::uint32_t> sw;
        bool sf = false, sg = false, sa = false;
        const auto sr = linear_material_hull_lightmap_gain_pixel_variant(o.data(), o.size(), 0.01f, 2.0f, sw, true, sf, sg, false,
                                                                         &widen, &sa);
        std::printf("%s static_widen_result=%u words=%zu applied=%d\n", h.c_str(), unsigned(sr), sw.size(), sa);
        save(out + "/" + h + "-swiden.bin", sw);
        // The sun-share producer's gained and widened siblings (motion_output.cpp sun_shadow_original_lightmap_*).
        std::vector<std::uint32_t> pg, pw;
        bool ps = false, pa = false, qs = false, qg = false, qa = false;
        const auto pr = linear_material_original_sun_share_pixel_variant(o.data(), o.size(), 0.01f, pg, true, ps, 2.0f, &pa, true);
        const auto qr = linear_material_original_sun_share_pixel_variant(o.data(), o.size(), 0.01f, pw, true, qs, 2.0f, &qg, true,
                                                                         &widen, &qa);
        std::printf("%s share_gain_result=%u words=%zu share=%d gain=%d share_widen_result=%u words=%zu share=%d widen=%d\n",
                    h.c_str(), unsigned(pr), pg.size(), ps, pa, unsigned(qr), pw.size(), qs, qa);
        save(out + "/" + h + "-sgain.bin", pg);
        save(out + "/" + h + "-swidenshare.bin", pw);
    }
}
