// Weapon effects, step 4: the bolt capsule's pixel program with the far flag (bolts-through-taa.md B'): the same source
// as weapon_bolt_ps.hlsl with oC1 = oC0 (the masked RT1 the route binds so the MRT set has no gap) and oC2 = far_flag_k x
// max(rgb) (RT2's GREEN lane). Drawn when the caller bound the flag targets (renderer::WeaponBoltDraw::far_flag).
#define BOLT_FAR_FLAG 1
#include "weapon_bolt_ps.hlsl"
