// Per-call cost of the loading_trace gzwrite row added on every modded launch (observer idle / pumping, music_keep's
// sample_due stride 32 and its sampled path's QPC + LightCallBoundary + GetCurrentThreadId), and of a bare
// LightCallBoundary (the music_keep Present KC check builds one before its fast-path test). Native, not game FPS.
#include "cpu_state.h"
#include <cstdio>
#include <cstdint>
static double qpc_ns() { static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f); LARGE_INTEGER t; QueryPerformanceCounter(&t); return double(t.QuadPart) * 1e9 / double(f.QuadPart); }
typedef int(__cdecl* GzWriteFn)(void*, const void*, unsigned);
volatile unsigned sink = 0;
extern "C" __attribute__((noinline)) int __cdecl fake_original(void*, const void*, unsigned size) { sink = sink + size; return int(size); }
GzWriteFn volatile original_ptr = fake_original;
volatile std::uint32_t pumping = 0;
struct Pump { unsigned writes = 0, countdown = 32, samples = 0; std::uint32_t thread = 0; std::uint64_t last = 0;
    bool sample_due() { ++writes; if (--countdown) return false; countdown = 32; ++samples; return true; } } pump;
__attribute__((noinline)) void sampled() {
    x3m::LightCallBoundary cpu; LARGE_INTEGER v; QueryPerformanceCounter(&v);
    if (GetCurrentThreadId() == pump.thread && std::uint64_t(v.QuadPart) - pump.last > 2500000) pump.last = v.QuadPart; }
extern "C" __attribute__((noinline, force_align_arg_pointer)) void __cdecl observer_write() { if (!pumping || !pump.sample_due()) return; sampled(); }
struct Obs { const volatile std::uint32_t* active; void(__cdecl* write)(); } obs{&pumping, &observer_write};
bool traced = false;
extern "C" __attribute__((noinline)) int __cdecl gz_write(void* f, const void* d, unsigned n) {
    if (obs.active && *obs.active) obs.write();
    return traced ? 0 : original_ptr(f, d, n);
}
GzWriteFn volatile row_wrapped = gz_write, row_direct = fake_original;
__attribute__((noinline)) void lcb_only() { x3m::LightCallBoundary cpu; }
int main() {
    const unsigned n = 24000000; char buf[4] = {};
    pump.thread = GetCurrentThreadId();
    double t0 = qpc_ns(); for (unsigned i = 0; i < n; ++i) row_direct(nullptr, buf, 3); const double direct = (qpc_ns() - t0) / n;
    pumping = 0; t0 = qpc_ns(); for (unsigned i = 0; i < n; ++i) row_wrapped(nullptr, buf, 3); const double idle = (qpc_ns() - t0) / n;
    pumping = 1; t0 = qpc_ns(); for (unsigned i = 0; i < n; ++i) row_wrapped(nullptr, buf, 3); const double pump_ns = (qpc_ns() - t0) / n;
    const unsigned m = 2000000; t0 = qpc_ns(); for (unsigned i = 0; i < m; ++i) sampled(); const double samp = (qpc_ns() - t0) / m;
    t0 = qpc_ns(); for (unsigned i = 0; i < m; ++i) lcb_only(); const double lcb = (qpc_ns() - t0) / m;
    std::printf("calls=%u direct_ns=%.2f wrapped_idle_ns=%.2f wrapped_pumping_ns=%.2f samples=%u sampled_path_ns=%.1f light_call_boundary_ns=%.1f\n",
                n, direct, idle, pump_ns, pump.samples, samp, lcb);
    std::printf("per_save_23939946_writes: idle_extra_ms=%.1f pumping_extra_ms=%.1f (of which samples_ms=%.1f)\n",
                (idle - direct) * 23939946 / 1e6, (pump_ns - direct) * 23939946 / 1e6, samp * 748124 / 1e6);
    return 0;
}
