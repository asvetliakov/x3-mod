#include "motion_history.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <tuple>

namespace x3m::renderer {
namespace {
auto fields(const RigidDrawKey& k) noexcept {
    return std::tie(k.object_lifetime, k.camera_lifetime, k.draw_domain,
        k.node, k.camera, k.mesh, k.node_handle, k.camera_handle, k.model, k.lod,
        k.vertex_buffer, k.vertex_revision, k.index_buffer, k.index_revision,
        k.declaration, k.position_program, k.stream_offset, k.stride,
        k.position_offset, k.position_type, k.topology, k.first, k.primitives, k.base_vertex,
        k.min_vertex, k.vertex_count, k.index_format, k.indexed, k.pass);
}
} // namespace

MotionHistory::MotionHistory(std::size_t capacity, MotionHistoryPurpose purpose) noexcept
    : capacity_(std::min<std::size_t>(capacity, 65536)), purpose_(purpose) {}
bool MotionHistory::less(const RigidDrawKey& a, const RigidDrawKey& b) noexcept { return fields(a) < fields(b); }
bool MotionHistory::equal(const RigidDrawKey& a, const RigidDrawKey& b) noexcept { return fields(a) == fields(b); }

Correspondence MotionHistory::validate(const RigidObservation& o) noexcept {
    const auto& k = o.key;
    const unsigned position_bytes = k.position_type == 2 ? 12 : k.position_type == 16 ? 8 : 0;
    if (!k.object_lifetime || !k.camera_lifetime || !k.draw_domain || !k.node ||
        !k.camera || !k.mesh || !k.vertex_buffer || !k.declaration || !k.position_program ||
        !position_bytes || !k.stride || k.position_offset > k.stride || k.stride - k.position_offset < position_bytes ||
        !k.primitives || (k.topology != 4 && k.topology != 5) ||
        (k.indexed && (!k.index_buffer || !k.vertex_count || (k.index_format != 101 && k.index_format != 102))) ||
        (!k.indexed && (k.index_buffer || k.index_revision || k.index_format || k.base_vertex || k.min_vertex || k.vertex_count)))
        return Correspondence::InvalidKey;
    if (o.proofs != AllRigidProofs) return Correspondence::MissingProof;
    for (float v : o.submitted_wvp)
        if (!std::isfinite(v) || std::fabs(v) > 1e15f) return Correspondence::InvalidMatrix;
    return Correspondence::Matched;
}

void MotionHistory::invalidate() noexcept {
    current_.clear(); previous_.clear();
    frame_ = {}; previous_frame_ = {}; previous_valid_ = false; phase_ = Phase::Idle;
}
bool MotionHistory::begin_frame(MotionFrame frame) noexcept {
    // An uncommitted prior frame is not eligible even if its numbers look adjacent.
    if (phase_ != Phase::Idle) invalidate();
    if (!capacity_ || !frame.frame || !frame.epoch || !frame.width || !frame.height ||
        (purpose_ != MotionHistoryPurpose::TemporalAccumulation &&
         purpose_ != MotionHistoryPurpose::DiagnosticStorageCorrespondence) ||
        (frame.continuity != MotionContinuity::Unknown &&
         frame.continuity != MotionContinuity::Discontinuity &&
         frame.continuity != MotionContinuity::Continuous)) {
        invalidate(); return false;
    }
    if (!previous_valid_ || previous_frame_.frame == std::numeric_limits<std::uint64_t>::max() ||
        frame.frame != previous_frame_.frame + 1 || frame.epoch != previous_frame_.epoch ||
        frame.width != previous_frame_.width || frame.height != previous_frame_.height ||
        frame.continuity == MotionContinuity::Discontinuity ||
        (purpose_ == MotionHistoryPurpose::TemporalAccumulation &&
         frame.continuity != MotionContinuity::Continuous)) {
        previous_.clear(); previous_valid_ = false;
    }
    current_.clear(); frame_ = frame; phase_ = Phase::Collecting;
    return true;
}
bool MotionHistory::observe(const RigidObservation& observation) noexcept {
    // Bound submitted observations, not only unique keys: adversarial duplicates
    // must not create unbounded work/memory. Never evict selectively mid-frame.
    if (phase_ != Phase::Collecting || current_.size() >= capacity_) {
        invalidate(); return false;
    }
    try {
        current_.push_back({observation.key, observation.submitted_wvp, validate(observation)});
        return true;
    } catch (...) { invalidate(); return false; }
}
bool MotionHistory::seal() noexcept {
    if (phase_ != Phase::Collecting) { invalidate(); return false; }
    std::sort(current_.begin(), current_.end(), [](const Entry& a, const Entry& b) { return less(a.key, b.key); });
    std::size_t count = 0;
    for (std::size_t i = 0; i < current_.size(); ++i) {
        if (count && equal(current_[count-1].key, current_[i].key)) {
            auto& group = current_[count-1];
            // Any disagreement (including signed-zero bits) or ineligible member
            // rejects this key. No draw-order-dependent "first/last wins" policy.
            if (group.status != Correspondence::Matched || current_[i].status != Correspondence::Matched ||
                std::memcmp(group.matrix.data(), current_[i].matrix.data(), sizeof(SubmittedMatrix)))
                group.status = Correspondence::Ambiguous;
        } else {
            if (count != i) current_[count] = current_[i];
            ++count;
        }
    }
    current_.resize(count); phase_ = Phase::Sealed; return true;
}
const MotionHistory::Entry* MotionHistory::find(const std::vector<Entry>& entries, const RigidDrawKey& key) noexcept {
    auto it = std::lower_bound(entries.begin(), entries.end(), key,
        [](const Entry& entry, const RigidDrawKey& candidate) { return less(entry.key, candidate); });
    return it != entries.end() && equal(it->key, key) ? &*it : nullptr;
}
RigidMotionPair MotionHistory::lookup(const RigidDrawKey& key) const noexcept {
    RigidMotionPair result{};
    result.purpose = purpose_;
    if (phase_ != Phase::Sealed) return result;
    const auto* now = find(current_, key);
    if (!now) { result.status = Correspondence::MissingCurrent; return result; }
    if (now->status != Correspondence::Matched) { result.status = now->status; return result; }
    if (!previous_valid_) { result.status = Correspondence::NoPreviousFrame; return result; }
    const auto* old = find(previous_, key);
    if (!old) { result.status = Correspondence::MissingPrevious; return result; }
    if (old->status != Correspondence::Matched) { result.status = old->status; return result; }
    result.status = Correspondence::Matched; result.continuity = frame_.continuity;
    result.current = now->matrix; result.previous = old->matrix;
    return result;
}
bool MotionHistory::commit(bool frame_succeeded) noexcept {
    if (!frame_succeeded || phase_ != Phase::Sealed) { invalidate(); return false; }
    previous_.swap(current_); current_.clear(); previous_frame_ = frame_;
    previous_valid_ = true; phase_ = Phase::Idle; return true;
}
} // namespace x3m::renderer
