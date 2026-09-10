#pragma once
#include <d3d9.h>
#include <cstdint>
#include <array>
#include "finite_buffer_evidence.h"

// Opt-in normal-D3D9 ownership boundary. Application COM references are separate from renderer-owned
// backend resources, so persistent history cannot keep its own owner alive.
namespace x3m::ownership {

struct Options {
    // Prepare a private snapshot of automatic, single-sample D24X8 through RESZ.
    // Copied into devices before their first application clear/draw. Default inert.
    bool capture_auto_depth = false;
    // Diagnostic revisions of observed VB/IB writes; never captures payload.
    bool track_buffer_writes = false;
    // Opt-in finite XYZ evidence from verified existing MANAGED write mappings.
    // Requires track_buffer_writes. No extra Lock or GPU readback is performed.
    bool capture_finite_positions = false;
    std::uint32_t finite_payload_budget = 32u * 1024u * 1024u;
    std::uint32_t finite_sidecar_limit = 4096;

};

// On success, consumes exactly the caller's owned native reference. On failure,
// the caller retains it. Native Ex factories are rejected before wrapper mode.
HRESULT wrap_factory(IDirect3D9* owned_native, IDirect3D9** out,
                     const Options& options = {}) noexcept;

struct BufferContentView {
    std::uint64_t revision = 0; // Observed successful write operations, not a hash.
    std::uint32_t pending_locks = 0;
    DWORD last_lock_flags = 0;
    HRESULT status = S_FALSE;
    bool requested = false;
    bool known = false; // False while locked, ambiguous, missing metadata or failed tracking.
    bool ambiguous = false; // Sticky uncertainty; revision must not imply stability.
};

// Application VB/IB wrappers only; native pointers and other resources rejected.
// Valid buffer returns S_OK even when off/unknown; inspect status/requested/known.
// Metadata is fixed native-resource POD, survives wrapper recreation, owns no COM refs.
// Only writes crossing this boundary are observed; borrowed-native writes are outside it.
// Caller must serialize buffer writes, these views and their draw snapshots.
// Native writes plus metadata updates are not a transaction for concurrent callers.
HRESULT get_buffer_content_view(IDirect3DResource9* application, BufferContentView* out) noexcept;

// Inspection-only native endpoint for a recognized VB/IB wrapper whose actual
// Lock/Unlock slots still use our original forwarding methods. No COM calls or
// AddRef; null, native, wrong-kind and replaced-slot inputs return null. This
// does NOT certify the native endpoint: the caller separately verifies it.
// Hold a live wrapper reference and serialize buffer operations, final Release
// and vtable changes throughout inspection and subsequent normal wrapper calls.
// Never call Lock/Unlock through the result: that would bypass write tracking.
IDirect3DVertexBuffer9* borrowed_native_buffer_for_lock_contract(IDirect3DVertexBuffer9* wrapped) noexcept;
IDirect3DIndexBuffer9* borrowed_native_buffer_for_lock_contract(IDirect3DIndexBuffer9* wrapped) noexcept;

enum class FiniteEvidenceReason : std::uint32_t {
    None, Disabled, Unrecognized, DeviceUnavailable, TrackingUnavailable,
    MissingAllocation, RevisionMismatch, Pending, Ambiguous, NativeContract,
    UnsupportedWrite, ThreadMismatch, MappingMismatch, UnlockFailed,
    InvalidLayout, InvalidRange, UnknownCells, NonFinite, IndexUnknown,
    AllocationFailure, Budget, MetadataTampered, ProcessVertices, Count
};
constexpr unsigned finite_evidence_reason_count=static_cast<unsigned>(FiniteEvidenceReason::Count);
const char* finite_evidence_reason_name(FiniteEvidenceReason reason) noexcept;
struct FinitePositionRequest {
    std::uint64_t expected_revision=0;
    std::uint64_t stream_offset=0;
    std::uint32_t stride=0,position_offset=0;
    std::int64_t first_vertex=0;
    std::uint64_t vertex_count=0;
    D3DDECLTYPE position_type=D3DDECLTYPE_UNUSED; // FLOAT3 or FLOAT16_4; XYZ only, W ignored.
};
struct FinitePositionView {
    FiniteStatus state=FiniteStatus::Unknown;
    FiniteEvidenceReason reason=FiniteEvidenceReason::Disabled;
    HRESULT status=S_FALSE;
    std::uint64_t generation=0,revision=0;
    bool requested=false;
};
struct IndexRangeRequest {
    std::uint64_t expected_revision=0;
    D3DFORMAT format=D3DFMT_UNKNOWN;
    std::uint64_t start_index=0,index_count=0;
};
struct IndexRangeView {
    bool known=false,requested=false,exact_range=false;
    FiniteEvidenceReason reason=FiniteEvidenceReason::Disabled;
    HRESULT status=S_FALSE;
    std::uint64_t generation=0,revision=0;
    std::uint32_t minimum=0,maximum=0;
    // For a proper subdraw, known extrema conservatively cover the whole IB.
};
struct FiniteRefusalDetail {
    bool available=false;
    FiniteEvidenceReason reason=FiniteEvidenceReason::None;
    D3DRESOURCETYPE type=D3DRTYPE_VERTEXBUFFER;
    D3DFORMAT format=D3DFMT_UNKNOWN;
    D3DPOOL pool=D3DPOOL_DEFAULT;
    std::uint32_t size=0,usage=0,lock_flags=0;
};
struct FiniteUploadStatistics {
    bool requested=false,active=false;
    HRESULT status=S_FALSE;
    std::uint64_t generation=0;
    std::uint64_t payload_bytes=0,peak_payload_bytes=0,sidecars=0,metadata_bytes=0;
    std::uint64_t global_payload_bytes=0,global_sidecars=0;
    std::uint64_t uploads=0,publications=0,invalidations=0,allocation_failures=0;
    std::uint64_t scans=0,classified_bytes=0,scan_ticks=0,queries=0,query_cache_hits=0,position_components=0;
    std::uint64_t qualifier_ticks=0,query_ticks=0;
    FiniteRefusalDetail first_refusal;
    std::array<std::uint64_t,finite_evidence_reason_count> reasons{};
};
// Application wrappers only. Hold their references and serialize uploads, queries,
// reset/destruction and foreign vtable changes. S_OK means a recognized wrapper;
// inspect state/known/reason. Queries never Lock/read back payload or issue draws.
// Trusted borrowed-native code must obey the observer boundary: completed foreign
// writes that bypass every wrapper cannot be detected or certified by this API.
HRESULT get_finite_position_view(IDirect3DVertexBuffer9* application,
    const FinitePositionRequest& request, FinitePositionView* out) noexcept;
HRESULT get_index_range_view(IDirect3DIndexBuffer9* application,
    const IndexRangeRequest& request, IndexRangeView* out) noexcept;
HRESULT get_finite_upload_statistics(IDirect3DDevice9* application,
    FiniteUploadStatistics* out) noexcept;

struct CopyDepthView {
    // Borrowed native D24X8 snapshot. On verified Preview this texture uses
    // comparison sampling, not raw red-channel depth; GPU decode is separate.
    IDirect3DTexture9* texture = nullptr;
    std::uint64_t generation = 0;
    std::uint64_t source_epoch = 0; // Successful clears of the original source.
    std::uint64_t copy_epoch = 0; // Source epoch of the last successful copy.
    D3DSURFACE_DESC source_desc{};
    HRESULT status = S_FALSE;
    bool requested = false;
    bool available = false;
    bool copy_valid = false;
    bool source_bound = false;
};

// Both calls require a live wrapper and rendering/reset serialization by caller.
// get_copy_depth_view returns S_OK for a recognized wrapper; inspect its fields.
// copy_auto_depth performs no app draw/clear/target substitution. The original
// source must currently be bound, and no state block may be recording. Texture 0
// and POINTSIZE are restored. A source clear does not invalidate a saved copy.
HRESULT get_copy_depth_view(IDirect3DDevice9* wrapped, CopyDepthView* out) noexcept;
HRESULT copy_auto_depth(IDirect3DDevice9* wrapped) noexcept;

// Renderer-only borrowed pointer. No AddRef; caller must hold a live application
// wrapper reference throughout use. Never return this pointer to application code.
// Returns null for an unrecognized pointer without dereferencing that pointer.
IDirect3DDevice9* borrowed_native_device(IDirect3DDevice9* wrapped) noexcept;

// Adopt a native renderer resource created through borrowed_native_device.
// Success consumes one owned reference; failure leaves ownership with caller.
// Application wrappers are rejected to avoid logical reference cycles. The
// renderer must restore app-visible bindings before returning from injected work.
// All adopted references are released before Reset and before backend teardown.
HRESULT retain_renderer_resource(IDirect3DDevice9* wrapped, IUnknown* owned_resource) noexcept;

} // namespace x3m::ownership
