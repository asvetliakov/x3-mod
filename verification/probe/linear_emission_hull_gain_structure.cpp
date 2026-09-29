// Host-only driver of the hull-program emitter gain
// (linear_emission_hull_source_gain_variant, phase 3 of the emitter plan).
// Original game shader bytes stay in the local corpus; the driver writes the
// variants for the Python oracle.
#include "../../src/renderer/linear_emission.cpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using namespace x3m::renderer;
using Words = std::vector<std::uint32_t>;
unsigned checks = 0;
void require(bool value, const char* reason) {
    ++checks;
    if (!value) throw std::runtime_error(reason);
}
Words read(const std::string& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    require(bool(stream), "open local original");
    const auto bytes = stream.tellg();
    require(bytes > 0 && bytes % 4 == 0, "original word alignment");
    Words result(static_cast<std::size_t>(bytes) / 4);
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(result.data()), bytes);
    require(bool(stream), "read original");
    return result;
}
void write(const std::string& path, const Words& words) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(words.data()), static_cast<std::streamsize>(words.size() * 4));
    require(bool(stream), "write local variant");
}
int main(int argc, char** argv) {
    try {
        require(argc == 3, "usage: linear_emission_hull_gain_structure <original directory> <local output directory>");
        const char* names[] = {"5f82ecacd39529cd", "6733b119142c8d42", "fffdabd910793aba", "496049cec2066ed3",
                               "e6794b6ec37ff71a", "f1b0e820c7b488c3", "0c1f3f0f440e4a0c", "7c83ed50c9894e44",
                               "99153c144030c396", "64bac8bb307eb896", "c1452981fd0bff64", "e70adc744a38ca59"};
        const float gains[] = {1, 2, 3.5f, 8};
        unsigned variants = 0, identical = 0, modulated = 0, reviewed = 0, clamped = 0, normalized = 0;
        long long create_ns = 0;
        for (const char* name : names) {
            const auto original = read(std::string(argv[1]) + "/ps_" + name + ".bin");
            const auto saved = original;
            const auto hash = fingerprint(original.data(), original.size());
            require(linear_emission_hull_program_reviewed(hash), "local original is a reviewed hull program");
            ++reviewed;
            const auto& profile = hull_profiles[linear_emission_hull_program_index(hash)];
            require(profile.words == original.size(), "profile word count");
            if (profile.modulated) ++modulated;
            for (unsigned g = 0; g < 4; ++g) {
                Words result{91, 92};
                const auto begin = std::chrono::steady_clock::now();
                const auto status = linear_emission_hull_source_gain_variant(original.data(), original.size(), gains[g],
                                                                             result);
                create_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() -
                                                                                  begin)
                                 .count();
                if (status != LinearEmissionResult::Applied)
                    std::cerr << name << " status=" << static_cast<int>(status) << '\n';
                require(status == LinearEmissionResult::Applied, "reviewed original admission");
                require(original == saved, "immutable input");
                if (g == 0) {
                    require(result == original, "gain 1 byte identity");
                    ++identical;
                } else {
                    require(result.size() == original.size() + 10, "one DEF and one MUL added");
                    // The DEF sits at the first declaration; the final colour
                    // instruction is redirected to r0.xyz (opcode, _pp, mask and
                    // operands kept) and the MUL into oC0.xyz follows it with the
                    // original destination token; everything else is verbatim.
                    const std::size_t site = profile.modulated ? 5u : 4u,
                                      colour = profile.definition + 6 + (profile.emission - profile.definition);
                    require(std::equal(original.begin(), original.begin() + profile.definition, result.begin()),
                            "header verbatim");
                    require(result[profile.definition] == ((5u << 24) | def) &&
                                result[profile.definition + 1] == (dst(constant, hull_gain_constant, 15)) &&
                                result[profile.definition + 2] == bits(gains[g]) &&
                                result[profile.definition + 3] == 0 && result[profile.definition + 4] == 0 &&
                                result[profile.definition + 5] == 0,
                            "gain DEF");
                    require(std::equal(original.begin() + profile.definition, original.begin() + profile.emission,
                                       result.begin() + profile.definition + 6),
                            "body verbatim");
                    require(result[colour] == original[profile.emission] &&
                                result[colour] ==
                                    ((profile.modulated ? 4u : 3u) << 24 | (profile.modulated ? mad : add)),
                            "colour opcode kept");
                    require(original[profile.emission + 1] == (dst(output, 0, 7) | pp) &&
                                result[colour + 1] == (dst(temporary, hull_emission_temporary, 7) | pp),
                            "colour instruction redirected to r0.xyz with _pp");
                    require(std::equal(original.begin() + profile.emission + 2,
                                       original.begin() + profile.emission + site, result.begin() + colour + 2),
                            "colour operands verbatim");
                    require(result[colour + site] == ((3u << 24) | mul) &&
                                result[colour + site + 1] == (dst(output, 0, 7) | pp) &&
                                result[colour + site + 2] == src(temporary, hull_emission_temporary) &&
                                result[colour + site + 3] == lane(constant, hull_gain_constant, 0),
                            "whole-output MUL into oC0.xyz");
                    require(std::equal(original.begin() + profile.emission + site, original.end(),
                                       result.begin() + colour + site + 4),
                            "alpha MUL and end verbatim");
                }
                auto alias = original;
                require(linear_emission_hull_source_gain_variant(alias.data(), alias.size(), gains[g], alias) ==
                                LinearEmissionResult::Applied &&
                            alias == result,
                        "successful alias");
                write(std::string(argv[2]) + "/ps_" + name + "-hull-" + std::to_string(g) + ".bin", result);
                ++variants;
            }
            // Clamp C: DEF c223 = (G, C, 0, 0); the colour instruction writes
            // r0.xyz, the gain MUL (omitted at gain 1) writes r0.xyz with _pp
            // and `min oC0.xyz, r0, c223.y` carries the original destination.
            for (const auto& gc : {std::pair<float, float>{2, 1}, std::pair<float, float>{1, 0.5f},
                                   std::pair<float, float>{8, 8}, std::pair<float, float>{3.5f, 0.25f}}) {
                Words result{91, 92};
                require(linear_emission_hull_source_gain_variant(original.data(), original.size(), gc.first, result,
                                                                 gc.second) == LinearEmissionResult::Applied,
                        "clamp variant admission");
                require(original == saved, "clamp immutable input");
                const bool gained = gc.first != 1;
                const std::size_t site = profile.modulated ? 5u : 4u,
                                  colour = profile.definition + 6 + (profile.emission - profile.definition),
                                  cap = colour + site + (gained ? 4u : 0u);
                require(result.size() == original.size() + (gained ? 14u : 10u), "DEF, MUL and MIN added");
                require(std::equal(original.begin(), original.begin() + profile.definition, result.begin()) &&
                            result[profile.definition + 1] == dst(constant, hull_gain_constant, 15) &&
                            result[profile.definition + 2] == bits(gc.first) &&
                            result[profile.definition + 3] == bits(gc.second),
                        "clamp DEF");
                require(std::equal(original.begin() + profile.definition, original.begin() + profile.emission,
                                   result.begin() + profile.definition + 6) &&
                            result[colour] == original[profile.emission] &&
                            result[colour + 1] == (dst(temporary, hull_emission_temporary, 7) | pp) &&
                            std::equal(original.begin() + profile.emission + 2,
                                       original.begin() + profile.emission + site, result.begin() + colour + 2),
                        "clamp colour redirect");
                if (gained)
                    require(result[colour + site] == ((3u << 24) | mul) &&
                                result[colour + site + 1] == (dst(temporary, hull_emission_temporary, 7) | pp) &&
                                result[colour + site + 2] == src(temporary, hull_emission_temporary) &&
                                result[colour + site + 3] == lane(constant, hull_gain_constant, 0),
                            "clamp gain MUL into r0.xyz");
                require(result[cap] == ((3u << 24) | minimum) && result[cap + 1] == (dst(output, 0, 7) | pp) &&
                            result[cap + 2] == src(temporary, hull_emission_temporary) &&
                            result[cap + 3] == lane(constant, hull_gain_constant, 1),
                        "MIN into oC0.xyz");
                require(std::equal(original.begin() + profile.emission + site, original.end(), result.begin() + cap + 4),
                        "clamp alpha MUL and end verbatim");
                if (gc.first == 2 && gc.second == 1)
                    write(std::string(argv[2]) + "/ps_" + name + "-hull-clamp.bin", result);
                ++clamped;
            }
            // Blend-factor form (clamp < 1): DEF .z = 1/C, the MIN writes
            // r0.xyz_pp and `mul oC0.xyz_pp, r0, c223.z` (t / C) ends the colour.
            for (const auto& gc : {std::pair<float, float>{2, 0.7f}, std::pair<float, float>{1, 0.5f}}) {
                const float clamp = linear_emission_blend_factor_clamp(gc.second);
                Words result{91, 92};
                require(linear_emission_hull_source_gain_variant(original.data(), original.size(), gc.first, result,
                                                                 clamp, true) == LinearEmissionResult::Applied,
                        "normalized variant admission");
                const bool gained = gc.first != 1;
                const std::size_t site = profile.modulated ? 5u : 4u,
                                  colour = profile.definition + 6 + (profile.emission - profile.definition),
                                  cap = colour + site + (gained ? 4u : 0u);
                require(result.size() == original.size() + (gained ? 18u : 14u) &&
                            result[profile.definition + 4] == bits(1.0f / clamp) &&
                            result[cap] == ((3u << 24) | minimum) &&
                            result[cap + 1] == (dst(temporary, hull_emission_temporary, 7) | pp) &&
                            result[cap + 4] == ((3u << 24) | mul) && result[cap + 5] == (dst(output, 0, 7) | pp) &&
                            result[cap + 6] == src(temporary, hull_emission_temporary) &&
                            result[cap + 7] == lane(constant, hull_gain_constant, 2) &&
                            std::equal(original.begin() + profile.emission + site, original.end(),
                                       result.begin() + cap + 8),
                        "t / C MUL into oC0.xyz");
                if (gc.first == 2) write(std::string(argv[2]) + "/ps_" + name + "-hull-normalized.bin", result);
                ++normalized;
                Words refused{91, 92};
                require(linear_emission_hull_source_gain_variant(original.data(), original.size(), 2, refused, 1.0f,
                                                                 true) == LinearEmissionResult::InvalidConfig &&
                            refused == Words({91, 92}),
                        "normalized needs 0 < C < 1");
            }
            for (float invalid : {-0.5f, 0.1f, 0.249f, 8.001f, std::numeric_limits<float>::infinity(),
                                  std::numeric_limits<float>::quiet_NaN()}) {
                Words result{91, 92};
                const auto before = result;
                auto alias = original;
                require(linear_emission_hull_source_gain_variant(original.data(), original.size(), 2, result,
                                                                 invalid) == LinearEmissionResult::InvalidConfig &&
                            result == before &&
                            linear_emission_hull_source_gain_variant(alias.data(), alias.size(), 2, alias, invalid) ==
                                LinearEmissionResult::InvalidConfig &&
                            alias == saved,
                        "invalid clamp rollback");
            }
            for (float invalid : {0.f, 0.999f, 8.001f, -1.0f, std::numeric_limits<float>::infinity(),
                                  -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
                Words result{91, 92};
                const auto before = result;
                require(linear_emission_hull_source_gain_variant(original.data(), original.size(), invalid, result) ==
                                LinearEmissionResult::InvalidConfig &&
                            result == before,
                        "invalid gain rollback");
                auto alias = original;
                require(linear_emission_hull_source_gain_variant(alias.data(), alias.size(), invalid, alias) ==
                                LinearEmissionResult::InvalidConfig &&
                            alias == saved,
                        "invalid gain alias rollback");
            }
            for (std::size_t offset : {std::size_t(0), std::size_t(2), std::size_t(profile.definition),
                                       std::size_t(profile.emission) + 1, original.size() - 2}) {
                auto broken = original;
                broken[offset] ^= 1;
                auto before = broken;
                require(linear_emission_hull_source_gain_variant(broken.data(), broken.size(), 2, broken) ==
                                LinearEmissionResult::UnsupportedShader &&
                            broken == before,
                        "corrupted original alias rollback");
            }
            Words result{91, 92};
            const auto before = result;
            require(linear_emission_hull_source_gain_variant(nullptr, original.size(), 2, result) ==
                            LinearEmissionResult::InvalidInput &&
                        result == before,
                    "null rollback");
            require(linear_emission_hull_source_gain_variant(original.data(), 1, 2, result) ==
                            LinearEmissionResult::InvalidInput &&
                        result == before,
                    "short rollback");
            require(linear_emission_hull_source_gain_variant(original.data(), original.size() - 1, 2, result) ==
                            LinearEmissionResult::UnsupportedShader &&
                        result == before,
                    "truncation rollback");
            // The effects gain and the hull gain never admit each other's programs.
            require(linear_emission_source_gain_variant(original.data(), original.size(), 2, result) ==
                            LinearEmissionResult::UnsupportedShader &&
                        result == before,
                    "effects gain refuses a hull program");
            require(!linear_emission_pair_reviewed(hash, hash), "hull program is not an effects pair");
        }
        for (const auto& row : profiles)
            require(!linear_emission_hull_program_reviewed(row.pixel), "hull gain refuses the effects programs");
        Words authored{0xffff0300u, 0xffffu}, result{91, 92};
        require(linear_emission_hull_source_gain_variant(authored.data(), authored.size(), 2, result) ==
                        LinearEmissionResult::UnsupportedShader &&
                    result == Words({91, 92}),
                "unreviewed valid framing");
        // Blend law: only ADD ONE/ONE with blending on and sRGB write off.
        unsigned admitted = 0, refused = 0;
        for (std::uint32_t enable : {0u, 1u})
            for (std::uint32_t srgb : {0u, 1u})
                for (std::uint32_t source : {1u, 2u, 4u, 5u})
                    for (std::uint32_t destination : {1u, 2u, 4u, 5u})
                        for (std::uint32_t op : {1u, 2u}) {
                            const auto verdict = linear_emission_hull_source_gain_blend(enable, srgb, source,
                                                                                        destination, op);
                            require(verdict != SourceGainBlend::Screen,
                                    "no screen substitution in the hull population");
                            if (enable && !srgb && source == 2 && destination == 2 && op == 1) {
                                require(verdict == SourceGainBlend::Admit, "ONE/ONE ADD admits");
                                ++admitted;
                            } else {
                                require(verdict == SourceGainBlend::Blend, "every other colour blend refuses");
                                ++refused;
                            }
                        }
        std::cout << "{\"programs\":" << reviewed << ",\"modulated\":" << modulated << ",\"variants\":" << variants
                  << ",\"identical\":" << identical << ",\"checks\":" << checks << ",\"creates_ns\":" << create_ns
                  << ",\"blend_admitted\":" << admitted << ",\"blend_refused\":" << refused
                  << ",\"clamped\":" << clamped << ",\"normalized\":" << normalized << "}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
