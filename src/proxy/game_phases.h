#pragma once
#include <cstdint>

// Opt-in, process-lifetime game phase diagnostics. Marker callbacks do not
// acquire capture ownership; Present supplies value metadata while it owns it.
namespace x3m::game_phases {
bool initialize(); // initialize_log only, while engine_patch install window is open
void present_endpoint(std::uintptr_t raw,std::uint64_t device,std::uint64_t reset,
                      std::uint64_t frame,bool captured,std::uint64_t qpc,std::uint32_t result) noexcept;
void invalidate_device() noexcept; // every Reset attempt and final Release, any thread
void report(std::uint64_t reporting_frame); // existing periodic report, owner thread only
// Audio-path witnesses (X3M_AUDIO_SITES=1 with X3M_GAME_PHASES=1): counters
// only, readable from any thread; the line is written by report() per window
// and by the sampling profiler's periodic callback every 2 s.
bool audio_active() noexcept;
void audio_report(const char* scope,std::uint64_t qpc);
#ifdef X3M_GAME_PHASE_FIXTURE
// Configure only while disabled; (0,0) clears before freeing the allocation.
// Caller owns the readable region; every marker still uses engine_memory::read.
bool fixture_pump_region(std::uintptr_t base,std::uint32_t bytes) noexcept;
// Same-thread fixture-only snapshot; four counters are publisher/play/create/seek.
bool fixture_target_state(std::uint64_t* completed4,std::uint64_t* ignored_joins,
                          std::uint64_t* read_failures,unsigned* depth);
void* fixture_emit(unsigned index,void*** next);
void fixture_set_callback(void (__cdecl* callback)(unsigned,const std::uint32_t*));
void fixture_enable(bool enabled);
#endif
}
