#pragma once
#include "engine_patch.h"

// Six accumulate-only stamps inside the main loop's per-sector update driver
// 0x0043a360 (X3M_LOOP_PHASES=1), in routine order. The driver walks the
// universe container list twice (class word [+0x48] == 1, skip bit
// [+0x148] & 1): pass A calls collide/simulate/post per active container,
// pass B calls economy and attach. Exact instruction boundaries, the incoming
// edges (the two gate `jne`s of each pass land on the pass-end span start:
// 0x0043a384/0x0043a38c -> sector_pass_a_end, 0x0043a3b4/0x0043a3bc ->
// sector_pass_b_end; the loop back edges 0x0043a3a5/0x0043a3cf return to the
// loop heads 0x0043a380/0x0043a3b0, outside every span), the rel32 contract of
// the four displaced calls, the ESP contract (no frame, no argument, every
// callee `ret 4`) and the disjointness from the 47 installed game-phase sites
// are checked by verification/probe/verify_loop_phase_sites.py. Owning study:
// docs/reverse-engineering/main-loop-input-region.md, sections 2 and 4.
//
// Sites 0, 1, 2 and 4 are `push esi; call rel32` (rel32 at offset 2, re-based
// by the claim tail so the callee keeps its absolute target and sees a return
// address in the arena, the contract the game_phase clock/pump/channels sites
// rely on). Sites 3 and 5 are plain copies of `mov esi,[esi]; cmp [esi],0`:
// the span is exactly the five patched bytes, so a gate edge landing on the
// span start executes the patch jump; the tail replays both instructions after
// the stub's popfd, and the `cmp` leaves the flags the following `jne` back
// edge consumes. Incoming flags are dead at all six sites.
namespace x3m::loop_phases::sites {
enum Index : unsigned {
    SectorCollide = 0,  // 43a38e: pass A, opens collide (0x0045d250)
    SectorSimulate = 1, // 43a394: closes collide, opens simulate (0x00452ad0)
    SectorPost = 2,     // 43a39a: closes simulate, opens post (0x0045b720)
    SectorPassAEnd = 3, // 43a3a0: closes post; every pass-A container (gate edges land here)
    SectorEconomy = 4,  // 43a3be: pass B, opens passb (0x004596e0 then 0x004526b0)
    SectorPassBEnd = 5, // 43a3ca: closes passb; every pass-B container (gate edges land here)
    Count = 6
};
constexpr unsigned kSiteCount = Count;
// clang-format off
constexpr engine_patch::SiteSpec kSites[Count] = {
    {"loop_phase_sector_collide",0x0043a38e,{0x56,0xe8,0xbc,0x2e,0x02,0x00},6,0,2},
    {"loop_phase_sector_simulate",0x0043a394,{0x56,0xe8,0x36,0x87,0x01,0x00},6,0,2},
    {"loop_phase_sector_post",0x0043a39a,{0x56,0xe8,0x80,0x13,0x02,0x00},6,0,2},
    {"loop_phase_sector_pass_a_end",0x0043a3a0,{0x8b,0x36,0x83,0x3e,0x00},5,0,0},
    {"loop_phase_sector_economy",0x0043a3be,{0x56,0xe8,0x1c,0xf3,0x01,0x00},6,0,2},
    {"loop_phase_sector_pass_b_end",0x0043a3ca,{0x8b,0x36,0x83,0x3e,0x00},5,0,0},
};
// clang-format on
static_assert(sizeof(kSites) / sizeof(kSites[0]) == Count, "All sector stamps are one group");
}
