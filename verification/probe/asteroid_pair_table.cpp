// Prints the Asteroid-class verdict of the reviewed material pairs given on
// stdin ("<vs hex> <ps hex>" per line), for
// verification/analysis/test_shimmer_trace.py: one line
// "<vs> <ps> asteroid=<0|1> fade=<sampler mask>" per input pair. Host tool;
// no device and no Windows headers.
#include "../../src/renderer/linear_distance_fade.h"
#include "../../src/renderer/linear_material.h"

#include <cstdio>
#include <cstdint>

int main() {
    char vs[32], ps[32];
    while (std::scanf("%31s %31s", vs, ps) == 2) {
        const std::uint64_t v = std::strtoull(vs, nullptr, 16), p = std::strtoull(ps, nullptr, 16);
        std::printf("%s %s asteroid=%d fade=%u\n", vs, ps,
                    int(x3m::renderer::linear_material_asteroid_pair(v, p)),
                    unsigned(x3m::renderer::linear_distance_fade_sampler_mask(v, p)));
    }
    return 0;
}
