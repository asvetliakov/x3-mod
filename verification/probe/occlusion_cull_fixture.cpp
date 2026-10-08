// Occlusion cull fixture (docs/architecture/occlusion-cull.md): the production pass (src/renderer/occlusion_cull_pass.cpp)
// on a real device, a synthetic scene of one hull and parts behind and in front of it, run twice per target shape: the
// HDR scene target A16B16G16R16F with the motion route's lazy RT1 (A32B32G32R32F, a -0.0 lane included) and RT2 (R32F)
// bound through the whole scene as between routed draws, and A8R8G8B8 alone: once with the cull (every part a
// candidate, tested at its draw site, skipped on the pass's verdict) and once without, frame by frame, every target read
// back (RT1/RT2 must be byte-identical with the cull on and off outside the one late frame).
//
//   occlusion_cull_fixture.exe <d3d9 path | builtin>
//
// Scene (clip space, w = 1, 256 x 256, CULLMODE cycling NONE/CW/CCW per frame for the parts): the hull covers the left
// half at z 0.3 until frame K, then only its upper half (the "mover" part below is revealed with its rectangle
// unchanged, so the pass skips it once more: the one late frame); three parts sit behind the hull, two right of it,
// one in front of it, one right of it with alpha test on, and a "teleport" part jumps from behind the hull to the right
// half at frame T (the stability guard draws it at once). After frame R the device is Reset (the pass releases and
// recreates its queries; the first frame after draws everything). Frame NR is presented without the GPU wait the other
// frames get, so the next frame may read results not ready (drawn, counted). Asserts: hidden parts skipped from the
// second frame on (except after the Reset and after a not-ready read), visible parts always drawn, the teleport drawn at
// T, the mover skipped through K and drawn from K + 1 (drawn_late 1), the frames identical with and without the cull
// except frame K (the difference inside the mover's rectangle), the application state restored after every test, the
// query pool back after the Reset, and the device's reference count unchanged by attach/detach. A last phase draws six
// frames back to back without readback or GPU wait, so reads may find results not ready: a skip only ever follows a
// ready hidden result. Documented D3D9 only.
#include <windows.h>
#include <d3d9.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../../src/renderer/occlusion_cull_pass.h"

namespace {
namespace oc = x3m::occlusion_cull::core;
using x3m::renderer::OcclusionCullPass;
using x3m::renderer::OcclusionCullState;
using x3m::renderer::OcclusionCullVerdict;
unsigned checks = 0, failures = 0;
void check(bool ok, const char* what) {
    ++checks;
    if (!ok) ++failures;
    std::printf("CHECK %s %s\n", what, ok ? "PASS" : "FAIL");
}
constexpr int kSize = 256, kFrames = 14, kK = 7, kT = 5, kR = 10, kNR = 8;
// The "game" programs: vs_3_0 position pass-through, ps_3_0 oC0 = c0.
constexpr DWORD game_vs[] = {0xfffe0300u, 0x0200001fu, 0x80000000u, 0x900f0000u, 0x0200001fu, 0x80000000u,
                             0xe00f0000u, 0x02000001u, 0xe00f0000u, 0x90e40000u, 0x0000ffffu};
constexpr DWORD game_ps[] = {0xffff0300u, 0x02000001u, 0x800f0800u, 0xa0e40000u, 0x0000ffffu};
// The routed shape: oC0 = c0, oC1 = c1 (the motion lane), oC2 = c2 (the depth lane).
constexpr DWORD game_mrt_ps[] = {0xffff0300u, 0x02000001u, 0x800f0800u, 0xa0e40000u, 0x02000001u, 0x800f0801u,
                                 0xa0e40001u, 0x02000001u, 0x800f0802u, 0xa0e40002u, 0x0000ffffu};
enum Kind { hidden, visible, front, mover, teleport, alpha };
struct Part {
    const char* name;
    Kind kind;
    float x0, y0, x1, y1, z0, z1;
    float color[4];
};
const Part parts[] = {
    {"hidden1", hidden, -0.9f, 0.2f, -0.6f, 0.5f, 0.5f, 0.7f, {1, 0, 0, 1}},
    {"hidden2", hidden, -0.5f, 0.6f, -0.2f, 0.9f, 0.5f, 0.7f, {0, 1, 0, 1}},
    {"hidden3", hidden, -0.9f, 0.6f, -0.6f, 0.9f, 0.5f, 0.7f, {0, 0, 1, 1}},
    {"visible1", visible, 0.2f, 0.2f, 0.5f, 0.5f, 0.5f, 0.7f, {1, 1, 0, 1}},
    {"visible2", visible, 0.6f, 0.6f, 0.9f, 0.9f, 0.5f, 0.7f, {0, 1, 1, 1}},
    {"front", front, -0.8f, -0.2f, -0.5f, 0.1f, 0.1f, 0.2f, {1, 0, 1, 1}},
    {"mover", mover, -0.6f, -0.7f, -0.3f, -0.4f, 0.5f, 0.7f, {1, .5f, 0, 1}},
    {"teleport", teleport, -0.5f, 0.2f, -0.2f, 0.5f, 0.5f, 0.7f, {.5f, 0, 1, 1}},
    {"alpha", alpha, 0.2f, -0.9f, 0.5f, -0.6f, 0.5f, 0.7f, {.5f, 1, .5f, 1}},
};
constexpr int kParts = int(sizeof parts / sizeof parts[0]);
constexpr float teleport_after[4] = {0.3f, -0.5f, 0.6f, -0.2f}; // x0 y0 x1 y1 from frame T
// Box corners (k bit 0 x, bit 1 y, bit 2 z) and the twelve triangles.
constexpr int box_index[36] = {0, 1, 2, 2, 1, 3, 4, 6, 5, 5, 6, 7, 0, 4, 1, 1, 4, 5,
                               2, 3, 6, 6, 3, 7, 0, 2, 4, 4, 2, 6, 1, 5, 3, 3, 5, 7};
struct V {
    float x, y, z, w;
};
void box(V* out, float x0, float y0, float x1, float y1, float z0, float z1) {
    for (int t = 0; t < 36; ++t) {
        const int k = box_index[t];
        out[t] = {(k & 1) ? x1 : x0, (k & 2) ? y1 : y0, (k & 4) ? z1 : z0, 1};
    }
}
void quad(V* out, float x0, float y0, float x1, float y1, float z) { // two triangles, both windings drawn (cull NONE)
    const V a{x0, y0, z, 1}, b{x1, y0, z, 1}, c{x0, y1, z, 1}, d{x1, y1, z, 1};
    out[0] = a, out[1] = b, out[2] = c, out[3] = c, out[4] = b, out[5] = d;
}
// Vertex buffer layout: [0] hull full (6), [6] hull upper (6), then per part a box (36), then the teleport's second box.
constexpr int hull_full = 0, hull_upper = 6, parts_base = 12, teleport_second = parts_base + kParts * 36,
              vertex_count = teleport_second + 36;
oc::Box part_box(int i, int frame) {
    const Part& p = parts[i];
    if (p.kind == teleport && frame >= kT)
        return {{teleport_after[0], teleport_after[1], p.z0}, {teleport_after[2], teleport_after[3], p.z1}};
    return {{p.x0, p.y0, p.z0}, {p.x1, p.y1, p.z1}};
}

struct Device {
    IDirect3DDevice9* d = nullptr;
    D3DPRESENT_PARAMETERS pp{};
    IDirect3DSurface9 *rt = nullptr, *sys = nullptr, *depth = nullptr;
    // The route's lazy RT1 (A32B32G32R32F) and RT2 (R32F), bound through the scene when mrt (the fp16 run).
    IDirect3DSurface9 *rt1 = nullptr, *rt2 = nullptr, *sys1 = nullptr, *sys2 = nullptr;
    IDirect3DPixelShader9* mrt_ps = nullptr;
    bool mrt = false;
    IDirect3DVertexBuffer9* vb = nullptr;
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexDeclaration9* decl = nullptr;
    D3DFORMAT format = D3DFMT_A8R8G8B8;
    bool targets(bool create) {
        if (rt) rt->Release(), rt = nullptr;
        if (depth) depth->Release(), depth = nullptr;
        if (rt1) rt1->Release(), rt1 = nullptr;
        if (rt2) rt2->Release(), rt2 = nullptr;
        if (!create) return true;
        return SUCCEEDED(d->CreateRenderTarget(kSize, kSize, format, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, nullptr)) &&
               SUCCEEDED(d->CreateDepthStencilSurface(kSize, kSize, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                                      &depth, nullptr)) &&
               (!mrt || (SUCCEEDED(d->CreateRenderTarget(kSize, kSize, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0,
                                                         FALSE, &rt1, nullptr)) &&
                         SUCCEEDED(d->CreateRenderTarget(kSize, kSize, D3DFMT_R32F, D3DMULTISAMPLE_NONE, 0, FALSE,
                                                         &rt2, nullptr))));
    }
};
ULONG references(IDirect3DDevice9* d) {
    d->AddRef();
    return d->Release();
}
void wait_gpu(IDirect3DDevice9* d) {
    IDirect3DQuery9* q = nullptr;
    if (FAILED(d->CreateQuery(D3DQUERYTYPE_EVENT, &q)) || !q) return;
    q->Issue(D3DISSUE_END);
    const DWORD start = GetTickCount();
    BOOL done = FALSE;
    while (q->GetData(&done, sizeof done, D3DGETDATA_FLUSH) == S_FALSE && GetTickCount() - start < 5000) Sleep(0);
    q->Release();
}

struct FrameLog {
    bool drawn[kParts];
    x3m::renderer::OcclusionCullFrameStats stats;
    std::vector<unsigned char> image, image1, image2; // RT0, and RT1 / RT2 when bound
};
bool read_target(IDirect3DDevice9* d, IDirect3DSurface9* rt, IDirect3DSurface9* sys, unsigned bytes_per_pixel,
                 std::vector<unsigned char>& out) {
    if (FAILED(d->GetRenderTargetData(rt, sys))) return false;
    D3DLOCKED_RECT lr{};
    if (FAILED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
    const unsigned row = kSize * bytes_per_pixel;
    out.resize(std::size_t(row) * kSize);
    for (int y = 0; y < kSize; ++y)
        std::memcpy(out.data() + std::size_t(y) * row, static_cast<const char*>(lr.pBits) + y * lr.Pitch, row);
    sys->UnlockRect();
    return true;
}

// One run of kFrames frames; cull = the pass decides. Returns false on a setup failure.
bool run(Device& dev, OcclusionCullPass* pass, FrameLog (&log)[kFrames], unsigned* state_failures, unsigned* resets_ok) {
    IDirect3DDevice9* d = dev.d;
    const DWORD culls[3] = {D3DCULL_NONE, D3DCULL_CW, D3DCULL_CCW};
    const float reserved[8] = {7, 7, 7, 7, 9, 9, 9, 9}; // the "application's" c252/c253, checked after every test
    for (int frame = 0; frame < kFrames; ++frame) {
        FrameLog& f = log[frame];
        if (pass) pass->begin_frame(std::uint32_t(frame + 1));
        d->SetRenderTarget(0, dev.rt);
        d->SetRenderTarget(1, dev.mrt ? dev.rt1 : nullptr);
        d->SetRenderTarget(2, dev.mrt ? dev.rt2 : nullptr);
        d->SetDepthStencilSurface(dev.depth);
        d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0x80336699, 1.0f, 0);
        d->BeginScene();
        d->SetVertexDeclaration(dev.decl);
        d->SetStreamSource(0, dev.vb, 0, sizeof(V));
        d->SetVertexShader(dev.vs);
        d->SetPixelShader(dev.mrt ? dev.mrt_ps : dev.ps);
        {
            const float minus_zero = -0.0f;
            const float lane1[4] = {minus_zero, 2.5f, -1.0f, 1e-30f}, lane2[4] = {0.75f, 0, 0, 1};
            d->SetPixelShaderConstantF(1, lane1, 1);
            d->SetPixelShaderConstantF(2, lane2, 1);
        }
        d->SetVertexShaderConstantF(252, reserved, 2);
        d->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
        d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        const float grey[4] = {.4f, .4f, .4f, 1};
        d->SetPixelShaderConstantF(0, grey, 1);
        d->DrawPrimitive(D3DPT_TRIANGLELIST, frame < kK ? hull_full : hull_upper, 2);
        for (int i = 0; i < kParts; ++i) {
            const Part& p = parts[i];
            const DWORD cull = culls[frame % 3];
            d->SetRenderState(D3DRS_CULLMODE, cull);
            if (p.kind == alpha) {
                d->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
                d->SetRenderState(D3DRS_ALPHAREF, 0x80);
                d->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
            }
            d->SetPixelShaderConstantF(0, p.color, 1);
            bool skip = false;
            if (pass) {
                static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
                oc::Rect rect{};
                const oc::Box b = part_box(i, frame);
                if (oc::test_rect(identity, b, kSize, kSize, oc::cmp_lessequal, &rect) == oc::RectStatus::ok) {
                    OcclusionCullState state{};
                    state.z_write = TRUE;
                    state.alpha_test = p.kind == alpha;
                    state.cull = cull;
                    state.reserved = reserved;
                    HRESULT restore = S_OK;
                    const auto verdict =
                        pass->candidate(oc::draw_key(std::uint32_t(i + 1), 0, 0, 36, 7), rect, true, state, &restore);
                    skip = verdict == OcclusionCullVerdict::skip;
                    // The application's state is back: shaders, declaration, stream, the changed render states, c252/c253.
                    IDirect3DVertexShader9* vs = nullptr;
                    IDirect3DPixelShader9* ps = nullptr;
                    IDirect3DVertexDeclaration9* decl = nullptr;
                    IDirect3DVertexBuffer9* vb = nullptr;
                    UINT offset = 1, stride = 0;
                    DWORD zw = 0, ab = 1, sb = 0, db = 1, at = 2, cm = 0;
                    float c[8]{};
                    d->GetVertexShader(&vs);
                    d->GetPixelShader(&ps);
                    d->GetVertexDeclaration(&decl);
                    d->GetStreamSource(0, &vb, &offset, &stride);
                    d->GetRenderState(D3DRS_ZWRITEENABLE, &zw);
                    d->GetRenderState(D3DRS_ALPHABLENDENABLE, &ab);
                    d->GetRenderState(D3DRS_SRCBLEND, &sb);
                    d->GetRenderState(D3DRS_DESTBLEND, &db);
                    d->GetRenderState(D3DRS_ALPHATESTENABLE, &at);
                    d->GetRenderState(D3DRS_CULLMODE, &cm);
                    d->GetVertexShaderConstantF(252, c, 2);
                    const bool same = FAILED(restore) == false && vs == dev.vs && ps == (dev.mrt ? dev.mrt_ps : dev.ps) && decl == dev.decl &&
                                      vb == dev.vb && offset == 0 && stride == sizeof(V) && zw == TRUE && ab == FALSE &&
                                      sb == D3DBLEND_ONE && db == D3DBLEND_ZERO && at == DWORD(p.kind == alpha) &&
                                      cm == cull && !std::memcmp(c, reserved, sizeof c);
                    if (!same) ++*state_failures;
                    if (vs) vs->Release();
                    if (ps) ps->Release();
                    if (decl) decl->Release();
                    if (vb) vb->Release();
                }
            }
            f.drawn[i] = !skip;
            if (!skip) {
                const int start = p.kind == teleport && frame >= kT ? teleport_second : parts_base + i * 36;
                d->DrawPrimitive(D3DPT_TRIANGLELIST, UINT(start), 12);
            }
            if (p.kind == alpha) d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        }
        d->EndScene();
        if (pass) {
            f.stats = pass->frame_stats();
            pass->frame_stats() = {};
        }
        d->SetRenderTarget(1, nullptr);
        d->SetRenderTarget(2, nullptr);
        if (!read_target(d, dev.rt, dev.sys, dev.format == D3DFMT_A16B16G16R16F ? 8 : 4, f.image)) return false;
        if (dev.mrt && (!read_target(d, dev.rt1, dev.sys1, 16, f.image1) || !read_target(d, dev.rt2, dev.sys2, 4, f.image2)))
            return false;
        d->Present(nullptr, nullptr, nullptr, nullptr);
        if (frame != kNR) wait_gpu(d);
        if (frame == kR) { // Reset: the DEFAULT targets and the pass's queries go first
            if (pass) pass->before_reset();
            dev.targets(false);
            const HRESULT hr = d->Reset(&dev.pp);
            if (pass) {
                pass->after_reset(hr);
                if (!pass->recreate_pending() || FAILED(pass->recreate())) return false; // the owner's lazy recreation
            }
            if (SUCCEEDED(hr) && dev.targets(true) && (!pass || pass->queries() == oc::pool_size)) ++*resets_ok;
            else
                return false;
        }
    }
    return true;
}

// Back-to-back frames without readback or GPU wait (the readback above synchronises): the next frame's reads may find
// results not ready. Every part is drawn unless its previous result was ready and hidden.
void unready_frames(Device& dev, OcclusionCullPass& pass, unsigned* not_ready, unsigned* skipped, unsigned* hidden,
                    unsigned* tested) {
    IDirect3DDevice9* d = dev.d;
    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (int frame = 0; frame < 6; ++frame) {
        pass.begin_frame(std::uint32_t(1000 + frame));
        d->SetRenderTarget(0, dev.rt);
        d->SetDepthStencilSurface(dev.depth);
        d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0x80336699, 1.0f, 0);
        d->BeginScene();
        d->SetVertexDeclaration(dev.decl);
        d->SetStreamSource(0, dev.vb, 0, sizeof(V));
        d->SetVertexShader(dev.vs);
        d->SetPixelShader(dev.ps);
        d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        d->DrawPrimitive(D3DPT_TRIANGLELIST, hull_full, 2);
        for (int i = 0; i < kParts; ++i) {
            oc::Rect rect{};
            if (oc::test_rect(identity, part_box(i, 0), kSize, kSize, oc::cmp_lessequal, &rect) != oc::RectStatus::ok)
                continue;
            OcclusionCullState state{};
            state.cull = D3DCULL_NONE;
            HRESULT restore = S_OK;
            if (pass.candidate(oc::draw_key(std::uint32_t(i + 1), 0, 0, 36, 7), rect, true, state, &restore) !=
                OcclusionCullVerdict::skip)
                d->DrawPrimitive(D3DPT_TRIANGLELIST, UINT(parts_base + i * 36), 12);
        }
        d->EndScene();
        d->Present(nullptr, nullptr, nullptr, nullptr);
        const auto& st = pass.frame_stats();
        *not_ready += st.not_ready;
        *skipped += st.skipped;
        *hidden += st.age1 + st.age2; // decisions on a ready result
        *tested += st.tested;
        if (st.skipped > st.age1 + st.age2) *hidden = 0, *skipped = ~0u; // a skip without a ready result: fail
        pass.frame_stats() = {};
    }
    wait_gpu(d);
}

LRESULT CALLBACK window_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    return DefWindowProcA(w, m, wp, lp);
}

int run_main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s <d3d9 path|builtin>\n", argv[0]);
        return 2;
    }
    const bool builtin = !std::strcmp(argv[1], "builtin");
    HMODULE module = LoadLibraryA(builtin ? "d3d9.dll" : argv[1]);
    if (!module) return 1;
    using Create9 = IDirect3D9*(WINAPI*)(UINT);
    Create9 create = nullptr;
    {
        auto p = GetProcAddress(module, "Direct3DCreate9");
        std::memcpy(&create, &p, sizeof p);
    }
    IDirect3D9* d3d = create ? create(D3D_SDK_VERSION) : nullptr;
    if (!d3d) return 1;
    D3DADAPTER_IDENTIFIER9 id{};
    d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
    std::printf("ADAPTER description=%s\n", id.Description);
    WNDCLASSA wc{};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "x3m_occlusion_cull";
    RegisterClassA(&wc);
    HWND window = CreateWindowExA(0, wc.lpszClassName, "x3m occlusion cull", WS_OVERLAPPEDWINDOW, 0, 0, kSize, kSize,
                                  nullptr, nullptr, wc.hInstance, nullptr);
    Device dev;
    dev.pp.BackBufferWidth = dev.pp.BackBufferHeight = kSize;
    dev.pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    dev.pp.BackBufferCount = 1;
    dev.pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    dev.pp.hDeviceWindow = window;
    dev.pp.Windowed = TRUE;
    dev.pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    // The game's device shape as the motion route creates it: HWVP | FPU_PRESERVE (PUREDEVICE stripped, capture.cpp).
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &dev.pp, &dev.d);
    check(SUCCEEDED(hr), "device");
    if (FAILED(hr)) return 1;
    IDirect3DDevice9* d = dev.d;
    const D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
    bool ok = SUCCEEDED(d->CreateVertexShader(game_vs, &dev.vs)) && SUCCEEDED(d->CreatePixelShader(game_ps, &dev.ps)) &&
              SUCCEEDED(d->CreateVertexDeclaration(elements, &dev.decl)) &&
              SUCCEEDED(d->CreateVertexBuffer(vertex_count * sizeof(V), D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &dev.vb,
                                              nullptr));
    void* p = nullptr;
    if (ok && SUCCEEDED(dev.vb->Lock(0, 0, &p, 0))) {
        V* v = static_cast<V*>(p);
        quad(v + hull_full, -1, -1, 0, 1, 0.3f);
        quad(v + hull_upper, -1, 0, 0, 1, 0.3f);
        for (int i = 0; i < kParts; ++i)
            box(v + parts_base + i * 36, parts[i].x0, parts[i].y0, parts[i].x1, parts[i].y1, parts[i].z0, parts[i].z1);
        box(v + teleport_second, teleport_after[0], teleport_after[1], teleport_after[2], teleport_after[3], 0.5f, 0.7f);
        dev.vb->Unlock();
    } else
        ok = false;
    check(ok, "scene resources");
    if (!ok) return 1;
    void* const* native = *reinterpret_cast<void* const* const*>(d);
    ok = SUCCEEDED(d->CreatePixelShader(game_mrt_ps, &dev.mrt_ps)) &&
         SUCCEEDED(d->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &dev.sys1, nullptr)) &&
         SUCCEEDED(d->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &dev.sys2, nullptr));
    check(ok, "RT1/RT2 resources");
    for (D3DFORMAT format : {D3DFMT_A16B16G16R16F, D3DFMT_A8R8G8B8}) {
        // fp16: the HDR scene target with the route's lazy RT1/RT2 bound through the scene; argb8: one target.
        const char* fname = format == D3DFMT_A16B16G16R16F ? "fp16_mrt" : "argb8";
        dev.format = format;
        dev.mrt = format == D3DFMT_A16B16G16R16F;
        if (dev.sys) dev.sys->Release(), dev.sys = nullptr;
        ok = dev.targets(true) &&
             SUCCEEDED(d->CreateOffscreenPlainSurface(kSize, kSize, format, D3DPOOL_SYSTEMMEM, &dev.sys, nullptr));
        if (!ok) {
            std::printf("FORMAT name=%s skipped=targets\n", fname);
            continue;
        }
        static FrameLog on[kFrames], off[kFrames];
        char label_unready[128];
        unsigned state_failures = 0, resets_on = 0, resets_off = 0;
        const ULONG refs_before = references(d);
        OcclusionCullPass pass;
        hr = pass.attach(d, native);
        std::printf("ATTACH format=%s hr=%08lx reason=%s queries=%u references_added=%ld\n", fname, (unsigned long)hr,
                    pass.reason(), pass.queries(), long(references(d)) - long(refs_before));
        check(SUCCEEDED(hr) && pass.queries() == oc::pool_size, "attach: the pool of 1,024 queries");
        if (FAILED(hr)) continue;
        const bool ran_on = run(dev, &pass, on, &state_failures, &resets_on);
        unsigned nr = 0, nr_skipped = 0, nr_hidden = 0, nr_tested = 0;
        if (ran_on) unready_frames(dev, pass, &nr, &nr_skipped, &nr_hidden, &nr_tested);
        std::printf("UNREADY format=%s frames=6 tested=%u not_ready=%u ready_decisions=%u skipped=%u\n", fname, nr_tested, nr,
                    nr_hidden, nr_skipped);
        std::snprintf(label_unready, sizeof label_unready,
                      "%s: back-to-back frames: a skip only on a ready hidden result (not ready = drawn)", fname);
        check(ran_on && nr_tested == 6u * kParts && nr_skipped <= nr_hidden, label_unready);
        pass.detach();
        check(references(d) == refs_before, "detach: the device's reference count is back");
        const bool ran_off = run(dev, nullptr, off, &state_failures, &resets_off);
        check(ran_on && ran_off && resets_on == 1 && resets_off == 1, "both runs completed, one Reset each");
        if (!ran_on || !ran_off) continue;
        // Per-frame rows.
        unsigned diff_frames = 0, diff_outside_mover = 0, late = 0, errors = 0, skipped_over_hidden = 0;
        unsigned lane_diff_frames = 0, lane_diff_outside = 0;
        int diff_frame = -1, lane_diff_frame = -1;
        // Texels that differ between the runs, and those outside the mover's rectangle (x -0.6..-0.3, y -0.7..-0.4;
        // window y down; one pixel margin).
        auto compare = [&](const std::vector<unsigned char>& a, const std::vector<unsigned char>& b, unsigned bpp,
                           unsigned* outside) {
            unsigned diff = 0;
            if (a.size() != b.size()) return 0xffffffffu;
            for (int y = 0; y < kSize; ++y)
                for (int x = 0; x < kSize; ++x) {
                    const std::size_t at = (std::size_t(y) * kSize + x) * bpp;
                    if (std::memcmp(a.data() + at, b.data() + at, bpp)) {
                        ++diff;
                        const float nx = (x + .5f) / kSize * 2 - 1, ny = 1 - (y + .5f) / kSize * 2;
                        if (nx < -0.61f || nx > -0.29f || ny < -0.71f || ny > -0.39f) ++*outside;
                    }
                }
            return diff;
        };
        for (int f = 0; f < kFrames; ++f) {
            char mask[kParts + 1]{};
            for (int i = 0; i < kParts; ++i) mask[i] = on[f].drawn[i] ? 'D' : 's';
            const unsigned diff = compare(on[f].image, off[f].image, format == D3DFMT_A16B16G16R16F ? 8 : 4, &diff_outside_mover);
            const unsigned diff1 = dev.mrt ? compare(on[f].image1, off[f].image1, 16, &lane_diff_outside) : 0;
            const unsigned diff2 = dev.mrt ? compare(on[f].image2, off[f].image2, 4, &lane_diff_outside) : 0;
            if (diff) ++diff_frames, diff_frame = f;
            if (diff1 || diff2) ++lane_diff_frames, lane_diff_frame = f;
            const auto& s = on[f].stats;
            late += s.drawn_late;
            errors += s.errors;
            if (s.skipped > s.age1 + s.age2) ++skipped_over_hidden; // a skip needs a ready result
            std::printf("FRAME format=%s frame=%d drawn=%s candidates=%u tested=%u skipped=%u hidden=%u visible=%u "
                        "not_ready=%u ready_age=%u,%u,%u errors=%u drawn_late=%u unstable=%u pool_truncated=%u "
                        "diff_px=%u rt1_diff=%u rt2_diff=%u\n",
                        fname, f, mask, s.candidates, s.tested, s.skipped, s.hidden, s.visible, s.not_ready, s.age1,
                        s.age2, s.age_none, s.errors, s.drawn_late, s.unstable, s.pool_truncated, diff, diff1, diff2);
        }
        // Expected skips: a hidden part from frame 1 on, except the first frame after the Reset and a frame whose
        // previous results were not ready (counted by the pass).
        unsigned hidden_skips = 0, hidden_expected = 0, visible_skips = 0;
        for (int f = 1; f < kFrames; ++f) {
            const bool fresh = f == kR + 1, unready = on[f].stats.age_none > 0;
            for (int i = 0; i < kParts; ++i) {
                const Kind k = parts[i].kind;
                if (k == visible || k == front || k == alpha) visible_skips += on[f].drawn[i] ? 0 : 1;
                if (k == hidden && !fresh && !unready) {
                    ++hidden_expected;
                    hidden_skips += on[f].drawn[i] ? 0 : 1;
                }
            }
        }
        for (int i = 0; i < kParts; ++i) visible_skips += parts[i].kind != hidden && !on[0].drawn[i];
        const int mi = 6, ti = 7;
        const bool not_ready_frames_ok = on[kNR + 1].stats.not_ready == 0 || on[kNR + 1].stats.skipped == 0 ||
                                         on[kNR + 1].stats.skipped <= on[kNR + 1].stats.hidden;
        std::printf("SUMMARY format=%s hidden_skips=%u hidden_expected=%u visible_skips=%u teleport_at_T=%c "
                    "mover_at_K=%c mover_after_K=%c drawn_late=%u diff_frames=%u diff_frame=%d diff_outside_mover=%u "
                    "state_failures=%u errors=%u not_ready_after_NR=%u fresh_skipped=%u lane_diff_frames=%u "
                    "lane_diff_frame=%d lane_diff_outside=%u\n",
                    fname, hidden_skips, hidden_expected, visible_skips, on[kT].drawn[ti] ? 'D' : 's',
                    on[kK].drawn[mi] ? 'D' : 's', on[kK + 1].drawn[mi] ? 'D' : 's', late, diff_frames, diff_frame,
                    diff_outside_mover, state_failures, errors, on[kNR + 1].stats.not_ready, on[kR + 1].stats.skipped,
                    lane_diff_frames, lane_diff_frame, lane_diff_outside);
        char label[128];
        std::snprintf(label, sizeof label, "%s: hidden parts skipped from frame 2 on (every frame with ready results)", fname);
        check(hidden_expected >= 3 * 8 && hidden_skips == hidden_expected, label);
        std::snprintf(label, sizeof label, "%s: visible, in-front and alpha-tested parts always drawn", fname);
        check(visible_skips == 0, label);
        std::snprintf(label, sizeof label, "%s: the teleport is drawn the frame it leaves the hull (guard)", fname);
        check(on[kT].drawn[ti] && !on[kT - 1].drawn[ti], label);
        std::snprintf(label, sizeof label, "%s: the revealed part is late by exactly one frame (skipped at K, drawn from K+1)", fname);
        check(!on[kK].drawn[mi] && on[kK + 1].drawn[mi] && on[kK + 2].drawn[mi] && late == 1, label);
        std::snprintf(label, sizeof label, "%s: frames identical with the cull on and off except frame K, inside the part", fname);
        check(diff_frames == 1 && diff_frame == kK && diff_outside_mover == 0, label);
        if (dev.mrt) {
            std::snprintf(label, sizeof label,
                          "%s: RT1 (RGBA32F) and RT2 (R32F) byte-identical on and off except frame K, inside the part",
                          fname);
            check(lane_diff_frames == 1 && lane_diff_frame == kK && lane_diff_outside == 0, label);
        }
        std::snprintf(label, sizeof label, "%s: the application state is back after every test", fname);
        check(state_failures == 0, label);
        std::snprintf(label, sizeof label, "%s: the first frame after the Reset draws everything; no query errors", fname);
        check(on[kR + 1].stats.skipped == 0 && errors == 0 && skipped_over_hidden == 0 && not_ready_frames_ok, label);
        dev.targets(false);
    }
    if (dev.sys) dev.sys->Release();
    if (dev.sys1) dev.sys1->Release();
    if (dev.sys2) dev.sys2->Release();
    if (dev.mrt_ps) dev.mrt_ps->Release();
    dev.vb->Release();
    dev.decl->Release();
    dev.ps->Release();
    dev.vs->Release();
    d->Release();
    d3d->Release();
    std::printf("RESULT checks=%u failed=%u %s\n", checks, failures, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    return run_main(argc, argv);
}
