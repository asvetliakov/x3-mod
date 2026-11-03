#pragma once
#include <cstdint>

// FL edition of the gz read-ahead buffer's import rows (src/engine/fl/gz_buffer_fl.cpp; AP: loading_trace.cpp's
// gz_buffer rows over the shared src/proxy/gz_buffer.cpp). FL edition only.
namespace x3m::fl::gz_buffer_fl {
// X3M_GZ_BUFFER=1 (+ X3M_GZ_BUFFER_KB): the X3FL.exe identity, every gz window, the six named import slots and their
// originals checked, the buffer bound to zlib1.dll's exports, then the six slots written in claim order (gzclose
// chained behind the music keep's stub when the keep holds that slot), all or none, read back; inside the engine_patch
// install window only. One gz_buffer row with edition=fl (nothing when the switch is off). LastError preserved.
bool initialize();
bool active();
const char* state();
#ifdef X3M_GZ_BUFFER_FL_FIXTURE
// The fixture's image base (instead of fl::module_base()) and zlib exports (gzopen, gzread, gzseek, gzgetc, gztell,
// gzclose, gzrewind or null): the fixture has no zlib1.dll.
void fixture_bind(std::uintptr_t base, void* const exports[7]);
void fixture_reset(); // a fresh initialize() (state only; the buffer core binds once per process)
#endif
}
