"""Host contracts of the sun-shadow cascades and the run-38 fixes
(docs/architecture/shadow-cascades.md; docs/verification/directional-shadows.md,
"Run 38 A (run111) diagnosis"). The pure headers are compiled natively (no
Wine, no D3D) into one driver:

* src/renderer/shader_constant_register.h on constant tables built here: a
  glow-style program (c4 = g_EnableGlow, LightDir_Dir0 at c5), a detail-style
  one (c4 = p_DetailMapBlendWeight, LightDir_Dir0 at c0), a hull one (c4), a
  program without the constant, and malformed tables;
* src/proxy/shadow_replay_sun.h: the sun latches on the second agreeing draw
  (a lone or stray sample only waits), the glow program drawn first with c4 =
  (1, 0, 0, 0) still yields the true sun, one frame of disagreement is refused,
  a persisting one re-latches, a frame without samples reuses the sun;
* src/proxy/shadow_replay_candidates.h: a frozen set of colliding extent keys
  gives identical answers on every frame with no store after the first, an
  entry used this frame is never evicted, a moved revision answers stale for
  extent_stale_frames and inflated after, an abandoned re-read is not queued;
* src/renderer/shadow_replay_projection.h: the default cascade set, the budget
  policy, the bounds mask with its open light side, and the shared-product
  light rows against shadow_replay_light_rows, and the rows' accuracy 70,000
  units from the origin (the snapped centre is folded in as a double);
* caster pool control (docs/architecture/shadow-cascade-extents.md, "Caster
  pool control"): the pool policy on the set (records, the static-only mask,
  the per-cascade bound), the projected size the mask test yields, the
  scene-end importance selection (the same kept set under every submission
  order, the serial tie-break, dropped_min_size, the compaction of records
  left without a cascade) and the static classification ring
  (src/proxy/shadow_caster_class.h: static within eps, moving beyond, a miss
  on the first sighting and after an eviction).
The launcher options are checked through tools/manage.py --dry-run.
"""
import contextlib
import importlib.util
import io
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
DRIVER = r'''
#include "renderer/shader_constant_register.h"
#include "renderer/shadow_replay_projection.h"
#include "proxy/shadow_replay_candidates.h"
#include "proxy/shadow_replay_sun.h"
#include "proxy/shadow_replay_sun_point.h"
#include "proxy/shadow_caster_class.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace x3m;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d %s\n", __LINE__, #x); ++failures; } } while (0)
static renderer::CameraState camera() {
    renderer::CameraState c{}; c.valid = true; c.m00 = .8f; c.m11 = 1.2f;
    const float r[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; std::memcpy(c.r, r, sizeof r);
    c.t[0] = -100.f; c.t[1] = -50.f; c.t[2] = 25.f; // position (100, 50, -25)
    return c;
}
int main(int argc, char** argv) {
    // ---- constant tables: one file per argument, "name expected_register"
    for (int i = 1; i + 1 < argc; i += 2) {
        std::FILE* f = std::fopen(argv[i], "rb"); CHECK(f != nullptr); if (!f) continue;
        std::vector<std::uint32_t> words(65536); const std::size_t n = std::fread(words.data(), 4, words.size(), f); std::fclose(f);
        const int got = renderer::shader_float_constant_register(words.data(), n, "LightDir_Dir0");
        std::printf("REGISTER %s %d\n", argv[i], got);
        CHECK(got == std::atoi(argv[i + 1]));
        // Every truncation is refused or answers the same register, never reads beyond.
        for (std::size_t cut = 0; cut < n; ++cut) { const int part = renderer::shader_float_constant_register(words.data(), cut, "LightDir_Dir0"); CHECK(part == -1 || part == got); }
    }
    // ---- the sun latch
    {
        using shadow_replay::SunLatch; using shadow_replay::SunVerdict;
        const float sun[4] = {.30151134f, .90453403f, -.30151134f, 0}, glow[4] = {1, 0, 0, 0}, weight[4] = {.3f, .2f, .7f, 0}, other[4] = {0, 1, 0, 0}, third[4] = {0, 0, 1, 0};
        // The per-node spread of one sector (0.5 degrees here) agrees and never moves the latched value; 2 degrees does not agree.
        const float near[4] = {.30151134f + .0083f, .90453403f - .0027f, -.30151134f, 0}, off[4] = {.30151134f + .033f, .90453403f - .0115f, -.30151134f, 0};
        const float positional[4] = {.30151134f, .90453403f, -.30151134f, 1}, loose[4] = {.30151134f * 1.01f, .90453403f * 1.01f, -.30151134f * 1.01f, 0};
        SunLatch latch; latch.begin_frame();
        CHECK(latch.frame_sun() == nullptr && latch.end_frame() == SunVerdict::None);
        // Frame 1: the glow program first. Its c4 (1,0,0,0) is never sampled: the
        // register resolved from its table is c5, which holds the sun.
        float registers[32][4]{}; std::memcpy(registers[4], glow, 16); std::memcpy(registers[5], sun, 16);
        latch.begin_frame();
        // A lone sample only waits, and a stray one is replaced, never latched: two draws must agree.
        CHECK(!latch.sample(other, 9, 3) && latch.frame_sun() == nullptr && latch.frame.unlatched == 1);
        CHECK(!latch.sample(registers[5], 5, 0xf1) && latch.frame_sun() == nullptr && latch.frame.unlatched == 2 && latch.end_frame() == SunVerdict::None);
        latch.begin_frame();
        std::memcpy(registers[0], sun, 16);
        CHECK(latch.sample(registers[0], 0, 0xd0) && latch.frame_sun() && latch.frame_sun()[1] == sun[1] && latch.source_register == 5 && latch.source_program == 0xf1); // the first of the two agreeing draws
        CHECK(latch.sample(registers[5], 5) && latch.frame.agree == 2 && latch.frame.unlatched == 0);
        std::memcpy(registers[0], sun, 16); std::memcpy(registers[4], weight, 16); // the detail program: c0, with a non-unit c4
        CHECK(latch.sample(registers[0], 0));
        std::memcpy(registers[4], sun, 16);                                         // a hull program: c4
        CHECK(latch.sample(registers[4], 4));
        latch.no_register();
        CHECK(latch.sample(near, 4, 7) && latch.frame_sun()[0] == sun[0] && latch.frame_sun()[1] == sun[1]); // agrees, and the latched value does not follow it
        CHECK(!latch.sample(positional, 4) && !latch.sample(loose, 4) && latch.frame.invalid == 2);           // w = 1, or 1 % off unit length: not a direction sample
        CHECK(latch.frame.samples == 7 && latch.frame.agree == 5 && latch.frame.no_register == 1 && latch.end_frame() == SunVerdict::Sampled);
        latch.begin_frame(); CHECK(!latch.sample(off, 4) && latch.frame.disagree == 1 && latch.end_frame() == SunVerdict::Changing);
        latch.begin_frame(); CHECK(latch.sample(sun, 4) && latch.end_frame() == SunVerdict::Sampled);
        // What the c4 rule would have taken is rejected as a sample, too: not unit / disagreeing.
        latch.begin_frame(); CHECK(!latch.sample(weight, 4) && latch.frame.invalid == 1 && latch.end_frame() == SunVerdict::Retained && latch.frame_sun()[1] == sun[1]);
        // No sample at all: the world-fixed sun is reused.
        latch.begin_frame(); CHECK(latch.end_frame() == SunVerdict::Retained);
        // One frame of pure disagreement is refused and changes nothing.
        latch.begin_frame(); CHECK(!latch.sample(glow, 4) && latch.end_frame() == SunVerdict::Changing && latch.frame_sun()[1] == sun[1]);
        latch.begin_frame(); CHECK(latch.sample(sun, 4) && latch.end_frame() == SunVerdict::Sampled && latch.candidate_frames == 0);
        // A mixed frame keeps the validated sun.
        latch.begin_frame(); CHECK(!latch.sample(glow, 4) && latch.sample(sun, 5) && latch.end_frame() == SunVerdict::Sampled);
        // A persisting new sun (a sector transit) re-latches on the eighth consecutive frame; a candidate that moves restarts the count.
        latch.begin_frame(); CHECK(!latch.sample(other, 4) && latch.end_frame() == SunVerdict::Changing);
        for (unsigned k = 1; k < shadow_replay::sun_relatch_frames; ++k) { latch.begin_frame(); CHECK(!latch.sample(third, 4, 11) && latch.end_frame() == SunVerdict::Changing && latch.frame_sun()[1] == sun[1]); }
        latch.begin_frame(); CHECK(!latch.sample(third, 0, 12) && latch.end_frame() == SunVerdict::Relatched && latch.frame_sun()[2] == 1.f && latch.source_register == 0 && latch.source_program == 12);
        latch.begin_frame(); CHECK(latch.sample(third, 0) && latch.end_frame() == SunVerdict::Sampled);
        CHECK(shadow_replay::sun_relatch_frames == 8);
        CHECK(!shadow_replay::sun_verdict_usable(SunVerdict::Changing) && !shadow_replay::sun_verdict_usable(SunVerdict::Relatched) && !shadow_replay::sun_verdict_usable(SunVerdict::None)
              && shadow_replay::sun_verdict_usable(SunVerdict::Retained));
        latch.reset(); CHECK(latch.frame_sun() == nullptr);
    }
    // ---- the extent cache: a frozen set with collisions
    {
        using namespace shadow_replay;
        static ExtentCache cache; cache.clear();
        std::vector<ExtentKey> keys;
        // 1,500 ranges; many share a set (1,024 sets of eight).
        for (unsigned i = 0; i < 1500; ++i) { ExtentKey k{}; k.vb = 700 + i % 300; k.revision = 3; k.stride = 24; k.first = (i / 300) * 120; k.count = 120; k.position_type = 16; keys.push_back(k); }
        unsigned long long signature[6]{}; unsigned stores[6]{}, misses[6]{};
        for (unsigned frame = 0; frame < 6; ++frame) {
            cache.begin_frame();
            std::vector<ExtentKey> queue;
            for (const auto& k : keys) {
                const ExtentEntry* e = cache.find(k);
                if (!e) { ++misses[frame]; queue.push_back(k); continue; }
                signature[frame] = signature[frame] * 1099511628211ull + (unsigned long long)(e->lo[0] * 4) + k.vb;
            }
            for (const auto& k : queue) { const float lo[3] = {float(k.vb), 0, 0}, hi[3] = {float(k.vb) + 1, 1, 1}; stores[frame] += cache.store(k, lo, hi, ExtentState::Known); }
        }
        std::printf("EXTENTS misses=%u,%u,%u stores=%u,%u,%u refused=%u\n", misses[0], misses[1], misses[2], stores[0], stores[1], stores[2], cache.refused);
        CHECK(misses[0] == 1500 && stores[0] + cache.refused == 1500);
        // Whatever did not fit on frame 0 (a set of more than four) is stored on a later frame or stays refused;
        // from the frame the set is stable on, every frame answers identically and nothing is read again.
        CHECK(misses[4] == misses[5] && stores[4] == 0 && stores[5] == 0 && signature[4] == signature[5] && signature[3] == signature[4]);
        CHECK(misses[5] <= 8); // at most a few ranges beyond four-way sets, never the thrash of a direct-mapped table (24 of 251 on the run)
        // Used this frame: never evicted. A full set, all found, then one more key of the same set is refused.
        cache.clear(); cache.begin_frame();
        constexpr unsigned W = ExtentCache::ways;
        std::vector<ExtentKey> same;
        for (unsigned vb = 1; same.size() < W + 1 && vb < 2000000; ++vb) { ExtentKey k{}; k.vb = vb; k.stride = 24; k.count = 3; k.position_type = 2; if (k.hash() % ExtentCache::sets == 7) same.push_back(k); }
        CHECK(same.size() == W + 1);
        const float lo[3] = {0, 0, 0}, hi[3] = {1, 1, 1};
        for (unsigned i = 0; i < W; ++i) CHECK(cache.store(same[i], lo, hi, ExtentState::Known));
        cache.begin_frame();
        for (unsigned i = 0; i < W; ++i) CHECK(cache.find(same[i]) != nullptr);
        CHECK(!cache.store(same[W], lo, hi, ExtentState::Known) && cache.refused == 1);
        for (unsigned i = 0; i < W; ++i) CHECK(cache.find(same[i]) != nullptr);
        // Next frame all but one are used: the unused one is the victim, the used ones stay.
        cache.begin_frame();
        for (unsigned i = 1; i < W; ++i) CHECK(cache.find(same[i]) != nullptr);
        CHECK(cache.store(same[W], lo, hi, ExtentState::Known) && cache.find(same[W]) && !cache.find(same[0]));
        for (unsigned i = 1; i < W; ++i) CHECK(cache.find(same[i]) != nullptr);
        // A moved revision: a miss with the previous extent as the stale answer, replaced by the new read.
        ExtentKey moved = same[1]; moved.revision = 9;
        const ExtentEntry* stale = nullptr;
        CHECK(cache.find(moved, &stale) == nullptr && stale != nullptr && stale->hi[0] == 1.f);
        cache.retry(moved);
        CHECK(cache.find(moved, &stale) == nullptr && stale != nullptr); // a failed read keeps the stale extent
        const float hi2[3] = {2, 2, 2};
        CHECK(cache.store(moved, lo, hi2, ExtentState::Known) && cache.find(moved) && cache.find(moved)->hi[0] == 2.f && cache.find(same[1]) == nullptr);
        // The stale answer is bounded: fresh for extent_stale_frames, inflated after; failed re-reads are counted on the
        // entry and abandoned at extent_read_attempts (no longer queued); a further revision restarts both; the read heals it.
        {
            ExtentKey next = moved; next.revision = 10;
            CHECK(cache.find(next, &stale) == nullptr && stale && !stale->abandoned() && cache.stale_age(*stale) == 0);
            for (unsigned k = 0; k < extent_stale_frames; ++k) { cache.begin_frame(); CHECK(cache.find(next, &stale) == nullptr && stale && cache.stale_age(*stale) == k + 1); }
            CHECK(cache.stale_age(*stale) <= extent_stale_frames);
            cache.begin_frame(); CHECK(cache.find(next, &stale) == nullptr && stale && cache.stale_age(*stale) > extent_stale_frames);
            float lo2[3], hi2x[3]; stale->inflated(lo2, hi2x);
            CHECK(lo2[0] == -1.f && hi2x[0] == 3.f && lo2[2] == -1.f && hi2x[2] == 3.f); // (0, 2) doubled about its centre
            for (unsigned k = 0; k < extent_read_attempts; ++k) { CHECK(!stale->abandoned()); cache.retry(next); }
            CHECK(cache.find(next, &stale) == nullptr && stale && stale->abandoned() && stale->hi[0] == 2.f);
            ExtentKey later = next; later.revision = 11;
            CHECK(cache.find(later, &stale) == nullptr && stale && !stale->abandoned() && cache.stale_age(*stale) == 0);
            const float hi3[3] = {3, 3, 3};
            CHECK(cache.store(later, lo, hi3, ExtentState::Known) && cache.find(later) && cache.find(later)->hi[0] == 3.f && cache.find(later)->stale_since == 0);
            const unsigned before = cache.refused_frame; cache.begin_frame(); CHECK(cache.refused_frame == 0 && cache.refused >= before);
        }
        // Retry to unreadable on a fresh range.
        ExtentKey fresh{}; fresh.vb = 999999; fresh.stride = 12; fresh.count = 3; fresh.position_type = 2;
        for (unsigned i = 0; i + 1 < extent_read_attempts; ++i) { cache.retry(fresh); CHECK(cache.find(fresh) == nullptr); }
        cache.retry(fresh);
        CHECK(cache.find(fresh) && cache.find(fresh)->state == ExtentState::Unreadable);
        // The per-cascade caps of the frame counter.
        static Frame frame; frame.reset();
        const unsigned caps[4] = {1, 2, 8, 8};
        std::uint8_t mask = 7;
        CHECK(frame.draw(true, true, true, false, false, true, PoolClass::Managed, PoolClass::Managed, false, record_capacity, &mask, caps) && mask == 7);
        frame.record(mask); frame.count_cascades(mask);
        mask = 7;
        CHECK(frame.draw(true, true, true, false, false, true, PoolClass::Managed, PoolClass::Managed, false, record_capacity, &mask, caps) && mask == 6 && frame.counts.cascade_capped[0] == 1);
        frame.record(mask); frame.count_cascades(mask);
        mask = 3;
        CHECK(!frame.draw(true, true, true, false, false, true, PoolClass::Managed, PoolClass::Managed, false, record_capacity, &mask, caps) && mask == 0 && frame.counts.capped == 1
              && frame.counts.cascade_capped[0] == 2 && frame.counts.cascade_capped[1] == 1 && frame.counts.cascade[0] == 1 && frame.counts.cascade[1] == 2 && frame.counts.cascade[2] == 2);
        CHECK(frame.records[1].cascades == 6 && frame.record_count == 2);
    }
    // ---- caster pool control: the policy on the set, the projected size, the importance selection, the classification ring
    {
        using namespace renderer;
        ShadowCascadeSet set{};
        CHECK(shadow_cascade_set(shadow_cascade_extent_defaults, 4, nullptr, nullptr, 640, set));
        CHECK(set.records[3] == 1024 && set.static_from == shadow_cascade_static_from_none && !set.importance && set.record_capacity() == 1024 && set.static_only_mask() == 0 && set.bound(3) == 1024);
        const unsigned records[4] = {1024, 1024, 2048, 4096}, caps[4] = {128, 512, 4096, 4096}, too_many[4] = {1, 1, 1, 4097}, zero[4] = {0, 1, 1, 1};
        CHECK(shadow_cascade_set(shadow_cascade_extent_defaults, 4, nullptr, caps, 640, set) && set.caps[2] == 4096);
        CHECK(!shadow_cascade_pool(set, too_many, 3, true) && !shadow_cascade_pool(set, zero, 3, true) && !shadow_cascade_pool(set, records, 5, true) && set.records[3] == 1024 && !set.importance);
        CHECK(shadow_cascade_pool(set, records, 2, true) && set.records[3] == 4096 && set.record_capacity() == 4096 && set.static_from == 2 && set.static_only_mask() == 12 && set.importance
              && set.bound(2) == 2048 && set.bound(3) == 4096 && set.bound(0) == 128 && set.static_only(3) && !set.static_only(1));
        CHECK(shadow_cascade_pool(set, nullptr, 4, false) && set.static_from == shadow_cascade_static_from_none && set.records[3] == 4096);
        ShadowCascadeSet none{}; CHECK(!shadow_cascade_pool(none, records, 1, true));
        // The projected size: the box diagonal over the distance; a nearer or larger box is larger, an unknown mask yields 0.
        const CameraState c = camera();
        const float sun[4] = {0, 1, 0, 0};
        ShadowCascadeBounds bounds{}; CHECK(shadow_cascade_set(shadow_cascade_extent_defaults, 4, nullptr, nullptr, 640, set) && shadow_cascade_bounds(c, sun, set, bounds));
        float clip[16] = {c.m00, 0, 0, 0, 0, c.m11, 0, 0, 0, 0, 1, 0, 0, 0, 1, 50};
        const float lo[3] = {-1, -1, -1}, hi[3] = {1, 1, 1}, hi2[3] = {2, 2, 2};
        float near_size = 0, far_size = 0, big_size = 0, none_size = 1;
        CHECK(shadow_cascade_bounds_mask(c, clip, bounds, lo, hi, &near_size) >= 0 && near_size > 0.f);
        clip[15] = 500; CHECK(shadow_cascade_bounds_mask(c, clip, bounds, lo, hi, &far_size) >= 0 && far_size > 0.f && far_size < near_size);
        CHECK(shadow_cascade_bounds_mask(c, clip, bounds, lo, hi2, &big_size) >= 0 && big_size > far_size && big_size < near_size);
        CHECK(shadow_cascade_bounds_mask(c, nullptr, bounds, lo, hi, &none_size) == -1 && none_size == 0.f);
        CHECK(shadow_cascade_bounds_mask(c, clip, bounds, lo, hi) == shadow_cascade_bounds_mask(c, clip, bounds, lo, hi, &far_size)); // the size is a pure output
        // The importance selection: eight casters of distinct sizes into a cascade capped at 4 under every rotation of the
        // submission order keep the four largest; ties fall to the lower serial; the dropped records compact away.
        static shadow_replay::Frame frame;
        std::uint16_t scratch[shadow_replay::record_capacity];
        const float sizes[8] = {.13f, .2f, .3f, .44f, .67f, 1.f, 1.5f, .05f};
        const unsigned select_caps[4] = {16, 4, 16, 16};
        for (unsigned rotation = 0; rotation < 8; ++rotation) {
            frame.reset();
            for (unsigned k = 0; k < 8; ++k) {
                const unsigned i = (k + rotation) % 8;
                std::uint8_t mask = 3;
                CHECK(frame.draw(true, true, true, false, false, true, shadow_replay::PoolClass::Managed, shadow_replay::PoolClass::Managed, false, shadow_replay::record_capacity, &mask, nullptr) && mask == 3);
                auto& r = frame.record(mask); frame.count_cascades(mask); ++frame.counts.leased;
                r.serial = 1000 + i; r.size = sizes[i]; r.vb = 100 + i;
            }
            frame.select_cascades(select_caps, 2, scratch);
            CHECK(frame.counts.cascade[1] == 4 && frame.counts.cascade_capped[1] == 4 && frame.counts.cascade[0] == 8 && frame.counts.cascade_capped[0] == 0 && frame.counts.dropped_size[1] == .3f && frame.counts.dropped_size[0] == 0.f);
            unsigned kept = 0; bool right = true;
            for (unsigned k = 0; k < frame.record_count; ++k) if (frame.records[k].cascades & 2) { ++kept; right &= frame.records[k].size >= .44f; }
            CHECK(kept == 4 && right);
            unsigned dropped = 0, moved = 0;
            CHECK(frame.compact([&](unsigned) { ++dropped; }, [&](unsigned, unsigned) { ++moved; }) == 0 && dropped == 0 && moved == 0 && frame.record_count == 8); // every record keeps cascade 0
        }
        // Ties: equal sizes keep the lower serials; a record dropped from every cascade compacts out, the survivors keep their order.
        frame.reset();
        for (unsigned i = 0; i < 6; ++i) { std::uint8_t mask = 2; frame.draw(true, true, true, false, false, true, shadow_replay::PoolClass::Managed, shadow_replay::PoolClass::Managed, false, shadow_replay::record_capacity, &mask, nullptr); auto& r = frame.record(mask); frame.count_cascades(mask); ++frame.counts.leased; r.serial = 2000 - i; r.size = 1.f; r.vb = 300 + i; }
        const unsigned tie_caps[4] = {16, 3, 16, 16};
        frame.select_cascades(tie_caps, 2, scratch);
        unsigned dropped = 0; std::uint64_t order[3]; unsigned n = 0;
        CHECK(frame.compact([&](unsigned) { ++dropped; }, [&](unsigned from, unsigned to) { CHECK(from > to); }) == 3 && dropped == 3 && frame.record_count == 3 && frame.counts.capped == 3 && frame.counts.leased == 3 && frame.counts.dropped_size[1] == 1.f);
        for (unsigned k = 0; k < frame.record_count; ++k) order[n++] = frame.records[k].vb;
        CHECK(order[0] == 303 && order[1] == 304 && order[2] == 305); // serials 1997, 1996, 1995: the lowest three, in submission order
        // The ring: a first sighting misses, the same rows are static, rows beyond eps are moving and become the new sighting, an eviction misses again.
        static shadow_caster_class::Ring ring; ring.clear();
        double world[12] = {1, 0, 0, 10, 0, 1, 0, 20, 0, 0, 1, 30}, moved_rows[12] = {1, 0, 0, 10.08, 0, 1, 0, 20, 0, 0, 1, 30}, near_rows[12] = {1, 0, 0, 10.03, 0, 1, 0, 20, 0, 0, 1, 30};
        const std::uint64_t key = shadow_caster_class::Ring::key_of(77, 5, 0, 0, 3);
        using shadow_caster_class::Verdict;
        CHECK(ring.test(key, world, lo, hi, .05, 1) == Verdict::Miss && ring.test(key, world, lo, hi, .05, 2) == Verdict::Static && ring.test(key, near_rows, lo, hi, .05, 3) == Verdict::Static);
        CHECK(ring.test(key, moved_rows, lo, hi, .05, 4) == Verdict::Moving && ring.test(key, moved_rows, lo, hi, .05, 5) == Verdict::Static && ring.test(key, moved_rows, lo, hi, .05, 5) == Verdict::Static);
        CHECK(shadow_caster_class::Ring::key_of(77, 5, 0, 0, 3) != shadow_caster_class::Ring::key_of(78, 5, 0, 0, 3) && shadow_caster_class::Ring::key_of(77, 5, 3, 0, 3) != key);
        std::uint64_t colliders[3]; unsigned found = 0; // three keys of one set evict the oldest way
        for (std::uint64_t s = 1; found < 3 && s < 1000000; ++s) { const std::uint64_t k = shadow_caster_class::Ring::key_of(s, 9, 0, 0, 3); if (k % shadow_caster_class::Ring::sets == key % shadow_caster_class::Ring::sets && k != key) colliders[found++] = k; }
        CHECK(found == 3);
        CHECK(ring.test(colliders[0], world, lo, hi, .05, 6) == Verdict::Miss && ring.test(colliders[1], world, lo, hi, .05, 7) == Verdict::Miss); // key (stamp 5) is evicted by the second
        CHECK(ring.test(key, world, lo, hi, .05, 8) == Verdict::Miss && ring.test(colliders[1], world, lo, hi, .05, 8) == Verdict::Static);
    }
    // ---- the cascade set, the budget policy, the bounds mask, the light rows
    {
        using namespace renderer;
        ShadowCascadeSet set{};
        CHECK(shadow_cascade_set(shadow_cascade_extent_defaults, 4, nullptr, nullptr, shadow_cascade_budget_default, set));
        CHECK(set.count == 4 && set.cascades[3].half_extent == 25000.f && set.cascades[0].depth_toward_light == 50000.f && set.cascades[0].depth_behind == 512.f
              && set.cascades[3].depth_behind == 50000.f && set.cascades[2].size == 4096 && set.caps[0] == 128 && set.caps[3] == 1024 && set.budget == 640
              && set.cascades[0].forward_offset == 128.f && set.cascades[1].forward_offset == 0.f);
        ShadowCascadeSet bad{};
        const float descending[2] = {1500, 250}, small[1] = {49}, huge[1] = {50001};
        const unsigned big[1] = {8192}, zero[1] = {0};
        CHECK(!shadow_cascade_set(descending, 2, nullptr, nullptr, 640, bad) && !shadow_cascade_set(small, 1, nullptr, nullptr, 640, bad) && !shadow_cascade_set(huge, 1, nullptr, nullptr, 640, bad)
              && !shadow_cascade_set(shadow_cascade_extent_defaults, 1, big, nullptr, 640, bad) && !shadow_cascade_set(shadow_cascade_extent_defaults, 1, nullptr, zero, 640, bad)
              && !shadow_cascade_set(shadow_cascade_extent_defaults, 1, nullptr, nullptr, 4097, bad) && !shadow_cascade_set(shadow_cascade_extent_defaults, 0, nullptr, nullptr, 640, bad) && bad.count == 0);
        CHECK(shadow_cascade_set(small, 1, nullptr, nullptr, 640, bad, false) && bad.cascades[0].forward_offset == 0.f && bad.cascades[0].depth_behind == 98.f);
        CHECK(shadow_cascade_replays(3, 4, 641, 640, 0) && !shadow_cascade_replays(3, 4, 641, 640, 1) && shadow_cascade_replays(3, 4, 640, 640, 1) && shadow_cascade_replays(2, 4, 9999, 640, 1)
              && shadow_cascade_replays(0, 1, 9999, 640, 1));
        const CameraState c = camera();
        const float sun[4] = {0, 1, 0, 0}; // straight down: sun space z = -world y
        ShadowCascadeBounds bounds{};
        CHECK(shadow_cascade_bounds(c, sun, set, bounds) && bounds.count == 4);
        // Object space = world space: clip x = m00 view x, clip y = m11 view y, clip w = view z; view = world - position.
        const float rows[16] = {.8f, 0, 0, -80.f, 0, 1.2f, 0, -60.f, 0, 0, 1, 0, 0, 0, 1, 25.f};
        auto mask_of = [&](float x, float y, float z, float h) { const float lo[3] = {x - h, y - h, z - h}, hi[3] = {x + h, y + h, z + h}; return shadow_cascade_bounds_mask(c, rows, bounds, lo, hi); };
        CHECK(mask_of(100, 50, 103, 10) == 15);            // at cascade 0's centre (128 forward)
        CHECK(mask_of(100, 50, -25 + 1000, 10) == 14);     // 1,000 units ahead: not cascade 0
        CHECK(mask_of(100 + 5000, 50, -25, 10) == 12 && mask_of(100 + 20000, 50, -25, 10) == 8 && mask_of(100 + 26000, 50, -25, 10) == 0);
        CHECK(mask_of(100, 50 + 2000, 103, 10) == 15);     // the sun column: 2,000 units towards the light
        CHECK(mask_of(100, 50 + 60000, 103, 10) == 15);    // beyond depth_toward_light: the light side is open (pancaked)
        CHECK(mask_of(100, 50 - 600, 103, 10) == 14);      // behind cascade 0's 512 units
        CHECK(mask_of(100, 50 - 60000, 103, 10) == 0);     // beyond every far side
        // The single-map verdict shares the open light side.
        ShadowReplayCascade single{}; ShadowReplayBasis basis{}; float view_rows[12];
        CHECK(shadow_replay_basis(c, sun, single, basis) && shadow_replay_view_rows(c, basis, single, view_rows));
        auto verdict_of = [&](float y) { const float lo[3] = {90, y - 10, 93}, hi[3] = {110, y + 10, 113}; return shadow_replay_bounds_verdict(c, rows, view_rows, lo, hi); };
        CHECK(verdict_of(50) == 1 && verdict_of(50 + 2000) == 1 && verdict_of(50 - 600) == 0);
        // Shared-product light rows against the single-map helper, per cascade, and the asymmetric depth law.
        for (unsigned k = 0; k < 4; ++k) {
            ShadowReplayBasis b{}; CHECK(shadow_replay_basis(c, sun, set.cascades[k], b));
            double base[3][4]; float got[12], reference[16];
            CHECK(shadow_cascade_draw_rows(c, rows, b, base) && shadow_cascade_light_rows(base, b, set.cascades[k], got) && shadow_replay_light_rows(c, rows, b, set.cascades[k], reference));
            for (unsigned i = 0; i < 12; ++i) CHECK(std::fabs(got[i] - reference[i]) <= 1e-6f * (1.f + std::fabs(reference[i])));
            CHECK(reference[12] == 0 && reference[13] == 0 && reference[14] == 0 && reference[15] == 1);
            // A point depth_toward_light above the centre maps to z = 0, depth_behind below to z = 1.
            const float centre_y = b.center[1], top = centre_y + set.cascades[k].depth_toward_light, bottom = centre_y - set.cascades[k].depth_behind;
            const double z_top = got[8] * 100. + got[9] * double(top) + got[10] * 103. + got[11], z_bottom = got[8] * 100. + got[9] * double(bottom) + got[10] * 103. + got[11];
            CHECK(std::fabs(z_top) < 1e-3 && std::fabs(z_bottom - 1.) < 1e-3);
        }
        // 70,000 units from the origin: the snapped centre and the axes enter the rows as doubles, so the row set of an
        // object near the camera is exact to float rounding of small numbers (a float centre is quantised to 0.0078 units
        // there: 3e-5 of cascade 0's NDC, an eighth of a 0.06-unit texel).
        {
            CameraState far_camera = camera(); far_camera.t[0] = -70000.3f; far_camera.t[2] = 25.7f;
            const double position[3] = {double(-far_camera.t[0]), 50., double(-far_camera.t[2])};
            const float tilted[4] = {.30151134f, .90453403f, -.30151134f, 0};
            ShadowReplayBasis b{}; CHECK(shadow_replay_basis(far_camera, tilted, set.cascades[0], b));
            const float local[16] = {.8f, 0, 0, 8.f, 0, 1.2f, 0, -3.6f, 0, 0, 1, 0, 0, 0, 1, 40.f}; // object origin at view (10, -3, 40)
            float got[16]; CHECK(shadow_replay_light_rows(far_camera, local, b, set.cascades[0], got));
            double worst = 0, worst_float_centre = 0;
            for (int corner = 0; corner < 8; ++corner) {
                const double o[3] = {corner & 1 ? 6. : -6., corner & 2 ? 4. : -4., corner & 4 ? 9. : -9.};
                const double world[3] = {position[0] + 10. + o[0], position[1] - 3. + o[1], position[2] + 40. + o[2]};
                for (unsigned a = 0; a < 2; ++a) {
                    double reference = 0, quantised = 0;
                    for (unsigned k = 0; k < 3; ++k) { reference += b.axes[a][k] * (world[k] - b.center_d[k]); quantised += b.axes[a][k] * (world[k] - double(b.center[k])); }
                    const double ndc = double(got[a * 4]) * o[0] + double(got[a * 4 + 1]) * o[1] + double(got[a * 4 + 2]) * o[2] + double(got[a * 4 + 3]);
                    worst = std::fmax(worst, std::fabs(ndc - reference / 250.)); worst_float_centre = std::fmax(worst_float_centre, std::fabs(quantised - reference) / 250.);
                }
            }
            std::printf("CENTRE rows_error_ndc=%.3g float_centre_error_ndc=%.3g\n", worst, worst_float_centre);
            CHECK(worst < 1e-6 && worst * 10. < worst_float_centre); // the float centre alone costs more than ten times the whole row set's error
        }
        // The single map's symmetric range is the same law with both sides at the half range.
        ShadowReplayCascade symmetric{}; symmetric.set_depth_half(512.f);
        CHECK(symmetric.depth_range() == 1024. && symmetric.depth_half() == 512. && symmetric.depth_toward_light == 512.f && symmetric.depth_behind == 512.f);
    }
    // ---- the sun as a polled world position (shadow_replay_sun_point.h)
    {
        using namespace x3m::renderer; using namespace x3m::shadow_replay;
        ShadowCascadeSet set{}; CHECK(shadow_cascade_set(shadow_cascade_extent_defaults, 4, nullptr, nullptr, 640, set));
        const CameraState cam = camera(); // position (100, 50, -25), looking down +z
        const double distance = 1.5689e7; // the run-39 fit, x 0.01
        const double unit[3] = {-.300079, .455345, -.838220};
        std::int32_t native[3]; double light[3];
        for (unsigned i = 0; i < 3; ++i) { native[i] = std::int32_t(unit[i] * distance * 100.); light[i] = native[i] * .01; }
        auto constant_at = [&](const double o[3], float out[4]) { double d[3], n = 0; for (unsigned i = 0; i < 3; ++i) { d[i] = light[i] - o[i]; n += d[i] * d[i]; } n = std::sqrt(n);
            for (unsigned i = 0; i < 3; ++i) out[i] = float(std::round(d[i] / n * 65536.) / 65536.); out[3] = 0; }; // the engine's 1/65536 quantisation
        PointSun sun;
        sun.begin_frame(); CHECK(!sun.decide(true, cam, set) && sun.reason == PointSunReason::Unavailable); sun.end_frame(); // no poll
        sun.begin_frame(); sun.set_poll(native, PointSunReason::Point);
        CHECK(!sun.decide(true, cam, set) && sun.reason == PointSunReason::Unchecked && sun.sun(0) == nullptr); sun.end_frame();
        sun.begin_frame(); sun.set_poll(native, PointSunReason::Point);
        const double node[3] = {-123845.57, -9315.6, -31755.59}; float c[4]; constant_at(node, c); // run111's node, 2.2e5 units from the camera
        CHECK(sun.check(c, node) && sun.agreement_degrees() < .002);
        { // the draw origin law: clip rows of an object at view (10, -3, 40)
          const float rows[16] = {.8f, 0, 0, 8.f, 0, 1.2f, 0, -3.6f, 0, 0, 1, 0, 0, 0, 1, 40.f}; double o[3];
          CHECK(PointSun::draw_origin(cam, rows, o) && std::fabs(o[0] - 110.) < 1e-4 && std::fabs(o[1] - 47.) < 1e-4 && std::fabs(o[2] - 15.) < 1e-4); }
        CHECK(sun.decide(true, cam, set) && sun.reason == PointSunReason::Point && sun.rederived == 4 && std::fabs(sun.distance / distance - 1.) < 1e-4);
        // Camera-centred cascades at this distance share one direction bit for bit (128 units of centre offset are 8e-6 rad).
        for (unsigned k = 1; k < 4; ++k) CHECK(!std::memcmp(sun.sun(0), sun.sun(k), 16));
        ShadowCascadeBounds shared{}; CHECK(shadow_cascade_bounds_suns(cam, sun.suns, set, shared) && shared.shared);
        // The latch would take that node s direction: 0.4 degrees from the direction at the camera, which the poll restores.
        { double at_camera[3] = {light[0] - 100., light[1] - 50., light[2] + 25.}, held[3] = {sun.sun(0)[0], sun.sun(0)[1], sun.sun(0)[2]}, latch[3] = {c[0], c[1], c[2]}, s2 = 0; bool opposed = false;
          CHECK(PointSun::sine2(at_camera, held, s2, opposed) && !opposed && point_sun_degrees(s2) < 1e-3); // cascade 0 s centre lies 128 units ahead: 4.7e-4 degrees
          CHECK(PointSun::sine2(at_camera, latch, s2, opposed) && point_sun_degrees(s2) > .3);
          std::printf("POINT_SUN latch_error_deg=%.4f\n", point_sun_degrees(s2)); }
        sun.end_frame();
        const float held0[4] = {sun.suns[0], sun.suns[1], sun.suns[2], 0};
        // Hold: 3,000 units of travel across the light stay inside 1 / 4096 rad (3,830 units); 5,000 re-derive.
        CameraState moved = cam; moved.t[0] -= 3000.f;
        sun.begin_frame(); sun.set_poll(native, PointSunReason::Point);
        CHECK(sun.decide(true, moved, set) && sun.rederived == 0 && !std::memcmp(held0, sun.sun(0), 12)); sun.end_frame(); // carried validation, held bit for bit
        moved.t[0] -= 2000.f;
        sun.begin_frame(); sun.set_poll(native, PointSunReason::Point);
        CHECK(sun.decide(true, moved, set) && sun.rederived == 4 && std::memcmp(held0, sun.sun(0), 12) != 0); sun.end_frame();
        { double a[3] = {held0[0], held0[1], held0[2]}, b[3] = {sun.sun(0)[0], sun.sun(0)[1], sun.sun(0)[2]}, s2 = 0; bool opposed = false;
          CHECK(PointSun::sine2(a, b, s2, opposed) && s2 <= (2. / 4096.) * (2. / 4096.)); } // the re-derivation turns the basis by at most 2 / size
        // Hysteresis: a changed light position that no draw has checked yet (and a frame without a poll) keeps `point` on
        // the last validated position, every held direction bit for bit, for point_sun_carry_frames frames; then unchecked.
        std::int32_t other[3] = {native[0] + 100000, native[1], native[2]};
        float held1[16]; std::memcpy(held1, sun.suns, sizeof held1);
        for (unsigned i = 0; i < point_sun_carry_frames; ++i) {
            sun.begin_frame(); if (i & 1) sun.set_poll(other, PointSunReason::Point);
            CHECK(sun.decide(true, moved, set) && sun.carried_frame && sun.rederived == 0 && !std::memcmp(held1, sun.suns, sizeof held1) && sun.used[0] == light[0]); sun.end_frame();
            CHECK(sun.carried == i + 1);
        }
        sun.begin_frame(); sun.set_poll(other, PointSunReason::Point); CHECK(!sun.decide(true, moved, set) && sun.reason == PointSunReason::Unchecked); sun.end_frame();
        sun.begin_frame(); sun.set_poll(native, PointSunReason::Point); CHECK(sun.check(c, node) && sun.decide(true, moved, set) && !sun.carried_frame); sun.end_frame(); CHECK(sun.carried == 0);
        // A disagreement before the decision: the frame is the latch's (disagrees), then exactly point_sun_cooldown_frames of cooldown.
        const float wrong[4] = {0, 0, 1, 0};
        sun.begin_frame(); sun.set_poll(native, PointSunReason::Point);
        CHECK(!sun.check(wrong, node) && sun.agreement_degrees() > 90. && !sun.decide(true, moved, set) && sun.reason == PointSunReason::Disagrees); sun.end_frame();
        auto cools_down = [&] {
            for (unsigned i = 0; i < point_sun_cooldown_frames; ++i) {
                sun.begin_frame(); sun.set_poll(native, PointSunReason::Point); CHECK(sun.check(c, node));
                CHECK(!sun.decide(true, moved, set) && sun.reason == PointSunReason::Cooldown && sun.sun(0) == nullptr); sun.end_frame();
            }
            sun.begin_frame(); sun.set_poll(native, PointSunReason::Point); CHECK(sun.check(c, node)); CHECK(sun.decide(true, moved, set) && sun.reason == PointSunReason::Point); sun.end_frame();
        };
        cools_down();
        // The production order: the box test decides `point` first, a later draw of the same frame disagrees. The frame
        // finishes as `point` (its masks and bases agree); the switch and the cooldown start on the next frame.
        const std::uint64_t point_frames = sun.frames_point;
        sun.begin_frame(); sun.set_poll(native, PointSunReason::Point); CHECK(sun.check(c, node) && sun.decide(true, moved, set));
        CHECK(!sun.check(wrong, node) && sun.decide(true, moved, set) && sun.sun(0) != nullptr && sun.reason == PointSunReason::Point); sun.end_frame();
        CHECK(sun.frames_point == point_frames + 1 && sun.cooldown == point_sun_cooldown_frames && !sun.validated);
        cools_down();
        // The grid anchor: 1e5 units out, a re-derivation keeps the grid phase at the centre (an origin-anchored grid
        // moves by |centre| x turn there) and moves a texel at the cascade's edge by at most one texel.
        {
            CameraState out = camera(); out.t[0] = -100000.f; out.t[2] = -40000.f;
            const double there[3] = {100000., 50., 40000.}; float k[4]; constant_at(there, k);
            PointSun far; far.begin_frame(); far.set_poll(native, PointSunReason::Point); CHECK(far.check(k, there) && far.decide(true, out, set));
            ShadowReplayBasis before{}; CHECK(shadow_replay_basis(out, far.sun(1), set.cascades[1], before, far.grid_anchor(1))); far.end_frame();
            out.t[0] -= 5000.f;
            far.begin_frame(); far.set_poll(native, PointSunReason::Point); CHECK(far.decide(true, out, set) && far.rederived == 4);
            ShadowReplayBasis anchored{}, origin{}; CHECK(shadow_replay_basis(out, far.sun(1), set.cascades[1], anchored, far.grid_anchor(1)) && shadow_replay_basis(out, far.sun(1), set.cascades[1], origin));
            const double texel = shadow_replay_world_texel(set.cascades[1]);
            auto phase = [&](const ShadowReplayBasis& b) { double worst = 0; for (unsigned a = 0; a < 2; ++a) { double u = 0; for (unsigned i = 0; i < 3; ++i) u += (b.center_d[i] - before.center_d[i]) * before.axes[a][i];
                u /= texel; worst = std::fmax(worst, std::fabs(u - std::round(u))); } return worst; }; // the new centre against the OLD grid, in texels
            double edge = 0; // a texel at the cascade's edge along the old right axis: its new-grid coordinate against its old-grid one
            for (unsigned a = 0; a < 2; ++a) { double now = 0, was = 0; for (unsigned i = 0; i < 3; ++i) { const double p = before.axes[0][i] * 1500.; now += p * anchored.axes[a][i]; was += p * before.axes[a][i]; } edge = std::fmax(edge, std::fabs(now - was) / texel); }
            std::printf("POINT_SUN anchor phase_anchored=%.6f phase_origin=%.6f edge_shift_texels=%.4f\n", phase(anchored), phase(origin), edge);
            CHECK(phase(anchored) < 1e-3 && phase(origin) > .02 && edge <= 1.);
            far.end_frame();
        }
        const std::int32_t close[3] = {10000, 5000 + 7000000, -2500}; // 70,000 units above the camera: inside 50,000 + 25,000
        sun.begin_frame(); sun.set_poll(close, PointSunReason::Point); const float up[4] = {0, 1, 0, 0}; const double here[3] = {100., 50., -25.};
        CHECK(sun.check(up, here) && !sun.decide(true, cam, set) && sun.reason == PointSunReason::Near); sun.end_frame();
        sun.begin_frame(); sun.set_poll(nullptr, PointSunReason::NoLight); CHECK(!sun.decide(true, cam, set) && sun.reason == PointSunReason::NoLight); sun.end_frame();
        sun.begin_frame(); sun.set_poll(native, PointSunReason::Point); CHECK(!sun.decide(false, cam, set) && sun.reason == PointSunReason::Off); sun.end_frame();
        CHECK(sun.frames_latch[unsigned(PointSunReason::Disagrees)] == 1 && sun.frames_latch[unsigned(PointSunReason::Near)] == 1 && sun.frames_latch[unsigned(PointSunReason::NoLight)] == 1);
        // Per-cascade suns through the bounds: equal suns give the one-sun bounds bit for bit; different suns give
        // per-cascade rows whose mask equals each cascade's own one-sun mask.
        const float one[4] = {.30151134f, .90453403f, -.30151134f, 0}, two[4] = {.35f, .88f, -.32f, 0};
        float same[16], mixed[16]; for (unsigned k = 0; k < 4; ++k) { std::memcpy(same + k * 4, one, 16); std::memcpy(mixed + k * 4, k ? two : one, 16); }
        ShadowCascadeBounds a{}, b{}, m{}, t{};
        CHECK(shadow_cascade_bounds(cam, one, set, a) && shadow_cascade_bounds_suns(cam, same, set, b) && b.shared && !std::memcmp(a.rows, b.rows, sizeof a.rows) && !std::memcmp(a.lo, b.lo, sizeof a.lo) && !std::memcmp(a.hi, b.hi, sizeof a.hi));
        CHECK(shadow_cascade_bounds_suns(cam, mixed, set, m) && !m.shared && shadow_cascade_bounds(cam, two, set, t));
        unsigned compared = 0;
        for (int x = -3; x <= 3; ++x) for (int z = 0; z < 4; ++z) {
            const float rows[16] = {.8f, 0, 0, .8f * 400.f * x, 0, 1.2f, 0, 1.2f * 120.f * z, 0, 0, 1, 0, 0, 0, 1, 300.f * z}; const float lo[3] = {-40, -40, -40}, hi[3] = {40, 40, 40};
            const int first = shadow_cascade_bounds_mask(cam, rows, a, lo, hi), rest = shadow_cascade_bounds_mask(cam, rows, t, lo, hi), both = shadow_cascade_bounds_mask(cam, rows, m, lo, hi);
            CHECK(first >= 0 && rest >= 0 && both == ((first & 1) | (rest & ~1))); ++compared;
        }
        std::printf("POINT_SUN compared_masks=%u\n", compared);
    }
    std::printf("RESULT %s failures=%d\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
'''


def constant_table(constants):
    """A ps_3_0 program whose leading comment is a constant table of
    (name, register_set, register_index) rows (the documented
    D3DXSHADER_CONSTANTTABLE layout), followed by END."""
    header = 28
    info = header
    strings = info + 20 * len(constants)
    blob = b''
    offsets = []
    for name, _, _ in constants:
        offsets.append(strings + len(blob)); blob += name.encode() + b'\0'
    creator = strings + len(blob); blob += b'test\0'
    table = struct.pack('<7I', 28, creator, 0xffff0300, len(constants), info, 0, 0)
    for (name, register_set, index), offset in zip(constants, offsets):
        table += struct.pack('<I4HII', offset, register_set, index, 1, 0, 0, 0)
    table += blob
    table += b'\0' * (-len(table) % 4)
    body = b'CTAB' + table
    words = len(body) // 4
    return struct.pack('<I', 0xffff0300) + struct.pack('<I', 0xfffe | (words << 16)) + body + struct.pack('<I', 0x0000ffff)


GLOW = [('g_EnableGlow', 2, 4), ('LightDir_Color0', 2, 3), ('LightDir_Dir0', 2, 5), ('LightDir_Dir1', 2, 7), ('s_Diffuse', 3, 0)]
DETAIL = [('LightDir_Dir0', 2, 0), ('p_DetailMapBlendWeight', 2, 4)]
HULL = [('LightDir_Color0', 2, 3), ('LightDir_Dir0', 2, 4)]
NONE = [('g_EnableGlow', 2, 4), ('LightDir_Dir1', 2, 5)]
SAMPLER = [('LightDir_Dir0', 3, 2)]  # the name in the sampler register set is not a float register
REAL = {'ps_8759c7838bbc86c2.bin': 4, 'ps_fffdabd910793aba.bin': 5, 'ps_5f82ecacd39529cd.bin': 5, 'ps_517540ae6d5e5410.bin': 0, 'ps_a66fb1981ba755b2.bin': 0}
REAL_DIRECTORY = Path('/tmp/x3-shader-sweep/programs')  # local untracked dumps; checked when present


class PureHeaders(unittest.TestCase):
    def test_driver(self):
        compiler = shutil.which('c++') or shutil.which('clang++') or shutil.which('g++')
        if compiler is None:
            self.skipTest('no host C++ compiler')
        with tempfile.TemporaryDirectory(prefix='x3-shadow-cascades-') as directory:
            work = Path(directory)
            (work / 'driver.cpp').write_text(DRIVER)
            subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'), str(work / 'driver.cpp'), '-o', str(work / 'driver')], check=True)
            arguments = []
            for name, rows, expected in (('glow', GLOW, 5), ('detail', DETAIL, 0), ('hull', HULL, 4), ('none', NONE, -1), ('sampler', SAMPLER, -1)):
                (work / name).write_bytes(constant_table(rows)); arguments += [str(work / name), str(expected)]
            malformed = bytearray(constant_table(GLOW)); malformed[8 + 4 + 16:8 + 4 + 20] = struct.pack('<I', 1 << 20)  # the info offset beyond the table
            (work / 'malformed').write_bytes(bytes(malformed)); arguments += [str(work / 'malformed'), '-1']
            real = 0
            for name, expected in REAL.items():
                if (REAL_DIRECTORY / name).exists():
                    arguments += [str(REAL_DIRECTORY / name), str(expected)]; real += 1
            result = subprocess.run([str(work / 'driver'), *arguments], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout[-2000:] + result.stderr[-2000:])
            self.assertIn('RESULT PASS failures=0', result.stdout)
            self.assertEqual(result.stdout.count('REGISTER '), 6 + real)


def launch(directory, *args, inherited=None):
    spec = importlib.util.spec_from_file_location('cascade_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    game = Path(directory) / 'game'; game.mkdir(exist_ok=True); (game / 'X3AP.exe').touch()
    wine = Path(directory) / 'wine'; wine.touch()
    argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game), *args]
    output, error = io.StringIO(), io.StringIO()
    with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), mock.patch.dict(module.os.environ, inherited or {}), \
            mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
            contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
        try:
            module.main()
        except SystemExit as exit_error:
            return exit_error.code, output.getvalue(), error.getvalue()
    return 0, output.getvalue(), error.getvalue()


class LauncherOptions(unittest.TestCase):
    BASE = ['--motion-output', '--ownership', '--shadow-replay-depth']

    def test_default_is_off_and_inherited_values_cannot_leak(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = launch(directory, *self.BASE, inherited={'X3M_SHADOW_CASCADES': '250,1500', 'X3M_SHADOW_SUN_POLL': '1', 'X3M_SHADOW_CASCADE_SIZES': '64', 'X3M_SHADOW_CASCADE_CAPS': '1',
                                                                          'X3M_SHADOW_CASCADE_BUDGET': '1'})
            self.assertEqual(code, 0, error); env = json.loads(output)['env']
            self.assertEqual(env['X3M_SHADOW_CASCADES'], '0')
            self.assertEqual(env['X3M_SHADOW_SUN_POLL'], '0')  # the poll exists only with the cascades; an inherited 1 cannot leak
            for name in ('X3M_SHADOW_CASCADE_SIZES', 'X3M_SHADOW_CASCADE_CAPS', 'X3M_SHADOW_CASCADE_BUDGET'):
                self.assertNotIn(name, env)
            # The single-map variables are untouched by the new options.
            self.assertEqual((env['X3M_SHADOW_REPLAY_EXTENT'], env['X3M_SHADOW_REPLAY_DEPTH_HALF'], env['X3M_SHADOW_REPLAY_SIZE'], env['X3M_SHADOW_REPLAY_CAP']), ('250.0', '512.0', '1024', '512'))

    def test_values(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = launch(directory, *self.BASE, '--shadow-cascades', 'default')
            self.assertEqual(code, 0, error); env = json.loads(output)['env']
            self.assertEqual(env['X3M_SHADOW_CASCADES'], '250.0,1500.0,7500.0,25000.0'); self.assertNotIn('X3M_SHADOW_CASCADE_SIZES', env)
            self.assertEqual(env['X3M_SHADOW_SUN_POLL'], '1')  # default on with the cascades
            for value, expected in (('off', '0'), ('on', '1')):
                code, output, error = launch(directory, *self.BASE, '--shadow-cascades', 'default', '--shadow-sun-poll', value, inherited={'X3M_SHADOW_SUN_POLL': '1' if value == 'off' else '0'})
                self.assertEqual(code, 0, error); self.assertEqual(json.loads(output)['env']['X3M_SHADOW_SUN_POLL'], expected)
            code, output, error = launch(directory, *self.BASE, '--shadow-cascades', '250,1500,7500', '--shadow-cascade-sizes', '4096,2048,1024', '--shadow-cascade-caps', '64',
                                         '--shadow-cascade-budget', '900')
            self.assertEqual(code, 0, error); env = json.loads(output)['env']
            self.assertEqual((env['X3M_SHADOW_CASCADES'], env['X3M_SHADOW_CASCADE_SIZES'], env['X3M_SHADOW_CASCADE_CAPS'], env['X3M_SHADOW_CASCADE_BUDGET']),
                             ('250.0,1500.0,7500.0', '4096,2048,1024', '64', '900'))

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('1500,250', '250,250', '49', '50001', '1,2,3,4,5', 'abc', '250,,1500', 'nan'):
                code, _, error = launch(directory, *self.BASE, '--shadow-cascades', value)
                self.assertNotEqual(code, 0, value); self.assertIn('--shadow-cascades takes 1..4 ascending half-extents within [50, 50000]', error)
            for option, value, message in (('--shadow-cascade-sizes', '63', '--shadow-cascade-sizes takes one value or one per cascade within [64, 4096]'),
                                           ('--shadow-cascade-sizes', '4096,4096', '--shadow-cascade-sizes takes one value or one per cascade'),
                                           ('--shadow-cascade-caps', '4097', '--shadow-cascade-caps takes one value or one per cascade within [1, 4096]'),
                                           ('--shadow-cascade-records', '4097', '--shadow-cascade-records takes one value or one per cascade within [1, 4096]'),
                                           ('--shadow-cascade-records', '1024,4096', '--shadow-cascade-records takes one value or one per cascade'),
                                           ('--shadow-cascade-static-from', '0', '--shadow-cascade-static-from must be within [1, 4]'),
                                           ('--shadow-cascade-static-from', '5', '--shadow-cascade-static-from must be within [1, 4]'),
                                           ('--shadow-cascade-budget', '0', '--shadow-cascade-budget must be within [1, 4096]'),
                                           ('--shadow-cascade-budget', '4097', '--shadow-cascade-budget must be within [1, 4096]')):
                code, _, error = launch(directory, *self.BASE, '--shadow-cascades', '250,1500,7500', option, value)
                self.assertNotEqual(code, 0, (option, value)); self.assertIn(message, error)
            code, _, error = launch(directory, '--motion-output', '--ownership', '--shadow-cascades', 'default')
            self.assertNotEqual(code, 0); self.assertIn('--shadow-cascades requires --shadow-replay-depth', error)
            for option, value in (('--shadow-cascade-sizes', '1024'), ('--shadow-cascade-caps', '8'), ('--shadow-cascade-budget', '64'), ('--shadow-sun-poll', 'on'),
                                  ('--shadow-cascade-records', '4096'), ('--shadow-cascade-static-from', '3'), ('--shadow-cascade-drop-order', 'importance')):
                code, _, error = launch(directory, *self.BASE, option, value)
                self.assertNotEqual(code, 0, option); self.assertIn(f'{option} requires --shadow-cascades', error)
            code, _, error = launch(directory, *self.BASE, '--shadow-cascades', 'default', '--shadow-cascade-drop-order', 'largest')
            self.assertNotEqual(code, 0); self.assertIn('invalid choice', error)

    def test_pool_options(self):
        """Caster pool control (shadow-cascade-extents.md, "Caster pool control"): absent by default, an inherited value cannot leak, the values pass through."""
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = launch(directory, *self.BASE, '--shadow-cascades', 'default',
                                         inherited={'X3M_SHADOW_CASCADE_RECORDS': '4096', 'X3M_SHADOW_CASCADE_STATIC_FROM': '1', 'X3M_SHADOW_CASCADE_DROP_ORDER': 'importance'})
            self.assertEqual(code, 0, error); env = json.loads(output)['env']
            for name in ('X3M_SHADOW_CASCADE_RECORDS', 'X3M_SHADOW_CASCADE_STATIC_FROM', 'X3M_SHADOW_CASCADE_DROP_ORDER'):
                self.assertNotIn(name, env)
            code, output, error = launch(directory, *self.BASE, '--shadow-cascades', 'default', '--shadow-cascade-records', '1024,1024,2048,4096', '--shadow-cascade-caps', '128,512,2048,4096',
                                         '--shadow-cascade-static-from', '3', '--shadow-cascade-drop-order', 'importance')
            self.assertEqual(code, 0, error); env = json.loads(output)['env']
            self.assertEqual((env['X3M_SHADOW_CASCADE_RECORDS'], env['X3M_SHADOW_CASCADE_CAPS'], env['X3M_SHADOW_CASCADE_STATIC_FROM'], env['X3M_SHADOW_CASCADE_DROP_ORDER']),
                             ('1024,1024,2048,4096', '128,512,2048,4096', '3', 'importance'))
            code, output, error = launch(directory, *self.BASE, '--shadow-cascades', 'default', '--shadow-cascade-records', '4096', '--shadow-cascade-drop-order', 'submission')
            self.assertEqual(code, 0, error); env = json.loads(output)['env']
            self.assertEqual((env['X3M_SHADOW_CASCADE_RECORDS'], env['X3M_SHADOW_CASCADE_DROP_ORDER']), ('4096', 'submission')); self.assertNotIn('X3M_SHADOW_CASCADE_STATIC_FROM', env)


if __name__ == '__main__':
    unittest.main()
