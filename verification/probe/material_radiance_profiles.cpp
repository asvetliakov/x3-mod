// Host-only execution of the production transformer on trusted local captures.
// Transformed copyrighted bytes are written only into the ignored probe build.
#include "../../src/renderer/material_radiance.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) return 2;
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
    if (bytes.empty() || bytes.size() % 4) return 2;
    std::vector<std::uint32_t> words(bytes.size() / 4), variant;
    for (std::size_t i = 0; i < words.size(); ++i)
        for (unsigned b = 0; b < 4; ++b) words[i] |= std::uint32_t(bytes[i * 4 + b]) << (b * 8);
    const auto result = x3m::renderer::material_radiance_variant(words.data(), words.size(), variant);
    if (result != x3m::renderer::RadianceResult::Applied) {
        std::cerr << "Rejected result=" << unsigned(result) << '\n';
        return 1;
    }
    std::ofstream output(argv[2], std::ios::binary);
    for (const auto word : variant)
        for (unsigned b = 0; b < 4; ++b) output.put(char((word >> (b * 8)) & 0xff));
    return output ? 0 : 2;
}
