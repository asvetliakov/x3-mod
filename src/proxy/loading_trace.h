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
// X3M_GZ_BUFFER=1 initializes the same import machinery without X3M_TELEMETRY=1:
// only the zlib gz rows the read-ahead buffer needs are patched then (gz_buffer.h).
namespace x3m::loading_trace {
enum class Operation : unsigned {
    FileOpen, FileRead, FileSeek, Effect, Texture, CubeTexture, Surface,
    CursorSet, CursorPosition, GzOpen, GzRead, GzSeek, Inflate, XmlRead, MeshCreate, MeshClean,
    FindFirst, FindNext, FindClose, // resource resolver directory enumeration (loading-orchestration.md, section 2)
    GzGetc, GzTell, GzClose, GzWrite, // savegame stream rows (savegame-gz-stream.md); routed through gz_buffer when X3M_GZ_BUFFER=1
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
// X3M_MESH_ADJACENCY_DUMP=1 (verify mode): every mismatching mesh, up to a bound,
// is written as <capture directory>\mesh-adjacency-<n>.bin for offline replay
// (tools/analysis/replay_mesh_adjacency.py; fixture `replay` mode). Layout, all
// little-endian: AdjacencyDumpHeader, then declaration_count D3DVERTEXELEMENT9
// (8 bytes each, without the end marker), the vertex bytes (vertices * stride),
// the index bytes (faces * 3 * 2 or 4), the native adjacency and the module
// adjacency (faces * 3 DWORDs each). Game data: the files stay untracked.
struct AdjacencyDumpHeader {
    char magic[8];                                 // "X3MADJ01"
    uint32_t header_size,faces,vertices,stride,position_offset,options,declaration_count,epsilon_bits;
    uint32_t x87_control,mxcsr,mismatches,first,reserved[2];
};
static_assert(sizeof(AdjacencyDumpHeader)==64);
bool adjacency_write_dump(const wchar_t* path,ID3DXMesh* mesh,FLOAT epsilon,const DWORD* native,const DWORD* module,uint64_t mismatches,DWORD first,DWORD x87_control,DWORD mxcsr);
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
// X3M_MESH_ADJACENCY seams: 0 native, 1 verify, 2 fast (the production switch is read once at initialization).
void fixture_adjacency_mode(unsigned mode);
struct AdjacencyStatistics {
    uint64_t calls=0,computed=0,fallbacks=0,faults=0,fast_ticks=0,native_ticks=0;
    uint64_t verify_meshes=0,verify_equal=0,verify_mismatched=0,verify_mismatch_entries=0;
    uint64_t quantized=0,unquantized=0,multi_candidate_meshes=0;
    std::array<uint64_t,6> fallback_reasons{}; // input, gate, declaration, size, lock, module
    std::array<uint64_t,7> module_status{};    // mesh_adjacency_fast::Status order
    bool faulted=false;
};
AdjacencyStatistics fixture_adjacency_statistics();
void fixture_adjacency_report();
#endif
}
