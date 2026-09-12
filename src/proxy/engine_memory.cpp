#include "engine_memory.h"
#include <windows.h>
#include <atomic>

namespace x3m::engine_memory {
namespace {
constexpr unsigned region_count = 32;   // distinct heap/image regions touched per frame are a handful
constexpr DWORD max_age_ms = 100;       // staleness bound when no frame advance arrives
constexpr unsigned tick_every = 64;     // GetTickCount is itself a Wine dispatch; amortize it
constexpr DWORD readable_protection = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                      PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
struct Region { std::uintptr_t begin = 0, end = 0; std::uint64_t frame = 0; DWORD tick = 0; };
Region regions[region_count];
unsigned victim = 0;
std::atomic<std::uint64_t> current_frame{1};
std::atomic<int> configured{0}; // 0 unknown, 1 direct, 2 rpm
std::atomic_flag cache_lock = ATOMIC_FLAG_INIT;
Stats counters;
DWORD last_tick = 0;
struct Guard {
    Guard() { while (cache_lock.test_and_set(std::memory_order_acquire)) {} }
    ~Guard() { cache_lock.clear(std::memory_order_release); }
};
// The copy itself uses string moves, not the CRT memcpy: it touches no XMM or
// x87 register, so a read leaves the caller's SSE state exactly as the
// ReadProcessMemory path did (its dispatcher saved and restored it), which the
// lifetime fixture's in-mutation probe asserts and the light hooks require.
inline void copy_bytes(void* out, const void* in, std::size_t size) {
    __asm__ __volatile__("rep movsb" : "+D"(out), "+S"(in), "+c"(size) : : "memory");
}
bool rpm(std::uintptr_t address, void* out, std::size_t size) {
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &copied) && copied == size;
}
// The committed, readable, non-guard region containing address.
bool query(std::uintptr_t address, Region& out) {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof info) != sizeof info) return false;
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || !(info.Protect & readable_protection)) return false;
    out.begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    out.end = out.begin + info.RegionSize;
    if (out.end < out.begin) out.end = UINTPTR_MAX;
    return out.begin <= address && address < out.end;
}
// Validates [address, end) piecewise: a cached region validated this frame and
// recently enough covers its part; anything else is queried and cached.
bool validated(std::uintptr_t address, std::uintptr_t end, std::uint64_t frame, DWORD tick) {
    while (address < end) {
        Region* hit = nullptr;
        for (auto& r : regions)
            if (r.end && address >= r.begin && address < r.end) { hit = &r; break; }
        if (hit && (hit->frame != frame || tick - hit->tick > max_age_ms)) { hit->end = 0; hit = nullptr; }
        if (!hit) {
            Region fresh{};
            ++counters.queries;
            if (!query(address, fresh)) { ++counters.rejected; return false; }
            fresh.frame = frame; fresh.tick = tick;
            // Drop every stale entry overlapping the fresh region, then fill the
            // first empty slot or evict round robin.
            Region* slot = nullptr;
            for (auto& r : regions) {
                if (r.end && r.begin < fresh.end && fresh.begin < r.end) r.end = 0;
                if (!r.end && !slot) slot = &r;
            }
            if (!slot) { slot = &regions[victim]; victim = (victim + 1) % region_count; }
            *slot = fresh; hit = slot;
        }
        address = hit->end;
    }
    return true;
}
}
void configure() {
    wchar_t setting[8]{};
    const DWORD length = GetEnvironmentVariableW(L"X3M_ENGINE_READS", setting, 8);
    configured.store(length == 3 && setting[0] == L'r' && setting[1] == L'p' && setting[2] == L'm' ? 2 : 1);
}
Mode mode() {
    if (!configured.load(std::memory_order_acquire)) {
        const DWORD error = GetLastError();
        configure();
        SetLastError(error);
    }
    return configured.load(std::memory_order_relaxed) == 2 ? Mode::ReadProcessMemory : Mode::Direct;
}
bool read(std::uintptr_t address, void* out, std::size_t size) {
    if (!address || !out || !size || address + size < address) return false;
    if (mode() == Mode::ReadProcessMemory) {
        Guard guard; ++counters.syscalls;
        return rpm(address, out, size);
    }
    const std::uint64_t frame = current_frame.load(std::memory_order_relaxed);
    {
        Guard guard;
        if ((++counters.reads % tick_every) == 1) last_tick = GetTickCount();
        if (!validated(address, address + size, frame, last_tick)) return false;
    }
    copy_bytes(out, reinterpret_cast<const void*>(address), size);
    return true;
}
void next_frame() {
    current_frame.fetch_add(1, std::memory_order_relaxed);
    Guard guard; last_tick = GetTickCount();
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
    return result;
}
}
