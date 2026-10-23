#include <windows.h>
#include <cstdio>
static double qpc_ns() { static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f); LARGE_INTEGER t; QueryPerformanceCounter(&t); return double(t.QuadPart) * 1e9 / double(f.QuadPart); }
int main() {
    const unsigned mbs[] = {1, 8, 32, 128, 256};
    for (unsigned mb : mbs) {
        unsigned char* p = (unsigned char*)VirtualAlloc(nullptr, SIZE_T(mb) << 20, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!p) { std::printf("mb=%u alloc_failed\n", mb); continue; }
        MEMORY_BASIC_INFORMATION info{}; const unsigned n = 500;
        double t0 = qpc_ns(); for (unsigned i = 0; i < n; ++i) VirtualQuery(p + 4096 * (i % 16), &info, sizeof info); double untouched = (qpc_ns() - t0) / n;
        for (SIZE_T o = 0; o < (SIZE_T(mb) << 20); o += 4096) p[o] = 1;
        t0 = qpc_ns(); for (unsigned i = 0; i < n; ++i) VirtualQuery(p + 4096 * (i % 16), &info, sizeof info); double touched = (qpc_ns() - t0) / n;
        std::printf("region_mb=%u reported_mb=%lu vq_untouched_ns=%.0f vq_touched_ns=%.0f\n", mb, (unsigned long)(info.RegionSize >> 20), untouched, touched);
        VirtualFree(p, 0, MEM_RELEASE);
    }
}
