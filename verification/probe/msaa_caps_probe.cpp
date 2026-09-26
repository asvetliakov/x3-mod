// Throwaway capability probe for docs/architecture/source-antialiasing.md.
// Documented D3D9 only. Prints multisample support for the route's formats,
// MRT caps, and whether a multisampled FP16 MRT set clears, draws and resolves.
// Build: i686-w64-mingw32-g++ -std=c++17 -O2 -static msaa_caps_probe.cpp -o build/msaa_caps_probe.exe -ld3d9 -luser32
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static const char* name(D3DFORMAT f) {
    switch (int(f)) {
    case D3DFMT_X8R8G8B8: return "X8R8G8B8";
    case D3DFMT_A8R8G8B8: return "A8R8G8B8";
    case D3DFMT_A16B16G16R16F: return "A16B16G16R16F";
    case D3DFMT_A32B32G32R32F: return "A32B32G32R32F";
    case D3DFMT_G32R32F: return "G32R32F";
    case D3DFMT_R32F: return "R32F";
    case D3DFMT_G16R16F: return "G16R16F";
    case D3DFMT_D24S8: return "D24S8";
    case D3DFMT_D24X8: return "D24X8";
    default: return "?";
    }
}

int main() {
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) {
        std::puts("create9 failed");
        return 1;
    }
    D3DADAPTER_IDENTIFIER9 id{};
    d3d->GetAdapterIdentifier(0, 0, &id);
    std::printf("adapter=%s driver=%s\n", id.Description, id.Driver);
    const D3DFORMAT formats[] = {D3DFMT_X8R8G8B8,      D3DFMT_A8R8G8B8, D3DFMT_A16B16G16R16F,
                                 D3DFMT_A32B32G32R32F, D3DFMT_G32R32F,  D3DFMT_R32F,
                                 D3DFMT_G16R16F,       D3DFMT_D24S8,    D3DFMT_D24X8};
    const D3DMULTISAMPLE_TYPE types[] = {D3DMULTISAMPLE_NONMASKABLE, D3DMULTISAMPLE_2_SAMPLES, D3DMULTISAMPLE_4_SAMPLES,
                                         D3DMULTISAMPLE_8_SAMPLES};
    for (D3DFORMAT f : formats)
        for (D3DMULTISAMPLE_TYPE t : types) {
            DWORD q = 0;
            const HRESULT hr = d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, f, TRUE, t, &q);
            std::printf("ms format=%s type=%u hr=%08lx quality_levels=%lu\n", name(f), unsigned(t), hr, q);
        }
    HWND window = CreateWindowA("STATIC", "msaa", WS_OVERLAPPEDWINDOW, 0, 0, 320, 240, nullptr, nullptr, nullptr,
                                nullptr);
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = 320;
    pp.BackBufferHeight = 240;
    pp.hDeviceWindow = window;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24X8;
    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
    std::printf("create_device hr=%08lx\n", hr);
    if (FAILED(hr)) return 2;
    D3DCAPS9 caps{};
    dev->GetDeviceCaps(&caps);
    std::printf(
        "caps NumSimultaneousRTs=%lu mrt_independent_bitdepths=%u mrt_postpixelshader_blending=%u stretchrect_caps=%08lx\n",
        caps.NumSimultaneousRTs, !!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS),
        !!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING), caps.StretchRectFilterCaps);
    for (D3DMULTISAMPLE_TYPE t : {D3DMULTISAMPLE_2_SAMPLES, D3DMULTISAMPLE_4_SAMPLES}) {
        const unsigned n = unsigned(t);
        IDirect3DSurface9 *rt0 = nullptr, *rt1 = nullptr, *rt2 = nullptr, *ds = nullptr, *old_ds = nullptr,
                          *old_rt = nullptr;
        IDirect3DTexture9 *t0 = nullptr, *t1 = nullptr, *t2 = nullptr;
        IDirect3DSurface9 *s0 = nullptr, *s1 = nullptr, *s2 = nullptr, *sys = nullptr;
        const HRESULT c0 = dev->CreateRenderTarget(320, 240, D3DFMT_A16B16G16R16F, t, 0, FALSE, &rt0, nullptr);
        const HRESULT c1 = dev->CreateRenderTarget(320, 240, D3DFMT_A32B32G32R32F, t, 0, FALSE, &rt1, nullptr);
        const HRESULT c2 = dev->CreateRenderTarget(320, 240, D3DFMT_A32B32G32R32F, t, 0, FALSE, &rt2, nullptr);
        const HRESULT cd = dev->CreateDepthStencilSurface(320, 240, D3DFMT_D24X8, t, 0, FALSE, &ds, nullptr);
        std::printf("x%u create fp16=%08lx rgba32f_a=%08lx rgba32f_b=%08lx d24x8=%08lx\n", n, c0, c1, c2, cd);
        dev->CreateTexture(320, 240, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &t0, nullptr);
        dev->CreateTexture(320, 240, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &t1, nullptr);
        dev->CreateTexture(320, 240, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &t2, nullptr);
        if (t0) t0->GetSurfaceLevel(0, &s0);
        if (t1) t1->GetSurfaceLevel(0, &s1);
        if (t2) t2->GetSurfaceLevel(0, &s2);
        dev->GetRenderTarget(0, &old_rt);
        dev->GetDepthStencilSurface(&old_ds);
        if (rt0 && rt1 && rt2 && ds && s0 && s1 && s2) {
            const HRESULT b0 = dev->SetRenderTarget(0, rt0), b1 = dev->SetRenderTarget(1, rt1),
                          b2 = dev->SetRenderTarget(2, rt2);
            const HRESULT bd = dev->SetDepthStencilSurface(ds);
            const HRESULT cl = dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.f, 0);
            // Half-covered pixel row: a triangle whose hypotenuse crosses pixels, fixed function, bright value in RT0
            // only.
            struct V {
                float x, y, z, w;
                DWORD c;
            } tri[3] = {{10.f, 10.f, .5f, 1.f, 0xffffffff},
                        {300.f, 17.f, .5f, 1.f, 0xffffffff},
                        {10.f, 200.f, .5f, 1.f, 0xffffffff}};
            dev->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
            dev->SetRenderState(D3DRS_LIGHTING, FALSE);
            dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            // ps_2_0: def c0,1,1,1,1; mov oC0,c0; mov oC1,c0; mov oC2,c0 (hand-assembled, no d3dx).
            static const DWORD ps_code[] = {0xffff0200, 0x05000051, 0xa00f0000, 0x3f800000, 0x3f800000, 0x3f800000,
                                            0x3f800000, 0x02000001, 0x800f0800, 0xa0e40000, 0x02000001, 0x800f0801,
                                            0xa0e40000, 0x02000001, 0x800f0802, 0xa0e40000, 0x0000ffff};
            IDirect3DPixelShader9* ps = nullptr;
            const HRESULT psr = dev->CreatePixelShader(ps_code, &ps);
            dev->SetPixelShader(ps);
            dev->BeginScene();
            const HRESULT dr = dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, tri, sizeof(V));
            dev->EndScene();
            dev->SetRenderTarget(1, nullptr);
            dev->SetRenderTarget(2, nullptr);
            const HRESULT r0 = dev->StretchRect(rt0, nullptr, s0, nullptr, D3DTEXF_NONE);
            const HRESULT r0l = dev->StretchRect(rt0, nullptr, s0, nullptr, D3DTEXF_LINEAR);
            const HRESULT r1 = dev->StretchRect(rt1, nullptr, s1, nullptr, D3DTEXF_POINT);
            const HRESULT r2 = dev->StretchRect(rt2, nullptr, s2, nullptr, D3DTEXF_NONE);
            std::printf(
                "x%u bind rt0=%08lx rt1=%08lx rt2=%08lx ds=%08lx clear=%08lx draw=%08lx resolve fp16_none=%08lx fp16_linear=%08lx rgba32f_point=%08lx rgba32f_none=%08lx\n",
                n, b0, b1, b2, bd, cl, dr, r0, r0l, r1, r2);
            // Read the resolved FP16 back: count distinct red values along the hypotenuse (>2 means a real multisample
            // resolve).
            if (SUCCEEDED(dev->CreateOffscreenPlainSurface(320, 240, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &sys,
                                                           nullptr)) &&
                SUCCEEDED(dev->GetRenderTargetData(s0, sys))) {
                D3DLOCKED_RECT lr{};
                if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
                    unsigned partial = 0, full = 0;
                    unsigned short seen[16]{};
                    unsigned kinds = 0;
                    for (unsigned y = 0; y < 240; ++y)
                        for (unsigned x = 0; x < 320; ++x) {
                            const unsigned short h = static_cast<const unsigned short*>(
                                lr.pBits)[(y * lr.Pitch / 2) + x * 4];
                            if (h == 0x3c00)
                                ++full;
                            else if (h)
                                ++partial;
                            bool known = false;
                            for (unsigned k = 0; k < kinds; ++k) known |= seen[k] == h;
                            if (!known && kinds < 16) seen[kinds++] = h;
                        }
                    std::printf("x%u resolved full=%u partial=%u distinct_values=%u\n", n, full, partial, kinds);
                    sys->UnlockRect();
                }
            }
            std::printf("x%u mrt_pixel_shader create=%08lx\n", n, psr);
            // Does a POINT (or NONE) resolve of the 32F target pick one sample or average? Count distinct red values.
            for (int pass = 0; pass < 2; ++pass) {
                IDirect3DSurface9* sys32 = nullptr;
                IDirect3DSurface9* from = pass ? s2 : s1;
                if (SUCCEEDED(dev->CreateOffscreenPlainSurface(320, 240, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM,
                                                               &sys32, nullptr)) &&
                    SUCCEEDED(dev->GetRenderTargetData(from, sys32))) {
                    D3DLOCKED_RECT lr{};
                    if (SUCCEEDED(sys32->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
                        unsigned partial = 0, full = 0;
                        for (unsigned y = 0; y < 240; ++y)
                            for (unsigned x = 0; x < 320; ++x) {
                                const float v = static_cast<const float*>(lr.pBits)[(y * lr.Pitch / 4) + x * 4];
                                if (v == 1.f)
                                    ++full;
                                else if (v != 0.f)
                                    ++partial;
                            }
                        std::printf("x%u rgba32f resolve filter=%s full=%u partial(averaged)=%u\n", n,
                                    pass ? "NONE" : "POINT", full, partial);
                        sys32->UnlockRect();
                    }
                }
                if (sys32) sys32->Release();
            }
            dev->SetPixelShader(nullptr);
            if (ps) ps->Release();
            // Mixed multisample types must be refused or fail at draw: MS colour with the non-MS auto depth.
            dev->SetRenderTarget(0, rt0);
            const HRESULT mix = dev->SetDepthStencilSurface(old_ds);
            dev->BeginScene();
            const HRESULT mix_draw = dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, tri, sizeof(V));
            dev->EndScene();
            std::printf("x%u mixed ms_colour+plain_depth set=%08lx draw=%08lx\n", n, mix, mix_draw);
        }
        dev->SetRenderTarget(0, old_rt);
        dev->SetDepthStencilSurface(old_ds);
        for (IUnknown* u : {(IUnknown*)rt0, (IUnknown*)rt1, (IUnknown*)rt2, (IUnknown*)ds, (IUnknown*)s0, (IUnknown*)s1,
                            (IUnknown*)s2, (IUnknown*)t0, (IUnknown*)t1, (IUnknown*)t2, (IUnknown*)sys,
                            (IUnknown*)old_rt, (IUnknown*)old_ds})
            if (u) u->Release();
    }
    dev->Release();
    d3d->Release();
    return 0;
}
