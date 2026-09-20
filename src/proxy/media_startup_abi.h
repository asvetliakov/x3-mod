#pragma once
// Shared by actual loader export and its x86 fixture. No return-slot rewrite or
// displaced engine instruction. Ordinary-return transparency only: exceptions
// crossing the factory skip leave and permanently prevent later scheduling;
// the adapter does not invent naked-frame C++/SEH cleanup or restart a context.
//
// At E: return, SDK, ... ancestor at E+20. PUSHFD/PUSHAD is 36 bytes,
// local area 288: x87[108], MXCSR[4], XMM[128], Entry[32], 16 spare bytes.
// C helpers execute with empty x87, default MXCSR and DF clear, with compiler
// stack realignment. Get/SetLastError are inside those helpers, after state save.
// Both incoming and real delegate-output FP/GPR/flags are separately restored.
#define X3M_STARTUP_SAVE_FP \
    "stmxcsr 108(%esp)\n fnsave 0(%esp)\n" \
    "movups %xmm0,112(%esp)\n movups %xmm1,128(%esp)\n movups %xmm2,144(%esp)\n movups %xmm3,160(%esp)\n" \
    "movups %xmm4,176(%esp)\n movups %xmm5,192(%esp)\n movups %xmm6,208(%esp)\n movups %xmm7,224(%esp)\n" \
    "ldmxcsr _x3m_media_startup_default_mxcsr\n cld\n"
#define X3M_STARTUP_RESTORE_FP \
    "frstor 0(%esp)\n ldmxcsr 108(%esp)\n" \
    "movups 112(%esp),%xmm0\n movups 128(%esp),%xmm1\n movups 144(%esp),%xmm2\n movups 160(%esp),%xmm3\n" \
    "movups 176(%esp),%xmm4\n movups 192(%esp),%xmm5\n movups 208(%esp),%xmm6\n movups 224(%esp),%xmm7\n"
#define X3M_MEDIA_STARTUP_EXPORT(export_symbol,delegate_symbol) \
asm(".text\n.globl " export_symbol "\n" export_symbol ":\n" \
    "pushfl\n pushal\n subl $288,%esp\n" \
    X3M_STARTUP_SAVE_FP \
    "leal 324(%esp),%eax\n leal 240(%esp),%ecx\n pushl %eax\n pushl %ecx\n" \
    "call _x3m_media_startup_enter\n addl $8,%esp\n" \
    X3M_STARTUP_RESTORE_FP \
    "pushl 320(%esp)\n popfl\n" \
    "movl 288(%esp),%edi\n movl 292(%esp),%esi\n movl 296(%esp),%ebp\n" \
    "movl 304(%esp),%ebx\n movl 308(%esp),%edx\n movl 312(%esp),%ecx\n movl 316(%esp),%eax\n" \
    "pushl 328(%esp)\n call " delegate_symbol "\n" \
    "pushfl\n pushal\n subl $288,%esp\n" \
    X3M_STARTUP_SAVE_FP \
    "leal 564(%esp),%ecx\n pushl 316(%esp)\n pushl %ecx\n" \
    "call _x3m_media_startup_leave\n addl $8,%esp\n" \
    X3M_STARTUP_RESTORE_FP \
    "addl $288,%esp\n popal\n popfl\n leal 324(%esp),%esp\n ret $4\n");
// First-CreateDevice notification has the same preservation envelope, without
// an original call/entry context. Defined once by media_startup.cpp.
#define X3M_MEDIA_STARTUP_DEVICE_NOTIFY \
asm(".text\n.globl _x3m_media_startup_device_attempt\n_x3m_media_startup_device_attempt:\n" \
    "pushfl\n pushal\n subl $288,%esp\n" \
    X3M_STARTUP_SAVE_FP \
    "call _x3m_media_startup_device_notify\n" \
    X3M_STARTUP_RESTORE_FP \
    "addl $288,%esp\n popal\n popfl\n ret\n");
