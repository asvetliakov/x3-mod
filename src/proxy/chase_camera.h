#pragma once
#include <windows.h>
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
// camera (+0x30 position, +0x40 basis) and cockpit+0xf0, so scene, HUD
// overlay, mouse-aim ray and the proxy's TAA reprojection all see one camera
// (docs/architecture/chase-camera.md). Internal, front, side and scripted
// views pass through untouched: the game's own view keys stay the switch.
//
// Fail closed: exact executable (object_trace::executable_verified), exact
// site bytes, install window (engine_patch), every engine pointer read
// through engine_memory with bounded validated reads, writes only to the two
// objects just read, NaN/denormal/range guards in the pipeline; any refusal
// leaves the vanilla pose for that frame. Initialization on the backend-load
// path, restore when the last device is released.
namespace x3m::chase_camera {
struct Stats {
    bool requested = false, installed = false;
    const char* status = "disabled";
    std::uint64_t frames = 0, applied = 0, refused = 0, snaps = 0, clamps = 0, write_refused = 0;
    std::uint32_t last_verdict = 0, last_snap_reason = 0, view_mode = 0, connect_mode = 0;
    double lag_deg = 0, pos_lag = 0, distance = 0, dt_ms = 0;
    unsigned atomic_write = 0;
};
bool wanted();            // X3M_CAMERA == "chase"
bool initialize();        // reads the environment; exact executable; install window; claims the site
bool installed();
const char* status();
Stats stats();
// A snap happened since the last call (the temporal route feeds it into the
// resolve's cut verdict: the smoothed history is discontinuous at a snap).
bool take_snap();
void shutdown();          // restores the site's bytes
// One `chase_camera` log line (frame_end cadence, from the Present path).
void report(std::uint64_t frame);
}
