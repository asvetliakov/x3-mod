#pragma once
#include <d3d9.h>
#include <array>
#include <cstdint>
#include "object_trace.h"
#include "../renderer/motion_history.h"
#include "../renderer/rigid_position.h"
#include "../renderer/rigid_motion.h"
#include "../ownership/d3d9_ownership.h"

namespace x3m {
// A live input reader shared by capture diagnostics and later motion routing.
// Reads actual application state without rewriting it or reconstructing matrices.
// Optional geometry reservations belong to the caller's ownership frame; the
// reader owns no persistent COM references and invents no lifetime/coverage proof.
enum class DrawMethod { Primitive, Indexed, UserMemory, IndexedUserMemory };
struct DrawArguments {
    DrawMethod method = DrawMethod::Primitive;
    D3DPRIMITIVETYPE topology = D3DPT_TRIANGLELIST;
    UINT primitives = 0, first = 0;
    INT base_vertex = 0;
    UINT minimum_vertex = 0, vertex_count = 0;
};
enum DrawInputBlocker : std::uint32_t {
    PositionProgram = 1, PixelCoverage = 2, PositionLayout = 4,
    BufferDescription = 8, BufferRevision = 16, DrawRange = 32,
    RasterState = 64, TargetLayout = 128, SubmittedRows = 256,
    ObjectScope = 512, UserMemory = 1024, QueryFailure = 2048,
    SubmissionFailure = 4096
};
struct DrawInput {
    // Partial local proofs only. LifetimeVerified is never supplied here.
    // Before replay the caller must consume separate finite-position evidence,
    // revalidate buffers through replay, and establish engine lifetimes and
    // whole-scene color coverage.
    renderer::RigidObservation observation{};
    // Issued from the actual submitted VS bytes before scratch is reused for PS.
    // This source association is independent of the legacy local proof mask.
    renderer::RigidReplayContract replay_source{};
    // Separate from the existing local proof mask. These describe the exact
    // allocation revisions queried now, not stability through a later replay.
    ownership::FinitePositionView finite_positions{};
    ownership::IndexRangeView indices{};
    bool index_range_verified = false, vertex_finite_verified = false;
    // Non-owning opaque value. S_FALSE also means no frame or unmet admission
    // gates. S_OK grants a caller-owned reservation in the supplied frame; the
    // caller must release it/end that frame even if the application draw fails.
    // Copying DrawInput does not acquire another lease or transfer cleanup duty.
    ownership::GeometryLeaseHandle geometry_lease{};
    HRESULT lease_status = S_FALSE;
    std::uint32_t blockers = 0;
    renderer::VertexPositionPath position_path = renderer::VertexPositionPath::Unknown;
    std::uint64_t vertex_program = 0, pixel_program = 0;
    std::uint64_t color_target = 0, depth_target = 0;
    UINT width = 0, height = 0;
    D3DCULL cull = D3DCULL_NONE;
};
class DrawInputReader {
public:
    // Requires the same serialization as application draws and resource writes.
    // A supplied scope must be sampled immediately before this same draw.
    // Empty geometry_frame preserves diagnostic-only behavior without retained
    // geometry. A nonempty frame must belong to this device and be caller-owned.
    DrawInput read(IDirect3DDevice9* application, const DrawArguments&,
                   const object_trace::Snapshot* scope,
                   ownership::GeometryFrameHandle geometry_frame = {}) noexcept;
    static void complete(DrawInput& input, HRESULT result) noexcept;
#ifdef X3M_DRAW_INPUT_FIXTURE
    // Original synthetic shader contracts only; absent from production builds.
    using PositionLookup = const renderer::RigidPositionProfile* (*)(const std::uint32_t*, std::size_t);
    using PixelLookup = const renderer::PixelCoverageProfile* (*)(const std::uint32_t*, std::size_t);
    void fixture_profiles(PositionLookup vertex, PixelLookup pixel) noexcept {
        position_lookup_ = vertex; pixel_lookup_ = pixel;
    }
#endif
private:
    // Bounded reusable scratch, no per-draw allocation or shader cache with stale
    // COM identities. Larger programs remain unknown. Never retain game bytecode.
    std::array<std::uint32_t,4096> shader_words_{};
#ifdef X3M_DRAW_INPUT_FIXTURE
    PositionLookup position_lookup_ = renderer::find_rigid_position;
    PixelLookup pixel_lookup_ = renderer::find_pixel_coverage;
#endif
};
} // namespace x3m
