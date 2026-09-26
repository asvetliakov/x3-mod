// Standalone exercise of the production sampling profiler object, without X3.
// Thread A spins in a frame-pointer function called from a known caller, thread
// B waits on an event inside a known function, thread C spins in code compiled
// without frame pointers (sampling_profiler_fpo.cpp), and the main thread
// allocates, frees, writes a file and logs while sampling runs (deadlock
// check). Function ranges are printed so the runner can check attribution.
// Built with -fno-toplevel-reorder: the *_end markers follow their function.
#include "../../src/proxy/sampling_profiler.h"
#include <tlhelp32.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {
CRITICAL_SECTION log_lock;
}
namespace x3m {
void log(const char* format, ...) {
    EnterCriticalSection(&log_lock);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    putchar('\n');
    fflush(stdout);
    LeaveCriticalSection(&log_lock);
}
}
extern "C" uint32_t fpo_spin(uint32_t deadline_ms);
extern "C" uint32_t fpo_leaf(uint32_t deadline_ms);
extern "C" void fpo_leaf_end();
extern "C" void fpo_spin_end();

static inline uint64_t qpc() {
    LARGE_INTEGER v{};
    QueryPerformanceCounter(&v);
    return uint64_t(v.QuadPart);
}
static uint64_t frequency() {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    return uint64_t(f.QuadPart);
}

__attribute__((noinline)) uint64_t spin_a(uint64_t deadline) {
    uint64_t iterations = 0;
    volatile uint32_t sink = 0;
    while (qpc() < deadline) {
        for (int i = 0; i < 200000; ++i) sink = sink * 1664525u + 1013904223u;
        ++iterations;
    }
    return iterations;
}
__attribute__((noinline)) void spin_a_end() {
    asm volatile("");
}
__attribute__((noinline)) uint64_t caller_a(uint64_t deadline) {
    const uint64_t n = spin_a(deadline);
    asm volatile("" ::"r"(n));
    return n + 1;
}
__attribute__((noinline)) void caller_a_end() {
    asm volatile("");
}
__attribute__((noinline)) DWORD wait_b(HANDLE event) {
    const DWORD r = WaitForSingleObject(event, INFINITE);
    asm volatile("" ::"r"(r));
    return r + 1;
}
__attribute__((noinline)) void wait_b_end() {
    asm volatile("");
}
__attribute__((noinline)) uint64_t caller_c(uint32_t deadline_ms) {
    const uint32_t n = fpo_spin(deadline_ms);
    asm volatile("" ::"r"(n));
    return uint64_t(n) + 1;
}
__attribute__((noinline)) void caller_c_end() {
    asm volatile("");
}

struct Shared {
    uint64_t deadline = 0;
    uint32_t deadline_ms = 0;
    HANDLE event = nullptr;
    uint64_t iterations_a = 0, iterations_c = 0;
    DWORD wait_result = 0;
};
static DWORD WINAPI thread_a(LPVOID p) {
    auto* s = static_cast<Shared*>(p);
    s->iterations_a = caller_a(s->deadline);
    return 0;
}
static DWORD WINAPI thread_b(LPVOID p) {
    auto* s = static_cast<Shared*>(p);
    s->wait_result = wait_b(s->event);
    return 0;
}
static DWORD WINAPI thread_c(LPVOID p) {
    auto* s = static_cast<Shared*>(p);
    s->iterations_c = caller_c(s->deadline_ms);
    return 0;
}

static unsigned long rva(const void* p) {
    return static_cast<unsigned long>(reinterpret_cast<uintptr_t>(p) -
                                      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)));
}
template <typename F, typename E> static void range(const char* name, F function, E end) {
    x3m::log("FIXTURE_FUNCTION name=%s rva_begin=0x%lx rva_end=0x%lx", name,
             rva(reinterpret_cast<const void*>(function)), rva(reinterpret_cast<const void*>(end)));
}
static unsigned thread_count() {
    unsigned n = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 e{};
    e.dwSize = sizeof e;
    const DWORD pid = GetCurrentProcessId();
    for (BOOL more = Thread32First(snapshot, &e); more; more = Thread32Next(snapshot, &e))
        n += e.th32OwnerProcessID == pid;
    CloseHandle(snapshot);
    return n;
}

int main() {
    InitializeCriticalSection(&log_lock);
    wchar_t setting[8]{};
    const bool requested = GetEnvironmentVariableW(L"X3M_PROFILE", setting, 8) == 1 && setting[0] == L'1';
    const uint64_t f = frequency();
    x3m::log("FIXTURE_BEGIN profile=%u pid=%lu tid=%lu qpc_frequency=%llu main_base=0x%08lx", requested,
             GetCurrentProcessId(), GetCurrentThreadId(), static_cast<unsigned long long>(f),
             static_cast<unsigned long>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))));
    range("spin_a", spin_a, spin_a_end);
    range("caller_a", caller_a, caller_a_end);
    range("wait_b", wait_b, wait_b_end);
    range("caller_c", caller_c, caller_c_end);
    range("fpo_leaf", fpo_leaf, fpo_leaf_end);
    range("fpo_spin", fpo_spin, fpo_spin_end);
    DWORD handles_before = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handles_before);
    const unsigned threads_before = thread_count();
    const uint64_t start = qpc();
    const bool active = x3m::sampling_profiler::initialize();
    x3m::log("FIXTURE_PROFILER requested=%u active=%u", requested, active);
    if (active != requested) {
        x3m::log("FIXTURE_END exit=2 reason=activation_mismatch");
        return 2;
    }
    Shared shared;
    shared.deadline = qpc() + f * 3;
    shared.deadline_ms = GetTickCount() + 3000;
    shared.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD tids[3]{};
    HANDLE handles[3] = {CreateThread(nullptr, 0, thread_a, &shared, 0, &tids[0]),
                         CreateThread(nullptr, 0, thread_b, &shared, 0, &tids[1]),
                         CreateThread(nullptr, 0, thread_c, &shared, 0, &tids[2])};
    x3m::log("FIXTURE_THREAD name=a tid=%lu", tids[0]);
    x3m::log("FIXTURE_THREAD name=b tid=%lu", tids[1]);
    x3m::log("FIXTURE_THREAD name=c tid=%lu", tids[2]);
    // Main thread: heap churn, file writes and log output under sampling.
    char temp[MAX_PATH]{};
    GetTempPathA(MAX_PATH, temp);
    strcat(temp, "x3-sampling-profiler-fixture.tmp");
    HANDLE file = CreateFileA(temp, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    void* live[64]{};
    unsigned next = 0;
    uint64_t main_iterations = 0, bytes_written = 0;
    uint32_t seed = 12345;
    uint64_t last_progress = qpc();
    while (qpc() < shared.deadline) {
        seed = seed * 1664525u + 1013904223u;
        const size_t size = 16 + (seed >> 8) % 4096;
        if (live[next]) HeapFree(GetProcessHeap(), 0, live[next]);
        live[next] = HeapAlloc(GetProcessHeap(), 0, size);
        if (live[next]) memset(live[next], int(seed & 0xff), size);
        next = (next + 1) % 64;
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(file, live[next ? next - 1 : 63], DWORD(size), &written, nullptr);
            bytes_written += written;
        }
        ++main_iterations;
        if (qpc() - last_progress >= f / 2) {
            last_progress = qpc();
            x3m::log("FIXTURE_PROGRESS main_iterations=%llu bytes=%llu",
                     static_cast<unsigned long long>(main_iterations), static_cast<unsigned long long>(bytes_written));
        }
    }
    for (auto* p : live)
        if (p) HeapFree(GetProcessHeap(), 0, p);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    SetEvent(shared.event);
    const DWORD joined = WaitForMultipleObjects(3, handles, TRUE, 15000);
    for (auto h : handles) CloseHandle(h);
    CloseHandle(shared.event);
    const auto status = x3m::sampling_profiler::status();
    x3m::log(
        "FIXTURE_STATUS joined=%lu ticks=%llu samples=%llu dropped=%llu reports=%llu threads=%u modules=%u unsampled=%u tick_us_mean=%.3f tick_us_max=%.3f report_us=%.3f refresh_us=%.3f",
        joined, static_cast<unsigned long long>(status.ticks), static_cast<unsigned long long>(status.samples),
        static_cast<unsigned long long>(status.dropped), static_cast<unsigned long long>(status.reports),
        status.threads, status.modules, status.threads_unsampled,
        status.ticks ? double(status.tick_ticks) * 1e6 / double(f) / double(status.ticks) : 0.0,
        double(status.tick_max_ticks) * 1e6 / double(f), double(status.report_ticks) * 1e6 / double(f),
        double(status.refresh_ticks) * 1e6 / double(f));
    const uint64_t before_stop = qpc();
    x3m::sampling_profiler::shutdown();
    const uint64_t stop_us = (qpc() - before_stop) * 1000000 / f;
    DWORD handles_after = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handles_after);
    const unsigned threads_after = thread_count();
    const uint64_t elapsed_ms = (qpc() - start) * 1000 / f;
    x3m::log(
        "FIXTURE_END exit=%u elapsed_ms=%llu iterations_a=%llu iterations_c=%llu main_iterations=%llu wait_result=%lu handles_before=%lu handles_after=%lu threads_before=%u threads_after=%u active_after=%u stop_us=%llu",
        joined == WAIT_OBJECT_0 ? 0u : 1u, static_cast<unsigned long long>(elapsed_ms),
        static_cast<unsigned long long>(shared.iterations_a), static_cast<unsigned long long>(shared.iterations_c),
        static_cast<unsigned long long>(main_iterations), shared.wait_result, handles_before, handles_after,
        threads_before, threads_after, x3m::sampling_profiler::active(), static_cast<unsigned long long>(stop_us));
    DeleteCriticalSection(&log_lock);
    return joined == WAIT_OBJECT_0 ? 0 : 1;
}
