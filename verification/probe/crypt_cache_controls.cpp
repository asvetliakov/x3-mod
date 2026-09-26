// Adversarial lifetime controls using deterministic Win32 CryptoAPI-shaped spies.
// This complements the real-CSP signature differential; it never owns a keyset.
#include "../../src/proxy/crypt_cache.h"
#include <cstdio>
#include <cstring>
#include <thread>
namespace cc = x3m::crypt_cache;
namespace {
unsigned checks = 0, failures = 0, acquires = 0, releases = 0, imports = 0, destroys = 0;
ULONG_PTR next = 10;
bool callbacks = false, inside_callback = false, reenter_acquire = false, reenter_import = false;
const char* name = "X3mCryptCacheControls";
const char* provider = "Microsoft Base Cryptographic Provider v1.0";
BYTE blob[276]{};
struct State {
    unsigned char fp[108];
    unsigned mxcsr;
    DWORD error;
    State() {
        asm volatile("fnsave %0\n\tfrstor %0\n\tstmxcsr %1" : "=m"(fp), "=m"(mxcsr)::"memory");
        error = GetLastError();
    }
    void restore() const {
        SetLastError(error);
        asm volatile("frstor %0\n\tldmxcsr %1" ::"m"(fp), "m"(mxcsr) : "memory");
    }
};
void seed() {
    const unsigned short cw = 0x077f;
    const unsigned mxcsr = 0x3f80;
    asm volatile("fninit\n\tfld1\n\tfldpi\n\tfldcw %0\n\tldmxcsr %1" ::"m"(cw), "m"(mxcsr) : "memory");
    SetLastError(0x173945ab);
}
bool same(const State& a, const State& b) {
    return a.error == b.error && a.mxcsr == b.mxcsr && !std::memcmp(a.fp, b.fp, 108);
}
void check(bool v, const char* n) {
    ++checks;
    if (!v) ++failures;
    std::printf("CHECK %s %s\n", n, v ? "PASS" : "FAIL");
}
void callback() {
    if (callbacks && !inside_callback) {
        inside_callback = true;
        cc::statistics();
        inside_callback = false;
    }
}
BOOL WINAPI acquire(HCRYPTPROV* out, LPCSTR, LPCSTR, DWORD, DWORD flags) {
    ++acquires;
    callback();
    if (reenter_acquire && flags == CRYPT_NEWKEYSET) {
        reenter_acquire = false;
        HCRYPTPROV other = 0;
        cc::acquire(&other, name, provider, 1, CRYPT_DELETEKEYSET);
    }
    if (flags == CRYPT_DELETEKEYSET) {
        SetLastError(NTE_BAD_KEYSET);
        return FALSE;
    }
    if (out) *out = ++next;
    return TRUE;
}
BOOL WINAPI release(HCRYPTPROV, DWORD flags) {
    ++releases;
    callback();
    if (flags) {
        SetLastError(NTE_BAD_FLAGS);
        return FALSE;
    }
    return TRUE;
}
BOOL WINAPI import(HCRYPTPROV, const BYTE*, DWORD, HCRYPTKEY, DWORD, HCRYPTKEY* out) {
    ++imports;
    callback();
    if (reenter_import) {
        reenter_import = false;
        HCRYPTPROV other = 0;
        cc::acquire(&other, name, provider, 1, CRYPT_DELETEKEYSET);
    }
    if (out) *out = ++next;
    return TRUE;
}
BOOL WINAPI destroy(HCRYPTKEY) {
    ++destroys;
    callback();
    return TRUE;
}
void absent() {
    HCRYPTPROV p = 0;
    cc::acquire(&p, name, provider, PROV_RSA_FULL, CRYPT_DELETEKEYSET);
}
HCRYPTPROV begin() {
    absent();
    HCRYPTPROV p = 0;
    check(cc::acquire(&p, name, provider, PROV_RSA_FULL, CRYPT_NEWKEYSET) == TRUE, "begin");
    return p;
}
HCRYPTKEY key(HCRYPTPROV p) {
    HCRYPTKEY k = 0;
    check(cc::import_key(p, blob, sizeof blob, 0, 0, &k) == TRUE, "import");
    return k;
}
void end(HCRYPTPROV p) {
    check(cc::release(p, 0) == TRUE, "end release");
    absent();
}
}
int main() {
    blob[0] = PUBLICKEYBLOB;
    blob[1] = CUR_BLOB_VERSION;
    blob[5] = BYTE(CALG_RSA_SIGN >> 8);
    blob[8] = 'R';
    blob[9] = 'S';
    blob[10] = 'A';
    blob[11] = '1';
    blob[13] = 8;
    cc::Originals real{acquire, release, import, destroy};
    check(cc::initialize(real, name), "initialize");
    HCRYPTPROV fp_provider = 0;
    HCRYPTKEY fp_key = 0;
    absent();
    {
        State original;
        seed();
        State before;
        const BOOL acquired = cc::acquire(&fp_provider, name, provider, 1, CRYPT_NEWKEYSET);
        State after;
        original.restore();
        check(acquired && same(before, after), "provider miss preserves seeded native CPU state and success LastError");
    }
    {
        State original;
        seed();
        State before;
        const BOOL imported = cc::import_key(fp_provider, blob, sizeof blob, 0, 0, &fp_key);
        State after;
        original.restore();
        check(imported && same(before, after), "key miss preserves seeded native CPU state and success LastError");
    }
    cc::destroy_key(fp_key);
    end(fp_provider);
    {
        State original;
        seed();
        State before;
        HCRYPTPROV hit = 0;
        const BOOL acquired = cc::acquire(&hit, name, provider, 1, CRYPT_NEWKEYSET);
        State after;
        original.restore();
        check(acquired && hit == fp_provider && same(before, after),
              "provider hit preserves full seeded CPU state and success LastError");
    }
    {
        State original;
        seed();
        State before;
        HCRYPTKEY hit = 0;
        const BOOL imported = cc::import_key(fp_provider, blob, sizeof blob, 0, 0, &hit);
        State after;
        original.restore();
        check(imported && hit == fp_key && same(before, after),
              "key hit preserves full seeded CPU state and success LastError");
    }
    cc::destroy_key(fp_key);
    end(fp_provider);
    HCRYPTPROV p = begin();
    HCRYPTKEY k = key(p);
    const unsigned oldimports = imports;
    HCRYPTKEY duplicate = key(p);
    check(duplicate != k && imports == oldimports + 1, "simultaneous imports never alias cached handle");
    check(cc::destroy_key(duplicate), "uncached duplicate destroy");
    check(cc::destroy_key(k), "cached key destroy");
    const unsigned before_reuse = imports;
    HCRYPTKEY reused = key(p);
    check(reused == k && imports == before_reuse, "released public key reused");
    cc::destroy_key(reused);
    unsigned before = acquires;
    HCRYPTPROV other = 0;
    cc::acquire(&other, name, provider, PROV_RSA_FULL, CRYPT_DELETEKEYSET | CRYPT_MACHINE_KEYSET);
    check(acquires == before + 1, "machine delete not emulated as user delete");
    before = acquires;
    cc::acquire(&other, "OtherContainer", provider, PROV_RSA_FULL, CRYPT_NEWKEYSET);
    check(acquires == before + 1, "other container not cached");
    cc::release(other, 0);
    before = imports;
    BYTE private_blob[276];
    std::memcpy(private_blob, blob, sizeof blob);
    private_blob[0] = PRIVATEKEYBLOB;
    HCRYPTKEY private_key = 0;
    cc::import_key(p, private_blob, sizeof private_blob, 0, 0, &private_key);
    check(imports == before + 1, "private key not cached");
    cc::destroy_key(private_key);
    before = imports;
    HCRYPTKEY foreign_thread_key = 0;
    std::thread worker([&] { cc::import_key(p, blob, sizeof blob, 0, 0, &foreign_thread_key); });
    worker.join();
    check(imports == before + 1 && foreign_thread_key != k, "foreign thread cannot borrow cached key");
    cc::destroy_key(foreign_thread_key);
    check(!cc::shutdown() && cc::enabled(), "shutdown refuses outstanding provider");
    callbacks = true;
    before = releases;
    const unsigned before_destroy = destroys;
    SetLastError(0x12345678);
    check(!cc::release(p, 1) && GetLastError() == DWORD(NTE_BAD_FLAGS),
          "nonzero release flags preserve native failure");
    check(releases == before + 1 && destroys == before_destroy + 1,
          "bad release retires owned key before native context release");
    check(!cc::statistics().providers_cached && !cc::statistics().keys_cached,
          "bad release retains no invalid handles");
    p = begin();
    k = key(p);
    cc::destroy_key(k);
    before = destroys;
    absent();
    check(destroys == before + 1, "in-use eviction destroys zero-outstanding cached key outside lock");
    cc::release(p, 0);
    p = begin();
    k = key(p);
    before = destroys;
    absent();
    check(destroys == before, "in-use eviction preserves caller-owned key");
    cc::destroy_key(k);
    cc::release(p, 0);
    p = begin();
    k = key(p);
    before = releases;
    check(cc::release(p, 0) && releases == before + 1, "release with outstanding key is native and retires provider");
    check(!cc::statistics().providers_cached && !cc::statistics().keys_cached,
          "outstanding key release cannot seed future cache reuse");
    cc::destroy_key(k);
    p = begin();
    k = key(p);
    before = destroys;
    std::thread destroy_worker([&] { cc::destroy_key(k); });
    destroy_worker.join();
    check(destroys == before + 1 && !cc::statistics().keys_cached,
          "foreign key destruction reaches native and retires cached key");
    end(p);
    p = begin();
    absent();
    cc::release(p, 0);
    check(!cc::statistics().providers_cached, "reentrant acquire control starts with no reusable provider");
    reenter_acquire = true;
    p = begin();
    check(!cc::statistics().providers_cached, "late native acquire cannot publish across reentrant lifecycle change");
    cc::release(p, 0);
    p = begin();
    reenter_import = true;
    k = key(p);
    check(!cc::statistics().providers_cached && !cc::statistics().keys_cached,
          "late native import cannot publish after provider eviction");
    cc::destroy_key(k);
    cc::release(p, 0);
    p = begin();
    k = key(p);
    cc::destroy_key(k);
    end(p);
    before = acquires;
    cc::acquire(&other, name, provider, PROV_RSA_FULL, CRYPT_NEWKEYSET);
    check(acquires == before && other == p, "next exact complete sequence reuses provider");
    end(p);
    const unsigned d = destroys, r = releases;
    check(cc::shutdown(), "quiescent shutdown succeeds with reentrant statistics callbacks");
    check(destroys == d + 1 && releases == r + 1, "shutdown releases each retained object exactly once");
    check(cc::shutdown(), "shutdown idempotent");
    check(!cc::statistics().providers_cached && !cc::statistics().keys_cached, "shutdown cache empty");
    std::printf("CRYPT CONTROLS RESULT checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
