// Sky stars, Decision C2 (docs/architecture/sector-background-modern.md, "Decision C"): the deferred batch's pixel
// program, the in-place one with SKY_STARS_DIM (the energy times 1 - k saturate(L / L0)). Compiled with
// tools/shaders/generate_sky_stars_programs.py (ps_3_0).
#define SKY_STARS_DIM 1
#include "sky_stars_ps.hlsl"
