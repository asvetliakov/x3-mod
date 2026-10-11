#pragma once
#include "chase_transition_core.h"

// Portable one-use rear-chase restore ticket on engine observations only
// (docs/architecture/chase-view-restore-engine-state.md, section 4 as amended
// 2026-10-12). The ticket is armed from applied 258 frames of the player's
// active-control cockpit, transferred to pending by the cockpit free command,
// bound to the fresh cockpit from the alloc command by the active-control
// proof and a registry-live ship (any ship, any sector: the trigger is the
// cockpit replacement itself; the sector is log-only), and consumed at the input
// routine's entry by the engine's own by-name script call
// `SelectMode(258)` on the main monitor object captured at the mode store the
// script made into that cockpit, followed after a restore by the monitor
// manager's `UpdateVisibility()` (the manager captured from the storing
// monitor's caller frame at the same store). Every decision is made from the fresh fields
// the caller passes in or from the Probe it supplies; no engine pointer is
// retained across callbacks.
namespace x3m::chase_transition::detail {
constexpr std::uint32_t restore_rear_mode = 258, restore_internal_mode = 1;
// Constructor/destructor callers are native return addresses (call + 5): the load chain's
// constructor call sits at 0x0041f83b and returns to 0x0041f840.
constexpr std::uint32_t restore_free_caller = 0x42d402, restore_alloc_caller = 0x42d397,
                        restore_load_constructor_caller = 0x41f840;
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
// Soft refusals of a candidate (fresh, alloc-caller) frame while pending/due: the frame does not count
// towards due and resets the bound cockpit's count. Diagnostic counters per ticket.
enum RestoreSoft : unsigned {
    soft_none = 0,
    soft_unreadable = 1,
    soft_mode_zero = 2,
    soft_connect_flags_view = 3,
    soft_no_handle = 4,
    soft_ship_dead = 5,
    soft_count = 6
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
// The HUD refresh after a restored SelectMode(258): the monitor manager's own `UpdateVisibility()` (the
// method the script's view keys run after SelectMode: Show -> StartMonitor -> ShowCockpit -> UpdateOverlays).
// none: no refresh was due (the ticket did not end restored); skipped: no manager captured for the bound
// cockpit; ok / refused: the wrapper's AL; unwound: the call never reported back (epoch changed first).
enum RestoreHudRefresh : unsigned { hud_none = 0, hud_skipped = 1, hud_ok = 2, hud_refused = 3, hud_unwound = 4,
                                    hud_count = 5 };
inline const char* restore_hud_name(unsigned h) {
    static const char* const names[hud_count] = {"none", "skipped", "ok", "refused", "unwound"};
    return h < hud_count ? names[h] : "?";
}
// The manager capture: the caller frame of the storing monitor, found by the engine's own return-record
// scan. found; bottom: the task's bottom record (return offset 0) came first, no caller frame; truncated:
// more used cells than the bounded scan reads; unreadable: a refused read or an implausible task stack.
enum RestoreManagerScan : unsigned { scan_none = 0, scan_found = 1, scan_bottom = 2, scan_truncated = 3,
                                     scan_unreadable = 4, scan_count = 5 };
inline const char* restore_scan_name(unsigned s) {
    static const char* const names[scan_count] = {"none", "found", "bottom", "truncated", "unreadable"};
    return s < scan_count ? names[s] : "?";
}
constexpr unsigned restore_caller_scan_cells = 128; // 640 bytes; the chain's frames are 4..12 locals each
// Mod-owned method names (the EXE carries neither literal; the wrapper matches by content).
inline constexpr char restore_select_mode_name[] = "SelectMode";
inline constexpr char restore_update_visibility_name[] = "UpdateVisibility";
// One cdecl call of the engine's by-name wrapper 0x0049f570(callback slot, cookie, lookup key, name,
// return kind, argc, args...). Argument 0 stays 0 (no completion callback: Run147 crashed with a non-zero
// value there); argc 0 passes six dwords, argc 1 seven.
struct RestoreEngineCall {
    std::uint32_t callback = 0, cookie = 0, key = 0;
    const char* name = nullptr;
    std::uint32_t return_kind = 1, argc = 0, value = 0;
};
// Caller frame of the script object making the current native call. Task stack (5-byte cells, tag byte +
// dword) at [task+0x14] with the signed top index [task+0x18] (written at 0x004a38d9 before the native
// dispatch, so the top is the native's argc cell); used cells lie in [base + 5*index, base). Every script
// call opcode (0x85 dynamic 0x004a3c85/0x004a3c97, 0x86 class-static 0x004a3d23/0x004a3d35, 0x88 self
// 0x004a3eee/0x004a3f00) pushes a tag-3 return offset and below it a tag-10 saved caller context
// (0x004a8620/0x004a8640); a task created by 0x0049f430 starts with the pair (3: 0, 10: 0). This walk
// applies the return opcode's own scan (0x004a39b3..0x004a39fc) frame after frame from the top: a tag-10
// cell directly above a tag-3 cell is a record; return offset 0 is the task bottom; a null context or one
// whose class pointer [ctx+8] is 0 is skipped as the engine skips it; a frame whose context is the current
// one (a self call, 0x88) is passed. The first other context is the caller object: [ctx] its lookup key
// (negative dynamic id, or the non-negative static-record key 0x004b06f0 matches), [[ctx+8]] its class id.
// Reader: bool(std::uintptr_t base, unsigned offset, void* out, unsigned n), checked.
struct RestoreCaller {
    std::uint32_t id = 0, class_id = 0;
    unsigned scan = scan_none, frames = 0;
};
template <class Reader>
RestoreCaller restore_caller_context(std::uint32_t task, std::uint32_t current, Reader bytes) noexcept {
    auto field = [&](std::uintptr_t base, unsigned offset, auto& out) { return bytes(base, offset, &out, sizeof out); };
    RestoreCaller r;
    r.scan = scan_unreadable;
    std::uint32_t base = 0, capacity = 0;
    std::int32_t index = 0;
    if (!task || (task & 3) || !field(task, 0x14, base) || !field(task, 0x10, capacity) || !field(task, 0x18, index) ||
        index > 0 || capacity > 1048576 || index < -std::int32_t(capacity))
        return r;
    const auto used = std::uint32_t(-std::int64_t(index));
    if (used > base / 5) return r;
    const unsigned cells = used < restore_caller_scan_cells ? used : restore_caller_scan_cells;
    unsigned char stack[restore_caller_scan_cells * 5];
    if (cells && !bytes(base - used * 5, 0, stack, cells * 5)) return r;
    for (unsigned i = 0; i + 1 < cells; ++i) {
        if (stack[i * 5] != 10 || stack[(i + 1) * 5] != 3) continue;
        std::uint32_t context = 0, ret = 0, desc = 0;
        std::memcpy(&context, stack + i * 5 + 1, 4);
        std::memcpy(&ret, stack + (i + 1) * 5 + 1, 4);
        if (!ret) {
            r.scan = scan_bottom;
            return r;
        }
        if (!context) continue;
        if ((context & 3) || !field(context, 8, desc)) return r;
        if (!desc) continue;
        ++r.frames;
        if (context == current) continue;
        std::uint32_t id = 0, class_id = 0;
        if ((desc & 3) || !field(context, 0, id) || !id || !field(desc, 0, class_id)) return r;
        r.id = id;
        r.class_id = class_id;
        r.scan = scan_found;
        return r;
    }
    r.scan = used > cells ? scan_truncated : scan_bottom;
    return r;
}
// One chase-hook visit (or the fresh re-read at the injection site). The
// caller reads the cockpit fields; the walks run through the Probe only when
// the ticket needs them. `sector` is +0x1fc (script-published, 0 under Mayhem),
// `sector54` the +0x54 sector pointer; both are log-only.
struct RestoreFrame {
    std::uint32_t cockpit = 0, mode = 0, connect = 0, flags = 0, ref = 0, view = 0, sector = 0, sector54 = 0;
    std::uint32_t constructor_caller = 0;
    std::uint64_t generation = 0;
    bool readable = false;
};
// What the engine call needs, handed out by the state machine; the caller
// performs `0x0049f570(0, 0, monitor, "SelectMode", 1, 1, 258)` and
// reports back with on_call_result.
struct RestoreCall {
    bool wanted = false;
    std::uint32_t monitor = 0, monitor_class = 0;
};
// The second call on a restored ticket, handed out by on_call_result: `0x0049f570(0, 0, manager,
// "UpdateVisibility", 1, 0)`; the caller reports back with on_refresh_result.
struct RestoreRefresh {
    bool wanted = false;
    std::uint32_t manager = 0, manager_class = 0;
};
inline RestoreEngineCall restore_select_call(const RestoreCall& c) noexcept {
    return {0, 0, c.monitor, restore_select_mode_name, 1, 1, restore_rear_mode};
}
inline RestoreEngineCall restore_refresh_call(const RestoreRefresh& r) noexcept {
    return {0, 0, r.manager, restore_update_visibility_name, 1, 0, 0};
}
struct RestoreOutcome {
    bool wanted = false;
    unsigned result = 0, cancel = 0, attempts = 0, hud_refresh = hud_none, manager_scan = scan_none;
    std::uint32_t monitor = 0, monitor_class = 0, manager = 0, manager_class = 0, ship_id_from = 0, ship_id_to = 0,
                  sector_from = 0, sector_to = 0, sector54_from = 0, sector54_to = 0, wait_updates = 0, mode_seen = 0;
    std::uint64_t generation_from = 0, generation_to = 0;
    // Pending/due diagnostics: soft refusals by reason, candidate-shaped frames of another constructor
    // caller, and the first soft-refused frame (reason, +0x1fc, +0x54, mode, active-control handle).
    std::uint32_t soft[soft_count]{}, skip_caller = 0, first_skip_caller = 0;
    std::uint32_t first_soft = 0, first_soft_sector = 0, first_soft_sector54 = 0, first_soft_mode = 0,
                  first_soft_handle = 0;
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
    std::uint32_t arm_cockpit = 0, arm_ship_id = 0, arm_controller = 0, arm_sector = 0, arm_sector54 = 0,
                  arm_epoch = 0;
    std::uint64_t arm_generation = 0;
    // Pending/bound/due.
    std::uint32_t bound_cockpit = 0, bound_sector = 0, bound_sector54 = 0, bound_ship_id = 0;
    std::uint64_t bound_generation = 0;
    unsigned due_count = 0;
    std::uint32_t pending_updates = 0, retry_after = 0;
    // The monitor object whose SelectMode stored a mode into a fresh cockpit (engine VM frame at 0x0042e742).
    std::uint32_t monitor_cockpit = 0, monitor = 0, monitor_class = 0;
    std::uint64_t monitor_generation = 0;
    // Its caller frame at the same store (the monitor manager), for the HUD refresh after a restore.
    std::uint32_t manager = 0, manager_class = 0;
    unsigned manager_scan = scan_none;
    // Attempts of this ticket.
    unsigned attempts = 0;
    std::uint32_t attempt_thread = 0, attempt_mode_seen = 0;
    // Per-ticket soft-refusal diagnostics (fixed counters, copied into the outcome row).
    std::uint32_t soft_counts[soft_count]{}, skip_caller = 0, first_skip_caller = 0;
    std::uint32_t first_soft = 0, first_soft_sector = 0, first_soft_sector54 = 0, first_soft_mode = 0,
                  first_soft_handle = 0;
    // Counters.
    std::uint64_t arms = 0, arm_refusals = 0, pendings = 0, dues = 0, calls = 0, monitors_captured = 0,
                  injections_skipped = 0, rearm_frames = 0, managers_captured = 0;
    std::uint64_t cancels[cancel_count]{}, results[result_count]{}, arm_refusal_reasons[arm_refusal_count]{},
        hud_refreshes[hud_count]{};
    std::uint32_t arm_sample_logged = 0;
    // One-shot rows captured under the lock, emitted after it is released.
    RestoreOutcome outcome;
    RestoreArmSample arm_sample;
    // A restored outcome waiting for its HUD refresh result (emitted by on_refresh_result, or as
    // hud_refresh=unwound once the epoch moves past held_epoch without a result).
    RestoreOutcome held;
    std::uint32_t held_epoch = 0;

    bool ticket_open() const noexcept { return phase != phase_idle; }
    bool wants_injection() const noexcept { return phase == phase_due; }
    void reset_ticket() noexcept {
        phase = phase_idle;
        arm_cockpit = arm_ship_id = arm_controller = arm_sector = arm_sector54 = arm_epoch = 0;
        arm_generation = 0;
        bound_cockpit = bound_sector = bound_sector54 = bound_ship_id = 0;
        bound_generation = 0;
        due_count = 0;
        pending_updates = retry_after = 0;
        monitor_cockpit = monitor = monitor_class = 0;
        monitor_generation = 0;
        manager = manager_class = 0;
        manager_scan = scan_none;
        attempts = 0;
        attempt_thread = attempt_mode_seen = 0;
        for (auto& n : soft_counts) n = 0;
        skip_caller = first_skip_caller = 0;
        first_soft = first_soft_sector = first_soft_sector54 = first_soft_mode = first_soft_handle = 0;
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
            outcome.manager = manager;
            outcome.manager_class = manager_class;
            outcome.manager_scan = manager_scan;
            outcome.generation_from = arm_generation;
            outcome.generation_to = bound_generation;
            outcome.ship_id_from = arm_ship_id;
            outcome.ship_id_to = bound_ship_id;
            outcome.sector_from = arm_sector;
            outcome.sector_to = bound_sector;
            outcome.sector54_from = arm_sector54;
            outcome.sector54_to = bound_sector54;
            for (unsigned i = 0; i < soft_count; ++i) outcome.soft[i] = soft_counts[i];
            outcome.skip_caller = skip_caller;
            outcome.first_skip_caller = first_skip_caller;
            outcome.first_soft = first_soft;
            outcome.first_soft_sector = first_soft_sector;
            outcome.first_soft_sector54 = first_soft_sector54;
            outcome.first_soft_mode = first_soft_mode;
            outcome.first_soft_handle = first_soft_handle;
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
    // EH adapter / VM epoch: an armed or pending ticket from an older epoch is gone, and a HUD refresh
    // that never reported back (a script error unwound through it) releases its held outcome.
    void epoch(std::uint32_t e) noexcept {
        if (held.wanted && held_epoch != e) release_held(hud_unwound);
        if (ticket_open() && arm_epoch != e) cancel(cancel_epoch);
    }
    void release_held(unsigned hud) noexcept {
        if (!held.wanted) return;
        held.hud_refresh = hud;
        ++hud_refreshes[hud];
        outcome = held;
        held = {};
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
            // cockpit is re-proved at due and again at the injection entry). Only the sectors refresh.
            arm_sector = f.sector;
            arm_sector54 = f.sector54;
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
        arm_sector54 = f.sector54;
        arm_epoch = epoch_now;
        return true;
    }
    bool candidate(const RestoreFrame& f) const noexcept {
        return !(f.cockpit == arm_cockpit && f.generation == arm_generation) &&
               f.constructor_caller == restore_alloc_caller;
    }
    // Pending/due: a fresh cockpit from the alloc command. Returns true when the frame counted towards due.
    // The sector (+0x1fc or +0x54) is not a condition: +0x1fc is script-published and reads 0 under
    // Mayhem (Run 146 A), and the trigger is the cockpit replacement, not a sector change.
    template <class Probe> bool candidate_frame(const RestoreFrame& f, Probe& p, bool at_injection) noexcept {
        if (!candidate(f)) {
            if (!(f.cockpit == arm_cockpit && f.generation == arm_generation) && !skip_caller++)
                first_skip_caller = f.constructor_caller;
            return false;
        }
        if (!f.readable) return soft(f, soft_unreadable, 0);
        if (!f.mode) return soft(f, soft_mode_zero, 0);
        if (bound_cockpit && f.cockpit != bound_cockpit && phase == phase_due) return false;
        if (f.mode != restore_internal_mode) {
            // A fresh cockpit shown at 258 already (the script restored itself) or a key the player pressed.
            cancel(cancel_mode_left);
            return false;
        }
        if (f.connect || (f.flags & 4) || !f.ref || f.view != f.ref) return soft(f, soft_connect_flags_view, 0);
        std::uint32_t handle = 0, ship_id = 0, controller = 0;
        if (!p.active_control(handle)) return soft(f, soft_no_handle, handle);
        // The ship may differ from the armed one (ship change, death respawn): the trigger is the
        // cockpit replacement, and the restore applies to whatever the player now flies. Only the
        // engine's controller object must be the one the arm saw.
        if (!p.ship_live(f.ref, ship_id) || !ship_id) return soft(f, soft_ship_dead, handle);
        if (!p.controller(controller) || controller != arm_controller) {
            cancel(cancel_identity);
            return false;
        }
        if (bound_cockpit != f.cockpit || bound_generation != f.generation) {
            bound_cockpit = f.cockpit;
            bound_generation = f.generation;
            due_count = 0;
        }
        bound_sector = f.sector;
        bound_sector54 = f.sector54;
        bound_ship_id = ship_id;
        if (at_injection) return true;
        if (pending_updates < retry_after) return false; // retry spacing: not counted yet
        if (phase == phase_pending && ++due_count >= restore_due_frames) {
            phase = phase_due;
            ++dues;
        }
        return true;
    }
    bool soft(const RestoreFrame& f, unsigned why, std::uint32_t handle) noexcept {
        if (f.cockpit == bound_cockpit) due_count = 0;
        if (!soft_counts[why]++ && !first_soft) {
            first_soft = why;
            first_soft_sector = f.sector;
            first_soft_sector54 = f.sector54;
            first_soft_mode = f.mode;
            first_soft_handle = handle;
        }
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
    // manager/manager_class/scan are the storing monitor's caller frame (restore_caller_context): the
    // monitor manager whose RestartAllMonitors or UpdateVisibility called into the monitor. A store whose
    // scan found no caller keeps the manager an earlier store captured for the same cockpit.
    void on_mode_store(std::uint32_t cockpit, std::uint64_t generation, std::uint32_t requested,
                       std::uint32_t thread, std::uint32_t constructor_caller, std::uint32_t monitor_id,
                       std::uint32_t monitor_class_id, std::uint32_t manager_id = 0,
                       std::uint32_t manager_class_id = 0, unsigned scan = scan_none) noexcept {
        if (phase == phase_calling) {
            if (thread == attempt_thread && cockpit == bound_cockpit && generation == bound_generation)
                attempt_mode_seen = requested;
            return;
        }
        if (phase != phase_pending && phase != phase_due) return;
        if (constructor_caller != restore_alloc_caller || !monitor_id || !generation) return;
        if (cockpit == arm_cockpit && generation == arm_generation) return;
        const bool same = monitor_cockpit == cockpit && monitor_generation == generation;
        monitor_cockpit = cockpit;
        monitor_generation = generation;
        monitor = monitor_id;
        monitor_class = monitor_class_id;
        ++monitors_captured;
        if (manager_id) {
            manager = manager_id;
            manager_class = manager_class_id;
            manager_scan = scan_found;
            ++managers_captured;
        } else if (!same || !manager) {
            manager = manager_class = 0;
            manager_scan = scan;
        }
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
    // SelectMode's result. A restored ticket with a captured manager hands out the HUD refresh and holds
    // its outcome row until on_refresh_result (epoch_now dates the hold); without a manager the row says
    // hud_refresh=skipped. Other results make no second call (hud_refresh=none).
    RestoreRefresh on_call_result(bool call_ok, std::uint32_t epoch_now = 0) noexcept {
        RestoreRefresh r;
        if (phase != phase_calling) return r; // cancelled during the call (lifetime, load, epoch)
        const std::uint32_t seen = attempt_mode_seen;
        if (!call_ok) {
            finish(result_call_refused, cancel_call_refused);
            ++hud_refreshes[hud_none];
            return r;
        }
        if (seen == restore_rear_mode) {
            r.wanted = manager != 0;
            r.manager = manager;
            r.manager_class = manager_class;
            finish(result_restored, cancel_none);
            if (!r.wanted) {
                outcome.hud_refresh = hud_skipped;
                ++hud_refreshes[hud_skipped];
                return r;
            }
            const RestoreOutcome restored = outcome;
            outcome.wanted = false;
            release_held(hud_unwound); // an older refresh that never reported back takes the row slot
            held = restored;
            held_epoch = epoch_now;
            return r;
        }
        if (seen) {
            finish(result_other_mode, cancel_other_mode); // the script chose another mode: not fought
            ++hud_refreshes[hud_none];
            return r;
        }
        if (attempts > retries) {
            finish(result_no_effect, cancel_retries);
            ++hud_refreshes[hud_none];
            return r;
        }
        ++results[result_no_effect];
        phase = phase_pending;
        due_count = 0;
        retry_after = pending_updates + restore_retry_spacing;
        return r;
    }
    // UpdateVisibility's result (the wrapper's AL): releases the held outcome row.
    void on_refresh_result(bool call_ok) noexcept { release_held(call_ok ? hud_ok : hud_refused); }
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
