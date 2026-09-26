#ifndef X3M_COMPOSITOR_BRIDGE_H
#define X3M_COMPOSITOR_BRIDGE_H
/* x86 no-stack-argument original-call transport. Not an owner selector or GPU
 * transaction. C/assembly layout is checked in compositor_bridge_seh.c.
 *
 * Install contract: stop admission at the patched game callsite, establish
 * actual render-thread quiescence, then bind an immutable Binding whose storage
 * remains alive until admission is stopped again and every invocation exits.
 * Only then install/restore the patch or replace/unbind the Binding. An active
 * count of zero is necessary but NOT a substitute for quiescence: entry/exit
 * have small instruction windows around their counter operations, and the
 * counter never grants permission to unload executing module code.
 *
 * The atomic Binding pointer prevents partial publication. The API deliberately
 * does not attempt to make patching, binding lifetime, or DLL unload lock-free.
 * Never enter x3m_compositor_bridge_entry while unbound.
 */
#define X3M_CB_CPU_SIZE 280
#define X3M_CB_X87 36
#define X3M_CB_MXCSR 144
#define X3M_CB_ERROR 148
#define X3M_CB_XMM 152
#define X3M_CB_CALLER_STACK 560
#define X3M_CB_CALLER_PC 564
#define X3M_CB_FLAGS 568
#define X3M_CB_BINDING 572
#define X3M_CB_STORAGE 592
#define X3M_CB_STORAGE_SIZE 512
#define X3M_CB_RECORD_SIZE 1104
#define X3M_CB_ORIGINAL_RETURNED 1
#ifndef __ASSEMBLER__
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct X3mCompositorCpu {
    uint32_t regs[9];       /* PUSHAD order: EDI ESI EBP ESP EBX EDX ECX EAX EFLAGS */
    unsigned char x87[108]; /* FNSAVE/FRSTOR legacy state, not our arithmetic */
    uint32_t mxcsr, error;
    unsigned char xmm[128];
} X3mCompositorCpu;
typedef struct X3mCompositorFrame {
    X3mCompositorCpu input, output;
    uintptr_t caller_stack; /* actual bridge-entry ESP, pointing at caller PC */
    uintptr_t caller_pc;    /* actual value at caller_stack, not a C return PC */
    uint32_t flags;         /* output is valid iff ORIGINAL_RETURNED is set */
} X3mCompositorFrame;

/* All callbacks use the ordinary x86 C ABI and accept four-byte incoming
 * stacks. storage is invocation-local, zeroed, 512 bytes, 16-byte aligned;
 * callers must statically check their payload's size/alignment. No constructor
 * or destructor is implicitly run. Only storage is writable by callbacks.
 *
 * pre may decline its own admission in storage; the transport still calls
 * original exactly once on every normal pre return. Exceptional pre does NOT
 * call original/post: the exception propagates after explicit cleanup. Output
 * is captured immediately in assembly before any post/finally C executes.
 * cleanup runs once on normal exit or Windows SEH unwind, must not throw/raise,
 * must explicitly release pins/locks/references and tolerate partial pre.
 * Do not assume GCC RAII destructors run under Windows SEH. The transport does
 * not translate C++ exceptions between differing compiler exception runtimes.
 */
typedef void (*X3mCompositorCallback)(const X3mCompositorFrame*, void* storage, void* context);
typedef void (*X3mCompositorCleanup)(const X3mCompositorFrame*, void* storage, void* context, int abnormal);
typedef struct X3mCompositorBinding {
    void (*original)(void); /* verified no-stack-argument target, plain RET */
    X3mCompositorCallback pre, post;
    X3mCompositorCleanup cleanup;
    void* context;
} X3mCompositorBinding;

/* Quiescent-only bind/unbind, outside DllMain; returns 0 on invalid callbacks
 * or an observed active invocation. All four function pointers are required.
 * Caller owns immutable Binding storage; no binding copy is retained globally.
 */
int x3m_compositor_bridge_bind(const X3mCompositorBinding* binding);
int x3m_compositor_bridge_unbind(void);
uint32_t x3m_compositor_bridge_active(void);
void x3m_compositor_bridge_entry(void);

#ifdef X3M_COMPOSITOR_BRIDGE_INTERNAL
/* C/assembly-private stack layout; not a second callback API. */
typedef struct X3mCompositorRecord {
    X3mCompositorFrame frame;
    X3mCompositorBinding binding;
    unsigned char storage[X3M_CB_STORAGE_SIZE];
} X3mCompositorRecord;
void x3m_compositor_bridge_invoke(X3mCompositorRecord*);
void x3m_compositor_bridge_seh(X3mCompositorRecord*);
#endif
#ifdef __cplusplus
}
#endif
#endif
#endif
