#include "crypt_cache.h"

// Compiled with -mno-sse -mno-mmx -mfpmath=387, integer only, no logging: see
// crypt_cache.h and loading_trace_light.h. Every state change happens under one
// spinlock (the script VM runs the check from one thread; the lock is there for
// the two-caller case 0x004cae90 and for shutdown). Counters are plain 64-bit
// fields written under the lock and read under it by statistics().
namespace x3m::crypt_cache {
namespace {
enum Phase : unsigned { Absent=0, InUse=1, Released=2 };
struct ProviderEntry {
    bool used=false,container_null=false,provider_null=false,probe_error_known=false;
    char container[name_limit]{},provider[name_limit]{};
    DWORD type=0,flags=0,probe_error=0;
    HCRYPTPROV handle=0;
    unsigned phase=Absent;
};
struct KeyEntry {
    bool used=false;
    HCRYPTPROV provider=0; DWORD flags=0,length=0; HCRYPTKEY key=0; unsigned outstanding=0;
    BYTE blob[blob_limit]{};
};
ProviderEntry providers[provider_slots];
KeyEntry keys[key_slots];
Originals real;
Statistics totals;
volatile LONG lock_word=0;
volatile LONG is_enabled=0;

struct Lock {
    bool held;
    explicit Lock(unsigned spins=0) noexcept : held(false) {
        for(unsigned i=0;;++i){
            if(InterlockedCompareExchange(&lock_word,1,0)==0){held=true;return;}
            if(spins&&i>=spins)return;
            YieldProcessor();
            if((i&0x3ff)==0x3ff)Sleep(0);
        }
    }
    ~Lock() noexcept { if(held)InterlockedExchange(&lock_word,0); }
};
bool copy_name(char* out,LPCSTR in,bool& null_flag) noexcept {
    null_flag=in==nullptr;
    unsigned i=0;
    if(in)for(;in[i];++i){if(i+1>=name_limit)return false;out[i]=in[i];}
    out[i]=0;return true;
}
bool same_name(const char* stored,bool stored_null,LPCSTR in) noexcept {
    if(stored_null||!in)return stored_null&&!in;
    unsigned i=0;for(;stored[i]&&in[i];++i)if(stored[i]!=in[i])return false;
    return stored[i]==in[i];
}
ProviderEntry* find_provider(LPCSTR container,LPCSTR provider,DWORD type) noexcept {
    for(auto& e:providers)if(e.used&&e.type==type&&same_name(e.container,e.container_null,container)&&same_name(e.provider,e.provider_null,provider))return &e;
    return nullptr;
}
ProviderEntry* find_provider(HCRYPTPROV handle) noexcept {
    if(!handle)return nullptr;
    for(auto& e:providers)if(e.used&&e.handle==handle)return &e;
    return nullptr;
}
ProviderEntry* claim_provider(LPCSTR container,LPCSTR provider,DWORD type) noexcept {
    for(auto& e:providers){
        if(e.used)continue;
        if(!copy_name(e.container,container,e.container_null)||!copy_name(e.provider,provider,e.provider_null))return nullptr;
        e.type=type;e.flags=0;e.probe_error=0;e.probe_error_known=false;e.handle=0;e.phase=Absent;e.used=true;
        return &e;
    }
    return nullptr;
}
KeyEntry* find_key(HCRYPTKEY key) noexcept {
    if(!key)return nullptr;
    for(auto& k:keys)if(k.used&&k.key==key)return &k;
    return nullptr;
}
KeyEntry* find_key(HCRYPTPROV provider,DWORD flags,const BYTE* data,DWORD length) noexcept {
    for(auto& k:keys){
        if(!k.used||k.provider!=provider||k.flags!=flags||k.length!=length)continue;
        DWORD i=0;for(;i<length;++i)if(k.blob[i]!=data[i])break;
        if(i==length)return &k;
    }
    return nullptr;
}
// Forgets a provider entry whose handle is out (and its keys) without releasing
// anything: the caller still holds the handle, exactly as it would uncached, and
// its later CryptDestroyKey/CryptReleaseContext pass through to the real calls.
void evict(ProviderEntry& e) noexcept {
    for(auto& k:keys)if(k.used&&k.provider==e.handle){k.used=false;k.key=0;k.outstanding=0;--totals.keys_cached;}
    e.handle=0;e.phase=Absent;e.flags=0;
    ++totals.evictions;--totals.providers_cached;
}
}

bool requested(){wchar_t setting[8]{};return GetEnvironmentVariableW(L"X3M_CRYPT_CACHE",setting,8)==1&&setting[0]==L'1';}
bool initialize(const Originals& originals) noexcept {
    if(!originals.acquire||!originals.release||!originals.import_key||!originals.destroy_key)return false;
    Lock lock;
    if(is_enabled)return false;
    real=originals;
    InterlockedExchange(&is_enabled,1);
    return true;
}
bool enabled() noexcept { return is_enabled!=0; }

BOOL WINAPI acquire(HCRYPTPROV* out,LPCSTR container,LPCSTR provider,DWORD type,DWORD flags) {
    if(!is_enabled)return real.acquire(out,container,provider,type,flags);
    const DWORD caller_error=GetLastError();
    if(flags&CRYPT_DELETEKEYSET){
        {
            Lock lock;
            ++totals.acquires;
            ProviderEntry* e=find_provider(container,provider,type);
            if(e&&e->handle){
                if(e->phase==Released){e->phase=Absent;++totals.deletes_emulated;SetLastError(caller_error);return TRUE;}
                if(e->phase==Absent){
                    ++totals.deletes_emulated;
                    SetLastError(e->probe_error_known?e->probe_error:DWORD(NTE_BAD_KEYSET));
                    return FALSE;
                }
                evict(*e); // a delete while the handle is out: not the game's sequence; pass it through
            }
            ++totals.deletes_passthrough;
        }
        SetLastError(caller_error);
        const BOOL result=real.acquire(out,container,provider,type,flags);
        const DWORD result_error=GetLastError();
        if(!result){
            Lock lock;
            ++totals.failed_passthrough;
            ProviderEntry* e=find_provider(container,provider,type);
            if(!e)e=claim_provider(container,provider,type);
            if(e&&!e->probe_error_known){e->probe_error=result_error;e->probe_error_known=true;totals.probe_error=result_error;}
        }
        SetLastError(result_error);
        return result;
    }
    {
        Lock lock;
        ++totals.acquires;
        ProviderEntry* e=find_provider(container,provider,type);
        if(e&&e->handle){
            if(e->phase!=InUse&&e->flags==flags&&out){*out=e->handle;e->phase=InUse;++totals.hits;SetLastError(caller_error);return TRUE;}
            ++totals.busy_passthrough; // handle out or a different flag set: served by the real call, not cached
        }
    }
    SetLastError(caller_error);
    const BOOL result=real.acquire(out,container,provider,type,flags);
    const DWORD result_error=GetLastError();
    {
        Lock lock;
        if(!result)++totals.failed_passthrough;
        else{
            ++totals.misses;
            if(out&&*out){
                ProviderEntry* e=find_provider(container,provider,type);
                if(!e)e=claim_provider(container,provider,type);
                if(e&&!e->handle){e->handle=*out;e->flags=flags;e->phase=InUse;++totals.providers_cached;}
            }
        }
    }
    SetLastError(result_error);
    return result;
}
BOOL WINAPI release(HCRYPTPROV provider,DWORD flags) {
    if(!is_enabled)return real.release(provider,flags);
    const DWORD caller_error=GetLastError();
    {
        Lock lock;
        ++totals.releases;
        ProviderEntry* e=find_provider(provider);
        if(e&&e->phase==InUse){e->phase=Released;++totals.releases_suppressed;SetLastError(caller_error);return TRUE;}
    }
    SetLastError(caller_error);
    return real.release(provider,flags);
}
BOOL WINAPI import_key(HCRYPTPROV provider,const BYTE* data,DWORD length,HCRYPTKEY public_key,DWORD flags,HCRYPTKEY* out) {
    if(!is_enabled)return real.import_key(provider,data,length,public_key,flags,out);
    const DWORD caller_error=GetLastError();
    // Only blobs imported into a cached provider, without a wrapping key, of a
    // bounded size are cached; anything else is the real call.
    const bool cacheable=data&&out&&length&&length<=blob_limit&&public_key==0;
    {
        Lock lock;
        ++totals.imports;
        ProviderEntry* e=cacheable?find_provider(provider):nullptr;
        if(e&&e->handle){
            if(KeyEntry* k=find_key(provider,flags,data,length)){*out=k->key;++k->outstanding;++totals.import_hits;SetLastError(caller_error);return TRUE;}
        }
        else ++totals.import_passthrough;
    }
    SetLastError(caller_error);
    const BOOL result=real.import_key(provider,data,length,public_key,flags,out);
    const DWORD result_error=GetLastError();
    if(result&&cacheable&&*out){
        Lock lock;
        ProviderEntry* e=find_provider(provider);
        if(e&&e->handle){
            for(auto& k:keys){
                if(k.used)continue;
                k.provider=provider;k.flags=flags;k.length=length;k.key=*out;k.outstanding=1;
                for(DWORD i=0;i<length;++i)k.blob[i]=data[i];
                k.used=true;++totals.keys_cached;break;
            }
        }
    }
    SetLastError(result_error);
    return result;
}
BOOL WINAPI destroy_key(HCRYPTKEY key) {
    if(!is_enabled)return real.destroy_key(key);
    const DWORD caller_error=GetLastError();
    {
        Lock lock;
        ++totals.destroys;
        if(KeyEntry* k=find_key(key)){if(k->outstanding)--k->outstanding;++totals.destroys_suppressed;SetLastError(caller_error);return TRUE;}
    }
    SetLastError(caller_error);
    return real.destroy_key(key);
}
bool shutdown() noexcept {
    if(!is_enabled)return true;
    Lock lock(4096);
    if(!lock.held)return false;
    InterlockedExchange(&is_enabled,0);
    for(auto& k:keys)if(k.used){real.destroy_key(k.key);k.used=false;k.key=0;k.outstanding=0;--totals.keys_cached;}
    for(auto& e:providers){
        if(!e.used)continue;
        if(e.handle){real.release(e.handle,0);--totals.providers_cached;}
        // The game's last word on the container was a delete: do it for real now.
        if(e.handle&&e.phase==Absent&&!(e.flags&CRYPT_VERIFYCONTEXT)){
            HCRYPTPROV scratch=0;
            real.acquire(&scratch,e.container_null?nullptr:e.container,e.provider_null?nullptr:e.provider,e.type,CRYPT_DELETEKEYSET);
        }
        e.handle=0;e.phase=Absent;e.used=false;
    }
    return true;
}
Statistics statistics() noexcept {
    Lock lock;
    Statistics copy;
    copy.acquires=totals.acquires;copy.hits=totals.hits;copy.misses=totals.misses;copy.failed_passthrough=totals.failed_passthrough;
    copy.busy_passthrough=totals.busy_passthrough;copy.deletes_emulated=totals.deletes_emulated;copy.deletes_passthrough=totals.deletes_passthrough;
    copy.evictions=totals.evictions;copy.releases=totals.releases;copy.releases_suppressed=totals.releases_suppressed;
    copy.imports=totals.imports;copy.import_hits=totals.import_hits;copy.import_passthrough=totals.import_passthrough;
    copy.destroys=totals.destroys;copy.destroys_suppressed=totals.destroys_suppressed;
    copy.providers_cached=totals.providers_cached;copy.keys_cached=totals.keys_cached;copy.probe_error=totals.probe_error;
    return copy;
}
}
