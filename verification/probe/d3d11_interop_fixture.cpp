// D3D11 post-chain feasibility probe (docs/architecture/d3d11-post-chain-feasibility.md):
// in one process, on the bottle the game runs in (or native Windows), does a D3D11
// device beside the game's plain D3D9 device get anything the post chain could use?
//   1. module provenance (d3d9, d3d11, dxgi, d3dcompiler_47: path, version resource,
//      Wine builtin signature, backend markers found in the mapped image) and device
//      creation: plain D3D9 as the game creates it, D3D9Ex, D3D11 on the same adapter;
//   2. cross-API texture sharing both ways (D3D11 MISC_SHARED -> IDXGIResource::GetSharedHandle
//      -> D3D9Ex CreateTexture(pSharedHandle); D3D9Ex CreateTexture(out handle) ->
//      ID3D11Device::OpenSharedResource), A8R8G8B8 / A16B16G16R16F / A32B32G32R32F and the
//      two depth formats, a gradient rendered on one API and read back on the other,
//      bit-exact, with the sync round trip timed; the plain-D3D9 device's answers to the
//      same calls (the game's device is plain: it cannot be replaced by a D3D9Ex one);
//   3. the 17x17 separable max/min dilation of an 8-bit mask as three D3D9 ps_3_0 passes,
//      one D3D11 cs_5_0 dispatch with group-shared memory and three D3D11 ps_4_0 passes,
//      at 1920x1080 and 5120x1440, every output checked against a CPU reference, timed
//      by D3D9 event-query brackets and D3D11 timestamp-disjoint queries;
//   4. a DXGI swapchain on a hidden window (flip model when accepted), the HDR colour
//      spaces IDXGISwapChain3::CheckColorSpaceSupport reports, IDXGIOutput6::GetDesc1;
//   5. the D3D11 caps the chain would rely on (compute, typed UAV loads, FP16 UAV).
// Every API is probed through LoadLibrary/GetProcAddress; a missing one reports
// status=unavailable for its step and the fixture continues. Built by CMake (target
// d3d11_interop_fixture); run through verification/probe/run_d3d11_interop.py under
// wine_lock.py. Never launches the game, never goes fullscreen, presents once.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned checks = 0, failures = 0, unavailable = 0;
bool require(const char* label, bool value) { ++checks; if (!value) ++failures; std::printf("CHECK %s %s\n", label, value ? "PASS" : "FAIL"); return value; }
void step_unavailable(const char* step, const char* reason, HRESULT hr = S_OK) { ++unavailable; std::printf("STEP %s status=unavailable reason=%s hr=%08lx\n", step, reason, (unsigned long)hr); }
void step_ok(const char* step) { std::printf("STEP %s status=ok\n", step); }
struct Unavailable : std::runtime_error { HRESULT hr; Unavailable(const char* what, HRESULT h = S_OK) : std::runtime_error(what), hr(h) {} };
void need(const char* what, HRESULT hr) { if (FAILED(hr)) throw Unavailable(what, hr); }
template <class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }
std::string token(const char* text) { std::string s = text ? text : ""; for (char& c : s) if (c == ' ' || c == '\t' || c == '\n' || c == '\r') c = '_'; if (s.empty()) s = "-"; return s.substr(0, 120); }
std::string token(const std::wstring& text) { std::string s; for (wchar_t c : text) s += c < 128 ? char(c) : '?'; return token(s.c_str()); }

// --- timing -------------------------------------------------------------------
LARGE_INTEGER qpf;
double now_us() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return double(t.QuadPart) * 1e6 / double(qpf.QuadPart); }
double median(std::vector<double> v) { if (v.empty()) return 0; std::sort(v.begin(), v.end()); return v[v.size() / 2]; }

// --- module provenance ----------------------------------------------------------
struct Module { const char* name; HMODULE handle = nullptr; };
bool find_marker(const unsigned char* base, SIZE_T size, const char* marker) {
    const SIZE_T n = std::strlen(marker);
    MEMORY_BASIC_INFORMATION info{};
    for (const unsigned char* p = base; p < base + size; p = static_cast<const unsigned char*>(info.BaseAddress) + info.RegionSize) {
        if (!VirtualQuery(p, &info, sizeof info)) break;
        const bool readable = info.State == MEM_COMMIT && !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && info.Protect != 0;
        const unsigned char* end = std::min(static_cast<const unsigned char*>(info.BaseAddress) + info.RegionSize, base + size);
        if (!readable || end <= p + n) continue;
        for (const unsigned char* q = p; q + n <= end; ++q)
            if (*q == static_cast<unsigned char>(marker[0]) && !std::memcmp(q, marker, n)) return true;
    }
    return false;
}
HMODULE report_module(const char* name) {
    HMODULE m = LoadLibraryA(name);
    if (!m) { std::printf("MODULE name=%s loaded=0 error=%lu\n", name, GetLastError()); return nullptr; }
    char path[MAX_PATH] = {}; GetModuleFileNameA(m, path, sizeof path);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(m);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const unsigned char*>(m) + dos->e_lfanew);
    const DWORD image_size = nt->OptionalHeader.SizeOfImage;
    const bool builtin = !std::memcmp(reinterpret_cast<const char*>(m) + 0x40, "Wine builtin DLL", 16);
    std::string version = "-", product = "-";
    if (HRSRC res = FindResourceA(m, MAKEINTRESOURCEA(1), MAKEINTRESOURCEA(16))) { // RT_VERSION, from the mapped image, not the path
        if (HGLOBAL data = LoadResource(m, res)) {
            const DWORD size = SizeofResource(m, res);
            std::vector<unsigned char> copy(static_cast<const unsigned char*>(LockResource(data)), static_cast<const unsigned char*>(LockResource(data)) + size);
            VS_FIXEDFILEINFO* fixed = nullptr; UINT len = 0;
            if (VerQueryValueA(copy.data(), "\\", reinterpret_cast<void**>(&fixed), &len) && fixed) {
                char text[64]; std::snprintf(text, sizeof text, "%u.%u.%u.%u", HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS), HIWORD(fixed->dwFileVersionLS), LOWORD(fixed->dwFileVersionLS)); version = text;
            }
            struct { WORD language, codepage; }* translation = nullptr;
            if (VerQueryValueA(copy.data(), "\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translation), &len) && translation && len >= 4) {
                char key[80]; std::snprintf(key, sizeof key, "\\StringFileInfo\\%04x%04x\\ProductName", translation->language, translation->codepage);
                char* value = nullptr; if (VerQueryValueA(copy.data(), key, reinterpret_cast<void**>(&value), &len) && value) product = token(value);
            }
        }
    }
    WIN32_FILE_ATTRIBUTE_DATA attributes{}; unsigned long long file_size = 0;
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &attributes)) file_size = (static_cast<unsigned long long>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
    std::string markers;
    for (const char* marker : {"DXMT", "dxmt", "winemetal", "D3DMetal", "wined3d", "vkd3d", "DXVK", "MoltenVK", "Wine placeholder DLL"})
        if (find_marker(static_cast<const unsigned char*>(static_cast<void*>(m)), image_size, marker)) markers += (markers.empty() ? "" : ",") + std::string(marker);
    std::printf("MODULE name=%s loaded=1 path=%s image_size=%lu file_size=%llu version=%s product=%s builtin=%u markers=%s\n", name, token(path).c_str(), (unsigned long)image_size, file_size,
                version.c_str(), product.c_str(), unsigned(builtin), markers.empty() ? "none" : markers.c_str());
    return m;
}

// --- shader compilation (d3dcompiler_47, probed) -------------------------------------
pD3DCompile compile_fn = nullptr;
std::vector<unsigned char> compile(const char* name, const char* source, const char* target) {
    if (!compile_fn) throw Unavailable("d3dcompiler_47");
    ID3DBlob* code = nullptr; ID3DBlob* errors = nullptr;
    const HRESULT hr = compile_fn(source, std::strlen(source), name, nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    std::string message = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "";
    std::printf("SHADER name=%s target=%s hr=%08lx bytes=%lu message=%s\n", name, target, (unsigned long)hr, code ? (unsigned long)code->GetBufferSize() : 0ul, token(message.substr(0, 160).c_str()).c_str());
    release(errors);
    if (FAILED(hr) || !code) throw Unavailable("compile", hr);
    std::vector<unsigned char> bytes(static_cast<const unsigned char*>(code->GetBufferPointer()), static_cast<const unsigned char*>(code->GetBufferPointer()) + code->GetBufferSize());
    release(code);
    return bytes;
}

// Gradient formulas (float arithmetic only, so ps_3_0 and ps_4_0 compute the same values, exactly representable in the target format).
const char* gradient_body =
    "  float fx = floor(px), fy = floor(py);\n"
    "  if (mode < 0.5) return float4(fmod(fx, 256.0), fmod(fy, 256.0), fmod(floor(fx / 256.0) * 16.0 + floor(fy / 256.0) * 32.0 + 7.0, 256.0), 255.0) / 255.0;\n"
    "  if (mode < 1.5) return float4(fmod(fx * 7.0 + fy * 3.0, 1024.0) / 8.0, fmod(fx, 2048.0) / 16.0, fmod(fy, 1024.0) / 8.0 - 64.0, 1.0);\n"
    "  return float4(fx * 4096.0 + fy, -fy, fx, 1.0);\n";
std::string gradient_ps9(bool vpos) { // c0 = (mode, mode, w, h)
    return std::string("float4 c0 : register(c0);\n") + (vpos ? "float4 main(float2 vpos : VPOS) : COLOR {\n  float px = vpos.x, py = vpos.y, mode = c0.x;\n" : "float4 main(float2 uv : TEXCOORD0) : COLOR {\n  float px = floor(uv.x * c0.z), py = floor(uv.y * c0.w), mode = c0.x;\n")
           + gradient_body + "}\n";
}
std::string gradient_ps11() { return "cbuffer C : register(b0) { float4 c0; };\nfloat4 main(float4 pos : SV_Position) : SV_Target {\n  float px = pos.x, py = pos.y, mode = c0.x;\n" + std::string(gradient_body) + "}\n"; }
void gradient_expected(unsigned mode, unsigned x, unsigned y, float out[4]) {
    const float fx = float(x), fy = float(y);
    if (mode == 0) { out[0] = std::fmod(fx, 256.f) / 255.f; out[1] = std::fmod(fy, 256.f) / 255.f; out[2] = std::fmod(std::floor(fx / 256.f) * 16.f + std::floor(fy / 256.f) * 32.f + 7.f, 256.f) / 255.f; out[3] = 1.f; }
    else if (mode == 1) { out[0] = std::fmod(fx * 7.f + fy * 3.f, 1024.f) / 8.f; out[1] = std::fmod(fx, 2048.f) / 16.f; out[2] = std::fmod(fy, 1024.f) / 8.f - 64.f; out[3] = 1.f; }
    else { out[0] = fx * 4096.f + fy; out[1] = -fy; out[2] = fx; out[3] = 1.f; }
}
float half_to_float(std::uint16_t h) {
    const unsigned sign = h >> 15, exponent = (h >> 10) & 31, mantissa = h & 1023; float value;
    if (exponent == 0) value = std::ldexp(float(mantissa), -24);
    else if (exponent == 31) value = mantissa ? NAN : INFINITY;
    else value = std::ldexp(float(mantissa | 1024), int(exponent) - 25);
    return sign ? -value : value;
}
// mode 0: 8-bit (bgra=true for A8R8G8B8 / B8G8R8A8 memory order), 1: fp16, 2: fp32. Returns mismatches; first mismatch position in fx/fy.
unsigned compare_gradient(unsigned mode, bool bgra, const unsigned char* data, unsigned pitch, unsigned w, unsigned h, unsigned* fx, unsigned* fy) {
    unsigned mismatches = 0;
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        float expected[4]; gradient_expected(mode, x, y, expected); bool same = true;
        if (mode == 0) {
            const unsigned char* p = data + y * pitch + x * 4;
            const unsigned char r = p[bgra ? 2 : 0], g = p[1], b = p[bgra ? 0 : 2], a = p[3];
            same = r == unsigned(std::lround(expected[0] * 255.f)) && g == unsigned(std::lround(expected[1] * 255.f)) && b == unsigned(std::lround(expected[2] * 255.f)) && a == 255;
        } else if (mode == 1) {
            const auto* p = reinterpret_cast<const std::uint16_t*>(data + y * pitch + x * 8);
            for (unsigned c = 0; c < 4; ++c) same = same && half_to_float(p[c]) == expected[c];
        } else {
            const float* p = reinterpret_cast<const float*>(data + y * pitch + x * 16);
            for (unsigned c = 0; c < 4; ++c) same = same && p[c] == expected[c];
        }
        if (!same) { if (!mismatches) { *fx = x; *fy = y; } ++mismatches; }
    }
    return mismatches;
}

// --- the dilation workload: mask, CPU reference, channel-order compare -----------------
constexpr int radius = 8;
std::vector<unsigned char> make_mask(unsigned w, unsigned h) {
    std::vector<unsigned char> m(size_t(w) * h);
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) m[size_t(y) * w + x] = static_cast<unsigned char>((x * 31u + y * 17u + (x >> 3) * (y >> 3) + ((x ^ y) & 4 ? 128u : 0u)) & 255u);
    return m;
}
// Output pixel (R,G,B,A) = (max17x17, min17x17, mask, max - min), clamp-to-edge.
std::vector<unsigned char> reference(const std::vector<unsigned char>& m, unsigned w, unsigned h) {
    std::vector<unsigned char> hmax(m.size()), hmin(m.size()), out(m.size() * 4);
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        unsigned char mx = 0, mn = 255;
        for (int k = -radius; k <= radius; ++k) { const int sx = std::min<int>(std::max<int>(int(x) + k, 0), int(w) - 1); const unsigned char v = m[size_t(y) * w + sx]; mx = std::max(mx, v); mn = std::min(mn, v); }
        hmax[size_t(y) * w + x] = mx; hmin[size_t(y) * w + x] = mn;
    }
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        unsigned char mx = 0, mn = 255;
        for (int k = -radius; k <= radius; ++k) { const int sy = std::min<int>(std::max<int>(int(y) + k, 0), int(h) - 1); mx = std::max(mx, hmax[size_t(sy) * w + x]); mn = std::min(mn, hmin[size_t(sy) * w + x]); }
        unsigned char* p = &out[(size_t(y) * w + x) * 4]; p[0] = mx; p[1] = mn; p[2] = m[size_t(y) * w + x]; p[3] = static_cast<unsigned char>(mx - mn);
    }
    return out;
}
unsigned compare_mask(const std::vector<unsigned char>& ref, const unsigned char* data, unsigned pitch, unsigned w, unsigned h, bool bgra) {
    unsigned mismatches = 0;
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        const unsigned char* p = data + size_t(y) * pitch + x * 4; const unsigned char* r = &ref[(size_t(y) * w + x) * 4];
        if (p[bgra ? 2 : 0] != r[0] || p[1] != r[1] || p[bgra ? 0 : 2] != r[2] || p[3] != r[3]) ++mismatches;
    }
    return mismatches;
}
std::string dilate_ps9(bool vpos) {
    return std::string("sampler2D s : register(s0); float4 c0 : register(c0); float4 c1 : register(c1);\n") // c0 = (1/w, 1/h, w, h), c1 = direction in texels
    + (vpos ? "float4 main(float2 vpos : VPOS) : COLOR {\n  float2 uv = (vpos + 0.5) * c0.xy;" : "float4 main(float2 tc : TEXCOORD0) : COLOR {\n  float2 uv = (floor(tc * c0.zw) + 0.5) * c0.xy;") + " float mx = 0.0, mn = 1.0;\n"
    "  [unroll] for (int k = -8; k <= 8; ++k) { float2 v = tex2D(s, uv + float(k) * c1.xy * c0.xy).rg; mx = max(mx, v.r); mn = min(mn, v.g); }\n  return float4(mx, mn, 0.0, 1.0);\n}\n";
}
std::string compose_ps9(bool vpos) {
    return std::string("sampler2D s : register(s0); sampler2D m : register(s1); float4 c0 : register(c0);\n")
    + (vpos ? "float4 main(float2 vpos : VPOS) : COLOR {\n  float2 uv = (vpos + 0.5) * c0.xy;" : "float4 main(float2 tc : TEXCOORD0) : COLOR {\n  float2 uv = (floor(tc * c0.zw) + 0.5) * c0.xy;")
    + " float2 d = tex2D(s, uv).rg; float v = tex2D(m, uv).r; return float4(d.r, d.g, v, d.r - d.g);\n}\n";
}
const char* fullscreen_vs11 = "float4 main(uint id : SV_VertexID) : SV_Position { float x = id == 1 ? 3.0 : -1.0; float y = id == 2 ? -3.0 : 1.0; return float4(x, y, 0.0, 1.0); }\n";
const char* dilate_ps11 =
    "Texture2D<float4> s : register(t0); cbuffer C : register(b0) { float4 c0; float4 c1; };\n"
    "float4 main(float4 pos : SV_Position) : SV_Target {\n  int2 p = int2(pos.xy); int2 dir = int2(c1.xy); int2 limit = int2(c0.zw) - 1; float mx = 0.0, mn = 1.0;\n"
    "  [unroll] for (int k = -8; k <= 8; ++k) { int2 q = min(max(p + k * dir, int2(0, 0)), limit); float2 v = s.Load(int3(q, 0)).rg; mx = max(mx, v.r); mn = min(mn, v.g); }\n  return float4(mx, mn, 0.0, 1.0);\n}\n";
const char* compose_ps11 =
    "Texture2D<float4> s : register(t0); Texture2D<float4> m : register(t1);\n"
    "float4 main(float4 pos : SV_Position) : SV_Target {\n  int3 p = int3(int2(pos.xy), 0); float2 d = s.Load(p).rg; float v = m.Load(p).r; return float4(d.r, d.g, v, d.r - d.g);\n}\n";
// One 16x16 tile per group: the 32x32 halo tile in group-shared memory, the horizontal pass into a 32x16 strip, the vertical pass by each thread.
const char* dilate_cs11 =
    "Texture2D<float4> src : register(t0); RWTexture2D<float4> dst : register(u0); cbuffer C : register(b0) { float4 c0; };\n"
    "groupshared float tile[1024]; groupshared float rowmax[512]; groupshared float rowmin[512];\n"
    "[numthreads(16, 16, 1)]\nvoid main(uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID) {\n"
    "  int2 limit = int2(c0.zw) - 1; int2 base = int2(gid.xy) * 16 - 8; uint tid = gtid.y * 16 + gtid.x;\n"
    "  for (uint i = tid; i < 1024; i += 256) { int2 p = min(max(base + int2(int(i % 32), int(i / 32)), int2(0, 0)), limit); tile[i] = src.Load(int3(p, 0)).r; }\n"
    "  GroupMemoryBarrierWithGroupSync();\n"
    "  for (uint j = tid; j < 512; j += 256) { uint ly = j / 16, lx = j % 16 + 8; float mx = 0.0, mn = 1.0;\n"
    "    [unroll] for (int k = -8; k <= 8; ++k) { float v = tile[ly * 32 + lx + k]; mx = max(mx, v); mn = min(mn, v); }\n    rowmax[j] = mx; rowmin[j] = mn; }\n"
    "  GroupMemoryBarrierWithGroupSync();\n"
    "  float mx = 0.0, mn = 1.0;\n  [unroll] for (int k = 0; k <= 16; ++k) { uint idx = (gtid.y + k) * 16 + gtid.x; mx = max(mx, rowmax[idx]); mn = min(mn, rowmin[idx]); }\n"
    "  int2 o = int2(gid.xy) * 16 + int2(gtid.xy); if (o.x <= limit.x && o.y <= limit.y) dst[uint2(o)] = float4(mx, mn, tile[(gtid.y + 8) * 32 + gtid.x + 8], mx - mn);\n}\n";

// --- D3D9 side --------------------------------------------------------------------
struct Vertex { float x, y, z, rhw, u, v; };
struct Device9 {
    IDirect3D9* api = nullptr; IDirect3DDevice9* device = nullptr; bool ex = false; D3DPRESENT_PARAMETERS pp{};
    IDirect3DQuery9* query = nullptr; // event query, the serialisation primitive
    void quad(unsigned w, unsigned h) {
        const Vertex v[4] = {{-0.5f, -0.5f, 0.f, 1.f, 0.f, 0.f}, {w - 0.5f, -0.5f, 0.f, 1.f, 1.f, 0.f}, {-0.5f, h - 0.5f, 0.f, 1.f, 0.f, 1.f}, {w - 0.5f, h - 0.5f, 0.f, 1.f, 1.f, 1.f}};
        need("DrawPrimitiveUP", device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof v[0]));
    }
    void bind_states() {
        need("SetFVF", device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1));
        device->SetRenderState(D3DRS_ZENABLE, FALSE); device->SetRenderState(D3DRS_LIGHTING, FALSE); device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE); device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE); device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xf);
        device->SetDepthStencilSurface(nullptr);
        for (DWORD s = 0; s < 2; ++s) { device->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_POINT); device->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_POINT); device->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
            device->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); device->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP); }
    }
    // Event query bracket: issue, then spin with D3DGETDATA_FLUSH until the GPU has passed it (as the production GpuSyncTiming does).
    double wait(bool* ok) {
        const double t0 = now_us();
        if (!query) { *ok = false; return 0; }
        if (FAILED(query->Issue(D3DISSUE_END))) { *ok = false; return 0; }
        for (;;) { const HRESULT hr = query->GetData(nullptr, 0, D3DGETDATA_FLUSH); if (hr == S_OK) break; if (FAILED(hr) || now_us() - t0 > 10e6) { *ok = false; return now_us() - t0; } }
        *ok = true; return now_us() - t0;
    }
    IDirect3DPixelShader9* shader(const std::vector<unsigned char>& bytes) { IDirect3DPixelShader9* s = nullptr; need("CreatePixelShader", device->CreatePixelShader(reinterpret_cast<const DWORD*>(bytes.data()), &s)); return s; }
    // Reads a render-target texture level 0 into system memory; calls f(data, pitch).
    template <class F> void readback(IDirect3DTexture9* texture, D3DFORMAT format, unsigned w, unsigned h, F f) {
        IDirect3DSurface9* level = nullptr; IDirect3DSurface9* sys = nullptr;
        need("GetSurfaceLevel", texture->GetSurfaceLevel(0, &level));
        need("CreateOffscreenPlainSurface", device->CreateOffscreenPlainSurface(w, h, format, D3DPOOL_SYSTEMMEM, &sys, nullptr));
        const HRESULT hr = device->GetRenderTargetData(level, sys); release(level);
        if (FAILED(hr)) { release(sys); throw Unavailable("GetRenderTargetData", hr); }
        D3DLOCKED_RECT rect{}; need("LockRect", sys->LockRect(&rect, nullptr, D3DLOCK_READONLY));
        f(static_cast<const unsigned char*>(rect.pBits), unsigned(rect.Pitch));
        sys->UnlockRect(); release(sys);
    }
    void destroy() { release(query); release(device); release(api); }
};
Device9 create_device9(HMODULE d3d9, HWND window, bool ex) {
    Device9 d; d.ex = ex;
    d.pp.Windowed = TRUE; d.pp.SwapEffect = D3DSWAPEFFECT_DISCARD; d.pp.hDeviceWindow = window; d.pp.BackBufferWidth = 256; d.pp.BackBufferHeight = 256;
    d.pp.BackBufferFormat = D3DFMT_A8R8G8B8; d.pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE; d.pp.EnableAutoDepthStencil = TRUE; d.pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    const DWORD flags = D3DCREATE_HARDWARE_VERTEXPROCESSING; // the game's device: hardware VP, plain IDirect3D9 (the proxy strips PUREDEVICE only)
    HRESULT hr;
    if (ex) {
        auto address = GetProcAddress(d3d9, "Direct3DCreate9Ex"); HRESULT(WINAPI* create)(UINT, IDirect3D9Ex**) = nullptr; std::memcpy(&create, &address, sizeof create);
        if (!create) throw Unavailable("Direct3DCreate9Ex_export");
        IDirect3D9Ex* api = nullptr; hr = create(D3D_SDK_VERSION, &api); if (FAILED(hr) || !api) throw Unavailable("Direct3DCreate9Ex", hr);
        d.api = api; IDirect3DDevice9Ex* device = nullptr;
        hr = api->CreateDeviceEx(0, D3DDEVTYPE_HAL, window, flags, &d.pp, nullptr, &device);
        if (FAILED(hr)) { d.destroy(); throw Unavailable("CreateDeviceEx", hr); }
        d.device = device;
    } else {
        auto address = GetProcAddress(d3d9, "Direct3DCreate9"); IDirect3D9*(WINAPI* create)(UINT) = nullptr; std::memcpy(&create, &address, sizeof create);
        if (!create) throw Unavailable("Direct3DCreate9_export");
        d.api = create(D3D_SDK_VERSION); if (!d.api) throw Unavailable("Direct3DCreate9");
        hr = d.api->CreateDevice(0, D3DDEVTYPE_HAL, window, flags, &d.pp, &d.device);
        if (FAILED(hr)) { d.destroy(); throw Unavailable("CreateDevice", hr); }
    }
    D3DADAPTER_IDENTIFIER9 ident{}; d.api->GetAdapterIdentifier(0, 0, &ident);
    D3DCAPS9 caps{}; d.device->GetDeviceCaps(&caps);
    IDirect3DDevice9Ex* as_ex = nullptr; const bool device_is_ex = SUCCEEDED(d.device->QueryInterface(__uuidof(IDirect3DDevice9Ex), reinterpret_cast<void**>(&as_ex))) && as_ex; release(as_ex);
    d.device->CreateQuery(D3DQUERYTYPE_EVENT, &d.query);
    std::printf("DEVICE api=%s hr=%08lx flags=%08lx adapter=%s driver=%s vendor=%04lx device=%04lx ps_version=%lx.%lx max_rts=%lu queries_event=%u device_is_ex=%u\n", ex ? "d3d9ex" : "d3d9", (unsigned long)hr, (unsigned long)flags,
                token(ident.Description).c_str(), token(ident.Driver).c_str(), ident.VendorId, ident.DeviceId, (unsigned long)D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion), (unsigned long)D3DSHADER_VERSION_MINOR(caps.PixelShaderVersion),
                (unsigned long)caps.NumSimultaneousRTs, unsigned(d.query != nullptr), unsigned(device_is_ex));
    return d;
}

// --- D3D11 side --------------------------------------------------------------------
struct Device11 {
    ID3D11Device* device = nullptr; ID3D11DeviceContext* context = nullptr; IDXGIAdapter1* adapter = nullptr; IDXGIFactory1* factory = nullptr;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL(0); UINT flags = 0; bool bgra = false;
    ID3D11VertexShader* fullscreen = nullptr; bool timestamps = true;
    ID3D11Texture2D* texture(unsigned w, unsigned h, DXGI_FORMAT format, UINT bind, UINT misc, const void* initial = nullptr, unsigned pitch = 0, D3D11_USAGE usage = D3D11_USAGE_DEFAULT, UINT cpu = 0) {
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1; desc.Format = format; desc.SampleDesc.Count = 1; desc.Usage = usage; desc.BindFlags = bind; desc.MiscFlags = misc; desc.CPUAccessFlags = cpu;
        D3D11_SUBRESOURCE_DATA data{initial, pitch, 0}; ID3D11Texture2D* t = nullptr;
        need("CreateTexture2D", device->CreateTexture2D(&desc, initial ? &data : nullptr, &t)); return t;
    }
    ID3D11Buffer* constants(const float* values, unsigned floats) {
        D3D11_BUFFER_DESC desc{}; desc.ByteWidth = floats * 4; desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER; D3D11_SUBRESOURCE_DATA data{values, 0, 0}; ID3D11Buffer* b = nullptr;
        need("CreateBuffer", device->CreateBuffer(&desc, &data, &b)); return b;
    }
    ID3D11PixelShader* ps(const std::vector<unsigned char>& bytes) { ID3D11PixelShader* s = nullptr; need("CreatePixelShader", device->CreatePixelShader(bytes.data(), bytes.size(), nullptr, &s)); return s; }
    ID3D11ComputeShader* cs(const std::vector<unsigned char>& bytes) { ID3D11ComputeShader* s = nullptr; need("CreateComputeShader", device->CreateComputeShader(bytes.data(), bytes.size(), nullptr, &s)); return s; }
    ID3D11RenderTargetView* rtv(ID3D11Texture2D* t) { ID3D11RenderTargetView* v = nullptr; need("CreateRenderTargetView", device->CreateRenderTargetView(t, nullptr, &v)); return v; }
    ID3D11ShaderResourceView* srv(ID3D11Texture2D* t) { ID3D11ShaderResourceView* v = nullptr; need("CreateShaderResourceView", device->CreateShaderResourceView(t, nullptr, &v)); return v; }
    ID3D11UnorderedAccessView* uav(ID3D11Texture2D* t) { ID3D11UnorderedAccessView* v = nullptr; need("CreateUnorderedAccessView", device->CreateUnorderedAccessView(t, nullptr, &v)); return v; }
    void fullscreen_draw(ID3D11RenderTargetView* target, ID3D11PixelShader* shader, unsigned w, unsigned h) {
        D3D11_VIEWPORT vp{0.f, 0.f, float(w), float(h), 0.f, 1.f}; context->RSSetViewports(1, &vp);
        context->OMSetRenderTargets(1, &target, nullptr); context->VSSetShader(fullscreen, nullptr, 0); context->PSSetShader(shader, nullptr, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); context->IASetInputLayout(nullptr); context->Draw(3, 0);
    }
    // Event-query wait after Flush (the documented cross-device sync without a keyed mutex).
    double wait(bool* ok) {
        const double t0 = now_us(); D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0}; ID3D11Query* q = nullptr;
        if (FAILED(device->CreateQuery(&desc, &q))) { *ok = false; return 0; }
        context->End(q); context->Flush();
        for (;;) { BOOL done = FALSE; const HRESULT hr = context->GetData(q, &done, sizeof done, 0); if (hr == S_OK) break; if (FAILED(hr) || now_us() - t0 > 10e6) { *ok = false; release(q); return now_us() - t0; } }
        release(q); *ok = true; return now_us() - t0;
    }
    template <class F> void readback(ID3D11Texture2D* t, F f) {
        D3D11_TEXTURE2D_DESC desc{}; t->GetDesc(&desc);
        ID3D11Texture2D* staging = texture(desc.Width, desc.Height, desc.Format, 0, 0, nullptr, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ);
        context->CopyResource(staging, t); D3D11_MAPPED_SUBRESOURCE map{};
        const HRESULT hr = context->Map(staging, 0, D3D11_MAP_READ, 0, &map);
        if (FAILED(hr)) { release(staging); throw Unavailable("Map", hr); }
        f(static_cast<const unsigned char*>(map.pData), unsigned(map.RowPitch));
        context->Unmap(staging, 0); release(staging);
    }
    void destroy() { release(fullscreen); if (context) context->ClearState(); release(context); release(device); release(adapter); release(factory); }
};
const char* level_name(D3D_FEATURE_LEVEL l) {
    switch (l) { case D3D_FEATURE_LEVEL_11_1: return "11_1"; case D3D_FEATURE_LEVEL_11_0: return "11_0"; case D3D_FEATURE_LEVEL_10_1: return "10_1"; case D3D_FEATURE_LEVEL_10_0: return "10_0"; default: return "other"; }
}
Device11 create_device11(HMODULE d3d11, HMODULE dxgi, DWORD vendor, DWORD device_id) {
    Device11 d;
    auto address = GetProcAddress(d3d11, "D3D11CreateDevice"); PFN_D3D11_CREATE_DEVICE create = nullptr; std::memcpy(&create, &address, sizeof create);
    if (!create) throw Unavailable("D3D11CreateDevice_export");
    if (dxgi) {
        auto f = GetProcAddress(dxgi, "CreateDXGIFactory1"); HRESULT(WINAPI* factory)(REFIID, void**) = nullptr; std::memcpy(&factory, &f, sizeof factory);
        if (factory && SUCCEEDED(factory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&d.factory))) && d.factory) {
            IDXGIAdapter1* candidate = nullptr;
            for (UINT i = 0; d.factory->EnumAdapters1(i, &candidate) == S_OK; ++i) {
                DXGI_ADAPTER_DESC1 desc{}; candidate->GetDesc1(&desc);
                std::printf("ADAPTER index=%u description=%s vendor=%04x device=%04x luid=%08lx:%08lx dedicated_mb=%llu flags=%x\n", i, token(desc.Description).c_str(), desc.VendorId, desc.DeviceId, (unsigned long)desc.AdapterLuid.HighPart, (unsigned long)desc.AdapterLuid.LowPart, (unsigned long long)(desc.DedicatedVideoMemory >> 20), desc.Flags);
                if (!d.adapter && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && (i == 0 || (desc.VendorId == vendor && desc.DeviceId == device_id))) { d.adapter = candidate; continue; }
                if (!d.adapter || (desc.VendorId == vendor && desc.DeviceId == device_id && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))) { release(d.adapter); d.adapter = candidate; continue; }
                release(candidate);
            }
        }
    }
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = E_FAIL; const char* path = "-";
    for (UINT flags : {UINT(D3D11_CREATE_DEVICE_BGRA_SUPPORT), UINT(0)}) {
        if (d.adapter) { hr = create(d.adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels, 4, D3D11_SDK_VERSION, &d.device, &d.level, &d.context); path = "adapter"; }
        if (FAILED(hr) || !d.device) { hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 4, D3D11_SDK_VERSION, &d.device, &d.level, &d.context); path = "hardware_default"; }
        if (SUCCEEDED(hr) && d.device) { d.flags = flags; break; }
    }
    std::printf("DEVICE api=d3d11 hr=%08lx path=%s flags=%08x feature_level=%s\n", (unsigned long)hr, path, d.flags, d.device ? level_name(d.level) : "-");
    if (FAILED(hr) || !d.device) { d.destroy(); throw Unavailable("D3D11CreateDevice", hr); }
    UINT support = 0; d.bgra = SUCCEEDED(d.device->CheckFormatSupport(DXGI_FORMAT_B8G8R8A8_UNORM, &support)) && (support & D3D11_FORMAT_SUPPORT_RENDER_TARGET);
    if (!d.adapter) { IDXGIDevice* dev = nullptr; IDXGIAdapter* base = nullptr;
        if (SUCCEEDED(d.device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dev))) && dev && SUCCEEDED(dev->GetAdapter(&base)) && base) base->QueryInterface(__uuidof(IDXGIAdapter1), reinterpret_cast<void**>(&d.adapter));
        release(base); release(dev); }
    return d;
}

// --- steps ----------------------------------------------------------------------------
struct Formats { const char* name; D3DFORMAT d3d9; DXGI_FORMAT dxgi; unsigned mode; bool depth; };
const Formats formats[] = {{"A8R8G8B8", D3DFMT_A8R8G8B8, DXGI_FORMAT_B8G8R8A8_UNORM, 0, false}, {"A16B16G16R16F", D3DFMT_A16B16G16R16F, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, false},
                           {"A32B32G32R32F", D3DFMT_A32B32G32R32F, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, false}, {"D24S8", D3DFMT_D24S8, DXGI_FORMAT_D24_UNORM_S8_UINT, 0, true},
                           {"D32F_LOCKABLE", D3DFMT_D32F_LOCKABLE, DXGI_FORMAT_D32_FLOAT, 0, true}};
constexpr unsigned share_w = 256, share_h = 256;

// D3D11-created shared texture opened by a D3D9 device (Ex or plain); rendered on D3D11, read on D3D9.
void share_d3d11_to_d3d9(Device11& d11, Device9& d9, const Formats& f, ID3D11PixelShader* gradient, ID3D11Buffer* constants) {
    const char* dir = d9.ex ? "d3d11_to_d3d9ex" : "d3d11_to_d3d9";
    ID3D11Texture2D* t = nullptr; IDXGIResource* resource = nullptr; HANDLE handle = nullptr; IDirect3DTexture9* tex9 = nullptr; IDirect3DSurface9* ds9 = nullptr;
    HRESULT create_hr = E_FAIL, handle_hr = E_FAIL, open_hr = E_FAIL, mutex_hr = E_FAIL; unsigned mismatches = 0, fx = 0, fy = 0; bool exact = false; double roundtrip = 0, sync = 0; bool synced = false;
    char first_bytes[40] = "-"; int reverse_exact = -1; // reverse: a D3D9 Clear of the opened texture, read through the D3D11 texture
    try {
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = share_w; desc.Height = share_h; desc.MipLevels = 1; desc.ArraySize = 1; desc.Format = f.dxgi; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = f.depth ? D3D11_BIND_DEPTH_STENCIL : (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE); desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
        create_hr = d11.device->CreateTexture2D(&desc, nullptr, &t);
        if (SUCCEEDED(create_hr) && t) {
            handle_hr = t->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void**>(&resource));
            if (SUCCEEDED(handle_hr) && resource) handle_hr = resource->GetSharedHandle(&handle);
            IDXGIKeyedMutex* mutex = nullptr; mutex_hr = t->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void**>(&mutex)); release(mutex);
        }
        if (SUCCEEDED(handle_hr) && handle) {
            HANDLE in = handle;
            open_hr = f.depth ? d9.device->CreateDepthStencilSurface(share_w, share_h, f.d3d9, D3DMULTISAMPLE_NONE, 0, FALSE, &ds9, &in)
                              : d9.device->CreateTexture(share_w, share_h, 1, D3DUSAGE_RENDERTARGET, f.d3d9, D3DPOOL_DEFAULT, &tex9, &in);
        }
        if (SUCCEEDED(open_hr) && tex9 && !f.depth) {
            ID3D11RenderTargetView* view = d11.rtv(t);
            std::vector<double> trips, syncs;
            for (unsigned i = 0; i < 9; ++i) {
                const double t0 = now_us();
                d11.context->PSSetConstantBuffers(0, 1, &constants); d11.fullscreen_draw(view, gradient, share_w, share_h);
                ID3D11RenderTargetView* none = nullptr; d11.context->OMSetRenderTargets(1, &none, nullptr);
                const double s = d11.wait(&synced);
                d9.readback(tex9, f.d3d9, share_w, share_h, [&](const unsigned char* data, unsigned pitch) { mismatches = compare_gradient(f.mode, true, data, pitch, share_w, share_h, &fx, &fy);
                    if (!i) for (unsigned b = 0; b < 16; ++b) std::snprintf(first_bytes + b * 2, 3, "%02x", data[b]); });
                if (i) { trips.push_back(now_us() - t0); syncs.push_back(s); }
            }
            roundtrip = median(trips); sync = median(syncs); exact = mismatches == 0; release(view);
            IDirect3DSurface9* level = nullptr;
            if (SUCCEEDED(tex9->GetSurfaceLevel(0, &level)) && level) {
                IDirect3DSurface9* back = nullptr; d9.device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back);
                if (SUCCEEDED(d9.device->SetRenderTarget(0, level)) && SUCCEEDED(d9.device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0x44, 0x11, 0x22, 0x33), 1.f, 0))) {
                    bool ok = false; d9.wait(&ok);
                    const float expected[4] = {0x11 / 255.f, 0x22 / 255.f, 0x33 / 255.f, 0x44 / 255.f};
                    d11.readback(t, [&](const unsigned char* data, unsigned) {
                        if (f.mode == 0) reverse_exact = data[2] == 0x11 && data[1] == 0x22 && data[0] == 0x33 && data[3] == 0x44;
                        else if (f.mode == 1) { const auto* h = reinterpret_cast<const std::uint16_t*>(data); reverse_exact = 1; for (unsigned c = 0; c < 4; ++c) reverse_exact = reverse_exact && std::fabs(half_to_float(h[c]) - expected[c]) < 1e-3f; }
                        else { const float* v = reinterpret_cast<const float*>(data); reverse_exact = 1; for (unsigned c = 0; c < 4; ++c) reverse_exact = reverse_exact && std::fabs(v[c] - expected[c]) < 1e-3f; }
                    });
                }
                if (back) { d9.device->SetRenderTarget(0, back); release(back); }
                release(level);
            }
        }
    } catch (const Unavailable& error) { std::printf("NOTE share=%s format=%s unavailable=%s hr=%08lx\n", dir, f.name, error.what(), (unsigned long)error.hr); }
    std::printf("SHARE dir=%s format=%s create_hr=%08lx handle_hr=%08lx keyed_mutex_hr=%08lx handle=%p open_hr=%08lx opened=%u exact=%u mismatches=%u first_x=%u first_y=%u first_bytes=%s reverse_exact=%d roundtrip_us=%.1f sync_us=%.1f synced=%u\n",
                dir, f.name, (unsigned long)create_hr, (unsigned long)handle_hr, (unsigned long)mutex_hr, handle, (unsigned long)open_hr, unsigned(tex9 || ds9), unsigned(exact), mismatches, fx, fy, first_bytes, reverse_exact, roundtrip, sync, unsigned(synced));
    // Sharing is a finding (opened / exact / reverse_exact), not a fixture check: a handle that opens without carrying the pixels is the observation itself.
    release(ds9); release(tex9); release(resource); release(t);
}
// D3D9-created shared texture (Ex: handle out; plain: the documented refusal) opened by D3D11; rendered on D3D9, read on D3D11.
void share_d3d9_to_d3d11(Device9& d9, Device11* d11, const Formats& f, IDirect3DPixelShader9* gradient) {
    const char* dir = d9.ex ? "d3d9ex_to_d3d11" : "d3d9_to_d3d11";
    IDirect3DTexture9* tex9 = nullptr; IDirect3DSurface9* ds9 = nullptr; HANDLE handle = nullptr; ID3D11Texture2D* t = nullptr;
    HRESULT create_hr = E_FAIL, open_hr = E_FAIL; unsigned mismatches = 0, fx = 0, fy = 0; bool exact = false, synced = false; double roundtrip = 0, sync = 0;
    try {
        create_hr = f.depth ? d9.device->CreateDepthStencilSurface(share_w, share_h, f.d3d9, D3DMULTISAMPLE_NONE, 0, FALSE, &ds9, &handle)
                            : d9.device->CreateTexture(share_w, share_h, 1, D3DUSAGE_RENDERTARGET, f.d3d9, D3DPOOL_DEFAULT, &tex9, &handle);
        if (SUCCEEDED(create_hr) && handle && d11) open_hr = d11->device->OpenSharedResource(handle, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t));
        if (SUCCEEDED(open_hr) && t && tex9 && gradient) {
            IDirect3DSurface9* level = nullptr; need("GetSurfaceLevel", tex9->GetSurfaceLevel(0, &level));
            const float c0[4] = {float(f.mode), float(f.mode), float(share_w), float(share_h)};
            std::vector<double> trips, syncs;
            for (unsigned i = 0; i < 9; ++i) {
                const double t0 = now_us();
                need("SetRenderTarget", d9.device->SetRenderTarget(0, level)); d9.bind_states(); d9.device->SetPixelShader(gradient); d9.device->SetPixelShaderConstantF(0, c0, 1);
                need("BeginScene", d9.device->BeginScene()); d9.quad(share_w, share_h); need("EndScene", d9.device->EndScene());
                const double s = d9.wait(&synced);
                d11->readback(t, [&](const unsigned char* data, unsigned pitch) { mismatches = compare_gradient(f.mode, f.mode == 0, data, pitch, share_w, share_h, &fx, &fy); });
                if (i) { trips.push_back(now_us() - t0); syncs.push_back(s); }
            }
            roundtrip = median(trips); sync = median(syncs); exact = mismatches == 0; release(level);
        }
    } catch (const Unavailable& error) { std::printf("NOTE share=%s format=%s unavailable=%s hr=%08lx\n", dir, f.name, error.what(), (unsigned long)error.hr); }
    std::printf("SHARE dir=%s format=%s create_hr=%08lx handle=%p open_hr=%08lx opened=%u exact=%u mismatches=%u first_x=%u first_y=%u roundtrip_us=%.1f sync_us=%.1f synced=%u\n",
                dir, f.name, (unsigned long)create_hr, handle, (unsigned long)open_hr, unsigned(t != nullptr), unsigned(exact), mismatches, fx, fy, roundtrip, sync, unsigned(synced));
    release(t); release(ds9); release(tex9);
}

// D3D11 timestamp bracket: ticks between two timestamps around f(), median over repeats; falls back to the event-query CPU bracket when timestamps are unsupported.
template <class F> void time_d3d11(Device11& d, const char* variant, unsigned w, unsigned h, unsigned iterations, F f) {
    std::vector<double> gpu_us, cpu_us; bool disjoint_seen = false; std::uint64_t frequency = 0; const char* method = "timestamp_disjoint";
    D3D11_QUERY_DESC dd{D3D11_QUERY_TIMESTAMP_DISJOINT, 0}, td{D3D11_QUERY_TIMESTAMP, 0};
    ID3D11Query* disjoint = nullptr; ID3D11Query* t0 = nullptr; ID3D11Query* t1 = nullptr;
    if (!d.timestamps || FAILED(d.device->CreateQuery(&dd, &disjoint)) || FAILED(d.device->CreateQuery(&td, &t0)) || FAILED(d.device->CreateQuery(&td, &t1))) { release(disjoint); release(t0); release(t1); d.timestamps = false; method = "event_bracket"; }
    for (unsigned repeat = 0; repeat < 6; ++repeat) {
        bool ok = false; d.wait(&ok); // drain before the bracket
        const double c0 = now_us();
        if (disjoint) { d.context->Begin(disjoint); d.context->End(t0); }
        for (unsigned i = 0; i < iterations; ++i) f();
        if (disjoint) { d.context->End(t1); d.context->End(disjoint); }
        const double waited = d.wait(&ok);
        const double cpu = (now_us() - c0) / iterations;
        if (disjoint) {
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{}; std::uint64_t a = 0, b = 0; const double start = now_us();
            while (d.context->GetData(disjoint, &dj, sizeof dj, 0) != S_OK && now_us() - start < 10e6) {}
            while (d.context->GetData(t0, &a, sizeof a, 0) != S_OK && now_us() - start < 10e6) {}
            while (d.context->GetData(t1, &b, sizeof b, 0) != S_OK && now_us() - start < 10e6) {}
            frequency = dj.Frequency; if (dj.Disjoint) disjoint_seen = true;
            if (repeat && dj.Frequency && !dj.Disjoint && b >= a) gpu_us.push_back(double(b - a) * 1e6 / double(dj.Frequency) / iterations);
        }
        if (repeat) cpu_us.push_back(cpu);
        (void)waited;
    }
    release(disjoint); release(t0); release(t1);
    std::printf("DILATE api=d3d11 variant=%s width=%u height=%u iterations=%u method=%s gpu_us=%.1f cpu_bracket_us=%.1f samples=%u frequency=%llu disjoint=%u\n", variant, w, h, iterations, method,
                gpu_us.empty() ? 0.0 : median(gpu_us), median(cpu_us), unsigned(gpu_us.size()), (unsigned long long)frequency, unsigned(disjoint_seen));
}

void dilation_d3d9(Device9& d, unsigned w, unsigned h, const std::vector<unsigned char>& mask, const std::vector<unsigned char>& ref, const std::vector<unsigned char>& dilate_bytes, const std::vector<unsigned char>& compose_bytes) {
    const char* step = d.ex ? "dilation_d3d9ex" : "dilation_d3d9";
    IDirect3DTexture9* source_sys = nullptr; IDirect3DTexture9* source = nullptr; IDirect3DTexture9* ping = nullptr; IDirect3DTexture9* pong = nullptr; IDirect3DTexture9* out = nullptr;
    IDirect3DPixelShader9* dilate = nullptr; IDirect3DPixelShader9* compose = nullptr;
    try {
        need("CreateTexture_sys", d.device->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &source_sys, nullptr));
        { D3DLOCKED_RECT rect{}; need("LockRect", source_sys->LockRect(0, &rect, nullptr, 0));
          for (unsigned y = 0; y < h; ++y) { unsigned char* row = static_cast<unsigned char*>(rect.pBits) + y * rect.Pitch; for (unsigned x = 0; x < w; ++x) { const unsigned char v = mask[size_t(y) * w + x]; row[x * 4] = v; row[x * 4 + 1] = v; row[x * 4 + 2] = v; row[x * 4 + 3] = 255; } }
          source_sys->UnlockRect(0); }
        need("CreateTexture_source", d.device->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &source, nullptr));
        need("UpdateTexture", d.device->UpdateTexture(source_sys, source));
        need("CreateTexture_ping", d.device->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &ping, nullptr));
        need("CreateTexture_pong", d.device->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pong, nullptr));
        need("CreateTexture_out", d.device->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &out, nullptr));
        dilate = d.shader(dilate_bytes); compose = d.shader(compose_bytes);
        IDirect3DSurface9* ping_s = nullptr; IDirect3DSurface9* pong_s = nullptr; IDirect3DSurface9* out_s = nullptr;
        ping->GetSurfaceLevel(0, &ping_s); pong->GetSurfaceLevel(0, &pong_s); out->GetSurfaceLevel(0, &out_s);
        const float c0[4] = {1.f / w, 1.f / h, float(w), float(h)}, horizontal[4] = {1.f, 0.f, 0.f, 0.f}, vertical[4] = {0.f, 1.f, 0.f, 0.f};
        auto chain = [&] {
            d.device->SetPixelShader(dilate); d.device->SetPixelShaderConstantF(0, c0, 1);
            d.device->SetRenderTarget(0, ping_s); d.device->SetTexture(0, source); d.device->SetPixelShaderConstantF(1, horizontal, 1); d.quad(w, h);
            d.device->SetRenderTarget(0, pong_s); d.device->SetTexture(0, ping); d.device->SetPixelShaderConstantF(1, vertical, 1); d.quad(w, h);
            d.device->SetPixelShader(compose); d.device->SetRenderTarget(0, out_s); d.device->SetTexture(0, pong); d.device->SetTexture(1, source); d.quad(w, h);
            d.device->SetTexture(0, nullptr); d.device->SetTexture(1, nullptr);
        };
        d.bind_states();
        need("BeginScene", d.device->BeginScene()); chain(); need("EndScene", d.device->EndScene());
        bool ok = false; d.wait(&ok);
        unsigned mismatches = 0;
        d.readback(out, D3DFMT_A8R8G8B8, w, h, [&](const unsigned char* data, unsigned pitch) { mismatches = compare_mask(ref, data, pitch, w, h, true); });
        const unsigned iterations = 8; std::vector<double> per_iteration, empty;
        for (unsigned repeat = 0; repeat < 6; ++repeat) {
            d.wait(&ok); double t0 = now_us(); need("BeginScene", d.device->BeginScene()); for (unsigned i = 0; i < iterations; ++i) chain(); need("EndScene", d.device->EndScene()); d.wait(&ok);
            if (repeat) per_iteration.push_back((now_us() - t0) / iterations);
            t0 = now_us(); d.wait(&ok); if (repeat) empty.push_back(now_us() - t0);
        }
        std::printf("DILATE api=%s variant=ps3_three_passes width=%u height=%u iterations=%u method=event_bracket gpu_us=%.1f empty_bracket_us=%.1f mismatches=%u synced=%u\n", d.ex ? "d3d9ex" : "d3d9", w, h, iterations, median(per_iteration), median(empty), mismatches, unsigned(ok));
        char label[96]; std::snprintf(label, sizeof label, "%s_%ux%u_matches_reference", step, w, h); require(label, mismatches == 0);
        release(ping_s); release(pong_s); release(out_s);
        step_ok(step);
    } catch (const Unavailable& error) { step_unavailable(step, error.what(), error.hr); }
    d.device->SetPixelShader(nullptr); d.device->SetTexture(0, nullptr); d.device->SetTexture(1, nullptr);
    IDirect3DSurface9* back = nullptr; if (SUCCEEDED(d.device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) && back) { d.device->SetRenderTarget(0, back); release(back); }
    release(dilate); release(compose); release(out); release(pong); release(ping); release(source); release(source_sys);
}

void dilation_d3d11(Device11& d, unsigned w, unsigned h, const std::vector<unsigned char>& mask, const std::vector<unsigned char>& ref, const std::vector<unsigned char>* dilate_bytes, const std::vector<unsigned char>* compose_bytes, const std::vector<unsigned char>* cs_bytes) {
    std::vector<unsigned char> rgba(size_t(w) * h * 4);
    for (size_t i = 0; i < mask.size(); ++i) { rgba[i * 4] = mask[i]; rgba[i * 4 + 1] = mask[i]; rgba[i * 4 + 2] = mask[i]; rgba[i * 4 + 3] = 255; }
    ID3D11Texture2D* source = nullptr; ID3D11Texture2D* ping = nullptr; ID3D11Texture2D* pong = nullptr; ID3D11Texture2D* out = nullptr;
    ID3D11ShaderResourceView* source_v = nullptr; ID3D11ShaderResourceView* ping_v = nullptr; ID3D11ShaderResourceView* pong_v = nullptr;
    ID3D11RenderTargetView* ping_r = nullptr; ID3D11RenderTargetView* pong_r = nullptr; ID3D11RenderTargetView* out_r = nullptr; ID3D11UnorderedAccessView* out_u = nullptr;
    ID3D11PixelShader* dilate = nullptr; ID3D11PixelShader* compose = nullptr; ID3D11ComputeShader* cs = nullptr;
    ID3D11Buffer* c_h = nullptr; ID3D11Buffer* c_v = nullptr;
    try {
        source = d.texture(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, 0, rgba.data(), w * 4);
        ping = d.texture(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0);
        pong = d.texture(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0);
        out = d.texture(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS, 0);
        source_v = d.srv(source); ping_v = d.srv(ping); pong_v = d.srv(pong); ping_r = d.rtv(ping); pong_r = d.rtv(pong); out_r = d.rtv(out);
        const float ch[8] = {1.f / w, 1.f / h, float(w), float(h), 1.f, 0.f, 0.f, 0.f}, cv[8] = {1.f / w, 1.f / h, float(w), float(h), 0.f, 1.f, 0.f, 0.f};
        c_h = d.constants(ch, 8); c_v = d.constants(cv, 8);
        ID3D11ShaderResourceView* none[2] = {nullptr, nullptr};
        // (c) three pixel passes
        if (dilate_bytes && compose_bytes) {
            try {
                dilate = d.ps(*dilate_bytes); compose = d.ps(*compose_bytes);
                ID3D11RenderTargetView* no_rt = nullptr;
                auto unbind = [&] { d.context->OMSetRenderTargets(1, &no_rt, nullptr); d.context->PSSetShaderResources(0, 2, none); }; // a resource still bound as RT is forced to NULL when bound as SRV
                auto chain = [&] {
                    unbind(); d.context->PSSetConstantBuffers(0, 1, &c_h); d.context->PSSetShaderResources(0, 1, &source_v); d.fullscreen_draw(ping_r, dilate, w, h);
                    unbind(); d.context->PSSetConstantBuffers(0, 1, &c_v); d.context->PSSetShaderResources(0, 1, &ping_v); d.fullscreen_draw(pong_r, dilate, w, h);
                    unbind(); ID3D11ShaderResourceView* both[2] = {pong_v, source_v}; d.context->PSSetShaderResources(0, 2, both); d.fullscreen_draw(out_r, compose, w, h);
                    unbind();
                };
                chain(); bool ok = false; d.wait(&ok); unsigned mismatches = 0;
                d.readback(out, [&](const unsigned char* data, unsigned pitch) { mismatches = compare_mask(ref, data, pitch, w, h, false); });
                time_d3d11(d, "ps_three_passes", w, h, 8, chain);
                std::printf("VERIFY api=d3d11 variant=ps_three_passes width=%u height=%u mismatches=%u\n", w, h, mismatches);
                char label[96]; std::snprintf(label, sizeof label, "dilation_d3d11_ps_%ux%u_matches_reference", w, h); require(label, mismatches == 0);
            } catch (const Unavailable& error) { std::printf("DILATE api=d3d11 variant=ps_three_passes width=%u height=%u status=unavailable reason=%s hr=%08lx\n", w, h, error.what(), (unsigned long)error.hr); ++unavailable; }
        } else { std::printf("DILATE api=d3d11 variant=ps_three_passes width=%u height=%u status=unavailable reason=shader_compile\n", w, h); ++unavailable; }
        // (b) one compute dispatch
        if (cs_bytes && d.level >= D3D_FEATURE_LEVEL_11_0) {
            try {
                cs = d.cs(*cs_bytes); out_u = d.uav(out);
                const UINT gx = (w + 15) / 16, gy = (h + 15) / 16;
                auto dispatch = [&] {
                    ID3D11UnorderedAccessView* no_uav = nullptr; ID3D11RenderTargetView* no_rt = nullptr; d.context->OMSetRenderTargets(1, &no_rt, nullptr);
                    d.context->CSSetShader(cs, nullptr, 0); d.context->CSSetConstantBuffers(0, 1, &c_h); d.context->CSSetShaderResources(0, 1, &source_v); d.context->CSSetUnorderedAccessViews(0, 1, &out_u, nullptr);
                    d.context->Dispatch(gx, gy, 1);
                    d.context->CSSetUnorderedAccessViews(0, 1, &no_uav, nullptr); d.context->CSSetShaderResources(0, 1, none);
                };
                const float clear[4] = {0.f, 0.f, 0.f, 0.f}; d.context->ClearRenderTargetView(out_r, clear);
                dispatch(); bool ok = false; d.wait(&ok); unsigned mismatches = 0;
                d.readback(out, [&](const unsigned char* data, unsigned pitch) { mismatches = compare_mask(ref, data, pitch, w, h, false); });
                time_d3d11(d, "cs5_one_dispatch", w, h, 8, dispatch);
                std::printf("VERIFY api=d3d11 variant=cs5_one_dispatch width=%u height=%u groups=%ux%u mismatches=%u\n", w, h, gx, gy, mismatches);
                char label[96]; std::snprintf(label, sizeof label, "dilation_d3d11_cs_%ux%u_matches_reference", w, h); require(label, mismatches == 0);
            } catch (const Unavailable& error) { std::printf("DILATE api=d3d11 variant=cs5_one_dispatch width=%u height=%u status=unavailable reason=%s hr=%08lx\n", w, h, error.what(), (unsigned long)error.hr); ++unavailable; }
        } else { std::printf("DILATE api=d3d11 variant=cs5_one_dispatch width=%u height=%u status=unavailable reason=%s\n", w, h, cs_bytes ? "feature_level_below_11_0" : "shader_compile"); ++unavailable; }
    } catch (const Unavailable& error) { std::printf("DILATE api=d3d11 variant=all width=%u height=%u status=unavailable reason=%s hr=%08lx\n", w, h, error.what(), (unsigned long)error.hr); ++unavailable; }
    d.context->ClearState();
    release(c_h); release(c_v); release(cs); release(compose); release(dilate); release(out_u); release(out_r); release(pong_r); release(ping_r); release(pong_v); release(ping_v); release(source_v); release(out); release(pong); release(ping); release(source);
}

const char* colour_space_name(DXGI_COLOR_SPACE_TYPE c) {
    switch (c) { case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709: return "RGB_FULL_G22_NONE_P709"; case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709: return "RGB_FULL_G10_NONE_P709";
                 case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020: return "RGB_FULL_G2084_NONE_P2020"; default: return "other"; }
}
void hdr_outputs(IDXGIAdapter1* adapter) {
    if (!adapter) { step_unavailable("hdr_outputs", "no_adapter"); return; }
    IDXGIOutput* output = nullptr; unsigned count = 0;
    for (UINT i = 0; adapter->EnumOutputs(i, &output) == S_OK; ++i, ++count) {
        IDXGIOutput6* six = nullptr; DXGI_OUTPUT_DESC desc{}; output->GetDesc(&desc);
        if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), reinterpret_cast<void**>(&six))) && six) {
            DXGI_OUTPUT_DESC1 d1{}; const HRESULT hr = six->GetDesc1(&d1);
            std::printf("OUTPUT index=%u name=%s desc1_hr=%08lx colour_space=%s(%d) bits_per_colour=%u min_nits=%.3f max_nits=%.1f max_full_frame_nits=%.1f width=%ld height=%ld\n", i, token(desc.DeviceName).c_str(), (unsigned long)hr,
                        colour_space_name(d1.ColorSpace), int(d1.ColorSpace), d1.BitsPerColor, d1.MinLuminance, d1.MaxLuminance, d1.MaxFullFrameLuminance, long(d1.DesktopCoordinates.right - d1.DesktopCoordinates.left), long(d1.DesktopCoordinates.bottom - d1.DesktopCoordinates.top));
            release(six);
        } else std::printf("OUTPUT index=%u name=%s desc1_hr=unavailable output6=0\n", i, token(desc.DeviceName).c_str());
        release(output);
    }
    if (count) step_ok("hdr_outputs"); else step_unavailable("hdr_outputs", "no_outputs");
}
void hdr_swapchain(Device11& d, HMODULE dxgi, HWND window) {
    IDXGIFactory2* factory2 = nullptr; IDXGISwapChain1* chain = nullptr;
    try {
        if (!d.factory) throw Unavailable("no_factory");
        need("IDXGIFactory2", d.factory->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory2)));
        struct Attempt { DXGI_SWAP_EFFECT effect; UINT buffers; DXGI_FORMAT format; const char* name; };
        const Attempt attempts[] = {{DXGI_SWAP_EFFECT_FLIP_DISCARD, 2, DXGI_FORMAT_R16G16B16A16_FLOAT, "flip_discard_fp16"}, {DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, 2, DXGI_FORMAT_R16G16B16A16_FLOAT, "flip_sequential_fp16"},
                                    {DXGI_SWAP_EFFECT_FLIP_DISCARD, 2, DXGI_FORMAT_R10G10B10A2_UNORM, "flip_discard_rgb10"}, {DXGI_SWAP_EFFECT_FLIP_DISCARD, 2, DXGI_FORMAT_B8G8R8A8_UNORM, "flip_discard_bgra8"},
                                    {DXGI_SWAP_EFFECT_DISCARD, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, "bitblt_discard_fp16"}, {DXGI_SWAP_EFFECT_DISCARD, 1, DXGI_FORMAT_B8G8R8A8_UNORM, "bitblt_discard_bgra8"}};
        HRESULT hr = E_FAIL; const char* accepted = "-";
        for (const Attempt& a : attempts) {
            DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width = 256; desc.Height = 256; desc.Format = a.format; desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = a.buffers; desc.Scaling = DXGI_SCALING_STRETCH; desc.SwapEffect = a.effect; desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            hr = factory2->CreateSwapChainForHwnd(d.device, window, &desc, nullptr, nullptr, &chain);
            std::printf("SWAPCHAIN attempt=%s hr=%08lx\n", a.name, (unsigned long)hr);
            if (SUCCEEDED(hr) && chain) { accepted = a.name; break; } release(chain);
        }
        if (!chain) throw Unavailable("CreateSwapChainForHwnd", hr);
        factory2->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES); // never fullscreen
        IDXGISwapChain3* three = nullptr; const HRESULT three_hr = chain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&three));
        std::string spaces;
        for (DXGI_COLOR_SPACE_TYPE space : {DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020}) {
            UINT support = 0; HRESULT s_hr = three ? three->CheckColorSpaceSupport(space, &support) : three_hr;
            std::printf("COLORSPACE space=%s hr=%08lx support=%u present=%u overlay=%u\n", colour_space_name(space), (unsigned long)s_hr, support, unsigned((support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0), unsigned((support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_OVERLAY_PRESENT) != 0));
        }
        // Actual write and the single present: clear the back buffer and present once (windowed, hidden window; DXGI_STATUS_OCCLUDED is a success code).
        ID3D11Texture2D* back = nullptr; need("GetBuffer", chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)));
        ID3D11RenderTargetView* view = d.rtv(back); const float colour[4] = {0.25f, 0.5f, 1.0f, 1.f}; d.context->ClearRenderTargetView(view, colour); release(view); release(back);
        const HRESULT present_hr = chain->Present(0, 0);
        HRESULT set_hr = E_FAIL; UINT g10 = 0;
        if (three && SUCCEEDED(three->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &g10)) && (g10 & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) set_hr = three->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
        IDXGIOutput* containing = nullptr; const HRESULT output_hr = chain->GetContainingOutput(&containing); release(containing);
        BOOL tearing = FALSE; IDXGIFactory5* five = nullptr; HRESULT tearing_hr = d.factory->QueryInterface(__uuidof(IDXGIFactory5), reinterpret_cast<void**>(&five));
        if (five) { tearing_hr = five->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof tearing); release(five); }
        (void)dxgi;
        std::printf("HDR swapchain=%s swapchain3_hr=%08lx present_hr=%08lx set_colorspace_g10_hr=%08lx containing_output_hr=%08lx tearing_hr=%08lx tearing=%u\n", accepted, (unsigned long)three_hr, (unsigned long)present_hr, (unsigned long)set_hr, (unsigned long)output_hr, (unsigned long)tearing_hr, unsigned(tearing));
        release(three);
        step_ok("hdr_swapchain");
    } catch (const Unavailable& error) { step_unavailable("hdr_swapchain", error.what(), error.hr); }
    release(chain); release(factory2);
}
void caps(Device11& d) {
    D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS x{}; D3D11_FEATURE_DATA_THREADING threading{}; D3D11_FEATURE_DATA_DOUBLES doubles{};
    D3D11_FEATURE_DATA_D3D11_OPTIONS o1{}; D3D11_FEATURE_DATA_D3D11_OPTIONS2 o2{}; D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT precision{};
    const HRESULT hx = d.device->CheckFeatureSupport(D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS, &x, sizeof x), ht = d.device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &threading, sizeof threading);
    const HRESULT hd = d.device->CheckFeatureSupport(D3D11_FEATURE_DOUBLES, &doubles, sizeof doubles), h1 = d.device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &o1, sizeof o1);
    const HRESULT h2 = d.device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &o2, sizeof o2), hp = d.device->CheckFeatureSupport(D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT, &precision, sizeof precision);
    const bool fl11 = d.level >= D3D_FEATURE_LEVEL_11_0;
    // Group-shared and thread-group limits are fixed by the feature level (documented constants, not queried).
    std::printf("CAPS feature_level=%s compute_shaders=%u compute_via_4x_hr=%08lx compute_via_4x=%u tgsm_bytes=%u max_threads_per_group=%u max_group_dim=%u threading_hr=%08lx driver_command_lists=%u driver_concurrent_creates=%u doubles_hr=%08lx doubles=%u "
                "options_hr=%08lx map_no_overwrite_cb=%u uav_only_rendering_forced_sample_count=%u options2_hr=%08lx typed_uav_load_additional=%u ps_specified_stencil_ref=%u min_precision_hr=%08lx ps_min_precision=%u all_other_min_precision=%u\n",
                level_name(d.level), unsigned(fl11 || x.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x), (unsigned long)hx, unsigned(x.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x), fl11 ? 32768u : 16384u, fl11 ? 1024u : 768u, fl11 ? 65535u : 65535u,
                (unsigned long)ht, unsigned(threading.DriverCommandLists), unsigned(threading.DriverConcurrentCreates), (unsigned long)hd, unsigned(doubles.DoublePrecisionFloatShaderOps), (unsigned long)h1, unsigned(o1.MapNoOverwriteOnDynamicConstantBuffer),
                unsigned(o1.UAVOnlyRenderingForcedSampleCount), (unsigned long)h2, unsigned(o2.TypedUAVLoadAdditionalFormats), unsigned(o2.PSSpecifiedStencilRefSupported), (unsigned long)hp, precision.PixelShaderMinPrecision, precision.AllOtherShaderStagesMinPrecision);
    struct Fmt { const char* name; DXGI_FORMAT format; };
    const Fmt list[] = {{"R16G16B16A16_FLOAT", DXGI_FORMAT_R16G16B16A16_FLOAT}, {"R8_UNORM", DXGI_FORMAT_R8_UNORM}, {"R8G8B8A8_UNORM", DXGI_FORMAT_R8G8B8A8_UNORM}, {"B8G8R8A8_UNORM", DXGI_FORMAT_B8G8R8A8_UNORM}, {"R32_FLOAT", DXGI_FORMAT_R32_FLOAT},
                        {"R16G16_FLOAT", DXGI_FORMAT_R16G16_FLOAT}, {"R11G11B10_FLOAT", DXGI_FORMAT_R11G11B10_FLOAT}, {"R10G10B10A2_UNORM", DXGI_FORMAT_R10G10B10A2_UNORM}, {"R32G32B32A32_FLOAT", DXGI_FORMAT_R32G32B32A32_FLOAT}, {"D24_UNORM_S8_UINT", DXGI_FORMAT_D24_UNORM_S8_UINT}, {"D32_FLOAT", DXGI_FORMAT_D32_FLOAT}};
    for (const Fmt& f : list) {
        UINT s1 = 0; D3D11_FEATURE_DATA_FORMAT_SUPPORT2 s2{f.format, 0};
        const HRESULT a = d.device->CheckFormatSupport(f.format, &s1), b = d.device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &s2, sizeof s2);
        std::printf("FORMAT name=%s hr=%08lx texture2d=%u render_target=%u depth_stencil=%u shader_sample=%u typed_uav=%u display=%u support2_hr=%08lx uav_typed_load=%u uav_typed_store=%u\n", f.name, (unsigned long)a,
                    unsigned((s1 & D3D11_FORMAT_SUPPORT_TEXTURE2D) != 0), unsigned((s1 & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0), unsigned((s1 & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL) != 0), unsigned((s1 & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) != 0),
                    unsigned((s1 & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) != 0), unsigned((s1 & D3D11_FORMAT_SUPPORT_DISPLAY) != 0), (unsigned long)b, unsigned((s2.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0), unsigned((s2.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0));
    }
    step_ok("caps");
}
} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QueryPerformanceFrequency(&qpf);
    WNDCLASSA cls{}; cls.lpfnWndProc = DefWindowProcA; cls.hInstance = GetModuleHandleA(nullptr); cls.lpszClassName = "X3D3D11InteropFixture"; RegisterClassA(&cls);
    HWND window9 = CreateWindowA(cls.lpszClassName, "X3 d3d11 interop fixture (d3d9)", WS_OVERLAPPEDWINDOW, 90, 90, 320, 240, nullptr, nullptr, cls.hInstance, nullptr);
    HWND window11 = CreateWindowA(cls.lpszClassName, "X3 d3d11 interop fixture (dxgi)", WS_OVERLAPPEDWINDOW, 120, 120, 320, 240, nullptr, nullptr, cls.hInstance, nullptr);
    if (!window9 || !window11) { std::printf("RESULT checks=0 failures=1 path=exception error=window FAIL\n"); return 2; }
    // 1. provenance
    HMODULE d3d9 = report_module("d3d9.dll"), d3d11 = report_module("d3d11.dll"), dxgi = report_module("dxgi.dll"), compiler = report_module("d3dcompiler_47.dll");
    if (compiler) { auto address = GetProcAddress(compiler, "D3DCompile"); std::memcpy(&compile_fn, &address, sizeof compile_fn); }
    std::printf("COMPILER available=%u\n", unsigned(compile_fn != nullptr));
    // devices
    Device9 plain{}, ex{}; Device11 d11{}; bool have_plain = false, have_ex = false, have_11 = false; DWORD vendor = 0, device_id = 0;
    if (!d3d9) step_unavailable("device_d3d9", "d3d9.dll");
    else { try { plain = create_device9(d3d9, window9, false); have_plain = true; step_ok("device_d3d9"); } catch (const Unavailable& e) { step_unavailable("device_d3d9", e.what(), e.hr); }
           try { ex = create_device9(d3d9, window9, true); have_ex = true; step_ok("device_d3d9ex"); } catch (const Unavailable& e) { step_unavailable("device_d3d9ex", e.what(), e.hr); } }
    require("d3d9_plain_device_created", have_plain);
    if (have_plain) { D3DADAPTER_IDENTIFIER9 ident{}; plain.api->GetAdapterIdentifier(0, 0, &ident); vendor = ident.VendorId; device_id = ident.DeviceId; }
    if (!d3d11) step_unavailable("device_d3d11", "d3d11.dll");
    else { try { d11 = create_device11(d3d11, dxgi, vendor, device_id); have_11 = true; step_ok("device_d3d11"); } catch (const Unavailable& e) { step_unavailable("device_d3d11", e.what(), e.hr); } }
    // shaders
    std::vector<unsigned char> ps9_gradient, ps9_dilate, ps9_compose, vs11, ps11_gradient, ps11_dilate, ps11_compose, cs11;
    auto try_compile = [&](std::vector<unsigned char>& out, const char* name, const std::string& source, const char* target) { try { out = compile(name, source.c_str(), target); } catch (const Unavailable& e) { std::printf("NOTE shader=%s unavailable=%s\n", name, e.what()); } };
    bool vpos = true; // VPOS first; a compiler without it gets the TEXCOORD form (floor(uv * size) is the same pixel index)
    try_compile(ps9_gradient, "gradient_ps3_vpos", gradient_ps9(true), "ps_3_0");
    if (ps9_gradient.empty()) { vpos = false; try_compile(ps9_gradient, "gradient_ps3_texcoord", gradient_ps9(false), "ps_3_0"); }
    try_compile(ps9_dilate, vpos ? "dilate_ps3_vpos" : "dilate_ps3_texcoord", dilate_ps9(vpos), "ps_3_0"); try_compile(ps9_compose, vpos ? "compose_ps3_vpos" : "compose_ps3_texcoord", compose_ps9(vpos), "ps_3_0");
    std::printf("PS3 pixel_position=%s\n", vpos ? "vpos" : "texcoord");
    if (have_11) {
        const char* ps_target = d11.level >= D3D_FEATURE_LEVEL_11_0 ? "ps_5_0" : "ps_4_0"; const char* vs_target = d11.level >= D3D_FEATURE_LEVEL_11_0 ? "vs_5_0" : "vs_4_0";
        try_compile(vs11, "fullscreen_vs", fullscreen_vs11, vs_target); try_compile(ps11_gradient, "gradient_ps", gradient_ps11(), ps_target);
        try_compile(ps11_dilate, "dilate_ps", dilate_ps11, ps_target); try_compile(ps11_compose, "compose_ps", compose_ps11, ps_target); try_compile(cs11, "dilate_cs", dilate_cs11, "cs_5_0");
        if (!vs11.empty()) { if (FAILED(d11.device->CreateVertexShader(vs11.data(), vs11.size(), nullptr, &d11.fullscreen))) { std::printf("NOTE d3d11 CreateVertexShader failed\n"); vs11.clear(); } }
    }
    // 2. sharing
    IDirect3DPixelShader9* gradient_plain = nullptr; IDirect3DPixelShader9* gradient_ex = nullptr;
    if (have_plain && !ps9_gradient.empty()) plain.device->CreatePixelShader(reinterpret_cast<const DWORD*>(ps9_gradient.data()), &gradient_plain);
    if (have_ex && !ps9_gradient.empty()) ex.device->CreatePixelShader(reinterpret_cast<const DWORD*>(ps9_gradient.data()), &gradient_ex);
    if (have_plain) { IDirect3DDevice9Ex* as_ex = nullptr; const HRESULT hr = plain.device->QueryInterface(__uuidof(IDirect3DDevice9Ex), reinterpret_cast<void**>(&as_ex)); release(as_ex); std::printf("PLAIN query_device9ex_hr=%08lx\n", (unsigned long)hr); }
    if (have_11 && !ps11_gradient.empty() && d11.fullscreen) {
        ID3D11PixelShader* gradient11 = nullptr;
        try {
            gradient11 = d11.ps(ps11_gradient);
            for (const Formats& f : formats) {
                const float c0[4] = {float(f.mode), float(f.mode), float(share_w), float(share_h)}; ID3D11Buffer* constants = d11.constants(c0, 4);
                if (have_ex) share_d3d11_to_d3d9(d11, ex, f, gradient11, constants);
                if (have_plain) share_d3d11_to_d3d9(d11, plain, f, gradient11, constants);
                release(constants);
            }
            step_ok("share_d3d11_to_d3d9");
        } catch (const Unavailable& e) { step_unavailable("share_d3d11_to_d3d9", e.what(), e.hr); }
        release(gradient11);
    } else step_unavailable("share_d3d11_to_d3d9", have_11 ? "d3d11_shaders" : "d3d11_device");
    for (const Formats& f : formats) { if (have_ex) share_d3d9_to_d3d11(ex, have_11 ? &d11 : nullptr, f, gradient_ex); if (have_plain) share_d3d9_to_d3d11(plain, have_11 ? &d11 : nullptr, f, gradient_plain); }
    if (have_ex || have_plain) step_ok("share_d3d9_to_d3d11"); else step_unavailable("share_d3d9_to_d3d11", "d3d9_devices");
    // Keyed mutex availability on D3D11 alone (D3D9 has no keyed-mutex API; reported for the D3D11-internal sync option).
    if (have_11) {
        ID3D11Texture2D* t = nullptr; IDXGIKeyedMutex* mutex = nullptr; HRESULT qi = E_FAIL, acquire = E_FAIL;
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = 16; desc.Height = 16; desc.MipLevels = 1; desc.ArraySize = 1; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_RENDER_TARGET; desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        const HRESULT hr = d11.device->CreateTexture2D(&desc, nullptr, &t);
        if (t) { qi = t->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void**>(&mutex)); if (mutex) { acquire = mutex->AcquireSync(0, 100); if (SUCCEEDED(acquire)) mutex->ReleaseSync(0); } }
        std::printf("KEYEDMUTEX create_hr=%08lx query_hr=%08lx acquire_hr=%08lx\n", (unsigned long)hr, (unsigned long)qi, (unsigned long)acquire);
        release(mutex); release(t);
    }
    // 3. dilation
    for (const auto size : {std::pair<unsigned, unsigned>{1920, 1080}, std::pair<unsigned, unsigned>{5120, 1440}}) {
        const unsigned w = size.first, h = size.second;
        const double t0 = now_us(); const std::vector<unsigned char> mask = make_mask(w, h), ref = reference(mask, w, h);
        std::printf("REFERENCE width=%u height=%u cpu_us=%.0f\n", w, h, now_us() - t0);
        if (have_plain && !ps9_dilate.empty() && !ps9_compose.empty()) dilation_d3d9(plain, w, h, mask, ref, ps9_dilate, ps9_compose); else step_unavailable("dilation_d3d9", have_plain ? "shader_compile" : "d3d9_device");
        if (have_11 && d11.fullscreen) dilation_d3d11(d11, w, h, mask, ref, ps11_dilate.empty() ? nullptr : &ps11_dilate, ps11_compose.empty() ? nullptr : &ps11_compose, cs11.empty() ? nullptr : &cs11);
        else step_unavailable("dilation_d3d11", have_11 ? "vertex_shader" : "d3d11_device");
    }
    // 4. HDR
    if (have_11) { hdr_outputs(d11.adapter); hdr_swapchain(d11, dxgi, window11); } else { step_unavailable("hdr_outputs", "d3d11_device"); step_unavailable("hdr_swapchain", "d3d11_device"); }
    // 5. caps
    if (have_11) caps(d11); else step_unavailable("caps", "d3d11_device");
    release(gradient_plain); release(gradient_ex);
    if (have_11) d11.destroy();
    if (have_ex) ex.destroy();
    if (have_plain) plain.destroy();
    DestroyWindow(window11); DestroyWindow(window9);
    std::printf("RESULT checks=%u failures=%u unavailable=%u path=measured %s\n", checks, failures, unavailable, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
