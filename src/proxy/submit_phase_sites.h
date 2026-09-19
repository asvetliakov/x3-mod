#pragma once
#include "engine_patch.h"

// Twenty-two accumulate-only stamps on the view_submit candidates R1-R8 of
// docs/reverse-engineering/view-submit-hot-path.md (X3M_SUBMIT_PHASES=1).
// Every span is whole instructions; the only relative transfers inside a span
// are the declared `call rel32` fields (the three sort returns `push reg;
// call 0x0047e6e0`, the two D3DXMatrixInverse calls, the walk-miss `push 0x70;
// call _malloc`), which the claim tail re-bases. Exact bytes, instruction
// boundaries, the incoming edges (all on span starts), the routine each span
// belongs to and disjointness from every other claimed site (game, frame,
// pass, residual, loop, media, chase and loading groups, the scene hook, the
// point-light patch, the cull census/small-parts trampolines and the collide
// patches) are checked by verification/probe/verify_submit_phase_sites.py.
//
// Pairs (open -> close):
//   sort      0x0047e620 entry -> the instruction after each of its three
//             callers' `call` (the routine's two 4-byte epilogues cannot hold
//             a 5-byte claim and the second one abuts the next function)
//   walk      0x0047e264 -> 0x0047e285 (miss: the malloc path starts) or
//             0x0047e315 (hit: the join both paths reach; ignored when the
//             miss stamp already closed the pair)
//   technique 0x004c0c2a (SetTechnique vtable load) -> 0x004c0c36
//   end       0x004c405d (End vtable load) -> 0x004c4068 (also reached by the
//             eleven early-out edges with nothing open: counted `idle`)
//   block     0x004c1eb3 (the Begin argument pushes, directly after the
//             residual group's span) -> 0x004c3fde (the pass-loop guard), or
//             0x004c405d when the geometry guard at 0x004c3fd8 skips it
//   inverse_world / inverse_view  the `call` -> the instruction after it
//   material  0x004c0150 entry -> 0x004c5230, one instruction after its only
//             caller's return (`add esp,0x18` cannot start a span: the
//             caller's skip edge 0x004c502a lands behind it; that edge
//             reaches the stamp with nothing open: `idle`)
//   world     0x004bdee0 entry -> 0x0047e007 / 0x0047e711, its two callers' returns
// No span holds an x87 instruction and the stub never touches the x87 stack
// (lean_stub.h emit_context), so the unresolved st(0) liveness of section 5.3
// does not matter here. Flags: every stub restores EFLAGS before the tail, so
// the displaced `cmp`s (0x004c0c36, 0x004c3fde, 0x004bdee3) set them as before.
namespace x3m::submit_phases::sites {
enum Index : unsigned {
    SortEnter = 0, SortReturnA, SortReturnB, SortReturnC,
    WalkBegin, WalkMiss, WalkJoin,
    TechniqueBegin, TechniqueEnd,
    EndBegin, EndEnd,
    BlockBegin, BlockEnd,
    InverseWorldBegin, InverseWorldEnd, InverseViewBegin, InverseViewEnd,
    MaterialEnter, MaterialReturn,
    WorldEnter, WorldReturnA, WorldReturnB,
    Count
};
constexpr unsigned kSiteCount = Count;
constexpr engine_patch::SiteSpec kSites[Count] = {
    {"submit_phase_sort_enter",0x0047e620,{0xf6,0x80,0x70,0x02,0x00,0x00,0x80},7,0,0},
    {"submit_phase_sort_return_a",0x004722b4,{0x56,0xe8,0x26,0xc4,0x00,0x00},6,0,2},
    {"submit_phase_sort_return_b",0x00472490,{0x56,0xe8,0x4a,0xc2,0x00,0x00},6,0,2},
    {"submit_phase_sort_return_c",0x0047e8f5,{0x57,0xe8,0xe5,0xfd,0xff,0xff},6,0,2},
    {"submit_phase_walk_begin",0x0047e264,{0x8b,0x87,0xa0,0x02,0x00,0x00,0x89,0x4c,0x24,0x18},10,0,0},
    {"submit_phase_walk_miss",0x0047e285,{0x6a,0x70,0xe8,0x38,0x30,0x09,0x00},7,0,3},
    {"submit_phase_walk_join",0x0047e315,{0x8b,0x83,0xf8,0x00,0x00,0x00},6,0,0},
    {"submit_phase_technique_begin",0x004c0c2a,{0x8b,0x0b,0x8b,0x91,0xe8,0x00,0x00,0x00},8,0,0},
    {"submit_phase_technique_end",0x004c0c36,{0x39,0xb7,0xa4,0x01,0x00,0x00},6,0,0},
    {"submit_phase_end_begin",0x004c405d,{0x8b,0x0b,0x8b,0x91,0x0c,0x01,0x00,0x00},8,0,0},
    {"submit_phase_end_end",0x004c4068,{0x8b,0x4d,0x08,0x8b,0x84,0x24,0xa0,0x00,0x00,0x00},10,0,0},
    {"submit_phase_block_begin",0x004c1eb3,{0x6a,0x01,0x8d,0x84,0x24,0x88,0x00,0x00,0x00},9,0,0},
    {"submit_phase_block_end",0x004c3fde,{0x83,0xbc,0x24,0x84,0x00,0x00,0x00,0x00},8,0,0},
    {"submit_phase_inverse_world_begin",0x004c2251,{0xe8,0xb6,0x8c,0x03,0x00},5,0,1},
    {"submit_phase_inverse_world_end",0x004c2256,{0x8d,0x84,0x24,0x90,0x03,0x00,0x00},7,0,0},
    {"submit_phase_inverse_view_begin",0x004c2316,{0xe8,0xf1,0x8b,0x03,0x00},5,0,1},
    {"submit_phase_inverse_view_end",0x004c231b,{0x8b,0x47,0x50,0x8b,0x0b},5,0,0},
    {"submit_phase_material_enter",0x004c0150,{0x55,0x8b,0xec,0x83,0xe4,0xf0},6,0,0},
    {"submit_phase_material_return",0x004c5230,{0x8b,0x4c,0x24,0x30,0x5f,0x5e},6,0,0},
    {"submit_phase_world_enter",0x004bdee0,{0x83,0xec,0x08,0x83,0x78,0x3c,0x00},7,0,0},
    {"submit_phase_world_return_a",0x0047e007,{0x8b,0x8b,0xac,0x01,0x00,0x00},6,0,0},
    {"submit_phase_world_return_b",0x0047e711,{0x8b,0x7e,0x10,0x8b,0x8f,0xac,0x01,0x00,0x00},9,0,0},
};
static_assert(sizeof(kSites)/sizeof(kSites[0]) == Count, "All submit stamps are one group");
}
