#pragma once
// In-frame previous-row lookup for the live same-draw motion route. Pure CPU
// bookkeeping: no D3D/COM ownership, no per-draw heap allocation after
// construction. The caller serializes every call under its device mutex.
//
// Unlike MotionHistory, which collects then seals before any lookup (deferred
// replay), this table answers lookups against the sealed PREVIOUS frame while
// the CURRENT frame is still collecting, because the live route must decide
// each draw at submission time. The two-phase class stays untouched as the
// replay reference.
#include "motion_history.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace x3m::renderer {
struct MotionRowFrame {
    // Resource/regime identity: reset generation and target dimensions. A change
    // drops the previous table; draw-level epochs travel inside RigidDrawKey.
    std::uint64_t generation = 0;
    std::uint32_t width = 0, height = 0;
};
struct MotionRowStats {
    std::size_t previous = 0, current = 0, capacity = 0;
    bool previous_valid = false, collecting = false, overflow = false;
};

class MotionRowHistory {
public:
    // Reserves both tables once. ready() is false if that allocation failed;
    // every operation then reports no history without touching the heap.
    explicit MotionRowHistory(std::size_t capacity = 4096) noexcept;
    bool ready() const noexcept { return ready_; }
    // Starts collecting a frame. The previous table survives only if the last
    // frame committed successfully with the same generation and dimensions.
    void begin_frame(const MotionRowFrame& frame) noexcept;
    // Records `current` under `key` in the collecting frame and looks the key up
    // in the previous frame. True and `previous` filled only when exactly one
    // unconsumed, unpoisoned, finite previous entry matches; that entry is then
    // consumed so a second current draw with the same key gets no history.
    // Invalid keys are neither recorded nor matched. Exceeding capacity stops
    // recording and poisons the whole frame at commit (fail closed).
    bool lookup_and_record(const RigidDrawKey& key, const SubmittedMatrix& current,
                           SubmittedMatrix& previous) noexcept;
    // Miss classification for the static-world option (called only after a
    // failed lookup_and_record; never on the matched path). 0: `key` has an
    // entry in the previous frame (poisoned, consumed or non-finite: a repeated
    // miss, not a new key) or there is no valid previous frame; 1: the key is
    // new and no previous entry carries its object identity (lifetime serials,
    // draw domain, node); 2: the key is new and its object was drawn last frame
    // under another key. One binary search, no allocation.
    unsigned classify_miss(const RigidDrawKey& key) const noexcept;
    // Seals the collecting frame as the new previous table: duplicate keys are
    // poisoned, entries sorted. A failed frame or overflow drops both tables.
    bool commit(bool frame_succeeded) noexcept;
    void invalidate() noexcept;
    MotionRowStats stats() const noexcept;
    static bool key_valid(const RigidDrawKey& key) noexcept;

private:
    struct Entry {
        RigidDrawKey key{};
        SubmittedMatrix rows{};
        bool poisoned = false, consumed = false;
    };
    static bool less(const RigidDrawKey&, const RigidDrawKey&) noexcept;
    static bool equal(const RigidDrawKey&, const RigidDrawKey&) noexcept;
    Entry* find_previous(const RigidDrawKey& key) noexcept;
    std::vector<Entry> current_, previous_;
    MotionRowFrame frame_{}, previous_frame_{};
    std::size_t capacity_ = 0;
    bool ready_ = false, collecting_ = false, previous_valid_ = false, overflow_ = false;
};
} // namespace x3m::renderer
