#pragma once
#include <cstdint>

// Opt-in (X3M_VOICE_DMO_FALLBACK=1, set by `manage.py launch --voice-decoder`)
// post-call hook at the media constructor's IDMOWrapperFilter::Init return
// (0x004cfd46): when the game's speech-decoder DMO is not registered
// (REGDB_E_CLASSNOTREG) the wrapper is re-initialised with the registered WMA
// decoder DMO so the graph can pause, run and be torn down
// (docs/architecture/voice-decoder-adapter.md, "DMO fallback hook";
// docs/reverse-engineering/voice-startup-sequence.md sections 12-13).
// Inert where Init succeeds (native Windows). One byte-verified site, claimed
// only while the engine_patch install window is open, rolled back on failure.
namespace x3m::voice_dmo_fallback {
bool initialize(); // capture initialize_log only, after game_phases::initialize
void report();     // Present time: formats one line per activation recorded by the hook
}
