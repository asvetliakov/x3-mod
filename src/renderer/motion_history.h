#pragma once
// Conservative CPU correspondence for deferred rigid-motion replay. No D3D/COM
// ownership. All access, source-buffer writes and GPU replay must be serialized.
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace x3m::renderer {
using SubmittedMatrix = std::array<float, 16>; // Exact shader-register rows.

// Pass identifier of RigidDrawKey::pass. The live route produces PassMainScene
// only (the selector's Scene phase on the latched main color/depth pair, ended
// by the engine scene-end hook or the bloom copy); the other values are
// reserved so a second pass drawing the same node/buffers/range in one frame
// (a depth prepass or a shadow pass) can be keyed apart before it exists.
// Environment-map faces are never keyed (the selector rejects those frames).
// PassUnknown never keys the live route; the capture/replay path leaves it 0.
enum MotionPass : std::uint32_t {
    PassUnknown = 0,
    PassMainScene = 1,
    PassDepthOnly = 2,
    PassShadow = 3,
    PassEnvironmentMap = 4
};

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
    std::uint32_t pass = PassUnknown; // MotionPass, from the route's boundary state.
};

enum RigidProof : std::uint32_t {
    LifetimeVerified = 1,  // Both object and camera lifetimes, including reuse.
    GeometryUnchanged = 2, // Complete known VB/IB revisions; no pending writes.
    PositionReviewed = 4,  // Exact ordinary POSITION.xyz/W=1 shader semantics.
    CoverageSupported = 8, // Opaque, ordinary depth/raster path; no instancing.
    SubmissionSucceeded = 16,
    AllRigidProofs = 31
};
struct RigidObservation {
    RigidDrawKey key{};
    SubmittedMatrix submitted_wvp{}; // Actual rows, never reconstructed W*V*P.
    std::uint32_t proofs = 0;        // Unknown is ineligible, not camera-only fallback.
};
enum class MotionHistoryPurpose { TemporalAccumulation, DiagnosticStorageCorrespondence };
enum class MotionContinuity { Unknown, Discontinuity, Continuous };
struct MotionFrame {
    // Observed identity/resource/coordinate domain. An unchanged epoch does not
    // itself prove camera continuity. Reload/reset and domain changes advance it.
    std::uint64_t frame = 0, epoch = 0;
    std::uint32_t width = 0, height = 0;
    // Caller evidence about the transition FROM the immediately preceding frame.
    // Unknown never permits temporal accumulation; a cut clears both purposes.
    MotionContinuity continuity = MotionContinuity::Unknown;
};
enum class Correspondence {
    Matched,
    NotSealed,
    MissingCurrent,
    InvalidKey,
    MissingProof,
    InvalidMatrix,
    Ambiguous,
    NoPreviousFrame,
    MissingPrevious
};
struct RigidMotionPair {
    Correspondence status = Correspondence::NotSealed;
    MotionHistoryPurpose purpose = MotionHistoryPurpose::TemporalAccumulation;
    MotionContinuity continuity = MotionContinuity::Unknown;
    SubmittedMatrix current{}, previous{};
    // Only caller continuity evidence, not color/depth/jitter/exposure readiness.
    bool temporal_continuity_attested() const noexcept {
        return status == Correspondence::Matched && purpose == MotionHistoryPurpose::TemporalAccumulation &&
               continuity == MotionContinuity::Continuous;
    }
};

// Collect ALL current observations, seal, then query pairs for GPU replay.
// Two phases prevent a late conflicting duplicate from invalidating motion that
// was already emitted. commit(true) requires a successful configured producer
// and frame/presentation boundary; failure invalidates both generations.
// Diagnostic mode commits matrix observations only, never temporal-color history.
// Temporal mode also requires its complete motion/resolve/presentation transaction;
// unknown continuity clears previous pairing but may seed the current frame.
// The caller must additionally verify buffers remain unchanged until replay,
// provide jitter metadata, and reject/react to unsupported color contributors.
// This class cannot discover engine lifetime, semantic or visibility proofs.
class MotionHistory {
public:
    explicit MotionHistory(std::size_t capacity = 8192,
                           MotionHistoryPurpose purpose = MotionHistoryPurpose::TemporalAccumulation) noexcept;
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
    MotionHistoryPurpose purpose_;
    Phase phase_ = Phase::Idle;
    bool previous_valid_ = false;
};
} // namespace x3m::renderer
