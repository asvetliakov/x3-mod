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
// Cascades (docs/architecture/shadow-cascades.md): attach_cascades gives the
// pass up to four maps of their own sizes and one depth attachment of the
// largest size (a depth-stencil surface larger than the render target is
// documented D3D9); execute_cascades fills any subset of them in one
// transaction (one block capture/apply); per map the pass keeps the basis the
// owner retained for it, cleared by before_reset, detach and at the start of a
// transaction that rewrites the map, so a stale or partial map is never applied.
#include <cstdint>
#include <d3d9.h>
#include "shadow_replay_projection.h"
namespace x3m::renderer {
constexpr unsigned shadow_replay_maps_max = shadow_cascade_max;
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
    unsigned halved = 0; // cascade maps whose requested size exceeded MaxTextureWidth/Height and was halved
};
// One draw issue of a cascade transaction: the draw (index into the shared
// draw list) and that cascade's light rows c0-c2 (shadow_cascade_light_rows).
struct ShadowReplayIssue { std::uint16_t draw = 0; float rows[12]{}; };
struct ShadowReplayMapList { unsigned map = 0; const ShadowReplayIssue* issues = nullptr; unsigned count = 0; };
struct ShadowReplayRetained { ShadowReplayBasis basis{}; std::uint64_t frame = 0; unsigned draws = 0; bool valid = false; };
enum class ShadowReplayStage : unsigned { None, Validate, Targets, Block, Capture, Scene, Bind, Clear, Draw, EndScene, Restore };
struct ShadowReplayResult {
    HRESULT operation = S_FALSE, restore = S_FALSE;
    ShadowReplayStage failed = ShadowReplayStage::None;
    unsigned drawn = 0; // draws re-issued before the first failure
    unsigned drawn_map[shadow_replay_maps_max]{}; // execute_cascades: per map
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
    // The same with `count` (1..4) maps of their own sizes. A size above
    // MaxTextureWidth/Height is halved until it fits (caps().halved counts the
    // maps affected; size(i) is what was kept), and refused below 64.
    HRESULT attach_cascades(IDirect3DDevice9*, void* const* native, const D3DCAPS9&, D3DFORMAT adapter_format, const unsigned* sizes, unsigned count) noexcept;
    const ShadowReplayCaps& caps() const noexcept { return caps_; }
    // The map and its depth attachment (default pool), created lazily and
    // rebuilt after Reset; a failure releases every partial object.
    HRESULT prepare() noexcept;
    // One transaction: capture the block and the bindings, bind the map,
    // Clear, replay every draw, restore. A failed step restores and reports
    // its stage; a lost device stops restoration as the other passes do.
    HRESULT execute(const ShadowReplayDraw* draws, unsigned count, bool caller_scene_open, bool caller_stateblock_recording,
                    ShadowReplayResult*) noexcept;
    // One transaction over several maps: per list bind the map (the shared
    // depth attachment stays), Clear, issue its draws. Every listed map's
    // retained basis is invalidated first; the owner retains again on success.
    HRESULT execute_cascades(const ShadowReplayDraw* draws, unsigned draw_count, const ShadowReplayMapList* lists, unsigned list_count,
                             bool caller_scene_open, bool caller_stateblock_recording, ShadowReplayResult*) noexcept;
    void before_reset() noexcept;
    void after_reset(HRESULT) noexcept;
    void detach() noexcept;
    bool reset_pending() const noexcept { return reset_pending_; }
    unsigned references() const noexcept;
    unsigned allocations() const noexcept { return allocations_; }
    unsigned size() const noexcept { return sizes_[0]; }
    unsigned size(unsigned map) const noexcept { return map < count_ ? sizes_[map] : 0; }
    unsigned maps() const noexcept { return count_; }
    unsigned depth_size() const noexcept { return depth_size_; }
    bool targets_ready() const noexcept { return map_surfaces_[0] != nullptr && depth_ != nullptr; }
    IDirect3DSurface9* map_surface(unsigned map = 0) const noexcept { return map < shadow_replay_maps_max ? map_surfaces_[map] : nullptr; } // borrowed; null until prepared
    // What the scene-end apply quad consumes (legacy-sun-application.md,
    // section 2): the map as a texture (borrowed; null until prepared or
    // after before_reset) and the frame's view -> sun-space rows
    // (shadow_replay_view_rows), which the owner records per frame beside
    // the draws it replays; the rows are invalidated by before_reset and
    // detach so a stale frame can never be applied.
    IDirect3DTexture9* map_texture(unsigned map = 0) const noexcept { return map < shadow_replay_maps_max ? maps_[map] : nullptr; }
    // The basis a map was last replayed with (the apply composes it with the
    // current camera); null while the map is absent: never replayed, rewritten
    // by a failed or running transaction, or released by before_reset/detach.
    const ShadowReplayRetained* retained(unsigned map) const noexcept { return map < count_ && retained_[map].valid ? &retained_[map] : nullptr; }
    void retain(unsigned map, const ShadowReplayBasis& basis, std::uint64_t frame, unsigned draws) noexcept {
        if (map < count_ && basis.valid && maps_[map]) { retained_[map].basis = basis; retained_[map].frame = frame; retained_[map].draws = draws; retained_[map].valid = true; }
    }
    // Everything a past replay published: the per-cascade bases and the single
    // map's view rows, so no consumer (the apply quad, the F8 dump) can read a
    // map the current state no longer stands behind.
    void invalidate_retained() noexcept { for (auto& r : retained_) r.valid = false; view_rows_valid_ = false; }
    void invalidate_retained(unsigned map) noexcept { if (map < shadow_replay_maps_max) retained_[map].valid = false; }
    const float* view_rows() const noexcept { return view_rows_valid_ ? view_rows_ : nullptr; }
    void set_view_rows(const float rows[12]) noexcept {
        view_rows_valid_ = rows != nullptr;
        if (rows) for (unsigned i = 0; i < 12; ++i) view_rows_[i] = rows[i];
    }
private:
    struct SavedState;
    template<class Fn> Fn call(unsigned slot) const noexcept {
        return reinterpret_cast<Fn>((vtable_ ? vtable_ : *reinterpret_cast<void* const* const*>(device_))[slot]);
    }
    HRESULT attach_maps(IDirect3DDevice9*, void* const* native, const D3DCAPS9&, D3DFORMAT adapter_format, const unsigned* sizes, unsigned count, bool halve) noexcept;
    HRESULT ensure_block() noexcept;
    HRESULT bind() noexcept;
    HRESULT bind_map(unsigned map) noexcept;
    HRESULT issue(const ShadowReplayDraw&, const float* rows, unsigned vectors) noexcept;
    void release_targets() noexcept;
    IDirect3DDevice9* device_ = nullptr;
    void* const* vtable_ = nullptr;
    ShadowReplayCaps caps_{};
    IDirect3DStateBlock9* block_ = nullptr;
    IDirect3DVertexShader9* vs_ = nullptr;
    IDirect3DPixelShader9* ps_ = nullptr;
    IDirect3DTexture9* maps_[shadow_replay_maps_max]{};
    IDirect3DSurface9* map_surfaces_[shadow_replay_maps_max]{};
    IDirect3DSurface9* depth_ = nullptr;
    unsigned sizes_[shadow_replay_maps_max]{}, count_ = 0, depth_size_ = 0, render_targets_ = 0, allocations_ = 0;
    ShadowReplayRetained retained_[shadow_replay_maps_max]{};
    float view_rows_[12]{};
    bool view_rows_valid_ = false;
    bool reset_pending_ = false;
};
} // namespace x3m::renderer
