#pragma once
#include <windows.h>
#include <cstdint>
#include "compositor_bridge.h"

// Engine scene-end boundary (X3M_SCENE_HOOK; default on with the route since
// review 26: unset means on when X3M_MOTION_OUTPUT=1, "1" on, "0" off; the
// fallback chain stays: bloom-copy StretchRect, then the structural selector,
// whenever the patch is absent or refused): a five-byte CALL-rel32 patch of
// the frame routine's compositing callsite 0x004721b1 (`CALL 0x004c4750`,
// docs/reverse-engineering/camera-state-and-frame-routine.md section 7/8) in
// the verified X3AP.exe. The trampoline signals the listener ("scene end,
// compositing begins") and then jumps to the original 0x004c4750, whose
// return lands at 0x004721b6 as before; ESI/EDI/EBX/EBP and the flags are
// preserved around the signal (pushfl/pushal); the x87 state, MXCSR and the
// thread's last error are restored by the signal itself (cpu_state.h, the full
// FNSAVE/FRSTOR boundary: the listener logs and resolves). The callee is
// void(void) with no stack cleanup, so nothing is forwarded. Same install and
// rollback discipline as object_trace: exact-executable identity, expected
// bytes at the site, VirtualProtect/FlushInstructionCache with rollback, the
// Never installs on differing bytes or after engine_patch's install window.
// Optional bloom uses compositor_bridge's full CPU/SEH pre/original/post
// transport after also checking adjacent caller instructions. The production
// site/binding stay installed for the process lifetime; last-device release is
// not proof of code quiescence. Explicit shutdown is for an externally
// quiescent caller (including fixtures), outside DllMain. No on-disk change.
namespace x3m::scene_hook {
using Listener = void (*)();
bool wanted(); // the switch as parsed: "1", or unset with X3M_MOTION_OUTPUT=1 ("0" or unset without the route: off)
// Optional callbacks select the qualified pre/original/post bridge. The hook
// copies the binding and supplies the verified original itself. Configuration
// is immutable after installation; null preserves the ordinary tail jump.
bool initialize(Listener listener, const X3mCompositorBinding* callbacks = nullptr);
bool compositor_active();
std::uintptr_t compositor_caller_pc(); // zero unless our wrapper is active
bool requested();                      // wanted() was seen by initialize
bool installed();                      // our bytes own the site (active or awaiting rollback)
bool active();                         // installed and signalling
const char* status();
unsigned long long signals(); // signals delivered since load (diagnostics)
bool shutdown();              // restore the original bytes; true when nothing is installed
#ifdef X3M_MOTION_OUTPUT_FIXTURE
// Compile-only fixture seam (absent from production): the fixture executable
// supplies its own E8 callsite and expected target; identity gate bypassed.
bool fixture_install(void* callsite, void* expected_target, Listener listener);
bool fixture_install_compositor(void* callsite, void* expected_target, const X3mCompositorBinding* callbacks);
bool fixture_shutdown();
#endif
}
