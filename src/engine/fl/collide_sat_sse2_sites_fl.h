#pragma once
#include "collide_windows_fl.h"
#include <cstdint>

// FL edition of the collision SAT sites (the AP twin is src/proxy/collide_sat_sse2_core.h). Verified facts:
// docs/reverse-engineering/fl/collision-sat.md (static contract, "Port decision (2026-10-29)" and "Port (2026-11-03)";
// verifier verification/probe/verify_collide_sat_fl.py). X3FL.exe's OBB separating-axis test already runs in scalar
// SSE (81 MULSS, 60 ADDSS, 9 SUBSS, 24 ANDPS, 15 COMISS, no x87/MMX instruction, no MXCSR load/store), the conversion
// AP's replacement makes; the AP thunk is neither ABI-compatible (FL: ECX = R, EDX = T, stack A/B extents; AP: ESI/EDI)
// nor numerically identical (AP's double core and its 2^-20 slack). So FL patches nothing: the module confirms the
// native body and its sole call once at load and reports it. Every value is an RVA added to x3m::fl::module_base().
//
// 11a73e  f3 0f 11 44 24 20              MOVSS [ESP+0x20],XMM0       <- call window (18 bytes, no fixup)
// 11a744  e8 87 1f 00 00                 CALL  0x11c6d0               ; the sole encoded caller of the SAT
// 11a749  83 c4 08 / 85 c0 / 75 8d       ADD ESP,8; TEST EAX,EAX; JNE (nonzero = separated, the pair is dropped)
// 11c6d0  55 8b ec 83 e4 f8 ...          the SAT body (0x67b bytes, fixups at +0x11 abs mask, +0x19 epsilon)
namespace x3m::fl::collide_sat_fl {
namespace W = x3m::fl::collide_windows;
constexpr std::uint32_t sat_rva = 0x11c6d0, sat_length = 0x67b;
constexpr std::uint32_t call_window_rva = 0x11a73e, call_rva = 0x11a744, call_return_rva = 0x11a749;
constexpr std::uint32_t descent_rva = 0x11a6c0;
constexpr unsigned call_offset = 6;
static_assert(call_window_rva + call_offset == call_rva && call_rva + 5 == call_return_rva, "the call in its window");
static_assert(W::sat_windows[0].rva == sat_rva && W::sat_windows[0].length == sat_length, "the body window");
static_assert(W::sat_windows[1].rva == call_window_rva, "the call window");
}
