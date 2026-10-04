#pragma once
// Engine plumes, phase 2 (docs/architecture/engine-effects-modern.md sections 3-6): the GPU side of the proxy's own
// engine plumes. One class owns the vs_3_0 / ps_3_0 pair (src/effects/engine_plume_{vs,ps}.hlsl), their declaration,
// one dynamic vertex buffer (DEFAULT, DYNAMIC | WRITEONLY, 1,024 nozzles x 8 vertices x 76 B, one DISCARD lock per
// frame) and the static quad index buffer, all created at attach (the arming latch, not the first plume) and all
// released before Reset; ensure_resources() recreates them after a successful Reset.
//
// run() builds the frame's vertices straight into the locked buffer (engine_plumes_core.h build(): the glow-jet records
// of the frame, the camera's view rows, the preset, the stage's clock, the flow phase and the look) and draws every nozzle with one
// DrawIndexedPrimitive inside the caller's state bracket: the temporal resolve calls it through FrameInputs::stage_callback after its normalize (RT0 the
// FP16 scene the resolve reads, RT1+ and the depth surface unbound, every render state at the pass's baseline, the
// scene open) and normalizes again afterwards, so this pass sets only what it draws with: the programs, declaration,
// stream, indices, constants, the lane at s0 (point, clamp), CLIPPING, CULLMODE NONE, Z off, ONE/ONE additive. No
// SetRenderTarget, no depth: occlusion is the lane's soft term. Documented D3D9 only; capabilities are checked at attach
// (shader model 3, FP16 post-pixel-shader blending on the adapter format, BLENDOP and ONE blend caps, CULLNONE, index
// and primitive limits, the compiled programs' slots; D3DDECLTYPE_D3DCOLOR is a base type, no capability bit).
#include "../proxy/engine_plumes_core.h"
#include <d3d9.h>
#include <cstdint>
namespace x3m::renderer {
struct EnginePlumesCaps {
    bool enabled = false;
    const char* reason = "not_attached";
    unsigned vs_slots = 0, ps_slots = 0;
    HRESULT fp16_blending = S_FALSE; // the CheckDeviceFormat result
};
struct EnginePlumesFrame {
    UINT width = 0, height = 0;
    engine_plumes::View view{};             // world -> view rows, m00 / m11, height, NEAR
    float m20 = 0.f, m21 = 0.f;             // the jittered projection's offset terms (m20 + 2 jx / W, m21 - 2 jy / H)
    float m22 = 0.f, m32 = 0.f;             // depth law of an R32F lane (ignored when four_channel)
    bool lane_four_channel = false;
    IDirect3DTexture9* lane = nullptr;      // the completed RT2 (borrowed)
    const engine_effects::core::Record* records = nullptr;
    unsigned record_count = 0;
    engine_plumes::BodyLookup body = nullptr;
    engine_plumes::ViewFilter filter{};     // null tags: every record is drawn
    engine_plumes::Preset preset = engine_plumes::default_preset;
    float seconds = 0.f;                    // the stage's clock, wrapped (engine_plumes::StageClock::wrapped)
    double flow = 0.;                       // the flow accumulator, nozzle widths (engine_plumes::FlowPhase; each nozzle's
                                            // phase is it x its flow_factor)
    float travel = 0.f;                     // the SETA travel weight 0..1 (engine_plumes::TravelRamp; the ribbons' T too)
    float step = 0.f;                       // this frame's stage-clock step, seconds (the attack memory's clock)
    float game_ms = 0.f;                    // this frame's step in game ms (step x the SETA rate): the attack's scale
    engine_plumes::Transients* transients = nullptr; // the RCS / brake attack memory (null: no attack); begun per run
    const engine_plumes::Look* look = nullptr; // null: engine_plumes::default_look (the proxy's carries the nozzle knob)
    const engine_plumes::LookTables* tables = nullptr; // look_tables(*look) cached at load (null: computed per run)
    const float* radii = nullptr;           // beside the records (Ring::parent_radius): the plume floor; null: none
    const std::uint32_t* parents = nullptr; // beside the records (Ring::parent): the co-located layer merge
                                            // (engine_plumes::merge_layers, plumes and ribbons); null: none
    const std::uint8_t* own = nullptr;      // beside the records (Ring::own): the own ship's jets, exempt from the
                                            // disc's distance law (engine_plumes_core.h); null: none
};
enum class EnginePlumesStep : unsigned { None, Validate, Resources, Lock, State, Draw };
struct EnginePlumesReport {
    HRESULT operation = S_FALSE;
    EnginePlumesStep failed = EnginePlumesStep::None;
    unsigned calls = 0;
    engine_plumes::BuildStats stats{};
    bool drew = false;
};
class EnginePlumesPass {
public:
    EnginePlumesPass() = default;
    ~EnginePlumesPass();
    EnginePlumesPass(const EnginePlumesPass&) = delete;
    EnginePlumesPass& operator=(const EnginePlumesPass&) = delete;
    // Checks the capabilities and creates the programs and buffers; a refusal names its reason in caps().reason and
    // holds nothing (D3DERR_NOTAVAILABLE for a missing capability).
    HRESULT attach(IDirect3DDevice9*, void* const* native, const D3DCAPS9&, D3DFORMAT adapter_format) noexcept;
    void detach() noexcept;
    const EnginePlumesCaps& caps() const noexcept { return caps_; }
    // Every device object goes before Reset; ensure_resources() (called by run) recreates programs and buffers after a
    // successful one.
    void before_reset() noexcept;
    void after_reset(HRESULT) noexcept;
    HRESULT ensure_resources() noexcept;
    bool resources_ready() const noexcept {
        return device_ && caps_.enabled && !reset_pending_ && vs_ && ps_ && declaration_ && vb_ && ib_;
    }
    // Draws the frame's records (see the header comment for the state contract). S_FALSE with no device call when no
    // record is drawable; a failed call leaves the report's failed step; D3DERR_DEVICELOST codes the device returned are
    // passed through (the internal reset-pending flag fails with E_FAIL: only the stage is skipped, not the resolve).
    HRESULT run(const EnginePlumesFrame&, EnginePlumesReport*) noexcept;
    unsigned references() const noexcept;
    static constexpr UINT vertex_bytes = engine_plumes::max_vertices * UINT(sizeof(engine_plumes::Vertex));
    static constexpr UINT index_bytes = engine_plumes::max_nozzles * engine_plumes::indices_per_nozzle * 2u;
#if defined(X3M_ENGINE_PLUMES_FIXTURE) || defined(X3M_MOTION_OUTPUT_FIXTURE)
    // Fixture faults: bit 0 refuses the FP16 blending capability at attach, bit 1 fails the draw once, bit 2 fails
    // every draw.
    void set_faults(unsigned faults) noexcept { faults_ = faults; }
#endif
private:
    template <class Fn> Fn call(unsigned slot) const noexcept {
        ++calls_;
        return reinterpret_cast<Fn>(vtable_[slot]);
    }
    HRESULT create_objects(bool* programs) noexcept; // *programs: the programs and declaration were created
    void release_objects() noexcept;
    IDirect3DDevice9* device_ = nullptr;
    void* const* vtable_ = nullptr; // borrowed
    EnginePlumesCaps caps_{};
    bool reset_pending_ = false;
    mutable unsigned calls_ = 0;
    unsigned faults_ = 0;
    IDirect3DVertexShader9* vs_ = nullptr;
    IDirect3DPixelShader9* ps_ = nullptr;
    IDirect3DVertexDeclaration9* declaration_ = nullptr;
    IDirect3DVertexBuffer9* vb_ = nullptr;
    IDirect3DIndexBuffer9* ib_ = nullptr;
};
} // namespace x3m::renderer
