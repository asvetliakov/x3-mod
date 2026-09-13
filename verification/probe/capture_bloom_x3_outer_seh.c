/* Compiler-supported outer test catch; no production implementation here. */
extern int capture_bloom_outer_filter(void*);
extern void capture_bloom_outer_caught(void);
void capture_bloom_outer_call(void (*target)(void)) {
    __try { target(); }
    __except (capture_bloom_outer_filter(__exception_info())) {
        capture_bloom_outer_caught();
    }
}
