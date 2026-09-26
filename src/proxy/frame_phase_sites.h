#pragma once
#include "engine_patch.h"

// Per-frame stamps inside the render routine 0x00471f50 (X3M_FRAME_PHASES=1),
// in recorder ID order. Exact instruction boundaries, incoming edges, the
// scene-hook disjointness and the relocated call targets are independently
// checked by verification/probe/verify_frame_phase_sites.py. Owning native
// study: docs/reverse-engineering/frame-loop-phases.md, section 4.
//
// Every span runs in the arena tail at the game's exact ESP: the shared stub
// (game_phases.cpp emit) ends with popad/popfd and `jmp [next]`, and the
// dispatcher is a plain `jmp [entry]`, so Views (`add esp,0x10`) and
// ViewSetupBegin (`push esi`, then a call whose return address lies in the
// tail like every displaced call) keep the native stack contract.
namespace x3m::frame_phases::sites {
enum Index : unsigned {
    Prologue = 0,        // 471f6c call 4f4fc0: frame prologue
    SceneUpdate = 1,     // 472044 call 4714c0: lens flare + recursive node update
    BeginScene = 2,      // 4720b5: BeginScene, per-view update, view qsort
    Views = 3,           // 472186: the per-view loop (after the qsort cleanup)
    Overlays = 4,        // 47238d: second view pass and cockpit view
    Text = 5,            // 4724ec: on-screen text
    SceneEnd = 6,        // 472574 call 4c5250: frame EndScene and the gated tails
    ViewSetupBegin = 7,  // 47224c: per-view setup begins (once per view)
    ViewSubmitBegin = 8, // 472270: setup ends, layer loop begins
    ViewSubmitEnd = 9,   // 4722c8: layer loop ends
    Count = 10,
    CoreCount = 7 // ordered once-per-frame boundaries; the rest accumulate per view
};
constexpr unsigned kSiteCount = Count;
// clang-format off
constexpr engine_patch::SiteSpec kSites[Count] = {
    {"frame_phase_prologue",0x00471f6c,{0xe8,0x4f,0x30,0x08,0x00},5,0,1},
    {"frame_phase_scene_update",0x00472044,{0xe8,0x77,0xf4,0xff,0xff},5,0,1},
    {"frame_phase_begin_scene",0x004720b5,{0xa1,0x3c,0x8b,0x60,0x00},5,0,0},
    {"frame_phase_views",0x00472186,{0x33,0xdb,0x83,0xc4,0x10},5,0,0},
    {"frame_phase_overlays",0x0047238d,{0x33,0xdb,0x39,0x5c,0x24,0x18},6,0,0},
    {"frame_phase_text",0x004724ec,{0xa1,0x18,0x85,0x60,0x00},5,0,0},
    {"frame_phase_scene_end",0x00472574,{0xe8,0xd7,0x2c,0x05,0x00},5,0,1},
    {"frame_phase_view_setup_begin",0x0047224c,{0x56,0xe8,0x4e,0x70,0x01,0x00},6,0,2},
    {"frame_phase_view_submit_begin",0x00472270,{0x8b,0x46,0x1c,0x8b,0x68,0x4c},6,0,0},
    {"frame_phase_view_submit_end",0x004722c8,{0x8b,0x15,0x18,0x85,0x60,0x00},6,0,0},
};
// clang-format on
static_assert(sizeof(kSites) / sizeof(kSites[0]) == Count, "All frame stamps are one group");
}
