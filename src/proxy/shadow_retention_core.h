#pragma once
// Sun-shadow caster retention by node: the store
// (docs/architecture/shadow-caster-retention.md, stages 1 and 2).
// Pure CPU bookkeeping on fixed storage: no allocation, no D3D, no COM, no
// lock. The owner (motion_output_shadow_retention_inc.h) feeds one seen() per
// recorded caster draw and one end_scene() per scene end, performs the native
// AddRef the store asks for at the draw (Acquired) and the Releases it queues
// (pending), and consumes the admitted list in the cascade transaction.
// Identity is the node serial of the lifetime observer; VB/IB/range is payload.
// No libm call the i686 build would route through x87 (no fabs, floor, sqrt).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "../renderer/shadow_replay_projection.h"

namespace x3m::shadow_retention {
constexpr unsigned node_capacity = 1024, draw_capacity = 4096, resource_capacity = 1024; // powers of two
constexpr unsigned static_sightings = 8;  // consecutive sightings within eps before a record counts static
constexpr unsigned node_reserve = 8, draw_reserve = 64; // headroom kept free by scene-end eviction, so the draw site never scans the table
constexpr unsigned check_period = 8;      // an unseen node's buffers and a resource's orphan probe: one slice in 8 per frame
constexpr unsigned transit_window = 60, resight_period = 300, resight_buckets = 5;
constexpr double eps_default = .05, eps_min = 1e-4, eps_max = 100.;
constexpr std::uint32_t age_cap_default = 7200, age_cap_min = 1, age_cap_max = 10000000;
constexpr double first_seen_range = .8; // of the outermost half-extent: a node first sighted nearer the eye than this did not enter through the box edge
constexpr std::uint16_t none = 0xFFFF;
// Camera-facing and screen nodes (shadow-caster-lifetime.md, section 2): never retained.
constexpr std::uint32_t excluded_flags12c = 0x20u | 0x200u | 0x4000u | 0x10000000u, excluded_flags130 = 0x200u;
constexpr bool excluded_class(std::uint32_t flags12c, std::uint32_t flags130) noexcept { return (flags12c & excluded_flags12c) || (flags130 & excluded_flags130); }

enum class Mode : std::uint8_t { Off = 0, Census = 1, Live = 2 };
enum class Flush : std::uint8_t { None = 0, Epoch, Reset, Device, Teardown, Sun, Observer, Idle, Count };
constexpr unsigned idle_flush_frames = 300; // presented frames without a scene end (menus, loading) before the store is flushed
constexpr const char* flush_name(Flush f) noexcept {
    switch (f) { case Flush::Epoch: return "epoch"; case Flush::Reset: return "reset"; case Flush::Device: return "device"; case Flush::Teardown: return "teardown";
                 case Flush::Sun: return "sun"; case Flush::Observer: return "observer"; case Flush::Idle: return "idle"; default: return "none"; }
}
enum class Expiry : std::uint8_t { Retired, BoxExit, Age, Evicted, BufferChanged, BufferGone, BufferOrphaned, Moving, Flushed, Abandoned, Empty, ContextLost };
// The owner's verdict on one node during a full revalidation.
enum class Revalidation : std::uint8_t { Known = 0, Dead = 1, ContextLost = 2 }; // ContextLost: the node's recorded registry or camera is gone; the node cannot be confirmed
enum class Seen : std::uint8_t { Excluded, Deferred, Refused, Known, New };
enum class BufferState : std::uint8_t { Quiet = 0, Changed = 1, Gone = 2 };

inline double abs_d(double v) noexcept { return v < 0. ? -v : v; }
inline unsigned age_bucket(std::uint64_t frames) noexcept { return frames < 60 ? 0u : frames < 600 ? 1u : frames < 3600 ? 2u : frames < 14400 ? 3u : 4u; }

// Pool index by 64-bit key: linear probing over twice the pool, backward-shift deletion.
template <unsigned Capacity> struct IndexMap {
    static constexpr unsigned slots = Capacity * 2;
    std::uint16_t table[slots];
    IndexMap() noexcept { clear(); }
    void clear() noexcept { std::memset(table, 0xFF, sizeof table); }
    static unsigned home(std::uint64_t key) noexcept {
        key ^= key >> 33; key *= 0xFF51AFD7ED558CCDull; key ^= key >> 33;
        return unsigned(key) & (slots - 1);
    }
    template <class KeyOf> std::uint16_t find(std::uint64_t key, KeyOf key_of) const noexcept {
        for (unsigned i = home(key), n = 0; n < slots; i = (i + 1) & (slots - 1), ++n) {
            if (table[i] == none) return none;
            if (key_of(table[i]) == key) return table[i];
        }
        return none;
    }
    void insert(std::uint64_t key, std::uint16_t index) noexcept {
        for (unsigned i = home(key), n = 0; n < slots; i = (i + 1) & (slots - 1), ++n) if (table[i] == none) { table[i] = index; return; }
    }
    template <class KeyOf> void erase(std::uint64_t key, std::uint16_t index, KeyOf key_of) noexcept {
        unsigned i = home(key), n = 0;
        for (; n < slots && table[i] != index; i = (i + 1) & (slots - 1), ++n) if (table[i] == none) return;
        if (n == slots) return;
        for (unsigned j = i;;) {
            j = (j + 1) & (slots - 1);
            if (table[j] == none) break;
            const unsigned k = home(key_of(table[j]));
            if (i <= j ? (i < k && k <= j) : (i < k || k <= j)) continue;
            table[i] = table[j]; i = j;
        }
        table[i] = none;
    }
};

struct DrawKey {
    std::uint64_t vb = 0, ib = 0, declaration = 0; // route allocation ids
    std::uint32_t stream_offset = 0, stride = 0, topology = 0, primitives = 0, first = 0, min_vertex = 0, vertex_count = 0;
    std::int32_t base_vertex = 0;
    std::uint32_t indexed = 0;
    bool operator==(const DrawKey& o) const noexcept {
        return vb == o.vb && ib == o.ib && declaration == o.declaration && stream_offset == o.stream_offset && stride == o.stride && topology == o.topology
            && primitives == o.primitives && first == o.first && min_vertex == o.min_vertex && vertex_count == o.vertex_count && base_vertex == o.base_vertex && indexed == o.indexed;
    }
};
// A buffer as the draw saw it: the wrapper identity (held by the store's own
// reference in live mode; a registry key only in census mode) and the
// buffer-lock view's allocation, generation and revision.
struct BufferStamp {
    std::uintptr_t identity = 0; std::uint64_t allocation = 0, generation = 0, revision = 0;
    bool same_object(const BufferStamp& o) const noexcept { return identity == o.identity && allocation == o.allocation && generation == o.generation; }
};
struct Draw {
    DrawKey key{};
    BufferStamp vb{}, ib{};
    std::uintptr_t declaration = 0; // the declaration interface (live mode; its own held reference)
    std::uint32_t cull_mode = 0;
    float rows[16]{};     // the submitted clip rows of the last sighting
    double world[12]{};   // object -> world (W . A), double
    double centre[3]{};   // world AABB
    float half[3]{}, lo[3]{}, hi[3]{};
    std::uint64_t stamp = 0; // frame of the last sighting
    std::uint16_t next = none, node = none, resource[3] = {none, none, none};
    std::uint8_t streak = 0, cascades = 0; // cascades: this frame's mask while the node is unseen
    bool used = false, world_valid = false, extent_known = false, bounds_dirty = false;
    bool complete() const noexcept { return world_valid && extent_known; }
};
struct Node {
    std::uint64_t serial = 0, last_seen = 0, first_seen = 0, unseen_before = 0;
    std::uintptr_t node = 0, registry = 0, camera = 0; // the scope of the last sighting: the full revalidation's arguments
    std::uint32_t handle = 0, camera_handle = 0, model = 0, lod = 0, flags12c = 0;
    double centre[3]{}; float half[3]{};
    std::uint16_t head = none, tail = none, cursor = none, draws = 0;
    bool used = false, is_static = false, dirty = false, fresh = false, changed = false, lod_changed = false, model_changed = false;
    bool partial = false, pre_transit = false, bounds_valid = false;
};
// owed: no record names it; one Release is owed to the owner (pop_owed). single_node: the one node whose
// records name it while uses were only ever taken by that node (none once shared), so an orphan drop
// walks that node's records instead of the draw pool.
struct Resource { std::uintptr_t identity = 0; std::uint32_t uses = 0; bool used = false, owed = false; std::uint16_t single_node = none; };

struct Sighting {
    std::uint64_t serial = 0, load_epoch = 0, registry_epoch = 0, observer_epoch = 0;
    std::uintptr_t node = 0, registry = 0, camera = 0;
    std::uint32_t handle = 0, camera_handle = 0, model = 0, lod = 0, flags12c = 0, flags130 = 0;
    DrawKey key{};
    BufferStamp vb{}, ib{};
    std::uintptr_t declaration = 0;
    std::uint32_t cull_mode = 0;
    const float* rows = nullptr;                    // 16 floats
    const float* lo = nullptr; const float* hi = nullptr; // the range's own extent of this revision, when known
};
// Resources the owner must AddRef now (at the draw, while the application's binding is live).
struct Acquired { std::uintptr_t identity[3]{}; unsigned count = 0; };

// Per-frame counts of the shadow_retention_frame line; reset by the owner after the line.
struct FrameStats {
    std::uint32_t excluded_class = 0, unscoped = 0, new_nodes = 0, first_seen_in_range = 0, promoted = 0;
    std::uint32_t superseded = 0, lod_replaced = 0, model_replaced = 0, reclassified = 0;
    std::uint32_t retired = 0, journal_overflow = 0, revalidated = 0;
    std::uint64_t mutation_delta = 0;
    std::uint32_t buffer_changed = 0, buffer_gone = 0, buffer_orphaned = 0;
    std::uint32_t box_exit = 0, age = 0, evicted = 0;
    Flush flush = Flush::None;
    std::uint32_t unseen_in_frustum = 0, unseen_outside = 0;
    std::uint32_t would[renderer::shadow_cascade_max]{}, capped[renderer::shadow_cascade_max]{};
    std::uint32_t drift_n = 0; float drift_p99 = 0, drift_max = 0;
    std::uint32_t sun_relatch = 0, cam_jump = 0, transit_survivors = 0;
    std::uint32_t far_alternate_due_to_retained = 0; // the far cascade skipped by the budget this frame only because of retained issues (live issues alone fit)
    std::uint32_t revalidate_context_lost = 0;       // nodes a full revalidation could not confirm because their recorded registry or camera is gone
    std::uint32_t release_queue_full = 0;            // acquisitions refused while owed references still hold the resource table (diagnostic; the queue itself cannot drop a reference)
    std::uint32_t reclassified_after_unseen = 0;     // a retained node resubmitted beyond eps of its stored rows: the moved-while-unseen residual, observed
    std::uint32_t admitted_checked = 0;              // retained records whose buffers were checked before issue this frame
    std::uint32_t refused = 0, moving_dropped = 0, abandoned = 0, deferred = 0; // beyond the contract's list: capacity refusals, moving nodes dropped unseen, sightings of a frame without a scene end, draws ignored while a flush is pending
    std::uint32_t nodes_live = 0, nodes_unseen = 0, records = 0, records_unseen = 0, statics = 0, moving = 0; // levels
    std::uint64_t age_max = 0;
};
// Cumulative since attach: the shadow_retention_resight line and the fixture's checks.
struct Totals {
    std::uint64_t same[resight_buckets]{}, moved[resight_buckets]{}, changed[resight_buckets]{};
    std::uint64_t expired_retired[resight_buckets]{}, expired_box[resight_buckets]{}, expired_gone[resight_buckets]{};
    std::uint64_t retired = 0, box_exit = 0, age = 0, evicted = 0, buffer_changed = 0, buffer_gone = 0, buffer_orphaned = 0, reclassified = 0;
    std::uint64_t lod_replaced = 0, model_replaced = 0, moving_dropped = 0, revalidated = 0, journal_overflow = 0, refused = 0, promoted = 0;
    std::uint64_t revalidate_context_lost = 0, far_alternate_due_to_retained = 0, reclassified_after_unseen = 0;
    std::uint64_t flushes[unsigned(Flush::Count)]{};
};
struct FrameInput {
    std::uint64_t frame = 0;
    renderer::CameraState camera{};                       // the scene's latch (valid or not)
    bool bases_valid = false;                             // the frame has a usable sun and every cascade basis
    renderer::ShadowReplayBasis bases[renderer::shadow_cascade_max]{};
    renderer::ShadowCascadeSet set{};
    unsigned room[renderer::shadow_cascade_max]{};        // per cascade: cap minus this frame's live records
    double eps = eps_default;
    std::uint32_t age_cap = age_cap_default;
};

// Object -> world rows (W . A) of a draw: clip = rows . pos; view = (clip.x / m00,
// clip.y / m11, clip.w); world_i = sum_j (view_j - t_j) r[i*3+j]. Double from the
// float latch, the product shadow_cascade_draw_rows folds into the sun axes.
inline bool world_rows(const renderer::CameraState& camera, const float rows[16], double out[12]) noexcept {
    if (!camera.valid || !rows || !out || !(camera.m00 > 0.f) || !(camera.m11 > 0.f)) return false;
    for (unsigned i = 0; i < 16; ++i) if (!std::isfinite(rows[i])) return false;
    for (unsigned i = 0; i < 3; ++i) for (unsigned k = 0; k < 4; ++k) {
        const double vx = double(rows[k]) / camera.m00, vy = double(rows[4 + k]) / camera.m11, vz = double(rows[12 + k]);
        double m = vx * double(camera.r[i * 3]) + vy * double(camera.r[i * 3 + 1]) + vz * double(camera.r[i * 3 + 2]);
        if (k == 3) for (unsigned j = 0; j < 3; ++j) m -= double(camera.t[j]) * double(camera.r[i * 3 + j]);
        if (!std::isfinite(m)) return false;
        out[i * 4 + k] = m;
    }
    return true;
}
// The retained rows folded into the frame's shared sun axes: what
// shadow_cascade_light_rows scales into one cascade. No camera enters.
inline void sun_rows(const double world[12], const renderer::ShadowReplayBasis& basis, double base[3][4]) noexcept {
    for (unsigned a = 0; a < 3; ++a) for (unsigned k = 0; k < 4; ++k)
        base[a][k] = basis.axes[a][0] * world[k] + basis.axes[a][1] * world[4 + k] + basis.axes[a][2] * world[8 + k];
}
inline void camera_position(const renderer::CameraState& camera, double out[3]) noexcept {
    for (unsigned i = 0; i < 3; ++i) { out[i] = 0; for (unsigned j = 0; j < 3; ++j) out[i] -= double(camera.t[j]) * double(camera.r[i * 3 + j]); }
}
// Largest displacement of the object AABB's corners between two placements, squared.
inline double drift2(const double a[12], const double b[12], const float lo[3], const float hi[3]) noexcept {
    double worst = 0;
    for (unsigned corner = 0; corner < 8; ++corner) {
        const double p[3] = {double((corner & 1) ? hi[0] : lo[0]), double((corner & 2) ? hi[1] : lo[1]), double((corner & 4) ? hi[2] : lo[2])};
        double d2 = 0;
        for (unsigned i = 0; i < 3; ++i) {
            const double d = (a[i * 4] - b[i * 4]) * p[0] + (a[i * 4 + 1] - b[i * 4 + 1]) * p[1] + (a[i * 4 + 2] - b[i * 4 + 2]) * p[2] + (a[i * 4 + 3] - b[i * 4 + 3]);
            d2 += d * d;
        }
        if (d2 > worst) worst = d2;
    }
    return worst;
}
inline void world_bounds(const double world[12], const float lo[3], const float hi[3], double centre[3], float half[3]) noexcept {
    for (unsigned i = 0; i < 3; ++i) {
        double c = world[i * 4 + 3], h = 0;
        for (unsigned j = 0; j < 3; ++j) {
            const double mid = .5 * (double(lo[j]) + double(hi[j])), ext = .5 * (double(hi[j]) - double(lo[j]));
            c += world[i * 4 + j] * mid; h += abs_d(world[i * 4 + j]) * ext;
        }
        centre[i] = c; half[i] = float(h);
    }
}

struct Store {
    Node nodes[node_capacity];
    Draw draws[draw_capacity];
    Resource resources[resource_capacity];
    IndexMap<node_capacity> node_map;
    IndexMap<resource_capacity> resource_map;
    std::uint16_t node_free[node_capacity], draw_free[draw_capacity], resource_free[resource_capacity];
    unsigned node_free_count = 0, draw_free_count = 0, resource_free_count = 0;
    std::uint16_t dirty[node_capacity]; unsigned dirty_count = 0;
    unsigned pending_count = 0; // resource slots owed one Release by the owner (pop_owed)
    std::uint16_t admitted[draw_capacity]; unsigned admitted_count = 0;   // this frame's unseen records with a cascade (draw indices)
    float scratch_key[draw_capacity]; std::uint16_t scratch_index[draw_capacity];
    float drift_samples[node_capacity]; unsigned drift_count = 0;
    std::uint64_t load_epoch = 0, registry_epoch = 0, observer_epoch = 0;
    bool epochs_set = false, hold = false, eye_valid = false;
    Flush flush_pending = Flush::None;
    double eye[3]{};
    std::uint64_t transit_frame = 0; bool transit_open = false;
    unsigned nodes_used = 0, draws_used = 0, resources_used = 0;
    FrameStats frame{};
    Totals totals{};

    Store() noexcept { reset_storage(); }
    // `hold_references`: live mode (records name held resources); census mode holds none.
    void configure(bool hold_references) noexcept { hold = hold_references; }
    void reset_storage() noexcept {
        for (auto& n : nodes) n = Node{};
        for (auto& d : draws) d = Draw{};
        for (auto& r : resources) r = Resource{};
        node_map.clear(); resource_map.clear();
        for (unsigned i = 0; i < node_capacity; ++i) node_free[i] = std::uint16_t(node_capacity - 1 - i);
        for (unsigned i = 0; i < draw_capacity; ++i) draw_free[i] = std::uint16_t(draw_capacity - 1 - i);
        for (unsigned i = 0; i < resource_capacity; ++i) resource_free[i] = std::uint16_t(resource_capacity - 1 - i);
        node_free_count = node_capacity; draw_free_count = draw_capacity; resource_free_count = resource_capacity;
        dirty_count = pending_count = admitted_count = drift_count = 0; nodes_used = draws_used = resources_used = 0;
        epochs_set = false; flush_pending = Flush::None; transit_open = false;
    }
    std::uint16_t find_node(std::uint64_t serial) const noexcept { return node_map.find(serial, [this](std::uint16_t i) { return nodes[i].serial; }); }
    unsigned references() const noexcept { return resources_used; }

    // ---- the seen path (per recorded caster draw) ------------------------------
    Seen seen(const Sighting& s, std::uint64_t now, Acquired& acquired) noexcept {
        acquired.count = 0;
        if (excluded_class(s.flags12c, s.flags130)) { ++frame.excluded_class; return Seen::Excluded; }
        if (flush_pending != Flush::None) { ++frame.deferred; return Seen::Deferred; }
        if (!epochs_set || !nodes_used) { load_epoch = s.load_epoch; registry_epoch = s.registry_epoch; observer_epoch = s.observer_epoch; epochs_set = true; }
        else if (load_epoch != s.load_epoch || registry_epoch != s.registry_epoch || observer_epoch != s.observer_epoch) {
            // The whole store goes at this frame's scene end, before the replay; nothing is released at a draw.
            flush_pending = Flush::Epoch; ++frame.deferred; return Seen::Deferred;
        }
        std::uint16_t index = find_node(s.serial);
        if (index == none) {
            if (!node_free_count) { count_refused(); return Seen::Refused; } // the scene end evicts into the reserve; this node is recorded from its next sighting
            index = node_free[--node_free_count]; ++nodes_used;
            Node& n = nodes[index]; n = Node{};
            n.used = true; n.fresh = true; n.serial = s.serial; n.node = s.node; n.handle = s.handle; n.model = s.model; n.lod = s.lod; n.flags12c = s.flags12c;
            n.registry = s.registry; n.camera = s.camera; n.camera_handle = s.camera_handle;
            n.first_seen = now; n.last_seen = ~std::uint64_t(0);
            node_map.insert(s.serial, index); ++frame.new_nodes;
        }
        Node& n = nodes[index];
        if (n.last_seen != now) { // the first draw of this sighting
            n.unseen_before = n.fresh ? 0 : now - n.last_seen - 1; n.last_seen = now; n.cursor = n.head; n.partial = false;
            if (!n.fresh) { n.model_changed = n.model != s.model; n.lod_changed = n.lod != s.lod; }
            n.model = s.model; n.lod = s.lod; n.node = s.node; n.handle = s.handle; n.flags12c = s.flags12c;
            n.registry = s.registry; n.camera = s.camera; n.camera_handle = s.camera_handle;
            if (!n.dirty) { n.dirty = true; if (dirty_count < node_capacity) dirty[dirty_count++] = index; }
        }
        const auto matches = [&](const Draw& d) noexcept { return d.stamp != now && d.key == s.key && d.vb.same_object(s.vb) && d.ib.same_object(s.ib) && (!hold || d.declaration == s.declaration); };
        std::uint16_t found = none;
        if (n.cursor != none && matches(draws[n.cursor])) found = n.cursor;
        else for (std::uint16_t i = n.head; i != none; i = draws[i].next) if (matches(draws[i])) { found = i; break; }
        if (found != none) {
            Draw& d = draws[found];
            d.stamp = now; d.cascades = 0; n.cursor = d.next; d.cull_mode = s.cull_mode;
            std::memcpy(d.rows, s.rows, sizeof d.rows);
            const bool rewritten = d.vb.revision != s.vb.revision || d.ib.revision != s.ib.revision;
            if (rewritten) { ++frame.buffer_changed; ++totals.buffer_changed; d.vb.revision = s.vb.revision; d.ib.revision = s.ib.revision; d.streak = 0; if (!s.lo) d.extent_known = false; }
            if (s.lo && s.hi) set_extent(d, s.lo, s.hi);
            return Seen::Known;
        }
        if (!draw_free_count) { n.partial = true; count_refused(); return Seen::Refused; }
        const std::uint16_t slot = draw_free[--draw_free_count]; ++draws_used;
        Draw& d = draws[slot]; d = Draw{};
        d.used = true; d.key = s.key; d.vb = s.vb; d.ib = s.ib; d.cull_mode = s.cull_mode; d.stamp = now; d.node = index;
        std::memcpy(d.rows, s.rows, sizeof d.rows);
        if (s.lo && s.hi) set_extent(d, s.lo, s.hi);
        if (hold) {
            d.declaration = s.declaration;
            const std::uintptr_t wanted[3] = {s.vb.identity, s.key.indexed ? s.ib.identity : 0, s.declaration};
            bool ok = wanted[0] != 0 && wanted[2] != 0 && (!s.key.indexed || wanted[1] != 0);
            for (unsigned i = 0; ok && i < 3; ++i) if (wanted[i]) ok = acquire(wanted[i], d.resource[i], acquired, index);
            if (!ok) { // table full or an identity missing: nothing was AddRef'd yet, so the fresh entries simply leave
                for (unsigned i = 0; i < 3; ++i) if (d.resource[i] != none) unacquire(d.resource[i], acquired);
                acquired.count = 0; d = Draw{}; draw_free[draw_free_count++] = slot; --draws_used;
                n.partial = true; count_refused(); return Seen::Refused;
            }
        }
        if (n.tail == none) n.head = slot; else draws[n.tail].next = slot;
        n.tail = slot; ++n.draws;
        if (!n.fresh) n.changed = true;
        return Seen::New;
    }
    void set_eye(const renderer::CameraState& camera) noexcept { eye_valid = camera.valid; if (camera.valid) camera_position(camera, eye); }

    // ---- lifetime events ------------------------------------------------------
    bool retire(std::uint64_t serial, std::uint64_t now) noexcept {
        const std::uint16_t index = find_node(serial);
        if (index == none) return false;
        remove_node(index, Expiry::Retired, now);
        return true;
    }
    void flush(Flush reason) noexcept {
        for (unsigned i = 0; i < node_capacity; ++i) if (nodes[i].used) remove_node(std::uint16_t(i), Expiry::Flushed, 0);
        dirty_count = admitted_count = 0; epochs_set = false; flush_pending = Flush::None; transit_open = false;
        frame.flush = reason; ++totals.flushes[unsigned(reason)];
    }
    // Full revalidation through the owner: every node is confirmed with its own
    // recorded registry, node and camera identity; Dead leaves as retired, a
    // lost context leaves too (fail closed) under its own count.
    template <class Verdict> void revalidate(Verdict verdict, std::uint64_t now) noexcept {
        for (unsigned i = 0; i < node_capacity; ++i) {
            if (!nodes[i].used) continue;
            ++frame.revalidated; ++totals.revalidated;
            const Revalidation v = verdict(static_cast<const Node&>(nodes[i]));
            if (v == Revalidation::Dead) remove_node(std::uint16_t(i), Expiry::Retired, now);
            else if (v == Revalidation::ContextLost) remove_node(std::uint16_t(i), Expiry::ContextLost, now);
        }
    }
    // The store's reference is the last one on this resource: every node naming it goes
    // (O(its records) while one node owns it; the draw pool is walked only for a shared mesh).
    void drop_resource(std::uint16_t slot, std::uint64_t now) noexcept {
        const std::uint16_t owner = resources[slot].single_node;
        if (owner != none) { if (nodes[owner].used) remove_node(owner, Expiry::BufferOrphaned, now); return; }
        for (unsigned i = 0; i < draw_capacity; ++i) {
            const Draw& d = draws[i];
            if (!d.used || (d.resource[0] != slot && d.resource[1] != slot && d.resource[2] != slot)) continue;
            remove_node(d.node, Expiry::BufferOrphaned, now);
        }
    }
    // A frame that never reached a scene end: its sightings hold rows of a
    // camera latch that is gone, so those nodes leave.
    void abandon_sightings() noexcept {
        for (unsigned i = 0; i < dirty_count; ++i) if (nodes[dirty[i]].used && nodes[dirty[i]].dirty) remove_node(dirty[i], Expiry::Abandoned, 0);
        dirty_count = 0; admitted_count = 0;
    }

    // ---- the scene end -----------------------------------------------------------
    // `check(draw)`: the buffer-lock verdict of an unseen record (the owner's registry lookups).
    template <class Check> void end_scene(const FrameInput& in, Check check) noexcept {
        admitted_count = 0; drift_count = 0;
        if (flush_pending != Flush::None) flush(flush_pending);
        const bool jumped = note_camera(in);
        // Capacity: the reserve is refilled here, once per frame, never at a draw (farthest unseen nodes first).
        for (unsigned guard = 0; guard < node_reserve && (node_free_count < node_reserve || draw_free_count < draw_reserve); ++guard) if (!evict_farthest(in.frame)) break;
        finalize_seen(in);
        walk_unseen(in, check);
        apply_caps(in);
        if (jumped) { transit_frame = in.frame; transit_open = true; for (auto& n : nodes) n.pre_transit = n.used; }
        else if (transit_open && in.frame >= transit_frame + transit_window) {
            for (auto& n : nodes) { if (n.used && n.pre_transit) ++frame.transit_survivors; n.pre_transit = false; }
            transit_open = false;
        }
        if (drift_count) {
            const unsigned k = (drift_count * 99u) / 100u < drift_count ? (drift_count * 99u) / 100u : drift_count - 1;
            std::nth_element(drift_samples, drift_samples + k, drift_samples + drift_count);
            frame.drift_p99 = drift_samples[k];
            frame.drift_n = drift_count;
        }
    }

private:
    void count_refused() noexcept { ++frame.refused; ++totals.refused; }
    static void set_extent(Draw& d, const float lo[3], const float hi[3]) noexcept {
        if (d.extent_known && !std::memcmp(d.lo, lo, sizeof d.lo) && !std::memcmp(d.hi, hi, sizeof d.hi)) return;
        std::memcpy(d.lo, lo, sizeof d.lo); std::memcpy(d.hi, hi, sizeof d.hi); d.extent_known = true; d.bounds_dirty = true;
    }
    bool acquire(std::uintptr_t identity, std::uint16_t& slot, Acquired& acquired, std::uint16_t node) noexcept {
        const std::uint16_t known = resource_map.find(identity, [this](std::uint16_t i) { return std::uint64_t(resources[i].identity); });
        if (known != none) {
            Resource& r = resources[known];
            if (r.owed && !r.uses) { r.owed = false; --pending_count; r.single_node = node; } // the reference still held is reused: no Release owed, no AddRef needed
            else if (r.single_node != node) r.single_node = none;
            ++r.uses; slot = known; return true;
        }
        if (!resource_free_count) { if (pending_count) ++frame.release_queue_full; return false; } // owed slots hold the table until the owner releases them
        slot = resource_free[--resource_free_count]; ++resources_used;
        resources[slot] = Resource{identity, 1, true, false, node};
        resource_map.insert(identity, slot);
        acquired.identity[acquired.count++] = identity;
        return true;
    }
    // Rolls one acquire back before the owner acted on it (no Release owed for a fresh entry).
    void unacquire(std::uint16_t slot, const Acquired& acquired) noexcept {
        Resource& r = resources[slot];
        if (--r.uses) return;
        bool fresh = false;
        for (unsigned i = 0; i < acquired.count; ++i) fresh |= acquired.identity[i] == r.identity;
        free_resource(slot, !fresh);
    }
    // A resource no record names any more. Owed: the held reference stays in the
    // table (still mapped, so a re-acquire before the owner's Release reuses it)
    // until pop_owed hands it to the owner. The queue is the table itself, so it
    // cannot fill: every owed reference is released.
    void free_resource(std::uint16_t slot, bool owed) noexcept {
        Resource& r = resources[slot];
        if (owed) { r.owed = true; ++pending_count; return; }
        resource_map.erase(r.identity, slot, [this](std::uint16_t i) { return std::uint64_t(resources[i].identity); });
        r = Resource{}; resource_free[resource_free_count++] = slot; --resources_used;
    }
public:
    // The next identity the owner owes one Release (0 when none); the slot is freed as it is handed out.
    std::uintptr_t pop_owed() noexcept {
        for (unsigned n = 0; pending_count && n < resource_capacity; ++n) {
            const unsigned slot = (owed_cursor_ + n) % resource_capacity;
            Resource& r = resources[slot];
            if (!r.used || !r.owed || r.uses) continue;
            const std::uintptr_t identity = r.identity;
            r.owed = false; --pending_count;
            free_resource(std::uint16_t(slot), false);
            owed_cursor_ = (slot + 1) % resource_capacity;
            return identity;
        }
        pending_count = 0; // nothing owed remains (defensive: the count and the flags agree by construction)
        return 0;
    }
private:
    unsigned owed_cursor_ = 0;
    void free_draw(std::uint16_t slot) noexcept {
        Draw& d = draws[slot];
        for (unsigned i = 0; i < 3; ++i) if (d.resource[i] != none && !--resources[d.resource[i]].uses) free_resource(d.resource[i], true);
        d = Draw{}; draw_free[draw_free_count++] = slot; --draws_used;
    }
    void remove_node(std::uint16_t index, Expiry why, std::uint64_t now) noexcept {
        Node& n = nodes[index];
        if (!n.used) return;
        const bool unseen_static = n.is_static && now && n.last_seen != now && !n.fresh;
        const unsigned bucket = unseen_static ? age_bucket(now - n.last_seen) : 0;
        switch (why) {
        case Expiry::Retired: ++frame.retired; ++totals.retired; if (unseen_static) ++totals.expired_retired[bucket]; break;
        case Expiry::BoxExit: ++frame.box_exit; ++totals.box_exit; if (unseen_static) ++totals.expired_box[bucket]; break;
        case Expiry::Age: ++frame.age; ++totals.age; break;
        case Expiry::Evicted: ++frame.evicted; ++totals.evicted; break;
        case Expiry::BufferChanged: ++frame.buffer_changed; ++totals.buffer_changed; if (unseen_static) ++totals.expired_gone[bucket]; break;
        case Expiry::BufferGone: ++frame.buffer_gone; ++totals.buffer_gone; if (unseen_static) ++totals.expired_gone[bucket]; break;
        case Expiry::BufferOrphaned: ++frame.buffer_orphaned; ++totals.buffer_orphaned; if (unseen_static) ++totals.expired_gone[bucket]; break;
        case Expiry::Moving: ++frame.moving_dropped; ++totals.moving_dropped; break;
        case Expiry::Abandoned: ++frame.abandoned; break;
        case Expiry::ContextLost: ++frame.revalidate_context_lost; ++totals.revalidate_context_lost; break;
        default: break;
        }
        for (std::uint16_t i = n.head; i != none;) { const std::uint16_t next = draws[i].next; free_draw(i); i = next; }
        node_map.erase(n.serial, index, [this](std::uint16_t i) { return nodes[i].serial; });
        n = Node{}; node_free[node_free_count++] = index; --nodes_used;
    }
    // Capacity: the farthest unseen node leaves; a node seen this frame never does.
    bool evict_farthest(std::uint64_t now) noexcept {
        std::uint16_t victim = none; double worst = -1;
        for (unsigned i = 0; i < node_capacity; ++i) {
            const Node& n = nodes[i];
            if (!n.used || n.last_seen == now || n.dirty) continue;
            double d2 = 1e300;
            if (n.bounds_valid && eye_valid) { d2 = 0; for (unsigned k = 0; k < 3; ++k) { const double d = n.centre[k] - eye[k]; d2 += d * d; } }
            if (d2 > worst) { worst = d2; victim = std::uint16_t(i); }
        }
        if (victim == none) return false;
        remove_node(victim, Expiry::Evicted, now);
        return true;
    }
    bool note_camera(const FrameInput& in) noexcept {
        if (!in.camera.valid) return false;
        double now_eye[3]; camera_position(in.camera, now_eye);
        bool jumped = false;
        if (eye_known_ && in.set.count) {
            double d2 = 0; for (unsigned k = 0; k < 3; ++k) { const double d = now_eye[k] - last_eye_[k]; d2 += d * d; }
            const double limit = 2. * double(in.set.cascades[in.set.count - 1].half_extent);
            jumped = d2 > limit * limit;
        }
        std::memcpy(last_eye_, now_eye, sizeof last_eye_); eye_known_ = true;
        std::memcpy(eye, now_eye, sizeof eye); eye_valid = true;
        if (jumped) ++frame.cam_jump;
        return jumped;
    }
    void finalize_seen(const FrameInput& in) noexcept {
        const double eps2 = in.eps * in.eps;
        for (unsigned q = 0; q < dirty_count; ++q) {
            const std::uint16_t index = dirty[q];
            Node& n = nodes[index];
            if (!n.used || !n.dirty) continue;
            n.dirty = false;
            if (n.last_seen != in.frame) { remove_node(index, Expiry::Abandoned, 0); continue; }
            // A node submitted this frame supersedes its whole retained set.
            for (std::uint16_t i = n.head, previous = none; i != none;) {
                const std::uint16_t next = draws[i].next;
                if (draws[i].stamp == in.frame) { previous = i; i = next; continue; }
                if (previous == none) n.head = next; else draws[previous].next = next;
                if (n.tail == i) n.tail = previous;
                free_draw(i); --n.draws; n.changed = true; i = next;
            }
            if (!n.draws) { remove_node(index, Expiry::Empty, 0); continue; }
            const bool set_changed = !n.fresh && (n.changed || n.lod_changed || n.model_changed);
            if (set_changed) ++frame.superseded;
            if (!n.fresh && n.lod_changed) { ++frame.lod_replaced; ++totals.lod_replaced; }
            if (!n.fresh && n.model_changed) { ++frame.model_replaced; ++totals.model_replaced; }
            const bool was_static = n.is_static, resight = was_static && n.unseen_before > 0;
            // Every sighting is verified (about 0.15 us per record): a node that starts moving while seen is
            // reclassified on that sighting, not up to 15 sightings later. One that starts moving while
            // unseen is the accepted residual, bounded by age_cap; its resighting counts reclassified_after_unseen.
            const bool verify = true;
            bool moved = false, all_static = !n.partial;
            double worst = 0;
            for (std::uint16_t i = n.head; i != none; i = draws[i].next) {
                Draw& d = draws[i];
                if (verify) {
                    double w[12];
                    if (!world_rows(in.camera, d.rows, w)) { d.streak = 0; if (!d.world_valid) { all_static = false; continue; } }
                    else if (d.world_valid) {
                        const float zero[3] = {0, 0, 0};
                        const double d2 = drift2(w, d.world, d.extent_known ? d.lo : zero, d.extent_known ? d.hi : zero);
                        if (d2 > worst) worst = d2;
                        if (d2 > eps2) { moved = true; std::memcpy(d.world, w, sizeof w); d.streak = 0; d.bounds_dirty = true; }
                        else if (d.streak < static_sightings) ++d.streak;
                    } else { std::memcpy(d.world, w, sizeof w); d.world_valid = true; d.streak = 0; d.bounds_dirty = true; }
                }
                if (d.bounds_dirty && d.complete()) { world_bounds(d.world, d.lo, d.hi, d.centre, d.half); d.bounds_dirty = false; n.bounds_valid = false; }
                all_static &= d.complete() && d.streak >= static_sightings;
            }
            if (!n.bounds_valid) node_bounds(n);
            if (verify && was_static && drift_count < node_capacity) {
                const float units = float(renderer::sqrt_sd(worst));
                drift_samples[drift_count++] = units;
                if (units > frame.drift_max) frame.drift_max = units;
            }
            if (was_static && moved) { ++frame.reclassified; ++totals.reclassified; if (n.unseen_before) { ++frame.reclassified_after_unseen; ++totals.reclassified_after_unseen; } }
            n.is_static = all_static && !moved;
            if (!was_static && n.is_static) { ++frame.promoted; ++totals.promoted; }
            if (resight) {
                const unsigned bucket = age_bucket(n.unseen_before);
                if (set_changed) ++totals.changed[bucket]; else if (moved) ++totals.moved[bucket]; else ++totals.same[bucket];
            }
            if (n.fresh && n.bounds_valid && eye_valid && in.set.count) {
                double d2 = 0; for (unsigned k = 0; k < 3; ++k) { const double d = n.centre[k] - eye[k]; d2 += d * d; }
                const double range = first_seen_range * double(in.set.cascades[in.set.count - 1].half_extent);
                if (d2 < range * range) ++frame.first_seen_in_range;
            }
            n.fresh = n.changed = n.lod_changed = n.model_changed = false;
        }
        dirty_count = 0;
    }
    void node_bounds(Node& n) noexcept {
        double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
        bool any = false;
        for (std::uint16_t i = n.head; i != none; i = draws[i].next) {
            const Draw& d = draws[i];
            if (!d.complete()) continue;
            any = true;
            for (unsigned k = 0; k < 3; ++k) { const double a = d.centre[k] - double(d.half[k]), b = d.centre[k] + double(d.half[k]); if (a < lo[k]) lo[k] = a; if (b > hi[k]) hi[k] = b; }
        }
        n.bounds_valid = any;
        if (any) for (unsigned k = 0; k < 3; ++k) { n.centre[k] = .5 * (lo[k] + hi[k]); n.half[k] = float(.5 * (hi[k] - lo[k])); }
    }
    // Sun-space interval of a world AABB along axis `a` of the shared basis.
    static void sun_interval(const renderer::ShadowReplayBasis& basis, unsigned a, const double centre[3], const float half[3], double& mid, double& radius) noexcept {
        mid = radius = 0;
        for (unsigned j = 0; j < 3; ++j) { mid += basis.axes[a][j] * centre[j]; radius += abs_d(basis.axes[a][j]) * double(half[j]); }
    }
public:
    // The world AABB wholly inside the view frustum (its bounding sphere against the four side planes and the eye plane).
    static bool inside_frustum(const renderer::CameraState& c, const double centre[3], const float half[3]) noexcept {
        double v[3], w[3];
        const double radius = renderer::sqrt_sd(double(half[0]) * half[0] + double(half[1]) * half[1] + double(half[2]) * half[2]);
        for (unsigned j = 0; j < 3; ++j) { w[j] = 0; for (unsigned i = 0; i < 3; ++i) w[j] += centre[i] * double(c.r[i * 3 + j]); v[j] = w[j] + double(c.t[j]); }
        if (v[2] - radius <= 0.) return false;
        const double m00 = double(c.m00), m11 = double(c.m11);
        const double side_x = (v[2] - abs_d(v[0]) * m00) / renderer::sqrt_sd(1. + m00 * m00), side_y = (v[2] - abs_d(v[1]) * m11) / renderer::sqrt_sd(1. + m11 * m11);
        return side_x >= radius && side_y >= radius;
    }
private:
    template <class Check> void walk_unseen(const FrameInput& in, Check check) noexcept {
        const unsigned cascades = in.bases_valid ? in.set.count : 0;
        double centre_sun[renderer::shadow_cascade_max][3]{};
        bool shared_axes[renderer::shadow_cascade_max] = {true, true, true, true}; // cascade c has cascade 0's axes (always, under the latched sun)
        for (unsigned c = 1; c < cascades; ++c) shared_axes[c] = renderer::shadow_replay_axes_equal(in.bases[c], in.bases[0]);
        for (unsigned c = 0; c < cascades; ++c) for (unsigned a = 0; a < 3; ++a) { double m = 0; for (unsigned j = 0; j < 3; ++j) m += in.bases[c].axes[a][j] * in.bases[c].center_d[j]; centre_sun[c][a] = m; }
        for (unsigned index = 0; index < node_capacity; ++index) {
            Node& n = nodes[index];
            if (!n.used) continue;
            if (n.last_seen == in.frame) { ++frame.nodes_live; frame.records += n.draws; if (n.is_static) ++frame.statics; else ++frame.moving; continue; }
            // Unseen. Only static nodes outlive the frame they were last seen.
            if (!n.is_static || !n.bounds_valid) { remove_node(std::uint16_t(index), Expiry::Moving, in.frame); continue; }
            const std::uint64_t age = in.frame - n.last_seen;
            if (age > in.age_cap) { remove_node(std::uint16_t(index), Expiry::Age, in.frame); continue; }
            if (cascades) { // box exit: outside 2 x the outermost cascade box about its snapped centre
                const auto& outer = in.set.cascades[cascades - 1];
                const double e = 2. * double(outer.half_extent), towards = 2. * double(outer.depth_toward_light), behind = 2. * double(outer.depth_behind);
                bool outside = false;
                for (unsigned a = 0; a < 3 && !outside; ++a) {
                    double mid, radius; sun_interval(in.bases[cascades - 1], a, n.centre, n.half, mid, radius);
                    const double s = mid - centre_sun[cascades - 1][a];
                    outside = a < 2 ? (s - radius > e || s + radius < -e) : (s - radius > behind || s + radius < -towards);
                }
                if (outside) { remove_node(std::uint16_t(index), Expiry::BoxExit, in.frame); continue; }
            }
            if ((index + in.frame) % check_period == 0) {
                BufferState worst = BufferState::Quiet;
                for (std::uint16_t i = n.head; i != none && worst != BufferState::Gone; i = draws[i].next) { const BufferState s = check(static_cast<const Draw&>(draws[i])); if (unsigned(s) > unsigned(worst)) worst = s; }
                if (worst != BufferState::Quiet) { remove_node(std::uint16_t(index), worst == BufferState::Gone ? Expiry::BufferGone : Expiry::BufferChanged, in.frame); continue; }
            }
            if (cascades) {
                // Every record that may be issued this frame is checked every frame (the revision compare
                // the live loop's bookends make): a buffer the application re-Locked is never replayed
                // with the old range or declaration.
                BufferState issue_state = BufferState::Quiet;
                for (std::uint16_t i = n.head; i != none && issue_state == BufferState::Quiet; i = draws[i].next) {
                    if (!draws[i].complete()) continue;
                    ++frame.admitted_checked;
                    issue_state = check(static_cast<const Draw&>(draws[i]));
                }
                if (issue_state != BufferState::Quiet) { remove_node(std::uint16_t(index), issue_state == BufferState::Gone ? Expiry::BufferGone : Expiry::BufferChanged, in.frame); continue; }
            }
            ++frame.nodes_unseen; ++frame.statics; frame.records += n.draws; frame.records_unseen += n.draws;
            if (age > frame.age_max) frame.age_max = age;
            if (in.camera.valid) { if (inside_frustum(in.camera, n.centre, n.half)) ++frame.unseen_in_frustum; else ++frame.unseen_outside; }
            if (!cascades) continue;
            for (std::uint16_t i = n.head; i != none; i = draws[i].next) {
                Draw& d = draws[i];
                d.cascades = 0;
                if (!d.complete()) continue;
                // Each cascade against its own current basis: the cascades share their axes under the
                // latched sun and may each hold another direction under the positional sun.
                double mid[3], radius[3];
                for (unsigned c = 0; c < cascades; ++c) {
                    if (!c || !shared_axes[c]) for (unsigned a = 0; a < 3; ++a) sun_interval(in.bases[c], a, d.centre, d.half, mid[a], radius[a]);
                    else if (!shared_axes[c - 1]) for (unsigned a = 0; a < 3; ++a) sun_interval(in.bases[0], a, d.centre, d.half, mid[a], radius[a]);
                    const auto& box = in.set.cascades[c];
                    const double e = double(box.half_extent), x = mid[0] - centre_sun[c][0], y = mid[1] - centre_sun[c][1], z = mid[2] - centre_sun[c][2];
                    // As the draw-time mask: the light side is open (a caster nearer the light is pancaked).
                    if (x + radius[0] >= -e && x - radius[0] <= e && y + radius[1] >= -e && y - radius[1] <= e && z - radius[2] <= double(box.depth_behind)) { d.cascades |= std::uint8_t(1u << c); ++frame.would[c]; }
                }
                if (d.cascades && admitted_count < draw_capacity) admitted[admitted_count++] = i;
            }
        }
    }
    // Live records take priority under a cascade's cap; retained ones fill nearest first.
    void apply_caps(const FrameInput& in) noexcept {
        const unsigned cascades = in.bases_valid ? in.set.count : 0;
        for (unsigned c = 0; c < cascades; ++c) {
            if (frame.would[c] <= in.room[c]) continue;
            unsigned count = 0;
            for (unsigned q = 0; q < admitted_count; ++q) {
                const Draw& d = draws[admitted[q]];
                if (!(d.cascades & (1u << c))) continue;
                double d2 = 0; for (unsigned k = 0; k < 3; ++k) { const double v = d.centre[k] - eye[k]; d2 += v * v; }
                scratch_key[admitted[q]] = float(d2); scratch_index[count++] = admitted[q];
            }
            const unsigned keep = in.room[c];
            std::nth_element(scratch_index, scratch_index + keep, scratch_index + count, [this](std::uint16_t a, std::uint16_t b) { return scratch_key[a] < scratch_key[b] || (scratch_key[a] == scratch_key[b] && a < b); });
            for (unsigned q = keep; q < count; ++q) draws[scratch_index[q]].cascades &= std::uint8_t(~(1u << c));
            frame.capped[c] = count - keep;
        }
        unsigned kept = 0;
        for (unsigned q = 0; q < admitted_count; ++q) if (draws[admitted[q]].cascades) admitted[kept++] = admitted[q];
        admitted_count = kept;
    }
    double last_eye_[3]{}; bool eye_known_ = false;
};
} // namespace x3m::shadow_retention
