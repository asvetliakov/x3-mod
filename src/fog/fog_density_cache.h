// Stored-density fog: CPU cache manager and background worker
// (docs/architecture/fog-density-runtime-integration.md §1–3, checkpoint 3).
//
// Portable C++17, no D3D and no engine memory. One worker thread fills two
// toroidal CPU caches (atlas layout, storage = node mod 128) through the
// checkpoint-1 generator; the render thread posts the camera, copies committed
// tile regions into caller-supplied staging memory under a byte budget and
// advances the readiness ramps. The render-thread entry points never wait: every
// access to shared state is a try-lock, and a missed lock retries next frame.
//
// Residency is tracked as one node box per level. The worker grows its box one
// slab at a time (a slab spans the box's other two extents, so the union stays
// a box); a window retarget intersects the box with the new window before any
// slot is overwritten. A box reaches the render side only after every tile
// region committed for it has been handed out and confirmed uploaded.
#pragma once
#include "fog_density_generator.h"
#include "fog_handover.h"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

namespace x3m {
namespace fog {

// The horizon level's reach (docs/architecture/fog-visible-distance.md): the 3x cap (337,500) plus the sun-ward tap
// (3000) and the far level's warp margin (1900), whatever fog_distance draws: one need box for every cap above 1.7333.
constexpr double kHorizonReach = 342400.0;
// Ray reach per level in render units: fine is sampled to the end of the LOD
// blend, far to the end of the horizon taper (200,000: the far share of the long fade ends at 195,000 plus the tap),
// the horizon level to the 3x cap.
constexpr double kLevelReach[kMaxLevels] = {kLodEnd, kTaperEnd, kHorizonReach};
// Retarget the window when the camera node is this far from the window centre
// on any axis (plan: fine refill at .25 km of the .4512 km margin, far at 4 km
// of 11.61 km; horizon at 13.1 km: need half 43 nodes + guard 1 + 7 < 63).
constexpr int kRetargetNodes[kMaxLevels] = {2, 9, 8};
// The first fill covers the need box plus this many nodes, then grows to the window. The horizon level starts from a
// seed box around the camera node instead and grows by slabs at most kHorizonSlabNodes thick (below).
constexpr int kFirstFillSlack[kMaxLevels] = {2, 2, 2};
// Horizon slab thickness bound: a horizon slab is at most 128 x 128 x 2 = 32,768 nodes (about 14 ms of the worker at the
// measured 2.29 M nodes/s), no more than a fine retarget slab and a quarter of the far level's 9-node retarget slab. The
// worker re-plans after every slab.
constexpr int kHorizonSlabNodes = 2;
// Horizon lead: the horizon box is kept this many nodes beyond its need box (13.1 km), so one SETA frame of up to 10 km
// (a 2 s game step at 5 km/s) keeps the need box and its readiness guard resident. need half <= 43 + 8 stays inside the
// window up to the 8-node retarget (5 nodes to spare). Priority: (1) the far need (with the cold-fill hold); (2) a warm
// horizon box (it held need + lead once in this epoch) that lost its lead: one slab ahead of the fine need, and while
// the fine need is open the next slab is the fine level's, so the two alternate and the fine level waits at most one
// horizon slab (<= 32,768 nodes, about 14 ms) per fine slab; (3) the fine need; (4) the horizon need + lead (a cold
// horizon after the fine need: first fog after a jump is not delayed); (5) window growth, fine, far, horizon. The horizon
// box sweeps about 103^2 / 8192 nodes per unit of travel against the fine need box's 121^2 / 512 (about 4.5 %).
constexpr int kHorizonLead = 8;
// Readiness falls (ramps) when the need box grown by this guard leaves the
// resident box, and drops to zero at once when the need box itself does.
constexpr int kReadinessGuard = 1;
constexpr unsigned kReadinessRampFrames = 90;
constexpr int kTileCount = kGroupCount;
// Enforced cap of one take_uploads, first rectangle included. A budget below one tile
// (133,128 B, the largest single rectangle is 131,072 B) is raised to one tile so uploads
// cannot stall.
constexpr std::size_t kDefaultUploadBudget = 8 * kTileBytes; // 1,065,024 B per frame
// Cameras beyond this magnitude (render units) are refused: node keys stay inside int32.
constexpr double kCameraLimit = 1e12;
constexpr unsigned kDefaultUploadRects = 64; // UpdateSurface calls per frame
constexpr int kTileRegions = 6;

// Inclusive node box; empty when lo > hi on any axis.
struct NodeBox {
    std::int64_t lo[3], hi[3];
    bool empty() const noexcept { return lo[0] > hi[0] || lo[1] > hi[1] || lo[2] > hi[2]; }
    bool contains(const NodeBox& inner) const noexcept;
    bool operator==(const NodeBox& other) const noexcept;
    std::uint64_t nodes() const noexcept;
};
NodeBox empty_box() noexcept;
NodeBox universe_box() noexcept;
NodeBox intersect(const NodeBox& a, const NodeBox& b) noexcept;
NodeBox grow(const NodeBox& box, std::int64_t nodes) noexcept;
NodeBox window_box(const NodeKey& origin) noexcept;
// Every node a ray from `camera` can read at a level: both trilinear corners of
// every base node within the level's reach.
NodeBox need_box(int level, const double camera[3]) noexcept;
// Shader constant c22/c23.xyz: camera modulo 128*delta, centred (|x| <= 64*delta), from doubles.
void camera_local(int level, const double camera[3], float out[3]) noexcept;
// True when the camera node left the retarget band around the window centre.
bool retarget_needed(int level, const NodeKey& origin, const double camera[3]) noexcept;

// One generation unit: a node box inside a single 32×32×4 storage cell (one Z
// group, contiguous in storage on every axis), at most 4096 nodes.
constexpr std::size_t kJobNodes = std::size_t(kBrickTexels) * kBrickTexels * kLanes;
constexpr std::size_t kMaxJobs = 1024;
struct Job {
    NodeBox box;
    double distance2; // box centre to camera, squared, in nodes
};
// Splits `slab` at storage multiples of 32 (x, y) and 4 (z), nearest the camera
// first. Returns the job count, or 0 when `capacity` is too small.
std::size_t plan_jobs(const NodeBox& slab, int level, const double camera[3], Job* out, std::size_t capacity) noexcept;
// The next slab that grows `have` toward `want` (have ⊆ want, both non-empty):
// the side where `need` is not yet covered first, then x-, x+, y-, y+, z-, z+.
// Returns false when have == want.
bool next_slab(const NodeBox& have, const NodeBox& want, const NodeBox& need, NodeBox& slab) noexcept;

// Atlas texel rectangle of one tile region (includes duplicate border texels).
struct TileRect {
    int level, group;
    int x, y, width, height; // atlas texels
    std::size_t bytes() const noexcept { return std::size_t(width) * height * kTexelBytes; }
};

struct CacheIdentity {
    std::uint64_t sector_key = 0;
    std::uint32_t recipe = 0;
    WorldOffset offset = kNoOffset;
    bool operator==(const CacheIdentity& o) const noexcept {
        return sector_key == o.sector_key && recipe == o.recipe && offset.x == o.offset.x && offset.y == o.offset.y &&
               offset.z == o.offset.z;
    }
};

struct StagingView {
    std::uint8_t* bits = nullptr; // whole 1032×516 atlas; null skips the level
    std::size_t pitch = 0;
};

struct FrameState {
    float ready[kMaxLevels]{};    // 0..1 ramps: [0] fine (lambda), [1] far (density), [2] horizon (tail share; 0 off)
    float local[kMaxLevels][3]{}; // camera modulo 128*delta, centred
    bool resident[kMaxLevels]{};  // need box resident on the GPU this frame
};

struct CacheStats {
    std::uint64_t nodes_generated = 0, jobs = 0, slabs = 0, retargets = 0, first_fills = 0;
    std::uint64_t upload_bytes = 0, upload_rects = 0, missed_locks = 0;
    std::uint64_t worker_busy_us = 0; // generation time only
};

class DensityCache {
public:
    DensityCache() noexcept;
    ~DensityCache();
    DensityCache(const DensityCache&) = delete;
    DensityCache& operator=(const DensityCache&) = delete;

    // Allocates both caches and starts the worker. False leaves nothing running.
    bool start() noexcept;
    bool running() const noexcept { return running_; }
    // Joins the worker. Must not run under the loader lock (not from DllMain). If the shared
    // mutex cannot be taken within 250 ms (a worker killed while holding it), the worker is
    // abandoned instead of joined, so teardown cannot deadlock.
    void stop() noexcept;
    // Process termination, or stop()'s 250 ms give-up: the worker may already be gone and may
    // have died holding the mutex or inside the condition variable. Releases nothing, takes no
    // lock, does not notify, join or detach. An abandoned cache must be leaked whole, never
    // destroyed: a merely starved worker still uses it. Owners release a heap cache with retire().
    void abandon() noexcept;
    bool abandoned() const noexcept { return abandoned_; }
    // stop(), then delete unless the cache ended up abandoned (then it is leaked).
    static void retire(DensityCache* cache) noexcept;
    // The horizon level (docs/architecture/fog-visible-distance.md, step 2). Render thread, after start()/start_stepped()
    // (FogPass calls it once the distance programs exist): allocates and zeroes the level's 4,260,096 B CPU cache here
    // (one allocation, no lock); the worker sees it from the next post (step or take_uploads, under the shared mutex).
    // False when it cannot run: not started, abandoned, already dropped, or the allocation failed ("cpu_cache"; the cache
    // keeps its two levels). Priority as kHorizonLead describes; never under the cold-fill hold; its own readiness ramp
    // (FrameState::ready[2]); uploads share the byte budget after far and fine. Never enabled, the cache is the two-level
    // cache, bit for bit.
    bool enable_horizon() noexcept;
    bool horizon() const noexcept { return horizon_on_; }
    int levels() const noexcept { return horizon_on_ ? kMaxLevels : kLevelCount; }
    // Why the horizon level is off although it was asked for ("cpu_cache", or the reason given to drop_horizon); null
    // while it runs or when it was never asked for.
    const char* horizon_refused() const noexcept { return horizon_refused_; }
    // Render thread: give the horizon level up for the cache's lifetime (a GPU allocation or upload failure: fail closed
    // to the two-level cap). Its readiness is 0 from this call on and nothing of it is uploaded or counted as work; at the
    // next post (the next step or take_uploads that takes the mutex) the worker request drops it and its CPU cache is
    // freed under the mutex (a horizon job in flight finds no cache and discards its slab). Never waits.
    void drop_horizon(const char* reason) noexcept;
#ifdef X3M_FOG_DENSITY_TEST_HOOKS
    static void test_fail_horizon(bool fail) noexcept { test_fail_horizon_.store(fail); }
    static void test_fail_thread(bool fail) noexcept { test_fail_thread_.store(fail); }
    std::mutex& test_mutex() noexcept { return sync().mutex; }
#endif

    // Render thread. The cold-start switches (docs/architecture/fog-handover.md, "Implementation"),
    // read at the next cold start (configure with a new identity, or invalidate); both off is the
    // legacy behaviour. step: the far readiness steps to 1 in the frame the far need box becomes
    // resident instead of ramping over kReadinessRampFrames (warm refills keep the ramp).
    // cold_fill: the worker fills only the far need box, the far level goes up in one whole-atlas
    // latch past the byte budget, then the fine level and the growth continue under the budget.
    void set_handover(bool step, bool cold_fill) noexcept {
        handover_step_ = step;
        handover_cold_fill_ = cold_fill;
    }
    // The report of the last completed cold start, once (due), then cleared.
    HandoverReport take_handover() noexcept {
        HandoverReport r = report_;
        report_.due = false;
        return r;
    }
    // A changed identity is an invalidation.
    void configure(const CacheIdentity& identity) noexcept;
    // R3 prefill (fog-handover.md, "R3 implementation"): configure(identity) (a cold start even for the resident
    // identity), then post `camera` to the worker without advancing readiness or uploading. Never waits. True when
    // the camera reached the worker; false (not running, camera refused, or a missed lock) leaves the caller to
    // retry at its next poll.
    bool prefill(const CacheIdentity& identity, const double camera[3]) noexcept;
    // Sector change or load: drop everything, refill, ramp (or step under the hand-over). The worker parks until
    // the next step or prefill posts a camera for the new epoch; the previous epoch's camera is never filled around.
    void invalidate() noexcept;
    FrameState step(const double camera[3], std::uint64_t frame) noexcept;
    // Need box of `camera` resident on the GPU (the execute-time guard).
    bool covers(int level, const double camera[3]) const noexcept;

    // Upload protocol, render thread, once per frame when has_work():
    //   take_uploads (copies committed regions into staging) -> UpdateSurface each
    //   rect -> confirm_uploads(all succeeded).
    bool has_work() const noexcept;
    // Render thread: the worker has seen the last posted camera / identity and found nothing
    // left to generate, and everything it committed is uploaded and confirmed. has_work()
    // alone is false while the worker is still generating its next slab.
    bool idle() const noexcept;
    // During a cold fill the far level has nothing to hand out until a box is published (no staging lock per frame).
    bool level_dirty(int level) const noexcept {
        if (level == kHorizonLevel && !horizon_on_) return false;
        if (level == 1 && cold_latch_ && !publication_pending_.load(std::memory_order_relaxed)) return false;
        return dirty_tiles_[level].load(std::memory_order_relaxed) != 0 || reupload_[level];
    }
    // views[kHorizonLevel] is read only while horizon() is on: a two-level caller may pass two views.
    unsigned take_uploads(const StagingView views[kMaxLevels], std::size_t byte_budget, TileRect* out,
                          unsigned capacity) noexcept;
    void confirm_uploads(bool succeeded) noexcept;
    // The DEFAULT atlases were lost or recreated: nothing is resident until every
    // tile with committed content has been uploaded again from the CPU cache.
    void gpu_reset() noexcept;

    CacheStats stats() const noexcept;
    NodeBox gpu_box(int level) const noexcept { return gpu_box_[level]; }
    const CacheIdentity& identity() const noexcept { return identity_; }

    // Host tests: run the worker's scheduling on the calling thread, one slab
    // per call (start() must not have been called). Returns false when idle.
    bool start_stepped() noexcept;
    bool worker_step() noexcept;
    const std::uint8_t* cache_bytes(int level) const noexcept { return cache_[level]; }
    // Worker-owned: stepped mode, or after stop(), only.
    NodeKey worker_origin(int level) const noexcept { return worker_[level].origin; }
    NodeBox worker_box(int level) const noexcept { return worker_[level].box; }
    std::uint64_t level_nodes(int level) const noexcept { return level_nodes_[level].load(std::memory_order_relaxed); }

private:
    // Dirty regions of one tile: up to four rectangles of the 128×128 body (an x slab and
    // a y slab through the same tile stay two thin strips, each possibly split by the
    // storage wrap, instead of one whole-tile box), then the duplicate column 128 and the
    // duplicate row 128 with the corner. Body rectangles merge only when the union wastes
    // nothing; a fifth collapses the body to its bounding box.
    struct Region {
        int x0, y0, x1, y1;
        bool dirty;
    };
    struct TileDirty {
        Region region[kTileRegions];
        std::uint64_t first_seq;
        bool dirty, reload;
    };
    struct WorkerLevel {
        NodeKey origin{};
        NodeBox box;
    };
    struct SharedLevel {
        NodeBox box, shrink;
        std::uint64_t box_seq = 0, commit_seq = 0; // box_seq: commit_seq when `box` last grew
        TileDirty tiles[kTileCount];
    };
    bool allocate() noexcept;
    void run() noexcept;
    bool work_once(bool& idle) noexcept;
    bool fill_slab(int level, const NodeBox& slab, std::uint64_t epoch, const WorldOffset& offset,
                   const double camera[3]) noexcept;
    void commit_locked(int level, const NodeBox& job, const std::uint16_t* words) noexcept;
    void dirty_locked(int level, int group, int region, int x0, int y0, int x1, int y1) noexcept;
    void post_locked() noexcept;
    void apply_invalidate_locked() noexcept;
    void sync_locked() noexcept;

    // Mutex, condition variable and thread live in raw storage so that an abandoned cache never
    // runs their destructors (~condition_variable waits for a waiter the OS already killed).
    struct Sync {
        std::mutex mutex;
        std::condition_variable wake;
        std::thread thread;
    };
    alignas(Sync) mutable unsigned char sync_storage_[sizeof(Sync)];
    Sync& sync() const noexcept { return *reinterpret_cast<Sync*>(sync_storage_); }
#ifdef X3M_FOG_DENSITY_TEST_HOOKS
    static std::atomic<bool> test_fail_thread_, test_fail_horizon_;
#endif
    // Shared, guarded by sync().mutex.
    // cold_hold: a cold fill in progress; the worker generates the far need box only.
    struct Request {
        std::uint64_t epoch = 0, serial = 0;
        WorldOffset offset = kNoOffset;
        double camera[3]{};
        bool camera_valid = false, stop = false, cold_hold = false, horizon = false;
    } request_;
    SharedLevel shared_[kMaxLevels];
    std::uint8_t* cache_[kMaxLevels]{};
    // The epoch's far need box publication (steady-clock us) and the worker's busy / CPU time until then; -1 until.
    std::int64_t far_published_us_ = -1, far_fill_busy_us_ = -1, far_fill_cpu_us_ = -1;

    // Worker-owned.
    WorkerLevel worker_[kMaxLevels];
    std::uint64_t worker_epoch_ = 0;
    Job* jobs_ = nullptr;
    std::uint64_t worker_serial_ = 0;
    std::uint16_t* scratch_ = nullptr;
    bool worker_far_published_ = false;
    bool horizon_led_ = false;  // the horizon box held its need box + kHorizonLead in this epoch (warm)
    bool horizon_urgent_ = false; // the last slab was a warm horizon's urgent slab: the fine need goes next
    std::int64_t worker_busy_base_ = 0, worker_cpu_base_ = -1;

    // Render-thread-owned.
    CacheIdentity identity_{};
    NodeBox gpu_box_[kMaxLevels], candidate_box_[kMaxLevels];
    std::uint64_t candidate_seq_[kMaxLevels]{}, transferred_seq_[kMaxLevels]{};
    bool candidate_valid_[kMaxLevels]{}, resident_[kMaxLevels]{}, reupload_[kMaxLevels]{};
    // Horizon: running, and the value last posted to the worker (request_.horizon); a CPU cache allocated by
    // enable_horizon and installed in cache_[kHorizonLevel] at the next post. cache_[kHorizonLevel] is written by the
    // render thread under the mutex only and read by the worker under the mutex only (commit).
    bool horizon_on_ = false, horizon_posted_ = false;
    std::uint8_t* horizon_staged_ = nullptr;
    const char* horizon_refused_ = nullptr;
    bool horizon_post_due() const noexcept {
        return horizon_posted_ != horizon_on_ || horizon_staged_ || (!horizon_on_ && cache_[kHorizonLevel]);
    }
    bool pending_invalidate_ = false, pending_camera_ = false, running_ = false, stepped_ = false, abandoned_ = false,
         frame_primed_ = false;
    double camera_[3]{};
    NodeBox posted_need_[kMaxLevels];
    std::uint64_t last_frame_ = 0, posted_serial_ = 0;
    float ready_[kMaxLevels]{};
    unsigned reload_left_[kMaxLevels]{};
    // Cold start: cold_ from configure/invalidate until the far readiness reaches 1; cold_latch_ until the
    // whole-atlas latch (cold_fill only). pending_ is the report in progress, report_ the last completed one.
    bool handover_step_ = false, handover_cold_fill_ = false, cold_ = false, cold_step_ = false, cold_latch_ = false,
         cold_frame_pending_ = false, whole_in_flight_ = false;
    std::int64_t cold_at_us_ = 0, cold_busy_base_ = 0;
    HandoverReport pending_{}, report_{};

    // Cross-thread counters.
    std::atomic<unsigned> dirty_tiles_[kMaxLevels];
    std::atomic<bool> publication_pending_{false}, stop_flag_{false};
    std::atomic<std::uint64_t> idle_serial_{~std::uint64_t(0)}; // request serial the worker last went idle on
    std::atomic<std::uint64_t> nodes_generated_{0}, jobs_done_{0}, slabs_done_{0}, retargets_{0}, first_fills_{0},
        busy_us_{0};
    std::atomic<std::uint64_t> level_nodes_[kMaxLevels]{}; // nodes generated per level (host witnesses, the perf pass)
    std::uint64_t upload_bytes_ = 0, upload_rects_ = 0, missed_locks_ = 0;
};

} // namespace fog
} // namespace x3m
