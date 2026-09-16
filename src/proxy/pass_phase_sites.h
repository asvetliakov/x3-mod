#pragma once
#include "engine_patch.h"

// Per-draw stamps inside the D3DX effect pass loop of the material submission
// routine 0x004c0150 (X3M_PASS_PHASES=1), in loop order. Exact instruction
// boundaries, the single incoming edge (the loop's `jb` back edge onto
// pass_begin), the plain-copy contract, the frame-depth anchors that put ESP
// at the routine's frame base at all four sites and the disjointness from the
// point-light admission patch are independently checked by
// verification/probe/verify_pass_phase_sites.py. Owning native study:
// docs/reverse-engineering/effect-pass-loop.md, sections 2-3.
//
// Every span is a plain copy (no relative control transfer inside), so the
// arena tail is byte-identical at any address: no rel32 field to re-base. The
// lean stub (pass_phases.cpp emit) restores every register and the flags
// before `jmp [next]`, and the displaced instructions run in the tail at the
// game's exact ESP. Incoming flags are dead at all four sites (the next
// consumer is the `cmp` at 0x004c4050 after pass_end).
namespace x3m::pass_phases::sites {
enum Index : unsigned {
    PassBegin = 0,   // 4c3ff0: loop head, opens the pass-apply interval (BeginPass)
    PassApplied = 1, // 4c4000: BeginPass returned; closes apply, opens draw
    PassDrawn = 2,   // 4c403e: DrawIndexedPrimitive returned; closes draw, opens EndPass
    PassEnd = 3,     // 4c4049: EndPass returned; closes EndPass, one pass counted
    Count = 4
};
constexpr unsigned kSiteCount = Count;
constexpr engine_patch::SiteSpec kSites[Count] = {
    {"pass_phase_pass_begin",0x004c3ff0,{0x8b,0x44,0x24,0x74,0x8b,0x13},6,0,0},
    {"pass_phase_pass_applied",0x004c4000,{0x8b,0x44,0x24,0x28,0x8b,0x48,0x14},7,0,0},
    {"pass_phase_pass_drawn",0x004c403e,{0x8b,0x13,0x8b,0x82,0x08,0x01,0x00,0x00},8,0,0},
    {"pass_phase_pass_end",0x004c4049,{0x8b,0x44,0x24,0x74,0x83,0xc0,0x01},7,0,0},
};
static_assert(sizeof(kSites)/sizeof(kSites[0]) == Count, "All pass stamps are one group");
}
