#pragma once
#include "engine_patch.h"

// Ten accumulate-only stamps (X3M_LOOP_PHASES=1): six inside the main loop's
// per-sector update driver 0x0043a360, in routine order, and four around the
// three calls of the main loop's `input_part=0` region that contains its
// only callsite (sites 6-9, below). The driver walks the
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
//
// Sites 6-9 bracket the three calls of the main loop's `input_part=0` region
// 0x00403b09..0x00403b3a (the note's section 1 and the "Run380" section):
// cutevent (6 -> 7, `call 0x0048f550`), containers (7 -> 8, `call 0x0043a360`,
// which holds sites 0-5 nested inside it) and sweep (8 -> 9, the container
// walk with its `call 0x0045b660` per class-1 container). Sites 6 and 7 are
// `call rel32` (rel32 at offset 1, both callees take no argument and return
// with a plain `ret`); site 8 is the plain copy `mov eax,[0x0060850c]` (EAX
// and the flags are dead: the span writes EAX itself, the walk's `cmp`
// rewrites the flags). Site 9 is the `jne rel32` (offset 2, target
// 0x00403db8) after the region's end marker: 0x00403b3a (`cmp
// [esi+0x4d8],ebp`) is the installed game_phase_input_body site, so the sweep
// closes on the next instruction, which every exit of the region reaches (the
// pause edge 0x00403b10 and the empty-list edge 0x00403b26 land on 0x00403b3a,
// the walk falls through to it). Its incoming flags are live (the `cmp`'s);
// the stub's pushfd/popfd hands them to the replayed `jne` in the tail
// unchanged. The pause edge reaches site 9 with nothing open, which is not an
// orphan. No direct branch lands on any of the four spans (the main loop
// 0x00403840 is decoded whole); the main loop has one caller (0x0040373a), so
// all four are main-loop-thread only, the Present thread the gate admits. ESP
// is the main loop's frame at all four sites: the region's only push is the
// `push edi` that 0x0045b660's `ret 4` removes.
namespace x3m::loop_phases::sites {
enum Index : unsigned {
    SectorCollide = 0,    // 43a38e: pass A, opens collide (0x0045d250)
    SectorSimulate = 1,   // 43a394: closes collide, opens simulate (0x00452ad0)
    SectorPost = 2,       // 43a39a: closes simulate, opens post (0x0045b720)
    SectorPassAEnd = 3,   // 43a3a0: closes post; every pass-A container (gate edges land here)
    SectorEconomy = 4,    // 43a3be: pass B, opens passb (0x004596e0 then 0x004526b0)
    SectorPassBEnd = 5,   // 43a3ca: closes passb; every pass-B container (gate edges land here)
    RegionCutEvent = 6,   // 403b12: opens cutevent (call 0x0048f550)
    RegionContainers = 7, // 403b17: closes cutevent, opens containers (call 0x0043a360)
    RegionSweep = 8,      // 403b1c: closes containers, opens sweep (the walk and 0x0045b660)
    RegionEnd = 9,        // 403b40: closes sweep; the pause edge reaches it with nothing open
    Count = 10
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
    {"loop_phase_region_cutevent",0x00403b12,{0xe8,0x39,0xba,0x08,0x00},5,0,1},
    {"loop_phase_region_containers",0x00403b17,{0xe8,0x44,0x68,0x03,0x00},5,0,1},
    {"loop_phase_region_sweep",0x00403b1c,{0xa1,0x0c,0x85,0x60,0x00},5,0,0},
    {"loop_phase_region_end",0x00403b40,{0x0f,0x85,0x72,0x02,0x00,0x00},6,0,2},
};
// clang-format on
static_assert(sizeof(kSites) / sizeof(kSites[0]) == Count, "All loop stamps are one group");
}
