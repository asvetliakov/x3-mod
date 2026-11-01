// Host-side generator of every shader variant the proxy creates from an FL
// motion row (src/proxy/motion_output.cpp, CreatePixelShader / CreateVertexShader):
//   ps: material_motion_pixel_variant with current depth on (-motion-depth) and
//       off (-motion), and the sun-lane sibling of the depth variant
//       (material_motion_invalid_sun_share, -sunlane);
//   vs: material_motion_vertex_variant with current depth on and off.
// It also reports what the linear-material variants built on the motion
// variant (original fill, hull light-map gain, sun share) return for each FL
// pixel program: until part 4b they must refuse (UnsupportedShader = no row).
// Output files are game-derived bytes: keep the output directory untracked.
// usage: gen_fl_motion_variants <out dir> <programs dir> (ps_<id>|vs_<id>)...
#include "../../../src/renderer/linear_material.h"
#include "../../../src/renderer/material_motion.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace x3m::renderer;
using Words = std::vector<std::uint32_t>;
static bool load(const std::string& path, Words& w) {
    std::ifstream s(path, std::ios::binary);
    if (!s) return false;
    std::vector<char> b((std::istreambuf_iterator<char>(s)), {});
    w.resize(b.size() / 4);
    std::memcpy(w.data(), b.data(), w.size() * 4);
    return true;
}
static void save(const std::string& path, const Words& w) {
    std::ofstream s(path, std::ios::binary);
    s.write(reinterpret_cast<const char*>(w.data()), std::streamsize(w.size() * 4));
}
int main(int argc, char** argv) {
    if (argc < 4) return 2;
    const std::string out = argv[1], in = argv[2];
    int failures = 0;
    for (int a = 3; a < argc; ++a) {
        const std::string name = argv[a];
        Words original;
        if (!load(in + "/" + name + ".bin", original)) {
            std::printf("%s missing\n", name.c_str());
            ++failures;
            continue;
        }
        if (name.compare(0, 3, "vs_") == 0) {
            for (const bool depth : {true, false}) {
                Words v;
                const auto r = material_motion_vertex_variant(original.data(), original.size(), v, depth);
                const std::string kind = depth ? "motion-depth" : "motion";
                std::printf("%s kind=%s result=%u original=%zu words=%zu\n", name.c_str(), kind.c_str(), unsigned(r),
                            original.size(), v.size());
                if (r != MaterialMotionResult::Applied) ++failures;
                else save(out + "/" + name + "-" + kind + ".bin", v);
            }
            continue;
        }
        const auto* row = material_motion_pixel_row(material_motion_fingerprint(original.data(), original.size()),
                                                    original.size());
        for (const bool depth : {true, false}) {
            Words v;
            const auto r = material_motion_pixel_variant(original.data(), original.size(), v, depth);
            const std::string kind = depth ? "motion-depth" : "motion";
            const bool writes_depth = row && material_motion_pixel_writes_depth(*row, depth);
            std::printf("%s kind=%s result=%u original=%zu words=%zu writes_depth=%d\n", name.c_str(), kind.c_str(),
                        unsigned(r), original.size(), v.size(), writes_depth);
            if (r != MaterialMotionResult::Applied) {
                ++failures;
                continue;
            }
            save(out + "/" + name + "-" + kind + ".bin", v);
            if (writes_depth) { // The proxy's sun-lane sibling exists only for a depth-writing variant.
                const bool lane = material_motion_invalid_sun_share(v);
                std::printf("%s kind=sunlane applied=%d words=%zu\n", name.c_str(), lane, v.size());
                if (!lane) ++failures;
                else save(out + "/" + name + "-sunlane.bin", v);
            }
        }
        Words w;
        bool fill = false, gain = false, share = false;
        const auto f = linear_material_original_fill_pixel_variant(original.data(), original.size(), 0.01f, w, true, fill);
        const auto g = linear_material_hull_lightmap_gain_pixel_variant(original.data(), original.size(), 0.01f, 2.0f, w,
                                                                        true, fill, gain, true);
        const auto s = linear_material_original_sun_share_pixel_variant(original.data(), original.size(), 0.01f, w, true,
                                                                        share);
        std::printf("%s linear fill=%u gain=%u share=%u\n", name.c_str(), unsigned(f), unsigned(g), unsigned(s));
    }
    std::printf("failures=%d\n", failures);
    return failures ? 1 : 0;
}
