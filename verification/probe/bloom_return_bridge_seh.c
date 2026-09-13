/* Compile ONLY this file with Clang's i686-pc-windows-msvc target. GCC's
 * i686 DWARF EH and Clang's i686 GNU target do not implement this contract.
 * No naked/invented exception registration: the compiler emits the entire
 * _except_handler3 registration, scope table, finally funclet and unlink.
 */
#include "bloom_return_bridge.h"
void bloom_return_seh(BloomCall* call) {
    __try {
        bloom_return_pre(call);
        bloom_return_invoke(call);
        bloom_return_post(call);
    } __finally {
        bloom_return_cleanup(call, __abnormal_termination());
    }
}
/* Outer fixture handler is compiler generated too. The exception filter runs
 * before unwind; caller can verify that cleanup happens only during unwind. */
extern int bloom_return_filter(void*);
extern void bloom_return_caught(void);
extern void bloom_return_test_call(void (*target)(void), unsigned misalignment);
void bloom_return_catch(void (*target)(void), unsigned misalignment) {
    __try {
        bloom_return_test_call(target, misalignment);
    } __except (bloom_return_filter(__exception_info())) {
        bloom_return_caught();
    }
}
