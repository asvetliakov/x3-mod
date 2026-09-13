#pragma once
#include <windows.h>
#include <cstdint>

// Opt-in observation of one verified X3 material-submission callsite. Initialization
// and shutdown require quiescent render submission, outside DllMain. No on-disk
// game change, transform rewrite, or temporal correspondence is performed.
namespace x3m::object_trace {
enum Valid : uint32_t { Node=1, Camera=2, Registry=4, World=8, WorldBasis=16, View=32, Projection=64 };
struct Snapshot {
    uint32_t valid=0, scope_depth=0;
    uint64_t session=0;
    uintptr_t mesh=0, node=0, camera=0, registry=0, engine=0;
    uint32_t node_handle=0, camera_handle=0, model=0, lod=0, flags12c=0, flags130=0;
    uint32_t position[3]{}, basis[9]{}, scale[4]{};
    uint32_t parent=0, alpha13c=0; // capture-only copies from the existing node read
    uint32_t world[16]{}, world_basis[16]{}, view[16]{}, projection[16]{};
};
bool initialize(); // X3M_OBJECT_TRACE=1, exact executable SHA256 + code/site checks
// Exact-executable identity alone (base 0x400000, PE headers, file SHA-256),
// evaluated once per process and cached. Shared by every module that reads
// engine globals; true never implies the callsite patch is installed.
bool executable_verified();
bool active(); // observation enabled; not synonymous with code ownership
bool recovery_required(); // owned code/protection still needs quiescent restoration
const char* status(); // static diagnostic string; initialize once, then read
// Bounded self-process reads through engine_memory (validated direct reads;
// X3M_ENGINE_READS=rpm for the syscall path). matrices=false skips the four
// engine matrices (8 of the 12 reads): the route needs node, camera and
// registry only; capture frames and diagnostics keep the default.
bool current(Snapshot* out, bool matrices = true);
bool shutdown(); // restore only our own displacement, while no submission can run
#ifdef X3M_OBJECT_TRACE_FIXTURE
// Compile-only original fixture seam: absent from production. Caller owns code
// storage and guarantees quiescence. Addresses refer only to original fixture data.
struct FixtureAddresses { uintptr_t engine_slot, world_slot, basis_slot, view_slot, projection_slot; };
bool fixture_install(void* callsite,void* expected_target,const FixtureAddresses&,unsigned fail_stage=0);
bool fixture_shutdown(unsigned fail_stage);
void fixture_fail_next_tls_set();
#endif
}
