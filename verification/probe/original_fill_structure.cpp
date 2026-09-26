// Host-only driver of the original-shading fill transformer
// (linear_material_original_fill_pixel_variant, option C of
// docs/architecture/original-shading-critique.md 1a). Reads every ps_/vs_
// program of the local corpus, writes the K=0 / K variants and the plain
// motion control next to them in a caller-supplied local folder, and prints
// one JSON object with per-program rows for the Python oracle. No game bytes
// are embedded; unreviewed programs are reported, never asserted.
#include "../../src/renderer/linear_material.h"
#include "../../src/renderer/material_motion.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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
// Independent weighted-slot walk (Microsoft SM3 slot table; the same one the
// linear-material structural driver uses).
std::pair<unsigned, unsigned> weighted_slots(const Words& result) {
    unsigned executable = 0, weighted = 0;
    std::array<unsigned, 16> texture_dimensions{};
    for (std::size_t at = 1; at < result.size() - 1;) {
        const auto token = result[at], op = token & 0xffffu;
        const unsigned count = op == 0xfffeu ? (token >> 16) & 0x7fffu : (token >> 24) & 15u;
        if (op == 31 && (((result[at + 2] >> 28) & 7) | ((result[at + 2] >> 8) & 24)) == 10)
            texture_dimensions.at(result[at + 2] & 0x7ff) = (result[at + 1] >> 27) & 15;
        if (op != 0xfffeu && op != 31 && op != 81) {
            ++executable;
            switch (op) {
            case 1:
            case 2:
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
            case 10:
            case 11:
            case 12:
            case 14:
            case 15:
            case 35:
            case 42:
            case 43:
            case 46:
            case 88: weighted += 1; break;
            case 18:
            case 39:
            case 90: weighted += 2; break;
            case 32:
            case 36:
            case 38:
            case 40:
            case 41: weighted += 3; break;
            case 66: {
                const auto dimension = texture_dimensions.at(result[at + 3] & 0x7ff);
                require(dimension == 2 || dimension == 3, "known sample slot dimension");
                weighted += dimension == 3 ? 4 : 1;
                break;
            }
            default: require(false, "known SM3 slot cost");
            }
        }
        at += count + 1;
    }
    require(result.back() == 0xffffu, "END token");
    return {executable, weighted};
}
int main(int argc, char** argv) {
    try {
        require(argc == 3, "usage: original_fill_structure <original directory> <local output directory>");
        const fs::path corpus = argv[1], out = argv[2];
        const float fills[] = {0.0f, 0.03f, 0.05f};
        std::vector<std::string> names;
        for (const auto& entry : fs::directory_iterator(corpus)) {
            const auto name = entry.path().filename().string();
            if (entry.is_regular_file() && name.size() > 7 && name.compare(name.size() - 4, 4, ".bin") == 0 &&
                (name.rfind("ps_", 0) == 0 || name.rfind("vs_", 0) == 0))
                names.push_back(name.substr(0, name.size() - 4));
        }
        std::sort(names.begin(), names.end());
        require(!names.empty(), "corpus has programs");
        std::cout << "{\"rows\":[";
        bool first = true;
        unsigned applied = 0, supported = 0;
        long long create_ns = 0;
        unsigned max_slots = 0;
        for (const auto& name : names) {
            const auto original = read(corpus / (name + ".bin"));
            const auto saved = original;
            Words probe{91, 92};
            bool entered = true;
            const auto begin = std::chrono::steady_clock::now();
            const auto status = linear_material_original_fill_pixel_variant(original.data(), original.size(), 0.05f,
                                                                            probe, true, entered);
            create_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin)
                             .count();
            require(original == saved, "immutable input");
            if (status != LinearMaterialResult::Applied) {
                require(probe == Words({91, 92}) && !entered, "refusal leaves output intact and reports no fill");
                std::cout << (first ? "" : ",") << "{\"name\":\"" << name << "\",\"words\":" << original.size()
                          << ",\"status\":" << int(status) << "}";
                first = false;
                continue;
            }
            ++supported;
            unsigned motion_slots[2]{}, variant_slots[2]{}, variant_instructions[2]{}, motion_instructions[2]{};
            bool fill_applied_any = false;
            for (bool depth : {false, true}) {
                Words motion;
                require(material_motion_pixel_variant(original.data(), original.size(), motion, depth) ==
                            MaterialMotionResult::Applied,
                        "motion control");
                write(out / (name + "-motion-" + std::to_string(depth) + ".bin"), motion);
                std::tie(motion_instructions[depth], motion_slots[depth]) = weighted_slots(motion);
                for (unsigned f = 0; f < 3; ++f) {
                    Words result{0x12345678};
                    bool fill_applied = true;
                    const auto outcome = linear_material_original_fill_pixel_variant(
                        original.data(), original.size(), fills[f], result, depth, fill_applied);
                    require(outcome == LinearMaterialResult::Applied, "reviewed original admission for every K");
                    require(original == saved, "immutable input");
                    if (f == 0)
                        require(!fill_applied && result == motion, "K=0 is the plain motion variant byte for byte");
                    else {
                        fill_applied_any = fill_applied_any || fill_applied;
                        if (!fill_applied)
                            require(result == motion, "a refused site keeps the plain motion variant");
                        else {
                            const auto [instructions, slots] = weighted_slots(result);
                            variant_instructions[depth] = instructions;
                            variant_slots[depth] = slots;
                            require(slots <= 512, "minimum SM3 static slot budget");
                            max_slots = std::max(max_slots, slots);
                        }
                    }
                    Words alias = original;
                    bool alias_applied = false;
                    require(linear_material_original_fill_pixel_variant(alias.data(), alias.size(), fills[f], alias,
                                                                        depth, alias_applied) ==
                                    LinearMaterialResult::Applied &&
                                alias == result && alias_applied == fill_applied,
                            "input/output alias");
                    write(out / (name + "-ofill-" + std::to_string(f) + "-" + std::to_string(depth) + ".bin"), result);
                }
            }
            for (float invalid : {-0.001f, 0.5001f, std::numeric_limits<float>::infinity(),
                                  -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
                Words guard{91, 92};
                bool guard_applied = true;
                require(linear_material_original_fill_pixel_variant(original.data(), original.size(), invalid, guard,
                                                                    true, guard_applied) ==
                                LinearMaterialResult::InvalidConfig &&
                            guard == Words({91, 92}) && !guard_applied,
                        "invalid fill rollback");
                Words alias = original;
                require(linear_material_original_fill_pixel_variant(alias.data(), alias.size(), invalid, alias, true,
                                                                    guard_applied) ==
                                LinearMaterialResult::InvalidConfig &&
                            alias == saved,
                        "invalid fill alias rollback");
            }
            for (std::size_t offset : {std::size_t(0), std::size_t(2), original.size() / 2, original.size() - 1}) {
                auto broken = original;
                broken[offset] ^= 1;
                const auto before = broken;
                bool broken_applied = true;
                require(linear_material_original_fill_pixel_variant(broken.data(), broken.size(), 0.05f, broken, true,
                                                                    broken_applied) != LinearMaterialResult::Applied &&
                            broken == before && !broken_applied,
                        "corrupted original alias rollback");
            }
            {
                Words guard{91, 92};
                bool guard_applied = true;
                require(linear_material_original_fill_pixel_variant(nullptr, original.size(), 0.05f, guard, true,
                                                                    guard_applied) ==
                                LinearMaterialResult::InvalidInput &&
                            guard == Words({91, 92}) && !guard_applied,
                        "null rollback");
                require(linear_material_original_fill_pixel_variant(original.data(), 1, 0.05f, guard, true,
                                                                    guard_applied) ==
                                LinearMaterialResult::InvalidInput &&
                            guard == Words({91, 92}),
                        "short rollback");
                require(linear_material_original_fill_pixel_variant(original.data(), original.size() - 1, 0.05f, guard,
                                                                    true,
                                                                    guard_applied) != LinearMaterialResult::Applied &&
                            guard == Words({91, 92}),
                        "truncation rollback");
            }
            if (fill_applied_any) ++applied;
            std::cout << (first ? "" : ",") << "{\"name\":\"" << name << "\",\"words\":" << original.size()
                      << ",\"status\":0,\"fill_applied\":" << (fill_applied_any ? 1 : 0) << ",\"motion_slots\":["
                      << motion_slots[0] << ',' << motion_slots[1] << "],\"variant_slots\":[" << variant_slots[0] << ','
                      << variant_slots[1] << ']' << ",\"motion_instructions\":[" << motion_instructions[0] << ','
                      << motion_instructions[1] << "],\"variant_instructions\":[" << variant_instructions[0] << ','
                      << variant_instructions[1] << "]}";
            first = false;
        }
        Words authored{0xffff0300u, 0xffffu}, result{91, 92};
        bool authored_applied = true;
        require(linear_material_original_fill_pixel_variant(authored.data(), authored.size(), 0.05f, result, true,
                                                            authored_applied) ==
                        LinearMaterialResult::UnsupportedShader &&
                    result == Words({91, 92}) && !authored_applied,
                "unreviewed valid framing");
        std::cout << "],\"programs\":" << names.size() << ",\"supported\":" << supported << ",\"applied\":" << applied
                  << ",\"max_variant_slots\":" << max_slots << ",\"checks\":" << checks
                  << ",\"creates_ns\":" << create_ns << "}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
