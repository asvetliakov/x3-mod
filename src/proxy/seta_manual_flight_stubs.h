#pragma once
#include "seta_manual_flight_core.h"
#include <cstdint>

// The five engine-entered stubs of part B (docs/architecture/seta-manual-flight.md, "Stubs"), included by exactly one
// translation unit: the DLL's seta_manual_flight.cpp, or the native stub fixture
// verification/probe/seta_manual_flight_stub_fixture.cpp, which executes them against a fake engine image. The
// includer defines the helper x3m_seta_manual_flight_axis.
//
// Each stub saves EFLAGS, EAX, ECX, EDX and XMM0..7, clears DF for the C ABI, calls the helper (cdecl, axis, record,
// ship; EBX/EBP/ESI/EDI are callee-saved), restores every register it saved and only then reads the helper's decision
// byte, so the original path leaves exactly as it entered (EFLAGS too, except the arithmetic flags the `cmp` writes:
// both spans are followed by a flag write before any flag read). The helper executes no x87 instruction (it is a
// check_no_x87.py root; its row formatter runs behind call_preserved, which saves and restores the whole x87 state),
// so the x87 stack the engine left (st0 = accel at a controller span, three values at a strafe site) is untouched
// until the stub's own instructions:
//   rotation, scale:  fmul qword [step_factor] (st0 = accel * game_ms / k^2, depth unchanged, as fimul);
//                     mov ecx,[target] (round(rec+0x24 / k)); mov eax,[0x00606f34] (as the original); jmp span+14
//   strafe, scale:    fild dword [esi+0x714]; fmul qword [inv_k] (one push, as the original fild); jmp site+6
//   otherwise:        jmp [continue] (the engine_patch tail: the displaced original instructions, then span end)
// ESP leaves as it entered on every path.
extern "C" {
void __cdecl x3m_seta_manual_flight_axis(unsigned axis, std::uint32_t record, std::uint32_t ship);
// Written by the helper on every call (main thread, the input routine is not reentrant), read by the stub right after.
volatile unsigned char x3m_smf_apply = 0;
volatile double x3m_smf_step_factor = 0, x3m_smf_inv_k = 1;
volatile std::int32_t x3m_smf_target = 0;
// The chain continuation per axis (the claim tail, set at install before the stub is pushed) and the span ends.
volatile std::uint32_t x3m_smf_continue[x3m::seta_manual_flight::core::axis_count] = {};
// (Not const: a const array would have internal linkage and could be dropped, and only the asm reads it.)
std::uint32_t x3m_smf_resume[x3m::seta_manual_flight::core::axis_count] = {
    x3m::seta_manual_flight::core::sites[0].va + x3m::seta_manual_flight::core::sites[0].length,
    x3m::seta_manual_flight::core::sites[1].va + x3m::seta_manual_flight::core::sites[1].length,
    x3m::seta_manual_flight::core::sites[2].va + x3m::seta_manual_flight::core::sites[2].length,
    x3m::seta_manual_flight::core::sites[3].va + x3m::seta_manual_flight::core::sites[3].length,
    x3m::seta_manual_flight::core::sites[4].va + x3m::seta_manual_flight::core::sites[4].length};
void x3m_smf_yaw_stub();
void x3m_smf_pitch_stub();
void x3m_smf_roll_stub();
void x3m_smf_strafe_y_stub();
void x3m_smf_strafe_x_stub();
}
namespace x3m::seta_manual_flight {
// The helper's decision and its three stores, shared by the DLL's helper (which adds the CPU-state boundary, the armed
// check and the debug row) and the native fixture's.
inline core::Skip publish(unsigned axis, std::uint32_t record, core::Inputs& in, core::Out& out) noexcept {
    const core::Skip skip = core::evaluate(axis, record, in, out);
    if (skip == core::applied) {
        x3m_smf_step_factor = out.step_factor;
        x3m_smf_inv_k = out.inv_k;
        x3m_smf_target = out.target;
    }
    x3m_smf_apply = skip == core::applied ? 1 : 0;
    return skip;
}
}

asm(R"(
    .intel_syntax noprefix
    .macro X3M_SMF_CALL axis, record, ship
    pushfd
    push eax
    push ecx
    push edx
    sub esp, 0x80
    movdqu [esp+0x00], xmm0
    movdqu [esp+0x10], xmm1
    movdqu [esp+0x20], xmm2
    movdqu [esp+0x30], xmm3
    movdqu [esp+0x40], xmm4
    movdqu [esp+0x50], xmm5
    movdqu [esp+0x60], xmm6
    movdqu [esp+0x70], xmm7
    cld
    push \ship
    push \record
    push \axis
    call _x3m_seta_manual_flight_axis
    add esp, 12
    movdqu xmm0, [esp+0x00]
    movdqu xmm1, [esp+0x10]
    movdqu xmm2, [esp+0x20]
    movdqu xmm3, [esp+0x30]
    movdqu xmm4, [esp+0x40]
    movdqu xmm5, [esp+0x50]
    movdqu xmm6, [esp+0x60]
    movdqu xmm7, [esp+0x70]
    add esp, 0x80
    pop edx
    pop ecx
    pop eax
    popfd
    cmp byte ptr [_x3m_smf_apply], 0
    .endm

    .macro X3M_SMF_ROTATION name, axis, record, ship
    .text
    .p2align 4
    .globl _x3m_smf_\name\()_stub
_x3m_smf_\name\()_stub:
    X3M_SMF_CALL \axis, \record, \ship
    je 1f
    fmul qword ptr [_x3m_smf_step_factor]
    mov ecx, dword ptr [_x3m_smf_target]
    mov eax, dword ptr ds:0x00606f34
    jmp dword ptr [_x3m_smf_resume + 4*\axis]
1:
    jmp dword ptr [_x3m_smf_continue + 4*\axis]
    .endm

    .macro X3M_SMF_STRAFE name, axis
    .text
    .p2align 4
    .globl _x3m_smf_\name\()_stub
_x3m_smf_\name\()_stub:
    X3M_SMF_CALL \axis, 0, 0
    je 1f
    fild dword ptr [esi+0x714]
    fmul qword ptr [_x3m_smf_inv_k]
    jmp dword ptr [_x3m_smf_resume + 4*\axis]
1:
    jmp dword ptr [_x3m_smf_continue + 4*\axis]
    .endm

    X3M_SMF_ROTATION yaw, 0, edi, esi
    X3M_SMF_ROTATION pitch, 1, edi, esi
    X3M_SMF_ROTATION roll, 2, esi, edi
    X3M_SMF_STRAFE strafe_y, 3
    X3M_SMF_STRAFE strafe_x, 4
    .att_syntax
)");
