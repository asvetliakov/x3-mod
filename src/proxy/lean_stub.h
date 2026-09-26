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
// The context variant (submit_phases.cpp), 128 bytes: pushfd; pushad; cld;
// sub esp,0x80; 8 movups saves; lea eax,[esp+0x80]; push eax; push index;
// call handler; add esp,8; 8 movups loads; add esp,0x80; popad; popfd;
// jmp [next]. The handler is `extern "C" void __cdecl handler(unsigned index,
// const std::uint32_t* saved)`; saved[0..8] = EDI, ESI, EBP, ESP (after the
// pushfd), EBX, EDX, ECX, EAX, EFLAGS as the site held them, read-only to the
// handler by contract. Same preconditions as `emit`: no x87 save, so the
// handler executes no x87 opcode and the x87 stack is never touched.
constexpr unsigned context_reserve = 176, context_emitted = 128;
enum SavedRegister : unsigned {
    SavedEdi = 0,
    SavedEsi,
    SavedEbp,
    SavedEsp,
    SavedEbx,
    SavedEdx,
    SavedEcx,
    SavedEax,
    SavedEflags
};
void* emit_context(const void* handler, unsigned index, void*** next_out);
}
