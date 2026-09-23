// Host witness for the fog hand-over (docs/architecture/fog-handover.md, "Implementation"):
// the cold-start readiness step against the warm ramp, the cold fill's single whole-atlas latch
// past the byte budget, and the docked view's bounded parent walk (sector_background.h).
// Stepped cache (no thread), a FogPass-shaped upload loop (views only for dirty levels, staging
// then a separate GPU copy per rectangle); synthetic engine memory for the walk. No Wine, no D3D.
#include "../../src/fog/fog_density_cache.h"
#include "sector_background_memory.h"
#include "fog_prefill.h"
#include "fog_card_policy.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
using namespace x3m::fog;
namespace {
unsigned failures = 0, checks_run = 0;
bool require(const char* name, bool value) { ++checks_run; failures += !value; std::printf("CHECK %s %s\n", name, value ? "PASS" : "FAIL"); return value; }

// The ramp from the first resident frame (1/90) to 1: 89 or 90 frames depending on the float sum.
bool ramp_length(long long resident, long long ready) { return resident > 0 && ready - resident >= long(kReadinessRampFrames) - 1 && ready - resident <= long(kReadinessRampFrames); }
const double kCamera[3] = {1.0e6 + 1234.5, 2.0e5 - 77.25, -3.0e5 + 4096.125};
const CacheIdentity kIdentity{0x5ec7, 1, kNoOffset};

struct Latch { unsigned rects[kLevelCount]{}; std::size_t bytes[kLevelCount]{}; bool whole[kLevelCount]{}, far_view = false; };
struct Gpu {
    std::vector<std::uint8_t> staging[kLevelCount], atlas[kLevelCount];
    Gpu() { for (int l = 0; l < kLevelCount; ++l) { staging[l].assign(kAtlasBytes, 0xcd); atlas[l].assign(kAtlasBytes, 0xcd); } }
    // FogPass::density_uploads: nothing without work; staging only for dirty levels; confirm after the copies.
    bool fail_next = false; // drop the next confirmation (a lost device inside the upload)
    Latch frame(DensityCache& cache, std::size_t budget) {
        Latch out;
        if (!cache.has_work()) return out;
        StagingView views[kLevelCount]{};
        for (int l = 0; l < kLevelCount; ++l) if (cache.level_dirty(l)) views[l] = {staging[l].data(), kAtlasPitch};
        out.far_view = views[1].bits != nullptr; // the far staging would be locked this frame
        TileRect rects[kDefaultUploadRects];
        const unsigned n = cache.take_uploads(views, budget, rects, kDefaultUploadRects);
        for (unsigned i = 0; i < n; ++i) {
            const TileRect& r = rects[i];
            for (int y = 0; y < r.height; ++y)
                std::memcpy(atlas[r.level].data() + std::size_t(r.y + y) * kAtlasPitch + std::size_t(r.x) * kTexelBytes,
                            staging[r.level].data() + std::size_t(r.y + y) * kAtlasPitch + std::size_t(r.x) * kTexelBytes, std::size_t(r.width) * kTexelBytes);
            ++out.rects[r.level]; out.bytes[r.level] += r.bytes();
            out.whole[r.level] = out.whole[r.level] || (r.x == 0 && r.y == 0 && r.width == kAtlasWidth && r.height == kAtlasHeight);
        }
        cache.confirm_uploads(!fail_next);
        fail_next = false;
        return out;
    }
};
struct Frame { FrameState state; Latch latch; HandoverReport report; };
// One frame in FogPass order (step, uploads), then up to `slabs` worker slabs.
Frame frame(DensityCache& cache, Gpu& gpu, const double camera[3], std::uint64_t f, unsigned slabs = 1, std::size_t budget = kDefaultUploadBudget) {
    Frame out;
    out.state = cache.step(camera, f);
    out.latch = gpu.frame(cache, budget);
    out.report = cache.take_handover();
    for (unsigned i = 0; i < slabs && cache.worker_step(); ++i) {}
    return out;
}
std::vector<std::uint8_t> static_atlas(int level, const NodeKey& origin, const WorldOffset& offset = kNoOffset) {
    std::vector<std::uint8_t> out(kAtlasBytes, 0);
    for (int g = 0; g < kGroupCount; ++g)
        generate_tile(kLevelDelta[level], origin, offset, g, out.data() + std::size_t(g / kGroupsPerRow) * kTileTexels * kAtlasPitch + std::size_t(g % kGroupsPerRow) * kTileTexels * kTexelBytes, kAtlasPitch);
    return out;
}
bool equal_static(const Gpu& gpu, const DensityCache& cache, const char* label, const WorldOffset& offset = kNoOffset) {
    bool same = true;
    for (int l = 0; l < kLevelCount; ++l) {
        const NodeBox box = cache.gpu_box(l);
        const auto expect = static_atlas(l, NodeKey{box.lo[0], box.lo[1], box.lo[2]}, offset);
        std::size_t differing = 0;
        for (std::size_t i = 0; i < kAtlasBytes; ++i) differing += expect[i] != gpu.atlas[l][i];
        std::printf("ATLAS %s level=%d differing_bytes=%zu\n", label, l, differing);
        same = same && differing == 0 && box.nodes() == std::uint64_t(kWindowNodes) * kWindowNodes * kWindowNodes;
    }
    return same;
}
// Pumps until both windows are complete, resident, ramped and the worker is idle.
bool settle(DensityCache& cache, Gpu& gpu, const double camera[3], std::uint64_t& f, std::size_t budget, std::size_t* max_latch_bytes = nullptr) {
    for (int i = 0; i < 4000; ++i) {
        const Frame fr = frame(cache, gpu, camera, ++f, 4, budget);
        if (max_latch_bytes) *max_latch_bytes = std::max(*max_latch_bytes, fr.latch.bytes[0] + fr.latch.bytes[1]);
        if (fr.state.ready[0] >= 1 && fr.state.ready[1] >= 1 && cache.idle()) return true;
    }
    return false;
}

// Cold start with the step: ready_far is 0 until the far need box is resident, then 1 in that frame.
void cold_step_then_warm_ramp() {
    DensityCache cache; Gpu gpu; std::uint64_t f = 0;
    require("stepped_cache_starts", cache.start_stepped());
    cache.set_handover(true, false);
    cache.configure(kIdentity);
    long long resident = -1, ready = -1; bool zero_before = true; HandoverReport report;
    for (int i = 0; i < 2000 && ready < 0; ++i) {
        const Frame fr = frame(cache, gpu, kCamera, ++f);
        if (fr.state.resident[1] && resident < 0) resident = (long long)f;
        if (resident < 0) zero_before = zero_before && fr.state.ready[1] == 0.f;
        if (fr.state.ready[1] >= 1.f) ready = (long long)f;
        if (fr.report.due) report = fr.report;
    }
    std::printf("COLD_STEP resident_frame=%lld ready_frame=%lld arm_frame=%llu latches=%u upload_bytes=%llu drawable_us=%lld fill_us=%lld fill_busy_us=%lld fill_cpu_us=%lld\n",
                resident, ready, (unsigned long long)report.arm_frame, report.latches, (unsigned long long)report.upload_bytes, (long long)report.drawable_us,
                (long long)report.fill_us, (long long)report.fill_busy_us, (long long)report.fill_cpu_us);
    require("cold_step_ready_far_is_zero_until_resident", zero_before && resident > 1);
    require("cold_step_ready_far_steps_to_one_in_the_resident_frame", ready == resident);
    require("cold_step_report_due_once_in_that_frame", report.due && report.step && !report.cold_fill && report.arm_frame == 1 &&
            report.drawable_frame == std::uint64_t(resident) && report.ready_frame == std::uint64_t(ready) && report.drawable_us >= 0 && report.ready_us >= report.drawable_us);
    require("cold_step_report_fill_measured", report.fill_us >= 0 && report.fill_us <= report.drawable_us && report.fill_busy_us >= 0 && report.latches >= 1);
    require("cold_step_report_taken_once", !cache.take_handover().due);
    require("cold_step_settles_to_the_static_field", settle(cache, gpu, kCamera, f, kDefaultUploadBudget) && equal_static(gpu, cache, "cold_step"));

    // Warm: the same identity, the camera jumps 300 far nodes (outside the window): residency is lost and refilled, ramped.
    const double jumped[3] = {kCamera[0] + 300 * kFarDelta, kCamera[1], kCamera[2]};
    long long warm_resident = -1, warm_ready = -1; float previous = 0.f, largest_step = 0.f; bool dropped = false, report_due = false;
    for (int i = 0; i < 4000 && warm_ready < 0; ++i) {
        const Frame fr = frame(cache, gpu, jumped, ++f);
        if (i == 0) dropped = fr.state.ready[1] == 0.f;
        if (fr.state.resident[1] && warm_resident < 0) warm_resident = (long long)f;
        if (warm_resident >= 0) largest_step = std::max(largest_step, fr.state.ready[1] - previous);
        previous = fr.state.ready[1];
        if (fr.state.ready[1] >= 1.f) warm_ready = (long long)f;
        report_due = report_due || fr.report.due;
    }
    std::printf("WARM_RAMP resident_frame=%lld ready_frame=%lld frames=%lld largest_step=%.6f\n", warm_resident, warm_ready, warm_ready - warm_resident, double(largest_step));
    require("warm_jump_drops_readiness_at_once", dropped);
    require("warm_refill_keeps_the_ramp", ramp_length(warm_resident, warm_ready) && largest_step <= 1.f / kReadinessRampFrames + 1e-6f);
    require("warm_refill_reports_nothing", !report_due);

    // Cold again through invalidate (the proxy's load gap): the step returns.
    cache.invalidate();
    long long again_resident = -1, again_ready = -1; HandoverReport again;
    for (int i = 0; i < 2000 && again_ready < 0; ++i) {
        const Frame fr = frame(cache, gpu, jumped, ++f);
        if (fr.state.resident[1] && again_resident < 0) again_resident = (long long)f;
        if (fr.state.ready[1] >= 1.f) again_ready = (long long)f;
        if (fr.report.due) again = fr.report;
    }
    require("invalidate_is_a_cold_start_and_steps", again_resident > 0 && again_ready == again_resident && again.due && again.arm_frame > std::uint64_t(warm_ready));
}

// Legacy (both switches off): the 90-frame ramp from the resident frame, one report at its end.
void cold_ramp_legacy() {
    DensityCache cache; Gpu gpu; std::uint64_t f = 0;
    cache.start_stepped();
    cache.configure(kIdentity);
    long long resident = -1, ready = -1; float first = -1.f; HandoverReport report; std::size_t max_bytes = 0; unsigned far_latches = 0;
    for (int i = 0; i < 2000 && ready < 0; ++i) {
        const Frame fr = frame(cache, gpu, kCamera, ++f);
        if (resident < 0) { far_latches += fr.latch.rects[1] != 0; max_bytes = std::max(max_bytes, fr.latch.bytes[0] + fr.latch.bytes[1]); }
        if (fr.state.resident[1] && resident < 0) { resident = (long long)f; first = fr.state.ready[1]; }
        if (fr.state.ready[1] >= 1.f) ready = (long long)f;
        if (fr.report.due) report = fr.report;
    }
    std::printf("COLD_RAMP resident_frame=%lld ready_frame=%lld frames=%lld far_latches=%u max_latch_bytes=%zu report_latches=%u\n",
                resident, ready, ready - resident, far_latches, max_bytes, report.latches);
    require("legacy_ramp_starts_at_one_ninetieth", resident > 0 && first == 1.f / float(kReadinessRampFrames));
    require("legacy_ramp_reaches_one_after_the_ramp", ramp_length(resident, ready));
    require("legacy_report_after_the_ramp", report.due && !report.step && !report.cold_fill && !report.whole_atlas && report.drawable_frame == std::uint64_t(resident) &&
            report.ready_frame == std::uint64_t(ready));
    require("legacy_far_goes_up_in_several_budgeted_latches", far_latches > 1 && max_bytes <= kDefaultUploadBudget && report.latches > 1);
}

// Cold fill: the far need box alone, then exactly one latch past the budget holding the whole far atlas.
void cold_fill() {
    DensityCache cache; Gpu gpu; std::uint64_t f = 0;
    cache.start_stepped();
    cache.set_handover(true, true);
    cache.configure(kIdentity);
    // Frame 1: the camera is posted, nothing to upload, the worker fills the far first-fill box.
    Frame fr = frame(cache, gpu, kCamera, ++f);
    const std::uint64_t far_first = intersect(window_box(window_origin(kFarDelta, kCamera[0], kCamera[1], kCamera[2])), grow(need_box(1, kCamera), kFirstFillSlack[1])).nodes();
    const std::uint64_t after_far = cache.stats().nodes_generated;
    const bool held = !cache.worker_step() && cache.stats().nodes_generated == after_far;
    std::printf("COLD_FILL far_first_nodes=%llu generated=%llu held=%u\n", (unsigned long long)far_first, (unsigned long long)after_far, unsigned(held));
    require("cold_fill_generates_only_the_far_need_box_first", after_far == far_first && fr.latch.rects[0] + fr.latch.rects[1] == 0);
    require("cold_fill_holds_the_worker_until_the_latch", held && !cache.idle());
    // Frame 2: one latch, one rectangle, the whole far atlas, nothing of the fine level.
    fr = frame(cache, gpu, kCamera, ++f);
    std::printf("COLD_FILL_LATCH far_rects=%u far_bytes=%zu whole=%u fine_rects=%u budget=%zu\n", fr.latch.rects[1], fr.latch.bytes[1], unsigned(fr.latch.whole[1]), fr.latch.rects[0], kDefaultUploadBudget);
    require("cold_fill_whole_atlas_in_one_latch_past_the_budget", fr.latch.rects[1] == 1 && fr.latch.whole[1] && fr.latch.bytes[1] == kAtlasBytes && kAtlasBytes > kDefaultUploadBudget);
    require("cold_fill_fine_waits_for_the_next_latch", fr.latch.rects[0] == 0);
    require("cold_fill_whole_atlas_equals_the_cpu_cache", std::memcmp(gpu.atlas[1].data(), cache.cache_bytes(1), kAtlasBytes) == 0);
    require("cold_fill_releases_the_worker", cache.stats().nodes_generated > after_far);
    // Frame 3: resident, stepped to 1; the report counts one latch.
    fr = frame(cache, gpu, kCamera, ++f);
    std::printf("COLD_FILL_REPORT ready=%.3f frames=%llu latches=%u upload_bytes=%llu whole=%u fill_us=%lld fill_busy_us=%lld fill_cpu_us=%lld drawable_us=%lld busy_us=%lld\n",
                double(fr.state.ready[1]), (unsigned long long)(fr.report.ready_frame - fr.report.arm_frame), fr.report.latches, (unsigned long long)fr.report.upload_bytes,
                unsigned(fr.report.whole_atlas), (long long)fr.report.fill_us, (long long)fr.report.fill_busy_us, (long long)fr.report.fill_cpu_us, (long long)fr.report.drawable_us, (long long)fr.report.busy_us);
    require("cold_fill_resident_and_stepped_in_frame_3", fr.state.resident[1] && fr.state.ready[1] == 1.f && fr.report.due && fr.report.ready_frame == 3 && fr.report.drawable_frame == 3);
    require("cold_fill_report_one_latch_whole_atlas", fr.report.cold_fill && fr.report.whole_atlas && fr.report.latches == 1 && fr.report.upload_bytes == kAtlasBytes &&
            fr.report.fill_us >= 0 && fr.report.fill_busy_us >= 0);
    // The rest goes up under the budget: no second latch past it, and the field ends identical to a from-scratch fill.
    std::size_t max_bytes = 0;
    require("cold_fill_settles_to_the_static_field", settle(cache, gpu, kCamera, f, kDefaultUploadBudget, &max_bytes) && equal_static(gpu, cache, "cold_fill"));
    require("cold_fill_later_latches_within_the_budget", max_bytes <= kDefaultUploadBudget);
    // A warm jump in the same identity keeps the budget (no whole-atlas latch) and the ramp.
    const double jumped[3] = {kCamera[0], kCamera[1] - 250 * kFarDelta, kCamera[2]};
    bool whole = false, over = false; long long resident = -1, ready = -1;
    for (int i = 0; i < 4000 && ready < 0; ++i) {
        fr = frame(cache, gpu, jumped, ++f);
        whole = whole || fr.latch.whole[1]; over = over || fr.latch.bytes[0] + fr.latch.bytes[1] > kDefaultUploadBudget;
        if (fr.state.resident[1] && resident < 0) resident = (long long)f;
        if (fr.state.ready[1] >= 1.f) ready = (long long)f;
    }
    require("cold_fill_warm_jump_stays_budgeted_and_ramped", !whole && !over && ramp_length(resident, ready));
    // Switched off during a cold fill: the hold is released at the next latch, the budgeted path takes over.
    cache.set_handover(true, true);
    cache.invalidate();
    frame(cache, gpu, jumped, ++f);
    const bool held_again = !cache.worker_step();
    cache.set_handover(true, false);
    fr = frame(cache, gpu, jumped, ++f);
    require("cold_fill_switched_off_releases_the_hold", held_again && !fr.latch.whole[1] && fr.latch.bytes[0] + fr.latch.bytes[1] <= kDefaultUploadBudget && cache.worker_step());
    require("cold_fill_switched_off_settles", settle(cache, gpu, jumped, f, kDefaultUploadBudget) && equal_static(gpu, cache, "cold_fill_off"));
    // Device loss during a cold fill (gpu_reset before the latch): the whole atlas still covers the re-upload.
    cache.set_handover(true, true);
    cache.invalidate();
    frame(cache, gpu, jumped, ++f);
    cache.gpu_reset();
    fr = frame(cache, gpu, jumped, ++f);
    const Frame next = frame(cache, gpu, jumped, ++f);
    require("cold_fill_after_gpu_reset_one_whole_latch_then_resident", fr.latch.whole[1] && fr.latch.rects[1] == 1 && next.state.resident[1] && next.state.ready[1] == 1.f);
}

// Docked view: the bounded parent walk (sector_background::sample with anchor_walk_limit).
void parent_walk() {
    namespace sb = x3m::sector_background;
    // setup(): cockpit 0x5000, its sector 0x7000 (class 1). The ref object 0x8000 hangs below a chain of parents.
    auto chain = [](unsigned depth, std::uint32_t top = 0x7000) {
        Memory m = setup(); m.word(0x500c, 0x8000);
        std::uint32_t object = 0x8000;
        for (unsigned i = 0; i < depth; ++i) {
            const std::uint32_t parent = 0x9000 + 0x100 * i;
            m.word(object + 0x54, parent); m.word(parent + 0x48, 2 + i); object = parent;
        }
        m.word(object + 0x54, top);
        return m;
    };
    {   Memory m = chain(0); const auto s = sb::sample(m, sb::anchor_walk_limit);
        require("walk_depth0_direct_match_without_walk", s.status == sb::Status::Ready && s.anchor_check == sb::Check::Match && s.anchor_walk == sb::AnchorWalk::None && s.anchor_depth == 0); }
    for (unsigned depth = 1; depth <= 3; ++depth) {
        Memory m = chain(depth); const auto s = sb::sample(m, sb::anchor_walk_limit);
        Memory legacy = chain(depth); const auto off = sb::sample(legacy);
        char name[64]; std::snprintf(name, sizeof name, "walk_depth%u_found_ready_same_identity", depth);
        require(name, s.status == sb::Status::Ready && s.anchor_check == sb::Check::Mismatch && !sb::anchor_refused(s) && s.anchor_walk == sb::AnchorWalk::Found && s.anchor_depth == depth &&
                s.anchor_last == 0x7000 && s.sector == 0x7000 && s.index == off.index && s.record == off.record && std::string(s.family) == off.family);
        std::snprintf(name, sizeof name, "walk_depth%u_off_is_the_legacy_mismatch", depth);
        require(name, off.status == sb::Status::AnchorMismatch && off.anchor_walk == sb::AnchorWalk::None);
        std::printf("WALK depth=%u reads_walk=%zu reads_legacy=%zu\n", depth, m.requested.size(), legacy.requested.size());
        std::snprintf(name, sizeof name, "walk_depth%u_costs_two_reads_per_hop", depth);
        require(name, m.requested.size() == legacy.requested.size() + 2 * depth + 1);
    }
    {   Memory m = chain(4); const auto s = sb::sample(m, sb::anchor_walk_limit);
        require("walk_depth4_exceeds_the_bound_and_refuses", s.status == sb::Status::AnchorMismatch && s.anchor_walk == sb::AnchorWalk::Bound && s.anchor_depth == 3); }
    {   Memory m = chain(1, 0xc000); m.word(0xc048, 1); const auto s = sb::sample(m, sb::anchor_walk_limit);
        require("walk_other_sector_refuses", s.status == sb::Status::AnchorMismatch && s.anchor_walk == sb::AnchorWalk::OtherSector && s.anchor_depth == 1 && s.anchor_last == 0xc000); }
    {   Memory m = chain(1, 0); const auto s = sb::sample(m, sb::anchor_walk_limit);
        require("walk_null_parent_refuses", s.status == sb::Status::AnchorMismatch && s.anchor_walk == sb::AnchorWalk::ReadFailure); }
    {   Memory m = chain(1, 0x7002); const auto s = sb::sample(m, sb::anchor_walk_limit);
        require("walk_misaligned_parent_refuses_without_reading_it", s.status == sb::Status::AnchorMismatch && s.anchor_walk == sb::AnchorWalk::ReadFailure &&
                std::none_of(m.requested.begin(), m.requested.end(), [](const auto& r) { return r.first == 0x7002 + 0x48; })); }
    {   Memory m = chain(1, 0xd000); const auto s = sb::sample(m, sb::anchor_walk_limit); // 0xd000 unmapped: its class word cannot be read
        require("walk_unreadable_object_refuses", s.status == sb::Status::AnchorMismatch && s.anchor_walk == sb::AnchorWalk::ReadFailure && s.anchor_depth == 1); }
    {   Memory m = setup(); m.word(0x500c, 0x8000); m.word(0x8054, 0x9000); m.word(0x9048, 2); m.word(0x9054, 0x9000); // a parent cycle
        const auto s = sb::sample(m, sb::anchor_walk_limit);
        const auto walked = std::count_if(m.requested.begin(), m.requested.end(), [](const auto& r) { return r.first >= 0x9000 && r.first < 0x9100; });
        require("walk_cycle_is_bounded", s.anchor_walk == sb::AnchorWalk::Bound && walked == 7); }
    {   Memory m = chain(2); const auto s = sb::sample(m, 1); // a smaller limit is honoured
        require("walk_limit_argument_is_honoured", s.anchor_walk == sb::AnchorWalk::Bound && s.status == sb::Status::AnchorMismatch); }
    {   Memory walk = chain(0), legacy = chain(0); sb::sample(walk, sb::anchor_walk_limit); sb::sample(legacy);
        require("walk_direct_match_reads_exactly_the_legacy_reads", walk.requested == legacy.requested); }
    sb::AnchorSpan span; Memory docked = chain(1), deeper = chain(2), undocked = chain(0);
    const bool first = span.update(sb::sample(docked, 3)), again = span.update(sb::sample(docked, 3)), deeper_change = span.update(sb::sample(deeper, 3));
    const bool end = span.update(sb::sample(undocked, 3)), restart = span.update(sb::sample(docked, 3));
    require("walk_span_reports_once_per_span_and_outcome", first && !again && deeper_change && !end && restart);
}

// Review cases for the cold fill: a new identity during the hold, a lost whole latch, a camera that leaves the
// published box before the latch, and the real worker thread's hold and wake.
void cold_fill_edges() {
    {   // A new identity (invalidate) during the hold, before the whole-atlas latch: only the new field goes up, once.
        DensityCache cache; Gpu gpu; std::uint64_t f = 0;
        cache.start_stepped(); cache.set_handover(true, true); cache.configure(kIdentity);
        frame(cache, gpu, kCamera, ++f);                                   // far first fill of the old identity, held
        const bool held = !cache.worker_step();
        const CacheIdentity other{0xbeef, 1, WorldOffset{7 * kFarDelta, -3 * kFarDelta, 11 * kFarDelta}};
        cache.configure(other);                                            // new epoch while held
        unsigned wholes = 0, far_rects_before = 0; long long resident = -1, ready = -1;
        for (int i = 0; i < 200 && ready < 0; ++i) {
            const Frame fr = frame(cache, gpu, kCamera, ++f);
            if (!wholes && !fr.latch.whole[1]) far_rects_before += fr.latch.rects[1];
            wholes += fr.latch.whole[1];
            if (fr.state.resident[1] && resident < 0) resident = (long long)f;
            if (fr.state.ready[1] >= 1.f) ready = (long long)f;
        }
        require("hold_invalidate_one_whole_latch_of_the_new_identity", held && wholes == 1 && far_rects_before == 0 && ready == resident && ready > 0);
        require("hold_invalidate_settles_to_the_new_field", settle(cache, gpu, kCamera, f, kDefaultUploadBudget) && equal_static(gpu, cache, "hold_invalidate", other.offset));
    }
    {   // The whole latch is lost (confirm_uploads(false)): it is taken again, and the report counts both latches.
        DensityCache cache; Gpu gpu; std::uint64_t f = 0;
        cache.start_stepped(); cache.set_handover(true, true); cache.configure(kIdentity);
        frame(cache, gpu, kCamera, ++f);
        gpu.fail_next = true;
        const Frame lost = frame(cache, gpu, kCamera, ++f);
        unsigned wholes = lost.latch.whole[1]; HandoverReport report; long long ready = -1;
        for (int i = 0; i < 200 && ready < 0; ++i) {
            const Frame fr = frame(cache, gpu, kCamera, ++f);
            wholes += fr.latch.whole[1];
            if (fr.report.due) report = fr.report;
            if (fr.state.ready[1] >= 1.f) ready = (long long)f;
        }
        std::printf("LOST_WHOLE wholes=%u latches=%u whole=%u\n", wholes, report.latches, unsigned(report.whole_atlas));
        require("lost_whole_latch_is_taken_again", lost.latch.whole[1] && wholes == 2 && report.due && report.whole_atlas && report.latches >= 2 && report.upload_bytes >= 2 * kAtlasBytes);
    }
    {   // The camera moves 20 far nodes before the latch: no whole latch until a box holding its need box is published.
        DensityCache cache; Gpu gpu; std::uint64_t f = 0;
        cache.start_stepped(); cache.set_handover(true, true); cache.configure(kIdentity);
        frame(cache, gpu, kCamera, ++f, 1);
        const double moved[3] = {kCamera[0] + 20 * kFarDelta, kCamera[1], kCamera[2]};
        const Frame waiting = frame(cache, gpu, moved, ++f, 0);            // published box misses the new need box
        const Frame after = frame(cache, gpu, moved, ++f, 1);              // still waiting; the held worker extends the far box
        unsigned wholes = waiting.latch.whole[1] + after.latch.whole[1]; long long resident = -1, ready = -1;
        for (int i = 0; i < 200 && ready < 0; ++i) {
            const Frame fr = frame(cache, gpu, moved, ++f);
            wholes += fr.latch.whole[1];
            if (fr.state.resident[1] && resident < 0) resident = (long long)f;
            if (fr.state.ready[1] >= 1.f) ready = (long long)f;
        }
        require("whole_latch_waits_for_the_need_box", !waiting.latch.whole[1] && waiting.latch.rects[1] == 0 && wholes == 1 && ready == resident && ready > 0);
    }
    {   // The real worker thread: parked on the condition variable during the hold, woken by the latch; the far staging
        // is offered only from the publication on (at most the latch frame and the frames before it once published).
        DensityCache cache; Gpu gpu; std::uint64_t f = 0;
        require("threaded_worker_starts", cache.start());
        cache.set_handover(true, true); cache.configure(kIdentity);
        unsigned wholes = 0, far_views_before = 0, far_rects_before = 0; long long resident = -1, ready = -1; bool idle = false;
        const auto begin = std::chrono::steady_clock::now();
        auto seconds = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count(); };
        while (seconds() < 120 && !idle) {
            const FrameState s = cache.step(kCamera, ++f);
            const Latch l = gpu.frame(cache, kDefaultUploadBudget);
            if (!wholes && !l.whole[1]) { far_views_before += l.far_view; far_rects_before += l.rects[1]; }
            wholes += l.whole[1];
            if (s.resident[1] && resident < 0) resident = (long long)f;
            if (s.ready[1] >= 1.f && ready < 0) ready = (long long)f;
            idle = ready > 0 && s.ready[0] >= 1.f && cache.idle();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        std::printf("THREADED_COLD_FILL frames=%llu wholes=%u far_views_before_latch=%u resident=%lld ready=%lld idle=%u seconds=%.2f\n",
                    (unsigned long long)f, wholes, far_views_before, resident, ready, unsigned(idle), seconds());
        require("threaded_hold_then_one_whole_latch", wholes == 1 && far_rects_before == 0 && ready == resident && ready > 0);
        require("threaded_far_staging_not_offered_while_unpublished", far_views_before == 0);
        require("threaded_worker_woken_after_the_latch_settles", idle && equal_static(gpu, cache, "threaded_cold_fill"));
        cache.stop();
    }
}

// R3: the prefill posts the destination at the sector origin, holds after its far need box, and the first Ready
// frame's configure of the same identity keeps it; another identity starts over.
void prefill_cache() {
    DensityCache cache; Gpu gpu; std::uint64_t f = 0;
    cache.start_stepped(); cache.set_handover(true, true);
    const CacheIdentity destination{0x9999, 1, WorldOffset{3 * kFarDelta, 0, -5 * kFarDelta}};
    const double origin[3] = {0, 0, 0};
    DensityCache idle_cache;                             // not started: nothing posted, the poll retries
    const bool refused = !idle_cache.prefill(destination, origin);
    const bool posted = cache.prefill(destination, origin);
    const bool filled = cache.worker_step();
    const std::uint64_t nodes = cache.stats().nodes_generated;
    const bool held = !cache.worker_step();
    cache.configure(destination);                        // the first Ready frame: the same identity is a no-op
    const bool kept = !cache.worker_step() && cache.stats().nodes_generated == nodes;
    require("prefill_posts_only_on_a_running_cache", refused && posted);
    require("prefill_fills_the_far_need_box_at_the_origin_and_holds", filled && nodes == intersect(window_box(window_origin(kFarDelta, 0, 0, 0)), grow(need_box(1, origin), kFirstFillSlack[1])).nodes() && held);
    require("prefill_confirmed_configure_keeps_the_fill", kept);
    const double arrival[3] = {20000.0, -10000.0, 5000.0}; // the arrival position, 20 km from the origin
    unsigned wholes = 0; long long ready = -1, resident = -1; HandoverReport report;
    for (int i = 0; i < 200 && ready < 0; ++i) {
        const Frame fr = frame(cache, gpu, arrival, ++f);
        wholes += fr.latch.whole[1];
        if (fr.state.resident[1] && resident < 0) resident = (long long)f;
        if (fr.state.ready[1] >= 1.f) ready = (long long)f;
        if (fr.report.due) report = fr.report;
    }
    std::printf("PREFILL ready_frame=%lld wholes=%u arm_frame=%llu latches=%u\n", ready, wholes, (unsigned long long)report.arm_frame, report.latches);
    // The arrival (20 km off) needs one extension slab per axis under the hold (one slab per frame here), then the latch.
    require("prefill_hands_over_after_three_extension_slabs_and_the_latch", wholes == 1 && ready == resident && ready > 0 && ready <= 5 && report.due && report.whole_atlas && report.latches == 1);
    require("prefill_settles_to_the_destination_field", settle(cache, gpu, arrival, f, kDefaultUploadBudget) && equal_static(gpu, cache, "prefill", destination.offset));
    const CacheIdentity other{0x7777, 1, kNoOffset};
    cache.configure(other);                              // a different key at Ready: discarded, a new epoch
    require("prefill_discard_starts_a_new_fill", cache.worker_step() && cache.identity() == other);
}

// R3 read side on synthetic global-list layouts (sector-transit-order.md §1, §5).
struct List {
    Memory m = setup();
    static constexpr std::uint32_t manager = 0x40000;
    std::vector<std::uint32_t> nodes; // [0] is the tail
    List() { m.word(0x60850c, manager); m.word(manager + 8, 0); m.word(manager + 0xc, 0); m.word(manager + 0x10, manager + 8); }
    std::uint32_t add(std::uint32_t type, std::uint32_t id = 900, std::uint32_t marker = 0xcafe, std::uint32_t scene = 0x7777000, std::int32_t index = 0) {
        const std::uint32_t node = 0x50000 + 0x400 * std::uint32_t(nodes.size());
        m.block(node, 0x180);
        m.word(node + 8, id); m.word(node + 0x48, type); m.word(node + 0x9c, marker);
        m.word(node + 0x130, scene); m.word(node + 0x13c, std::uint32_t(index));
        nodes.push_back(node); link(); return node;
    }
    void link() {
        m.word(manager + 0x10, nodes.empty() ? manager + 8 : nodes[0]);
        for (std::size_t i = 0; i < nodes.size(); ++i) m.word(nodes[i] + 4, i + 1 < nodes.size() ? nodes[i + 1] : manager + 8);
    }
};
constexpr std::uint32_t kSector = 1, kShip = 7, kCutScene = 0x00010001;
void prefill_walk() {
    namespace fp = x3m::fog_prefill;
    unsigned worst = 0;
    auto run = [&](List& l, std::uint32_t last = 0) { const auto r = fp::walk(l.m, last); worst = std::max(worst, r.reads); return r; };
    {   List l; l.add(kSector, 555); const auto r = run(l);
        std::printf("PREFILL_WALK case=tail status=%s steps=%u reads=%u family=%s\n", fp::name(r.status), r.steps, r.reads, r.sample.family);
        require("walk_accepts_the_sector_at_the_tail", r.status == fp::Walk::Found && r.steps == 1 && r.id == 555 && r.sample.index == 0 &&
                std::string(r.sample.family) == "bluewell" && r.sample.status == x3m::sector_background::Status::Ready && r.sample.sector_id == 555); }
    {   List l; for (int i = 0; i < 7; ++i) l.add(kShip); l.add(kSector, 556); const auto r = run(l);
        std::printf("PREFILL_WALK case=eighth status=%s steps=%u reads=%u\n", fp::name(r.status), r.steps, r.reads);
        require("walk_accepts_the_eighth_node_within_25_reads", r.status == fp::Walk::Found && r.steps == 8 && r.reads <= fp::kReadBudget); }
    {   List l; for (int i = 0; i < 8; ++i) l.add(kShip); l.add(kSector); const auto r = run(l);
        std::printf("PREFILL_WALK case=ninth status=%s steps=%u reads=%u\n", fp::name(r.status), r.steps, r.reads);
        require("walk_is_cut_at_eight_nodes", r.status == fp::Walk::Bound && r.steps == 8); }
    {   List l; l.add(kShip); l.add(kShip); const auto r = run(l);
        require("walk_stops_at_the_list_head", r.status == fp::Walk::Head && r.steps == 2); }
    {   List l; const auto r = run(l); require("walk_empty_list_is_the_head", r.status == fp::Walk::Head && r.steps == 0); }
    {   List l; l.add(kCutScene); l.add(kSector, 557); const auto r = run(l);
        require("walk_skips_a_cut_scene_space_subtype", r.status == fp::Walk::Found && r.steps == 2 && r.id == 557); }
    {   List l; l.add(kSector, 558, 0xefac); require("walk_refuses_a_freed_object", run(l).status == fp::Walk::Dead); }
    {   List l; l.add(kSector, 559, 0xcafe, 0); require("walk_refuses_a_sector_without_scene", run(l).status == fp::Walk::NoScene); }
    {   List l; l.add(kSector, 560); require("walk_refuses_the_last_ready_id", run(l, 560).status == fp::Walk::SameId && run(l, 561).status == fp::Walk::Found); }
    {   List l; l.add(kSector); l.m.word(0x60850c, 0); require("walk_no_manager", run(l).status == fp::Walk::NoManager); }
    {   List l; l.add(kSector); l.m.bytes.erase(0x60850c); require("walk_unreadable_manager", run(l).status == fp::Walk::ReadFailure); }
    {   List l; l.add(kSector); l.m.word(List::manager + 0x10, l.nodes[0] + 2); require("walk_misaligned_node", run(l).status == fp::Walk::Malformed); }
    {   List l; l.add(kShip); l.m.bytes.erase(l.nodes[0] + 4); require("walk_unreadable_link", run(l).status == fp::Walk::ReadFailure); }
    {   List l; l.add(kSector); l.m.word(0x606fc0, 0); require("walk_table_loading", run(l).status == fp::Walk::Loading); }
    {   List l; l.add(kSector, 900, 0xcafe, 0x7777000, 83); require("walk_index_out_of_range", run(l).status == fp::Walk::BadIndex); }
    {   List l; l.add(kSector); l.m.word(0x10134, 99); require("walk_bad_record", run(l).status == fp::Walk::BadRecord); }
    std::printf("PREFILL_WALK worst_reads=%u budget=%u\n", worst, fp::kReadBudget);
    require("walk_never_exceeds_25_reads", worst <= fp::kReadBudget);

    // The stall gate: nothing before the first Present, nothing within 250 ms of it, then one poll per 250 ms.
    fp::Gate g;
    const bool before = g.due(5000);
    g.present(1000);
    const bool at250 = g.due(1250), at251 = g.due(1251), again = g.due(1400), next = g.due(1501);
    g.present(1600);
    const bool after_present = g.due(1800), stalled = g.due(1851);
    fp::Gate h; h.present(0);
    const bool looked = h.stalled(300) && h.stalled(300), slot = h.take(300), spent = !h.take(400);
    require("gate_stalled_does_not_consume_the_slot", looked && slot && spent);
    require("gate_polls_only_in_a_stall_and_once_per_250ms", !before && !at250 && at251 && !again && next && !after_present && stalled);

    // The decision at the first Ready sample.
    fp::Record r; r.key = 42; r.recipe = 1;
    const auto none = fp::decide(r, true, 42, 1);
    r.pending = true;
    require("decide_confirms_the_same_key_and_discards_the_rest", none == fp::Decision::None && fp::decide(r, true, 42, 1) == fp::Decision::Confirmed &&
            fp::decide(r, true, 43, 1) == fp::Decision::Discarded && fp::decide(r, true, 42, 2) == fp::Decision::Discarded && fp::decide(r, false, 42, 1) == fp::Decision::Discarded);
}

// Review round 2, item 1: the cards arm in the cold-step frame itself; the frame's transaction stays the proof.
void card_arming() {
    x3m::FogCardPolicy p; p.begin(true);
    const bool warm = p.warmup && !p.may_replace();
    p.arm_on_cold_step();
    const bool now = p.may_replace() && p.armed;
    ++p.suppressed; p.finish(true);
    require("cold_step_arms_the_cards_in_the_same_frame", warm && now && p.armed && !p.fault);
    x3m::FogCardPolicy q; q.begin(true); q.arm_on_cold_step(); ++q.suppressed; q.finish(false);
    require("cold_step_arming_faults_when_the_frame_fails", q.fault && !q.armed);
    x3m::FogCardPolicy r; r.begin(true); r.reject(); r.arm_on_cold_step();
    x3m::FogCardPolicy s; s.begin(false); s.arm_on_cold_step();
    require("cold_step_arming_respects_refusal_and_inactive", !r.may_replace() && r.warmup && !s.may_replace() && !s.armed);
}
}  // namespace

int main() {
    cold_step_then_warm_ramp();
    cold_ramp_legacy();
    cold_fill();
    parent_walk();
    cold_fill_edges();
    prefill_cache();
    prefill_walk();
    card_arming();
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks_run, failures);
    return failures ? 1 : 0;
}
