// Native D3D9 fixture of the sky-star batch (docs/architecture/sector-background-modern.md, "Implementation A"): the
// production renderer::SkyStarsPass (src/renderer/sky_stars_pass.cpp, built with X3M_SKY_STARS_FIXTURE) on a real
// device, no game. Renders the seed's batch into an A16B16G16R16F target at four sub-pixel offsets (the TAA jitter
// input of sky_stars::core::vertex_constants) and two view rotations and checks, per isolated star, the summed RGB
// against the buffer's energy (within 1 %) and across offsets (within 1 %), every pixel <= 1.0, and no written pixel
// outside a star's quad; that every state, binding and constant the pass touches is put back (with a draw fault and a
// restore fault injected); a temporal proxy (60 frames, 0.5 and 10 px/frame pans: an exponential history of weight 0.9
// with exact reprojection and a 3x3 min/max clip, a model of the resolve, not the resolve); and the GPU/CPU cost at
// 5120x1440. Prints PASS/FAIL lines and one RESULT line. Build: verification/results/sky-stars/run_fixture.py.
#include "../../src/renderer/sky_stars_pass.h"
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace core = x3m::sky_stars::core;
using x3m::renderer::SkyStarsPass;
using x3m::renderer::SkyStarsReport;

static unsigned checks = 0, failures = 0;
static void check(bool ok, const char* what, const char* detail = "") {
    ++checks;
    if (!ok) ++failures;
    std::printf("%s %s %s\n", ok ? "PASS" : "FAIL", what, detail);
}
static float half_to_float(std::uint16_t h) {
    const std::uint32_t sign = std::uint32_t(h >> 15) << 31, exp = (h >> 10) & 31, man = h & 1023;
    std::uint32_t bits;
    if (!exp) {
        if (!man) bits = sign;
        else {
            float v = std::ldexp(float(man), -24);
            return sign ? -v : v;
        }
    } else if (exp == 31)
        bits = sign | 0x7f800000u | (man << 13);
    else
        bits = sign | ((exp + 112) << 23) | (man << 13);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
struct Target {
    IDirect3DTexture9* texture = nullptr;
    IDirect3DSurface9* surface = nullptr;
    IDirect3DSurface9* readback = nullptr;
    UINT width = 0, height = 0;
    bool create(IDirect3DDevice9* d, UINT w, UINT h) {
        width = w;
        height = h;
        return SUCCEEDED(d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &texture,
                                          nullptr)) &&
               SUCCEEDED(texture->GetSurfaceLevel(0, &surface)) &&
               SUCCEEDED(d->CreateOffscreenPlainSurface(w, h, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &readback, nullptr));
    }
    void release() {
        if (readback) readback->Release();
        if (surface) surface->Release();
        if (texture) texture->Release();
        readback = surface = nullptr;
        texture = nullptr;
    }
    bool read(IDirect3DDevice9* d, std::vector<float>& rgb) {
        rgb.assign(std::size_t(width) * height * 3, 0.f);
        const HRESULT copied = d->GetRenderTargetData(surface, readback);
        D3DLOCKED_RECT lr{};
        const HRESULT locked = SUCCEEDED(copied) ? readback->LockRect(&lr, nullptr, D3DLOCK_READONLY) : E_FAIL;
        if (FAILED(copied) || FAILED(locked)) {
            std::printf("READBACK copy=%08lx lock=%08lx\n", copied, locked);
            return false;
        }
        for (UINT y = 0; y < height; ++y) {
            const auto* row = reinterpret_cast<const std::uint16_t*>(static_cast<const char*>(lr.pBits) + y * lr.Pitch);
            for (UINT x = 0; x < width; ++x)
                for (unsigned c = 0; c < 3; ++c) rgb[(std::size_t(y) * width + x) * 3 + c] = half_to_float(row[x * 4 + c]);
        }
        readback->UnlockRect();
        return true;
    }
};
// The view rotation of yaw (about y) then pitch (about x), as camera_reprojection.h's r (view_j = sum_i world_i r[i*3+j]).
static void rotation(double yaw_deg, double pitch_deg, float r[9]) {
    const double a = yaw_deg * 3.14159265358979 / 180, b = pitch_deg * 3.14159265358979 / 180;
    const double ya[9] = {std::cos(a), 0, -std::sin(a), 0, 1, 0, std::sin(a), 0, std::cos(a)};
    const double pb[9] = {1, 0, 0, 0, std::cos(b), std::sin(b), 0, -std::sin(b), std::cos(b)};
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j) {
            double s = 0;
            for (unsigned k = 0; k < 3; ++k) s += ya[i * 3 + k] * pb[k * 3 + j];
            r[i * 3 + j] = float(s);
        }
}
struct Star {
    unsigned index;
    double cx, cy, h;
    double expect[3];
};
// The vertex program's arithmetic on the CPU: the screen centre (D3D9 pixel centres at integer coordinates) and h.
static std::vector<Star> project(const std::vector<core::Vertex>& vb, unsigned n, const float c[16], UINT w, UINT h) {
    std::vector<Star> out;
    for (unsigned i = 0; i < n; ++i) {
        const auto& v = vb[i * 4];
        const float x = v.dir[0] * c[0] + v.dir[1] * c[1] + v.dir[2] * c[2];
        const float y = v.dir[0] * c[4] + v.dir[1] * c[5] + v.dir[2] * c[6];
        const float ww = v.dir[0] * c[8] + v.dir[1] * c[9] + v.dir[2] * c[10];
        if (!(ww > 1e-3f)) continue;
        const double cx = (double(x) / ww + 1.) * .5 * w, cy = (1. - double(y) / ww) * .5 * h;
        const double hh = std::fabs(v.corner[0]);
        if (cx < -hh - 2 || cy < -hh - 2 || cx > w + hh + 2 || cy > h + hh + 2) continue;
        Star s{i, cx, cy, hh, {}};
        const double tint[3] = {double((v.tint >> 16) & 255) / 255., double((v.tint >> 8) & 255) / 255.,
                                double(v.tint & 255) / 255.};
        for (unsigned k = 0; k < 3; ++k) s.expect[k] = v.energy * tint[k];
        out.push_back(s);
    }
    return out;
}
static bool isolated(const std::vector<Star>& stars, std::size_t i, UINT w, UINT h, double extra = 0) {
    const auto& s = stars[i];
    if (s.cx - s.h - 3 - extra < 0 || s.cy - s.h - 3 < 0 || s.cx + s.h + 3 + extra >= w || s.cy + s.h + 3 >= h) return false;
    for (std::size_t j = 0; j < stars.size(); ++j)
        if (j != i && std::fabs(stars[j].cx - s.cx) < s.h + stars[j].h + 3 + extra &&
            std::fabs(stars[j].cy - s.cy) < s.h + stars[j].h + 3)
            return false;
    return true;
}
static void window_sum(const std::vector<float>& rgb, UINT w, const Star& s, double out[3], double* peak) {
    out[0] = out[1] = out[2] = 0;
    *peak = 0;
    const int rows = int(rgb.size() / 3 / w);
    const int x0 = std::max(0, int(std::floor(s.cx - s.h - 1))), x1 = std::min(int(w) - 1, int(std::ceil(s.cx + s.h + 1)));
    const int y0 = std::max(0, int(std::floor(s.cy - s.h - 1))), y1 = std::min(rows - 1, int(std::ceil(s.cy + s.h + 1)));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            for (unsigned c = 0; c < 3; ++c) {
                const float v = rgb[(std::size_t(y) * w + unsigned(x)) * 3 + c];
                out[c] += v;
                if (v > *peak) *peak = v;
            }
}
static double halton(unsigned i, unsigned b) {
    double f = 1, r = 0;
    while (i) {
        f /= b;
        r += f * (i % b);
        i /= b;
    }
    return r;
}
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcA(h, m, w, l); }

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    WNDCLASSA wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "x3m_sky_stars_fixture";
    RegisterClassA(&wc);
    HWND window = CreateWindowA(wc.lpszClassName, "sky stars", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr,
                                wc.hInstance, nullptr);
    IDirect3D9* api = Direct3DCreate9(D3D_SDK_VERSION);
    if (!window || !api) {
        std::printf("RESULT FAIL setup\n");
        return 2;
    }
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.hDeviceWindow = window;
    IDirect3DDevice9* d = nullptr;
    HRESULT hr = api->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &d);
    if (FAILED(hr)) hr = api->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                           D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &d);
    const HRESULT plain = hr;
    const char* kind = "d3d9";
    bool ex = false;
    if (FAILED(hr)) {
        // Another process in exclusive fullscreen refuses a plain windowed device (D3DERR_NOTAVAILABLE); a D3D9Ex
        // windowed device is not refused then (d3dx_texture_hook_fixture.cpp). Ex has no MANAGED pool: the pass is
        // asked for DEFAULT buffers below; everything else is the same code.
        IDirect3D9Ex* api_ex = nullptr;
        IDirect3DDevice9Ex* device_ex = nullptr;
        hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &api_ex);
        if (SUCCEEDED(hr))
            hr = api_ex->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, nullptr,
                                        &device_ex);
        if (SUCCEEDED(hr)) {
            api->Release();
            api = api_ex;
            d = device_ex;
            kind = "d3d9ex";
            ex = true;
        }
    }
    std::printf("DEVICE kind=%s plain=%08lx created=%08lx\n", kind, plain, hr);
    if (FAILED(hr)) {
        std::printf("RESULT FAIL device hr=%08lx\n", hr);
        return 2;
    }
    D3DADAPTER_IDENTIFIER9 id{};
    api->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
    std::printf("DEVICE adapter=\"%s\"\n", id.Description);
    D3DCAPS9 caps{};
    d->GetDeviceCaps(&caps);
    D3DDISPLAYMODE mode{};
    d->GetDisplayMode(0, &mode);
    SkyStarsPass pass;
    if (ex) pass.set_pool(D3DPOOL_DEFAULT);
    void* const* vtable = *reinterpret_cast<void* const* const*>(d);
    hr = pass.attach(d, vtable, caps, mode.Format);
    char detail[512];
    std::snprintf(detail, sizeof detail, "hr=%08lx reason=%s vs_slots=%u ps_slots=%u references=%u", hr, pass.caps().reason,
                  pass.caps().vs_slots, pass.caps().ps_slots, pass.references());
    check(SUCCEEDED(hr) && pass.ready() && pass.references() == 5, "attach", detail);
    if (!pass.ready()) {
        std::printf("RESULT FAIL attach\n");
        return 1;
    }
    const std::uint32_t seed = 0xa7u | 1u << 8 | 65u << 16 | core::seed_valid;
    bool rebuilt = false;
    LARGE_INTEGER f{}, t0{}, t1{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    const unsigned n = pass.prepare(seed, 1.f, &rebuilt);
    QueryPerformanceCounter(&t1);
    const double build_ms = double(t1.QuadPart - t0.QuadPart) * 1e3 / double(f.QuadPart);
    std::snprintf(detail, sizeof detail, "stars=%u rebuilt=%u build_ms=%.3f", n, unsigned(rebuilt), build_ms);
    check(n == 3120 && rebuilt && pass.prepare(seed, 1.f, &rebuilt) == n && !rebuilt, "prepare_once_per_seed", detail);
    std::vector<core::Vertex> vb(core::vertex_capacity);
    core::build(seed, 1.f, vb.data(), core::stars_max);

    // ---- 1. Energy, offsets, rotations, peak, outside pixels.
    const UINT W = 1024, H = 512;
    Target target;
    check(target.create(d, W, H), "target");
    const double m11 = 1. / std::tan(30. * 3.14159265358979 / 180.), m00 = m11 * H / W;
    const double offsets[4][2] = {{0, 0}, {.25, .5}, {.5, .5}, {.75, .125}};
    double worst_energy = 0, worst_offset_spread = 0, worst_peak = 0, worst_outside_dist = 0;
    unsigned outside_pixels = 0, measured_stars = 0;
    for (unsigned rot = 0; rot < 2; ++rot) {
        float r[9];
        rotation(rot ? 23. : 0., rot ? 11. : 0., r);
        std::vector<std::vector<double>> sums; // per offset, per isolated star: summed luma
        std::vector<std::size_t> chosen;
        for (unsigned o = 0; o < 4; ++o) {
            float c[16];
            core::vertex_constants(r, float(m00), float(m11), 0.f, 0.f, float(offsets[o][0]), float(offsets[o][1]), W, H, c);
            d->SetRenderTarget(0, target.surface);
            d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
            d->BeginScene();
            SkyStarsReport rep{};
            pass.draw(c, &rep);
            d->EndScene();
            if (!rot && !o)
                std::printf("DRAW operation=%08lx restore=%08lx failed=%u calls=%u stars=%u drew=%u\n", rep.operation,
                            rep.restore, unsigned(rep.failed), rep.calls, rep.stars, unsigned(rep.drew));
            std::vector<float> rgb;
            target.read(d, rgb);
            const auto stars = project(vb, n, c, W, H);
            if (o == 0)
                for (std::size_t i = 0; i < stars.size(); ++i)
                    if (isolated(stars, i, W, H)) chosen.push_back(stars[i].index);
            // Peak and outside pixels over the whole image.
            for (UINT y = 0; y < H; ++y)
                for (UINT x = 0; x < W; ++x) {
                    const float* p = &rgb[(std::size_t(y) * W + x) * 3];
                    const float m = std::max(p[0], std::max(p[1], p[2]));
                    worst_peak = std::max(worst_peak, double(m));
                    if (m <= 0.f) continue;
                    double best = 1e9;
                    for (const auto& s : stars) {
                        const double dx = std::fabs(double(x) - s.cx) - s.h, dy = std::fabs(double(y) - s.cy) - s.h;
                        best = std::min(best, std::max(dx, dy));
                    }
                    worst_outside_dist = std::max(worst_outside_dist, best);
                    if (best > .5) ++outside_pixels; // 0.5 px tolerance on the CPU replica of the centre
                }
            std::vector<double> row;
            for (std::size_t k = 0; k < chosen.size(); ++k) {
                const Star* s = nullptr;
                for (const auto& t : stars)
                    if (t.index == chosen[k]) s = &t;
                double sum[3], peak = 0;
                if (!s) {
                    row.push_back(-1);
                    continue;
                }
                window_sum(rgb, W, *s, sum, &peak);
                double e = 0;
                for (unsigned c3 = 0; c3 < 3; ++c3) e = std::max(e, std::fabs(sum[c3] - s->expect[c3]) / s->expect[c3]);
                worst_energy = std::max(worst_energy, e);
                row.push_back(sum[0] + sum[1] + sum[2]);
            }
            sums.push_back(row);
        }
        for (std::size_t k = 0; k < chosen.size(); ++k) {
            double lo = 1e9, hi = -1e9;
            for (unsigned o = 0; o < 4; ++o) {
                lo = std::min(lo, sums[o][k]);
                hi = std::max(hi, sums[o][k]);
            }
            if (lo > 0) worst_offset_spread = std::max(worst_offset_spread, (hi - lo) / lo);
        }
        measured_stars += unsigned(chosen.size());
    }
    std::snprintf(detail, sizeof detail, "stars=%u worst_rel_err=%.5f", measured_stars, worst_energy);
    check(measured_stars >= 100 && worst_energy < .01, "energy_within_1pct", detail);
    std::snprintf(detail, sizeof detail, "worst_spread=%.5f", worst_offset_spread);
    check(worst_offset_spread < .01, "energy_offsets_within_1pct", detail);
    std::snprintf(detail, sizeof detail, "peak=%.5f", worst_peak);
    check(worst_peak <= 1.0 + 1. / 1024., "peak_le_1", detail);
    std::snprintf(detail, sizeof detail, "outside_pixels=%u worst_dist_px=%.3f", outside_pixels, worst_outside_dist);
    check(outside_pixels == 0, "nothing_outside_quads", detail);

    // ---- 2. State restoration: distinctive application state before the draw, compared after it.
    {
        IDirect3DVertexBuffer9* app_vb = nullptr;
        IDirect3DIndexBuffer9* app_ib = nullptr;
        IDirect3DVertexDeclaration9* app_decl = nullptr;
        const D3DVERTEXELEMENT9 el[] = {{0, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
        d->CreateVertexBuffer(256, 0, 0, ex ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED, &app_vb, nullptr);
        d->CreateIndexBuffer(64, 0, D3DFMT_INDEX16, ex ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED, &app_ib, nullptr);
        d->CreateVertexDeclaration(el, &app_decl);
        const D3DRENDERSTATETYPE keys[] = {D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP,
                                           D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_COLORWRITEENABLE, D3DRS_ZENABLE,
                                           D3DRS_ZWRITEENABLE, D3DRS_CULLMODE, D3DRS_ALPHATESTENABLE, D3DRS_SRGBWRITEENABLE,
                                           D3DRS_FOGENABLE, D3DRS_STENCILENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_CLIPPING,
                                           D3DRS_FILLMODE, D3DRS_CLIPPLANEENABLE, D3DRS_WRAP0, D3DRS_WRAP1};
        const DWORD app[] = {FALSE, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_REVSUBTRACT, TRUE, 0x0b, D3DZB_TRUE,
                             TRUE, D3DCULL_CW, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, D3DFILL_WIREFRAME, 1, 3, 5};
        const unsigned nk = sizeof keys / sizeof keys[0];
        unsigned mismatches = 0, runs = 0;
        for (unsigned fault = 0; fault < 3; ++fault) {
            for (unsigned i = 0; i < nk; ++i) d->SetRenderState(keys[i], app[i]);
            const float consts[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
            d->SetVertexShaderConstantF(252, consts, 4);
            d->SetVertexDeclaration(app_decl);
            d->SetStreamSource(0, app_vb, 16, 16);
            d->SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA | 2u);
            d->SetIndices(app_ib);
            d->SetVertexShader(nullptr);
            d->SetPixelShader(nullptr);
            pass.set_faults(fault == 1 ? 1u : fault == 2 ? 2u : 0u);
            float c[16];
            float r[9];
            rotation(0, 0, r);
            core::vertex_constants(r, float(m00), float(m11), 0.f, 0.f, 0.f, 0.f, W, H, c);
            d->BeginScene();
            SkyStarsReport rep{};
            pass.draw(c, &rep);
            d->EndScene();
            const bool expect_draw = fault != 1, expect_restore = fault != 2;
            mismatches += (rep.drew != expect_draw) + (SUCCEEDED(rep.restore) != expect_restore);
            for (unsigned i = 0; i < nk; ++i) {
                DWORD v = 0;
                d->GetRenderState(keys[i], &v);
                // The injected restore fault skips one restoring SetRenderState: exactly one state may differ then.
                if (v != app[i] && !(fault == 2)) ++mismatches;
            }
            float back[16];
            d->GetVertexShaderConstantF(252, back, 4);
            mismatches += std::memcmp(back, consts, sizeof back) != 0;
            IDirect3DVertexDeclaration9* dd = nullptr;
            IDirect3DVertexBuffer9* sb = nullptr;
            IDirect3DIndexBuffer9* ib = nullptr;
            IDirect3DVertexShader9* vs = nullptr;
            IDirect3DPixelShader9* ps = nullptr;
            UINT off = 0, stride = 0, freq = 0;
            d->GetVertexDeclaration(&dd);
            d->GetStreamSource(0, &sb, &off, &stride);
            d->GetStreamSourceFreq(0, &freq);
            d->GetIndices(&ib);
            d->GetVertexShader(&vs);
            d->GetPixelShader(&ps);
            mismatches += (dd != app_decl) + (sb != app_vb) + (off != 16) + (stride != 16) +
                          (freq != (D3DSTREAMSOURCE_INDEXEDDATA | 2u)) + (ib != app_ib) + (vs != nullptr) + (ps != nullptr);
            for (IUnknown* u : {static_cast<IUnknown*>(dd), static_cast<IUnknown*>(sb), static_cast<IUnknown*>(ib),
                                static_cast<IUnknown*>(vs), static_cast<IUnknown*>(ps)})
                if (u) u->Release();
            ++runs;
        }
        d->SetStreamSourceFreq(0, 1);
        d->SetStreamSource(0, nullptr, 0, 0);
        d->SetIndices(nullptr);
        d->SetVertexDeclaration(nullptr);
        app_vb->Release();
        app_ib->Release();
        app_decl->Release();
        std::snprintf(detail, sizeof detail, "runs=%u mismatches=%u (clean, draw fault, restore fault)", runs, mismatches);
        check(mismatches == 0, "state_restored", detail);
        // Reset the defaults the energy pass relied on.
        const DWORD defaults[] = {FALSE, D3DBLEND_ONE, D3DBLEND_ZERO, D3DBLENDOP_ADD, FALSE, 15, D3DZB_TRUE, TRUE, D3DCULL_CCW,
                                  FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, D3DFILL_SOLID, 0, 0, 0};
        for (unsigned i = 0; i < nk; ++i) d->SetRenderState(keys[i], defaults[i]);
    }

    // ---- 3. Temporal proxy: 60 frames of a pan at 0.5 and 10 px/frame with the Halton(2,3) jitter; an exponential
    // history (weight 0.9) reprojected exactly (bilinear) and clipped to the current 3x3 min/max. A model, not the resolve.
    {
        const UINT PW = 2048, PH = 256;
        Target pan;
        check(pan.create(d, PW, PH), "pan_target");
        const double pm11 = m11, pm00 = m11 * PH / PW * 4.; // a narrow horizontal band of the sky
        float r[9];
        rotation(0, 0, r);
        for (double speed : {0.5, 10.0}) {
            std::vector<float> history, current, next;
            std::vector<std::vector<double>> star_peaks, star_current;
            std::vector<unsigned> chosen;
            for (unsigned frame = 0; frame < 60; ++frame) {
                const double jx = halton(frame % 8 + 1, 2) - .5, jy = halton(frame % 8 + 1, 3) - .5;
                const double shift = speed * frame;
                float c[16];
                core::vertex_constants(r, float(pm00), float(pm11), 0.f, 0.f, float(shift + jx), float(jy), PW, PH, c);
                d->SetRenderTarget(0, pan.surface);
                d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
                d->BeginScene();
                SkyStarsReport rep{};
                pass.draw(c, &rep);
                d->EndScene();
                pan.read(d, current);
                if (!frame) {
                    history = current;
                    float c0[16];
                    core::vertex_constants(r, float(pm00), float(pm11), 0.f, 0.f, 0.f, 0.f, PW, PH, c0);
                    const auto stars = project(vb, n, c0, PW, PH);
                    for (std::size_t i = 0; i < stars.size(); ++i)
                        if (stars[i].cx + speed * 60 + stars[i].h + 4 < PW && isolated(stars, i, PW, PH)) // a pan moves all together
                            chosen.push_back(stars[i].index);
                    star_peaks.assign(chosen.size(), {});
                    star_current.assign(chosen.size(), {});
                } else {
                    next.assign(current.size(), 0.f);
                    for (UINT y = 0; y < PH; ++y)
                        for (UINT x = 0; x < PW; ++x) {
                            const double hx = double(x) - speed, hy = double(y);
                            const int ix = int(std::floor(hx)), iy = int(hy);
                            const double fx = hx - ix;
                            for (unsigned ch = 0; ch < 3; ++ch) {
                                double lo = 1e9, hi = -1e9;
                                for (int dy = -1; dy <= 1; ++dy)
                                    for (int dx = -1; dx <= 1; ++dx) {
                                        const int sx = std::clamp(int(x) + dx, 0, int(PW) - 1),
                                                  sy = std::clamp(int(y) + dy, 0, int(PH) - 1);
                                        const double v = current[(std::size_t(sy) * PW + sx) * 3 + ch];
                                        lo = std::min(lo, v);
                                        hi = std::max(hi, v);
                                    }
                                double h = 0;
                                if (ix >= 0 && ix + 1 < int(PW))
                                    h = history[(std::size_t(iy) * PW + ix) * 3 + ch] * (1 - fx) +
                                        history[(std::size_t(iy) * PW + ix + 1) * 3 + ch] * fx;
                                else
                                    h = current[(std::size_t(y) * PW + x) * 3 + ch];
                                h = std::clamp(h, lo, hi);
                                next[(std::size_t(y) * PW + x) * 3 + ch] =
                                    float(.9 * h + .1 * current[(std::size_t(y) * PW + x) * 3 + ch]);
                            }
                        }
                    history.swap(next);
                }
                if (frame >= 20) {
                    float cf[16];
                    core::vertex_constants(r, float(pm00), float(pm11), 0.f, 0.f, float(shift), 0.f, PW, PH, cf);
                    const auto stars = project(vb, n, cf, PW, PH);
                    for (std::size_t k = 0; k < chosen.size(); ++k)
                        for (const auto& s : stars)
                            if (s.index == chosen[k]) {
                                double sum[3], peak = 0, peak_c = 0;
                                window_sum(history, PW, s, sum, &peak);
                                window_sum(current, PW, s, sum, &peak_c);
                                star_peaks[k].push_back(peak);
                                star_current[k].push_back(peak_c);
                            }
                }
            }
            auto cv = [](const std::vector<double>& v) {
                if (v.size() < 2) return 0.;
                double m = 0, q = 0;
                for (double x : v) m += x;
                m /= double(v.size());
                for (double x : v) q += (x - m) * (x - m);
                return m > 0 ? std::sqrt(q / double(v.size())) / m : 0.;
            };
            std::vector<double> cvs, cvc;
            for (std::size_t k = 0; k < chosen.size(); ++k)
                if (star_peaks[k].size() == 40) {
                    cvs.push_back(cv(star_peaks[k]));
                    cvc.push_back(cv(star_current[k]));
                }
            std::sort(cvs.begin(), cvs.end());
            std::sort(cvc.begin(), cvc.end());
            const double med = cvs.empty() ? -1 : cvs[cvs.size() / 2], mx = cvs.empty() ? -1 : cvs.back();
            const double medc = cvc.empty() ? -1 : cvc[cvc.size() / 2], mxc = cvc.empty() ? -1 : cvc.back();
            std::printf("TEMPORAL speed=%.1f stars=%zu resolved_peak_cv_median=%.4f resolved_peak_cv_max=%.4f "
                        "current_peak_cv_median=%.4f current_peak_cv_max=%.4f (model, frames 20-59)\n",
                        speed, cvs.size(), med, mx, medc, mxc);
        }
        pan.release();
    }

    // ---- 4. Cost at 5120x1440: GPU time per batch (event-query flushed) and the CPU time of draw().
    {
        Target big;
        check(big.create(d, 5120, 1440), "target_5120x1440");
        IDirect3DQuery9* q = nullptr;
        d->CreateQuery(D3DQUERYTYPE_EVENT, &q);
        auto flush = [&] {
            q->Issue(D3DISSUE_END);
            while (q->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {
            }
        };
        const double bm11 = m11, bm00 = m11 * 1440 / 5120;
        float r[9];
        rotation(0, 0, r);
        float c[16];
        core::vertex_constants(r, float(bm00), float(bm11), 0.f, 0.f, 0.f, 0.f, 5120, 1440, c);
        for (std::uint32_t s : {seed, 0x11u | 0u << 8 | 143u << 16 | core::seed_valid}) {
            const unsigned stars = pass.prepare(s, s == seed ? 1.f : 4.f);
            d->SetRenderTarget(0, big.surface);
            d->BeginScene();
            d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
            flush();
            const unsigned reps = 200;
            double cpu = 0;
            QueryPerformanceCounter(&t0);
            for (unsigned i = 0; i < reps; ++i) {
                LARGE_INTEGER a{}, b{};
                QueryPerformanceCounter(&a);
                SkyStarsReport rep{};
                pass.draw(c, &rep);
                QueryPerformanceCounter(&b);
                cpu += double(b.QuadPart - a.QuadPart);
            }
            flush();
            QueryPerformanceCounter(&t1);
            d->EndScene();
            const double total_ms = double(t1.QuadPart - t0.QuadPart) * 1e3 / double(f.QuadPart);
            std::printf("COST target=5120x1440 stars=%u reps=%u batch_ms_upper_bound=%.4f draw_cpu_us=%.2f\n", stars, reps,
                        total_ms / reps, cpu * 1e6 / double(f.QuadPart) / reps);
        }
        QueryPerformanceCounter(&t0);
        bool rb = false;
        pass.prepare(0x22u | 3u << 8 | 200u << 16 | core::seed_valid, 1.f, &rb);
        QueryPerformanceCounter(&t1);
        std::printf("COST rebuild stars=%u ms=%.3f\n", pass.stars(), double(t1.QuadPart - t0.QuadPart) * 1e3 / double(f.QuadPart));
        q->Release();
        big.release();
    }
    target.release();
    pass.detach();
    check(pass.references() == 0, "detach_releases");
    const ULONG left = d->Release();
    std::printf("DEVICE_RELEASE refs=%lu\n", left);
    check(left == 0, "device_released_clean");
    api->Release();
    DestroyWindow(window);
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
