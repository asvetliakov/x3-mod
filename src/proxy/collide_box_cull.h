#pragma once
#include <cstdint>

// Sector-collide bounding-box early-out (X3M_COLLIDE_BOX_CULL=1; unset =
// vanilla, nothing patched): two engine_patch trampolines on the square-root
// pair tests of the sector collision pass 0x0045d250 (P1, the all-pairs loop,
// site 0x0045d58e) and its swept query 0x0045cab0 (P2, site 0x0045cc7c),
// docs/reverse-engineering/sector-collide.md sections 6 and 10. Each stub
// computes the integer bounding box the engine already uses for class-0/7
// pairs and, when the box rejects, jumps to the engine's own continue label
// (0x0045df90 / 0x0045ce07) where the engine's reject path would have arrived
// with nothing written; otherwise it re-executes the displaced instructions.
// A rejected pair is one the engine's `dist > R` compare would have rejected
// (margin proof in the note; host model and X3 CPU fixture). Class-7 pairs,
// whose distance the engine reuses, always take the engine path.
//
// Per frame the stubs count pairs entering each site and pairs the box
// rejected (four `inc dword` slots, no logging per pair); present() reads and
// zeroes them once per Present and emits one `collide_census` line per
// 300-frame window (p50/max/sum of each) and one `collide_census_frame` line
// per capture frame. Installed on the backend-load path inside the
// engine_patch install window after the exact-executable, window-byte and
// helper checks, with this module pinned; a failed second claim rolls the
// first back.
namespace x3m::collide_box_cull {
bool initialize();  // backend-load path only; logs one collide_box_cull line when the variable is set
bool shutdown();    // restores both sites (dynamic-unload detach only); true when nothing is installed
// Verifies the windows around the two sites (offsets relative to each site,
// so the fixture passes a layout-preserving synthetic copy), claims them,
// emits the stubs (with or without the counters) and chains them in.
bool install_at(std::uintptr_t p1_site, std::uintptr_t p2_site, bool counters);
const char* state();
std::uintptr_t p1_stub_address();
std::uintptr_t p2_stub_address();
// Arms or disarms both stubs (the enabled byte); armed at install.
void set_enabled(bool enabled);
bool enabled();
// Reads and zeroes the four counters (p1 entered, p1 rejected, p2 entered, p2 rejected).
void take_counters(std::uint32_t out[4]);
// Present-time telemetry: folds the frame's counters into the window, emits
// the frame line on a capture frame and the window line when it closes.
void present(unsigned long long device, unsigned long long frame, bool captured);
}
// The byte both stubs test and the four counter slots they increment.
extern "C" volatile unsigned char x3m_collide_box_cull_enabled;
extern "C" volatile std::uint32_t x3m_collide_box_cull_counters[4];
