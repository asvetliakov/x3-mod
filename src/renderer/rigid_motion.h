#pragma once
#include <d3d9.h>
#include <cstddef>
#include <cstdint>

namespace x3m::renderer {
enum class RigidReplaySource : std::uint8_t { Unknown, ReviewedArchiveSm3, OriginalSyntheticSm3 };
// Value proof issued once from exact immutable source bytecode, then cached with
// that shader's identity. Copying is safe; no bytecode or COM resource is retained.
// The upstream draw record MUST associate this token with its actual submitted
// source program, not borrow a valid token from another shader. This is an API
// invariant, not protection against arbitrary memory corruption in the caller.
class RigidReplayContract {
public:
    RigidReplayContract() = default; // Unknown cannot admit a draw.
    RigidReplaySource source() const noexcept { return source_; }
    std::uint64_t source_hash() const noexcept { return source_hash_; }
    std::uint32_t source_words() const noexcept { return source_words_; }
    bool qualified() const noexcept { return source_ != RigidReplaySource::Unknown; }
private:
    RigidReplaySource source_ = RigidReplaySource::Unknown;
    std::uint64_t source_hash_ = 0;
    std::uint32_t source_words_ = 0;
    friend RigidReplayContract qualify_rigid_replay_source(const std::uint32_t*, std::size_t) noexcept;
#ifdef X3M_RIGID_MOTION_VERIFICATION
    // Only original synthetic fixtures compile this explicit test issuer.
    friend RigidReplayContract original_synthetic_sm3_contract() noexcept;
#endif
};
// Exact reviewed whole-program lookup + SM3/XYZW/MAD constructor gate. No COM
// or allocation; never reads vertex/index buffers. Perform once at source admission.
RigidReplayContract qualify_rigid_replay_source(const std::uint32_t* words,
                                               std::size_t count) noexcept;
#ifdef X3M_RIGID_MOTION_VERIFICATION
RigidReplayContract original_synthetic_sm3_contract() noexcept;
#endif
struct RigidMotionDraw {
    RigidReplayContract source_program{};
    // Explicit ordinary finite XYZ proof; exact constructor tests do not waive it.
    // Both FLOAT3 and FLOAT16_4 preserve native declaration conversion. Stored W
    // is ignored, including exceptional packed-half W; only XYZ must be finite.
    bool finite_positions_attested = false;
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
    // Vertex program is fixed internally; callers cannot replace its arithmetic.
    HRESULT initialize(IDirect3DDevice9* native_device,
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
