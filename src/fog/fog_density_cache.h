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
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

namespace x3m {
namespace fog {

// Ray reach per level in render units: fine is sampled to the end of the LOD
// blend, far to the end of the horizon taper.
constexpr double kLevelReach[kLevelCount] = {kLodEnd, kTaperEnd};
// Retarget the window when the camera node is this far from the window centre
// on any axis (plan: fine refill at .25 km of the .4512 km margin, far at 4 km
// of 11.61 km).
constexpr int kRetargetNodes[kLevelCount] = {2, 9};
// The first fill covers the need box plus this many nodes, then grows to the window.
constexpr int kFirstFillSlack[kLevelCount] = {2, 2};
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
constexpr unsigned kDefaultUploadRects = 64;                 // UpdateSurface calls per frame
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
        return sector_key == o.sector_key && recipe == o.recipe && offset.x == o.offset.x && offset.y == o.offset.y && offset.z == o.offset.z;
    }
};

struct StagingView {
    std::uint8_t* bits = nullptr; // whole 1032×516 atlas; null skips the level
    std::size_t pitch = 0;
};

struct FrameState {
    float ready[kLevelCount]{};        // 0..1 ramps: [0] fine (lambda), [1] far (density)
    float local[kLevelCount][3]{};     // camera modulo 128*delta, centred
    bool resident[kLevelCount]{};      // need box resident on the GPU this frame
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
    // Process termination only: the worker may already be gone and may have died
    // holding the mutex. Releases nothing and takes no lock.
    void abandon() noexcept;

    // Render thread. A changed identity is an invalidation.
    void configure(const CacheIdentity& identity) noexcept;
    void invalidate() noexcept; // sector change or load: drop everything, refill, ramp
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
    bool level_dirty(int level) const noexcept { return dirty_tiles_[level].load(std::memory_order_relaxed) != 0 || reupload_[level]; }
    unsigned take_uploads(const StagingView views[kLevelCount], std::size_t byte_budget, TileRect* out, unsigned capacity) noexcept;
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

private:
    // Dirty regions of one tile: up to four rectangles of the 128×128 body (an x slab and
    // a y slab through the same tile stay two thin strips, each possibly split by the
    // storage wrap, instead of one whole-tile box), then the duplicate column 128 and the
    // duplicate row 128 with the corner. Body rectangles merge only when the union wastes
    // nothing; a fifth collapses the body to its bounding box.
    struct Region { int x0, y0, x1, y1; bool dirty; };
    struct TileDirty { Region region[kTileRegions]; std::uint64_t first_seq; bool dirty, reload; };
    struct WorkerLevel { NodeKey origin{}; NodeBox box; };
    struct SharedLevel {
        NodeBox box, shrink;
        std::uint64_t box_seq = 0, commit_seq = 0; // box_seq: commit_seq when `box` last grew
        TileDirty tiles[kTileCount];
    };
    bool allocate() noexcept;
    void run() noexcept;
    bool work_once(bool& idle) noexcept;
    bool fill_slab(int level, const NodeBox& slab, std::uint64_t epoch, const WorldOffset& offset, const double camera[3]) noexcept;
    void commit_locked(int level, const NodeBox& job, const std::uint16_t* words) noexcept;
    void dirty_locked(int level, int group, int region, int x0, int y0, int x1, int y1) noexcept;
    void post_locked() noexcept;
    void apply_invalidate_locked() noexcept;
    void sync_locked() noexcept;

    // Shared, guarded by mutex_.
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    struct Request { std::uint64_t epoch = 0, serial = 0; WorldOffset offset = kNoOffset; double camera[3]{}; bool camera_valid = false, stop = false; } request_;
    SharedLevel shared_[kLevelCount];
    std::uint8_t* cache_[kLevelCount]{};

    // Worker-owned.
    WorkerLevel worker_[kLevelCount];
    std::uint64_t worker_epoch_ = 0;
    Job* jobs_ = nullptr;
    std::uint64_t worker_serial_ = 0;
    std::uint16_t* scratch_ = nullptr;
    std::thread thread_;

    // Render-thread-owned.
    CacheIdentity identity_{};
    NodeBox gpu_box_[kLevelCount], candidate_box_[kLevelCount];
    std::uint64_t candidate_seq_[kLevelCount]{}, transferred_seq_[kLevelCount]{};
    bool candidate_valid_[kLevelCount]{}, resident_[kLevelCount]{}, reupload_[kLevelCount]{};
    bool pending_invalidate_ = false, pending_camera_ = false, running_ = false, stepped_ = false, abandoned_ = false, frame_primed_ = false;
    double camera_[3]{};
    NodeBox posted_need_[kLevelCount];
    std::uint64_t last_frame_ = 0, posted_serial_ = 0;
    float ready_[kLevelCount]{};
    unsigned reload_left_[kLevelCount]{};

    // Cross-thread counters.
    std::atomic<unsigned> dirty_tiles_[kLevelCount];
    std::atomic<bool> publication_pending_{false}, stop_flag_{false};
    std::atomic<std::uint64_t> idle_serial_{~std::uint64_t(0)}; // request serial the worker last went idle on
    std::atomic<std::uint64_t> nodes_generated_{0}, jobs_done_{0}, slabs_done_{0}, retargets_{0}, first_fills_{0}, busy_us_{0};
    std::uint64_t upload_bytes_ = 0, upload_rects_ = 0, missed_locks_ = 0;
};

}  // namespace fog
}  // namespace x3m
