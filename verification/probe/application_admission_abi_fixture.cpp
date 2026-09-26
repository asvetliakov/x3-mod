#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../../src/ownership/application_admission_abi.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <optional>
#include <thread>
using namespace x3m::ownership;
namespace {
std::atomic<unsigned> checks{0}, failures{0};
unsigned samples = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) ++failures;
    std::printf("CHECK %s %s\n", label, value ? "PASS" : "FAIL");
}
struct State {
    unsigned char x87[108];
    unsigned mxcsr;
    DWORD error;
    State()
        : error(GetLastError()) {
        asm volatile("fnsave %0\n\tfrstor %0\n\tstmxcsr %1" : "=m"(x87), "=m"(mxcsr)::"memory");
    }
    void restore() const {
        asm volatile("frstor %0\n\tldmxcsr %1" ::"m"(x87), "m"(mxcsr) : "memory");
        SetLastError(error);
    }
};
struct RestoreState {
    State state;
    ~RestoreState() { state.restore(); }
};
bool same(const State& a, const State& b) {
    return a.error == b.error && a.mxcsr == b.mxcsr && !std::memcmp(a.x87, b.x87, 108);
}
void seed(bool outgoing = false) {
    const unsigned short cw = outgoing ? 0x0b7f : 0x077f;
    const unsigned mx = outgoing ? 0x5fa0 : 0x3fa1;
    // Test-only x87 arithmetic produces a masked invalid sticky flag plus three
    // live stack values. Production transports these bytes without arithmetic.
    asm volatile("fninit\n\tfldz\n\tfldz\n\tfdivp\n\tfld1\n\tfldpi\n\tfldcw %0\n\tldmxcsr %1" ::"m"(cw), "m"(mx)
                 : "memory");
    SetLastError(outgoing ? 0x55667788 : 0x11223344);
}
template <class Function> void preserved(const char* label, Function function) {
    RestoreState restore;
    seed();
    State before;
    function();
    State after;
    restore.state.restore();
    check(same(before, after), label);
}
template <class Predicate> bool until(Predicate predicate) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > end) return false;
        std::this_thread::yield();
    }
    return true;
}
void balanced(AdmissionMonitor& monitor) {
    auto view = admission_snapshot(&monitor);
    check(!view.active_roots && !view.waiting_roots && !view.replay_active, "monitor balanced");
}
__attribute__((noinline)) HRESULT original_native(unsigned* output) {
    seed(true);
    *output = 0xabcdef12;
    return E_ACCESSDENIED;
}
void disabled() {
    std::optional<ApplicationAdmissionAbi> app;
    preserved("disabled entry preserves complete CPU state", [&] { app.emplace(nullptr); });
    check(!app->requested() && !app->admitted() && app->result() == AdmissionResult::InactiveBoundary,
          "disabled is not admitted monitor coverage");
    ApplicationAdmission* pointer = reinterpret_cast<ApplicationAdmission*>(1);
    preserved("disabled boundary accessor preserves state", [&] { pointer = app->boundary(); });
    check(!pointer, "disabled exposes no boundary");
    std::optional<ReplayAdmissionAbi> replay;
    preserved("disabled replay preserves state", [&] { replay.emplace(*app); });
    check(!replay->admitted() && replay->result() == AdmissionResult::InactiveBoundary, "disabled cannot promote");
    bool finished = false;
    preserved("disabled finish preserves state", [&] { finished = app->finish() && app->finish(); });
    check(finished, "disabled finish idempotent");
    preserved("disabled replay destruction preserves state", [&] { replay.reset(); });
    preserved("disabled destruction preserves state", [&] { app.reset(); });
    preserved("disabled veto preserves state", [] { admission_veto(nullptr, AdmissionVeto::PrivateUnknown); });
    AdmissionSnapshot view;
    preserved("disabled snapshot preserves state", [&] { view = admission_snapshot(nullptr); });
    check(!view.active_roots && !view.vetoes, "disabled snapshot empty");
    std::puts("CASE disabled PASS");
}
void ordinary() {
    AdmissionMonitor monitor;
    std::optional<ApplicationAdmissionAbi> app;
    preserved("outer entry preserves live x87 sticky controls MXCSR LastError", [&] { app.emplace(&monitor); });
    check(app->requested() && app->admitted() && app->result() == AdmissionResult::Admitted,
          "outer admission succeeds");
    ApplicationAdmission* pointer = nullptr;
    preserved("active boundary accessor preserves state", [&] { pointer = app->boundary(); });
    check(pointer, "active boundary available");
    AdmissionSnapshot view;
    preserved("snapshot preserves state", [&] { view = admission_snapshot(&monitor); });
    check(view.active_roots == 1, "one active root");
    {
        RestoreState restore;
        seed();
        State incoming;
        unsigned output = 0;
        const HRESULT hr = original_native(&output);
        State native;
        const bool done = app->finish();
        State after;
        restore.state.restore();
        check(!same(incoming, native), "native simulation changes incoming CPU state");
        check(done && same(native, after) && hr == E_ACCESSDENIED && output == 0xabcdef12,
              "finish preserves original native outgoing state HRESULT and output");
    }
    check(!app->admitted() && !app->boundary() && app->result() == AdmissionResult::Admitted,
          "finished boundary inactive while constructor result retained");
    bool done = false;
    preserved("double finish preserves current state", [&] { done = app->finish(); });
    check(done, "double finish succeeds");
    preserved("finished adapter destruction preserves state", [&] { app.reset(); });
    preserved("second entry preserves state", [&] { app.emplace(&monitor); });
    {
        RestoreState restore;
        unsigned output = 0;
        const HRESULT hr = original_native(&output);
        State native;
        app.reset();
        State after;
        restore.state.restore();
        check(same(native, after) && hr == E_ACCESSDENIED && output == 0xabcdef12,
              "destructor independently preserves native outgoing state");
    }
    balanced(monitor);
    std::puts("CASE ordinary PASS");
}
void handoff() {
    AdmissionMonitor monitor;
    ApplicationAdmissionAbi child(&monitor);
    bool done = false;
    preserved("explicit child finish preserves state", [&] { done = child.finish(); });
    check(done && !child.boundary(), "child handoff retires underlying boundary");
    std::optional<ApplicationAdmissionAbi> parent;
    preserved("parent after child handoff preserves state", [&] { parent.emplace(&monitor); });
    std::optional<ReplayAdmissionAbi> replay;
    preserved("parent replay promotion preserves state", [&] { replay.emplace(parent->boundary()); });
    check(replay->admitted(), "parent promotes after child scope explicitly ends");
    preserved("replay explicit finish preserves state", [&] { done = replay->finish(); });
    check(done, "replay explicit finish succeeds");
    preserved("replay double finish preserves state", [&] { done = replay->finish(); });
    check(done, "replay double finish succeeds");
    preserved("finished replay destruction preserves state", [&] { replay.reset(); });
    preserved("parent finish preserves state", [&] { done = parent->finish(); });
    check(done, "parent finish succeeds");
    parent.reset();
    balanced(monitor);
    std::puts("CASE handoff PASS");
}
void nested_and_order() {
    AdmissionMonitor monitor;
    ApplicationAdmissionAbi outer(&monitor);
    std::optional<ApplicationAdmissionAbi> inner;
    preserved("nested entry preserves state", [&] { inner.emplace(&monitor); });
    check(inner->admitted() && admission_snapshot(&monitor).active_roots == 1, "nested shares one ordinary root");
    std::optional<ReplayAdmissionAbi> attempt;
    preserved("nested promotion refusal preserves state", [&] { attempt.emplace(*inner); });
    check(attempt->result() == AdmissionResult::NestedBoundary && !attempt->admitted(), "nested promotion refused");
    preserved("refused replay destruction preserves state", [&] { attempt.reset(); });
    preserved("outer promotion across inner refusal preserves state", [&] { attempt.emplace(outer); });
    check(attempt->result() == AdmissionResult::NestedBoundary, "outer cannot bypass nested scope");
    attempt.reset();
    auto* before = outer.boundary();
    bool done = true;
    preserved("out of order finish refusal preserves state", [&] { done = outer.finish(); });
    check(!done && outer.admitted() && outer.boundary() == before,
          "failed finish retains active storage and boundary pointer");
    preserved("inner finish preserves state", [&] { done = inner->finish(); });
    check(done, "inner finishes before outer");
    inner.reset();
    preserved("ordered retry finish preserves state", [&] { done = outer.finish(); });
    check(done, "corrected order finishes outer");
    balanced(monitor);
    std::puts("CASE nested_order PASS");
}
void vetoes() {
    AdmissionMonitor monitor;
    ApplicationAdmissionAbi app(&monitor);
    preserved("permanent veto bookkeeping preserves state",
              [&] { admission_veto(&monitor, AdmissionVeto::PrivateUnknown); });
    preserved("empty veto fast path preserves state", [&] { admission_veto(&monitor, AdmissionVeto::None); });
    auto view = admission_snapshot(&monitor);
    check(view.vetoes == 1 && view.first_veto == AdmissionVeto::PrivateUnknown && app.admitted(),
          "veto leaves ordinary work active");
    std::optional<ReplayAdmissionAbi> replay;
    preserved("vetoed promotion preserves state", [&] { replay.emplace(app); });
    check(!replay->admitted() && replay->result() == AdmissionResult::Vetoed, "vetoed promotion refused");
    replay.reset();
    {
        RestoreState restore;
        unsigned output = 0;
        const HRESULT hr = original_native(&output);
        State native;
        const bool done = app.finish();
        State after;
        restore.state.restore();
        check(done && same(native, after) && hr == E_ACCESSDENIED && output == 0xabcdef12,
              "veto does not replace ordinary native result");
    }
    balanced(monitor);
    std::puts("CASE vetoes PASS");
}
void replay_refusals() {
    AdmissionMonitor monitor;
    ApplicationAdmissionAbi app(&monitor);
    std::optional<ReplayAdmissionAbi> replay;
    preserved("exclusive replay promotion preserves state", [&] { replay.emplace(app); });
    check(replay->admitted(), "sole root exclusive");
    std::optional<ReplayAdmissionAbi> duplicate;
    preserved("duplicate replay refusal preserves state", [&] { duplicate.emplace(app); });
    check(duplicate->result() == AdmissionResult::ReplayAlreadyActive, "duplicate replay refused");
    duplicate.reset();
    std::optional<ApplicationAdmissionAbi> callback;
    preserved("same thread replay reentry refusal preserves state", [&] { callback.emplace(&monitor); });
    check(!callback->admitted() && callback->result() == AdmissionResult::SameThreadReentry,
          "reentry result retained without native dispatch or invented HRESULT");
    auto view = admission_snapshot(&monitor);
    check(view.replay_active && view.active_roots == 1 && (view.vetoes & 16),
          "refusal leaves active replay and records permanent veto");
    preserved("refused application destruction preserves state", [&] { callback.reset(); });
    bool done = true;
    preserved("application finish during replay refusal preserves state", [&] { done = app.finish(); });
    check(!done && app.boundary(), "active replay keeps application boundary alive");
    preserved("active replay destructor preserves state", [&] { replay.reset(); });
    preserved("application finish after replay preserves state", [&] { done = app.finish(); });
    check(done, "application ends after replay");
    balanced(monitor);
    std::puts("CASE replay_refusals PASS");
}
void different_monitor() {
    AdmissionMonitor first, second;
    ApplicationAdmissionAbi outer(&first);
    std::optional<ApplicationAdmissionAbi> other;
    preserved("different monitor refusal preserves state", [&] { other.emplace(&second); });
    check(!other->admitted() && other->result() == AdmissionResult::DifferentMonitor, "different monitor refused");
    check(admission_snapshot(&first).vetoes == 2 && admission_snapshot(&second).vetoes == 2,
          "both monitors retain route veto");
    preserved("different monitor refused destruction preserves state", [&] { other.reset(); });
    outer.finish();
    balanced(first);
    balanced(second);
    std::puts("CASE different_monitor PASS");
}
void foreign_finish() {
    AdmissionMonitor monitor;
    ApplicationAdmissionAbi app(&monitor);
    ReplayAdmissionAbi replay(app);
    std::thread worker([&] {
        bool app_done = true, replay_done = true;
        ApplicationAdmission* pointer = reinterpret_cast<ApplicationAdmission*>(1);
        preserved("foreign application finish refusal preserves state", [&] { app_done = app.finish(); });
        preserved("foreign replay finish refusal preserves state", [&] { replay_done = replay.finish(); });
        preserved("foreign boundary refusal preserves state", [&] { pointer = app.boundary(); });
        check(!app_done && !replay_done && !pointer, "foreign thread cannot mutate or borrow active storage");
    });
    worker.join();
    check(app.admitted() && replay.admitted(), "foreign refusals retain owner scopes");
    replay.finish();
    app.finish();
    balanced(monitor);
    std::puts("CASE foreign_finish PASS");
}
void waiting() {
    AdmissionMonitor monitor;
    ApplicationAdmissionAbi owner(&monitor);
    ReplayAdmissionAbi replay(owner);
    std::atomic<bool> native_called{false};
    std::thread worker([&] {
        RestoreState restore;
        seed();
        State incoming;
        ApplicationAdmissionAbi app(&monitor);
        State entered;
        const bool preserved_entry = same(incoming, entered);
        unsigned output = 0;
        const HRESULT hr = original_native(&output);
        State native;
        native_called.store(true, std::memory_order_release);
        const bool done = app.finish();
        State after;
        restore.state.restore();
        check(preserved_entry, "waiting application entry restores its pre-wait full CPU state");
        check(done && same(native, after) && hr == E_ACCESSDENIED && output == 0xabcdef12,
              "waited application forwards native result with independent finish state");
    });
    const bool waited = until([&] { return admission_snapshot(&monitor).waiting_roots == 1; });
    check(waited && !native_called.load(std::memory_order_acquire), "worker waits before simulated native dispatch");
    bool done = false;
    preserved("replay finish wakes waiter without CPU drift", [&] { done = replay.finish(); });
    check(done, "replay waiter wake succeeds");
    worker.join();
    check(native_called.load(std::memory_order_acquire), "worker dispatches after replay interval ends");
    owner.finish();
    balanced(monitor);
    std::puts("CASE waiting PASS");
}
void existing_roots() {
    AdmissionMonitor monitor;
    ApplicationAdmissionAbi owner(&monitor);
    std::atomic<bool> ready{false}, leave{false};
    std::thread worker([&] {
        ApplicationAdmissionAbi other(&monitor);
        ready.store(true, std::memory_order_release);
        while (!leave.load(std::memory_order_acquire)) std::this_thread::yield();
    });
    check(until([&] { return ready.load(std::memory_order_acquire); }), "independent ordinary root enters");
    std::optional<ReplayAdmissionAbi> replay;
    preserved("other applications promotion refusal preserves state", [&] { replay.emplace(owner); });
    check(!replay->admitted() && replay->result() == AdmissionResult::OtherApplications,
          "active other root blocks promotion");
    replay.reset();
    leave.store(true, std::memory_order_release);
    worker.join();
    owner.finish();
    balanced(monitor);
    std::puts("CASE existing_roots PASS");
}
struct FourByteContext {
    unsigned entry_mod = 0;
    AdmissionMonitor* monitor;
};
extern "C" void admission_abi_four_byte(void (*)(void*), void*);
asm(".text\n.globl _admission_abi_four_byte\n_admission_abi_four_byte:\n pushl %ebp\n movl %esp,%ebp\n andl $-16,%esp\n subl $4,%esp\n pushl 12(%ebp)\n movl %esp,%eax\n subl $4,%eax\n andl $15,%eax\n movl 12(%ebp),%edx\n movl %eax,(%edx)\n call *8(%ebp)\n movl %ebp,%esp\n popl %ebp\n ret\n");
void four_byte_callback(void* raw) {
    auto& context = *static_cast<FourByteContext*>(raw);
    ApplicationAdmissionAbi app(context.monitor);
    ReplayAdmissionAbi replay(app);
    if (!app.admitted() || !replay.admitted()) ++failures;
}
void four_byte() {
    AdmissionMonitor monitor;
    FourByteContext context{0, &monitor};
    preserved("four-byte incoming stack preserves CPU state",
              [&] { admission_abi_four_byte(four_byte_callback, &context); });
    check(context.entry_mod == 4, "thunk invokes callback at four-byte-only stack alignment");
    balanced(monitor);
    std::puts("CASE four_byte PASS");
}
void benchmark() {
    LARGE_INTEGER frequency;
    check(QueryPerformanceFrequency(&frequency) != 0, "benchmark QPC available");
    constexpr unsigned iterations = 100000, warmup = 10000;
    for (unsigned mode = 0; mode < 3; ++mode) {
        AdmissionMonitor monitor;
        std::optional<ApplicationAdmissionAbi> parent;
        if (mode == 2) parent.emplace(&monitor);
        for (unsigned trial = 0; trial < 8; ++trial) {
            const unsigned count = trial ? iterations : warmup;
            unsigned admitted = 0, requested = 0;
            LARGE_INTEGER before, after;
            QueryPerformanceCounter(&before);
            for (unsigned i = 0; i < count; ++i) {
                ApplicationAdmissionAbi app(mode ? &monitor : nullptr);
                admitted += app.admitted();
                requested += app.requested();
            }
            QueryPerformanceCounter(&after);
            check(admitted == (mode ? count : 0) && requested == (mode ? count : 0), "benchmark route inventory");
            if (trial) {
                ++samples;
                const double ns = 1e9 * double(after.QuadPart - before.QuadPart) / double(frequency.QuadPart) / count;
                std::printf("SAMPLE mode=%s trial=%u iterations=%u ns_per_entry=%.3f\n",
                            mode == 0   ? "disabled"
                            : mode == 1 ? "outer"
                                        : "nested",
                            trial, count, ns);
            }
        }
        parent.reset();
        balanced(monitor);
    }
}
}
int main() {
    disabled();
    ordinary();
    handoff();
    nested_and_order();
    vetoes();
    replay_refusals();
    different_monitor();
    foreign_finish();
    waiting();
    existing_roots();
    four_byte();
    benchmark();
    std::printf("RESULT %s checks=%u failures=%u samples=%u\n", failures.load() ? "FAIL" : "PASS", checks.load(),
                failures.load(), samples);
    return failures.load() ? 1 : 0;
}
