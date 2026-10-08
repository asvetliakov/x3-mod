#pragma once
// Occlusion cull: the D3D side (docs/architecture/occlusion-cull.md). One object per device, attached by the motion
// route at the first candidate draw. It owns a pool of occlusion_cull::core::pool_size occlusion queries (two frame
// slots, the ring), a two-instruction vs_3_0 (`mad o0, v0, c253, c252`), a constant ps_3_0, a POSITION FLOAT2
// declaration and an eight-vertex MANAGED strip buffer (the unit square in both windings). At a candidate's draw site
// it issues this frame's test before the draw: the part's test rectangle (core::test_rect) at its nearest depth under a
// query, z write off, alpha test and stencil off, every bound target kept by a ZERO/ONE blend over a program that writes
// 0 to every output (a colour write mask of 0, or unbinding the route's lazy RT1/RT2, costs 55-79 us per test on DXVK
// over MoltenVK, measured: verification/results/occlusion-cull/gate.json), and restores what it changed; and it answers
// whether the real draw is skipped (core::may_skip on the most recent ready result up to two frames old, read with
// GetData(..., 0) at the frame's first candidate; not ready, an error or any sample counts as visible: never a stall).
// Every target bound at the test (read from the device) must pass CheckDeviceFormat(D3DUSAGE_QUERY_POSTPIXELSHADER_
// BLENDING), and more than one needs D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING; otherwise the part is drawn untested.
// Documented D3D9 only: CreateQuery(D3DQUERYTYPE_OCCLUSION, nullptr) is the capability probe. Device and native table are borrowed (no
// AddRef); the caller serialises rendering, Reset and teardown and runs attach/before_reset/detach under its
// reference accounting (the queries hold device references). Queries go in before_reset and come back in a
// successful Reset (recreate(), called by the owner under its accounting); the programs, declaration and MANAGED
// buffer survive Reset. No heap after attach.
#include <d3d9.h>
#include <cstdint>
#include "../proxy/occlusion_cull_core.h"

namespace x3m::renderer {
// The application's values of the states the test changes, as the caller knows them (the motion route's shadow).
struct OcclusionCullState { // defaults: z write on, alpha test / stencil off, D3DCULL_CCW (3), ONE (2) / ZERO (1) / ADD (1)
    DWORD z_write = 1, alpha_test = 0, stencil = 0, cull = 3;
    DWORD alpha_blend = 0, src_blend = 2, dest_blend = 1, blend_op = 1, separate_alpha = 0;
    const float* reserved = nullptr; // the application's c252-c253 (8 floats) to put back; null: never written, left
};
enum class OcclusionCullVerdict : std::uint8_t {
    draw,      // tested, drawn
    skip,      // tested, the real draw is skipped (last frame's test read 0 samples)
    truncated, // the frame's pool slot is full: drawn untested
    refused,   // the target format cannot blend, the stream is instanced, the pass is not available: drawn untested
    failed     // a call failed: drawn; *restore says whether the device state is the application's
};
struct OcclusionCullFrameStats {
    // hidden/visible/errors: reads of the older frames' queries; not_ready: frame - 1 reads not ready; ready_lag2:
    // frame - 2 queries ready at their second read; age1/age2/age_none: per candidate, the age of the most recent ready
    // result its decision used (none: no ready result within two frames).
    unsigned candidates = 0, tested = 0, skipped = 0, hidden = 0, visible = 0, not_ready = 0, errors = 0,
             ready_lag2 = 0, age1 = 0, age2 = 0, age_none = 0, drawn_late = 0, pool_truncated = 0, refused = 0,
             failed = 0, unstable = 0, no_hull = 0;
    void add(const OcclusionCullFrameStats& o) noexcept;
};
class OcclusionCullPass {
public:
    OcclusionCullPass() = default;
    ~OcclusionCullPass(); // detach()
    OcclusionCullPass(const OcclusionCullPass&) = delete;
    OcclusionCullPass& operator=(const OcclusionCullPass&) = delete;
    // Creates every object; S_OK when available. D3DERR_NOTAVAILABLE (reason "unsupported") when the device refuses
    // occlusion queries; any failure releases what was created (nothing held).
    HRESULT attach(IDirect3DDevice9* device, void* const* native) noexcept;
    void detach() noexcept;
    void before_reset() noexcept;             // releases the queries; the ring is emptied
    void after_reset(HRESULT reset) noexcept; // a successful Reset arms recreate(); a failed one leaves them released
    // After a successful Reset: the queries come back here, under the caller's reference accounting (the motion route
    // calls it at the next candidate inside taa_call); the first frame after has no previous result and draws everything.
    bool recreate_pending() const noexcept { return recreate_pending_; }
    HRESULT recreate() noexcept;
    bool available() const noexcept { return available_; }
    const char* reason() const noexcept { return reason_; }
    // Frame boundary: the next candidate reads the previous frame's slot first.
    void begin_frame(std::uint32_t frame) noexcept;
    // One candidate draw, before it is forwarded. hull_drawn: one of its ancestors owns a hull drawn this frame.
    OcclusionCullVerdict candidate(std::uint64_t key, const occlusion_cull::core::Rect& rect, bool hull_drawn,
                                   const OcclusionCullState& state, HRESULT* restore) noexcept;
    const OcclusionCullFrameStats& frame_stats() const noexcept { return frame_; }
    OcclusionCullFrameStats& frame_stats() noexcept { return frame_; }
    unsigned queries() const noexcept; // live queries (fixture)
private:
    void read_previous() noexcept;
    bool blendable(D3DFORMAT format) noexcept;
    bool targets_blend() noexcept;
    HRESULT create_queries() noexcept;
    void release_queries() noexcept;
    IDirect3DDevice9* device_ = nullptr;
    void* const* native_ = nullptr;
    IDirect3DVertexShader9* vs_ = nullptr;
    IDirect3DPixelShader9* ps_ = nullptr;
    IDirect3DVertexDeclaration9* declaration_ = nullptr;
    IDirect3DVertexBuffer9* strip_ = nullptr;
    IDirect3DQuery9* queries_[occlusion_cull::core::pool_size]{};
    occlusion_cull::core::Ring ring_{};
    OcclusionCullFrameStats frame_{};
    struct FormatVerdict {
        D3DFORMAT format;
        bool known, blendable;
    } formats_[8]{};
    unsigned format_next_ = 0;
    unsigned targets_ = 1;      // the device's simultaneous targets (at most 4): the outputs the program writes
    bool mrt_blending_ = false; // D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING
    bool available_ = false, reset_pending_ = false, recreate_pending_ = false;
    const char* reason_ = "detached";
};
}
