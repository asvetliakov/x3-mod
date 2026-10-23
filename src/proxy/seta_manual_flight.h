#pragma once

// SETA and manual flight (docs/architecture/seta-manual-flight.md; ledger docs/verification/seta-manual-flight.md;
// engine facts docs/reverse-engineering/seta-manual-flight.md). X3M_SETA_MANUAL_FLIGHT (schema default 2, launcher
// --seta-manual-flight):
//   1 = part A: the input stop 0x0041211d `je` -> `jmp` (one byte), so manual input no longer calls StopFastForward;
//   2 = part A and part B: the yaw/pitch/roll controller spans 0x0040ebde/0x0040ee3e/0x0040f04d and the strafe step
//       loads 0x00411e0c/0x00411eb2 claimed through engine_patch; under SETA (cfg+0xcc > 1.0) each stub scales the
//       controller's ramp step by 1/k^2 and its target by 1/k (k = R / 65536, the clock's effective rate), and the
//       strafe limit by 1/k, so the ship turns and strafes at its unwarped real-time rates;
//   0, empty or anything else = nothing patched.
// Fails closed: executable identity, the install window and every byte window (whole) before any write; a failed step
// rolls the earlier ones back and logs one `seta_manual_flight refused` row naming the window or site.
namespace x3m::seta_manual_flight {
bool initialize();   // backend-load path only (the engine_patch install window); one row when the variable is set
bool shutdown();     // dynamic unload only: the five claims and the stop byte back
const char* state(); // ok / disabled / <refusal>
// Stores the Present frame counter for the debug row (one relaxed store, only while part B is live under debug = 1).
void present(unsigned long long frame);
}
