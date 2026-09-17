"""Host contracts of the sun-shadow caster retention
(docs/architecture/shadow-caster-retention.md, stages 1 and 2). No Wine, no D3D.

* src/proxy/shadow_retention_core.h compiled natively into one driver: the
  static class after 8 sightings within eps, a moving node dropped on its first
  unseen frame, a static node moved while unseen (reclassified, resight
  `moved`), a node resubmitted with another LOD replacing its whole draw set,
  one held reference per shared resource with AddRef/Release balance, the
  1,025th node evicting the farthest unseen one, retirement, full revalidation,
  the round-robin buffer check within 8 frames, box exit, the age cap, the
  per-frame cascade mask against the current boxes, nearest-first under a cap,
  the deferred epoch flush, the excluded classes, abandoned sightings, the
  index map against std::map, and the precision twin: rows recovered from two
  unrelated cameras 81,000 units from the origin agree within eps;
* tools/analysis/shadow_retention.py: the frame and resight line parsers, their
  identities and the census summary;
* verification/probe/shadow_replay_depth.py: the replayed_live / replayed_retained tail;
* tools/manage.py --dry-run: the launcher options.
"""
import contextlib
import importlib.util
import io
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'verification/probe'))
import shadow_retention as retention  # noqa: E402
import shadow_replay_depth as depth_replay  # noqa: E402

DRIVER = r'''
#include "proxy/shadow_retention_core.h"
#include <cstdio>
#include <map>
#include <memory>
#include <random>
using namespace x3m;
namespace sr = x3m::shadow_retention;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d %s\n", __LINE__, #x); ++failures; } } while (0)
static const float sun[4] = {0.30151134f, 0.90453403f, -0.30151134f, 0.f};
struct Pose { double yaw, pitch, pos[3]; };
static renderer::CameraState camera(const Pose& p) {
    renderer::CameraState c{}; c.valid = true; c.m00 = .8f; c.m11 = 4.f / 3.f;
    const double cy = std::cos(p.yaw), sy = std::sin(p.yaw), cp = std::cos(p.pitch), sp = std::sin(p.pitch);
    const double right[3] = {cy, 0, -sy}, forward[3] = {sy * cp, -sp, cy * cp}, up[3] = {sy * sp, cp, cy * sp};
    for (unsigned i = 0; i < 3; ++i) { c.r[i * 3] = float(right[i]); c.r[i * 3 + 1] = float(up[i]); c.r[i * 3 + 2] = float(forward[i]); }
    for (unsigned j = 0; j < 3; ++j) { double t = 0; for (unsigned i = 0; i < 3; ++i) t -= double(c.r[i * 3 + j]) * p.pos[i]; c.t[j] = float(t); }
    return c;
}
// The engine's product in float32: clip rows of an object at `world` (row-major 3x4, object -> world).
static void clip_rows(const renderer::CameraState& c, const float world[12], float rows[16]) {
    for (unsigned k = 0; k < 4; ++k) {
        float view[3];
        for (unsigned j = 0; j < 3; ++j) { float v = k == 3 ? c.t[j] : 0.f; for (unsigned i = 0; i < 3; ++i) v += c.r[i * 3 + j] * world[i * 4 + k]; view[j] = v; }
        rows[k] = c.m00 * view[0]; rows[4 + k] = c.m11 * view[1]; rows[8 + k] = view[2]; rows[12 + k] = view[2];
    }
    rows[11] -= 6.f;
}
static void place(float world[12], double x, double y, double z, double turn = .3) {
    const float m[12] = {float(std::cos(turn)), 0, float(std::sin(turn)), float(x), 0, 1, 0, float(y), float(-std::sin(turn)), 0, float(std::cos(turn)), float(z)};
    std::memcpy(world, m, sizeof m);
}
struct Rig {
    std::unique_ptr<sr::Store> store{new sr::Store};
    renderer::ShadowCascadeSet set{};
    std::map<std::uintptr_t, int> refs; // the owner's AddRef/Release ledger
    Pose pose{0, 0, {0, 0, 0}};
    std::uint64_t frame = 1;
    unsigned room[4] = {1024, 1024, 1024, 1024};
    std::uint32_t age_cap = sr::age_cap_default;
    std::uint64_t changed_vb = 0; // the check callback reports this allocation as rewritten
    const float* outer_sun = nullptr; // the positional sun: cascade 1 holds another direction than cascade 0
    Rig(bool hold = true) { const float extents[2] = {250.f, 1500.f}; renderer::shadow_cascade_set(extents, 2, nullptr, nullptr, 640, set); store->configure(hold); }
    sr::Seen draw(std::uint64_t serial, const float world[12], std::uint64_t vb = 100, std::uint32_t lod = 0, std::uint32_t flags12c = 0, std::uint32_t flags130 = 0, std::uint64_t epoch = 1, std::uint64_t revision = 1) {
        float rows[16]; clip_rows(camera(pose), world, rows);
        const float lo[3] = {-10, -10, -10}, hi[3] = {10, 10, 10};
        sr::Sighting s; s.serial = serial; s.load_epoch = epoch; s.registry_epoch = 1; s.observer_epoch = 1; s.node = 0x1000 + serial; s.handle = std::uint32_t(serial); s.model = 7; s.lod = lod;
        s.flags12c = flags12c; s.flags130 = flags130; s.key.vb = vb; s.key.ib = vb + 1; s.key.declaration = 5; s.key.stride = 24; s.key.primitives = 12; s.key.vertex_count = 8; s.key.indexed = 1; s.key.topology = 4;
        s.vb = {std::uintptr_t(0x100000 + vb), vb, 1, revision}; s.ib = {std::uintptr_t(0x200000 + vb), vb + 1, 1, 1}; s.declaration = 0x300000; s.rows = rows; s.lo = lo; s.hi = hi;
        sr::Acquired acquired; store->set_eye(camera(pose));
        const sr::Seen result = store->seen(s, frame, acquired);
        for (unsigned i = 0; i < acquired.count; ++i) ++refs[acquired.identity[i]];
        return result;
    }
    void end() {
        sr::FrameInput in; in.frame = frame; in.camera = camera(pose); in.set = set; in.age_cap = age_cap;
        in.bases_valid = true;
        for (unsigned c = 0; c < set.count; ++c) in.bases_valid = in.bases_valid && renderer::shadow_replay_basis(in.camera, c && outer_sun ? outer_sun : sun, set.cascades[c], in.bases[c]);
        for (unsigned c = 0; c < 4; ++c) in.room[c] = room[c];
        store->end_scene(in, [&](const sr::Draw& d) { return changed_vb && d.key.vb == changed_vb ? sr::BufferState::Changed : sr::BufferState::Quiet; });
        release();
    }
    void release() { while (const auto id = store->pop_owed()) { if (--refs[id] == 0) refs.erase(id); } }
    sr::FrameStats next() { const sr::FrameStats f = store->frame; store->frame = {}; ++frame; return f; }
    // `frames` sightings of one static node, then the last frame's stats.
    void settle(std::uint64_t serial, const float world[12], unsigned frames = 10, std::uint64_t vb = 100) { for (unsigned i = 0; i < frames; ++i) { draw(serial, world, vb); end(); next(); } }
    int held() const { int n = 0; for (const auto& r : refs) n += r.second; return n; }
};
int main() {
    float w[12], w2[12];
    { // static after 8 sightings within eps, retained while unseen, mask against the current boxes
        Rig r; place(w, 20, 0, 60);
        for (unsigned i = 0; i < 8; ++i) { CHECK(r.draw(1, w) == (i ? sr::Seen::Known : sr::Seen::New)); r.end(); CHECK(!r.store->nodes[r.store->find_node(1)].is_static); r.next(); }
        r.draw(1, w); r.end(); CHECK(r.store->nodes[r.store->find_node(1)].is_static); CHECK(r.next().promoted == 1);
        r.pose.yaw = 2.5; r.end(); // turned away, unsubmitted
        CHECK(r.store->admitted_count == 1 && r.store->draws[r.store->admitted[0]].cascades == 3);
        auto f = r.next(); CHECK(f.nodes_unseen == 1 && f.would[0] == 1 && f.would[1] == 1 && f.unseen_outside == 1 && f.statics == 1);
        r.pose.yaw = 0; r.end(); f = r.next(); CHECK(f.unseen_in_frustum == 1 && f.admitted_checked == 1); // inside the frustum and unsubmitted: still retained, its buffer checked before issue
        r.pose.pos[0] = 600; r.end(); CHECK(r.store->admitted_count == 1 && r.store->draws[r.store->admitted[0]].cascades == 2); f = r.next(); CHECK(f.would[0] == 0 && f.would[1] == 1);
        CHECK(r.held() == 3 && r.store->references() == 3);
        r.pose.pos[0] = 20000; r.end(); f = r.next(); CHECK(f.box_exit == 1 && r.store->nodes_used == 0 && r.held() == 0); // beyond 2 x the outermost box
    }
    { // a moving node leaves on its first unseen frame
        Rig r;
        for (unsigned i = 0; i < 12; ++i) { place(w, 20 + i, 0, 60); r.draw(2, w); r.end(); r.next(); }
        CHECK(!r.store->nodes[r.store->find_node(2)].is_static);
        r.end(); auto f = r.next(); CHECK(f.moving_dropped == 1 && r.store->nodes_used == 0 && r.held() == 0 && r.store->admitted_count == 0);
    }
    { // a static node moved while unseen: reclassified on resubmission, resight `moved`; then unseen again it is dropped
        Rig r; place(w, 20, 0, 60); r.settle(3, w);
        for (unsigned i = 0; i < 5; ++i) { r.end(); r.next(); }
        place(w2, 25, 0, 60); r.draw(3, w2); r.end(); auto f = r.next();
        CHECK(f.reclassified == 1 && f.reclassified_after_unseen == 1 && r.store->totals.moved[0] == 1 && r.store->totals.same[0] == 0 && r.store->admitted_count == 0 && !r.store->nodes[r.store->find_node(3)].is_static);
        // A static node that starts moving while seen is reclassified on that very sighting (every sighting is verified).
        Rig m; place(w, 20, 0, 60); m.settle(3, w); place(w2, 20.2, 0, 60); m.draw(3, w2); m.end(); f = m.next();
        CHECK(f.reclassified == 1 && f.reclassified_after_unseen == 0 && !m.store->nodes[m.store->find_node(3)].is_static);
        r.end(); f = r.next(); CHECK(f.moving_dropped == 1);
        // ... and one resubmitted where it was counts `same`
        Rig q; q.settle(3, w); for (unsigned i = 0; i < 70; ++i) { q.end(); q.next(); }
        q.pose.yaw = .4; q.pose.pos[2] = -30; q.draw(3, w); q.end(); f = q.next();
        CHECK(f.reclassified == 0 && q.store->totals.same[1] == 1 && f.drift_n == 1 && f.drift_max < .05f && q.store->nodes[q.store->find_node(3)].is_static);
    }
    { // LOD swap on a stable serial: the new draw set replaces the old one
        Rig r; place(w, 20, 0, 60);
        for (unsigned i = 0; i < 10; ++i) { r.draw(4, w, 100); r.draw(4, w, 110); r.end(); r.next(); }
        CHECK(r.store->draws_used == 2 && r.held() == 5);
        r.draw(4, w, 200, 1); r.end(); auto f = r.next();
        CHECK(f.lod_replaced == 1 && f.superseded == 1 && r.store->draws_used == 1 && r.held() == 3 && r.store->nodes[r.store->find_node(4)].lod == 1);
    }
    { // shared mesh: one reference per resource; retiring one node leaves the other's record
        Rig r; place(w, 20, 0, 60); place(w2, -40, 10, 90);
        for (unsigned i = 0; i < 10; ++i) { r.draw(5, w); r.draw(6, w2); r.end(); r.next(); }
        CHECK(r.store->references() == 3 && r.held() == 3 && r.store->draws_used == 2);
        r.end(); CHECK(r.store->admitted_count == 2); r.next();
        CHECK(r.store->retire(5, r.frame)); r.release(); CHECK(r.held() == 3 && r.store->nodes_used == 1);
        r.end(); CHECK(r.store->admitted_count == 1); auto f = r.next(); CHECK(f.retired == 1);
        CHECK(r.store->retire(6, r.frame)); r.release(); CHECK(r.held() == 0 && r.store->references() == 0 && r.store->totals.expired_retired[0] == 2);
        CHECK(!r.store->retire(6, r.frame));
    }
    { // revalidation, flush, the deferred epoch flush, abandoned sightings
        Rig r; place(w, 20, 0, 60); place(w2, -40, 10, 90);
        for (unsigned i = 0; i < 10; ++i) { r.draw(7, w); r.draw(8, w2, 300); r.end(); r.next(); }
        r.store->revalidate([](const sr::Node& n) { return n.serial != 7 ? sr::Revalidation::Known : sr::Revalidation::Dead; }, r.frame); r.release();
        CHECK(r.store->nodes_used == 1 && r.store->find_node(7) == sr::none && r.store->frame.revalidated == 2 && r.held() == 3 && r.store->frame.retired == 1);
        // A re-acquire before the owner's Release reuses the owed reference: no AddRef, no Release.
        const int before = r.held();
        r.store->retire(8, r.frame); CHECK(r.store->pending_count == 3 && r.held() == before);
        r.draw(8, w2, 300); CHECK(r.store->pending_count == 0 && r.held() == before && r.store->references() == 3);
        r.release(); CHECK(r.held() == before);
        r.end(); r.next(); r.store->retire(8, r.frame); r.release(); CHECK(r.held() == 0 && r.store->references() == 0);
        // A lost context is its own reason.
        r.settle(8, w2, 10, 300);
        r.store->revalidate([](const sr::Node&) { return sr::Revalidation::ContextLost; }, r.frame); r.release();
        CHECK(r.store->nodes_used == 0 && r.store->frame.revalidate_context_lost == 1 && r.held() == 0);
        r.settle(8, w2, 10, 300); CHECK(r.held() == 3);
        CHECK(r.draw(9, w, 400, 0, 0, 0, 2) == sr::Seen::Deferred && r.held() == 3); // another load epoch: nothing released at the draw
        r.end(); auto f = r.next(); CHECK(f.flush == sr::Flush::Epoch && r.store->nodes_used == 0 && r.held() == 0);
        r.draw(9, w, 400, 0, 0, 0, 2); CHECK(r.store->nodes_used == 1); r.next(); // no scene end
        r.store->abandon_sightings(); r.release(); CHECK(r.store->nodes_used == 0 && r.held() == 0);
        r.settle(9, w, 10, 400); r.store->flush(sr::Flush::Reset); r.release(); CHECK(r.held() == 0 && r.store->nodes_used == 0 && r.store->draws_used == 0 && r.store->totals.flushes[unsigned(sr::Flush::Reset)] == 1);
    }
    { // a rewritten held buffer: a record that would be issued is checked every frame and never issued; one outside every cascade within 8 frames
        Rig r; place(w, 20, 0, 60); r.settle(10, w);
        r.changed_vb = 100; r.end(); const auto f = r.next();
        CHECK(f.admitted_checked == 1 && f.buffer_changed == 1 && r.store->admitted_count == 0 && r.store->nodes_used == 0 && r.held() == 0 && f.nodes_unseen == 0 && f.records == 0);
        Rig far_rig; place(w, 20, 0, 60); far_rig.settle(10, w); far_rig.pose.pos[0] = 4000; // outside both cascades, inside 2 x the outer box: not issued, round-robin checked
        far_rig.changed_vb = 100; unsigned frames = 0;
        while (far_rig.store->nodes_used && frames < 20) { far_rig.end(); far_rig.next(); ++frames; }
        CHECK(frames >= 1 && frames <= 8 && far_rig.store->totals.buffer_changed == 1 && far_rig.held() == 0);
        // seen with another revision: the record stays, counted
        Rig q; q.settle(10, w); q.draw(10, w, 100, 0, 0, 0, 1, 2); q.end(); const auto g = q.next(); CHECK(g.buffer_changed == 1 && q.store->draws_used == 1);
    }
    { // the age cap
        Rig r; r.age_cap = 5; place(w, 20, 0, 60); r.settle(11, w);
        for (unsigned i = 0; i < 5; ++i) { r.end(); CHECK(r.store->nodes_used == 1); r.next(); }
        r.end(); auto f = r.next(); CHECK(f.age == 1 && r.store->nodes_used == 0 && r.held() == 0);
    }
    { // under a cascade's cap the nearest retained records stay
        Rig r; place(w, 20, 0, 60); place(w2, 20, 0, 120);
        for (unsigned i = 0; i < 10; ++i) { r.draw(12, w, 100); r.draw(13, w2, 300); r.end(); r.next(); }
        r.room[0] = 1; r.end(); auto f = r.next();
        CHECK(f.would[0] == 2 && f.capped[0] == 1 && f.would[1] == 2 && f.capped[1] == 0 && r.store->admitted_count == 2);
        for (unsigned q = 0; q < r.store->admitted_count; ++q) { const auto& d = r.store->draws[r.store->admitted[q]]; CHECK(d.cascades == (d.key.vb == 100 ? 3 : 2)); }
        r.room[0] = r.room[1] = 0; r.end(); CHECK(r.store->admitted_count == 0); r.next();
    }
    { // per-cascade suns: a retained record's mask is taken against each cascade's own current basis
        Rig r; renderer::ShadowReplayBasis shared{};
        CHECK(renderer::shadow_replay_basis(camera(r.pose), sun, r.set.cascades[1], shared));
        const double* right = shared.axes[0]; // 2,000 units along the shared basis' x axis: outside cascade 1's 1,500-unit box
        place(w, 2000 * right[0], 2000 * right[1], 2000 * right[2]); r.settle(15, w);
        r.end(); CHECK(r.store->nodes_used == 1 && r.store->admitted_count == 0); auto f = r.next(); CHECK(f.would[0] == 0 && f.would[1] == 0);
        // Cascade 1 lit from -x of that basis: the node now lies on its light axis, 2,000 units behind the centre, inside its box.
        const float other[4] = {float(-right[0]), float(-right[1]), float(-right[2]), 0.f};
        r.outer_sun = other; r.end(); CHECK(r.store->admitted_count == 1 && r.store->draws[r.store->admitted[0]].cascades == 2);
        f = r.next(); CHECK(f.would[0] == 0 && f.would[1] == 1);
        r.outer_sun = nullptr; r.end(); CHECK(r.store->admitted_count == 0); r.next();
    }
    { // excluded classes are never retained
        Rig r; place(w, 20, 0, 60);
        const std::uint32_t bits[4] = {0x20, 0x200, 0x4000, 0x10000000};
        for (std::uint32_t bit : bits) CHECK(r.draw(14, w, 100, 0, bit) == sr::Seen::Excluded);
        CHECK(r.draw(14, w, 100, 0, 0, 0x200) == sr::Seen::Excluded && r.store->nodes_used == 0 && r.store->frame.excluded_class == 5 && r.held() == 0);
        CHECK(r.draw(14, w, 100, 0, 0x800 | 0x40000) == sr::Seen::New);
    }
    { // capacity: the 1,025th node evicts the farthest unseen one; a node seen this frame is never evicted
        Rig r(false);
        for (unsigned f = 0; f < 10; ++f) { for (unsigned i = 0; i < sr::node_capacity; ++i) { place(w, 5. * (i % 32), 0, 50. + 8. * (i / 32)); r.draw(1000 + i, w, 5000 + 2 * i); } r.end(); r.next(); }
        CHECK(r.store->nodes_used == sr::node_capacity); // every node seen: nothing to evict into the reserve
        r.end(); auto f = r.next(); // all unseen: the scene end evicts the node_reserve farthest ones, never at a draw
        CHECK(f.evicted == sr::node_reserve && f.nodes_unseen == sr::node_capacity - sr::node_reserve);
        std::uint16_t farthest = r.store->find_node(1000 + 31 + 31 * 32); // the far corner of the grid
        CHECK(farthest == sr::none && r.store->find_node(1000) != sr::none);
        place(w, 0, 0, 40); CHECK(r.draw(5000, w, 9000) == sr::Seen::New && r.store->nodes_used == sr::node_capacity - sr::node_reserve + 1 && r.store->frame.evicted == 0);
        r.end(); f = r.next(); CHECK(f.evicted == 1 && r.store->nodes_used == sr::node_capacity - sr::node_reserve); // the reserve refilled once, at the scene end
        Rig q(false);
        for (unsigned i = 0; i < sr::node_capacity; ++i) { place(w, 5. * (i % 32), 0, 50. + 8. * (i / 32)); q.draw(1000 + i, w, 5000 + 2 * i); }
        place(w, 0, 0, 40); CHECK(q.draw(5000, w, 9000) == sr::Seen::Refused && q.store->frame.refused == 1 && q.store->frame.evicted == 0);
    }
    { // the precision twin: one static node recovered from unrelated cameras 81,000 units out
        std::mt19937 rng(7); std::uniform_real_distribution<double> unit(-1., 1.);
        double worst = 0;
        for (unsigned n = 0; n < 2000; ++n) {
            const double base[3] = {55962, 20286, 55517};
            float world[12]; place(world, base[0] + 3000 * unit(rng), base[1] + 3000 * unit(rng), base[2] + 3000 * unit(rng), 3 * unit(rng));
            double recovered[2][12];
            for (unsigned k = 0; k < 2; ++k) {
                const Pose p{3.1 * unit(rng), 1.2 * unit(rng), {base[0] + 2000 * unit(rng), base[1] + 2000 * unit(rng), base[2] + 2000 * unit(rng)}};
                float rows[16]; const auto c = camera(p); clip_rows(c, world, rows);
                CHECK(sr::world_rows(c, rows, recovered[k]));
            }
            const float lo[3] = {-300, -300, -300}, hi[3] = {300, 300, 300};
            const double d = renderer::sqrt_sd(sr::drift2(recovered[0], recovered[1], lo, hi));
            if (d > worst) worst = d;
        }
        std::printf("PRECISION worst=%.6f eps=%.6f\n", worst, sr::eps_default);
        CHECK(worst <= sr::eps_default);
    }
    { // the index map against std::map
        auto map = std::make_unique<sr::IndexMap<1024>>(); std::map<std::uint64_t, std::uint16_t> model; std::vector<std::uint64_t> keys(1024, 0);
        std::mt19937_64 rng(11);
        const auto key_of = [&](std::uint16_t i) { return keys[i]; };
        for (unsigned step = 0; step < 200000; ++step) {
            const std::uint16_t slot = std::uint16_t(rng() % 1024);
            if (keys[slot]) { map->erase(keys[slot], slot, key_of); model.erase(keys[slot]); keys[slot] = 0; }
            else { keys[slot] = (rng() % 4096) * 2048 + 1 + slot; map->insert(keys[slot], slot); model[keys[slot]] = slot; }
            if (step % 97 == 0) for (const auto& e : model) if (map->find(e.first, key_of) != e.second) { CHECK(false); step = 200000; break; }
        }
        CHECK(map->find(12345678901ull, key_of) == sr::none);
    }
    std::printf("RESULT %s failures=%d\n", failures ? "FAIL" : "PASS", failures);
    return failures != 0;
}
'''


class Core(unittest.TestCase):
    def test_store(self):
        compiler = shutil.which('c++') or shutil.which('clang++') or shutil.which('g++')
        if compiler is None:
            self.skipTest('no host C++ compiler')
        with tempfile.TemporaryDirectory(prefix='x3-shadow-retention-') as directory:
            work = Path(directory)
            (work / 'driver.cpp').write_text(DRIVER)
            subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'), str(work / 'driver.cpp'), '-o', str(work / 'driver')], check=True)
            result = subprocess.run([str(work / 'driver')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout[-3000:] + result.stderr[-2000:])
            self.assertIn('RESULT PASS failures=0', result.stdout)


def frame_line(**overrides):
    values = {k: 0 for k in retention.FRAME_FIELDS}
    values.update(device=1, frame=10, mode='census', known=1, flush='none', drift_p99='0', drift_max='0', us='12.5', journal_us='0.4', walk_us='11.0', draw_us='0.0')
    values.update(overrides)
    return 'shadow_retention_frame ' + ' '.join(f'{k}={values[k]}' for k in retention.FRAME_FIELDS)


def resight_line(frame=300, **overrides):
    values = {k: 0 for k in retention.RESIGHT_FIELDS}
    values.update(device=1, frame=frame); values.update(overrides)
    return 'shadow_retention_resight ' + ' '.join(f'{k}={values[k]}' for k in retention.RESIGHT_FIELDS)


class Lines(unittest.TestCase):
    def test_frame_and_summary(self):
        text = '\n'.join([frame_line(frame=1, nodes_live=3, static=2, moving=1, records=9, drift_n=2, drift_p99='0.004', drift_max='0.006', new_nodes=3),
                          frame_line(frame=2, nodes_live=1, nodes_unseen=2, static=3, records=9, records_unseen=6, would_c0=4, capped_c0=1, unseen_outside=2, age_max=1, retired=1, cam_jump=1),
                          'shadow_retention_caster device=1 frame=2 handle=9 serial=44 class=static unseen=1 cascades=3',
                          'shadow_replay_caster device=1 frame=2 record=0 vb=5 cascades=3 retained=0', 'shadow_replay_caster device=1 frame=2 record=1 vb=6 cascades=1 retained=1',
                          resight_line(300, b0_same=40, b1_same=50, b1_moved=1, b2_same=10, b2_changed=3, expired_retired_b0=2)])
        frames, resights, casters, flushes, replayed = retention.parse_text(text)
        self.assertEqual((len(frames), len(resights), len(casters), flushes, replayed), (2, 1, 1, [], {'0': 1, '1': 1}))
        result = retention.summary(frames, resights)
        self.assertEqual(result['levels']['nodes_unseen'], 2); self.assertEqual(result['would_peak'][0], 4); self.assertEqual(result['totals']['retired'], 1)
        self.assertEqual(result['drift']['max'], 0.006); self.assertEqual(result['drift']['frames_over_eps'], 0)
        self.assertEqual(result['resight']['age_cap_below_bucket'], '<600')  # 1 of 50 moved is 2 %: not negligible
        self.assertEqual(result['resight']['expired']['retired'][0], 2)
        self.assertEqual(result['totals']['admitted_checked'], 0)

    def test_malformed(self):
        for line in (frame_line().replace(' known=1', ''), frame_line(mode='other'), frame_line(flush='maybe'), frame_line(nodes_live='x'),
                     frame_line(nodes_live=2, static=1), frame_line(records=1, records_unseen=2), frame_line(would_c1=1, capped_c1=2),
                     frame_line(refs_held=1), frame_line(mode='live', nodes_live=1025, static=1025), frame_line() + ' extra=1', frame_line(drift_max='0.5'), frame_line(flush='lost')):
            with self.assertRaises(retention.MalformedLine, msg=line):
                retention.parse_frame_line(line)
        self.assertEqual(retention.parse_frame_line(frame_line(mode='live', refs_held=12, orphan_probe=1, buffer_orphaned=1, flush='idle'))['refs_held'], 12)
        with self.assertRaises(retention.MalformedLine):
            retention.parse_resight_line(resight_line().replace(' b0_moved=0', ''))

    def test_depth_line_tail(self):
        base = 'shadow_replay_depth device=1 frame=4 replayed=2 skipped_lease=0 skipped_state=0 skipped_caps=0 draws=2 us=50.0 draws0=3 draws1=2 far_replayed=1 far_frame=4 issues=5 budget=640'
        row = depth_replay.parse_depth_line(base + ' replayed_live0=2 replayed_retained0=1 replayed_live1=2 replayed_retained1=0')
        self.assertEqual(row['retention'], {'live': [2, 2], 'retained': [1, 0]})
        self.assertNotIn('retention', depth_replay.parse_depth_line(base))
        empty = 'shadow_replay_depth device=1 frame=4 replayed=0 skipped_lease=0 skipped_state=0 skipped_caps=0 draws=0 us=50.0 draws0=1 far_replayed=0 far_frame=-1 issues=1 budget=640 replayed_live0=0 replayed_retained0=1'
        self.assertEqual(depth_replay.parse_depth_line(empty)['retention']['retained'], [1])
        for bad in (base + ' replayed_live0=2 replayed_retained0=2 replayed_live1=2 replayed_retained1=0', base + ' replayed_live0=3', base + ' replayed_retained0=1 replayed_live0=2 replayed_live1=2 replayed_retained1=0'):
            with self.assertRaises(depth_replay.MalformedLine, msg=bad):
                depth_replay.parse_depth_line(bad)


def launch(directory, *args, inherited=None):
    spec = importlib.util.spec_from_file_location('retention_manage', ROOT / 'tools/manage.py')
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
    BASE = ['--motion-output', '--ownership', '--shadow-replay-depth', '--shadow-cascades', 'default']

    def test_default_off_and_values(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = launch(directory, *self.BASE, inherited={'X3M_SHADOW_CASTER_RETENTION': '1', 'X3M_SHADOW_RETENTION_CENSUS': '1', 'X3M_SHADOW_CASTER_RETENTION_AGE': '9',
                                                                          'X3M_SHADOW_CASTER_RETENTION_EPS': '9', 'X3M_SHADOW_RETENTION_TIMING': '1'})
            self.assertEqual(code, 0, error); env = json.loads(output)['env']
            self.assertEqual((env['X3M_SHADOW_CASTER_RETENTION'], env['X3M_SHADOW_RETENTION_CENSUS']), ('0', '0'))
            for name in ('X3M_SHADOW_CASTER_RETENTION_AGE', 'X3M_SHADOW_CASTER_RETENTION_EPS', 'X3M_SHADOW_RETENTION_TIMING'):
                self.assertNotIn(name, env)
            code, output, error = launch(directory, *self.BASE, '--shadow-retention-census', '--shadow-caster-retention-age', '3600', '--shadow-caster-retention-eps', '0.1')
            self.assertEqual(code, 0, error); env = json.loads(output)['env']
            self.assertEqual((env['X3M_SHADOW_RETENTION_CENSUS'], env['X3M_SHADOW_CASTER_RETENTION'], env['X3M_SHADOW_CASTER_RETENTION_AGE'], env['X3M_SHADOW_CASTER_RETENTION_EPS']), ('1', '0', '3600', '0.1'))
            code, output, error = launch(directory, *self.BASE, '--shadow-caster-retention', '--shadow-retention-timing')
            self.assertEqual(code, 0, error); env = json.loads(output)['env']
            self.assertEqual((env['X3M_SHADOW_CASTER_RETENTION'], env['X3M_SHADOW_RETENTION_TIMING']), ('1', '1'))

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            for option in ('--shadow-retention-census', '--shadow-caster-retention'):
                code, _, error = launch(directory, '--motion-output', '--ownership', '--shadow-replay-depth', option)
                self.assertNotEqual(code, 0); self.assertIn('require --shadow-cascades', error)
            for option in (('--shadow-caster-retention-age', '100'), ('--shadow-retention-timing',)):
                code, _, error = launch(directory, *self.BASE, *option)
                self.assertNotEqual(code, 0, option); self.assertIn('require --shadow-retention-census or --shadow-caster-retention', error)
            for option, value, message in (('--shadow-caster-retention-age', '0', 'must be within [1, 10000000]'), ('--shadow-caster-retention-eps', '0', 'must be within [0.0001, 100]'),
                                           ('--shadow-caster-retention-eps', 'nan', 'must be within [0.0001, 100]')):
                code, _, error = launch(directory, *self.BASE, '--shadow-caster-retention', option, value)
                self.assertNotEqual(code, 0, (option, value)); self.assertIn(message, error)


if __name__ == '__main__':
    unittest.main()
