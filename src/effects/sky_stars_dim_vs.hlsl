// Sky stars, Decision C2 (docs/architecture/sector-background-modern.md, "Decision C"): the deferred batch's vertex
// program, the in-place one with SKY_STARS_DIM (the dim-copy UV and the dimming law for the pixel program). Compiled with
// tools/shaders/generate_sky_stars_programs.py (vs_3_0).
#define SKY_STARS_DIM 1
#include "sky_stars_vs.hlsl"
