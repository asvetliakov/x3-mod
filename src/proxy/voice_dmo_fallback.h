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
void report();     // Present time: formats one line per activation recorded by the hook, and the fault witness line if one was recorded
void shutdown();   // quiescent: disarms the fault witness (last device destroyed, DLL detach); the site patch stays
#ifdef X3M_VOICE_DMO_FIXTURE
// Fixture builds only (voice_startup_replica.cpp, mode game-dmo-hook): the site
// address is a replica function carrying the game's eight bytes; the expected
// bytes, length and register contract stay the production ones.
bool fixture_site(std::uintptr_t address); // before initialize()
const void* fixture_stub();
const void* fixture_tail();
#endif
}
