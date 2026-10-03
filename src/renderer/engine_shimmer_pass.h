#pragma once
// Engine heat shimmer (docs/architecture/engine-exhaust-gap-analysis.md gap 9, phase 5; effects-modernisation-opus.md
// section 3.8): the GPU side. After the temporal resolve and before the HDR write-back and bloom read the resolved FP16
// image, run() copies that image over the union of the frame's rects (engine_shimmer_core.h collect) into a scratch
// texture with one same-format, same-size StretchRect, then draws the rects' quads into the resolved image with the
// refraction pixel program (src/effects/engine_shimmer_ps.hlsl) sampling the copy, scissored to the union. The resolved
// image is also the TAA history, so revert() copies the saved rect back after the frame's consumers (the caller does it
// before Present): the next resolve never sees the shimmer (it stays unaveraged, and no displacement accumulates).
//
// Objects: the pass-through quad vertex program and declaration (renderer/quad_vertex_program.h) and the pixel program,
// created at attach; a D3DSBT_ALL state block and the scratch (A16B16G16R16F render-target texture at the target's
// size, DEFAULT pool, one level), created at the first run and again after a size change. before_reset() releases the
// block and the scratch and forgets a pending revert (the target goes with the TAA history); the programs survive
// Reset. Documented D3D9 only: shader model 3, FP16 render-target textures with filtering (D3DUSAGE_QUERY_FILTER), the
// StretchRect-from-texture capability, scissor test, CULLNONE; the compiled program's creation is the slot test
// (docs/architecture/platform-portability.md, "Shader slot budget"). Every device call goes through the saved native
// table; run() saves and restores every state it touches (the block, the render targets, depth, viewport, scissor rect,
// the vertex input mode) and leaves the caller's scene as it found it.
#include "../proxy/engine_shimmer_core.h"
#include <d3d9.h>
#include <cstdint>
namespace x3m::renderer {
struct EngineShimmerCaps {
    bool enabled = false;
    const char* reason = "not_attached";
    unsigned ps_slots = 0;
    HRESULT fp16_filter = S_FALSE; // the CheckDeviceFormat result
};
struct EngineShimmerFrame {
    UINT width = 0, height = 0;
    IDirect3DTexture9* target = nullptr;         // the resolved A16B16G16R16F image (written in place; borrowed)
    IDirect3DSurface9* target_surface = nullptr; // its level 0 (borrowed until revert() or before_reset())
    IDirect3DTexture9* lane = nullptr;           // device depth in .r at the target's size (-1 sky); null: no occlusion
    const engine_shimmer::Rect* rects = nullptr;
    unsigned rect_count = 0;
    float amplitude_px = 0.f; // pixels (engine_shimmer::amplitude_px)
    float seconds = 0.f;      // the stage's clock (engine_plumes::StageClock::wrapped)
    bool caller_scene_open = true;
    bool caller_stateblock_recording = false;
};
enum class EngineShimmerStep : unsigned { None, Validate, Resources, Copy, Capture, State, Draw, Restore };
struct EngineShimmerReport {
    HRESULT operation = S_FALSE, restore = S_FALSE;
    EngineShimmerStep failed = EngineShimmerStep::None;
    const char* skipped = nullptr; // S_FALSE without a device call: why
    unsigned calls = 0, rects = 0;
    unsigned scissor_px = 0, copy_px = 0; // the union's area and the copied area
    int scissor[4]{}, copy[4]{};
    bool drew = false;
    bool stale_revert = false; // a revert was still pending at this run (the caller missed its Present step)
};
class EngineShimmerPass {
public:
    EngineShimmerPass() = default;
    ~EngineShimmerPass();
    EngineShimmerPass(const EngineShimmerPass&) = delete;
    EngineShimmerPass& operator=(const EngineShimmerPass&) = delete;
    // Checks the capabilities and creates the programs; a refusal names its reason in caps().reason and holds nothing
    // (D3DERR_NOTAVAILABLE for a missing capability).
    HRESULT attach(IDirect3DDevice9*, void* const* native, const D3DCAPS9&, D3DFORMAT adapter_format) noexcept;
    void detach() noexcept;
    const EngineShimmerCaps& caps() const noexcept { return caps_; }
    void before_reset() noexcept;
    void after_reset(HRESULT) noexcept;
    // Draws the frame's rects into the target (see the header comment). S_FALSE with no device call when there is
    // nothing to draw (no rect, zero amplitude) or the frame does not qualify (report.skipped: format, recording);
    // E_FAIL while a Reset is pending; a failed call names its step, and the caller's state is restored (a lost device
    // stops the restoration). The copy has been made when the step is past Copy: revert() is then pending.
    HRESULT run(const EngineShimmerFrame&, EngineShimmerReport*) noexcept;
    // The copy back into the target over the copied rect (S_FALSE: nothing pending). Clears the pending state.
    HRESULT revert() noexcept;
    bool revert_pending() const noexcept { return pending_ != nullptr; }
    // Forgets a pending revert without a device call (the target was released or its content no longer matters).
    void forget() noexcept { pending_ = nullptr; }
    unsigned references() const noexcept;
    UINT scratch_width() const noexcept { return scratch_width_; }
#if defined(X3M_ENGINE_SHIMMER_FIXTURE) || defined(X3M_MOTION_OUTPUT_FIXTURE)
    // Fixture faults: bit 0 refuses the FP16 filter capability at attach, bit 1 fails the draw once, bit 2 fails the
    // scratch's creation once.
    void set_faults(unsigned faults) noexcept { faults_ = faults; }
#endif
private:
    struct SavedState;
    template <class Fn> Fn call(unsigned slot) const noexcept {
        ++calls_;
        return reinterpret_cast<Fn>(vtable_[slot]);
    }
    HRESULT ensure_scratch(UINT width, UINT height) noexcept;
    HRESULT normalize(IDirect3DSurface9* target, UINT width, UINT height, const int scissor[4]) noexcept;
    void release_frame_objects() noexcept;
    IDirect3DDevice9* device_ = nullptr;
    void* const* vtable_ = nullptr; // borrowed
    EngineShimmerCaps caps_{};
    bool reset_pending_ = false;
    mutable unsigned calls_ = 0;
    unsigned faults_ = 0;
    UINT render_targets_ = 1, streams_ = 1;
    IDirect3DVertexShader9* vs_ = nullptr;
    IDirect3DPixelShader9* ps_ = nullptr;
    IDirect3DVertexDeclaration9* declaration_ = nullptr;
    IDirect3DStateBlock9* block_ = nullptr;
    IDirect3DTexture9* scratch_ = nullptr;
    IDirect3DSurface9* scratch_surface_ = nullptr;
    UINT scratch_width_ = 0, scratch_height_ = 0;
    IDirect3DSurface9* pending_ = nullptr; // the target surface a revert writes (borrowed)
    RECT pending_rect_{};
};
} // namespace x3m::renderer
