#pragma once
#include <cstdint>

// Opt-in, process-lifetime game phase diagnostics. Marker callbacks do not
// acquire capture ownership; Present supplies value metadata while it owns it.
namespace x3m::game_phases {
bool initialize(); // initialize_log only, while engine_patch install window is open
void present_endpoint(std::uintptr_t raw, std::uint64_t device, std::uint64_t reset, std::uint64_t frame, bool captured,
                      std::uint64_t qpc, std::uint32_t result) noexcept;
void invalidate_device() noexcept; // every Reset attempt and final Release, any thread
// Present-cadence loading markers, every mode (no X3M_GAME_PHASES, no engine
// site): one `loading_phase` line per transition per process. Present path
// only, under the capture mutex; one QueryPerformanceCounter per call.
// Returns true when this call wrote the save_load_complete marker.
bool loading_phase_present(std::uint64_t device, std::uint64_t reset, std::uint64_t frame) noexcept;
void report(std::uint64_t reporting_frame); // existing periodic report, owner thread only
// The register-saving stub that enters x3m_game_phase_enter with `index`;
// indices at or above sites::Count are routed to frame_phases::stamp.
void* emit_stub(unsigned index, void*** next);
// The Input phase (site game_phase_input, 0x00403b09) of the last completed
// main-loop iteration in microseconds; false when the group is off, on any
// thread but the owner, or before the first completed Input phase. Read by the
// loop-phase group at the frame boundary (the owner is the Present thread).
bool last_input_us(std::uint64_t* out) noexcept;
#ifdef X3M_GAME_PHASE_FIXTURE
// Configure only while disabled; (0,0) clears before freeing the allocation.
// Caller owns the readable region; every marker still uses engine_memory::read.
bool fixture_pump_region(std::uintptr_t base, std::uint32_t bytes) noexcept;
// Same-thread fixture-only snapshot; four counters are publisher/play/create/seek.
bool fixture_target_state(std::uint64_t* completed4, std::uint64_t* ignored_joins, std::uint64_t* read_failures,
                          unsigned* depth);
void* fixture_emit(unsigned index, void*** next);
void fixture_set_callback(void(__cdecl* callback)(unsigned, const std::uint32_t*));
void fixture_enable(bool enabled);
#endif
}
