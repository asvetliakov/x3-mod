// Host witness for src/fog/fog_density_cache.{h,cpp}: window / recentre / readiness
// logic on the deterministic stepped worker, then the real worker thread handoff.
// A plain heap atlas stands in for the SYSTEMMEM staging texture and the GPU copy.
// Usage: fog_density_cache_host [logic|threaded|all]
#include "../../src/fog/fog_density_cache.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>
using namespace x3m::fog;
namespace {
unsigned failures = 0, checks = 0;
bool require(const char* name, bool value) { ++checks; failures += !value; std::printf("CHECK %s %s\n", name, value ? "PASS" : "FAIL"); return value; }
double now_us() { return double(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) / 1e3; }

// The render-side half: staging doubles as the GPU atlas (an UpdateSurface of each rect is the identity here).
struct Gpu {
    std::vector<std::uint8_t> atlas[kLevelCount];
    std::size_t max_bytes = 0, max_rects = 0, frames_with_upload = 0;
    bool over_budget = false; // any single frame above its enforced cap, first rectangle included
    Gpu() { for (auto& a : atlas) a.assign(kAtlasBytes, 0xcd); }
    void lose() { for (auto& a : atlas) a.assign(kAtlasBytes, 0xcd); }
    // One frame of the upload protocol; `fail` drops the confirmation.
    unsigned frame(DensityCache& cache, std::size_t budget = kDefaultUploadBudget, bool fail = false) {
        if (!cache.has_work()) return 0;
        StagingView views[kLevelCount];
        for (int l = 0; l < kLevelCount; ++l) views[l] = {atlas[l].data(), kAtlasPitch};
        TileRect rects[kDefaultUploadRects];
        const unsigned n = cache.take_uploads(views, budget, rects, unsigned(sizeof rects / sizeof rects[0]));
        std::size_t bytes = 0;
        for (unsigned i = 0; i < n; ++i) bytes += rects[i].bytes();
        over_budget = over_budget || bytes > std::max(budget, kTileBytes);
        max_bytes = std::max(max_bytes, bytes); max_rects = std::max<std::size_t>(max_rects, n); frames_with_upload += n != 0;
        cache.confirm_uploads(!fail);
        return n;
    }
};
std::vector<std::uint8_t> static_atlas(int level, const NodeKey& origin, const WorldOffset& offset) {
    std::vector<std::uint8_t> out(kAtlasBytes, 0);
    for (int g = 0; g < kGroupCount; ++g)
        generate_tile(kLevelDelta[level], origin, offset, g, out.data() + std::size_t(g / kGroupsPerRow) * kTileTexels * kAtlasPitch + std::size_t(g % kGroupsPerRow) * kTileTexels * kTexelBytes, kAtlasPitch);
    return out;
}
// Render-thread state only (gpu_box): safe against a live worker. The resident box must be a whole window.
bool equal_static(const Gpu& gpu, const DensityCache& cache, const WorldOffset& offset, const char* label) {
    bool same = true;
    for (int l = 0; l < kLevelCount; ++l) {
        const NodeBox box = cache.gpu_box(l);
        const NodeKey origin{box.lo[0], box.lo[1], box.lo[2]};
        const auto expect = static_atlas(l, origin, offset);
        std::size_t differing = 0;
        for (std::size_t i = 0; i < kAtlasBytes; ++i) differing += expect[i] != gpu.atlas[l][i];
        std::printf("ATLAS %s level=%d differing_bytes=%zu\n", label, l, differing);
        same = same && differing == 0 && !box.empty() && box == window_box(origin);
    }
    return same;
}
// Runs worker and uploads to quiescence; returns frames used.
unsigned settle(DensityCache& cache, Gpu& gpu, const double camera[3], std::uint64_t& frame, FrameState* last = nullptr) {
    unsigned frames = 0;
    for (bool busy = true; busy && frames < 100000; ++frames) {
        const FrameState s = cache.step(camera, ++frame);
        if (last) *last = s;
        busy = cache.worker_step();
        busy = gpu.frame(cache) != 0 || busy || cache.has_work();
    }
    return frames;
}

void planning() {
    const double camera[3] = {95576., 97323., 82698.};
    for (int level = 0; level < kLevelCount; ++level) {
        const NodeKey origin = window_origin(kLevelDelta[level], camera[0], camera[1], camera[2]);
        const NodeBox window = window_box(origin), need = need_box(level, camera);
        require(level ? "far_need_inside_window_with_14_nodes" : "fine_need_inside_window_with_4_nodes",
                window.contains(grow(need, level ? 14 : 4)) && !window.contains(grow(need, level ? 15 : 5)));
        require("no_retarget_at_centre", !retarget_needed(level, origin, camera));
        double moved[3] = {camera[0] + kRetargetNodes[level] * kLevelDelta[level], camera[1], camera[2]};
        require("retarget_at_threshold", retarget_needed(level, origin, moved) && window.contains(need_box(level, moved)));
        moved[0] -= kLevelDelta[level];
        require("no_retarget_below_threshold", !retarget_needed(level, origin, moved));
        std::unique_ptr<Job[]> jobs(new Job[kMaxJobs]);
        const std::size_t count = plan_jobs(window, level, camera, jobs.get(), kMaxJobs);
        std::uint64_t nodes = 0; bool cells = true, sorted = true;
        for (std::size_t i = 0; i < count; ++i) {
            const NodeBox& b = jobs[i].box; nodes += b.nodes();
            for (int a = 0; a < 3; ++a) { const std::int64_t cell = a == 2 ? kLanes : kBrickTexels; cells = cells && (b.lo[a] & ~(cell - 1)) == (b.hi[a] & ~(cell - 1)); }
            cells = cells && window.contains(b) && b.nodes() <= kJobNodes;
            sorted = sorted && (!i || jobs[i - 1].distance2 <= jobs[i].distance2);
        }
        // Disjoint + same node total + inside the slab = exact cover.
        bool disjoint = true;
        for (std::size_t i = 0; i < count && disjoint; ++i) for (std::size_t j = i + 1; j < count; ++j) if (!intersect(jobs[i].box, jobs[j].box).empty()) { disjoint = false; break; }
        std::printf("PLAN level=%d jobs=%zu nodes=%llu\n", level, count, (unsigned long long)nodes);
        require("plan_exact_cover_of_window", count > 0 && count <= kMaxJobs && nodes == window.nodes() && disjoint);
        require("plan_jobs_in_one_storage_cell_nearest_first", cells && sorted);
        // Growth by slabs always stays a box and ends at the window; the urgent side goes first.
        NodeBox have = intersect(window, grow(need, kFirstFillSlack[level])), slab; NodeBox urgent_need = have; urgent_need.hi[1] += 1;
        require("urgent_side_first", next_slab(have, window, urgent_need, slab) && slab.lo[1] == have.hi[1] + 1 && slab.hi[1] == window.hi[1] && slab.lo[0] == have.lo[0] && slab.hi[2] == have.hi[2]);
        std::uint64_t grown = have.nodes(); unsigned steps = 0;
        while (next_slab(have, window, need, slab) && steps < 8) { ++steps; grown += slab.nodes(); require("slab_disjoint_from_box", intersect(have, slab).empty()); for (int a = 0; a < 3; ++a) { have.lo[a] = std::min(have.lo[a], slab.lo[a]); have.hi[a] = std::max(have.hi[a], slab.hi[a]); } }
        require("slabs_reach_window_exactly", have == window && grown == window.nodes() && steps <= 6);
    }
    require("box_algebra", intersect(empty_box(), universe_box()).empty() && universe_box().contains(empty_box()) && !empty_box().contains(universe_box()) && empty_box() == intersect(window_box({0, 0, 0}), window_box({200, 0, 0})));
}

void logic() {
    planning();
    // Camera whose window origin has storage index 126 on every axis of the fine level, so +2-node
    // recentres write storage 126,127 then 0,1: the tile seam with its duplicate border in x and y,
    // group 31 lane 3 -> group 0 lane 0 in z.
    double camera[3];
    for (int a = 0; a < 3; ++a) camera[a] = (128. * (3 + a) + 126 + 63 + .37) * kFineDelta;
    const CacheIdentity identity{0x1234, 1, {4096. * 5, -4096. * 9, 4096. * 2}};
    DensityCache cache; Gpu gpu; std::uint64_t frame = 0;
    require("stepped_start", cache.start_stepped());
    require("off_before_any_frame", !cache.has_work() && cache.stats().nodes_generated == 0);
    cache.configure(identity); cache.gpu_reset();
    {
        const NodeKey o = window_origin(kFineDelta, camera[0], camera[1], camera[2]);
        require("origin_storage_126", storage_index(o.x) == 126 && storage_index(o.y) == 126 && storage_index(o.z) == 126);
    }
    {   // has_work() is false between slabs although the windows are far from complete: only idle() means quiescent,
        // and readiness never counts a node that is not generated, uploaded and confirmed.
        FrameState s = cache.step(camera, ++frame);
        require("not_idle_before_the_worker_saw_the_camera", !cache.idle());
        cache.worker_step();
        while (gpu.frame(cache)) {}
        s = cache.step(camera, ++frame);
        require("has_work_false_mid_fill_is_not_idle", !cache.has_work() && !cache.idle() && cache.stats().nodes_generated < 2ull * 128 * 128 * 128);
        require("mid_fill_resident_box_is_exactly_the_uploaded_slab", s.resident[1] && !s.resident[0] && cache.gpu_box(0).empty() &&
                cache.gpu_box(1) == intersect(window_box(cache.worker_origin(1)), grow(need_box(1, camera), kFirstFillSlack[1])) && cache.gpu_box(1).nodes() == cache.stats().nodes_generated);
        // A committed but not yet uploaded slab does not count either.
        cache.worker_step();
        s = cache.step(camera, ++frame);
        require("committed_but_not_uploaded_is_not_resident", cache.has_work() && !s.resident[0] && s.ready[0] == 0);
    }
    // First fill: far becomes resident first, readiness ramps 1/90 per frame, monotone, no regression.
    unsigned far_ready_frame = 0, fine_ready_frame = 0, frames = 0; bool monotone = true, bounded = true; float previous[2] = {0, cache.step(camera, frame).ready[1]};
    for (bool busy = true; (busy || previous[0] < 1 || previous[1] < 1) && frames < 100000; ++frames) {
        const FrameState s = cache.step(camera, ++frame);
        for (int l = 0; l < kLevelCount; ++l) { monotone = monotone && s.ready[l] >= previous[l]; bounded = bounded && s.ready[l] - previous[l] <= 1.f / kReadinessRampFrames + 1e-6f; previous[l] = s.ready[l]; }
        if (!far_ready_frame && s.resident[1]) far_ready_frame = frames + 1;
        if (!fine_ready_frame && s.resident[0]) fine_ready_frame = frames + 1;
        busy = cache.worker_step(); busy = gpu.frame(cache) != 0 || busy || cache.has_work();
    }
    const CacheStats filled = cache.stats();
    std::printf("FILL frames=%u far_resident_frame=%u fine_resident_frame=%u nodes=%llu jobs=%llu slabs=%llu max_upload_bytes=%zu max_rects=%zu upload_bytes=%llu\n", frames, far_ready_frame, fine_ready_frame,
                (unsigned long long)filled.nodes_generated, (unsigned long long)filled.jobs, (unsigned long long)filled.slabs, gpu.max_bytes, gpu.max_rects, (unsigned long long)filled.upload_bytes);
    require("far_resident_before_fine", far_ready_frame && fine_ready_frame && far_ready_frame <= fine_ready_frame);
    require("readiness_monotone_bounded_reaches_one", monotone && bounded && previous[0] == 1 && previous[1] == 1);
    require("fill_generates_each_node_once", filled.nodes_generated == 2ull * kWindowNodes * kWindowNodes * kWindowNodes && filled.first_fills == 2);
    require("upload_budget_enforced_including_the_first_rectangle", !gpu.over_budget && gpu.max_bytes <= kDefaultUploadBudget && gpu.max_bytes > 0);
    require("dynamic_fill_equals_static_atlas", equal_static(gpu, cache, identity.offset, "fill"));
    require("idle_when_settled", !cache.has_work() && !cache.worker_step() && cache.idle());
    {   // Steady state: no lock, no work.
        const std::uint64_t missed = cache.stats().missed_locks; const double start = now_us(); const int n = 200000;
        for (int i = 0; i < n; ++i) cache.step(camera, ++frame);
        std::printf("STEP_COST steady_ns_per_frame=%.1f\n", (now_us() - start) * 1e3 / n);
        require("steady_state_no_uploads_no_misses", !cache.has_work() && cache.stats().missed_locks == missed && cache.stats().nodes_generated == filled.nodes_generated);
    }
    // Recentre across the seam: 2 fine nodes per move on all three axes, five moves (storage 126..135 -> wraps).
    bool recentre = true; std::uint64_t before = cache.stats().nodes_generated; const std::vector<std::uint8_t> untouched_far = gpu.atlas[1];
    for (int move = 0; move < 5; ++move) {
        for (int a = 0; a < 3; ++a) camera[a] += 2 * kFineDelta;
        FrameState s; settle(cache, gpu, camera, frame, &s);
        const std::uint64_t after = cache.stats().nodes_generated, expect = 128ull * 128 * 128 - 126ull * 126 * 126;
        std::printf("RECENTRE move=%d nodes=%llu expected=%llu fine_ready=%.3f\n", move, (unsigned long long)(after - before), (unsigned long long)expect, s.ready[0]);
        recentre = recentre && after - before == expect && s.ready[0] == 1 && s.ready[1] == 1 && equal_static(gpu, cache, identity.offset, "recentre");
        before = after;
    }
    require("recentre_across_seam_and_lane_wrap_equals_from_scratch", recentre);
    require("recentre_uploads_no_full_refill", cache.stats().retargets == 5 && cache.stats().first_fills == 2);
    require("far_window_untouched_below_its_threshold", untouched_far == gpu.atlas[1]);
    {   // Back across the seam in the negative direction, far level included (9 far nodes).
        for (int a = 0; a < 3; ++a) camera[a] -= 9 * kFarDelta;
        FrameState s; settle(cache, gpu, camera, frame, &s);
        require("negative_recentre_both_levels_equals_from_scratch", equal_static(gpu, cache, identity.offset, "negative"));
        // 72 fine nodes is inside the 128 window (overlap 56 planes), so no first fill for fine either.
        require("large_shift_reuses_overlap", cache.stats().first_fills == 2);
    }
    {   // Stalled worker: guard violation ramps down, need violation drops at once.
        for (int i = 0; i < 100; ++i) cache.step(camera, ++frame);
        double away[3] = {camera[0] + 5 * kFineDelta, camera[1], camera[2]}; // need hi = window hi: inside, guard outside
        const NodeBox box = cache.gpu_box(0), need = need_box(0, away);
        FrameState s = cache.step(away, ++frame);
        const bool soft = box.contains(need) && !box.contains(grow(need, kReadinessGuard));
        require("guard_violation_ramps_down", soft && s.ready[0] < 1 && s.ready[0] >= 1 - 1.5f / kReadinessRampFrames && s.ready[1] == 1);
        away[0] += 2 * kFineDelta; s = cache.step(away, ++frame);
        require("need_violation_drops_fine_only", s.ready[0] == 0 && !s.resident[0] && s.ready[1] == 1 && !cache.covers(0, away) && cache.covers(1, away));
        settle(cache, gpu, away, frame, &s);
        for (int a = 0; a < 3; ++a) camera[a] = away[a];
        require("fine_recovers_and_ramps", s.resident[0] && s.ready[0] > 0 && equal_static(gpu, cache, identity.offset, "stall"));
    }
    {   // Reset mid-recentre and a failed upload: everything is uploaded again, nothing regenerated twice.
        for (int a = 0; a < 3; ++a) camera[a] += 2 * kFineDelta;
        cache.step(camera, ++frame); cache.worker_step(); cache.worker_step(); gpu.frame(cache, 64 * 1024);
        const std::uint64_t generated_before = cache.stats().nodes_generated;
        gpu.lose(); cache.gpu_reset();
        FrameState s = cache.step(camera, ++frame);
        require("reset_drops_readiness", s.ready[0] == 0 && s.ready[1] == 0 && !cache.covers(1, camera));
        gpu.frame(cache, kDefaultUploadBudget, true); // injected upload failure
        settle(cache, gpu, camera, frame, &s);
        require("reset_mid_fill_recovers_from_cpu_cache", equal_static(gpu, cache, identity.offset, "reset") && s.resident[0] && s.resident[1]);
        require("reset_regenerates_only_the_pending_slabs", cache.stats().nodes_generated - generated_before <= 128ull * 128 * 128 - 126ull * 126 * 126);
    }
    {   // Identity change: stale data dropped, new offset generated, ramps restart.
        CacheIdentity other = identity; other.sector_key = 0x77; other.offset.x += 4096. * 100;
        cache.configure(other);
        FrameState s = cache.step(camera, ++frame);
        require("identity_change_invalidates", s.ready[0] == 0 && s.ready[1] == 0 && cache.gpu_box(1).empty());
        settle(cache, gpu, camera, frame, &s);
        require("new_identity_equals_static_with_its_offset", equal_static(gpu, cache, other.offset, "identity") && cache.stats().first_fills == 4);
        cache.configure(other);
        require("same_identity_is_not_an_invalidation", !cache.gpu_box(1).empty());
    }
    {   // Camera cut far away: toroidal slots are reused, nothing of the old window may count as resident.
        camera[0] += 1e6; camera[2] -= 3e6;
        FrameState s = cache.step(camera, ++frame);
        require("cut_drops_both", s.ready[0] == 0 && s.ready[1] == 0);
        settle(cache, gpu, camera, frame, &s);
        require("cut_refill_equals_static", equal_static(gpu, cache, cache.identity().offset, "cut"));
    }
    const double nan_camera[3] = {std::numeric_limits<double>::quiet_NaN(), 0, 0};
    require("non_finite_camera_is_not_ready", cache.step(nan_camera, ++frame).ready[1] == 0);
    {   // One-tile budget: the enforced cap is one tile, never one tile more.
        Gpu small; DensityCache tight; std::uint64_t f = 0; tight.start_stepped(); tight.gpu_reset();
        const double c[3] = {95576., 97323., 82698.};
        for (int i = 0; i < 6; ++i) { tight.step(c, ++f); tight.worker_step(); }
        unsigned frames_used = 0; while (small.frame(tight, 1) && frames_used < 100000) ++frames_used; // a 1-byte budget is raised to one tile
        std::printf("TIGHT_BUDGET frames=%u max_upload_bytes=%zu\n", frames_used, small.max_bytes);
        require("tight_budget_never_exceeds_one_tile_and_still_drains", !small.over_budget && small.max_bytes <= kTileBytes && small.max_bytes > 0 && !tight.has_work());
    }
    {   // Admission boundary: node keys near +-2^31, where lo + hi of a job box leaves int32.
        DensityCache edge; Gpu far_gpu; std::uint64_t f = 0; edge.start_stepped(); edge.gpu_reset();
        const double c[3] = {kCameraLimit, -kCameraLimit, 5.6e11};
        std::unique_ptr<Job[]> jobs(new Job[kMaxJobs]); bool sane = true;
        for (int level = 0; level < kLevelCount; ++level) {
            const std::size_t count = plan_jobs(window_box(window_origin(kLevelDelta[level], c[0], c[1], c[2])), level, c, jobs.get(), kMaxJobs);
            sane = sane && count > 0 && jobs[0].distance2 < 3 * 32. * 32. && jobs[count - 1].distance2 < 3 * 80. * 80.;
            for (std::size_t i = 0; i < count; ++i) sane = sane && jobs[i].distance2 >= 0 && jobs[i].distance2 < 3 * 80. * 80.;
        }
        require("job_distances_at_the_camera_limit_do_not_overflow", sane);
        FrameState s; settle(edge, far_gpu, c, f, &s);
        require("camera_limit_fills_and_equals_static", s.resident[0] && s.resident[1] && edge.idle() && equal_static(far_gpu, edge, kNoOffset, "limit"));
        const double beyond[3] = {std::nextafter(kCameraLimit, 2e12), 0, 0};
        s = edge.step(beyond, ++f);
        require("camera_beyond_the_limit_is_refused", !s.resident[0] && !s.resident[1] && s.ready[1] == 0 && !edge.covers(1, beyond) && edge.covers(1, c));
    }
}

void threaded() {
    double camera[3] = {95576., 97323., 82698.};
    {
        DensityCache cache; Gpu gpu; std::uint64_t frame = 0;
        require("worker_started", cache.start() && cache.running());
        cache.gpu_reset();
        const double begin = now_us(); double far_ready = 0, fine_ready = 0, worst_step = 0, worst_frame = 0; FrameState s{};
        // Quiescent = idle(): has_work() alone is false while the worker generates its next slab.
        while ((s.ready[0] < 1 || s.ready[1] < 1 || !cache.idle()) && now_us() - begin < 120e6) {
            const double t0 = now_us(); s = cache.step(camera, ++frame); const double t1 = now_us(); gpu.frame(cache); const double t2 = now_us();
            worst_step = std::max(worst_step, t1 - t0); worst_frame = std::max(worst_frame, t2 - t1);
            if (!far_ready && s.resident[1]) far_ready = t2 - begin;
            if (!fine_ready && s.resident[0]) fine_ready = t2 - begin;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const CacheStats st = cache.stats();
        std::printf("THREADED far_resident_ms=%.1f fine_resident_ms=%.1f frames=%llu nodes=%llu worker_busy_ms=%.1f nodes_per_second=%.0f worst_step_us=%.1f worst_upload_frame_us=%.1f max_upload_bytes=%zu max_rects=%zu missed_locks=%llu\n",
                    far_ready / 1e3, fine_ready / 1e3, (unsigned long long)frame, (unsigned long long)st.nodes_generated, st.worker_busy_us / 1e3, st.worker_busy_us ? st.nodes_generated * 1e6 / st.worker_busy_us : 0., worst_step, worst_frame, gpu.max_bytes, gpu.max_rects, (unsigned long long)st.missed_locks);
        require("threaded_fill_equals_static_atlas", s.ready[0] == 1 && s.ready[1] == 1 && equal_static(gpu, cache, kNoOffset, "threaded"));
        require("threaded_budget_respected", gpu.max_bytes <= kDefaultUploadBudget);
        // Moving camera against the live worker: 40 moves of one fine node, then settle.
        for (int move = 0; move < 40; ++move) { camera[move % 3] += kFineDelta * (move % 5 == 4 ? -1 : 1); for (int i = 0; i < 3; ++i) { cache.step(camera, ++frame); gpu.frame(cache); std::this_thread::sleep_for(std::chrono::milliseconds(1)); } }
        const double settle_begin = now_us();
        for (;;) { cache.step(camera, ++frame); gpu.frame(cache); if (cache.idle() || now_us() - settle_begin > 60e6) break; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        const bool quiet = cache.idle();
        cache.stop(); // worker-owned state (worker_origin) is read only after the join
        bool centred = true;
        for (int l = 0; l < kLevelCount; ++l) centred = centred && cache.gpu_box(l) == window_box(cache.worker_origin(l)) && !retarget_needed(l, cache.worker_origin(l), camera);
        require("threaded_recentre_equals_static_atlas", quiet && centred && equal_static(gpu, cache, kNoOffset, "threaded_recentre"));
        require("stop_is_idempotent", !cache.running()); cache.stop();
    }
    {   // Shutdown while generating, invalidation storms, restart: no deadlock, prompt join.
        double worst = 0; bool ok = true;
        for (int round = 0; round < 24; ++round) {
            DensityCache cache; Gpu gpu; std::uint64_t frame = 0; ok = ok && cache.start(); cache.gpu_reset();
            for (int i = 0; i < 4 + round; ++i) {
                if (i % 3 == 1) { CacheIdentity id; id.sector_key = std::uint64_t(round) * 16 + i; id.offset.x = 4096. * i; cache.configure(id); }
                if (i % 5 == 2) cache.invalidate();
                if (i % 7 == 3) cache.gpu_reset();
                cache.step(camera, ++frame); gpu.frame(cache);
                std::this_thread::sleep_for(std::chrono::microseconds(200 * (round % 5)));
            }
            const double t = now_us(); if (round % 2) cache.stop(); // odd rounds: explicit stop, even: destructor joins
            if (round % 2) worst = std::max(worst, now_us() - t);
        }
        std::printf("SHUTDOWN rounds=24 worst_stop_ms=%.2f\n", worst / 1e3);
        require("shutdown_mid_fill_joins_promptly", ok && worst < 500e3);
    }
    {   // Invalidate under a live worker converges to the new identity.
        DensityCache cache; Gpu gpu; std::uint64_t frame = 0; cache.start(); cache.gpu_reset();
        for (int i = 0; i < 50; ++i) { cache.step(camera, ++frame); gpu.frame(cache); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        CacheIdentity id; id.sector_key = 9; id.offset = {4096. * 3, 0, -4096. * 7}; cache.configure(id);
        const double begin = now_us(); FrameState s{};
        while ((!s.resident[0] || !s.resident[1] || !cache.idle()) && now_us() - begin < 120e6) { s = cache.step(camera, ++frame); gpu.frame(cache); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        cache.stop();
        require("invalidate_under_live_worker_equals_static", equal_static(gpu, cache, id.offset, "live_invalidate"));
    }
}
}  // namespace
int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "all";
    if (mode == "logic" || mode == "all") logic();
    if (mode == "threaded" || mode == "all") threaded();
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
