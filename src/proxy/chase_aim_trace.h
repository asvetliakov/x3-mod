#pragma once
#include <cstdint>

// Read-only, bounded weapon observations for X3M_CAMERA=chase + telemetry.
// Four exact game sites observe cursor-state updates, admission, admitted
// pre-cone ray and chosen muzzle direction. They never change game input, ray, gun or camera state.
// Initialization follows the camera installation, before the install window
// closes; installed stubs remain for the process lifetime.
namespace x3m::chase_aim_trace {
bool initialize();
// Called only for an active control cockpit, after the camera handler's final
// write/pass-through decision. Zero identity invalidates unknown context;
// inactive monitor visits must not call this. Internal view is retained for A/B.
void camera_context(std::uintptr_t cockpit, std::uintptr_t ship, std::uintptr_t camera, std::uint32_t mode,
                    bool applied, std::uint64_t handler_frame = 0);
// Unknown cockpit reads invalidate only a previously accepted current address.
void invalidate_camera(std::uintptr_t cockpit);
void report(std::uint64_t frame);
}
