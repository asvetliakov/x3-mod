#pragma once
#include "engine_patch.h"
// R7 whole-call: replay the real prologue and common epilogue in place.
// Exit resumes at native pop ebp; ret 4, never an appended trampoline ret.
namespace x3m::light_phases::sites {
enum Index : unsigned { Enter, Exit, Count };
constexpr engine_patch::SiteSpec kSites[Count] = {
    {"light_phase_enter",0x0047d5e0,{0x55,0x8b,0xec,0x83,0xe4,0xf0},6,0,0},
    {"light_phase_exit",0x0047d9ab,{0x5f,0x5e,0x5b,0x8b,0xe5},5,0,0},
};
}
