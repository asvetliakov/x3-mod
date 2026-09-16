// Host checks of the frame-time diagnostic. Two layers: the window reduction
// in src/proxy/frame_timing.h, and src/proxy/frame_timing.cpp itself, compiled
// into this probe against the Win32 stand-in of
// verification/probe/frame_timing_standin (scripted QueryPerformanceCounter,
// one tick per microsecond, and a log sink here), so the accumulation the
// hooks drive is executed rather than inspected: outermost-only accounting,
// the nesting depth, the native-Present subtraction, the slow-call witness and
// the emitted line. `--cost` measures the added wall time per hooked call with
// the stand-in reading the host monotonic clock; it is not part of the test.
//
// Host checks of the frame-time window reduction in src/proxy/frame_timing.h:
// percentiles, the slow count, the four-slot slowest ring, the per-bucket
// proxy-time reduction with its call counts and slow-call witness, and the
// absence of any allocation while samples are collected. No Win32, no device,
// no game.
#include "../../src/proxy/frame_timing.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>

static bool refuse_allocation = false;
void* operator new(std::size_t n) { if (refuse_allocation) { std::fprintf(stderr, "allocated\n"); std::abort(); } if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

// The production translation unit, with its Win32 stand-in and its log sink.
#include "../../src/proxy/frame_timing.cpp"
using namespace x3m::frame_timing;
static char logged[8][1024]; // the window line carries the per-entry state mix
static unsigned logged_count = 0;
namespace x3m {
void log(const char* format, ...) {
    std::va_list args;
    va_start(args, format);
    std::vsnprintf(logged[logged_count % 8], sizeof logged[0], format, args);
    va_end(args);
    ++logged_count;
}
}
// One simulated frame of hooked calls, in the order and nesting the hooks use:
// two draw hooks around a forwarded native draw, five state calls of which one
// reenters the proxy's own hooked call, then the Present hook's scene scope
// holding the forwarded native Present, a reentrant call and the frame
// boundary. Returns the ticks it advanced.
static long long simulate_frame(std::uint64_t index) {
    const long long before = x3m_win32_standin::clock_ticks;
    for (unsigned d = 0; d < 2; ++d) {
        Scope draw_scope(Bucket::Draw, "draw_indexed");
        x3m_win32_standin::advance(5);
        draw_native_begin();
        x3m_win32_standin::advance(20); // the forwarded native draw
        draw_native_end();
        draw(3);
        x3m_win32_standin::advance(5);
    }
    for (unsigned c = 0; c < 4; ++c) {
        Scope state_scope(Bucket::State, "set_texture");
        x3m_win32_standin::advance(2);
    }
    {
        Scope outer(Bucket::State, "set_render_state");
        x3m_win32_standin::advance(1);
        {
            Scope reentered(Bucket::State, "set_texture"); // the proxy's own call
            x3m_win32_standin::advance(3);
        }
        x3m_win32_standin::advance(1);
    }
    {
        Scope scene_scope(Bucket::Scene, "present");
        x3m_win32_standin::advance(7);
        present_begin();
        x3m_win32_standin::advance(100); // the forwarded native Present
        present_end();
        {
            Scope reentered(Bucket::State, "set_texture"); // a pass reentering a hook
            x3m_win32_standin::advance(2000);
        }
        frame(index, 2);
    }
    return x3m_win32_standin::clock_ticks - before;
}
// One scripted frame with explicit unhooked game time between the hooked
// calls: 300 us before the first draw, 500 + 600 us between the two draws
// around one 7 us state call, and 900 us after the last draw, then the Present
// hook's scene scope (10 us of proxy work, a 100 us native Present) holding
// the frame boundary. dt is 2477 us.
static long long simulate_gap_frame(std::uint64_t index) {
    const long long before = x3m_win32_standin::clock_ticks;
    x3m_win32_standin::advance(300); // game time before the first draw
    {
        Scope draw_scope(Bucket::Draw, "draw_indexed");
        x3m_win32_standin::advance(5);
        draw_native_begin();
        x3m_win32_standin::advance(20);
        draw_native_end();
        draw(3);
        x3m_win32_standin::advance(5);
    }
    x3m_win32_standin::advance(500); // game time between hooked calls
    { Scope state_scope(Bucket::State, "set_texture"); x3m_win32_standin::advance(7); }
    x3m_win32_standin::advance(600);
    { Scope draw_scope(Bucket::Draw, "draw_primitive"); x3m_win32_standin::advance(30); }
    x3m_win32_standin::advance(900); // game time after the last draw
    {
        Scope scene_scope(Bucket::Scene, "present");
        x3m_win32_standin::advance(10);
        present_begin();
        x3m_win32_standin::advance(100);
        present_end();
        frame(index, 2);
    }
    return x3m_win32_standin::clock_ticks - before;
}
static const char* last_line(const char* prefix) {
    for (unsigned i = logged_count; i-- > 0 && logged_count - i <= 8;)
        if (!std::strncmp(logged[i % 8], prefix, std::strlen(prefix))) return logged[i % 8];
    return "";
}
static double cost_per_call(bool on) {
    x3m_win32_standin::real_clock = true;
    active = on;
    timespec begin{}, end{};
    const unsigned long calls = 2000000;
    clock_gettime(CLOCK_MONOTONIC_RAW, &begin);
    for (unsigned long i = 0; i < calls; ++i) { Scope scope(Bucket::State, "set_texture"); }
    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    const double ns = double(end.tv_sec - begin.tv_sec) * 1e9 + double(end.tv_nsec - begin.tv_nsec);
    x3m_win32_standin::real_clock = false;
    return ns / double(calls);
}

static unsigned checks = 0;
static void check_impl(bool value, int line) { ++checks; if (!value) { std::fprintf(stderr, "check %u failed (line %d)\n", checks, line); std::exit(1); } }
#define check(expression) check_impl((expression), __LINE__)

static Window reduction; // static storage: the production window is a global too

int main(int argc, char** argv) {
    Summary summary;
    check(!reduction.close(summary)); // an empty window reports nothing
    check(reduction.count() == 0 && !reduction.full());

    // Ascending series: dt 10..3000 us, draws 0..299, present 0..598.
    refuse_allocation = true;
    for (unsigned i = 0; i < window_frames; ++i)
        reduction.add({i, (i + 1) * 10ull, i * 2ull, i, i * 7ull});
    check(reduction.full() && reduction.count() == window_frames);
    check(reduction.close(summary));
    refuse_allocation = false;
    check(summary.frames == 300 && summary.frame == 299);
    check(summary.dt_p50 == 1510 && summary.dt_p95 == 2860 && summary.dt_max == 3000);
    check(summary.draws_p50 == 150 && summary.draws_max == 299);
    check(summary.present_p50 == 300 && summary.present_p95 == 570 && summary.present_max == 598);
    check(summary.slow == 0); // no frame exceeds twice the median here
    check(summary.slow_frames_count == 4);
    check(summary.slow_frames[0].frame == 299 && summary.slow_frames[0].dt_us == 3000);
    check(summary.slow_frames[0].draws == 299 && summary.slow_frames[0].present_us == 598 && summary.slow_frames[0].prims == 299 * 7);
    check(summary.slow_frames[1].frame == 298 && summary.slow_frames[2].frame == 297 && summary.slow_frames[3].frame == 296);
    check(reduction.count() == 0); // close restarts the window
    check(!reduction.close(summary));

    // Flat 1000 us window with five 5000 us spikes: slow counts every frame
    // above twice the median, the ring retains the four earliest of the tie.
    for (unsigned i = 0; i < window_frames; ++i) {
        const bool spike = i == 10 || i == 20 || i == 30 || i == 40 || i == 50;
        reduction.add({i, spike ? 5000ull : 1000ull, spike ? 900ull : 100ull, spike ? 4000ull : 500ull, 0});
    }
    check(reduction.close(summary));
    check(summary.dt_p50 == 1000 && summary.dt_max == 5000 && summary.dt_p95 == 1000);
    check(summary.slow == 5);
    check(summary.draws_p50 == 500 && summary.draws_max == 4000);
    check(summary.present_p50 == 100 && summary.present_max == 900);
    check(summary.slow_frames_count == 4);
    check(summary.slow_frames[0].frame == 10 && summary.slow_frames[1].frame == 20);
    check(summary.slow_frames[2].frame == 30 && summary.slow_frames[3].frame == 40);
    for (unsigned i = 0; i < 4; ++i) check(summary.slow_frames[i].dt_us == 5000);

    // A partial window reduces over the samples it has; the ring is ordered by
    // dt, slowest first, with a late spike displacing an earlier smaller one.
    const std::uint64_t dts[] = {300, 100, 500, 200, 400};
    for (unsigned i = 0; i < 5; ++i) reduction.add({i, dts[i], dts[i] / 10, i, i});
    check(!reduction.full() && reduction.count() == 5);
    check(reduction.close(summary));
    check(summary.frames == 5 && summary.frame == 4);
    check(summary.dt_p50 == 300 && summary.dt_p95 == 500 && summary.dt_max == 500);
    check(summary.present_p50 == 30 && summary.present_max == 50);
    check(summary.slow == 0);
    check(summary.slow_frames_count == 4);
    check(summary.slow_frames[0].frame == 2 && summary.slow_frames[1].frame == 4);
    check(summary.slow_frames[2].frame == 0 && summary.slow_frames[3].frame == 3);

    // One sample: every percentile is that sample and nothing is slow.
    reduction.add({77, 9000, 8000, 12, 34});
    check(reduction.close(summary));
    check(summary.frames == 1 && summary.frame == 77 && summary.dt_p50 == 9000 && summary.dt_p95 == 9000 && summary.dt_max == 9000);
    check(summary.draws_p50 == 12 && summary.present_p95 == 8000 && summary.slow == 0);
    check(summary.slow_frames_count == 1 && summary.slow_frames[0].prims == 34);

    // More than 300 samples without a close: the extra samples are dropped
    // rather than written past the fixed arrays, and the ring still tracks dt.
    refuse_allocation = true;
    for (unsigned i = 0; i < window_frames + 50; ++i) reduction.add({i, 1000, 100, 1, 1});
    refuse_allocation = false;
    check(reduction.count() == window_frames);
    check(reduction.close(summary));
    check(summary.frames == 300 && summary.frame == 349 && summary.dt_max == 1000);


    // Buckets: draw/scene/state microseconds, their call counts and the
    // slowest single hooked call travel with the sample and reduce like dt.
    // draw 100..30000 us, scene a flat 500 with one spike, state 10..3000.
    static const char* const names[] = {"draw_indexed", "present", "set_texture"};
    refuse_allocation = true;
    for (unsigned i = 0; i < window_frames; ++i) {
        Frame sample{i, 1000 + i, 0, i, 0, {}, 0, {}, "", 0};
        sample.bucket_us[unsigned(Bucket::Draw)] = (i + 1) * 100ull;
        sample.bucket_us[unsigned(Bucket::Scene)] = i == 7 ? 9000ull : 500ull;
        sample.bucket_us[unsigned(Bucket::State)] = (i + 1) * 10ull;
        sample.draw_native_us = (i + 1) * 60ull;
        sample.bucket_calls[unsigned(Bucket::Draw)] = i;
        sample.bucket_calls[unsigned(Bucket::Scene)] = 2;
        sample.bucket_calls[unsigned(Bucket::State)] = i * 10;
        sample.slow_call = names[i % 3];
        sample.slow_call_us = i * 3ull;
        reduction.add(sample);
    }
    check(reduction.close(summary));
    refuse_allocation = false;
    check(summary.bucket_p50[unsigned(Bucket::Draw)] == 15100 && summary.bucket_p95[unsigned(Bucket::Draw)] == 28600);
    check(summary.bucket_max[unsigned(Bucket::Draw)] == 30000);
    check(summary.draw_native_p50 == 9060 && summary.draw_native_max == 18000);
    check(summary.bucket_p50[unsigned(Bucket::Scene)] == 500 && summary.bucket_max[unsigned(Bucket::Scene)] == 9000);
    check(summary.bucket_p95[unsigned(Bucket::Scene)] == 500);
    check(summary.bucket_p50[unsigned(Bucket::State)] == 1510 && summary.bucket_max[unsigned(Bucket::State)] == 3000);
    check(summary.bucket_calls_p50[unsigned(Bucket::Draw)] == 150);
    check(summary.bucket_calls_p50[unsigned(Bucket::Scene)] == 2);
    check(summary.bucket_calls_p50[unsigned(Bucket::State)] == 1500);
    // The slowest frame of the window carries its own bucket values, counts
    // and the name of its slowest hooked call.
    check(summary.slow_frames_count == 4 && summary.slow_frames[0].frame == 299);
    check(summary.slow_frames[0].bucket_us[unsigned(Bucket::Draw)] == 30000);
    check(summary.slow_frames[0].bucket_us[unsigned(Bucket::State)] == 3000);
    check(summary.slow_frames[0].draw_native_us == 18000);
    check(summary.slow_frames[0].bucket_calls[unsigned(Bucket::State)] == 2990);
    check(summary.slow_frames[0].slow_call_us == 897);
    check(!std::strcmp(summary.slow_frames[0].slow_call, "set_texture"));
    check(!std::strcmp(summary.slow_frames[1].slow_call, "present"));

    // A sample with no hooked call at all keeps zeroed buckets and an empty
    // slow-call name; the defaults must not read past the array.
    reduction.add({5, 100, 0, 0, 0});
    check(reduction.close(summary));
    check(summary.bucket_p50[unsigned(Bucket::Draw)] == 0 && summary.bucket_calls_p50[unsigned(Bucket::State)] == 0);
    check(summary.draw_native_max == 0 && summary.slow_frames[0].slow_call_us == 0);
    check(summary.slow_frames[0].slow_call && !summary.slow_frames[0].slow_call[0]);


    // The production accumulation, executed. One read of the option, then a
    // steady 300-frame window of hooked calls: per frame two draws of 30 us
    // each (20 us of it the forwarded native draw), five state calls totalling
    // 13 us (the sixth is a reentrant call inside one of them), and the Present
    // hook's scene scope of 2107 us holding a 100 us native Present and a
    // 2000 us reentrant call.
    x3m_win32_standin::environment = L"1";
    x3m_win32_standin::environment_stamps = L"1"; // stamp every state call
    x3m_win32_standin::environment_reads = 0;
    initialize();
    check(active && state_stamps == 1 && x3m_win32_standin::environment_reads == 2);
    check(depth == 0 && native_excluded == 0);
    refuse_allocation = true;
    const unsigned counter_before = x3m_win32_standin::counter_reads;
    const long long frame_ticks = simulate_frame(1);
    // One QueryPerformanceCounter pair per outermost hooked call and per
    // forwarded native call, none for the two reentrant calls, one at the
    // frame boundary: 8 for the draws, 10 for the five state calls, 5 for the
    // Present hook.
    check(x3m_win32_standin::counter_reads - counter_before == 23);
    check(frame_ticks == 2180 && depth == 0); // the depth returns to zero
    bool steady = true;
    for (std::uint64_t f = 2; f <= window_frames + 1; ++f)
        steady = steady && simulate_frame(f) == 2180 && depth == 0;
    check(steady); // every frame of the window, and the depth back to zero
    refuse_allocation = false;
    // The native Present is subtracted from the enclosing scope exactly once
    // per frame and never counted as proxy time.
    check(native_excluded == 100 * (window_frames + 1));
    check(logged_count == 5); // one window line and four slow witnesses
    const char* line = last_line("frame_timing frame=");
    check(std::strstr(line, "frame_timing frame=301 frames=300 dt_p50_us=2180") != nullptr);
    check(std::strstr(line, "present_p50_us=100 present_p95_us=100 present_max_us=100") != nullptr);
    check(std::strstr(line, "draw_p50_us=60 draw_p95_us=60 draw_max_us=60") != nullptr);
    check(std::strstr(line, "draw_native_p50_us=40 draw_native_max_us=40") != nullptr);
    // The scene scope closes after the frame boundary, so it is accounted to
    // the next frame; its 2000 us reentrant call stays inside it.
    check(std::strstr(line, "scene_p50_us=2007 scene_p95_us=2007 scene_max_us=2007") != nullptr);
    check(std::strstr(line, "state_p50_us=13 state_p95_us=13 state_max_us=13") != nullptr);
    check(std::strstr(line, "draw_calls_p50=2 scene_calls_p50=1 state_calls_p50=5 state_sampled=1 slow=0") != nullptr);
    // The frame's first draw begins at the boundary and the Present scope of
    // the previous frame closes just after it, so gap_pre clamps at zero; the
    // 2007 us the scope holds is charged to this frame and shows in gap_post.
    check(std::strstr(line, "gap_pre_p50_us=0 gap_pre_p95_us=0 gap_pre_max_us=0") != nullptr);
    check(std::strstr(line, "gap_draw_p50_us=0 gap_draw_p95_us=0 gap_draw_max_us=0") != nullptr);
    check(std::strstr(line, "gap_post_p50_us=2007 gap_post_p95_us=2007 gap_post_max_us=2007") != nullptr);
    check(std::strstr(line, "gap_draw_per_draw_us=0.000") != nullptr);
    // Per-entry state counts, most-called first: four set_texture calls and
    // one set_render_state per frame; the reentrant call is not counted.
    check(std::strstr(line, "state_top=set_texture:4,set_render_state:1 state_other_p50=0") != nullptr);
    const char* witness = last_line("frame_timing_slow frame=");
    check(std::strstr(witness, "dt_us=2180 draws=2 present_us=100 prims=6") != nullptr);
    check(std::strstr(witness, "draw_us=60 draw_native_us=40 scene_us=2007 state_us=13") != nullptr);
    check(std::strstr(witness, "draw_calls=2 scene_calls=1 state_calls=5") != nullptr);
    check(std::strstr(witness, "gap_pre_us=0 gap_draw_us=0 gap_post_us=2007") != nullptr);
    // The reentrant 2000 us call never becomes the slow call: the entry the
    // game made owns that time, and it is the Present hook's scope.
    check(std::strstr(witness, "slow_call=present slow_call_us=2007") != nullptr);

    // Early close: the scope is counted once, the destructor adds nothing and
    // the depth still returns to zero.
    const std::uint64_t state_before = bucket_ticks[unsigned(Bucket::State)];
    const std::uint64_t calls_before = bucket_calls[unsigned(Bucket::State)];
    {
        Scope scope(Bucket::State, "set_sampler_state");
        x3m_win32_standin::advance(9);
        scope.close();
        x3m_win32_standin::advance(90); // after the measurement, not counted
    }
    check(depth == 0);
    check(bucket_ticks[unsigned(Bucket::State)] == state_before + 9);
    check(bucket_calls[unsigned(Bucket::State)] == calls_before + 1);
    // Option off: the scope is inert and reads no clock.
    active = false;
    const unsigned reads_before = x3m_win32_standin::counter_reads;
    { Scope scope(Bucket::Draw, "draw_primitive"); x3m_win32_standin::advance(50); }
    check(x3m_win32_standin::counter_reads == reads_before && depth == 0);
    // The per-frame accumulators were reset at the last frame boundary.
    check(bucket_calls[unsigned(Bucket::Draw)] == 0 && bucket_ticks[unsigned(Bucket::Draw)] == 0);


    // Default: state calls are counted, never stamped. The same scripted frame
    // with explicit game time between the hooked calls, 301 frames, so the
    // window line carries the gap split and state_us=-1.
    x3m_win32_standin::environment = L"1";
    x3m_win32_standin::environment_stamps = nullptr;
    logged_count = 0;
    initialize();
    check(active && state_stamps == 0);
    simulate_gap_frame(1); // only starts the interval
    const unsigned gap_reads_before = x3m_win32_standin::counter_reads;
    check(simulate_gap_frame(2) == 2477);
    // Six reads for the two draws (the first forwards a native draw), five for
    // the Present hook and the frame boundary, none at all for the state call.
    check(x3m_win32_standin::counter_reads - gap_reads_before == 11);
    check(bucket_ticks[unsigned(Bucket::State)] == 0 && bucket_calls[unsigned(Bucket::State)] == 0);
    for (std::uint64_t f = 3; f <= window_frames + 1; ++f) check(simulate_gap_frame(f) == 2477);
    check(logged_count == 5);
    const char* gap_line = last_line("frame_timing frame=");
    check(std::strstr(gap_line, "frame_timing frame=301 frames=300 dt_p50_us=2477") != nullptr);
    // Unstamped state time is not measured, so it stays inside the gaps: 300 us
    // before the first draw less the 10 us Present scope charged to this frame,
    // 500 + 600 us plus the 7 us state call between the draws, 900 us after the
    // last draw plus the 10 us of Present-hook work before the native Present.
    check(std::strstr(gap_line, "gap_pre_p50_us=290 gap_pre_p95_us=290 gap_pre_max_us=290") != nullptr);
    check(std::strstr(gap_line, "gap_draw_p50_us=1107 gap_draw_p95_us=1107 gap_draw_max_us=1107") != nullptr);
    check(std::strstr(gap_line, "gap_post_p50_us=910 gap_post_p95_us=910 gap_post_max_us=910") != nullptr);
    check(std::strstr(gap_line, "gap_draw_per_draw_us=553.500") != nullptr);
    check(std::strstr(gap_line, "state_p50_us=-1 state_p95_us=-1 state_max_us=-1") != nullptr);
    check(std::strstr(gap_line, "state_calls_p50=1 state_sampled=0") != nullptr);
    check(std::strstr(gap_line, "state_top=set_texture:1 state_other_p50=0") != nullptr);
    check(std::strstr(gap_line, "draw_p50_us=60 draw_p95_us=60 draw_max_us=60") != nullptr);
    check(std::strstr(gap_line, "scene_p50_us=10 scene_p95_us=10 scene_max_us=10") != nullptr);
    check(std::strstr(gap_line, "present_p50_us=100") != nullptr);
    // The three gaps and the measured hooked time sum to dt exactly: no gap
    // clamped on this frame.
    check(290 + 1107 + 910 + 60 + 10 + 100 == 2477);
    const char* gap_witness = last_line("frame_timing_slow frame=");
    check(std::strstr(gap_witness, "state_us=-1 ") != nullptr);
    check(std::strstr(gap_witness, "gap_pre_us=290 gap_draw_us=1107 gap_post_us=910") != nullptr);

    // Sampling: every fourth state call is stamped and the sum is scaled by
    // four. Eight 10 us calls read the clock four times (two stamped calls).
    x3m_win32_standin::environment_stamps = L"4";
    logged_count = 0;
    initialize();
    check(state_stamps == 4);
    frame(0, 0); // only starts the interval
    const unsigned sampled_reads = x3m_win32_standin::counter_reads;
    for (unsigned c = 0; c < 8; ++c) {
        Scope state_scope(Bucket::State, "set_texture");
        x3m_win32_standin::advance(10);
    }
    check(x3m_win32_standin::counter_reads - sampled_reads == 4);
    check(bucket_ticks[unsigned(Bucket::State)] == 20 && bucket_calls[unsigned(Bucket::State)] == 8);
    frame(1, 0);
    Summary sampled;
    check(window.close(sampled));
    check(sampled.frames == 1 && sampled.dt_p50 == 80);
    check(sampled.bucket_p50[unsigned(Bucket::State)] == 80); // 20 us stamped, scaled by 4
    check(sampled.bucket_calls_p50[unsigned(Bucket::State)] == 8);
    // The sampled ticks enter the hooked total scaled by four as well, so the
    // gaps and the buckets still sum to dt: no unhooked time is left over here,
    // and there is no draw, so all three gaps are zero.
    check(sampled.gap_p50[0] == 0 && sampled.gap_p50[1] == 0 && sampled.gap_p50[2] == 0);
    check(sampled.bucket_p50[unsigned(Bucket::State)] + sampled.gap_p50[0] + sampled.gap_p50[1]
          + sampled.gap_p50[2] == sampled.dt_p50);
    check(sampled.state_top_used == 1 && sampled.state_top[0].calls == 8);
    check(logged_count == 0); // one frame is not a window
    // The same at N=4 with draws and unhooked time around them: four state
    // calls of 25 us each (one stamped, estimated as 4 x 25 us), 200 us before
    // the first draw, 300 us between the draws and 400 us after the last.
    frame(2, 0); // restart the interval after the closed window
    {
        x3m_win32_standin::advance(200);
        { Scope draw_scope(Bucket::Draw, "draw_primitive"); x3m_win32_standin::advance(40); }
        for (unsigned c = 0; c < 4; ++c) {
            Scope state_scope(Bucket::State, "set_render_state");
            x3m_win32_standin::advance(25);
        }
        x3m_win32_standin::advance(300);
        { Scope draw_scope(Bucket::Draw, "draw_primitive"); x3m_win32_standin::advance(60); }
        x3m_win32_standin::advance(400);
    }
    frame(3, 2);
    Summary spread;
    check(window.close(spread));
    check(spread.dt_p50 == 1100 && spread.draws_p50 == 2);
    check(spread.bucket_p50[unsigned(Bucket::Draw)] == 100);
    check(spread.bucket_p50[unsigned(Bucket::State)] == 100); // 25 us stamped, scaled by 4
    check(spread.gap_p50[0] == 200 && spread.gap_p50[1] == 300 && spread.gap_p50[2] == 400);
    check(spread.bucket_p50[unsigned(Bucket::Draw)] + spread.bucket_p50[unsigned(Bucket::State)]
          + spread.gap_p50[0] + spread.gap_p50[1] + spread.gap_p50[2] == spread.dt_p50);

    // Per-entry counting with no stamps: distinct static entry names, counted
    // once each, reentrant calls excluded, reported most-called first.
    static const char texture[] = "set_texture";
    static const char render[] = "set_render_state";
    static const char sampler[] = "set_sampler_state";
    x3m_win32_standin::environment_stamps = nullptr;
    initialize();
    frame(0, 0);
    const unsigned mix_reads = x3m_win32_standin::counter_reads;
    for (unsigned c = 0; c < 8; ++c) { Scope scope(Bucket::State, texture); x3m_win32_standin::advance(1); }
    for (unsigned c = 0; c < 3; ++c) { Scope scope(Bucket::State, render); x3m_win32_standin::advance(1); }
    for (unsigned c = 0; c < 5; ++c) { Scope scope(Bucket::State, sampler); x3m_win32_standin::advance(1); }
    for (unsigned c = 0; c < 2; ++c) {
        Scope outer(Bucket::State, texture);
        Scope reentered(Bucket::State, sampler); // the proxy's own call: not counted
        x3m_win32_standin::advance(1);
    }
    check(x3m_win32_standin::counter_reads == mix_reads); // not one clock read
    frame(1, 0);
    Summary mix;
    check(window.close(mix));
    check(mix.bucket_calls_p50[unsigned(Bucket::State)] == 18);
    check(mix.bucket_p50[unsigned(Bucket::State)] == unknown_us); // nothing stamped
    check(mix.state_top_used == 3 && mix.state_other_p50 == 0);
    check(mix.state_top[0].calls == 10 && mix.state_top[1].calls == 5 && mix.state_top[2].calls == 3);
    check(!std::strcmp(state_entry_name[mix.state_top[0].slot], texture));
    check(!std::strcmp(state_entry_name[mix.state_top[1].slot], sampler));
    check(!std::strcmp(state_entry_name[mix.state_top[2].slot], render));

    std::printf("frame_timing_host checks=%u failures=0\n", checks);
    // Optional, never part of the test: the added wall time per hooked call,
    // the production scope with the stand-in reading the host monotonic clock
    // in place of QueryPerformanceCounter.
    if (argc > 1 && !std::strcmp(argv[1], "--cost")) {
        cost_per_call(false); cost_per_call(true); // warm up
        const double off = cost_per_call(false);
        state_stamps = 0; // the default: the state call is counted, not stamped
        const double counted = cost_per_call(true);
        state_stamps = 1; // every state call stamped
        const double stamped = cost_per_call(true);
        state_stamps = 0;
        std::printf("frame_timing_cost off_ns_per_call=%.2f counted_ns_per_call=%.2f stamped_ns_per_call=%.2f"
                    " added_counted_ns=%.2f added_stamped_ns=%.2f added_us_at_30000_counted_calls=%.1f\n",
                    off, counted, stamped, counted - off, stamped - off, (counted - off) * 30.0);
    }
    return 0;
}
