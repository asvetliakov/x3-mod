// CryptoAPI context/key cache (src/proxy/crypt_cache.cpp) against the bottle's
// real ADVAPI32/rsaenh, driving the exact call sequence of the game's script
// signature check 0x004cabc0 (docs/reverse-engineering/script-signature-check.md):
//   CryptAcquireContextA(container, MS_DEF_PROV, PROV_RSA_FULL, CRYPT_DELETEKEYSET)
//   CryptAcquireContextA(..., CRYPT_NEWKEYSET) -> CryptImportKey(PUBLICKEYBLOB)
//   CryptCreateHash(CALG_MD5) -> CryptHashData -> CryptVerifySignatureA
//   CryptGetHashParam(HP_HASHSIZE) -> CryptGetHashParam(HP_HASHVAL)
//   CryptDestroyHash -> CryptDestroyKey -> CryptReleaseContext
//   CryptAcquireContextA(..., CRYPT_DELETEKEYSET)
// The fixture generates an RSA-2048 signature key pair in its own container,
// exports the 276-byte public blob (the game's 0x114), signs one message per
// iteration (every fourth signature corrupted, every sixteenth truncated) and
// runs the sequence N times with the cache off (real calls through counting
// shims) and N times with the cache on (crypt_cache bound to the same shims).
// Every call's BOOL result, the last error of every failing call, the verify
// outcome and the digest bytes must agree between the two passes; the shims
// count real acquires/releases and imports/destroys so a leak shows as a
// non-zero live count after crypt_cache::shutdown(). A delete-while-in-use case
// exercises the eviction path. Per-iteration time is reported for both passes.
// usage: crypt_cache_fixture.exe [iterations]
#include "../../src/proxy/crypt_cache.h"
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace cc=x3m::crypt_cache;
static unsigned checks=0,failures=0;
static void check(bool ok,const char* what,const char* detail=""){++checks;if(!ok){++failures;printf("FAIL %s %s\n",what,detail);}}
static const char CONTAINER[]="X3mCryptCacheFixture";
static const char SIGNER[]="X3mCryptCacheSigner";
static const char PROVIDER[]="Microsoft Base Cryptographic Provider v1.0"; // MS_DEF_PROV_A, the game's provider
static uint64_t tick(){LARGE_INTEGER v{};QueryPerformanceCounter(&v);return uint64_t(v.QuadPart);}
static double frequency(){LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return double(f.QuadPart);}

// ---- counting shims: the real ADVAPI32 calls, the cache's originals ----
struct RealCounts { unsigned acquires=0,acquire_ok=0,deletes=0,delete_ok=0,releases=0,release_ok=0,imports=0,import_ok=0,destroys=0,destroy_ok=0; };
static RealCounts real_counts;
static int live_providers=0,live_keys=0;
static BOOL WINAPI shim_acquire(HCRYPTPROV* out,LPCSTR container,LPCSTR provider,DWORD type,DWORD flags){
    ++real_counts.acquires;
    const BOOL r=CryptAcquireContextA(out,container,provider,type,flags);
    const DWORD e=GetLastError();
    if(flags&CRYPT_DELETEKEYSET){++real_counts.deletes;if(r)++real_counts.delete_ok;}
    else if(r){++real_counts.acquire_ok;++live_providers;}
    SetLastError(e);return r;
}
static BOOL WINAPI shim_release(HCRYPTPROV provider,DWORD flags){
    ++real_counts.releases;const BOOL r=CryptReleaseContext(provider,flags);const DWORD e=GetLastError();
    if(r){++real_counts.release_ok;--live_providers;}
    SetLastError(e);return r;
}
static BOOL WINAPI shim_import(HCRYPTPROV provider,const BYTE* data,DWORD length,HCRYPTKEY key,DWORD flags,HCRYPTKEY* out){
    ++real_counts.imports;const BOOL r=CryptImportKey(provider,data,length,key,flags,out);const DWORD e=GetLastError();
    if(r){++real_counts.import_ok;++live_keys;}
    SetLastError(e);return r;
}
static BOOL WINAPI shim_destroy(HCRYPTKEY key){
    ++real_counts.destroys;const BOOL r=CryptDestroyKey(key);const DWORD e=GetLastError();
    if(r){++real_counts.destroy_ok;--live_keys;}
    SetLastError(e);return r;
}
struct Api { cc::AcquireFn acquire;cc::ReleaseFn release;cc::ImportFn import_key;cc::DestroyKeyFn destroy_key; };
static const Api direct{shim_acquire,shim_release,shim_import,shim_destroy};
static const Api cached{cc::acquire,cc::release,cc::import_key,cc::destroy_key};

// ---- one signature check, recorded step by step ----
constexpr unsigned step_count=12;
static const char* step_names[step_count]={"delete_before","new_keyset","import_key","create_hash","hash_data","verify","hash_size","hash_value","destroy_hash","destroy_key","release","delete_after"};
struct Step { int result=-1; DWORD error=0; }; // -1 = not reached
struct Record { Step steps[step_count]; bool verified=false; DWORD digest_len=0; BYTE digest[64]{}; uint64_t ticks=0,context_ticks=0; };
static void record(Record& r,unsigned step,BOOL result){r.steps[step].result=result?1:0;r.steps[step].error=result?0:GetLastError();}
static void run_check(const Api& api,const BYTE* message,DWORD message_len,const BYTE* signature,DWORD signature_len,const BYTE* blob,DWORD blob_len,Record& r){
    HCRYPTPROV prov=0;HCRYPTHASH hash=0;HCRYPTKEY key=0;
    SetLastError(0);
    const uint64_t t0=tick();
    record(r,0,api.acquire(&prov,CONTAINER,PROVIDER,PROV_RSA_FULL,CRYPT_DELETEKEYSET));
    const BOOL acquired=api.acquire(&prov,CONTAINER,PROVIDER,PROV_RSA_FULL,CRYPT_NEWKEYSET);record(r,1,acquired);
    const uint64_t t1=tick();
    r.context_ticks+=t1-t0;
    if(acquired){
        const BOOL imported=api.import_key(prov,blob,blob_len,0,0,&key);record(r,2,imported);
        if(imported){
            const BOOL created=CryptCreateHash(prov,CALG_MD5,0,0,&hash);record(r,3,created);
            if(created){
                const BOOL hashed=CryptHashData(hash,message,message_len,0);record(r,4,hashed);
                if(hashed){
                    const BOOL verified=CryptVerifySignatureA(hash,signature,signature_len,key,nullptr,0);record(r,5,verified);
                    r.verified=verified!=0;
                    if(verified){
                        DWORD size=0,size_len=sizeof size;
                        const BOOL got_size=CryptGetHashParam(hash,HP_HASHSIZE,reinterpret_cast<BYTE*>(&size),&size_len,0);record(r,6,got_size);
                        if(got_size&&size<=sizeof r.digest){r.digest_len=size;record(r,7,CryptGetHashParam(hash,HP_HASHVAL,r.digest,&r.digest_len,0));}
                    }
                }
            }
        }
        if(hash)record(r,8,CryptDestroyHash(hash));
        if(key)record(r,9,api.destroy_key(key));
        const uint64_t t2=tick();
        record(r,10,api.release(prov,0));
        record(r,11,api.acquire(&prov,CONTAINER,PROVIDER,PROV_RSA_FULL,CRYPT_DELETEKEYSET));
        r.context_ticks+=tick()-t2;
    }
    r.ticks=tick()-t0;
}
static bool same(const Record& a,const Record& b,char* detail,size_t detail_len){
    for(unsigned i=0;i<step_count;++i){
        if(a.steps[i].result!=b.steps[i].result){snprintf(detail,detail_len,"step=%s result off=%d on=%d",step_names[i],a.steps[i].result,b.steps[i].result);return false;}
        if(a.steps[i].result==0&&a.steps[i].error!=b.steps[i].error){snprintf(detail,detail_len,"step=%s error off=0x%08lx on=0x%08lx",step_names[i],(unsigned long)a.steps[i].error,(unsigned long)b.steps[i].error);return false;}
    }
    if(a.verified!=b.verified){snprintf(detail,detail_len,"verified off=%u on=%u",unsigned(a.verified),unsigned(b.verified));return false;}
    if(a.verified&&(a.digest_len!=b.digest_len||memcmp(a.digest,b.digest,a.digest_len))){snprintf(detail,detail_len,"digest differs len off=%lu on=%lu",(unsigned long)a.digest_len,(unsigned long)b.digest_len);return false;}
    return true;
}
struct Timing { double total_ms=0,mean_us=0,median_us=0,min_us=0,max_us=0,context_mean_us=0; };
static Timing timing(const std::vector<Record>& records){
    Timing t;if(records.empty())return t;
    const double f=frequency();std::vector<uint64_t> ticks;uint64_t sum=0,context=0;
    for(const auto& r:records){ticks.push_back(r.ticks);sum+=r.ticks;context+=r.context_ticks;}
    std::sort(ticks.begin(),ticks.end());
    t.total_ms=double(sum)*1e3/f;t.mean_us=double(sum)*1e6/f/double(records.size());t.median_us=double(ticks[ticks.size()/2])*1e6/f;
    t.min_us=double(ticks.front())*1e6/f;t.max_us=double(ticks.back())*1e6/f;t.context_mean_us=double(context)*1e6/f/double(records.size());
    return t;
}
static void print_mode(const char* mode,const std::vector<Record>& records,const RealCounts& before,const RealCounts& after){
    unsigned verified=0;for(const auto& r:records)verified+=r.verified;
    const Timing t=timing(records);
    printf("CRYPT_MODE mode=%s iterations=%u verified=%u rejected=%u real_acquires=%u real_acquire_ok=%u real_deletes=%u real_delete_ok=%u real_releases=%u real_imports=%u real_destroys=%u total_ms=%.3f mean_us=%.3f median_us=%.3f min_us=%.3f max_us=%.3f context_mean_us=%.3f\n",
        mode,unsigned(records.size()),verified,unsigned(records.size())-verified,after.acquires-before.acquires,after.acquire_ok-before.acquire_ok,after.deletes-before.deletes,after.delete_ok-before.delete_ok,
        after.releases-before.releases,after.imports-before.imports,after.destroys-before.destroys,t.total_ms,t.mean_us,t.median_us,t.min_us,t.max_us,t.context_mean_us);
}
static uint32_t rng_state=0x9e3779b9u;
static uint32_t rnd(){uint32_t s=rng_state;s^=s<<13;s^=s>>17;s^=s<<5;rng_state=s;return s;}
static void make_message(std::vector<BYTE>& out,unsigned index){
    const unsigned n=512+(rnd()%3584);out.resize(n);
    const int head=snprintf(reinterpret_cast<char*>(out.data()),n,"<script index=\"%u\">",index);
    for(unsigned i=unsigned(head<0?0:head);i<n;++i){const uint32_t r=rnd()>>8;out[i]=(r&0x70)?BYTE('a'+(r>>8)%26):BYTE(r&0xff);}
}
static bool sign(HCRYPTPROV signer,const std::vector<BYTE>& message,std::vector<BYTE>& signature){
    HCRYPTHASH hash=0;if(!CryptCreateHash(signer,CALG_MD5,0,0,&hash))return false;
    bool ok=CryptHashData(hash,message.data(),DWORD(message.size()),0)!=0;
    DWORD len=0;
    if(ok)ok=CryptSignHashA(hash,AT_SIGNATURE,nullptr,0,nullptr,&len)!=0;
    if(ok){signature.resize(len);ok=CryptSignHashA(hash,AT_SIGNATURE,nullptr,0,signature.data(),&len)!=0;signature.resize(len);}
    CryptDestroyHash(hash);return ok;
}

int main(int argc,char** argv){
    const unsigned iterations=argc>1?unsigned(strtoul(argv[1],nullptr,10)):200;
    // Signature key pair in a private container (fresh each run).
    HCRYPTPROV signer=0;
    CryptAcquireContextA(&signer,SIGNER,PROVIDER,PROV_RSA_FULL,CRYPT_DELETEKEYSET);
    check(CryptAcquireContextA(&signer,SIGNER,PROVIDER,PROV_RSA_FULL,CRYPT_NEWKEYSET)!=0,"signer_container");
    HCRYPTKEY private_key=0;
    check(CryptGenKey(signer,AT_SIGNATURE,(2048u<<16)|CRYPT_EXPORTABLE,&private_key)!=0,"gen_key_2048");
    std::vector<BYTE> blob;DWORD blob_len=0;
    check(CryptExportKey(private_key,0,PUBLICKEYBLOB,0,nullptr,&blob_len)!=0,"export_size");
    blob.resize(blob_len);
    check(CryptExportKey(private_key,0,PUBLICKEYBLOB,0,blob.data(),&blob_len)!=0,"export_blob");
    blob.resize(blob_len);
    char detail[160];snprintf(detail,sizeof detail,"blob_len=%lu",(unsigned long)blob_len);
    check(blob_len==0x114,"blob_is_the_games_0x114_bytes",detail); // BLOBHEADER + RSAPUBKEY + 256-byte modulus
    // Messages and signatures (the same set drives both passes).
    std::vector<std::vector<BYTE>> messages(iterations),signatures(iterations);
    unsigned expected_verified=0;DWORD signature_bytes=0;
    for(unsigned i=0;i<iterations;++i){
        make_message(messages[i],i);
        check(sign(signer,messages[i],signatures[i]),"sign");
        if(!signature_bytes)signature_bytes=DWORD(signatures[i].size());
        if(i%16==15&&!signatures[i].empty())signatures[i].pop_back();           // truncated
        else if(i%4==3&&!signatures[i].empty())signatures[i][signatures[i].size()/2]^=0x5a; // corrupted
        else ++expected_verified;
    }
    printf("CRYPT_KEY bits=2048 blob_bytes=%lu signature_bytes=%lu expected_verified=%u provider=\"%s\" container=%s\n",(unsigned long)blob_len,(unsigned long)signature_bytes,expected_verified,PROVIDER,CONTAINER);
    // Start from the game's steady state: no container.
    {HCRYPTPROV scratch=0;CryptAcquireContextA(&scratch,CONTAINER,PROVIDER,PROV_RSA_FULL,CRYPT_DELETEKEYSET);}
    // Pass 1: cache off.
    std::vector<Record> off(iterations),on(iterations);
    const RealCounts before_off=real_counts;
    for(unsigned i=0;i<iterations;++i)run_check(direct,messages[i].data(),DWORD(messages[i].size()),signatures[i].data(),DWORD(signatures[i].size()),blob.data(),blob_len,off[i]);
    const RealCounts after_off=real_counts;
    print_mode("off",off,before_off,after_off);
    check(live_providers==0&&live_keys==0,"off_pass_leaks");
    // Pass 2: cache on, bound to the same shims.
    cc::Originals originals;originals.acquire=shim_acquire;originals.release=shim_release;originals.import_key=shim_import;originals.destroy_key=shim_destroy;
    check(cc::initialize(originals),"cache_initialize");
    check(!cc::initialize(originals),"cache_initialize_refused_twice");
    check(cc::enabled(),"cache_enabled");
    const RealCounts before_on=real_counts;
    for(unsigned i=0;i<iterations;++i)run_check(cached,messages[i].data(),DWORD(messages[i].size()),signatures[i].data(),DWORD(signatures[i].size()),blob.data(),blob_len,on[i]);
    const RealCounts after_on=real_counts;
    print_mode("on",on,before_on,after_on);
    // Same observable outcome, call by call.
    unsigned mismatches=0,verified_off=0,verified_on=0;
    for(unsigned i=0;i<iterations;++i){
        verified_off+=off[i].verified;verified_on+=on[i].verified;
        if(!same(off[i],on[i],detail,sizeof detail)){++mismatches;if(mismatches<=5)printf("FAIL compare iteration=%u %s\n",i,detail);}
    }
    printf("CRYPT_COMPARE iterations=%u steps=%u mismatches=%u verified_off=%u verified_on=%u expected_verified=%u\n",iterations,step_count,mismatches,verified_off,verified_on,expected_verified);
    check(mismatches==0,"compare_identical");
    check(verified_off==expected_verified,"off_verifies_expected");
    check(verified_on==expected_verified,"on_verifies_expected");
    // Steady-state failure of the first delete: NTE_BAD_KEYSET recorded from the real call, replayed after.
    check(iterations<2||(off[1].steps[0].result==0&&on[1].steps[0].result==0&&on[1].steps[0].error==off[1].steps[0].error),"delete_before_fails_identically");
    check(iterations<1||(off[0].steps[11].result==1&&on[0].steps[11].result==1),"delete_after_succeeds");
    // The cache's own view.
    const auto s=cc::statistics();
    printf("CRYPT_STATS acquires=%llu hits=%llu misses=%llu failed_passthrough=%llu releases_suppressed=%llu imports=%llu import_hits=%llu deletes_emulated=%llu deletes_passthrough=%llu busy_passthrough=%llu evictions=%llu releases=%llu import_passthrough=%llu destroys=%llu destroys_suppressed=%llu providers_cached=%u keys_cached=%u probe_error=0x%08x\n",
        (unsigned long long)s.acquires,(unsigned long long)s.hits,(unsigned long long)s.misses,(unsigned long long)s.failed_passthrough,(unsigned long long)s.releases_suppressed,(unsigned long long)s.imports,(unsigned long long)s.import_hits,
        (unsigned long long)s.deletes_emulated,(unsigned long long)s.deletes_passthrough,(unsigned long long)s.busy_passthrough,(unsigned long long)s.evictions,(unsigned long long)s.releases,(unsigned long long)s.import_passthrough,
        (unsigned long long)s.destroys,(unsigned long long)s.destroys_suppressed,s.providers_cached,s.keys_cached,unsigned(s.probe_error));
    check(s.acquires==uint64_t(iterations)*3,"stats_acquires_three_per_check");
    check(s.hits==uint64_t(iterations)-1&&s.misses==1,"stats_one_miss_then_hits");
    check(s.failed_passthrough==1&&s.deletes_passthrough==1&&s.deletes_emulated==uint64_t(iterations)*2-1,"stats_deletes_emulated_after_the_first");
    check(s.releases_suppressed==iterations&&s.imports==iterations&&s.import_hits==uint64_t(iterations)-1&&s.destroys_suppressed==iterations,"stats_release_import_destroy");
    check(s.providers_cached==1&&s.keys_cached==1&&s.evictions==0,"stats_one_provider_one_key");
    check(after_on.acquires-before_on.acquires==2&&after_on.acquire_ok-before_on.acquire_ok==1&&after_on.releases==before_on.releases&&after_on.imports-before_on.imports==1&&after_on.destroys==before_on.destroys,"real_calls_on_two_acquires_one_import");
    check(after_off.acquires-before_off.acquires==iterations*3&&after_off.releases-before_off.releases==iterations,"real_calls_off_three_acquires_per_check");
    // Eviction: a delete while the handle is out passes through and forgets the
    // entry; the caller's handle stays valid and its release/destroy are real.
    {
        HCRYPTPROV prov=0;HCRYPTKEY key=0;
        const RealCounts b=real_counts;
        check(cc::acquire(&prov,CONTAINER,PROVIDER,PROV_RSA_FULL,CRYPT_NEWKEYSET)!=0,"evict_acquire_hit");
        check(cc::import_key(prov,blob.data(),blob_len,0,0,&key)!=0&&key!=0,"evict_import_hit");
        HCRYPTPROV scratch=0;
        const BOOL deleted=cc::acquire(&scratch,CONTAINER,PROVIDER,PROV_RSA_FULL,CRYPT_DELETEKEYSET);
        snprintf(detail,sizeof detail,"deleted=%d error=0x%08lx",int(deleted),(unsigned long)GetLastError());
        check(real_counts.deletes==b.deletes+1,"evict_delete_passed_through",detail);
        check(cc::destroy_key(key)!=0,"evict_destroy_real");
        check(cc::release(prov,0)!=0,"evict_release_real");
        check(real_counts.destroys==b.destroys+1&&real_counts.releases==b.releases+1,"evict_destroy_release_real_calls");
        const auto e=cc::statistics();
        check(e.evictions==1&&e.providers_cached==0&&e.keys_cached==0,"evict_stats");
        check(live_providers==0&&live_keys==0,"evict_no_leak");
        // The CSP's own behaviour after that off-contract order is not the cache's
        // business (Wine's rsaenh re-stores the container when the still-open handle
        // is released, so it exists again here): restore the game's steady state
        // (no container) through the cache, whose entry now passes deletes through.
        cc::acquire(&scratch,CONTAINER,PROVIDER,PROV_RSA_FULL,CRYPT_DELETEKEYSET);
        // The state machine recovers: the next check misses once and caches again.
        Record r;run_check(cached,messages[0].data(),DWORD(messages[0].size()),signatures[0].data(),DWORD(signatures[0].size()),blob.data(),blob_len,r);
        check(same(off[0],r,detail,sizeof detail),"recovered_check_identical",detail);
        const auto f=cc::statistics();
        check(f.misses==2&&f.providers_cached==1&&f.keys_cached==1,"recovered_stats");
    }
    // Shutdown: real release of the cached key and handle, then the container is
    // deleted for real because the game's last word on it was a delete.
    const RealCounts before_shutdown=real_counts;
    const bool released=cc::shutdown();
    check(released,"shutdown_released");
    check(cc::shutdown(),"shutdown_idempotent");
    check(!cc::enabled(),"shutdown_disables");
    HCRYPTPROV leftover=0;
    const BOOL exists=CryptAcquireContextA(&leftover,CONTAINER,PROVIDER,PROV_RSA_FULL,0);
    const DWORD exists_error=GetLastError();
    if(exists){CryptReleaseContext(leftover,0);HCRYPTPROV scratch=0;CryptAcquireContextA(&scratch,CONTAINER,PROVIDER,PROV_RSA_FULL,CRYPT_DELETEKEYSET);}
    snprintf(detail,sizeof detail,"exists=%d error=0x%08lx",int(exists),(unsigned long)exists_error);
    check(!exists,"container_absent_after_shutdown",detail);
    printf("CRYPT_SHUTDOWN released=%u real_acquires=%u real_releases=%u real_destroys=%u container_absent=%u\n",unsigned(released),real_counts.acquires-before_shutdown.acquires,real_counts.releases-before_shutdown.releases,real_counts.destroys-before_shutdown.destroys,unsigned(!exists));
    check(real_counts.releases-before_shutdown.releases==1&&real_counts.destroys-before_shutdown.destroys==1&&real_counts.deletes-before_shutdown.deletes==1,"shutdown_real_calls");
    printf("CRYPT_LEAK live_providers=%d live_keys=%d\n",live_providers,live_keys);
    check(live_providers==0&&live_keys==0,"no_handle_leak");
    // Signer cleanup.
    CryptDestroyKey(private_key);CryptReleaseContext(signer,0);
    {HCRYPTPROV scratch=0;CryptAcquireContextA(&scratch,SIGNER,PROVIDER,PROV_RSA_FULL,CRYPT_DELETEKEYSET);}
    printf("CRYPT CACHE RESULT checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
