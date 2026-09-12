#pragma once
#include <windows.h>
#include <wincrypt.h>
#include <cstdint>

// CryptoAPI context/key cache in front of the game's ADVAPI32 imports
// (X3M_CRYPT_CACHE=1, tools/manage.py launch --crypt-cache). The script
// signature check 0x004cabc0 (docs/reverse-engineering/script-signature-check.md)
// runs, per script: CryptAcquireContextA("X2EgosoftCSPContainer", "Microsoft
// Base Cryptographic Provider v1.0", PROV_RSA_FULL, CRYPT_DELETEKEYSET) [fails:
// the container does not exist], the same with
// CRYPT_NEWKEYSET [creates it], CryptImportKey of a constant 276-byte
// PUBLICKEYBLOB, MD5 hash + CryptVerifySignatureA, CryptDestroyKey,
// CryptReleaseContext, and a final CRYPT_DELETEKEYSET [succeeds]. Under Wine each
// container create/delete is registry work (~4 ms), 844 checks per save load.
//
// The cache keeps the provider handle of the first successful non-delete acquire
// per (container, provider, type) and answers later acquires with the same flags
// from it; CryptReleaseContext of a cached handle only marks it released; the
// deletes are emulated by a three-phase state machine (absent -> in_use ->
// released -> absent) that reproduces exactly what the game observed uncached:
// a delete while absent fails with the last error recorded from the first real
// failure (NTE_BAD_KEYSET if none was seen), a delete after a release succeeds.
// CryptImportKey results are cached per (cached provider, flags, blob bytes) and
// CryptDestroyKey of a cached key is suppressed. Hash creation, hashing,
// verification and the hash parameters pass through untouched, so the
// verification outcome is the original one. A delete while the handle is out
// (the game never does this) evicts the entry without releasing anything: the
// delete passes through, the caller keeps its handle and key, and their later
// release/destroy pass through too. shutdown() releases everything for real and
// deletes a container the game had deleted last (docs/verification/crypt-cache.md).
//
// Compiled with -mno-sse -mno-mmx -mfpmath=387 and integer only (the light TU
// rule of loading_trace_light.h: no logging, no floating point, no 64-bit
// atomics); every entry runs on a game thread with no CPU-state boundary, so it
// preserves only the last error, which it sets deliberately on emulated failures.
namespace x3m::crypt_cache {
using AcquireFn=BOOL (WINAPI*)(HCRYPTPROV*,LPCSTR,LPCSTR,DWORD,DWORD);
using ReleaseFn=BOOL (WINAPI*)(HCRYPTPROV,DWORD);
using ImportFn=BOOL (WINAPI*)(HCRYPTPROV,const BYTE*,DWORD,HCRYPTKEY,DWORD,HCRYPTKEY*);
using DestroyKeyFn=BOOL (WINAPI*)(HCRYPTKEY);
struct Originals { AcquireFn acquire=nullptr; ReleaseFn release=nullptr; ImportFn import_key=nullptr; DestroyKeyFn destroy_key=nullptr; };
constexpr unsigned provider_slots=4,key_slots=4,name_limit=96,blob_limit=512;
bool requested();                          // X3M_CRYPT_CACHE=1
bool initialize(const Originals& originals) noexcept; // binds the real functions (all four required); refused once active
bool enabled() noexcept;
BOOL WINAPI acquire(HCRYPTPROV* out,LPCSTR container,LPCSTR provider,DWORD type,DWORD flags);
BOOL WINAPI release(HCRYPTPROV provider,DWORD flags);
BOOL WINAPI import_key(HCRYPTPROV provider,const BYTE* data,DWORD length,HCRYPTKEY public_key,DWORD flags,HCRYPTKEY* out);
BOOL WINAPI destroy_key(HCRYPTKEY key);
// Real release of every cached key and handle, then a real CRYPT_DELETEKEYSET for
// each container the game had deleted last (so the key store is left as the game
// leaves it). Idempotent; gives up (returns false) if the lock is held, so it is
// safe from DLL_PROCESS_DETACH after other threads were terminated.
bool shutdown() noexcept;
struct Statistics {
    uint64_t acquires=0,hits=0,misses=0,failed_passthrough=0,busy_passthrough=0,deletes_emulated=0,deletes_passthrough=0,evictions=0;
    uint64_t releases=0,releases_suppressed=0,imports=0,import_hits=0,import_passthrough=0,destroys=0,destroys_suppressed=0;
    uint32_t providers_cached=0,keys_cached=0,probe_error=0;
};
Statistics statistics() noexcept; // cumulative, read under the lock
}
