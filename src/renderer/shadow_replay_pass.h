#pragma once
// One-cascade depth replay of the frame's own caster candidates into a private
// sun-space map (docs/architecture/shadow-replay-gates.md, section 2). The
// class owns the map (R32F render target, or an unreadable X8R8G8B8 target
// with colour writes off when R32F is not a render-target format), the depth
// attachment (D24X8, else D16), the two authored programs and a D3DSBT_ALL
// state block, and re-issues the candidates' draws with their own buffers,
// declaration and cull mode under an authored vertex program. Nothing samples
// the map; the caller (MotionOutput) decides the frame, proves the leases and
// serializes rendering, Reset and teardown. Documented D3D9 only.
#include <cstdint>
#include <d3d9.h>
namespace x3m::renderer {
struct ShadowReplayDraw {
    IDirect3DVertexBuffer9* vertex_buffer = nullptr;   // borrowed for the call: the caller holds the lease
    IDirect3DIndexBuffer9* index_buffer = nullptr;     // null for a non-indexed draw
    IDirect3DVertexDeclaration9* declaration = nullptr;
    UINT stream_offset = 0, stride = 0;
    D3DPRIMITIVETYPE topology = D3DPT_TRIANGLELIST;
    UINT primitives = 0, first = 0, min_vertex = 0, vertex_count = 0;
    INT base_vertex = 0;
    bool indexed = false;
    DWORD cull_mode = D3DCULL_NONE;
    float light_rows[16]{}; // shadow_replay_light_rows output: c0-c3 of the authored program
};
struct ShadowReplayCaps {
    bool enabled = false;
    const char* reason = "detached";
    D3DFORMAT map_format = D3DFMT_UNKNOWN, depth_format = D3DFMT_UNKNOWN;
    bool readable = false; // the map is an R32F colour target that GetRenderTargetData can copy
    HRESULT formats = S_FALSE, programs = S_FALSE;
};
enum class ShadowReplayStage : unsigned { None, Validate, Targets, Block, Capture, Scene, Bind, Clear, Draw, EndScene, Restore };
struct ShadowReplayResult {
    HRESULT operation = S_FALSE, restore = S_FALSE;
    ShadowReplayStage failed = ShadowReplayStage::None;
    unsigned drawn = 0; // draws re-issued before the first failure
};
class ShadowReplayPass {
public:
    ShadowReplayPass() = default;
    ~ShadowReplayPass();
    ShadowReplayPass(const ShadowReplayPass&) = delete;
    ShadowReplayPass& operator=(const ShadowReplayPass&) = delete;
    // Device and native table borrowed (no AddRef). Qualifies the formats with
    // CheckDeviceFormat / CheckDepthStencilMatch and creates the two programs
    // (surviving Reset). A refusal leaves the pass detached with caps().reason.
    HRESULT attach(IDirect3DDevice9*, void* const* native, const D3DCAPS9&, D3DFORMAT adapter_format, unsigned size) noexcept;
    const ShadowReplayCaps& caps() const noexcept { return caps_; }
    // The map and its depth attachment (default pool), created lazily and
    // rebuilt after Reset; a failure releases every partial object.
    HRESULT prepare() noexcept;
    // One transaction: capture the block and the bindings, bind the map,
    // Clear, replay every draw, restore. A failed step restores and reports
    // its stage; a lost device stops restoration as the other passes do.
    HRESULT execute(const ShadowReplayDraw* draws, unsigned count, bool caller_scene_open, bool caller_stateblock_recording,
                    ShadowReplayResult*) noexcept;
    void before_reset() noexcept;
    void after_reset(HRESULT) noexcept;
    void detach() noexcept;
    bool reset_pending() const noexcept { return reset_pending_; }
    unsigned references() const noexcept;
    unsigned allocations() const noexcept { return allocations_; }
    unsigned size() const noexcept { return size_; }
    bool targets_ready() const noexcept { return map_surface_ != nullptr && depth_ != nullptr; }
    IDirect3DSurface9* map_surface() const noexcept { return map_surface_; } // borrowed; null until prepared
private:
    struct SavedState;
    template<class Fn> Fn call(unsigned slot) const noexcept {
        return reinterpret_cast<Fn>((vtable_ ? vtable_ : *reinterpret_cast<void* const* const*>(device_))[slot]);
    }
    HRESULT ensure_block() noexcept;
    HRESULT bind() noexcept;
    void release_targets() noexcept;
    IDirect3DDevice9* device_ = nullptr;
    void* const* vtable_ = nullptr;
    ShadowReplayCaps caps_{};
    IDirect3DStateBlock9* block_ = nullptr;
    IDirect3DVertexShader9* vs_ = nullptr;
    IDirect3DPixelShader9* ps_ = nullptr;
    IDirect3DTexture9* map_ = nullptr;
    IDirect3DSurface9* map_surface_ = nullptr;
    IDirect3DSurface9* depth_ = nullptr;
    unsigned size_ = 0, render_targets_ = 0, allocations_ = 0;
    bool reset_pending_ = false;
};
} // namespace x3m::renderer
