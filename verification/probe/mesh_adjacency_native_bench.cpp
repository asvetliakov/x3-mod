// Native Windows benchmark of src/proxy/mesh_adjacency_fast against the real
// ID3DXMesh::GenerateAdjacency (MinGW i686, run through WSL interop; never X3).
// Each D3DX DLL is loaded by name (system search order) and its own
// D3DXCreateMesh builds the meshes, so both editions' DLLs can be timed in one
// process. Per mesh: one untimed warm-up each, then N timed calls of native
// GenerateAdjacency (epsilon 1e-6, the engine's value) and N of the module's
// generate() on the locked buffers (QueryPerformanceCounter); byte equality of
// every native output against the module's (`equal`: the verify policy, D3DX rules with the dispatched normalize;
// `served_equal`: what fast mode returns, the module's output when production admission accepts the mesh, else
// native's; only a served mismatch counts as a failure). `production_min_us` times fast mode's own policy (SSE2:
// refuse_competing_normals) on the same buffers: the module time of an admitted mesh, or the module work up to
// the refusal of a refused one (which fast mode then pays on top of native).
// argv: [--reps N] [--meshes a,b,...] [--dll d3dx9_37.dll,d3dx9_43.dll] [--normalize sse2|generic]
// N (default 20) is the minimum; a mesh whose warm-up native call is short repeats until about 0.25 s of
// native time (at most 2000 calls) so that the small realistic meshes are not dominated by timer noise.
// Output: one BENCH_ROW per DLL and mesh, BENCH_INFO for the environment.
#include "../../src/proxy/mesh_adjacency_fast.h"
#include <d3dx9.h>
#include <cpuid.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
namespace fast = x3m::mesh_adjacency_fast;
namespace {
[[noreturn]] void fail(const char* what, long code = 0) {
    std::printf("BENCH_FAIL %s code=%08lx\n", what, code);
    std::exit(1);
}
void ok(HRESULT hr, const char* what) {
    if (FAILED(hr)) fail(what, hr);
}
double frequency = 1;
uint64_t qpc() {
    LARGE_INTEGER v{};
    QueryPerformanceCounter(&v);
    return uint64_t(v.QuadPart);
}
double us(uint64_t ticks) {
    return double(ticks) * 1e6 / frequency;
}
constexpr float quantum = 1.f / 16384.f; // the engine's int16 position scale
float grid(int k) {
    return float(k) * quantum;
}
struct Mesh {
    std::string name;
    unsigned stride = 20; // FLOAT3 position at 0, then NORMAL (stride 32) and/or TEXCOORD
    bool normal = false;
    bool wide = false; // 32-bit indices (the `-32` meshes, as in the fixture); 16-bit otherwise
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
    unsigned vertex_count() const { return unsigned(vertices.size() * 4 / stride); }
    unsigned face_count() const { return unsigned(indices.size() / 3); }
    bool bits32() const { return wide; }
    uint32_t push(float x, float y, float z, float nx = 0, float ny = 0, float nz = 1, float u = 0, float v = 0) {
        const uint32_t index = vertex_count();
        vertices.insert(vertices.end(), {x, y, z});
        if (normal) vertices.insert(vertices.end(), {nx, ny, nz});
        vertices.insert(vertices.end(), {u, v});
        return index;
    }
    void face(uint32_t a, uint32_t b, uint32_t c) { indices.insert(indices.end(), {a, b, c}); }
};
// Synthetic family of the arm64 benchmark (verification/results/mesh-adjacency-fast-host-benchmark.txt):
// grid, split and star as in mesh_adjacency_fast_fixture.cpp; the fan and the unquantized grid are
// rebuilt from that record's description and face/vertex counts.
Mesh grid224() {
    Mesh m;
    m.name = "grid-224x224-32";
    m.wide = true;
    const int n = 224;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) m.push(grid(x - 112), grid(y - 112), grid((x * 7 + y * 3) % 5));
    for (int y = 0; y + 1 < n; ++y)
        for (int x = 0; x + 1 < n; ++x) {
            const uint32_t a = y * n + x, b = a + 1, d = a + n, e = d + 1;
            m.face(a, b, e);
            m.face(a, e, d);
        }
    return m;
}
Mesh split(const char* name, int n, bool flat, uint32_t limit) {
    Mesh m;
    m.name = name;
    m.wide = limit > 65535;
    auto at = [&](int x, int y) {
        return m.push(grid(x - n / 2), grid(y - n / 2), flat ? 0.f : grid((x * 5 + y * 11) % 7));
    };
    for (int y = 0; y + 1 < n; ++y)
        for (int x = 0; x + 1 < n; ++x) {
            if (m.vertex_count() + 6 > limit) return m;
            uint32_t a = at(x, y), b = at(x + 1, y), c = at(x + 1, y + 1);
            m.face(a, b, c);
            a = at(x, y), b = at(x + 1, y + 1), c = at(x, y + 1);
            m.face(a, b, c);
        }
    return m;
}
Mesh star20000() {
    Mesh m;
    m.name = "star-20000-32";
    m.wide = true;
    const int n = 20000;
    for (int i = 0; i < n; ++i) {
        m.push(0, 0, 0);
        m.push(grid(1000 + (i % 97)), grid((i * 13) % 2000 - 1000), grid((i * 7) % 300));
    }
    for (int i = 0; i < n; ++i) m.face(uint32_t(2 * i), uint32_t(2 * i + 1), uint32_t((2 * (i + 1) + 1) % (2 * n)));
    return m;
}
// Every face on one edge: n faces (0,1,a) and n faces (1,0,a) over n distinct apexes (distinct y, so distinct normals).
Mesh fan(int n) {
    Mesh m;
    m.name = "fan-same-edge-" + std::to_string(n);
    m.push(0, 0, 0);
    m.push(grid(16), 0, 0);
    for (int i = 0; i < n; ++i) {
        const int y = i - n / 2 + (i >= n / 2 ? 1 : 0);
        const uint32_t a = m.push(grid(8), grid(y), grid((i * 37) % 1001 - 500));
        m.face(0, 1, a);
        m.face(1, 0, a);
    }
    return m;
}
// Shared-vertex grid off the power-of-two grid (the module's cell-scan gate path).
Mesh unquantized150() {
    Mesh m;
    m.name = "unquantized-grid-150";
    const int n = 150;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            m.push(float(x) * 0.0137f + 0.00031f, float(y) * 0.0137f + 0.00017f, float((x * 7 + y * 3) % 5) * 0.0011f);
    for (int y = 0; y + 1 < n; ++y)
        for (int x = 0; x + 1 < n; ++x) {
            const uint32_t a = y * n + x, b = a + 1, d = a + n, e = d + 1;
            m.face(a, b, e);
            m.face(a, e, d);
        }
    return m;
}
// Realistic family: the AP flight averaged 630 faces and 940 vertices per mesh with 44 % of the vertices welded.
// Disjoint islands of 3x4 quads (20 positions, 24 faces), each split into six 1x2-quad charts with their own
// normal and UV (36 vertices; the 16 seam copies weld: 44.4 %, 1.5 vertices per face). Positions quantized to the
// engine's 1/16384 grid, stride 32 (POSITION, NORMAL, TEXCOORD0), 16-bit indices, faces emitted until `faces`.
Mesh realistic(unsigned faces) {
    Mesh m;
    m.name = "realistic-" + std::to_string(faces);
    m.stride = 32;
    m.normal = true;
    const int quad = 64;
    for (int island = 0; m.face_count() < faces; ++island) {
        const int ox = (island % 32) * 8 * quad, oy = (island / 32) * 8 * quad;
        auto z = [&](int gx, int gy) { return grid(((gx * 7 + gy * 13 + island * 5) % 9) * 4); };
        for (int col = 0; col < 3 && m.face_count() < faces; ++col)
            for (int pair = 0; pair < 2 && m.face_count() < faces; ++pair) {
                const float len = std::sqrt(float(col * col + pair * pair + 1));
                const float nx = float(col) / len, ny = float(pair) / len, nz = 1.f / len;
                uint32_t v[3][2];
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 2; ++c) {
                        const int gx = col + c, gy = pair * 2 + r;
                        v[r][c] = m.push(grid(ox + gx * quad), grid(oy + gy * quad), z(gx, gy), nx, ny, nz,
                                         float(c), float(r) * 0.5f + float(island % 7));
                    }
                for (int r = 0; r < 2; ++r) {
                    const uint32_t a = v[r][0], b = v[r][1], d = v[r + 1][0], e = v[r + 1][1];
                    if (m.face_count() < faces) m.face(a, b, e);
                    if (m.face_count() < faces) m.face(a, e, d);
                }
            }
    }
    return m;
}
std::vector<Mesh> all_meshes() {
    std::vector<Mesh> all;
    all.push_back(grid224());
    all.push_back(split("split-150x150-32", 150, false, 0xffffffffu));
    all.push_back(star20000());
    all.push_back(split("split-16", 100, true, 65535));
    all.push_back(fan(2000));
    all.push_back(fan(8000));
    all.push_back(unquantized150());
    for (unsigned f : {200u, 630u, 2000u, 8000u, 30000u}) all.push_back(realistic(f));
    return all;
}
// The D3DXVec3Normalize table D3DX dispatches (mirror of loading_trace.cpp d3dx_math_table_detect, documented
// inputs only); the module reproduces the SSE2 and generic tables.
const char* math_table(fast::Normalize& normalize) {
    auto registry = [](const char* value, DWORD& out) {
        HKEY key = nullptr;
        if (RegOpenKeyA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Direct3D", &key) != ERROR_SUCCESS) return false;
        DWORD type = 0, size = sizeof out;
        const LSTATUS s = RegQueryValueExA(key, value, nullptr, &type, reinterpret_cast<BYTE*>(&out), &size);
        RegCloseKey(key);
        return s == ERROR_SUCCESS && type == REG_DWORD && size == sizeof out;
    };
    DWORD psgp = 0, d3dx_psgp = 0;
    if (!registry("DisablePSGP", psgp)) psgp = 0;
    if (registry("DisableD3DXPSGP", d3dx_psgp)) psgp = d3dx_psgp;
    normalize = fast::Normalize::Generic;
    if (psgp == 1) return "generic";
    unsigned a = 0, b = 0, c = 0, d = 0, edx1 = 0;
    if (__get_cpuid(1, &a, &b, &c, &d)) edx1 = d;
    if (psgp != 2 && IsProcessorFeaturePresent(PF_3DNOW_INSTRUCTIONS_AVAILABLE)) return "3dnow_or_generic";
    if (edx1 & (1u << 26)) {
        normalize = fast::Normalize::Sse2;
        return "sse2";
    }
    return "sse";
}
using Create = HRESULT(WINAPI*)(DWORD, DWORD, DWORD, const D3DVERTEXELEMENT9*, IDirect3DDevice9*, ID3DXMesh**);
ID3DXMesh* create_mesh(Create create, IDirect3DDevice9* device, const Mesh& m) {
    std::vector<D3DVERTEXELEMENT9> decl{{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}};
    if (m.normal) decl.push_back({0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0});
    decl.push_back({0, WORD(m.normal ? 24 : 12), D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0});
    decl.push_back(D3DDECL_END());
    ID3DXMesh* mesh = nullptr;
    ok(create(m.face_count(), m.vertex_count(), D3DXMESH_SYSTEMMEM | (m.bits32() ? D3DXMESH_32BIT : 0), decl.data(),
              device, &mesh),
       "D3DXCreateMesh");
    if (mesh->GetNumBytesPerVertex() != m.stride) fail("stride");
    void* data = nullptr;
    ok(mesh->LockVertexBuffer(0, &data), "lock vb");
    std::memcpy(data, m.vertices.data(), m.vertices.size() * 4);
    ok(mesh->UnlockVertexBuffer(), "unlock vb");
    ok(mesh->LockIndexBuffer(0, &data), "lock ib");
    if (m.bits32())
        std::memcpy(data, m.indices.data(), m.indices.size() * 4);
    else
        for (size_t i = 0; i < m.indices.size(); ++i) static_cast<WORD*>(data)[i] = WORD(m.indices[i]);
    ok(mesh->UnlockIndexBuffer(), "unlock ib");
    DWORD* attributes = nullptr;
    ok(mesh->LockAttributeBuffer(0, &attributes), "lock attributes");
    std::fill(attributes, attributes + m.face_count(), 0u);
    ok(mesh->UnlockAttributeBuffer(), "unlock attributes");
    return mesh;
}
struct Stats {
    double min_us = 1e300, sum_us = 0;
    void add(uint64_t ticks) {
        const double t = us(ticks);
        min_us = std::min(min_us, t);
        sum_us += t;
    }
};
std::vector<std::string> split_list(const char* s) {
    std::vector<std::string> out;
    std::string cur;
    for (; *s; ++s)
        if (*s == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else
            cur += *s;
    if (!cur.empty()) out.push_back(cur);
    return out;
}
} // namespace
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    unsigned reps = 20;
    const char* forced = nullptr;
    std::vector<std::string> wanted, dlls{"d3dx9_37.dll", "d3dx9_43.dll"};
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--reps")) reps = unsigned(std::max(1, std::atoi(argv[i + 1])));
        else if (!std::strcmp(argv[i], "--meshes")) wanted = split_list(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--dll")) dlls = split_list(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--normalize")) forced = argv[i + 1];
        else fail("usage: [--reps N] [--meshes a,b] [--dll x.dll,y.dll]");
    }
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    frequency = double(f.QuadPart);
    fast::Normalize normalize;
    const char* table = math_table(normalize);
    if (forced) normalize = std::strcmp(forced, "generic") ? fast::Normalize::Sse2 : fast::Normalize::Generic;
    std::printf("BENCH_INFO qpc_hz=%.0f reps=%u epsilon=1e-06 math_table=%s normalize=%s rsqrt=%s\n", frequency, reps,
                table, fast::normalize_name(normalize), fast::rsqrt_implementation());
    using Create9 = IDirect3D9*(WINAPI*)(UINT);
    HMODULE d3d = LoadLibraryW(L"d3d9.dll");
    if (!d3d) fail("d3d9.dll");
    auto create9 = reinterpret_cast<Create9>(reinterpret_cast<void*>(GetProcAddress(d3d, "Direct3DCreate9")));
    IDirect3D9* api = create9 ? create9(D3D_SDK_VERSION) : nullptr;
    if (!api) fail("Direct3DCreate9");
    WNDCLASSA cls{};
    cls.lpfnWndProc = DefWindowProcA;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = "X3MeshAdjacencyNativeBench";
    RegisterClassA(&cls);
    HWND window = CreateWindowA(cls.lpszClassName, "mesh adjacency bench", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr,
                                nullptr, cls.hInstance, nullptr);
    if (!window) fail("window", long(GetLastError()));
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.BackBufferWidth = pp.BackBufferHeight = 64;
    IDirect3DDevice9* device = nullptr;
    HRESULT hr = api->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
    const char* devtype = "hal";
    if (FAILED(hr)) {
        hr = api->CreateDevice(0, D3DDEVTYPE_NULLREF, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
        devtype = "nullref";
    }
    ok(hr, "CreateDevice");
    std::printf("BENCH_INFO device=%s\n", devtype);
    std::vector<Mesh> meshes = all_meshes();
    if (!wanted.empty()) {
        std::vector<Mesh> chosen;
        for (const auto& w : wanted) {
            auto it = std::find_if(meshes.begin(), meshes.end(), [&](const Mesh& m) { return m.name == w; });
            if (it == meshes.end()) fail(("unknown mesh " + w).c_str());
            chosen.push_back(*it);
        }
        meshes.swap(chosen);
    }
    unsigned failures = 0;
    for (const auto& dll : dlls) {
        std::wstring wide(dll.begin(), dll.end());
        HMODULE module = LoadLibraryW(wide.c_str());
        if (!module) {
            std::printf("BENCH_DLL name=%s loaded=0 error=%lu\n", dll.c_str(), GetLastError());
            ++failures;
            continue;
        }
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(module, path, MAX_PATH);
        std::printf("BENCH_DLL name=%s loaded=1 path=%ls\n", dll.c_str(), path);
        auto create = reinterpret_cast<Create>(reinterpret_cast<void*>(GetProcAddress(module, "D3DXCreateMesh")));
        if (!create) fail("D3DXCreateMesh export");
        for (const auto& m : meshes) {
            ID3DXMesh* mesh = create_mesh(create, device, m);
            const size_t n = size_t(m.face_count()) * 3;
            std::vector<DWORD> first(n), native(n);
            std::vector<uint32_t> module_out(n, 0xabcdefu);
            const uint64_t w0 = qpc();
            ok(mesh->GenerateAdjacency(1e-6f, first.data()), "warm-up GenerateAdjacency");
            const double warm_us = std::max(1.0, us(qpc() - w0));
            const unsigned count = std::max(reps, unsigned(std::min(2000.0, 250000.0 / warm_us)));
            Stats nat, fst, locked, prod;
            bool native_stable = true;
            for (unsigned r = 0; r < count; ++r) {
                std::fill(native.begin(), native.end(), 0xabcdefu);
                const uint64_t t0 = qpc();
                const HRESULT h = mesh->GenerateAdjacency(1e-6f, native.data());
                nat.add(qpc() - t0);
                ok(h, "GenerateAdjacency");
                native_stable &= native == first;
            }
            fast::Input in;
            in.vertex_count = m.vertex_count();
            in.stride = m.stride;
            in.position_offset = 0;
            in.indices_32bit = m.bits32();
            in.face_count = m.face_count();
            in.epsilon = 1e-6f;
            fast::Policy policy; // the verify policy (D3DX rules) with the dispatched normalize
            policy.normalize = normalize;
            fast::Report report;
            void* vb = nullptr;
            void* ib = nullptr;
            ok(mesh->LockVertexBuffer(D3DLOCK_READONLY, &vb), "fast lock vb");
            ok(mesh->LockIndexBuffer(D3DLOCK_READONLY, &ib), "fast lock ib");
            in.vertices = vb;
            in.indices = ib;
            report = fast::generate(in, module_out.data(), policy); // warm-up (arena, page faults)
            bool module_stable = true;
            const std::vector<uint32_t> module_first = module_out;
            for (unsigned r = 0; r < count; ++r) {
                std::fill(module_out.begin(), module_out.end(), 0xabcdefu);
                const uint64_t t0 = qpc();
                report = fast::generate(in, module_out.data(), policy);
                fst.add(qpc() - t0);
                module_stable &= module_out == module_first;
            }
            fast::Policy production = policy; // fast mode's admission: SSE2 refuses competing normals
            production.refuse_competing_normals = normalize == fast::Normalize::Sse2;
            std::vector<uint32_t> scratch(n);
            const fast::Report admitted = fast::generate(in, scratch.data(), production);
            const bool scratch_equal = std::memcmp(first.data(), scratch.data(), n * 4) == 0;
            for (unsigned r = 0; r < count; ++r) {
                const uint64_t t0 = qpc();
                fast::generate(in, scratch.data(), production);
                prod.add(qpc() - t0);
            }
            ok(mesh->UnlockIndexBuffer(), "fast unlock ib");
            ok(mesh->UnlockVertexBuffer(), "fast unlock vb");
            // Lock + generate + unlock: the service's public-buffer path without the gate checks.
            for (unsigned r = 0; r < count; ++r) {
                const uint64_t t0 = qpc();
                ok(mesh->LockVertexBuffer(D3DLOCK_READONLY, &vb), "fast lock vb");
                ok(mesh->LockIndexBuffer(D3DLOCK_READONLY, &ib), "fast lock ib");
                in.vertices = vb;
                in.indices = ib;
                fast::generate(in, scratch.data(), policy);
                ok(mesh->UnlockIndexBuffer(), "fast unlock ib");
                ok(mesh->UnlockVertexBuffer(), "fast unlock vb");
                locked.add(qpc() - t0);
            }
            const bool equal = report.status == fast::Status::Ok && native_stable && module_stable &&
                               std::memcmp(first.data(), module_out.data(), n * 4) == 0;
            // What fast mode serves: the module's output when production admits it, else native's own.
            const bool served_equal = admitted.status == fast::Status::Ok ? equal && scratch_equal : true;
            failures += !served_equal;
            unsigned mismatches = 0;
            for (size_t i = 0; i < n; ++i) mismatches += first[i] != module_out[i];
            std::printf(
                "BENCH_ROW dll=%s mesh=%s faces=%u vertices=%u bits=%u native_min_us=%.1f native_mean_us=%.1f "
                "fast_min_us=%.1f fast_mean_us=%.1f speedup_min=%.2f equal=%u served_equal=%u reps=%u mismatches=%u locked_min_us=%.1f "
                "locked_mean_us=%.1f status=%s production_status=%s welded=%u representatives=%u "
                "multi_candidates=%u normal_selected=%u quantized=%u production_min_us=%.1f\n",
                dll.c_str(), m.name.c_str(), m.face_count(), m.vertex_count(), m.bits32() ? 32u : 16u, nat.min_us,
                nat.sum_us / count, fst.min_us, fst.sum_us / count, nat.min_us / fst.min_us, unsigned(equal),
                unsigned(served_equal), count, mismatches, locked.min_us, locked.sum_us / count,
                fast::status_name(unsigned(report.status)), fast::status_name(unsigned(admitted.status)), report.welded,
                report.representatives, report.multi_candidates, report.normal_selected, unsigned(report.quantized),
                prod.min_us);
            mesh->Release();
        }
    }
    fast::release_scratch();
    device->Release();
    api->Release();
    DestroyWindow(window);
    std::printf("BENCH_DONE failures=%u\n", failures);
    return failures ? 2 : 0;
}
