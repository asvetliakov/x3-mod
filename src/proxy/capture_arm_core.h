#pragma once
#include <cstdint>

// Portable core of the delayed F8 capture (X3M_CAPTURE_DELAY, launcher
// --capture-delay): pressing F8 cancels the game's SETA time compression, so an
// immediate capture can never show the compressed case. With a non-zero delay
// the F8 edge only arms the capture; the user re-engages SETA during the delay
// and the burst starts by itself N frames later. No Windows or D3D dependency,
// so the host tests compile this directly.
namespace x3m::capture_arm::core {
// What the caller must do this frame.
enum class Action : unsigned char {
    none = 0,  // nothing: no edge, or an edge that the pending arming swallows
    arm = 1,   // pending capture armed; log capture_armed and keep rendering
    start = 2, // begin the burst now (immediate press, or the delay elapsed)
};
// Pending state; one per device, cleared on Reset with ctx.capture/ctx.remaining.
struct Pending {
    bool armed = false;
    std::uint64_t start_frame = 0;
};
// Upper bound of the setting: 36000 frames, ten minutes at roughly 60 fps.
// A larger value is refused (the delay stays 0, today's behaviour) rather than
// clamped, so a typo cannot silently mean "in an hour".
constexpr unsigned delay_max = 36000;
inline void clear(Pending& pending) { pending.armed = false; pending.start_frame = 0; }

// One call per frame, at the F8 poll: `frame` is the frame that is beginning,
// `edge` the F8 press edge (down now, up last frame), `delay` the configured
// frame delay (0 = immediate, today's behaviour).
//
// While a capture is pending, a further F8 neither re-arms nor cancels it: the
// firing test comes first and the edge is ignored, so a second press cannot
// move the start frame. `>=` rather than `==` so a missed frame (the counter
// only ever advances) still fires instead of arming forever.
inline Action step(Pending& pending, bool edge, std::uint64_t frame, unsigned delay) {
    if (pending.armed) {
        if (frame < pending.start_frame) return Action::none;
        clear(pending);
        return Action::start;
    }
    if (!edge) return Action::none;
    if (!delay) return Action::start;
    pending.armed = true;
    pending.start_frame = frame + delay;
    return Action::arm;
}
// Frames still to wait, for a diagnostic line; 0 when nothing is pending.
inline std::uint64_t frames_left(const Pending& pending, std::uint64_t frame) {
    return pending.armed && frame < pending.start_frame ? pending.start_frame - frame : 0;
}
}
