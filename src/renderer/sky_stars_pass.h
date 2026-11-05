#pragma once
// Sky stars, Decision A (docs/architecture/sector-background-modern.md, "Implementation A"): the GPU side of the
// procedural star batch. One class owns the vs_3_0 / ps_3_0 pair (src/effects/sky_stars_{vs,ps}.hlsl), their
// declaration, a static MANAGED index buffer for stars_max quads (built once) and a static MANAGED vertex buffer of
// stars_max quads rewritten only when the seed, the look or the sector key changes (src/proxy/sky_stars_core.h build()). MANAGED
// resources and shader objects survive Reset, so nothing is released or recreated there; a lost-device code from a
// draw holds the pass until the next Reset.
//
// draw() runs inside the application's draw hook, right after the masked star-body draw of the background phase, so
// it saves every binding and state it changes and puts them back: the programs, the declaration (or the FVF the
// application used), stream 0 (buffer, offset, stride, frequency), the indices, vertex constants c251-c255 and the
// render states of `touched` below. Every value is read before the first write (a failed read changes nothing); every
// attempted write is restored in reverse order; the draw's and the restoration's results are reported separately so
// the caller can quarantine a device whose restoration failed. RT0 is the caller's (the FP16 scene); the caller
// guarantees no other target is bound. Documented D3D9 only; capabilities are checked at attach (shader model 3,
// ONE/ONE blending and BLENDOP, CULLNONE, FP16 post-pixel-shader blending on the adapter format, MANAGED buffers).
//
// Decision C2 (sector-background-modern.md "Decision C"): draw_deferred() is the batch at the end of the background
// phase, Z-tested (ZENABLE TRUE, ZFUNC LESSEQUAL, DEPTHBIAS and SLOPESCALEDEPTHBIAS 0, no Z write, z = 0.99999 w)
// against the phase's depth, with the dim
// twins of the programs (src/effects/sky_stars_dim_{vs,ps}.hlsl), VS c250-c255 and the 1/8-size copy of the HDR target
// on stage 0 (texture, ADDRESSU/V clamp, MIN/MAG linear or point, no mip, no sRGB; all six sampler states and the
// texture saved and put back). The copy (copy_dim: StretchRect of the HDR target into the dim texture with
// D3DTEXF_LINEAR when StretchRectFilterCaps has MINFLINEAR, else D3DTEXF_POINT) needs
// D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES; without either the dim side stays off (caps().dim) and the caller keeps the
// in-place batch. The dim texture is a D3DPOOL_DEFAULT render target: created by prepare_dim() at the first deferred
// batch and after Reset or a target size change (never per frame), released by before_reset() and detach(). draw()
// (the in-place batch) is unchanged by any of this.
#include "../proxy/sky_stars_core.h"
#include <d3d9.h>
#include <cstdint>
namespace x3m::renderer {
struct SkyStarsCaps {
    bool enabled = false;
    const char* reason = "not_attached";
    unsigned vs_slots = 0, ps_slots = 0;
    HRESULT fp16_blending = S_FALSE;
    // Decision C2: the deferred batch's dim side (copy filter D3DTEXF_LINEAR or D3DTEXF_POINT; sampling linear or point).
    bool dim = false;
    const char* dim_reason = "not_attached";
    unsigned copy_filter = 0, dim_vs_slots = 0, dim_ps_slots = 0;
    bool dim_linear = false;
};
enum class SkyStarsStep : unsigned { None, Validate, Resources, Build, Save, State, Draw };
struct SkyStarsReport {
    HRESULT operation = S_FALSE; // the batch: S_OK drawn, a failure code otherwise
    HRESULT restore = S_OK;      // the restoration: a failure leaves the device's state unknown
    SkyStarsStep failed = SkyStarsStep::None;
    unsigned calls = 0, stars = 0;
    bool drew = false, rebuilt = false;
};
class SkyStarsPass {
public:
    SkyStarsPass() = default;
    ~SkyStarsPass();
    SkyStarsPass(const SkyStarsPass&) = delete;
    SkyStarsPass& operator=(const SkyStarsPass&) = delete;
    // Checks the capabilities and creates the programs, the declaration and both buffers (the index list written
    // once); a refusal names its reason in caps().reason and holds nothing.
    HRESULT attach(IDirect3DDevice9*, void* const* native, const D3DCAPS9&, D3DFORMAT adapter_format) noexcept;
    void detach() noexcept;
    const SkyStarsCaps& caps() const noexcept { return caps_; }
    // Nothing is released (MANAGED buffers and shader objects survive Reset); a lost-device latch is dropped, so the
    // batch is tried again after the Reset (three failed batches stop the route's matching until the next one).
    void before_reset() noexcept;
    bool ready() const noexcept { return device_ && caps_.enabled && !lost_ && vs_ && ps_ && declaration_ && vb_ && ib_; }
    // Writes the vertex buffer for (seed, look, sector_key) when they differ from the buffer's (core::same_build);
    // returns the star count (0 on a refused seed or a failed Lock, the buffer then marked empty).
    unsigned prepare(std::uint32_t seed, const sky_stars::core::Look& look, std::uint64_t sector_key,
                     bool* rebuilt = nullptr) noexcept;
    std::uint32_t seed() const noexcept { return built_seed_; }
    std::uint64_t sector_key() const noexcept { return built_key_; }
    unsigned stars() const noexcept { return built_stars_; }
    const sky_stars::core::BuildStats& stats() const noexcept { return built_stats_; } // the last build's
    // Draws the prepared batch with the vertex constants c251 (sky_stars::core::twinkle_constants) and c252-c255
    // (sky_stars::core::vertex_constants), set in one call. See the header comment for the state contract.
    HRESULT draw(const float constants[16], const float twinkle[4], SkyStarsReport* report) noexcept;
    unsigned references() const noexcept;
    // Decision C2. prepare_dim: the dim texture for a target of width x height (dim_size per axis), created when absent
    // or of another size. copy_dim: the StretchRect of source (the HDR target, RT0) into it. draw_deferred: the batch as
    // draw() plus c250 = dim (sky_stars::core::dim_constants), the Z test and the dim copy on stage 0.
    HRESULT prepare_dim(unsigned width, unsigned height) noexcept;
    HRESULT copy_dim(IDirect3DSurface9* source) noexcept;
    HRESULT draw_deferred(const float constants[16], const float twinkle[4], const float dim[4],
                          SkyStarsReport* report) noexcept;
    unsigned dim_width() const noexcept { return dim_width_; }
    // A dim texture creation that failed without a device loss: latched until before_reset(); prepare_dim refuses at
    // once meanwhile and the route keeps the in-place batch.
    bool dim_unavailable() const noexcept { return dim_unavailable_; }
    unsigned dim_height() const noexcept { return dim_height_; }
#if defined(X3M_SKY_STARS_FIXTURE)
    // Fixture faults: bit 0 fails the draw once, bit 1 fails the first restoring SetRenderState once.
    void set_faults(unsigned faults) noexcept { faults_ = faults; }
    // A D3D9Ex device (the fixture's fallback while another process holds exclusive fullscreen) has no MANAGED pool:
    // the fixture asks for DEFAULT buffers before attach. Production always uses MANAGED.
    void set_pool(D3DPOOL pool) noexcept { pool_ = std::uint32_t(pool); }
    IDirect3DSurface9* dim_surface() const noexcept { return dim_surface_; } // the copy's readback (Decision C2)
#endif
private:
    template <class Fn> Fn call(unsigned slot) const noexcept {
        ++calls_;
        return reinterpret_cast<Fn>(vtable_[slot]);
    }
    HRESULT create_objects() noexcept;
    void release_objects() noexcept;
    void release_dim() noexcept;
    HRESULT draw_batch(const float constants[16], const float twinkle[4], const float* dim, SkyStarsReport* report) noexcept;
    IDirect3DDevice9* device_ = nullptr;
    void* const* vtable_ = nullptr; // borrowed
    SkyStarsCaps caps_{};
    bool lost_ = false;
    mutable unsigned calls_ = 0;
    unsigned faults_ = 0;
    std::uint32_t pool_ = 1; // D3DPOOL_MANAGED (an integer: host fixtures include this header without D3DPOOL)
    IDirect3DVertexShader9* vs_ = nullptr;
    IDirect3DPixelShader9* ps_ = nullptr;
    IDirect3DVertexDeclaration9* declaration_ = nullptr;
    IDirect3DVertexBuffer9* vb_ = nullptr;
    IDirect3DIndexBuffer9* ib_ = nullptr;
    IDirect3DVertexShader9* dim_vs_ = nullptr;
    IDirect3DPixelShader9* dim_ps_ = nullptr;
    IDirect3DTexture9* dim_texture_ = nullptr;
    IDirect3DSurface9* dim_surface_ = nullptr;
    unsigned dim_width_ = 0, dim_height_ = 0;
    bool dim_unavailable_ = false;
    std::uint32_t built_seed_ = 0;
    std::uint64_t built_key_ = 0;
    sky_stars::core::Look built_look_{};
    sky_stars::core::BuildStats built_stats_{};
    unsigned built_stars_ = 0;
};
} // namespace x3m::renderer
