#pragma once
#include <cstdint>
#include "loading_windows_fl.h"

// FL sites of the mesh adjacency fast path (src/proxy/loading_trace.cpp's two mesh import rows and the shared COM
// vtable hooks over src/proxy/mesh_adjacency_fast.cpp; AP matches d3dx9_37.dll). Verified facts:
// docs/reverse-engineering/fl/mesh-adjacency.md, "Port (2026-11-03)" (verifier
// verification/probe/verify_mesh_adjacency_sites_fl.py). X3FL.exe imports its mesh factories from d3dx9_43.dll; the
// rows are found by name as on AP and must sit at these slot RVAs (base + RVA). Witnesses (table mesh): the direct
// `call [IAT]` of D3DXCreateMesh at 0x0dbbdb and of D3DXCleanMesh at 0x0dcd35 (operands HIGHLOW = base + slot), the
// GenerateAdjacency call through vtable +0x58 with epsilon 0x358637bd (1e-6f) at 0x0dcc4f and its
// ConvertPointRepsToAdjacency fallback through +0x50 at 0x0dcca1.
namespace x3m::fl::mesh_adjacency_fl {
namespace W = x3m::fl::loading_windows;
constexpr char dll[] = "d3dx9_43.dll";
constexpr std::uint32_t create_slot_rva = 0x150334, clean_slot_rva = 0x150348;
static_assert(W::fixup_target(W::mesh_windows, W::mesh_fixups, "create_call", 0x0d) == create_slot_rva);
static_assert(W::fixup_target(W::mesh_windows, W::mesh_fixups, "clean_call", 0x0d) == clean_slot_rva);
}
