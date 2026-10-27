#pragma once
#include <cstdint>

// Oversized-disc fix (X3M_CIRCLE_FILL_FIX / ini circle_fill_fix = on|off, unset = on; the launcher sends on;
// docs/architecture/text-density.md, "Oversized disc";
// docs/reverse-engineering/sector-map-scanner-range.md section 5): the engine's filled-ellipse rasteriser 0x004ef620
// (KC B3D_TexCircleFill, the sector map's scanner-coverage discs) does its midpoint arithmetic in 32 bits and draws a
// dented, then collapsed polygon from a radius of 826 texels; text density d = 2 doubles the texel radius of the same
// disc. One entry claim through engine_patch on the backend-load path (circle_fill_sites.h): radii up to 813 texels run
// the stock routine unchanged, larger radii run a 64-bit port through the engine's own lock and span leaves. The
// claim is independent of the text-density rows and of d (the defect is stock): with on it is installed whenever the
// executable and every compared region match, off leaves the engine's bytes; one `circle_fill site=` row with status
// patched, off or refused; a late call is refused (late_claim).
// Device Reset does not touch it; nothing runs per frame.
namespace x3m::circle_fill {
bool initialize(); // backend-load path only; logs one circle_fill site= row
bool shutdown();   // dynamic-unload detach only; true when nothing stays registered
const char* state();
bool patched(); // registered (live, or a failed rollback still to be restored)
}
extern "C" {
void x3m_circle_fill_thunk();
extern std::uintptr_t x3m_circle_fill_continue; // the engine_patch tail: the displaced pair, then jmp 0x004ef627
// The wide path, entered from the thunk with every register and EFLAGS saved (frame = the pushad/pushfd frame):
// 1 = drawn (the thunk returns to the caller), 0 = the stock routine runs.
std::uint32_t __cdecl x3m_circle_fill_wide(std::uint32_t* frame);
// The register-convention shim for the pixel-class function (ECX = surface, EAX = 0): its AX, sign-extended.
std::int32_t __cdecl x3m_circle_fill_call_class(std::uintptr_t fn, void* surface);
}
