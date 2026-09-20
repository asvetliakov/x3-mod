#pragma once
#include <d3d9.h>
#include <cstdint>
#include <array>
#include "finite_buffer_evidence.h"
#include "execution_state.h"
#include "buffer_lock_observation.h"
#include "surface_lock_observation.h"
#include "surface_lease_core.h"

// Opt-in normal-D3D9 ownership boundary. Application COM references are separate from renderer-owned
// backend resources, so persistent history cannot keep its own owner alive.
namespace x3m::ownership {

// CPU-only observation of a live canonical device/surface pair. Both inputs
// are registry keys only until membership is established. S_OK returns an
// identity but NO reference: caller must independently qualify engine binding
// publication/raw reads, thread admission and the whole copy/Reset interval.
HRESULT snapshot_surface_identity(IDirect3DDevice9* device, IDirect3DSurface9* candidate,
    SurfaceLeaseIdentity* out) noexcept;

class SurfaceLease final {
public:
    SurfaceLease() noexcept = default;
    SurfaceLease(SurfaceLease&&) noexcept = default;
    SurfaceLease& operator=(SurfaceLease&&) noexcept = default;
    SurfaceLease(const SurfaceLease&) = delete;
    SurfaceLease& operator=(const SurfaceLease&) = delete;
    // Borrowed canonical application interface, valid only while lease is held.
    IDirect3DSurface9* get() const noexcept { return retained_.get(); }
    // May perform final backend cleanup/reenter. Call after UnlockRect and
    // BEFORE dropping the separately qualified owned-copy/Reset exclusion.
    void reset() noexcept { retained_.reset(); }
private:
    detail::LogicalSurfaceLease<IDirect3DSurface9> retained_;
    friend HRESULT acquire_surface_lease(IDirect3DDevice9*, IDirect3DSurface9*,
        const SurfaceLeaseIdentity&, SurfaceLease&) noexcept;
};
// Empty output required (nonempty refuses unchanged). S_OK logically retains
// exactly once under the registry mutex, without backend AddRef or vtable read.
// Both lookup APIs preserve x87 payload/environment, MXCSR and LastError on
// ordinary return, including refusals; volatile XMM follows the C++ ABI.
// E_INVALIDARG: unknown/native/wrong-kind/wrong-device/stale/zero identity;
// S_FALSE: unavailable device/identity exhaustion; E_FAIL: logical ref overflow
// or registry failure (never unwinds through the preserving entry shell).
// No Reset lock is held after return; this API alone NEVER makes Reset safe.
// Lease keeps the canonical surface's existing native ref AND logical parent
// device alive. It does not keep engine tables, slots or media records alive.
HRESULT acquire_surface_lease(IDirect3DDevice9* device, IDirect3DSurface9* candidate,
    const SurfaceLeaseIdentity& expected, SurfaceLease& out) noexcept;

// Serialized startup registration; callbacks are CPU-only, noexcept and run
// outside the registry mutex under an ordinary-return CPU/LastError shell.
enum class ResetPhase { begin, end };
struct ResetEvent {IDirect3DDevice9* application=nullptr;std::uint64_t device_serial=0,generation=0;ResetPhase phase=ResetPhase::begin;HRESULT result=S_FALSE;};
using ResetObserver=void(*)(const ResetEvent&) noexcept;
void set_reset_observer(ResetObserver) noexcept;

struct Options {
    // Prepare a private snapshot of automatic, single-sample D24X8 through RESZ.
    // Copied into devices before their first application clear/draw. Default inert.
    bool capture_auto_depth = false;
    // Diagnostic revisions of observed VB/IB writes; never captures payload.
    bool track_buffer_writes = false;
    // Optional attempt/completion diagnostics; requires track_buffer_writes.
    bool track_buffer_lock_attempts = false;
    // Observe scene/state-block/query intervals from pristine device creation.
    bool track_execution_state = false;
    // Opt-in finite XYZ evidence from verified existing MANAGED write mappings.
    // Requires track_buffer_writes. No extra Lock or GPU readback is performed.
    bool capture_finite_positions = false;
    std::uint32_t finite_payload_budget = 32u * 1024u * 1024u;
    std::uint32_t finite_sidecar_limit = 4096;
    // Locked-prefix bounds (docs/architecture/screen-emission-region.md, step
    // B/D): sentinel the mapped window of a marked DISCARD-locked vertex
    // buffer at its Lock and copy the written positions at its Unlock
    // (src/proxy/locked_prefix_core.h). Off by default;
    // X3M_SCREEN_EMISSION_BOUND=1. Only positions are retained.
    bool locked_prefix_bounds = false;
};

// On success, consumes exactly the caller's owned native reference. On failure,
// the caller retains it. Native Ex factories are rejected before wrapper mode.
HRESULT wrap_factory(IDirect3D9* owned_native, IDirect3D9** out,
                     const Options& options = {}) noexcept;

// S_OK means recognized wrapper; inspect requested/known before using any bits.
// Serialize application calls, this snapshot and injected work. All application
// execution must cross this boundary; native renderer access is trusted to leave
// scene/state-block/query scopes unchanged. Foreign native bypass is unknowable.
HRESULT get_execution_view(IDirect3DDevice9* application, ExecutionView* out) noexcept;
// Call after uncertain injected-native execution/restoration, or known bypass.
// Permanent for the living device, including successful Reset. No native calls.
HRESULT invalidate_execution_state(IDirect3DDevice9* application) noexcept;

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

// CPU-only atomic registry snapshot of an allocation's observed Lock interval.
// allocation_id survives wrapper recreation and is never reused. generation is
// device-local and changes before every Reset attempt. Counts saturate and veto
// known; READONLY attempts advance attempt_serial without changing revision.
// Hold a live application reference. This is observation, never replay admission:
// unknown/native escape coverage and source submission still need separate gates.
// Ordinary returns preserve x87/MXCSR/LastError. Bookkeeping exceptions are caught
// internally and return S_FALSE with unknown evidence (status E_FAIL).
struct BufferLockView : BufferLockObservation {
    std::uint64_t generation=0;
    HRESULT status=S_FALSE;
    bool requested=false, known=false;
};
HRESULT get_buffer_lock_view(IDirect3DResource9* application, BufferLockView* out) noexcept;
// Same view without the FNSAVE/FRSTOR shell: preserves NOTHING itself. Only for
// a caller that already runs under a CPU-state boundary restoring MXCSR and
// LastError and whose whole path is audited x87-free (the proxy's draw hooks
// under LightCallBoundary; verification/probe/check_no_x87.py walks this entry
// and its core as a required root and is the build gate). Every other caller
// uses get_buffer_lock_view.
HRESULT get_buffer_lock_view_light(IDirect3DResource9* application, BufferLockView* out) noexcept;

// Step B/D locked-prefix positions of an application vertex buffer wrapper
// for its leading vertex_count vertices (POSITION FLOAT3 at 0, stride 24
// assumed by the scan; the caller validates the declaration and the
// producer). mark learns the buffer as a scan candidate: only marked buffers
// are sentinelled at their next DISCARD Lock and scanned at its Unlock, so the
// first draw of a buffer is refused by design. S_OK for a recognised wrapper;
// inspect requested/known. reason is a prefix::Lookup value. positions (3
// floats per vertex, at least vertex_count of them) point into the table's
// pooled storage and are valid while the revision holds: project them, then
// call again with mark false and compare the revision. Never locks, reads
// back or dereferences the wrapper; one registry find and one fixed-table
// probe under the registry mutex.
struct LockedPrefixView {
    HRESULT status = S_FALSE;
    bool requested = false, known = false;
    unsigned reason = 1; // prefix::Lookup::Unknown
    std::uint64_t revision = 0;
    std::uint32_t scanned = 0;          // vertices the Unlock scan published
    const float* positions = nullptr;   // known only
};
HRESULT get_locked_prefix_view(IDirect3DResource9* application, std::uint32_t vertex_count, bool mark, LockedPrefixView* out) noexcept;
struct LockedPrefixStatistics {
    std::uint64_t locks = 0, scans = 0, scanned_vertices = 0, scan_ticks = 0, qpc_frequency = 0;
    std::uint64_t lookups = 0, bounds = 0, marks = 0, evictions = 0;
    std::uint64_t sentinel_bytes = 0, sentinel_ticks = 0, window_end_scans = 0; // step D: sentinel written at Lock (bytes, QPC ticks); scans that met no sentinel
    unsigned used = 0;
};
void get_locked_prefix_statistics(LockedPrefixStatistics* out) noexcept;

// Inspection-only native endpoint for a recognized VB/IB wrapper whose actual
// Lock/Unlock slots still use our original forwarding methods. No COM calls or
// AddRef; null, native, wrong-kind and replaced-slot inputs return null. This
// does NOT certify the native endpoint: the caller separately verifies it.
// Hold a live wrapper reference and serialize buffer operations, final Release
// and vtable changes throughout inspection and subsequent normal wrapper calls.
// Never call Lock/Unlock through the result: that would bypass write tracking.
// Trusted read-only inspection only; this pointer is technically mutable.
// Every trusted native mutation outside wrapper Lock/Unlock must first call
// invalidate_native_buffer_evidence and serialize its entire interval against
// evidence queries/replay. Notification advances the observed storage revision.
// Arbitrary unobserved native writes are unsupported.
HRESULT invalidate_native_buffer_evidence(IUnknown* wrapped) noexcept;
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

// Frame-scoped native geometry reservations for a renderer. These opaque values
// never own an application wrapper reference and are never reused in a process.
struct GeometryFrameHandle { std::uint64_t value=0; };
struct GeometryLeaseHandle { std::uint64_t value=0; };
constexpr std::uint32_t geometry_frame_limit=64;
constexpr std::uint32_t geometry_leases_per_frame=4096;
constexpr std::uint32_t geometry_lease_limit=8192;
constexpr std::uint64_t geometry_native_byte_limit=512ull*1024ull*1024ull;
struct GeometryLeaseRequest {
    std::uint64_t expected_generation=0;
    FinitePositionRequest positions;
    bool indexed=false;
    IndexRangeRequest indices; // Ignored only when indexed=false and IB=null.
};
struct GeometryLeaseView {
    HRESULT status=S_FALSE;
    FiniteEvidenceReason reason=FiniteEvidenceReason::Unrecognized;
    GeometryFrameHandle frame;
    GeometryLeaseHandle lease;
    std::uint64_t generation=0;
    IDirect3DVertexBuffer9* vertex_buffer=nullptr; // Borrowed native, never application-visible.
    IDirect3DIndexBuffer9* index_buffer=nullptr;
    FinitePositionView positions;
    IndexRangeView indices;
};
// Caller holds the device and actual getter VB/IB references during acquisition;
// serialize all frame/lease operations with writes, reset, teardown and mutation.
// One active frame per device. Capacity refusal never evicts existing leases.
// Indexedness must match IB presence. Requests are copied, never reinterpreted.
// S_OK acquisition consumes no caller refs; it acquires its own native refs.
// S_FALSE means evidence refused; failed HRESULT means invalid handle/arguments,
// unavailable device or capacity. Every failed acquisition leaves out->value=0.
HRESULT begin_geometry_frame(IDirect3DDevice9* application,GeometryFrameHandle* out) noexcept;
HRESULT acquire_geometry_lease(GeometryFrameHandle frame,IDirect3DVertexBuffer9* vertex_buffer,
    IDirect3DIndexBuffer9* index_buffer,const GeometryLeaseRequest& request,GeometryLeaseHandle* out) noexcept;
// Revalidates the exact stored requests on held native allocations without
// recreating wrappers. S_OK/status S_OK alone exposes borrowed native pointers.
// Content refusal returns S_FALSE with null pointers; stale handles E_INVALIDARG.
// Borrowed pointers expire on release/end/Reset/loss/final logical device release.
HRESULT inspect_geometry_lease(GeometryFrameHandle frame,GeometryLeaseHandle lease,GeometryLeaseView* out) noexcept;
HRESULT release_geometry_lease(GeometryFrameHandle frame,GeometryLeaseHandle lease) noexcept;
HRESULT end_geometry_frame(GeometryFrameHandle frame) noexcept;

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
// Hands the wrapper a failing HRESULT the renderer received from a value-only
// call it made directly on borrowed_native_device(wrapped), so a device-loss
// code is observed exactly as if the call had crossed the wrapper (lost flag,
// geometry/finite/copy-depth retirement). Cold path; returns hr. Preserves
// x87/MXCSR/LastError when it acts. An unrecognised pointer is ignored.
HRESULT observe_native_result(IDirect3DDevice9* wrapped, HRESULT hr) noexcept;

// Adopt a native renderer resource created through borrowed_native_device.
// Success consumes one owned reference; failure leaves ownership with caller.
// Application wrappers are rejected to avoid logical reference cycles. The
// renderer must restore app-visible bindings before returning from injected work.
// All adopted references are released before Reset and before backend teardown.
HRESULT retain_renderer_resource(IDirect3DDevice9* wrapped, IUnknown* owned_resource) noexcept;

} // namespace x3m::ownership
