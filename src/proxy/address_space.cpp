#include "address_space.h"
#include "address_space_core.h"
#include "capture.h"
#include <windows.h>

namespace x3m::address_space {
namespace {
std::uint64_t now() {
    LARGE_INTEGER v{};
    return QueryPerformanceCounter(&v) && v.QuadPart > 0 ? std::uint64_t(v.QuadPart) : 0;
}
std::uint64_t frequency() {
    LARGE_INTEGER f{};
    return QueryPerformanceFrequency(&f) && f.QuadPart > 0 ? std::uint64_t(f.QuadPart) : 0;
}
enum class Stop { done, budget, refused };
// Walks from `next` into t until the end of the range, a VirtualQuery refusal, or the budget: the elapsed
// time is checked every 64 regions of this call against budget_ticks, or, when QPC gave no frequency
// (budget_ticks 0), the call stops after region_limit regions. At least one region per call. VirtualQuery
// reports the region from the page holding `next`, so a resumed walk continues the partition exactly.
Stop walk(core::Totals& t, std::uint64_t& next, std::uint64_t started, std::uint64_t budget_ticks,
          unsigned region_limit) {
    unsigned walked = 0;
    for (;;) {
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery(reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(next)), &info, sizeof info) != sizeof info)
            return Stop::refused;
        next = core::add(t, reinterpret_cast<std::uintptr_t>(info.BaseAddress), info.RegionSize, info.State, info.Type);
        if (!next) return t.span == core::walk_end ? Stop::done : Stop::refused;
        ++walked;
        if (budget_ticks ? (walked & 63u) == 0 && now() - started > budget_ticks : walked >= region_limit)
            return Stop::budget;
    }
}
void emit(const char* when, unsigned long long device, unsigned long long frame, const core::Totals& t, bool capped,
          unsigned long long ticks, unsigned long long us) {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof status;
    if (!GlobalMemoryStatusEx(&status)) status.ullTotalVirtual = status.ullAvailVirtual = 0;
    log("address_space device=%llu frame=%llu when=%s total_virtual=%llu avail_virtual=%llu span=%llu regions=%llu "
        "free_total=%llu free_largest=%llu reserved=%llu committed_private=%llu committed_mapped=%llu "
        "committed_image=%llu committed_other=%llu chunk16_count=%llu chunk16_bytes=%llu big_private_count=%llu "
        "big_private_bytes=%llu capped=%u ticks=%llu us=%llu",
        device, frame, when, static_cast<unsigned long long>(status.ullTotalVirtual),
        static_cast<unsigned long long>(status.ullAvailVirtual), static_cast<unsigned long long>(t.span),
        static_cast<unsigned long long>(t.regions), static_cast<unsigned long long>(t.free_total),
        static_cast<unsigned long long>(t.free_largest), static_cast<unsigned long long>(t.reserved),
        static_cast<unsigned long long>(t.committed_private), static_cast<unsigned long long>(t.committed_mapped),
        static_cast<unsigned long long>(t.committed_image), static_cast<unsigned long long>(t.committed_other),
        static_cast<unsigned long long>(t.chunk16_count), static_cast<unsigned long long>(t.chunk16_bytes),
        static_cast<unsigned long long>(t.big_private_count), static_cast<unsigned long long>(t.big_private_bytes),
        unsigned(capped), ticks, us);
}
// The Present pass: one walk of the whole range spread over 300-frame ticks (main-loop thread only).
core::Totals pass_{};
std::uint64_t pass_next_ = 0, pass_ticks_ = 0, pass_elapsed_ = 0;
}

void report_create(unsigned long long device, unsigned long long frame) {
    const DWORD error = GetLastError(); // VirtualQuery and GlobalMemoryStatusEx may set it
    const std::uint64_t freq = frequency(), started = now();
    core::Totals t{};
    std::uint64_t next = 0;
    const Stop stop = walk(t, next, started, freq ? std::uint64_t(create_budget_us) * freq / 1000000u : 0,
                           create_region_limit);
    const std::uint64_t elapsed = now() - started;
    emit("create", device, frame, t, stop == Stop::budget, 1, freq ? elapsed * 1000000u / freq : 0);
    SetLastError(error);
}

bool tick(unsigned long long device, unsigned long long frame) {
    const DWORD error = GetLastError();
    const std::uint64_t freq = frequency(), started = now();
    const Stop stop = walk(pass_, pass_next_, started, freq ? std::uint64_t(tick_budget_us) * freq / 1000000u : 0,
                           tick_region_limit);
    pass_elapsed_ += now() - started;
    ++pass_ticks_;
    const bool emitted = stop != Stop::budget;
    if (emitted) {
        // Done, or VirtualQuery refused (span= then says how far the pass got); either way the pass restarts at 0.
        emit("present", device, frame, pass_, false, pass_ticks_, freq ? pass_elapsed_ * 1000000u / freq : 0);
        pass_ = core::Totals{};
        pass_next_ = pass_ticks_ = pass_elapsed_ = 0;
    }
    SetLastError(error);
    return emitted;
}
}
