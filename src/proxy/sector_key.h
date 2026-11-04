#pragma once
// Sector key, the edition part (sector_key_core.h; docs/reverse-engineering/sector-identity.md section 3;
// docs/architecture/sector-background-modern.md, "Implementation A, per-sector seed"). Both editions: the AP build
// binds X3AP.exe (base 0x400000, admitted by object_trace::executable_verified()), the FL build X3FL.exe
// (fl::module_base(), admitted by fl::verified()). At the first sample the running edition's byte windows are compared
// once over their whole length (relocated dwords rebased); a refusal turns the key off for the process (every sample
// returns unknown(disabled)), never the star pass. Render thread only (capture.cpp's sector_background_context, inside
// the device call's CPU boundary and LastError restore). No hook, no patch, no engine call, no allocation.
#include "sector_key_core.h"

namespace x3m::sector_key {
// The key of the class-1 sector object `sector` (bounded reads through engine_memory::read, cached on
// (S, [S+8], [S+0x94])). The first call decides the windows and writes one sky_stars_sector_check row. LastError
// preserved.
core::Result sample(std::uint32_t sector) noexcept;
// Drops the cache: the next sample walks the chain again (a load, a stall).
void invalidate() noexcept;
const char* edition() noexcept; // "ap" or "fl"
const char* status() noexcept;  // undecided, ready, executable_mismatch, window_mismatch, base_overflow
} // namespace x3m::sector_key
