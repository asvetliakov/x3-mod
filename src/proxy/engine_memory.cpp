#include "engine_memory.h"
#include <windows.h>
#include <atomic>

// This translation unit is compiled with -mno-sse -mno-mmx -mfpmath=387 (CMake
// source property; the fixture build scripts compile it separately): the
// lifetime observer's read path runs inside the game's own map mutations and
// the light D3D hooks, which preserve no XMM state, so nothing here may touch
// an XMM/MMX register (GCC otherwise zeroes and copies the structs below with
// pxor/movups). It contains no floating-point arithmetic, so the x87 fpmath
// setting emits no x87 instruction either (verification/probe/check_no_x87.py).
// The frame epoch is 32-bit for the same reason: a 64-bit atomic load without
// SSE would be an x87 fild/fistp pair.

namespace x3m::engine_memory {
namespace {
constexpr unsigned region_count = 32; // distinct heap/image regions touched per frame are a handful
constexpr DWORD max_age_ms = 100;     // a cached region's own age bound, in time sampled on every read
// Frames count as advancing while the last next_frame() (a Present) is at most
// this old. Past it (a load stall, the game's shutdown) a cached region is
// trusted only for stalled_age_ms after its own validation: one VirtualQuery
// per region per few ms instead of one per read, which the 2.5 M reads of a
// save load's 6 s Present-less window cannot afford (run287). GetTickCount
// advances in steps (about 15.6 ms on Windows), so the effective window is up
// to one step. Only the shutdown signal queries on every read.
constexpr DWORD stall_ms = 250;
constexpr DWORD stalled_age_ms = 5;
enum class Trust { Frame, Stalled, None };
constexpr DWORD readable_protection = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                      PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
struct Region {
    std::uintptr_t begin = 0, end = 0;
    std::uint32_t frame = 0;
    DWORD tick = 0;
};
Region regions[region_count];
unsigned victim = 0;
std::atomic<std::uint32_t> current_frame{1};
std::atomic_flag cache_lock = ATOMIC_FLAG_INIT;
Stats counters;
DWORD frame_tick = 0;     // GetTickCount at the last next_frame(), under cache_lock
bool frames_seen = false; // no next_frame() yet: the region age bound alone applies
// Set by begin_shutdown(), cleared only by next_frame() (a Present; not by
// revalidate()): after an engine teardown the game presents no further frame,
// so at exit it holds to the end.
std::atomic<bool> shutdown_flag{false};
std::atomic<const char*> shutdown_source{nullptr};
struct Guard {
    Guard() {
        while (cache_lock.test_and_set(std::memory_order_acquire)) {}
    }
    ~Guard() { cache_lock.clear(std::memory_order_release); }
};
// The copy itself uses string moves, not the CRT memcpy: it touches no XMM or
// x87 register, so a read leaves the caller's SSE state exactly as it found it,
// which the lifetime fixture's in-mutation probe asserts and the light hooks
// require.
inline void copy_bytes(void* out, const void* in, std::size_t size) {
    __asm__ __volatile__("rep movsb" : "+D"(out), "+S"(in), "+c"(size) : : "memory");
}
// The committed, readable, non-guard region containing address. LastError is
// preserved: a refused query may set it, and the stalled and shutdown paths
// query from inside the game's own calls.
bool query(std::uintptr_t address, Region& out) {
    MEMORY_BASIC_INFORMATION info{};
    const DWORD error = GetLastError();
    const SIZE_T size = VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof info);
    SetLastError(error);
    if (size != sizeof info) return false;
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
        !(info.Protect & readable_protection))
        return false;
    out.begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    out.end = out.begin + info.RegionSize;
    if (out.end < out.begin) out.end = UINTPTR_MAX;
    return out.begin <= address && address < out.end;
}
// now is sampled before the lock, so another thread may already have stamped a
// newer tick: a signed wrap-safe difference, where "newer" is not "older".
inline bool older(DWORD now, DWORD stamp, DWORD bound) {
    return static_cast<std::int32_t>(now - stamp) > static_cast<std::int32_t>(bound);
}
// Validates [address, end) piecewise: a cached region validated this epoch and
// within the mode's age (100 ms while frames advance, 5 ms while stalled)
// covers its part; anything else, and every piece under Trust::None, is
// queried and cached.
bool validated(std::uintptr_t address, std::uintptr_t end, std::uint32_t frame, DWORD tick, Trust trust) {
    const DWORD age = trust == Trust::Frame ? max_age_ms : stalled_age_ms;
    while (address < end) {
        Region* hit = nullptr;
        for (auto& r : regions)
            if (r.end && address >= r.begin && address < r.end) {
                hit = &r;
                break;
            }
        if (hit && (trust == Trust::None || hit->frame != frame || older(tick, hit->tick, age))) {
            hit->end = 0;
            hit = nullptr;
        }
        if (!hit) {
            Region fresh{};
            ++counters.queries;
            if (!query(address, fresh)) return false;
            fresh.frame = frame;
            fresh.tick = tick;
            // Drop every stale entry overlapping the fresh region, then fill the
            // first empty slot or evict round robin.
            Region* slot = nullptr;
            for (auto& r : regions) {
                if (r.end && r.begin < fresh.end && fresh.begin < r.end) r.end = 0;
                if (!r.end && !slot) slot = &r;
            }
            if (!slot) {
                slot = &regions[victim];
                victim = (victim + 1) % region_count;
            }
            *slot = fresh;
            hit = slot;
        }
        address = hit->end;
    }
    return true;
}
}
bool read(std::uintptr_t address, void* out, std::size_t size) {
    if (!address || !out || !size || address + size < address) return false;
    const std::uint32_t frame = current_frame.load(std::memory_order_relaxed);
    // Sampled on every read: the age bound checked against a tick refreshed
    // only every 64th read let a freed engine block pass the cache at exit
    // (Run 77, run287/run288).
    const DWORD tick = GetTickCount();
    {
        Guard guard;
        ++counters.reads;
        const bool shutdown = shutdown_flag.load(std::memory_order_relaxed); // stored under the same lock
        const bool stalled = frames_seen && older(tick, frame_tick, stall_ms);
        const Trust trust = shutdown ? Trust::None : stalled ? Trust::Stalled : Trust::Frame;
        if (shutdown)
            ++counters.strict_reads;
        else if (stalled)
            ++counters.stalled_reads;
        if (!validated(address, address + size, frame, tick, trust)) {
            ++counters.rejected;
            if (shutdown)
                ++counters.refused_shutdown;
            else if (stalled)
                ++counters.refused_stalled;
            return false;
        }
    }
    copy_bytes(out, reinterpret_cast<const void*>(address), size);
    return true;
}
void next_frame() {
    const DWORD tick = GetTickCount();
    current_frame.fetch_add(1, std::memory_order_relaxed);
    Guard guard;
    frame_tick = tick;
    frames_seen = true;
    shutdown_flag.store(false, std::memory_order_release);
}
void revalidate() {
    current_frame.fetch_add(1, std::memory_order_relaxed);
}
void begin_shutdown(const char* source) {
    const char* expected = nullptr;
    shutdown_source.compare_exchange_strong(expected, source ? source : "unnamed", std::memory_order_relaxed);
    Guard guard;
    ++counters.shutdown_signals;
    shutdown_flag.store(true, std::memory_order_release);
}
bool shutting_down() {
    return shutdown_flag.load(std::memory_order_acquire);
}
void reset() {
    Guard guard;
    for (auto& r : regions) r = Region{};
    victim = 0;
}
Stats stats() {
    Guard guard;
    Stats result = counters;
    result.frame = current_frame.load(std::memory_order_relaxed);
    result.shutdown_source = shutdown_source.load(std::memory_order_relaxed);
    return result;
}
}
