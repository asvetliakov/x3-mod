#pragma once
// Engine ribbons, phase 3 (docs/architecture/engine-effects-modern.md sections 3-5): the GPU side of the proxy's ribbon
// trails and the ring buffers they draw from. One class owns the vs_3_0 / ps_3_0 pair (src/effects/
// engine_ribbon_{vs,ps}.hlsl), their declaration, one dynamic vertex buffer (DEFAULT, DYNAMIC | WRITEONLY, 256 ribbons
// x 34 vertices x 48 B, one DISCARD lock per frame), the static strip index buffer, and the CPU pool of 256 ribbons
// (engine_ribbons_core.h). The device objects are created at attach (the arming latch) and released before Reset,
// which also clears the pool; ensure_resources() recreates them after a successful Reset.
//
// run() updates the pool from the frame's glow-jet records (the plume stage's EnginePlumesFrame: records, camera, lane,
// preset, fog) and draws every drawable ribbon with one DrawIndexedPrimitive inside the caller's state bracket, after
// the plumes (TemporalPass FrameInputs::stage_callback: RT0 the FP16 scene, RT1+ and the depth surface unbound). It sets
// the full state it draws with (the plume pass's list: programs, declaration, stream, indices, constants, the lane at
// s0 point/clamp, CLIPPING, CULLMODE NONE, Z off, ONE/ONE additive), so it draws correctly whether or not the plume
// pass drew this frame. Documented D3D9 only; capabilities are checked at attach as the plume pass checks them.
#include "../proxy/engine_ribbons_core.h"
#include "engine_plumes_pass.h"
#include <d3d9.h>
#include <cstdint>
namespace x3m::renderer {
struct EngineRibbonsCaps {
    bool enabled = false;
    const char* reason = "not_attached";
    unsigned vs_slots = 0, ps_slots = 0;
    HRESULT fp16_blending = S_FALSE;
};
struct EngineRibbonsFrame {
    const EnginePlumesFrame* base = nullptr; // camera, lane, records, preset, fog (the plume stage's frame)
    double seconds = 0.;                     // the pool's clock (monotonic seconds, any epoch)
    bool cut = false;                        // the resolve's camera cut this frame
    std::uint64_t load_epoch = 0;            // object_lifetime load epoch, 0 unknown
};
enum class EngineRibbonsStep : unsigned { None, Validate, Resources, Lock, State, Draw };
struct EngineRibbonsReport {
    HRESULT operation = S_FALSE;
    EngineRibbonsStep failed = EngineRibbonsStep::None;
    unsigned calls = 0;
    engine_ribbons::UpdateStats update{};
    engine_ribbons::BuildStats stats{};
    bool updated = false, drew = false;
};
class EngineRibbonsPass {
public:
    EngineRibbonsPass() = default;
    ~EngineRibbonsPass();
    EngineRibbonsPass(const EngineRibbonsPass&) = delete;
    EngineRibbonsPass& operator=(const EngineRibbonsPass&) = delete;
    HRESULT attach(IDirect3DDevice9*, void* const* native, const D3DCAPS9&, D3DFORMAT adapter_format) noexcept;
    void detach() noexcept;
    const EngineRibbonsCaps& caps() const noexcept { return caps_; }
    // Every device object goes before Reset and the pool is cleared (a Reset is a cut for the ribbons).
    void before_reset() noexcept;
    void after_reset(HRESULT) noexcept;
    HRESULT ensure_resources() noexcept;
    bool resources_ready() const noexcept {
        return device_ && caps_.enabled && !reset_pending_ && vs_ && ps_ && declaration_ && vb_ && ib_;
    }
    // A camera cut seen on a frame the stage did not run: the next run clears the pool first.
    void note_cut() noexcept { cut_pending_ = true; }
    unsigned live() const noexcept { return pool_.live; }
    const engine_ribbons::Pool& pool() const noexcept { return pool_; }
    // Updates the pool and draws (see the header comment). S_FALSE with no device call when nothing is drawable; a
    // failed call leaves the report's failed step; D3DERR_DEVICELOST codes are passed through.
    HRESULT run(const EngineRibbonsFrame&, EngineRibbonsReport*) noexcept;
    unsigned references() const noexcept;
    static constexpr UINT vertex_bytes = engine_ribbons::max_vertices * UINT(sizeof(engine_ribbons::Vertex));
    static constexpr UINT index_bytes = engine_ribbons::max_ribbons * engine_ribbons::indices_per_ribbon * 2u;
#if defined(X3M_ENGINE_PLUMES_FIXTURE) || defined(X3M_MOTION_OUTPUT_FIXTURE)
    // Fixture faults: bit 0 refuses the FP16 blending capability at attach, bit 1 fails the draw once.
    void set_faults(unsigned faults) noexcept { faults_ = faults; }
#endif
private:
    template <class Fn> Fn call(unsigned slot) const noexcept {
        ++calls_;
        return reinterpret_cast<Fn>(vtable_[slot]);
    }
    HRESULT create_objects(bool* programs) noexcept;
    void release_objects() noexcept;
    IDirect3DDevice9* device_ = nullptr;
    void* const* vtable_ = nullptr; // borrowed
    EngineRibbonsCaps caps_{};
    bool reset_pending_ = false, cut_pending_ = false;
    mutable unsigned calls_ = 0;
    unsigned faults_ = 0;
    IDirect3DVertexShader9* vs_ = nullptr;
    IDirect3DPixelShader9* ps_ = nullptr;
    IDirect3DVertexDeclaration9* declaration_ = nullptr;
    IDirect3DVertexBuffer9* vb_ = nullptr;
    IDirect3DIndexBuffer9* ib_ = nullptr;
    engine_ribbons::Pool pool_{};
};
} // namespace x3m::renderer
