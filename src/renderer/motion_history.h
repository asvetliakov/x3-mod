#pragma once
// Conservative CPU correspondence for deferred rigid-motion replay. No D3D/COM
// ownership. All access, source-buffer writes and GPU replay must be serialized.
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace x3m::renderer {
using SubmittedMatrix = std::array<float, 16>; // Exact shader-register rows.

struct RigidDrawKey {
    // Lifetimes must come from a verified lifecycle producer, not pointer values,
    // draw order, observed handle equality, or the object-trace hook session.
    std::uint64_t object_lifetime = 0, camera_lifetime = 0, draw_domain = 0;
    std::uint64_t node = 0, camera = 0, mesh = 0;
    std::uint32_t node_handle = 0, camera_handle = 0, model = 0, lod = 0;
    // Allocation IDs, not COM addresses. Program identity must already pass the
    // exact reviewed position profile; declaration identity describes the input.
    std::uint64_t vertex_buffer = 0, vertex_revision = 0;
    std::uint64_t index_buffer = 0, index_revision = 0;
    std::uint64_t declaration = 0, position_program = 0;
    std::uint32_t stream_offset = 0, stride = 0, position_offset = 0;
    std::uint32_t position_type = 0; // D3DDECLTYPE_FLOAT3=2 or FLOAT16_4=16.
    std::uint32_t topology = 0, first = 0, primitives = 0;
    std::int32_t base_vertex = 0;
    std::uint32_t min_vertex = 0, vertex_count = 0, index_format = 0;
    bool indexed = false;
};

enum RigidProof : std::uint32_t {
    LifetimeVerified = 1,      // Both object and camera lifetimes, including reuse.
    GeometryUnchanged = 2,     // Complete known VB/IB revisions; no pending writes.
    PositionReviewed = 4,     // Exact ordinary POSITION.xyz/W=1 shader semantics.
    CoverageSupported = 8,    // Opaque, ordinary depth/raster path; no instancing.
    SubmissionSucceeded = 16,
    AllRigidProofs = 31
};
struct RigidObservation {
    RigidDrawKey key{};
    SubmittedMatrix submitted_wvp{}; // Actual rows, never reconstructed W*V*P.
    std::uint32_t proofs = 0; // Unknown is ineligible, not camera-only fallback.
};
struct MotionFrame {
    // Trusted caller epoch changes on scene/camera cuts, reload/reset, resource
    // generation or exposure/coordinate-regime changes. Ordinary frames retain it.
    std::uint64_t frame = 0, epoch = 0;
    std::uint32_t width = 0, height = 0;
};
enum class Correspondence {
    Matched, NotSealed, MissingCurrent, InvalidKey, MissingProof,
    InvalidMatrix, Ambiguous, NoPreviousFrame, MissingPrevious
};
struct RigidMotionPair {
    Correspondence status = Correspondence::NotSealed;
    SubmittedMatrix current{}, previous{};
};

// Collect ALL current observations, seal, then query pairs for GPU replay.
// Two phases prevent a late conflicting duplicate from invalidating motion that
// was already emitted. commit(true) is only for a completely successful frame,
// including motion/resolve/presentation; failure invalidates both generations.
// The caller must additionally verify buffers remain unchanged until replay,
// provide jitter metadata, and reject/react to unsupported color contributors.
// This class cannot discover engine lifetime, semantic or visibility proofs.
class MotionHistory {
public:
    explicit MotionHistory(std::size_t capacity = 8192) noexcept;
    bool begin_frame(MotionFrame frame) noexcept;
    // True means stored, not eligible. Ineligible entries remain to poison any
    // duplicate key; only lookup().status == Matched authorizes correspondence.
    bool observe(const RigidObservation& observation) noexcept;
    bool seal() noexcept;
    RigidMotionPair lookup(const RigidDrawKey& key) const noexcept;
    bool commit(bool frame_succeeded) noexcept;
    void invalidate() noexcept;
    std::size_t current_size() const noexcept { return current_.size(); }
    std::size_t capacity() const noexcept { return capacity_; }

private:
    struct Entry {
        RigidDrawKey key{};
        SubmittedMatrix matrix{};
        Correspondence status = Correspondence::MissingProof;
    };
    enum class Phase { Idle, Collecting, Sealed };
    static bool less(const RigidDrawKey&, const RigidDrawKey&) noexcept;
    static bool equal(const RigidDrawKey&, const RigidDrawKey&) noexcept;
    static Correspondence validate(const RigidObservation&) noexcept;
    static const Entry* find(const std::vector<Entry>&, const RigidDrawKey&) noexcept;
    std::vector<Entry> current_, previous_;
    MotionFrame frame_{}, previous_frame_{};
    std::size_t capacity_;
    Phase phase_ = Phase::Idle;
    bool previous_valid_ = false;
};
} // namespace x3m::renderer
