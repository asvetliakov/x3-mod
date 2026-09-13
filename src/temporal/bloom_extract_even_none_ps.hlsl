// Four-tap identity-decode extraction. BOTH source dimensions must be even.
#define BLOOM_EVEN
#define BLOOM_EXTRACT
#define BLOOM_DECODE_MODE 2
#include "bloom_down_ps.hlsl"
