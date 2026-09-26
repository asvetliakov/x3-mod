// Portable verification of the production qualification helper against local,
// untracked archive bytes. No game program bytes enter this source or results.
#include "../../src/renderer/rigid_replay_program.h"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace x3m::renderer;
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream manifest(argv[1]);
    unsigned approved = 0, rejected = 0, mutations = 0;
    int expected;
    std::string path;
    while (manifest >> expected >> path) {
        std::ifstream f(path, std::ios::binary);
        std::vector<char> bytes{std::istreambuf_iterator<char>(f), {}};
        if (bytes.empty() || bytes.size() % 4) return 3;
        std::vector<std::uint32_t> words(bytes.size() / 4);
        for (size_t i = 0; i < words.size(); ++i) {
            words[i] = 0;
            for (unsigned b = 0; b < 4; ++b)
                words[i] |= std::uint32_t(static_cast<unsigned char>(bytes[4 * i + b])) << (b * 8);
        }
        const auto* p = find_rigid_replay_profile(words.data(), words.size());
        if (bool(p) != bool(expected)) return 4;
        if (p) {
            ++approved;
            for (size_t i = 0; i < words.size(); ++i) {
                words[i] ^= 1;
                if (find_rigid_replay_profile(words.data(), words.size())) return 5;
                words[i] ^= 1;
                ++mutations;
            }
        } else
            ++rejected;
    }
    if (approved != 32 || rejected != 202) return 6;
    std::printf("PROFILES PASS qualified_sm3=%u rejected_legacy=%u rejected_word_mutations=%u\n", approved, rejected,
                mutations);
}
