#pragma once
#include <d3d9.h>
#include <cstddef>
#include <cstdint>

namespace x3m::renderer {
// An explicit semantic attestation, not a declaration inferred from a name.
// POSITION0 must be finite FLOAT3 or FLOAT16_4; shader position is four row
// dot-products with (xyz,1). Stored W is ignored, including packed-half W.
enum class RigidPositionSemantic { Unknown, PositionXyzWOneRowDots };
struct RigidMotionDraw {
    RigidPositionSemantic semantic = RigidPositionSemantic::Unknown;
    IDirect3DVertexBuffer9* vertices = nullptr; // native, borrowed
    IDirect3DIndexBuffer9* indices = nullptr;   // optional native, borrowed
    UINT stream_offset = 0, stride = 0, position_offset = 0, stream_frequency = 1;
    D3DDECLTYPE position_type = D3DDECLTYPE_FLOAT3;
    D3DPRIMITIVETYPE topology = D3DPT_TRIANGLELIST;
    UINT primitive_count = 0;
    // DrawPrimitive start_vertex, OR the exact DrawIndexedPrimitive arguments.
    UINT start_vertex = 0, minimum_vertex = 0, vertex_count = 0, start_index = 0;
    INT base_vertex = 0;
    D3DCULL cull = D3DCULL_NONE;
    float current_wvp[16]{}, previous_wvp[16]{};
    // Caller verified ordinary opaque rasterization, matching adjacent geometry,
    // stable VB/IB contents through this replay and exact submitted row matrices.
    // Excludes instancing, deformation, alpha-test/discard/blend, custom depth,
    // depth bias, user clipping, scissor and nonordinary position conversion.
    bool correspondence_attested = false;
};
struct RigidMotionInputs {
    IDirect3DTexture9* motion = nullptr; // caller-owned native RGBA32F RT, mip zero
    IDirect3DSurface9* scene_depth = nullptr; // original still-valid native D24X8
    UINT width = 0, height = 0; // full zero-origin viewport, depth range [0,1]
    const RigidMotionDraw* draws = nullptr;
    std::size_t draw_count = 0;
    // Both WVPs above include their ACTUAL submitted raster jitter. Output removes
    // previous jitter here because resolve.hlsl adds it when consuming motion.
    float previous_jitter[2]{}; // raster pixels, positive Y down
    bool scene_depth_current = false;
    bool caller_scene_open = true;
    bool caller_stateblock_recording = false;
    bool caller_queries_idle = false;
    // Depth EQUAL establishes depth agreement, not exclusive color ownership.
    // Caller must mask/reject unsupported transparent or equal-depth competing
    // contributors, including draws outside this batch. No whole-frame coverage
    // is implied. Uncovered pixels initialize to alpha=-1, never camera fallback.
};
struct RigidMotionOutput {
    // Borrowed caller texture, published ONLY after draw + restoration success.
    // A failed run may partially overwrite the texture; do not consume it.
    IDirect3DTexture9* motion = nullptr;
    std::uint64_t generation = 0;
};
struct RigidMotionDiagnostics {
    HRESULT operation = S_OK, restoration = S_OK;
    std::size_t completed_draws = 0;
};
class RigidMotionPass {
public:
    RigidMotionPass() = default;
    ~RigidMotionPass();
    RigidMotionPass(const RigidMotionPass&) = delete;
    RigidMotionPass& operator=(const RigidMotionPass&) = delete;
    // Device borrowed; caller serializes render/reset and releases pass before
    // native Reset/final device teardown. No persistent VB/IB/RT/DS references.
    HRESULT initialize(IDirect3DDevice9* native_device, const DWORD* vertex_shader,
                       const DWORD* pixel_shader) noexcept;
    HRESULT run(const RigidMotionInputs&, RigidMotionOutput*) noexcept;
    void before_reset() noexcept;
    void shutdown() noexcept;
    RigidMotionDiagnostics diagnostics() const noexcept { return diagnostics_; }
private:
    IDirect3DDevice9* device_ = nullptr;
    IDirect3DVertexShader9* vertex_ = nullptr;
    IDirect3DPixelShader9* pixel_ = nullptr;
    IDirect3DVertexDeclaration9* declarations_[2]{};
    UINT targets_ = 0, streams_ = 0;
    std::uint64_t generation_ = 0;
    RigidMotionDiagnostics diagnostics_{};
};
} // namespace x3m::renderer
