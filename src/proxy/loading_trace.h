#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
struct ID3DXMesh; // d3dx9mesh.h interface; declared here so translation units without D3DX headers can include this file
#ifdef X3M_LOADING_TRACE_FIXTURE
#endif
struct ID3DXMesh; // adjacency_write_dump takes the mesh opaquely; the production header does not include d3dx9mesh.h

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
// X3M_CRYPT_CACHE=1 does the same for the four CryptoAPI rows of the context/key
// cache (crypt_cache.h); with telemetry on those rows are patched even without
// X3M_LOADING_PROBES=1, and the cache's real calls go through the traced wrappers.
namespace x3m::loading_trace {
enum class Operation : unsigned {
    FileOpen, FileRead, FileSeek, Effect, Texture, CubeTexture, Surface,
    CursorSet, CursorPosition, GzOpen, GzRead, GzSeek, Inflate, XmlRead, MeshCreate, MeshClean,
    FindFirst, FindNext, FindClose, // resource resolver directory enumeration (loading-orchestration.md, section 2)
    GzGetc, GzTell, GzClose, GzWrite, // savegame stream rows (savegame-gz-stream.md); routed through gz_buffer when X3M_GZ_BUFFER=1
    // Probe batch 2 (X3M_LOADING_PROBES=1, docs/reverse-engineering/loading-probes.md):
    // patched only when requested; the CryptoAPI rows measure the script
    // signature check 0x004cabc0, the write-side rows the negative-cache
    // invalidation set, the last three the per-open Wine cost next to CreateFileA.
    // (Names avoid the Win32 A/W API macros: CreateDirectory, DeleteFile, CryptAcquireContext... expand.)
    InflateInit, InflateEnd,
    CryptAcquire, CryptRelease, CryptImport, CryptHashCreate, CryptHash,
    CryptVerify, CryptHashParam, CryptHashDestroy, CryptKeyDestroy,
    DirectoryCreate, FileDelete, FileMove, FileMoveEx, FileWrite, FileType, HandleClose,
    MeshPointReps, MeshAdjacency, MeshOptimize, Count
};
constexpr unsigned probe_row_begin=static_cast<unsigned>(Operation::InflateInit);
constexpr unsigned probe_row_end=static_cast<unsigned>(Operation::MeshPointReps);
bool probes_requested(); // X3M_LOADING_PROBES=1 (read once at initialization; needs X3M_TELEMETRY=1)
struct Sample {
    uint64_t count=0, failures=0, pending=0, ambiguous=0, bytes=0;
    uint64_t inclusive_ticks=0, exclusive_ticks=0, maximum_ticks=0;
    uint64_t overhead_ticks=0;
};
using Snapshot=std::array<Sample,static_cast<unsigned>(Operation::Count)>;
// One installation generation per process. Reinitialization after teardown is
// refused so foreign chains retain immutable callable originals.
bool initialize();
// X3M_MESH_ADJACENCY=fast (the only mode that arms without X3M_TELEMETRY=1):
// initialize() then patches the two D3DX mesh import rows alone. The caller's
// initialize gate must include this for a fast-only launch.
bool mesh_adjacency_requested();
bool active();
// Per-field atomic exchange: concurrent calls can straddle adjacent reports.
// Totals over the complete run are conserved, but a delta is not a transaction.
Snapshot take_snapshot();
void report();
// crypt_cache line: scope "window" (deltas since the previous window line, part
// of report) or "session" (cumulative; capture.cpp logs it when the last device
// is destroyed). No-op unless the cache is active.
void crypt_cache_report(const char* scope);
bool crypt_cache_enabled();
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
// D3DX's math-table dispatch, mirrored (docs/reverse-engineering/
// d3dx-generate-adjacency.md, section 4): the D3DXVec3Normalize the process's
// d3dx9_37 installed at load decides the normals of the adjacency candidate
// selection, so the module must use the same one. Evaluated once from the
// documented inputs D3DX itself reads (HKLM\Software\Microsoft\Direct3D
// DisablePSGP / DisableD3DXPSGP, IsProcessorFeaturePresent for 3DNow and SSE,
// CPUID for MMX, SSE2 and the extended 3DNow bit); D3DX's memory is never read.
// Generic and SSE2 are modeled; other tables and unknown dispatch use native.
// The dump header's reserved[0] records 1 + this value.
enum class D3dxMathTable : unsigned { Generic, ThreeDNow, Sse2, Sse, Unknown };
D3dxMathTable d3dx_math_table();
const char* d3dx_math_table_name(D3dxMathTable table);
#ifdef X3M_LOADING_TRACE_FIXTURE
// Compile-only fixture seam: these symbols do not exist in the production DLL.
bool fixture_initialize(HMODULE target);
void fixture_crypt_image_base(HMODULE base);
void fixture_crypt_patch_control(unsigned step,void(*observer)());
bool fixture_crypt_site(const void* caller,Operation operation);
// One-shot failed mesh slot installation, 1..3; quiescent synthetic tests only.
void fixture_fail_mesh_patch(unsigned step);
void fixture_fail_protection_restores(unsigned calls);
unsigned fixture_protection_debts();
// X3M_MESH_ADJACENCY seams: 0 native, 1 verify, 2 fast (the production switch is read once at initialization).
void fixture_adjacency_mode(unsigned mode);
void fixture_adjacency_math_table(int table); // -1 restores real process dispatch; no production override.
bool fixture_adjacency_registry_dword(LSTATUS status,DWORD type,DWORD size);
bool fixture_adjacency_registry_ambiguous(LSTATUS status,DWORD type,DWORD size);
HRESULT fixture_adjacency_fast(ID3DXMesh*,FLOAT,DWORD*,HRESULT(WINAPI*)(ID3DXMesh*,FLOAT,DWORD*));
struct AdjacencyStatistics {
    uint64_t calls=0,computed=0,fallbacks=0,faults=0,fast_ticks=0,native_ticks=0;
    uint64_t verify_admitted=0,verify_admitted_mismatched=0,verify_refused_fp=0,verify_refused_competing=0;
    uint64_t verify_meshes=0,verify_equal=0,verify_mismatched=0,verify_mismatch_entries=0;
    uint64_t quantized=0,unquantized=0,multi_candidate_meshes=0;
    std::array<uint64_t,9> fallback_reasons{}; // input, gate, declaration, size, lock, module, math_table, competing_normals, fp_domain
    std::array<uint64_t,8> module_status{};    // mesh_adjacency_fast::Status order
    bool faulted=false;
};
AdjacencyStatistics fixture_adjacency_statistics();
void fixture_adjacency_report();
#endif
}
