/* Full CPU boundary for the verified x86 no-stack-argument compositor ABI.
 * Immediate output capture and FNSAVE/FRSTOR follow the reviewed synthetic
 * bridge. No TEB offsets, Wine helpers or global invocation scratch are used.
 * Binding and active-count semantics are documented in compositor_bridge.h.
 */
#include "compositor_bridge.h"
.text
.macro save_fp base
    fnsave X3M_CB_X87(\base)
    stmxcsr X3M_CB_MXCSR(\base)
    movups %xmm0, X3M_CB_XMM+0(\base)
    movups %xmm1, X3M_CB_XMM+16(\base)
    movups %xmm2, X3M_CB_XMM+32(\base)
    movups %xmm3, X3M_CB_XMM+48(\base)
    movups %xmm4, X3M_CB_XMM+64(\base)
    movups %xmm5, X3M_CB_XMM+80(\base)
    movups %xmm6, X3M_CB_XMM+96(\base)
    movups %xmm7, X3M_CB_XMM+112(\base)
.endm
.macro restore_fp base
    frstor X3M_CB_X87(\base)
    ldmxcsr X3M_CB_MXCSR(\base)
    movups X3M_CB_XMM+0(\base), %xmm0
    movups X3M_CB_XMM+16(\base), %xmm1
    movups X3M_CB_XMM+32(\base), %xmm2
    movups X3M_CB_XMM+48(\base), %xmm3
    movups X3M_CB_XMM+64(\base), %xmm4
    movups X3M_CB_XMM+80(\base), %xmm5
    movups X3M_CB_XMM+96(\base), %xmm6
    movups X3M_CB_XMM+112(\base), %xmm7
.endm
.globl _x3m_compositor_bridge_entry
_x3m_compositor_bridge_entry:
    pushfl
    pushal
    lock incl _x3m_compositor_bridge_active_count
    movl %esp, %ebp
    subl $X3M_CB_RECORD_SIZE+16, %esp
    andl $-16, %esp
    movl %esp, %ebx
    save_fp %ebx
    cld
    ldmxcsr _x3m_compositor_bridge_default_mxcsr
    call *__imp__GetLastError@0
    movl %eax, X3M_CB_ERROR(%ebx)
    movl %ebp, %esi
    movl %ebx, %edi
    movl $9, %ecx
    rep movsl
    leal 36(%ebp), %eax
    movl %eax, X3M_CB_CALLER_STACK(%ebx)
    movl (%eax), %edx
    movl %edx, X3M_CB_CALLER_PC(%ebx)
    movl $0, X3M_CB_FLAGS(%ebx)
    /* One immutable pointer is atomically published by quiescent bind(). */
    movl _x3m_compositor_bridge_binding_bits, %esi
    leal X3M_CB_BINDING(%ebx), %edi
    movl $5, %ecx
    rep movsl
    leal X3M_CB_STORAGE(%ebx), %edi
    xorl %eax, %eax
    movl $X3M_CB_STORAGE_SIZE/4, %ecx
    rep stosl
    pushl %ebx
    call _x3m_compositor_bridge_seh
    addl $4, %esp
    /* Compiler SEH epilogue and cleanup may clobber outputs; restore last. */
    movl X3M_CB_CALLER_STACK(%ebx), %ebp
    leal X3M_CB_CPU_SIZE(%ebx), %esi
    leal -36(%ebp), %edi
    movl $9, %ecx
    cld
    rep movsl
    pushl X3M_CB_CPU_SIZE+X3M_CB_ERROR(%ebx)
    call *__imp__SetLastError@4
    leal X3M_CB_CPU_SIZE(%ebx), %eax
    restore_fp %eax
    leal -36(%ebp), %esp
    lock decl _x3m_compositor_bridge_active_count
    popal
    popfl
    ret

.globl _x3m_compositor_bridge_invoke
_x3m_compositor_bridge_invoke:
    /* Preserve this helper's C caller independently of all original GPRs. */
    pushfl
    pushal
    movl 40(%esp), %ebx
    /* Scratch stack: input pushad/flags, target, record; original plain RET
     * returns with this stack balanced even if every original GPR changes. */
    subl $44, %esp
    movl %ebx, 40(%esp)
    movl X3M_CB_BINDING(%ebx), %eax
    movl %eax, 36(%esp)
    movl %ebx, %esi
    movl %esp, %edi
    movl $9, %ecx
    cld
    rep movsl
    pushl X3M_CB_ERROR(%ebx)
    call *__imp__SetLastError@4
    restore_fp %ebx
    popal
    popfl
    call *(%esp)
.globl _x3m_compositor_bridge_capture_output
_x3m_compositor_bridge_capture_output:
    /* Immediate output capture: no arithmetic before flags, no call before
     * all computational state is saved; MOV does not alter EFLAGS. */
    pushfl
    pushal
    movl 40(%esp), %ebx
    leal X3M_CB_CPU_SIZE(%ebx), %ebx
    save_fp %ebx
    cld
    ldmxcsr _x3m_compositor_bridge_default_mxcsr
    call *__imp__GetLastError@0
    movl %eax, X3M_CB_ERROR(%ebx)
    movl %esp, %esi
    movl %ebx, %edi
    movl $9, %ecx
    rep movsl
    addl $44, %esp
    popal
    popfl
    ret
.section .rdata,"dr"
.align 4
_x3m_compositor_bridge_default_mxcsr: .long 0x1f80
