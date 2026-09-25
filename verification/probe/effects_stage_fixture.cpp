// Effects stage fixture (docs/architecture/effects-modernisation-opus.md section 9; docs/verification/effects-stage.md).
// Standalone D3D9 (builtin d3d9 through the runner's override), no game, no proxy: the production EffectsStagePass and
// the production TemporalPass on synthetic FP16 scenes, lanes and depth at 1920x1080 and 5120x1440.
//   pass_open      the price of the stage's pass at 5120x1440: riding an RT0-only pass a fog-like quad opened, and
//                  opening its own after the RT1/RT2 unbind (timestamp queries; the 0.1 ms gate of section 9)
//   bolt_capsule   core >= 3 px, length >= L, occluded by the lane, soft at the end (R32F lane too)
//   resolve        a moving bolt and a shell through the real resolve, 8 jitter phases at 0 / 4 / 8 px per frame
//   shell          rim, four rings, the soft hull cut, the decal fallback
//   off_path       no record: S_FALSE, no device call, the frame byte-identical; a callback that draws nothing gives
//                  the resolve's own output byte for byte
//   suppression    armed: taken; disarmed or failed: forwarded, never both (Arming plus a fault-injected run)
//   reset          released before Reset, recreated after
//   fp16_refused   the FP16 blending capability refused (fault): the stage is off, its reason named
//   timing         60 bolts + 2 shells, and a shell filling the screen
// With X3M_EFFECTS_KEYS_FIXTURE (a second executable): --keys <dds> <expected> ...: D3DX loads the textures through
// the ownership wrapper (Options::texture_upload_keys) and the upload-time key must equal the table's; the texture
// lock path, the surface lock path and the read-only fallback each match the in-process sparse_key of known bytes.
// EVENT-fenced plus TIMESTAMP timing; validation-only readback. Never launches the game.
#include "../../src/renderer/effects_stage_pass.h"
#include "../../src/renderer/temporal_pass.h"
#include "../../src/renderer/temporal_resolve_program.h"
#include "../../src/renderer/quad_vertex_program.h"
#ifdef X3M_EFFECTS_KEYS_FIXTURE
#include "../../src/ownership/d3d9_ownership.h"
#endif
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
template<class T> struct Com { T* p = nullptr; ~Com() { if (p) p->Release(); } T* operator->() const { return p; } };
unsigned checks = 0, failures = 0;
void report(const char* label, bool ok) { ++checks; if (!ok) ++failures; std::printf("CHECK %s %s\n", label, ok ? "PASS" : "FAIL"); }
void check(const char* label, HRESULT hr) { if (FAILED(hr)) { std::printf("FATAL %s %08lx\n", label, hr); throw std::runtime_error(label); } }
float half_to_float(unsigned short h) {
    const unsigned s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023; float v;
    if (e == 0) v = std::ldexp(float(m), -24); else if (e == 31) v = m ? NAN : INFINITY; else v = std::ldexp(float(m + 1024), int(e) - 25);
    return s ? -v : v;
}
using Compiler = decltype(&D3DXCompileShader);
Compiler compiler = nullptr;
HMODULE d3dx_module = nullptr;
void compile(const char* source, const char* target, std::vector<DWORD>& out) {
    Com<ID3DXBuffer> code, errors;
    const HRESULT hr = compiler(source, UINT(std::strlen(source)), nullptr, nullptr, "main", target, D3DXSHADER_OPTIMIZATION_LEVEL3, &code.p, &errors.p, nullptr);
    if (errors.p) std::printf("COMPILER %s\n", static_cast<char*>(errors->GetBufferPointer()));
    check(target, hr);
    out.assign(static_cast<DWORD*>(code->GetBufferPointer()), static_cast<DWORD*>(code->GetBufferPointer()) + code->GetBufferSize() / 4);
}
double median(std::vector<double> v) { if (v.empty()) return 0; std::sort(v.begin(), v.end()); return v[v.size() / 2]; }
HRESULT complete_fence(IDirect3DQuery9* q) { check("fence issue", q->Issue(D3DISSUE_END)); HRESULT hr; const DWORD start = GetTickCount(); while ((hr = q->GetData(nullptr, 0, D3DGETDATA_FLUSH)) == S_FALSE && GetTickCount() - start < 5000) Sleep(0); return hr; }
// Timestamp bracket of one GPU sequence (the fog fixture's law): begin / end stamps, frequency, disjoint.
struct Stamps {
    Com<IDirect3DQuery9> disjoint, frequency, begin, end, fence; bool ok = false;
    explicit Stamps(IDirect3DDevice9* d) {
        ok = SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &disjoint.p)) && SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &frequency.p)) &&
             SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &begin.p)) && SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &end.p));
        check("event query", d->CreateQuery(D3DQUERYTYPE_EVENT, &fence.p));
    }
    void open() { if (ok) { disjoint->Issue(D3DISSUE_BEGIN); begin->Issue(D3DISSUE_END); } }
    // GPU milliseconds of the bracket, or -1.
    double close() {
        if (!ok) return -1;
        end->Issue(D3DISSUE_END); frequency->Issue(D3DISSUE_END); disjoint->Issue(D3DISSUE_END); complete_fence(fence.p);
        BOOL dj = TRUE; UINT64 fq = 0, t0 = 0, t1 = 0;
        auto wait = [&](IDirect3DQuery9* q, void* data, DWORD size) { const DWORD start = GetTickCount(); HRESULT hr; while ((hr = q->GetData(data, size, D3DGETDATA_FLUSH)) == S_FALSE && GetTickCount() - start < 5000) Sleep(0); return hr; };
        if (wait(disjoint.p, &dj, sizeof dj) == S_OK && wait(frequency.p, &fq, sizeof fq) == S_OK && wait(begin.p, &t0, sizeof t0) == S_OK && wait(end.p, &t1, sizeof t1) == S_OK && !dj && fq && t1 >= t0) return double(t1 - t0) * 1000. / double(fq);
        return -1;
    }
};
// Full-screen quad through the production vs_3_0 pass-through (the resolve's own layout).
struct Quad {
    Com<IDirect3DVertexShader9> vs; Com<IDirect3DVertexDeclaration9> decl;
    explicit Quad(IDirect3DDevice9* d) { check("quad vs", d->CreateVertexShader(reinterpret_cast<const DWORD*>(x3m::renderer::quad_vertex_program()), &vs.p)); check("quad decl", d->CreateVertexDeclaration(x3m::renderer::quad_declaration, &decl.p)); }
    void draw(IDirect3DDevice9* d, UINT w, UINT h, IDirect3DPixelShader9* ps) {
        x3m::renderer::QuadVertex q[4]; x3m::renderer::quad_vertices(w, h, q);
        check("quad state", d->SetVertexShader(vs.p)); check("quad decl", d->SetVertexDeclaration(decl.p)); check("quad ps", d->SetPixelShader(ps));
        D3DVIEWPORT9 vp{0, 0, w, h, 0, 1}; check("quad viewport", d->SetViewport(&vp));
        check("quad draw", d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof q[0]));
    }
};
const float projection_m22 = 1.000003f, projection_m32 = -6.0000184f;
struct Targets {
    UINT w, h; Com<IDirect3DTexture9> scene, lane4, lane1, other; Com<IDirect3DSurface9> scene_s, lane4_s, lane1_s, other_s, depth, staging;
    Targets(IDirect3DDevice9* d, UINT width, UINT height) : w(width), h(height) {
        check("scene", d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &scene.p, nullptr)); check("scene s", scene->GetSurfaceLevel(0, &scene_s.p));
        check("lane4", d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &lane4.p, nullptr)); check("lane4 s", lane4->GetSurfaceLevel(0, &lane4_s.p));
        check("lane1", d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &lane1.p, nullptr)); check("lane1 s", lane1->GetSurfaceLevel(0, &lane1_s.p));
        check("other", d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &other.p, nullptr)); check("other s", other->GetSurfaceLevel(0, &other_s.p));
        check("depth", d->CreateDepthStencilSurface(w, h, D3DFMT_D24X8, D3DMULTISAMPLE_NONE, 0, TRUE, &depth.p, nullptr));
        check("staging", d->CreateOffscreenPlainSurface(w, h, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &staging.p, nullptr));
    }
    // The scene as floats (rgba per pixel).
    std::vector<float> read(IDirect3DDevice9* d, IDirect3DSurface9* source = nullptr) {
        check("readback", d->GetRenderTargetData(source ? source : scene_s.p, staging.p));
        D3DLOCKED_RECT r{}; check("lock staging", staging->LockRect(&r, nullptr, D3DLOCK_READONLY));
        std::vector<float> out(std::size_t(w) * h * 4);
        for (UINT y = 0; y < h; ++y) { auto* row = reinterpret_cast<const unsigned short*>(static_cast<const unsigned char*>(r.pBits) + y * r.Pitch); for (UINT x = 0; x < w * 4; ++x) out[std::size_t(y) * w * 4 + x] = half_to_float(row[x]); }
        staging->UnlockRect(); return out;
    }
};
struct Scene {
    IDirect3DDevice9* d; Targets& t; Quad& quad; Com<IDirect3DPixelShader9> fill_lane, fill_lane1, mrt, add_zero;
    Scene(IDirect3DDevice9* device, Targets& targets, Quad& q) : d(device), t(targets), quad(q) {
        std::vector<DWORD> code;
        // The lane: (z/w, 0, view depth, 0) inside the hull rectangle (pixels, c0 = x0 y0 x1 y1; c1.x = depth), the sentinel outside.
        compile("float4 rect:register(c0);float4 depth:register(c1);float4 main(float2 uv:TEXCOORD0):COLOR0{bool inside=uv.x>=rect.x&&uv.x<rect.z&&uv.y>=rect.y&&uv.y<rect.w;float z=depth.x;return inside?float4(1.000003+(-6.0000184)/z,0,z,0):float4(-1,0,0,0);}", "ps_3_0", code);
        check("lane ps", d->CreatePixelShader(code.data(), &fill_lane.p));
        compile("float4 rect:register(c0);float4 depth:register(c1);float4 main(float2 uv:TEXCOORD0):COLOR0{bool inside=uv.x>=rect.x&&uv.x<rect.z&&uv.y>=rect.y&&uv.y<rect.w;float z=depth.x;return inside?float4(1.000003+(-6.0000184)/z,0,0,0):float4(-1,0,0,0);}", "ps_3_0", code);
        check("lane1 ps", d->CreatePixelShader(code.data(), &fill_lane1.p));
        compile("struct O{float4 c0:COLOR0;float4 c1:COLOR1;float4 c2:COLOR2;};O main(float2 uv:TEXCOORD0){O o;o.c0=float4(0.001,0.001,0.002,0);o.c1=float4(0,0,0,1);o.c2=float4(-1,0,0,0);return o;}", "ps_3_0", code);
        check("mrt ps", d->CreatePixelShader(code.data(), &mrt.p));
        compile("float4 main(float2 uv:TEXCOORD0):COLOR0{return float4(0,0,0,0);}", "ps_3_0", code);
        check("add ps", d->CreatePixelShader(code.data(), &add_zero.p));
    }
    void baseline(bool four_channel, float x0, float y0, float x1, float y1, float depth) {
        check("rt0 lane", d->SetRenderTarget(0, four_channel ? t.lane4_s.p : t.lane1_s.p)); check("no rt1", d->SetRenderTarget(1, nullptr)); check("no rt2", d->SetRenderTarget(2, nullptr)); check("no ds", d->SetDepthStencilSurface(nullptr));
        const float rect[4] = {x0 / float(t.w), y0 / float(t.h), x1 / float(t.w), y1 / float(t.h)}, dz[4] = {depth, 0, 0, 0};
        check("rect", d->SetPixelShaderConstantF(0, rect, 1)); check("depth", d->SetPixelShaderConstantF(1, dz, 1));
        check("blend off", d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE)); check("z off", d->SetRenderState(D3DRS_ZENABLE, FALSE)); check("cull", d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
        quad.draw(d, t.w, t.h, four_channel ? fill_lane.p : fill_lane1.p);
        check("rt0 scene", d->SetRenderTarget(0, t.scene_s.p)); check("clear scene", d->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0));
        for (UINT i = 0; i < 7; ++i) { check("sampler", d->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_POINT)); check("sampler", d->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_POINT)); check("sampler", d->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_NONE)); check("sampler", d->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP)); check("sampler", d->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP)); }
    }
};
x3m::renderer::EffectsFrame frame_for(Targets& t, bool four_channel, float jx = 0.f, float jy = 0.f) {
    x3m::renderer::EffectsFrame f{}; f.width = t.w; f.height = t.h;
    const float rows[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}; std::memcpy(f.view_rows, rows, sizeof rows);
    f.m00 = 1.7f; f.m11 = 1.7f; f.m20 = 2.f * jx / float(t.w); f.m21 = -2.f * jy / float(t.h); f.m22 = projection_m22; f.m32 = projection_m32; f.near_z = 1.f;
    f.lane_four_channel = four_channel; f.lane = four_channel ? t.lane4.p : t.lane1.p;
    return f;
}
x3m::effects_stage::BoltInstance bolt(float x, float y, float z, float half_length, float half_width, float vx = 0.f) {
    x3m::effects_stage::BoltInstance b{}; b.centre[0] = x; b.centre[1] = y; b.centre[2] = z; b.axis[0] = 1.f; b.half_length = half_length; b.half_width = half_width; b.alpha = 1.f; b.uv[0] = b.uv[1] = 0.5f; b.period = 24;
    b.velocity[0] = vx; b.matched = vx != 0.f; return b;
}
// The four hit directions: 30 degrees from the front pole (the point facing the camera, local (0, 0, -1)) towards +x, -x, +y, -y.
const float hit_angle = 0.5235988f;
const float hit_meridians[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
x3m::effects_stage::ShellInstance shell(float x, float y, float z, float radius, unsigned hits, const float ages[4]) {
    x3m::effects_stage::ShellInstance s{}; s.centre[0] = x; s.centre[1] = y; s.centre[2] = z; s.axes[0] = radius; s.axes[4] = radius; s.axes[8] = radius; s.alpha = 1.f; s.tint[0] = s.tint[1] = s.tint[2] = 1.f;
    s.hit_count = hits;
    for (unsigned i = 0; i < hits && i < 4; ++i) { s.hits[i][0] = std::sin(hit_angle) * hit_meridians[i][0]; s.hits[i][1] = std::sin(hit_angle) * hit_meridians[i][1]; s.hits[i][2] = -std::cos(hit_angle); s.hits[i][3] = ages[i]; }
    return s;
}
// The window position of the shell point at angle phi from the front pole along a meridian (identity view, the fixture's projection).
void shell_point(float cz, float radius, float phi, const float* meridian, UINT w, UINT h, int& px, int& py) {
    const float x = radius * std::sin(phi) * meridian[0], y = radius * std::sin(phi) * meridian[1], z = cz - radius * std::cos(phi);
    px = int(((1.7f * x / z) + 1.f) * float(w) * .5f); py = int(((-(1.7f * y / z)) + 1.f) * float(h) * .5f);
}
// Pixel helpers on a readback (max over rgb).
float luma(const std::vector<float>& px, UINT w, int x, int y) { const std::size_t i = (std::size_t(y) * w + std::size_t(x)) * 4; return std::max(px[i], std::max(px[i + 1], px[i + 2])); }
float peak(const std::vector<float>& px, UINT w, int x0, int y0, int x1, int y1) { float m = 0; for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) m = std::max(m, luma(px, w, x, y)); return m; }
void window_of(float x, float y, float z, const x3m::renderer::EffectsFrame& f, float& px, float& py) { px = ((1.7f * x / z + f.m20) + 1.f) * float(f.width) * .5f; py = ((-(1.7f * y / z + f.m21)) + 1.f) * float(f.height) * .5f; }

void bolt_capsule_case(IDirect3DDevice9* d, Targets& t, Scene& scene, x3m::renderer::EffectsStagePass& pass, bool four_channel) {
    const x3m::renderer::EffectsStageTuning tuning{};
    auto run = [&](const x3m::effects_stage::BoltInstance& b) { x3m::effects_stage::BoltVertex v[4]; x3m::effects_stage::write_bolt_vertices(&b, 1, v, 1); auto f = frame_for(t, four_channel); f.bolt_vertices = v; f.bolt_instances = 1; f.tuning = &tuning; x3m::renderer::EffectsReport r; check("bolt run", pass.run(f, &r)); return r; };
    const float z = 1000.f; float cx, cy; window_of(0, 0, z, frame_for(t, four_channel), cx, cy);
    // Thin and long: the width rule widens it to W_min; the native length is 2 x 40 x 1.7 x W/2 / z pixels.
    scene.baseline(four_channel, 0, 0, 0, 0, 500); check("scene begin", d->BeginScene()); run(bolt(0, 0, z, 40.f, 0.3f)); check("scene end", d->EndScene());
    auto px = t.read(d); const int ix = int(cx), iy = int(cy);
    const float core = luma(px, t.w, ix, iy);
    int width = 0; for (int y = iy - 12; y <= iy + 12; ++y) width += luma(px, t.w, ix, y) >= .5f * tuning.core_intensity;
    int length = 0; for (int x = 0; x < int(t.w); ++x) length += luma(px, t.w, x, iy) >= .5f * tuning.core_intensity;
    const float native_length = 2.f * 40.f * 1.7f * float(t.w) * .5f / z;
    std::printf("BOLT_CAPSULE width=%u height=%u lane=%s core=%.3f core_width_px=%d length_px=%d native_length_px=%.1f halo=%.3f\n", t.w, t.h, four_channel ? "4ch" : "r32f", core, width, length, native_length, luma(px, t.w, ix, iy - 5));
    report(four_channel ? "bolt_core_bright_4ch" : "bolt_core_bright_r32f", core >= tuning.core_intensity * .9f);
    report(four_channel ? "bolt_core_width_4ch" : "bolt_core_width_r32f", width >= 3);
    report(four_channel ? "bolt_length_native_4ch" : "bolt_length_native_r32f", float(length) >= native_length * .95f);
    // Tiny: the length rule lengthens it to L_min x H / 1080.
    scene.baseline(four_channel, 0, 0, 0, 0, 500); check("scene begin", d->BeginScene()); run(bolt(0, 0, z, 0.5f, 0.3f)); check("scene end", d->EndScene());
    px = t.read(d); length = 0; for (int x = 0; x < int(t.w); ++x) length += luma(px, t.w, x, iy) >= .5f * tuning.core_intensity;
    const float l_min = tuning.min_length_px * float(t.h) / 1080.f;
    std::printf("BOLT_TINY lane=%s length_px=%d l_min=%.1f\n", four_channel ? "4ch" : "r32f", length, l_min);
    report(four_channel ? "bolt_length_min_4ch" : "bolt_length_min_r32f", float(length) >= l_min - 1.f);
    // Occluded: a hull at depth 500 over the right half of the screen hides that half of the bolt; the left half stays.
    scene.baseline(four_channel, cx, 0, float(t.w), float(t.h), 500); check("scene begin", d->BeginScene()); run(bolt(0, 0, z, 40.f, 0.3f)); check("scene end", d->EndScene());
    px = t.read(d); const float left = peak(px, t.w, ix - 40, iy - 4, ix - 10, iy + 4), right = peak(px, t.w, ix + 10, iy - 4, ix + 40, iy + 4);
    std::printf("BOLT_OCCLUDED lane=%s left=%.3f right=%.3f\n", four_channel ? "4ch" : "r32f", left, right);
    report(four_channel ? "bolt_occluded_4ch" : "bolt_occluded_r32f", left >= tuning.core_intensity * .9f && right <= 1e-3f);
    // Soft end: a hull just behind the bolt's depth (within its soft radius: max(0.3, 40 x 0.25) x SOFT = 5 units) halves it.
    scene.baseline(four_channel, cx, 0, float(t.w), float(t.h), z + 2.5f); check("scene begin", d->BeginScene()); run(bolt(0, 0, z, 40.f, 0.3f)); check("scene end", d->EndScene());
    px = t.read(d); const float soft = peak(px, t.w, ix + 10, iy - 4, ix + 40, iy + 4);
    std::printf("BOLT_SOFT lane=%s covered=%.3f open=%.3f\n", four_channel ? "4ch" : "r32f", soft, peak(px, t.w, ix - 40, iy - 4, ix - 10, iy + 4));
    report(four_channel ? "bolt_soft_end_4ch" : "bolt_soft_end_r32f", soft > .25f * tuning.core_intensity && soft < .75f * tuning.core_intensity);
}
void shell_case(IDirect3DDevice9* d, Targets& t, Scene& scene, x3m::renderer::EffectsStagePass& pass) {
    const x3m::renderer::EffectsStageTuning tuning{};
    const float z = 800.f, radius = 100.f; const float ages[4] = {0.05f, 0.10f, 0.15f, 0.20f};
    auto run = [&](const x3m::effects_stage::ShellInstance& s, const x3m::effects_stage::DecalVertex* decal) { auto f = frame_for(t, true); f.shells = &s; f.shell_count = 1; f.tuning = &tuning; if (decal) { f.decal_vertices = decal; f.decal_count = 1; f.shell_count = 0; } x3m::renderer::EffectsReport r; check("shell run", pass.run(f, &r)); return r; };
    float cx, cy; window_of(0, 0, z, frame_for(t, true), cx, cy); const float r_px = radius * 1.7f * float(t.w) * .5f / z; // the projected radius, near enough for the readback windows
    scene.baseline(true, 0, 0, 0, 0, 500); check("scene begin", d->BeginScene()); run(shell(0, 0, z, radius, 4, ages), nullptr); check("scene end", d->EndScene());
    auto px = t.read(d);
    const float centre = luma(px, t.w, int(cx), int(cy));
    // The rim, on its own draw with one old hit (age 0.5 s: its ring far behind, the rim envelope at 1 - 0.5 / 0.6): 70
    // degrees from the front pole on eight meridians (inside the icosphere's polygonal silhouette, which lies up to 3 px
    // inside the sphere's) against 45 degrees on the same meridians: the Fresnel term rises towards the silhouette.
    float rim = 0, mid = 0;
    {
        const float old_age[4] = {0.5f, 0.f, 0.f, 0.f};
        scene.baseline(true, 0, 0, 0, 0, 500); check("scene begin", d->BeginScene()); run(shell(0, 0, z, radius, 1, old_age), nullptr); check("scene end", d->EndScene());
        const auto rim_px = t.read(d); std::printf("SHELL_MERIDIANS");
        for (int k = 0; k < 8; ++k) { const float m[2] = {std::cos(float(k) * .7853982f), std::sin(float(k) * .7853982f)}; int rx, ry, mx, my; shell_point(z, radius, 1.2217305f, m, t.w, t.h, rx, ry); shell_point(z, radius, .7853982f, m, t.w, t.h, mx, my);
            const float r70 = peak(rim_px, t.w, rx - 1, ry - 1, rx + 2, ry + 2), r45 = peak(rim_px, t.w, mx - 1, my - 1, mx + 2, my + 2); std::printf(" k%d=%.3f/%.3f", k, r70, r45); rim = std::max(rim, r70); mid = std::max(mid, r45); }
        std::printf("\n");
    }
    // Rings: hit i sits 30 degrees from the pole on its meridian; its ring is theta = ring_speed x age away from it, so the ring
    // crosses the meridian at 30 degrees - theta from the pole (negative: past the pole); the off sample lies 3 ring widths
    // further out on the same meridian, where no other hit's ring runs.
    unsigned rings = 0; std::printf("SHELL_RINGS");
    for (unsigned i = 0; i < 4; ++i) {
        const float theta = tuning.ring_speed * ages[i], on_phi = hit_angle - theta, off_phi = on_phi + 3.f * tuning.ring_width;
        int rx, ry, ox, oy; shell_point(z, radius, on_phi, hit_meridians[i], t.w, t.h, rx, ry); shell_point(z, radius, off_phi, hit_meridians[i], t.w, t.h, ox, oy);
        const float on = peak(px, t.w, rx - 2, ry - 2, rx + 3, ry + 3), off = peak(px, t.w, ox - 2, oy - 2, ox + 3, oy + 3);
        std::printf(" ring%u=%.3f/%.3f", i, on, off); rings += on > 2.f * off && on > .2f;
    }
    const float open = peak(px, t.w, int(cx - r_px) - 2, int(cy - r_px) - 2, int(cx + r_px) + 3, int(cy + r_px) + 3);
    std::printf("\nSHELL centre=%.4f rim=%.4f mid=%.4f rings=%u open=%.4f r_px=%.1f\n", centre, rim, mid, rings, open, r_px);
    report("shell_rim", rim > 2.f * std::max(mid, 1e-3f) && rim > .05f);
    report("shell_four_rings", rings == 4);
    // Soft hull cut: a hull at depth 650 over the left half hides the left half of the shell (its nearest point is at 700).
    scene.baseline(true, 0, 0, cx, float(t.h), 650.f); check("scene begin", d->BeginScene()); run(shell(0, 0, z, radius, 4, ages), nullptr); check("scene end", d->EndScene());
    px = t.read(d); const float left = peak(px, t.w, int(cx - r_px) - 2, int(cy - r_px) - 2, int(cx) - 2, int(cy + r_px) + 3), right = peak(px, t.w, int(cx) + 2, int(cy - r_px) - 2, int(cx + r_px) + 3, int(cy + r_px) + 3);
    // A hull inside the soft radius (100 x SOFT 0.5 = 50 units) at 720 leaves the front partially visible: between 5 and 95 % of the open peak.
    scene.baseline(true, 0, 0, float(t.w), float(t.h), 720.f); check("scene begin", d->BeginScene()); run(shell(0, 0, z, radius, 4, ages), nullptr); check("scene end", d->EndScene());
    const auto soft_px = t.read(d); const float soft = peak(soft_px, t.w, int(cx - r_px) - 2, int(cy - r_px) - 2, int(cx + r_px) + 3, int(cy + r_px) + 3);
    std::printf("SHELL_CUT left=%.4f right=%.4f soft=%.4f open=%.4f\n", left, right, soft, open);
    report("shell_hull_cut", left <= 1e-3f && right > .1f);
    report("shell_soft_cut", soft > .05f * open && soft < .95f * open);
    // The decal: a quad at the hit point on a hull at its own depth (the two-sided affinity), nothing over open sky.
    x3m::effects_stage::DecalVertex decal[4]; const float P[3] = {0, 0, z}; x3m::effects_stage::write_decal_vertices(P, 60.f, 0.1f, 1.f, decal);
    scene.baseline(true, 0, 0, float(t.w), float(t.h), z); check("scene begin", d->BeginScene()); run(shell(0, 0, z, radius, 0, ages), decal); check("scene end", d->EndScene());
    px = t.read(d); const float on_hull = peak(px, t.w, int(cx - 40), int(cy - 40), int(cx + 40), int(cy + 40));
    scene.baseline(true, 0, 0, 0, 0, z); check("scene begin", d->BeginScene()); run(shell(0, 0, z, radius, 0, ages), decal); check("scene end", d->EndScene());
    px = t.read(d); const float on_sky = peak(px, t.w, int(cx - 40), int(cy - 40), int(cx + 40), int(cy + 40));
    std::printf("DECAL on_hull=%.4f on_sky=%.4f\n", on_hull, on_sky);
    report("decal_on_hull", on_hull > .1f); report("decal_not_on_sky", on_sky <= 1e-3f);
}
// A moving bolt or a shell through the real resolve: 8 frames at 8 jitter phases; the resolved core against the current
// frame's core, and the trail behind the capsule over dark sky.
struct StageContext { x3m::renderer::EffectsStagePass* pass; x3m::renderer::EffectsFrame frame; HRESULT result; unsigned calls; };
HRESULT stage_callback(void* context, IDirect3DDevice9*) noexcept { auto* c = static_cast<StageContext*>(context); x3m::renderer::EffectsReport r; c->result = c->pass->run(c->frame, &r); c->calls = r.calls; return c->result; }
HRESULT idle_callback(void*, IDirect3DDevice9*) noexcept { return S_FALSE; }
float halton(unsigned i, unsigned b) { float f = 1, r = 0; while (i) { f /= float(b); r += f * float(i % b); i /= b; } return r; }
void resolve_case(IDirect3DDevice9* d, Targets& t, Scene& scene, x3m::renderer::EffectsStagePass& pass, bool shell_case_flag, float speed_px) {
    x3m::renderer::TemporalPass taa; check("taa initialize", taa.initialize(d, nullptr, reinterpret_cast<const DWORD*>(x3m::renderer::temporal_resolve_program())));
    const x3m::renderer::EffectsStageTuning tuning{};
    const float z = 1000.f; const float speed_world = speed_px * z / (1.7f * float(t.w) * .5f); // world units per frame for speed_px pixels
    float x = -speed_world * 4.f; x3m::effects_stage::BoltVertex v[4]; const float ages[4] = {0.1f, 0.f, 0.f, 0.f};
    x3m::effects_stage::ShellInstance s = shell(0, 0, 800.f, 100.f, 1, ages);
    x3m::renderer::Output out{}; float current_peak = 0, cx = 0, cy = 0;
    for (unsigned frame = 0; frame < 8; ++frame) {
        const float jx = halton(frame + 1, 2) - .5f, jy = halton(frame + 1, 3) - .5f;
        scene.baseline(true, 0, 0, 0, 0, 500);
        StageContext ctx{&pass, frame_for(t, true, jx, jy), S_OK, 0}; ctx.frame.tuning = &tuning;
        if (shell_case_flag) { s.hits[0][3] = 0.1f + .016f * float(frame); ctx.frame.shells = &s; ctx.frame.shell_count = 1; }
        else { const auto b = bolt(x, 0, z, 40.f, 0.3f, speed_world); x3m::effects_stage::write_bolt_vertices(&b, 1, v, 1); ctx.frame.bolt_vertices = v; ctx.frame.bolt_instances = 1; }
        x3m::renderer::FrameInputs in; in.color = t.scene.p; in.current_depth = t.lane1.p; in.width = t.w; in.height = t.h; in.epoch = 1;
        const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; std::copy(identity, identity + 16, in.clip_to_previous);
        in.motion_policy = x3m::renderer::MotionPolicy::KnownCameraOnly; in.reactive_policy = x3m::renderer::ReactivePolicy::KnownNonReactive; in.weight = .9f; in.history_allowed = true;
        in.current_jitter[0] = jx; in.current_jitter[1] = jy; in.caller_scene_open = true; in.caller_queries_idle = true; in.stage_callback = &stage_callback; in.stage_context = &ctx;
        // The R32F lane must carry the hull-free sky for the resolve: baseline filled lane1 only when four_channel is false; fill it now.
        check("rt0 lane1", d->SetRenderTarget(0, t.lane1_s.p)); const float rect[4] = {0, 0, 0, 0}, dz[4] = {500, 0, 0, 0}; check("rect", d->SetPixelShaderConstantF(0, rect, 1)); check("depth", d->SetPixelShaderConstantF(1, dz, 1)); scene.quad.draw(d, t.w, t.h, scene.fill_lane1.p); check("rt0 scene", d->SetRenderTarget(0, t.scene_s.p));
        check("resolve begin", d->BeginScene()); check("resolve run", taa.run(in, &out)); check("resolve end", d->EndScene());
        report("resolve_stage_ran", taa.diagnostics().stage_ran && SUCCEEDED(ctx.result));
        if (frame == 7) { const auto px = t.read(d); window_of(x, 0, shell_case_flag ? 800.f : z, ctx.frame, cx, cy); const float r_px = shell_case_flag ? 100.f * 1.7f * float(t.w) * .5f / 800.f : 8.f; current_peak = peak(px, t.w, int(cx - r_px - 40), int(cy - r_px - 8), int(cx + r_px + 40), int(cy + r_px + 8)); }
        x += speed_world;
    }
    Com<IDirect3DSurface9> resolved; check("resolved surface", out.color->GetSurfaceLevel(0, &resolved.p));
    const auto res = t.read(d, resolved.p);
    const float r_px = shell_case_flag ? 100.f * 1.7f * float(t.w) * .5f / 800.f : 8.f;
    const float resolved_peak = peak(res, t.w, int(cx - r_px - 40), int(cy - r_px - 8), int(cx + r_px + 40), int(cy + r_px + 8));
    // Trail: pixels behind the drawn capsule's back end (centre - travel - half length - halo) over dark sky above 5 % of the core.
    int trail = 0;
    if (!shell_case_flag) {
        const float half_len_px = std::max(40.f * 1.7f * float(t.w) * .5f / z, tuning.min_length_px * float(t.h) / 1080.f * .5f), travel = speed_px * tuning.stretch, halo = std::max(1.5f, .3f * 1.7f * float(t.w) * .5f / z) * tuning.halo;
        const int back = int(cx - half_len_px - travel - halo) - 1;
        for (int px_x = back; px_x >= 0 && px_x > back - 64; --px_x) { float m = 0; for (int y = int(cy) - 6; y <= int(cy) + 6; ++y) m = std::max(m, luma(res, t.w, px_x, y)); if (m > .05f * current_peak) ++trail; else break; }
    }
    std::printf("RESOLVE %s speed_px=%.0f current=%.3f resolved=%.3f ratio=%.3f trail_px=%d calls=%u\n", shell_case_flag ? "shell" : "bolt", speed_px, current_peak, resolved_peak, current_peak > 0 ? resolved_peak / current_peak : 0, trail, 0u);
    char label[64]; std::snprintf(label, sizeof label, "resolve_%s_%.0fpx_core", shell_case_flag ? "shell" : "bolt", speed_px); report(label, current_peak > 0 && resolved_peak / current_peak >= .9f);
    if (!shell_case_flag) { std::snprintf(label, sizeof label, "resolve_bolt_%.0fpx_trail", speed_px); report(label, trail <= 3); }
    // The off path through the resolve: a callback that draws nothing gives the run without the field byte for byte.
    if (!shell_case_flag && speed_px == 0.f) {
        x3m::renderer::TemporalPass a, b; check("a", a.initialize(d, nullptr, reinterpret_cast<const DWORD*>(x3m::renderer::temporal_resolve_program()))); check("b", b.initialize(d, nullptr, reinterpret_cast<const DWORD*>(x3m::renderer::temporal_resolve_program())));
        std::vector<float> outputs[2];
        for (unsigned which = 0; which < 2; ++which) {
            scene.baseline(true, 0, 0, 0, 0, 500); check("scene begin", d->BeginScene()); { auto b0 = bolt(0, 0, z, 40.f, 0.3f); x3m::effects_stage::write_bolt_vertices(&b0, 1, v, 1); auto f = frame_for(t, true); f.bolt_vertices = v; f.bolt_instances = 1; f.tuning = &tuning; x3m::renderer::EffectsReport r; check("pre draw", pass.run(f, &r)); } check("scene end", d->EndScene());
            check("rt0 lane1", d->SetRenderTarget(0, t.lane1_s.p)); const float rect[4] = {0, 0, 0, 0}, dz[4] = {500, 0, 0, 0}; check("rect", d->SetPixelShaderConstantF(0, rect, 1)); check("depth", d->SetPixelShaderConstantF(1, dz, 1)); scene.quad.draw(d, t.w, t.h, scene.fill_lane1.p); check("rt0 scene", d->SetRenderTarget(0, t.scene_s.p));
            x3m::renderer::FrameInputs in; in.color = t.scene.p; in.current_depth = t.lane1.p; in.width = t.w; in.height = t.h; in.epoch = 1;
            const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; std::copy(identity, identity + 16, in.clip_to_previous);
            in.motion_policy = x3m::renderer::MotionPolicy::KnownCameraOnly; in.reactive_policy = x3m::renderer::ReactivePolicy::KnownNonReactive; in.weight = .9f; in.history_allowed = true; in.caller_scene_open = true; in.caller_queries_idle = true;
            if (which == 1) { in.stage_callback = &idle_callback; }
            x3m::renderer::Output o{}; check("resolve begin", d->BeginScene()); check("resolve run", (which ? b : a).run(in, &o)); check("resolve end", d->EndScene());
            Com<IDirect3DSurface9> rs; check("resolved surface", o.color->GetSurfaceLevel(0, &rs.p)); outputs[which] = t.read(d, rs.p);
        }
        report("off_path_resolve_identical", outputs[0] == outputs[1]);
    }
}
void off_path_case(IDirect3DDevice9* d, Targets& t, Scene& scene, x3m::renderer::EffectsStagePass& pass) {
    scene.baseline(true, 0, 0, 0, 0, 500); const auto before = t.read(d);
    check("scene begin", d->BeginScene()); auto f = frame_for(t, true); x3m::renderer::EffectsReport r; const HRESULT hr = pass.run(f, &r); check("scene end", d->EndScene());
    const auto after = t.read(d);
    std::printf("OFF_PATH result=%08lx calls=%u identical=%u\n", hr, r.calls, unsigned(before == after));
    report("off_path_no_record", hr == S_FALSE && r.calls == 0 && before == after);
}
void suppression_case(IDirect3DDevice9* d, Targets& t, Scene& scene, x3m::renderer::EffectsStagePass& pass) {
    // The stage's admission rule: armed -> the record is taken (not forwarded); disarmed or failed -> forwarded; never both.
    x3m::effects_stage::Arming arming; unsigned taken = 0, forwarded = 0, both = 0;
    auto draw = [&](std::uint64_t frame, bool resources) { const bool armed = arming.armed(frame, resources, true, true); const bool take = armed, forward = !armed; taken += take; forwarded += forward; both += take && forward; };
    for (std::uint64_t frame = 1; frame <= 10; ++frame) draw(frame, true);
    const unsigned taken_armed = taken;
    draw(11, false); // resources missing: forwarded
    // A failed stage (fault: the bolt draw fails once) disarms for 64 frames.
    scene.baseline(true, 0, 0, 0, 0, 500); check("scene begin", d->BeginScene());
    const auto b = bolt(0, 0, 1000.f, 40.f, 0.3f); x3m::effects_stage::BoltVertex v[4]; x3m::effects_stage::write_bolt_vertices(&b, 1, v, 1);
    auto f = frame_for(t, true); f.bolt_vertices = v; f.bolt_instances = 1; x3m::renderer::EffectsReport r;
    pass.set_faults(2); const HRESULT failed = pass.run(f, &r); pass.set_faults(0);
    if (FAILED(failed)) arming.fail(12);
    for (std::uint64_t frame = 12; frame < 12 + 64; ++frame) draw(frame, true);
    const unsigned forwarded_window = forwarded;
    draw(76, true);
    const HRESULT again = pass.run(f, &r); check("scene end", d->EndScene());
    std::printf("SUPPRESSION taken_armed=%u forwarded_missing=1 failed=%08lx step=%u forwarded_window=%u taken_after=%u both=%u again=%08lx\n", taken_armed, failed, unsigned(r.failed), forwarded_window - 1, taken - taken_armed, both, again);
    report("suppression_armed_taken", taken_armed == 10);
    report("suppression_failed_forwarded", FAILED(failed) && forwarded_window == 65 && taken == 11 && both == 0);
    report("suppression_recovers", SUCCEEDED(again));
}
void reset_case(IDirect3DDevice9* d, D3DPRESENT_PARAMETERS& pp, x3m::renderer::EffectsStagePass& pass, Targets*& t, Scene*& scene, Quad& quad) {
    const unsigned before = pass.references();
    pass.before_reset(); const unsigned released = pass.references();
    delete scene; scene = nullptr; delete t; t = nullptr;
    const HRESULT reset = d->Reset(&pp); pass.after_reset(reset);
    const HRESULT ensured = pass.ensure_resources(); const unsigned after = pass.references();
    t = new Targets(d, 1920, 1080); scene = new Scene(d, *t, quad);
    scene->baseline(true, 0, 0, 0, 0, 500); check("scene begin", d->BeginScene());
    const auto b = bolt(0, 0, 1000.f, 40.f, 0.3f); x3m::effects_stage::BoltVertex v[4]; x3m::effects_stage::write_bolt_vertices(&b, 1, v, 1);
    auto f = frame_for(*t, true); f.bolt_vertices = v; f.bolt_instances = 1; x3m::renderer::EffectsReport r; const HRESULT run = pass.run(f, &r); check("scene end", d->EndScene());
    std::printf("RESET before=%u released=%u reset=%08lx ensured=%08lx after=%u run=%08lx bolts=%u\n", before, released, reset, ensured, after, run, r.bolts);
    report("reset_released", released == before - 4 && before == 13);
    report("reset_recreated", SUCCEEDED(reset) && SUCCEEDED(ensured) && after == before && SUCCEEDED(run) && r.bolts == 1);
}
// One frame tail (the shape the stage rides in production): an MRT quad into RT0 + RT1 + RT2 with the depth surface
// attached, the unbind the resolve's normalize does, optionally a fog-like additive quad on RT0 (`ride`: the pass a
// sun-apply or fog quad opened), optionally the stage, then the resolve's first target change with a quad on the other
// target. Fenced wall time of the whole tail (this backend answers no TIMESTAMP query; the fog fixture's law); the
// stage's CPU submit is measured separately so the on/off difference minus it is the GPU share.
double tail_ms(IDirect3DDevice9* d, Targets& t, Scene& scene, x3m::renderer::EffectsStagePass& pass, Stamps& stamps, bool ride, const x3m::renderer::EffectsFrame* stage, double* submit, bool* depth_bound) {
    LARGE_INTEGER freq{}; QueryPerformanceFrequency(&freq);
    auto ms = [&](LONGLONG a, LONGLONG b) { return double(b - a) * 1000. / double(freq.QuadPart); };
    check("scene begin", d->BeginScene()); check("lead fence", complete_fence(stamps.fence.p)); LARGE_INTEGER a{}, s0{}, s1{}, c{}; QueryPerformanceCounter(&a);
    check("rt0", d->SetRenderTarget(0, t.scene_s.p)); check("rt1", d->SetRenderTarget(1, t.other_s.p)); check("rt2", d->SetRenderTarget(2, t.lane4_s.p)); check("ds", d->SetDepthStencilSurface(t.depth.p));
    check("blend off", d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE)); check("z", d->SetRenderState(D3DRS_ZENABLE, FALSE));
    scene.quad.draw(d, t.w, t.h, scene.mrt.p);
    IDirect3DSurface9* ds = nullptr; if (SUCCEEDED(d->GetDepthStencilSurface(&ds)) && ds) { if (depth_bound) *depth_bound = true; ds->Release(); }
    check("no rt1", d->SetRenderTarget(1, nullptr)); check("no rt2", d->SetRenderTarget(2, nullptr)); check("no ds", d->SetDepthStencilSurface(nullptr));
    if (ride) { check("blend on", d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE)); check("src", d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE)); check("dst", d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE)); scene.quad.draw(d, t.w, t.h, scene.add_zero.p); }
    QueryPerformanceCounter(&s0);
    if (stage) { x3m::renderer::EffectsReport r; check("stage", pass.run(*stage, &r)); }
    QueryPerformanceCounter(&s1);
    check("rt other", d->SetRenderTarget(0, t.other_s.p)); check("blend off", d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE)); scene.quad.draw(d, t.w, t.h, scene.add_zero.p);
    check("rt scene", d->SetRenderTarget(0, t.scene_s.p));
    check("tail fence", complete_fence(stamps.fence.p)); QueryPerformanceCounter(&c); check("scene end", d->EndScene());
    if (submit) *submit = ms(s0.QuadPart, s1.QuadPart);
    return ms(a.QuadPart, c.QuadPart);
}
// The marginal cost of the stage's draws in the frame tail: 3 warm pairs and 15 measured pairs, stage on and off alternating.
void timing_case(IDirect3DDevice9* d, Targets& t, Scene& scene, x3m::renderer::EffectsStagePass& pass, const char* label, unsigned bolts, unsigned shells, float shell_radius, float shell_z) {
    const x3m::renderer::EffectsStageTuning tuning{}; Stamps stamps(d);
    std::vector<x3m::effects_stage::BoltVertex> v(std::size_t(bolts) * 4 + 4); std::vector<x3m::effects_stage::BoltInstance> inst;
    for (unsigned i = 0; i < bolts; ++i) inst.push_back(bolt(-400.f + 13.f * float(i), -200.f + 7.f * float(i % 17), 1000.f + 20.f * float(i % 5), 40.f, 0.3f, 8.f));
    x3m::effects_stage::write_bolt_vertices(inst.data(), bolts, v.data(), bolts);
    const float ages[4] = {0.05f, 0.15f, 0.25f, 0.35f}; std::vector<x3m::effects_stage::ShellInstance> sh; for (unsigned i = 0; i < shells; ++i) sh.push_back(shell(-150.f + 300.f * float(i), 0, shell_z, shell_radius, 4, ages));
    auto f = frame_for(t, true); f.tuning = &tuning; f.bolt_vertices = v.data(); f.bolt_instances = bolts; f.shells = sh.data(); f.shell_count = shells;
    std::vector<double> on, off, submit; unsigned calls = 0;
    { check("scene begin", d->BeginScene()); x3m::renderer::EffectsReport r; check("calls", pass.run(f, &r)); calls = r.calls; check("scene end", d->EndScene()); }
    for (unsigned i = 0; i < 36; ++i) { const bool run = i % 2 == 0; double s = 0; const double ms = tail_ms(d, t, scene, pass, stamps, false, run ? &f : nullptr, &s, nullptr); if (i < 6) continue; (run ? on : off).push_back(ms); if (run) submit.push_back(s); }
    const double chain = median(on) - median(off), gpu = std::max(0., chain - median(submit));
    std::printf("TIMING %s width=%u height=%u bolts=%u shells=%u fenced_on_ms=%.4f fenced_off_ms=%.4f submit_ms=%.4f chain_ms=%.4f gpu_ms=%.4f samples=%zu calls=%u method=tail_fenced\n",
                label, t.w, t.h, bolts, shells, median(on), median(off), median(submit), chain, gpu, on.size(), calls);
}
// The pass-opening price (section 9, work order 1): the tail with the stage riding a fog-like quad's RT0-only pass
// against the tail where the stage opens its own pass right after the RT1/RT2 unbind, each on and off.
void pass_open_case(IDirect3DDevice9* d, Targets& t, Scene& scene, x3m::renderer::EffectsStagePass& pass) {
    const x3m::renderer::EffectsStageTuning tuning{}; Stamps stamps(d);
    std::vector<x3m::effects_stage::BoltVertex> v(60 * 4); std::vector<x3m::effects_stage::BoltInstance> inst;
    for (unsigned i = 0; i < 60; ++i) inst.push_back(bolt(-400.f + 13.f * float(i), -200.f + 7.f * float(i % 17), 1000.f, 40.f, 0.3f, 8.f));
    x3m::effects_stage::write_bolt_vertices(inst.data(), 60, v.data(), 60);
    auto f = frame_for(t, true); f.tuning = &tuning; f.bolt_vertices = v.data(); f.bolt_instances = 60;
    double results[2][2], submits[2] = {0, 0}; bool depth_bound = false;
    for (unsigned ride = 0; ride < 2; ++ride) for (unsigned stage = 0; stage < 2; ++stage) {
        std::vector<double> gpu, submit;
        for (unsigned i = 0; i < 18; ++i) { double s = 0; const double ms = tail_ms(d, t, scene, pass, stamps, ride != 0, stage ? &f : nullptr, &s, &depth_bound); if (i >= 3) { gpu.push_back(ms); if (stage) submit.push_back(s); } }
        results[ride][stage] = median(gpu); if (stage) submits[ride] = median(submit);
    }
    const double own = std::max(0., results[0][1] - results[0][0] - submits[0]), riding = std::max(0., results[1][1] - results[1][0] - submits[1]);
    std::printf("PASS_OPEN width=%u height=%u own_off_ms=%.4f own_on_ms=%.4f own_submit_ms=%.4f ride_off_ms=%.4f ride_on_ms=%.4f ride_submit_ms=%.4f own_stage_ms=%.4f ride_stage_ms=%.4f own_pass_ms=%.4f gate_ms=0.1 depth_bound_before_unbind=%u method=tail_fenced\n",
                t.w, t.h, results[0][0], results[0][1], submits[0], results[1][0], results[1][1], submits[1], own, riding, own - riding, unsigned(depth_bound));
    report("pass_open_measured", results[0][1] > 0 && results[1][1] > 0);
}
void fp16_refused_case(IDirect3DDevice9* d, const D3DCAPS9& caps, D3DFORMAT format) {
    x3m::renderer::EffectsStagePass refused; refused.set_faults(1);
    const HRESULT hr = refused.attach(d, *reinterpret_cast<void* const* const*>(d), caps, format);
    std::printf("FP16_REFUSED result=%08lx enabled=%u reason=%s references=%u\n", hr, unsigned(refused.caps().enabled), refused.caps().reason, refused.references());
    report("fp16_refused", FAILED(hr) && !refused.caps().enabled && !std::strcmp(refused.caps().reason, "fp16_blending") && refused.references() == 0);
}
#ifdef X3M_EFFECTS_KEYS_FIXTURE
int keys_main(IDirect3DDevice9* d, int argc, char** argv) {
    using namespace x3m::ownership;
    using CreateFromFileFn = HRESULT(WINAPI*)(IDirect3DDevice9*, LPCSTR, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, DWORD, DWORD, D3DCOLOR, D3DXIMAGE_INFO*, PALETTEENTRY*, IDirect3DTexture9**);
    CreateFromFileFn create_from_file = nullptr; auto fp = GetProcAddress(d3dx_module, "D3DXCreateTextureFromFileExA"); std::memcpy(&create_from_file, &fp, sizeof create_from_file);
    if (!create_from_file) throw std::runtime_error("D3DXCreateTextureFromFileExA");
    // D3DX loads through the wrapper (CreateTexture, GetSurfaceLevel, LockRect): the upload-time key must equal the table's.
    for (int i = 0; i + 1 < argc; i += 2) {
        Com<IDirect3DTexture9> tex;
        const HRESULT hr = create_from_file(d, argv[i], D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED, D3DX_FILTER_NONE, D3DX_FILTER_NONE, 0, nullptr, nullptr, &tex.p);
        TextureKeyView view{}; const HRESULT q = SUCCEEDED(hr) ? get_texture_key_view(tex.p, &view) : E_FAIL;
        char key[24]; std::snprintf(key, sizeof key, "%016llx", static_cast<unsigned long long>(view.key));
        const bool match = SUCCEEDED(hr) && SUCCEEDED(q) && view.known && !std::strcmp(key, argv[i + 1]);
        const char* name = std::strrchr(argv[i], '\\') ? std::strrchr(argv[i], '\\') + 1 : argv[i];
        std::printf("KEYS name=%s load=%08lx query=%08lx known=%u key=%s expected=%s match=%u source=%u uploads=%u size=%lux%lu format=%lu\n", name, hr, q, unsigned(view.known), key, argv[i + 1], unsigned(match), view.source, view.uploads, static_cast<unsigned long>(view.width), static_cast<unsigned long>(view.height), static_cast<unsigned long>(view.format));
        report("texture_key_matches_table", match);
    }
    // The three paths on known bytes: the texture's own LockRect (source 1), its level-0 surface's (2), the read-only fallback (3).
    std::vector<unsigned char> bytes(64 * 64 * 4); for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = (unsigned char)((i * 2654435761u) >> 13);
    const std::uint64_t expected = x3m::effects_stage::sparse_key(64, 64, x3m::effects_stage::fmt_a8r8g8b8, 64, 256, bytes.data(), 256);
    auto upload = [&](IDirect3DTexture9* tex, bool through_surface, bool readonly_pass) {
        if (through_surface) { Com<IDirect3DSurface9> s; check("level", tex->GetSurfaceLevel(0, &s.p)); D3DLOCKED_RECT r{}; check("slock", s->LockRect(&r, nullptr, 0)); for (unsigned y = 0; y < 64; ++y) std::memcpy(static_cast<unsigned char*>(r.pBits) + y * r.Pitch, bytes.data() + y * 256, 256); check("sunlock", s->UnlockRect()); }
        else { D3DLOCKED_RECT r{}; check("tlock", tex->LockRect(0, &r, nullptr, readonly_pass ? D3DLOCK_READONLY : 0)); if (!readonly_pass) for (unsigned y = 0; y < 64; ++y) std::memcpy(static_cast<unsigned char*>(r.pBits) + y * r.Pitch, bytes.data() + y * 256, 256); check("tunlock", tex->UnlockRect(0)); }
    };
    for (unsigned path = 1; path <= 3; ++path) {
        Com<IDirect3DTexture9> tex;
        if (path == 3) check("disarm", configure_texture_upload_keys(d, false));
        check("create", d->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex.p, nullptr));
        upload(tex.p, path == 2, false);
        if (path == 3) check("arm", configure_texture_upload_keys(d, true));
        TextureKeyView view{}; check("view", get_texture_key_view(tex.p, &view));
        const bool unknown_before = !view.known;
        if (path == 3) check("readonly", compute_texture_key_readonly(tex.p, &view));
        // A read-only lock after the key never rehashes (uploads stay); a second write does.
        upload(tex.p, false, true); TextureKeyView after{}; check("view after", get_texture_key_view(tex.p, &after));
        std::printf("KEY_PATH source=%u known=%u match=%u uploads=%u unknown_before_fallback=%u readonly_kept=%u\n", view.source, unsigned(view.known), unsigned(view.known && view.key == expected), view.uploads, unsigned(unknown_before), unsigned(after.uploads == view.uploads && after.key == view.key));
        char label[40]; std::snprintf(label, sizeof label, "key_path_%u", path);
        report(label, view.known && view.key == expected && view.source == path && (path != 3 || unknown_before) && after.key == view.key);
    }
    // A DEFAULT-pool texture (no observed upload, not lockable) stays unknown through the fallback: fail closed.
    Com<IDirect3DTexture9> def; check("default", d->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &def.p, nullptr));
    TextureKeyView view{}; check("fallback default", compute_texture_key_readonly(def.p, &view)); TextureKeyView again{}; check("again", compute_texture_key_readonly(def.p, &again));
    std::printf("KEY_DEFAULT known=%u source=%u\n", unsigned(view.known), view.source);
    report("key_default_pool_unknown", !view.known && !again.known);
    // A writable upload of a DEFAULT-pool (dynamic) texture is not keyed either: its level 0 names a moment, not an asset.
    Com<IDirect3DTexture9> dynamic; check("dynamic", d->CreateTexture(64, 64, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &dynamic.p, nullptr));
    upload(dynamic.p, false, false); TextureKeyView dyn{}; check("dynamic view", get_texture_key_view(dynamic.p, &dyn));
    std::printf("KEY_DYNAMIC known=%u uploads=%u\n", unsigned(dyn.known), dyn.uploads);
    report("key_dynamic_default_pool_unknown", !dyn.known && dyn.uploads == 0);
    return 0;
}
#endif
} // namespace
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        if (argc < 2) { std::puts("usage: effects_stage_fixture <d3dx9_37.dll> [--keys <dds> <key> ...]"); return 2; }
        d3dx_module = LoadLibraryA(argv[1]); if (!d3dx_module) throw std::runtime_error("d3dx9_37.dll");
        auto cp = GetProcAddress(d3dx_module, "D3DXCompileShader"); std::memcpy(&compiler, &cp, sizeof compiler); if (!compiler) throw std::runtime_error("D3DXCompileShader");
        WNDCLASSA cls{}; cls.lpfnWndProc = DefWindowProcA; cls.hInstance = GetModuleHandleA(nullptr); cls.lpszClassName = "X3EffectsStageFixture"; RegisterClassA(&cls);
        HWND window = CreateWindowA(cls.lpszClassName, "X3 effects stage fixture", WS_OVERLAPPEDWINDOW, 90, 90, 128, 128, nullptr, nullptr, cls.hInstance, nullptr);
        if (!window) throw std::runtime_error("window");
        HMODULE runtime = LoadLibraryA("d3d9.dll"); if (!runtime) throw std::runtime_error("d3d9.dll");
        auto address = GetProcAddress(runtime, "Direct3DCreate9"); IDirect3D9*(WINAPI* create)(UINT) = nullptr; std::memcpy(&create, &address, sizeof create);
        if (!create) throw std::runtime_error("Direct3DCreate9");
        IDirect3D9* api = create(D3D_SDK_VERSION); if (!api) throw std::runtime_error("Create9");
        const bool keys = argc >= 3 && !std::strcmp(argv[2], "--keys");
#ifdef X3M_EFFECTS_KEYS_FIXTURE
        if (keys) { IDirect3D9* wrapped = nullptr; x3m::ownership::Options options{}; options.texture_upload_keys = true; check("wrap_factory", x3m::ownership::wrap_factory(api, &wrapped, options)); api = wrapped; }
#else
        if (keys) throw std::runtime_error("keys mode needs the keys executable");
#endif
        D3DPRESENT_PARAMETERS pp{}; pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.BackBufferWidth = 64; pp.BackBufferHeight = 64; pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.hDeviceWindow = window;
        Com<IDirect3DDevice9> device; check("device", api->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device.p));
        IDirect3DDevice9* d = device.p;
        D3DCAPS9 caps{}; check("caps", d->GetDeviceCaps(&caps)); D3DDISPLAYMODE mode{}; check("mode", d->GetDisplayMode(0, &mode));
#ifdef X3M_EFFECTS_KEYS_FIXTURE
        if (keys) { keys_main(d, argc - 3, argv + 3); std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures); return failures ? 1 : 0; }
#endif
        x3m::renderer::EffectsStagePass pass;
        const HRESULT attached = pass.attach(d, *reinterpret_cast<void* const* const*>(d), caps, mode.Format);
        const auto& c = pass.caps();
        std::printf("ATTACH result=%08lx enabled=%u reason=%s largest_program_slots=%u bolt=%u/%u shell=%u/%u decal=%u/%u fp16_blending=%08lx max_ps_slots=%lu references=%u\n", attached, unsigned(c.enabled), c.reason, c.largest_program_slots, c.bolt_vs_slots, c.bolt_ps_slots, c.shell_vs_slots, c.shell_ps_slots, c.decal_vs_slots, c.decal_ps_slots, c.fp16_blending, static_cast<unsigned long>(caps.MaxPixelShader30InstructionSlots), pass.references());
        report("attach", SUCCEEDED(attached) && c.enabled && pass.references() == 13);
        fp16_refused_case(d, caps, mode.Format);
        Quad quad(d);
        for (const auto size : {std::pair{1920u, 1080u}, std::pair{5120u, 1440u}}) {
            Targets* t = new Targets(d, size.first, size.second); Scene* scene = new Scene(d, *t, quad);
            std::printf("SIZE width=%u height=%u\n", size.first, size.second);
            bolt_capsule_case(d, *t, *scene, pass, true);
            if (size.first == 1920) bolt_capsule_case(d, *t, *scene, pass, false);
            shell_case(d, *t, *scene, pass);
            off_path_case(d, *t, *scene, pass);
            for (const float speed : {0.f, 4.f, 8.f}) resolve_case(d, *t, *scene, pass, false, speed);
            resolve_case(d, *t, *scene, pass, true, 0.f);
            timing_case(d, *t, *scene, pass, "bolts60_shells2", 60, 2, 100.f, 800.f);
            timing_case(d, *t, *scene, pass, "fullscreen_shell", 0, 1, 250.f, 300.f); // its front hemisphere projects past every screen edge
            if (size.first == 5120) pass_open_case(d, *t, *scene, pass);
            if (size.first == 1920) { suppression_case(d, *t, *scene, pass); reset_case(d, pp, pass, t, scene, quad); }
            delete scene; delete t;
        }
        pass.detach();
        report("detach_released", pass.references() == 0);
        std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
        return failures ? 1 : 0;
    } catch (const std::exception& error) { std::printf("RESULT FAIL exception=%s checks=%u failures=%u\n", error.what(), checks, failures + 1); return 1; }
}
