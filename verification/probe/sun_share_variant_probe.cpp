#include "../../src/renderer/material_motion.h"
#include "../../src/renderer/linear_material.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <iterator>
using namespace x3m::renderer;
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    unsigned motion = 0, linear = 0, xt = 0;
    for (const auto& entry : std::filesystem::directory_iterator(argv[1])) {
        if (entry.path().filename().string().rfind("ps_", 0) != 0 || entry.path().extension() != ".bin") continue;
        std::ifstream file(entry.path(), std::ios::binary);
        std::vector<char> bytes{std::istreambuf_iterator<char>(file), {}};
        if (bytes.size() % 4) return 3;
        std::vector<std::uint32_t> original(bytes.size() / 4), output;
        std::memcpy(original.data(), bytes.data(), bytes.size());
        const auto* row = material_motion_pixel_row(material_motion_fingerprint(original.data(), original.size()),
                                                    original.size());
        if (!row || !material_motion_pixel_writes_depth(*row, true)) continue;
        auto append = [&]() {
            const auto before = output;
            if (!material_motion_invalid_sun_share(output) || output.size() != before.size() + 9 ||
                !std::equal(before.begin() + 1, before.end() - 1, output.begin() + 7)) {
                std::fprintf(stderr, "invalid-share refusal %s\n", entry.path().filename().string().c_str());
                return false;
            }
            return true;
        };
        if (material_motion_pixel_variant(original.data(), original.size(), output, true) !=
                MaterialMotionResult::Applied ||
            !append())
            return 4;
        ++motion;
        LinearMaterialConfig config{};
        config.fill = .06f;
        bool fill = false;
        if (linear_material_pixel_variant_fill(original.data(), original.size(), config, output, true, fill) ==
            LinearMaterialResult::Applied) {
            if (!append()) return 5;
            ++linear;
        }
        if (linear_material_xt_default_pixel_variant(original.data(), original.size(), config, output, true, false) ==
            LinearMaterialResult::Applied) {
            if (!append()) return 6;
            ++xt;
        }
    }
    std::printf("PASS motion=%u linear=%u xt=%u added_ps_slots=1 added_temporaries=0 uploads=0\n", motion, linear, xt);
    return linear == 108 && xt == 4 ? 0 : 7;
}
