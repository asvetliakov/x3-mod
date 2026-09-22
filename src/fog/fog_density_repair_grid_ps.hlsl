// The single look's repair with the sun-visibility slice grid (FOG_SHADOW_PASS): the same texture the march read, so a
// repaired pixel and its half-resolution neighbours share one shaft law. Drawn with X3M_FOG_SHADOW_PASS=1 only.
#define FOG_LOOK
#define FOG_LOOK_NO_OFFSET
#define FOG_SHADOW_PASS
#include "fog_density_repair_ps.hlsl"
