#pragma once
#include "collide_narrow_census_core.h"
#include <cstdint>

// Sector-collide narrow-phase census (X3M_COLLIDE_NARROW_CENSUS=1; unset =
// vanilla, nothing patched; independent of X3M_COLLIDE_BOX_CULL, whose sites
// are disjoint): a one-flight diagnostic of the accepted pairs of the
// all-pairs loop, docs/reverse-engineering/sector-collide.md 11.5 and 11.7.
//   site 5  0x0045d665  `call 0x0048ac80` redirected to a stub that brackets the
//           narrow phase of one accepted pair: both objects, their class /
//           subtype / radius / flags, the integer positions, the model id, a
//           hash of the node transform, the return value, the BVH node-pair
//           visits and mesh-pair tests attributed to the pair, and the QPC
//           ticks across the call, into a fixed ring of 256 entries per frame;
//   site 6  0x0048a9a5  `call 0x0047f1b0` redirected to `inc; jmp` (mesh-pair tests);
//   site 7  0x004e2530  entry trampoline `inc; mov eax,[0x0060854c]; jmp +5`
//           (BVH node-pair visits; recursive path: no clock, no call).
// present() emits one `collide_narrow` line per 300 frames and, on capture
// frames, one `collide_narrow_pair` row per ring entry ordered by visits.
//
// Threads. The stubs run on the thread that executes the engine's main loop,
// and that loop also issues Present: loop_phases accumulates its `collide`
// interval (the stamp around `call 0x0045d250`) only on the Present thread
// and measures it non-zero in every flight, so they are one thread. Nothing
// relies on it: the asm counters are monotonic, written by one thread and only read at
// Present (deltas of aligned 32-bit loads); the ring and the frame totals are
// guarded by a try-lock that neither side waits on (a contended post handler
// counts a dropped pair, a contended Present defers its frame), and the line
// reports same_thread so a cross-thread session is visible.
namespace x3m::collide_narrow_census {
struct Addresses { std::uintptr_t n5_site, n5_target, n6_site, n6_target, n7_site, n7_hits, n7_mode; };
bool initialize();  // backend-load path only; logs one collide_narrow_census line when the variable is set
bool shutdown();    // restores the three sites (dynamic-unload detach only); true when nothing is installed
// Verifies the windows around the three sites (offsets relative to each site,
// so the fixture passes layout-preserving synthetic copies), claims 7, 6, 5 in
// that order and rolls the earlier ones back when a later claim fails.
bool install_at(const Addresses& addresses);
const char* state();
std::uintptr_t stub_address(unsigned site);   // 5, 6 or 7; 0 when not installed
// False when nothing is installed or the frame was deferred (a post handler on another thread held the lock).
bool present(unsigned long long device, unsigned long long frame, bool captured);
// The frame most recently taken by present() (fixture and tests).
struct FrameView { const core::Entry* entries; unsigned count; std::uint32_t accepted, overflow, mesh_pairs, node_pairs; std::uint64_t ticks; core::MemoSummary memo; };
FrameView last_frame();
std::uint32_t dropped_total();   // pairs a contended post handler could not record (cross-thread Present only)
}
extern "C" {
// Monotonic, written by the stubs only: [0] mesh-pair tests, [1] node-pair visits, [2] nested passes, [3] foreign callers.
extern volatile std::uint32_t x3m_collide_narrow_counters[4];
extern volatile unsigned char x3m_collide_narrow_busy;
void __cdecl x3m_collide_narrow_pre(const unsigned char* object_a, const unsigned char* object_b);
void __cdecl x3m_collide_narrow_post(std::int32_t result);
}
