#pragma once
#include "engine_patch.h"

// Two accumulate-only stamps that split the residual of the pass and frame
// groups (X3M_RESIDUAL_PHASES=1). Exact instruction boundaries, the incoming
// edges (the four guard/skip edges of the material-initialisation path land
// on the material_setup span start; view_particles has none), the plain-copy
// contract, the ESP anchors (material_setup sits at the frame base of
// 0x004c0150, proved by Begin's `lea eax,[esp+0x88]` one push deep writing
// the same slot the pass loop reads at frame depth; view_particles displaces
// the `add esp,4` that removes the particles argument, which runs in the
// claim tail at the game's exact ESP like frame site 4's `add esp,0x10`) and
// the disjointness from every installed game/frame/pass site, the scene hook
// and the point-light patch are checked by
// verification/probe/verify_residual_phase_sites.py. Owning studies:
// docs/reverse-engineering/effect-pass-loop.md section 7 and
// docs/reverse-engineering/frame-loop-phases.md section 5d.
//
// material_setup (0x004c1eab, `mov edx,[ebx]; mov ecx,[edx+0xfc]`) is the
// ID3DXEffect::Begin dispatch of the material submission routine: everything
// from the previous pass_end to here is the engine's per-object preparation
// (loop tail, End, routine exit, the caller's queue walk, cull, the next
// node's entry and its guards), everything from here to the next pass_begin
// is D3DX (Begin, the parameter setters, the two engine render-state writes
// and the geometry guard). view_particles (0x0047230c, `mov edx,[0x608518];
// add esp,4`) is the return of the particles call 0x004bf4c0 in the frame
// routine's per-view loop: from the view's view_submit_end stamp to here is
// the particles pass. Incoming flags are dead at both sites; the `add esp,4`
// leaves flags the next `test` overwrites.
namespace x3m::residual_phases::sites {
enum Index : unsigned {
    MaterialSetup = 0, // 4c1eab: closes prepare (from the last pass_end), opens setup (to the first pass_begin)
    ViewParticles = 1, // 47230c: closes particles (from the view's view_submit_end)
    Count = 2
};
constexpr unsigned kSiteCount = Count;
constexpr engine_patch::SiteSpec kSites[Count] = {
    {"residual_phase_material_setup",0x004c1eab,{0x8b,0x13,0x8b,0x8a,0xfc,0x00,0x00,0x00},8,0,0},
    {"residual_phase_view_particles",0x0047230c,{0x8b,0x15,0x18,0x85,0x60,0x00,0x83,0xc4,0x04},9,0,0},
};
static_assert(sizeof(kSites)/sizeof(kSites[0]) == Count, "Both residual stamps are one group");
}
