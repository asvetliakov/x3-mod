#pragma once
#include <windows.h>
#include <wincrypt.h>
#include <cstdint>

// Bounded scratch-provider/public-key reuse for the reviewed game signature
// verifier only. loading_trace qualifies six exact game return sites, argument
// bytes and all four lifecycle IAT slots before publishing cache activation.
// Other callers remain native. No backend layout/export/hash qualification.
//
// Only the configured scratch name, Microsoft Base provider, PROV_RSA_FULL and
// exact NEWKEYSET/DELETEKEYSET flags qualify. Imports require an RSA-2048 public
// blob, no wrapping key and flags zero. A cached key is reissued only after its
// logical destroy, on the owning thread while its provider is in use. Native
// completions crossing another observed mutation cannot publish cache entries.
// This is not a general CryptoAPI emulation: persistent named-container lifetime
// is intentionally longer than the game's delete/create loop. No external keyset
// writer or unobserved handle escape is within the reviewed game-site contract.
//
// No CSP call occurs under the cache lock or in DllMain. Production keeps the
// bounded native objects until process exit. The named scratch container can
// remain afterward; the next game's first real delete removes it. Explicit
// quiescent shutdown (fixtures only) releases the objects and removes an absent-
// phase container outside loader lock. It is not safe concurrent with API calls.
//
// Integer-only TU, compiled without SSE/MMX; no x87 arithmetic/state boundary on
// hot calls. LastError is preserved around cache bookkeeping and set explicitly
// for an emulated absent-delete failure. See docs/verification/crypt-cache.md.
namespace x3m::crypt_cache {
using AcquireFn=BOOL (WINAPI*)(HCRYPTPROV*,LPCSTR,LPCSTR,DWORD,DWORD);
using ReleaseFn=BOOL (WINAPI*)(HCRYPTPROV,DWORD);
using ImportFn=BOOL (WINAPI*)(HCRYPTPROV,const BYTE*,DWORD,HCRYPTKEY,DWORD,HCRYPTKEY*);
using DestroyKeyFn=BOOL (WINAPI*)(HCRYPTKEY);
struct Originals { AcquireFn acquire=nullptr; ReleaseFn release=nullptr; ImportFn import_key=nullptr; DestroyKeyFn destroy_key=nullptr; };
constexpr unsigned provider_slots=4,key_slots=4,name_limit=96,blob_limit=512;
bool requested();                          // X3M_CRYPT_CACHE=1
bool initialize(const Originals& originals, const char* scratch_container="X2EgosoftCSPContainer") noexcept; // exact scratch name may be changed only by a standalone fixture
bool enabled() noexcept;
BOOL WINAPI acquire(HCRYPTPROV* out,LPCSTR container,LPCSTR provider,DWORD type,DWORD flags);
BOOL WINAPI release(HCRYPTPROV provider,DWORD flags);
BOOL WINAPI import_key(HCRYPTPROV provider,const BYTE* data,DWORD length,HCRYPTKEY public_key,DWORD flags,HCRYPTKEY* out);
BOOL WINAPI destroy_key(HCRYPTKEY key);
// Explicit quiescent cleanup only; NEVER call from DllMain. Refuses known
// outstanding provider/key ownership. Native operations happen outside the lock.
bool shutdown() noexcept;
struct Statistics {
    uint64_t acquires=0,hits=0,misses=0,failed_passthrough=0,busy_passthrough=0,deletes_emulated=0,deletes_passthrough=0,evictions=0;
    uint64_t releases=0,releases_suppressed=0,imports=0,import_hits=0,import_passthrough=0,destroys=0,destroys_suppressed=0;
    uint32_t providers_cached=0,keys_cached=0,probe_error=0;
};
Statistics statistics() noexcept; // cumulative, read under the lock
}
