// Host-only driver of the ps_3_0 constant read-port guard
// (verification/analysis/test_linear_material_constant_port.py): for every
// ps_*.bin of the given folders writes each pixel variant the hull light-map
// transformers produce (linear_material_hull_lightmap_gain_pixel_variant and
// linear_material_original_engine_light_pixel_variant) over fill {0, 0.01},
// gain {1, 2}, current depth {0, 1}, far fade (dynamic gain) {0, 1}, share
// {0, 1} (engine light only) and widening {off, K/B = 4/4, 3/3, 8/1} into the
// output folder as <stem>-<gain|engine>-<flags>.bin (game-derived bytes: the
// output folder is a caller-supplied temporary). Prints one line per program
// and a final "variants=N widened=M" line.
// usage: linear_material_constant_port_variants <out dir> <program dir>...
#include "../../src/renderer/linear_material.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
using namespace x3m::renderer;
namespace fs = std::filesystem;
using Words = std::vector<std::uint32_t>;
static bool load(const fs::path& path, Words& w) {
    std::ifstream s(path, std::ios::binary | std::ios::ate);
    if (!s) return false;
    const auto bytes = static_cast<std::size_t>(s.tellg());
    if (bytes == 0 || bytes % 4) return false;
    w.resize(bytes / 4);
    s.seekg(0);
    s.read(reinterpret_cast<char*>(w.data()), static_cast<std::streamsize>(bytes));
    return bool(s);
}
static void save(const fs::path& path, const Words& w) {
    std::ofstream s(path, std::ios::binary);
    s.write(reinterpret_cast<const char*>(w.data()), static_cast<std::streamsize>(w.size() * 4));
}
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const fs::path out = argv[1];
    const HullLightmapWiden widens[3] = {{4.0f, 4.0f}, {3.0f, 3.0f}, {8.0f, 1.0f}};
    unsigned variants = 0, widened = 0, duplicates = 0;
    std::set<std::string> seen;  // a program present in two folders (sweep corpus and native captures) is one program
    for (int a = 2; a < argc; ++a) {
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(argv[a], error)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("ps_", 0) != 0 || entry.path().extension() != ".bin") continue;
            if (!seen.insert(name).second) { ++duplicates; continue; }
            Words original;
            if (!load(entry.path(), original) || original[0] != 0xffff0300u) continue;
            const std::string stem = entry.path().stem().string();
            unsigned program_variants = 0, program_widened = 0;
            // flags: bit 0 fill, 1 gain, 2 depth, 3 far fade, 4 share, 5-6 widening, 7 engine light.
            for (unsigned flags = 0; flags < 256; ++flags) {
                const bool fill = flags & 1, gain = flags & 2, depth = flags & 4, dynamic = flags & 8,
                           share = flags & 16, engine = flags & 128;
                const unsigned w = (flags >> 5) & 3;
                if (!engine && share) continue;
                const HullLightmapWiden* widen = w ? &widens[w - 1] : nullptr;
                Words variant;
                bool widen_applied = false, gain_applied = false;
                LinearMaterialResult result;
                if (engine) {
                    OriginalVariantOptions options;
                    options.fill = fill ? 0.01f : 0.f;
                    options.share = share;
                    options.lightmap_gain = gain ? 2.f : 1.f;
                    options.lightmap_dynamic = dynamic;
                    options.widen = widen;
                    bool engine_applied = false, share_applied = false;
                    result = linear_material_original_engine_light_pixel_variant(
                        original.data(), original.size(), options, variant, depth, engine_applied, &share_applied,
                        &gain_applied, &widen_applied);
                } else {
                    bool fill_applied = false;
                    result = linear_material_hull_lightmap_gain_pixel_variant(
                        original.data(), original.size(), fill ? 0.01f : 0.f, gain ? 2.f : 1.f, variant, depth,
                        fill_applied, gain_applied, dynamic, widen, &widen_applied);
                }
                if (result != LinearMaterialResult::Applied || variant.empty()) continue;
                char tag[32];
                std::snprintf(tag, sizeof tag, "-%s-%03u.bin", engine ? "engine" : "gain", flags);
                save(out / (stem + tag), variant);
                ++variants;
                ++program_variants;
                if (widen_applied) ++widened, ++program_widened;
            }
            std::printf("%s variants=%u widened=%u\n", stem.c_str(), program_variants, program_widened);
        }
    }
    std::printf("duplicates=%u\n", duplicates);
    std::printf("variants=%u widened=%u\n", variants, widened);
    return 0;
}
