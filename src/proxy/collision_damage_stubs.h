#pragma once
#include "collision_damage_core.h"
#include <cstddef>
#include <cstdint>

// The two contact-join stubs of X3M_COLLISION_DAMAGE=0 (docs/reverse-engineering/collision-damage.md section 4.1,
// "Implementation"), included by exactly one translation unit: the DLL's src/proxy/collision_damage.cpp, or the fixture
// verification/probe/collision_damage_fixture.cpp, which executes them against a fabricated register, stack and code
// image. Both editions' stubs are assembled into both DLLs (the other edition's is never bound or reached).
//
// Entered by the site's `jmp rel32` (ESP as at the site, so the stack slots are the engine's). Integer only: no call
// into C, no CRT, no x87, no SSE. Each stub
//   1. saves EFLAGS, EAX, ECX, EDX and records the contact for the flight row (the event count, the last and largest
//      damage values, the two classes [obj+0x48], c and v from the law's stack slots, and the piloted bit [obj+0x40] & 2
//      of either body), then restores them, so every register and EFLAGS leave this part as they entered;
//   2. zeroes the two damage values with MOV (no flag write): AP [esp+0x10] (dmgA) and EDI (dmgB); FL EDX = [esp+0x10]
//      (dmgA) and ECX (dmgB, stored to [esp+0x20] by the next engine instruction);
//   3. replays the displaced whole instructions: AP `mov eax,ebx; call 0x0043ff80` (the leaf through a bound pointer,
//      so its EAX/AL and flags are the leaf's own); FL `mov eax,[esi+0x40]; and eax,0xc0000` (flags defined as vanilla);
//   4. jumps to site + length (AP 0x0045eafb; FL module base + 0x68a60).
// Main thread only (inside the sector collision pass); the record is plain stores, read on the same thread at Present.
extern "C" {
struct X3mCollisionDamageStats {
    std::uint32_t events;       // +0x00, contacts zeroed (wraps)
    std::int32_t last_a, last_b; // +0x04, +0x08
    std::int32_t max_a, max_b;  // +0x0c, +0x10, since the last row (reset by the reader)
    std::uint32_t class_a, class_b; // +0x14, +0x18, of the last contact
    std::int32_t c, v;          // +0x1c, +0x20, the law's strength and relative speed (stale on a planet contact)
    std::uint32_t player;       // +0x24, 1 when either body has the piloted flag
};
volatile X3mCollisionDamageStats x3m_collision_damage_stats = {};
// Bound before the site is written (collision_damage::bind): AP the leaf 0x0043ff80 and the return 0x0045eafb; FL the
// return base + 0x68a60. (Not const: a const object has internal linkage, and only the asm reads these.)
std::uint32_t x3m_collision_damage_ap_leaf = 0, x3m_collision_damage_ap_resume = 0, x3m_collision_damage_fl_resume = 0;
void x3m_collision_damage_ap_stub();
void x3m_collision_damage_fl_stub();
}
static_assert(offsetof(X3mCollisionDamageStats, last_a) == 0x04 && offsetof(X3mCollisionDamageStats, max_a) == 0x0c &&
                  offsetof(X3mCollisionDamageStats, class_a) == 0x14 && offsetof(X3mCollisionDamageStats, c) == 0x1c &&
                  offsetof(X3mCollisionDamageStats, player) == 0x24 && sizeof(X3mCollisionDamageStats) == 0x28,
              "the stubs' record offsets");
namespace x3m::collision_damage {
// The stub of an edition, and its return (and AP leaf) bound to the module base.
inline std::uint32_t stub_address(const core::Edition& e) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(e.leaf_va ? &x3m_collision_damage_ap_stub
                                                                     : &x3m_collision_damage_fl_stub));
}
inline void bind(const core::Edition& e, std::uintptr_t base) {
    const std::uint32_t resume = std::uint32_t(base + core::rva(e.contact_va) + e.contact_length);
    if (e.leaf_va) {
        x3m_collision_damage_ap_leaf = std::uint32_t(base + core::rva(e.leaf_va));
        x3m_collision_damage_ap_resume = resume;
    } else {
        x3m_collision_damage_fl_resume = resume;
    }
}
}

asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_collision_damage_ap_stub
_x3m_collision_damage_ap_stub:
    pushfd
    push eax
    push ecx
    push edx
    mov eax, dword ptr [esp+0x20]
    mov dword ptr [_x3m_collision_damage_stats+0x04], eax
    cmp eax, dword ptr [_x3m_collision_damage_stats+0x0c]
    jle 1f
    mov dword ptr [_x3m_collision_damage_stats+0x0c], eax
1:
    mov dword ptr [_x3m_collision_damage_stats+0x08], edi
    cmp edi, dword ptr [_x3m_collision_damage_stats+0x10]
    jle 2f
    mov dword ptr [_x3m_collision_damage_stats+0x10], edi
2:
    movzx ecx, word ptr [ebx+0x48]
    mov dword ptr [_x3m_collision_damage_stats+0x14], ecx
    movzx ecx, word ptr [esi+0x48]
    mov dword ptr [_x3m_collision_damage_stats+0x18], ecx
    mov ecx, dword ptr [esp+0x1c]
    mov dword ptr [_x3m_collision_damage_stats+0x1c], ecx
    mov ecx, dword ptr [esp+0x2c]
    mov dword ptr [_x3m_collision_damage_stats+0x20], ecx
    mov ecx, dword ptr [ebx+0x40]
    or ecx, dword ptr [esi+0x40]
    shr ecx, 1
    and ecx, 1
    mov dword ptr [_x3m_collision_damage_stats+0x24], ecx
    add dword ptr [_x3m_collision_damage_stats+0x00], 1
    pop edx
    pop ecx
    pop eax
    popfd
    mov dword ptr [esp+0x10], 0
    mov edi, 0
    mov eax, ebx
    call dword ptr [_x3m_collision_damage_ap_leaf]
    jmp dword ptr [_x3m_collision_damage_ap_resume]

    .p2align 4
    .globl _x3m_collision_damage_fl_stub
_x3m_collision_damage_fl_stub:
    pushfd
    push eax
    push ecx
    push edx
    mov dword ptr [_x3m_collision_damage_stats+0x04], edx
    cmp edx, dword ptr [_x3m_collision_damage_stats+0x0c]
    jle 1f
    mov dword ptr [_x3m_collision_damage_stats+0x0c], edx
1:
    mov dword ptr [_x3m_collision_damage_stats+0x08], ecx
    cmp ecx, dword ptr [_x3m_collision_damage_stats+0x10]
    jle 2f
    mov dword ptr [_x3m_collision_damage_stats+0x10], ecx
2:
    movzx eax, word ptr [esi+0x48]
    mov dword ptr [_x3m_collision_damage_stats+0x14], eax
    movzx eax, word ptr [edi+0x48]
    mov dword ptr [_x3m_collision_damage_stats+0x18], eax
    mov eax, dword ptr [esp+0x48]
    mov dword ptr [_x3m_collision_damage_stats+0x1c], eax
    mov eax, dword ptr [esp+0x6c]
    mov dword ptr [_x3m_collision_damage_stats+0x20], eax
    mov eax, dword ptr [esi+0x40]
    or eax, dword ptr [edi+0x40]
    shr eax, 1
    and eax, 1
    mov dword ptr [_x3m_collision_damage_stats+0x24], eax
    add dword ptr [_x3m_collision_damage_stats+0x00], 1
    pop edx
    pop ecx
    pop eax
    popfd
    mov ecx, 0
    mov edx, 0
    mov dword ptr [esp+0x10], 0
    mov eax, dword ptr [esi+0x40]
    and eax, 0xc0000
    jmp dword ptr [_x3m_collision_damage_fl_resume]
    .att_syntax
)");
