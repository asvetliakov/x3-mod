// Run176 diagnostics item 1: the per-call cost of QueryThreadCycleTime(GetCurrentThread()) (the handler's phase reads) beside
// QueryPerformanceCounter, i686, run natively on the Windows host (not Wine, not the game). Build and run from WSL2:
//   i686-w64-mingw32-g++ -O2 -msse2 -mfpmath=sse -static qtct_bench.cpp -o <scratch>/qtct_bench.exe && <scratch>/qtct_bench.exe
#include <windows.h>
#include <cstdio>
int main() {
    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);
    const int N = 1000000;
    volatile unsigned long long sink = 0;
    for (int rep = 0; rep < 3; ++rep) {
        QueryPerformanceCounter(&a);
        for (int i = 0; i < N; ++i) {
            ULONG64 c;
            QueryThreadCycleTime(GetCurrentThread(), &c);
            sink += c;
        }
        QueryPerformanceCounter(&b);
        const double qtct = double(b.QuadPart - a.QuadPart) * 1e9 / double(f.QuadPart) / N;
        QueryPerformanceCounter(&a);
        for (int i = 0; i < N; ++i) {
            LARGE_INTEGER v;
            QueryPerformanceCounter(&v);
            sink += v.QuadPart;
        }
        QueryPerformanceCounter(&b);
        const double qpc = double(b.QuadPart - a.QuadPart) * 1e9 / double(f.QuadPart) / N;
        ULONG64 c0, c1;
        QueryThreadCycleTime(GetCurrentThread(), &c0);
        for (int i = 0; i < 8; ++i) {
            ULONG64 c;
            QueryThreadCycleTime(GetCurrentThread(), &c);
            sink += c;
        }
        QueryThreadCycleTime(GetCurrentThread(), &c1);
        std::printf("rep %d: QueryThreadCycleTime %.1f ns/call, QueryPerformanceCounter %.1f ns/call, 8 QTCT calls %llu thread cycles\n", rep,
                    qtct, qpc, static_cast<unsigned long long>(c1 - c0));
    }
    return 0;
}
