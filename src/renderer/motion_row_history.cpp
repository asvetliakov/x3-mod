#include "motion_row_history.h"
#include <algorithm>
#include <cmath>
#include <tuple>

namespace x3m::renderer {
namespace {
auto fields(const RigidDrawKey& k) noexcept {
    return std::tie(k.object_lifetime, k.camera_lifetime, k.draw_domain,
        k.node, k.camera, k.mesh, k.node_handle, k.camera_handle, k.model, k.lod,
        k.vertex_buffer, k.vertex_revision, k.index_buffer, k.index_revision,
        k.declaration, k.position_program, k.stream_offset, k.stride,
        k.position_offset, k.position_type, k.topology, k.first, k.primitives, k.base_vertex,
        k.min_vertex, k.vertex_count, k.index_format, k.indexed);
}
bool finite(const SubmittedMatrix& rows) noexcept {
    for (float v : rows) if (!std::isfinite(v) || std::fabs(v) > 1e15f) return false;
    return true;
}
} // namespace

MotionRowHistory::MotionRowHistory(std::size_t capacity) noexcept
    : capacity_(std::min<std::size_t>(capacity, 65536)) {
    try {
        current_.reserve(capacity_);
        previous_.reserve(capacity_);
        ready_ = capacity_ > 0;
    } catch (...) { ready_ = false; }
}
bool MotionRowHistory::less(const RigidDrawKey& a, const RigidDrawKey& b) noexcept { return fields(a) < fields(b); }
bool MotionRowHistory::equal(const RigidDrawKey& a, const RigidDrawKey& b) noexcept { return fields(a) == fields(b); }

bool MotionRowHistory::key_valid(const RigidDrawKey& k) noexcept {
    // Lifetimes and epochs come from the verified observer; geometry identity
    // from the shadowed bindings. Zero anywhere means an unknown component.
    if (!k.object_lifetime || !k.camera_lifetime || !k.draw_domain || !k.node || !k.camera ||
        !k.vertex_buffer || !k.declaration || !k.position_program || !k.stride || !k.primitives)
        return false;
    if (k.indexed && !k.index_buffer) return false;
    return true;
}

void MotionRowHistory::invalidate() noexcept {
    current_.clear(); previous_.clear();
    frame_ = {}; previous_frame_ = {};
    collecting_ = previous_valid_ = overflow_ = false;
}

void MotionRowHistory::begin_frame(const MotionRowFrame& frame) noexcept {
    if (!ready_) return;
    // An uncommitted prior frame (Present never observed) is not history.
    if (collecting_) { previous_.clear(); previous_valid_ = false; }
    if (!frame.generation || !frame.width || !frame.height ||
        frame.generation != previous_frame_.generation ||
        frame.width != previous_frame_.width || frame.height != previous_frame_.height) {
        previous_.clear(); previous_valid_ = false;
    }
    current_.clear(); frame_ = frame; collecting_ = true; overflow_ = false;
}

MotionRowHistory::Entry* MotionRowHistory::find_previous(const RigidDrawKey& key) noexcept {
    auto it = std::lower_bound(previous_.begin(), previous_.end(), key,
        [](const Entry& entry, const RigidDrawKey& candidate) { return less(entry.key, candidate); });
    return it != previous_.end() && equal(it->key, key) ? &*it : nullptr;
}

bool MotionRowHistory::lookup_and_record(const RigidDrawKey& key, const SubmittedMatrix& current,
                                         SubmittedMatrix& previous) noexcept {
    if (!ready_ || !collecting_ || !key_valid(key)) return false;
    if (current_.size() >= capacity_) { overflow_ = true; return false; }
    // Capacity was reserved in the constructor; push_back cannot reallocate.
    current_.push_back({key, current, false, false});
    if (!previous_valid_) return false;
    Entry* old = find_previous(key);
    if (!old || old->poisoned || old->consumed || !finite(old->rows)) return false;
    old->consumed = true;
    previous = old->rows;
    return true;
}

bool MotionRowHistory::commit(bool frame_succeeded) noexcept {
    if (!ready_ || !collecting_ || !frame_succeeded || overflow_) { invalidate(); return false; }
    std::sort(current_.begin(), current_.end(),
              [](const Entry& a, const Entry& b) { return less(a.key, b.key); });
    // Duplicate keys within one frame cannot name one object: poison them so
    // neither draw-order policy nor row equality can produce a guess.
    for (std::size_t i = 1; i < current_.size(); ++i)
        if (equal(current_[i - 1].key, current_[i].key))
            current_[i - 1].poisoned = current_[i].poisoned = true;
    previous_.swap(current_); current_.clear();
    for (auto& entry : previous_) entry.consumed = false;
    previous_frame_ = frame_; previous_valid_ = true; collecting_ = false;
    return true;
}

MotionRowStats MotionRowHistory::stats() const noexcept {
    return {previous_.size(), current_.size(), capacity_, previous_valid_, collecting_, overflow_};
}
} // namespace x3m::renderer
