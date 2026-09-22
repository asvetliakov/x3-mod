// The single look with the sun-visibility slice grid in place of the in-march shaft lookup (FOG_SHADOW_PASS;
// docs/architecture/fog-shadow-pass.md). Drawn with X3M_FOG_SHADOW_PASS=1 only; fog_density_march_look is the control.
#define FOG_LOOK
#define FOG_SHADOW_PASS
#include "fog_density_march_ps.hlsl"
