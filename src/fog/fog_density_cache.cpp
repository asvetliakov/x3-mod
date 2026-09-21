#include "fog_density_cache.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace x3m {
namespace fog {
namespace {
constexpr std::int64_t kFar = std::int64_t(1) << 40; // beyond any reachable node key
// Node keys fit int32 for every admitted camera (|p| <= 1e12 render units: |p/512| < 2^31); the
// int32 conversion keeps the i686 build on SSE2 (a double -> int64 conversion goes through x87).
// Truncate-and-correct floor (cvttsd2si): no CRT floor call, whose cdecl double return is an x87 move.
inline std::int32_t floor_i32(double q) noexcept { const std::int32_t t = static_cast<std::int32_t>(q); return t - (q < double(t) ? 1 : 0); }
inline std::int64_t floor_node(double q) noexcept { return floor_i32(q); }
inline bool admitted(const double camera[3]) noexcept {
    for (int a = 0; a < 3; ++a)
        if (!(camera[a] <= kCameraLimit && camera[a] >= -kCameraLimit)) return false; // ordered compares: NaN is refused
    return true;
}
inline std::int64_t key_axis(const NodeKey& k, int a) noexcept { return a == 0 ? k.x : a == 1 ? k.y : k.z; }
}  // namespace

// --- Boxes ---------------------------------------------------------------

bool NodeBox::contains(const NodeBox& inner) const noexcept {
    if (inner.empty()) return true;
    if (empty()) return false;
    for (int a = 0; a < 3; ++a)
        if (inner.lo[a] < lo[a] || inner.hi[a] > hi[a]) return false;
    return true;
}
bool NodeBox::operator==(const NodeBox& o) const noexcept {
    if (empty() || o.empty()) return empty() && o.empty();
    for (int a = 0; a < 3; ++a)
        if (lo[a] != o.lo[a] || hi[a] != o.hi[a]) return false;
    return true;
}
std::uint64_t NodeBox::nodes() const noexcept {
    if (empty()) return 0;
    std::uint64_t n = 1;
    for (int a = 0; a < 3; ++a) n *= std::uint64_t(hi[a] - lo[a] + 1);
    return n;
}
NodeBox empty_box() noexcept { return {{0, 0, 0}, {-1, -1, -1}}; }
NodeBox universe_box() noexcept { return {{-kFar, -kFar, -kFar}, {kFar, kFar, kFar}}; }
NodeBox intersect(const NodeBox& a, const NodeBox& b) noexcept {
    if (a.empty() || b.empty()) return empty_box();
    NodeBox r;
    for (int i = 0; i < 3; ++i) {
        r.lo[i] = std::max(a.lo[i], b.lo[i]);
        r.hi[i] = std::min(a.hi[i], b.hi[i]);
    }
    return r.empty() ? empty_box() : r;
}
NodeBox grow(const NodeBox& box, std::int64_t nodes) noexcept {
    if (box.empty()) return box;
    NodeBox r = box;
    for (int a = 0; a < 3; ++a) {
        r.lo[a] -= nodes;
        r.hi[a] += nodes;
    }
    return r;
}
NodeBox window_box(const NodeKey& o) noexcept {
    return {{o.x, o.y, o.z}, {o.x + kWindowNodes - 1, o.y + kWindowNodes - 1, o.z + kWindowNodes - 1}};
}
NodeBox need_box(int level, const double camera[3]) noexcept {
    const double delta = kLevelDelta[level], reach = kLevelReach[level];
    NodeBox r;
    for (int a = 0; a < 3; ++a) {
        r.lo[a] = floor_node((camera[a] - reach) / delta);
        r.hi[a] = floor_node((camera[a] + reach) / delta) + 1;
    }
    return r;
}
void camera_local(int level, const double camera[3], float out[3]) noexcept {
    const double period = kWindowNodes * kLevelDelta[level];
    for (int a = 0; a < 3; ++a) out[a] = float(camera[a] - period * double(floor_i32(camera[a] / period + .5)));
}
bool retarget_needed(int level, const NodeKey& origin, const double camera[3]) noexcept {
    for (int a = 0; a < 3; ++a) {
        const std::int64_t offset = floor_node(camera[a] / kLevelDelta[level]) - (key_axis(origin, a) + kWindowHalf);
        if (offset >= kRetargetNodes[level] || offset <= -kRetargetNodes[level]) return true;
    }
    return false;
}

// --- Planning ------------------------------------------------------------

std::size_t plan_jobs(const NodeBox& slab, int level, const double camera[3], Job* out, std::size_t capacity) noexcept {
    if (slab.empty()) return 0;
    const std::int64_t cell[3] = {kBrickTexels, kBrickTexels, kLanes};
    const double delta = kLevelDelta[level];
    std::size_t count = 0;
    // n | (cell-1) is the last node of n's storage cell (two's complement, cells at multiples of `cell`).
    for (std::int64_t z = slab.lo[2]; z <= slab.hi[2];) {
        const std::int64_t z1 = std::min(slab.hi[2], z | (cell[2] - 1));
        for (std::int64_t y = slab.lo[1]; y <= slab.hi[1];) {
            const std::int64_t y1 = std::min(slab.hi[1], y | (cell[1] - 1));
            for (std::int64_t x = slab.lo[0]; x <= slab.hi[0];) {
                const std::int64_t x1 = std::min(slab.hi[0], x | (cell[0] - 1));
                if (count == capacity) return 0;
                Job& job = out[count++];
                job.box = {{x, y, z}, {x1, y1, z1}};
                job.distance2 = 0;
                for (int a = 0; a < 3; ++a) {
                    // Converted one by one: lo + hi leaves int32 beyond 5.5e11 units, and an int64 conversion would be x87.
                    const double d = .5 * (double(std::int32_t(job.box.lo[a])) + double(std::int32_t(job.box.hi[a]))) - camera[a] / delta;
                    job.distance2 += d * d;
                }
                x = x1 + 1;
            }
            y = y1 + 1;
        }
        z = z1 + 1;
    }
    std::sort(out, out + count, [](const Job& a, const Job& b) { return a.distance2 < b.distance2; });
    return count;
}

bool next_slab(const NodeBox& have, const NodeBox& want, const NodeBox& need, NodeBox& slab) noexcept {
    if (have.empty() || want.empty()) return false;
    for (int pass = 0; pass < 2; ++pass)
        for (int a = 0; a < 3; ++a)
            for (int side = 0; side < 2; ++side) {
                const bool missing = side ? have.hi[a] < want.hi[a] : have.lo[a] > want.lo[a];
                const bool urgent = !need.empty() && (side ? need.hi[a] > have.hi[a] : need.lo[a] < have.lo[a]);
                if (!missing || (pass == 0 && !urgent)) continue;
                slab = have;
                if (side) { slab.lo[a] = have.hi[a] + 1; slab.hi[a] = want.hi[a]; }
                else { slab.lo[a] = want.lo[a]; slab.hi[a] = have.lo[a] - 1; }
                return true;
            }
    return false;
}

// --- Lifetime ------------------------------------------------------------

DensityCache::DensityCache() noexcept {
    for (int l = 0; l < kLevelCount; ++l) {
        dirty_tiles_[l].store(0);
        worker_[l].box = shared_[l].box = gpu_box_[l] = candidate_box_[l] = posted_need_[l] = empty_box();
        shared_[l].shrink = universe_box();
        std::memset(shared_[l].tiles, 0, sizeof shared_[l].tiles);
    }
}
DensityCache::~DensityCache() {
    if (abandoned_) return;
    stop();
    for (int l = 0; l < kLevelCount; ++l) delete[] cache_[l];
    delete[] jobs_;
    delete[] scratch_;
}
bool DensityCache::allocate() noexcept {
    for (int l = 0; l < kLevelCount; ++l)
        if (!cache_[l]) {
            cache_[l] = new (std::nothrow) std::uint8_t[kAtlasBytes];
            if (cache_[l]) std::memset(cache_[l], 0, kAtlasBytes);
        }
    if (!jobs_) jobs_ = new (std::nothrow) Job[kMaxJobs];
    if (!scratch_) scratch_ = new (std::nothrow) std::uint16_t[kJobNodes];
    return cache_[0] && cache_[1] && jobs_ && scratch_;
}
bool DensityCache::start() noexcept {
    if (running_ || stepped_ || abandoned_) return running_;
    if (!allocate()) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        request_.stop = false;
    }
    stop_flag_.store(false);
    try {
        thread_ = std::thread([this] { run(); });
    } catch (...) {
        return false;
    }
    running_ = true;
    return true;
}
bool DensityCache::start_stepped() noexcept {
    if (running_ || abandoned_) return false;
    stepped_ = allocate();
    return stepped_;
}
void DensityCache::stop() noexcept {
    if (!running_) return;
    stop_flag_.store(true);
    std::unique_lock<std::mutex> lock(mutex_, std::defer_lock);
    for (int attempt = 0; attempt < 250 && !lock.try_lock(); ++attempt) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!lock.owns_lock()) { // held for far longer than any commit: the holder is gone
        abandon();
        return;
    }
    request_.stop = true;
    ++request_.serial;
    lock.unlock();
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
    running_ = false;
}
void DensityCache::abandon() noexcept {
    stop_flag_.store(true);
    if (thread_.joinable()) thread_.detach();
    running_ = false;
    abandoned_ = true;
}

// --- Worker ---------------------------------------------------------------

void DensityCache::run() noexcept {
#ifdef _WIN32
    // Generation must not compete with the render thread for a core.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
    for (;;) {
        bool idle = false;
        if (!work_once(idle)) return;
        if (idle) {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return request_.stop || request_.serial != worker_serial_; });
            if (request_.stop) return;
        }
    }
}
bool DensityCache::worker_step() noexcept {
    bool idle = false;
    return stepped_ && work_once(idle) && !idle;
}

void DensityCache::commit_locked(int level, const NodeBox& job, const std::uint16_t* words) noexcept {
    std::uint8_t* atlas = cache_[level];
    const int group = storage_index(job.lo[2]) / kLanes;
    const int sx0 = storage_index(job.lo[0]), sy0 = storage_index(job.lo[1]);
    const int nx = int(job.hi[0] - job.lo[0]) + 1, ny = int(job.hi[1] - job.lo[1]) + 1, nz = int(job.hi[2] - job.lo[2]) + 1;
    const int tile_x = (group % kGroupsPerRow) * kTileTexels, tile_y = (group / kGroupsPerRow) * kTileTexels;
    for (int z = 0; z < nz; ++z) {
        const std::size_t lane = std::size_t(storage_index(job.lo[2] + z) % kLanes) * 2;
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                const std::uint16_t word = *words++;
                const std::uint8_t bytes[2] = {std::uint8_t(word & 0xff), std::uint8_t(word >> 8)};
                const int sx = sx0 + x, sy = sy0 + y;
                auto put = [&](int tx, int ty) {
                    std::uint8_t* at = atlas + std::size_t(tile_y + ty) * kAtlasPitch + std::size_t(tile_x + tx) * kTexelBytes + lane;
                    at[0] = bytes[0];
                    at[1] = bytes[1];
                };
                put(sx, sy);
                if (sx == 0) put(kWindowNodes, sy);
                if (sy == 0) put(sx, kWindowNodes);
                if (sx == 0 && sy == 0) put(kWindowNodes, kWindowNodes);
            }
    }
    ++shared_[level].commit_seq;
    dirty_locked(level, group, 0, sx0, sy0, sx0 + nx - 1, sy0 + ny - 1);
    if (sx0 == 0) dirty_locked(level, group, kTileRegions - 2, kWindowNodes, sy0, kWindowNodes, sy0 + ny - 1);
    if (sy0 == 0) dirty_locked(level, group, kTileRegions - 1, sx0, kWindowNodes, sx0 + nx - 1, kWindowNodes);
    if (sy0 == 0 && sx0 == 0) dirty_locked(level, group, kTileRegions - 1, kWindowNodes, kWindowNodes, kWindowNodes, kWindowNodes); // corner rides with the row
}
void DensityCache::dirty_locked(int level, int group, int region, int x0, int y0, int x1, int y1) noexcept {
    TileDirty& tile = shared_[level].tiles[group];
    auto unite = [](Region& r, int ax0, int ay0, int ax1, int ay1) {
        r.x0 = std::min(r.x0, ax0); r.y0 = std::min(r.y0, ay0);
        r.x1 = std::max(r.x1, ax1); r.y1 = std::max(r.y1, ay1);
    };
    auto area = [](int ax0, int ay0, int ax1, int ay1) { return (ax1 - ax0 + 1) * (ay1 - ay0 + 1); };
    if (region != 0) { // border strips: one bounding box each
        Region& r = tile.region[region];
        if (r.dirty) unite(r, x0, y0, x1, y1); else r = {x0, y0, x1, y1, true};
    } else {
        const int body = kTileRegions - 2;
        Region* slot = nullptr;
        for (int i = 0; i < body; ++i) {
            Region& r = tile.region[i];
            if (!r.dirty) { if (!slot) slot = &r; continue; }
            const int ux0 = std::min(r.x0, x0), uy0 = std::min(r.y0, y0), ux1 = std::max(r.x1, x1), uy1 = std::max(r.y1, y1);
            if (area(ux0, uy0, ux1, uy1) <= area(r.x0, r.y0, r.x1, r.y1) + area(x0, y0, x1, y1)) { r = {ux0, uy0, ux1, uy1, true}; slot = &r; x0 = -1; break; }
        }
        if (x0 >= 0) {
            if (slot) *slot = {x0, y0, x1, y1, true};
            else { // full: collapse the body to one bounding box
                Region& first = tile.region[0];
                unite(first, x0, y0, x1, y1);
                for (int i = 1; i < body; ++i) { unite(first, tile.region[i].x0, tile.region[i].y0, tile.region[i].x1, tile.region[i].y1); tile.region[i].dirty = false; }
            }
        }
    }
    if (!tile.dirty) {
        tile.dirty = true;
        tile.first_seq = shared_[level].commit_seq;
        dirty_tiles_[level].fetch_add(1, std::memory_order_relaxed);
    }
}

bool DensityCache::fill_slab(int level, const NodeBox& slab, std::uint64_t epoch, const WorldOffset& offset, const double camera[3]) noexcept {
    const std::size_t count = plan_jobs(slab, level, camera, jobs_, kMaxJobs);
    const double delta = kLevelDelta[level];
    for (std::size_t j = 0; j < count; ++j) {
        if (stop_flag_.load(std::memory_order_relaxed)) return false;
        const NodeBox& box = jobs_[j].box;
        const auto begin = std::chrono::steady_clock::now();
        std::uint16_t* word = scratch_;
        NodeKey key;
        for (key.z = box.lo[2]; key.z <= box.hi[2]; ++key.z)
            for (key.y = box.lo[1]; key.y <= box.hi[1]; ++key.y)
                for (key.x = box.lo[0]; key.x <= box.hi[0]; ++key.x) *word++ = node_word(delta, key, offset);
        busy_us_.fetch_add(std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begin).count()), std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (request_.stop || request_.epoch != epoch) return false;
            commit_locked(level, box, scratch_);
        }
        nodes_generated_.fetch_add(box.nodes(), std::memory_order_relaxed);
        jobs_done_.fetch_add(1, std::memory_order_relaxed);
    }
    return true;
}

// One scheduling decision and at most one slab. Returns false to end the worker.
bool DensityCache::work_once(bool& idle) noexcept {
    Request request;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        request = request_;
        worker_serial_ = request_.serial;
    }
    idle = true;
    if (request.stop || stop_flag_.load(std::memory_order_relaxed)) return false;
    if (request.epoch != worker_epoch_) {
        worker_epoch_ = request.epoch;
        for (auto& level : worker_) level.box = empty_box();
    }
    if (!request.camera_valid) { idle_serial_.store(request.serial, std::memory_order_relaxed); return true; }
    // Far need first (it gates drawing), then fine need, then the background growth
    // to the full window, fine first because its margin is the tighter one.
    int level = -1;
    NodeBox need[kLevelCount];
    for (int l = 0; l < kLevelCount; ++l) need[l] = need_box(l, request.camera);
    for (int l : {1, 0})
        if (level < 0 && !worker_[l].box.contains(need[l])) level = l;
    for (int l : {0, 1})
        if (level < 0 && (retarget_needed(l, worker_[l].origin, request.camera) || !(worker_[l].box == window_box(worker_[l].origin)))) level = l;
    if (level < 0) { idle_serial_.store(request.serial, std::memory_order_relaxed); return true; }
    idle = false;
    WorkerLevel& state = worker_[level];
    const bool first = state.box.empty();
    if (first || retarget_needed(level, state.origin, request.camera)) {
        const NodeKey origin = window_origin(kLevelDelta[level], request.camera[0], request.camera[1], request.camera[2]);
        const NodeBox window = window_box(origin);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (request_.epoch != worker_epoch_) return true;
            // Published before any slot of the new window is overwritten.
            shared_[level].shrink = intersect(shared_[level].shrink, window);
            shared_[level].box = intersect(shared_[level].box, window);
        }
        state.origin = origin;
        state.box = intersect(state.box, window);
        if (!first) {
            retargets_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }
    NodeBox slab, grown;
    const NodeBox window = window_box(state.origin);
    if (state.box.empty()) {
        slab = grown = intersect(window, grow(need[level], kFirstFillSlack[level]));
        first_fills_.fetch_add(1, std::memory_order_relaxed);
    } else {
        if (!next_slab(state.box, window, need[level], slab)) return true;
        grown = state.box;
        for (int a = 0; a < 3; ++a) {
            grown.lo[a] = std::min(grown.lo[a], slab.lo[a]);
            grown.hi[a] = std::max(grown.hi[a], slab.hi[a]);
        }
    }
    if (slab.empty() || slab.nodes() > std::uint64_t(kWindowNodes) * kWindowNodes * kWindowNodes) { idle = true; return true; } // unreachable by construction
    if (!fill_slab(level, slab, worker_epoch_, request.offset, request.camera)) return !stop_flag_.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (request_.stop) return false;
        if (request_.epoch != worker_epoch_) return true;
        shared_[level].box = grown;
        shared_[level].box_seq = shared_[level].commit_seq;
        publication_pending_.store(true, std::memory_order_relaxed);
    }
    state.box = grown;
    slabs_done_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// --- Render thread --------------------------------------------------------

void DensityCache::apply_invalidate_locked() noexcept {
    ++request_.epoch;
    ++request_.serial;
    request_.offset = identity_.offset;
    for (int l = 0; l < kLevelCount; ++l) {
        SharedLevel& s = shared_[l];
        s.box = empty_box();
        s.shrink = universe_box();
        s.box_seq = 0;
        for (auto& tile : s.tiles) tile = {};
        dirty_tiles_[l].store(0, std::memory_order_relaxed);
    }
    publication_pending_.store(false, std::memory_order_relaxed);
    pending_invalidate_ = false;
}
void DensityCache::post_locked() noexcept {
    if (pending_invalidate_) apply_invalidate_locked();
    if (pending_camera_) {
        for (int a = 0; a < 3; ++a) request_.camera[a] = camera_[a];
        request_.camera_valid = true;
        ++request_.serial;
        pending_camera_ = false;
    }
    posted_serial_ = request_.serial;
}
void DensityCache::configure(const CacheIdentity& identity) noexcept {
    if (identity == identity_ && (running_ || stepped_)) return;
    identity_ = identity;
    invalidate();
}
void DensityCache::invalidate() noexcept {
    for (int l = 0; l < kLevelCount; ++l) {
        gpu_box_[l] = candidate_box_[l] = empty_box();
        candidate_valid_[l] = false;
        candidate_seq_[l] = transferred_seq_[l] = 0;
        ready_[l] = 0;
    }
    pending_invalidate_ = true;
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) { ++missed_locks_; return; }
    post_locked();
    lock.unlock();
    wake_.notify_one();
}
bool DensityCache::covers(int level, const double camera[3]) const noexcept {
    return admitted(camera) && resident_[level] && gpu_box_[level].contains(need_box(level, camera));
}
FrameState DensityCache::step(const double camera[3], std::uint64_t frame) noexcept {
    FrameState out{};
    if (!(running_ || stepped_)) return out;
    if (!admitted(camera)) return out;
    NodeBox need[kLevelCount];
    bool moved = false;
    for (int l = 0; l < kLevelCount; ++l) {
        need[l] = need_box(l, camera);
        moved = moved || !(need[l] == posted_need_[l]);
    }
    if (moved) {
        for (int a = 0; a < 3; ++a) camera_[a] = camera[a];
        pending_camera_ = true;
    }
    if (pending_camera_ || pending_invalidate_) {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (lock.owns_lock()) {
            post_locked();
            lock.unlock();
            wake_.notify_one();
            for (int l = 0; l < kLevelCount; ++l) posted_need_[l] = need_box(l, camera_);
        } else {
            ++missed_locks_;
        }
    }
    std::uint64_t elapsed = !frame_primed_ ? 1 : frame > last_frame_ ? frame - last_frame_ : 0;
    if (elapsed > kReadinessRampFrames) elapsed = kReadinessRampFrames;
    const float ramp = float(elapsed) / float(kReadinessRampFrames);
    for (int l = 0; l < kLevelCount; ++l) {
        const bool hard = resident_[l] && gpu_box_[l].contains(need[l]);
        const bool soft = hard && gpu_box_[l].contains(grow(need[l], kReadinessGuard));
        if (!hard) ready_[l] = 0;
        else if (soft) ready_[l] = std::min(1.f, ready_[l] + ramp);
        else ready_[l] = std::max(0.f, ready_[l] - ramp);
        out.ready[l] = ready_[l];
        out.resident[l] = hard;
        camera_local(l, camera, out.local[l]);
    }
    last_frame_ = frame;
    frame_primed_ = true;
    return out;
}

bool DensityCache::idle() const noexcept {
    return !pending_invalidate_ && !pending_camera_ && !has_work() && !candidate_valid_[0] && !candidate_valid_[1] &&
           idle_serial_.load(std::memory_order_relaxed) == posted_serial_;
}
bool DensityCache::has_work() const noexcept {
    return publication_pending_.load(std::memory_order_relaxed) || reupload_[0] || reupload_[1] ||
           dirty_tiles_[0].load(std::memory_order_relaxed) != 0 || dirty_tiles_[1].load(std::memory_order_relaxed) != 0;
}
void DensityCache::gpu_reset() noexcept {
    for (int l = 0; l < kLevelCount; ++l) {
        resident_[l] = false;
        reupload_[l] = true;
        candidate_valid_[l] = false;
        ready_[l] = 0;
    }
}
unsigned DensityCache::take_uploads(const StagingView views[kLevelCount], std::size_t byte_budget, TileRect* out, unsigned capacity) noexcept {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) { ++missed_locks_; return 0; }
    const bool wake = pending_invalidate_ || pending_camera_;
    post_locked();
    unsigned count = 0;
    std::size_t bytes = 0;
    byte_budget = std::max(byte_budget, kTileBytes);
    bool full = false, unpublished = false;
    for (int l : {1, 0}) {
        SharedLevel& s = shared_[l];
        gpu_box_[l] = intersect(gpu_box_[l], s.shrink);
        if (candidate_valid_[l]) candidate_box_[l] = intersect(candidate_box_[l], s.shrink);
        s.shrink = universe_box();
        if (reupload_[l] && views[l].bits) {
            // Every tile that ever received a commit is uploaded again in full.
            if (s.commit_seq)
                for (int g = 0; g < kTileCount; ++g) {
                    TileDirty& tile = s.tiles[g];
                    if (!tile.dirty) {
                        tile.dirty = true;
                        tile.first_seq = s.commit_seq + 1;
                        dirty_tiles_[l].fetch_add(1, std::memory_order_relaxed);
                    }
                    tile.reload = true;
                    for (Region& r : tile.region) r.dirty = false;
                    tile.region[0] = {0, 0, kWindowNodes - 1, kWindowNodes - 1, true};
                    tile.region[kTileRegions - 2] = {kWindowNodes, 0, kWindowNodes, kWindowNodes - 1, true};
                    tile.region[kTileRegions - 1] = {0, kWindowNodes, kWindowNodes, kWindowNodes, true};
                }
            reupload_[l] = false;
        }
        std::uint64_t flushed = s.commit_seq;
        unsigned reload_left = 0;
        for (int g = 0; g < kTileCount; ++g) {
            TileDirty& tile = s.tiles[g];
            if (!tile.dirty) continue;
            bool remaining = false;
            for (Region& r : tile.region) {
                if (!r.dirty) continue;
                TileRect rect{l, g, (g % kGroupsPerRow) * kTileTexels + r.x0, (g / kGroupsPerRow) * kTileTexels + r.y0, r.x1 - r.x0 + 1, r.y1 - r.y0 + 1};
                if (!views[l].bits || full || count == capacity || bytes + rect.bytes() > byte_budget) {
                    full = full || views[l].bits != nullptr;
                    remaining = true;
                    continue;
                }
                for (int y = 0; y < rect.height; ++y)
                    std::memcpy(views[l].bits + std::size_t(rect.y + y) * views[l].pitch + std::size_t(rect.x) * kTexelBytes,
                                cache_[l] + std::size_t(rect.y + y) * kAtlasPitch + std::size_t(rect.x) * kTexelBytes, std::size_t(rect.width) * kTexelBytes);
                bytes += rect.bytes();
                out[count++] = rect;
                r.dirty = false;
            }
            if (remaining) {
                flushed = std::min(flushed, tile.first_seq - 1);
                reload_left += tile.reload;
            } else {
                tile.dirty = tile.reload = false;
                dirty_tiles_[l].fetch_sub(1, std::memory_order_relaxed);
            }
        }
        reload_left_[l] = reload_left + (reupload_[l] ? 1u : 0u);
        if (s.box_seq != transferred_seq_[l] && flushed >= s.box_seq) {
            candidate_box_[l] = s.box;
            candidate_seq_[l] = s.box_seq;
            candidate_valid_[l] = true;
        }
        unpublished = unpublished || (s.box_seq != transferred_seq_[l] && !(candidate_valid_[l] && candidate_seq_[l] == s.box_seq));
    }
    publication_pending_.store(unpublished, std::memory_order_relaxed);
    lock.unlock();
    if (wake) wake_.notify_one();
    upload_bytes_ += bytes;
    upload_rects_ += count;
    return count;
}
void DensityCache::confirm_uploads(bool succeeded) noexcept {
    if (!succeeded) {
        // The copies handed out may not have reached the GPU: upload everything again.
        gpu_reset();
        return;
    }
    for (int l = 0; l < kLevelCount; ++l) {
        if (candidate_valid_[l]) {
            gpu_box_[l] = candidate_box_[l];
            transferred_seq_[l] = candidate_seq_[l];
            candidate_valid_[l] = false;
        }
        if (!resident_[l] && !reupload_[l] && reload_left_[l] == 0) resident_[l] = true;
    }
}
CacheStats DensityCache::stats() const noexcept {
    CacheStats s;
    s.nodes_generated = nodes_generated_.load(std::memory_order_relaxed);
    s.jobs = jobs_done_.load(std::memory_order_relaxed);
    s.slabs = slabs_done_.load(std::memory_order_relaxed);
    s.retargets = retargets_.load(std::memory_order_relaxed);
    s.first_fills = first_fills_.load(std::memory_order_relaxed);
    s.worker_busy_us = busy_us_.load(std::memory_order_relaxed);
    s.upload_bytes = upload_bytes_;
    s.upload_rects = upload_rects_;
    s.missed_locks = missed_locks_;
    return s;
}

}  // namespace fog
}  // namespace x3m
