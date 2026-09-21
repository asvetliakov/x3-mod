#pragma once
#include <cstdint>

// Startup-only, opt-in caller-owned installation. There is no environment,
// Store/device ownership, loading hook or F8 wiring in this component.
namespace x3m::lattice_upload_hook {
bool initialize(); // Verified EXE + whole surrounding instructions + install window.
bool installed();  // Includes a live patch retained after rollback failure.
const char* status();
// Arm the Store BEFORE enabling; disable BEFORE requesting observer retirement.
// Already-entered calls keep the ABI observer's own completion/abort ownership.
// This flag changes only routing, never frees executable or observer storage.
bool set_armed(bool);
// Caller owes a safe install/rollback window or external code quiescence.
// Device retirement must NOT call this: patch/arena/module have process lifetime.
bool shutdown_quiescent();
#ifdef X3M_LATTICE_UPLOAD_HOOK_FIXTURE
bool fixture_install(void* site);
const void* fixture_stub();
const void* fixture_tail();
bool fixture_atomic_write();
unsigned fixture_context(unsigned char* output, unsigned capacity);
constexpr unsigned fixture_context_prefix = 18;
#endif
}
