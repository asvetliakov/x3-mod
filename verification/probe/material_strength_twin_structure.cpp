// Host-only driver of the merged-LOD material-strength twin
// (linear_material_strength_twin_pixel_variant; docs/architecture/overlay-material-strength.md 5).
// Reads every ps_ program of the local corpus, builds the 14 original-shading
// parent forms the proxy binds (the plain motion variant, the fill, the gained,
// the widened, the share producer, its gained and widened forms, and the
// engine-light twin of each), writes each parent and its strength twin
// next to each other in a caller-supplied local folder and prints one JSON
// object with per-program rows for the Python oracle
// (verification/analysis/test_material_strength.py). No game bytes are
// embedded; unreviewed programs are reported, never asserted.
#include "../../src/renderer/linear_material.h"
#include "../../src/renderer/material_motion.h"
#include "../../src/renderer/ps3_program_slots.h"
#include "../../src/renderer/ps3_slot_budget.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace x3m::renderer;
using Words = std::vector<std::uint32_t>;
namespace fs = std::filesystem;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
Words read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    require(bool(stream), "open original");
    const auto bytes = stream.tellg();
    require(bytes > 0 && bytes % 4 == 0, "aligned original");
    Words result(static_cast<std::size_t>(bytes) / 4);
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(result.data()), bytes);
    require(bool(stream), "read original");
    return result;
}
void write(const fs::path& path, const Words& words) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(words.data()), static_cast<std::streamsize>(words.size() * 4));
    require(bool(stream), "write local variant");
}
struct Kind {
    const char* name;
    OriginalVariantOptions options;
    bool engine;
};
int main(int argc, char** argv) {
    try {
        require(argc == 3, "usage: material_strength_twin_structure <original directory> <local output directory>");
        const fs::path corpus = argv[1], out = argv[2];
        const HullLightmapWiden widen{3.0f, 3.0f};
        OriginalVariantOptions motion, fill, gain, widened, share, share_gain;
        fill.fill = 0.05f;
        gain.fill = 0.05f;
        gain.lightmap_gain = 4.0f;
        widened = gain;
        widened.widen = &widen;
        widened.lightmap_dynamic = true;
        share.fill = 0.05f;
        share.share = true;
        share_gain = gain;
        share_gain.share = true;
        OriginalVariantOptions share_widen = widened;
        share_widen.share = true;
        // The 14 forms the proxy can bind: the seven original-shading kinds and their engine-light twins
        // (motion_output_material_strength_inc.h kind order 0..6: motion, fill, gain, widen, share, sgain, swiden).
        const Kind kinds[] = {{"motion", motion, false},  {"fill", fill, false},        {"gain", gain, false},
                              {"widen", widened, false},  {"share", share, false},      {"sgain", share_gain, false},
                              {"swiden", share_widen, false}, {"emotion", motion, true}, {"efill", fill, true},
                              {"egain", gain, true},      {"ewiden", widened, true},    {"eshare", share, true},
                              {"esgain", share_gain, true}, {"eswiden", share_widen, true}};
        std::vector<std::string> names;
        for (const auto& entry : fs::directory_iterator(corpus)) {
            const auto name = entry.path().filename().string();
            if (entry.is_regular_file() && name.size() > 7 && name.compare(name.size() - 4, 4, ".bin") == 0 &&
                name.rfind("ps_", 0) == 0)
                names.push_back(name.substr(0, name.size() - 4));
        }
        std::sort(names.begin(), names.end());
        require(!names.empty(), "corpus has programs");
        std::cout << "{\"rows\":[";
        bool first = true;
        for (const auto& name : names) {
            const auto original = read(corpus / (name + ".bin"));
            const auto hash = material_motion_fingerprint(original.data(), original.size());
            const unsigned stage = linear_material_strength_stage(hash, original.size());
            for (const auto& kind : kinds) {
                Words parent;
                bool engine_applied = false, share_applied = false, gain_applied = false, widen_applied = false;
                const auto status = linear_material_original_variant_pixel(
                    original.data(), original.size(), kind.options, kind.engine, parent, true, nullptr,
                    &share_applied, &gain_applied, &widen_applied, &engine_applied);
                if (status != LinearMaterialResult::Applied) continue; // unreviewed, or a form the program lacks
                if (kind.engine && !engine_applied) continue;
                if (kind.options.share && !share_applied) continue;
                if (kind.options.lightmap_gain != 1.f && !gain_applied) continue;
                if (kind.options.widen && !widen_applied) continue;
                const auto saved = parent;
                Words twin{91, 92};
                StrengthTwinInfo info;
                StrengthTwinRefusal refusal = StrengthTwinRefusal::None;
                const auto twin_status = linear_material_strength_twin_pixel_variant(
                    parent.data(), parent.size(), original.data(), original.size(), twin, &info, &refusal);
                require(parent == saved, "immutable parent");
                const std::string base = name + "-" + kind.name;
                write(out / (base + "-parent.bin"), parent);
                std::cout << (first ? "" : ",") << "{\"name\":\"" << name << "\",\"kind\":\"" << kind.name
                          << "\",\"stage\":" << stage << ",\"parent_words\":" << parent.size()
                          << ",\"parent_slots\":" << ps3_program_slots(parent.data(), parent.size())
                          << ",\"status\":" << int(twin_status) << ",\"refusal\":\""
                          << linear_material_strength_refusal_name(refusal) << "\",\"lanes_read\":" << info.lanes_read;
                if (twin_status == LinearMaterialResult::Applied) {
                    write(out / (base + "-twin.bin"), twin);
                    std::cout << ",\"twin_words\":" << twin.size()
                              << ",\"twin_slots\":" << ps3_program_slots(twin.data(), twin.size())
                              << ",\"texel_at\":" << info.texel_at << ",\"join_at\":" << info.join_at
                              << ",\"source_register\":" << info.source_register
                              << ",\"scratch_register\":" << info.scratch_register
                              << ",\"product\":" << (info.product ? "true" : "false");
                } else
                    require(twin == Words({91, 92}), "refusal leaves output intact");
                std::cout << "}";
                first = false;
            }
        }
        std::cout << "],\"programs\":" << names.size() << ",\"slot_budget\":" << ps3_slot_budget() << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
