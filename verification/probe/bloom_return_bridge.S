/* No game bytes or patching. A no-stack-argument CALL replacement prototype.
 * All original output is captured in assembly before returning to any C code.
 * FNSAVE/FRSTOR is intentional legacy-state transport (see cpu_state.h).
 * LastError uses public Win32 imports; no TEB offsets/backend-private data.
 * DF is cleared only after EFLAGS capture, before any injected C/CRT work.
 */
#include "bloom_return_bridge.h"
.text
.macro save_fp base
    fnsave BC_X87(\base)
    stmxcsr BC_MXCSR(\base)
    movups %xmm0, BC_XMM+0(\base)
    movups %xmm1, BC_XMM+16(\base)
    movups %xmm2, BC_XMM+32(\base)
    movups %xmm3, BC_XMM+48(\base)
    movups %xmm4, BC_XMM+64(\base)
    movups %xmm5, BC_XMM+80(\base)
    movups %xmm6, BC_XMM+96(\base)
    movups %xmm7, BC_XMM+112(\base)
.endm
.macro restore_fp base
    frstor BC_X87(\base)
    ldmxcsr BC_MXCSR(\base)
    movups BC_XMM+0(\base), %xmm0
    movups BC_XMM+16(\base), %xmm1
    movups BC_XMM+32(\base), %xmm2
    movups BC_XMM+48(\base), %xmm3
    movups BC_XMM+64(\base), %xmm4
    movups BC_XMM+80(\base), %xmm5
    movups BC_XMM+96(\base), %xmm6
    movups BC_XMM+112(\base), %xmm7
.endm
.globl _bloom_return_bridge
_bloom_return_bridge:
    pushfl
    pushal
    movl %esp, %ebp
    subl $BR_SIZE+16, %esp
    andl $-16, %esp
    movl %esp, %ebx
    save_fp %ebx
    cld
    ldmxcsr _bloom_return_default_mxcsr
    call *__imp__GetLastError@0
    movl %eax, BC_ERROR(%ebx)
    movl %ebp, %esi
    movl %ebx, %edi
    movl $9, %ecx
    rep movsl
    leal 36(%ebp), %eax
    movl %eax, BR_STACK(%ebx)
    movl _bloom_return_original, %eax
    movl %eax, BR_TARGET(%ebx)
    movl $0, BR_TICKET(%ebx)
    pushl %ebx
    call _bloom_return_seh
    addl $4, %esp
    /* Compiler SEH epilogue and cleanup may clobber outputs; restore last. */
    movl BR_STACK(%ebx), %ebp
    leal BC_SIZE(%ebx), %esi
    leal -36(%ebp), %edi
    movl $9, %ecx
    cld
    rep movsl
    pushl BC_SIZE+BC_ERROR(%ebx)
    call *__imp__SetLastError@4
    leal BC_SIZE(%ebx), %eax
    restore_fp %eax
    leal -36(%ebp), %esp
    popal
    popfl
    ret

.globl _bloom_return_invoke
_bloom_return_invoke:
    /* Preserve this helper's C caller independently of all original GPRs. */
    pushfl
    pushal
    movl 40(%esp), %ebx
    /* Scratch stack: input pushad/flags, target, record; original plain RET
     * returns with this stack balanced even if every original GPR changes. */
    subl $44, %esp
    movl %ebx, 40(%esp)
    movl BR_TARGET(%ebx), %eax
    movl %eax, 36(%esp)
    movl %ebx, %esi
    movl %esp, %edi
    movl $9, %ecx
    cld
    rep movsl
    pushl BC_ERROR(%ebx)
    call *__imp__SetLastError@4
    restore_fp %ebx
    popal
    popfl
    call *(%esp)
.globl _bloom_return_capture_output
_bloom_return_capture_output:
    /* Immediate output capture: no arithmetic before flags, no call before
     * all computational state is saved; MOV does not alter EFLAGS. */
    pushfl
    pushal
    movl 40(%esp), %ebx
    leal BC_SIZE(%ebx), %ebx
    save_fp %ebx
    cld
    ldmxcsr _bloom_return_default_mxcsr
    call *__imp__GetLastError@0
    movl %eax, BC_ERROR(%ebx)
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
_bloom_return_default_mxcsr: .long 0x1f80
/* Synthetic harness below is deliberately outside any production path. */
.text
.globl _bloom_return_test_call
_bloom_return_test_call:
    pushfl
    pushal
    movl %esp, %ebp
    subl $32, %esp
    andl $-16, %esp
    movl 44(%ebp), %eax
    subl %eax, %esp
    movl %ebp, 4(%esp)
    movl 40(%ebp), %eax
    movl %eax, (%esp)
    movl %esp, _bloom_return_stack_before
    call _bloom_return_seed_input
    call *(%esp)
    pushfl
    pushal
    movl $_bloom_return_observed, %ebx
    save_fp %ebx
    cld
    ldmxcsr _bloom_return_default_mxcsr
    call *__imp__GetLastError@0
    movl %eax, BC_ERROR(%ebx)
    movl %esp, %esi
    movl %ebx, %edi
    movl $9, %ecx
    rep movsl
    leal 36(%esp), %eax
    movl %eax, _bloom_return_stack_after
    movl 40(%esp), %esp
    popal
    popfl
    ret
.globl _bloom_return_seed_input
_bloom_return_seed_input:
    pushl $0x12345678
    call *__imp__SetLastError@4
    fninit
    fld1
    fldpi
    fldz
    fldcw _bloom_return_input_cw
    ldmxcsr _bloom_return_input_mxcsr
    movups _bloom_return_input_xmm, %xmm0
    movups _bloom_return_input_xmm, %xmm1
    movups _bloom_return_input_xmm, %xmm2
    movups _bloom_return_input_xmm, %xmm3
    movups _bloom_return_input_xmm, %xmm4
    movups _bloom_return_input_xmm, %xmm5
    movups _bloom_return_input_xmm, %xmm6
    movups _bloom_return_input_xmm, %xmm7
    movl $0x11111111, %eax
    movl $0x22222222, %ecx
    movl $0x33333333, %edx
    movl $0x44444444, %ebx
    movl $0x55555555, %ebp
    movl $0x66666666, %esi
    movl $0x77777777, %edi
    pushl $0x647
    popfl
    ret
.globl _bloom_return_synthetic_original
_bloom_return_synthetic_original:
    pushfl
    pushal
    movl $_bloom_return_seen_input, %ebx
    save_fp %ebx
    cld
    ldmxcsr _bloom_return_default_mxcsr
    call *__imp__GetLastError@0
    movl %eax, BC_ERROR(%ebx)
    movl %esp, %esi
    movl %ebx, %edi
    movl $9, %ecx
    rep movsl
    call _bloom_return_original_body
    /* Intentionally violate callee-saved GPR conventions: the bridge must
     * propagate exact game output while protecting its C SEH parent frame. */
    fninit
    fldl2t
    fldln2
    fld1
    fldpi
    fldcw _bloom_return_output_cw
    ldmxcsr _bloom_return_output_mxcsr
    movups _bloom_return_output_xmm, %xmm0
    movups _bloom_return_output_xmm, %xmm1
    movups _bloom_return_output_xmm, %xmm2
    movups _bloom_return_output_xmm, %xmm3
    movups _bloom_return_output_xmm, %xmm4
    movups _bloom_return_output_xmm, %xmm5
    movups _bloom_return_output_xmm, %xmm6
    movups _bloom_return_output_xmm, %xmm7
    addl $36, %esp
    movl $0xabcdef01, %eax
    movl $0xabcdef02, %ecx
    movl $0xabcdef03, %edx
    movl $0xabcdef04, %ebx
    movl $0xabcdef05, %ebp
    movl $0xabcdef06, %esi
    movl $0xabcdef07, %edi
    pushl $0xe93
    popfl
    ret
.globl _bloom_return_clobber
_bloom_return_clobber:
    fninit
    fldz
    ldmxcsr _bloom_return_default_mxcsr
    pxor %xmm0, %xmm0
    pxor %xmm1, %xmm1
    pxor %xmm2, %xmm2
    pxor %xmm3, %xmm3
    pxor %xmm4, %xmm4
    pxor %xmm5, %xmm5
    pxor %xmm6, %xmm6
    pxor %xmm7, %xmm7
    pushl $0xf00dcafe
    call *__imp__SetLastError@4
    ret
.section .rdata,"dr"
.align 4
_bloom_return_input_mxcsr: .long 0x5f85
_bloom_return_output_mxcsr: .long 0x3f91
_bloom_return_input_cw: .short 0x077f
_bloom_return_output_cw: .short 0x0b7f
_bloom_return_input_xmm: .long 0x10203040,0x50607080,0x90abcdef,0x12345678
_bloom_return_output_xmm: .long 0xcafef00d,0x10234567,0x89abcdef,0x98765432
