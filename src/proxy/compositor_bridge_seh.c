/* Compile this isolated C unit with Clang i686-pc-windows-msvc. Its generated
 * _except_handler3 frame, table and finally funclet span pre/original/post.
 * GCC DWARF exceptions and Clang's i686 GNU target are not substitutes.
 * Plain C callbacks avoid cross-compiler C++ object/EH-runtime assumptions.
 */
#define X3M_COMPOSITOR_BRIDGE_INTERNAL
#include "compositor_bridge.h"
#include <stddef.h>

_Static_assert(sizeof(void*)==4 && sizeof(long)==4,"x86 Windows ABI only");
_Static_assert(sizeof(X3mCompositorCpu)==X3M_CB_CPU_SIZE,"CPU layout");
_Static_assert(offsetof(X3mCompositorCpu,x87)==X3M_CB_X87,"x87 offset");
_Static_assert(offsetof(X3mCompositorCpu,mxcsr)==X3M_CB_MXCSR,"MXCSR offset");
_Static_assert(offsetof(X3mCompositorCpu,error)==X3M_CB_ERROR,"LastError offset");
_Static_assert(offsetof(X3mCompositorCpu,xmm)==X3M_CB_XMM,"XMM offset");
_Static_assert(offsetof(X3mCompositorFrame,caller_stack)==X3M_CB_CALLER_STACK,"stack offset");
_Static_assert(offsetof(X3mCompositorFrame,caller_pc)==X3M_CB_CALLER_PC,"PC offset");
_Static_assert(offsetof(X3mCompositorFrame,flags)==X3M_CB_FLAGS,"flags offset");
_Static_assert(sizeof(X3mCompositorBinding)==20,"immutable binding layout");
_Static_assert(offsetof(X3mCompositorRecord,binding)==X3M_CB_BINDING,"binding offset");
_Static_assert(offsetof(X3mCompositorRecord,storage)==X3M_CB_STORAGE,"storage offset");
_Static_assert(X3M_CB_STORAGE%16==0,"entry aligns record to 16 bytes");
_Static_assert(sizeof(X3mCompositorRecord)==X3M_CB_RECORD_SIZE,"record size");

/* Documented Microsoft compiler intrinsics emit x86 atomic operations and
 * introduce no CRT/Win32 imports. The only generated runtime helper is the
 * compiler's _except_handler3; its narrow import is packaged separately. */
long _InterlockedCompareExchange(long volatile*,long,long);
long _InterlockedExchange(long volatile*,long);
long _InterlockedDecrement(long volatile*);
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedDecrement)

/* C symbols consumed only by the assembly entry. Pointer bits are one aligned
 * machine word; the pointed-to Binding remains immutable under caller ownership.
 */
__declspec(align(4)) long volatile x3m_compositor_bridge_binding_bits;
__declspec(align(4)) long volatile x3m_compositor_bridge_active_count;
uint32_t x3m_compositor_bridge_active(void) {
    return (uint32_t)_InterlockedCompareExchange(&x3m_compositor_bridge_active_count,0,0);
}
int x3m_compositor_bridge_bind(const X3mCompositorBinding* binding) {
    if(!binding || !binding->original || !binding->pre || !binding->post || !binding->cleanup)
        return 0;
    if(x3m_compositor_bridge_active()!=0)return 0;
    _InterlockedExchange(&x3m_compositor_bridge_binding_bits,(long)(uintptr_t)binding);
    return 1;
}
int x3m_compositor_bridge_unbind(void) {
    if(x3m_compositor_bridge_active()!=0)return 0;
    _InterlockedExchange(&x3m_compositor_bridge_binding_bits,0);
    return 1;
}
void x3m_compositor_bridge_seh(X3mCompositorRecord* call) {
    __try {
        call->binding.pre(&call->frame,call->storage,call->binding.context);
        x3m_compositor_bridge_invoke(call);
        call->frame.flags=X3M_CB_ORIGINAL_RETURNED;
        call->binding.post(&call->frame,call->storage,call->binding.context);
    } __finally {
        const int abnormal=__abnormal_termination();
        call->binding.cleanup(&call->frame,call->storage,call->binding.context,abnormal);
        /* Normal exit remains counted through the assembly output restore.
         * On unwind that epilogue is never reached; finally owns this decrement.
         * cleanup must not raise/throw, including during partially completed pre.
         */
        if(abnormal)_InterlockedDecrement(&x3m_compositor_bridge_active_count);
    }
}
