// Portable restore-ticket core (engine-state arm/pending/bound/due/consume,
// docs/architecture/chase-view-restore-engine-state.md section 4) on
// controlled observations. The X3 CPU fixture drives the same state machine
// through the actual emitted stubs; this host run needs no Wine.
#include "../../src/proxy/chase_transition_restore_core.h"
#include <cstdio>
#include <cstdlib>
using namespace x3m::chase_transition::detail;
static unsigned checks = 0;
static void check(bool ok, const char* why) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", why);
        std::exit(1);
    }
}
// The engine walks the core asks for, under fixture control.
struct Probe {
    bool control = true, live = true, controller_ok = true;
    std::uint32_t handle = 7, ship_id = 0xfffefa78u, controller_id = 0xffff6eafu;
    unsigned walks = 0;
    bool active_control(std::uint32_t& h) {
        ++walks;
        h = handle;
        return control;
    }
    bool ship_live(std::uint32_t, std::uint32_t& id) {
        ++walks;
        id = ship_id;
        return live;
    }
    bool controller(std::uint32_t& id) {
        id = controller_id;
        return controller_ok;
    }
};
constexpr std::uint32_t old_cockpit = 0x1000, new_cockpit = 0x2000, side_cockpit = 0x3000, ship = 0x47e69ad0u,
                        sector_a = 0x265e8e08u, sector_b = 0x80e89c18u, sector54_a = 0x4daa56d0u,
                        sector54_b = 0x9fd981a8u, monitor_id = 0xffff6eaeu,
                        monitor_class = 0x25e, thread = 11;
struct World {
    RestoreState st;
    Probe p;
    std::uint32_t epoch = 1;
    RestoreFrame rear(std::uint32_t cockpit = old_cockpit, std::uint64_t gen = 1) {
        RestoreFrame f;
        f.cockpit = cockpit;
        f.mode = restore_rear_mode;
        f.ref = f.view = ship;
        f.sector = sector_a;
        f.sector54 = sector54_a;
        f.generation = gen;
        f.constructor_caller = restore_alloc_caller;
        f.readable = true;
        return f;
    }
    RestoreFrame fresh(std::uint32_t cockpit = new_cockpit, std::uint64_t gen = 2, std::uint32_t sector = sector_b,
                       std::uint32_t mode = restore_internal_mode) {
        RestoreFrame f = rear(cockpit, gen);
        f.mode = mode;
        f.sector = sector;
        f.sector54 = sector54_b;
        return f;
    }
    bool frame(const RestoreFrame& f) { return st.on_frame(f, p, epoch); }
    // Arm on a rear frame, free through the free command, construct the fresh cockpit, let the script store
    // mode 1 into it (the monitor capture).
    void arm_and_free() {
        check(frame(rear()) && st.phase == phase_armed, "applied rear frame arms");
        st.on_destroy(old_cockpit, 1, restore_free_caller);
        check(st.phase == phase_pending && st.pendings >= 1, "free command transfers to pending");
        st.on_complete(restore_alloc_caller);
        st.on_mode_store(new_cockpit, 2, restore_internal_mode, thread, restore_alloc_caller, monitor_id, monitor_class);
        check(st.phase == phase_pending && st.monitor == monitor_id && st.monitor_cockpit == new_cockpit,
              "StartMonitor's store captures the monitor object for the fresh cockpit");
    }
    void due(std::uint32_t sector = sector_b) {
        check(!frame(fresh(new_cockpit, 2, sector)) || st.phase == phase_pending, "first fresh frame is not due");
        check(frame(fresh(new_cockpit, 2, sector)) && st.phase == phase_due, "second consecutive frame is due");
    }
    RestoreCall inject(std::uint32_t sector = sector_b, std::uint32_t controlled = ship, std::uint32_t pause = 0) {
        return st.on_inject_entry(thread, fresh(new_cockpit, 2, sector), controlled, pause, p, epoch);
    }
};
int main() {
    // 1. The acceptance path: arm, free, construct, capture, due, one call with the captured object, restored.
    {
        World w;
        w.arm_and_free();
        w.due();
        check(w.st.wants_injection(), "due ticket publishes the injection prefilter");
        RestoreCall c = w.inject();
        check(c.wanted && c.monitor == monitor_id && c.monitor_class == monitor_class && w.st.phase == phase_calling &&
                  w.st.attempts == 1 && !w.st.wants_injection(),
              "injection hands out the captured monitor once and leaves due before the call");
        w.st.on_mode_store(new_cockpit, 2, restore_rear_mode, thread, restore_alloc_caller, monitor_id, monitor_class);
        w.st.on_call_result(true);
        const RestoreOutcome o = w.st.take_outcome();
        check(o.wanted && o.result == result_restored && o.cancel == cancel_none && o.attempts == 1 &&
                  o.monitor == monitor_id && o.sector_from == sector_a && o.sector_to == sector_b &&
                  o.ship_id_from == w.p.ship_id && o.ship_id_to == w.p.ship_id && o.generation_from == 1 &&
                  o.generation_to == 2 && w.st.phase == phase_idle && w.st.results[result_restored] == 1,
              "258 stored on the bound cockpit inside the call is restored");
        check(!w.st.take_outcome().wanted, "outcome row is taken once");
        check(!w.inject().wanted, "consumed ticket never calls again");
    }
    // 2. Same sector, different ship (ship change): restored. 3. Different sector, same ship: restored.
    {
        World w;
        w.arm_and_free();
        w.p.ship_id = 0xffff1766u;
        w.due(sector_a);
        RestoreCall c = w.inject(sector_a);
        check(c.wanted, "same sector with a different ship still calls");
        w.st.on_mode_store(new_cockpit, 2, restore_rear_mode, thread, restore_alloc_caller, monitor_id, monitor_class);
        w.st.on_call_result(true);
        const RestoreOutcome o = w.st.take_outcome();
        check(o.result == result_restored && o.ship_id_from == 0xfffefa78u && o.ship_id_to == 0xffff1766u &&
                  o.sector_from == sector_a && o.sector_to == sector_a,
              "ship change in the same sector restores and logs both ship ids");
    }
    {
        World w;
        w.arm_and_free();
        w.due(sector_b);
        check(w.inject(sector_b).wanted, "different sector, same ship calls");
        w.st.on_mode_store(new_cockpit, 2, restore_rear_mode, thread, restore_alloc_caller, monitor_id, monitor_class);
        w.st.on_call_result(true);
        check(w.st.take_outcome().result == result_restored, "different sector, same ship restored");
    }
    // 4. Run78 shape: the fresh cockpit's +0x10 stays 0 for 36 updates; due only after it binds.
    {
        World w;
        w.arm_and_free();
        for (unsigned i = 0; i < 36; ++i) {
            RestoreFrame f = w.fresh();
            f.view = 0;
            check(!w.frame(f) && w.st.phase == phase_pending, "unbound fresh frame never counts");
            w.st.on_update();
        }
        w.due();
        check(w.st.pending_updates == 36 && w.inject().wanted, "binding after 36 updates reaches due and calls");
    }
    // 5. A load-chain constructor cancels (cancel_lifetime). 6. Kind 7 cancels (cancel_load). Epoch/VM cancel.
    {
        World w;
        w.arm_and_free();
        w.st.on_complete(restore_load_constructor_caller);
        check(w.st.phase == phase_idle && w.st.cancels[cancel_lifetime] == 1 &&
                  w.st.take_outcome().cancel == cancel_lifetime,
              "load-chain constructor cancels pending");
    }
    {
        World w;
        w.arm_and_free();
        w.st.on_load();
        check(w.st.phase == phase_idle && w.st.cancels[cancel_load] == 1 && w.st.take_outcome().result == result_cancelled,
              "deserializer write cancels");
        check(w.frame(w.rear()) && w.st.phase == phase_armed, "a later rear frame re-arms");
        w.st.on_load();
        check(w.st.cancels[cancel_load] == 2 && !w.st.take_outcome().wanted, "an armed cancel logs no row");
    }
    {
        World w;
        w.arm_and_free();
        w.epoch = 2;
        check(!w.frame(w.fresh()) && w.st.phase == phase_idle && w.st.cancels[cancel_epoch] == 1,
              "EH adapter epoch bump cancels at the next frame");
        w.arm_and_free();
        w.st.on_vm();
        check(w.st.phase == phase_idle && w.st.cancels[cancel_epoch] == 2, "VM clear/load cancels");
    }
    // 7. Mode left before due: a fresh cockpit shown at another mode, and a key press after binding.
    {
        World w;
        w.arm_and_free();
        check(!w.frame(w.fresh(new_cockpit, 2, sector_b, restore_rear_mode)) && w.st.phase == phase_idle &&
                  w.st.cancels[cancel_mode_left] == 1,
              "fresh cockpit already at 258 cancels (nothing to restore)");
        w.arm_and_free();
        w.frame(w.fresh());
        check(!w.frame(w.fresh(new_cockpit, 2, sector_b, 3)) && w.st.cancels[cancel_mode_left] == 2,
              "another mode before due cancels");
        World v;
        v.arm_and_free();
        v.due();
        check(!v.st.on_inject_entry(thread, v.fresh(new_cockpit, 2, sector_b, 5), ship, 0, v.p, v.epoch).wanted &&
                  v.st.phase == phase_idle && v.st.cancels[cancel_mode_left] == 1,
              "mode changed between due and the injection entry cancels");
        World u;
        check(u.frame(u.rear()), "arm");
        RestoreFrame f = u.rear();
        f.mode = 1;
        check(!u.frame(f) && u.st.phase == phase_idle && u.st.cancels[cancel_mode_left] == 1 &&
                  !u.st.take_outcome().wanted,
              "armed cockpit at mode 1 disarms without a row");
    }
    // 8. No monitor captured for the bound cockpit: cancel_no_monitor at the injection.
    {
        World w;
        check(w.frame(w.rear()), "arm");
        w.st.on_destroy(old_cockpit, 1, restore_free_caller);
        w.st.on_complete(restore_alloc_caller);
        w.due();
        check(!w.inject().wanted && w.st.phase == phase_idle && w.st.cancels[cancel_no_monitor] == 1 &&
                  w.st.take_outcome().cancel == cancel_no_monitor,
              "no captured monitor cancels with no_monitor");
        World v;
        check(v.frame(v.rear()), "arm");
        v.st.on_destroy(old_cockpit, 1, restore_free_caller);
        v.st.on_complete(restore_alloc_caller);
        v.st.on_mode_store(side_cockpit, 3, restore_internal_mode, thread, restore_alloc_caller, 0xffff0001u,
                           monitor_class);
        v.due();
        check(!v.inject().wanted && v.st.cancels[cancel_no_monitor] == 1,
              "a monitor captured for another cockpit does not serve the bound one");
        World x;
        check(x.frame(x.rear()), "arm");
        x.st.on_destroy(old_cockpit, 1, restore_free_caller);
        x.st.on_mode_store(new_cockpit, 2, restore_internal_mode, thread, restore_alloc_caller, 0, 0);
        check(x.st.monitor == 0, "an unreadable VM frame captures nothing");
        x.st.on_mode_store(new_cockpit, 2, restore_internal_mode, thread, 0x41f83bu, monitor_id, monitor_class);
        check(x.st.monitor == 0, "a store into a non-alloc cockpit captures nothing");
    }
    // 9. no_effect: at most `retries` further attempts spaced >= 60 updates, then cancel_retries.
    {
        World w;
        w.st.retries = 2;
        w.arm_and_free();
        w.due();
        for (unsigned attempt = 1; attempt <= 3; ++attempt) {
            RestoreCall c = w.inject();
            check(c.wanted && w.st.attempts == attempt, "attempt made");
            w.st.on_call_result(true); // no store seen
            if (attempt < 3) {
                check(w.st.phase == phase_pending && w.st.results[result_no_effect] == attempt &&
                          !w.st.take_outcome().wanted,
                      "no_effect schedules a retry without a row");
                for (unsigned i = 0; i < 59; ++i) {
                    w.st.on_update();
                    w.frame(w.fresh());
                }
                check(w.st.phase == phase_pending, "frames inside the 60-update spacing do not count");
                w.st.on_update();
                w.due();
            }
        }
        const RestoreOutcome o = w.st.take_outcome();
        check(w.st.phase == phase_idle && o.wanted && o.result == result_no_effect && o.cancel == cancel_retries &&
                  o.attempts == 3 && w.st.cancels[cancel_retries] == 1,
              "the attempt cap cancels with retries after retries+1 presses");
        World z;
        z.st.retries = 0;
        z.arm_and_free();
        z.due();
        check(z.inject().wanted, "retries 0: one attempt");
        z.st.on_call_result(true);
        check(z.st.phase == phase_idle && z.st.take_outcome().cancel == cancel_retries, "retries 0 cancels at once");
    }
    // 10. call_refused (method or object not found: the wrapper returns 0). 11. other_mode is terminal.
    {
        World w;
        w.arm_and_free();
        w.due();
        check(w.inject().wanted, "call");
        w.st.on_call_result(false);
        const RestoreOutcome o = w.st.take_outcome();
        check(o.result == result_call_refused && o.cancel == cancel_call_refused && w.st.phase == phase_idle,
              "a refused engine call cancels call_refused");
        World v;
        v.arm_and_free();
        v.due();
        check(v.inject().wanted, "call");
        v.st.on_mode_store(new_cockpit, 2, 3, thread, restore_alloc_caller, monitor_id, monitor_class);
        v.st.on_call_result(true);
        const RestoreOutcome p = v.st.take_outcome();
        check(p.result == result_other_mode && p.cancel == cancel_other_mode && p.mode_seen == 3 &&
                  v.st.phase == phase_idle,
              "another mode stored by the script ends the ticket without a fight");
        World u;
        u.arm_and_free();
        u.due();
        check(u.inject().wanted, "call");
        u.st.on_mode_store(side_cockpit, 3, restore_rear_mode, thread, restore_alloc_caller, monitor_id, monitor_class);
        u.st.on_mode_store(new_cockpit, 2, restore_rear_mode, 12, restore_alloc_caller, monitor_id, monitor_class);
        u.st.on_call_result(true);
        check(u.st.take_outcome().result == result_no_effect || u.st.phase == phase_pending,
              "stores on other cockpits or threads are not the result");
    }
    // 12. Expiry: 600 updates from pending cancels; the clock does not run while armed.
    {
        World w;
        check(w.frame(w.rear()), "arm");
        for (unsigned i = 0; i < 700; ++i) w.st.on_update();
        check(w.st.phase == phase_armed, "armed tickets do not expire");
        w.st.on_destroy(old_cockpit, 1, restore_free_caller);
        for (unsigned i = 0; i <= restore_pending_update_expiry; ++i) w.st.on_update();
        check(w.st.phase == phase_idle && w.st.cancels[cancel_expiry] == 1 && w.st.take_outcome().cancel == cancel_expiry,
              "bounded update expiry cancels pending");
    }
    // 13. Controller changed: cancel_identity. A dead or unregistered ship, no control: soft (wait).
    {
        World w;
        w.arm_and_free();
        w.p.controller_id = 0xffff0003u;
        check(!w.frame(w.fresh()) && w.st.phase == phase_idle && w.st.cancels[cancel_identity] == 1,
              "a different controller object cancels");
        World v;
        v.arm_and_free();
        v.p.live = false;
        v.frame(v.fresh());
        v.frame(v.fresh());
        check(v.st.phase == phase_pending, "an unregistered ship waits");
        v.p.live = true;
        v.p.control = false;
        v.frame(v.fresh());
        v.frame(v.fresh());
        check(v.st.phase == phase_pending, "a cockpit without the active-control handle waits");
        v.p.control = true;
        v.due();
    }
    // 14. Destructors: armed cockpit by another caller, a second destruction while pending, the bound cockpit.
    {
        World w;
        check(w.frame(w.rear()), "arm");
        w.st.on_destroy(old_cockpit, 1, 0x41ccfbu);
        check(w.st.phase == phase_idle && w.st.cancels[cancel_destructor] == 1, "deleting destructor cancels the arm");
        check(w.frame(w.rear()), "re-arm");
        w.st.on_destroy(side_cockpit, 5, restore_free_caller);
        check(w.st.phase == phase_armed, "another cockpit's free leaves the arm");
        w.st.on_destroy(old_cockpit, 1, restore_free_caller);
        w.st.on_destroy(side_cockpit, 5, 0x41ccfbu);
        check(w.st.phase == phase_idle && w.st.cancels[cancel_second_destruction] == 1,
              "a second destruction while pending (not yet bound) cancels");
        World v;
        v.arm_and_free();
        v.due();
        v.st.on_destroy(new_cockpit, 2, 0x41ccfbu);
        check(v.st.phase == phase_idle && v.st.cancels[cancel_lifetime] == 1, "the bound cockpit's destruction cancels");
        World u;
        u.arm_and_free();
        u.due();
        check(u.inject().wanted, "call");
        u.st.on_destroy(new_cockpit, 2, 0x41ccfbu);
        u.st.on_call_result(true);
        check(u.st.phase == phase_idle && u.st.take_outcome().cancel == cancel_lifetime,
              "a destruction during the call cancels and the call result is ignored");
    }
    // 15. Arm refusals keep their reasons; samples once per reason; re-arming each frame.
    {
        World w;
        RestoreFrame f = w.rear();
        f.connect = 2;
        check(!w.frame(f) && w.st.arm_refusal_reasons[arm_connect] == 1 && w.st.take_arm_sample().reason == arm_connect,
              "connect refuses arming");
        f = w.rear();
        f.view = 0;
        check(!w.frame(f) && w.st.arm_refusal_reasons[arm_view_not_ready] == 1, "view 0 refuses arming");
        f = w.rear();
        f.flags = 4;
        check(!w.frame(f) && w.st.arm_refusal_reasons[arm_flags] == 1, "flag bit 2 refuses arming");
        w.p.control = false;
        check(!w.frame(w.rear()) && w.st.arm_refusal_reasons[arm_no_handle] == 1, "no active handle refuses arming");
        w.p.control = true;
        w.p.live = false;
        check(!w.frame(w.rear()) && w.st.arm_refusal_reasons[arm_ship_dead] == 1 &&
                  w.st.take_arm_sample().reason == arm_ship_dead,
              "unregistered ship refuses arming");
        w.p.live = true;
        w.p.ship_id = 0;
        check(!w.frame(w.rear()) && w.st.arm_refusal_reasons[arm_ship_dead] == 2 && !w.st.take_arm_sample().wanted,
              "ship id 0 refuses arming; the reason is sampled once");
        w.p.ship_id = 0xfffefa78u;
        check(w.frame(w.rear()) && w.frame(w.rear()) && w.st.arms == 1 && w.st.phase == phase_armed,
              "rear frames re-arm each frame and count one arm");
        RestoreFrame g = w.rear(side_cockpit, 9);
        w.p.control = false;
        check(!w.frame(g) && w.st.phase == phase_armed, "another cockpit's 258 frame without control leaves the arm");
        w.p.control = true;
        RestoreFrame h = w.rear();
        h.readable = false;
        check(!w.frame(h) && w.st.phase == phase_idle && w.st.arm_refusal_reasons[arm_unreadable] == 1,
              "an unreadable armed cockpit disarms");
    }
    // 16. Injection skips (pause bit, controlled object mismatch) keep the ticket due; a side cockpit never binds.
    {
        World w;
        w.arm_and_free();
        w.due();
        check(!w.inject(sector_b, ship, 1).wanted && w.st.phase == phase_due && w.st.injections_skipped == 1,
              "pause bit set skips this iteration");
        check(!w.inject(sector_b, 0x5555).wanted && w.st.phase == phase_due && w.st.injections_skipped == 2,
              "another controlled object skips this iteration");
        check(w.inject().wanted, "the next iteration calls");
        World v;
        v.arm_and_free();
        RestoreFrame side = v.fresh(side_cockpit, 3);
        v.p.control = false;
        v.frame(side);
        v.frame(side);
        check(v.st.phase == phase_pending && v.st.bound_cockpit == 0, "a side cockpit from the alloc command never binds");
        v.p.control = true;
        RestoreFrame other = v.fresh(side_cockpit, 3);
        other.constructor_caller = 0;
        v.frame(other);
        v.frame(other);
        check(v.st.phase == phase_pending, "a cockpit not from the alloc command never binds");
        v.due();
    }
    // 17. The fresh cockpit reuses the freed cockpit's address (review 2026-10-12, finding 1): the monitor is
    //     captured for (address, generation 2) and the ticket restores.
    {
        World w;
        check(w.frame(w.rear()), "arm");
        w.st.on_destroy(old_cockpit, 1, restore_free_caller);
        w.st.on_complete(restore_alloc_caller);
        w.st.on_mode_store(old_cockpit, 2, restore_internal_mode, thread, restore_alloc_caller, monitor_id,
                           monitor_class);
        check(w.st.monitor == monitor_id && w.st.monitor_cockpit == old_cockpit && w.st.monitor_generation == 2,
              "a store into the reused address with the new generation captures the monitor");
        w.frame(w.fresh(old_cockpit, 2));
        w.frame(w.fresh(old_cockpit, 2));
        RestoreCall c = w.st.on_inject_entry(thread, w.fresh(old_cockpit, 2), ship, 0, w.p, w.epoch);
        check(c.wanted && c.monitor == monitor_id, "the reused address calls with the captured monitor");
        w.st.on_mode_store(old_cockpit, 2, restore_rear_mode, thread, restore_alloc_caller, monitor_id, monitor_class);
        w.st.on_call_result(true);
        check(w.st.take_outcome().result == result_restored, "reused address restores");
        World v;
        check(v.frame(v.rear()), "arm");
        v.st.on_destroy(old_cockpit, 1, restore_free_caller);
        v.st.on_mode_store(old_cockpit, 1, restore_internal_mode, thread, restore_alloc_caller, monitor_id,
                           monitor_class);
        check(v.st.monitor == 0, "a store attributed to the freed generation captures nothing");
    }
    // 18. A ticket whose call never returned a result (EH unwind) expires from phase_calling.
    {
        World w;
        w.arm_and_free();
        w.due();
        check(w.inject().wanted && w.st.phase == phase_calling, "call in flight");
        for (unsigned i = 0; i <= restore_pending_update_expiry; ++i) w.st.on_update();
        check(w.st.phase == phase_idle && w.st.cancels[cancel_call_unwound] == 1 &&
                  w.st.take_outcome().cancel == cancel_call_unwound,
              "a stalled call expires with call_unwound");
        check(w.frame(w.rear()) && w.st.phase == phase_armed, "the next transit can arm again");
    }
    // 19. The injection entry observes a stale epoch.
    {
        World w;
        w.arm_and_free();
        w.due();
        w.epoch = 2;
        check(!w.inject().wanted && w.st.phase == phase_idle && w.st.cancels[cancel_epoch] == 1,
              "an epoch bump between due and the entry cancels at the entry");
    }
    // 20. Refusals on another cockpit do not cancel the armed ticket; re-arm frames skip the walks.
    {
        World w;
        check(w.frame(w.rear()), "arm");
        const unsigned walks = w.p.walks;
        RestoreFrame other = w.rear(side_cockpit, 9);
        other.connect = 2;
        check(!w.frame(other) && w.st.phase == phase_armed && w.st.arm_refusal_reasons[arm_connect] == 1,
              "a side cockpit's refusal is counted and leaves the arm");
        RestoreFrame again = w.rear();
        again.sector = sector_b;
        check(w.frame(again) && w.st.phase == phase_armed && w.p.walks == walks && w.st.arm_sector == sector_b &&
                  w.st.rearm_frames == 1,
              "a re-arm frame of the armed cockpit skips the walks and refreshes the sector");
        RestoreFrame stale = w.rear(old_cockpit, 7);
        stale.connect = 2;
        check(!w.frame(stale) && w.st.phase == phase_armed, "the same address at another generation is not the armed one");
    }
    // 21. Run 146 A (Mayhem): +0x1fc reads 0 on every frame; the sector is log-only, so the fresh cockpit
    // binds, reaches due and restores, and the row carries the +0x54 sectors.
    {
        World w;
        RestoreFrame r = w.rear();
        r.sector = 0;
        check(w.frame(r) && w.st.phase == phase_armed && w.st.arm_sector == 0 && w.st.arm_sector54 == sector54_a,
              "a rear frame with +0x1fc 0 arms and records +0x54");
        w.st.on_destroy(old_cockpit, 1, restore_free_caller);
        w.st.on_complete(restore_alloc_caller);
        w.st.on_mode_store(new_cockpit, 2, restore_internal_mode, thread, restore_alloc_caller, monitor_id, monitor_class);
        w.due(0);
        check(w.st.bound_cockpit == new_cockpit && w.st.bound_generation == 2, "+0x1fc 0 binds the fresh cockpit");
        check(w.inject(0).wanted, "+0x1fc 0 reaches the call");
        w.st.on_mode_store(new_cockpit, 2, restore_rear_mode, thread, restore_alloc_caller, monitor_id, monitor_class);
        w.st.on_call_result(true);
        const RestoreOutcome o = w.st.take_outcome();
        check(o.result == result_restored && o.sector_from == 0 && o.sector_to == 0 && o.sector54_from == sector54_a &&
                  o.sector54_to == sector54_b && o.generation_to == 2,
              "restored with +0x1fc 0; the row logs +0x54 from/to");
    }
    // 22. The load chain's constructor is recorded by its return address 0x0041f840 (call at 0x0041f83b).
    {
        World w;
        w.arm_and_free();
        w.st.on_complete(0x0041f83bu);
        check(w.st.phase == phase_pending, "the call address 0x0041f83b is not a recorded caller and cancels nothing");
        w.st.on_complete(0x0041f840u);
        check(w.st.phase == phase_idle && w.st.cancels[cancel_lifetime] == 1 &&
                  w.st.take_outcome().cancel == cancel_lifetime,
              "constructor completion from 0x0041f840 while pending cancels lifetime");
    }
    // 23. Soft refusals while pending are counted per reason; the first refused frame is recorded; a
    // fresh cockpit of another constructor caller counts as skip_caller; the counters reset per ticket.
    {
        World w;
        w.arm_and_free();
        RestoreFrame f = w.fresh();
        f.readable = false;
        check(!w.frame(f), "unreadable frame does not count");
        check(!w.frame(w.fresh(new_cockpit, 2, sector_b, 0)), "mode 0 frame does not count");
        f = w.fresh();
        f.connect = 2;
        check(!w.frame(f), "connect frame does not count");
        w.p.control = false;
        check(!w.frame(w.fresh()), "no active control does not count");
        w.p.control = true;
        w.p.live = false;
        check(!w.frame(w.fresh()) && !w.frame(w.fresh()), "dead ship does not count");
        w.p.live = true;
        f = w.fresh(side_cockpit, 5);
        f.constructor_caller = restore_load_constructor_caller;
        check(!w.frame(f), "a non-alloc cockpit frame is skipped");
        check(w.st.phase == phase_pending, "soft refusals keep the ticket pending");
        w.st.on_load();
        const RestoreOutcome o = w.st.take_outcome();
        check(o.cancel == cancel_load && o.soft[soft_unreadable] == 1 && o.soft[soft_mode_zero] == 1 &&
                  o.soft[soft_connect_flags_view] == 1 && o.soft[soft_no_handle] == 1 && o.soft[soft_ship_dead] == 2 &&
                  o.skip_caller == 1 && o.first_skip_caller == restore_load_constructor_caller,
              "the outcome row carries the soft counters and the skipped caller");
        check(o.first_soft == soft_unreadable && o.first_soft_sector == sector_b && o.first_soft_sector54 == sector54_b &&
                  o.first_soft_mode == restore_internal_mode && o.first_soft_handle == 0,
              "the first soft frame is recorded with +0x1fc, +0x54, mode and handle");
        w.arm_and_free();
        w.p.live = false;
        check(!w.frame(w.fresh()), "dead ship on the next ticket");
        w.p.live = true;
        w.st.on_load();
        const RestoreOutcome n = w.st.take_outcome();
        check(n.soft[soft_unreadable] == 0 && n.soft[soft_ship_dead] == 1 && n.first_soft == soft_ship_dead &&
                  n.first_soft_handle == w.p.handle && n.skip_caller == 0,
              "counters reset per ticket; a later refusal records the active-control handle");
    }
    std::printf("chase restore host: %u checks PASS\n", checks);
}
