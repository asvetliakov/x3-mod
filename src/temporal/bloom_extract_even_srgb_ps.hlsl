// Four-tap sRGB extraction. BOTH source dimensions must be even.
#define BLOOM_EVEN
#define BLOOM_EXTRACT
#define BLOOM_DECODE_MODE 1
#include "bloom_down_ps.hlsl"
