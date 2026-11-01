#pragma once
// Pure arithmetic over one FL camera sample, the engine's 64-byte row-vector projection and view
// buffers (docs/architecture/farnhams-legacy-camera.md): the figures of the fl.camera_sample
// diagnostic row, shared with the host oracle (verification/analysis/test_fl_camera_state.py). No
// Windows headers, no allocation, no fabs (the mingw build emits x87 for it); doubles are SSE2.
#include <cmath>
#include <cstdint>
#include <cstring>

namespace x3m::fl::camera_sanity {
enum class Shape : std::uint32_t { Other = 0, Perspective = 1, Orthographic = 2 };
struct Summary {
    Shape shape = Shape::Other;
    double aspect = 0;             // m11 / m00 when both are positive (5120x1440 gives 3.556)
    double ortho_dot_max = 0;      // max |r_i . r_j|, i != j, over the view's upper-left 3x3 rows
    double ortho_len_err_max = 0;  // max |1 - |r_i||
    double translation_length = 0; // |V[12..14]|
    bool finite = true;            // all 32 floats finite
};
inline double absolute(double x) noexcept {
    return x < 0 ? -x : x;
}
inline const char* shape_name(Shape shape) noexcept {
    return shape == Shape::Perspective ? "persp" : shape == Shape::Orthographic ? "ortho" : "other";
}
// Row-vector D3D projection (v' = v * P): m23 = P[11] = 1 with m33 = P[15] = 0 makes w_clip = z_view
// (Perspective); P[11] = 0 with P[15] = 1 is the orthographic form. Both need positive m00/m11 and
// zero P[3]/P[7]. Anything else, including a zeroed or garbage buffer, is Other.
inline Shape projection_shape(const float p[16]) noexcept {
    if (!(p[0] > 0.f) || !(p[5] > 0.f) || p[3] != 0.f || p[7] != 0.f) return Shape::Other;
    if (p[11] == 1.f && p[15] == 0.f) return Shape::Perspective;
    if (p[11] == 0.f && p[15] == 1.f) return Shape::Orthographic;
    return Shape::Other;
}
inline void summarize(const float projection[16], const float view[16], Summary& out) noexcept {
    out = Summary{};
    for (unsigned i = 0; i < 16; ++i)
        if (!std::isfinite(projection[i]) || !std::isfinite(view[i])) out.finite = false;
    if (!out.finite) return;
    out.shape = projection_shape(projection);
    if (projection[0] > 0.f && projection[5] > 0.f) out.aspect = double(projection[5]) / double(projection[0]);
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned j = i; j < 3; ++j) {
            double dot = 0;
            for (unsigned k = 0; k < 3; ++k) dot += double(view[i * 4 + k]) * double(view[j * 4 + k]);
            if (i == j) {
                const double error = absolute(1. - std::sqrt(dot));
                if (error > out.ortho_len_err_max) out.ortho_len_err_max = error;
            } else if (absolute(dot) > out.ortho_dot_max)
                out.ortho_dot_max = absolute(dot);
        }
    }
    out.translation_length = std::sqrt(double(view[12]) * view[12] + double(view[13]) * view[13] +
                                       double(view[14]) * view[14]);
}
// The camera differs from the previous sample's (projection then view, bitwise), ignoring P[10]/P[14]: the
// per-submission depth scratch the engine rewrites per draw, so changed=1 means the camera moved.
inline bool changed(const float projection[16], const float view[16], const float previous[32]) noexcept {
    for (unsigned i = 0; i < 16; ++i)
        if (i != 10 && i != 14 && std::memcmp(projection + i, previous + i, sizeof(float)) != 0) return true;
    return std::memcmp(view, previous + 16, 64) != 0;
}
inline void remember(const float projection[16], const float view[16], float previous[32]) noexcept {
    std::memcpy(previous, projection, 64);
    std::memcpy(previous + 16, view, 64);
}
}
