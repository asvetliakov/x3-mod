#pragma once
#include <algorithm>
#include <cstdint>

// Value-only state of the frame-phase diagnostic (X3M_FRAME_PHASES=1): the
// per-frame stamp tracker and the 300-frame window reduction. No Win32, no
// allocation, one caller thread (the runtime admits the main-loop thread and
// drives this from the ten engine stamps and the Present hook). Host-tested by
// verification/probe/frame_phases_host.cpp.
//
// A frame runs from one native Present return to the next. The interval that
// starts at a stamp is named after that stamp (docs/reverse-engineering/
// frame-loop-phases.md, section 4): pre_render is everything from the Present
// return to the render routine's prologue (the main loop's input, script VM,
// simulation and cockpit phases, see X3M_GAME_PHASES), present is the native
// Present call. The seven core stamps are ordered and fire at most once per
// frame; a forward skip is allowed (a frame without views jumps from
// begin_scene to text, docs section 4 "incoming edges"), a repeat or a
// backward step is an order error that drops the frame. The three view stamps
// accumulate per view and never change the current phase.
namespace x3m::frame_phases::detail {
inline constexpr unsigned core_sites = 7, site_count = 10;
inline constexpr unsigned phase_count = 9; // pre_render, seven stamp intervals, present
inline constexpr unsigned pre_render = 0, views_phase = 4, present_phase = 8;
inline constexpr unsigned window_frames = 300, slow_slots = 4;
inline constexpr const char* const phase_names[phase_count] = {
    "pre_render", "prologue", "scene_update", "begin_scene", "views", "overlays", "text", "scene_end", "present"};

struct Sample {
    std::uint64_t frame = 0;
    std::uint64_t dt_us = 0; // sum of the nine phases: Present return to Present return
    std::uint64_t phase_us[phase_count]{};
    std::uint64_t view_setup_us = 0, view_submit_us = 0; // sums over the frame's views
    std::uint32_t views = 0;
    bool complete = false; // all seven core stamps fired
};

struct Tracker {
    std::uint64_t order_errors = 0, clock_errors = 0, unmatched = 0, dropped = 0;
    bool live = false;    // a frame is being accumulated (after the first Present return)
    bool pending = false; // `closed` holds a finished frame not yet taken by frame()
    unsigned phase = 0, fired = 0;
    std::uint64_t last_qpc = 0, setup_begin = 0, submit_begin = 0;
    std::uint64_t submit_end = 0; // the frame's last view_submit_end clock, retained for the residual group (one store
                                  // per view)
    std::uint64_t phase_ticks[phase_count]{};
    std::uint64_t setup_ticks = 0, submit_ticks = 0;
    std::uint32_t views = 0;
    struct Closed {
        std::uint64_t phase_ticks[phase_count]{}, setup_ticks = 0, submit_ticks = 0;
        std::uint32_t views = 0;
        bool complete = false;
    } closed{};

    void restart(std::uint64_t qpc) noexcept {
        live = true;
        phase = pre_render;
        fired = 0;
        last_qpc = qpc;
        setup_begin = submit_begin = submit_end = 0;
        for (auto& t : phase_ticks) t = 0;
        setup_ticks = submit_ticks = 0;
        views = 0;
    }
    // Cumulative intersection with submission intervals, sampled using a sibling's
    // existing QPC. Owner-thread only; no additional clock or boundary callback.
    std::uint64_t submission_ticks_at(std::uint64_t now) const noexcept {
        return submit_ticks + (live && submit_begin && now >= submit_begin ? now - submit_begin : 0);
    }
    void drop() noexcept {
        if (live) ++dropped;
        live = false;
    }
    bool advance(unsigned to, std::uint64_t qpc) noexcept {
        if (qpc < last_qpc) {
            ++clock_errors;
            drop();
            return false;
        }
        phase_ticks[phase] += qpc - last_qpc;
        last_qpc = qpc;
        phase = to;
        return true;
    }
    // Engine stamp `index` (frame_phase_sites.h order) at `qpc`.
    void site(unsigned index, std::uint64_t qpc) noexcept {
        if (index >= site_count) {
            ++unmatched;
            return;
        }
        if (!live) {
            ++unmatched;
            return;
        }
        if (index < core_sites) {
            const unsigned to = index + 1;
            if (to <= phase) {
                ++order_errors;
                drop();
                return;
            }
            if (advance(to, qpc)) fired |= 1u << index;
            return;
        }
        if (qpc < last_qpc) {
            ++clock_errors;
            drop();
            return;
        }
        if (index == core_sites) {
            setup_begin = qpc;
            ++views;
        } else if (index == core_sites + 1) {
            if (setup_begin && qpc >= setup_begin) setup_ticks += qpc - setup_begin;
            setup_begin = 0;
            submit_begin = qpc;
        } else {
            if (submit_begin && qpc >= submit_begin) submit_ticks += qpc - submit_begin;
            submit_begin = 0;
            submit_end = qpc;
        }
    }
    // Ahead of the forwarded native Present.
    void present_begin(std::uint64_t qpc) noexcept {
        if (!live) return;
        if (phase == present_phase) {
            ++order_errors;
            drop();
            return;
        }
        advance(present_phase, qpc);
    }
    // After the native Present returned: closes the frame and starts the next.
    void present_end(std::uint64_t qpc) noexcept {
        if (live && phase == present_phase && advance(pre_render, qpc)) {
            for (unsigned i = 0; i < phase_count; ++i) closed.phase_ticks[i] = phase_ticks[i];
            closed.setup_ticks = setup_ticks;
            closed.submit_ticks = submit_ticks;
            closed.views = views;
            closed.complete = fired == (1u << core_sites) - 1;
            pending = true;
        } else if (live) {
            ++order_errors;
            drop();
        }
        restart(qpc);
    }
    // Takes the closed frame as a sample in microseconds; false when none.
    bool take(std::uint64_t frame, std::uint64_t frequency, Sample& out) noexcept {
        if (!pending || !frequency) return false;
        pending = false;
        out = Sample{};
        out.frame = frame;
        for (unsigned i = 0; i < phase_count; ++i) {
            out.phase_us[i] = closed.phase_ticks[i] * 1000000ull / frequency;
            out.dt_us += out.phase_us[i];
        }
        out.view_setup_us = closed.setup_ticks * 1000000ull / frequency;
        out.view_submit_us = closed.submit_ticks * 1000000ull / frequency;
        out.views = closed.views;
        out.complete = closed.complete;
        return true;
    }
};

struct Summary {
    std::uint64_t frame = 0;
    std::uint32_t frames = 0, incomplete = 0;
    std::uint64_t dt_p50 = 0, dt_p95 = 0;
    std::uint64_t phase_p50[phase_count]{}, phase_p95[phase_count]{};
    std::uint64_t view_setup_p50 = 0, view_setup_p95 = 0, view_submit_p50 = 0, view_submit_p95 = 0;
    std::uint64_t views_p50 = 0;
    Sample slow_frames[slow_slots]{};
    std::uint32_t slow_frames_count = 0; // slowest first, by dt
};

// Fixed 300-sample window: `add` is a store plus at most four comparisons;
// `close` runs one std::nth_element per statistic, once per window.
class Window {
public:
    unsigned count() const noexcept { return count_; }
    bool full() const noexcept { return count_ >= window_frames; }
    void add(const Sample& s) noexcept {
        if (count_ < window_frames) {
            dt_[count_] = s.dt_us;
            for (unsigned i = 0; i < phase_count; ++i) phase_[i][count_] = s.phase_us[i];
            setup_[count_] = s.view_setup_us;
            submit_[count_] = s.view_submit_us;
            views_[count_] = s.views;
            if (!s.complete) ++incomplete_;
            ++count_;
        }
        last_frame_ = s.frame;
        unsigned slot = slow_count_;
        while (slot > 0 && slow_[slot - 1].dt_us < s.dt_us) --slot;
        if (slot >= slow_slots) return;
        for (unsigned i = slow_count_ < slow_slots ? slow_count_ : slow_slots - 1; i > slot; --i)
            slow_[i] = slow_[i - 1];
        slow_[slot] = s;
        if (slow_count_ < slow_slots) ++slow_count_;
    }
    bool close(Summary& out) noexcept {
        if (!count_) {
            reset();
            return false;
        }
        out = Summary{};
        out.frame = last_frame_;
        out.frames = count_;
        out.incomplete = incomplete_;
        out.dt_p50 = percentile(dt_, 50);
        out.dt_p95 = percentile(dt_, 95);
        for (unsigned i = 0; i < phase_count; ++i) {
            out.phase_p50[i] = percentile(phase_[i], 50);
            out.phase_p95[i] = percentile(phase_[i], 95);
        }
        out.view_setup_p50 = percentile(setup_, 50);
        out.view_setup_p95 = percentile(setup_, 95);
        out.view_submit_p50 = percentile(submit_, 50);
        out.view_submit_p95 = percentile(submit_, 95);
        out.views_p50 = percentile(views_, 50);
        out.slow_frames_count = slow_count_;
        for (unsigned i = 0; i < slow_count_; ++i) out.slow_frames[i] = slow_[i];
        reset();
        return true;
    }
    void reset() noexcept {
        count_ = slow_count_ = incomplete_ = 0;
        last_frame_ = 0;
    }

private:
    std::uint64_t percentile(const std::uint64_t* values, unsigned p) noexcept {
        std::copy(values, values + count_, scratch_);
        std::size_t index = (static_cast<std::size_t>(count_) * p) / 100;
        if (index >= count_) index = count_ - 1;
        std::nth_element(scratch_, scratch_ + index, scratch_ + count_);
        return scratch_[index];
    }
    std::uint64_t dt_[window_frames]{}, phase_[phase_count][window_frames]{};
    std::uint64_t setup_[window_frames]{}, submit_[window_frames]{}, views_[window_frames]{};
    std::uint64_t scratch_[window_frames]{};
    Sample slow_[slow_slots]{};
    unsigned count_ = 0, slow_count_ = 0, incomplete_ = 0;
    std::uint64_t last_frame_ = 0;
};
}
