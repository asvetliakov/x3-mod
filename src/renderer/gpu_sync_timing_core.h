#pragma once
// Serialised GPU pass timing, the D3D-free part (X3M_GPU_SYNC_TIMING=1;
// docs/architecture/engine-frame-time.md, "GPU sync timing"). The owner
// (gpu_sync_timing.cpp) issues one D3DQUERYTYPE_EVENT at each pass boundary
// and spins on GetData(D3DGETDATA_FLUSH) until the GPU has retired everything
// before it; this file turns the QueryPerformanceCounter stamps taken after
// each spin into per-pass spans. A pass's span is end-stamp minus begin-stamp:
// the begin spin drained all earlier work, so the span is the pass's CPU
// submission plus its GPU execution with nothing overlapping (a serialised
// frame). Several begin/end pairs of one pass in a frame add up (the HDR
// flushes, bloom prepare + commit); Scene and Engine take only their first
// begin per frame. A pass still open at the frame's end, or any pass of a
// frame whose sync failed, is dropped. Aggregation: a fixed window (300
// frames by default) with exact nearest-rank median / p90 per pass and of the
// Present-to-Present CPU dt, plus a session histogram (exact below 64 us, 16
// buckets per octave above: <= 6.25 % bucket width) for the session figures.
// Integers only (no x87 by construction), no heap, no OS calls, no D3D types:
// the host test executes this file directly.
#include <algorithm>
#include <cstdint>

namespace x3m::gpu_sync_timing {
enum Pass : unsigned {
    Scene = 0,        // first proxy work after the frame's first BeginScene .. just before the native Present
    Engine,           // the frame's first BeginScene .. the scene end (the engine's own draw span; nests FogFill and HdrReadback)
    ShadowDepth,      // the shadow depth replay (cascades or single map)
    SunApply,         // the sun-shadow apply quad
    Retention,        // the caster retention's scene-end walk
    FogFill,          // the stored-density upload/prepare at the HDR latch
    FogRoute,         // the fog pass transaction on the routed FP16 target (nests Motes)
    Motes,            // the dust-mote draw inside the fog transaction
    Taa,              // the temporal resolve (nests the five taa_* sub-passes below)
    HdrWriteback,     // the FP16 write-back / tonemap (nests Meter)
    Meter,            // the exposure meter chain inside the write-back
    HdrReadback,      // the previous frame's meter readback at the HDR latch
    Bloom,            // the bloom prepare and commit
    Present,          // the proxy's Present work and the native Present
    // Sub-passes inside Taa (TemporalPass::run), appended so the indices above stay: each pair adds its sync floor to Taa.
    TaaCopy,          // input copies: colour scratch / staging, depth StretchRect or copy draw, depth decode, supplemental snapshot
    TaaMask,          // the stabiliser mask draws (far / thin region, one or three full-screen draws)
    TaaBox,           // the camera gate's 7x7 box: the 49-tap draw or the sentinel stabiliser's rows + columns
    TaaResolve,       // the resolve draw (with the age target) and the required-mask snapshot
    TaaDisplay,       // the 8-bit route's display draw: the post-resolve sharpen or the identity copy
    // Sub-passes inside FogRoute (FogPass::execute, stored path), appended likewise: each pair adds its sync floor to FogRoute.
    FogMarch,         // the half-resolution march quad
    FogComposite,     // the full-resolution composite quad
    FogRepair,        // the full-resolution repair quad (the pixel census's occlusion query brackets it)
    pass_count
};
constexpr unsigned boundary_count = 2 * pass_count; // one event query per boundary (begin, end) of every pass
static_assert(pass_count <= 32, "the Tracker's open / begun / closed masks are 32-bit");
constexpr unsigned window_frames_default = 300, window_frames_max = 300;
inline const char* pass_name(unsigned pass) noexcept {
    static constexpr const char* names[pass_count] = {"scene", "engine", "shadow_depth", "sun_apply", "retention", "fog_fill", "fog_route",
                                                      "motes", "taa", "hdr_writeback", "meter", "hdr_readback", "bloom", "present",
                                                      "taa_copy", "taa_mask", "taa_box", "taa_resolve", "taa_display",
                                                      "fog_march", "fog_composite", "fog_repair"};
    return pass < pass_count ? names[pass] : "?";
}
constexpr bool once_per_frame(unsigned pass) noexcept { return pass == Scene || pass == Engine; }

// The boundary pass hooks call. The production owner implements it; a caller
// holds a null pointer while the option is off (one branch per boundary).
struct Marks {
    virtual void begin(unsigned pass) noexcept = 0;
    virtual void end(unsigned pass) noexcept = 0;
    // Pixel census of one draw (the fog repair quad): the owner brackets it with one occlusion query and reads the
    // count at the frame's end; `area` is the draw's full pixel count. The first bracket per frame counts. Default: nothing.
    virtual void census_begin() noexcept {}
    virtual void census_end(std::uint32_t area) noexcept { (void)area; }
    // The needs-repair census (fog-gpu-cost.md, step C): a second occlusion bracket around a diagnostic quad that keeps
    // exactly the pixels the repair marches at march spacing `scale` (2 or 4). The caller draws that quad only while
    // needs_wanted() is true (this frame's bracket unspent and counted), so a frame without it adds no GPU work.
    virtual bool needs_wanted() noexcept { return false; }
    virtual void needs_begin() noexcept {}
    virtual void needs_end(std::uint32_t area, std::uint32_t scale) noexcept { (void)area; (void)scale; }
protected:
    ~Marks() = default;
};
// A begin/end pair around a scope with several exits; nothing when `marks` is null.
class Span {
public:
    Span(Marks* marks, unsigned pass) noexcept : marks_(marks), pass_(pass) { if (marks_) marks_->begin(pass_); }
    ~Span() { if (marks_) marks_->end(pass_); }
    Span(const Span&) = delete; Span& operator=(const Span&) = delete;
private:
    Marks* marks_; unsigned pass_;
};
// A census bracket around one draw; nothing when `marks` is null. Nest it inside the draw's Span so the end
// boundary's sync also retires the query.
class CensusSpan {
public:
    CensusSpan(Marks* marks, std::uint32_t area) noexcept : marks_(marks), area_(area) { if (marks_) marks_->census_begin(); }
    ~CensusSpan() { if (marks_) marks_->census_end(area_); }
    CensusSpan(const CensusSpan&) = delete; CensusSpan& operator=(const CensusSpan&) = delete;
private:
    Marks* marks_; std::uint32_t area_;
};
// The needs-repair bracket around its quad; nothing when `marks` is null.
class NeedsSpan {
public:
    NeedsSpan(Marks* marks, std::uint32_t area, std::uint32_t scale) noexcept : marks_(marks), area_(area), scale_(scale) { if (marks_) marks_->needs_begin(); }
    ~NeedsSpan() { if (marks_) marks_->needs_end(area_, scale_); }
    NeedsSpan(const NeedsSpan&) = delete; NeedsSpan& operator=(const NeedsSpan&) = delete;
private:
    Marks* marks_; std::uint32_t area_, scale_;
};

// Ticks of a counter at `frequency` Hz as microseconds, saturated to 32 bits.
inline std::uint32_t ticks_to_us(std::uint64_t ticks, std::uint64_t frequency) noexcept {
    if (!frequency) return 0;
    const std::uint64_t whole = ticks / frequency, rest = ticks % frequency;
    if (whole >= 4295u) return 0xffffffffu;
    const std::uint64_t us = whole * 1000000u + rest * 1000000u / frequency;
    return us > 0xffffffffu ? 0xffffffffu : std::uint32_t(us);
}

// Session histogram buckets: 0..63 us exact, then 16 per octave up to 2^32 us.
constexpr unsigned histogram_linear = 64, histogram_sub = 16, histogram_buckets = histogram_linear + (32 - 6) * histogram_sub; // 480
inline unsigned bucket_of(std::uint32_t us) noexcept {
    if (us < histogram_linear) return us;
    const unsigned e = 31u - unsigned(__builtin_clz(us)); // 6..31
    return histogram_linear + (e - 6u) * histogram_sub + ((us >> (e - 4u)) & (histogram_sub - 1u));
}
inline std::uint32_t bucket_low(unsigned bucket) noexcept {
    if (bucket < histogram_linear) return bucket;
    const unsigned e = (bucket - histogram_linear) / histogram_sub + 6u, sub = (bucket - histogram_linear) % histogram_sub;
    return (histogram_sub + sub) << (e - 4u);
}
// The value a bucket reports: exact below 64 us, the bucket's midpoint above.
inline std::uint32_t bucket_value(unsigned bucket) noexcept {
    if (bucket < histogram_linear) return bucket;
    const unsigned e = (bucket - histogram_linear) / histogram_sub + 6u;
    return bucket_low(bucket) + ((1u << (e - 4u)) >> 1);
}
// A census count as parts per million of `area` (0 for an empty area, saturated at 1e6).
inline std::uint32_t census_ppm(std::uint32_t pixels, std::uint32_t area) noexcept {
    if (!area) return 0;
    const std::uint64_t ppm = std::uint64_t(pixels) * 1000000u / area;
    return ppm > 1000000u ? 1000000u : std::uint32_t(ppm);
}
// Nearest rank ceil(q n / 100), 1-based, q in 1..100.
inline std::uint32_t rank_of(std::uint32_t n, unsigned q) noexcept {
    const std::uint64_t r = (std::uint64_t(n) * q + 99u) / 100u;
    return r < 1 ? 1u : r > n ? n : std::uint32_t(r);
}

struct Stat { std::uint32_t n = 0, median = 0, p90 = 0; };
struct PassReport { Stat window, session; std::uint32_t wait_median = 0; };
// The window's pixel census: per frame with a counted draw, the count as ppm of its area (median, p90, max) and
// the last frame's area and count; for counted draws of kept frames whose result was not read: unread (not ready at
// the frame end), lost (the device was lost), failed (any other refusal).
// px: the same frames as pixel counts; tag: the last counted frame's tag (the needs census: the march spacing).
struct CensusReport { Stat ppm, px; std::uint32_t max_ppm = 0, max_px = 0, area = 0, pixels = 0, tag = 0, unread = 0, lost = 0, failed = 0; };
enum CensusRead : unsigned { CensusNone = 0, CensusOk, CensusNotReady, CensusLost, CensusFailed };
// One census counter's window: the frame's pending read, filed with the frame, and the window's samples.
class CensusWindow {
public:
    void pending(std::uint32_t pixels, std::uint32_t area, std::uint32_t tag, CensusRead read) noexcept { read_ = read; pixels_ = pixels; area_ = area; tag_ = tag; }
    void file() noexcept {
        if (read_ == CensusNotReady) ++unread_;
        else if (read_ == CensusLost) ++lost_;
        else if (read_ == CensusFailed) ++failed_;
        else if (read_ == CensusOk && count_ < window_frames_max) {
            const std::uint32_t ppm = census_ppm(pixels_, area_);
            ppm_[count_] = ppm; px_[count_] = pixels_; ++count_;
            max_ppm_ = std::max(max_ppm_, ppm); max_px_ = std::max(max_px_, pixels_);
            last_area_ = area_; last_pixels_ = pixels_; last_tag_ = tag_;
        }
        read_ = CensusNone;
    }
    void drop_pending() noexcept { read_ = CensusNone; }
    template<class Exact> CensusReport take(Exact exact) noexcept {
        CensusReport r; r.ppm = exact(ppm_, count_); r.px = exact(px_, count_); r.max_ppm = max_ppm_; r.max_px = max_px_;
        r.area = last_area_; r.pixels = last_pixels_; r.tag = last_tag_; r.unread = unread_; r.lost = lost_; r.failed = failed_;
        count_ = max_ppm_ = max_px_ = last_area_ = last_pixels_ = last_tag_ = unread_ = lost_ = failed_ = 0;
        return r;
    }
private:
    CensusRead read_ = CensusNone;
    std::uint32_t pixels_ = 0, area_ = 0, tag_ = 0;
    std::uint32_t ppm_[window_frames_max]{}, px_[window_frames_max]{}, count_ = 0, max_ppm_ = 0, max_px_ = 0, last_area_ = 0, last_pixels_ = 0, last_tag_ = 0;
    std::uint32_t unread_ = 0, lost_ = 0, failed_ = 0;
};
struct Report {
    std::uint64_t window = 0, first_frame = 0, last_frame = 0;
    std::uint32_t frames = 0, dropped = 0, unclosed = 0; // frames in the window; frames dropped by a failed sync; passes open at a frame end
    PassReport pass[pass_count]{};
    Stat dt_window, dt_session;
    CensusReport census; // the repair's written pixels (step A)
    CensusReport needs;  // the pixels the repair marches at the drawn spacing (step C; tag = the spacing)
};

class Tracker {
public:
    void configure(unsigned window) noexcept { window_ = window < 1 ? 1u : window > window_frames_max ? window_frames_max : window; }
    unsigned window() const noexcept { return window_; }
    // True when a begin/end of `pass` would be recorded now: the owner syncs only then.
    bool wants_begin(unsigned pass) const noexcept {
        if (pass >= pass_count || abandoned_) return false;
        const std::uint32_t bit = 1u << pass;
        return !(open_ & bit) && !(once_per_frame(pass) && (begun_ & bit));
    }
    bool wants_end(unsigned pass) const noexcept { return pass < pass_count && !abandoned_ && (open_ & (1u << pass)); }
    void begin(unsigned pass, std::uint64_t stamp) noexcept {
        if (!wants_begin(pass)) return;
        open_ |= 1u << pass; begun_ |= 1u << pass; begin_[pass] = stamp;
    }
    void end(unsigned pass, std::uint64_t stamp, std::uint64_t wait) noexcept {
        if (!wants_end(pass)) return;
        open_ &= ~(1u << pass); closed_ |= 1u << pass;
        sum_[pass] += stamp >= begin_[pass] ? stamp - begin_[pass] : 0u;
        wait_[pass] += wait;
    }
    // The frame's census count (the owner reads it just before frame()); filed with the frame unless it is abandoned.
    // `read`: CensusOk, or why the counted draw's result was not available (pixels ignored then).
    void census(std::uint32_t pixels, std::uint32_t area, CensusRead read) noexcept { census_.pending(pixels, area, 0, read); }
    // The frame's needs-repair count at march spacing `scale`, filed like census().
    void needs(std::uint32_t pixels, std::uint32_t area, std::uint32_t scale, CensusRead read) noexcept { needs_.pending(pixels, area, scale, read); }
    // A failed sync: nothing of this frame is recorded and no boundary syncs again until frame().
    void abandon_frame() noexcept { abandoned_ = true; open_ = 0; }
    // Device Reset or release: the frame's marks go and the next dt has no predecessor.
    void clear_frame() noexcept { clear_marks(); has_last_ = false; }
    // The frame's end (after the native Present): files the closed passes and the
    // Present-to-Present dt (not for an abandoned frame). True when the window is complete (take report()).
    bool frame(std::uint64_t frame_index, std::uint64_t stamp, std::uint64_t frequency) noexcept {
        if (!frames_) first_frame_ = frame_index;
        last_frame_ = frame_index; ++frames_;
        // An abandoned frame files no dt either (its interval holds a failed or timed-out spin).
        if (has_last_ && !abandoned_ && stamp > last_ && dt_count_ < window_frames_max) {
            const std::uint32_t us = ticks_to_us(stamp - last_, frequency);
            dt_[dt_count_++] = us; ++dt_hist_[bucket_of(us)]; ++dt_session_n_;
        }
        last_ = stamp; has_last_ = true;
        if (abandoned_) ++dropped_;
        else {
            unclosed_ += unsigned(__builtin_popcount(open_));
            for (unsigned pass = 0; pass < pass_count; ++pass) {
                if (!(closed_ & (1u << pass)) || count_[pass] >= window_frames_max) continue;
                const std::uint32_t us = ticks_to_us(sum_[pass], frequency);
                samples_[pass][count_[pass]] = us; waits_[pass][count_[pass]] = ticks_to_us(wait_[pass], frequency); ++count_[pass];
                ++hist_[pass][bucket_of(us)]; ++session_n_[pass];
            }
            census_.file(); needs_.file();
        }
        clear_marks();
        return frames_ >= window_;
    }
    // The window's figures (and the session's so far); starts the next window.
    Report report() noexcept {
        Report r = summary();
        r.window = ++windows_; r.first_frame = first_frame_; r.last_frame = last_frame_;
        r.frames = frames_; r.dropped = dropped_; r.unclosed = unclosed_;
        for (unsigned pass = 0; pass < pass_count; ++pass) {
            r.pass[pass].window = exact(samples_[pass], count_[pass]);
            r.pass[pass].wait_median = exact(waits_[pass], count_[pass]).median;
            count_[pass] = 0;
        }
        r.dt_window = exact(dt_, dt_count_);
        r.census = census_.take(&Tracker::exact); r.needs = needs_.take(&Tracker::exact);
        frames_ = dropped_ = unclosed_ = dt_count_ = 0;
        return r;
    }
    // Session figures from the histograms (window fields empty).
    Report summary() const noexcept {
        Report r; r.window = windows_; r.first_frame = first_frame_; r.last_frame = last_frame_;
        for (unsigned pass = 0; pass < pass_count; ++pass) r.pass[pass].session = histogram(hist_[pass], session_n_[pass]);
        r.dt_session = histogram(dt_hist_, dt_session_n_);
        return r;
    }
    std::uint64_t windows() const noexcept { return windows_; }
    std::uint32_t pending_frames() const noexcept { return frames_; }
    std::uint32_t open_mask() const noexcept { return open_; }
    std::uint32_t closed_mask() const noexcept { return closed_; }
    bool abandoned() const noexcept { return abandoned_; }
    static Stat exact_of(std::uint32_t* values, std::uint32_t n) noexcept { return exact(values, n); } // host test
private:
    void clear_marks() noexcept {
        open_ = begun_ = closed_ = 0; abandoned_ = false; census_.drop_pending(); needs_.drop_pending();
        for (unsigned pass = 0; pass < pass_count; ++pass) sum_[pass] = wait_[pass] = 0;
    }
    // Sorts `values` in place (the window arrays are refilled next window).
    static Stat exact(std::uint32_t* values, std::uint32_t n) noexcept {
        Stat s; s.n = n;
        if (!n) return s;
        std::sort(values, values + n);
        s.median = values[rank_of(n, 50) - 1]; s.p90 = values[rank_of(n, 90) - 1];
        return s;
    }
    static Stat histogram(const std::uint32_t* buckets, std::uint32_t n) noexcept {
        Stat s; s.n = n;
        if (!n) return s;
        const std::uint32_t r50 = rank_of(n, 50), r90 = rank_of(n, 90);
        std::uint32_t seen = 0; bool median = false;
        for (unsigned b = 0; b < histogram_buckets; ++b) {
            seen += buckets[b];
            if (!median && seen >= r50) { s.median = bucket_value(b); median = true; }
            if (seen >= r90) { s.p90 = bucket_value(b); break; }
        }
        return s;
    }
    std::uint64_t begin_[pass_count]{}, sum_[pass_count]{}, wait_[pass_count]{};
    std::uint32_t open_ = 0, begun_ = 0, closed_ = 0;
    bool abandoned_ = false, has_last_ = false;
    std::uint64_t last_ = 0, first_frame_ = 0, last_frame_ = 0, windows_ = 0;
    unsigned window_ = window_frames_default;
    std::uint32_t frames_ = 0, dropped_ = 0, unclosed_ = 0, dt_count_ = 0;
    std::uint32_t samples_[pass_count][window_frames_max]{}, waits_[pass_count][window_frames_max]{}, count_[pass_count]{};
    std::uint32_t dt_[window_frames_max]{};
    std::uint32_t hist_[pass_count][histogram_buckets]{}, session_n_[pass_count]{};
    std::uint32_t dt_hist_[histogram_buckets]{}, dt_session_n_ = 0;
    CensusWindow census_{}, needs_{};
};
} // namespace x3m::gpu_sync_timing
