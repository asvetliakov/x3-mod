// Frame-pointer-omitting unit of the sampling profiler fixture. Built with
// -fomit-frame-pointer so fpo_leaf/fpo_spin carry no EBP frame: their
// attribution must come from the return-address scan. Everything here stays
// 32-bit (GetTickCount deadline, 32-bit counters): this MinGW target keeps an
// EBP frame for functions that handle 64-bit values even with the flag set.
// Definition order is preserved (-fno-toplevel-reorder) so the *_end markers
// bound each function's code range.
#include <windows.h>
#include <cstdint>

extern "C" __attribute__((noinline)) uint32_t fpo_leaf(uint32_t deadline_ms) {
    uint32_t iterations = 0;
    volatile uint32_t sink = 0;
    while (LONG(GetTickCount() - deadline_ms) < 0) {
        for (int i = 0; i < 200000; ++i) sink = sink * 1664525u + 1013904223u;
        ++iterations;
    }
    return iterations;
}
extern "C" __attribute__((noinline)) void fpo_leaf_end() {
    asm volatile("");
}
extern "C" __attribute__((noinline)) uint32_t fpo_spin(uint32_t deadline_ms) {
    const uint32_t n = fpo_leaf(deadline_ms);
    asm volatile("" ::"r"(n));
    return n + 1; // no tail call
}
extern "C" __attribute__((noinline)) void fpo_spin_end() {
    asm volatile("");
}
