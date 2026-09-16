#pragma once
#include <cstdint>

// The lean stamp stub shared by the high-rate accumulate-only diagnostic groups
// (pass_phases.cpp, loop_phases.cpp). 124 bytes: pushfd; push eax/ecx/edx;
// cld; sub esp,0x80; 8 movups saves; push index; call handler; add esp,4;
// 8 movups loads; add esp,0x80; pop edx/ecx/eax; popfd; jmp [next]. The
// handler is `extern "C" void __cdecl handler(unsigned index)` and keeps
// EBX/ESI/EDI/EBP itself; the displaced instructions then run in the claim
// tail at the game's exact ESP with every register and the flags restored. No
// x87 save: the handler must execute no x87 opcode (cpu_state.h,
// LightCallBoundary precondition; verification/probe/check_no_x87.py walks
// every such handler from its extern "C" symbol).
namespace x3m::lean_stub {
constexpr unsigned reserve = 160, emitted = 124;
void* emit(const void* handler, unsigned index, void*** next_out);
}
