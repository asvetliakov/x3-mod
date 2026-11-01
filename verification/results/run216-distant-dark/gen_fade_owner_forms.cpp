// The fade owner's lane forms (motion_output.cpp owner_form; linear-distance-fade-region.md "Fade-band fill"): for
// each original ps_<hash>.bin, with the fade-owner depth fragment on (material_motion_configure_fade_owner) and depth
// on, the fill (K 0.01), gained (gain 4, far-fade dynamic) and widened (K 4, B 4) variants each passed through
// material_motion_invalid_sun_share, plus the material-strength twin of each form where the program has one
// (kinds 8-10, motion_output_material_strength_inc.h). Written as <hash>-<form>.bin for native_validate.exe
// (verification/results/hull-widen-native-validator/). Game-derived bytes: keep the output directory untracked.
// usage: gen_fade_owner_forms <capture dir> <out dir>
#include "../../../src/renderer/linear_material.h"
#include "../../../src/renderer/material_motion.h"
#include "../../../src/renderer/ps3_program_slots.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace x3m::renderer;
namespace fs = std::filesystem;
using Words = std::vector<std::uint32_t>;
static Words load(const fs::path& path) {
    std::ifstream s(path, std::ios::binary);
    std::vector<char> b((std::istreambuf_iterator<char>(s)), {});
    Words w(b.size() / 4);
    std::memcpy(w.data(), b.data(), w.size() * 4);
    return w;
}
static void save(const fs::path& path, const Words& w) {
    std::ofstream s(path, std::ios::binary);
    s.write(reinterpret_cast<const char*>(w.data()), std::streamsize(w.size() * 4));
}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    material_motion_configure_fade_owner(true);
    const fs::path in = argv[1], out = argv[2];
    const HullLightmapWiden widen{4.0f, 4.0f};
    unsigned programs = 0, forms = 0, twins = 0;
    for (const auto& entry : fs::directory_iterator(in)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("ps_", 0) != 0 || entry.path().extension() != ".bin") continue;
        const auto original = load(entry.path());
        const std::string hash = name.substr(3, name.size() - 7);
        Words motion;
        if (material_motion_pixel_variant(original.data(), original.size(), motion, true) !=
                MaterialMotionResult::Applied ||
            !material_motion_invalid_sun_share(motion))
            continue; // unreviewed or no depth write: no sun_motion_variant, no owner forms
        ++programs;
        for (unsigned kind = 0; kind < 3; ++kind) {
            Words parent;
            bool fill = false, gain = false, widened = false;
            LinearMaterialResult result;
            if (kind == 0)
                result = linear_material_original_fill_pixel_variant(original.data(), original.size(), 0.01f, parent,
                                                                     true, fill);
            else
                result = linear_material_hull_lightmap_gain_pixel_variant(
                    original.data(), original.size(), 0.01f, 4.0f, parent, true, fill, gain, true,
                    kind == 2 ? &widen : nullptr, kind == 2 ? &widened : nullptr);
            const bool applied = result == LinearMaterialResult::Applied &&
                                 (kind == 0 ? fill : kind == 1 ? gain : gain && widened);
            if (!applied) continue;
            const char* form = kind == 0 ? "ofill" : kind == 1 ? "ogain" : "owiden";
            if (!material_motion_invalid_sun_share(parent)) {
                std::printf("%s %s invalid_share_refused\n", hash.c_str(), form);
                continue;
            }
            save(out / (hash + "-" + form + ".bin"), parent);
            ++forms;
            Words twin;
            if (linear_material_strength_twin_pixel_variant(parent.data(), parent.size(), original.data(),
                                                            original.size(), twin) == LinearMaterialResult::Applied) {
                save(out / (hash + "-" + form + "-strength.bin"), twin);
                ++twins;
            }
            std::printf("%s %s words=%zu slots=%u strength=%d\n", hash.c_str(), form, parent.size(),
                        ps3_program_slots(parent.data(), parent.size()), int(!twin.empty()));
        }
    }
    std::printf("programs=%u forms=%u strength_twins=%u\n", programs, forms, twins);
    return 0;
}
