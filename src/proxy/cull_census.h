#pragma once
#include <cstdint>

// Cull/LOD census (X3M_CULL_CENSUS=1; unset = vanilla, nothing patched): two
// read-only trampolines on the engine's per-node cull and LOD pass 0x0047cfe0
// (docs/reverse-engineering/lod-selection.md, "Cull census sites"). The
// measure site 0x0047d258 records, per node visited on a capture frame, the
// node, its model id, the LOD metric s = r*640/D, the small-object measure
// r*W/D, D, the radius, the two per-node thresholds and the effective limit;
// the exit site 0x0047d528 completes the entry with the renderable bit and the
// LOD index the pass selected. Bounded ring of 8,192 entries, no allocation
// and no logging per node: outside capture frames each site costs the
// trampoline round trip (site jmp, dispatcher jmp, a byte compare and
// branch, jmp back and the displaced tail; measured 0.234 -> 0.244 us per
// 12-node pass), and a compare-and-store through an integer-only cdecl
// handler inside them. The rows are emitted at Present
// (`cull_census_frame` with `overflow=`, then one `cull_census` row per
// entry). Installed on the backend-load path inside the engine_patch install
// window after the exact-executable and window-byte checks, with this module
// pinned; a failed second claim rolls the first back.
namespace x3m::cull_census {
bool initialize();  // backend-load path only; logs one cull_census line when the variable is set
bool shutdown();    // restores both sites (dynamic-unload detach only); true when nothing is installed
// Verifies the two windows around the given sites (the fixture passes a
// synthetic copy of the engine bytes), claims them, emits the stubs and
// chains them in; the production initialize() passes the engine sites.
bool install_at(std::uintptr_t measure_site, std::uintptr_t exit_site);
const char* state();
std::uintptr_t measure_stub_address();
std::uintptr_t exit_stub_address();
// Frame telemetry, only while the patch is live. begin_frame(capture) arms the
// stubs for a capture frame (and clears the ring); present() emits the frame
// row and the entry rows when the ended frame was captured, then disarms.
void begin_frame(bool capture);
void present(unsigned long long device, unsigned long long frame, bool captured);
struct Stats { std::uint32_t entries, overflow, unmeasured, exited; bool armed; };
Stats stats();
// The small-parts stub's threshold for the current frame (cull_small_parts,
// 0 = none): rows whose `s` is below it and that the engine's own limit did
// not cull are reported as `culled_small`. One plain store per frame.
void note_small_threshold(std::int32_t threshold);
}
// The stubs' cdecl targets: integer only, no Win32 call, LastError untouched by
// construction; EBX/ESI/EDI/EBP preserved by the ABI, the stubs save EAX/ECX/EDX.
extern "C" void x3m_cull_census_measure(std::uint32_t node, std::int32_t measure, std::int32_t s, std::int32_t d, std::uint32_t view);
extern "C" void x3m_cull_census_exit(std::uint32_t node);
// The byte both stubs test: non-zero only while a capture frame is armed.
extern "C" volatile unsigned char x3m_cull_census_enabled;
