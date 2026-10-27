// Host-only driver of the native validator rule guard
// (verification/analysis/test_native_validator_rules.py): for every ps_*.bin
// of the given folders that is one of the six SM1 profiles of
// linear_emission_sm1.cpp, writes each pixel variant the transformer authors
// (Native, Emission, Coverage at gains 0/1/4 with and without the partial
// precision request; PackedScreen; AdditiveGain at gains 2/8 with and without
// the bolt far flag) into the output folder as
// <stem>-o<outputs>-g<gain>-p<pp>-f<flag>.bin (game-derived bytes: the output
// folder is a caller-supplied temporary). Other programs are skipped by the
// transformer's own identity check. Prints "variants=N flagged=M".
// usage: linear_emission_sm1_port_variants <out dir> <program dir>...
#include "../../src/renderer/linear_emission_sm1.h"
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
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const fs::path out = argv[1];
    unsigned variants = 0, flagged = 0;
    struct Case {
        LinearEmissionSm1Outputs outputs;
        float gain;
        bool pp, flag;
    };
    std::vector<Case> cases;
    for (auto o : {LinearEmissionSm1Outputs::Native, LinearEmissionSm1Outputs::Emission,
                   LinearEmissionSm1Outputs::Coverage})
        for (float g : {0.f, 1.f, 4.f})
            for (bool pp : {false, true}) cases.push_back({o, g, pp, false});
    cases.push_back({LinearEmissionSm1Outputs::PackedScreen, 1.f, false, false});
    for (float g : {2.f, 8.f})
        for (bool flag : {false, true}) cases.push_back({LinearEmissionSm1Outputs::AdditiveGain, g, false, flag});
    unsigned duplicates = 0;
    std::set<std::string> seen;  // a program present in two folders (sweep corpus and native captures) is one program
    for (int a = 2; a < argc; ++a) {
        if (!fs::is_directory(argv[a])) continue;
        for (const auto& entry : fs::directory_iterator(argv[a])) {
            const std::string stem = entry.path().stem().string();
            if (entry.path().extension() != ".bin" || stem.rfind("ps_", 0) != 0) continue;
            if (!seen.insert(stem).second) { ++duplicates; continue; }
            Words original;
            if (!load(entry.path(), original) || original.size() > 59 || original[0] != 0xffff0101u) continue;
            unsigned here = 0;
            for (const auto& c : cases) {
                LinearEmissionSm1Config config{};
                config.gain = c.gain;
                config.outputs = c.outputs;
                config.native_partial_precision = c.pp;
                config.far_flag = c.flag;
                Words w;
                const auto r = linear_emission_sm1_pixel_variant(original.data(), original.size(), config, w);
                if (r == LinearEmissionResult::UnsupportedShader) break;
                if (r != LinearEmissionResult::Applied) {
                    std::printf("%s outputs=%d gain=%g pp=%d flag=%d result=%d\n", stem.c_str(), int(c.outputs),
                                double(c.gain), int(c.pp), int(c.flag), int(r));
                    return 1;
                }
                char name[96];
                std::snprintf(name, sizeof name, "%s-o%d-g%g-p%d-f%d.bin", stem.c_str(), int(c.outputs),
                              double(c.gain), int(c.pp), int(c.flag));
                std::ofstream s(out / name, std::ios::binary);
                s.write(reinterpret_cast<const char*>(w.data()), static_cast<std::streamsize>(w.size() * 4));
                if (!s) return 1;
                ++variants;
                ++here;
                flagged += c.flag;
            }
            if (here) std::printf("%s variants=%u\n", stem.c_str(), here);
        }
    }
    std::printf("duplicates=%u\n", duplicates);
    std::printf("variants=%u flagged=%u\n", variants, flagged);
    return 0;
}
