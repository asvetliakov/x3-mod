#include "crypt_cache.h"
#include "config.h"

// Compiled with -mno-sse -mno-mmx -mfpmath=387, integer only, no logging: see
// crypt_cache.h and loading_trace_light.h. Every state change happens under one
// spinlock (the script VM runs the check from one thread; the lock is there for
// the two-caller case 0x004cae90 and for shutdown). Counters are plain 64-bit
// fields written under the lock and read under it by statistics().
namespace x3m::crypt_cache {
namespace {
enum Phase : unsigned { Absent = 0, InUse = 1, Released = 2 };
struct ProviderEntry {
    bool used = false, container_null = false, provider_null = false, probe_error_known = false;
    char container[name_limit]{}, provider[name_limit]{};
    DWORD type = 0, flags = 0, probe_error = 0;
    HCRYPTPROV handle = 0;
    unsigned phase = Absent;
    DWORD owner_thread = 0;
};
struct KeyEntry {
    bool used = false;
    HCRYPTPROV provider = 0;
    DWORD flags = 0, length = 0;
    HCRYPTKEY key = 0;
    unsigned outstanding = 0;
    BYTE blob[blob_limit]{};
};
ProviderEntry providers[provider_slots];
KeyEntry keys[key_slots];
Originals real;
Statistics totals;
uint64_t mutation_generation = 0; // under lock; reject late native completions
char allowed_container[name_limit]{};
constexpr const char* allowed_provider = "Microsoft Base Cryptographic Provider v1.0";
volatile LONG lock_word = 0;
volatile LONG is_enabled = 0;

struct Lock {
    bool held;
    explicit Lock(unsigned spins = 0) noexcept
        : held(false) {
        for (unsigned i = 0;; ++i) {
            if (InterlockedCompareExchange(&lock_word, 1, 0) == 0) {
                held = true;
                return;
            }
            if (spins && i >= spins) return;
            YieldProcessor();
            if ((i & 0x3ff) == 0x3ff) Sleep(0);
        }
    }
    ~Lock() noexcept {
        if (held) InterlockedExchange(&lock_word, 0);
    }
};
bool copy_name(char* out, LPCSTR in, bool& null_flag) noexcept {
    null_flag = in == nullptr;
    unsigned i = 0;
    if (in)
        for (; in[i]; ++i) {
            if (i + 1 >= name_limit) return false;
            out[i] = in[i];
        }
    out[i] = 0;
    return true;
}
bool same_name(const char* stored, bool stored_null, LPCSTR in) noexcept {
    if (stored_null || !in) return stored_null && !in;
    unsigned i = 0;
    for (; stored[i] && in[i]; ++i)
        if (stored[i] != in[i]) return false;
    return stored[i] == in[i];
}
ProviderEntry* find_provider(LPCSTR container, LPCSTR provider, DWORD type) noexcept {
    for (auto& e : providers)
        if (e.used && e.type == type && same_name(e.container, e.container_null, container) &&
            same_name(e.provider, e.provider_null, provider))
            return &e;
    return nullptr;
}
ProviderEntry* find_provider(HCRYPTPROV handle) noexcept {
    if (!handle) return nullptr;
    for (auto& e : providers)
        if (e.used && e.handle == handle) return &e;
    return nullptr;
}
ProviderEntry* claim_provider(LPCSTR container, LPCSTR provider, DWORD type) noexcept {
    for (auto& e : providers) {
        if (e.used) continue;
        if (!copy_name(e.container, container, e.container_null) || !copy_name(e.provider, provider, e.provider_null))
            return nullptr;
        e.type = type;
        e.flags = 0;
        e.probe_error = 0;
        e.probe_error_known = false;
        e.handle = 0;
        e.phase = Absent;
        e.used = true;
        return &e;
    }
    return nullptr;
}
KeyEntry* find_key(HCRYPTKEY key) noexcept {
    if (!key) return nullptr;
    for (auto& k : keys)
        if (k.used && k.key == key) return &k;
    return nullptr;
}
KeyEntry* find_key(HCRYPTPROV provider, DWORD flags, const BYTE* data, DWORD length) noexcept {
    for (auto& k : keys) {
        if (!k.used || k.provider != provider || k.flags != flags || k.length != length) continue;
        DWORD i = 0;
        for (; i < length; ++i)
            if (k.blob[i] != data[i]) break;
        if (i == length) return &k;
    }
    return nullptr;
}
// Retire only cache-owned keys outside the lock. Outstanding native handles
// remain with their caller; their subsequent destroy/release goes to native.
struct RetiredKeys {
    HCRYPTKEY handles[key_slots]{};
    unsigned count = 0;
    void destroy() noexcept {
        for (unsigned i = 0; i < count; ++i) real.destroy_key(handles[i]);
        count = 0;
    }
};
void evict(ProviderEntry& e, RetiredKeys& retired) noexcept {
    for (auto& k : keys)
        if (k.used && k.provider == e.handle) {
            if (!k.outstanding) retired.handles[retired.count++] = k.key;
            k.used = false;
            k.key = 0;
            k.outstanding = 0;
            --totals.keys_cached;
        }
    e.handle = 0;
    e.phase = Absent;
    e.flags = 0;
    e.owner_thread = 0;
    ++totals.evictions;
    --totals.providers_cached;
}
}

bool requested() {
    wchar_t setting[8]{};
    return x3m::config::get(L"X3M_CRYPT_CACHE", setting, 8) == 1 && setting[0] == L'1';
}
bool initialize(const Originals& originals, const char* scratch_container) noexcept {
    if (!originals.acquire || !originals.release || !originals.import_key || !originals.destroy_key) return false;
    Lock lock;
    if (is_enabled) return false;
    bool null_name = false;
    if (!scratch_container || !copy_name(allowed_container, scratch_container, null_name)) return false;
    real = originals;
    InterlockedExchange(&is_enabled, 1);
    return true;
}
bool enabled() noexcept {
    return is_enabled != 0;
}

BOOL WINAPI acquire(HCRYPTPROV* out, LPCSTR container, LPCSTR provider, DWORD type, DWORD flags) {
    if (!is_enabled) return real.acquire(out, container, provider, type, flags);
    // Only the disassembled game's scratch sequence is admitted by integration.
    // In particular MACHINE_KEYSET and combined/unknown flags are not aliases.
    if (!out || type != PROV_RSA_FULL || !same_name(allowed_container, false, container) ||
        !same_name(allowed_provider, false, provider) || (flags != CRYPT_DELETEKEYSET && flags != CRYPT_NEWKEYSET))
        return real.acquire(out, container, provider, type, flags);
    const DWORD caller_error = GetLastError();
    uint64_t generation = 0;
    if (flags == CRYPT_DELETEKEYSET) {
        RetiredKeys retired;
        {
            Lock lock;
            generation = ++mutation_generation;
            ++totals.acquires;
            ProviderEntry* e = find_provider(container, provider, type);
            if (e && e->handle) {
                if (e->phase == Released) {
                    e->phase = Absent;
                    ++totals.deletes_emulated;
                    SetLastError(caller_error);
                    return TRUE;
                }
                if (e->phase == Absent) {
                    ++totals.deletes_emulated;
                    SetLastError(e->probe_error_known ? e->probe_error : DWORD(NTE_BAD_KEYSET));
                    return FALSE;
                }
                evict(*e, retired); // a delete while the handle is out: not the game's sequence; pass it through
            }
            ++totals.deletes_passthrough;
        }
        retired.destroy();
        SetLastError(caller_error);
        const BOOL result = real.acquire(out, container, provider, type, flags);
        const DWORD result_error = GetLastError();
        if (!result) {
            Lock lock;
            ++totals.failed_passthrough;
            ProviderEntry* e = mutation_generation == generation ? find_provider(container, provider, type) : nullptr;
            if (!e && mutation_generation == generation) e = claim_provider(container, provider, type);
            if (e && !e->probe_error_known) {
                e->probe_error = result_error;
                e->probe_error_known = true;
                totals.probe_error = result_error;
            }
        }
        SetLastError(result_error);
        return result;
    }
    {
        Lock lock;
        generation = ++mutation_generation;
        ++totals.acquires;
        ProviderEntry* e = find_provider(container, provider, type);
        if (e && e->handle) {
            if (e->phase == Absent && e->flags == flags && out) {
                *out = e->handle;
                e->phase = InUse;
                e->owner_thread = GetCurrentThreadId();
                ++totals.hits;
                SetLastError(caller_error);
                return TRUE;
            }
            ++totals.busy_passthrough; // handle out or a different flag set: served by the real call, not cached
        }
    }
    SetLastError(caller_error);
    const BOOL result = real.acquire(out, container, provider, type, flags);
    const DWORD result_error = GetLastError();
    {
        Lock lock;
        if (!result)
            ++totals.failed_passthrough;
        else {
            ++totals.misses;
            if (out && *out && mutation_generation == generation) {
                ProviderEntry* e = find_provider(container, provider, type);
                if (!e) e = claim_provider(container, provider, type);
                if (e && !e->handle) {
                    e->handle = *out;
                    e->flags = flags;
                    e->phase = InUse;
                    e->owner_thread = GetCurrentThreadId();
                    ++totals.providers_cached;
                }
            }
        }
    }
    SetLastError(result_error);
    return result;
}
BOOL WINAPI release(HCRYPTPROV provider, DWORD flags) {
    if (!is_enabled) return real.release(provider, flags);
    const DWORD caller_error = GetLastError();
    RetiredKeys retired;
    {
        Lock lock;
        ++mutation_generation;
        ++totals.releases;
        ProviderEntry* e = find_provider(provider);
        bool key_outstanding = false;
        if (e)
            for (const auto& k : keys) key_outstanding |= k.used && k.provider == provider && k.outstanding;
        if (e && e->phase == InUse && !key_outstanding && flags == 0 && e->owner_thread == GetCurrentThreadId()) {
            e->phase = Released;
            e->owner_thread = 0;
            ++totals.releases_suppressed;
            SetLastError(caller_error);
            return TRUE;
        }
        if (e) evict(*e, retired); // real release can invalidate the context even on bad flags
    }
    retired.destroy();
    SetLastError(caller_error);
    return real.release(provider, flags);
}
BOOL WINAPI import_key(HCRYPTPROV provider, const BYTE* data, DWORD length, HCRYPTKEY public_key, DWORD flags,
                       HCRYPTKEY* out) {
    if (!is_enabled) return real.import_key(provider, data, length, public_key, flags, out);
    const DWORD caller_error = GetLastError();
    // Only blobs imported into a cached provider, without a wrapping key, of a
    // bounded size are cached; anything else is the real call.
    uint64_t generation = 0;
    const bool cacheable = data && out && length == 276 && public_key == 0 && flags == 0 && data[0] == PUBLICKEYBLOB &&
                           data[1] == CUR_BLOB_VERSION && data[2] == 0 && data[3] == 0 && data[4] == 0 &&
                           (data[5] == BYTE(CALG_RSA_SIGN >> 8) || data[5] == BYTE(CALG_RSA_KEYX >> 8)) &&
                           data[6] == 0 && data[7] == 0 && data[8] == 'R' && data[9] == 'S' && data[10] == 'A' &&
                           data[11] == '1' && data[12] == 0 && data[13] == 8 && data[14] == 0 && data[15] == 0;
    {
        Lock lock;
        generation = ++mutation_generation;
        ++totals.imports;
        ProviderEntry* e = cacheable ? find_provider(provider) : nullptr;
        if (e && e->phase == InUse && e->owner_thread == GetCurrentThreadId()) {
            if (KeyEntry* k = find_key(provider, flags, data, length); k && !k->outstanding) {
                *out = k->key;
                ++k->outstanding;
                ++totals.import_hits;
                SetLastError(caller_error);
                return TRUE;
            }
        } else
            ++totals.import_passthrough;
    }
    SetLastError(caller_error);
    const BOOL result = real.import_key(provider, data, length, public_key, flags, out);
    const DWORD result_error = GetLastError();
    if (result && cacheable && *out) {
        Lock lock;
        ProviderEntry* e = mutation_generation == generation ? find_provider(provider) : nullptr;
        if (e && e->phase == InUse && e->owner_thread == GetCurrentThreadId() &&
            !find_key(provider, flags, data, length)) {
            for (auto& k : keys) {
                if (k.used) continue;
                k.provider = provider;
                k.flags = flags;
                k.length = length;
                k.key = *out;
                k.outstanding = 1;
                for (DWORD i = 0; i < length; ++i) k.blob[i] = data[i];
                k.used = true;
                ++totals.keys_cached;
                break;
            }
        }
    }
    SetLastError(result_error);
    return result;
}
BOOL WINAPI destroy_key(HCRYPTKEY key) {
    if (!is_enabled) return real.destroy_key(key);
    const DWORD caller_error = GetLastError();
    {
        Lock lock;
        ++mutation_generation;
        ++totals.destroys;
        if (KeyEntry* k = find_key(key)) {
            const auto* e = find_provider(k->provider);
            if (k->outstanding && e && e->phase == InUse && e->owner_thread == GetCurrentThreadId()) {
                --k->outstanding;
                ++totals.destroys_suppressed;
                SetLastError(caller_error);
                return TRUE;
            }
            k->used = false;
            k->key = 0;
            --totals.keys_cached; // duplicate destroy goes to the real API
        }
    }
    SetLastError(caller_error);
    return real.destroy_key(key);
}
bool shutdown() noexcept {
    // Explicit quiescent caller only, never DllMain. All CSP calls occur after
    // releasing our lock. Production retains this bounded cache until exit.
    HCRYPTKEY retired_keys[key_slots]{};
    ProviderEntry retired_providers[provider_slots];
    unsigned key_count = 0, provider_count = 0;
    {
        Lock lock(4096);
        if (!lock.held) return false;
        if (!is_enabled) return true;
        for (const auto& e : providers)
            if (e.used && e.phase == InUse) return false;
        for (const auto& k : keys)
            if (k.used && k.outstanding) return false;
        ++mutation_generation;
        InterlockedExchange(&is_enabled, 0);
        for (auto& k : keys)
            if (k.used) {
                retired_keys[key_count++] = k.key;
                k.used = false;
                k.key = 0;
                --totals.keys_cached;
            }
        for (auto& e : providers)
            if (e.used) {
                retired_providers[provider_count++] = e;
                if (e.handle) --totals.providers_cached;
                e.used = false;
                e.handle = 0;
            }
    }
    for (unsigned i = 0; i < key_count; ++i) real.destroy_key(retired_keys[i]);
    for (unsigned i = 0; i < provider_count; ++i) {
        const auto& e = retired_providers[i];
        if (!e.handle) continue;
        real.release(e.handle, 0);
        if (e.phase == Absent) {
            HCRYPTPROV scratch = 0;
            real.acquire(&scratch, e.container, e.provider, e.type, CRYPT_DELETEKEYSET);
        }
    }
    return true;
}
Statistics statistics() noexcept {
    Lock lock;
    Statistics copy;
    copy.acquires = totals.acquires;
    copy.hits = totals.hits;
    copy.misses = totals.misses;
    copy.failed_passthrough = totals.failed_passthrough;
    copy.busy_passthrough = totals.busy_passthrough;
    copy.deletes_emulated = totals.deletes_emulated;
    copy.deletes_passthrough = totals.deletes_passthrough;
    copy.evictions = totals.evictions;
    copy.releases = totals.releases;
    copy.releases_suppressed = totals.releases_suppressed;
    copy.imports = totals.imports;
    copy.import_hits = totals.import_hits;
    copy.import_passthrough = totals.import_passthrough;
    copy.destroys = totals.destroys;
    copy.destroys_suppressed = totals.destroys_suppressed;
    copy.providers_cached = totals.providers_cached;
    copy.keys_cached = totals.keys_cached;
    copy.probe_error = totals.probe_error;
    return copy;
}
}
