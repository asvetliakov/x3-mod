#pragma once
// Conservative recognizer for the observed X3 scene/bloom/clear sequence.
// No D3D/COM calls or allocations. One instance per logical device; events are
// completed application calls in order, never the renderer's own injected calls.
// Unknown/failed queries, missing events or unexpected operations fail closed.
#include <cstdint>

namespace x3m::renderer {

struct ShaderPair { std::uint64_t vs = 0, ps = 0; };
// Stored by value. Custom profiles are for verified versions or original test
// shaders; the default retains only the observed X3 hash pairs.
struct SceneSignatures {
    ShaderPair background[3] = {
        {0x7b6393fe2d3e1d85ull, 0x6109cf64c03529ddull},
        {0x37c34a7478544c14ull, 0x5f82ecacd39529cdull},
        {0xbe199829a9bb78dbull, 0xcd6d6eb4b3d99443ull}}; // Haze is one allowed background family, not a mandatory draw.
    ShaderPair bloom[4] = {
        {0xcbbf26102694c961ull, 0x1c90e79667bdaddfull},
        {0x6059306306203243ull, 0xf3172baa8dd19a40ull},
        {0x6059306306203243ull, 0x241c3fa33270f58eull},
        {0x1279d081455f5815ull, 0xff6eed5a5ddf3a3aull}};
};

struct Surface {
    bool known = false;             // Query succeeded, including a known null binding.
    std::uint64_t identity = 0;     // Lifetime ID, not a reusable COM address.
    std::uint64_t container = 0;    // Texture lifetime ID; 0 for ordinary surfaces.
    std::uint32_t width = 0, height = 0, format = 0, msaa = 0;
};
struct Viewport {
    bool known = false;
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
    float min_z = 0, max_z = 1;
};
enum class EventKind { Clear, Draw, SetRenderTarget, SetDepth, Copy, Unsupported, ColorFill };
struct Event {
    EventKind kind = EventKind::Unsupported;
    std::uint64_t sequence = 0;     // Starts at 1, contiguous within begin_frame().
    bool result_known = false;
    std::uint32_t result = 0;       // HRESULT bits from the completed application call.
    Surface rt, depth;             // Actual bindings for Clear/Draw; new binding for Set*.
    Viewport viewport;
    bool only_rt0 = false;          // Known absence of extra MRTs for Clear/Draw.
    std::uint32_t rt_index = 0;
    std::uint64_t vs = 0, ps = 0, texture0 = 0;
    bool draw_state_known = false; // Required shader/state/texture queries all succeeded.
    std::uint32_t topology = 0, primitives = 0, z_enable = 0, z_write = 0;
    std::uint32_t clear_flags = 0, rect_count = 0;
    float clear_z = 0;
    Surface source, destination;
    bool source_rect_null = false, destination_rect_null = false;
};
struct Selection {
    bool valid = false;
    std::uint64_t device = 0, generation = 0, frame = 0, sequence = 0;
    std::uint64_t depth_epoch = 0;  // Local to this frame, not ownership's epoch counter.
    Surface color, depth;          // Post-bloom/pre-overlay color; main depth epoch.
};
enum class BoundaryState {
    AwaitInitialClear, Background, Scene, AwaitCopy, AwaitBloomTarget,
    AwaitBloomDraw, AwaitDepthRebind, AwaitFinalClear, Selected, Rejected
};
enum class BoundaryRejection { None, InvalidFrame, Invalidated, Sequence, FailedCall, Pattern };

class SceneBoundarySelector {
public:
    explicit SceneBoundarySelector(const SceneSignatures& signatures = SceneSignatures{})
        : signatures_(signatures) {}
    void begin_frame(std::uint64_t device, std::uint64_t generation, std::uint64_t frame) {
        *this = SceneBoundarySelector{signatures_};
        state_ = BoundaryState::AwaitInitialClear; rejection_ = BoundaryRejection::None;
        device_ = device; generation_ = generation; frame_ = frame;
        if (!device || !generation) reject(BoundaryRejection::InvalidFrame);
    }
    // Required on loss/Reset/resource-identity uncertainty, even without a frame end.
    // Start again only after a new begin_frame with the current resource generation.
    void invalidate() { reject(BoundaryRejection::Invalidated); }
    BoundaryState state() const { return state_; }
    BoundaryRejection rejection() const { return rejection_; }
    std::uint64_t last_sequence() const { return sequence_; }
    std::uint64_t rejection_sequence() const { return rejection_sequence_; }

    // Called BEFORE the application's Clear. Its result is necessarily unknown.
    // This is permission to preserve a candidate, never to publish valid history.
    Selection before_clear(const Event& pending) const {
        if (state_ != BoundaryState::AwaitFinalClear || pending.sequence != sequence_ + 1 ||
            pending.kind != EventKind::Clear || !depth_clear(pending)) return {};
        return {true, device_, generation_, frame_, pending.sequence, epoch_, main_, depth_};
    }
    // Called AFTER the application call. A valid return confirms the same boundary
    // only after Clear succeeds; caller must separately verify its pre-call copy.
    Selection observe(const Event& event) {
        if (state_ == BoundaryState::Rejected) return {};
        if (event.sequence != sequence_ + 1) { reject(BoundaryRejection::Sequence, event.sequence); return {}; }
        const Selection confirmed = before_clear(event);
        sequence_ = event.sequence;
        if (!event.result_known || (event.result & 0x80000000u)) {
            reject(BoundaryRejection::FailedCall); return {};
        }
        // Untracked writes and scratch writes outside their proven harmless phase
        // still invalidate a selected candidate; ordinary overlay draws may follow.
        if (event.kind == EventKind::Unsupported ||
            (event.kind == EventKind::ColorFill && state_ != BoundaryState::AwaitCopy)) {
            reject(BoundaryRejection::Pattern); return {};
        }
        if (state_ == BoundaryState::Selected) return {}; // Exactly one selection per frame.
        if (!advance(event)) { reject(BoundaryRejection::Pattern); return {}; }
        return confirmed;
    }

private:
    SceneSignatures signatures_;
    BoundaryState state_ = BoundaryState::Rejected;
    BoundaryRejection rejection_ = BoundaryRejection::InvalidFrame;
    std::uint64_t device_ = 0, generation_ = 0, frame_ = 0, sequence_ = 0, epoch_ = 0, rejection_sequence_ = 0;
    Surface main_, depth_, copied_, a_, b_, bound_rt_;
    bool background_draw_ = false, scene_writer_ = false;
    unsigned bloom_ = 0;
    void reject(BoundaryRejection why, std::uint64_t event_sequence = 0) {
        if (state_ == BoundaryState::Rejected) return; // Preserve the first cause.
        state_ = BoundaryState::Rejected; rejection_ = why;
        rejection_sequence_ = event_sequence ? event_sequence : sequence_;
    }
    static bool same(const Surface& a, const Surface& b) {
        return a.known && b.known && a.identity && a.identity == b.identity &&
               a.container == b.container && a.width == b.width && a.height == b.height &&
               a.format == b.format && a.msaa == b.msaa;
    }
    static bool color(const Surface& s) {
        return s.known && s.identity && s.width && s.height && s.format == 21 && !s.msaa;
    }
    static bool full(const Event& e) {
        return e.only_rt0 && e.viewport.known && e.viewport.x == 0 && e.viewport.y == 0 &&
               e.viewport.width == e.rt.width && e.viewport.height == e.rt.height &&
               e.viewport.min_z == 0 && e.viewport.max_z == 1;
    }
    bool depth_clear(const Event& e) const {
        return e.clear_flags == 2 && e.rect_count == 0 && e.clear_z == 1 && full(e) &&
               same(e.rt, main_) && same(e.depth, depth_);
    }
    bool scene_draw(const Event& e) const {
        return e.draw_state_known && e.primitives && e.vs && e.ps && full(e) &&
               same(e.rt, main_) && same(e.depth, depth_) && e.z_enable <= 1 && e.z_write <= 1;
    }
    static bool matches(const Event& e, const ShaderPair& pair) {
        return pair.vs && pair.ps && e.vs == pair.vs && e.ps == pair.ps;
    }
    bool background_pair(const Event& e) const {
        for (const auto& pair : signatures_.background) if (matches(e, pair)) return true;
        return false;
    }
    static bool aliases(const Surface& a, const Surface& b) {
        return a.identity == b.identity || (a.container && b.container && a.container == b.container);
    }
    bool scratch_color(const Surface& s) const {
        // Require a positively identified color texture. Unknown standalone
        // surfaces are not accepted until a trace establishes their provenance.
        return color(s) && s.container && !aliases(s, main_) && !aliases(s, depth_);
    }
    bool half_target(const Surface& s) const {
        return color(s) && s.container && s.identity != main_.identity &&
               s.identity != copied_.identity && s.container != copied_.container &&
               std::uint64_t(s.width) * 2 == main_.width &&
               std::uint64_t(s.height) * 2 == main_.height;
    }
    bool advance(const Event& e) {
        switch (state_) {
        case BoundaryState::AwaitInitialClear:
            if (e.kind != EventKind::Clear || e.clear_flags != 3 || e.rect_count || e.clear_z != 1 ||
                !full(e) || !color(e.rt) || !e.depth.known || !e.depth.identity ||
                e.depth.format != 77 || e.depth.msaa || e.depth.width != e.rt.width ||
                e.depth.height != e.rt.height || e.depth.identity == e.rt.identity) return false;
            main_ = e.rt; depth_ = e.depth; epoch_ = 1;
            state_ = BoundaryState::Background; return true;
        case BoundaryState::Background:
            if (e.kind == EventKind::Draw && scene_draw(e) && background_pair(e)) {
                background_draw_ = true; return true;
            }
            if (e.kind != EventKind::Clear || !background_draw_ || !depth_clear(e)) return false;
            ++epoch_; state_ = BoundaryState::Scene; return true;
        case BoundaryState::Scene:
            if (e.kind == EventKind::Draw && scene_draw(e)) {
                scene_writer_ |= e.z_enable == 1 && e.z_write == 1; return true;
            }
            if (e.kind != EventKind::SetDepth || !e.depth.known || e.depth.identity || !scene_writer_) return false;
            state_ = BoundaryState::AwaitCopy; return true;
        case BoundaryState::AwaitCopy:
            if (e.kind == EventKind::ColorFill) return scratch_color(e.destination);
            if (e.kind != EventKind::Copy || !same(e.source, main_) || !color(e.destination) ||
                !e.destination.container || e.destination.identity == main_.identity ||
                e.destination.width != main_.width || e.destination.height != main_.height ||
                !e.source_rect_null || !e.destination_rect_null) return false;
            copied_ = e.destination; state_ = BoundaryState::AwaitBloomTarget; return true;
        case BoundaryState::AwaitBloomTarget:
            if (e.kind != EventKind::SetRenderTarget || e.rt_index) return false;
            if (bloom_ == 0) { if (!half_target(e.rt)) return false; a_ = e.rt; }
            else if (bloom_ == 1) {
                if (!half_target(e.rt) || e.rt.identity == a_.identity || e.rt.container == a_.container) return false;
                b_ = e.rt;
            } else if (!same(e.rt, bloom_ == 2 ? a_ : main_)) return false;
            bound_rt_ = e.rt; state_ = BoundaryState::AwaitBloomDraw; return true;
        case BoundaryState::AwaitBloomDraw: {
            const auto input = bloom_ == 0 ? copied_.container : (bloom_ == 2 ? b_.container : a_.container);
            if (e.kind != EventKind::Draw || !e.draw_state_known || !same(e.rt, bound_rt_) ||
                !e.depth.known || e.depth.identity || !full(e) || e.topology != 5 || e.primitives != 2 ||
                e.z_enable || e.z_write || !matches(e, signatures_.bloom[bloom_]) || e.texture0 != input) return false;
            ++bloom_; state_ = bloom_ == 4 ? BoundaryState::AwaitDepthRebind : BoundaryState::AwaitBloomTarget;
            return true;
        }
        case BoundaryState::AwaitDepthRebind:
            if (e.kind != EventKind::SetDepth || !same(e.depth, depth_)) return false;
            state_ = BoundaryState::AwaitFinalClear; return true;
        case BoundaryState::AwaitFinalClear:
            if (e.kind != EventKind::Clear || !depth_clear(e)) return false;
            state_ = BoundaryState::Selected; return true;
        default: return false;
        }
    }
};
} // namespace x3m::renderer
