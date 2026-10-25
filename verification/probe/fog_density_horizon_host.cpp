// Host witness for the horizon level of src/fog/fog_density_cache.{h,cpp} (docs/architecture/fog-visible-distance.md,
// step 2): geometry, priority (far need first, a cold horizon after the fine need, a warm one ahead of it only to keep its
// lead), bounded horizon slabs, the shared upload budget, the content against the static generator, the fail-closed
// paths (CPU allocation refused, drop_horizon) and a SETA travel against the two-level cache.
// A plain heap atlas stands in for the SYSTEMMEM staging texture and the GPU copy (as fog_density_cache_host.cpp).
// Usage: fog_density_horizon_host [logic|seta|alloc|all]; `alloc` needs -DX3M_FOG_DENSITY_TEST_HOOKS.
#include "../../src/fog/fog_density_cache.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>
using namespace x3m::fog;
namespace {
unsigned failures = 0, checks = 0;
bool require(const char* name, bool value) {
    ++checks;
    failures += !value;
    std::printf("CHECK %s %s\n", name, value ? "PASS" : "FAIL");
    return value;
}
constexpr int kOrder[kMaxLevels] = {1, 0, 2}; // upload priority rank of levels 0, 1, 2: far, fine, horizon

struct Gpu {
    std::vector<std::uint8_t> atlas[kMaxLevels];
    std::size_t max_bytes = 0, horizon_bytes = 0;
    unsigned levels = kMaxLevels; // views handed to take_uploads (2: a two-level caller)
    bool over_budget = false, out_of_order = false, horizon_before_base_done = false;
    unsigned rects[kMaxLevels]{};
    Gpu() {
        for (auto& a : atlas) a.assign(kAtlasBytes, 0xcd);
    }
    unsigned frame(DensityCache& cache, std::size_t budget = kDefaultUploadBudget) {
        if (!cache.has_work()) return 0;
        std::vector<StagingView> views(levels);
        for (unsigned l = 0; l < levels; ++l) views[l] = {atlas[l].data(), kAtlasPitch};
        TileRect r[kDefaultUploadRects];
        const unsigned n = cache.take_uploads(views.data(), budget, r, unsigned(sizeof r / sizeof r[0]));
        std::size_t bytes = 0;
        int rank = -1;
        bool horizon = false;
        for (unsigned i = 0; i < n; ++i) {
            bytes += r[i].bytes();
            ++rects[r[i].level];
            const int k = kOrder[r[i].level];
            out_of_order = out_of_order || k < rank;
            rank = std::max(rank, k);
            if (r[i].level == kHorizonLevel) {
                horizon = true;
                horizon_bytes += r[i].bytes();
            }
        }
        // The budget is shared: a horizon rectangle goes up only when nothing of far and fine was left behind.
        if (horizon && (cache.level_dirty(0) || cache.level_dirty(1))) horizon_before_base_done = true;
        over_budget = over_budget || bytes > std::max(budget, kTileBytes);
        max_bytes = std::max(max_bytes, bytes);
        cache.confirm_uploads(true);
        return n;
    }
};
std::vector<std::uint8_t> static_atlas(int level, const NodeKey& origin) {
    std::vector<std::uint8_t> out(kAtlasBytes, 0);
    for (int g = 0; g < kGroupCount; ++g)
        generate_tile(kLevelDelta[level], origin, kNoOffset, g,
                      out.data() + std::size_t(g / kGroupsPerRow) * kTileTexels * kAtlasPitch +
                          std::size_t(g % kGroupsPerRow) * kTileTexels * kTexelBytes,
                      kAtlasPitch);
    return out;
}
bool equal_static(const Gpu& gpu, const DensityCache& cache, int level) {
    const NodeBox box = cache.gpu_box(level);
    const NodeKey origin{box.lo[0], box.lo[1], box.lo[2]};
    const auto expect = static_atlas(level, origin);
    std::size_t differing = 0;
    for (std::size_t i = 0; i < kAtlasBytes; ++i) differing += expect[i] != gpu.atlas[level][i];
    std::printf("ATLAS level=%d differing_bytes=%zu\n", level, differing);
    return differing == 0 && !box.empty() && box == window_box(origin);
}
std::uint64_t level_sum(const DensityCache& c) {
    return c.level_nodes(0) + c.level_nodes(1) + c.level_nodes(2);
}

void planning() {
    // need + lead inside the window for every camera node offset the retarget allows, and the retarget at 8 nodes.
    bool inside = true;
    const double base[3] = {95576., -1797323., 82698.};
    const NodeKey origin = window_origin(kHorizonDelta, base[0], base[1], base[2]);
    const NodeBox window = window_box(origin);
    for (int dx = -(kRetargetNodes[2] - 1); dx < kRetargetNodes[2]; ++dx)
        for (double frac : {.01, .5, .99}) {
            const double c[3] = {(double(origin.x + kWindowHalf + dx) + frac) * kHorizonDelta,
                                 (double(origin.y + kWindowHalf - dx) + frac) * kHorizonDelta,
                                 (double(origin.z + kWindowHalf) + 1 - frac) * kHorizonDelta};
            inside = inside && !retarget_needed(2, origin, c) &&
                     window.contains(grow(need_box(2, c), kHorizonLead + kReadinessGuard));
        }
    const NodeBox need = need_box(2, base);
    std::printf("HORIZON_GEOMETRY need_nodes=%lld lead=%d retarget=%d reach=%.0f\n",
                (long long)(need.hi[0] - need.lo[0] + 1), kHorizonLead, kRetargetNodes[2], kHorizonReach);
    require("horizon_need_plus_lead_inside_window_until_retarget", inside);
    const double moved[3] = {base[0] + kRetargetNodes[2] * kHorizonDelta, base[1], base[2]};
    require("horizon_retarget_at_8_nodes", retarget_needed(2, origin, moved));
    // The far level's reach holds the tail's far share: 195,000 + the 3000-unit tap (no warp in the tail).
    require("far_reach_holds_the_far_share", kLevelReach[1] >= 195000. + 3000.);
    require("horizon_reach_holds_the_3x_cap", kHorizonReach >= 3 * 112500. + 3000. + 1900.);
}

struct Fill {
    long long far_resident = -1, fine_resident = -1, horizon_ready = -1, first_horizon_step = -1;
    bool horizon_after_needs = true, slabs_bounded = true;
    std::uint64_t max_horizon_slab = 0;
};
// One slab per frame (settle-style): step, worker_step, upload. Returns frames to quiescence.
Fill fill(DensityCache& cache, Gpu& gpu, const double camera[3], bool horizon, std::size_t budget) {
    Fill out;
    std::uint64_t frame = 0;
    for (unsigned frames = 0; frames < 20000; ++frames) {
        const FrameState s = cache.step(camera, ++frame);
        if (s.resident[1] && out.far_resident < 0) out.far_resident = (long long)frame;
        if (s.resident[0] && out.fine_resident < 0) out.fine_resident = (long long)frame;
        if (s.ready[2] >= 1.f && out.horizon_ready < 0) out.horizon_ready = (long long)frame;
        const std::uint64_t before = cache.level_nodes(2);
        const bool busy = cache.worker_step();
        const std::uint64_t slab = cache.level_nodes(2) - before;
        if (slab) {
            if (out.first_horizon_step < 0) {
                out.first_horizon_step = (long long)frame;
                // A cold horizon goes after the far and the fine need.
                out.horizon_after_needs = cache.worker_box(1).contains(need_box(1, camera)) &&
                                          cache.worker_box(0).contains(need_box(0, camera));
            }
            out.max_horizon_slab = std::max(out.max_horizon_slab, slab);
            out.slabs_bounded = out.slabs_bounded &&
                                slab <= std::uint64_t(kHorizonSlabNodes) * kWindowNodes * kWindowNodes;
        }
        const unsigned n = gpu.frame(cache, budget);
        if (!busy && !n && !cache.has_work() &&
            (s.ready[0] >= 1.f && s.ready[1] >= 1.f && (!horizon || s.ready[2] >= 1.f)))
            break;
    }
    return out;
}

void logic() {
    planning();
    const double camera[3] = {(128. * 3 + 63 + .37) * kFineDelta, -91234.5, 1234567.};
    // Two-level reference: the cold fill order without the horizon.
    DensityCache two;
    Gpu gpu2;
    require("two_level_start", two.start_stepped() && !two.horizon() && two.levels() == kLevelCount);
    two.configure({7, 1, kNoOffset});
    gpu2.levels = kLevelCount; // a two-level caller passes two views
    const Fill f2 = fill(two, gpu2, camera, false, kDefaultUploadBudget);
    require("two_level_generates_no_horizon_node", two.level_nodes(2) == 0 && gpu2.rects[2] == 0);

    DensityCache three;
    Gpu gpu3;
    require("horizon_needs_a_started_cache", !three.enable_horizon() && !three.horizon() && !three.horizon_refused());
    require("three_level_start", three.start_stepped() && three.enable_horizon() && three.horizon() &&
                                     three.levels() == kMaxLevels &&
                                     !three.horizon_refused());
    three.configure({7, 1, kNoOffset});
    const Fill f3 = fill(three, gpu3, camera, true, kDefaultUploadBudget);
    std::printf("FILL far_resident_2=%lld fine_resident_2=%lld far_resident_3=%lld fine_resident_3=%lld "
                "first_horizon_frame=%lld horizon_ready_frame=%lld max_horizon_slab=%llu horizon_nodes=%llu "
                "max_upload_bytes=%zu horizon_upload_bytes=%zu\n",
                f2.far_resident, f2.fine_resident, f3.far_resident, f3.fine_resident, f3.first_horizon_step,
                f3.horizon_ready, (unsigned long long)f3.max_horizon_slab,
                (unsigned long long)three.level_nodes(2), gpu3.max_bytes, gpu3.horizon_bytes);
    require("cold_horizon_after_far_and_fine_need", f3.first_horizon_step > 0 && f3.horizon_after_needs);
    require("fine_need_not_later_than_two_levels", f3.fine_resident > 0 && f3.fine_resident <= f2.fine_resident &&
                                                       f3.far_resident == f2.far_resident);
    require("horizon_slabs_bounded", f3.slabs_bounded);
    require("horizon_ramps_to_ready", f3.horizon_ready > f3.first_horizon_step);
    require("budget_respected_and_far_fine_horizon_order", !gpu3.over_budget && !gpu3.out_of_order);
    require("horizon_uploads_only_after_far_and_fine", !gpu3.horizon_before_base_done && gpu3.rects[2] > 0);
    require("every_node_once", three.level_nodes(0) == 128ull * 128 * 128 && three.level_nodes(1) == 128ull * 128 * 128 &&
                                   three.level_nodes(2) == 128ull * 128 * 128);
    require("three_levels_equal_static", equal_static(gpu3, three, 0) && equal_static(gpu3, three, 1) &&
                                             equal_static(gpu3, three, 2));
    require("two_level_cache_unchanged", equal_static(gpu2, two, 0) && equal_static(gpu2, two, 1));
    // A tight budget: the horizon still waits for far and fine in every frame.
    {
        DensityCache tight;
        Gpu gpu;
        tight.start_stepped();
        tight.enable_horizon();
        tight.configure({8, 1, kNoOffset});
        fill(tight, gpu, camera, true, 2 * kTileBytes);
        require("tight_budget_shared_in_order", !gpu.over_budget && !gpu.out_of_order && !gpu.horizon_before_base_done &&
                                                    gpu.max_bytes <= 2 * kTileBytes && equal_static(gpu, tight, 2));
    }
    // Readiness: the horizon level has its own ramp; a Reset drops it with the others.
    {
        FrameState s = three.step(camera, 30000);
        require("horizon_ready_and_covering", s.ready[2] == 1.f && s.resident[2] && three.covers(2, camera));
        three.gpu_reset();
        s = three.step(camera, 30001);
        require("reset_drops_horizon", s.ready[2] == 0.f && !three.covers(2, camera));
        std::uint64_t frame = 30001;
        unsigned frames = 0;
        float previous = 0.f;
        bool ramp = true;
        while (frames++ < 400) {
            gpu3.frame(three);
            s = three.step(camera, ++frame);
            ramp = ramp && s.ready[2] - previous <= 1.f / kReadinessRampFrames + 1e-6f;
            previous = s.ready[2];
            if (s.ready[2] >= 1.f) break;
        }
        require("horizon_ramps_back_over_90_frames", ramp && s.ready[2] == 1.f && frames >= kReadinessRampFrames);
    }
    // drop_horizon: readiness 0 at once, nothing of the level is work or uploaded, far and fine keep their readiness,
    // the worker stops filling it, and a two-view caller works afterwards.
    {
        DensityCache cache;
        Gpu gpu;
        cache.start_stepped();
        cache.enable_horizon();
        cache.configure({9, 1, kNoOffset});
        std::uint64_t frame = 0;
        while (cache.level_nodes(2) == 0 && frame < 5000) {
            cache.step(camera, ++frame);
            cache.worker_step();
            gpu.frame(cache);
        }
        for (int i = 0; i < 3; ++i) cache.worker_step(); // horizon slabs committed, not uploaded
        require("horizon_cache_installed", cache.cache_bytes(2) != nullptr);
        cache.drop_horizon("atlas");
        require("drop_is_final", !cache.enable_horizon() && !cache.horizon());
        FrameState s = cache.step(camera, ++frame);
        const std::uint64_t horizon_nodes = cache.level_nodes(2);
        require("drop_is_immediate", !cache.horizon() && cache.levels() == kLevelCount && s.ready[2] == 0.f &&
                                         !cache.covers(2, camera) && !cache.level_dirty(2) &&
                                         std::strcmp(cache.horizon_refused(), "atlas") == 0);
        require("drop_frees_the_horizon_cache_at_the_next_post", cache.cache_bytes(2) == nullptr);
        gpu.levels = kLevelCount;
        unsigned frames = 0;
        for (bool busy = true; busy && frames < 20000; ++frames) {
            s = cache.step(camera, ++frame);
            busy = cache.worker_step();
            busy = gpu.frame(cache) != 0 || busy || cache.has_work();
        }
        for (unsigned ramp = 0; ramp < 200 && (s.ready[0] < 1.f || s.ready[1] < 1.f); ++ramp) s = cache.step(camera, ++frame);
        const unsigned horizon_rects = gpu.rects[2];
        require("dropped_horizon_is_not_filled_or_uploaded", cache.level_nodes(2) == horizon_nodes && horizon_rects == 0);
        require("base_levels_settle_after_drop", s.ready[0] == 1.f && s.ready[1] == 1.f && cache.idle() &&
                                                     equal_static(gpu, cache, 0) && equal_static(gpu, cache, 1));
    }
}

// SETA travel (fog-visible-distance.md; the user flies under SETA on 65-98 % of frames, game steps up to 2 s on single
// frames): the design's 5 km/s on screen (a 500 m/s ship at SETA x10) at 60 fps is 417 units per frame, and a 2 s game
// step of that ship is 5000 units, here every 120th frame. The worker runs at the measured 2.29 M nodes/s (38,167 nodes
// per 60 fps frame; a slab finishing past the frame's share is paid by the next frames: stepped commits land early by the
// slab's duration, the same for both caches). A stress row puts a 50,000-unit (10 km) step every 120th frame: reported
// for both caches, gated only on the horizon level doing no worse than the far level it hands over from.
struct Travel {
    unsigned frames = 0, fine_resident = 0, far_zero = 0, horizon_zero = 0, horizon_full = 0;
    float horizon_min = 1.f;
    std::uint64_t nodes[kMaxLevels]{};
};
Travel travel(bool horizon, double typical, double hitch) {
    DensityCache cache;
    Gpu gpu;
    cache.start_stepped();
    if (horizon) cache.enable_horizon();
    cache.configure({11, 1, kNoOffset});
    double camera[3] = {1000., 2000., -3000.};
    std::uint64_t frame = 0;
    for (unsigned i = 0; i < 20000; ++i) { // warm start: everything resident and ready
        const FrameState s = cache.step(camera, ++frame);
        const bool busy = cache.worker_step();
        if (!busy && !gpu.frame(cache) && !cache.has_work() && s.ready[0] >= 1.f && s.ready[1] >= 1.f &&
            (!horizon || s.ready[2] >= 1.f))
            break;
    }
    Travel t;
    std::uint64_t start[kMaxLevels];
    for (int l = 0; l < kMaxLevels; ++l) start[l] = cache.level_nodes(l);
    const double per_frame = 2.29e6 / 60.;
    double balance = 0;
    for (unsigned f = 1; f <= 900; ++f) {
        const double step = f % 120 == 0 ? hitch : typical;
        camera[0] += step * .8;
        camera[1] += step * .6;
        const FrameState s = cache.step(camera, ++frame);
        balance += per_frame;
        while (balance > 0) {
            const std::uint64_t before = level_sum(cache);
            if (!cache.worker_step()) {
                balance = std::min(balance, per_frame);
                break;
            }
            balance -= double(level_sum(cache) - before);
        }
        gpu.frame(cache);
        ++t.frames;
        t.fine_resident += s.resident[0];
        t.far_zero += s.ready[1] == 0.f;
        if (horizon) {
            t.horizon_zero += s.ready[2] == 0.f;
            t.horizon_full += s.ready[2] >= 1.f;
            t.horizon_min = std::min(t.horizon_min, s.ready[2]);
        }
    }
    for (int l = 0; l < kMaxLevels; ++l) t.nodes[l] = cache.level_nodes(l) - start[l];
    require("seta_budget_and_order", !gpu.over_budget && !gpu.out_of_order);
    return t;
}
void print(const char* tag, int levels, const Travel& t) {
    std::printf("%s levels=%d frames=%u fine_resident=%u far_zero=%u horizon_zero=%u horizon_full=%u "
                "horizon_min=%.4f nodes_fine=%llu nodes_far=%llu nodes_horizon=%llu\n",
                tag, levels, t.frames, t.fine_resident, t.far_zero, t.horizon_zero, t.horizon_full, double(t.horizon_min),
                (unsigned long long)t.nodes[0], (unsigned long long)t.nodes[1], (unsigned long long)t.nodes[2]);
}
void seta() {
    const double typical = 25000. / 60.;
    const Travel two = travel(false, typical, 5000.), three = travel(true, typical, 5000.);
    print("SETA", 2, two);
    print("SETA", 3, three);
    require("seta_far_never_drops", two.far_zero == 0 && three.far_zero == 0);
    require("seta_horizon_never_reaches_zero", three.horizon_zero == 0 && three.horizon_min > 0.f);
    // The horizon's share of the worker, and the fine level's residency against the two-level cache.
    const double share = double(three.nodes[2]) / double(three.nodes[0] + three.nodes[1] + three.nodes[2]);
    std::printf("SETA_SHARE horizon_share=%.4f fine_resident_ratio=%.4f\n", share,
                two.fine_resident ? double(three.fine_resident) / double(two.fine_resident) : 1.);
    require("seta_horizon_share_bounded", share <= .10);
    require("seta_fine_not_starved", three.fine_resident + three.frames / 20 >= two.fine_resident);
    const Travel stress2 = travel(false, typical, 50000.), stress3 = travel(true, typical, 50000.);
    print("SETA_STRESS", 2, stress2);
    print("SETA_STRESS", 3, stress3);
    require("stress_horizon_no_worse_than_far", stress3.horizon_zero <= stress3.far_zero + stress3.frames / 20);
}

#ifdef X3M_FOG_DENSITY_TEST_HOOKS
void alloc() {
    DensityCache::test_fail_horizon(true);
    DensityCache cache;
    const bool started = cache.start_stepped();
    const bool enabled = cache.enable_horizon();
    DensityCache::test_fail_horizon(false);
    require("horizon_allocation_failure_keeps_two_levels",
            started && !enabled && !cache.horizon() && cache.levels() == kLevelCount && cache.horizon_refused() &&
                std::strcmp(cache.horizon_refused(), "cpu_cache") == 0 && cache.cache_bytes(2) == nullptr);
    Gpu gpu;
    gpu.levels = kLevelCount;
    const double camera[3] = {5000., 6000., 7000.};
    cache.configure({12, 1, kNoOffset});
    const Fill f = fill(cache, gpu, camera, false, kDefaultUploadBudget);
    require("refused_horizon_fills_two_levels", f.fine_resident > 0 && cache.level_nodes(2) == 0 &&
                                                    cache.level_nodes(0) == 128ull * 128 * 128 &&
                                                    cache.level_nodes(1) == 128ull * 128 * 128 &&
                                                    !cache.covers(2, camera));
    // The threaded worker with the horizon: starts, fills all three levels and stops.
    DensityCache threaded;
    Gpu tgpu;
    require("threaded_start_with_horizon", threaded.start() && threaded.enable_horizon() && threaded.horizon());
    threaded.configure({13, 1, kNoOffset});
    FrameState s{};
    std::uint64_t frame = 0;
    const auto begin = std::chrono::steady_clock::now();
    while ((s.ready[0] < 1 || s.ready[1] < 1 || s.ready[2] < 1 || !threaded.idle()) &&
           std::chrono::steady_clock::now() - begin < std::chrono::seconds(120)) {
        s = threaded.step(camera, ++frame);
        tgpu.frame(threaded);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    threaded.stop();
    require("threaded_worker_fills_three_levels", s.ready[2] == 1.f && threaded.level_nodes(2) == 128ull * 128 * 128 &&
                                                      equal_static(tgpu, threaded, 0) && equal_static(tgpu, threaded, 1) &&
                                                      equal_static(tgpu, threaded, 2));
}
#endif

// The render thread's per-frame cost of the third level (the perf pass): step() on a settled cache, the camera still and
// moving 417 units per frame (the SETA travel), and has_work() (density_uploads' steady-state gate), two levels against
// three. Host CPU, -O2; the i686 proxy build differs in absolute terms, not in the ratio.
void perf() {
    for (bool horizon : {false, true}) {
        DensityCache cache;
        Gpu gpu;
        cache.start_stepped();
        if (horizon) cache.enable_horizon();
        cache.configure({14, 1, kNoOffset});
        double camera[3] = {1000., 2000., -3000.};
        fill(cache, gpu, camera, horizon, kDefaultUploadBudget);
        std::uint64_t frame = 100000;
        constexpr int calls = 200000;
        float sink = 0;
        auto time = [&](double step) {
            const auto begin = std::chrono::steady_clock::now();
            for (int i = 0; i < calls; ++i) {
                camera[0] += step;
                sink += cache.step(camera, ++frame).ready[1] + float(cache.has_work());
            }
            return double(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin)
                              .count()) /
                   calls;
        };
        const double still = time(0.), moving = time(417. / 64.); // sub-node steps: no new need box most frames
        std::printf("PERF levels=%d step_still_ns=%.1f step_moving_ns=%.1f sink=%g\n", horizon ? 3 : 2, still, moving,
                    double(sink > 0));
    }
}
} // namespace

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "all";
    const bool all = !std::strcmp(mode, "all");
    if (all || !std::strcmp(mode, "logic")) logic();
    if (all || !std::strcmp(mode, "seta")) seta();
    if (!std::strcmp(mode, "perf")) perf();
#ifdef X3M_FOG_DENSITY_TEST_HOOKS
    if (all || !std::strcmp(mode, "alloc")) alloc();
#endif
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
