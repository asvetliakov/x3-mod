// Host rebuild of the screen-emission additive variants (motion_output.cpp:4600-4655):
// AdditiveGain at gain 2, without and with the bolt far flag (oC2 = 32 max(rgb)).
// usage: gen_screen_flag_variant <ps_original.bin> <out prefix>   (output is game-derived: keep untracked)
#include "../../../src/renderer/linear_emission_sm1.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace x3m::renderer;
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::ifstream s(argv[1], std::ios::binary);
    std::vector<char> b((std::istreambuf_iterator<char>(s)), {});
    std::vector<std::uint32_t> o(b.size() / 4);
    std::memcpy(o.data(), b.data(), o.size() * 4);
    for (int flag = 0; flag < 2; ++flag) {
        LinearEmissionSm1Config c{};
        c.gain = 2.0f;
        c.outputs = LinearEmissionSm1Outputs::AdditiveGain;
        c.far_flag = flag != 0;
        std::vector<std::uint32_t> w;
        const auto r = linear_emission_sm1_pixel_variant(o.data(), o.size(), c, w);
        std::printf("%s result=%u words=%zu\n", flag ? "flag" : "additive", unsigned(r), w.size());
        std::ofstream out(std::string(argv[2]) + (flag ? "-flag.bin" : "-additive.bin"), std::ios::binary);
        out.write(reinterpret_cast<const char*>(w.data()), std::streamsize(w.size() * 4));
    }
}
