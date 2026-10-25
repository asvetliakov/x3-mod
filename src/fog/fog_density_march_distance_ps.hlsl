// The long distance fade (docs/architecture/fog-visible-distance.md; fog_distance > 1): the look march with the inverse-square
// fade, the 40 geometric tail bins and the horizon level (c36, c37, s8). fog_distance = 1 keeps fog_density_march_look.
#define FOG_DISTANCE
#include "fog_density_march_look_ps.hlsl"
