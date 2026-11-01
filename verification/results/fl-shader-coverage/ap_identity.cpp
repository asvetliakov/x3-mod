// AP parity probe for part 4b: one digest line per program of a corpus over
// every public material-variant API this change touches (linear_material.cpp
// converted and original-shading forms, strength twins, stage and pair
// lookups; linear_emission.cpp hull gain). ap_identity.py builds it against the
// base commit's renderer sources and against the working tree and requires the
// two outputs to be equal line for line over the AP corpus. No shader bytes are
// written: each line carries result codes, out-flags and an FNV-1a digest.
// usage: ap_identity <programs dir> <pairs file: "vs ps" hex per line> (ps_<id>|vs_<id>)...
#include "linear_emission.h"
#include "linear_material.h"
#include "material_motion.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace x3m::renderer;
using Words = std::vector<std::uint32_t>;
namespace {
struct Digest {
    std::uint64_t h = 14695981039346656037ull;
    void add(std::uint64_t v) {
        for (unsigned b = 0; b < 8; ++b) {
            h ^= (v >> (8 * b)) & 0xffu;
            h *= 1099511628211ull;
        }
    }
    void add(const Words& w) {
        add(w.size());
        for (auto x : w) add(x);
    }
};
bool load(const std::string& path, Words& w) {
    std::ifstream s(path, std::ios::binary);
    if (!s) return false;
    std::vector<char> b((std::istreambuf_iterator<char>(s)), {});
    w.resize(b.size() / 4);
    std::memcpy(w.data(), b.data(), w.size() * 4);
    return true;
}
} // namespace
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const std::string in = argv[1];
    {
        std::ifstream pairs(argv[2]);
        std::string vs, ps;
        Digest d;
        unsigned count = 0;
        while (pairs >> vs >> ps) {
            const auto v = std::stoull(vs, nullptr, 16), p = std::stoull(ps, nullptr, 16);
            const auto c = linear_material_pair_contract(v, p);
            d.add(c.sampler_mask);
            d.add(c.bump);
            d.add(c.scalar_transport_count);
            for (const auto& t : c.scalar_transport)
                d.add((t.source_texcoord << 24) | (t.source_component << 16) | (t.destination_texcoord << 8) |
                      t.destination_component);
            d.add(linear_material_pair_reviewed(v, p));
            d.add(linear_material_asteroid_pair(v, p));
            d.add(linear_material_xt_default_pair(v, p));
            ++count;
        }
        std::printf("pairs %u %016llx\n", count, static_cast<unsigned long long>(d.h));
    }
    static const HullLightmapWiden widen{4.f, 4.f};
    for (int a = 3; a < argc; ++a) {
        const std::string name = argv[a];
        Words original;
        if (!load(in + "/" + name + ".bin", original)) {
            std::printf("%s missing\n", name.c_str());
            continue;
        }
        Digest d;
        LinearMaterialConfig plain, filled;
        filled.fill = 0.05f;
        Words out;
        if (name.compare(0, 3, "vs_") == 0) {
            for (const bool depth : {true, false}) {
                for (const auto* config : {&plain, &filled}) {
                    out.clear();
                    d.add(unsigned(linear_material_vertex_variant(original.data(), original.size(), *config, out, depth)));
                    d.add(out);
                    out.clear();
                    d.add(unsigned(linear_material_xt_default_vertex_variant(original.data(), original.size(), *config,
                                                                             out, depth)));
                    d.add(out);
                }
            }
            std::printf("%s %016llx\n", name.c_str(), static_cast<unsigned long long>(d.h));
            continue;
        }
        const auto hash = material_motion_fingerprint(original.data(), original.size());
        d.add(linear_material_hull_lightmap_stage(hash, original.size()));
        d.add(linear_material_strength_stage(hash, original.size()));
        // The index of a reviewed program (an unknown one's sentinel is the table size, 12 -> 24 by design).
        d.add(linear_emission_hull_program_reviewed(hash) ? linear_emission_hull_program_index(hash) : ~0u);
        for (const bool depth : {true, false}) {
            for (const auto* config : {&plain, &filled}) {
                bool fill = false, sun = false;
                out.clear();
                d.add(unsigned(linear_material_pixel_variant_fill(original.data(), original.size(), *config, out, depth,
                                                                  fill)));
                d.add(out);
                d.add(fill);
                out.clear();
                d.add(unsigned(linear_material_pixel_variant_sun_share(original.data(), original.size(), *config, out,
                                                                       depth, sun)));
                d.add(out);
                d.add(sun);
                out.clear();
                d.add(unsigned(linear_material_xt_default_pixel_variant(original.data(), original.size(), *config, out,
                                                                        depth)));
                d.add(out);
            }
            for (unsigned kind = 0; kind < 7; ++kind)
                for (const bool engine : {false, true})
                    for (const bool dynamic : {false, true}) {
                        OriginalVariantOptions options;
                        if (kind >= 1) options.fill = 0.01f;
                        if (kind == 2 || kind == 3 || kind == 5 || kind == 6) {
                            options.lightmap_gain = 4.f;
                            options.lightmap_dynamic = dynamic;
                        }
                        if (kind == 3 || kind == 6) options.widen = &widen;
                        options.share = kind >= 4;
                        Words parent;
                        bool f = false, s = false, g = false, w = false, e = false;
                        d.add(unsigned(linear_material_original_variant_pixel(original.data(), original.size(), options,
                                                                              engine, parent, depth, &f, &s, &g, &w,
                                                                              &e)));
                        d.add(parent);
                        d.add((f << 4) | (s << 3) | (g << 2) | (w << 1) | unsigned(e));
                        Words twin;
                        StrengthTwinRefusal why = StrengthTwinRefusal::None;
                        d.add(unsigned(linear_material_strength_twin_pixel_variant(
                            parent.data(), parent.size(), original.data(), original.size(), twin, nullptr, &why)));
                        d.add(unsigned(why));
                        d.add(twin);
                    }
        }
        for (const float clamp : {0.f, .5f, 1.f}) {
            out.clear();
            d.add(unsigned(linear_emission_hull_source_gain_variant(original.data(), original.size(), 2.f, out, clamp,
                                                                    clamp > 0.f && clamp < 1.f)));
            d.add(out);
        }
        std::printf("%s %016llx\n", name.c_str(), static_cast<unsigned long long>(d.h));
    }
    return 0;
}
