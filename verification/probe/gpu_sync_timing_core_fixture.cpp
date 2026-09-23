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
    check(g::pass_count == 25 && g::boundary_count == 50 && std::string_view(g::pass_name(g::FogRoute)) == "fog_route" && std::string_view(g::pass_name(g::Present)) == "present" && std::string_view(g::pass_name(g::TaaCopy)) == "taa_copy" && std::string_view(g::pass_name(g::TaaDisplay)) == "taa_display"
          && g::TaaDisplay == 18 && g::FogMarch == 19 && std::string_view(g::pass_name(g::FogMarch)) == "fog_march" && std::string_view(g::pass_name(g::FogComposite)) == "fog_composite" && std::string_view(g::pass_name(g::FogRepair)) == "fog_repair"
          && g::FogRepair == 21 && g::TaaMaskTests == 22 && g::TaaMaskY == 24 && std::string_view(g::pass_name(g::TaaMaskTests)) == "taa_mask_tests" && std::string_view(g::pass_name(g::TaaMaskX)) == "taa_mask_x" && std::string_view(g::pass_name(g::TaaMaskY)) == "taa_mask_y" && std::string_view(g::pass_name(99)) == "?", "passes and names");
    check(g::census_ppm(0, 100) == 0 && g::census_ppm(1, 3) == 333333 && g::census_ppm(255, 527) == 483870 && g::census_ppm(5, 0) == 0 && g::census_ppm(7, 5) == 1000000
          && g::census_ppm(2073600, 2073600) == 1000000, "census ppm: integer, zero area, saturated");

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
    // Census: filed with kept frames only (an abandoned frame drops it), unread counted, per-window reset.
    t->clear_frame();
    const std::uint32_t census_pixels[6] = {100, 300, 200, 0, 0, 0};
    const g::CensusRead census_reads[6] = {g::CensusOk, g::CensusOk, g::CensusOk, g::CensusNotReady, g::CensusLost, g::CensusFailed};
    for (unsigned k = 0; k < 6; ++k) {
        const std::uint64_t base = 200000 + k * 20000;
        t->begin(g::Scene, base); t->end(g::Scene, base + 10, 0);
        if (k == 1) t->abandon_frame();
        t->census(census_pixels[k], 1000, census_reads[k]);
        t->frame(20 + k, base + 16000, f);
    }
    const g::Report rc = t->report();
    check(rc.census.ppm.n == 2 && rc.census.ppm.median == 100000 && rc.census.ppm.p90 == 200000 && rc.census.max_ppm == 200000 && rc.census.pixels == 200 && rc.census.area == 1000
          && rc.census.unread == 1 && rc.census.lost == 1 && rc.census.failed == 1, "census: kept frames only; not ready, lost and failed counted apart");
    t->begin(g::Scene, 300000); t->end(g::Scene, 300010, 0); t->frame(30, 316000, f);
    const g::Report rn = t->report();
    check(rn.census.ppm.n == 0 && rn.census.max_ppm == 0 && rn.census.unread == 0 && rn.census.lost == 0 && rn.census.failed == 0 && rn.census.area == 0, "census resets per window; a frame without a census files none");
    // The needs-repair counter (fog-gpu-cost.md step C): its own window beside the repair census, pixel counts kept exact,
    // the spacing tag of the last counted frame; an abandoned frame drops both.
    const std::uint32_t needs_pixels[4] = {40, 70, 10, 999};
    for (unsigned k = 0; k < 4; ++k) {
        const std::uint64_t base = 400000 + k * 20000;
        t->begin(g::Scene, base); t->end(g::Scene, base + 10, 0);
        if (k == 3) t->abandon_frame();
        t->census(5, 1000, g::CensusOk);
        t->needs(needs_pixels[k], 1000, k == 2 ? 4u : 2u, k == 1 ? g::CensusNotReady : g::CensusOk);
        t->frame(40 + k, base + 16000, f);
    }
    const g::Report rq = t->report();
    check(rq.needs.px.n == 2 && rq.needs.px.median == 10 && rq.needs.px.p90 == 40 && rq.needs.max_px == 40 && rq.needs.ppm.median == 10000 && rq.needs.tag == 4
          && rq.needs.pixels == 10 && rq.needs.unread == 1 && rq.census.px.n == 3 && rq.census.px.median == 5 && rq.census.tag == 0, "needs census: own window, exact pixels, spacing tag, abandoned frame dropped");
    // Bounded arrays: frames past the window without a report never overflow.
    t->configure(1);
    for (unsigned k = 0; k < 700; ++k) { t->begin(g::Taa, k * 10); t->end(g::Taa, k * 10 + 3, 0); t->frame(1000 + k, 1000000 + k * 100, f); }
    const g::Report r3 = t->report();
    check(r3.frames == 700 && r3.pass[g::Taa].window.n == g::window_frames_max && r3.dt_window.n <= g::window_frames_max, "window arrays capped");
    std::printf("gpu_sync_timing_core checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
