// The game-thread half of the weapon effects (weapon_effects_patch.h): the two stubs, the handler entry points and the
// state they write. Integer only, compiled with -fno-exceptions -mno-sse -mno-mmx -mfpmath=387 (CMakeLists.txt) and
// holding no floating point at all: no x87, MXCSR or XMM is touched, nothing is called, nothing is allocated, and the
// object has no undefined symbol (the hook fixture's link check: no CRT and no Win32 import). The stubs save EFLAGS,
// EAX, ECX and EDX and clear DF around the handler; LastError is untouched because nothing runs that could set it.
#include "weapon_effects_patch.h"
#include "weapon_effects_sites.h"

static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
x3m::weapon_effects::core::HookState state_;
}
namespace x3m::weapon_effects {
core::HookState& hook_state() {
    return state_;
}
}
extern "C" {
std::uintptr_t x3m_weapon_effects_continue_impact = x3m::weapon_effects::sites::impact_target_va;
std::uintptr_t x3m_weapon_effects_continue_tap = x3m::weapon_effects::sites::tap_target_va;
int __cdecl x3m_weapon_event(const std::uint32_t* entry) {
    return x3m::weapon_effects::core::impact(entry, state_);
}
void __cdecl x3m_weapon_tap(const std::uint32_t* entry) {
    x3m::weapon_effects::core::tap(entry, state_);
}
}
// In: the engine's call at 0x00449d85 (impact) or 0x00413da8 / 0x00413e79 (taps); [esp] the return address. The saved
// block is EDX ECX EAX EFLAGS above the return address, so `lea eax,[esp+0x10]` is the stack at stub entry.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_weapon_effects_stub_impact
_x3m_weapon_effects_stub_impact:
    pushfd
    push eax
    push ecx
    push edx
    cld
    lea eax, [esp+0x10]
    push eax
    call _x3m_weapon_event
    add esp, 4
    test eax, eax
    pop edx
    pop ecx
    pop eax
    jnz .Lx3m_weapon_effects_skip
    popfd
    jmp dword ptr [_x3m_weapon_effects_continue_impact]
.Lx3m_weapon_effects_skip:
    popfd
    xor eax, eax
    ret
    .p2align 4
    .globl _x3m_weapon_effects_stub_tap
_x3m_weapon_effects_stub_tap:
    pushfd
    push eax
    push ecx
    push edx
    cld
    lea eax, [esp+0x10]
    push eax
    call _x3m_weapon_tap
    add esp, 4
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_weapon_effects_continue_tap]
    .att_syntax
)");
