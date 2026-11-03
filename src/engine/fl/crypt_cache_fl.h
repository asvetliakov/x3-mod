#pragma once
#include <cstdint>

// FL edition of the CryptoAPI context/key cache's rows (src/engine/fl/crypt_cache_fl.cpp; AP: loading_trace.cpp's
// crypt_cache rows over the shared src/proxy/crypt_cache.cpp). FL edition only.
namespace x3m::fl::crypt_cache_fl {
// X3M_CRYPT_CACHE=1: the X3FL.exe identity, the checker and sibling windows, the two strings, the four named
// ADVAPI32.dll import slots holding their exports; the four slots written (CryptAcquireContextA last), read back, the
// cache bound, then admission published; all or none, inside the engine_patch install window. One crypt_cache row and
// four loading_hook rows with edition=fl (nothing when the switch is off). LastError preserved.
bool initialize();
bool active();
const char* state();
// One crypt_cache row with the cache's counters (scope=session: cumulative; nothing before activation). LastError
// preserved.
void report(const char* scope);
#ifdef X3M_CRYPT_CACHE_FL_FIXTURE
// The fixture's image base and the four functions it treats as ADVAPI32's exports (acquire, release, import, destroy).
void fixture_bind(std::uintptr_t base, void* const exports[4], const char* scratch_container);
void fixture_reset();
void fixture_call_site(std::uint32_t rva); // the admitted caller RVA for the next entry call (0 = unadmitted)
std::uint64_t fixture_foreign_acquires();
std::uint64_t fixture_foreign_retired();
#endif
}
