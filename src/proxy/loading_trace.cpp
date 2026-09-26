#include "loading_trace.h"
#include "config.h"
#include "capture.h"
#include "mesh_adjacency_fast.h"
#include "../ownership/d3d9_ownership.h"
#include "../ownership/application_admission_abi.h"
#include "cpu_state.h"
#include "gz_buffer.h"
#include "crypt_cache.h"
#include "loading_trace_light.h"
#include "loading_probes.h"
#include "resource_reader.h"
#include <new>
#include <d3dx9.h>
#include <cpuid.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <type_traits>
#include <cwchar>
#include "log_tiers.h"

namespace x3m::loading_trace {
namespace {
constexpr unsigned count = static_cast<unsigned>(Operation::Count);
struct Hook {
    const char* dll;
    const char* name;
    PVOID replacement;
    PVOID original = nullptr;
    PVOID* slot = nullptr;
};
std::atomic<bool> installed{false};
bool installation_started = false; // One installation generation; originals never rebound.
std::atomic<unsigned> mesh_owned_slots{0}, protection_debts{0};
uint64_t clock_frequency = 0, coverage_start = 0;
unsigned hook_count = 0;
constexpr unsigned mesh_table_limit = 8;
constexpr unsigned mesh_slot_indices[3] = {20, 22, 27};
constexpr unsigned mesh_contract_slots[] = {4, 5, 7, 8, 9, 13, 14, 15, 16, 17, 18};
struct MeshTable {
    PVOID* table = nullptr;
    Hook slots[3]{};
    PVOID contract[11]{};
    int last_result = -1, last_owned = -1;
};
MeshTable mesh_tables[mesh_table_limit];
SRWLOCK mesh_lock = SRWLOCK_INIT;
std::atomic<bool> mesh_observation_enabled{false};
HMODULE mesh_module = nullptr; // Factory implementation lifetime is pinned, not version qualified.
bool mesh_module_checked = false;
// The public buffer qualification of the adjacency service (fast/verify); its
// rejection reasons are counted per reason (the adjacency cache that first used
// it was removed on 2026-09-25).
enum class GateReason : unsigned {
    Unavailable,
    Input,
    MeshObject,
    MeshTable,
    MeshMethod,
    MeshPool,
    MeshOptions,
    VertexAcquire,
    IndexAcquire,
    BufferMissing,
    Tracker,
    DescriptorCall,
    DescriptorPool,
    DescriptorUsage,
    DescriptorFormat,
    DescriptorSize,
    Count
};
constexpr unsigned gate_reason_count = static_cast<unsigned>(GateReason::Count);
const char* gate_reason_name(unsigned i) {
    static constexpr const char* names[] = {
        "unavailable",    "input",           "mesh_object",     "mesh_table",       "mesh_method",
        "mesh_pool",      "mesh_options",    "vertex_acquire",  "index_acquire",    "buffer_missing",
        "tracker",        "descriptor_call", "descriptor_pool", "descriptor_usage", "descriptor_format",
        "descriptor_size"};
    return i < gate_reason_count ? names[i] : "unknown";
}
struct GateDetail {
    unsigned scope = 0; // 0 mesh, 1 vertex buffer, 2 index buffer.
    DWORD options = 0, slot = 0, actual_entry = 0, expected_entry = 0;
    HRESULT status = S_OK;
    DWORD pool = 0, usage = 0, format = 0;
    uint64_t required_bytes = 0, size_bytes = 0, pending = 0;
    bool known = false;
};
struct GateCounter {
    std::atomic<uint64_t> count{0};
    std::atomic<unsigned> publication{0};
    GateDetail first{};
};
GateCounter cache_gate_reasons[gate_reason_count];
uint64_t cache_gate_last_counts[gate_reason_count]{};
bool reject_gate(GateReason reason, const GateDetail& detail = {}) {
    auto& row = cache_gate_reasons[static_cast<unsigned>(reason)];
    row.count.fetch_add(1, std::memory_order_relaxed);
    unsigned empty = 0;
    if (row.publication.compare_exchange_strong(empty, 1, std::memory_order_acquire)) {
        row.first = detail;
        row.publication.store(2, std::memory_order_release);
    }
    return false;
}
struct X87Environment {
    DWORD control, status, tag, ip, cs, dp, ds;
};
struct ComputationalState {
    X87Environment x87;
    DWORD mxcsr;
};
ComputationalState computational_state() {
    ComputationalState value{};
    asm volatile("fnstenv %0\n\tfldenv %0\n\tstmxcsr %1" : "=m"(value.x87), "=m"(value.mxcsr)::"memory");
    return value;
}
void restore_computational_state(const ComputationalState& value) {
    asm volatile("fldenv %0\n\tldmxcsr %1" ::"m"(value.x87), "m"(value.mxcsr) : "memory");
}
// Our SSE2 arithmetic runs under the default MXCSR (masked, nearest, no FTZ/DAZ)
// whatever the caller's state (the game enters with 0x9fc0); callers restore theirs.
void set_default_mxcsr() {
    static const DWORD value = 0x1f80;
    asm volatile("ldmxcsr %0" ::"m"(value) : "memory");
}
bool cache_buffer_contract(ID3DXMesh* mesh);
#ifdef X3M_LOADING_TRACE_FIXTURE
unsigned fail_mesh_patch = 0, fail_protection_restore = 0;
#endif
void observe_mesh(ID3DXMesh* mesh);
void restore_mesh_hooks();
uint64_t tick() {
    return light::tick();
}
// Timing span of the heavy (D3DX/mesh) rows: the accounting and the nesting
// live in loading_trace_light.cpp (light::Span); this adapter keeps the former
// call shape (finish after the CpuCallBoundary's after_original()).
struct Span {
    light::Span inner;
    Operation op;
    explicit Span(Operation value)
        : op(value) {
        inner.begin(static_cast<unsigned>(value));
        inner.before_call();
    }
    void finish(uint64_t, DWORD, bool failed = false, uint64_t bytes = 0, bool pending = false,
                bool ambiguous = false) {
        inner.finish(failed, bytes, pending, ambiguous);
    }
};

// Each bounded table has distinct trampolines. Dispatch therefore continues to
// the correct original even if another interceptor later clones/replaces a
// mesh vptr and chains to us. No object registry/refcounts or lock around calls.
using PointRepsFn = HRESULT(WINAPI*)(ID3DXMesh*, const DWORD*, DWORD*);
using AdjacencyFn = HRESULT(WINAPI*)(ID3DXMesh*, FLOAT, DWORD*);
using OptimizeFn = HRESULT(WINAPI*)(ID3DXMesh*, DWORD, const DWORD*, DWORD*, DWORD*, ID3DXBuffer**);
static_assert(
    std::is_same_v<decltype(&ID3DXMesh::GenerateAdjacency), HRESULT (STDMETHODCALLTYPE ID3DXMesh::*)(FLOAT, DWORD*)>);
static_assert(std::is_same_v<decltype(&ID3DXMesh::ConvertPointRepsToAdjacency),
                             HRESULT (STDMETHODCALLTYPE ID3DXMesh::*)(const DWORD*, DWORD*)>);
static_assert(
    std::is_same_v<decltype(&ID3DXMesh::OptimizeInplace),
                   HRESULT (STDMETHODCALLTYPE ID3DXMesh::*)(DWORD, const DWORD*, DWORD*, DWORD*, ID3DXBuffer**)>);
template <unsigned Index> HRESULT WINAPI mesh_point_reps(ID3DXMesh* mesh, const DWORD* reps, DWORD* adjacency) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    Span span(Operation::MeshPointReps);
    cpu.before_original();
    const HRESULT hr = reinterpret_cast<PointRepsFn>(mesh_tables[Index].slots[0].original)(mesh, reps, adjacency);
    cpu.after_original();
    const DWORD error = GetLastError();
    const auto end = tick();
    span.finish(end, error, FAILED(hr));
    return hr;
}
// X3M_MESH_ADJACENCY=native|verify|fast: exact-equality replacement of the
// D3DX epsilon welding (docs/verification/mesh-adjacency-fast.md). Verify runs
// the native method, recomputes and compares; fast answers from the module and
// falls through to native on any qualification failure. Both use the public
// buffer qualification (cache_buffer_contract).
namespace adjacency_fast = mesh_adjacency_fast;
using AdjacencyMode = adjacency_fast::Mode; // parse/arming policy is host-tested (mesh_adjacency_fast.h)
std::atomic<AdjacencyMode> adjacency_mode{AdjacencyMode::Native};
std::atomic<bool> adjacency_faulted{false};
enum class AdjacencyFallback : unsigned {
    Input,
    Gate,
    Declaration,
    Size,
    Lock,
    Module,
    MathTable,
    CompetingNormals,
    FpDomain,
    Count
};
constexpr unsigned adjacency_fallback_count = static_cast<unsigned>(AdjacencyFallback::Count);
// d3dx9_37's math-table dispatch (0x00587e6b), mirrored from its documented
// inputs: DisablePSGP / DisableD3DXPSGP (HKLM\Software\Microsoft\Direct3D,
// DWORD; 1 keeps the generic table, 2 skips 3DNow); then, unless 2, the 3DNow
// check (IsProcessorFeaturePresent(7) on NT 5+, D3DX's 0x00587e06) selects the
// 3DNow installer (0x0074a996), which installs only when CPUID reports MMX (leaf
// 1 EDX bit 23) and 3DNow (leaf 0x80000001 EDX bit 31) and otherwise leaves the
// generic table (the FEX case: 3DNow reported, bit absent); without 3DNow, SSE2
// (CPUID leaf 1 EDX bit 26, 0x00587c94) selects the SSE2 table, else
// IsProcessorFeaturePresent(6) the SSE table.
bool adjacency_registry_dword(LSTATUS status, DWORD type, DWORD size) {
    return status == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(DWORD);
}
bool adjacency_registry_ambiguous(LSTATUS status, DWORD type, DWORD size) {
    return status == ERROR_SUCCESS && type == REG_DWORD && size != sizeof(DWORD);
}
D3dxMathTable d3dx_math_table_detect() {
    bool ambiguous = false;
    auto registry = [&](const char* value, DWORD& out) {
        HKEY key = nullptr;
        if (RegOpenKeyA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Direct3D", &key) != ERROR_SUCCESS) return false;
        DWORD type = 0, size = sizeof out;
        const LSTATUS status = RegQueryValueExA(key, value, nullptr, &type, reinterpret_cast<BYTE*>(&out), &size);
        RegCloseKey(key);
        // Native checks the type but not returned size. A malformed DWORD must
        // not be mistaken for an absent override and silently select a table.
        ambiguous |= adjacency_registry_ambiguous(status, type, size);
        return adjacency_registry_dword(status, type, size);
    };
    DWORD psgp = 0;
    if (!registry("DisablePSGP", psgp)) psgp = 0;
    DWORD d3dx_psgp = 0;
    if (registry("DisableD3DXPSGP", d3dx_psgp)) psgp = d3dx_psgp;
    if (ambiguous) return D3dxMathTable::Unknown;
    if (psgp == 1) return D3dxMathTable::Generic;
    unsigned edx1 = 0, ext_edx = 0;
    {
        unsigned a = 0, b = 0, c = 0, d = 0;
        if (__get_cpuid(0, &a, &b, &c, &d) && a != 0) {
            __get_cpuid(1, &a, &b, &c, &d);
            edx1 = d;
        }
        if (__get_cpuid(0x80000000u, &a, &b, &c, &d) && a > 0x80000000u) {
            __get_cpuid(0x80000001u, &a, &b, &c, &d);
            ext_edx = d;
        }
    }
    if (psgp != 2 && IsProcessorFeaturePresent(PF_3DNOW_INSTRUCTIONS_AVAILABLE))
        return ((edx1 & (1u << 23)) && (ext_edx & (1u << 31))) ? D3dxMathTable::ThreeDNow : D3dxMathTable::Generic;
    if (edx1 & (1u << 26)) return D3dxMathTable::Sse2;
    if (IsProcessorFeaturePresent(PF_XMMI_INSTRUCTIONS_AVAILABLE)) return D3dxMathTable::Sse;
    return D3dxMathTable::Generic;
}
}
#ifdef X3M_LOADING_TRACE_FIXTURE
int adjacency_fixture_math_table = -1;
#endif
D3dxMathTable d3dx_math_table() {
#ifdef X3M_LOADING_TRACE_FIXTURE
    if (adjacency_fixture_math_table >= 0) return static_cast<D3dxMathTable>(adjacency_fixture_math_table);
#endif
    static const D3dxMathTable table = d3dx_math_table_detect();
    return table;
}
const char* d3dx_math_table_name(D3dxMathTable table) {
    static constexpr const char* names[] = {"generic", "3dnow", "sse2", "sse", "unknown"};
    return unsigned(table) < 5 ? names[unsigned(table)] : "unknown";
}
namespace {
const char* adjacency_mode_name(AdjacencyMode mode) {
    return mode == AdjacencyMode::Fast ? "fast" : mode == AdjacencyMode::Verify ? "verify" : "native";
}
struct AdjacencyCounters {
    std::atomic<uint64_t> calls{0}, computed{0}, fallbacks{0}, faults{0}, faces{0}, vertices{0};
    std::atomic<uint64_t> fast_ticks{0}, fast_max_ticks{0}, native_ticks{0}, native_max_ticks{0};
    std::atomic<uint64_t> verify_meshes{0}, verify_equal{0}, verify_mismatched{0}, verify_entries{0},
        verify_mismatch_entries{0}, verify_native_failures{0};
    std::atomic<uint64_t> verify_admitted{0}, verify_admitted_mismatched{0}, verify_refused_fp{0},
        verify_refused_competing{0};
    std::atomic<uint64_t> quantized{0}, unquantized{0}, welded_vertices{0}, multi_candidate_meshes{0},
        normal_selected{0}, degenerate_faces{0};
    std::atomic<uint64_t> mismatch_lines{0};
    std::atomic<unsigned> fp_publication{0};
    ComputationalState first_fp{};
    std::array<std::atomic<uint64_t>, adjacency_fallback_count> fallback_reasons{};
    std::array<std::atomic<uint64_t>, adjacency_fast::status_count> module_status{};
} adjacency_counters;
uint64_t adjacency_last_report_calls = 0;
bool adjacency_fp_reported = false;
void adjacency_note_fp(const ComputationalState& state) {
    unsigned empty = 0;
    if (adjacency_counters.fp_publication.compare_exchange_strong(empty, 1, std::memory_order_acquire)) {
        adjacency_counters.first_fp = state;
        adjacency_counters.fp_publication.store(2, std::memory_order_release);
    }
}
constexpr uint64_t adjacency_mismatch_line_limit = 64;
thread_local AdjacencyFn adjacency_thread_original = nullptr;
struct AdjacencyCompute {
    bool computed = false, fault = false;
    HRESULT fault_hr = S_OK;
    AdjacencyFallback fallback = AdjacencyFallback::Input;
    adjacency_fast::Report report{};
    uint64_t ticks = 0;
    DWORD faces = 0, vertices = 0;
};
void adjacency_note_max(std::atomic<uint64_t>& slot, uint64_t value) {
    uint64_t old = slot.load(std::memory_order_relaxed);
    while (old < value && !slot.compare_exchange_weak(old, value, std::memory_order_relaxed)) {}
}
// Public-interface path only: the cache gate (mesh table/method pointers,
// SYSTEMMEM pool, descriptors), then GetDeclaration and READONLY Lock/Unlock.
AdjacencyCompute adjacency_compute(ID3DXMesh* mesh, FLOAT epsilon, DWORD* output, bool enforce_admission,
                                   const ComputationalState& incoming) {
    AdjacencyCompute r;
    auto fall = [&](AdjacencyFallback reason) {
        r.fallback = reason;
        adjacency_counters.fallbacks.fetch_add(1, std::memory_order_relaxed);
        adjacency_counters.fallback_reasons[unsigned(reason)].fetch_add(1, std::memory_order_relaxed);
        return r;
    };
    if (!mesh || !output) return fall(AdjacencyFallback::Input);
    if (enforce_admission &&
        !adjacency_fast::supported_fp_domain(incoming.x87.control, incoming.x87.tag, incoming.mxcsr))
        return fall(AdjacencyFallback::FpDomain);
    if (!cache_buffer_contract(mesh)) return fall(AdjacencyFallback::Gate);
    const DWORD options = mesh->GetOptions(), vertices = mesh->GetNumVertices(), faces = mesh->GetNumFaces(),
                stride = mesh->GetNumBytesPerVertex();
    r.faces = faces;
    r.vertices = vertices;
    if (!vertices || !faces || stride < 12) return fall(AdjacencyFallback::Declaration);
    D3DVERTEXELEMENT9 declaration[MAX_FVF_DECL_SIZE]{};
    if (FAILED(mesh->GetDeclaration(declaration))) return fall(AdjacencyFallback::Declaration);
    DWORD position_offset = 0;
    bool position_found = false;
    for (unsigned i = 0; i < MAX_FVF_DECL_SIZE && declaration[i].Stream != 0xff; ++i) {
        const auto& e = declaration[i];
        if (e.Stream != 0)
            return fall(AdjacencyFallback::Declaration); // D3DX's parse ignores the stream number; only single-stream
                                                         // declarations are mirrored here
        if (e.Usage == D3DDECLUSAGE_POSITION && e.UsageIndex == 0 && e.Type == D3DDECLTYPE_FLOAT3) {
            position_offset = e.Offset;
            position_found = true;
        } // D3DX keeps the last such element
    }
    if (!position_found || position_offset > stride - 12) return fall(AdjacencyFallback::Declaration);
    // D3DX walks the faces of the attribute table's ranges when a table exists;
    // the module walks all faces in order, which is the same only for a table
    // covering the faces contiguously in order (none on the engine's fresh meshes).
    DWORD attribute_count = 0;
    if (FAILED(mesh->GetAttributeTable(nullptr, &attribute_count))) return fall(AdjacencyFallback::Declaration);
    if (attribute_count) {
        D3DXATTRIBUTERANGE ranges[16]{};
        if (attribute_count > 16 || FAILED(mesh->GetAttributeTable(ranges, &attribute_count)))
            return fall(AdjacencyFallback::Declaration);
        DWORD next_face = 0;
        for (DWORD i = 0; i < attribute_count; ++i) {
            if (ranges[i].FaceStart != next_face) return fall(AdjacencyFallback::Declaration);
            next_face += ranges[i].FaceCount;
        }
        if (next_face != faces) return fall(AdjacencyFallback::Declaration);
    }
    // The candidate selection's normals must come from the D3DXVec3Normalize this
    // process's D3DX installed (loading_trace.h, d3dx_math_table); the module
    // reproduces the generic and the SSE2 tables, the 3DNow and SSE ones are native's.
    adjacency_fast::Policy policy;
    switch (d3dx_math_table()) {
    case D3dxMathTable::Sse2:
        policy.normalize = adjacency_fast::Normalize::Sse2;
        policy.refuse_competing_normals = enforce_admission;
        break;
    case D3dxMathTable::Generic: policy.normalize = adjacency_fast::Normalize::Generic; break;
    default: return fall(AdjacencyFallback::MathTable);
    }
    const uint64_t vb_bytes = uint64_t(vertices) * stride,
                   ib_bytes = uint64_t(faces) * 3 * ((options & D3DXMESH_32BIT) ? 4 : 2),
                   value_bytes = uint64_t(faces) * 3 * sizeof(DWORD);
    if (vb_bytes > 0x7fffffffu || ib_bytes > 0x7fffffffu || value_bytes > 0x7fffffffu ||
        uint64_t(reinterpret_cast<uintptr_t>(output)) + value_bytes > (uint64_t(1) << 32))
        return fall(AdjacencyFallback::Size);
    void* vertex_data = nullptr;
    void* index_data = nullptr;
    if (FAILED(mesh->LockVertexBuffer(D3DLOCK_READONLY, &vertex_data)) || !vertex_data)
        return fall(AdjacencyFallback::Lock);
    auto unlock = [&](bool vertex) {
        HRESULT hr = vertex ? mesh->UnlockVertexBuffer() : mesh->UnlockIndexBuffer();
        if (FAILED(hr))
            hr = vertex ? mesh->UnlockVertexBuffer() : mesh->UnlockIndexBuffer(); // one recovery attempt, as the cache
        if (FAILED(hr)) {
            r.fault = true;
            r.fault_hr = hr;
        }
    };
    if (FAILED(mesh->LockIndexBuffer(D3DLOCK_READONLY, &index_data)) || !index_data) {
        unlock(true);
        if (r.fault) return r;
        return fall(AdjacencyFallback::Lock);
    }
    const uintptr_t out_begin = reinterpret_cast<uintptr_t>(output), out_end = out_begin + size_t(value_bytes);
    auto overlaps = [&](const void* p, uint64_t n) {
        const uintptr_t b = reinterpret_cast<uintptr_t>(p);
        return b + n > (uint64_t(1) << 32) || (b < out_end && out_begin < b + n);
    };
    bool ran = false;
    if (!overlaps(vertex_data, vb_bytes) && !overlaps(index_data, ib_bytes)) {
        adjacency_fast::Input in;
        in.vertices = vertex_data;
        in.vertex_count = vertices;
        in.stride = stride;
        in.position_offset = position_offset;
        in.indices = index_data;
        in.indices_32bit = (options & D3DXMESH_32BIT) != 0;
        in.face_count = faces;
        in.epsilon = epsilon;
        static_assert(sizeof(DWORD) == sizeof(uint32_t));
        const auto begin = tick();
        r.report = adjacency_fast::generate(in, reinterpret_cast<uint32_t*>(output), policy);
        r.ticks = tick() - begin;
        ran = true;
    }
    unlock(false);
    unlock(true);
    if (r.fault) return r;
    if (!ran) return fall(AdjacencyFallback::Size);
    adjacency_counters.module_status[unsigned(r.report.status)].fetch_add(1, std::memory_order_relaxed);
    if (r.report.status == adjacency_fast::Status::CompetingNormals) return fall(AdjacencyFallback::CompetingNormals);
    if (r.report.status != adjacency_fast::Status::Ok) return fall(AdjacencyFallback::Module);
    r.computed = true;
    adjacency_counters.computed.fetch_add(1, std::memory_order_relaxed);
    adjacency_counters.faces.fetch_add(faces, std::memory_order_relaxed);
    adjacency_counters.vertices.fetch_add(vertices, std::memory_order_relaxed);
    adjacency_counters.fast_ticks.fetch_add(r.ticks, std::memory_order_relaxed);
    adjacency_note_max(adjacency_counters.fast_max_ticks, r.ticks);
    (r.report.quantized ? adjacency_counters.quantized : adjacency_counters.unquantized)
        .fetch_add(1, std::memory_order_relaxed);
    adjacency_counters.welded_vertices.fetch_add(r.report.welded, std::memory_order_relaxed);
    adjacency_counters.multi_candidate_meshes.fetch_add(r.report.multi_candidates != 0, std::memory_order_relaxed);
    adjacency_counters.normal_selected.fetch_add(r.report.normal_selected, std::memory_order_relaxed);
    adjacency_counters.degenerate_faces.fetch_add(r.report.degenerate_faces, std::memory_order_relaxed);
    return r;
}
void adjacency_fault(HRESULT hr) {
    adjacency_counters.faults.fetch_add(1, std::memory_order_relaxed);
    bool expected = false;
    if (adjacency_faulted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        adjacency_mode.store(AdjacencyMode::Native, std::memory_order_release);
        log("mesh_adjacency_fault origin=acquisition_cleanup_failure hr=%08lx mode=native restart_required=1 native_fallback=0 lock_repaired=0",
            hr);
    }
}
// Serves as the adjacency cache's "original": preserves LastError and the
// computational state of a state-transparent call on the computed path.
HRESULT WINAPI adjacency_fast_service(ID3DXMesh* mesh, FLOAT epsilon, DWORD* adjacency) {
    const AdjacencyFn original = adjacency_thread_original;
    const DWORD error = GetLastError();
    const auto fp = computational_state();
    adjacency_note_fp(fp);
    set_default_mxcsr();
    adjacency_counters.calls.fetch_add(1, std::memory_order_relaxed);
    const AdjacencyCompute r = adjacency_compute(mesh, epsilon, adjacency, true, fp);
    restore_computational_state(fp);
    SetLastError(error);
    if (r.fault) {
        adjacency_fault(r.fault_hr);
        return r.fault_hr;
    }
    if (r.computed) return S_OK;
    return original ? original(mesh, epsilon, adjacency) : E_POINTER;
}
}
bool adjacency_write_dump(const wchar_t* path, ID3DXMesh* mesh, FLOAT epsilon, const DWORD* native, const DWORD* module,
                          uint64_t mismatches, DWORD first, DWORD x87_control, DWORD mxcsr) {
    if (!path || !mesh || !native || !module) return false;
    const DWORD options = mesh->GetOptions(), vertices = mesh->GetNumVertices(), faces = mesh->GetNumFaces(),
                stride = mesh->GetNumBytesPerVertex();
    if (!vertices || !faces || stride < 12) return false;
    D3DVERTEXELEMENT9 declaration[MAX_FVF_DECL_SIZE]{};
    if (FAILED(mesh->GetDeclaration(declaration))) return false;
    AdjacencyDumpHeader header{};
    std::memcpy(header.magic, "X3MADJ01", 8);
    header.reserved[0] = 1u + unsigned(d3dx_math_table());
    header.header_size = sizeof header;
    header.faces = faces;
    header.vertices = vertices;
    header.stride = stride;
    header.options = options;
    bool position_found = false;
    for (unsigned i = 0; i < MAX_FVF_DECL_SIZE && declaration[i].Stream != 0xff; ++i) {
        const auto& e = declaration[i];
        ++header.declaration_count;
        if (e.Stream == 0 && e.Usage == D3DDECLUSAGE_POSITION && e.UsageIndex == 0 && e.Type == D3DDECLTYPE_FLOAT3) {
            header.position_offset = e.Offset;
            position_found = true;
        } // D3DX keeps the last such element
    }
    if (!position_found) return false;
    static_assert(sizeof(D3DVERTEXELEMENT9) == 8);
    std::memcpy(&header.epsilon_bits, &epsilon, sizeof header.epsilon_bits);
    header.x87_control = x87_control;
    header.mxcsr = mxcsr;
    header.mismatches = mismatches > 0xffffffffu ? 0xffffffffu : DWORD(mismatches);
    header.first = first;
    const uint64_t vb_bytes = uint64_t(vertices) * stride,
                   ib_bytes = uint64_t(faces) * 3 * ((options & D3DXMESH_32BIT) ? 4 : 2),
                   adjacency_bytes = uint64_t(faces) * 3 * sizeof(DWORD);
    if (vb_bytes > 0x7fffffffu || ib_bytes > 0x7fffffffu || adjacency_bytes > 0x7fffffffu) return false;
    void* vertex_data = nullptr;
    void* index_data = nullptr;
    if (FAILED(mesh->LockVertexBuffer(D3DLOCK_READONLY, &vertex_data)) || !vertex_data) return false;
    if (FAILED(mesh->LockIndexBuffer(D3DLOCK_READONLY, &index_data)) || !index_data) {
        mesh->UnlockVertexBuffer();
        return false;
    }
    bool written = false;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        auto put = [&](const void* data, uint64_t bytes) {
            DWORD done = 0;
            return WriteFile(file, data, DWORD(bytes), &done, nullptr) && done == DWORD(bytes);
        };
        written = put(&header, sizeof header) &&
                  put(declaration, uint64_t(header.declaration_count) * sizeof(D3DVERTEXELEMENT9)) &&
                  put(vertex_data, vb_bytes) && put(index_data, ib_bytes) && put(native, adjacency_bytes) &&
                  put(module, adjacency_bytes);
        CloseHandle(file);
    }
    mesh->UnlockIndexBuffer();
    mesh->UnlockVertexBuffer();
    return written;
}
namespace {
HRESULT WINAPI adjacency_verify_service(ID3DXMesh* mesh, FLOAT epsilon, DWORD* adjacency) {
    const AdjacencyFn original = adjacency_thread_original;
    if (!original) return E_POINTER;
    const auto incoming = computational_state();
    adjacency_counters.calls.fetch_add(1, std::memory_order_relaxed);
    adjacency_note_fp(incoming);
    const auto native_begin = tick();
    const HRESULT hr = original(mesh, epsilon, adjacency);
    const uint64_t native_ticks = tick() - native_begin;
    const DWORD error = GetLastError();
    const auto fp = computational_state();
    set_default_mxcsr();
    adjacency_counters.native_ticks.fetch_add(native_ticks, std::memory_order_relaxed);
    adjacency_note_max(adjacency_counters.native_max_ticks, native_ticks);
    if (FAILED(hr)) {
        adjacency_counters.verify_native_failures.fetch_add(1, std::memory_order_relaxed);
        restore_computational_state(fp);
        SetLastError(error);
        return hr;
    }
    const DWORD faces = mesh ? mesh->GetNumFaces() : 0;
    DWORD* scratch = faces && faces <= 0x0fffffffu
                         ? static_cast<DWORD*>(HeapAlloc(GetProcessHeap(), 0, size_t(faces) * 3 * sizeof(DWORD)))
                         : nullptr;
    if (scratch) {
        const AdjacencyCompute r = adjacency_compute(mesh, epsilon, scratch, false, incoming);
        if (r.fault) {
            HeapFree(GetProcessHeap(), 0, scratch);
            restore_computational_state(fp);
            SetLastError(error);
            adjacency_fault(r.fault_hr);
            return r.fault_hr;
        }
        if (r.computed) {
            const size_t entries = size_t(faces) * 3;
            uint64_t mismatches = 0;
            size_t first = entries;
            for (size_t i = 0; i < entries; ++i)
                if (scratch[i] != adjacency[i]) {
                    if (!mismatches) first = i;
                    ++mismatches;
                }
            adjacency_counters.verify_meshes.fetch_add(1, std::memory_order_relaxed);
            adjacency_counters.verify_entries.fetch_add(entries, std::memory_order_relaxed);
            const bool fp_supported = adjacency_fast::supported_fp_domain(incoming.x87.control, incoming.x87.tag,
                                                                          incoming.mxcsr);
            const bool competing = r.report.potential_competing_normals && d3dx_math_table() == D3dxMathTable::Sse2;
            if (!fp_supported) adjacency_counters.verify_refused_fp.fetch_add(1, std::memory_order_relaxed);
            if (competing) adjacency_counters.verify_refused_competing.fetch_add(1, std::memory_order_relaxed);
            if (fp_supported && !competing) {
                adjacency_counters.verify_admitted.fetch_add(1, std::memory_order_relaxed);
                adjacency_counters.verify_admitted_mismatched.fetch_add(mismatches != 0, std::memory_order_relaxed);
            }
            if (!mismatches)
                adjacency_counters.verify_equal.fetch_add(1, std::memory_order_relaxed);
            else {
                adjacency_counters.verify_mismatched.fetch_add(1, std::memory_order_relaxed);
                adjacency_counters.verify_mismatch_entries.fetch_add(mismatches, std::memory_order_relaxed);
                if (adjacency_counters.mismatch_lines.fetch_add(1, std::memory_order_relaxed) <
                    adjacency_mismatch_line_limit)
                    log("mesh_adjacency verify faces=%lu vertices=%lu equal=0 mismatches=%llu first=%lu native=%08lx fast=%08lx native_us=%.3f fast_us=%.3f quantized=%u welded=%lu multi_candidates=%lu normal_selected=%lu degenerate_faces=%lu welded_degenerate_faces=%lu refused_welds=%lu repeated_neighbours=%lu",
                        faces, r.vertices, mismatches, DWORD(first), adjacency[first], scratch[first],
                        double(native_ticks) * 1e6 / clock_frequency, double(r.ticks) * 1e6 / clock_frequency,
                        unsigned(r.report.quantized), DWORD(r.report.welded), DWORD(r.report.multi_candidates),
                        DWORD(r.report.normal_selected), DWORD(r.report.degenerate_faces),
                        DWORD(r.report.welded_degenerate_faces), DWORD(r.report.refused_welds),
                        DWORD(r.report.repeated_neighbours));
            }
        }
        HeapFree(GetProcessHeap(), 0, scratch);
    }
    restore_computational_state(fp);
    SetLastError(error);
    return hr;
}
void adjacency_report() {
    const auto mode = adjacency_mode.load(std::memory_order_acquire);
    if (mode == AdjacencyMode::Native && !adjacency_faulted.load(std::memory_order_acquire)) return;
    auto& c = adjacency_counters;
    const auto calls = c.calls.load(std::memory_order_relaxed);
    if (calls == adjacency_last_report_calls) return;
    adjacency_last_report_calls = calls;
    log("mesh_adjacency_metric cumulative=1 qpc=%llu mode=%s faulted=%u calls=%llu computed=%llu fallbacks=%llu faults=%llu faces=%llu vertices=%llu fast_ticks=%llu fast_max_ticks=%llu fast_us=%.3f fast_max_us=%.3f native_ticks=%llu native_max_ticks=%llu native_us=%.3f native_max_us=%.3f verify_meshes=%llu verify_equal=%llu verify_mismatched=%llu verify_entries=%llu verify_mismatch_entries=%llu verify_native_failures=%llu quantized=%llu unquantized=%llu welded_vertices=%llu multi_candidate_meshes=%llu normal_selected=%llu degenerate_faces=%llu fallback_input=%llu fallback_gate=%llu fallback_declaration=%llu fallback_size=%llu fallback_lock=%llu fallback_module=%llu fallback_math_table=%llu module_input=%llu module_index_range=%llu module_non_finite=%llu module_magnitude=%llu module_epsilon_neighbour=%llu module_allocation=%llu module_competing_normals=%llu fallback_competing_normals=%llu fallback_fp_domain=%llu verify_admitted=%llu verify_admitted_mismatched=%llu verify_refused_fp=%llu verify_refused_competing=%llu",
        tick(), adjacency_mode_name(mode), unsigned(adjacency_faulted.load(std::memory_order_relaxed)), calls,
        c.computed.load(), c.fallbacks.load(), c.faults.load(), c.faces.load(), c.vertices.load(), c.fast_ticks.load(),
        c.fast_max_ticks.load(), double(c.fast_ticks.load()) * 1e6 / clock_frequency,
        double(c.fast_max_ticks.load()) * 1e6 / clock_frequency, c.native_ticks.load(), c.native_max_ticks.load(),
        double(c.native_ticks.load()) * 1e6 / clock_frequency,
        double(c.native_max_ticks.load()) * 1e6 / clock_frequency, c.verify_meshes.load(), c.verify_equal.load(),
        c.verify_mismatched.load(), c.verify_entries.load(), c.verify_mismatch_entries.load(),
        c.verify_native_failures.load(), c.quantized.load(), c.unquantized.load(), c.welded_vertices.load(),
        c.multi_candidate_meshes.load(), c.normal_selected.load(), c.degenerate_faces.load(),
        c.fallback_reasons[0].load(), c.fallback_reasons[1].load(), c.fallback_reasons[2].load(),
        c.fallback_reasons[3].load(), c.fallback_reasons[4].load(), c.fallback_reasons[5].load(),
        c.fallback_reasons[6].load(), c.module_status[1].load(), c.module_status[2].load(), c.module_status[3].load(),
        c.module_status[4].load(), c.module_status[5].load(), c.module_status[6].load(), c.module_status[7].load(),
        c.fallback_reasons[7].load(), c.fallback_reasons[8].load(), c.verify_admitted.load(),
        c.verify_admitted_mismatched.load(), c.verify_refused_fp.load(), c.verify_refused_competing.load());
    if (!adjacency_fp_reported && c.fp_publication.load(std::memory_order_acquire) == 2) {
        adjacency_fp_reported = true;
        const auto& f = c.first_fp;
        log("mesh_adjacency_fp_first control=%08lx status=%08lx tag=%08lx mxcsr=%08lx compute_mxcsr=00001f80 restored=1",
            f.x87.control, f.x87.status, f.x87.tag, f.mxcsr);
    }
    for (unsigned i = 0; i < gate_reason_count; ++i) {
        auto& row = cache_gate_reasons[i];
        const auto count = row.count.load(std::memory_order_relaxed);
        if (count != cache_gate_last_counts[i]) {
            cache_gate_last_counts[i] = count;
            log("mesh_adjacency_gate cumulative=1 reason=%s count=%llu", gate_reason_name(i), count);
        }
    }
}
template <unsigned Index> HRESULT WINAPI mesh_adjacency(ID3DXMesh* mesh, FLOAT epsilon, DWORD* adjacency) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    const auto native = reinterpret_cast<AdjacencyFn>(mesh_tables[Index].slots[1].original);
    // The fast/verify services fall through to this table's own original,
    // carried by the thread-local.
    const auto mode = adjacency_mode.load(std::memory_order_acquire);
    adjacency_thread_original = native;
    const AdjacencyFn original = mode == AdjacencyMode::Fast     ? &adjacency_fast_service
                                 : mode == AdjacencyMode::Verify ? &adjacency_verify_service
                                                                 : native;
    Span span(Operation::MeshAdjacency);
    cpu.before_original();
    const HRESULT hr = original(mesh, epsilon, adjacency);
    cpu.after_original();
    const DWORD error = GetLastError();
    const auto end = tick();
    span.finish(end, error, FAILED(hr));
    return hr;
}
template <unsigned Index>
HRESULT WINAPI mesh_optimize(ID3DXMesh* mesh, DWORD flags, const DWORD* in, DWORD* out, DWORD* faces,
                             ID3DXBuffer** vertices) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    Span span(Operation::MeshOptimize);
    cpu.before_original();
    const HRESULT hr = reinterpret_cast<OptimizeFn>(mesh_tables[Index].slots[2].original)(mesh, flags, in, out, faces,
                                                                                          vertices);
    cpu.after_original();
    const DWORD error = GetLastError();
    const auto end = tick();
    span.finish(end, error, FAILED(hr));
    return hr;
}
#define MESH_THUNKS(i)                                                                                                 \
    {reinterpret_cast<PVOID>(mesh_point_reps<i>), reinterpret_cast<PVOID>(mesh_adjacency<i>),                          \
     reinterpret_cast<PVOID>(mesh_optimize<i>)}
PVOID mesh_replacements[mesh_table_limit][3] = {MESH_THUNKS(0), MESH_THUNKS(1), MESH_THUNKS(2), MESH_THUNKS(3),
                                                MESH_THUNKS(4), MESH_THUNKS(5), MESH_THUNKS(6), MESH_THUNKS(7)};
#undef MESH_THUNKS

// The counting/timing forwarders live in loading_trace_light.cpp (no SSE, no
// x87, no CpuCallBoundary); only the D3DX rows below, whose wrappers observe
// meshes and log, keep the full boundary.
namespace light = loading_trace::light;
HRESULT WINAPI effect(IDirect3DDevice9*, const void*, UINT, const D3DXMACRO*, ID3DXInclude*, DWORD, ID3DXEffectPool*,
                      ID3DXEffect**, ID3DXBuffer**);
HRESULT WINAPI texture(IDirect3DDevice9*, const void*, UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, DWORD, DWORD,
                       D3DCOLOR, D3DXIMAGE_INFO*, PALETTEENTRY*, IDirect3DTexture9**);
HRESULT WINAPI cube(IDirect3DDevice9*, const void*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, DWORD, DWORD, D3DCOLOR,
                    D3DXIMAGE_INFO*, PALETTEENTRY*, IDirect3DCubeTexture9**);
HRESULT WINAPI surface(IDirect3DSurface9*, const PALETTEENTRY*, const RECT*, const void*, UINT, const RECT*, DWORD,
                       D3DCOLOR, D3DXIMAGE_INFO*);
HRESULT WINAPI mesh_create(DWORD, DWORD, DWORD, const D3DVERTEXELEMENT9*, IDirect3DDevice9*, ID3DXMesh**);
HRESULT WINAPI mesh_clean(D3DXCLEANTYPE, ID3DXMesh*, const DWORD*, ID3DXMesh**, DWORD*, ID3DXBuffer**);
// cdecl and argument widths corroborated by target callsites; local zlib/libxml
// SDK prototypes supply semantics. Opaque pointers avoid importing struct layouts.
using GzOpenFn = void*(__cdecl*)(const char*, const char*);
using GzReadFn = int(__cdecl*)(void*, void*, unsigned);
using GzSeekFn = LONG(__cdecl*)(void*, LONG, int);
static_assert(sizeof(LONG) == 4 && sizeof(int) == 4 && sizeof(void*) == 4);
using GzTellFn = LONG(__cdecl*)(void*);
using GzGetcFn = int(__cdecl*)(void*);
using GzCloseFn = int(__cdecl*)(void*);
void* __cdecl gz_open(const char*, const char*);
int __cdecl gz_read(void*, void*, unsigned);
LONG __cdecl gz_seek(void*, LONG, int);
int __cdecl gz_getc(void*);
LONG __cdecl gz_tell(void*);
int __cdecl gz_close(void*);
// X3M_GZ_BUFFER=1: the gz hooks route through gz_buffer (set before patching, never
// cleared); its real functions are the traced wrappers with telemetry, else the originals.
bool gz_buffer_active = false;
// X3M_CRYPT_CACHE=1: the four CryptoAPI rows the context/key cache needs route
// through crypt_cache only after all four routes and the cache are initialized.
std::atomic<bool> crypt_cache_active{false};
// Only the six reviewed call returns inside the signature verifier qualify.
// The gate is established before patching; no backend DLL identity is assumed.
struct CryptSite {
    uintptr_t result;
    Operation operation;
};
#ifdef X3M_LOADING_TRACE_FIXTURE
uintptr_t crypt_image_base = 0x400000; // relocated synthetic PE only; setter absent in production
#else
constexpr uintptr_t crypt_image_base = 0x400000;
#endif
constexpr CryptSite crypt_sites[] = {{0x4cac3e, Operation::CryptAcquire}, {0x4cac57, Operation::CryptAcquire},
                                     {0x4cac85, Operation::CryptImport},  {0x4cae4d, Operation::CryptKeyDestroy},
                                     {0x4cae5a, Operation::CryptRelease}, {0x4cae73, Operation::CryptAcquire}};
bool crypt_site(const void* caller, Operation operation) {
    if (!crypt_cache_active.load(std::memory_order_acquire)) return false;
    for (const auto& site : crypt_sites)
        if (crypt_image_base + site.result - 0x400000 == reinterpret_cast<uintptr_t>(caller) &&
            site.operation == operation)
            return true;
    return false;
}
__attribute__((noinline)) BOOL WINAPI crypt_acquire(HCRYPTPROV*, LPCSTR, LPCSTR, DWORD, DWORD);
__attribute__((noinline)) BOOL WINAPI crypt_release(HCRYPTPROV, DWORD);
__attribute__((noinline)) BOOL WINAPI crypt_import(HCRYPTPROV, const BYTE*, DWORD, HCRYPTKEY, DWORD, HCRYPTKEY*);
__attribute__((noinline)) BOOL WINAPI crypt_key_destroy(HCRYPTKEY);
// clang-format off
Hook hooks[]={
    {"KERNEL32.dll","CreateFileA",reinterpret_cast<PVOID>(light::file_open)},
    {"KERNEL32.dll","ReadFile",reinterpret_cast<PVOID>(light::file_read)},
    {"KERNEL32.dll","SetFilePointer",reinterpret_cast<PVOID>(light::file_seek)},
    {"d3dx9_37.dll","D3DXCreateEffect",reinterpret_cast<PVOID>(effect)},
    {"d3dx9_37.dll","D3DXCreateTextureFromFileInMemoryEx",reinterpret_cast<PVOID>(texture)},
    {"d3dx9_37.dll","D3DXCreateCubeTextureFromFileInMemoryEx",reinterpret_cast<PVOID>(cube)},
    {"d3dx9_37.dll","D3DXLoadSurfaceFromFileInMemory",reinterpret_cast<PVOID>(surface)},
    {"USER32.dll","SetCursor",reinterpret_cast<PVOID>(light::cursor_set)},
    {"USER32.dll","SetCursorPos",reinterpret_cast<PVOID>(light::cursor_position)},
    {"zlib1.dll","gzopen",reinterpret_cast<PVOID>(gz_open)},
    {"zlib1.dll","gzread",reinterpret_cast<PVOID>(gz_read)},
    {"zlib1.dll","gzseek",reinterpret_cast<PVOID>(gz_seek)},
    {"zlib1.dll","inflate",reinterpret_cast<PVOID>(light::inflate_stream)},
    {"libxml2.dll","xmlReadMemory",reinterpret_cast<PVOID>(light::xml_read)},
    {"d3dx9_37.dll","D3DXCreateMesh",reinterpret_cast<PVOID>(mesh_create)},
    {"d3dx9_37.dll","D3DXCleanMesh",reinterpret_cast<PVOID>(mesh_clean)},
    {"KERNEL32.dll","FindFirstFileA",reinterpret_cast<PVOID>(light::find_first)},
    {"KERNEL32.dll","FindNextFileA",reinterpret_cast<PVOID>(light::find_next)},
    {"KERNEL32.dll","FindClose",reinterpret_cast<PVOID>(light::find_close)},
    {"zlib1.dll","gzgetc",reinterpret_cast<PVOID>(gz_getc)},
    {"zlib1.dll","gztell",reinterpret_cast<PVOID>(gz_tell)},
    {"zlib1.dll","gzclose",reinterpret_cast<PVOID>(gz_close)},
    {"zlib1.dll","gzwrite",reinterpret_cast<PVOID>(light::gz_write)},
    // Probe batch 2 rows (patched only with X3M_LOADING_PROBES=1).
    {"zlib1.dll","inflateInit2_",reinterpret_cast<PVOID>(light::inflate_init2)},
    {"zlib1.dll","inflateEnd",reinterpret_cast<PVOID>(light::inflate_end)},
    {"ADVAPI32.dll","CryptAcquireContextA",reinterpret_cast<PVOID>(crypt_acquire)},
    {"ADVAPI32.dll","CryptReleaseContext",reinterpret_cast<PVOID>(crypt_release)},
    {"ADVAPI32.dll","CryptImportKey",reinterpret_cast<PVOID>(crypt_import)},
    {"ADVAPI32.dll","CryptCreateHash",reinterpret_cast<PVOID>(light::crypt_create_hash)},
    {"ADVAPI32.dll","CryptHashData",reinterpret_cast<PVOID>(light::crypt_hash_data)},
    {"ADVAPI32.dll","CryptVerifySignatureA",reinterpret_cast<PVOID>(light::crypt_verify_signature)},
    {"ADVAPI32.dll","CryptGetHashParam",reinterpret_cast<PVOID>(light::crypt_get_hash_param)},
    {"ADVAPI32.dll","CryptDestroyHash",reinterpret_cast<PVOID>(light::crypt_destroy_hash)},
    {"ADVAPI32.dll","CryptDestroyKey",reinterpret_cast<PVOID>(crypt_key_destroy)},
    {"KERNEL32.dll","CreateDirectoryA",reinterpret_cast<PVOID>(light::create_directory)},
    {"KERNEL32.dll","DeleteFileA",reinterpret_cast<PVOID>(light::delete_file)},
    {"KERNEL32.dll","MoveFileA",reinterpret_cast<PVOID>(light::move_file)},
    {"KERNEL32.dll","MoveFileExA",reinterpret_cast<PVOID>(light::move_file_ex)},
    {"KERNEL32.dll","WriteFile",reinterpret_cast<PVOID>(light::write_file)},
    {"KERNEL32.dll","GetFileType",reinterpret_cast<PVOID>(light::get_file_type)},
    {"KERNEL32.dll","CloseHandle",reinterpret_cast<PVOID>(light::close_handle)}
};
// clang-format on
bool probe_row(unsigned index) {
    return index >= probe_row_begin && index < probe_row_end;
}
// Rows the read-ahead buffer needs; patched alone when telemetry is off.
bool gz_buffer_row(const Hook& hook) {
    return !std::strcmp(hook.dll, "zlib1.dll") && std::strcmp(hook.name, "inflate") &&
           std::strcmp(hook.name, "gzwrite");
}
// Rows the CryptoAPI context/key cache needs; patched alone when telemetry is off
// and together with the probe batch 2 rows when it is on.
bool crypt_cache_row(const Hook& hook) {
    return !std::strcmp(hook.dll, "ADVAPI32.dll") &&
           (!std::strcmp(hook.name, "CryptAcquireContextA") || !std::strcmp(hook.name, "CryptReleaseContext") ||
            !std::strcmp(hook.name, "CryptImportKey") || !std::strcmp(hook.name, "CryptDestroyKey"));
}
constexpr unsigned import_count = sizeof hooks / sizeof *hooks;
static_assert(import_count == static_cast<unsigned>(Operation::MeshPointReps));
const char* method_names[] = {"ID3DXMesh::ConvertPointRepsToAdjacency", "ID3DXMesh::GenerateAdjacency",
                              "ID3DXMesh::OptimizeInplace"};
const char* operation_name(unsigned index) {
    return index < import_count ? hooks[index].name : method_names[index - import_count];
}
static_assert(std::is_same_v<decltype(&light::file_open), decltype(&CreateFileA)>);
static_assert(std::is_same_v<decltype(&light::file_read), decltype(&ReadFile)>);
static_assert(std::is_same_v<decltype(&light::file_seek), decltype(&SetFilePointer)>);
static_assert(std::is_same_v<decltype(&effect), decltype(&D3DXCreateEffect)>);
static_assert(std::is_same_v<decltype(&texture), decltype(&D3DXCreateTextureFromFileInMemoryEx)>);
static_assert(std::is_same_v<decltype(&cube), decltype(&D3DXCreateCubeTextureFromFileInMemoryEx)>);
static_assert(std::is_same_v<decltype(&surface), decltype(&D3DXLoadSurfaceFromFileInMemory)>);
static_assert(std::is_same_v<decltype(&mesh_create), decltype(&D3DXCreateMesh)>);
static_assert(std::is_same_v<decltype(&mesh_clean), decltype(&D3DXCleanMesh)>);
static_assert(std::is_same_v<decltype(&light::cursor_set), decltype(&SetCursor)>);
static_assert(std::is_same_v<decltype(&light::cursor_position), decltype(&SetCursorPos)>);
static_assert(std::is_same_v<decltype(&light::find_first), decltype(&FindFirstFileA)>);
static_assert(std::is_same_v<decltype(&light::find_next), decltype(&FindNextFileA)>);
static_assert(std::is_same_v<decltype(&light::find_close), decltype(&FindClose)>);
static_assert(std::is_same_v<decltype(&light::crypt_acquire_context), decltype(&CryptAcquireContextA)>);
static_assert(std::is_same_v<decltype(&crypt_acquire), decltype(&CryptAcquireContextA)>);
static_assert(std::is_same_v<decltype(&crypt_release), decltype(&CryptReleaseContext)>);
static_assert(std::is_same_v<decltype(&crypt_import), decltype(&CryptImportKey)>);
static_assert(std::is_same_v<decltype(&crypt_key_destroy), decltype(&CryptDestroyKey)>);
static_assert(std::is_same_v<decltype(&light::crypt_verify_signature), decltype(&CryptVerifySignatureA)>);
static_assert(std::is_same_v<decltype(&light::create_directory), decltype(&CreateDirectoryA)>);
static_assert(std::is_same_v<decltype(&light::move_file_ex), decltype(&MoveFileExA)>);
static_assert(std::is_same_v<decltype(&light::write_file), decltype(&WriteFile)>);
static_assert(std::is_same_v<decltype(&light::close_handle), decltype(&CloseHandle)>);
template <typename T> T original(Operation op) {
    return reinterpret_cast<T>(hooks[static_cast<unsigned>(op)].original);
}

HRESULT WINAPI effect(IDirect3DDevice9* d, const void* data, UINT size, const D3DXMACRO* defines, ID3DXInclude* include,
                      DWORD flags, ID3DXEffectPool* pool, ID3DXEffect** out, ID3DXBuffer** errors) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    Span span(Operation::Effect);
    cpu.before_original();
    HRESULT result = original<decltype(&D3DXCreateEffect)>(span.op)(d, data, size, defines, include, flags, pool, out,
                                                                    errors);
    cpu.after_original();
    const DWORD error = GetLastError();
    const auto end = tick();
    span.finish(end, error, FAILED(result), size);
    return result;
}
HRESULT WINAPI texture(IDirect3DDevice9* d, const void* data, UINT size, UINT width, UINT height, UINT levels,
                       DWORD usage, D3DFORMAT format, D3DPOOL pool, DWORD filter, DWORD mipfilter, D3DCOLOR key,
                       D3DXIMAGE_INFO* info, PALETTEENTRY* palette, IDirect3DTexture9** out) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    Span span(Operation::Texture);
    cpu.before_original();
    HRESULT result = original<decltype(&D3DXCreateTextureFromFileInMemoryEx)>(span.op)(
        d, data, size, width, height, levels, usage, format, pool, filter, mipfilter, key, info, palette, out);
    cpu.after_original();
    const DWORD error = GetLastError();
    const auto end = tick();
    span.finish(end, error, FAILED(result), size);
    return result;
}
HRESULT WINAPI cube(IDirect3DDevice9* d, const void* data, UINT size, UINT edge, UINT levels, DWORD usage,
                    D3DFORMAT format, D3DPOOL pool, DWORD filter, DWORD mipfilter, D3DCOLOR key, D3DXIMAGE_INFO* info,
                    PALETTEENTRY* palette, IDirect3DCubeTexture9** out) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    Span span(Operation::CubeTexture);
    cpu.before_original();
    HRESULT result = original<decltype(&D3DXCreateCubeTextureFromFileInMemoryEx)>(span.op)(
        d, data, size, edge, levels, usage, format, pool, filter, mipfilter, key, info, palette, out);
    cpu.after_original();
    const DWORD error = GetLastError();
    const auto end = tick();
    span.finish(end, error, FAILED(result), size);
    return result;
}
HRESULT WINAPI surface(IDirect3DSurface9* dest, const PALETTEENTRY* palette, const RECT* destrect, const void* data,
                       UINT size, const RECT* srcrect, DWORD filter, D3DCOLOR key, D3DXIMAGE_INFO* info) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    Span span(Operation::Surface);
    cpu.before_original();
    HRESULT result = original<decltype(&D3DXLoadSurfaceFromFileInMemory)>(span.op)(dest, palette, destrect, data, size,
                                                                                   srcrect, filter, key, info);
    cpu.after_original();
    const DWORD error = GetLastError();
    const auto end = tick();
    span.finish(end, error, FAILED(result), size);
    return result;
}
HRESULT WINAPI mesh_create(DWORD faces, DWORD vertices, DWORD options, const D3DVERTEXELEMENT9* declaration,
                           IDirect3DDevice9* device, ID3DXMesh** out) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    Span span(Operation::MeshCreate);
    cpu.before_original();
    HRESULT result = original<decltype(&D3DXCreateMesh)>(span.op)(faces, vertices, options, declaration, device, out);
    cpu.after_original();
    const DWORD error = GetLastError();
    const auto end = tick();
    if (SUCCEEDED(result) && out && *out) observe_mesh(*out);
    span.finish(end, error, FAILED(result));
    return result;
}
HRESULT WINAPI mesh_clean(D3DXCLEANTYPE type, ID3DXMesh* input, const DWORD* adjacency_in, ID3DXMesh** output,
                          DWORD* adjacency_out, ID3DXBuffer** errors) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    Span span(Operation::MeshClean);
    cpu.before_original();
    HRESULT result = original<decltype(&D3DXCleanMesh)>(span.op)(type, input, adjacency_in, output, adjacency_out,
                                                                 errors);
    cpu.after_original();
    const DWORD error = GetLastError();
    const auto end = tick();
    if (SUCCEEDED(result) && output && *output) observe_mesh(*output);
    span.finish(end, error, FAILED(result));
    return result;
}

// Import-slot entry points. With the buffer off these are the light traced
// wrappers; with it on, the buffer's fast path runs with no span of its own.
void* __cdecl gz_open(const char* path, const char* mode) {
    return gz_buffer_active ? gz_buffer::open(path, mode) : light::gz_open_traced(path, mode);
}
int __cdecl gz_read(void* file, void* data, unsigned size) {
    return gz_buffer_active ? gz_buffer::read(file, data, size) : light::gz_read_traced(file, data, size);
}
LONG __cdecl gz_seek(void* file, LONG offset, int whence) {
    return gz_buffer_active ? gz_buffer::seek(file, offset, whence) : light::gz_seek_traced(file, offset, whence);
}
int __cdecl gz_getc(void* file) {
    return gz_buffer_active ? gz_buffer::getc(file) : light::gz_getc_traced(file);
}
LONG __cdecl gz_tell(void* file) {
    return gz_buffer_active ? gz_buffer::tell(file) : light::gz_tell_traced(file);
}
int __cdecl gz_close(void* file) {
    return gz_buffer_active ? gz_buffer::close(file) : light::gz_close_traced(file);
}
// CryptoAPI slots: with the cache off the light traced wrappers, with it on the
// cache (whose real calls are those wrappers when telemetry is on, else the originals).
__attribute__((noinline)) BOOL WINAPI crypt_acquire(HCRYPTPROV* out, LPCSTR container, LPCSTR provider, DWORD type,
                                                    DWORD flags) {
    return crypt_site(__builtin_return_address(0), Operation::CryptAcquire)
               ? crypt_cache::acquire(out, container, provider, type, flags)
               : light::crypt_acquire_context(out, container, provider, type, flags);
}
__attribute__((noinline)) BOOL WINAPI crypt_release(HCRYPTPROV provider, DWORD flags) {
    return crypt_site(__builtin_return_address(0), Operation::CryptRelease)
               ? crypt_cache::release(provider, flags)
               : light::crypt_release_context(provider, flags);
}
__attribute__((noinline)) BOOL WINAPI crypt_import(HCRYPTPROV provider, const BYTE* data, DWORD length, HCRYPTKEY key,
                                                   DWORD flags, HCRYPTKEY* out) {
    return crypt_site(__builtin_return_address(0), Operation::CryptImport)
               ? crypt_cache::import_key(provider, data, length, key, flags, out)
               : light::crypt_import_key(provider, data, length, key, flags, out);
}
__attribute__((noinline)) BOOL WINAPI crypt_key_destroy(HCRYPTKEY key) {
    return crypt_site(__builtin_return_address(0), Operation::CryptKeyDestroy) ? crypt_cache::destroy_key(key)
                                                                               : light::crypt_destroy_key(key);
}

bool requested() {
    return log_tier::telemetry();
} // X3M_TELEMETRY=1 or a logging group (log_tiers.h)
}
bool probes_requested() {
    return log_tier::debug_flag(L"X3M_LOADING_PROBES");
} // X3M_LOADING_PROBES=1 or X3M_DEBUG=1
namespace {
bool readable(const void* address, size_t bytes, HMODULE owner = nullptr, bool executable = false);

// Only validated module memory is traversed. Reject unterminated names, missing
// OriginalFirstThunk and RVAs that escape SizeOfImage; never infer names from IAT.
struct Image {
    unsigned char* base;
    DWORD size;
    bool range(DWORD rva, size_t length) const { return rva < size && length <= size - rva; }
    template <typename T> T* at(DWORD rva, size_t n = 1) const {
        if (n > size / sizeof(T) || !range(rva, sizeof(T) * n) ||
            !readable(base + rva, sizeof(T) * n, reinterpret_cast<HMODULE>(base)))
            return nullptr;
        return reinterpret_cast<T*>(base + rva);
    }
    const char* string(DWORD rva) const {
        if (!range(rva, 1)) return nullptr;
        const char* start = reinterpret_cast<char*>(base + rva);
        DWORD offset = rva;
        while (offset < size) {
            MEMORY_BASIC_INFORMATION info{};
            const auto cursor = base + offset;
            if (!readable(cursor, 1, reinterpret_cast<HMODULE>(base)) ||
                VirtualQuery(cursor, &info, sizeof info) != sizeof info)
                return nullptr;
            const size_t count = std::min<size_t>(
                size - offset, info.RegionSize - (cursor - static_cast<unsigned char*>(info.BaseAddress)));
            if (std::memchr(cursor, 0, count)) return start;
            offset += static_cast<DWORD>(count);
        }
        return nullptr;
    }
};
// Serializes page-permission changes, not API calls. A failed restore retains
// the original page protection until recovery; PAGE_READWRITE is never mistaken
// for a new baseline when a later slot on the same page is patched.
struct ProtectionDebt {
    void* page = nullptr;
    DWORD protection = 0;
};
ProtectionDebt protection_pages[64]; // at most 16 IAT + 8*3 mesh slot pages
SRWLOCK protection_lock = SRWLOCK_INIT;
void* page_of(const void* address) {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(address) & ~(uintptr_t(info.dwPageSize) - 1));
}
bool restore_protection(void* address, DWORD protection) {
#ifdef X3M_LOADING_TRACE_FIXTURE
    if (fail_protection_restore) {
        --fail_protection_restore;
        SetLastError(ERROR_ACCESS_DENIED);
        return false;
    }
#endif
    DWORD ignored = 0;
    return VirtualProtect(address, sizeof(PVOID), protection, &ignored) != 0;
}
void update_debt_count() {
    unsigned n = 0;
    for (const auto& debt : protection_pages) n += debt.page != nullptr;
    protection_debts.store(n, std::memory_order_release);
}
bool has_protection_debt(const void* address) {
    AcquireSRWLockShared(&protection_lock);
    const auto page = page_of(address);
    bool found = false;
    for (const auto& debt : protection_pages) found |= debt.page == page;
    ReleaseSRWLockShared(&protection_lock);
    return found;
}
void recover_protections() {
    AcquireSRWLockExclusive(&protection_lock);
    for (auto& debt : protection_pages)
        if (debt.page && restore_protection(debt.page, debt.protection)) debt = {};
    update_debt_count();
    ReleaseSRWLockExclusive(&protection_lock);
}
bool patch(Hook& hook, bool restore) {
    if (!hook.slot) return false;
    AcquireSRWLockExclusive(&protection_lock);
    const auto page = page_of(hook.slot);
    ProtectionDebt* record = nullptr;
    for (auto& debt : protection_pages)
        if (debt.page == page) {
            record = &debt;
            break;
        }
    if (!record)
        for (auto& debt : protection_pages)
            if (!debt.page) {
                record = &debt;
                break;
            }
    if (!record) {
        ReleaseSRWLockExclusive(&protection_lock);
        return false;
    }
    DWORD observed = 0;
    if (!VirtualProtect(hook.slot, sizeof(PVOID), PAGE_READWRITE, &observed)) {
        ReleaseSRWLockExclusive(&protection_lock);
        return false;
    }
    if (!record->page) *record = {page, observed};
    const PVOID expected = restore ? hook.replacement : hook.original;
    const PVOID desired = restore ? hook.original : hook.replacement;
    const bool swapped = InterlockedCompareExchangePointer(hook.slot, desired, expected) == expected;
    bool protected_again = restore_protection(hook.slot, record->protection), rolled_back = false;
    if (!protected_again && !restore && swapped) {
        InterlockedCompareExchangePointer(hook.slot, hook.original, hook.replacement);
        rolled_back = true;
        protected_again = restore_protection(hook.slot, record->protection);
    }
    if (protected_again) *record = {};
    update_debt_count();
    ReleaseSRWLockExclusive(&protection_lock);
    return swapped && protected_again && !rolled_back;
}
bool readable(const void* address, size_t bytes, HMODULE owner, bool executable) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || VirtualQuery(address, &info, sizeof info) != sizeof info || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || (owner && info.AllocationBase != owner))
        return false;
    const auto start = reinterpret_cast<uintptr_t>(address), base = reinterpret_cast<uintptr_t>(info.BaseAddress);
    if (start < base || bytes > info.RegionSize - (start - base)) return false;
    const DWORD protection = info.Protect & 0xff;
    if (executable)
        return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
               protection == PAGE_EXECUTE_WRITECOPY;
    return protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
           protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
           protection == PAGE_EXECUTE_WRITECOPY;
}
bool verified_dynamic_options(DWORD options) {
    // Bounded actual mesh variants; physical public descriptors must also agree.
    return options == 0x990u || options == 0x991u || options == 0x18990u || options == 0x18991u;
}
template <class Buffer>
bool buffer_contract(Buffer* application, uint64_t required_bytes, D3DFORMAT format, DWORD options) {
    GateDetail detail{};
    detail.scope = std::is_same_v<Buffer, IDirect3DVertexBuffer9> ? 1 : 2;
    detail.options = options;
    detail.required_bytes = required_bytes;
    if (!application) return reject_gate(GateReason::BufferMissing, detail);
    // The typed reference returned by the verified mesh owns this interface.
    // Public descriptor and READONLY Lock/Unlock contracts require no backend
    // vtable, native object offset, implementation import or DLL fingerprint.
    ownership::BufferContentView view{};
    detail.status = ownership::get_buffer_content_view(application, &view);
    detail.known = view.known;
    detail.pending = view.pending_locks;
    if (SUCCEEDED(detail.status)) {
        detail.status = view.status;
        if (FAILED(view.status) || (view.requested && (!view.known || view.ambiguous || view.pending_locks)))
            return reject_gate(GateReason::Tracker, detail);
    } else if (detail.status != E_INVALIDARG)
        return reject_gate(GateReason::Tracker, detail);
    std::conditional_t<std::is_same_v<Buffer, IDirect3DVertexBuffer9>, D3DVERTEXBUFFER_DESC, D3DINDEXBUFFER_DESC>
        desc{};
    detail.status = application->GetDesc(&desc);
    detail.pool = desc.Pool;
    detail.usage = desc.Usage;
    detail.format = desc.Format;
    detail.size_bytes = desc.Size;
    if (FAILED(detail.status)) return reject_gate(GateReason::DescriptorCall, detail);
    if (desc.Pool != D3DPOOL_SYSTEMMEM) return reject_gate(GateReason::DescriptorPool, detail);
    const auto expected_type = std::is_same_v<Buffer, IDirect3DVertexBuffer9> ? D3DRTYPE_VERTEXBUFFER
                                                                              : D3DRTYPE_INDEXBUFFER;
    if (desc.Type != expected_type || desc.Format != format) return reject_gate(GateReason::DescriptorFormat, detail);
    const DWORD software_option = std::is_same_v<Buffer, IDirect3DVertexBuffer9> ? D3DXMESH_VB_SOFTWAREPROCESSING
                                                                                 : D3DXMESH_IB_SOFTWAREPROCESSING;
    const DWORD expected_usage = ((options & D3DXMESH_DYNAMIC) ? D3DUSAGE_DYNAMIC : 0) |
                                 ((options & software_option) ? D3DUSAGE_SOFTWAREPROCESSING : 0);
    if (desc.Usage != expected_usage) return reject_gate(GateReason::DescriptorUsage, detail);
    if (!required_bytes || required_bytes > desc.Size) return reject_gate(GateReason::DescriptorSize, detail);
    return true;
}
bool cache_buffer_contract(ID3DXMesh* mesh) {
    GateDetail detail{};
    if (!readable(mesh, sizeof(PVOID))) return reject_gate(GateReason::MeshObject, detail);
    auto table = *reinterpret_cast<PVOID**>(mesh);
    if (!readable(table, 19 * sizeof(PVOID))) return reject_gate(GateReason::MeshTable, detail);
    const MeshTable* recorded = nullptr;
    for (const auto& row : mesh_tables)
        if (row.table == table) {
            recorded = &row;
            break;
        }
    if (!recorded) return reject_gate(GateReason::MeshTable, detail);
    for (unsigned i = 0; i < 11; ++i) {
        detail.slot = mesh_contract_slots[i];
        detail.actual_entry = DWORD(reinterpret_cast<uintptr_t>(table[detail.slot]));
        detail.expected_entry = DWORD(reinterpret_cast<uintptr_t>(recorded->contract[i]));
        if (table[detail.slot] != recorded->contract[i]) return reject_gate(GateReason::MeshMethod, detail);
    }
    const DWORD options = mesh->GetOptions();
    detail.options = options;
    if ((options & D3DXMESH_SYSTEMMEM) != D3DXMESH_SYSTEMMEM) return reject_gate(GateReason::MeshPool, detail);
    constexpr DWORD supported_options = D3DXMESH_SYSTEMMEM | D3DXMESH_32BIT | D3DXMESH_DYNAMIC |
                                        D3DXMESH_SOFTWAREPROCESSING;
    if (options & ~supported_options) return reject_gate(GateReason::MeshOptions, detail);
    if ((options & D3DXMESH_DYNAMIC) && !verified_dynamic_options(options))
        return reject_gate(GateReason::MeshOptions, detail);
    IDirect3DVertexBuffer9* vb = nullptr;
    IDirect3DIndexBuffer9* ib = nullptr;
    detail.status = mesh->GetVertexBuffer(&vb);
    bool okay = SUCCEEDED(detail.status) && vb;
    if (!okay) reject_gate(GateReason::VertexAcquire, detail);
    if (okay) {
        detail.status = mesh->GetIndexBuffer(&ib);
        okay = SUCCEEDED(detail.status) && ib;
        if (!okay) reject_gate(GateReason::IndexAcquire, detail);
    }
    if (okay) {
        const uint64_t vertices = uint64_t(mesh->GetNumVertices()) * mesh->GetNumBytesPerVertex();
        const uint64_t indices = uint64_t(mesh->GetNumFaces()) * 3 * ((options & D3DXMESH_32BIT) ? 4 : 2);
        okay = buffer_contract(vb, vertices, D3DFMT_VERTEXDATA, options) &&
               buffer_contract(ib, indices, (options & D3DXMESH_32BIT) ? D3DFMT_INDEX32 : D3DFMT_INDEX16, options);
    }
    if (ib) ib->Release();
    if (vb) vb->Release();
    return okay;
}
bool pin_address(const void* address, bool executable) {
    if (!readable(address, 1, nullptr, executable)) return false;
    HMODULE pinned = nullptr;
    return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCSTR>(address), &pinned) != FALSE;
}
bool pin_mesh_module() {
    if (mesh_module_checked) return mesh_module != nullptr;
    mesh_module_checked = true;
    const auto create = hooks[static_cast<unsigned>(Operation::MeshCreate)].original;
    if (!create || !readable(create, 1, nullptr, true) ||
        !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCSTR>(create), &mesh_module))
        return false;
    log("mesh_trace module_pinned=1 version_gate=0 scope=shared_public_com_vtables table_limit=%u objects_retained=0",
        mesh_table_limit);
    return true;
}
void observe_mesh(ID3DXMesh* mesh) {
    if (!mesh_observation_enabled.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&mesh_lock);
    if (!mesh_observation_enabled.load(std::memory_order_relaxed) || !pin_mesh_module() ||
        !readable(mesh, sizeof(PVOID))) {
        ReleaseSRWLockExclusive(&mesh_lock);
        return;
    }
    auto table = *reinterpret_cast<PVOID**>(mesh);
    if (reinterpret_cast<uintptr_t>(table) % alignof(PVOID) || !readable(table, 29 * sizeof(PVOID))) {
        ReleaseSRWLockExclusive(&mesh_lock);
        return;
    }
    unsigned index = 0;
    while (index < mesh_table_limit && mesh_tables[index].table && mesh_tables[index].table != table) ++index;
    if (index == mesh_table_limit) {
        ReleaseSRWLockExclusive(&mesh_lock);
        return;
    }
    auto& row = mesh_tables[index];
    if (!row.table) {
        // Once per distinct shared table: saved table/call targets must outlive
        // trampoline chains. Pin actual owning modules, without placement/RVAs.
        if (!pin_address(table, false)) {
            ReleaseSRWLockExclusive(&mesh_lock);
            return;
        }
        for (unsigned slot : {0u, 1u, 2u, 20u, 22u, 27u}) {
            bool ours = false;
            for (const auto& prior : mesh_tables)
                for (const auto& h : prior.slots) ours |= h.replacement && table[slot] == h.replacement;
            if (ours || !pin_address(table[slot], true)) {
                ReleaseSRWLockExclusive(&mesh_lock);
                return;
            }
        }
        for (unsigned slot : mesh_contract_slots)
            if (!pin_address(table[slot], true)) {
                ReleaseSRWLockExclusive(&mesh_lock);
                return;
            }
        row.table = table;
        for (unsigned i = 0; i < 11; ++i) row.contract[i] = table[mesh_contract_slots[i]];
        for (unsigned i = 0; i < 3; ++i)
            row.slots[i] = {"d3dx9_37.dll", method_names[i], mesh_replacements[index][i], table[mesh_slot_indices[i]],
                            &table[mesh_slot_indices[i]]};
    }
    bool already = true;
    for (const auto& h : row.slots) already &= *h.slot == h.replacement;
    if (already) {
        ReleaseSRWLockExclusive(&mesh_lock);
        return;
    }
    bool okay = true;
    for (unsigned i = 0; i < 3 && okay; ++i) {
#ifdef X3M_LOADING_TRACE_FIXTURE
        if (fail_mesh_patch == i + 1) {
            fail_mesh_patch = 0;
            okay = false;
            break;
        }
#endif
        auto& h = row.slots[i];
        okay = *h.slot == h.replacement || patch(h, false);
    }
    if (!okay)
        for (auto& h : row.slots)
            if (*h.slot == h.replacement) patch(h, true);
    unsigned owned = 0;
    for (const auto& h : row.slots) owned += *h.slot == h.replacement;
    unsigned total_owned = 0;
    for (const auto& r : mesh_tables)
        if (r.table)
            for (const auto& h : r.slots) total_owned += *h.slot == h.replacement;
    mesh_owned_slots.store(total_owned, std::memory_order_release);
    if (row.last_result != int(okay) || row.last_owned != int(owned)) {
        log("mesh_hook table=%u installed=%u owned_slots=%u scope=shared_native_vtable", index, okay, owned);
        row.last_result = int(okay);
        row.last_owned = int(owned);
    }
    ReleaseSRWLockExclusive(&mesh_lock);
}
void restore_mesh_hooks() {
    mesh_observation_enabled.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&mesh_lock);
    for (unsigned i = 0; i < mesh_table_limit; ++i)
        if (mesh_tables[i].table) {
            unsigned restored = 0, foreign = 0, remaining = 0;
            for (auto& h : mesh_tables[i].slots) {
                if (*h.slot == h.replacement) {
                    restored += patch(h, true);
                    remaining += *h.slot == h.replacement;
                } else
                    foreign += *h.slot != h.original;
            }
            log("mesh_hook table=%u restored_slots=%u foreign_slots=%u remaining_owned_slots=%u", i, restored, foreign,
                remaining);
        }
    unsigned total_owned = 0;
    for (const auto& row : mesh_tables)
        if (row.table)
            for (const auto& h : row.slots) total_owned += *h.slot == h.replacement;
    mesh_owned_slots.store(total_owned, std::memory_order_release);
    // Records/originals are intentionally kept: foreign interceptors may chain
    // to our trampoline after teardown. The native module remains pinned.
    ReleaseSRWLockExclusive(&mesh_lock);
}
bool crypt_game_contract(HMODULE target) {
    if (reinterpret_cast<uintptr_t>(target) != crypt_image_base) return false;
    // Short hook-site/argument sequences derived from the reviewed executable.
    struct Bytes {
        uintptr_t address;
        const unsigned char* bytes;
        size_t size;
    };
    static const unsigned char create_args[] = {0x6a, 0x08, 0x6a, 0x01, 0x68, 0x88, 0x38, 0x56, 0x00, 0x68,
                                                0xb4, 0x38, 0x56, 0x00, 0x8d, 0x4c, 0x24, 0x28, 0x51};
    static const unsigned char release_args[] = {0x8b, 0x54, 0x24, 0x18, 0x6a, 0x00, 0x52};
    const Bytes signatures[] = {{0x4cac3e, create_args, sizeof create_args},
                                {0x4cae4d, release_args, sizeof release_args}};
    for (const auto& sig : signatures) {
        const auto address = crypt_image_base + sig.address - 0x400000;
        if (!readable(reinterpret_cast<void*>(address), sig.size, target, true) ||
            std::memcmp(reinterpret_cast<void*>(address), sig.bytes, sig.size))
            return false;
    }
    for (const auto& site : crypt_sites) {
        const auto* instruction = reinterpret_cast<const unsigned char*>(crypt_image_base + site.result - 0x400000 - 6);
        if (!readable(instruction, 6, target, true) || instruction[0] != 0xff || instruction[1] != 0x15) return false;
        uint32_t slot = 0;
        std::memcpy(&slot, instruction + 2, 4);
        const auto& hook = hooks[unsigned(site.operation)];
        if (slot != reinterpret_cast<uintptr_t>(hook.slot) || !hook.original) return false;
    }
    return true;
}
// All four lifetime routes must be owned before any request can reach the cache.
// Failed rollback may leave a forwarding shim, but never an active cache.
#ifdef X3M_LOADING_TRACE_FIXTURE
unsigned fail_crypt_patch_step = 0;
void (*crypt_patch_observer)() = nullptr;
#endif
bool install_crypt_group(const crypt_cache::Originals& real, HMODULE target) {
    if (!crypt_game_contract(target)) return false;
    bool complete = true;
#ifdef X3M_LOADING_TRACE_FIXTURE
    unsigned step = 0;
#endif
    for (auto& hook : hooks)
        if (crypt_cache_row(hook)) {
#ifdef X3M_LOADING_TRACE_FIXTURE
            if (++step == fail_crypt_patch_step) {
                complete = false;
                break;
            }
#endif
            if (!hook.slot || !patch(hook, false)) {
                complete = false;
                break;
            }
#ifdef X3M_LOADING_TRACE_FIXTURE
            if (crypt_patch_observer) crypt_patch_observer();
#endif
        }
    if (complete)
        for (const auto& hook : hooks)
            if (crypt_cache_row(hook))
                complete = complete && hook.slot && *hook.slot == hook.replacement && !has_protection_debt(hook.slot);
    if (complete) complete = crypt_cache::initialize(real);
    if (!complete) {
        for (auto& hook : hooks)
            if (crypt_cache_row(hook) && hook.slot && *hook.slot == hook.replacement) patch(hook, true);
        return false;
    }
    crypt_cache_active.store(true, std::memory_order_release);
    return true;
}
// The two D3DX import rows whose results are observed; observe_mesh patches the
// shared mesh vtables (GenerateAdjacency among them) from these.
bool mesh_row(const Hook& hook) {
    return &hook == &hooks[static_cast<unsigned>(Operation::MeshCreate)] ||
           &hook == &hooks[static_cast<unsigned>(Operation::MeshClean)];
}
AdjacencyMode adjacency_requested_mode() {
    wchar_t setting[adjacency_fast::mode_capacity]{};
    return adjacency_fast::parse_mode(setting,
                                      x3m::config::get(L"X3M_MESH_ADJACENCY", setting, adjacency_fast::mode_capacity));
}
// One mesh_adjacency_config row per process: enabled=1 when a non-native mode is
// armed and at least one mesh import row routes meshes to the vtable hooks.
bool adjacency_config_logged = false;
void log_adjacency_config(AdjacencyMode requested_mode, bool trace) {
    if (adjacency_config_logged) return;
    adjacency_config_logged = true;
    bool rows = false;
    for (const auto& hook : hooks) rows |= mesh_row(hook) && hook.slot && *hook.slot == hook.replacement;
    const bool enabled = rows && mesh_observation_enabled.load(std::memory_order_acquire) &&
                         adjacency_mode.load(std::memory_order_acquire) != AdjacencyMode::Native;
    log("mesh_adjacency_config requested=%s enabled=%u telemetry=%u", adjacency_fast::mode_name(requested_mode),
        unsigned(enabled), unsigned(trace));
}
bool install(HMODULE target) {
    if (installed.load()) return true;
    if (installation_started) {
        log("loading_trace disabled=reinitialization_not_supported");
        return false;
    }
    const bool trace = requested(), buffer = gz_buffer::requested(), crypt = crypt_cache::requested();
    // X3M_MESH_ADJACENCY=fast arms without telemetry: only the two mesh import
    // rows are patched then, and no metric row is written (report() runs from
    // the telemetry summary only). With telemetry the full set is patched below.
    const AdjacencyMode adjacency_request = adjacency_requested_mode(),
                        adjacency_armed = adjacency_fast::armed_mode(adjacency_request, trace);
    const bool adjacency = !trace && adjacency_armed != AdjacencyMode::Native;
    if (!trace && !buffer && !crypt && !adjacency) return false;
    auto base = reinterpret_cast<unsigned char*>(target);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (!readable(dos, sizeof *dos, target) || dos->e_magic != IMAGE_DOS_SIGNATURE ||
        dos->e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER)) || dos->e_lfanew > 0x100000)
        return false;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (!readable(nt, sizeof *nt, target) || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT ||
        nt->OptionalHeader.SizeOfImage < sizeof(IMAGE_DOS_HEADER) || nt->OptionalHeader.SizeOfImage > 0x40000000)
        return false;
    Image image{base, nt->OptionalHeader.SizeOfImage};
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress || !image.range(directory.VirtualAddress, directory.Size)) return false;
    PVOID originals[count]{};
    PVOID* slots[count]{};
    hook_count = 0;
    for (DWORD pos = 0; pos + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= directory.Size;
         pos += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        auto descriptor = image.at<IMAGE_IMPORT_DESCRIPTOR>(directory.VirtualAddress + pos);
        if (!descriptor) return false;
        if (!descriptor->Name) break;
        const auto dll = image.string(descriptor->Name);
        if (!dll || !descriptor->OriginalFirstThunk) continue;
        for (DWORD i = 0; i < image.size / sizeof(IMAGE_THUNK_DATA32); ++i) {
            const uint64_t name_rva = uint64_t(descriptor->OriginalFirstThunk) +
                                      uint64_t(i) * sizeof(IMAGE_THUNK_DATA32);
            const uint64_t slot_rva = uint64_t(descriptor->FirstThunk) + uint64_t(i) * sizeof(IMAGE_THUNK_DATA32);
            if (name_rva > 0xffffffffull || slot_rva > 0xffffffffull) break;
            auto name = image.at<IMAGE_THUNK_DATA32>(static_cast<DWORD>(name_rva));
            auto slot = image.at<IMAGE_THUNK_DATA32>(static_cast<DWORD>(slot_rva));
            if (!name || !slot || !name->u1.AddressOfData) break;
            if (IMAGE_SNAP_BY_ORDINAL32(name->u1.Ordinal) || name->u1.AddressOfData > 0xfffffffd) continue;
            const auto symbol = image.string(name->u1.AddressOfData + 2);
            if (!symbol) continue;
            for (auto& hook : hooks) {
                if (_stricmp(dll, hook.dll) || std::strcmp(symbol, hook.name)) continue;
                const unsigned index = static_cast<unsigned>(&hook - hooks);
                if (slots[index]) continue;
                if (!slot->u1.Function || reinterpret_cast<uintptr_t>(&slot->u1.Function) % alignof(PVOID) ||
                    !readable(reinterpret_cast<PVOID>(slot->u1.Function), 1, nullptr, true))
                    continue;
                auto* proposed = reinterpret_cast<PVOID*>(&slot->u1.Function);
                for (auto* existing : slots)
                    if (existing == proposed) return false;
                slots[index] = proposed;
                originals[index] = reinterpret_cast<PVOID>(slot->u1.Function);
            }
        }
    }
    bool found = false;
    for (auto* slot : slots) found |= slot != nullptr;
    if (!found) return false;
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return false;
    for (auto& hook : hooks) {
        const auto i = &hook - hooks;
        hook.slot = slots[i];
        hook.original = originals[i];
        light::set_original(unsigned(i), originals[i]);
    }
    const bool nesting = light::initialize();
    if (buffer) {
        gz_buffer::Originals real;
        real.open = trace ? light::gz_open_traced : original<GzOpenFn>(Operation::GzOpen);
        real.read = trace ? light::gz_read_traced : original<GzReadFn>(Operation::GzRead);
        real.seek = trace ? light::gz_seek_traced : original<GzSeekFn>(Operation::GzSeek);
        real.getc = trace ? light::gz_getc_traced : original<GzGetcFn>(Operation::GzGetc);
        real.tell = trace ? light::gz_tell_traced : original<GzTellFn>(Operation::GzTell);
        real.close = trace ? light::gz_close_traced : original<GzCloseFn>(Operation::GzClose);
        bool imports = true;
        for (const auto& hook : hooks)
            if (gz_buffer_row(hook) && !hook.slot) imports = false;
        if (imports) {
            if (HMODULE zlib = GetModuleHandleW(L"zlib1.dll"))
                real.rewind = reinterpret_cast<gz_buffer::RewindFn>(GetProcAddress(zlib, "gzrewind"));
            gz_buffer_active = gz_buffer::initialize(real, gz_buffer::requested_capacity());
        }
        log("gz_buffer requested=1 enabled=%u capacity_kb=%u telemetry=%u imports=%u rewind=%u slots=%u",
            gz_buffer_active, gz_buffer::requested_capacity() / 1024, trace, imports, real.rewind != nullptr,
            gz_buffer::slot_count);
    }
    if (crypt) {
        crypt_cache::Originals real;
        real.acquire = trace ? light::crypt_acquire_context : original<crypt_cache::AcquireFn>(Operation::CryptAcquire);
        real.release = trace ? light::crypt_release_context : original<crypt_cache::ReleaseFn>(Operation::CryptRelease);
        real.import_key = trace ? light::crypt_import_key : original<crypt_cache::ImportFn>(Operation::CryptImport);
        real.destroy_key = trace ? light::crypt_destroy_key
                                 : original<crypt_cache::DestroyKeyFn>(Operation::CryptKeyDestroy);
        bool imports = true;
        for (const auto& hook : hooks)
            if (crypt_cache_row(hook) && !hook.slot) imports = false;
        if (imports) install_crypt_group(real, target);
        log("crypt_cache requested=1 enabled=%u telemetry=%u imports=%u provider_slots=%u key_slots=%u blob_limit=%u",
            unsigned(crypt_cache_active.load()), trace, imports, crypt_cache::provider_slots, crypt_cache::key_slots,
            crypt_cache::blob_limit);
    }
    if (!trace && !gz_buffer_active && !crypt_cache_active && !adjacency) {
        log_adjacency_config(adjacency_request, trace);
        return false;
    }
    installation_started = true;
    if (!trace) { // buffer, crypt cache and/or the adjacency fast path only: no mesh cache, no metric rows
        LARGE_INTEGER buffer_frequency{};
        QueryPerformanceFrequency(&buffer_frequency);
        clock_frequency = buffer_frequency.QuadPart;
        if (adjacency) {
            adjacency_mode.store(adjacency_armed, std::memory_order_release);
            mesh_observation_enabled.store(true, std::memory_order_release);
        }
        bool adjacency_rows = false;
        for (auto& hook : hooks) {
            if (crypt && crypt_cache_row(hook)) {
                if (hook.slot && *hook.slot == hook.replacement) ++hook_count;
                log("loading_hook name=%s installed=%u cache_active=%u", hook.name,
                    unsigned(hook.slot && *hook.slot == hook.replacement), unsigned(crypt_cache_active.load()));
                continue;
            }
            if (!((gz_buffer_active && gz_buffer_row(hook)) || (crypt_cache_active && crypt_cache_row(hook)) ||
                  (adjacency && mesh_row(hook)))) {
                hook.slot = nullptr;
                continue;
            }
            if (hook.slot && patch(hook, false)) {
                ++hook_count;
                adjacency_rows |= adjacency && mesh_row(hook);
                log("loading_hook name=%s installed=1", hook.name);
            } else {
                log("loading_hook name=%s installed=0", hook.name);
                if (hook.slot && !has_protection_debt(hook.slot)) hook.slot = nullptr;
            }
        }
        // Neither mesh row patched: no mesh reaches the vtable hooks; keep the native state.
        if (adjacency && !adjacency_rows) {
            mesh_observation_enabled.store(false, std::memory_order_release);
            adjacency_mode.store(AdjacencyMode::Native, std::memory_order_release);
        }
        coverage_start = tick();
        installed.store(hook_count != 0);
        char scope[48]{};
        const bool scope_on[] = {gz_buffer_active, crypt_cache_active.load(), adjacency_rows};
        const char* const scope_names[] = {"gz_buffer", "crypt_cache", "mesh_adjacency"};
        for (unsigned i = 0; i < 3; ++i)
            if (scope_on[i]) {
                if (scope[0]) std::strcat(scope, "+");
                std::strcat(scope, scope_names[i]);
            }
        log("loading_trace coverage_begin=%llu frequency=%llu hooks=%u module=main inclusive=0 paths=0 qualification=named_pe32_imports scope=%s",
            coverage_start, clock_frequency, hook_count, scope[0] ? scope : "none");
        log_adjacency_config(adjacency_request, trace);
        return installed.load();
    }
    const AdjacencyMode requested_mode = adjacency_armed; // with telemetry every parsed mode arms
    adjacency_mode.store(requested_mode, std::memory_order_release);
    log("mesh_adjacency mode=%s scope=hook_service equivalence=d3dx_rules+exact_position_equality fp_domain=pc53_nearest_masked_empty_mxcsr_1f80_or_9fc0 normal_gate=sse2_no_competing gate=public_systemmem_readonly+declaration_float3+no_attribute_table order=cache_lookup,compute,cache_store native_fallback=1 rsqrt=%s math_table=%s normalize=%s",
        adjacency_mode_name(requested_mode), adjacency_fast::rsqrt_implementation(),
        d3dx_math_table_name(d3dx_math_table()),
        d3dx_math_table() == D3dxMathTable::Sse2      ? "sse2"
        : d3dx_math_table() == D3dxMathTable::Generic ? "generic"
                                                      : "native_only");
    clock_frequency = frequency.QuadPart;
    mesh_observation_enabled.store(true, std::memory_order_release);
    const bool probes = probes_requested();
    for (auto& hook : hooks) {
        if (crypt && crypt_cache_row(hook)) {
            if (hook.slot && *hook.slot == hook.replacement) ++hook_count;
            log("loading_hook name=%s installed=%u cache_active=%u", hook.name,
                unsigned(hook.slot && *hook.slot == hook.replacement), unsigned(crypt_cache_active.load()));
            continue;
        }
        if (probe_row(unsigned(&hook - hooks)) && !probes && !(crypt_cache_active && crypt_cache_row(hook))) {
            hook.slot = nullptr;
            continue;
        }
        if (hook.slot && patch(hook, false)) {
            ++hook_count;
            log("loading_hook name=%s installed=1", hook.name);
        } else {
            log("loading_hook name=%s installed=0", hook.name);
            if (hook.slot && !has_protection_debt(hook.slot)) hook.slot = nullptr;
        }
    }
    coverage_start = tick();
    installed.store(hook_count != 0);
    log("loading_trace coverage_begin=%llu frequency=%llu hooks=%u module=main inclusive=1 paths=0 qualification=named_pe32_imports light_rows=1 nesting=%u probes=%u crypt_cache=%u",
        coverage_start, clock_frequency, hook_count, unsigned(nesting), unsigned(probes), unsigned(crypt_cache_active));
    if (probes) loading_probes::initialize();
    log_adjacency_config(adjacency_request, trace);
    return installed.load();
}
}

bool initialize() {
    const DWORD error = GetLastError();
    bool result = install(GetModuleHandleW(nullptr));
    SetLastError(error);
    return result;
}
bool mesh_adjacency_requested() {
    return adjacency_fast::armed_mode(adjacency_requested_mode(), false) != AdjacencyMode::Native;
}
bool active() {
    return installed.load() || mesh_owned_slots.load() || protection_debts.load();
}
Snapshot take_snapshot() {
    Snapshot result{};
    for (unsigned i = 0; i < count; ++i) light::take(i, result[i]);
    return result;
}
void report() {
    if (!active()) return;
    const DWORD error = GetLastError();
    const auto fp = computational_state();
    const auto data = take_snapshot();
    const auto end = tick();
    for (unsigned i = 0; i < count; ++i) {
        const auto& s = data[i];
        if (!s.count && !s.failures && !s.pending && !s.ambiguous && !s.bytes && !s.inclusive_ticks &&
            !s.exclusive_ticks && !s.maximum_ticks && !s.overhead_ticks)
            continue;
        log("loading_metric op=%s qpc=%llu count=%llu failures=%llu pending=%llu ambiguous=%llu bytes=%llu inclusive_ticks=%llu exclusive_ticks=%llu max_ticks=%llu wrapper_tail_ticks=%llu total_us=%.3f exclusive_us=%.3f max_us=%.3f wrapper_tail_us=%.3f",
            operation_name(i), end, s.count, s.failures, s.pending, s.ambiguous, s.bytes, s.inclusive_ticks,
            s.exclusive_ticks, s.maximum_ticks, s.overhead_ticks, double(s.inclusive_ticks) * 1e6 / clock_frequency,
            double(s.exclusive_ticks) * 1e6 / clock_frequency, double(s.maximum_ticks) * 1e6 / clock_frequency,
            double(s.overhead_ticks) * 1e6 / clock_frequency);
    }
    adjacency_report();
    loading_probes::report();
    resource_reader::report();
    crypt_cache_report("window");
    restore_computational_state(fp);
    SetLastError(error);
}
namespace {
crypt_cache::Statistics crypt_reported; // the cumulative totals at the last window line
}
// crypt_cache scope=window: the counters since the previous window line (nothing
// logged when idle); scope=session: the cumulative totals (last device destroyed,
// no telemetry needed). Integer fields only; called under the reporter's CPU
// state save or from the device teardown path.
void crypt_cache_report(const char* scope) {
    if (!crypt_cache_active) return;
    const DWORD error = GetLastError();
    const auto s = crypt_cache::statistics();
    const bool window = scope && scope[0] == 'w';
    const crypt_cache::Statistics zero{};
    const auto& base = window ? crypt_reported : zero;
    const bool idle = window && s.acquires == base.acquires && s.releases == base.releases &&
                      s.imports == base.imports && s.destroys == base.destroys;
    if (!idle)
        log("crypt_cache scope=%s acquires=%llu hits=%llu misses=%llu failed_passthrough=%llu releases_suppressed=%llu imports=%llu import_hits=%llu deletes_emulated=%llu deletes_passthrough=%llu busy_passthrough=%llu evictions=%llu releases=%llu import_passthrough=%llu destroys=%llu destroys_suppressed=%llu providers_cached=%u keys_cached=%u probe_error=0x%08x",
            scope, (unsigned long long)(s.acquires - base.acquires), (unsigned long long)(s.hits - base.hits),
            (unsigned long long)(s.misses - base.misses),
            (unsigned long long)(s.failed_passthrough - base.failed_passthrough),
            (unsigned long long)(s.releases_suppressed - base.releases_suppressed),
            (unsigned long long)(s.imports - base.imports), (unsigned long long)(s.import_hits - base.import_hits),
            (unsigned long long)(s.deletes_emulated - base.deletes_emulated),
            (unsigned long long)(s.deletes_passthrough - base.deletes_passthrough),
            (unsigned long long)(s.busy_passthrough - base.busy_passthrough),
            (unsigned long long)(s.evictions - base.evictions), (unsigned long long)(s.releases - base.releases),
            (unsigned long long)(s.import_passthrough - base.import_passthrough),
            (unsigned long long)(s.destroys - base.destroys),
            (unsigned long long)(s.destroys_suppressed - base.destroys_suppressed), s.providers_cached, s.keys_cached,
            unsigned(s.probe_error));
    if (window) crypt_reported = s;
    SetLastError(error);
}
bool crypt_cache_enabled() {
    return crypt_cache_active;
}
void shutdown() {
    const DWORD error = GetLastError();
    if (crypt_cache_active) {
        crypt_cache_report("session");
        log("crypt_cache_shutdown released=%u", unsigned(crypt_cache::shutdown()));
    }
    loading_probes::shutdown();
    restore_mesh_hooks();
    for (auto& hook : hooks)
        if (hook.slot) {
            const bool owned = *hook.slot == hook.replacement;
            const bool restored = owned && patch(hook, true);
            log("loading_hook name=%s restored=%u owned=%u", hook.name, restored, owned);
            // Keep callable original pointers: a callback already dispatched to this
            // module must still be valid. Callers must ensure teardown is quiescent.
            if (restored || *hook.slot != hook.replacement) hook.slot = nullptr;
        }
    hook_count = 0;
    for (const auto& hook : hooks)
        if (hook.slot) ++hook_count;
    installed.store(hook_count != 0);
    recover_protections();
    SetLastError(error);
}
#ifdef X3M_LOADING_TRACE_FIXTURE
bool fixture_initialize(HMODULE target) {
    return install(target);
}
void fixture_crypt_image_base(HMODULE base) {
    crypt_image_base = reinterpret_cast<uintptr_t>(base);
}
void fixture_crypt_patch_control(unsigned step, void (*observer)()) {
    fail_crypt_patch_step = step;
    crypt_patch_observer = observer;
}
bool fixture_crypt_site(const void* caller, Operation operation) {
    return crypt_site(caller, operation);
}
void fixture_fail_mesh_patch(unsigned step) {
    fail_mesh_patch = step;
}
void fixture_fail_protection_restores(unsigned calls) {
    fail_protection_restore = calls;
}
unsigned fixture_protection_debts() {
    return protection_debts.load();
}
void fixture_adjacency_math_table(int table) {
    adjacency_fixture_math_table = table;
}
bool fixture_adjacency_registry_dword(LSTATUS status, DWORD type, DWORD size) {
    return adjacency_registry_dword(status, type, size);
}
bool fixture_adjacency_registry_ambiguous(LSTATUS status, DWORD type, DWORD size) {
    return adjacency_registry_ambiguous(status, type, size);
}
HRESULT fixture_adjacency_fast(ID3DXMesh* mesh, FLOAT epsilon, DWORD* output,
                               HRESULT(WINAPI* original)(ID3DXMesh*, FLOAT, DWORD*)) {
    // Match the real mesh_adjacency wrapper: cold emutls lookup may alter
    // LastError, so restore caller state immediately before entering the service.
    CpuCallBoundary cpu;
    const auto saved = adjacency_thread_original;
    adjacency_thread_original = original;
    cpu.before_original();
    const HRESULT hr = adjacency_fast_service(mesh, epsilon, output);
    cpu.after_original();
    adjacency_thread_original = saved;
    return hr;
}
void fixture_adjacency_mode(unsigned mode) {
    adjacency_mode.store(mode == 2   ? AdjacencyMode::Fast
                         : mode == 1 ? AdjacencyMode::Verify
                                     : AdjacencyMode::Native,
                         std::memory_order_release);
}
AdjacencyStatistics fixture_adjacency_statistics() {
    const auto& c = adjacency_counters;
    AdjacencyStatistics s;
    s.calls = c.calls.load();
    s.computed = c.computed.load();
    s.fallbacks = c.fallbacks.load();
    s.faults = c.faults.load();
    s.fast_ticks = c.fast_ticks.load();
    s.native_ticks = c.native_ticks.load();
    s.verify_admitted = c.verify_admitted.load();
    s.verify_admitted_mismatched = c.verify_admitted_mismatched.load();
    s.verify_refused_fp = c.verify_refused_fp.load();
    s.verify_refused_competing = c.verify_refused_competing.load();
    s.verify_meshes = c.verify_meshes.load();
    s.verify_equal = c.verify_equal.load();
    s.verify_mismatched = c.verify_mismatched.load();
    s.verify_mismatch_entries = c.verify_mismatch_entries.load();
    s.quantized = c.quantized.load();
    s.unquantized = c.unquantized.load();
    s.multi_candidate_meshes = c.multi_candidate_meshes.load();
    for (unsigned i = 0; i < adjacency_fallback_count; ++i) s.fallback_reasons[i] = c.fallback_reasons[i].load();
    for (unsigned i = 0; i < adjacency_fast::status_count; ++i) s.module_status[i] = c.module_status[i].load();
    s.faulted = adjacency_faulted.load();
    return s;
}
void fixture_adjacency_report() {
    adjacency_report();
}
#endif
}
