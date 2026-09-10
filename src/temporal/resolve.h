#pragma once
// CPU-side ABI for resolve.hlsl. This module owns no D3D interfaces or GPU state.
#include <cmath>
#include <cstdint>

namespace x3::temporal {
struct ResolveConstants {
    float clip_to_previous[4][4]{}; // row-major rows, column-vector multiplication
    float size_jitter[4]{};
    float history[4]{};
    float rejection[4]{0.0001f, 0.0f, 65000.0f, 0.000001f};
    float options[4]{}; // motion enabled, reactive masks enabled, mask-snapshot mode, reserved
};
static_assert(sizeof(ResolveConstants) == 8 * 4 * sizeof(float));

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
// Matrix must exclude jitter; equal-sized histories are required.
inline bool prepare(ResolveConstants& out, const HistoryState& state,
                    const float* matrix_rows, float current_x, float current_y,
                    float previous_x, float previous_y, float weight,
                    bool motion_enabled, bool reactive_enabled=false) noexcept {
    if(!matrix_rows || !state.width || !state.height || !std::isfinite(weight)
        || weight<0 || weight>1 || !std::isfinite(current_x) || !std::isfinite(current_y)
        || !std::isfinite(previous_x) || !std::isfinite(previous_y)) return false;
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
    out.options[2]=out.options[3]=0;
    return true;
}
} // namespace x3::temporal
