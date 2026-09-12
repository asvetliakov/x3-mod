#pragma once
// CPU-side ABI for resolve.hlsl. This module owns no D3D interfaces or GPU state.
#include <cmath>
#include <cstdint>

namespace x3::temporal {
struct ResolveConstants {
    float clip_to_previous[4][4]{}; // row-major rows, column-vector multiplication
    float size_jitter[4]{};
    float history[4]{};
    // Depth tolerance max(absolute, relative * |expected|), HDR limit, minimum W.
    float rejection[4]{0.0001f, 0.02f, 65000.0f, 0.000001f};
    // motion enabled, reactive masks enabled, mask-snapshot mode, depth-sentinel
    // policy (0 off, 1 sentinel pixels current-only, 2 camera path at the far plane).
    float options[4]{};
    // c22 (kLuminanceRegister; c8..c21 belong to the AgX block, agx.h): x is k
    // of the reversible luminance weighting w = 1 / (1 + k * luma) the resolve
    // applies to the current colour, its 3x3 neighbourhood and every history
    // tap before the statistics, the clip and the blend, inverting afterwards
    // (docs/architecture/hdr-scene-path.md, section 3). 0 is the exact
    // identity (the 8-bit route, and the migration test); the HDR route uploads
    // the exposure multiplier the write-back applies to the resolved image.
    float luminance[4]{};
};
static_assert(sizeof(ResolveConstants) == 9 * 4 * sizeof(float));
constexpr unsigned kResolveRegisterCount = 8;   // c0..c7, uploaded as one block
constexpr unsigned kLuminanceRegister = 22;     // ResolveConstants::luminance
constexpr float kLuminanceMaxK = 65504.f;       // FP16 max; the weighted domain stays finite

// Invalidate on camera cut, device loss/reset, resize, scene/camera regime changes,
// missing motion, exposure convention changes, and any failed resolve. Calling
// completed() is allowed only after color/depth AND any required reactive mask
// histories succeed. Unavailable coverage must not complete usable history.
struct HistoryState {
    std::uint32_t width=0, height=0;
    // Stable scene/camera-regime + resource-generation token. NOT a per-frame
    // counter or depth-clear count: ordinary frame rendering retains history.
    std::uint64_t epoch=0;
    bool valid=false;
    void invalidate() noexcept { valid=false; }
    void begin(std::uint32_t w, std::uint32_t h, std::uint64_t e) noexcept {
        if(w!=width || h!=height || e!=epoch) valid=false;
        width=w; height=h; epoch=e;
        if(!w || !h) valid=false;
    }
    void completed() noexcept { valid=width!=0 && height!=0; }
};
// Populate constants from pixel displacement, whose positive Y points down.
// Matrix must exclude jitter; equal-sized histories are required. The current
// jitter (c4.zw) is consumed by the camera path only. The previous jitter is
// still packed into c5.xy for ABI stability but the shader does not read it:
// history is the accumulated output on the unjittered grid and is sampled at
// the previous unjittered position, so no previous-jitter term exists.
// sentinel_camera (with depth_sentinel_reactive) reprojects sentinel pixels
// through the camera matrix at the far plane instead of keeping them
// current-only; only valid when matrix_rows is a real camera reprojection.
// luminance_k is the weighting constant (c22.x): finite, 0 <= k <= 65504.
inline bool prepare(ResolveConstants& out, const HistoryState& state,
                    const float* matrix_rows, float current_x, float current_y,
                    float previous_x, float previous_y, float weight,
                    bool motion_enabled, bool reactive_enabled=false,
                    bool depth_sentinel_reactive=false, bool sentinel_camera=false,
                    float luminance_k=0.f) noexcept {
    if(!matrix_rows || !state.width || !state.height || !std::isfinite(weight)
        || weight<0 || weight>1 || !std::isfinite(current_x) || !std::isfinite(current_y)
        || !std::isfinite(previous_x) || !std::isfinite(previous_y)
        || !std::isfinite(luminance_k) || luminance_k<0 || luminance_k>kLuminanceMaxK) return false;
    for(unsigned i=0;i<16;++i) {
        if(!std::isfinite(matrix_rows[i]) || std::fabs(matrix_rows[i])>1e15f) return false;
        out.clip_to_previous[i/4][i%4]=matrix_rows[i];
    }
    if(!std::isfinite(out.rejection[0]) || out.rejection[0]<0
       || !std::isfinite(out.rejection[1]) || out.rejection[1]<0
       || !std::isfinite(out.rejection[2]) || out.rejection[2]<=0 || out.rejection[2]>65000
       || !std::isfinite(out.rejection[3]) || out.rejection[3]<=0) return false;
    out.size_jitter[0]=1.f/state.width; out.size_jitter[1]=1.f/state.height;
    out.size_jitter[2]=current_x/state.width; out.size_jitter[3]=current_y/state.height;
    out.history[0]=previous_x/state.width; out.history[1]=previous_y/state.height;
    out.history[2]=weight; out.history[3]=state.valid?1.f:0.f;
    out.options[0]=motion_enabled?1.f:0.f;
    out.options[1]=reactive_enabled?1.f:0.f;
    out.options[2]=0;
    out.options[3]=depth_sentinel_reactive?(sentinel_camera?2.f:1.f):0.f;
    out.luminance[0]=luminance_k; out.luminance[1]=out.luminance[2]=out.luminance[3]=0.f;
    return true;
}
} // namespace x3::temporal
