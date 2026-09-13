/* Synthetic outer catch frame only; production finally is compiled separately. */
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
