#pragma once
// Live same-draw motion route (checkpoint B1). Opt-in with X3M_MOTION_OUTPUT=1.
// One instance per hooked device, owned by capture.cpp's Device and called only
// from its hooks under the capture mutex. Every device call made here goes
// through the NATIVE vtable slots supplied at attach, never the hooked table,
// so the shadow state and the scene selector observe application calls only.
//
// Responsibilities: shader-variant registry, application state shadow, motion
// render target ownership across Reset/release, one-time capability self-test,
// per-frame sentinel fill, per-draw gate evaluation with variant substitution
// and exact restoration, previous-row history, and capture-frame diagnostics.
// See docs/architecture/live-motion-route.md (Implementation section).
#include <d3d9.h>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>
#include "../renderer/scene_boundary.h"
#include "../renderer/motion_history.h"
#include "../renderer/motion_row_history.h"

namespace x3m {
struct MotionDrawCall {
    bool indexed = false, user_memory = false;
    D3DPRIMITIVETYPE topology = D3DPT_TRIANGLELIST;
    UINT primitives = 0, first = 0;
    INT base_vertex = 0;
    UINT min_vertex = 0, vertex_count = 0;
};
// The first gate that refused a draw; None means every gate passed. Numbers
// match the design document's gate list.
enum class MotionGate : unsigned { None = 0, Feature = 1, Scene = 2, Pair = 3, DrawState = 4, Scope = 5, History = 6 };
// Per-draw decision. Stack object; carries what after_draw must undo.
struct MotionRoute {
    MotionGate gate = MotionGate::Feature;
    bool routed = false, matched = false, scene = false;
    bool vs_set = false, ps_set = false, rt_set = false, write_set = false;
    bool vs_constants_set = false, ps_constants_set = false;
    DWORD saved_write1 = 15;
    renderer::RigidDrawKey key{};
    std::uint64_t rows_hash = 0;
    std::uint64_t load_epoch = 0, registry_epoch = 0;
};
struct MotionFrameCounters {
    std::uint32_t draws = 0, routed = 0, matched = 0, gates[7]{};
    std::uint32_t apply_failures = 0, restore_failures = 0;
    bool latched = false, filled = false;
    HRESULT fill_result = S_FALSE, fill_restore = S_OK;
};

#ifdef X3M_MOTION_OUTPUT_FIXTURE
// Original synthetic fixture seam; absent from production builds. It replaces
// the game observers with a caller-supplied scope and lets the fixture's own
// shaders act as the selector's background family. Exported by capture.cpp.
struct MotionOutputFixtureScope {
    std::uint32_t known = 0;
    std::uint64_t load_epoch = 0, registry_epoch = 0, node_serial = 0, camera_serial = 0;
    std::uintptr_t node = 0, camera = 0, registry = 0, mesh = 0;
    std::uint32_t node_handle = 0, camera_handle = 0, model = 0, lod = 0;
};
struct MotionOutputFixtureConfig {
    std::uint32_t size = sizeof(MotionOutputFixtureConfig);
    std::uint64_t background_vs[3]{}, background_ps[3]{};
    MotionOutputFixtureScope scope{};
};
#endif

class MotionOutput {
public:
    MotionOutput() noexcept;
    ~MotionOutput();
    MotionOutput(const MotionOutput&) = delete;
    MotionOutput& operator=(const MotionOutput&) = delete;

    // Lifecycle. attach runs the capability gate and the one-time mixed-format
    // MRT self test; on failure the route stays disabled for this device.
    void attach(IDirect3DDevice9* device, void** native, std::uint64_t device_id,
                const D3DCAPS9& caps, bool requested) noexcept;
    bool enabled() const noexcept { return enabled_; }
    // Device references held by owned objects (variants, sentinel shader,
    // motion target surface), one per object in every reference model the
    // route runs under (native D3D9 and the ownership wrapper; see
    // ensure_target). The release hook releases them before the
    // application's final Release.
    unsigned device_references() const noexcept;
    void release_resources() noexcept;
    void before_reset() noexcept;
    void after_reset(HRESULT result) noexcept;

    // Frame boundaries, called from the Present hook.
    void begin_frame(std::uint64_t frame, bool capture) noexcept;
    void before_present() noexcept;
    void after_present(HRESULT result) noexcept;

    // Variant registry: called after a successful native create with the hash
    // the capture already computed. Pointer reuse replaces the old entry.
    void register_vertex_shader(IDirect3DVertexShader9* shader, const DWORD* code,
                                std::size_t bytes, std::uint64_t hash) noexcept;
    void register_pixel_shader(IDirect3DPixelShader9* shader, const DWORD* code,
                               std::size_t bytes, std::uint64_t hash) noexcept;

    // Shadow updates from successful application setter calls. Ignored while a
    // state block is recording, because recorded calls do not reach the device.
    void set_vertex_shader(IDirect3DVertexShader9* shader) noexcept;
    void set_pixel_shader(IDirect3DPixelShader9* shader) noexcept;
    void set_vertex_constants_f(UINT start, const float* data, UINT count) noexcept;
    void set_vertex_constants_i(UINT start, const int* data, UINT count) noexcept;
    void set_pixel_constants_f(UINT start, const float* data, UINT count) noexcept;
    void set_stream_source(UINT stream, IDirect3DVertexBuffer9* buffer, UINT offset, UINT stride) noexcept;
    void set_indices(IDirect3DIndexBuffer9* buffer) noexcept;
    void set_vertex_declaration(IDirect3DVertexDeclaration9* declaration) noexcept;
    void set_fvf(DWORD fvf) noexcept;
    void set_viewport(const D3DVIEWPORT9* viewport) noexcept;
    void begin_stateblock() noexcept;
    void end_stateblock() noexcept;
    // A state block Apply changes device state outside the setter hooks:
    // resynchronize the whole shadow from the public getters (once per Apply).
    void stateblock_applied() noexcept;

    // Scene-boundary events, after the application call completed. The
    // SetRenderTarget/depth events also refresh the binding shadow.
    void before_clear(DWORD count, DWORD flags, float z) noexcept;
    void after_clear(HRESULT result) noexcept;
    void after_set_render_target(DWORD index, IDirect3DSurface9* surface, HRESULT result) noexcept;
    void after_set_depth(IDirect3DSurface9* surface, HRESULT result) noexcept;
    void after_stretch(IDirect3DSurface9* source, const RECT* source_rect,
                       IDirect3DSurface9* destination, const RECT* destination_rect, HRESULT result) noexcept;
    void after_color_fill(IDirect3DSurface9* destination, const RECT* rect, HRESULT result) noexcept;
    void unsupported(HRESULT result) noexcept;

    // Per-draw route. before_draw performs the pending sentinel fill, evaluates
    // the gates cheapest first and, when routed, substitutes the variant pair,
    // reserved constants and RT1. after_draw restores exactly what was set,
    // feeds the selector and writes capture diagnostics.
    MotionRoute before_draw(const MotionDrawCall& call) noexcept;
    void after_draw(MotionRoute& route, HRESULT result) noexcept;
    const MotionFrameCounters& counters() const noexcept { return counters_; }

#ifdef X3M_MOTION_OUTPUT_FIXTURE
    void fixture_configure(const MotionOutputFixtureConfig& config) noexcept;
    HRESULT fixture_readback(float* out, std::size_t floats, UINT* width, UINT* height) noexcept;
#endif

private:
    struct ShaderEntry { std::uint64_t hash = 0; IUnknown* variant = nullptr; };
    struct Shadow {
        IDirect3DVertexShader9* vs = nullptr;
        IDirect3DPixelShader9* ps = nullptr;
        std::uint64_t vs_hash = 0, ps_hash = 0;
        IDirect3DVertexShader9* vs_variant = nullptr;
        IDirect3DPixelShader9* ps_variant = nullptr;
        float rows[16]{};             // c24-27 as submitted
        bool rows_known = false;
        float vs_reserved[16]{};      // application c252-255, restored only if written
        bool vs_reserved_written = false;
        float ps_reserved[8]{};       // application c216-217
        bool ps_reserved_written = false;
        int integer0[4]{};
        bool integer0_known = false;
        std::uint64_t stream0 = 0, indices = 0, declaration = 0;
        UINT stream0_offset = 0, stream0_stride = 0;
        std::uint32_t position_offset = 0, position_type = 0;
        renderer::Surface rt0, depth;
        bool extra_rt[4]{};
        renderer::Viewport viewport;
        bool recording = false;
    };
    struct SavedState;
    template<typename Fn> Fn native(unsigned slot) const noexcept { return reinterpret_cast<Fn>(native_[slot]); }
    bool self_test(char* reason, std::size_t reason_size) noexcept;
    HRESULT draw_quad(IDirect3DSurface9* rt0, IDirect3DSurface9* rt1, IDirect3DPixelShader9* shader,
                      UINT width, UINT height, HRESULT* restore) noexcept;
    HRESULT save_state(SavedState& saved) noexcept;
    HRESULT restore_state(const SavedState& saved) noexcept;
    bool ensure_target(UINT width, UINT height) noexcept;
    void release_target() noexcept;
    void resync_shadow() noexcept;
    void fill_sentinel() noexcept;
    void describe_binding(DWORD index, IDirect3DSurface9* surface) noexcept;
    renderer::Event event(renderer::EventKind kind) noexcept;
    void bindings(renderer::Event& event) const noexcept;
    bool scene_bound() const noexcept;
    bool sample_scope(MotionRoute& route) noexcept;
    void observe(renderer::Event& event, HRESULT result) noexcept;
    void undo(MotionRoute& route) noexcept;
    renderer::SceneSignatures signatures() const noexcept;
    void readback() noexcept;

    IDirect3DDevice9* device_ = nullptr;
    void** native_ = nullptr;
    std::uint64_t id_ = 0, frame_ = 0, generation_ = 1, sequence_ = 0;
    D3DCAPS9 caps_{};
    bool requested_ = false, enabled_ = false, capture_ = false, telemetry_ = false;
    bool history_available_ = false, releasing_ = false;
    std::map<void*, ShaderEntry> vertex_, pixel_;
    Shadow shadow_{};
    IDirect3DSurface9* target_surface_ = nullptr; // Level 0 of the owned RGBA32F texture.
    UINT target_width_ = 0, target_height_ = 0;
    bool target_failed_ = false;
    IDirect3DPixelShader9* sentinel_ps_ = nullptr;
    renderer::SceneBoundarySelector selector_;
    renderer::Surface main_, main_depth_;
    renderer::Event pending_{};
    bool pending_valid_ = false, fill_pending_ = false;
    // Sized at attach when the route is requested; a Device that never asked
    // for the route does not pay for the two reserved tables.
    renderer::MotionRowHistory history_{0};
    MotionFrameCounters counters_{};
    unsigned logged_failures_ = 0;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    MotionOutputFixtureConfig fixture_{};
    bool fixture_configured_ = false;
#endif
};
} // namespace x3m
