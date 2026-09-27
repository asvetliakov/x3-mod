// The camera-gate resolve with the luminance lock (docs/architecture/taa-luminance-lock.md): resolve_far_camera_hold.hlsl
// plus the lock lane as COLOR3 (the previous lane at s13, c14 / c15); see resolve.hlsl, X3M_LUMA_LOCK.
#define X3M_REGION_HOLD 1
#define X3M_LUMA_LOCK 1
#include "resolve.hlsl"
