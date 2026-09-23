// The single look with 24 far bins (docs/architecture/fog-gpu-cost.md, step B; launcher --fog-far-bins 24, X3M_FOG_FAR_BINS):
// ds = (cap - 12000) / 24 over the unchanged far range. fog_density_march_look stays the default and the accepted look.
#define FOG_FAR_BINS 24
#include "fog_density_march_look_ps.hlsl"
