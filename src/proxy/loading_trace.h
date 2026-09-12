#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#ifdef X3M_LOADING_TRACE_FIXTURE
#include "mesh_adjacency_cache.h"
#endif

// Optional loading diagnostics and separately requested experimental adjacency cache.
// Only the main EXE's verified named
// imports and validated shared native mesh-vtable slots are intercepted. Timing-only
// callbacks never inspect payloads; X3M_MESH_CACHE=1 explicitly permits bounded
// exact mesh-byte acquisition/reuse after public readable-buffer qualification.
// Bounded first-table setup pins table/callable module lifetimes and records
// method pointers; it never reads or fingerprints DLL/EXE files.
// Runtime thread-local storage may initialize on a thread's first callback.
// Initialize outside DllMain, after telemetry initialization. Call report from
// the existing periodic telemetry summary. No engine code/prologues are patched.
namespace x3m::loading_trace {
enum class Operation : unsigned {
    FileOpen, FileRead, FileSeek, Effect, Texture, CubeTexture, Surface,
    CursorSet, CursorPosition, GzOpen, GzRead, GzSeek, Inflate, XmlRead, MeshCreate, MeshClean,
    FindFirst, FindNext, FindClose, // resource resolver directory enumeration (loading-orchestration.md, section 2)
    MeshPointReps, MeshAdjacency, MeshOptimize, Count
};
struct Sample {
    uint64_t count=0, failures=0, pending=0, ambiguous=0, bytes=0;
    uint64_t inclusive_ticks=0, exclusive_ticks=0, maximum_ticks=0;
    uint64_t overhead_ticks=0;
};
using Snapshot=std::array<Sample,static_cast<unsigned>(Operation::Count)>;
// One installation generation per process. Reinitialization after teardown is
// refused so foreign chains retain immutable callable originals.
bool initialize();
bool active();
// Per-field atomic exchange: concurrent calls can straddle adjacent reports.
// Totals over the complete run are conserved, but a delta is not a transaction.
Snapshot take_snapshot();
void report();
// Explicit quiescent teardown only, not safe during active callbacks or DllMain.
// Restores a slot only if it still points at this module's interceptor. A cache
// cleanup fault requires stopping application mesh work/restarting; teardown is
// not repair, and restored/unobserved native methods are outside fault coverage.
void shutdown();
#ifdef X3M_LOADING_TRACE_FIXTURE
// Compile-only fixture seam: these symbols do not exist in the production DLL.
bool fixture_initialize(HMODULE target);
// One-shot failed mesh slot installation, 1..3; quiescent synthetic tests only.
void fixture_fail_mesh_patch(unsigned step);
void fixture_fail_protection_restores(unsigned calls);
unsigned fixture_protection_debts();
mesh_adjacency_cache::Statistics fixture_cache_statistics();
bool fixture_cache_constructed();
bool fixture_cache_faulted();
uint64_t fixture_cache_gate_rejections();
uint64_t fixture_cache_blocked();
void fixture_cache_cleanup_failure(HRESULT hr); // Outcome seam; no real buffer is left locked.
void fixture_cache_reenter_once();
bool fixture_cache_contract(ID3DXMesh* mesh); // Preflight only; never dispatches adjacency.
uint64_t fixture_cache_gate_reason(const char* reason);
#endif
}
