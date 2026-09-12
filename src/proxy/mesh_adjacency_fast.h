#pragma once
#include <cstddef>
#include <cstdint>

// Exact-equality replacement for ID3DXMesh::GenerateAdjacency(epsilon).
// Pure, host-testable: no Windows headers, COM, hooks or globals. The caller
// supplies locked read-only vertex/index memory and receives the D3DX output
// (DWORD[3*faces], 0xffffffff where a face has no neighbour across an edge).
// Ok is returned only when equivalence to the D3DX epsilon welding is
// established for this input (docs/verification/mesh-adjacency-fast.md):
// every distinct pair of positions is further apart than 2*epsilon, so the
// epsilon neighbourhoods are exactly the bit-equality classes (-0 == +0).
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
// D3DX-observed matching rules (fixture evidence in the verification document).
// The defaults reproduce d3dx9_37; the alternatives exist for the fixture to
// demonstrate that they are distinguishable and wrong.
struct Policy {
    bool head_insertion=true;   // Later faces precede earlier ones in an edge bucket.
    bool normal_selection=true; // Among several reverse edges prefer the most parallel face normal.
    bool skip_raw_degenerate=true;  // A face with a repeated index neither enters nor searches the table.
    bool skip_rep_degenerate=false; // A face that repeats a representative only through welding still participates...
    bool drop_welded_corners=true;  // ...but of two corners sharing a representative the larger raw index is invalid; its edges are skipped.
    bool single_adjacency=true;     // The selected candidate is refused when its face is already adjacent to the querying face.
};
struct Report {
    Status status=Status::Input;
    uint32_t representatives=0,welded=0; // Distinct positions; vertices mapped onto an earlier one.
    bool quantized=false;                // All coordinates on the power-of-two grid coarser than 2*epsilon.
    uint32_t degenerate_faces=0,welded_degenerate_faces=0,dropped_edges=0,multi_candidates=0,normal_selected=0,repeated_neighbours=0,unmatched=0;
};
// Writes all 3*face_count entries only on Ok; the array is untouched otherwise.
Report generate(const Input& input,uint32_t* adjacency,const Policy& policy={}) noexcept;
}
