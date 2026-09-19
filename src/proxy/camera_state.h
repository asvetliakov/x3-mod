#pragma once
// Live camera state of the engine's current view: the projection buffer at
// `*0x00608a38` and the view buffer at `*0x00608a40` of the verified X3AP.exe
// (docs/reverse-engineering/camera-state-and-frame-routine.md). Read once per
// scene phase by the motion route at the selector's Clear events, behind the
// same exact-executable identity gate object_trace uses; no code patch, no
// engine memory read for a foreign executable. Values are final from the view
// activation until the next one; P[10]/P[14] are per-submission scratch and
// are never interpreted (camera_reprojection.h reads only the stable terms).
#include <cstdint>
#include "../renderer/camera_reprojection.h"

namespace x3m::camera_state {
enum ReadFailure : std::uint32_t { Ok = 0, Unavailable = 1, NullPointer = 2, Unreadable = 3, Invalid = 4 };
struct Sample {
    renderer::CameraState state;
    renderer::CameraFailure failure = renderer::CameraFailure::None; // matrix validation
    std::uint32_t read_failure = Ok;                                  // ReadFailure
    std::uintptr_t projection = 0, view = 0;                          // buffer addresses (diagnostics)
    float projection_raw[16]{}, view_raw[16]{};
};
// X3M_MOTION_OUTPUT=1 and X3M_TAA=1 only; the executable identity is checked
// once (shared cache with object_trace). Idempotent.
bool initialize();
// A consumer whose option the DLL's own parser accepted after the loader's
// initialize() (the light-map far fade): arms the same read-only latch.
bool request_consumer();
bool available();
const char* status();
// One read of both buffers: the two pointer slots are validated at initialize
// (they live in the image's data section); each buffer pointer is validated
// with VirtualQuery when its value changes and the result cached, so a steady
// state costs two 64-byte copies per call. Returns state.valid.
bool read(Sample* out);
// Drops the pointer validation cache (renderer re-initialization, device Reset).
void reset();
#ifdef X3M_MOTION_OUTPUT_FIXTURE
// Fixture seam (absent from production): the fixture executable supplies its
// own pointer slots in place of the engine globals and bypasses the identity gate.
void fixture_install(const float* const* projection_slot, const float* const* view_slot);
#endif
}
