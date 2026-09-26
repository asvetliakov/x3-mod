#include "../../src/ownership/application_admission.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace x3m::ownership;
namespace {
std::atomic<unsigned> checks{0}, failures{0};
void check(bool value, const char* label) {
    ++checks;
    if (!value) {
        ++failures;
        std::fprintf(stderr, "FAIL %s\n", label);
    }
}
template <class Predicate> bool until(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::yield();
    }
    return true;
}
void balanced(const AdmissionMonitor& monitor) {
    const auto state = monitor.snapshot();
    check(!state.active_roots && !state.waiting_roots && !state.replay_active, "balanced monitor");
}
void basic() {
    AdmissionMonitor monitor;
    {
        ApplicationAdmission root(monitor);
        check(root.admitted(), "outer admitted");
        check(monitor.snapshot().active_roots == 1, "one root");
        {
            ApplicationAdmission nested(monitor);
            check(nested.admitted(), "nested admitted");
            check(monitor.snapshot().active_roots == 1, "nested does not count another root");
            ReplayAdmission nested_attempt(nested), outer_attempt(root);
            check(nested_attempt.result() == AdmissionResult::NestedBoundary, "nested cannot promote");
            check(outer_attempt.result() == AdmissionResult::NestedBoundary, "outer cannot promote across nested work");
        }
        {
            ReplayAdmission replay(root);
            check(replay.admitted(), "sole root promoted");
            check(monitor.snapshot().replay_active, "published exclusivity");
            ReplayAdmission duplicate(root);
            check(duplicate.result() == AdmissionResult::ReplayAlreadyActive, "second replay refused");
            check(replay.finish() && replay.finish(), "explicit replay finish idempotent");
        }
        check(root.finish() && root.finish(), "explicit application finish idempotent");
        ReplayAdmission stale(root);
        check(stale.result() == AdmissionResult::InactiveBoundary, "retired boundary refuses");
        // Models a final child's parent dispatch after the child ticket ends.
        ApplicationAdmission parent(monitor);
        ReplayAdmission parent_replay(parent);
        check(parent_replay.admitted(), "new parent root may promote after handoff");
    }
    check(monitor.snapshot().admitted_roots == 2 && monitor.snapshot().promotions == 2, "root and promotion counts");
    balanced(monitor);
    std::puts("CASE basic PASS");
}
void waiting() {
    for (unsigned operation = 0; operation < 6; ++operation) {
        AdmissionMonitor monitor;
        std::atomic<bool> native_dispatch{false};
        {
            ApplicationAdmission root(monitor);
            ReplayAdmission replay(root);
            check(replay.admitted(), "wait test replay promoted");
            std::thread worker([&] {
                ApplicationAdmission entry(monitor);
                check(entry.admitted(), "waited application admitted");
                // Models a native mutator and result publication within entry.
                native_dispatch.store(true, std::memory_order_release);
            });
            check(until([&] { return monitor.snapshot().waiting_roots == 1; }), "worker reaches monitor wait");
            check(!native_dispatch.load(std::memory_order_acquire), "no native dispatch during replay");
            replay.finish();
            {
                ReplayAdmission retry(root);
                check(!retry.admitted() || native_dispatch.load(std::memory_order_acquire),
                      "re-promotion cannot overtake a waiting application");
            }
            worker.join();
            check(native_dispatch.load(std::memory_order_acquire), "dispatch resumes after restoration boundary");
            check(monitor.snapshot().active_roots == 1, "worker publication precedes root retirement");
        }
        balanced(monitor);
    }
    std::puts("CASE waiting PASS");
}
void existing_roots() {
    AdmissionMonitor monitor;
    std::atomic<bool> release{false};
    {
        ApplicationAdmission root(monitor);
        std::vector<std::thread> workers;
        for (unsigned n = 0; n < 8; ++n)
            workers.emplace_back([&] {
                ApplicationAdmission call(monitor);
                check(call.admitted(), "parallel ordinary root");
                while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
            });
        check(until([&] { return monitor.snapshot().active_roots == 9; }), "all ordinary roots entered");
        ReplayAdmission denied(root);
        check(denied.result() == AdmissionResult::OtherApplications, "promotion refuses existing work without waiting");
        check(!monitor.snapshot().waiting_roots, "ordinary calls do not wait on each other");
        release.store(true, std::memory_order_release);
        for (auto& worker : workers) worker.join();
        ReplayAdmission allowed(root);
        check(allowed.admitted(), "promotion after outstanding work publishes");
    }
    balanced(monitor);
    std::puts("CASE existing_roots PASS");
}
void vetoes() {
    AdmissionMonitor monitor;
    {
        ApplicationAdmission root(monitor);
        monitor.veto(AdmissionVeto::PrivateUnknown);
        monitor.veto(AdmissionVeto::UnobservedRoute);
        ReplayAdmission denied(root);
        check(denied.result() == AdmissionResult::Vetoed, "callback veto prevents promotion");
        ApplicationAdmission nested(monitor);
        check(nested.admitted(), "veto does not reject application calls");
    }
    {
        ApplicationAdmission after_reset(monitor);
        ReplayAdmission denied(after_reset);
        check(denied.result() == AdmissionResult::Vetoed, "veto survives all prior scope retirement");
    }
    const auto state = monitor.snapshot();
    check(state.first_veto == AdmissionVeto::PrivateUnknown && state.vetoes == 3,
          "first reason immutable and all vetoes retained");
    balanced(monitor);
    {
        AdmissionMonitor first, second;
        ApplicationAdmission root(first), wrong(second);
        check(wrong.result() == AdmissionResult::DifferentMonitor, "nested monitor mismatch refuses");
        check(first.snapshot().vetoes && second.snapshot().vetoes, "mismatched monitors both lose coverage");
    }
    std::puts("CASE vetoes PASS");
}
void invariants() {
    AdmissionMonitor monitor;
    {
        ApplicationAdmission root(monitor);
        ReplayAdmission replay(root);
        ApplicationAdmission callback(monitor);
        check(callback.result() == AdmissionResult::SameThreadReentry && !callback.admitted(),
              "same-thread callback diagnosed without blocking or admission");
        check(monitor.snapshot().active_roots == 1 && monitor.snapshot().replay_active,
              "refusal does not retire active replay");
        check(!root.finish(), "root cannot finish before replay");
        replay.finish();
        ReplayAdmission next(root);
        check(next.result() == AdmissionResult::Vetoed, "unexpected callback permanently prevents later promotion");
    }
    balanced(monitor);
    AdmissionMonitor order;
    {
        ApplicationAdmission root(order), nested(order);
        check(!root.finish(), "non-LIFO explicit end rejected");
        check(nested.finish() && root.finish(), "correct end order can still retire scopes");
    }
    balanced(order);
    AdmissionMonitor foreign;
    {
        ApplicationAdmission root(foreign);
        ReplayAdmission replay(root);
        std::thread worker([&] {
            check(!root.finish() && !replay.finish(), "foreign thread cannot retire scopes");
            ReplayAdmission wrong(root);
            check(!wrong.admitted(), "foreign boundary promotion rejected");
        });
        worker.join();
        check(foreign.snapshot().replay_active, "foreign refusal leaves owner token intact");
    }
    balanced(foreign);
    std::puts("CASE invariants PASS");
}
void racing() {
    unsigned promoted = 0;
    for (unsigned round = 0; round < 128; ++round) {
        AdmissionMonitor monitor;
        std::atomic<unsigned> before{0}, after{0};
        {
            ApplicationAdmission root(monitor);
            std::thread worker([&] {
                ApplicationAdmission update(monitor);
                check(update.admitted(), "racing ordinary entry admitted");
                before.store(1, std::memory_order_relaxed);
                std::this_thread::yield();
                after.store(1, std::memory_order_relaxed);
            });
            {
                ReplayAdmission replay(root);
                if (replay.admitted()) {
                    ++promoted;
                    check(before.load(std::memory_order_relaxed) == after.load(std::memory_order_relaxed),
                          "replay cannot observe half publication");
                    const auto value = before.load(std::memory_order_relaxed);
                    std::this_thread::yield();
                    check(value == before.load(std::memory_order_relaxed), "promoted storage stays unchanged");
                } else {
                    check(replay.result() == AdmissionResult::OtherApplications, "race has only existing-work refusal");
                    check(!monitor.snapshot().replay_active, "refused race does not publish exclusivity");
                }
            }
            worker.join();
            check(before == 1 && after == 1, "racing publication eventually completes");
        }
        balanced(monitor);
    }
    // The deterministic waiting/existing tests cover both orderings; stress
    // interleavings deliberately do not require a scheduler-dependent split.
    std::printf("RACES rounds=128 promoted=%u\n", promoted);
    std::puts("CASE racing PASS");
}
void nested_stress() {
    AdmissionMonitor monitor;
    std::vector<std::thread> workers;
    for (unsigned n = 0; n < 4; ++n)
        workers.emplace_back([&] {
            for (unsigned iteration = 0; iteration < 1000; ++iteration) {
                ApplicationAdmission outer(monitor), middle(monitor), inner(monitor);
                check(outer.admitted() && middle.admitted() && inner.admitted(), "concurrent nested scopes admitted");
            }
        });
    for (auto& worker : workers) worker.join();
    balanced(monitor);
    check(monitor.snapshot().admitted_roots == 4000, "nested stress exact root count");
    std::puts("CASE nested_stress PASS");
}
void veto_race() {
    for (unsigned round = 0; round < 32; ++round) {
        AdmissionMonitor monitor;
        {
            ApplicationAdmission root(monitor);
            std::atomic<bool> go{false};
            std::thread announcer([&] {
                while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
                monitor.veto(AdmissionVeto::UnobservedRoute);
            });
            go.store(true, std::memory_order_release);
            ReplayAdmission first(root);
            check(first.admitted() || first.result() == AdmissionResult::Vetoed,
                  "veto/promotion has one serialized decision");
            announcer.join();
            first.finish();
            check(monitor.snapshot().first_veto == AdmissionVeto::UnobservedRoute, "concurrent veto is retained");
            ReplayAdmission second(root);
            check(second.result() == AdmissionResult::Vetoed, "no later promotion after concurrent veto");
        }
        balanced(monitor);
    }
    // This only tests monitor linearization. A native callback registration must
    // additionally hold ordinary admission before announcing and dispatching it.
    std::puts("CASE veto_race PASS");
}
}
int main() {
    basic();
    waiting();
    existing_roots();
    vetoes();
    invariants();
    racing();
    nested_stress();
    veto_race();
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks.load(), failures.load());
    return failures ? 1 : 0;
}
