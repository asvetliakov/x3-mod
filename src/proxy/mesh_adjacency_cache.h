#pragma once
#include <windows.h>
#include <d3dx9mesh.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Detached exact-input cache. No hooks, globals, COM ownership or disk storage.
// The caller pins the implementation lifetime, holds mesh/output lifetime, and
// serializes mutations exactly as required for a direct GenerateAdjacency call.
namespace x3m::mesh_adjacency_cache {
using Generate = HRESULT (STDMETHODCALLTYPE*)(ID3DXMesh*, FLOAT, DWORD*);
struct RuntimeIdentity {
    uint64_t algorithm_token=0; // Caller-owned process-local identity, never a file digest.
    uint64_t generation=0; // Immutable generation of the verified/pinned module.
    bool public_contract=false; // Caller supplies the public mesh/lifetime contract.
    // Public readable SYSTEMMEM Lock/Unlock contract, not a general DYNAMIC opt-out.
    bool systemmem_dynamic_readonly_verified=false;
};
struct Config {
    size_t retained_bytes=16u*1024*1024;
    size_t scratch_bytes=4u*1024*1024;
    unsigned entries=512;
};
enum class Origin { Native, CacheHit, AcquisitionCleanupFailure, InvalidCallback };
struct Outcome { HRESULT hr=E_FAIL; Origin origin=Origin::Native; };
enum class BypassReason : unsigned { Input, Runtime, FloatingPoint, Epsilon, Configuration, Disabled, Contention, Options, Metadata, Declaration, Size, OutputRange, Allocation, BufferRead, Count };
constexpr unsigned bypass_reason_count=static_cast<unsigned>(BypassReason::Count);
const char* bypass_reason_name(unsigned reason) noexcept;
struct FloatingPointDiagnostic {DWORD control=0,status=0,tag=0,mxcsr=0;};
struct Statistics {
    uint64_t calls=0,hits=0,misses=0,bypasses=0,contention=0,admissions=0,evictions=0;
    uint64_t allocation_failures=0,acquisition_failures=0,unrecoverable_unlocks=0;
    uint64_t native_calls=0,native_failures=0,retained_bytes=0,retained_entries=0;
    uint64_t acquired_bytes=0,copied_bytes=0,evicted_bytes=0;
    uint64_t acquisition_ticks=0,lookup_ticks=0,copy_ticks=0,native_ticks=0,total_ticks=0;
    std::array<uint64_t,bypass_reason_count> bypass_reasons{};
    uint64_t rejected_result=0,rejected_last_error=0,rejected_fp=0;
    bool unsupported_fp_available=false;FloatingPointDiagnostic unsupported_fp{};
    bool first_fp_available=false,first_fp_supported=false;FloatingPointDiagnostic first_fp{}; // First incoming state, accepted or not.
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
        X3M_CACHE_COUNTER(rejected_result) X3M_CACHE_COUNTER(rejected_last_error) X3M_CACHE_COUNTER(rejected_fp)
        std::array<std::atomic<uint64_t>,bypass_reason_count> bypass_reasons{};
        std::atomic<unsigned> unsupported_fp_publication{0},first_fp_publication{0};
        FloatingPointDiagnostic unsupported_fp{},first_fp{};bool first_fp_supported=false;
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
