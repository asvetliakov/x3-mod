#pragma once
#include <cstdint>

// Chase camera for the external back view (X3M_CAMERA=chase; default off,
// vanilla camera when unset): a byte-verified trampoline in the cockpit
// update FUN_004205e0 of the verified X3AP.exe, at 0x00420e06, the first
// instruction after the vanilla pose of the cockpit's sector camera
// (cockpit+0x58) is final and before the engine derives camera-relative object
// positions, the target overlay, the HUD cameras and the frame's render from
// it (docs/reverse-engineering/external-camera.md). The handler reads the
// vanilla pose, the ship node and the view-relative basis (cockpit+0xf0),
// runs the portable spring pipeline (chase_camera_math.h) with a wall-clock
// dt (QueryPerformanceCounter) and writes the smoothed pose back into the
// camera (+0x30 position, +0x40 basis) and cockpit+0xf0. Scene, HUD overlay
// and TAA use that camera; +0xf0 preserves cursor-ray angular orientation.
// Cursor-fire admission and finite muzzle convergence are separate contracts
// (docs/reverse-engineering/chase-mouse-fire.md). Internal, front, side and scripted
// views pass through untouched: the game's own view keys stay the switch.
//
// The cockpit update runs once per registered cockpit per frame (the registry
// walk 0x0041cde0); the handler acts only on the active control cockpit, the
// one whose ref view object (+0x10) is its ref object (+0xc), the predicate the
// fire control uses. Every other cockpit's visit leaves the pipeline state,
// the clock and the frame counters untouched (review 31 A1). Run78 pose gap:
// a fresh cockpit generation after a gate transit reaches the site with +0x10
// still 0 for tens of updates while the engine already renders its rear view
// from it; that unbound phase is admitted only when chase_transition proves
// a complete lifetime whose registry active-control handle maps to the
// cockpit (docs/reverse-engineering/chase-view-transition.md, "Run 28 (run78)
// pose gap"). Fire, lead and aim contexts keep the bound predicate.
//
// Fail closed: exact executable (object_trace::executable_verified), exact
// site bytes, install window (engine_patch), every engine pointer read
// through engine_memory with bounded validated reads, writes only to the
// objects just read, NaN/denormal/range guards in the pipeline; any refusal
// leaves the vanilla pose for that frame. Initialization on the backend-load
// path; the site is kept for the process lifetime (review 31 A3: a device
// recreate cannot re-claim it, the install window is closed at the first
// Present, and the handler needs no device).
namespace x3m::chase_camera {
// Pose admission for one cockpit visit (portable; the CPU fixture exercises
// it): bound = the fire control's predicate; unbound = +0x10 still 0 on a
// cockpit the registry active-control proof admits. Any other +0x10 refuses.
inline bool admits_pose(std::uintptr_t ref_object, std::uintptr_t view_object, bool active_control) {
    if (view_object != 0) return view_object == ref_object;
    return ref_object != 0 && (ref_object & 3) == 0 && active_control;
}
struct FirstApplied {        // the static inferences of the first applied frame, logged once (review 31 O2/O3)
    bool captured = false, logged = false, native_base_domain = false;
    double domain_delta = 0;
    std::uint64_t handler_frame = 0;
    double half_vfov_tan = 0, boom_local[3] = {0, 0, 0};
    std::uint32_t fov298 = 0, plane_w = 0, plane_h = 0, default_plane_h = 0, view_mode = 0, connect_mode = 0, flags_1a0 = 0, tracking_mode = 0, aim_gun = 0;
    std::uintptr_t ref_object = 0, view_object = 0, tracked_object = 0;
    bool target_locked = false;
};
struct Stats {
    bool native_base_domain = false;
    std::uint64_t rotation_clamps = 0, position_clamps = 0;
    std::uint64_t window_applied = 0, window_base_domain = 0, window_domain_samples = 0;
    double domain_delta = 0, domain_delta_min = 0, domain_delta_max = 0, render_basis_deviation = 0;
    double rotation_lag_min = 0, rotation_lag_max = 0, position_lag_min = 0, position_lag_max = 0;
    // Report-window CPU timings, enabled by X3M_TELEMETRY (handler only;
    // includes inactive cockpits, excludes the assembly stub/CPU boundary).
    std::uint64_t timed_calls = 0, handler_ticks = 0, handler_max_ticks = 0;
    bool requested = false, installed = false;
    const char* status = "disabled";
    std::uint64_t frames = 0, applied = 0, refused = 0, inactive = 0, unbound = 0, snaps = 0, coalesced = 0, clamps = 0, write_refused = 0, locked_frames = 0, scene_fixed = 0;
    unsigned cockpits_seen = 0;   // distinct cockpit pointers reaching the site since the last report
    std::uint32_t last_verdict = 0, last_snap_reason = 0, view_mode = 0, connect_mode = 0, flags_1a0 = 0, tracking_mode = 0;
    bool target_locked = false;
    double lag_deg = 0, pos_lag = 0, distance = 0, dt_ms = 0, half_vfov_tan = 0, boom_local[3] = {0, 0, 0}, scene_fix_deg = 0;
    // FOV compensation (chase::fov_compensation): the factor and half-FOV tangent recorded at the last
    // change (an applied frame whose factor differs from the last recorded factor by more than 1e-4, so
    // slower drift accumulates until it crosses that hysteresis), the handler frame of that change and the change count.
    double fov_factor = 0, fov_half_vfov_tan = 0;
    std::uint64_t fov_changes = 0, fov_change_frame = 0;
    unsigned atomic_write = 0;
    FirstApplied first;
};
bool wanted();            // X3M_CAMERA == "chase"
bool initialize();        // reads the environment; exact executable; install window; claims the site
bool installed();
const char* status();
Stats stats();
// Process-wide broadcast: observing a snap must not consume another device's
// cut. A fresh history conservatively cuts on the first nonzero generation.
std::uint32_t snap_generation();
// The bolt footprint's view gate, frame-stamped: open only when the handler
// wrote its chase pose (verdict Applied, both writes done) on a visit of the
// active control cockpit after the consumer's mark, taken as
// pose_write_count() at its previous Present, and that visit was the last
// one. Closed when the site is not installed, before the first visit, on a
// frame without an admitted visit (a target or remote view whose view object
// is not the ref object, a load, a menu), and after an internal
// (first-person), front, side, scripted or refused visit. Relaxed atomics: the
// cockpit update, the frame's draws and its Present run on the game's thread
// in that order.
inline bool pose_gate_open(bool installed, bool last_visit_written, std::uint32_t writes, std::uint32_t mark) {
    return installed && last_visit_written && writes != mark;
}
std::uint32_t pose_write_count();
bool pose_applied_since(std::uint32_t mark);
struct SnapCursor {
    std::uint32_t generation = 0;
    // Call only when submitting a cut to the temporal pass (a failed pass
    // invalidates its history), or when history is already invalidated.
    bool observe(std::uint32_t current) {
        const bool changed = current != generation;
        generation = current;
        return changed;
    }
};
// A transition back to vanilla also invalidates the smoothed history, even
// when its angular change is below the ordinary camera-cut threshold.
struct PoseContinuity {
    bool applied = false;
    bool update(bool written, bool snapped) {
        const bool cut = (written && snapped) || (applied && !written);
        applied = written;
        return cut;
    }
};
// The last device of the session was released: the site is kept (logged),
// nothing is restored.
void note_last_device();
void shutdown();          // restores the site's bytes; explicit teardown only, not on the device path
// One `chase_camera` log line (frame_end cadence, from the Present path); the
// first applied frame's inferences are logged once before it.
void report(std::uint64_t frame);
}
