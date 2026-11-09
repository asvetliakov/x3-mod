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
#if X3M_EDITION_FL
#include "../engine/fl/weapon_effects_sites_fl.h"
#include "weapon_effects_projectile.h"
#endif

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
#if !X3M_EDITION_FL
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
// impact (handler 1: `xor eax,eax; ret`, the caller pops its three arguments) or forward with every register unchanged
// (handler 0: the tail jump).
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
    cmp eax, 1
    pop edx
    pop ecx
    pop eax
    je .Lx3m_weapon_effects_death_area_skip
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
    cmp eax, 1
    pop edx
    pop ecx
    pop eax
    je .Lx3m_weapon_effects_death_other_skip
    popfd
    jmp dword ptr [_x3m_weapon_effects_continue_death]
.Lx3m_weapon_effects_death_other_skip:
    popfd
    xor eax, eax
    ret
    .att_syntax
)");
#else
// FL edition (X3FL.exe, src/engine/fl/weapon_effects_sites_fl.h; docs/reverse-engineering/fl/weapon-effect-spawn.md
// sections 2 and 4). Each FL entry shim saves EFLAGS, EAX, ECX and EDX like the AP stubs and passes the stack at stub
// entry and the FL register inputs to its handler, which rebuilds the AP stub contract in a local array (only the slots
// the AP handler reads) and runs the same core handler. The forward path pops every register and tail-jumps to the
// callee with ECX, EDX, the flags and the stack exactly as the call left them (the FL callees read ECX/EDX); the skip
// path returns `xor eax,eax; ret` (plain: every FL callee has a plain ret and the caller pops its own arguments; EAX,
// ECX, EDX and EFLAGS are dead after the impact, muzzle, flight and death calls). The continues are base + the callee
// RVAs, published by install_base before any redirect goes live. Nothing is called: LastError stays the engine's.
namespace S = x3m::fl::weapon_effects_fl;
namespace core = x3m::weapon_effects::core;
extern "C" {
std::uintptr_t x3m_weapon_effects_continue_impact = 0;
std::uintptr_t x3m_weapon_effects_continue_tap = 0;
std::uintptr_t x3m_weapon_effects_continue_bolt_tap = 0;
std::uintptr_t x3m_weapon_effects_continue_spawner = 0; // the muzzle's and the flight's callee (base + 0x0150e0)
std::uintptr_t x3m_weapon_effects_continue_death = 0;
std::uintptr_t x3m_weapon_effects_continue_batch = 0; // the bullet batch submit (base + 0x0e0b40)
// The projectile stage's pool and enabled-body set (weapon_effects_projectile.h): the object tap registers, the batch
// stub reads skip_on / bits and bumps skipped at their fixed offsets, the stage and Present own the rest.
x3m::weapon_effects::core::ProjectilePool x3m_projectile_pool;
// Impact 0x053390 -> 0x014440: ECX victim, EDX source, [esp+0x18] T, [esp+0x1c] the remaining shield-stage damage, EBP
// the running total, ESI the absorbed path's 1. AP slots: [1] victim, [2] source, [7] total, [9] absorbed.
int __cdecl x3m_weapon_event_fl(const std::uint32_t* entry, std::uint32_t ecx, std::uint32_t edx, std::uint32_t ebp,
                                std::uint32_t esi) {
    std::uint32_t ap[10];
    ap[1] = ecx;
    ap[2] = edx;
    ap[7] = ebp;
    ap[9] = ecx ? core::fl_absorbed(esi, ebp, entry[S::impact_total_slot], entry[S::impact_remaining_slot],
                                    core::rd32(ecx + core::layout::obj_flags))
                : 0u;
    return core::impact(ap, state_);
}
// Taps 0x014583 / 0x014650 -> 0x015270: [esp+8] hit, [esp+0xc] &pos. AP slots: [3] hit, [4] &pos.
void __cdecl x3m_weapon_tap_fl(const std::uint32_t* entry) {
    std::uint32_t ap[5];
    ap[3] = entry[S::tap_hit_slot];
    ap[4] = entry[S::tap_pos_slot];
    core::tap(ap, state_);
}
// Object tap 0x0645bd -> 0x014d90: ECX the object. AP slot: [1] obj.
void __cdecl x3m_weapon_bolt_tap_fl(std::uint32_t ecx) {
    std::uint32_t ap[2];
    ap[1] = ecx;
    core::bolt_tap(ap, bolt_state_);
    core::projectile_tap(ecx, x3m_projectile_pool); // the projectile stage's registration (stage modes)
}
// Muzzle 0x04f639 -> 0x0150e0: EDX the gun slot, [esp+8] ship, ESI the TBullets record offset. The projectile stage's
// muzzle (weapon_effects_projectile.h projectile_muzzle, docs/architecture/muzzle-flash-look.md (superseded) section
// 4): skipped for a shot of an enabled body while the batch latch is on (the stage draws its flash), forwarded
// otherwise. The AP bolt muzzle (bolt_muzzle, its muzzle_owned / suppress_muzzle latch) is not used on FL.
int __cdecl x3m_weapon_muzzle_fl(const std::uint32_t* entry, std::uint32_t edx, std::uint32_t esi) {
    return core::projectile_muzzle(entry[S::spawner_object_slot], edx, esi, x3m_projectile_pool);
}
// Flight 0x014eaa -> 0x0150e0: [esp+8] the bullet. AP slot: [4] obj.
int __cdecl x3m_weapon_flight_fl(const std::uint32_t* entry) {
    std::uint32_t ap[5];
    ap[4] = entry[S::spawner_object_slot];
    return core::bolt_flight(ap, bolt_state_);
}
// Death 0x0543b3 -> 0x013ed0: ECX obj, EDX killer, [esp+4] arg4, EBP the kill routine's frame. AP: [1] obj, [2] killer,
// [3] arg4; the site tag from AP's own branch (core::fl_death_site).
int __cdecl x3m_weapon_death_fl(const std::uint32_t* entry, std::uint32_t ecx, std::uint32_t edx, std::uint32_t ebp) {
    std::uint32_t ap[4];
    ap[1] = ecx;
    ap[2] = edx;
    ap[3] = entry[S::death_arg4_slot];
    return core::death(ap, ebp, core::fl_death_site(ecx, state_.globals), state_);
}
}
// After pushfd and the three saves the stack at stub entry is [esp+0x10]; each extra push before the lea adds 4.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_weapon_effects_stub_impact_fl
_x3m_weapon_effects_stub_impact_fl:
    pushfd
    push eax
    push ecx
    push edx
    cld
    push esi
    push ebp
    push edx
    push ecx
    lea eax, [esp+0x20]
    push eax
    call _x3m_weapon_event_fl
    add esp, 0x14
    test eax, eax
    pop edx
    pop ecx
    pop eax
    jnz .Lx3m_weapon_effects_impact_fl_skip
    popfd
    jmp dword ptr [_x3m_weapon_effects_continue_impact]
.Lx3m_weapon_effects_impact_fl_skip:
    popfd
    xor eax, eax
    ret
    .p2align 4
    .globl _x3m_weapon_effects_stub_tap_fl
_x3m_weapon_effects_stub_tap_fl:
    pushfd
    push eax
    push ecx
    push edx
    cld
    lea eax, [esp+0x10]
    push eax
    call _x3m_weapon_tap_fl
    add esp, 4
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_weapon_effects_continue_tap]
    .p2align 4
    .globl _x3m_weapon_effects_stub_bolt_tap_fl
_x3m_weapon_effects_stub_bolt_tap_fl:
    pushfd
    push eax
    push ecx
    push edx
    cld
    push ecx
    call _x3m_weapon_bolt_tap_fl
    add esp, 4
    pop edx
    pop ecx
    pop eax
    popfd
    jmp dword ptr [_x3m_weapon_effects_continue_bolt_tap]
    .p2align 4
    .globl _x3m_weapon_effects_stub_muzzle_fl
_x3m_weapon_effects_stub_muzzle_fl:
    pushfd
    push eax
    push ecx
    push edx
    cld
    push esi
    push edx
    lea eax, [esp+0x18]
    push eax
    call _x3m_weapon_muzzle_fl
    add esp, 0xc
    test eax, eax
    pop edx
    pop ecx
    pop eax
    jnz .Lx3m_weapon_effects_muzzle_fl_skip
    popfd
    jmp dword ptr [_x3m_weapon_effects_continue_spawner]
.Lx3m_weapon_effects_muzzle_fl_skip:
    popfd
    xor eax, eax
    ret
    .p2align 4
    .globl _x3m_weapon_effects_stub_flight_fl
_x3m_weapon_effects_stub_flight_fl:
    pushfd
    push eax
    push ecx
    push edx
    cld
    lea eax, [esp+0x10]
    push eax
    call _x3m_weapon_flight_fl
    add esp, 4
    test eax, eax
    pop edx
    pop ecx
    pop eax
    jnz .Lx3m_weapon_effects_flight_fl_skip
    popfd
    jmp dword ptr [_x3m_weapon_effects_continue_spawner]
.Lx3m_weapon_effects_flight_fl_skip:
    popfd
    xor eax, eax
    ret
    .p2align 4
    .globl _x3m_weapon_effects_stub_death_fl
_x3m_weapon_effects_stub_death_fl:
    pushfd
    push eax
    push ecx
    push edx
    cld
    push ebp
    push edx
    push ecx
    lea eax, [esp+0x1c]
    push eax
    call _x3m_weapon_death_fl
    add esp, 0x10
    cmp eax, 1
    pop edx
    pop ecx
    pop eax
    je .Lx3m_weapon_effects_death_fl_skip
    popfd
    jmp dword ptr [_x3m_weapon_effects_continue_death]
.Lx3m_weapon_effects_death_fl_skip:
    popfd
    xor eax, eax
    ret
    .p2align 4
    .globl _x3m_weapon_effects_stub_batch_fl
_x3m_weapon_effects_stub_batch_fl:
    pushfd
    cmp dword ptr [_x3m_projectile_pool], 0
    je .Lx3m_weapon_effects_batch_fl_forward
    push eax
    mov eax, dword ptr [edx+0x0c]
    cmp eax, 0xffff
    ja .Lx3m_weapon_effects_batch_fl_forward_eax
    bt dword ptr [_x3m_projectile_pool+8], eax
    pop eax
    jc .Lx3m_weapon_effects_batch_fl_skip
.Lx3m_weapon_effects_batch_fl_forward:
    popfd
    jmp dword ptr [_x3m_weapon_effects_continue_batch]
.Lx3m_weapon_effects_batch_fl_forward_eax:
    pop eax
    popfd
    jmp dword ptr [_x3m_weapon_effects_continue_batch]
.Lx3m_weapon_effects_batch_fl_skip:
    add dword ptr [_x3m_projectile_pool+4], 1
    popfd
    mov eax, 1
    ret
    .att_syntax
)");
// The bullet re-shade's scope (mode_reshade; bullet-draw-batch.md section 6): the same call 0x097af8 -> 0x0e0b40, always
// forwarded, as a call so the submit's extent is known. x3m_bullet_scope is raised before and lowered after the
// submit; the draw hook binds the re-shade program for a draw inside it whose bound pixel shader is the game's
// INSTANCE_BULLETS one. The stub re-pushes the two stack words (material, view) under a 4-byte pad, so the callee sees
// its arguments where the original call put them and the stack at the original call's alignment modulo 16 (the
// return addresses differ: the callee returns here, then here to the caller). ECX, EDX (the callee's inputs) and
// EAX (its result) pass untouched; EBX, ESI, EDI and EBP are the callee's to preserve; EFLAGS reaches the callee as the
// caller left it and the caller gets the callee's (the counters are updated under pushfd/popfd and the pad moves by
// lea; the flags are dead after the call anyway: the caller's add esp,8 writes them). No handler and no call besides
// the forwarded callee, no XMM, no string instruction: LastError and DF are the engine's. An exception unwinding
// through the submit would leave the counter raised: weapon_effects::present zeroes it every frame while
// x3m_bullet_reshade_armed, and a raised counter only admits draws that already bind the game's bullet program.
// x3m_bullet_scope_calls counts the submits for the frame row. The draw hook keys on x3m_bullet_scope and on
// MotionOutput's bullet_reshade_requested_ (taken from x3m_bullet_reshade_armed at configure_weapon_effects).
extern "C" {
volatile std::uint32_t x3m_bullet_scope = 0;
volatile std::uint32_t x3m_bullet_scope_calls = 0;
volatile std::uint32_t x3m_bullet_reshade_armed = 0; // install_base: the scope stub is live (configure_weapon_effects,
                                                      // Present's counter reset and frame row)
}
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_weapon_effects_stub_batch_scope_fl
_x3m_weapon_effects_stub_batch_scope_fl:
    pushfd
    add dword ptr [_x3m_bullet_scope], 1
    add dword ptr [_x3m_bullet_scope_calls], 1
    popfd
    lea esp, [esp-4]
    push dword ptr [esp+12]
    push dword ptr [esp+12]
    call dword ptr [_x3m_weapon_effects_continue_batch]
    lea esp, [esp+12]
    pushfd
    sub dword ptr [_x3m_bullet_scope], 1
    popfd
    ret
    .att_syntax
)");
// The batch submit 0x097af8 -> 0x0e0b40 (bullet-draw-batch.md section 5, Q2): ECX scene, EDX batch, [esp+4] material,
// [esp+8] view. While Present latched skip_on (the stage drew the previous frame) and the batch's body id [EDX+0x0c] is
// in the enabled set, the call is skipped with the callee's own plain ret and EAX = 1 (the caller pops its two words
// and never reads EAX, ECX, EDX or EFLAGS); otherwise the stub forwards with EAX, ECX, EDX, EFLAGS and the stack as the
// call left them. No call, no XMM, no string instruction: LastError, DF and every other register are the engine's.
static_assert(S::batch_body == 0x0c && sizeof(core::ProjectilePool::skip_on) == 4, "the batch stub's operands");
static_assert(S::impact_total_slot == 6 && S::impact_remaining_slot == 7 && S::tap_hit_slot == 2 && S::tap_pos_slot == 3 &&
                  S::spawner_object_slot == 2 && S::spawner_pos_slot == 6 && S::spawner_orient_slot == 8 &&
                  S::death_arg4_slot == 1,
              "the FL shims' slots follow the FL site contract");
#endif
