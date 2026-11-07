// The game-thread half of the weapon effects (weapon_effects_patch.h): the two stubs, the handler entry points and the
// state they write. Integer only, compiled with -fno-exceptions -mno-sse -mno-mmx -mfpmath=387 (CMakeLists.txt) and
// holding no floating point at all: no x87, MXCSR or XMM is touched, nothing is called, nothing is allocated, and the
// object has no undefined symbol (the hook fixture's link check: no CRT and no Win32 import). The stubs save EFLAGS,
// EAX, ECX and EDX and clear DF around the handler; LastError is untouched because nothing runs that could set it.
#include "weapon_effects_patch.h"
#include "weapon_effects_bolt.h"
#include "weapon_effects_bolt_sites.h"
#include "weapon_effects_death_sites.h"
#include "weapon_effects_sites.h"

static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
x3m::weapon_effects::core::HookState state_;
x3m::weapon_effects::core::BoltHookState bolt_state_; // step 4: the bolt lists and the muzzle records (about 85 KB)
}
namespace x3m::weapon_effects {
core::HookState& hook_state() {
    return state_;
}
core::BoltHookState& bolt_hook_state() {
    return bolt_state_;
}
}
extern "C" {
std::uintptr_t x3m_weapon_effects_continue_impact = x3m::weapon_effects::sites::impact_target_va;
std::uintptr_t x3m_weapon_effects_continue_tap = x3m::weapon_effects::sites::tap_target_va;
std::uintptr_t x3m_weapon_effects_continue_bolt_tap = x3m::weapon_effects::bolt_sites::tap_target_va;
std::uintptr_t x3m_weapon_effects_continue_muzzle = x3m::weapon_effects::bolt_sites::muzzle_target_va;
std::uintptr_t x3m_weapon_effects_continue_flight = x3m::weapon_effects::bolt_sites::flight_target_va;
std::uintptr_t x3m_weapon_effects_continue_death = x3m::weapon_effects::death_sites::target_va;
int __cdecl x3m_weapon_event(const std::uint32_t* entry) {
    return x3m::weapon_effects::core::impact(entry, state_);
}
void __cdecl x3m_weapon_tap(const std::uint32_t* entry) {
    x3m::weapon_effects::core::tap(entry, state_);
}
// Step 4 (weapon_effects_bolt.h): the per-bullet tap, the muzzle and the flight effect.
void __cdecl x3m_weapon_bolt_tap(const std::uint32_t* entry) {
    x3m::weapon_effects::core::bolt_tap(entry, bolt_state_);
}
int __cdecl x3m_weapon_muzzle(const std::uint32_t* entry, std::uint32_t eax, std::uint32_t edx) {
    return x3m::weapon_effects::core::bolt_muzzle(entry, eax, edx, bolt_state_);
}
int __cdecl x3m_weapon_flight(const std::uint32_t* entry) {
    return x3m::weapon_effects::core::bolt_flight(entry, bolt_state_);
}
// Step 3 (weapon_effects_death_sites.h): the two death sites, each passing the caller's EBP (the reason word).
int __cdecl x3m_weapon_death_area(const std::uint32_t* entry, std::uint32_t ebp) {
    return x3m::weapon_effects::core::death(entry, ebp, 0, state_);
}
int __cdecl x3m_weapon_death_other(const std::uint32_t* entry, std::uint32_t ebp) {
    return x3m::weapon_effects::core::death(entry, ebp, 1, state_);
}
}
// In: the engine's call at 0x00449d85 (impact) or 0x00413da8 / 0x00413e79 (taps); [esp] the return address. The saved
// block is EDX ECX EAX EFLAGS above the return address, so `lea eax,[esp+0x10]` is the stack at stub entry.
// Step 4: the bolt tap 0x0045ac7d has the tap's shape (always forwards), the flight site 0x004146c0 the impact's (skip:
// `xor eax,eax; ret`, the caller pops its ten arguments); the muzzle 0x00446268 passes EAX (the bullet index) and EDX
// (&orientation) to its handler as two more arguments (`lea eax,[esp+0x18]`: the saved block and the two pushes above
// the return address) and forwards both unchanged, or skips like the impact.
// Step 3: the death sites 0x0044ac64 / 0x0044acb2 pass EBP (the kill routine's frame: the reason word) as the handler's
// second argument (`lea eax,[esp+0x14]`: the saved block and the push above the return address) and skip like the
// impact (`xor eax,eax; ret`: the caller pops its three arguments) or forward with every register unchanged.
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
    .p2align 4
    .globl _x3m_weapon_effects_stub_bolt_tap
_x3m_weapon_effects_stub_bolt_tap:
    pushfd
    push eax
    push ecx
    push edx
    cld
    lea eax, [esp+0x10]
    push eax
    call _x3m_weapon_bolt_tap
    add esp, 4
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_weapon_effects_continue_bolt_tap]
    .p2align 4
    .globl _x3m_weapon_effects_stub_muzzle
_x3m_weapon_effects_stub_muzzle:
    pushfd
    push eax
    push ecx
    push edx
    cld
    push edx
    push eax
    lea eax, [esp+0x18]
    push eax
    call _x3m_weapon_muzzle
    add esp, 0xc
    test eax, eax
    pop edx
    pop ecx
    pop eax
    jnz .Lx3m_weapon_effects_muzzle_skip
    popfd
    jmp dword ptr [_x3m_weapon_effects_continue_muzzle]
.Lx3m_weapon_effects_muzzle_skip:
    popfd
    xor eax, eax
    ret
    .p2align 4
    .globl _x3m_weapon_effects_stub_flight
_x3m_weapon_effects_stub_flight:
    pushfd
    push eax
    push ecx
    push edx
    cld
    lea eax, [esp+0x10]
    push eax
    call _x3m_weapon_flight
    add esp, 4
    test eax, eax
    pop edx
    pop ecx
    pop eax
    jnz .Lx3m_weapon_effects_flight_skip
    popfd
    jmp dword ptr [_x3m_weapon_effects_continue_flight]
.Lx3m_weapon_effects_flight_skip:
    popfd
    xor eax, eax
    ret
    .p2align 4
    .globl _x3m_weapon_effects_stub_death_area
_x3m_weapon_effects_stub_death_area:
    pushfd
    push eax
    push ecx
    push edx
    cld
    push ebp
    lea eax, [esp+0x14]
    push eax
    call _x3m_weapon_death_area
    add esp, 8
    test eax, eax
    pop edx
    pop ecx
    pop eax
    jnz .Lx3m_weapon_effects_death_area_skip
    popfd
    jmp dword ptr [_x3m_weapon_effects_continue_death]
.Lx3m_weapon_effects_death_area_skip:
    popfd
    xor eax, eax
    ret
    .p2align 4
    .globl _x3m_weapon_effects_stub_death_other
_x3m_weapon_effects_stub_death_other:
    pushfd
    push eax
    push ecx
    push edx
    cld
    push ebp
    lea eax, [esp+0x14]
    push eax
    call _x3m_weapon_death_other
    add esp, 8
    test eax, eax
    pop edx
    pop ecx
    pop eax
    jnz .Lx3m_weapon_effects_death_other_skip
    popfd
    jmp dword ptr [_x3m_weapon_effects_continue_death]
.Lx3m_weapon_effects_death_other_skip:
    popfd
    xor eax, eax
    ret
    .att_syntax
)");
