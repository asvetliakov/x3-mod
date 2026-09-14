#pragma once
#include <windows.h>
#include <cstdint>

// Opt-in in-process sampling profiler (X3M_PROFILE=1). One sampler thread
// suspends each application thread in turn, copies its register context, walks
// the EBP chain and scans the stack for return addresses, resumes it, and only
// then aggregates into fixed-size tables. Reports are written to the proxy log
// with the loading_metric QPC clock. Off by default: nothing is allocated, no
// thread exists and no handle is opened unless the switch is set. Initialize
// outside DllMain after the log exists; shutdown only from a quiescent caller
// that holds no proxy lock (the sampler's reports take the log lock).
// See docs/verification/sampling-profiler.md for the safety rules and limits.
namespace x3m::sampling_profiler {
struct Settings {
    unsigned interval_us=2000;  // X3M_PROFILE_INTERVAL_US, 100..1000000
    unsigned report_s=5;        // X3M_PROFILE_REPORT_S, 1..3600
    bool raw=false;             // X3M_PROFILE_RAW=1: raw context + 32 stack dwords on an unresolved leaf, once per thread per report
};
// Starts the sampler when X3M_PROFILE=1; false (and silent) otherwise. One
// generation per process: after shutdown a second initialize is refused.
bool initialize();
bool active();
// Periodic callback run by the sampler thread between ticks (never inside the
// suspended window), every `seconds` (1..3600); one slot, any thread may set
// it. Nothing runs when the profiler is off.
void set_periodic(void (*callback)(uint64_t qpc),unsigned seconds);
// Quiescent teardown: stops the thread (bounded wait), writes the cumulative
// report, closes every thread handle. Never call under a proxy lock or DllMain.
void shutdown();
// Counters for fixtures and the shutdown log line; no lock, values may lag.
struct Status {
    bool active=false;
    uint64_t ticks=0, samples=0, dropped=0, reports=0;
    uint64_t tick_ticks=0, tick_max_ticks=0, report_ticks=0, refresh_ticks=0;
    unsigned threads=0, modules=0, threads_unsampled=0;
};
Status status();
}
