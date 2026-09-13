#ifndef X3M_BLOOM_RETURN_BRIDGE_H
#define X3M_BLOOM_RETURN_BRIDGE_H
/* Verification-only x86 transport layout, shared with the assembly offsets.
 * Plain C ABI keeps the Clang/MS SEH object independent of C++ EH runtimes. */
#ifndef __ASSEMBLER__
#include <stdint.h>
typedef struct BloomCpu {
    uint32_t regs[9]; /* pushad order: EDI ESI EBP ESP EBX EDX ECX EAX EFLAGS */
    unsigned char x87[108];
    uint32_t mxcsr, error;
    unsigned char xmm[128];
} BloomCpu;
typedef struct BloomCall {
    BloomCpu input, output;
    void (*original)(void);
    uintptr_t caller_stack;
    uint32_t ticket;
} BloomCall;
void bloom_return_bridge(void);
void bloom_return_invoke(BloomCall*);
void bloom_return_seh(BloomCall*);
void bloom_return_pre(BloomCall*);
void bloom_return_post(BloomCall*);
void bloom_return_cleanup(BloomCall*, int abnormal);
extern void (*bloom_return_original)(void);
#endif
#define BC_X87 36
#define BC_MXCSR 144
#define BC_ERROR 148
#define BC_XMM 152
#define BC_SIZE 280
#define BR_TARGET 560
#define BR_STACK 564
#define BR_TICKET 568
#define BR_SIZE 572
#endif
