#pragma once
#include "chase_transition_core.h"

// Portable one-use rear-chase restore ticket on engine observations only
// (docs/architecture/chase-view-restore-engine-state.md, section 4 as amended
// 2026-10-12). The ticket is armed from applied 258 frames of the player's
// active-control cockpit, transferred to pending by the cockpit free command,
// bound to the fresh cockpit from the alloc command by the active-control
// proof, a registry-live ship and a published sector (any ship, any sector:
// the trigger is the cockpit replacement itself), and consumed at the input
// routine's entry by the engine's own by-name script call
// `SelectMode(258)` on the main monitor object captured at the mode store the
// script made into that cockpit. Every decision is made from the fresh fields
// the caller passes in or from the Probe it supplies; no engine pointer is
// retained across callbacks.
namespace x3m::chase_transition::detail {
constexpr std::uint32_t restore_rear_mode = 258, restore_internal_mode = 1;
constexpr std::uint32_t restore_free_caller = 0x42d402, restore_alloc_caller = 0x42d397,
                        restore_load_constructor_caller = 0x41f83b;
constexpr std::uint32_t restore_inject_site = 0x410100, restore_script_call = 0x49f570;
constexpr std::uint32_t restore_game_root = 0x57fc60, restore_cockpit_registry_root = 0x608504;
constexpr std::uint32_t restore_ship_delete_flag = 0x08000000; // [ship+0x40], deferred delete (sweep 0x0045b660)
constexpr unsigned restore_pending_update_expiry = 600, restore_due_frames = 2, restore_retry_spacing = 60,
                   restore_retry_cap = 5, restore_default_retries = 3;
enum RestoreCancel : unsigned {
    cancel_none = 0,
    cancel_destructor = 1,
    cancel_second_destruction = 2,
    cancel_lifetime = 3,
    cancel_identity = 4, // the engine's controller object changed between arm and due
    cancel_mode_left = 5,
    cancel_expiry = 6,
    cancel_load = 7,
    cancel_epoch = 8,
    cancel_no_monitor = 9,
    cancel_retries = 10,
    cancel_call_refused = 11,
    cancel_other_mode = 12,
    cancel_call_unwound = 13, // on_call_result never ran (a script error unwound through the call); expiry clock
    cancel_count = 14
};
enum RestoreResult : unsigned {
    result_none = 0,
    result_restored = 1,
    result_no_effect = 2,
    result_other_mode = 3,
    result_call_refused = 4,
    result_cancelled = 5,
    result_count = 6
};
// Arm refusal reasons (1..3 as before; 4 replaces the script identity walk).
enum RestoreArmRefusal : unsigned {
    arm_ok = 0,
    arm_connect = 1,
    arm_view_not_ready = 2,
    arm_no_handle = 3,
    arm_ship_dead = 4,
    arm_flags = 5,
    arm_unreadable = 6,
    arm_refusal_count = 7
};
enum RestorePhase : unsigned { phase_idle = 0, phase_armed = 1, phase_pending = 2, phase_due = 3, phase_calling = 4 };
inline const char* restore_cancel_name(unsigned c) {
    static const char* const names[cancel_count] = {"none",   "destructor", "second_destruction", "lifetime",
                                                    "identity", "mode_left", "expiry",             "load",
                                                    "epoch",  "no_monitor", "retries",            "call_refused",
                                                    "other_mode", "call_unwound"};
    return c < cancel_count ? names[c] : "?";
}
inline const char* restore_result_name(unsigned r) {
    static const char* const names[result_count] = {"none",       "restored",     "no_effect",
                                                    "other_mode", "call_refused", "cancelled"};
    return r < result_count ? names[r] : "?";
}
// One chase-hook visit (or the fresh re-read at the injection site). The
// caller reads the cockpit fields; the walks run through the Probe only when
// the ticket needs them.
struct RestoreFrame {
    std::uint32_t cockpit = 0, mode = 0, connect = 0, flags = 0, ref = 0, view = 0, sector = 0;
    std::uint32_t constructor_caller = 0;
    std::uint64_t generation = 0;
    bool readable = false;
};
// What the engine call needs, handed out by the state machine; the caller
// performs `0x0049f570(monitor, 0, monitor, "SelectMode", 1, 1, 258)` and
// reports back with on_call_result.
struct RestoreCall {
    bool wanted = false;
    std::uint32_t monitor = 0, monitor_class = 0;
};
struct RestoreOutcome {
    bool wanted = false;
    unsigned result = 0, cancel = 0, attempts = 0;
    std::uint32_t monitor = 0, monitor_class = 0, ship_id_from = 0, ship_id_to = 0, sector_from = 0, sector_to = 0,
                  wait_updates = 0, mode_seen = 0;
    std::uint64_t generation_from = 0, generation_to = 0;
};
struct RestoreArmSample {
    bool wanted = false, armed = false, ship_live = false;
    unsigned reason = 0;
    std::uint32_t cockpit = 0, mode = 0, connect = 0, ship = 0, view = 0, handle = 0, ship_id = 0;
    std::uint64_t generation = 0;
};
struct RestoreState {
    unsigned phase = phase_idle;
    unsigned retries = restore_default_retries;
    // Arm (re-armed on every applied rear frame of the active-control cockpit).
    std::uint32_t arm_cockpit = 0, arm_ship_id = 0, arm_controller = 0, arm_sector = 0, arm_epoch = 0;
    std::uint64_t arm_generation = 0;
    // Pending/bound/due.
    std::uint32_t bound_cockpit = 0, bound_sector = 0, bound_ship_id = 0;
    std::uint64_t bound_generation = 0;
    unsigned due_count = 0;
    std::uint32_t pending_updates = 0, retry_after = 0;
    // The monitor object whose SelectMode stored a mode into a fresh cockpit (engine VM frame at 0x0042e742).
    std::uint32_t monitor_cockpit = 0, monitor = 0, monitor_class = 0;
    std::uint64_t monitor_generation = 0;
    // Attempts of this ticket.
    unsigned attempts = 0;
    std::uint32_t attempt_thread = 0, attempt_mode_seen = 0;
    // Counters.
    std::uint64_t arms = 0, arm_refusals = 0, pendings = 0, dues = 0, calls = 0, monitors_captured = 0,
                  injections_skipped = 0, rearm_frames = 0;
    std::uint64_t cancels[cancel_count]{}, results[result_count]{}, arm_refusal_reasons[arm_refusal_count]{};
    std::uint32_t arm_sample_logged = 0;
    // One-shot rows captured under the lock, emitted after it is released.
    RestoreOutcome outcome;
    RestoreArmSample arm_sample;

    bool ticket_open() const noexcept { return phase != phase_idle; }
    bool wants_injection() const noexcept { return phase == phase_due; }
    void reset_ticket() noexcept {
        phase = phase_idle;
        arm_cockpit = arm_ship_id = arm_controller = arm_sector = arm_epoch = 0;
        arm_generation = 0;
        bound_cockpit = bound_sector = bound_ship_id = 0;
        bound_generation = 0;
        due_count = 0;
        pending_updates = retry_after = 0;
        monitor_cockpit = monitor = monitor_class = 0;
        monitor_generation = 0;
        attempts = 0;
        attempt_thread = attempt_mode_seen = 0;
    }
    void finish(unsigned result, unsigned cancel) noexcept {
        if (phase == phase_idle) return;
        // An armed ticket that is simply disarmed again (mode left, refused frame) logs nothing.
        const bool silent = phase == phase_armed;
        if (cancel) ++cancels[cancel];
        if (!silent) {
            ++results[result];
            outcome = {};
            outcome.wanted = true;
            outcome.result = result;
            outcome.cancel = cancel;
            outcome.attempts = attempts;
            outcome.monitor = monitor;
            outcome.monitor_class = monitor_class;
            outcome.generation_from = arm_generation;
            outcome.generation_to = bound_generation;
            outcome.ship_id_from = arm_ship_id;
            outcome.ship_id_to = bound_ship_id;
            outcome.sector_from = arm_sector;
            outcome.sector_to = bound_sector;
            outcome.wait_updates = pending_updates;
            outcome.mode_seen = attempt_mode_seen;
        }
        reset_ticket();
    }
    void cancel(unsigned reason) noexcept { finish(result_cancelled, reason); }
    void refuse_arm(unsigned reason, const RestoreFrame& f, std::uint32_t handle, bool ship_live,
                    std::uint32_t ship_id) noexcept {
        if (reason >= arm_refusal_count) reason = 0;
        ++arm_refusals;
        ++arm_refusal_reasons[reason];
        if (phase == phase_armed && f.cockpit == arm_cockpit && f.generation == arm_generation) cancel(cancel_mode_left);
        const std::uint32_t bit = 1u << reason;
        if (arm_sample_logged & bit) return;
        arm_sample_logged |= bit;
        arm_sample = {};
        arm_sample.wanted = true;
        arm_sample.reason = reason;
        arm_sample.cockpit = f.cockpit;
        arm_sample.generation = f.generation;
        arm_sample.mode = f.mode;
        arm_sample.connect = f.connect;
        arm_sample.ship = f.ref;
        arm_sample.view = f.view;
        arm_sample.handle = handle;
        arm_sample.ship_live = ship_live;
        arm_sample.ship_id = ship_id;
    }
    // EH adapter / VM epoch: an armed or pending ticket from an older epoch is gone.
    void epoch(std::uint32_t e) noexcept {
        if (ticket_open() && arm_epoch != e) cancel(cancel_epoch);
    }
    // Chase hook visit. Probe: bool active_control(uint32_t& handle); bool ship_live(uint32_t ref, uint32_t& ship_id);
    // bool controller(uint32_t& id). Returns true when the frame armed or counted towards due.
    template <class Probe> bool on_frame(const RestoreFrame& f, Probe& p, std::uint32_t epoch_now) noexcept {
        epoch(epoch_now);
        if (phase == phase_idle || phase == phase_armed) return arm_frame(f, p, epoch_now);
        if (phase == phase_calling) return false;
        return candidate_frame(f, p, false);
    }
    template <class Probe> bool arm_frame(const RestoreFrame& f, Probe& p, std::uint32_t epoch_now) noexcept {
        const bool mine = phase == phase_armed && f.cockpit == arm_cockpit && f.generation == arm_generation;
        if (!f.readable) {
            if (mine) refuse_arm(arm_unreadable, f, 0, false, 0);
            return false;
        }
        if (f.mode != restore_rear_mode) {
            if (mine) cancel(cancel_mode_left);
            return false;
        }
        if (!f.ref || !f.view || f.view != f.ref) {
            if (mine) cancel(cancel_mode_left);
            else if (phase == phase_idle) refuse_arm(arm_view_not_ready, f, 0, false, 0);
            return false;
        }
        if (f.connect) {
            refuse_arm(arm_connect, f, 0, false, 0);
            return false;
        }
        if (f.flags & 4) {
            refuse_arm(arm_flags, f, 0, false, 0);
            return false;
        }
        if (mine) {
            // Re-arm frame: the field checks above hold; the registry walk, the ship hash and the
            // controller read are skipped (the arm's identity decides nothing by itself: the fresh
            // cockpit is re-proved at due and again at the injection entry). Only the sector refreshes.
            arm_sector = f.sector;
            ++rearm_frames;
            return true;
        }
        std::uint32_t handle = 0, ship_id = 0, controller = 0;
        if (!p.active_control(handle)) {
            // Another cockpit owns control (a side monitor at 258): not a refusal of the armed one.
            if (phase == phase_armed) return false;
            refuse_arm(arm_no_handle, f, handle, false, 0);
            return false;
        }
        const bool live = p.ship_live(f.ref, ship_id);
        if (!live || !ship_id || !p.controller(controller) || !controller) {
            refuse_arm(arm_ship_dead, f, handle, live, ship_id);
            return false;
        }
        if (phase == phase_idle) ++arms;
        phase = phase_armed;
        arm_cockpit = f.cockpit;
        arm_generation = f.generation;
        arm_ship_id = ship_id;
        arm_controller = controller;
        arm_sector = f.sector;
        arm_epoch = epoch_now;
        return true;
    }
    bool candidate(const RestoreFrame& f) const noexcept {
        return !(f.cockpit == arm_cockpit && f.generation == arm_generation) &&
               f.constructor_caller == restore_alloc_caller;
    }
    // Pending/due: a fresh cockpit from the alloc command. Returns true when the frame counted towards due.
    template <class Probe> bool candidate_frame(const RestoreFrame& f, Probe& p, bool at_injection) noexcept {
        if (!candidate(f)) return false;
        if (!f.readable || !f.mode) return soft(f);
        if (bound_cockpit && f.cockpit != bound_cockpit && phase == phase_due) return false;
        if (f.mode != restore_internal_mode) {
            // A fresh cockpit shown at 258 already (the script restored itself) or a key the player pressed.
            cancel(cancel_mode_left);
            return false;
        }
        if (f.connect || (f.flags & 4) || !f.ref || f.view != f.ref) return soft(f);
        std::uint32_t handle = 0, ship_id = 0, controller = 0;
        if (!p.active_control(handle)) return soft(f);
        // The ship may differ from the armed one (ship change, death respawn): the trigger is the
        // cockpit replacement, and the restore applies to whatever the player now flies. Only the
        // engine's controller object must be the one the arm saw.
        if (!p.ship_live(f.ref, ship_id) || !ship_id) return soft(f);
        if (!p.controller(controller) || controller != arm_controller) {
            cancel(cancel_identity);
            return false;
        }
        if (!f.sector) return soft(f);
        if (bound_cockpit != f.cockpit || bound_generation != f.generation) {
            bound_cockpit = f.cockpit;
            bound_generation = f.generation;
            due_count = 0;
        }
        bound_sector = f.sector;
        bound_ship_id = ship_id;
        if (at_injection) return true;
        if (pending_updates < retry_after) return false; // retry spacing: not counted yet
        if (phase == phase_pending && ++due_count >= restore_due_frames) {
            phase = phase_due;
            ++dues;
        }
        return true;
    }
    bool soft(const RestoreFrame& f) noexcept {
        if (f.cockpit == bound_cockpit) due_count = 0;
        return false;
    }
    // Every cockpit update (the expiry clock while a ticket is past the arm).
    void on_update() noexcept {
        if (phase == phase_pending || phase == phase_due) {
            if (++pending_updates > restore_pending_update_expiry) cancel(cancel_expiry);
        } else if (phase == phase_calling) {
            // on_call_result never ran: a script error unwound through the engine call. The clock keeps
            // running so the next transit is not blocked behind a stalled ticket.
            if (++pending_updates > restore_pending_update_expiry) cancel(cancel_call_unwound);
        }
    }
    void on_destroy(std::uint32_t cockpit, std::uint64_t generation, std::uint32_t caller) noexcept {
        if (phase == phase_armed) {
            if (cockpit != arm_cockpit) return;
            if (generation != arm_generation || caller != restore_free_caller) {
                cancel(cancel_destructor);
                return;
            }
            phase = phase_pending;
            ++pendings;
            pending_updates = 0;
            retry_after = 0;
            due_count = 0;
            return;
        }
        if (phase == phase_pending || phase == phase_due || phase == phase_calling) {
            if (bound_cockpit) {
                if (cockpit == bound_cockpit) cancel(cancel_lifetime);
                return;
            }
            cancel(cancel_second_destruction);
        }
    }
    // Constructor completion: the load chain's constructor cancels; others are candidates by their caller.
    void on_complete(std::uint32_t caller) noexcept {
        if (ticket_open() && phase != phase_armed && caller == restore_load_constructor_caller)
            cancel(cancel_lifetime);
    }
    void on_load() noexcept {
        if (ticket_open()) cancel(cancel_load);
    }
    void on_vm() noexcept {
        if (ticket_open()) cancel(cancel_epoch);
    }
    // Kind 6 (0x0042e742, `mov [cockpit+0x150], mode` from the script command): while a ticket is past the
    // arm, the script object that stores into a candidate cockpit is the monitor that owns it (StartMonitor's
    // SelectMode(1), and every later Show); during our call the store on the bound cockpit is the result.
    // monitor/monitor_class are the engine VM frame's current context id (a dynamic object id, negative)
    // and class, 0 when unreadable or refused. The fresh cockpit may reuse the freed one's address, so
    // the stored cockpit is identified by (address, generation), never by the address alone.
    void on_mode_store(std::uint32_t cockpit, std::uint64_t generation, std::uint32_t requested,
                       std::uint32_t thread, std::uint32_t constructor_caller, std::uint32_t monitor_id,
                       std::uint32_t monitor_class_id) noexcept {
        if (phase == phase_calling) {
            if (thread == attempt_thread && cockpit == bound_cockpit && generation == bound_generation)
                attempt_mode_seen = requested;
            return;
        }
        if (phase != phase_pending && phase != phase_due) return;
        if (constructor_caller != restore_alloc_caller || !monitor_id || !generation) return;
        if (cockpit == arm_cockpit && generation == arm_generation) return;
        monitor_cockpit = cockpit;
        monitor_generation = generation;
        monitor = monitor_id;
        monitor_class = monitor_class_id;
        ++monitors_captured;
    }
    // Site I entry. The caller re-reads the bound cockpit's frame fresh and passes the engine's controlled
    // object ([ESP+4]) and the pause bits ([game+0x4a0]).
    template <class Probe>
    RestoreCall on_inject_entry(std::uint32_t thread, const RestoreFrame& fresh, std::uint32_t controlled_object,
                                std::uint32_t pause_bits, Probe& p, std::uint32_t epoch_now) noexcept {
        RestoreCall c;
        epoch(epoch_now);
        if (phase != phase_due) return c;
        if (!candidate_frame(fresh, p, true) || phase != phase_due) return c;
        if ((pause_bits & 1) || controlled_object != fresh.ref || controlled_object == 0) {
            ++injections_skipped;
            return c;
        }
        if (!monitor || monitor_cockpit != bound_cockpit || monitor_generation != bound_generation) {
            cancel(cancel_no_monitor);
            return c;
        }
        phase = phase_calling;
        ++attempts;
        ++calls;
        attempt_thread = thread;
        attempt_mode_seen = 0;
        c.wanted = true;
        c.monitor = monitor;
        c.monitor_class = monitor_class;
        return c;
    }
    void on_call_result(bool call_ok) noexcept {
        if (phase != phase_calling) return; // cancelled during the call (lifetime, load, epoch)
        const std::uint32_t seen = attempt_mode_seen;
        if (!call_ok) {
            finish(result_call_refused, cancel_call_refused);
            return;
        }
        if (seen == restore_rear_mode) {
            finish(result_restored, cancel_none);
            return;
        }
        if (seen) {
            finish(result_other_mode, cancel_other_mode); // the script chose another mode: not fought
            return;
        }
        if (attempts > retries) {
            finish(result_no_effect, cancel_retries);
            return;
        }
        ++results[result_no_effect];
        phase = phase_pending;
        due_count = 0;
        retry_after = pending_updates + restore_retry_spacing;
    }
    RestoreOutcome take_outcome() noexcept {
        RestoreOutcome o = outcome;
        outcome.wanted = false;
        return o;
    }
    RestoreArmSample take_arm_sample() noexcept {
        RestoreArmSample s = arm_sample;
        arm_sample.wanted = false;
        return s;
    }
};
}
