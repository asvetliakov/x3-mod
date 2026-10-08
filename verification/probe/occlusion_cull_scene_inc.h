// The occlusion cull's realistic-state scene (docs/architecture/occlusion-cull.md, "Cost"), shared by
// occlusion_cull_fixture.cpp (the batched pass) and verification/results/occlusion-cull-batched/legacy_cost.cpp (the
// Run137 per-part pass built from commit 833f1ac7), so both measure the same frame. No dependency on the cull's code:
// the caller's hooks see each hull piece and each part draw and answer whether the part is skipped.
//
// 1280 x 720, the HDR scene shape the motion route draws into: RT0 A16B16G16R16F, the route's lazy RT1 (A32B32G32R32F)
// and RT2 (R32F) bound through the scene, D24S8. A "game" vs_3_0 (clip = rows x position, rows in c0-c3, as the engine's
// world-view-projection) and an MRT ps_3_0 (oC0..oC2 from c0..c2) are bound before every hook; every draw sets its rows
// and eight further vertex constants and its colour, as the engine does per draw. Three ships, each four hull pieces
// (quads at depth 0.3, drawn with D3DCULL_NONE) and then 50 parts (12-triangle boxes, D3DCULL_CCW): 37 behind the hull
// (depth 0.5-0.6) and 13 above or below it, 111 hidden and 39 visible in all. Ship 2 drifts right by 0.0015 per frame
// (about one pixel); from frame `reveal` ship 0 draws without its first hull piece (the upper left quadrant), revealing
// the hidden parts behind it.
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace occlusion_scene {
constexpr int width = 1280, height = 720, ships = 3, parts_per_ship = 50, hidden_per_ship = 37, pieces = 4;
constexpr float hull_half_w = 0.25f, hull_half_h = 0.5f, hull_z = 0.3f, drift = 0.0015f;
struct V {
    float x, y, z, w;
};
struct PartDef {
    float lo[3], hi[3];
    bool hidden;
    int quadrant; // the hull piece in front of it (-1: none)
};
// vs_3_0: dcl_position v0; dcl_position o0; dp4 o0.x, v0, c0; dp4 o0.y, v0, c1; dp4 o0.z, v0, c2; dp4 o0.w, v0, c3.
constexpr DWORD game_vs[] = {0xfffe0300u, 0x0200001fu, 0x80000000u, 0x900f0000u, 0x0200001fu, 0x80000000u,
                             0xe00f0000u, 0x03000009u, 0xe0010000u, 0x90e40000u, 0xa0e40000u, 0x03000009u,
                             0xe0020000u, 0x90e40000u, 0xa0e40001u, 0x03000009u, 0xe0040000u, 0x90e40000u,
                             0xa0e40002u, 0x03000009u, 0xe0080000u, 0x90e40000u, 0xa0e40003u, 0x0000ffffu};
// ps_3_0: oC0 = c0, oC1 = c1 (the motion lane), oC2 = c2 (the depth lane).
constexpr DWORD game_ps[] = {0xffff0300u, 0x02000001u, 0x800f0800u, 0xa0e40000u, 0x02000001u, 0x800f0801u,
                             0xa0e40001u, 0x02000001u, 0x800f0802u, 0xa0e40002u, 0x0000ffffu};
constexpr int box_index[36] = {0, 1, 2, 2, 1, 3, 4, 6, 5, 5, 6, 7, 0, 4, 1, 1, 4, 5,
                               2, 3, 6, 6, 3, 7, 0, 2, 4, 4, 2, 6, 1, 5, 3, 3, 5, 7};
inline float ship_x(int s) {
    return -0.6f + 0.6f * float(s);
}
// The ship's rows at a frame: a translation in clip space (w = 1), ship 2 drifting.
inline void ship_rows(int s, int frame, float rows[16]) {
    const float tx = ship_x(s) + (s == 2 ? drift * float(frame) : 0.f);
    const float r[16] = {1, 0, 0, tx, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(rows, r, sizeof r);
}
// Ship-local layout: hidden parts on a 7 x 6 grid inside the hull (37 of its 42 cells), visible parts on two rows
// above and below it.
inline PartDef part_def(int i) {
    PartDef p{};
    if (i < hidden_per_ship) {
        const int col = i % 6, row = i / 6; // 6 columns x 7 rows (the 37th cell starts row 6)
        const float x0 = -0.22f + 0.075f * float(col), y0 = -0.46f + 0.13f * float(row);
        p = PartDef{{x0, y0, 0.5f}, {x0 + 0.05f, y0 + 0.08f, 0.6f}, true, (x0 + 0.025f < 0.f ? 0 : 1) + (y0 + 0.04f < 0.f ? 2 : 0)};
    } else {
        const int k = i - hidden_per_ship, col = k % 7;
        const float x0 = -0.24f + 0.07f * float(col), y0 = k < 7 ? 0.6f : -0.72f;
        p = PartDef{{x0, y0, 0.5f}, {x0 + 0.04f, y0 + 0.1f, 0.6f}, false, -1};
    }
    return p;
}
// Hull pieces: 0 upper left, 1 upper right, 2 lower left, 3 lower right (ship-local).
inline void piece_rect(int piece, float* x0, float* y0, float* x1, float* y1) {
    *x0 = piece & 1 ? 0.f : -hull_half_w;
    *x1 = piece & 1 ? hull_half_w : 0.f;
    *y0 = piece & 2 ? -hull_half_h : 0.f;
    *y1 = piece & 2 ? 0.f : hull_half_h;
}
constexpr int pieces_base = 0, parts_base = pieces * 6, vertex_count = parts_base + parts_per_ship * 36;

struct Scene {
    IDirect3DDevice9* d = nullptr;
    IDirect3DSurface9 *rt0 = nullptr, *rt1 = nullptr, *rt2 = nullptr, *depth = nullptr;
    IDirect3DSurface9 *sys0 = nullptr, *sys1 = nullptr, *sys2 = nullptr;
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexDeclaration9* decl = nullptr;
    IDirect3DVertexBuffer9* vb = nullptr;
    bool create(IDirect3DDevice9* device) {
        d = device;
        const D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
        bool ok = SUCCEEDED(d->CreateRenderTarget(width, height, D3DFMT_A16B16G16R16F, D3DMULTISAMPLE_NONE, 0, FALSE, &rt0, nullptr)) &&
                  SUCCEEDED(d->CreateRenderTarget(width, height, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &rt1, nullptr)) &&
                  SUCCEEDED(d->CreateRenderTarget(width, height, D3DFMT_R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &rt2, nullptr)) &&
                  SUCCEEDED(d->CreateDepthStencilSurface(width, height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &depth, nullptr)) &&
                  SUCCEEDED(d->CreateOffscreenPlainSurface(width, height, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &sys0, nullptr)) &&
                  SUCCEEDED(d->CreateOffscreenPlainSurface(width, height, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &sys1, nullptr)) &&
                  SUCCEEDED(d->CreateOffscreenPlainSurface(width, height, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &sys2, nullptr)) &&
                  SUCCEEDED(d->CreateVertexShader(game_vs, &vs)) && SUCCEEDED(d->CreatePixelShader(game_ps, &ps)) &&
                  SUCCEEDED(d->CreateVertexDeclaration(elements, &decl)) &&
                  SUCCEEDED(d->CreateVertexBuffer(vertex_count * sizeof(V), D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &vb, nullptr));
        void* p = nullptr;
        if (!ok || FAILED(vb->Lock(0, 0, &p, 0))) return false;
        V* v = static_cast<V*>(p);
        for (int piece = 0; piece < pieces; ++piece) {
            float x0, y0, x1, y1;
            piece_rect(piece, &x0, &y0, &x1, &y1);
            const V a{x0, y0, hull_z, 1}, b{x1, y0, hull_z, 1}, c{x0, y1, hull_z, 1}, e{x1, y1, hull_z, 1};
            V* q = v + pieces_base + piece * 6;
            q[0] = a, q[1] = b, q[2] = c, q[3] = c, q[4] = b, q[5] = e;
        }
        for (int i = 0; i < parts_per_ship; ++i) {
            const PartDef pd = part_def(i);
            for (int t = 0; t < 36; ++t) {
                const int k = box_index[t];
                v[parts_base + i * 36 + t] = {(k & 1) ? pd.hi[0] : pd.lo[0], (k & 2) ? pd.hi[1] : pd.lo[1],
                                              (k & 4) ? pd.hi[2] : pd.lo[2], 1};
            }
        }
        return SUCCEEDED(vb->Unlock());
    }
    void release() {
        IUnknown* all[] = {vb, decl, ps, vs, sys2, sys1, sys0, depth, rt2, rt1, rt0};
        for (IUnknown* u : all)
            if (u) u->Release();
        vb = nullptr, decl = nullptr, ps = nullptr, vs = nullptr, sys0 = sys1 = sys2 = nullptr, depth = nullptr;
        rt0 = rt1 = rt2 = nullptr;
    }
    static bool read(IDirect3DDevice9* d, IDirect3DSurface9* rt, IDirect3DSurface9* sys, unsigned bpp,
                     std::vector<unsigned char>& out) {
        if (FAILED(d->GetRenderTargetData(rt, sys))) return false;
        D3DLOCKED_RECT lr{};
        if (FAILED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
        const unsigned row = width * bpp;
        out.resize(std::size_t(row) * height);
        for (int y = 0; y < height; ++y)
            std::memcpy(out.data() + std::size_t(y) * row, static_cast<const char*>(lr.pBits) + y * lr.Pitch, row);
        sys->UnlockRect();
        return true;
    }
    // RT0, RT1, RT2 of the last frame (RT1/RT2 unbound first, as the route's flush does).
    bool readback(std::vector<unsigned char>* images) {
        d->SetRenderTarget(1, nullptr);
        d->SetRenderTarget(2, nullptr);
        return read(d, rt0, sys0, 8, images[0]) && read(d, rt1, sys1, 16, images[1]) && read(d, rt2, sys2, 4, images[2]);
    }
    // One frame. hooks.hull(ship, piece, rows) after each hull piece; hooks.part(ship, index, rows, def) before each
    // part, true = skipped. Returns the CPU time from BeginScene to EndScene in microseconds.
    template <class Hooks> double frame(int frame_index, int reveal, Hooks& hooks) {
        LARGE_INTEGER f{}, t0{}, t1{};
        QueryPerformanceFrequency(&f);
        d->SetRenderTarget(0, rt0);
        d->SetRenderTarget(1, rt1);
        d->SetRenderTarget(2, rt2);
        d->SetDepthStencilSurface(depth);
        d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0x80336699, 1.0f, 0);
        QueryPerformanceCounter(&t0);
        d->BeginScene();
        d->SetVertexDeclaration(decl);
        d->SetStreamSource(0, vb, 0, sizeof(V));
        d->SetVertexShader(vs);
        d->SetPixelShader(ps);
        d->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
        const float lane1[4] = {-0.0f, 2.5f, -1.0f, 1e-30f}, lane2[4] = {0.75f, 0, 0, 1};
        d->SetPixelShaderConstantF(1, lane1, 1);
        d->SetPixelShaderConstantF(2, lane2, 1);
        float filler[32];
        for (int i = 0; i < 32; ++i) filler[i] = 0.01f * float(i + frame_index);
        for (int s = 0; s < ships; ++s) {
            float rows[16];
            ship_rows(s, frame_index, rows);
            d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            for (int piece = 0; piece < pieces; ++piece) {
                if (s == 0 && piece == 0 && frame_index >= reveal) continue;
                d->SetVertexShaderConstantF(0, rows, 4);
                d->SetVertexShaderConstantF(4, filler, 8);
                const float grey[4] = {.4f + .1f * float(piece), .4f, .4f, 1};
                d->SetPixelShaderConstantF(0, grey, 1);
                d->DrawPrimitive(D3DPT_TRIANGLELIST, UINT(pieces_base + piece * 6), 2);
                hooks.hull(s, piece, rows);
            }
            d->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
            for (int i = 0; i < parts_per_ship; ++i) {
                const PartDef pd = part_def(i);
                d->SetVertexShaderConstantF(0, rows, 4);
                d->SetVertexShaderConstantF(4, filler, 8);
                const float color[4] = {float(i % 5) * .2f, float(s) * .3f, float(i % 3) * .3f, 1};
                d->SetPixelShaderConstantF(0, color, 1);
                if (!hooks.part(s, i, rows, pd)) d->DrawPrimitive(D3DPT_TRIANGLELIST, UINT(parts_base + i * 36), 12);
            }
        }
        d->EndScene();
        QueryPerformanceCounter(&t1);
        return double(t1.QuadPart - t0.QuadPart) * 1e6 / double(f.QuadPart);
    }
};
// Frame pacing for the timing phases: the remainder of a 16 ms period (the game's ~60 fps), so the GPU queue does not
// back up and lag-1 results are ready as in flight.
struct Pacer {
    LARGE_INTEGER f{}, last{};
    Pacer() {
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&last);
    }
    void wait(double period_ms = 16.0) {
        for (;;) {
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            const double ms = double(now.QuadPart - last.QuadPart) * 1e3 / double(f.QuadPart);
            if (ms >= period_ms) {
                last = now;
                return;
            }
            Sleep(ms < period_ms - 2.0 ? 1 : 0);
        }
    }
};
inline double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    for (std::size_t i = 1; i < v.size(); ++i) // insertion sort: a few hundred values
        for (std::size_t j = i; j > 0 && v[j - 1] > v[j]; --j) std::swap(v[j - 1], v[j]);
    const std::size_t at = std::size_t(p * double(v.size() - 1) + 0.5);
    return v[at < v.size() ? at : v.size() - 1];
}
}
