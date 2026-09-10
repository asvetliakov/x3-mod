// Local shader bytes are inputs only; this fixture emits derived facts, no code.
#include "../../src/renderer/rigid_position.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
using x3m::renderer::find_rigid_position;
static int verify(char** argv) {
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file) return 3;
    const auto bytes = file.tellg();
    if (bytes <= 0 || bytes % 4 || bytes > 1024*1024) return 4;
    std::vector<std::uint32_t> code(static_cast<std::size_t>(bytes)/4);
    file.seekg(0); file.read(reinterpret_cast<char*>(code.data()), bytes);
    if (!file) return 5;
    const int expected_register = std::atoi(argv[2]);
    const bool named_wvp = std::atoi(argv[3]) != 0;
    const auto* profile = find_rigid_position(code.data(), code.size());
    if (expected_register < 0) {
        if (profile) return 6;
    } else if (!profile || profile->word_count != code.size() ||
               profile->matrix_register != expected_register ||
               profile->named_world_view_projection != named_wvp) return 7;
    if (find_rigid_position(nullptr, code.size()) ||
        find_rigid_position(code.data(), 0) ||
        find_rigid_position(code.data(), static_cast<std::size_t>(-1)) ||
        find_rigid_position(code.data(), code.size()-1)) return 8;
    // Single-bit changes at the header, middle and END must never retain a profile.
    for (std::size_t offset : {std::size_t(0), code.size()/2, code.size()-1}) {
        code[offset] ^= 1;
        if (find_rigid_position(code.data(), code.size())) return 9;
        code[offset] ^= 1;
    }
    std::printf("PASS qualified=%u words=%u matrix=%d checks=8\n",
        profile != nullptr, unsigned(code.size()), expected_register);
    return 0;
}
int main(int argc, char** argv) {
    if (argc < 4 || (argc-1)%3) return 2;
    for (int i = 1; i < argc; i += 3) {
        char* input[] = {argv[0], argv[i], argv[i+1], argv[i+2]};
        if (const int result = verify(input)) return result;
    }
}
