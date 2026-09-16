#pragma once
// Scalar helpers for code on the light-envelope draw path (cpu_state.h
// LightCallBoundary): the MinGW <math.h> inlines fabs as an x87 "fabs" and
// links sqrtf to an x87 routine that returns in st(0), which check_no_x87.py
// refuses. These stay in SSE (or are plain integer work) on the i686 build and
// fall back to <cmath> on host builds without SSE2.
#include <cmath>
#include <cstdint>
#include <cstring>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace x3m::scalar {
// |x| by clearing the sign bit; the same value as std::fabs for every input
// including -0.0, NaN and infinities.
inline double abs(double x) noexcept {
    std::uint64_t bits; std::memcpy(&bits, &x, sizeof bits);
    bits &= ~(std::uint64_t(1) << 63);
    std::memcpy(&x, &bits, sizeof x);
    return x;
}
// floor/ceil for |x| < 2^31 (the callers clamp first), by the truncating
// cvttsd2si conversion, so the result does not depend on the rounding mode in
// MXCSR (the application's, live inside a light hook). NaN and larger
// magnitudes are returned unchanged (larger doubles are the callers' clamp
// limits or already integral).
inline double floor(double x) noexcept {
    if (!(x > -2147483648.0 && x < 2147483648.0)) return x;
    const double t = double(int(x));
    return t > x ? t - 1.0 : t;
}
inline double ceil(double x) noexcept {
    if (!(x > -2147483648.0 && x < 2147483648.0)) return x;
    const double t = double(int(x));
    return t < x ? t + 1.0 : t;
}
// sqrtss: IEEE square root like std::sqrt(float), NaN for a negative input,
// without the errno path that makes GCC call the library routine.
inline float sqrt(float x) noexcept {
#if defined(__SSE2__)
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x)));
#else
    return std::sqrt(x);
#endif
}
} // namespace x3m::scalar
