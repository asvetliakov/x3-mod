// The 24-far-bin look's repair at the quarter-resolution sample spacing (docs/architecture/fog-gpu-cost.md, step C; launcher --fog-march-scale 4, X3M_FOG_MARCH_SCALE):
// samples at full pixels 4q under the same depth-class law. The scale-2 program stays the default and the accepted look.
#define FOG_MARCH_SCALE 4
#include "fog_density_repair_look_far24_ps.hlsl"
