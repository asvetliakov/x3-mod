#pragma once
#include <cstddef>
#include <cstdint>

// Exact replacement of ID3DXMesh::GenerateAdjacency(epsilon) for inputs whose
// epsilon welding reduces to position equality. Pure, host-testable: no Windows
// headers, COM or hooks; the only state is one thread-local scratch arena reused
// between calls (release_scratch frees it). The caller supplies locked read-only
// vertex/index memory and receives the D3DX output (DWORD[3*faces], 0xffffffff
// where a face has no neighbour across an edge). The rules are d3dx9_37's own
// (docs/reverse-engineering/d3dx-generate-adjacency.md): vertices are swept in
// D3DX's heapsort order of the float at byte 0 of each vertex (the normals of
// the candidate selection also read the three floats at byte 0), a vertex welds to
// the current representative unless a face contains both, faces whose welded
// corners repeat contribute nothing, directed edges are matched through a chain
// with head insertion, the most parallel normal wins among several candidates,
// and matched or refused entries leave the table exactly as in D3DX.
// Ok is returned only when equivalence to the D3DX epsilon comparison is
// established for this input (docs/verification/mesh-adjacency-fast.md): every
// distinct pair of positions is further apart than 2*epsilon, so the epsilon
// neighbourhoods are exactly the bit-equality classes (-0 == +0).
namespace x3m::mesh_adjacency_fast {
constexpr uint32_t unused=0xffffffffu;
enum class Status : unsigned { Ok, Input, IndexRange, NonFinite, Magnitude, EpsilonNeighbour, Allocation, Count };
constexpr unsigned status_count=static_cast<unsigned>(Status::Count);
const char* status_name(unsigned status) noexcept;
struct Input {
    const void* vertices=nullptr;
    uint32_t vertex_count=0,stride=0,position_offset=0; // FLOAT3 position at offset within each stride.
    const void* indices=nullptr;
    bool indices_32bit=false;
    uint32_t face_count=0;
    float epsilon=0.f;
};
// D3DX rules (decompiled; fixture evidence in the verification document). The
// defaults reproduce d3dx9_37; the alternatives exist for the fixture to
// demonstrate that they are distinguishable and wrong.
struct Policy {
    bool head_insertion=true;    // Later faces precede earlier ones in an edge chain.
    bool normal_selection=true;  // Among several reverse edges prefer the most parallel face normal (first found on ties).
    bool weld_refusal=true;      // A vertex is not welded to a representative when a face contains both.
    bool heap_order=true;        // Sweep vertices in D3DX's heapsort permutation of the byte-0 key (false: stable descending order).
    bool retire_own_entry=false; // true: hide the querying edge even when its lookup found nothing (the pre-review-26 module).
    bool unlink_refused=true;    // The selected entry leaves the table even when the single-adjacency check then refuses it.
    bool later_slot_check=false; // true: the single-adjacency check also looks at the face's later slots (D3DX: earlier slots only).
};
struct Report {
    Status status=Status::Input;
    uint32_t representatives=0,welded=0; // D3DX point representatives; vertices mapped onto another vertex.
    bool quantized=false;                // All coordinates on the power-of-two grid coarser than 2*epsilon.
    uint32_t degenerate_faces=0,welded_degenerate_faces=0,refused_welds=0,multi_candidates=0,normal_selected=0,repeated_neighbours=0,unmatched=0;
};
// Writes all 3*face_count entries only on Ok; the array is untouched otherwise.
Report generate(const Input& input,uint32_t* adjacency,const Policy& policy={}) noexcept;
// Frees the calling thread's retained scratch arena (one malloc per call otherwise;
// arenas above 16 MB are released after the call, smaller ones kept for the next mesh).
void release_scratch() noexcept;
// Which reciprocal square root the build uses for D3DXVec3Normalize: "rsqrtss"
// (x86, the instruction D3DX's SSE table uses) or "portable" (1/sqrt, host tests).
const char* rsqrt_implementation() noexcept;
}
