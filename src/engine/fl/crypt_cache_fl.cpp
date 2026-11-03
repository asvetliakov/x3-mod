// FL edition of the CryptoAPI context/key cache's rows: the AP policy (src/proxy/crypt_cache.cpp, shared) on X3FL.exe's
// four ADVAPI32.dll lifecycle import slots, admitted only from the six return sites of the signature checker
// 0x0f8b50 (crypt_cache_sites_fl.h; docs/reverse-engineering/fl/loading-hooks.md, "Port (2026-11-03)"). Every other
// caller goes to the original export unchanged, except that an unadmitted acquire naming the scratch container (the
// sibling signer 0x0f7c90's delete / new keyset / final delete) first retires every cached object
// (crypt_cache::retire_foreign), so that signer's native namespace is native's. The four entries are plain stdcall
// functions in the slots (the AP rows' shape): no stub, no displaced instruction. Compiled without SSE/MMX, integer
// only (CMakeLists.txt): they run on the game's script thread with no CPU-state boundary. The FL loader has no unload
// path: the slots and the bounded cache stay for the process lifetime (AP's exit policy).
#include "crypt_cache_fl.h"
#include "../../proxy/crypt_cache.h"
#include "../../proxy/engine_patch.h"
#include "crypt_cache_sites_fl.h"
#include "executable_identity_fl.h"
#include "import_slots_fl.h"
#include <windows.h>
#include <atomic>
#include <cstring>
#include <type_traits>

namespace x3m {
void log(const char* format, ...);
}
namespace {
namespace F = x3m::fl::crypt_cache_fl;
namespace W = x3m::fl::loading_windows;
namespace slots = x3m::fl::import_slots;
namespace cache = x3m::crypt_cache;
const char* state_ = "disabled";
const char* window_ = "-";
bool initialized_ = false;
unsigned live_ = 0;
std::uintptr_t base_ = 0;
std::atomic<bool> active_{false};
cache::Originals real_;
const char* scratch_ = F::container_string;
volatile LONG foreign_acquires_ = 0, foreign_retired_ = 0; // 32-bit interlocked (no 64-bit atomics without SSE)
#ifdef X3M_CRYPT_CACHE_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
void* fixture_exports_[4]{};
const char* fixture_scratch_ = nullptr;
#endif
std::uint32_t address_of(const void* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
bool read_self(std::uintptr_t at, void* out, std::size_t n) {
    return x3m::engine_patch::read_code(at, static_cast<unsigned char*>(out), unsigned(n));
}
std::uint32_t load32(std::uintptr_t at) {
    std::uint32_t v = 0;
    return read_self(at, &v, 4) ? v : 0;
}
#ifdef X3M_CRYPT_CACHE_FL_FIXTURE
std::uint32_t fixture_caller_rva_ = 0; // the fixture sets the admitted caller directly (no in-image gate asm)
#endif
// Only the six reviewed call returns inside the checker reach the cache (AP's crypt_site, at base + RVA).
bool admitted(const void* caller, unsigned index) {
    if (!active_.load(std::memory_order_acquire)) return false;
    std::uintptr_t rva = reinterpret_cast<std::uintptr_t>(caller) - base_;
#ifdef X3M_CRYPT_CACHE_FL_FIXTURE
    if (fixture_caller_rva_) rva = fixture_caller_rva_;
#endif
    for (const F::Gate& g : F::gates)
        if (g.return_rva == rva && g.index == index) return true;
    return false;
}
bool scratch_name(LPCSTR container) {
    if (!container) return false;
    unsigned i = 0;
    for (; scratch_[i] && i < cache::name_limit; ++i)
        if (container[i] != scratch_[i]) return false;
    return container[i] == 0;
}
__attribute__((noinline)) BOOL WINAPI acquire_entry(HCRYPTPROV* out, LPCSTR container, LPCSTR provider, DWORD type,
                                                    DWORD flags) {
    if (admitted(__builtin_return_address(0), F::acquire_index))
        return cache::acquire(out, container, provider, type, flags);
    if (active_.load(std::memory_order_acquire) && scratch_name(container)) {
        const DWORD error = GetLastError();
        InterlockedIncrement(&foreign_acquires_);
        InterlockedExchangeAdd(&foreign_retired_, LONG(cache::retire_foreign()));
        SetLastError(error);
    }
    return real_.acquire(out, container, provider, type, flags);
}
__attribute__((noinline)) BOOL WINAPI release_entry(HCRYPTPROV provider, DWORD flags) {
    return admitted(__builtin_return_address(0), F::release_index) ? cache::release(provider, flags)
                                                                   : real_.release(provider, flags);
}
__attribute__((noinline)) BOOL WINAPI import_entry(HCRYPTPROV provider, const BYTE* data, DWORD length,
                                                   HCRYPTKEY public_key, DWORD flags, HCRYPTKEY* out) {
    return admitted(__builtin_return_address(0), F::import_index)
               ? cache::import_key(provider, data, length, public_key, flags, out)
               : real_.import_key(provider, data, length, public_key, flags, out);
}
__attribute__((noinline)) BOOL WINAPI destroy_entry(HCRYPTKEY key) {
    return admitted(__builtin_return_address(0), F::destroy_index) ? cache::destroy_key(key) : real_.destroy_key(key);
}
static_assert(std::is_same_v<decltype(&acquire_entry), decltype(&CryptAcquireContextA)> &&
              std::is_same_v<decltype(&release_entry), decltype(&CryptReleaseContext)> &&
              std::is_same_v<decltype(&import_entry), decltype(&CryptImportKey)> &&
              std::is_same_v<decltype(&destroy_entry), decltype(&CryptDestroyKey)>);
std::uint32_t entry(unsigned i) {
    if (i == F::destroy_index) return address_of(reinterpret_cast<const void*>(&destroy_entry));
    if (i == F::release_index) return address_of(reinterpret_cast<const void*>(&release_entry));
    if (i == F::import_index) return address_of(reinterpret_cast<const void*>(&import_entry));
    return address_of(reinterpret_cast<const void*>(&acquire_entry));
}
bool strings_match() {
    char provider[sizeof F::provider_string]{}, container[sizeof F::container_string]{};
    return read_self(base_ + F::provider_string_rva, provider, sizeof provider) &&
           read_self(base_ + F::container_string_rva, container, sizeof container) &&
           !std::memcmp(provider, F::provider_string, sizeof provider) &&
           !std::memcmp(container, F::container_string, sizeof container);
}
bool install() {
    window_ = "-";
    live_ = 0;
    if (!base_) {
        state_ = "executable_mismatch";
        return false;
    }
    if (!x3m::engine_patch::install_window_open()) {
        state_ = "late_claim";
        return false;
    }
    if (const char* bad = W::first_mismatch(read_self, base_, W::crypt_windows, W::crypt_fixups)) {
        window_ = bad;
        state_ = "bytes_mismatch";
        return false;
    }
    if (!strings_match()) {
        window_ = "strings";
        state_ = "bytes_mismatch";
        return false;
    }
    for (const F::Import& i : F::imports)
        if (slots::named_slot(base_, "ADVAPI32.dll", i.name, read_self) != i.slot_rva) {
            window_ = i.name;
            state_ = "import_mismatch";
            return false;
        }
    std::uint32_t exports[F::import_count]{};
#ifndef X3M_CRYPT_CACHE_FL_FIXTURE
    const HMODULE advapi = GetModuleHandleW(L"ADVAPI32.dll");
    for (unsigned i = 0; i < F::import_count; ++i)
        exports[i] = advapi ? address_of(reinterpret_cast<const void*>(GetProcAddress(advapi, F::imports[i].name))) : 0;
#else
    const unsigned order[F::import_count] = {3, 1, 2, 0}; // fixture_bind order: acquire release import destroy
    for (unsigned i = 0; i < F::import_count; ++i) exports[i] = address_of(fixture_exports_[order[i]]);
#endif
    for (unsigned i = 0; i < F::import_count; ++i) {
        if (!exports[i]) {
            state_ = "advapi_not_loaded";
            return false;
        }
        if (load32(base_ + F::imports[i].slot_rva) != exports[i]) {
            window_ = F::imports[i].name;
            state_ = "original_mismatch";
            return false;
        }
    }
    real_.acquire = reinterpret_cast<cache::AcquireFn>(exports[F::acquire_index]);
    real_.release = reinterpret_cast<cache::ReleaseFn>(exports[F::release_index]);
    real_.import_key = reinterpret_cast<cache::ImportFn>(exports[F::import_index]);
    real_.destroy_key = reinterpret_cast<cache::DestroyKeyFn>(exports[F::destroy_index]);
    // The routes first (they forward natively until admission is published), the cache bound only when all four are
    // ours, then admission; any failure puts every earlier write back in reverse.
    unsigned done = 0;
    while (done < F::import_count && slots::exchange(base_ + F::imports[done].slot_rva, exports[done], entry(done)))
        ++done;
    const bool bound = done == F::import_count && cache::initialize(real_, scratch_);
    if (bound) {
        live_ = done;
        active_.store(true, std::memory_order_release);
        state_ = "ok";
        return true;
    }
    window_ = done < F::import_count ? F::imports[done].name : "-";
    bool rolled_back = true;
    for (unsigned k = done; k-- > 0;) {
        const bool back = slots::exchange(base_ + F::imports[k].slot_rva, entry(k), exports[k]);
        rolled_back = rolled_back && back;
        if (!back) ++live_;
    }
    state_ = !rolled_back ? "rollback_failed" : done < F::import_count ? "slot_not_writable" : "cache_refused";
    return false;
}
}

namespace x3m::fl::crypt_cache_fl {
bool initialize() {
    const DWORD error = GetLastError();
    if (initialized_) {
        SetLastError(error);
        return active_.load();
    }
    initialized_ = true;
    if (!cache::requested()) {
        state_ = "disabled";
        SetLastError(error);
        return false;
    }
#ifdef X3M_CRYPT_CACHE_FL_FIXTURE
    base_ = fixture_base_;
    scratch_ = fixture_scratch_ ? fixture_scratch_ : F::container_string;
#else
    base_ = x3m::fl::module_base();
#endif
    const bool ok = install();
    for (unsigned i = 0; i < import_count; ++i)
        log("loading_hook name=%s installed=%u cache_active=%u slot=0x%08lx edition=fl", imports[i].name,
            unsigned(base_ && load32(base_ + imports[i].slot_rva) == entry(i)), ok ? 1u : 0u,
            static_cast<unsigned long>(base_ + imports[i].slot_rva));
    log("crypt_cache requested=1 enabled=%u telemetry=0 imports=%u provider_slots=%u key_slots=%u blob_limit=%u "
        "reason=%s window=%s live=%u gates=%u foreign=retire edition=fl",
        ok ? 1u : 0u, std::strcmp(state_, "import_mismatch") ? 1u : 0u, cache::provider_slots, cache::key_slots,
        cache::blob_limit, state_, window_, live_, gate_count);
    SetLastError(error);
    return ok;
}
bool active() {
    return active_.load(std::memory_order_acquire);
}
const char* state() {
    return state_;
}
void report(const char* scope) {
    if (!active_.load(std::memory_order_acquire)) return;
    const DWORD error = GetLastError();
    const cache::Statistics s = cache::statistics();
    log("crypt_cache scope=%s acquires=%llu hits=%llu misses=%llu failed_passthrough=%llu releases_suppressed=%llu "
        "imports=%llu import_hits=%llu deletes_emulated=%llu deletes_passthrough=%llu busy_passthrough=%llu "
        "evictions=%llu releases=%llu import_passthrough=%llu destroys=%llu destroys_suppressed=%llu providers_cached=%u "
        "keys_cached=%u probe_error=0x%08x foreign_acquires=%lu foreign_retired=%lu edition=fl",
        scope, (unsigned long long)s.acquires, (unsigned long long)s.hits, (unsigned long long)s.misses,
        (unsigned long long)s.failed_passthrough, (unsigned long long)s.releases_suppressed,
        (unsigned long long)s.imports, (unsigned long long)s.import_hits, (unsigned long long)s.deletes_emulated,
        (unsigned long long)s.deletes_passthrough, (unsigned long long)s.busy_passthrough,
        (unsigned long long)s.evictions, (unsigned long long)s.releases, (unsigned long long)s.import_passthrough,
        (unsigned long long)s.destroys, (unsigned long long)s.destroys_suppressed, s.providers_cached, s.keys_cached,
        unsigned(s.probe_error), static_cast<unsigned long>(foreign_acquires_),
        static_cast<unsigned long>(foreign_retired_));
    SetLastError(error);
}
#ifdef X3M_CRYPT_CACHE_FL_FIXTURE
void fixture_bind(std::uintptr_t base, void* const exports[4], const char* scratch_container) {
    fixture_base_ = base;
    for (unsigned i = 0; i < 4; ++i) fixture_exports_[i] = exports[i];
    fixture_scratch_ = scratch_container;
}
void fixture_call_site(std::uint32_t rva) {
    fixture_caller_rva_ = rva;
}
void fixture_reset() {
    initialized_ = false;
    active_.store(false);
    state_ = "disabled";
    foreign_acquires_ = foreign_retired_ = 0;
}
std::uint64_t fixture_foreign_acquires() {
    return std::uint64_t(foreign_acquires_);
}
std::uint64_t fixture_foreign_retired() {
    return std::uint64_t(foreign_retired_);
}
#endif
}
