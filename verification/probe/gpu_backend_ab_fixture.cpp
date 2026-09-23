// GPU backend A/B fixture (docs/architecture/d3d9-to-d3d11-translation.md, Decision): the same
// GPU work on D3D9 (the bottle's builtin d3d9, wined3d) and on D3D11 (the bottle's d3d11/dxgi,
// DXMT on this bottle), one API after the other in one process, same vertex data, same HLSL
// compiled for vs_3_0/ps_3_0 and vs_4_0/ps_4_0 through d3dcompiler_47.
//   empty           the bracket with no work (its floor);
//   fullscreen_pass one quad into an offscreen A16B16G16R16F target: 4 bilinear taps of an FP16
//                   source and a 3x3 ALU loop over them (post-chain-like), 1920x1080 and 5120x1440;
//   scene_460       460 indexed draws of 1,012 triangles (16 VB+IB INDEX16 meshes, D3D9 MANAGED /
//                   D3D11 IMMUTABLE), one of 4 A8R8G8B8 mip-mapped textures, WVP vs and a
//                   texture x diffuse ps, X8R8G8B8 + D24S8, depth test on, alpha test on one
//                   draw in five (D3D9 ALPHATESTENABLE; D3D11 a clip() ps variant); per draw
//                   the census pattern: 32 SetRenderState + 27 SetSamplerState (1.3 value
//                   changes), 4.5 SetTexture, VS/PS bind, 47 vs + 36 ps float4 constants
//                   (D3D11: a translator's shadow compare, state objects bound on change, one
//                   cbuffer Map WRITE_DISCARD per draw), 1920x1080 and 5120x1440;
//   scene_460_cpu   the same with 2 triangles per draw (submission cost), 1920x1080.
// Timing, per iteration (30 warm-up, 300 measured): record CPU time, issue the work (submit_us
// ends here), issue an event query (D3D11: End + Flush), spin GetData (D3D9 with FLUSH) until
// the GPU passed it: event_us. Timestamp-disjoint brackets around the same work on D3D11 and,
// when the device accepts the query types, on D3D9: timestamp_us. Thread and process CPU times
// over the measured iterations (Wine-reported; on Wine the process figure equals the thread figure at a
// 10 ms tick, so only the raw log keeps them). Then the same work 300 times back to back with a
// Present-like flush per iteration and one drain: pipelined_us (throughput). Each workload is read back once and summarised
// (coverage, channel means) so the runner can check both APIs drew the same image.
// Built by CMake (target gpu_backend_ab_fixture); run through
// verification/probe/run_gpu_backend_ab.py under wine_lock.py. Never launches the game; no
// Present, no fullscreen; the device windows stay hidden.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi.h>
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
unsigned checks = 0, failures = 0;
bool require(const char* label, bool value) { ++checks; if (!value) ++failures; std::printf("CHECK %s %s\n", label, value ? "PASS" : "FAIL"); return value; }
struct Unavailable : std::runtime_error { HRESULT hr; Unavailable(const char* what, HRESULT h = S_OK) : std::runtime_error(what), hr(h) {} };
void need(const char* what, HRESULT hr) { if (FAILED(hr)) throw Unavailable(what, hr); }
template <class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }
std::string token(const char* text) { std::string s = text ? text : ""; for (char& c : s) if (c == ' ' || c == '\t' || c == '\n' || c == '\r') c = '_'; if (s.empty()) s = "-"; return s.substr(0, 120); }

LARGE_INTEGER qpf;
double now_us() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return double(t.QuadPart) * 1e6 / double(qpf.QuadPart); }
double quantile(std::vector<double> v, double q) { if (v.empty()) return 0; std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, size_t(q * double(v.size() - 1) + 0.5))]; }
double filetime_us(const FILETIME& f) { return double((std::uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 10.0; }
double thread_cpu_us() { FILETIME c, e, k, u; return GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u) ? filetime_us(k) + filetime_us(u) : -1; }
double process_cpu_us() { FILETIME c, e, k, u; return GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u) ? filetime_us(k) + filetime_us(u) : -1; }

constexpr unsigned kWarmup = 30, kFrames = 300;
constexpr unsigned kDraws = 460, kMeshes = 16, kTextures = 4, kTextureSize = 256, kTextureLevels = 9;
constexpr unsigned kGridX = 23, kGridY = 22, kVertices = (kGridX + 1) * (kGridY + 1), kTriangles = kGridX * kGridY * 2; // 552 vertices, 1,012 triangles
constexpr unsigned kVsConstants = 47, kPsConstants = 36, kRenderStates = 32, kSamplerStates = 27;
constexpr D3DCOLOR kClear = 0xff203040;

// --- module provenance ------------------------------------------------------------------
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
    if (HRSRC res = FindResourceA(m, MAKEINTRESOURCEA(1), MAKEINTRESOURCEA(16))) {
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
    std::string markers;
    for (const char* marker : {"DXMT", "winemetal", "D3DMetal", "wined3d", "vkd3d", "DXVK", "MoltenVK"})
        if (find_marker(static_cast<const unsigned char*>(static_cast<void*>(m)), image_size, marker)) markers += (markers.empty() ? "" : ",") + std::string(marker);
    std::printf("MODULE name=%s loaded=1 path=%s image_size=%lu version=%s product=%s builtin=%u markers=%s\n", name, token(path).c_str(), (unsigned long)image_size,
                version.c_str(), product.c_str(), unsigned(builtin), markers.empty() ? "none" : markers.c_str());
    return m;
}
// Wine's per-user Direct3D renderer override (absent: wined3d's default renderer; absent on Windows).
void report_renderer_key() {
    char value[64] = {}; DWORD size = sizeof value; HKEY key = nullptr;
    LONG open = RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\Direct3D", 0, KEY_READ, &key), query = ERROR_FILE_NOT_FOUND;
    if (open == ERROR_SUCCESS) { DWORD type = 0; query = RegQueryValueExA(key, "renderer", nullptr, &type, reinterpret_cast<BYTE*>(value), &size); RegCloseKey(key); }
    std::printf("RENDERER key_open=%ld renderer=%s\n", open, query == ERROR_SUCCESS ? token(value).c_str() : "absent");
}

// --- shaders ---------------------------------------------------------------------------------
pD3DCompile compile_fn = nullptr;
std::vector<unsigned char> compile(const char* name, const char* source, const char* entry, const char* target) {
    if (!compile_fn) throw Unavailable("d3dcompiler_47");
    const bool sm4 = target[3] == '4', vs = target[0] == 'v';
    D3D_SHADER_MACRO macros[3] = {}; unsigned n = 0;
    if (sm4) macros[n++] = {"SM4", "1"};
    if (vs) macros[n++] = {"VS", "1"};
    ID3DBlob* code = nullptr; ID3DBlob* errors = nullptr;
    const HRESULT hr = compile_fn(source, std::strlen(source), name, macros, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    std::string message = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "";
    std::printf("SHADER name=%s entry=%s target=%s hr=%08lx bytes=%lu message=%s\n", name, entry, target, (unsigned long)hr, code ? (unsigned long)code->GetBufferSize() : 0ul, token(message.substr(0, 160).c_str()).c_str());
    release(errors);
    if (FAILED(hr) || !code) throw Unavailable("compile", hr);
    std::vector<unsigned char> bytes(static_cast<const unsigned char*>(code->GetBufferPointer()), static_cast<const unsigned char*>(code->GetBufferPointer()) + code->GetBufferSize());
    release(code);
    return bytes;
}

// One source per workload; SM4 selects the D3D10+ spelling, VS the vertex stage's D3D9 register set.
#define SHADER_PRELUDE                                                                                        \
    "#if SM4\n"                                                                                               \
    "#define POS_OUT SV_Position\n#define TARGET SV_Target\n"                                                 \
    "#else\n"                                                                                                 \
    "#define POS_OUT POSITION\n#define TARGET COLOR\n"                                                        \
    "#endif\n"
const char* post_hlsl = SHADER_PRELUDE
    "#if SM4\n"
    "cbuffer C : register(b0) { float4 vc[4]; float4 pc[4]; };\n"
    "Texture2D src_tex : register(t0); SamplerState src_smp : register(s0);\n"
    "#define TAP(uv) src_tex.Sample(src_smp, uv)\n"
    "#define PS_IN float4 pos : SV_Position, float2 uv : TEXCOORD0\n"
    "#elif VS\n"
    "float4 vc[4] : register(c0);\n"
    "#else\n"
    "float4 pc[4] : register(c0); sampler2D src_smp : register(s0);\n"
    "#define TAP(uv) tex2D(src_smp, uv)\n"
    "#define PS_IN float2 uv : TEXCOORD0\n"
    "#endif\n"
    "struct V { float4 pos : POS_OUT; float2 uv : TEXCOORD0; };\n"
    "#if SM4 || VS\n"
    "V vs_main(float2 p : POSITION, float2 uv : TEXCOORD0) { V o; o.pos = float4(p + vc[0].xy, 0.0, 1.0); o.uv = uv; return o; }\n"
    "#endif\n"
    "#if SM4 || !VS\n"
    "float4 ps_fill(PS_IN) : TARGET { float2 px = floor(uv * pc[0].zw); return float4(frac(px.x * 0.013) * 4.0, frac(px.y * 0.021) * 4.0, frac((px.x + px.y) * 0.007) * 4.0, 1.0); }\n"
    "#define K(X, Y) { float2 o = float2(X, Y); float w = exp2(-dot(o, o) * pc[1].x); float4 v = lerp(lo, hi, frac(dot(o, pc[1].yz) + m.x * pc[1].w)); acc += v * w; wsum += w; }\n"
    "float4 ps_post(PS_IN) : TARGET {\n"
    "  float2 t = pc[0].xy;\n"
    "  float4 a = TAP(uv + float2(-0.5, -0.5) * t), b = TAP(uv + float2(0.5, -0.5) * t), c = TAP(uv + float2(-0.5, 0.5) * t), d = TAP(uv + float2(0.5, 0.5) * t);\n"
    "  float4 m = (a + b + c + d) * 0.25, lo = min(min(a, b), min(c, d)), hi = max(max(a, b), max(c, d));\n"
    "  float4 acc = float4(0.0, 0.0, 0.0, 0.0); float wsum = 0.0;\n"
    "  K(-1, -1) K(0, -1) K(1, -1) K(-1, 0) K(0, 0) K(1, 0) K(-1, 1) K(0, 1) K(1, 1)\n"
    "  float4 r = acc / wsum; r.rgb = r.rgb / (1.0 + dot(r.rgb, float3(0.2126, 0.7152, 0.0722)));\n"
    "  return float4(r.rgb, m.a);\n}\n"
    "#endif\n";
const char* scene_hlsl = SHADER_PRELUDE
    "#if SM4\n"
    "cbuffer C : register(b0) { float4 vc[47]; float4 pc[36]; };\n"
    "Texture2D tex0 : register(t0); SamplerState smp0 : register(s0);\n"
    "#define TAP(uv) tex0.Sample(smp0, uv)\n"
    "#define PS_IN float4 pos : SV_Position, float2 uv : TEXCOORD0, float3 n : TEXCOORD1\n"
    "#elif VS\n"
    "float4 vc[47] : register(c0);\n"
    "#else\n"
    "float4 pc[36] : register(c0); sampler2D smp0 : register(s0);\n"
    "#define TAP(uv) tex2D(smp0, uv)\n"
    "#define PS_IN float2 uv : TEXCOORD0, float3 n : TEXCOORD1\n"
    "#endif\n"
    "struct V { float4 pos : POS_OUT; float2 uv : TEXCOORD0; float3 n : TEXCOORD1; };\n"
    "#if SM4 || VS\n"
    "V vs_main(float3 p : POSITION, float3 n : NORMAL, float2 uv : TEXCOORD0) {\n"
    "  V o; float4 q = float4(p, 1.0);\n"
    "  o.pos = float4(dot(q, vc[0]), dot(q, vc[1]), dot(q, vc[2]), dot(q, vc[3]));\n"
    "  o.n = normalize(float3(dot(n, vc[4].xyz), dot(n, vc[5].xyz), dot(n, vc[6].xyz)));\n"
    "  o.uv = uv * vc[7].xy + vc[7].zw; return o;\n}\n"
    "#endif\n"
    "#if SM4 || !VS\n"
    "float4 shade(float2 uv, float3 n) { float4 t = TAP(uv); float l = saturate(dot(n, pc[1].xyz)) * pc[1].w + pc[2].x; return float4(t.rgb * pc[0].rgb * l, t.a * pc[0].a); }\n"
    "float4 ps_main(PS_IN) : TARGET { return shade(uv, n); }\n"
    "#endif\n"
    "#if SM4\n"
    "float4 ps_alpha(PS_IN) : TARGET { float4 c = shade(uv, n); clip(c.a - pc[3].x); return c; }\n" // D3DRS_ALPHAFUNC GREATEREQUAL, ALPHAREF pc[3].x * 255
    "#endif\n";

// --- shared workload data (identical input for both APIs) ------------------------------------
struct PostVertex { float x, y, u, v; };
const PostVertex post_quad[4] = {{-1.f, 1.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 0.f}, {-1.f, -1.f, 0.f, 1.f}, {1.f, -1.f, 1.f, 1.f}};
struct SceneVertex { float p[3], n[3], uv[2]; };
const DWORD rs_ids[kRenderStates] = {7, 14, 15, 19, 20, 22, 23, 24, 25, 27, 28, 52, 53, 54, 55, 56, 57, 58, 59, 168, 171, 185, 186, 187, 188, 189, 193, 194, 206, 207, 208, 209};
const DWORD rs_base[kRenderStates] = {1, 1, 0, 2, 1, 3, 4, 1, 7, 0, 0, 0, 1, 1, 1, 8, 0, 0xffffffff, 0xffffffff, 15, 1, 0, 1, 1, 1, 8, 0xffffffff, 0, 0, 1, 1, 1};
enum { kRsAlphaTest = 2, kRsCull = 5, kRsColorWrite = 19 };
// Translator groups: which pre-created D3D11 object a changed render state rebinds.
enum Group : unsigned { kDepth = 1, kBlend = 2, kRaster = 4, kAlpha = 8, kSampler = 16, kNone = 0 };
const unsigned rs_group[kRenderStates] = {kDepth, kDepth, kAlpha, kBlend, kBlend, kRaster, kDepth, kAlpha, kAlpha, kBlend, kNone, kDepth, kDepth, kDepth, kDepth, kDepth, kDepth, kDepth, kDepth, kBlend,
                                          kBlend, kDepth, kDepth, kDepth, kDepth, kDepth, kBlend, kBlend, kBlend, kBlend, kBlend, kBlend};
struct SamplerState { DWORD sampler, state; };
SamplerState ss_ids[kSamplerStates];
struct Scene {
    std::vector<SceneVertex> vertices[kMeshes]; std::vector<std::uint16_t> indices[kMeshes];
    std::vector<unsigned char> texels[kTextures][kTextureLevels];
    std::vector<float> vs, ps; // kDraws x kVsConstants x 4, kDraws x kPsConstants x 4
    DWORD rs[kDraws][kRenderStates]; DWORD ss[kDraws][kSamplerStates];
    double changes_per_draw = 0, raster_overdraw = 0;
};
Scene scene;

std::uint32_t lcg_state = 12345;
float rnd() { lcg_state = lcg_state * 1664525u + 1013904223u; return float(lcg_state >> 8) / 16777216.0f; }

void build_scene() {
    for (unsigned m = 0; m < kMeshes; ++m) {
        auto& v = scene.vertices[m]; auto& ix = scene.indices[m];
        const float fx = 1.f + 0.37f * m, fy = 2.f + 0.23f * m;
        for (unsigned j = 0; j <= kGridY; ++j)
            for (unsigned i = 0; i <= kGridX; ++i) {
                const float x = -1.f + 2.f * i / kGridX, y = -1.f + 2.f * j / kGridY;
                const float z = 0.3f * std::sin(fx * x) * std::cos(fy * y);
                const float dzdx = 0.3f * fx * std::cos(fx * x) * std::cos(fy * y), dzdy = -0.3f * fy * std::sin(fx * x) * std::sin(fy * y);
                const float len = std::sqrt(dzdx * dzdx + dzdy * dzdy + 1.f);
                v.push_back({{x, y, z}, {-dzdx / len, -dzdy / len, -1.f / len}, {2.f * i / kGridX, 2.f * j / kGridY}});
            }
        for (unsigned j = 0; j < kGridY; ++j)
            for (unsigned i = 0; i < kGridX; ++i) { // clockwise on screen: front-facing for D3D9 CULL_CCW and D3D11 CULL_BACK
                const std::uint16_t v00 = std::uint16_t(j * (kGridX + 1) + i), v10 = std::uint16_t(v00 + 1), v01 = std::uint16_t(v00 + kGridX + 1), v11 = std::uint16_t(v01 + 1);
                for (std::uint16_t k : {v00, v01, v10, v10, v01, v11}) ix.push_back(k);
            }
    }
    for (unsigned t = 0; t < kTextures; ++t)
        for (unsigned l = 0; l < kTextureLevels; ++l) {
            const unsigned size = kTextureSize >> l, scale = 1u << l; auto& out = scene.texels[t][l]; out.resize(size_t(size) * size * 4);
            for (unsigned y = 0; y < size; ++y)
                for (unsigned x = 0; x < size; ++x) {
                    const unsigned gx = x * scale, gy = y * scale; unsigned char* p = &out[(size_t(y) * size + x) * 4]; // B, G, R, A (A8R8G8B8 / B8G8R8A8 memory order)
                    p[0] = (unsigned char)((gx * 3 + t * 60) & 255); p[1] = (unsigned char)((gy * 5 + t * 40) & 255); p[2] = (unsigned char)(((gx ^ gy) + t * 90) & 255);
                    p[3] = (((gx / 32) + (gy / 32) + t) % 3 == 0) ? 0 : 255;
                }
        }
    scene.vs.assign(size_t(kDraws) * kVsConstants * 4, 0.f); scene.ps.assign(size_t(kDraws) * kPsConstants * 4, 0.f);
    for (unsigned d = 0; d < kDraws; ++d) {
        float* c = &scene.vs[size_t(d) * kVsConstants * 4];
        const float cx = -0.95f + 1.9f * rnd(), cy = -0.95f + 1.9f * rnd(), cz = 0.05f + 0.9f * rnd(), s = 0.1f;
        const float rows[8][4] = {{s, 0, 0, cx}, {0, s, 0, cy}, {0, 0, 0.01f, cz}, {0, 0, 0, 1}, {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {1.f + (d % 3), 1.f, 0.f, 0.f}};
        std::memcpy(c, rows, sizeof rows);
        for (unsigned k = 8; k < kVsConstants; ++k) for (unsigned e = 0; e < 4; ++e) c[k * 4 + e] = 0.001f * float(k + e + d);
        float* p = &scene.ps[size_t(d) * kPsConstants * 4];
        const float prow[4][4] = {{0.6f + 0.4f * rnd(), 0.6f + 0.4f * rnd(), 0.6f + 0.4f * rnd(), 1.f}, {0.3f, 0.5f, -0.81f, 0.8f}, {0.25f, 0, 0, 0}, {1.f / 255.f, 0, 0, 0}};
        std::memcpy(p, prow, sizeof prow);
        for (unsigned k = 4; k < kPsConstants; ++k) for (unsigned e = 0; e < 4; ++e) p[k * 4 + e] = 0.002f * float(k + e);
        for (unsigned k = 0; k < kRenderStates; ++k) scene.rs[d][k] = rs_base[k];
        scene.rs[d][kRsAlphaTest] = d % 5 == 0 ? 1 : 0;                         // alpha test on one draw in five: 0.4 changes per draw
        scene.rs[d][kRsCull] = (d / 2) % 2 ? D3DCULL_NONE : D3DCULL_CCW;       // 0.5 changes per draw
        scene.rs[d][kRsColorWrite] = d % 5 == 2 ? 7 : 15;                      // 0.4 changes per draw
    }
    unsigned n = 0;
    for (DWORD s = 0; s < 4; ++s) for (DWORD state = D3DSAMP_MAGFILTER; state <= D3DSAMP_SRGBTEXTURE; ++state) if (n < kSamplerStates) ss_ids[n++] = {s, state};
    for (unsigned d = 0; d < kDraws; ++d)
        for (unsigned k = 0; k < kSamplerStates; ++k) {
            const SamplerState id = ss_ids[k]; DWORD v = 0;
            switch (id.state) {
                case D3DSAMP_MAGFILTER: v = D3DTEXF_LINEAR; break;
                case D3DSAMP_MINFILTER: v = id.sampler == 0 ? D3DTEXF_ANISOTROPIC : D3DTEXF_LINEAR; break;
                case D3DSAMP_MIPFILTER: v = D3DTEXF_LINEAR; break;
                case D3DSAMP_MAXANISOTROPY: v = id.sampler == 0 ? 16 : 1; break;
                default: v = 0; break;
            }
            scene.ss[d][k] = v;
        }
    unsigned changes = 0;
    for (unsigned d = 0; d < kDraws; ++d) { const unsigned prev = (d + kDraws - 1) % kDraws; for (unsigned k = 0; k < kRenderStates; ++k) changes += scene.rs[d][k] != scene.rs[prev][k]; }
    scene.changes_per_draw = double(changes) / kDraws;
    double area = 0; // each draw is a flat 0.2 x 0.2 NDC quad (z only moves depth): its on-screen share of the 2 x 2 viewport
    for (unsigned d = 0; d < kDraws; ++d) { const float* c = &scene.vs[size_t(d) * kVsConstants * 4]; const double cx = c[3], cy = c[7], s = c[0];
        area += (std::min(1.0, cx + s) - std::max(-1.0, cx - s)) * (std::min(1.0, cy + s) - std::max(-1.0, cy - s)) / 4.0; }
    scene.raster_overdraw = area;
    std::printf("SCENE draws=%u triangles_per_draw=%u meshes=%u textures=%u state_calls_per_draw=%u value_changes_per_draw=%.2f set_texture_per_draw=4.5 vs_constants=%u ps_constants=%u alpha_test_draws=%u raster_overdraw=%.3f\n",
                kDraws, kTriangles, kMeshes, kTextures, kRenderStates + kSamplerStates, scene.changes_per_draw, kVsConstants, kPsConstants, (kDraws + 4) / 5, scene.raster_overdraw);
}

float half_to_float(std::uint16_t h) {
    const unsigned sign = h >> 15, exponent = (h >> 10) & 31, mantissa = h & 1023;
    float v = exponent == 0 ? std::ldexp(float(mantissa), -24) : exponent == 31 ? INFINITY : std::ldexp(float(mantissa | 1024), int(exponent) - 25);
    return sign ? -v : v;
}
// Readback summaries: FP16 channel means; X8R8G8B8 coverage (pixels not the clear colour) and channel means.
void summarise_fp16(const char* api, const char* workload, unsigned w, unsigned h, const unsigned char* data, unsigned pitch) {
    double sum[3] = {0, 0, 0}; unsigned long long covered = 0; // the target was cleared to zero before the timed passes
    for (unsigned y = 0; y < h; ++y) {
        const auto* row = reinterpret_cast<const std::uint16_t*>(data + size_t(y) * pitch);
        for (unsigned x = 0; x < w; ++x) { covered += (row[x * 4] | row[x * 4 + 1] | row[x * 4 + 2]) != 0; for (unsigned c = 0; c < 3; ++c) sum[c] += half_to_float(row[x * 4 + c]); }
    }
    const double n = double(w) * h;
    std::printf("VERIFY api=%s workload=%s width=%u height=%u coverage=%.6f mean_r=%.6f mean_g=%.6f mean_b=%.6f\n", api, workload, w, h, covered / n, sum[0] / n, sum[1] / n, sum[2] / n);
    char label[96]; std::snprintf(label, sizeof label, "%s_%s_%ux%u_wrote_output", api, workload, w, h); require(label, covered >= n * 0.999 && sum[0] > n * 0.05 && sum[1] > n * 0.05 && sum[2] > n * 0.05);
}
void summarise_bgrx(const char* api, const char* workload, unsigned w, unsigned h, const unsigned char* data, unsigned pitch, double min_coverage) {
    double sum[3] = {0, 0, 0}; unsigned long long covered = 0;
    for (unsigned y = 0; y < h; ++y) {
        const unsigned char* row = data + size_t(y) * pitch;
        for (unsigned x = 0; x < w; ++x) { const unsigned char* p = row + x * 4; std::uint32_t rgb = p[0] | (p[1] << 8) | (p[2] << 16); covered += rgb != (kClear & 0xffffff); sum[0] += p[2]; sum[1] += p[1]; sum[2] += p[0]; }
    }
    const double n = double(w) * h;
    std::printf("VERIFY api=%s workload=%s width=%u height=%u coverage=%.6f mean_r=%.6f mean_g=%.6f mean_b=%.6f\n", api, workload, w, h, covered / n, sum[0] / n / 255, sum[1] / n / 255, sum[2] / n / 255);
    char label[96]; std::snprintf(label, sizeof label, "%s_%s_%ux%u_wrote_output", api, workload, w, h); require(label, covered > n * min_coverage);
}

// Samples that passed depth and alpha test in one frame, per target pixel: the depth-passing overdraw.
void print_occlusion(const char* api, const char* workload, unsigned w, unsigned h, bool got, unsigned long long samples) {
    std::printf("OCCLUSION api=%s workload=%s width=%u height=%u available=%u samples=%llu depth_passing_overdraw=%.4f\n", api, workload, w, h, unsigned(got), samples, got ? double(samples) / (double(w) * h) : 0.0);
}

// --- the common measurement --------------------------------------------------------------------
// B: begin() (timestamp begin), end() (timestamp end), wait(ok) (event + spin), timestamp(us) (read the pair).
template <class B, class W> void measure(B& b, const char* api, const char* workload, unsigned w, unsigned h, W work) {
    std::vector<double> event_us, submit_us, ts_us; unsigned timeouts = 0, disjoint = 0; bool ok = false;
    b.wait(&ok);
    double thread0 = 0, process0 = 0, wall0 = 0;
    for (unsigned i = 0; i < kWarmup + kFrames; ++i) {
        if (i == kWarmup) { thread0 = thread_cpu_us(); process0 = process_cpu_us(); wall0 = now_us(); }
        const double t0 = now_us();
        b.begin(); work();
        const double t1 = now_us();
        b.end(); b.wait(&ok);
        const double t2 = now_us();
        double ts = 0; const int ts_state = b.timestamp(&ts);
        if (i < kWarmup) continue;
        if (!ok) ++timeouts;
        event_us.push_back(t2 - t0); submit_us.push_back(t1 - t0);
        if (ts_state > 0) ts_us.push_back(ts); else if (ts_state < 0) ++disjoint;
    }
    const double wall = now_us() - wall0, thread = thread_cpu_us() - thread0, process = process_cpu_us() - process0;
    // Pipelined: the same work back to back, a Present-like flush per iteration, one drain at the end (throughput, no idle gaps).
    double pipelined_submit = 0; const double p0 = now_us();
    for (unsigned i = 0; i < kFrames; ++i) { const double s0 = now_us(); work(); b.flush(); pipelined_submit += now_us() - s0; }
    bool pipelined_ok = false; b.wait(&pipelined_ok);
    const double pipelined = (now_us() - p0) / kFrames;
    std::printf("MEASURE api=%s workload=%s width=%u height=%u frames=%u event_us=%.1f event_p90_us=%.1f submit_us=%.1f submit_p90_us=%.1f timestamp_us=%.1f timestamp_p90_us=%.1f timestamp_samples=%u timestamp_rejected=%u "
                "wall_per_frame_us=%.1f main_thread_cpu_us=%.1f process_cpu_us=%.1f other_threads_cpu_us=%.1f timeouts=%u pipelined_us=%.1f pipelined_submit_us=%.1f pipelined_synced=%u\n",
                api, workload, w, h, kFrames, quantile(event_us, 0.5), quantile(event_us, 0.9), quantile(submit_us, 0.5), quantile(submit_us, 0.9),
                quantile(ts_us, 0.5), quantile(ts_us, 0.9), unsigned(ts_us.size()), disjoint, wall / kFrames, thread / kFrames, process / kFrames, (process - thread) / kFrames, timeouts, pipelined, pipelined_submit / kFrames, unsigned(pipelined_ok));
    char label[96]; std::snprintf(label, sizeof label, "%s_%s_%ux%u_synced", api, workload, w, h); require(label, timeouts == 0 && event_us.size() == kFrames && pipelined_ok);
}

// --- D3D9 ----------------------------------------------------------------------------------------
struct D9 {
    IDirect3D9* api = nullptr; IDirect3DDevice9* dev = nullptr; IDirect3DQuery9* event = nullptr;
    IDirect3DQuery9* disjoint = nullptr; IDirect3DQuery9* ts0 = nullptr; IDirect3DQuery9* ts1 = nullptr; IDirect3DQuery9* freq = nullptr; bool ts = false;
    void begin() { if (ts) { disjoint->Issue(D3DISSUE_BEGIN); freq->Issue(D3DISSUE_END); ts0->Issue(D3DISSUE_END); } }
    void end() { if (ts) { ts1->Issue(D3DISSUE_END); disjoint->Issue(D3DISSUE_END); } }
    void wait(bool* ok) {
        *ok = false; if (FAILED(event->Issue(D3DISSUE_END))) return; const double t0 = now_us();
        for (;;) { const HRESULT hr = event->GetData(nullptr, 0, D3DGETDATA_FLUSH); if (hr == S_OK) { *ok = true; return; } if (FAILED(hr) || now_us() - t0 > 10e6) return; }
    }
    void flush() { event->Issue(D3DISSUE_END); event->GetData(nullptr, 0, D3DGETDATA_FLUSH); } // one poll: the flush a Present would do
    template <class T> bool get(IDirect3DQuery9* q, T* value) { const double t0 = now_us(); for (;;) { const HRESULT hr = q->GetData(value, sizeof *value, D3DGETDATA_FLUSH); if (hr == S_OK) return true; if (FAILED(hr) || now_us() - t0 > 1e6) return false; } }
    int timestamp(double* us) { // 1 sample, 0 no timestamps, -1 disjoint or unreadable
        if (!ts) return 0;
        BOOL dj = TRUE; UINT64 f = 0, a = 0, b = 0;
        if (!get(disjoint, &dj) || !get(freq, &f) || !get(ts0, &a) || !get(ts1, &b) || dj || !f || b < a) return -1;
        *us = double(b - a) * 1e6 / double(f); return 1;
    }
    void destroy() { release(freq); release(ts1); release(ts0); release(disjoint); release(event); release(dev); release(api); }
};

template <class F> void readback9(IDirect3DDevice9* dev, IDirect3DSurface9* surface, D3DFORMAT format, unsigned w, unsigned h, F f) {
    IDirect3DSurface9* sys = nullptr;
    need("CreateOffscreenPlainSurface", dev->CreateOffscreenPlainSurface(w, h, format, D3DPOOL_SYSTEMMEM, &sys, nullptr));
    const HRESULT hr = dev->GetRenderTargetData(surface, sys);
    if (FAILED(hr)) { release(sys); throw Unavailable("GetRenderTargetData", hr); }
    D3DLOCKED_RECT rect{}; need("LockRect", sys->LockRect(&rect, nullptr, D3DLOCK_READONLY));
    f(static_cast<const unsigned char*>(rect.pBits), unsigned(rect.Pitch));
    sys->UnlockRect(); release(sys);
}

struct Shaders { std::vector<unsigned char> post_vs, fill_ps, post_ps, scene_vs, scene_ps, scene_alpha_ps; };

void run_d3d9(HMODULE module, HWND window, const Shaders& s) {
    D9 d; auto address = GetProcAddress(module, "Direct3DCreate9"); IDirect3D9*(WINAPI* create)(UINT) = nullptr; std::memcpy(&create, &address, sizeof create);
    if (!create || !(d.api = create(D3D_SDK_VERSION))) throw Unavailable("Direct3DCreate9");
    D3DPRESENT_PARAMETERS pp{}; pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window; pp.BackBufferWidth = 256; pp.BackBufferHeight = 256;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE; pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    const HRESULT created = d.api->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &d.dev); // the game's device: plain, hardware VP
    D3DADAPTER_IDENTIFIER9 ident{}; d.api->GetAdapterIdentifier(0, 0, &ident);
    std::printf("DEVICE api=d3d9 hr=%08lx adapter=%s driver=%s vendor=%04lx device=%04lx\n", (unsigned long)created, token(ident.Description).c_str(), token(ident.Driver).c_str(), ident.VendorId, ident.DeviceId);
    if (FAILED(created)) { d.destroy(); throw Unavailable("CreateDevice", created); }
    IDirect3DDevice9* dev = d.dev;
    for (const auto& f : {std::pair<const char*, D3DFORMAT>{"A16B16G16R16F", D3DFMT_A16B16G16R16F}, {"X8R8G8B8", D3DFMT_X8R8G8B8}}) {
        const HRESULT rt = d.api->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, f.second);
        const HRESULT filter = d.api->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_QUERY_FILTER, D3DRTYPE_TEXTURE, f.second);
        std::printf("FORMAT api=d3d9 name=%s render_target_hr=%08lx filter_hr=%08lx\n", f.first, (unsigned long)rt, (unsigned long)filter);
    }
    need("CreateQuery_event", dev->CreateQuery(D3DQUERYTYPE_EVENT, &d.event));
    const HRESULT qd = dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &d.disjoint), q0 = dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &d.ts0), q1 = dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &d.ts1), qf = dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &d.freq);
    d.ts = SUCCEEDED(qd) && SUCCEEDED(q0) && SUCCEEDED(q1) && SUCCEEDED(qf) && d.disjoint && d.ts0 && d.ts1 && d.freq;
    std::printf("QUERY api=d3d9 event=1 timestamp_disjoint_hr=%08lx timestamp_hr=%08lx timestamp_freq_hr=%08lx timestamps=%u\n", (unsigned long)qd, (unsigned long)q0, (unsigned long)qf, unsigned(d.ts));
    if (!d.ts) { release(d.freq); release(d.ts1); release(d.ts0); release(d.disjoint); }

    IDirect3DVertexShader9* post_vs = nullptr; IDirect3DPixelShader9* fill_ps = nullptr; IDirect3DPixelShader9* post_ps = nullptr; IDirect3DVertexShader9* scene_vs = nullptr; IDirect3DPixelShader9* scene_ps = nullptr;
    IDirect3DVertexDeclaration9* post_decl = nullptr; IDirect3DVertexDeclaration9* scene_decl = nullptr; IDirect3DVertexBuffer9* quad = nullptr;
    IDirect3DVertexBuffer9* vb[kMeshes] = {}; IDirect3DIndexBuffer9* ib[kMeshes] = {}; IDirect3DTexture9* tex[kTextures] = {};
    try {
        need("CreateVertexShader_post", dev->CreateVertexShader(reinterpret_cast<const DWORD*>(s.post_vs.data()), &post_vs));
        need("CreatePixelShader_fill", dev->CreatePixelShader(reinterpret_cast<const DWORD*>(s.fill_ps.data()), &fill_ps));
        need("CreatePixelShader_post", dev->CreatePixelShader(reinterpret_cast<const DWORD*>(s.post_ps.data()), &post_ps));
        need("CreateVertexShader_scene", dev->CreateVertexShader(reinterpret_cast<const DWORD*>(s.scene_vs.data()), &scene_vs));
        need("CreatePixelShader_scene", dev->CreatePixelShader(reinterpret_cast<const DWORD*>(s.scene_ps.data()), &scene_ps));
        const D3DVERTEXELEMENT9 post_elements[] = {{0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, {0, 8, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0}, D3DDECL_END()};
        const D3DVERTEXELEMENT9 scene_elements[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
                                                    {0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0}, D3DDECL_END()};
        need("CreateVertexDeclaration_post", dev->CreateVertexDeclaration(post_elements, &post_decl));
        need("CreateVertexDeclaration_scene", dev->CreateVertexDeclaration(scene_elements, &scene_decl));
        void* p = nullptr;
        need("CreateVertexBuffer_quad", dev->CreateVertexBuffer(sizeof post_quad, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &quad, nullptr));
        need("Lock_quad", quad->Lock(0, 0, &p, 0)); std::memcpy(p, post_quad, sizeof post_quad); quad->Unlock();
        for (unsigned m = 0; m < kMeshes; ++m) { // the census pools: WRITEONLY in MANAGED
            const UINT vbytes = UINT(scene.vertices[m].size() * sizeof(SceneVertex)), ibytes = UINT(scene.indices[m].size() * 2);
            need("CreateVertexBuffer", dev->CreateVertexBuffer(vbytes, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &vb[m], nullptr));
            need("Lock_vb", vb[m]->Lock(0, 0, &p, 0)); std::memcpy(p, scene.vertices[m].data(), vbytes); vb[m]->Unlock();
            need("CreateIndexBuffer", dev->CreateIndexBuffer(ibytes, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib[m], nullptr));
            need("Lock_ib", ib[m]->Lock(0, 0, &p, 0)); std::memcpy(p, scene.indices[m].data(), ibytes); ib[m]->Unlock();
        }
        for (unsigned t = 0; t < kTextures; ++t) {
            need("CreateTexture_managed", dev->CreateTexture(kTextureSize, kTextureSize, kTextureLevels, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex[t], nullptr));
            for (unsigned l = 0; l < kTextureLevels; ++l) {
                const unsigned size = kTextureSize >> l; D3DLOCKED_RECT rect{}; need("LockRect_texture", tex[t]->LockRect(l, &rect, nullptr, 0));
                for (unsigned y = 0; y < size; ++y) std::memcpy(static_cast<unsigned char*>(rect.pBits) + size_t(y) * rect.Pitch, &scene.texels[t][l][size_t(y) * size * 4], size_t(size) * 4);
                tex[t]->UnlockRect(l);
            }
        }
        measure(d, "d3d9", "empty", 0, 0, [] {});
        // full-screen pass
        for (const auto size : {std::pair<unsigned, unsigned>{1920, 1080}, std::pair<unsigned, unsigned>{5120, 1440}}) {
            const unsigned w = size.first, h = size.second;
            IDirect3DTexture9* src = nullptr; IDirect3DTexture9* dst = nullptr; IDirect3DSurface9* src_s = nullptr; IDirect3DSurface9* dst_s = nullptr;
            try {
                need("CreateTexture_src", dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &src, nullptr));
                need("CreateTexture_dst", dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &dst, nullptr));
                src->GetSurfaceLevel(0, &src_s); dst->GetSurfaceLevel(0, &dst_s);
                const float vc[4] = {-1.f / w, 1.f / h, 0.f, 0.f}, pc[8] = {1.f / w, 1.f / h, float(w), float(h), 0.7f, 0.37f, 0.61f, 0.5f};
                dev->SetVertexShaderConstantF(0, vc, 1); dev->SetPixelShaderConstantF(0, pc, 2);
                dev->SetRenderState(D3DRS_ZENABLE, FALSE); dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE); dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE); dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
                dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE); dev->SetRenderState(D3DRS_COLORWRITEENABLE, 15); dev->SetDepthStencilSurface(nullptr);
                dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR); dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR); dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
                dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP); dev->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, 1);
                dev->SetVertexDeclaration(post_decl); dev->SetStreamSource(0, quad, 0, sizeof(PostVertex)); dev->SetVertexShader(post_vs);
                need("BeginScene", dev->BeginScene()); dev->SetRenderTarget(0, src_s); dev->SetTexture(0, nullptr); dev->SetPixelShader(fill_ps); need("DrawPrimitive_fill", dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2)); dev->EndScene();
                need("ColorFill_dst", dev->ColorFill(dst_s, nullptr, 0)); dev->SetPixelShader(post_ps);
                measure(d, "d3d9", "fullscreen_pass", w, h, [&] { dev->BeginScene(); dev->SetRenderTarget(0, dst_s); dev->SetTexture(0, src); dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2); dev->EndScene(); });
                readback9(dev, dst_s, D3DFMT_A16B16G16R16F, w, h, [&](const unsigned char* data, unsigned pitch) { summarise_fp16("d3d9", "fullscreen_pass", w, h, data, pitch); });
            } catch (const Unavailable& e) { std::printf("NOTE api=d3d9 workload=fullscreen_pass width=%u height=%u unavailable=%s hr=%08lx\n", w, h, e.what(), (unsigned long)e.hr); require("d3d9_fullscreen_pass_ran", false); }
            dev->SetTexture(0, nullptr); IDirect3DSurface9* back = nullptr; if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back))) { dev->SetRenderTarget(0, back); release(back); }
            release(dst_s); release(src_s); release(dst); release(src);
        }
        // the 460-draw scene
        dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP); dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        const std::pair<unsigned, unsigned> scene_runs[] = {{1920, 1080}, {5120, 1440}, {1920, 1080}};
        for (unsigned run = 0; run < 3; ++run) {
            const unsigned w = scene_runs[run].first, h = scene_runs[run].second, triangles = run == 2 ? 2 : kTriangles; const char* workload = run == 2 ? "scene_460_cpu" : "scene_460";
            IDirect3DSurface9* rt = nullptr; IDirect3DSurface9* ds = nullptr;
            try {
                need("CreateRenderTarget", dev->CreateRenderTarget(w, h, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, nullptr));
                need("CreateDepthStencilSurface", dev->CreateDepthStencilSurface(w, h, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &ds, nullptr));
                auto frame = [&] {
                    dev->BeginScene(); dev->SetRenderTarget(0, rt); dev->SetDepthStencilSurface(ds); dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, kClear, 1.f, 0);
                    for (unsigned dr = 0; dr < kDraws; ++dr) {
                        const unsigned m = dr % kMeshes;
                        for (unsigned k = 0; k < kRenderStates; ++k) dev->SetRenderState(D3DRENDERSTATETYPE(rs_ids[k]), scene.rs[dr][k]);
                        for (unsigned k = 0; k < kSamplerStates; ++k) dev->SetSamplerState(ss_ids[k].sampler, D3DSAMPLERSTATETYPE(ss_ids[k].state), scene.ss[dr][k]);
                        dev->SetVertexDeclaration(scene_decl); dev->SetStreamSource(0, vb[m], 0, sizeof(SceneVertex)); dev->SetIndices(ib[m]);
                        dev->SetVertexShader(scene_vs); dev->SetPixelShader(scene_ps);
                        dev->SetTexture(0, tex[dr % kTextures]); dev->SetTexture(1, nullptr); dev->SetTexture(2, nullptr); dev->SetTexture(3, nullptr); if (dr & 1) dev->SetTexture(4, nullptr);
                        dev->SetVertexShaderConstantF(0, &scene.vs[size_t(dr) * kVsConstants * 4], kVsConstants); dev->SetPixelShaderConstantF(0, &scene.ps[size_t(dr) * kPsConstants * 4], kPsConstants);
                        dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, kVertices, 0, triangles);
                    }
                    dev->EndScene();
                };
                measure(d, "d3d9", workload, w, h, frame);
                { IDirect3DQuery9* q = nullptr; DWORD samples = 0; bool got = false;
                  if (SUCCEEDED(dev->CreateQuery(D3DQUERYTYPE_OCCLUSION, &q)) && q) { q->Issue(D3DISSUE_BEGIN); frame(); q->Issue(D3DISSUE_END); got = d.get(q, &samples); }
                  release(q); print_occlusion("d3d9", workload, w, h, got, samples); }
                readback9(dev, rt, D3DFMT_X8R8G8B8, w, h, [&](const unsigned char* data, unsigned pitch) { summarise_bgrx("d3d9", workload, w, h, data, pitch, run == 2 ? 0.002 : 0.5); });
            } catch (const Unavailable& e) { std::printf("NOTE api=d3d9 workload=%s width=%u height=%u unavailable=%s hr=%08lx\n", workload, w, h, e.what(), (unsigned long)e.hr); require("d3d9_scene_ran", false); }
            IDirect3DSurface9* back = nullptr; if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back))) { dev->SetRenderTarget(0, back); release(back); }
            dev->SetDepthStencilSurface(nullptr); release(ds); release(rt);
        }
        std::printf("STEP d3d9 status=ok\n");
    } catch (const Unavailable& e) { std::printf("STEP d3d9 status=unavailable reason=%s hr=%08lx\n", e.what(), (unsigned long)e.hr); require("d3d9_setup", false); }
    dev->SetTexture(0, nullptr); dev->SetStreamSource(0, nullptr, 0, 0); dev->SetIndices(nullptr); dev->SetVertexShader(nullptr); dev->SetPixelShader(nullptr); dev->SetVertexDeclaration(nullptr);
    for (auto*& t : tex) release(t);
    for (auto*& b : ib) release(b);
    for (auto*& b : vb) release(b);
    release(quad); release(scene_decl); release(post_decl); release(scene_ps); release(scene_vs); release(post_ps); release(fill_ps); release(post_vs);
    d.destroy();
}

// --- D3D11 ----------------------------------------------------------------------------------------
struct D11 {
    ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr; ID3D11Query* event = nullptr; ID3D11Query* disjoint = nullptr; ID3D11Query* ts0 = nullptr; ID3D11Query* ts1 = nullptr; bool ts = false;
    void begin() { if (ts) { ctx->Begin(disjoint); ctx->End(ts0); } }
    void end() { if (ts) { ctx->End(ts1); ctx->End(disjoint); } }
    void wait(bool* ok) {
        *ok = false; ctx->End(event); ctx->Flush(); const double t0 = now_us();
        for (;;) { BOOL done = FALSE; const HRESULT hr = ctx->GetData(event, &done, sizeof done, 0); if (hr == S_OK) { *ok = true; return; } if (FAILED(hr) || now_us() - t0 > 10e6) return; }
    }
    void flush() { ctx->Flush(); }
    template <class T> bool get(ID3D11Query* q, T* value) { const double t0 = now_us(); for (;;) { const HRESULT hr = ctx->GetData(q, value, sizeof *value, 0); if (hr == S_OK) return true; if (FAILED(hr) || now_us() - t0 > 1e6) return false; } }
    int timestamp(double* us) {
        if (!ts) return 0;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{}; UINT64 a = 0, b = 0;
        if (!get(disjoint, &dj) || !get(ts0, &a) || !get(ts1, &b) || dj.Disjoint || !dj.Frequency || b < a) return -1;
        *us = double(b - a) * 1e6 / double(dj.Frequency); return 1;
    }
    void destroy() { if (ctx) ctx->ClearState(); release(ts1); release(ts0); release(disjoint); release(event); release(ctx); release(dev); }
};

// What a D3D9-on-D3D11 translator would issue for the census call pattern: every incoming state call
// compared against a shadow, pre-created state objects bound only when their group changed.
struct Translator {
    DWORD rs[kRenderStates]; DWORD ss[kSamplerStates]; ID3D11ShaderResourceView* srv[5]; unsigned mesh = ~0u; ID3D11VertexShader* vs = nullptr; ID3D11PixelShader* ps = nullptr;
    void reset() { std::fill(rs, rs + kRenderStates, 0xdeadbeefu); std::fill(ss, ss + kSamplerStates, 0xdeadbeefu); std::fill(srv, srv + 5, reinterpret_cast<ID3D11ShaderResourceView*>(1)); mesh = ~0u; vs = nullptr; ps = nullptr; }
};

void run_d3d11(HMODULE module, const Shaders& s) {
    D11 d; auto address = GetProcAddress(module, "D3D11CreateDevice"); PFN_D3D11_CREATE_DEVICE create = nullptr; std::memcpy(&create, &address, sizeof create);
    if (!create) throw Unavailable("D3D11CreateDevice_export");
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0}; D3D_FEATURE_LEVEL level{};
    HRESULT hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 4, D3D11_SDK_VERSION, &d.dev, &level, &d.ctx);
    if (FAILED(hr)) hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 4, D3D11_SDK_VERSION, &d.dev, &level, &d.ctx);
    std::printf("DEVICE api=d3d11 hr=%08lx feature_level=%x\n", (unsigned long)hr, unsigned(level));
    if (FAILED(hr) || !d.dev) { d.destroy(); throw Unavailable("D3D11CreateDevice", hr); }
    ID3D11Device* dev = d.dev; ID3D11DeviceContext* ctx = d.ctx;
    { IDXGIDevice* xd = nullptr; IDXGIAdapter* adapter = nullptr; DXGI_ADAPTER_DESC desc{};
      if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&xd))) && xd && SUCCEEDED(xd->GetAdapter(&adapter)) && adapter && SUCCEEDED(adapter->GetDesc(&desc))) {
          std::string name; for (wchar_t c : desc.Description) { if (!c) break; name += c < 128 ? char(c) : '?'; }
          std::printf("ADAPTER api=d3d11 description=%s vendor=%04x device=%04x\n", token(name.c_str()).c_str(), desc.VendorId, desc.DeviceId);
      }
      release(adapter); release(xd); }
    DXGI_FORMAT scene_format = DXGI_FORMAT_B8G8R8X8_UNORM;
    for (const auto& f : {std::pair<const char*, DXGI_FORMAT>{"R16G16B16A16_FLOAT", DXGI_FORMAT_R16G16B16A16_FLOAT}, {"B8G8R8X8_UNORM", DXGI_FORMAT_B8G8R8X8_UNORM}, {"B8G8R8A8_UNORM", DXGI_FORMAT_B8G8R8A8_UNORM}, {"D24_UNORM_S8_UINT", DXGI_FORMAT_D24_UNORM_S8_UINT}}) {
        UINT support = 0; const HRESULT q = dev->CheckFormatSupport(f.second, &support);
        std::printf("FORMAT api=d3d11 name=%s hr=%08lx render_target=%u sample=%u depth_stencil=%u\n", f.first, (unsigned long)q, unsigned(!!(support & D3D11_FORMAT_SUPPORT_RENDER_TARGET)),
                    unsigned(!!(support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)), unsigned(!!(support & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL)));
        if (f.second == DXGI_FORMAT_B8G8R8X8_UNORM && (FAILED(q) || !(support & D3D11_FORMAT_SUPPORT_RENDER_TARGET))) { scene_format = DXGI_FORMAT_B8G8R8A8_UNORM; std::printf("NOTE api=d3d11 workaround=scene_target_B8G8R8A8_UNORM reason=B8G8R8X8_not_renderable\n"); }
    }
    const D3D11_QUERY_DESC ed{D3D11_QUERY_EVENT, 0}, dd{D3D11_QUERY_TIMESTAMP_DISJOINT, 0}, td{D3D11_QUERY_TIMESTAMP, 0};
    need("CreateQuery_event", dev->CreateQuery(&ed, &d.event));
    const HRESULT qd = dev->CreateQuery(&dd, &d.disjoint), q0 = dev->CreateQuery(&td, &d.ts0), q1 = dev->CreateQuery(&td, &d.ts1);
    d.ts = SUCCEEDED(qd) && SUCCEEDED(q0) && SUCCEEDED(q1);
    std::printf("QUERY api=d3d11 event=1 timestamp_disjoint_hr=%08lx timestamp_hr=%08lx timestamps=%u\n", (unsigned long)qd, (unsigned long)q0, unsigned(d.ts));

    ID3D11VertexShader* post_vs = nullptr; ID3D11PixelShader* fill_ps = nullptr; ID3D11PixelShader* post_ps = nullptr; ID3D11VertexShader* scene_vs = nullptr; ID3D11PixelShader* scene_ps = nullptr; ID3D11PixelShader* alpha_ps = nullptr;
    ID3D11InputLayout* post_layout = nullptr; ID3D11InputLayout* scene_layout = nullptr; ID3D11Buffer* quad = nullptr; ID3D11Buffer* post_cb = nullptr; ID3D11Buffer* scene_cb = nullptr;
    ID3D11Buffer* vb[kMeshes] = {}; ID3D11Buffer* ib[kMeshes] = {}; ID3D11Texture2D* tex[kTextures] = {}; ID3D11ShaderResourceView* srv[kTextures] = {};
    ID3D11RasterizerState* raster[2] = {}; ID3D11BlendState* blend[2] = {}; ID3D11DepthStencilState* depth = nullptr; ID3D11SamplerState* samplers[4] = {}; ID3D11SamplerState* clamp = nullptr;
    auto buffer = [&](UINT bytes, UINT bind, D3D11_USAGE usage, UINT cpu, const void* data) {
        D3D11_BUFFER_DESC desc{}; desc.ByteWidth = bytes; desc.Usage = usage; desc.BindFlags = bind; desc.CPUAccessFlags = cpu; D3D11_SUBRESOURCE_DATA init{data, 0, 0}; ID3D11Buffer* b = nullptr;
        need("CreateBuffer", dev->CreateBuffer(&desc, data ? &init : nullptr, &b)); return b;
    };
    try {
        need("CreateVertexShader_post", dev->CreateVertexShader(s.post_vs.data(), s.post_vs.size(), nullptr, &post_vs));
        need("CreatePixelShader_fill", dev->CreatePixelShader(s.fill_ps.data(), s.fill_ps.size(), nullptr, &fill_ps));
        need("CreatePixelShader_post", dev->CreatePixelShader(s.post_ps.data(), s.post_ps.size(), nullptr, &post_ps));
        need("CreateVertexShader_scene", dev->CreateVertexShader(s.scene_vs.data(), s.scene_vs.size(), nullptr, &scene_vs));
        need("CreatePixelShader_scene", dev->CreatePixelShader(s.scene_ps.data(), s.scene_ps.size(), nullptr, &scene_ps));
        need("CreatePixelShader_alpha", dev->CreatePixelShader(s.scene_alpha_ps.data(), s.scene_alpha_ps.size(), nullptr, &alpha_ps));
        const D3D11_INPUT_ELEMENT_DESC post_elements[] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}, {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        const D3D11_INPUT_ELEMENT_DESC scene_elements[] = {{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}, {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                                                          {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        need("CreateInputLayout_post", dev->CreateInputLayout(post_elements, 2, s.post_vs.data(), s.post_vs.size(), &post_layout));
        need("CreateInputLayout_scene", dev->CreateInputLayout(scene_elements, 3, s.scene_vs.data(), s.scene_vs.size(), &scene_layout));
        quad = buffer(sizeof post_quad, D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_IMMUTABLE, 0, post_quad);
        post_cb = buffer(8 * 16, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DEFAULT, 0, nullptr);
        scene_cb = buffer((kVsConstants + kPsConstants) * 16, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE, nullptr);
        for (unsigned m = 0; m < kMeshes; ++m) { // MANAGED has no D3D11 pool: IMMUTABLE default-memory buffers
            vb[m] = buffer(UINT(scene.vertices[m].size() * sizeof(SceneVertex)), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_IMMUTABLE, 0, scene.vertices[m].data());
            ib[m] = buffer(UINT(scene.indices[m].size() * 2), D3D11_BIND_INDEX_BUFFER, D3D11_USAGE_IMMUTABLE, 0, scene.indices[m].data());
        }
        for (unsigned t = 0; t < kTextures; ++t) {
            D3D11_TEXTURE2D_DESC desc{}; desc.Width = desc.Height = kTextureSize; desc.MipLevels = kTextureLevels; desc.ArraySize = 1; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE; D3D11_SUBRESOURCE_DATA init[kTextureLevels];
            for (unsigned l = 0; l < kTextureLevels; ++l) init[l] = {scene.texels[t][l].data(), (kTextureSize >> l) * 4, 0};
            need("CreateTexture2D_immutable", dev->CreateTexture2D(&desc, init, &tex[t])); need("CreateShaderResourceView", dev->CreateShaderResourceView(tex[t], nullptr, &srv[t]));
        }
        for (unsigned i = 0; i < 2; ++i) {
            D3D11_RASTERIZER_DESC r{}; r.FillMode = D3D11_FILL_SOLID; r.CullMode = i ? D3D11_CULL_NONE : D3D11_CULL_BACK; r.DepthClipEnable = TRUE; need("CreateRasterizerState", dev->CreateRasterizerState(&r, &raster[i]));
            D3D11_BLEND_DESC b{}; b.RenderTarget[0].SrcBlend = b.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; b.RenderTarget[0].DestBlend = b.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
            b.RenderTarget[0].BlendOp = b.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD; b.RenderTarget[0].RenderTargetWriteMask = i ? 7 : 15; need("CreateBlendState", dev->CreateBlendState(&b, &blend[i]));
        }
        { D3D11_DEPTH_STENCIL_DESC z{}; z.DepthEnable = TRUE; z.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; z.DepthFunc = D3D11_COMPARISON_LESS_EQUAL; need("CreateDepthStencilState", dev->CreateDepthStencilState(&z, &depth)); }
        for (unsigned i = 0; i < 4; ++i) {
            D3D11_SAMPLER_DESC sd{}; sd.Filter = i == 0 ? D3D11_FILTER_ANISOTROPIC : D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.MaxAnisotropy = i == 0 ? 16 : 1;
            sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; sd.ComparisonFunc = D3D11_COMPARISON_NEVER; sd.MaxLOD = D3D11_FLOAT32_MAX; need("CreateSamplerState", dev->CreateSamplerState(&sd, &samplers[i]));
        }
        { D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxAnisotropy = 1; sd.ComparisonFunc = D3D11_COMPARISON_NEVER; sd.MaxLOD = D3D11_FLOAT32_MAX;
          need("CreateSamplerState_clamp", dev->CreateSamplerState(&sd, &clamp)); }
        auto texture = [&](unsigned w, unsigned h, DXGI_FORMAT format, UINT bind) { D3D11_TEXTURE2D_DESC desc{}; desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1; desc.Format = format; desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = bind; ID3D11Texture2D* t = nullptr; need("CreateTexture2D", dev->CreateTexture2D(&desc, nullptr, &t)); return t; };
        auto readback = [&](ID3D11Texture2D* t, auto f) {
            D3D11_TEXTURE2D_DESC desc{}; t->GetDesc(&desc); desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
            ID3D11Texture2D* staging = nullptr; need("CreateTexture2D_staging", dev->CreateTexture2D(&desc, nullptr, &staging)); ctx->CopyResource(staging, t);
            D3D11_MAPPED_SUBRESOURCE map{}; const HRESULT mh = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map); if (FAILED(mh)) { release(staging); throw Unavailable("Map_staging", mh); }
            f(static_cast<const unsigned char*>(map.pData), unsigned(map.RowPitch)); ctx->Unmap(staging, 0); release(staging);
        };
        measure(d, "d3d11", "empty", 0, 0, [] {});
        const UINT post_stride = sizeof(PostVertex), scene_stride = sizeof(SceneVertex), zero = 0;
        for (const auto size : {std::pair<unsigned, unsigned>{1920, 1080}, std::pair<unsigned, unsigned>{5120, 1440}}) {
            const unsigned w = size.first, h = size.second;
            ID3D11Texture2D* src = nullptr; ID3D11Texture2D* dst = nullptr; ID3D11RenderTargetView* src_r = nullptr; ID3D11RenderTargetView* dst_r = nullptr; ID3D11ShaderResourceView* src_v = nullptr;
            try {
                src = texture(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE); dst = texture(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET);
                need("CreateRenderTargetView", dev->CreateRenderTargetView(src, nullptr, &src_r)); need("CreateRenderTargetView", dev->CreateRenderTargetView(dst, nullptr, &dst_r)); need("CreateShaderResourceView", dev->CreateShaderResourceView(src, nullptr, &src_v));
                const float c[8][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {1.f / w, 1.f / h, float(w), float(h)}, {0.7f, 0.37f, 0.61f, 0.5f}, {0, 0, 0, 0}, {0, 0, 0, 0}};
                ctx->UpdateSubresource(post_cb, 0, nullptr, c, 0, 0);
                const D3D11_VIEWPORT vp{0.f, 0.f, float(w), float(h), 0.f, 1.f}; ctx->RSSetViewports(1, &vp); ctx->RSSetState(raster[1]); ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff); ctx->OMSetDepthStencilState(nullptr, 0);
                ctx->IASetInputLayout(post_layout); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP); ctx->IASetVertexBuffers(0, 1, &quad, &post_stride, &zero);
                ctx->VSSetShader(post_vs, nullptr, 0); ctx->VSSetConstantBuffers(0, 1, &post_cb); ctx->PSSetConstantBuffers(0, 1, &post_cb); ctx->PSSetSamplers(0, 1, &clamp);
                ctx->OMSetRenderTargets(1, &src_r, nullptr); ctx->PSSetShader(fill_ps, nullptr, 0); ctx->Draw(4, 0);
                const float zero4[4] = {0.f, 0.f, 0.f, 0.f}; ctx->ClearRenderTargetView(dst_r, zero4); ctx->PSSetShader(post_ps, nullptr, 0);
                measure(d, "d3d11", "fullscreen_pass", w, h, [&] { ctx->OMSetRenderTargets(1, &dst_r, nullptr); ctx->PSSetShaderResources(0, 1, &src_v); ctx->Draw(4, 0); });
                ID3D11ShaderResourceView* none = nullptr; ctx->PSSetShaderResources(0, 1, &none);
                readback(dst, [&](const unsigned char* data, unsigned pitch) { summarise_fp16("d3d11", "fullscreen_pass", w, h, data, pitch); });
            } catch (const Unavailable& e) { std::printf("NOTE api=d3d11 workload=fullscreen_pass width=%u height=%u unavailable=%s hr=%08lx\n", w, h, e.what(), (unsigned long)e.hr); require("d3d11_fullscreen_pass_ran", false); }
            ctx->ClearState(); release(src_v); release(dst_r); release(src_r); release(dst); release(src);
        }
        const std::pair<unsigned, unsigned> scene_runs[] = {{1920, 1080}, {5120, 1440}, {1920, 1080}};
        for (unsigned run = 0; run < 3; ++run) {
            const unsigned w = scene_runs[run].first, h = scene_runs[run].second, triangles = run == 2 ? 2 : kTriangles; const char* workload = run == 2 ? "scene_460_cpu" : "scene_460";
            ID3D11Texture2D* rt = nullptr; ID3D11Texture2D* ds = nullptr; ID3D11RenderTargetView* rtv = nullptr; ID3D11DepthStencilView* dsv = nullptr;
            try {
                rt = texture(w, h, scene_format, D3D11_BIND_RENDER_TARGET); ds = texture(w, h, DXGI_FORMAT_D24_UNORM_S8_UINT, D3D11_BIND_DEPTH_STENCIL);
                need("CreateRenderTargetView", dev->CreateRenderTargetView(rt, nullptr, &rtv)); need("CreateDepthStencilView", dev->CreateDepthStencilView(ds, nullptr, &dsv));
                Translator tr; tr.reset();
                const float clear[4] = {((kClear >> 16) & 255) / 255.f, ((kClear >> 8) & 255) / 255.f, (kClear & 255) / 255.f, 1.f};
                const D3D11_VIEWPORT vp{0.f, 0.f, float(w), float(h), 0.f, 1.f};
                auto frame = [&] {
                    ctx->OMSetRenderTargets(1, &rtv, dsv); ctx->RSSetViewports(1, &vp); ctx->ClearRenderTargetView(rtv, clear); ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
                    ctx->IASetInputLayout(scene_layout); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); ctx->VSSetConstantBuffers(0, 1, &scene_cb); ctx->PSSetConstantBuffers(0, 1, &scene_cb);
                    for (unsigned dr = 0; dr < kDraws; ++dr) {
                        const unsigned m = dr % kMeshes; unsigned dirty = 0;
                        for (unsigned k = 0; k < kRenderStates; ++k) { const DWORD v = scene.rs[dr][k]; if (tr.rs[k] != v) { tr.rs[k] = v; dirty |= rs_group[k]; } }
                        for (unsigned k = 0; k < kSamplerStates; ++k) { const DWORD v = scene.ss[dr][k]; if (tr.ss[k] != v) { tr.ss[k] = v; dirty |= kSampler; } }
                        if (dirty & kDepth) ctx->OMSetDepthStencilState(depth, 0);
                        if (dirty & kBlend) ctx->OMSetBlendState(blend[tr.rs[kRsColorWrite] == 7], nullptr, 0xffffffff);
                        if (dirty & kRaster) ctx->RSSetState(raster[tr.rs[kRsCull] == D3DCULL_NONE]);
                        if (dirty & kSampler) ctx->PSSetSamplers(0, 4, samplers);
                        if (m != tr.mesh) { tr.mesh = m; ctx->IASetVertexBuffers(0, 1, &vb[m], &scene_stride, &zero); ctx->IASetIndexBuffer(ib[m], DXGI_FORMAT_R16_UINT, 0); }
                        if (tr.vs != scene_vs) { tr.vs = scene_vs; ctx->VSSetShader(scene_vs, nullptr, 0); }
                        ID3D11PixelShader* want = tr.rs[kRsAlphaTest] ? alpha_ps : scene_ps; // alpha test: the clip() variant
                        if (tr.ps != want) { tr.ps = want; ctx->PSSetShader(want, nullptr, 0); }
                        ID3D11ShaderResourceView* bind[5] = {srv[dr % kTextures], nullptr, nullptr, nullptr, nullptr};
                        for (unsigned t = 0; t < (dr & 1 ? 5u : 4u); ++t) if (tr.srv[t] != bind[t]) { tr.srv[t] = bind[t]; ctx->PSSetShaderResources(t, 1, &bind[t]); }
                        D3D11_MAPPED_SUBRESOURCE map{};
                        if (SUCCEEDED(ctx->Map(scene_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
                            std::memcpy(map.pData, &scene.vs[size_t(dr) * kVsConstants * 4], kVsConstants * 16); std::memcpy(static_cast<unsigned char*>(map.pData) + kVsConstants * 16, &scene.ps[size_t(dr) * kPsConstants * 4], kPsConstants * 16);
                            ctx->Unmap(scene_cb, 0);
                        }
                        ctx->DrawIndexed(triangles * 3, 0, 0);
                    }
                };
                measure(d, "d3d11", workload, w, h, frame);
                { const D3D11_QUERY_DESC od{D3D11_QUERY_OCCLUSION, 0}; ID3D11Query* q = nullptr; UINT64 samples = 0; bool got = false;
                  if (SUCCEEDED(dev->CreateQuery(&od, &q)) && q) { ctx->Begin(q); frame(); ctx->End(q); ctx->Flush(); got = d.get(q, &samples); }
                  release(q); print_occlusion("d3d11", workload, w, h, got, samples); }
                readback(rt, [&](const unsigned char* data, unsigned pitch) { summarise_bgrx("d3d11", workload, w, h, data, pitch, run == 2 ? 0.002 : 0.5); });
            } catch (const Unavailable& e) { std::printf("NOTE api=d3d11 workload=%s width=%u height=%u unavailable=%s hr=%08lx\n", workload, w, h, e.what(), (unsigned long)e.hr); require("d3d11_scene_ran", false); }
            ctx->ClearState(); release(dsv); release(rtv); release(ds); release(rt);
        }
        std::printf("STEP d3d11 status=ok\n");
    } catch (const Unavailable& e) { std::printf("STEP d3d11 status=unavailable reason=%s hr=%08lx\n", e.what(), (unsigned long)e.hr); require("d3d11_setup", false); }
    ctx->ClearState();
    release(clamp); release(depth);
    for (auto*& x : samplers) release(x);
    for (auto*& x : blend) release(x);
    for (auto*& x : raster) release(x);
    for (auto*& x : srv) release(x);
    for (auto*& x : tex) release(x);
    for (auto*& x : ib) release(x);
    for (auto*& x : vb) release(x);
    release(scene_cb); release(post_cb); release(quad); release(scene_layout); release(post_layout); release(alpha_ps); release(scene_ps); release(scene_vs); release(post_ps); release(fill_ps); release(post_vs);
    d.destroy();
}
} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QueryPerformanceFrequency(&qpf);
    WNDCLASSA cls{}; cls.lpfnWndProc = DefWindowProcA; cls.hInstance = GetModuleHandleA(nullptr); cls.lpszClassName = "X3GpuBackendAB"; RegisterClassA(&cls);
    HWND window = CreateWindowA(cls.lpszClassName, "X3 GPU backend A/B fixture (d3d9)", WS_OVERLAPPEDWINDOW, 90, 90, 320, 240, nullptr, nullptr, cls.hInstance, nullptr); // never shown
    if (!window) { std::printf("RESULT checks=0 failures=1 error=window FAIL\n"); return 2; }
    report_renderer_key();
    HMODULE d3d9 = report_module("d3d9.dll"), d3d11 = report_module("d3d11.dll"); report_module("dxgi.dll"); HMODULE compiler = report_module("d3dcompiler_47.dll");
    if (compiler) { auto address = GetProcAddress(compiler, "D3DCompile"); std::memcpy(&compile_fn, &address, sizeof compile_fn); }
    require("modules_loaded", d3d9 && d3d11 && compile_fn);
    build_scene();
    Shaders s; bool compiled = false;
    try {
        s.post_vs = compile("post_vs3", post_hlsl, "vs_main", "vs_3_0"); s.fill_ps = compile("fill_ps3", post_hlsl, "ps_fill", "ps_3_0"); s.post_ps = compile("post_ps3", post_hlsl, "ps_post", "ps_3_0");
        s.scene_vs = compile("scene_vs3", scene_hlsl, "vs_main", "vs_3_0"); s.scene_ps = compile("scene_ps3", scene_hlsl, "ps_main", "ps_3_0");
        compiled = true;
    } catch (const Unavailable& e) { std::printf("NOTE shaders_sm3 unavailable=%s hr=%08lx\n", e.what(), (unsigned long)e.hr); }
    require("shaders_sm3_compiled", compiled);
    if (compiled && d3d9) { try { run_d3d9(d3d9, window, s); } catch (const Unavailable& e) { std::printf("STEP d3d9 status=unavailable reason=%s hr=%08lx\n", e.what(), (unsigned long)e.hr); require("d3d9_device", false); } }
    Shaders s11; compiled = false;
    try {
        s11.post_vs = compile("post_vs4", post_hlsl, "vs_main", "vs_4_0"); s11.fill_ps = compile("fill_ps4", post_hlsl, "ps_fill", "ps_4_0"); s11.post_ps = compile("post_ps4", post_hlsl, "ps_post", "ps_4_0");
        s11.scene_vs = compile("scene_vs4", scene_hlsl, "vs_main", "vs_4_0"); s11.scene_ps = compile("scene_ps4", scene_hlsl, "ps_main", "ps_4_0"); s11.scene_alpha_ps = compile("scene_alpha_ps4", scene_hlsl, "ps_alpha", "ps_4_0");
        compiled = true;
    } catch (const Unavailable& e) { std::printf("NOTE shaders_sm4 unavailable=%s hr=%08lx\n", e.what(), (unsigned long)e.hr); }
    require("shaders_sm4_compiled", compiled);
    if (compiled && d3d11) { try { run_d3d11(d3d11, s11); } catch (const Unavailable& e) { std::printf("STEP d3d11 status=unavailable reason=%s hr=%08lx\n", e.what(), (unsigned long)e.hr); require("d3d11_device", false); } }
    DestroyWindow(window);
    std::printf("RESULT checks=%u failures=%u %s\n", checks, failures, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
