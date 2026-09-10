#pragma once
#include <windows.h>
#include <d3dx9mesh.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Detached exact-input cache. No hooks, globals, COM ownership or disk storage.
// The caller verifies/pins the native runtime, holds mesh/output lifetime, and
// serializes mutations exactly as required for a direct GenerateAdjacency call.
namespace x3m::mesh_adjacency_cache {
using Generate = HRESULT (STDMETHODCALLTYPE*)(ID3DXMesh*, FLOAT, DWORD*);
struct RuntimeIdentity {
    std::array<unsigned char,32> sha256{};
    uint64_t generation=0; // Immutable generation of the verified/pinned module.
    bool verified=false;
    bool success_preserves_last_error=false;
};
struct Config {
    size_t retained_bytes=16u*1024*1024;
    size_t scratch_bytes=4u*1024*1024;
    unsigned entries=512;
};
enum class Origin { Native, CacheHit, AcquisitionCleanupFailure, InvalidCallback };
struct Outcome { HRESULT hr=E_FAIL; Origin origin=Origin::Native; };
struct Statistics {
    uint64_t calls=0,hits=0,misses=0,bypasses=0,contention=0,admissions=0,evictions=0;
    uint64_t allocation_failures=0,acquisition_failures=0,unrecoverable_unlocks=0;
    uint64_t native_calls=0,native_failures=0,retained_bytes=0,retained_entries=0;
    uint64_t acquired_bytes=0,copied_bytes=0,evicted_bytes=0;
    uint64_t acquisition_ticks=0,lookup_ticks=0,copy_ticks=0,native_ticks=0,total_ticks=0;
};
class Cache final {
public:
    explicit Cache(Config config={}) noexcept;
    ~Cache(); // All callers must be quiescent before destruction/clear.
    Cache(const Cache&)=delete;
    Cache& operator=(const Cache&)=delete;
    Outcome generate(ID3DXMesh* mesh,FLOAT epsilon,DWORD* output,
                     Generate original,const RuntimeIdentity& runtime) noexcept;
    Statistics statistics() const noexcept; // Cumulative, nontransactional atomics.
    void clear() noexcept; // Quiescent only; keeps counters and permanent cleanup-failure poison.
#ifdef X3M_MESH_CACHE_FIXTURE
    void fixture_fail_next_allocation() noexcept { fail_allocation_=true; }
    void fixture_force_hash_collision(bool enabled) noexcept { collision_=enabled; }
#endif
private:
    static constexpr unsigned max_entries=512;
    struct Entry {
        unsigned char* allocation=nullptr;
        size_t allocation_bytes=0,key_bytes=0,value_bytes=0;
        uint64_t hash=0,age=0;
        DWORD x87_status=0,mxcsr=0;
    };
    struct Counters {
#define X3M_CACHE_COUNTER(n) std::atomic<uint64_t> n{0};
        X3M_CACHE_COUNTER(calls) X3M_CACHE_COUNTER(hits) X3M_CACHE_COUNTER(misses)
        X3M_CACHE_COUNTER(bypasses) X3M_CACHE_COUNTER(contention) X3M_CACHE_COUNTER(admissions)
        X3M_CACHE_COUNTER(evictions) X3M_CACHE_COUNTER(allocation_failures)
        X3M_CACHE_COUNTER(acquisition_failures) X3M_CACHE_COUNTER(unrecoverable_unlocks)
        X3M_CACHE_COUNTER(native_calls) X3M_CACHE_COUNTER(native_failures)
        X3M_CACHE_COUNTER(retained_bytes) X3M_CACHE_COUNTER(retained_entries)
        X3M_CACHE_COUNTER(acquired_bytes) X3M_CACHE_COUNTER(copied_bytes) X3M_CACHE_COUNTER(evicted_bytes)
        X3M_CACHE_COUNTER(acquisition_ticks) X3M_CACHE_COUNTER(lookup_ticks)
        X3M_CACHE_COUNTER(copy_ticks) X3M_CACHE_COUNTER(native_ticks) X3M_CACHE_COUNTER(total_ticks)
#undef X3M_CACHE_COUNTER
    } counters_;
    Config config_;
    std::array<Entry,max_entries> entries_{};
    std::atomic_flag workspace_=ATOMIC_FLAG_INIT;
    std::atomic<bool> disabled_{false};
    size_t retained_=sizeof(Cache);
    unsigned count_=0;
    uint64_t age_=0;
#ifdef X3M_MESH_CACHE_FIXTURE
    bool fail_allocation_=false,collision_=false;
#endif
};
}
