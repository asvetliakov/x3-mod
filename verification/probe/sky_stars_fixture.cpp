// Native D3D9 fixture of the sky-star batch (docs/architecture/sector-background-modern.md, "Implementation A"): the
// production renderer::SkyStarsPass (src/renderer/sky_stars_pass.cpp, built with X3M_SKY_STARS_FIXTURE) on a real
// device, no game. Renders the seed's batch into an A16B16G16R16F target at four sub-pixel offsets (the TAA jitter
// input of sky_stars::core::vertex_constants) and two view rotations and checks, per isolated star, the summed RGB
// against the buffer's energy (within 1 %) and across offsets (within 1 %), every pixel <= peak_cap (2.0), and no written
// pixel outside a star's quad; the twinkle (c251 at amplitude 1): a twinkling star's sum follows
// 1 + sin(2 pi frac(rate t + phase) - pi), a steady star's does not move; that every state, binding and constant
// (c251-c255) the pass touches is put back (with a draw fault and a
// restore fault injected); a temporal proxy (60 frames, 0.5 and 10 px/frame pans: an exponential history of weight 0.9
// with exact reprojection and a 3x3 min/max clip, a model of the resolve, not the resolve); and the GPU/CPU cost at
// 5120x1440 and the rebuild of a full 16384-star buffer. The per-sector seed: prepare with a sector key rebuilds once,
// the same key is the cached build, the drawn buffer is core::build's with that key, key 0 is the mark's field again;
// and the per-frame cost of the sector key reader's cached check (src/proxy/sector_key_core.h through the production
// engine_memory::read, on a fabricated sector in this process). Decision C2 (the deferred batch, section 5): the
// dimming law over flat fields at two pre-decode luma levels (energy 1 - k saturate(L / L0) of the black-sky energy
// within 1 %), the k = 0 deferred batch against the in-place one, the deferred batch over additive quads against the
// in-place order (within 1 FP16 ulp), no star pixel inside a Z-written disc at depth 0.5, the StretchRect's filter on
// one lit pixel per 8x8 block, the state restore with the stage-0 texture and sampler states, and the cost of the copy
// and the deferred batch at 5120x1440. Prints PASS/FAIL lines and one RESULT line. Build:
// verification/results/sky-stars/run_fixture.py.
#include "../../src/renderer/sky_stars_pass.h"
#include "../../src/proxy/sector_key_core.h"
#include "../../src/proxy/engine_memory.h"
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
    check(SUCCEEDED(hr) && pass.ready() && pass.references() == 7, "attach", detail);
    if (!pass.ready()) {
        std::printf("RESULT FAIL attach\n");
        return 1;
    }
    const std::uint32_t seed = 0xa7u | 1u << 8 | 65u << 16 | core::seed_valid;
    const core::Look look{}; // the defaults: density 2, brightness 1.5, size 1, min_size 1.5, clustering 1
    const float steady[4] = {0.f, 0.f, 0.f, 0.f}; // c251 with amplitude 0
    bool rebuilt = false;
    LARGE_INTEGER f{}, t0{}, t1{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    const unsigned n = pass.prepare(seed, look, 0, &rebuilt);
    QueryPerformanceCounter(&t1);
    const double build_ms = double(t1.QuadPart - t0.QuadPart) * 1e3 / double(f.QuadPart);
    std::snprintf(detail, sizeof detail, "stars=%u rebuilt=%u build_ms=%.3f support_max=%.4f peak_max=%.4f twinkling=%u", n,
                  unsigned(rebuilt), build_ms, pass.stats().support_max, pass.stats().peak_max, pass.stats().twinkling);
    check(n == 6240 && rebuilt && pass.prepare(seed, look, 0, &rebuilt) == n && !rebuilt, "prepare_once_per_seed", detail);
    {
        core::Look other = look;
        other.brightness = 3.f;
        bool again = false;
        pass.prepare(seed, other, 0, &again);
        bool back = false;
        pass.prepare(seed, look, 0, &back);
        core::Look uniform = look;
        uniform.clustering = 0.f;
        bool flat = false, clustered = false;
        pass.prepare(seed, uniform, 0, &flat);
        pass.prepare(seed, look, 0, &clustered);
        check(again && back && flat && clustered, "prepare_rebuilds_on_look_change");
    }
    std::vector<core::Vertex> vb(core::vertex_capacity);
    core::build(seed, look, vb.data(), core::stars_max);
    const std::uint64_t sector_key = x3m::sector_key::core::key_of(1, 3);
    {
        // The cached-build check includes the sector key: a key rebuilds once, the same key does not, key 0 again
        // rebuilds the mark's field; the stats are core::build's with that key.
        bool keyed = false, same = true, back = false;
        pass.prepare(seed, look, sector_key, &keyed);
        core::BuildStats want;
        std::vector<core::Vertex> other(core::vertex_capacity);
        core::build(seed, look, other.data(), core::stars_max, &want, sector_key);
        const bool stats = pass.sector_key() == sector_key && pass.stats().twinkling == want.twinkling &&
                           pass.stats().candidates == want.candidates && pass.stats().peak_max == want.peak_max;
        pass.prepare(seed, look, sector_key, &same);
        std::snprintf(detail, sizeof detail, "key=%llu twinkling=%u/%u candidates=%u/%u", (unsigned long long)sector_key,
                      pass.stats().twinkling, want.twinkling, pass.stats().candidates, want.candidates);
        check(keyed && !same && stats && std::memcmp(vb.data(), other.data(), n * 4 * sizeof(core::Vertex)),
              "prepare_rebuilds_on_sector_key", detail);
        pass.prepare(seed, look, 0, &back);
        check(back && pass.sector_key() == 0, "prepare_key0_is_the_mark_field");
    }

    // ---- 1. Energy, offsets, rotations, peak, outside pixels.
    const UINT W = 1024, H = 512;
    Target target;
    check(target.create(d, W, H), "target");
    const double m11 = 1. / std::tan(30. * 3.14159265358979 / 180.), m00 = m11 * H / W;
    {
        // The actual writes of a keyed rebuild: the drawn image's isolated stars carry core::build(seed, look, key)'s
        // energy (within 1 %); then the mark's field again for the sections below.
        std::vector<core::Vertex> keyed(core::vertex_capacity);
        core::build(seed, look, keyed.data(), core::stars_max, nullptr, sector_key);
        pass.prepare(seed, look, sector_key);
        float r[9], c[16];
        rotation(0., 0., r);
        core::vertex_constants(r, float(m00), float(m11), 0.f, 0.f, 0.f, 0.f, W, H, c);
        d->SetRenderTarget(0, target.surface);
        d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
        d->BeginScene();
        SkyStarsReport rep{};
        pass.draw(c, steady, &rep);
        d->EndScene();
        std::vector<float> rgb;
        target.read(d, rgb);
        const auto stars = project(keyed, n, c, W, H);
        unsigned isolated_stars = 0, within = 0;
        for (std::size_t i = 0; i < stars.size(); ++i) {
            if (!isolated(stars, i, W, H)) continue;
            ++isolated_stars;
            double sum[3], peak = 0, e = 0;
            window_sum(rgb, W, stars[i], sum, &peak);
            for (unsigned c3 = 0; c3 < 3; ++c3) e = std::max(e, std::fabs(sum[c3] - stars[i].expect[c3]) / stars[i].expect[c3]);
            within += e < .01;
        }
        std::snprintf(detail, sizeof detail, "isolated=%u within_1pct=%u drew=%u", isolated_stars, within, unsigned(rep.drew));
        check(rep.drew && isolated_stars > 50 && within == isolated_stars, "keyed_field_drawn", detail);
        pass.prepare(seed, look, 0);
    }
    const double offsets[4][2] = {{0, 0}, {.25, .5}, {.5, .5}, {.75, .125}};
    double worst_energy = 0, worst_offset_spread = 0, worst_peak = 0, worst_star_peak = 0, worst_outside_dist = 0;
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
            pass.draw(c, steady, &rep);
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
                worst_star_peak = std::max(worst_star_peak, peak);
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
    // The cap is per star: an isolated star's largest pixel; quads that overlap add (the image maximum, reported).
    std::snprintf(detail, sizeof detail, "star_peak=%.5f image_peak=%.5f (overlaps add)", worst_star_peak, worst_peak);
    check(worst_star_peak <= core::peak_cap * (1. + 1. / 1024.), "star_peak_le_cap", detail);
    std::snprintf(detail, sizeof detail, "outside_pixels=%u worst_dist_px=%.3f", outside_pixels, worst_outside_dist);
    check(outside_pixels == 0, "nothing_outside_quads", detail);

    // ---- 1b. Twinkle: c251 = (t, 1) at three times; per isolated star the sum against the energy times the
    // modulation the vertex program applies (steady stars: times 1).
    {
        float r[9];
        rotation(0, 0, r);
        float c[16];
        core::vertex_constants(r, float(m00), float(m11), 0.f, 0.f, 0.f, 0.f, W, H, c);
        const auto stars = project(vb, n, c, W, H);
        std::vector<std::size_t> chosen;
        for (std::size_t i = 0; i < stars.size(); ++i)
            if (isolated(stars, i, W, H)) chosen.push_back(i);
        double worst_twinkle = 0, worst_steady = 0, swing = 0;
        unsigned twinkling = 0, steady_n = 0;
        for (double t : {0.37, 17.5, 101.03}) {
            const float tw[4] = {float(t), 1.f, 0.f, 0.f};
            d->SetRenderTarget(0, target.surface);
            d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
            d->BeginScene();
            SkyStarsReport rep{};
            pass.draw(c, tw, &rep);
            d->EndScene();
            std::vector<float> rgb;
            target.read(d, rgb);
            for (std::size_t k : chosen) {
                const auto& s = stars[k];
                const std::uint32_t lanes = vb[s.index * 4].twinkle;
                double factor = 1.;
                if (lanes) {
                    const double rate = double((lanes >> 16) & 255) / 128., phase = double((lanes >> 8) & 255) / 255.;
                    const double cycles = rate * t + phase - std::floor(rate * t + phase);
                    factor = 1. + std::sin(6.28318531 * cycles - 3.14159265);
                }
                double sum[3], peak = 0;
                window_sum(rgb, W, s, sum, &peak);
                const double expect = s.expect[0] + s.expect[1] + s.expect[2];
                const double got = sum[0] + sum[1] + sum[2];
                if (lanes) {
                    ++twinkling;
                    worst_twinkle = std::max(worst_twinkle, std::fabs(got - expect * factor) / expect);
                    swing = std::max(swing, std::fabs(factor - 1.));
                } else {
                    ++steady_n;
                    worst_steady = std::max(worst_steady, std::fabs(got - expect) / expect);
                }
            }
        }
        std::snprintf(detail, sizeof detail, "twinkling=%u steady=%u worst_twinkle_err=%.5f worst_steady_err=%.5f swing_max=%.3f",
                      twinkling, steady_n, worst_twinkle, worst_steady, swing);
        check(twinkling >= 30 && steady_n >= 90 && worst_twinkle < .02 && worst_steady < .01 && swing > .5,
              "twinkle_modulation", detail);
    }

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
            const float consts[20] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
            d->SetVertexShaderConstantF(251, consts, 5);
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
            const float tw[4] = {12.5f, .3f, 0.f, 0.f};
            d->BeginScene();
            SkyStarsReport rep{};
            pass.draw(c, tw, &rep);
            d->EndScene();
            const bool expect_draw = fault != 1, expect_restore = fault != 2;
            mismatches += (rep.drew != expect_draw) + (SUCCEEDED(rep.restore) != expect_restore);
            for (unsigned i = 0; i < nk; ++i) {
                DWORD v = 0;
                d->GetRenderState(keys[i], &v);
                // The injected restore fault skips one restoring SetRenderState: exactly one state may differ then.
                if (v != app[i] && !(fault == 2)) ++mismatches;
            }
            float back[20];
            d->GetVertexShaderConstantF(251, back, 5);
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
                pass.draw(c, steady, &rep);
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
        const float tw[4] = {33.3f, core::twinkle_default, 0.f, 0.f};
        for (std::uint32_t s : {seed, 0x11u | 0u << 8 | 143u << 16 | core::seed_valid}) {
            core::Look cost_look = look;
            cost_look.density = s == seed ? core::density_default : 4.f;
            const unsigned stars = pass.prepare(s, cost_look, 0);
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
                pass.draw(c, tw, &rep);
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
        // The rebuild of a full buffer (K 200 x 48 x 2 = 19200, capped at 16384): the Lock, the generator, the Unlock;
        // at clustering 0 (the first law's cost), 1 (the default) and 2 (the most rejections).
        bool full = true;
        for (float clustering : {0.f, core::clustering_default, core::clustering_max}) {
            double best_us = 1e30;
            for (unsigned rep = 0; rep < 5; ++rep) {
                core::Look rebuild_look = look;
                rebuild_look.clustering = clustering;
                rebuild_look.brightness = 2.f + .25f * float(rep); // a new look each time: a real rebuild
                QueryPerformanceCounter(&t0);
                bool rb = false;
                pass.prepare(0x22u | 3u << 8 | 200u << 16 | core::seed_valid, rebuild_look, 0, &rb);
                QueryPerformanceCounter(&t1);
                best_us = std::min(best_us, double(t1.QuadPart - t0.QuadPart) * 1e6 / double(f.QuadPart));
            }
            std::printf("COST rebuild stars=%u vb_bytes=%u clustering=%.1f reject_rate=%.3f cluster_max_min=%.2f"
                        " build_us_best_of_5=%.1f\n",
                        pass.stars(), unsigned(core::vertex_capacity * core::vertex_stride), double(clustering),
                        pass.stats().reject_rate, pass.stats().cluster_max_min, best_us);
            full = full && pass.stars() == core::stars_max;
        }
        check(full, "rebuild_full_buffer");
        q->Release();
        big.release();
    }
    {
        // ---- The sector key reader's per-frame cost: one header span of a fabricated sector through the production
        // engine_memory::read, cached on (S, id, kc). "frame" advances the validation epoch first (next_frame, the
        // Present's: one VirtualQuery on the first touch); "warm" is a second read in the same frame (the region already
        // validated, as after the cockpit sample's reads of the same sector). The walk: the cache cleared first.
        namespace sk = x3m::sector_key::core;
        struct Engine {
            bool read(std::uint32_t at, void* out, unsigned n) { return x3m::engine_memory::read(at, out, n); }
        } engine;
        static std::uint32_t vm_global = 0;
        std::vector<unsigned char> sector(0x180), vm(0x1300), cells(5 * 40);
        std::uint32_t map[2], buckets[16] = {}, node[3], object[4], record[2] = {2001, 0};
        const auto at = [](const void* p) { return std::uint32_t(reinterpret_cast<std::uintptr_t>(p)); };
        const std::uint32_t id = 3221, kc = std::uint32_t(-1234), key = ~kc;
        std::memcpy(&sector[8], &id, 4);
        sector[0x48] = 1;
        std::memcpy(&sector[0x94], &kc, 4);
        sector[0x9c] = 0xfe;
        sector[0x9d] = 0xca;
        map[0] = at(buckets);
        map[1] = 16;
        node[0] = 0;
        node[1] = key;
        node[2] = at(object);
        buckets[key & 15] = at(node);
        object[0] = kc;
        object[1] = 0;
        object[2] = at(record);
        object[3] = at(cells.data());
        const std::uint32_t map_at = at(map);
        std::memcpy(&vm[0x12d0], &map_at, 4);
        vm_global = at(vm.data());
        const std::uint32_t values[3][2] = {{0, 1}, {1, 3}, {34, id}};
        for (const auto& v : values) {
            cells[5 * v[0]] = 1;
            std::memcpy(&cells[5 * v[0] + 1], &v[1], 4);
        }
        sk::Layout layout{at(&vm_global), 34};
        sk::Cache cache;
        x3m::engine_memory::next_frame();
        const auto first = sk::read(engine, layout, at(sector.data()), cache);
        const unsigned reps = 20000;
        double walk = 0, frame = 0, warm = 0;
        bool ok = first.known && first.key == sk::key_of(1, 3);
        for (unsigned i = 0; i < reps; ++i) {
            LARGE_INTEGER a{}, b{}, c{}, e{};
            x3m::engine_memory::next_frame();
            cache.clear();
            QueryPerformanceCounter(&a);
            ok = ok && sk::read(engine, layout, at(sector.data()), cache).known;
            QueryPerformanceCounter(&b);
            x3m::engine_memory::next_frame();
            ok = ok && sk::read(engine, layout, at(sector.data()), cache).cached;
            QueryPerformanceCounter(&c);
            ok = ok && sk::read(engine, layout, at(sector.data()), cache).cached;
            QueryPerformanceCounter(&e);
            walk += double(b.QuadPart - a.QuadPart);
            frame += double(c.QuadPart - b.QuadPart);
            warm += double(e.QuadPart - c.QuadPart);
        }
        const double ns = 1e9 / double(f.QuadPart) / reps;
        std::printf("COST sector_key reps=%u walk_ns=%.0f cached_frame_ns=%.0f cached_warm_ns=%.0f\n", reps, walk * ns,
                    frame * ns, warm * ns);
        check(ok, "sector_key_cached_check");
    }
    // ---- 5. Decision C2: the deferred batch at the phase end (draw_deferred), its copy and its dimming law.
    {
        std::snprintf(detail, sizeof detail, "dim=%u reason=%s copy_filter=%u dim_linear=%u dim_vs_slots=%u dim_ps_slots=%u",
                      unsigned(pass.caps().dim), pass.caps().dim_reason, pass.caps().copy_filter,
                      unsigned(pass.caps().dim_linear), pass.caps().dim_vs_slots, pass.caps().dim_ps_slots);
        check(pass.caps().dim && pass.caps().copy_filter == D3DTEXF_LINEAR, "dim_side", detail);
        pass.prepare(seed, look, 0);
        IDirect3DSurface9* depth = nullptr;
        check(SUCCEEDED(d->CreateDepthStencilSurface(W, H, D3DFMT_D24X8, D3DMULTISAMPLE_NONE, 0, TRUE, &depth, nullptr)),
              "dim_depth");
        IDirect3DSurface9* old_depth = nullptr;
        d->GetDepthStencilSurface(&old_depth);
        d->SetRenderTarget(0, target.surface);
        d->SetDepthStencilSurface(depth);
        float r[9], c[16];
        rotation(0, 0, r);
        core::vertex_constants(r, float(m00), float(m11), 0.f, 0.f, 0.f, 0.f, W, H, c);
        const float steady_tw[4] = {0.f, 0.f, 0.f, 0.f};
        const auto stars = project(vb, n, c, W, H);
        std::vector<std::size_t> chosen;
        for (std::size_t i = 0; i < stars.size(); ++i)
            if (isolated(stars, i, W, H)) chosen.push_back(i);
        check(SUCCEEDED(pass.prepare_dim(W, H)) && pass.dim_width() == W / 8 && pass.dim_height() == H / 8 &&
                  pass.references() == 9,
              "dim_texture_created");
        // One deferred frame: the target cleared to grey n/255 and depth 1, the copy; then the target cleared to black
        // (the copy keeps the grey, so the stars' energy is measured without a background), the batch with c250.
        auto deferred = [&](unsigned grey, float k, std::vector<float>* rgb, std::vector<std::uint16_t>* raw) {
            d->SetRenderTarget(0, target.surface);
            d->SetDepthStencilSurface(depth);
            d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(255, grey, grey, grey), 1.f, 0);
            d->BeginScene();
            const HRESULT copied = pass.copy_dim(target.surface);
            d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
            float dim[4];
            core::dim_constants(k, core::nebula_dim_level_default, pass.dim_width(), pass.dim_height(), dim);
            SkyStarsReport rep{};
            pass.draw_deferred(c, steady_tw, dim, &rep);
            d->EndScene();
            target.read(d, *rgb);
            if (raw) {
                D3DLOCKED_RECT lr{};
                raw->assign(std::size_t(W) * H * 4, 0);
                if (SUCCEEDED(target.readback->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
                    for (UINT y = 0; y < H; ++y)
                        std::memcpy(raw->data() + std::size_t(y) * W * 4, static_cast<const char*>(lr.pBits) + y * lr.Pitch, W * 8);
                    target.readback->UnlockRect();
                }
            }
            return SUCCEEDED(copied) && rep.drew && SUCCEEDED(rep.restore);
        };
        // Invariant 8, the law: per isolated star, (window sum - background) / black-sky energy against 1 - k sat(L/L0).
        for (unsigned grey : {64u, 128u}) {
            std::vector<float> rgb;
            const bool ok = deferred(grey, .5f, &rgb, nullptr);
            const double v = double(grey) / 255.; // the grey copied (the 8-bit Clear colour, stored in FP16)
            const double L = v * .299 + v * .587 + v * .114;
            const double want = core::dim_factor(.5f, core::nebula_dim_level_default, float(L));
            double worst = 0;
            unsigned measured = 0;
            for (std::size_t i : chosen) {
                double sum[3], peak = 0;
                window_sum(rgb, W, stars[i], sum, &peak);
                for (unsigned ch = 0; ch < 3; ++ch) {
                    const double e = sum[ch] / stars[i].expect[ch];
                    worst = std::max(worst, std::fabs(e - want) / want);
                }
                ++measured;
            }
            std::snprintf(detail, sizeof detail, "grey=%u L=%.4f factor_expected=%.4f stars=%u worst_rel_err=%.5f drawn=%u",
                          grey, L, want, measured, worst, unsigned(ok));
            std::printf("DIM %s\n", detail);
            check(ok && measured >= 50 && worst < .01, grey == 64 ? "dim_law_unsaturated" : "dim_law_full", detail);
        }
        // k = 0: the deferred batch over black against the in-place batch over black (the production k = 0 path is the
        // in-place draw() itself; this compares the twins).
        {
            std::vector<float> rgb_d, rgb_i;
            std::vector<std::uint16_t> raw_d, raw_i;
            deferred(0, 0.f, &rgb_d, &raw_d);
            d->SetRenderTarget(0, target.surface);
            d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.f, 0);
            d->BeginScene();
            SkyStarsReport rep{};
            pass.draw(c, steady_tw, &rep);
            d->EndScene();
            target.read(d, rgb_i);
            D3DLOCKED_RECT lr{};
            raw_i.assign(std::size_t(W) * H * 4, 0);
            if (SUCCEEDED(target.readback->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
                for (UINT y = 0; y < H; ++y)
                    std::memcpy(raw_i.data() + std::size_t(y) * W * 4, static_cast<const char*>(lr.pBits) + y * lr.Pitch, W * 8);
                target.readback->UnlockRect();
            }
            std::size_t differing = 0, lit = 0;
            int worst_ulp = 0;
            for (std::size_t i = 0; i < raw_i.size(); ++i) {
                if ((i & 3) == 3) continue;
                lit += raw_i[i] != 0;
                if (raw_i[i] != raw_d[i]) {
                    ++differing;
                    worst_ulp = std::max(worst_ulp, std::abs(int(raw_i[i]) - int(raw_d[i])));
                }
            }
            std::snprintf(detail, sizeof detail, "lit_channels=%zu differing_channels=%zu worst_ulp=%d", lit, differing, worst_ulp);
            std::printf("K0 %s\n", detail);
            check(lit > 1000 && differing == 0, "dim_k0_deferred_equals_in_place", detail);
        }
        // Invariant 6, commutativity: additive fixed-function quads then the deferred batch (k = 0), against the batch
        // then the quads (the in-place order); FP16 halves compared.
        {
            struct Q {
                float x, y, z, rhw;
                DWORD colour;
            };
            auto quads = [&] {
                d->SetVertexShader(nullptr);
                d->SetPixelShader(nullptr);
                d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
                d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
                d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
                d->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
                d->SetRenderState(D3DRS_LIGHTING, FALSE);
                d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
                d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
                for (unsigned k = 0; k < 6; ++k) {
                    const float x0 = float(40 + 150 * k), y0 = float(30 + 60 * k), x1 = x0 + 260.f, y1 = y0 + 200.f;
                    const DWORD col = D3DCOLOR_ARGB(255, 20 + 30 * k, 90 - 10 * k, 15 + 25 * k);
                    const Q v[4] = {{x0, y0, .5f, 1, col}, {x1, y0, .5f, 1, col}, {x0, y1, .5f, 1, col}, {x1, y1, .5f, 1, col}};
                    d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Q));
                }
                d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            };
            float dim0[4];
            core::dim_constants(0.f, core::nebula_dim_level_default, pass.dim_width(), pass.dim_height(), dim0);
            std::vector<std::uint16_t> a, b;
            for (unsigned order = 0; order < 2; ++order) {
                d->SetRenderTarget(0, target.surface);
                d->SetDepthStencilSurface(depth);
                d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.f, 0);
                d->BeginScene();
                SkyStarsReport rep{};
                if (order == 0) {
                    quads();
                    pass.draw_deferred(c, steady_tw, dim0, &rep);
                } else {
                    pass.draw_deferred(c, steady_tw, dim0, &rep);
                    quads();
                }
                d->EndScene();
                std::vector<float> rgb;
                target.read(d, rgb);
                auto& raw = order ? b : a;
                D3DLOCKED_RECT lr{};
                raw.assign(std::size_t(W) * H * 4, 0);
                if (SUCCEEDED(target.readback->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
                    for (UINT y = 0; y < H; ++y)
                        std::memcpy(raw.data() + std::size_t(y) * W * 4, static_cast<const char*>(lr.pBits) + y * lr.Pitch, W * 8);
                    target.readback->UnlockRect();
                }
            }
            int worst = 0;
            std::size_t differing = 0;
            for (std::size_t i = 0; i < a.size(); ++i)
                if ((i & 3) != 3 && a[i] != b[i]) {
                    ++differing;
                    worst = std::max(worst, std::abs(int(a[i]) - int(b[i])));
                }
            std::snprintf(detail, sizeof detail, "differing_channels=%zu worst_ulp=%d", differing, worst);
            std::printf("COMMUTE %s\n", detail);
            // The note asks for 1 ulp; each ONE/ONE blend rounds to FP16, so swapping the order of a star and two or
            // more overlapping quads can differ by one ulp per rounding: 2 is the bound checked, the figure reported.
            check(worst <= 2, "deferred_commutes_within_2_ulp", detail);
        }
        // Invariant 6, occlusion: a disc written to depth 0.5 (colour writes off), then the deferred batch: no star
        // pixel inside the disc, stars outside it.
        {
            struct Q {
                float x, y, z, rhw;
            };
            d->SetRenderTarget(0, target.surface);
            d->SetDepthStencilSurface(depth);
            d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.f, 0);
            d->BeginScene();
            d->SetVertexShader(nullptr);
            d->SetPixelShader(nullptr);
            d->SetFVF(D3DFVF_XYZRHW);
            d->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
            d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
            d->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
            d->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
            const float cx = W * .5f, cy = H * .5f, radius = 180.f;
            Q fan[66];
            fan[0] = {cx, cy, .5f, 1};
            for (unsigned i = 0; i <= 64; ++i) {
                const double a = 2 * 3.14159265358979 * i / 64;
                fan[i + 1] = {cx + radius * float(std::cos(a)), cy + radius * float(std::sin(a)), .5f, 1};
            }
            d->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 64, fan, sizeof(Q));
            d->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
            d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
            float dim0[4];
            core::dim_constants(0.f, core::nebula_dim_level_default, pass.dim_width(), pass.dim_height(), dim0);
            SkyStarsReport rep{};
            pass.draw_deferred(c, steady_tw, dim0, &rep);
            d->EndScene();
            std::vector<float> rgb;
            target.read(d, rgb);
            unsigned inside = 0, outside = 0;
            for (UINT y = 0; y < H; ++y)
                for (UINT x = 0; x < W; ++x) {
                    const float* px = &rgb[(std::size_t(y) * W + x) * 3];
                    if (px[0] + px[1] + px[2] <= 0.f) continue;
                    const double dx = double(x) - cx, dy = double(y) - cy;
                    if (dx * dx + dy * dy < (radius - 1.5) * (radius - 1.5)) ++inside;
                    else ++outside;
                }
            std::snprintf(detail, sizeof detail, "star_pixels_inside_disc=%u outside=%u", inside, outside);
            std::printf("OCCLUDE %s\n", detail);
            check(inside == 0 && outside > 1000, "deferred_occluded_by_z_disc", detail);
            d->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
            d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        }
        // The StretchRect's filter on one lit pixel (1.0) per 8x8 block (mean 1/64): a box average gives 1/64 in every
        // dim texel, a bilinear sample of a 2x2 footprint 0 or a fraction. Reported, not asserted.
        {
            IDirect3DSurface9* pattern = nullptr;
            HRESULT made = d->CreateOffscreenPlainSurface(W, H, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &pattern, nullptr);
            D3DLOCKED_RECT lr{};
            if (SUCCEEDED(made) && SUCCEEDED(pattern->LockRect(&lr, nullptr, 0))) {
                for (UINT y = 0; y < H; ++y) {
                    auto* row = reinterpret_cast<std::uint16_t*>(static_cast<char*>(lr.pBits) + y * lr.Pitch);
                    for (UINT x = 0; x < W; ++x)
                        for (unsigned ch = 0; ch < 4; ++ch)
                            row[x * 4 + ch] = (x % 8 == 3 && y % 8 == 3) ? 0x3c00 : 0; // 1.0 in FP16
                }
                pattern->UnlockRect();
            }
            const HRESULT up = SUCCEEDED(made) ? d->UpdateSurface(pattern, nullptr, target.surface, nullptr) : made;
            const HRESULT copy = pass.copy_dim(target.surface);
            IDirect3DSurface9* dim_read = nullptr;
            double lo = 1e9, hi = -1e9, mean = 0;
            unsigned texels = 0;
            if (SUCCEEDED(d->CreateOffscreenPlainSurface(pass.dim_width(), pass.dim_height(), D3DFMT_A16B16G16R16F,
                                                         D3DPOOL_SYSTEMMEM, &dim_read, nullptr)) &&
                SUCCEEDED(d->GetRenderTargetData(pass.dim_surface(), dim_read)) &&
                SUCCEEDED(dim_read->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
                for (UINT y = 0; y < pass.dim_height(); ++y) {
                    const auto* row = reinterpret_cast<const std::uint16_t*>(static_cast<const char*>(lr.pBits) + y * lr.Pitch);
                    for (UINT x = 0; x < pass.dim_width(); ++x) {
                        const double v = half_to_float(row[x * 4]);
                        lo = std::min(lo, v);
                        hi = std::max(hi, v);
                        mean += v;
                        ++texels;
                    }
                }
                dim_read->UnlockRect();
            }
            if (dim_read) dim_read->Release();
            if (pattern) pattern->Release();
            std::printf("STRETCH update=%08lx copy=%08lx texels=%u min=%.5f max=%.5f mean=%.5f box_mean=%.5f\n", up, copy,
                        texels, lo, hi, texels ? mean / texels : 0., 1. / 64.);
        }
        // Invariant 4 for the deferred batch: the application's stage-0 texture and sampler states, Z states and
        // c250-c255 back after a clean batch, a draw fault and a restore fault.
        {
            IDirect3DTexture9* app_texture = nullptr;
            d->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                             &app_texture, nullptr);
            const D3DSAMPLERSTATETYPE sk[] = {D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER, D3DSAMP_MINFILTER,
                                              D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE};
            const DWORD sv[] = {D3DTADDRESS_MIRROR, D3DTADDRESS_WRAP, D3DTEXF_POINT, D3DTEXF_ANISOTROPIC, D3DTEXF_LINEAR, TRUE};
            const float consts[24] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
            unsigned mismatches = 0;
            for (unsigned fault = 0; fault < 3; ++fault) {
                d->SetTexture(0, app_texture);
                for (unsigned i = 0; i < 6; ++i) d->SetSamplerState(0, sk[i], sv[i]);
                d->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
                d->SetRenderState(D3DRS_ZFUNC, D3DCMP_GREATER);
                d->SetVertexShaderConstantF(250, consts, 6);
                pass.set_faults(fault == 1 ? 1u : fault == 2 ? 2u : 0u);
                float dim[4];
                core::dim_constants(.5f, .5f, pass.dim_width(), pass.dim_height(), dim);
                d->BeginScene();
                SkyStarsReport rep{};
                pass.draw_deferred(c, steady_tw, dim, &rep);
                d->EndScene();
                mismatches += (rep.drew != (fault != 1)) + (SUCCEEDED(rep.restore) != (fault != 2));
                IDirect3DBaseTexture9* t = nullptr;
                d->GetTexture(0, &t);
                mismatches += t != app_texture;
                if (t) t->Release();
                for (unsigned i = 0; i < 6; ++i) {
                    DWORD v = 0;
                    d->GetSamplerState(0, sk[i], &v);
                    mismatches += v != sv[i];
                }
                DWORD z = 0, zf = 0;
                d->GetRenderState(D3DRS_ZENABLE, &z);
                d->GetRenderState(D3DRS_ZFUNC, &zf);
                // The injected restore fault skips one restoring SetRenderState (the last touched, ZFUNC): it may differ.
                mismatches += (z != D3DZB_FALSE) + (fault != 2 && zf != D3DCMP_GREATER);
                float back[24];
                d->GetVertexShaderConstantF(250, back, 6);
                mismatches += std::memcmp(back, consts, sizeof back) != 0;
            }
            d->SetTexture(0, nullptr);
            if (app_texture) app_texture->Release();
            std::snprintf(detail, sizeof detail, "runs=3 mismatches=%u (clean, draw fault, restore fault)", mismatches);
            check(mismatches == 0, "deferred_state_restored", detail);
            d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
            d->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        }
        d->SetDepthStencilSurface(old_depth);
        if (old_depth) old_depth->Release();
        depth->Release();
        // Invariant 5: the copy (5120x1440 -> 640x180, StretchRect linear) and the deferred batch at 5120x1440,
        // event-flushed over 100 frames each (upper bounds), and the deferred draw's CPU time and device calls.
        {
            Target big;
            big.create(d, 5120, 1440);
            IDirect3DSurface9* big_depth = nullptr;
            d->CreateDepthStencilSurface(5120, 1440, D3DFMT_D24X8, D3DMULTISAMPLE_NONE, 0, TRUE, &big_depth, nullptr);
            IDirect3DQuery9* q = nullptr;
            d->CreateQuery(D3DQUERYTYPE_EVENT, &q);
            auto flush = [&] {
                q->Issue(D3DISSUE_END);
                while (q->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {
                }
            };
            const double bm11 = m11, bm00 = m11 * 1440 / 5120;
            float cb[16];
            core::vertex_constants(r, float(bm00), float(bm11), 0.f, 0.f, 0.f, 0.f, 5120, 1440, cb);
            d->SetRenderTarget(0, big.surface);
            d->SetDepthStencilSurface(big_depth);
            d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(255, 60, 40, 80), 1.f, 0);
            const bool prepared = SUCCEEDED(pass.prepare_dim(5120, 1440)) && pass.dim_width() == 640 && pass.dim_height() == 180;
            d->BeginScene();
            flush();
            const unsigned reps = 100;
            QueryPerformanceCounter(&t0);
            for (unsigned i = 0; i < reps; ++i) pass.copy_dim(big.surface);
            flush();
            QueryPerformanceCounter(&t1);
            const double copy_ms = double(t1.QuadPart - t0.QuadPart) * 1e3 / double(f.QuadPart) / reps;
            float dim[4];
            core::dim_constants(.5f, .5f, pass.dim_width(), pass.dim_height(), dim);
            double cpu = 0;
            unsigned calls = 0;
            QueryPerformanceCounter(&t0);
            for (unsigned i = 0; i < reps; ++i) {
                LARGE_INTEGER a{}, b{};
                QueryPerformanceCounter(&a);
                SkyStarsReport rep{};
                pass.draw_deferred(cb, steady_tw, dim, &rep);
                QueryPerformanceCounter(&b);
                cpu += double(b.QuadPart - a.QuadPart);
                calls = rep.calls;
            }
            flush();
            QueryPerformanceCounter(&t1);
            d->EndScene();
            const double batch_ms = double(t1.QuadPart - t0.QuadPart) * 1e3 / double(f.QuadPart) / reps;
            std::snprintf(detail, sizeof detail,
                          "dim=640x180 prepared=%u copy_ms_upper_bound=%.4f deferred_batch_ms_upper_bound=%.4f"
                          " draw_deferred_cpu_us=%.2f calls=%u stars=%u",
                          unsigned(prepared), copy_ms, batch_ms, cpu * 1e6 / double(f.QuadPart) / reps, calls, pass.stars());
            std::printf("COST %s\n", detail);
            check(prepared && copy_ms <= .2, "dim_copy_le_0.2ms", detail);
            q->Release();
            d->SetDepthStencilSurface(nullptr);
            big_depth->Release();
            big.release();
            pass.before_reset();
            check(pass.references() == 7 && pass.dim_width() == 0, "before_reset_releases_dim_texture");
        }
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
