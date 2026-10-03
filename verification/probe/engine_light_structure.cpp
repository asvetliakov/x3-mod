// Host-only driver of the engine light's pixel twin
// (linear_material_original_engine_light_pixel_variant,
// docs/architecture/engine-light.md). For every ps_ program of the local
// corpus and each of the six original-shading option sets the route builds
// (fill K, share producer, light-map gain with the dynamic far fade, widening),
// it builds the base variant through the existing entry point and its twin,
// writes both into a caller-supplied local folder and prints one JSON object
// with per-program rows for the Python oracle
// (verification/analysis/test_engine_light.py): status, engine_applied and the
// weighted slots of base and twin (the oracle re-derives the expected engine
// words and compares the twin minus them with the base). No game bytes are
// embedded; the written variants stay in the caller's temporary folder.
#include "../../src/renderer/linear_material.h"
#include "../../src/renderer/material_motion.h"
#include <algorithm>
#include <array>
#include <chrono>
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
unsigned checks = 0;
void require(bool condition, const char* message) {
    ++checks;
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
unsigned weighted_slots(const Words& result) {
    unsigned weighted = 0;
    std::array<unsigned, 16> dims{};
    for (std::size_t at = 1; at < result.size() - 1;) {
        const auto token = result[at], op = token & 0xffffu;
        const unsigned count = op == 0xfffeu ? (token >> 16) & 0x7fffu : (token >> 24) & 15u;
        if (op == 31 && (((result[at + 2] >> 28) & 7) | ((result[at + 2] >> 8) & 24)) == 10)
            dims.at(result[at + 2] & 0x7ff) = (result[at + 1] >> 27) & 15;
        if (op != 0xfffeu && op != 31 && op != 81) {
            switch (op) {
            case 18:
            case 39:
            case 90:
            case 91:
            case 92: weighted += 2; break;
            case 32:
            case 36:
            case 38:
            case 40:
            case 41:
            case 93: weighted += 3; break;
            case 66: weighted += dims.at(result[at + 3] & 0x7ff) == 3 ? 4 : 1; break;
            default: weighted += 1;
            }
        }
        at += count + 1;
    }
    return weighted;
}
struct Set {
    const char* name;
    float fill;
    bool share;
    float gain;
    bool widen;
};
int main(int argc, char** argv) {
    try {
        require(argc == 3, "usage: engine_light_structure <original directory> <local output directory>");
        const fs::path corpus = argv[1], out = argv[2];
        const HullLightmapWiden widen{4.f, 4.f};
        const Set sets[] = {{"fill", .01f, false, 1.f, false},       {"fill0", 0.f, false, 1.f, false},
                            {"gain", .01f, false, 4.f, false},       {"widen", .01f, false, 4.f, true},
                            {"share", .01f, true, 1.f, false},       {"share_gain", .01f, true, 4.f, false},
                            {"share_widen", .01f, true, 4.f, true},  {"share0", 0.f, true, 1.f, false}};
        std::vector<std::string> names;
        for (const auto& entry : fs::directory_iterator(corpus)) {
            const auto name = entry.path().filename().string();
            if (entry.is_regular_file() && name.rfind("ps_", 0) == 0 && name.size() > 7 &&
                name.compare(name.size() - 4, 4, ".bin") == 0)
                names.push_back(name.substr(0, name.size() - 4));
        }
        std::sort(names.begin(), names.end());
        std::cout << "{\"rows\":[";
        bool first = true;
        long long twin_ns = 0;
        unsigned twins = 0;
        for (const auto& name : names) {
            const auto original = read(corpus / (name + ".bin"));
            const auto saved = original;
            std::cout << (first ? "" : ",") << "{\"name\":\"" << name << "\",\"sets\":{";
            first = false;
            bool first_set = true;
            for (const auto& set : sets) {
                Words base{91}, twin{92};
                bool fill_applied = false, share_applied = false, gain_applied = false, widen_applied = false;
                LinearMaterialResult b;
                if (set.share)
                    b = linear_material_original_sun_share_pixel_variant(
                        original.data(), original.size(), set.fill, base, true, share_applied, set.gain,
                        &gain_applied, set.gain != 1.f, set.widen ? &widen : nullptr, &widen_applied);
                else if (set.gain != 1.f)
                    b = linear_material_hull_lightmap_gain_pixel_variant(
                        original.data(), original.size(), set.fill, set.gain, base, true, fill_applied, gain_applied,
                        true, set.widen ? &widen : nullptr, &widen_applied);
                else
                    b = linear_material_original_fill_pixel_variant(original.data(), original.size(), set.fill, base,
                                                                    true, fill_applied);
                OriginalVariantOptions options;
                options.fill = set.fill;
                options.share = set.share;
                options.lightmap_gain = set.gain;
                options.lightmap_dynamic = set.gain != 1.f;
                options.widen = set.widen ? &widen : nullptr;
                bool engine = true, twin_share = false, twin_gain = false, twin_widen = false;
                const auto begin = std::chrono::steady_clock::now();
                const auto t = linear_material_original_engine_light_pixel_variant(
                    original.data(), original.size(), options, twin, true, engine, &twin_share,
                    set.gain != 1.f ? &twin_gain : nullptr, set.widen ? &twin_widen : nullptr);
                twin_ns +=
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin)
                        .count();
                require(original == saved, "immutable input");
                require(t == b, "twin status equals the base status");
                if (t != LinearMaterialResult::Applied) {
                    require(twin == Words({92}) && !engine, "refusal leaves output intact");
                } else {
                    require(twin_share == share_applied && twin_gain == gain_applied && twin_widen == widen_applied,
                            "twin reports the base's options");
                    if (!engine) require(twin == base, "a refused light keeps the base variant byte for byte");
                    if (engine) ++twins;
                    // Depth off: never a light (no w interpolator).
                    Words nodepth{93};
                    bool engine_nodepth = true;
                    linear_material_original_engine_light_pixel_variant(original.data(), original.size(), options,
                                                                        nodepth, false, engine_nodepth);
                    require(!engine_nodepth, "no light without the depth export");
                }
                std::cout << (first_set ? "" : ",") << '"' << set.name << "\":{\"status\":" << int(t)
                          << ",\"engine\":" << (engine ? 1 : 0) << ",\"share\":" << (share_applied ? 1 : 0)
                          << ",\"gain\":" << (gain_applied ? 1 : 0) << ",\"widen\":" << (widen_applied ? 1 : 0)
                          << ",\"fill\":" << (fill_applied ? 1 : 0);
                if (t == LinearMaterialResult::Applied) {
                    std::cout << ",\"base_slots\":" << weighted_slots(base) << ",\"twin_slots\":" << weighted_slots(twin);
                    if (engine) {
                        write(out / (name + "-" + set.name + "-base.bin"), base);
                        write(out / (name + "-" + set.name + "-twin.bin"), twin);
                    }
                }
                std::cout << '}';
                first_set = false;
            }
            std::cout << "}}";
        }
        std::cout << "],\"programs\":" << names.size() << ",\"twins\":" << twins << ",\"checks\":" << checks
                  << ",\"twin_ns\":" << twin_ns << "}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
