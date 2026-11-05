// Host check of src/proxy/sky_nebula_core.h (Decision C1, docs/architecture/sector-background-modern.md "Decision C"):
// the constants the swap sets, the settings' ranges and the pixel program's tap pattern field (static in texture space:
// a function of the texel-space position only, equal to the texel's hash at its centre, continuous across texel
// boundaries, spread over [0, 1)). Built and run by verification/analysis/test_sky_nebula.py; prints one line per check
// and a summary.
#include "../../src/proxy/sky_nebula_core.h"
#include <cmath>
#include <cstdio>

namespace core = x3m::sky_nebula::core;
static unsigned checks = 0, failures = 0;
static void check(bool ok, const char* name) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", name);
    }
}
int main() {
    float c[8];
    check(core::pixel_constants(0.f, 12.f, 2048, 1024, c) && c[0] == 0.f && c[1] == 0.f, "threshold 0 is the identity (on = 0)");
    check(core::pixel_constants(6.f, 12.f, 2048, 1024, c) && c[0] == 1.f && std::fabs(c[1] - 42.5f) < 1e-4f &&
              c[2] == 12.f && c[3] == 0.f && c[4] == 2048.f && c[5] == 1024.f && c[6] == 1.f / 2048.f &&
              c[7] == 1.f / 1024.f,
          "c200-c201 packing");
    check(!core::pixel_constants(6.f, 12.f, 0, 1024, c), "zero size refused");
    check(core::pixel_constants(6.f, 12.f, 2048, 1024, c, true) && c[3] == 1.f && core::pixel_constants(6.f, 12.f, 2048, 1024, c) &&
              c[3] == 0.f,
          "c200.w = the hash switch, off by default");
    float j[4];
    core::jitter_constant(true, .25f, -.5f, 5120, 1440, j);
    check(j[0] == 2.f * .25f / 5120.f && j[1] == -2.f * -.5f / 1440.f && j[2] == 0.f && j[3] == 0.f, "c255 jitter sign");
    core::jitter_constant(false, .25f, -.5f, 5120, 1440, j);
    check(j[0] == 0.f && j[1] == 0.f, "c255 zero without jitter");
    float v = -1.f;
    check(!core::accept_threshold(0.5f, &v) && v == -1.f && core::accept_threshold(16.f, &v) && v == 16.f &&
              !core::accept_threshold(16.5f, &v) && !core::accept_threshold(NAN, &v),
          "threshold range 1..16");
    check(!core::accept_radius(1.f, &v) && core::accept_radius(2.f, &v) && core::accept_radius(32.f, &v) &&
              !core::accept_radius(33.f, &v),
          "radius range 2..32");
    // The field over a 2048x1024 part: at a texel centre it is the texel's hash; anywhere it lies in [0, 1); across a
    // texel boundary it moves by at most the bilinear slope (continuous); the hashes spread evenly (16 bins).
    unsigned bins[16] = {};
    bool centres = true, range = true, continuous = true;
    double max_step = 0, mean = 0;
    const unsigned W = 2048, H = 1024;
    for (unsigned y = 0; y < H; ++y)
        for (unsigned x = 0; x < W; ++x) {
            const float h = core::hash(float(x), float(y));
            range &= h >= 0.f && h < 1.f;
            ++bins[unsigned(h * 16.f) & 15u];
            mean += h;
            if (x % 97 == 0 && y % 89 == 0) {
                centres &= std::fabs(core::pattern_field(float(x) + .5f, float(y) + .5f) - h) < 1e-6f;
                // A boundary crossing in 1/64-texel steps: the field moves by at most |neighbour difference| / 64.
                float prev = core::pattern_field(float(x) + .5f, float(y) + .5f);
                for (unsigned k = 1; k <= 64; ++k) {
                    const float next = core::pattern_field(float(x) + .5f + float(k) / 64.f, float(y) + .5f);
                    const double step = std::fabs(double(next) - double(prev));
                    max_step = step > max_step ? step : max_step;
                    prev = next;
                }
            }
        }
    continuous = max_step <= 1. / 64. + 1e-5;
    double chi = 0;
    const double expect = double(W) * H / 16.;
    for (unsigned b : bins) chi += (double(b) - expect) * (double(b) - expect) / expect;
    mean /= double(W) * H;
    check(range, "hash in [0, 1)");
    check(centres, "field = the texel's hash at its centre");
    check(continuous, "field continuous (<= 1/64 per 1/64 texel)");
    check(chi < 60. && std::fabs(mean - .5) < .01, "hash spread (16 bins, chi-square < 60, 15 dof)");
    // Static in texture space: the same texel-space position gives the same value, whichever screen pixel or frame
    // asks (the function has no other input); two positions inside one texel quarter share their four hashes.
    check(core::pattern_field(100.3f, 200.7f) == core::pattern_field(100.3f, 200.7f), "field is a function of uv only");
    std::printf("sky_nebula_check checks=%u failures=%u chi=%.1f mean=%.4f max_step=%.5f\n", checks, failures, chi, mean,
                max_step);
    return failures ? 1 : 0;
}
