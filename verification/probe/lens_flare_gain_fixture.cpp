// Detached GPU fixture of the lens-flare gain's blend law (src/proxy/lens_flare_gain.h; docs/architecture/
// sun-partial-occlusion.md, "Lens-flare gain"), carved out of the deleted partial-sun-occlusion fixture in own sun step 2
// (docs/architecture/own-sun-pass.md section 14). The production law and steps on a ps_2_0 ONE/ONE quad of
// 0.8,0.6,0.4 over a known background (26,51,77 of 255), in MotionOutput::apply_lens_gain's order (checked before the
// first setter, put back after the draw from the application's values): G = 1 off, G = 0.5 through BLENDFACTOR, G = 0
// not issued (Law::skip; an occlusion query around the transaction counts the rasterised samples). A hostile
// BLENDFACTOR is the application's and must come back; the caller's state is compared byte for byte around every
// transaction. Validation-only readback. Built by build_own_sun_hook.py; run by run_own_sun_hook.py.
#include <windows.h>
#include <d3d9.h>
#include "../../src/proxy/lens_flare_gain.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
template <class T> struct Com {
    T* p = nullptr;
    ~Com() { reset(); }
    void reset() {
        if (p) {
            p->Release();
            p = nullptr;
        }
    }
    T* operator->() const { return p; }
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
};
unsigned checks = 0, failures = 0;
void check(const char* label, HRESULT hr) {
    if (FAILED(hr)) {
        std::printf("API FAIL %s %08lx\n", label, (unsigned long)hr);
        throw std::runtime_error(label);
    }
}
bool require(const char* label, bool value) {
    ++checks;
    if (!value) ++failures;
    std::printf("CHECK %s %s\n", label, value ? "PASS" : "FAIL");
    return value;
}
// ---- the caller's state, compared byte for byte around every transaction ----
struct Snapshot {
    std::vector<unsigned char> bytes;
    template <class T> void add(const T& v) {
        auto* p = reinterpret_cast<const unsigned char*>(&v);
        bytes.insert(bytes.end(), p, p + sizeof v);
    }
    template <class T> void object(T* v) {
        add(reinterpret_cast<std::uintptr_t>(v));
        if (v) v->Release();
    }
    explicit Snapshot(IDirect3DDevice9* d) {
        for (UINT i = 0; i < 4; ++i) {
            IDirect3DSurface9* s = nullptr;
            HRESULT hr = d->GetRenderTarget(i, &s);
            add(hr);
            object(s);
        }
        IDirect3DSurface9* ds = nullptr;
        HRESULT hr = d->GetDepthStencilSurface(&ds);
        add(hr);
        object(ds);
        D3DVIEWPORT9 vp{};
        check("snapshot viewport", d->GetViewport(&vp));
        add(vp);
        RECT sc{};
        check("snapshot scissor", d->GetScissorRect(&sc));
        add(sc);
        IDirect3DVertexShader9* vs = nullptr;
        check("snapshot vs", d->GetVertexShader(&vs));
        object(vs);
        IDirect3DPixelShader9* ps = nullptr;
        check("snapshot ps", d->GetPixelShader(&ps));
        object(ps);
        DWORD fvf = 0;
        check("snapshot fvf", d->GetFVF(&fvf));
        add(fvf);
        for (UINT n = 0; n < 4; ++n) {
            IDirect3DBaseTexture9* t = nullptr;
            check("snapshot texture", d->GetTexture(n, &t));
            object(t);
            for (UINT j = 1; j <= 13; ++j) {
                DWORD v = 0;
                hr = d->GetSamplerState(n, D3DSAMPLERSTATETYPE(j), &v);
                add(hr);
                add(v);
            }
        }
        // clang-format off
        const D3DRENDERSTATETYPE states[] = {D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC, D3DRS_STENCILENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHAREF, D3DRS_ALPHAFUNC, D3DRS_ALPHABLENDENABLE,
            D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP, D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_SRCBLENDALPHA, D3DRS_DESTBLENDALPHA, D3DRS_FOGENABLE,
            D3DRS_SRGBWRITEENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_CLIPPING, D3DRS_LIGHTING, D3DRS_FILLMODE, D3DRS_CULLMODE,
            D3DRS_COLORWRITEENABLE, D3DRS_MULTISAMPLEMASK, D3DRS_DITHERENABLE, D3DRS_BLENDFACTOR};
        // clang-format on
        for (auto s : states) {
            DWORD v = 0;
            check("snapshot rs", d->GetRenderState(s, &v));
            add(v);
        }
        float c[16]{};
        check("snapshot constants", d->GetPixelShaderConstantF(0, c, 4));
        add(c);
    }
    bool operator==(const Snapshot& o) const { return bytes == o.bytes; }
};
struct Canvas {
    Com<IDirect3DTexture9> texture;
    Com<IDirect3DSurface9> surface, sink;
    explicit Canvas(IDirect3DDevice9* d) {
        check("canvas", d->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture.p,
                                         nullptr));
        check("canvas surface", texture->GetSurfaceLevel(0, &surface.p));
        check("canvas sink",
              d->CreateOffscreenPlainSurface(16, 16, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sink.p, nullptr));
    }
    void pixel(IDirect3DDevice9* d, double out[3]) {
        check("canvas read", d->GetRenderTargetData(surface.p, sink.p));
        D3DLOCKED_RECT lr{};
        check("canvas lock", sink->LockRect(&lr, nullptr, D3DLOCK_READONLY));
        const DWORD c = static_cast<const DWORD*>(lr.pBits)[8 + 8 * (lr.Pitch / 4)];
        check("canvas unlock", sink->UnlockRect());
        out[0] = ((c >> 16) & 255) / 255.;
        out[1] = ((c >> 8) & 255) / 255.;
        out[2] = (c & 255) / 255.;
    }
};
bool close_to(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}
// ps_2_0: def c0, 0.8, 0.6, 0.4, 0.5 / mov r0, c0 / mov oC0, r0.
const DWORD ps20_words[] = {0xffff0200, 0x05000051, 0xa00f0000, 0x3f4ccccd, 0x3f19999a, 0x3ecccccd, 0x3f000000,
                            0x02000001, 0x800f0000, 0xa0e40000, 0x02000001, 0x800f0800, 0x80e40000, 0x0000ffff};
const double source[3] = {.8, .6, .4}, background[3] = {26. / 255., 51. / 255., 77. / 255.};
// The application's state for one lens draw: hostile textures, samplers and constants on the units the gain never
// touches, then what an additive ONE/ONE sprite draw sets itself, and a hostile BLENDFACTOR.
void prepare(IDirect3DDevice9* d, Canvas& canvas, IDirect3DTexture9* junk, IDirect3DPixelShader9* ps) {
    for (UINT i = 0; i < 2; ++i) {
        check("hostile texture", d->SetTexture(i, junk));
        check("hostile sampler", d->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_BORDER));
        check("hostile sampler", d->SetSamplerState(i, D3DSAMP_SRGBTEXTURE, TRUE));
    }
    const float garbage[16] = {9, 8, 7, 6, 5, 4, 3, 2, 1, .5f, .25f, .125f, -1, -2, -3, -4};
    check("hostile constants", d->SetPixelShaderConstantF(0, garbage, 4));
    for (UINT i = 1; i < 4; ++i) check("lens rt off", d->SetRenderTarget(i, nullptr));
    check("lens rt", d->SetRenderTarget(0, canvas.surface.p));
    check("lens depth", d->SetDepthStencilSurface(nullptr));
    check("lens clear", d->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 26, 51, 77), 1.f, 0));
    for (auto [s, v] : {std::pair{D3DRS_ALPHABLENDENABLE, DWORD(TRUE)}, std::pair{D3DRS_SRCBLEND, DWORD(D3DBLEND_ONE)},
                        std::pair{D3DRS_DESTBLEND, DWORD(D3DBLEND_ONE)}, std::pair{D3DRS_BLENDOP, DWORD(D3DBLENDOP_ADD)},
                        std::pair{D3DRS_SCISSORTESTENABLE, DWORD(FALSE)}, std::pair{D3DRS_ZENABLE, DWORD(FALSE)},
                        std::pair{D3DRS_COLORWRITEENABLE, DWORD(15)}, std::pair{D3DRS_CULLMODE, DWORD(D3DCULL_NONE)},
                        std::pair{D3DRS_FILLMODE, DWORD(D3DFILL_SOLID)}, std::pair{D3DRS_SRGBWRITEENABLE, DWORD(FALSE)},
                        std::pair{D3DRS_FOGENABLE, DWORD(FALSE)}, std::pair{D3DRS_ALPHATESTENABLE, DWORD(FALSE)},
                        std::pair{D3DRS_STENCILENABLE, DWORD(FALSE)}, std::pair{D3DRS_SEPARATEALPHABLENDENABLE, DWORD(FALSE)},
                        std::pair{D3DRS_MULTISAMPLEMASK, DWORD(0xffffffff)}, std::pair{D3DRS_CLIPPING, DWORD(FALSE)},
                        std::pair{D3DRS_BLENDFACTOR, DWORD(0x12345678u)}})
        check("lens rs", d->SetRenderState(s, v));
    check("lens fvf", d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1));
    check("lens vs", d->SetVertexShader(nullptr));
    check("lens ps", d->SetPixelShader(ps));
}
void draw(IDirect3DDevice9* d) {
    const float v[4][6] = {
        {-.5f, -.5f, 0, 1, 0, 0}, {15.5f, -.5f, 0, 1, 1, 0}, {-.5f, 15.5f, 0, 1, 0, 1}, {15.5f, 15.5f, 0, 1, 1, 1}};
    check("lens begin scene", d->BeginScene());
    check("lens draw", d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, 24));
    check("lens end scene", d->EndScene());
}
} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        WNDCLASSA cls{};
        cls.lpfnWndProc = DefWindowProcA;
        cls.hInstance = GetModuleHandleA(nullptr);
        cls.lpszClassName = "X3LensFlareGainFixture";
        RegisterClassA(&cls);
        HWND window = CreateWindowA(cls.lpszClassName, "X3 lens-flare gain fixture", WS_OVERLAPPEDWINDOW, 90, 90, 128,
                                    128, nullptr, nullptr, cls.hInstance, nullptr);
        if (!window) throw std::runtime_error("window");
        HMODULE runtime = LoadLibraryA("d3d9.dll");
        if (!runtime) throw std::runtime_error("d3d9.dll");
        auto address = GetProcAddress(runtime, "Direct3DCreate9");
        IDirect3D9*(WINAPI * create)(UINT) = nullptr;
        std::memcpy(&create, &address, sizeof create);
        if (!create) throw std::runtime_error("Direct3DCreate9");
        Com<IDirect3D9> api;
        api.p = create(D3D_SDK_VERSION);
        if (!api.p) throw std::runtime_error("Create9");
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window;
        pp.BackBufferWidth = 64;
        pp.BackBufferHeight = 64;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        Com<IDirect3DDevice9> device;
        check("CreateDevice",
              api->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device.p));
        IDirect3DDevice9* d = device.p;
        D3DCAPS9 caps{};
        check("caps", d->GetDeviceCaps(&caps));
        namespace lfg = x3m::lens_flare_gain;
        const bool cap = (caps.SrcBlendCaps & D3DPBLENDCAPS_BLENDFACTOR) != 0;
        const lfg::Law off = lfg::law(1.f), half = lfg::law(.5f), zero = lfg::law(0.f), high = lfg::law(1.5f),
                       nan = lfg::law(std::nanf(""));
        require("lens_gain_law", !off.active && half.active && half.constant && half.factor == 0x80808080u &&
                                     zero.active && !zero.constant && !high.active && !nan.active && zero.skip &&
                                     !half.skip && !off.skip && !high.skip && !nan.skip);
        require("lens_gain_admits", lfg::admits(TRUE, D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_ADD) &&
                                        !lfg::admits(FALSE, D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_ADD) &&
                                        !lfg::admits(TRUE, D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_ADD) &&
                                        !lfg::admits(TRUE, D3DBLEND_ONE, D3DBLEND_INVSRCCOLOR, D3DBLENDOP_ADD) &&
                                        !lfg::admits(TRUE, D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_MAX));
        require("lens_gain_blendfactor_cap", cap);
        Canvas canvas(d);
        Com<IDirect3DTexture9> junk;
        check("junk", d->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &junk.p, nullptr));
        Com<IDirect3DPixelShader9> ps20;
        check("ps20", d->CreatePixelShader(ps20_words, &ps20.p));
        Com<IDirect3DQuery9> occlusion;
        const bool query = SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_OCCLUSION, &occlusion.p)) && occlusion.p;
        require("lens_gain_occlusion_query", query);
        struct Issued {
            unsigned draws = 0, skipped = 0;
            long samples = -1;
        };
        // One draw in apply_lens_gain's order; true when every snapshotted state is the application's afterwards.
        auto gained = [&](const lfg::Law& law, double out[3], Issued& issued) {
            prepare(d, canvas, junk.p, ps20.p);
            const Snapshot before(d);
            DWORD enable = 0, src = 0, dst = 0, op = 0, factor = 0;
            check("gain read", d->GetRenderState(D3DRS_ALPHABLENDENABLE, &enable));
            check("gain read", d->GetRenderState(D3DRS_SRCBLEND, &src));
            check("gain read", d->GetRenderState(D3DRS_DESTBLEND, &dst));
            check("gain read", d->GetRenderState(D3DRS_BLENDOP, &op));
            check("gain read", d->GetRenderState(D3DRS_BLENDFACTOR, &factor));
            D3DRENDERSTATETYPE states[2]{};
            DWORD values[2]{};
            const bool admitted = law.active && lfg::admits(enable, src, dst, op);
            const bool skip = admitted && law.skip; // apply_lens_gain: not submitted, no setter
            const unsigned n = admitted && !skip ? lfg::steps(law, states, values) : 0u;
            for (unsigned i = 0; i < n; ++i) check("gain set", d->SetRenderState(states[i], values[i]));
            if (query) check("gain query begin", occlusion.p->Issue(D3DISSUE_BEGIN));
            if (!skip) {
                draw(d);
                ++issued.draws;
            } else
                ++issued.skipped;
            if (query) {
                check("gain query end", occlusion.p->Issue(D3DISSUE_END));
                DWORD samples = 0;
                HRESULT hr = S_FALSE;
                for (unsigned spin = 0; spin < 100000 && hr == S_FALSE; ++spin)
                    hr = occlusion.p->GetData(&samples, sizeof samples, D3DGETDATA_FLUSH);
                issued.samples = hr == S_OK ? long(samples) : -1;
            }
            for (unsigned i = n; i; --i)
                check("gain restore", d->SetRenderState(states[i - 1], states[i - 1] == D3DRS_BLENDFACTOR ? factor : src));
            const Snapshot after(d);
            canvas.pixel(d, out);
            return before == after;
        };
        struct GainCase {
            const char* name;
            const lfg::Law* law;
        };
        const GainCase cases[] = {{"off", &off}, {"half", &half}, {"zero", &zero}};
        for (const GainCase& c : cases) {
            const double g = c.law->active ? double(c.law->factor & 255u) / 255. : 1.;
            double pixel[3], want[3];
            Issued issued;
            const bool restored = gained(*c.law, pixel, issued);
            bool matches = true;
            for (unsigned i = 0; i < 3; ++i) {
                want[i] = background[i] + source[i] * g;
                matches = matches && close_to(pixel[i], want[i], 2.5 / 255);
            }
            // Issued exactly when G > 0: one draw whose 16x16 quad passes samples; G = 0 none and no samples.
            const bool issue_ok = c.law->skip ? issued.draws == 0 && issued.skipped == 1 && issued.samples == 0
                                              : issued.draws == 1 && issued.skipped == 0 && issued.samples > 0;
            std::printf("LENS_GAIN name=%s gain=%.4f applied=%.4f r=%.4f g=%.4f b=%.4f expected_r=%.4f expected_g=%.4f "
                        "expected_b=%.4f restored=%u issued=%u skipped=%u samples=%ld\n",
                        c.name, double(c.law->gain), g, pixel[0], pixel[1], pixel[2], want[0], want[1], want[2],
                        unsigned(restored), issued.draws, issued.skipped, issued.samples);
            require((std::string("lens_gain_") + c.name).c_str(), matches && restored && issue_ok);
            if (c.law->skip) require("lens_gain_zero_not_issued", issue_ok && matches);
        }
        // Cost of the per-draw transaction without the draw: 113 lens draws (Mayhem 3, run364) x (two sets, two restores)
        // per frame, over 200 frames; CPU wall time of the calls only (the draw is the game's).
        LARGE_INTEGER frequency{}, t0{}, t1{};
        QueryPerformanceFrequency(&frequency);
        D3DRENDERSTATETYPE states[2]{};
        DWORD values[2]{};
        const unsigned n = lfg::steps(half, states, values);
        QueryPerformanceCounter(&t0);
        for (unsigned frame = 0; frame < 200; ++frame)
            for (unsigned i_draw = 0; i_draw < 113; ++i_draw) {
                for (unsigned i = 0; i < n; ++i) d->SetRenderState(states[i], values[i]);
                for (unsigned i = n; i; --i)
                    d->SetRenderState(states[i - 1], states[i - 1] == D3DRS_BLENDFACTOR ? 0xffffffffu : DWORD(D3DBLEND_ONE));
            }
        QueryPerformanceCounter(&t1);
        std::printf("LENS_GAIN_COST draws_per_frame=113 frames=200 calls_per_draw=%u us_per_frame=%.2f\n", 2 * n,
                    double(t1.QuadPart - t0.QuadPart) * 1e6 / double(frequency.QuadPart) / 200.);
        for (UINT i = 0; i < 4; ++i) d->SetTexture(i, nullptr);
        d->SetPixelShader(nullptr);
        std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::printf("RESULT FAIL exception=%s checks=%u failures=%u\n", e.what(), checks, failures);
        return 2;
    }
}
