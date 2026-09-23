// Host driver for src/proxy/engine_memory.cpp (verification/analysis/
// test_engine_memory_shutdown.py): the production translation unit is compiled
// on the host against a mock <windows.h> (VirtualQuery over a synthetic region
// table, a settable GetTickCount, LastError) with its `rep movsb` replaced by
// a counted copy. The arena stays readable on the host, so a "decommitted"
// region is detected by the copy counter and an untouched output sentinel,
// not by a fault. It does not qualify the x86 build, the no-SSE contract or
// Wine; the object-lifetime fixture does.
//
// ENGINE_MEMORY_HOST_BASELINE compiles only the scenarios the pre-fix reader
// (5a9f45f8) can express, to show that it copies from the decommitted region.
#include "engine_memory_under_test_inc.h"
#include <chrono>
#include <cstdio>
#include <cstring>

namespace {
struct MockRegion { std::uintptr_t begin, end; DWORD state, protect; };
alignas(4096) unsigned char arena[4][4096];
MockRegion table[4];
unsigned long long vq_calls = 0, tick_calls = 0, copies = 0;
DWORD now = 0, last_error = 0;
unsigned checks = 0, failures = 0, scenarios = 0;
void check(bool ok, const char* what) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", what); }
}
void setup() {
    for (unsigned i = 0; i < 4; ++i) {
        table[i] = {reinterpret_cast<std::uintptr_t>(arena[i]), reinterpret_cast<std::uintptr_t>(arena[i]) + 4096,
                    MEM_COMMIT, PAGE_READWRITE};
        std::memset(arena[i], 0x40 + i, sizeof arena[i]);
    }
}
void decommit(unsigned i) { table[i].state = MEM_RESERVE; table[i].protect = 0; }
void recommit(unsigned i) { table[i].state = MEM_COMMIT; table[i].protect = PAGE_READWRITE; }
std::uintptr_t at(unsigned region, unsigned offset = 16) { return reinterpret_cast<std::uintptr_t>(arena[region]) + offset; }
namespace em = x3m::engine_memory;
// One read into a sentinel; true only when read() succeeded, copied once and
// delivered the region's bytes; refused reads must leave the sentinel alone.
bool read_ok(std::uintptr_t address, std::size_t size = 4) {
    unsigned char out[64]; std::memset(out, 0xee, sizeof out);
    const auto before = copies;
    const bool result = em::read(address, out, size);
    if (!result) {
        bool untouched = copies == before;
        for (auto b : out) untouched = untouched && b == 0xee;
        check(untouched, "refused read copies nothing");
        return false;
    }
    return copies == before + 1 && !std::memcmp(out, reinterpret_cast<const void*>(address), size);
}
}

SIZE_T VirtualQuery(const void* address, MEMORY_BASIC_INFORMATION* info, SIZE_T length) {
    ++vq_calls;
    const auto a = reinterpret_cast<std::uintptr_t>(address);
    if (length < sizeof *info) { last_error = 24; return 0; }
    for (const auto& r : table)
        if (a >= r.begin && a < r.end) {
            *info = {};
            info->BaseAddress = reinterpret_cast<void*>(r.begin);
            info->RegionSize = r.end - r.begin;
            info->State = r.state;
            info->Protect = r.protect;
            return sizeof *info;
        }
    last_error = 87; // ERROR_INVALID_PARAMETER, as for an address outside the user range
    return 0;
}
DWORD GetTickCount() { ++tick_calls; return now; }
DWORD GetLastError() { return last_error; }
void SetLastError(DWORD value) { last_error = value; }
void host_copy(void* out, const void* in, std::size_t size) { ++copies; std::memcpy(out, in, size); }

int main() {
    setup();
    // Before any frame advance only the region age bound applies (callers
    // outside the route): cached within 100 ms, re-queried after it.
    {
        ++scenarios;
        now = 100;
        const auto q = vq_calls;
        check(read_ok(at(0)), "pre-frame read");
        now = 150; check(read_ok(at(0)), "pre-frame cached read");
        check(vq_calls == q + 1, "pre-frame reads share one query within the age bound");
        now = 50000; check(read_ok(at(0)), "pre-frame read after the age bound");
        check(vq_calls == q + 2, "pre-frame read past the age bound re-queries");
    }
    // (a) Run 77 exit shape: a region validated at the last Present, the frame
    // number stops advancing, a few reads (far fewer than 64) later the block
    // is decommitted ~0.9 s after that Present. The read must be refused.
    {
        ++scenarios;
        now = 100000; em::next_frame();
        const auto q = vq_calls;
        check(read_ok(at(1)), "read in the last presented frame");
        now = 100010; check(read_ok(at(1)), "cached read in the same frame");
        check(vq_calls == q + 1, "one query for the frame");
        now = 100900; decommit(1);
        check(!read_ok(at(1)), "decommitted engine block 0.9 s after the last Present is refused");
#ifndef ENGINE_MEMORY_HOST_BASELINE
        check(em::stats().refused_stalled == 1, "counted as a stalled refusal");
#endif
        recommit(1);
    }
    // (a2) The region age bound runs on time, not every 64th read: 150 ms
    // after validation (inside the stall bound) a decommitted region is
    // re-queried and refused.
    {
        ++scenarios;
        now = 200000; em::next_frame();
        check(read_ok(at(2)), "validated");
        now = 200150; decommit(2);
        check(!read_ok(at(2)), "decommitted region past the 100 ms age bound is refused");
        recommit(2);
    }
#ifndef ENGINE_MEMORY_HOST_BASELINE
    // (b) Hot path: frames advancing every 16 ms, 48 reads over 3 regions per
    // frame. Exactly one VirtualQuery per distinct region per frame.
    {
        ++scenarios;
        const unsigned frames = 1000, per_frame = 48;
        const auto q = vq_calls; const auto t = tick_calls; const auto s0 = em::stats();
        const auto begin = std::chrono::steady_clock::now();
        bool all = true;
        for (unsigned f = 0; f < frames; ++f) {
            now = 300000 + f * 16; em::next_frame();
            for (unsigned r = 0; r < per_frame; ++r) {
                unsigned char out[12];
                all = em::read(at(r % 3, 32 + (r % 7) * 12), out, sizeof out) && all;
            }
        }
        const auto end = std::chrono::steady_clock::now();
        const auto s1 = em::stats();
        check(all, "hot-path reads succeed");
        check(vq_calls - q == 3ull * frames, "one query per distinct region per frame while frames advance");
        check(s1.queries - s0.queries == 3ull * frames, "reader counts the same queries");
        check(s1.stalled_reads == s0.stalled_reads && s1.strict_reads == s0.strict_reads, "no stalled or strict read while frames advance");
        const double ns = std::chrono::duration<double, std::nano>(end - begin).count() / (frames * per_frame);
        std::printf("HOTPATH frames=%u reads=%u queries=%llu ticks=%llu host_ns_per_read=%.1f\n", frames, frames * per_frame,
                    vq_calls - q, tick_calls - t, ns);
    }
    // (b2) Within one frame the cache survives while the frame is younger
    // than the stall bound; past it a region is trusted for 5 ms after its own
    // validation (one query per region per tick step, not per read).
    {
        ++scenarios;
        now = 400000; em::next_frame();
        const auto q = vq_calls;
        for (unsigned i = 0; i < 10; ++i) { now = 400000 + i * 9; check(read_ok(at(0)), "young frame read"); }
        check(vq_calls == q + 1, "young frame: one query");
        now = 400300;
        const auto st0 = em::stats();
        for (unsigned i = 0; i < 5; ++i) check(read_ok(at(0)), "stalled read of a committed region");
        check(vq_calls == q + 2, "stalled: one query for reads within 5 ms of the region's validation");
        now = 400305; check(read_ok(at(0)), "stalled read 5 ms after validation");
        check(vq_calls == q + 2, "stalled: still trusted at 5 ms");
        now = 400306; check(read_ok(at(0)), "stalled read 6 ms after validation");
        check(vq_calls == q + 3, "stalled: re-queried past 5 ms");
        now = 400320; decommit(0);
        check(!read_ok(at(0)), "stalled: decommitted region past 5 ms refused");
        recommit(0);
        check(em::stats().stalled_reads - st0.stalled_reads == 8, "stalled reads counted");
        now = 400316; em::next_frame();
        const auto q2 = vq_calls;
        for (unsigned i = 0; i < 5; ++i) check(read_ok(at(0)), "resumed read");
        check(vq_calls == q2 + 1, "frames advancing again: cache trusted again");
        // A tick sampled before another thread's next_frame()/validation is
        // older than their stamps: neither stalled nor aged.
        now = 400310;
        check(read_ok(at(0)), "read with a tick older than the stamps");
        check(vq_calls == q2 + 1, "an older sampled tick is not a wrap-around stall");
    }
    // (b3) revalidate() (sector background, fog prefill poll, cull census) is
    // an epoch only: it neither restarts the stall bound nor ends the signal.
    {
        ++scenarios;
        now = 450000; em::next_frame();
        check(read_ok(at(2)), "validated");
        const auto s0 = em::stats();
        now = 450300; em::revalidate();
        check(read_ok(at(2)), "read after revalidate in a stall");
        check(em::stats().stalled_reads == s0.stalled_reads + 1, "revalidate does not restart the stall bound");
        check(em::stats().frame == s0.frame + 1, "revalidate advances the epoch");
        em::begin_shutdown("host_test");
        em::revalidate();
        check(em::shutting_down(), "revalidate does not end the shutdown signal");
        now = 450316; em::next_frame();
        check(!em::shutting_down(), "next_frame ends the shutdown signal");
    }
    // (c) Shutdown signal: every read queries, uncommitted spans are refused,
    // LastError survives a failed query, the next frame advance ends it.
    {
        ++scenarios;
        now = 500000; em::next_frame();
        check(read_ok(at(1)), "pre-shutdown read");
        const auto s0 = em::stats();
        em::begin_shutdown("host_test");
        check(em::shutting_down(), "shutting_down() reports the signal");
        const auto q = vq_calls;
        check(read_ok(at(1)) && read_ok(at(1)), "committed region still readable after the signal");
        check(vq_calls == q + 2, "each read after the signal queries");
        check(em::stats().strict_reads == s0.strict_reads + 2, "strict reads counted");
        decommit(1);
        check(!read_ok(at(1)), "decommitted region refused after the signal");
        check(!read_ok(at(0, 4090), 12), "span into a decommitted neighbour refused");
        last_error = 0x1234;
        check(!read_ok(0x10), "address outside every region refused");
        check(last_error == 0x1234, "LastError preserved across a failed VirtualQuery");
        const auto s1 = em::stats();
        check(s1.refused_shutdown - s0.refused_shutdown == 3, "three shutdown refusals");
        check(s1.shutdown_signals == s0.shutdown_signals + 1 && s1.shutdown_source && !std::strcmp(s1.shutdown_source, "host_test"), "signal source kept");
        em::begin_shutdown("second");
        check(!std::strcmp(em::stats().shutdown_source, "host_test"), "first source kept");
        recommit(1);
        now = 500016; em::next_frame();
        check(!em::shutting_down(), "a frame advance ends the signal");
        const auto q2 = vq_calls;
        check(read_ok(at(1)) && read_ok(at(1)), "reads after the frame advance");
        check(vq_calls == q2 + 1, "cache trusted again after the frame advance");
    }
#endif
    std::printf("engine_memory_host scenarios=%u checks=%u failures=%u\n", scenarios, checks, failures);
    return failures ? 1 : 0;
}
