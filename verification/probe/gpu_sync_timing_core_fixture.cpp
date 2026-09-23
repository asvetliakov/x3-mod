// Host execution of the D3D-free --gpu-sync-timing core (src/renderer/gpu_sync_timing_core.h;
// docs/architecture/engine-frame-time.md, "GPU sync timing"): tick conversion, the session
// histogram's buckets, nearest-rank percentiles, the per-frame pair rules (first-per-frame
// Scene/Engine, accumulated repeats, stray ends, unclosed passes, an abandoned frame), the
// window rotation with its dt and the bounded arrays. Run by verification/analysis/test_gpu_sync_timing.py.
#include "../../src/renderer/gpu_sync_timing_core.h"
#include <cstdio>
#include <memory>
#include <string_view>

namespace g = x3m::gpu_sync_timing;
static unsigned checks = 0, failures = 0;
static void check(bool value, const char* label) { ++checks; if (!value) { ++failures; std::printf("FAIL %s\n", label); } }

int main() {
    // Ticks at 10 MHz (the Wine counter) and at 1 GHz, saturation.
    check(g::ticks_to_us(10, 10000000) == 1 && g::ticks_to_us(15000, 10000000) == 1500 && g::ticks_to_us(0, 10000000) == 0, "ticks at 10 MHz");
    check(g::ticks_to_us(2500000000ull, 1000000000ull) == 2500000 && g::ticks_to_us(1, 0) == 0, "ticks at 1 GHz, zero frequency");
    check(g::ticks_to_us(~0ull, 10000000) == 0xffffffffu && g::ticks_to_us(4295000000ull * 10, 10000000) == 0xffffffffu, "saturated");
    // Buckets: exact below 64, contiguous and monotonic, <= 6.25 % wide above.
    bool buckets = g::histogram_buckets == 480 && g::bucket_of(0) == 0 && g::bucket_of(63) == 63 && g::bucket_of(64) == 64 && g::bucket_of(0xffffffffu) == 479;
    for (unsigned b = 0; b + 1 < g::histogram_buckets && buckets; ++b) {
        const std::uint32_t low = g::bucket_low(b), next = g::bucket_low(b + 1);
        buckets = next > low && g::bucket_of(low) == b && g::bucket_of(next - 1) == b && g::bucket_value(b) >= low && g::bucket_value(b) < next
                  && (b < g::histogram_linear || std::uint64_t(next - low) * 16 <= low);
    }
    check(buckets, "histogram buckets contiguous, exact below 64, <= 1/16 wide");
    check(g::rank_of(1, 50) == 1 && g::rank_of(10, 50) == 5 && g::rank_of(10, 90) == 9 && g::rank_of(11, 90) == 10 && g::rank_of(300, 90) == 270 && g::rank_of(3, 100) == 3, "nearest rank");
    std::uint32_t values[5] = {50, 10, 40, 20, 30};
    const g::Stat exact = g::Tracker::exact_of(values, 5);
    check(exact.n == 5 && exact.median == 30 && exact.p90 == 50, "exact nearest-rank median and p90");
    check(g::pass_count == 14 && g::boundary_count == 28 && std::string_view(g::pass_name(g::FogRoute)) == "fog_route" && std::string_view(g::pass_name(99)) == "?", "passes and names");

    auto t = std::make_unique<g::Tracker>();
    t->configure(4);
    check(t->window() == 4, "window configured");
    const std::uint64_t f = 1000000; // 1 MHz: one tick = 1 us
    // Frame 1: pairs, a repeat of Scene/Engine ignored, two Bloom pairs added, a stray end, an unclosed Taa.
    std::uint64_t now = 0;
    t->begin(g::Scene, now); t->begin(g::Engine, now);
    check(!t->wants_begin(g::Scene) && !t->wants_begin(g::Engine) && t->wants_end(g::Engine), "open once");
    t->end(g::Engine, now + 100, 5);
    check(!t->wants_begin(g::Engine), "engine only once per frame");
    t->begin(g::Engine, now + 110); // ignored
    t->begin(g::Bloom, now + 200); t->end(g::Bloom, now + 230, 10);
    t->begin(g::Bloom, now + 300); t->end(g::Bloom, now + 320, 20);
    check(!t->wants_end(g::Motes), "stray end refused");
    t->end(g::Motes, now + 330, 0);
    t->begin(g::Taa, now + 340);
    t->end(g::Scene, now + 400, 0);
    check(!t->frame(1, 10000, f), "frame 1 files, window open");
    // Frame 2: the same with different spans; frame 3 abandoned; frame 4 closes the window.
    t->begin(g::Scene, 20000); t->begin(g::Engine, 20000); t->end(g::Engine, 20300, 7); t->end(g::Scene, 20500, 0);
    t->begin(g::Bloom, 20600); t->end(g::Bloom, 20640, 1);
    check(!t->frame(2, 26000, f), "frame 2 files");
    t->begin(g::Scene, 30000); t->abandon_frame();
    check(t->abandoned() && !t->wants_begin(g::Taa) && !t->wants_end(g::Scene), "abandoned frame refuses further syncs");
    t->end(g::Scene, 30100, 0);
    check(!t->frame(3, 36000, f), "frame 3 dropped");
    t->begin(g::Scene, 40000); t->end(g::Scene, 40200, 0);
    check(t->frame(4, 46000, f), "frame 4 closes the window");
    const g::Report r = t->report();
    check(r.window == 1 && r.first_frame == 1 && r.last_frame == 4 && r.frames == 4 && r.dropped == 1 && r.unclosed == 1, "window header: dropped and unclosed counted");
    check(r.pass[g::Scene].window.n == 3 && r.pass[g::Scene].window.median == 400 && r.pass[g::Scene].window.p90 == 500, "scene spans 400/500/200");
    check(r.pass[g::Engine].window.n == 2 && r.pass[g::Engine].window.median == 100 && r.pass[g::Engine].window.p90 == 300, "engine spans 100/300");
    check(r.pass[g::Bloom].window.n == 2 && r.pass[g::Bloom].window.median == 40 && r.pass[g::Bloom].window.p90 == 50 && r.pass[g::Bloom].wait_median == 1, "bloom pairs add up: 30+20 and 40; waits 30 and 1");
    check(r.pass[g::Taa].window.n == 0 && r.pass[g::Motes].window.n == 0, "unclosed and stray passes not filed");
    check(r.dt_window.n == 2 && r.dt_window.median == 10000 && r.dt_window.p90 == 16000, "dt 16000/10000: the first frame has none, the abandoned frame files none");
    check(r.pass[g::Scene].session.n == 3 && r.pass[g::Scene].session.median == g::bucket_value(g::bucket_of(400)) && r.dt_session.n == 2, "session so far");
    // Next window: counts restart; the session keeps accumulating; Reset clears the dt predecessor.
    t->clear_frame();
    for (unsigned k = 0; k < 4; ++k) {
        const std::uint64_t base = 100000 + k * 20000;
        t->begin(g::Scene, base); t->end(g::Scene, base + 40 + k, 0);
        const bool closed = t->frame(10 + k, base + 16000, f);
        check(closed == (k == 3), "second window closes on its fourth frame");
    }
    const g::Report r2 = t->report();
    check(r2.window == 2 && r2.first_frame == 10 && r2.frames == 4 && r2.dropped == 0 && r2.pass[g::Scene].window.n == 4 && r2.pass[g::Scene].window.median == 41 && r2.pass[g::Scene].window.p90 == 43,
          "second window exact");
    check(r2.dt_window.n == 3 && r2.dt_window.median == 20000, "dt after clear_frame: no predecessor");
    check(r2.pass[g::Scene].session.n == 7 && r2.pass[g::Scene].session.median == 43 && r2.dt_session.n == 5, "session across windows (exact below 64 us)");
    const g::Report s = t->summary();
    check(s.window == 2 && s.pass[g::Scene].session.n == 7 && s.pass[g::Scene].window.n == 0, "summary: session only");
    // Bounded arrays: frames past the window without a report never overflow.
    t->configure(1);
    for (unsigned k = 0; k < 700; ++k) { t->begin(g::Taa, k * 10); t->end(g::Taa, k * 10 + 3, 0); t->frame(1000 + k, 1000000 + k * 100, f); }
    const g::Report r3 = t->report();
    check(r3.frames == 700 && r3.pass[g::Taa].window.n == g::window_frames_max && r3.dt_window.n <= g::window_frames_max, "window arrays capped");
    std::printf("gpu_sync_timing_core checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
