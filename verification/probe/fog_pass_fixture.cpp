// Detached qualification of the production FogPass (docs/architecture/volumetric-fog.md,
// "Stage 1 implementation"): a synthetic RT2 (plate occluder, far wall, sky)
// and three synthetic sun cascade maps through the real transaction; the lit
// fraction against the float64 twin (fog_reference.h, the maps) and against an
// analytic shadow oracle (no maps), the shaft behind the occluder, the sky cap,
// the composite law and its energy bound, the sky-hue history, hostile state
// restoration, the untouched motion/depth targets, the refusal (off-path)
// identity, the fault ladder through a hooked vtable, native Reset, and
// EVENT-fenced plus TIMESTAMP timing. Validation-only readback. No game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "../../src/renderer/fog_pass.h"
#include "fog_reference.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
using namespace x3m::renderer;
template<class T> struct Com {
    T* p = nullptr;
    ~Com() { reset(); }
    void reset() { if (p) { p->Release(); p = nullptr; } }
    T* operator->() const { return p; }
    Com() = default; Com(const Com&) = delete; Com& operator=(const Com&) = delete;
};
unsigned checks = 0, failures = 0;
void check(const char* label, HRESULT hr) { if (FAILED(hr)) { std::printf("API FAIL %s %08lx\n", label, (unsigned long)hr); throw std::runtime_error(label); } }
bool require(const char* label, bool value) { ++checks; if (!value) ++failures; std::printf("CHECK %s %s\n", label, value ? "PASS" : "FAIL"); return value; }
double half_to_double(std::uint16_t h) {
    const unsigned e = (h >> 10) & 31; const double s = (h & 0x8000) ? -1 : 1;
    if (e == 31) return (h & 1023) ? NAN : s * INFINITY;
    return s * (e ? std::ldexp(double(1024 + (h & 1023)), int(e) - 25) : std::ldexp(double(h & 1023), -24));
}
std::uint16_t double_to_half(double v) { // round to nearest, v in the normal fp16 range or 0
    if (v <= 0) return 0;
    int e; const double m = std::frexp(v, &e); // v = m 2^e, m in [.5, 1)
    int exponent = e - 1 + 15; long mantissa = std::lround((m * 2 - 1) * 1024);
    if (mantissa == 1024) { mantissa = 0; ++exponent; }
    if (exponent <= 0) return std::uint16_t(std::lround(v * 16777216.)); // subnormal
    return std::uint16_t((exponent << 10) | mantissa);
}
// ---- the synthetic scene (view units; camera at the origin looking +z, y up) ----
constexpr double kPlateZ = 2000, kPlateHalfX = 400, kPlateY0 = 0, kPlateY1 = 600, kWallZ = 20000, kSceneRadius = 3000;
constexpr double kSun[3] = {0, .6, .8};                      // unit, toward the sun
constexpr double kExtents[3] = {4000, 16000, 64000}, kDepthHalf = 80000;
constexpr unsigned kMapSize = 512;
constexpr double kSkyEngine[3] = {.05, .10, .20};
fog_reference::Params reference_params(unsigned w, unsigned h) {
    fog_reference::Params p;
    p.m11 = 1.7320508; p.m00 = p.m11 * double(h) / double(w); p.m20 = 1. / w; p.m21 = -1. / h; // the latch with the quad pixel-centre term
    p.tau_max = .05; p.radius = kSceneRadius; p.g = .3; p.margin = 1; p.decode = 2.2;
    for (unsigned i = 0; i < 3; ++i) { p.sun[i] = kSun[i]; p.radiance[i] = 3.14159265358979 * (i == 0 ? 1. : i == 1 ? .9 : .7); }
    return p;
}
// Analytic shadow test: does the segment from the point toward the sun hit the plate or the wall?
double analytic_visible(const double q[3]) {
    if (q[2] < kPlateZ) { const double t = (kPlateZ - q[2]) / kSun[2], x = q[0] + kSun[0] * t, y = q[1] + kSun[1] * t; if (std::fabs(x) <= kPlateHalfX && y >= kPlateY0 && y <= kPlateY1) return 0; }
    if (q[2] < kWallZ) { const double t = (kWallZ - q[2]) / kSun[2], y = q[1] + kSun[1] * t; if (y < 0) return 0; }
    return 1;
}
struct Scene { unsigned w, h; std::vector<float> depth; }; // RGBA per pixel
Scene make_scene(unsigned w, unsigned h, const fog_reference::Params& p) {
    Scene s{w, h, std::vector<float>(std::size_t(w) * h * 4, 0.f)};
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        double ray[3], length; fog_reference::ray_of(x, y, w, h, p, ray, &length);
        double z = -1;
        const double px = ray[0] * kPlateZ, py = ray[1] * kPlateZ;
        if (std::fabs(px) <= kPlateHalfX && py >= kPlateY0 && py <= kPlateY1) z = kPlateZ;
        else if (ray[1] * kWallZ < 0) z = kWallZ;
        float* texel = &s.depth[(std::size_t(y) * w + x) * 4];
        texel[0] = z > 0 ? float(p.m22 + p.m32 / z) : -1.f; texel[1] = .5f; texel[2] = z > 0 ? float(z) : 0.f; texel[3] = 0.f;
    }
    return s;
}
std::vector<fog_reference::Cascade> make_cascades() {
    std::vector<fog_reference::Cascade> out(3);
    const double forward[3] = {-kSun[0], -kSun[1], -kSun[2]}, right[3] = {1, 0, 0}, up[3] = {0, .8, -.6};
    for (unsigned c = 0; c < 3; ++c) {
        auto& k = out[c]; k.size = kMapSize; k.valid = true; k.map.assign(std::size_t(kMapSize) * kMapSize, 1.f);
        const double e = kExtents[c];
        for (unsigned i = 0; i < 3; ++i) { k.rows[i] = right[i] / e; k.rows[4 + i] = up[i] / e; k.rows[8 + i] = forward[i] / (2 * kDepthHalf); }
        k.rows[11] = .5;
        k.bias = 3. * (2 * e / kMapSize) / (2 * kDepthHalf);
        for (unsigned j = 0; j < kMapSize; ++j) for (unsigned i = 0; i < kMapSize; ++i) {
            const double sx = 2. * i / kMapSize - 1, sy = 1 - 2. * j / kMapSize;
            const double o[3] = {right[0] * sx * e + up[0] * sy * e, right[1] * sx * e + up[1] * sy * e, right[2] * sx * e + up[2] * sy * e};
            double nearest = 1e30;
            { const double t = (kPlateZ - o[2]) / forward[2], x = o[0] + forward[0] * t, y = o[1] + forward[1] * t; if (std::fabs(x) <= kPlateHalfX && y >= kPlateY0 && y <= kPlateY1) nearest = std::min(nearest, t); }
            { const double t = (kWallZ - o[2]) / forward[2], y = o[1] + forward[1] * t; if (y < 0) nearest = std::min(nearest, t); }
            if (nearest < 1e29) k.map[std::size_t(j) * kMapSize + i] = float(std::clamp(nearest / (2 * kDepthHalf) + .5, 0., 1.));
        }
    }
    return out;
}
// ---- hooked vtable: fault injection and per-draw timing ----
using CreateTextureFn = HRESULT(WINAPI*)(IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
using CreatePsFn = HRESULT(WINAPI*)(IDirect3DDevice9*, const DWORD*, IDirect3DPixelShader9**);
using DrawUpFn = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using StretchFn = HRESULT(WINAPI*)(IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE);
struct Faults { unsigned texture_fail_at = 0, texture_calls = 0, shader_fail_at = 0, shader_calls = 0, draw_fail_at = 0, draw_calls = 0, stretch_fail_at = 0, stretch_calls = 0; HRESULT draw_error = E_FAIL; } faults;
void* hooked[119]; void* original[119];
HRESULT WINAPI hook_create_texture(IDirect3DDevice9* d, UINT w, UINT h, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DTexture9** t, HANDLE* s) {
    if (++faults.texture_calls == faults.texture_fail_at) return E_OUTOFMEMORY;
    return reinterpret_cast<CreateTextureFn>(original[23])(d, w, h, l, u, f, p, t, s);
}
HRESULT WINAPI hook_create_ps(IDirect3DDevice9* d, const DWORD* words, IDirect3DPixelShader9** out) {
    if (++faults.shader_calls == faults.shader_fail_at) return E_OUTOFMEMORY;
    return reinterpret_cast<CreatePsFn>(original[106])(d, words, out);
}
HRESULT WINAPI hook_stretch(IDirect3DDevice9* d, IDirect3DSurface9* a, const RECT* ar, IDirect3DSurface9* b, const RECT* br, D3DTEXTUREFILTERTYPE f) {
    if (++faults.stretch_calls == faults.stretch_fail_at) return E_FAIL;
    return reinterpret_cast<StretchFn>(original[34])(d, a, ar, b, br, f);
}
HRESULT complete_fence(IDirect3DQuery9* q) {
    HRESULT hr = q->Issue(D3DISSUE_END); if (FAILED(hr)) return hr;
    const DWORD begin = GetTickCount();
    for (;;) { hr = q->GetData(nullptr, 0, D3DGETDATA_FLUSH); if (hr != S_FALSE) return hr; if (GetTickCount() - begin > 5000) return E_FAIL; Sleep(0); }
}
struct DrawTimer { IDirect3DQuery9* fence = nullptr; unsigned index = 0; LONGLONG ticks[4]{}; HRESULT failure = S_OK; } draw_timer;
HRESULT WINAPI hook_draw(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT c, const void* v, UINT s) {
    if (++faults.draw_calls == faults.draw_fail_at) return faults.draw_error;
    if (!draw_timer.fence) return reinterpret_cast<DrawUpFn>(original[83])(d, t, c, v, s);
    HRESULT hr = complete_fence(draw_timer.fence);
    LARGE_INTEGER a{}, b{}; QueryPerformanceCounter(&a);
    const HRESULT draw = reinterpret_cast<DrawUpFn>(original[83])(d, t, c, v, s);
    if (SUCCEEDED(hr)) hr = complete_fence(draw_timer.fence);
    QueryPerformanceCounter(&b);
    if (FAILED(hr)) draw_timer.failure = hr;
    if (draw_timer.index < 4) draw_timer.ticks[draw_timer.index] += b.QuadPart - a.QuadPart;
    ++draw_timer.index;
    return draw;
}
// Everything the transaction may touch, compared byte for byte around execute.
struct Snapshot {
    std::vector<unsigned char> bytes;
    template<class T> void add(const T& v) { auto* p = reinterpret_cast<const unsigned char*>(&v); bytes.insert(bytes.end(), p, p + sizeof v); }
    template<class T> void object(T* v) { add(reinterpret_cast<std::uintptr_t>(v)); if (v) v->Release(); }
    explicit Snapshot(IDirect3DDevice9* d) {
        for (UINT i = 0; i < 4; ++i) { IDirect3DSurface9* s = nullptr; HRESULT hr = d->GetRenderTarget(i, &s); add(hr); object(s); }
        IDirect3DSurface9* ds = nullptr; HRESULT hr = d->GetDepthStencilSurface(&ds); add(hr); object(ds);
        D3DVIEWPORT9 vp{}; check("snapshot viewport", d->GetViewport(&vp)); add(vp);
        RECT sc{}; check("snapshot scissor", d->GetScissorRect(&sc)); add(sc);
        IDirect3DVertexShader9* vs = nullptr; check("snapshot vs", d->GetVertexShader(&vs)); object(vs);
        IDirect3DPixelShader9* ps = nullptr; check("snapshot ps", d->GetPixelShader(&ps)); object(ps);
        IDirect3DVertexDeclaration9* decl = nullptr; check("snapshot decl", d->GetVertexDeclaration(&decl)); object(decl);
        DWORD fvf = 0; check("snapshot fvf", d->GetFVF(&fvf)); add(fvf);
        IDirect3DVertexBuffer9* vb = nullptr; UINT off = 0, stride = 0, freq = 0;
        check("snapshot stream", d->GetStreamSource(0, &vb, &off, &stride)); check("snapshot freq", d->GetStreamSourceFreq(0, &freq)); object(vb); add(off); add(stride); add(freq);
        IDirect3DIndexBuffer9* ib = nullptr; check("snapshot indices", d->GetIndices(&ib)); object(ib);
        for (UINT n = 0; n < 4; ++n) { IDirect3DBaseTexture9* t = nullptr; check("snapshot vertex texture", d->GetTexture(D3DVERTEXTEXTURESAMPLER0 + n, &t)); object(t); }
        for (UINT n = 0; n < 8; ++n) {
            IDirect3DBaseTexture9* t = nullptr; check("snapshot texture", d->GetTexture(n, &t)); object(t);
            for (UINT j = 1; j <= 13; ++j) { DWORD v = 0; hr = d->GetSamplerState(n, D3DSAMPLERSTATETYPE(j), &v); add(hr); add(v); }
        }
        const D3DRENDERSTATETYPE states[] = {D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC, D3DRS_STENCILENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHABLENDENABLE,
            D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP, D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_SRCBLENDALPHA, D3DRS_DESTBLENDALPHA, D3DRS_FOGENABLE,
            D3DRS_SRGBWRITEENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_CLIPPING, D3DRS_LIGHTING, D3DRS_INDEXEDVERTEXBLENDENABLE,
            D3DRS_VERTEXBLEND, D3DRS_FILLMODE, D3DRS_CULLMODE, D3DRS_COLORWRITEENABLE, D3DRS_COLORWRITEENABLE1, D3DRS_MULTISAMPLEMASK, D3DRS_WRAP0,
            D3DRS_POINTSPRITEENABLE, D3DRS_DITHERENABLE, D3DRS_ANTIALIASEDLINEENABLE};
        for (auto s : states) { DWORD v = 0; check("snapshot rs", d->GetRenderState(s, &v)); add(v); }
        for (auto s : {D3DTSS_TEXCOORDINDEX, D3DTSS_TEXTURETRANSFORMFLAGS}) { DWORD v = 0; check("snapshot tss", d->GetTextureStageState(0, s, &v)); add(v); }
        float c[17 * 4]{}; check("snapshot constants", d->GetPixelShaderConstantF(0, c, 17)); add(c);
    }
    bool operator==(const Snapshot& o) const { return bytes == o.bytes; }
};
// One frame size: RT2 (A32B32G32R32F render target, bound at index 2 as the
// route binds it), the motion target RT1, the FP16 scene target, the cascade
// maps and the readback surfaces.
struct Frame {
    unsigned w, h, hw, hh;
    Com<IDirect3DTexture9> depth, depth_sys, rt1, target, target_sys, vertex[4], maps[3], maps_sys[3];
    Com<IDirect3DSurface9> depth_surface, rt1_surface, target_surface, auto_depth, read_lit, read_target, read_depth, read_rt1, read_sky, read_level;
    Frame(IDirect3DDevice9* d, unsigned width, unsigned height, D3DFORMAT depth_format = D3DFMT_A32B32G32R32F) : w(width), h(height), hw((width + 1) / 2), hh((height + 1) / 2) {
        check("depth texture", d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, depth_format, D3DPOOL_DEFAULT, &depth.p, nullptr)); check("depth surface", depth->GetSurfaceLevel(0, &depth_surface.p));
        check("depth staging", d->CreateTexture(w, h, 1, 0, depth_format, D3DPOOL_SYSTEMMEM, &depth_sys.p, nullptr));
        check("rt1", d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &rt1.p, nullptr)); check("rt1 surface", rt1->GetSurfaceLevel(0, &rt1_surface.p));
        check("rt1 fill", d->ColorFill(rt1_surface.p, nullptr, D3DCOLOR_ARGB(17, 34, 51, 68)));
        check("target texture", d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &target.p, nullptr)); check("target surface", target->GetSurfaceLevel(0, &target_surface.p));
        check("target staging", d->CreateTexture(w, h, 1, 0, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &target_sys.p, nullptr));
        for (auto& v : vertex) check("vertex texture", d->CreateTexture(4, 4, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &v.p, nullptr));
        check("auto depth", d->GetDepthStencilSurface(&auto_depth.p));
        check("lit readback", d->CreateOffscreenPlainSurface(hw, hh, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &read_lit.p, nullptr));
        check("target readback", d->CreateOffscreenPlainSurface(w, h, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &read_target.p, nullptr));
        check("depth readback", d->CreateOffscreenPlainSurface(w, h, depth_format, D3DPOOL_SYSTEMMEM, &read_depth.p, nullptr));
        check("rt1 readback", d->CreateOffscreenPlainSurface(w, h, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &read_rt1.p, nullptr));
        check("sky readback", d->CreateOffscreenPlainSurface(1, 1, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &read_sky.p, nullptr));
        check("level readback", d->CreateOffscreenPlainSurface(8, 8, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &read_level.p, nullptr));
    }
    void upload_depth(IDirect3DDevice9* d, const Scene& s, unsigned channels) {
        D3DLOCKED_RECT lr{}; check("lock depth", depth_sys->LockRect(0, &lr, nullptr, 0));
        for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x)
            std::memcpy(static_cast<char*>(lr.pBits) + y * lr.Pitch + x * channels * 4, &s.depth[(std::size_t(y) * w + x) * 4], channels * 4);
        check("unlock depth", depth_sys->UnlockRect(0));
        check("update depth", d->UpdateTexture(depth_sys.p, depth.p));
    }
    void upload_maps(IDirect3DDevice9* d, const std::vector<fog_reference::Cascade>& cascades) {
        for (unsigned c = 0; c < 3; ++c) {
            const unsigned n = cascades[c].size;
            maps[c].reset(); maps_sys[c].reset();
            check("map texture", d->CreateTexture(n, n, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &maps[c].p, nullptr));
            check("map staging", d->CreateTexture(n, n, 1, 0, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &maps_sys[c].p, nullptr));
            D3DLOCKED_RECT lr{}; check("lock map", maps_sys[c]->LockRect(0, &lr, nullptr, 0));
            for (unsigned y = 0; y < n; ++y) std::memcpy(static_cast<char*>(lr.pBits) + y * lr.Pitch, &cascades[c].map[std::size_t(y) * n], n * 4);
            check("unlock map", maps_sys[c]->UnlockRect(0));
            check("update map", d->UpdateTexture(maps_sys[c].p, maps[c].p));
        }
    }
    // Engine-space scene: the constant sky colour on sentinel pixels, a pattern below and above one on geometry, alpha .375.
    std::vector<double> fill_target(IDirect3DDevice9* d, const Scene& s, const double sky[3]) {
        std::vector<double> values(std::size_t(w) * h * 4);
        D3DLOCKED_RECT lr{}; check("lock target", target_sys->LockRect(0, &lr, nullptr, 0));
        for (unsigned y = 0; y < h; ++y) { auto* row = reinterpret_cast<std::uint16_t*>(static_cast<char*>(lr.pBits) + y * lr.Pitch);
            for (unsigned x = 0; x < w; ++x) {
                const bool is_sky = s.depth[(std::size_t(y) * w + x) * 4] < 0;
                const double rgba[4] = {is_sky ? sky[0] : .02 + .9 * double(x) / w, is_sky ? sky[1] : 1.6 * double(y) / h + .01, is_sky ? sky[2] : .5, .375};
                for (unsigned c = 0; c < 4; ++c) { row[x * 4 + c] = double_to_half(rgba[c]); values[(std::size_t(y) * w + x) * 4 + c] = half_to_double(row[x * 4 + c]); }
            } }
        check("unlock target", target_sys->UnlockRect(0));
        check("update target", d->UpdateTexture(target_sys.p, target.p));
        return values;
    }
    template<class T> std::vector<T> read(IDirect3DDevice9* d, IDirect3DSurface9* source, IDirect3DSurface9* sink, unsigned width, unsigned height, unsigned channels) {
        check("readback", d->GetRenderTargetData(source, sink));
        D3DLOCKED_RECT lr{}; check("lock readback", sink->LockRect(&lr, nullptr, D3DLOCK_READONLY));
        std::vector<T> out(std::size_t(width) * height * channels);
        for (unsigned y = 0; y < height; ++y) std::memcpy(&out[std::size_t(y) * width * channels], static_cast<char*>(lr.pBits) + y * lr.Pitch, width * channels * sizeof(T));
        check("unlock readback", sink->UnlockRect());
        return out;
    }
    // The route's bindings at the scene end plus hostile state.
    void hostile(IDirect3DDevice9* d) {
        check("bind rt0", d->SetRenderTarget(0, target_surface.p)); check("bind rt1", d->SetRenderTarget(1, rt1_surface.p)); check("bind rt2", d->SetRenderTarget(2, depth_surface.p));
        check("bind depth", d->SetDepthStencilSurface(auto_depth.p));
        for (UINT i = 0; i < 4; ++i) check("bind vertex texture", d->SetTexture(D3DVERTEXTEXTURESAMPLER0 + i, vertex[i].p));
        for (auto [s, v] : {std::pair{D3DRS_ALPHABLENDENABLE, DWORD(TRUE)}, std::pair{D3DRS_SRCBLEND, DWORD(D3DBLEND_ONE)}, std::pair{D3DRS_DESTBLEND, DWORD(D3DBLEND_INVSRCCOLOR)},
                            std::pair{D3DRS_BLENDOP, DWORD(D3DBLENDOP_REVSUBTRACT)}, std::pair{D3DRS_SCISSORTESTENABLE, DWORD(TRUE)}, std::pair{D3DRS_ZENABLE, DWORD(TRUE)},
                            std::pair{D3DRS_COLORWRITEENABLE, DWORD(1)}, std::pair{D3DRS_CULLMODE, DWORD(D3DCULL_CW)}, std::pair{D3DRS_FILLMODE, DWORD(D3DFILL_WIREFRAME)},
                            std::pair{D3DRS_SRGBWRITEENABLE, DWORD(TRUE)}, std::pair{D3DRS_FOGENABLE, DWORD(TRUE)}, std::pair{D3DRS_ALPHATESTENABLE, DWORD(TRUE)},
                            std::pair{D3DRS_STENCILENABLE, DWORD(TRUE)}, std::pair{D3DRS_MULTISAMPLEMASK, DWORD(0)}, std::pair{D3DRS_CLIPPING, DWORD(TRUE)}})
            check("hostile rs", d->SetRenderState(s, v));
        RECT sc{3, 3, 9, 9}; check("hostile scissor", d->SetScissorRect(&sc));
        D3DVIEWPORT9 vp{2, 2, 7, 7, .2f, .7f}; check("hostile viewport", d->SetViewport(&vp));
        for (UINT i = 0; i < 4; ++i) {
            check("hostile texture", d->SetTexture(i, rt1.p));
            check("hostile sampler", d->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR)); check("hostile sampler", d->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR));
            check("hostile sampler", d->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP)); check("hostile sampler", d->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_MIRROR));
            check("hostile sampler", d->SetSamplerState(i, D3DSAMP_SRGBTEXTURE, TRUE));
        }
        float garbage[17 * 4]; for (unsigned i = 0; i < 17 * 4; ++i) garbage[i] = float(i) * .37f - 9.f;
        check("hostile constants", d->SetPixelShaderConstantF(0, garbage, 17));
        check("hostile fvf", d->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE));
        check("hostile tss", d->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 2));
    }
    void unbind(IDirect3DDevice9* d, IDirect3DSurface9* backbuffer) {
        check("unbind rt1", d->SetRenderTarget(1, nullptr)); check("unbind rt2", d->SetRenderTarget(2, nullptr)); check("unbind rt0", d->SetRenderTarget(0, backbuffer));
        for (UINT i = 0; i < 4; ++i) { check("unbind texture", d->SetTexture(i, nullptr)); check("unbind vertex texture", d->SetTexture(D3DVERTEXTEXTURESAMPLER0 + i, nullptr)); }
    }
};
FogFrame frame_inputs(Frame& f, const fog_reference::Params& p, const std::vector<fog_reference::Cascade>& cascades) {
    FogFrame in;
    in.depth_share = f.depth.p; in.target = f.target_surface.p; in.width = f.w; in.height = f.h; in.count = 3;
    for (unsigned c = 0; c < 3; ++c) {
        in.cascades[c].map = f.maps[c].p; in.cascades[c].valid = cascades[c].valid; in.cascades[c].bias = float(cascades[c].bias);
        for (unsigned k = 0; k < 12; ++k) in.cascades[c].rows[k] = float(cascades[c].rows[k]);
    }
    FogParams& q = in.params;
    q.m00 = float(p.m00); q.m11 = float(p.m11); q.m20 = float(p.m20); q.m21 = float(p.m21); q.m22 = float(p.m22); q.m32 = float(p.m32);
    q.tau_max = float(p.tau_max); q.radius = float(p.radius); q.anisotropy = float(p.g); q.margin = float(p.margin); q.decode_exponent = float(p.decode);
    for (unsigned i = 0; i < 3; ++i) { q.sun_view[i] = float(p.sun[i]); q.sun_radiance[i] = float(p.radiance[i]); }
    q.jitter_index = p.jitter_index; q.update_sky = true; q.sky_blend = .25f;
    in.caller_scene_open = false; in.caller_queries_idle = true;
    return in;
}
std::vector<double> lit_values(Frame& f, IDirect3DDevice9* d, IDirect3DTexture9* lit) {
    Com<IDirect3DSurface9> s; check("lit surface", lit->GetSurfaceLevel(0, &s.p));
    const auto raw = f.read<std::uint8_t>(d, s.p, f.read_lit.p, f.hw, f.hh, 4);
    std::vector<double> out(std::size_t(f.hw) * f.hh);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = raw[i * 4 + 2] / 255.; // BGRA: the red lane
    return out;
}
std::array<double, 4> sky_value(Frame& f, IDirect3DDevice9* d, FogPass& pass) {
    Com<IDirect3DSurface9> s; check("sky surface", pass.fixture_sky()->GetSurfaceLevel(0, &s.p));
    const auto raw = f.read<std::uint16_t>(d, s.p, f.read_sky.p, 1, 1, 4);
    return {half_to_double(raw[0]), half_to_double(raw[1]), half_to_double(raw[2]), half_to_double(raw[3])};
}
struct Compare { double mean_abs = 0, max = 0; unsigned within = 0, pixels = 0; };
Compare compare(const std::vector<double>& a, const std::vector<double>& b, double tolerance) {
    Compare c; c.pixels = unsigned(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) { const double e = std::fabs(a[i] - b[i]); c.mean_abs += e; c.max = std::max(c.max, e); c.within += e <= tolerance; }
    c.mean_abs /= double(a.size());
    return c;
}
double ms(LONGLONG ticks, LONGLONG f) { return double(ticks) * 1000. / double(f); }
double median(std::vector<double> v) { if (v.empty()) return -1; std::sort(v.begin(), v.end()); return v[v.size() / 2]; }
void timing(IDirect3DDevice9* d, FogPass& pass, unsigned w, unsigned h, LONGLONG freq, const std::vector<fog_reference::Cascade>& cascades) {
    auto p = reference_params(w, h); p.radius = 10000; p.tau_max = .02; // the production medium
    Frame f(d, w, h); const Scene s = make_scene(w, h, p);
    f.upload_depth(d, s, 4); f.upload_maps(d, cascades); f.fill_target(d, s, kSkyEngine);
    Com<IDirect3DQuery9> fence; check("event query", d->CreateQuery(D3DQUERYTYPE_EVENT, &fence.p));
    Com<IDirect3DQuery9> disjoint, frequency, begin, end;
    const bool stamps = SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &disjoint.p)) && SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &frequency.p)) &&
                        SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &begin.p)) && SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &end.p));
    f.hostile(d);
    check("timing begin", d->BeginScene());
    FogFrame in = frame_inputs(f, p, cascades); in.caller_scene_open = true;
    FogResult out;
    check("timing warm", pass.execute(in, &out));
    for (const bool sky : {true, false}) {
        in.params.update_sky = sky;
        std::vector<double> on, off, submit, gpu; unsigned calls = 0;
        for (unsigned i = 0; i < 36; ++i) { // 3 warmup pairs, 15 measured pairs (on, off alternating)
            LARGE_INTEGER a{}, b{}, c{};
            check("lead fence", complete_fence(fence.p));
            const bool run = i % 2 == 0, stamp = run && stamps;
            if (stamp) { check("disjoint begin", disjoint->Issue(D3DISSUE_BEGIN)); check("stamp begin", begin->Issue(D3DISSUE_END)); }
            QueryPerformanceCounter(&a);
            if (run) { check("timed execute", pass.execute(in, &out)); calls = out.device_calls; }
            QueryPerformanceCounter(&b);
            if (stamp) { check("stamp end", end->Issue(D3DISSUE_END)); check("stamp frequency", frequency->Issue(D3DISSUE_END)); check("disjoint end", disjoint->Issue(D3DISSUE_END)); }
            check("tail fence", complete_fence(fence.p));
            QueryPerformanceCounter(&c);
            if (stamp && i >= 6) {
                BOOL dj = TRUE; UINT64 fq = 0, t0 = 0, t1 = 0;
                auto wait = [&](IDirect3DQuery9* q, void* data, DWORD size) { const DWORD start = GetTickCount(); HRESULT hr; while ((hr = q->GetData(data, size, D3DGETDATA_FLUSH)) == S_FALSE && GetTickCount() - start < 5000) Sleep(0); return hr; };
                if (wait(disjoint.p, &dj, sizeof dj) == S_OK && wait(frequency.p, &fq, sizeof fq) == S_OK && wait(begin.p, &t0, sizeof t0) == S_OK && wait(end.p, &t1, sizeof t1) == S_OK && !dj && fq && t1 >= t0)
                    gpu.push_back(double(t1 - t0) * 1000. / double(fq));
            }
            if (i < 6) continue;
            (run ? on : off).push_back(ms(c.QuadPart - a.QuadPart, freq));
            if (run) submit.push_back(ms(b.QuadPart - a.QuadPart, freq));
        }
        const double on_m = median(on), off_m = median(off), submit_m = median(submit);
        std::printf("TIMING width=%u height=%u sky=%u fenced_on_ms=%.4f fenced_off_ms=%.4f submit_ms=%.4f chain_ms=%.4f gpu_ms=%.4f gpu_timestamp_ms=%.4f timestamp_samples=%zu device_calls=%u samples=%zu\n",
                    w, h, unsigned(sky), on_m, off_m, submit_m, on_m - off_m, std::max(0., on_m - off_m - submit_m), median(gpu), gpu.size(), calls, on.size());
    }
    // Per-quad breakdown with the sky update on: march, sky level, sky reduce, composite.
    in.params.update_sky = true;
    const char* names[4] = {"march", "sky_level", "sky_reduce", "composite"};
    std::vector<double> per_quad[4];
    for (unsigned i = 0; i < 8; ++i) {
        draw_timer = {}; draw_timer.fence = fence.p;
        check("quads execute", pass.execute(in, &out));
        const DrawTimer sample = draw_timer; draw_timer = {};
        check("quads fences", sample.failure);
        require("per_quad_draw_count", sample.index == 4);
        if (i >= 2) for (unsigned q = 0; q < 4; ++q) per_quad[q].push_back(ms(sample.ticks[q], freq));
    }
    check("final fence", complete_fence(fence.p));
    check("timing end", d->EndScene());
    std::printf("TIMING_QUADS width=%u height=%u", w, h);
    double total = 0;
    for (unsigned q = 0; q < 4; ++q) { const double m = median(per_quad[q]); total += m; std::printf(" %s_ms=%.4f", names[q], m); }
    std::printf(" sum_ms=%.4f samples=%zu\n", total, per_quad[0].size());
}
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        WNDCLASSA cls{}; cls.lpfnWndProc = DefWindowProcA; cls.hInstance = GetModuleHandleA(nullptr); cls.lpszClassName = "X3FogPassFixture"; RegisterClassA(&cls);
        HWND window = CreateWindowA(cls.lpszClassName, "X3 fog pass fixture", WS_OVERLAPPEDWINDOW, 90, 90, 128, 128, nullptr, nullptr, cls.hInstance, nullptr);
        if (!window) throw std::runtime_error("window");
        HMODULE runtime = LoadLibraryA("d3d9.dll"); if (!runtime) throw std::runtime_error("d3d9.dll");
        auto address = GetProcAddress(runtime, "Direct3DCreate9"); IDirect3D9*(WINAPI* create)(UINT) = nullptr; std::memcpy(&create, &address, sizeof create);
        if (!create) throw std::runtime_error("Direct3DCreate9");
        Com<IDirect3D9> api; api.p = create(D3D_SDK_VERSION); if (!api.p) throw std::runtime_error("Create9");
        const unsigned W = 1280, H = 768;
        D3DPRESENT_PARAMETERS pp{}; pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window; pp.BackBufferWidth = W; pp.BackBufferHeight = H;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D24S8;
        Com<IDirect3DDevice9> device; check("CreateDevice", api->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device.p));
        IDirect3DDevice9* d = device.p;
        D3DCAPS9 caps{}; check("caps", d->GetDeviceCaps(&caps));
        Com<IDirect3DSurface9> backbuffer; check("backbuffer", d->GetRenderTarget(0, &backbuffer.p));
        D3DDISPLAYMODE mode{}; check("display mode", api->GetAdapterDisplayMode(0, &mode));
        LARGE_INTEGER freq{}; QueryPerformanceFrequency(&freq);
        std::memcpy(original, *reinterpret_cast<void***>(d), sizeof original); std::memcpy(hooked, original, sizeof hooked);
        hooked[23] = reinterpret_cast<void*>(&hook_create_texture); hooked[106] = reinterpret_cast<void*>(&hook_create_ps); hooked[83] = reinterpret_cast<void*>(&hook_draw); hooked[34] = reinterpret_cast<void*>(&hook_stretch);
        std::printf("CAPS ps=%08lx vs=%08lx ps30_slots=%u rts=%u stretch_from_textures=%d\n", (unsigned long)caps.PixelShaderVersion, (unsigned long)caps.VertexShaderVersion,
                    unsigned(caps.MaxPixelShader30InstructionSlots), unsigned(caps.NumSimultaneousRTs), (caps.DevCaps2 & D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES) != 0);
        { FogPass twin; D3DCAPS9 c = caps; c.PixelShaderVersion = D3DPS_VERSION(2, 0);
          require("twin_ps_2_0", FAILED(twin.attach(d, hooked, c, mode.Format)) && std::string(twin.caps().reason) == "ps_3_0" && twin.references() == 0);
          c = caps; c.MaxPixelShader30InstructionSlots = 64;
          require("twin_ps_slots", FAILED(twin.attach(d, hooked, c, mode.Format)) && std::string(twin.caps().reason) == "ps_slots" && twin.references() == 0);
          c = caps; c.MaxPixelShader30InstructionSlots = 512; // the ps_3_0 minimum every device has
          { const bool attached = SUCCEEDED(twin.attach(d, hooked, c, mode.Format)); require("twin_ps_slots_512", attached && twin.caps().enabled && twin.caps().largest_program_slots <= 512); twin.detach(); }
          c = caps; c.DestBlendCaps &= ~DWORD(D3DPBLENDCAPS_INVSRCALPHA);
          require("twin_blend_factors", FAILED(twin.attach(d, hooked, c, mode.Format)) && std::string(twin.caps().reason) == "blend_factors" && twin.references() == 0);
          c = caps; c.DevCaps2 &= ~DWORD(D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES);
          require("twin_stretch_rect", FAILED(twin.attach(d, hooked, c, mode.Format)) && std::string(twin.caps().reason) == "stretch_rect" && twin.references() == 0);
          faults = {}; faults.shader_fail_at = 3;
          require("fault_shader_create", FAILED(twin.attach(d, hooked, caps, mode.Format)) && std::string(twin.caps().reason) == "programs" && twin.references() == 0 && !twin.caps().enabled);
          faults = {}; }
        FogPass pass;
        check("attach", pass.attach(d, hooked, caps, mode.Format));
        std::printf("ATTACH enabled=%d largest_program_slots=%u references=%u\n", pass.caps().enabled, pass.caps().largest_program_slots, pass.references());
        require("attach_enabled", pass.caps().enabled && pass.references() == 6 && pass.caps().largest_program_slots <= 512);
        // Allocation failure on the third target: every partial target is released.
        faults = {}; faults.texture_fail_at = 3;
        require("fault_target_create", pass.prepare(W, H) == E_OUTOFMEMORY && pass.references() == 6 && pass.allocations() == 0);
        faults = {};
        auto p = reference_params(W, H);
        const Scene scene = make_scene(W, H, p);
        std::vector<fog_reference::Cascade> cascades = make_cascades();
        Frame* f = new Frame(d, W, H);
        f->upload_depth(d, scene, 4); f->upload_maps(d, cascades);
        const auto depth_before = f->read<float>(d, f->depth_surface.p, f->read_depth.p, W, H, 4);
        const auto rt1_before = f->read<std::uint8_t>(d, f->rt1_surface.p, f->read_rt1.p, W, H, 4);
        require("depth_upload_exact", depth_before == scene.depth);
        // Refusals (the off path): nothing touched, the target byte-identical.
        { std::vector<double> engine = f->fill_target(d, scene, kSkyEngine);
          const auto before = f->read<std::uint16_t>(d, f->target_surface.p, f->read_target.p, W, H, 4);
          f->hostile(d); Snapshot pre(d);
          FogResult out; FogFrame in = frame_inputs(*f, p, cascades); in.params.tau_max = 0.f;
          const bool zero = pass.execute(in, &out) == E_INVALIDARG && out.failed == FogStage::Validate && !out.applied;
          in = frame_inputs(*f, p, cascades); in.caller_queries_idle = false; const bool queries = pass.execute(in, &out) == E_INVALIDARG;
          in = frame_inputs(*f, p, cascades); in.params.anisotropy = .95f; const bool g = pass.execute(in, &out) == E_INVALIDARG;
          in = frame_inputs(*f, p, cascades); in.params.sun_view[2] = 2.f; const bool sun = pass.execute(in, &out) == E_INVALIDARG;
          in = frame_inputs(*f, p, cascades); in.target = f->rt1_surface.p; const bool eight = pass.execute(in, &out) == E_INVALIDARG;
          Snapshot post(d);
          require("refuse_zero_strength", zero); require("refuse_unknown_queries", queries); require("refuse_bad_anisotropy", g); require("refuse_bad_sun", sun); require("refuse_8bit_target", eight);
          require("refusal_state_untouched", pre == post);
          require("refusal_target_bit_identical", before == f->read<std::uint16_t>(d, f->target_surface.p, f->read_target.p, W, H, 4));
          require("refusal_allocates_nothing", pass.allocations() == 0 && pass.references() == 6); }
        // ---- the scene through the transaction, two jitter phases ----
        std::vector<double> clean_lit; std::vector<std::uint16_t> clean_after;
        for (const unsigned phase : {0u, 3u}) {
            p.jitter_index = phase;
            const std::string label = "scene_phase" + std::to_string(phase);
            const std::vector<double> engine = f->fill_target(d, scene, kSkyEngine);
            f->hostile(d); Snapshot pre(d);
            FogResult out; const HRESULT hr = pass.execute(frame_inputs(*f, p, cascades), &out);
            Snapshot post(d);
            require((label + "_execute").c_str(), SUCCEEDED(hr) && out.applied && out.sky_updated && out.cascades_bound == 3 && out.lit && out.half_width == f->hw && out.failed == FogStage::None);
            require((label + "_state_preserved").c_str(), pre == post);
            require((label + "_references").c_str(), pass.references() == 15);
            const std::vector<double> lit = lit_values(*f, d, out.lit);
            const auto sky = sky_value(*f, d, pass);
            const auto after = f->read<std::uint16_t>(d, f->target_surface.p, f->read_target.p, W, H, 4);
            if (phase == 0) { clean_lit = lit; clean_after = after; }
            // Lit fraction: the map twin and the analytic oracle.
            const auto twin = fog_reference::march(scene.depth, W, H, p, [&](const double q[3]) { return fog_reference::map_visibility(cascades, p, q); });
            const auto analytic = fog_reference::march(scene.depth, W, H, p, analytic_visible);
            const Compare ct = compare(lit, twin, 1. / 16 + 1e-3), ca = compare(lit, analytic, 1. / 16 + 1e-3);
            std::printf("REFERENCE scene=phase%u kind=map_twin pixels=%u mean_abs=%.6f max=%.4f within_one_step=%u\n", phase, ct.pixels, ct.mean_abs, ct.max, ct.within);
            std::printf("REFERENCE scene=phase%u kind=analytic pixels=%u mean_abs=%.6f max=%.4f within_one_step=%u\n", phase, ca.pixels, ca.mean_abs, ca.max, ca.within);
            require((label + "_lit_map_twin").c_str(), ct.mean_abs <= .002 && double(ct.within) / ct.pixels >= .998);
            require((label + "_lit_analytic").c_str(), ca.mean_abs <= .01 && double(ca.within) / ca.pixels >= .98);
            // Shaft behind the occluder, the clear side, the sky cap.
            double shaft = 0, sky_lit = 0; unsigned shaft_n = 0, sky_n = 0, sky_bad = 0;
            for (unsigned j = 0; j < f->hh; ++j) for (unsigned i = 0; i < f->hw; ++i) {
                double ray[3], length; fog_reference::ray_of(2 * i, 2 * j, W, H, p, ray, &length);
                const bool is_sky = scene.depth[(std::size_t(2 * j) * W + 2 * i) * 4] < 0;
                const double v = lit[std::size_t(j) * f->hw + i];
                if (!is_sky && std::fabs(ray[0]) < .1 && ray[1] < -.1 && ray[1] > -.4) { shaft += v; ++shaft_n; }  // rays under the plate: through its shadow volume
                if (is_sky && std::fabs(ray[0]) > .6) { sky_lit += v; ++sky_n; sky_bad += v != 1.; } // beside the plate, above the wall: no occluder toward the sun on any sample
            }
            std::printf("ORACLE scene=phase%u label=shaft mean=%.4f pixels=%u %s\n", phase, shaft / shaft_n, shaft_n, shaft_n && shaft / shaft_n < .9 ? "PASS" : "FAIL");
            require((label + "_shaft_behind_occluder").c_str(), shaft_n > 1000 && shaft / shaft_n < .9);
            std::printf("ORACLE scene=phase%u label=sky_clear mean=%.4f pixels=%u not_one=%u %s\n", phase, sky_lit / sky_n, sky_n, sky_bad, sky_bad == 0 ? "PASS" : "FAIL");
            require((label + "_sky_marched_lit").c_str(), sky_n > 1000 && sky_bad == 0);
            // Sky hue history against the CPU mean (the seed writes the whole mean).
            double coverage = 0; const auto mean = fog_reference::sky_mean(scene.depth, engine, W, H, p.decode, &coverage);
            double sky_error = 0; for (unsigned c = 0; c < 3; ++c) sky_error = std::max(sky_error, std::fabs(sky[c] - mean[c]) / mean[c]);
            std::printf("SKY scene=phase%u coverage=%.4f gpu=%.6f,%.6f,%.6f cpu=%.6f,%.6f,%.6f max_rel=%.5f\n", phase, coverage, sky[0], sky[1], sky[2], mean[0], mean[1], mean[2], sky_error);
            require((label + "_sky_hue").c_str(), coverage > .3 && sky_error <= .01);
            // Composite law from the GPU's own F and sky; alpha carried; the energy bound.
            const double sky_rgb[3] = {sky[0], sky[1], sky[2]};
            const double gain_bound = 4. * *std::max_element(p.radiance, p.radiance + 3) * fog_phase(p.g, 1.) * (1 - std::exp(-p.tau_max));
            unsigned over = 0, alpha_changed = 0, bound_bad = 0, darker = 0; double max_rel = 0, sky_cap_error = 0;
            for (unsigned y = 0; y < H; ++y) for (unsigned x = 0; x < W; ++x) {
                const std::size_t i = std::size_t(y) * W + x;
                const auto ref = fog_reference::composite(scene.depth, lit, W, H, x, y, &engine[i * 4], sky_rgb, p);
                for (unsigned c = 0; c < 3; ++c) {
                    const double expected = std::pow(ref.linear_out[c], 1 / p.decode), actual = half_to_double(after[i * 4 + c]);
                    const double rel = std::fabs(actual - expected) / std::max(expected, 1e-3);
                    max_rel = std::max(max_rel, rel); over += rel > 2e-3;
                    const double linear_in = std::pow(engine[i * 4 + c], p.decode), linear_out = std::pow(actual, p.decode);
                    bound_bad += linear_out - linear_in * ref.transmittance > gain_bound * 1.01 + 2e-3 * linear_out;
                    darker += linear_out < linear_in * std::exp(-p.tau_max) * (1 - 3e-3);
                    // Sky cap in closed form (no reference call): a sentinel pixel beside the shaft carries the whole medium, F = 1.
                    if (scene.depth[i * 4] < 0 && ref.lit == 1.) {
                        const double cap = 1 - std::exp(-p.tau_max), closed = linear_in * (1 - cap) + fog_reference::hue_of(sky_rgb)[c] * p.radiance[c] * ref.phase * cap;
                        sky_cap_error = std::max(sky_cap_error, std::fabs(linear_out - closed) / closed);
                    }
                }
                alpha_changed += after[i * 4 + 3] != double_to_half(engine[i * 4 + 3]);
            }
            std::printf("APPLY scene=phase%u channels=%zu over=%u max_rel=%.6f alpha_changed=%u bound_bad=%u darker=%u gain_bound=%.6f sky_cap_error=%.3g\n", phase, std::size_t(W) * H * 3, over, max_rel, alpha_changed, bound_bad, darker, gain_bound, sky_cap_error);
            require((label + "_composite_law").c_str(), over == 0 && alpha_changed == 0);
            require((label + "_energy_bound").c_str(), bound_bad == 0 && darker == 0);
            require((label + "_sky_cap").c_str(), sky_cap_error > 0 && sky_cap_error < 5e-3);
            // The motion and depth targets stayed byte-identical and bound.
            require((label + "_rt2_untouched").c_str(), depth_before == f->read<float>(d, f->depth_surface.p, f->read_depth.p, W, H, 4));
            require((label + "_rt1_untouched").c_str(), rt1_before == f->read<std::uint8_t>(d, f->rt1_surface.p, f->read_rt1.p, W, H, 4));
        }
        p.jitter_index = 0;
        { // Sky history blend: a second update with another sky colour moves the history by the blend weight.
          const double other[3] = {.20, .10, .05};
          const auto before = sky_value(*f, d, pass);
          const std::vector<double> engine = f->fill_target(d, scene, other);
          FogResult out; FogFrame in = frame_inputs(*f, p, cascades); in.params.sky_blend = .25f;
          check("blend execute", pass.execute(in, &out));
          const auto after = sky_value(*f, d, pass); const auto mean = fog_reference::sky_mean(scene.depth, engine, W, H, p.decode, nullptr);
          double error = 0; for (unsigned c = 0; c < 3; ++c) { const double expected = .75 * before[c] + .25 * mean[c]; error = std::max(error, std::fabs(after[c] - expected) / expected); }
          std::printf("SKY scene=blend max_rel=%.5f\n", error);
          require("sky_history_blend", error <= .01);
          in.params.update_sky = false; const auto held = sky_value(*f, d, pass);
          check("hold execute", pass.execute(in, &out));
          require("sky_update_skipped", !out.sky_updated && sky_value(*f, d, pass) == held);
          // No sky on screen: the level's coverage is zero, the history is kept.
          Scene covered = scene; for (std::size_t i = 0; i < covered.depth.size(); i += 4) if (covered.depth[i] < 0) { covered.depth[i] = float(p.m22 + p.m32 / kWallZ); covered.depth[i + 2] = float(kWallZ); }
          f->upload_depth(d, covered, 4); in.params.update_sky = true;
          check("covered execute", pass.execute(in, &out));
          require("sky_history_kept_without_sky", out.sky_updated && sky_value(*f, d, pass) == held);
          f->upload_depth(d, scene, 4); }
        { // Cascade 0 absent: its samples fall through to cascade 1 (coarser texels); the shaft stays.
          std::vector<fog_reference::Cascade> partial = cascades; partial[0].valid = false;
          f->fill_target(d, scene, kSkyEngine);
          FogResult out; check("partial execute", pass.execute(frame_inputs(*f, p, partial), &out));
          const auto lit = lit_values(*f, d, out.lit);
          const auto twin = fog_reference::march(scene.depth, W, H, p, [&](const double q[3]) { return fog_reference::map_visibility(partial, p, q); });
          const Compare c = compare(lit, twin, 1. / 16 + 1e-3);
          std::printf("REFERENCE scene=cascade0_absent kind=map_twin pixels=%u mean_abs=%.6f max=%.4f within_one_step=%u\n", c.pixels, c.mean_abs, c.max, c.within);
          require("cascade_fallthrough", out.cascades_bound == 2 && c.mean_abs <= .002 && double(c.within) / c.pixels >= .998);
          // No valid cascade (the replay refused the frame): F = 1 everywhere, the veil without shafts.
          for (auto& k : partial) k.valid = false;
          check("no cascade execute", pass.execute(frame_inputs(*f, p, partial), &out));
          const auto flat = lit_values(*f, d, out.lit);
          require("no_cascade_all_lit", out.cascades_bound == 0 && out.applied && std::all_of(flat.begin(), flat.end(), [](double v) { return v == 1.; })); }
        { // R32F RT2 (no view-depth lane): z from the device depth; the same field within float32 depth precision.
          Frame narrow(d, W, H, D3DFMT_R32F); narrow.upload_depth(d, scene, 1); narrow.upload_maps(d, cascades); narrow.fill_target(d, scene, kSkyEngine);
          FogResult out; check("r32f execute", pass.execute(frame_inputs(narrow, p, cascades), &out));
          const Compare c = compare(lit_values(narrow, d, out.lit), clean_lit, 1. / 16 + 1e-3);
          std::printf("REFERENCE scene=r32f kind=against_view_depth_lane pixels=%u mean_abs=%.6f max=%.4f within_one_step=%u\n", c.pixels, c.mean_abs, c.max, c.within);
          require("r32f_depth_fallback", c.mean_abs <= .002 && double(c.within) / c.pixels >= .998);
          narrow.unbind(d, backbuffer.p); }
        // ---- fault ladder ----
        { f->fill_target(d, scene, kSkyEngine);
          const auto before = f->read<std::uint16_t>(d, f->target_surface.p, f->read_target.p, W, H, 4);
          f->hostile(d); Snapshot pre(d);
          FogResult out; faults = {}; faults.draw_fail_at = 1;
          HRESULT hr = pass.execute(frame_inputs(*f, p, cascades), &out); faults = {};
          require("fault_march_failed", hr == E_FAIL && out.failed == FogStage::March && !out.applied && out.lit == nullptr && SUCCEEDED(out.restore));
          faults.stretch_fail_at = 1; hr = pass.execute(frame_inputs(*f, p, cascades), &out); faults = {};
          require("fault_copy_failed", hr == E_FAIL && out.failed == FogStage::Copy && !out.applied);
          faults.draw_fail_at = 2; hr = pass.execute(frame_inputs(*f, p, cascades), &out); faults = {};
          require("fault_sky_failed", hr == E_FAIL && out.failed == FogStage::SkyLevel && !out.applied);
          Snapshot post(d);
          require("fault_restored_target_untouched", pre == post && before == f->read<std::uint16_t>(d, f->target_surface.p, f->read_target.p, W, H, 4));
          check("recover execute", pass.execute(frame_inputs(*f, p, cascades), &out));
          require("fault_recovered_bit_identical", lit_values(*f, d, out.lit) == clean_lit);
          faults = {}; faults.draw_fail_at = 2; faults.draw_error = D3DERR_DEVICELOST; // the recovery above drew: the counter restarts
          check("lost scene", d->BeginScene());
          FogFrame lost_frame = frame_inputs(*f, p, cascades); lost_frame.caller_scene_open = true; lost_frame.params.update_sky = false;
          hr = pass.execute(lost_frame, &out); faults = {};
          check("lost scene end", d->EndScene());
          require("fault_lost_mid_chain", hr == D3DERR_DEVICELOST && out.failed == FogStage::Composite && out.restore == D3DERR_DEVICELOST);
          require("execute_after_lost_still_runs", SUCCEEDED(pass.execute(frame_inputs(*f, p, cascades), &out))); }
        // ---- Reset ----
        pass.before_reset();
        { FogResult out;
          require("before_reset_released", pass.references() == 6 && pass.reset_pending() && !pass.sky_seeded());
          require("refuse_while_reset_pending", pass.execute(frame_inputs(*f, p, cascades), &out) == D3DERR_DEVICENOTRESET && pass.prepare(W, H) == D3DERR_DEVICENOTRESET); }
        f->unbind(d, backbuffer.p); delete f; f = nullptr; backbuffer.reset();
        check("unbind depth", d->SetDepthStencilSurface(nullptr));
        const HRESULT reset = d->Reset(&pp); check("Reset", reset); pass.after_reset(reset);
        require("after_reset_accepts", !pass.reset_pending());
        check("backbuffer after reset", d->GetRenderTarget(0, &backbuffer.p));
        { Frame g(d, W, H); g.upload_depth(d, scene, 4); g.upload_maps(d, cascades); g.fill_target(d, scene, kSkyEngine);
          g.hostile(d); Snapshot pre(d);
          FogResult out; const HRESULT hr = pass.execute(frame_inputs(g, p, cascades), &out);
          Snapshot post(d);
          const bool same_lit = SUCCEEDED(hr) && lit_values(g, d, out.lit) == clean_lit;
          const bool same_target = g.read<std::uint16_t>(d, g.target_surface.p, g.read_target.p, W, H, 4) == clean_after;
          require("reset_bit_identical", same_lit && same_target && pre == post && pass.references() == 15 && pass.allocations() == 2 && out.sky_updated);
          std::printf("RESET PASS references=%u allocations=%u\n", pass.references(), pass.allocations());
          g.unbind(d, backbuffer.p); }
        timing(d, pass, 1280, 768, freq.QuadPart, cascades);
        check("timing unbind", d->SetRenderTarget(1, nullptr)); check("timing unbind", d->SetRenderTarget(2, nullptr)); check("timing unbind", d->SetRenderTarget(0, backbuffer.p));
        timing(d, pass, 1920, 1080, freq.QuadPart, cascades);
        check("timing unbind", d->SetRenderTarget(1, nullptr)); check("timing unbind", d->SetRenderTarget(2, nullptr)); check("timing unbind", d->SetRenderTarget(0, backbuffer.p));
        timing(d, pass, 1280, 768, freq.QuadPart, cascades); // repeat: the first block of a device runs cold
        check("timing unbind", d->SetRenderTarget(1, nullptr)); check("timing unbind", d->SetRenderTarget(2, nullptr)); check("timing unbind", d->SetRenderTarget(0, backbuffer.p));
        for (UINT i = 0; i < 4; ++i) { d->SetTexture(i, nullptr); d->SetTexture(D3DVERTEXTEXTURESAMPLER0 + i, nullptr); }
        pass.detach();
        require("detach_released", pass.references() == 0 && !pass.caps().enabled);
        std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::printf("RESULT FAIL exception=%s checks=%u failures=%u\n", e.what(), checks, failures);
        return 2;
    }
}
