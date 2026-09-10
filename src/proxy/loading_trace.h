#pragma once
#include <windows.h>
#include <array>
#include <cstdint>

// Optional, read-only loading diagnostics. Only the main EXE's verified named
// imports and validated shared native mesh-vtable slots are intercepted; steady-state timing callbacks never log/inspect payloads or allocate events.
// Bounded first-mesh setup verifies/pins the native DLL and emits setup records;
// that one-time file read and table work are measured in the wrapper tail.
// Runtime thread-local storage may initialize on a thread's first callback.
// Initialize outside DllMain, after telemetry initialization. Call report from
// the existing periodic telemetry summary. No engine code/prologues are patched.
namespace x3m::loading_trace {
enum class Operation : unsigned {
    FileOpen, FileRead, FileSeek, Effect, Texture, CubeTexture, Surface,
    CursorSet, CursorPosition, GzOpen, GzRead, GzSeek, Inflate, XmlRead, MeshCreate, MeshClean, MeshPointReps, MeshAdjacency, MeshOptimize, Count
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
// Restores a slot only if it still points at this module's interceptor.
void shutdown();
#ifdef X3M_LOADING_TRACE_FIXTURE
// Compile-only fixture seam: these symbols do not exist in the production DLL.
uint64_t fixture_fingerprint(HMODULE target);
bool fixture_initialize(HMODULE target,uint64_t expected_hash);
// One-shot failed mesh slot installation, 1..3; quiescent synthetic tests only.
void fixture_fail_mesh_patch(unsigned step);
void fixture_fail_protection_restores(unsigned calls);
unsigned fixture_protection_debts();
#endif
}
