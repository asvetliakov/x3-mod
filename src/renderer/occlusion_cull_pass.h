#pragma once
// Occlusion cull: the D3D side (docs/architecture/occlusion-cull.md). One object per device, attached by the motion
// route at the first candidate draw. It owns a pool of occlusion_cull::core::pool_size occlusion queries (two frame
// slots, the ring), a one-instruction vs_3_0 (`mov o0, v0`: the rectangles arrive in clip space), a constant ps_3_0, a
// POSITION FLOAT4 declaration and one rectangle buffer sized to the pool (four vertices per test; no per-frame
// allocation). Tests are batched per ship (core::Batcher): at a ship's first part draw of the frame (its hull pieces
// drawn before) the pass issues the ship's block, one state swap, one Lock writing the block's rectangles, per test a
// query around a two-triangle strip, one restore; then every part draw answers whether it is skipped (core::may_skip on
// the most recent ready result up to two frames old, read with GetData(..., 0) at the frame's first part; not ready, an
// error or any sample counts as visible: never a stall). The test keeps every bound target with a ZERO/ONE blend over a
// program that writes 0 to every output (a colour write mask of 0, or unbinding the route's lazy RT1/RT2, costs 55-79 us
// per test on DXVK over MoltenVK, measured: verification/results/occlusion-cull/gate.json). Every target bound at the
// block (read from the device) must pass CheckDeviceFormat(D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING), and more than one
// needs D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING; otherwise the block's parts are drawn untested.
// Documented D3D9 only: CreateQuery(D3DQUERYTYPE_OCCLUSION, nullptr) is the capability probe. Device and native table
// are borrowed (no AddRef); the caller serialises rendering, Reset and teardown and runs attach/before_reset/recreate/
// detach under its reference accounting (the queries and the DEFAULT-pool buffer hold device references). The queries
// (and the dynamic buffer) go in before_reset and come back in a successful Reset (recreate()); the programs, the
// declaration and a MANAGED buffer survive Reset. No heap after attach.
#include <d3d9.h>
#include <cstdint>
#include "../proxy/occlusion_cull_core.h"

namespace x3m::renderer {
// The application's values of the states the block changes, as the caller knows them (the motion route's shadow).
struct OcclusionCullState { // defaults: z write on, alpha test / stencil off, D3DCULL_CCW (3), ONE (2) / ZERO (1) / ADD (1)
    DWORD z_write = 1, alpha_test = 0, stencil = 0, cull = 3;
    DWORD alpha_blend = 0, src_blend = 2, dest_blend = 1, blend_op = 1, separate_alpha = 0;
    const float* reserved = nullptr; // the application's c252-c253 (8 floats; constants mode); null: never written
};
// One part draw as the caller sees it: its draw key, its ship (the root its hull owner hangs off), its vertex-extent
// box and this frame's test rectangle (core::test_rect through its clip rows), the rows themselves (the reprojection
// input), whether its hull drew this frame, the depth function and the viewport.
struct OcclusionCullPart {
    std::uint64_t key = 0;
    std::uint32_t ship = 0, model = 0; // model: with the key's node, the part's re-test phase
    occlusion_cull::core::Box box{};
    occlusion_cull::core::Rect rect{};
    const float* rows = nullptr;
    bool hull_drawn = false;
    // The caller's identity of the bound target set (the route: its lazy RT1/RT2 bits and the HDR redirect state; under
    // its scene gate RT0 is the latched main target and no application MRT is bound): the blendability check of the
    // bound targets is cached per frame and key, so a frame with many ship blocks reads the targets once per binding.
    std::uint32_t targets_key = 0;
    unsigned zfunc = occlusion_cull::core::cmp_lessequal;
    float vp_width = 0.f, vp_height = 0.f;
};
enum class OcclusionCullVerdict : std::uint8_t {
    draw,      // drawn (tested in a block, waiting for its cadence, or first seen this frame)
    skip,      // the real draw is skipped (its most recent ready test read 0 samples)
    truncated, // not listed (the per-draw table or the frame's list is full): drawn untested
    refused,   // the pass is not available: drawn untested
    failed     // the block failed: drawn; *restore says whether the device state is the application's
};
// The rectangle geometry: dynamic (DEFAULT pool, D3DUSAGE_DYNAMIC, NOOVERWRITE appends and a DISCARD at the wrap),
// discard (the same buffer, every block written from its start with DISCARD) - both released and recreated around
// Reset - managed (MANAGED pool, one Lock of the block's region of its frame slot; survives Reset), or constants (no
// per-frame write: a MANAGED unit-square strip and the rectangle in c252-c253 per test, the application's c252-c253 put
// back after the block). The fixture measures all four (occlusion_cull_fixture's realistic case); production uses
// default_buffer (constants).
enum class OcclusionCullBuffer : std::uint8_t { dynamic, managed, discard, constants };
struct OcclusionCullFrameStats {
    // hidden/visible/errors: reads of the older frames' queries; not_ready: frame - 1 reads not ready; ready_lag2:
    // frame - 2 queries ready at their second read; age1/age2/age_none: per decided part, the age of the most recent
    // ready result its decision used (none: no ready result within two frames). tested: tests issued in this frame's
    // blocks; retest_skipped: parts of a block left untested by the cadence (last read visible); blocks: ship blocks
    // issued; stale: tests on last frame's rectangle (no reference hull this frame); cadence: tests of parts last read
    // visible on their phase frame; forced: the same off-phase (moved, hull changed); test_ns: QPC time inside the blocks.
    unsigned candidates = 0, tested = 0, skipped = 0, hidden = 0, visible = 0, not_ready = 0, errors = 0,
             ready_lag2 = 0, age1 = 0, age2 = 0, age_none = 0, drawn_late = 0, pool_truncated = 0, refused = 0,
             failed = 0, unstable = 0, no_hull = 0, retest_skipped = 0, blocks = 0, stale = 0, cadence = 0, forced = 0;
    std::uint64_t test_ns = 0;
    void add(const OcclusionCullFrameStats& o) noexcept;
};
class OcclusionCullPass {
public:
    // Production: constants (no Lock on the game thread; on wined3d over macOS GL every buffer Lock waits for the
    // command stream, 157-272 us per block; measured in docs/architecture/occlusion-cull.md, "Cost"). The buffer modes
    // are fixture-measured alternatives only.
    static constexpr OcclusionCullBuffer default_buffer = OcclusionCullBuffer::constants;
    OcclusionCullPass() = default;
    ~OcclusionCullPass(); // detach()
    OcclusionCullPass(const OcclusionCullPass&) = delete;
    OcclusionCullPass& operator=(const OcclusionCullPass&) = delete;
    // Creates every object; S_OK when available. D3DERR_NOTAVAILABLE (reason "unsupported") when the device refuses
    // occlusion queries; any failure releases what was created (nothing held).
    HRESULT attach(IDirect3DDevice9* device, void* const* native, OcclusionCullBuffer buffer = default_buffer) noexcept;
    void detach() noexcept;
    void before_reset() noexcept;             // releases the queries (and a dynamic buffer); the ring is emptied
    void after_reset(HRESULT reset) noexcept; // a successful Reset arms recreate(); a failed one leaves them released
    // After a successful Reset: the queries come back here, under the caller's reference accounting (the motion route
    // calls it at the next part inside taa_call); the first frame after has no previous result and draws everything.
    // The caller resets its Batcher's results (Batcher::reset_results) with before_reset.
    bool recreate_pending() const noexcept { return recreate_pending_; }
    HRESULT recreate() noexcept;
    bool available() const noexcept { return available_; }
    const char* reason() const noexcept { return reason_; }
    OcclusionCullBuffer buffer() const noexcept { return buffer_; }
    // Frame boundary: the next part reads the older frames' slots first.
    void begin_frame(std::uint32_t frame) noexcept;
    // One part draw, before it is forwarded: lists it in the batcher (hull drawn), issues its ship's block when this
    // is the ship's first part draw of the frame, and answers draw or skip.
    OcclusionCullVerdict part(occlusion_cull::core::Batcher& batcher, const OcclusionCullPart& part,
                              const OcclusionCullState& state, HRESULT* restore) noexcept;
    const OcclusionCullFrameStats& frame_stats() const noexcept { return frame_; }
    OcclusionCullFrameStats& frame_stats() noexcept { return frame_; }
    unsigned queries() const noexcept; // live queries (fixture)
private:
    void read_previous(occlusion_cull::core::Batcher& batcher) noexcept;
    bool blendable(D3DFORMAT format) noexcept;
    bool targets_blend() noexcept;
    HRESULT create_queries() noexcept;
    void release_queries() noexcept;
    HRESULT create_buffer() noexcept;
    bool default_pool() const noexcept {
        return buffer_ == OcclusionCullBuffer::dynamic || buffer_ == OcclusionCullBuffer::discard;
    }
    HRESULT write_rects(unsigned first_record, unsigned n, UINT* base_vertex) noexcept;
    void block(occlusion_cull::core::Batcher& batcher, const OcclusionCullPart& part, const OcclusionCullState& state,
               HRESULT* restore) noexcept;
    OcclusionCullVerdict decide(occlusion_cull::core::Batcher& batcher, std::uint16_t entry,
                                const OcclusionCullPart& part) noexcept;
    IDirect3DDevice9* device_ = nullptr;
    void* const* native_ = nullptr;
    IDirect3DVertexShader9* vs_ = nullptr;
    IDirect3DPixelShader9* ps_ = nullptr;
    IDirect3DVertexDeclaration9* declaration_ = nullptr;
    IDirect3DVertexBuffer9* rects_ = nullptr;
    unsigned cursor_ = 0; // dynamic: the next free rectangle; a block that does not fit starts over with DISCARD
    OcclusionCullBuffer buffer_ = default_buffer;
    IDirect3DQuery9* queries_[occlusion_cull::core::pool_size]{};
    occlusion_cull::core::Ring ring_{};
    occlusion_cull::core::BlockItem items_[occlusion_cull::core::pool_per_frame]{};
    OcclusionCullFrameStats frame_{};
    long long qpc_frequency_ = 0; // QueryPerformanceFrequency (test_ns in integer arithmetic)
    struct FormatVerdict {
        D3DFORMAT format;
        bool known, blendable;
    } formats_[8]{};
    unsigned format_next_ = 0;
    std::uint32_t targets_frame_ = 0, targets_key_ = 0; // the cached target check (frame 0: none)
    bool targets_ok_ = false;
    unsigned targets_ = 1;      // the device's simultaneous targets (at most 4): the outputs the program writes
    bool mrt_blending_ = false; // D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING
    bool available_ = false, reset_pending_ = false, recreate_pending_ = false;
    const char* reason_ = "detached";
};
}
