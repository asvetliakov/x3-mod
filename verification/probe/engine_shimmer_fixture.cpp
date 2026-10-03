// Engine heat shimmer fixture (docs/architecture/engine-exhaust-gap-analysis.md gap 9; docs/verification/engine-effects.md).
// Standalone D3D9 (builtin d3d9 through the runner's override), no game, no proxy, no shader compiler: the production
// EngineShimmerPass and the production rect builder (engine_shimmer_core.h collect over engine_plumes_core.h) on a
// synthetic resolved FP16 target at 1920x1080 and 5120x1440, uploaded from the CPU: R = (x mod 32) + 0.5, G = (y mod 32)
// + 0.5 (the displacement is read back as the change of these ramps), B a checker of 8 px squares, A 1. Identity view,
// square pixels, m11 1.7.
//   attach      programs, slots; the FP16 filter refusal (fault) -> D3DERR_NOTAVAILABLE, nothing held
//   displace    a side-view plume of a 60 px nozzle through collect: per valid pixel |d| <= amplitude (+0.05 px) and
//               <= amplitude x the replica's mask (+0.05 px); outside the scissor (the rects' union) byte-equal, inside it
//               where the replica's mask is 0 (one pixel from every rect) byte-equal; the fade: mean |d| near the nozzle,
//               mid-way and near 1.5 L along the axis, and at the rect's sides; revert() -> the whole target byte-equal
//   gate        nozzles of 20 / 23.9 / 24.5 / 30 px: rects only from 24 px; no rect -> S_FALSE, no device call
//   occlusion   a lane occluder nearer than the plume over the back half of the rect: byte-equal there, displaced in front
//   state       hostile caller state (RT0 another target, RT1, depth, viewport, scissor, blend, a texture, a constant, FVF
//               mode) restored after the run; the pass's own scene without a caller scene
//   refusal     a non-FP16 target, a recording caller, an empty rect list, zero amplitude: S_FALSE, the target unchanged
//   fault       a failed draw names its step, restores the state, leaves the copy pending and revert() restores it; a
//               failed scratch creation holds nothing pending; the next frame draws
//   reset       before_reset releases the block and the scratch (programs survive), forgets the pending revert; after
//               Reset the next run recreates them and draws
//   timing      EVENT-fenced run + revert in a frame tail for 1 / 4 / 16 rects of 10 % of the screen each
// Validation-only readback; never launches the game.
#include "../../src/renderer/engine_shimmer_pass.h"
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
namespace es = x3m::engine_shimmer;
namespace ep = x3m::engine_plumes;
namespace ee = x3m::engine_effects::core;
namespace rr = x3m::renderer;
template <class T> struct Com {
    T* p = nullptr;
    ~Com() {
        if (p) p->Release();
    }
    T* operator->() const { return p; }
};
unsigned checks = 0, failures = 0;
void report(const char* label, bool ok) {
    ++checks;
    if (!ok) ++failures;
    std::printf("CHECK %s %s\n", label, ok ? "PASS" : "FAIL");
}
void check(const char* label, HRESULT hr) {
    if (FAILED(hr)) {
        std::printf("FATAL %s %08lx\n", label, hr);
        throw std::runtime_error(label);
    }
}
float half_to_float(unsigned short h) {
    const unsigned s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
    float v;
    if (e == 0) v = std::ldexp(float(m), -24);
    else if (e == 31) v = m ? NAN : INFINITY;
    else v = std::ldexp(float(m + 1024), int(e) - 25);
    return s ? -v : v;
}
unsigned short float_to_half(float f) { // finite, normal range, round to nearest
    unsigned bits;
    std::memcpy(&bits, &f, 4);
    const unsigned sign = (bits >> 16) & 0x8000u;
    int e = int((bits >> 23) & 255) - 127 + 15;
    unsigned m = bits & 0x7fffffu;
    if (e <= 0) return static_cast<unsigned short>(sign);
    unsigned h = sign | (unsigned(e) << 10) | (m >> 13);
    if (m & 0x1000u) ++h;
    return static_cast<unsigned short>(h);
}
double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
HRESULT complete_fence(IDirect3DQuery9* q) {
    check("fence issue", q->Issue(D3DISSUE_END));
    HRESULT hr;
    const DWORD start = GetTickCount();
    while ((hr = q->GetData(nullptr, 0, D3DGETDATA_FLUSH)) == S_FALSE && GetTickCount() - start < 5000) Sleep(0);
    return hr;
}
const float m11 = 1.7f, projection_m22 = 1.000003f, projection_m32 = -6.0000184f;
constexpr unsigned period = 32;
struct Targets {
    UINT w, h;
    Com<IDirect3DTexture9> target, lane, other, rgba8;
    Com<IDirect3DSurface9> target_s, lane_s, other_s, rgba8_s, depth, staging, lane_staging;
    std::vector<unsigned short> pristine;
    Targets(IDirect3DDevice9* d, UINT width, UINT height)
        : w(width), h(height) {
        auto rt = [&](D3DFORMAT f, Com<IDirect3DTexture9>& t, Com<IDirect3DSurface9>& s, const char* what) {
            check(what, d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, f, D3DPOOL_DEFAULT, &t.p, nullptr));
            check(what, t->GetSurfaceLevel(0, &s.p));
        };
        rt(D3DFMT_A16B16G16R16F, target, target_s, "target");
        rt(D3DFMT_R32F, lane, lane_s, "lane");
        rt(D3DFMT_A16B16G16R16F, other, other_s, "other");
        rt(D3DFMT_A8R8G8B8, rgba8, rgba8_s, "rgba8");
        check("depth", d->CreateDepthStencilSurface(w, h, D3DFMT_D24X8, D3DMULTISAMPLE_NONE, 0, TRUE, &depth.p, nullptr));
        check("staging", d->CreateOffscreenPlainSurface(w, h, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &staging.p, nullptr));
        check("lane staging", d->CreateOffscreenPlainSurface(w, h, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &lane_staging.p, nullptr));
        pristine.resize(std::size_t(w) * h * 4);
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w; ++x) {
                unsigned short* p = &pristine[(std::size_t(y) * w + x) * 4];
                p[0] = float_to_half(float(x % period) + .5f);
                p[1] = float_to_half(float(y % period) + .5f);
                p[2] = float_to_half(((x / 8) + (y / 8)) % 2 ? .75f : .25f);
                p[3] = float_to_half(1.f);
            }
    }
    // The pattern into the target (UpdateSurface from the system-memory staging surface).
    void upload(IDirect3DDevice9* d) {
        D3DLOCKED_RECT r{};
        check("lock staging", staging->LockRect(&r, nullptr, 0));
        for (UINT y = 0; y < h; ++y)
            std::memcpy(static_cast<unsigned char*>(r.pBits) + y * r.Pitch, &pristine[std::size_t(y) * w * 4], w * 8);
        check("unlock staging", staging->UnlockRect());
        check("upload", d->UpdateSurface(staging.p, nullptr, target_s.p, nullptr));
    }
    // The lane: device depth of view z `z` inside [x0, x1) x [y0, y1), the sky's -1 elsewhere.
    void lane_fill(IDirect3DDevice9* d, int x0, int y0, int x1, int y1, float z) {
        D3DLOCKED_RECT r{};
        check("lock lane", lane_staging->LockRect(&r, nullptr, 0));
        const float depth = projection_m22 + projection_m32 / z;
        for (UINT y = 0; y < h; ++y) {
            float* row = reinterpret_cast<float*>(static_cast<unsigned char*>(r.pBits) + y * r.Pitch);
            for (UINT x = 0; x < w; ++x)
                row[x] = int(x) >= x0 && int(x) < x1 && int(y) >= y0 && int(y) < y1 ? depth : -1.f;
        }
        check("unlock lane", lane_staging->UnlockRect());
        check("lane upload", d->UpdateSurface(lane_staging.p, nullptr, lane_s.p, nullptr));
    }
    std::vector<unsigned short> read(IDirect3DDevice9* d) {
        check("readback", d->GetRenderTargetData(target_s.p, staging.p));
        D3DLOCKED_RECT r{};
        check("lock staging", staging->LockRect(&r, nullptr, D3DLOCK_READONLY));
        std::vector<unsigned short> out(std::size_t(w) * h * 4);
        for (UINT y = 0; y < h; ++y)
            std::memcpy(&out[std::size_t(y) * w * 4], static_cast<const unsigned char*>(r.pBits) + y * r.Pitch, w * 8);
        staging->UnlockRect();
        return out;
    }
    float m00() const { return m11 * float(h) / float(w); } // square pixels
    float ppu(float z) const { return m11 * float(h) * .5f / z; }
};
ep::View view_of(const Targets& t) {
    ep::View v;
    const float r[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    std::memcpy(v.rows, r, sizeof r);
    v.m00 = t.m00();
    v.m11 = m11;
    v.height = float(t.h);
    v.near_z = -projection_m32 / projection_m22;
    return v;
}
es::Projection projection_of(const Targets& t) {
    es::Projection p;
    p.m00 = t.m00();
    p.m11 = m11;
    p.m22 = projection_m22;
    p.m32 = projection_m32;
    p.width = float(t.w);
    p.height = float(t.h);
    return p;
}
ee::Record record(float x, float y, float z, float ax, float ay, float az, float value, float zscale) {
    ee::Record r{};
    const float n = std::sqrt(ax * ax + ay * ay + az * az);
    r.origin[0] = x;
    r.origin[1] = y;
    r.origin[2] = z;
    r.axis[0] = ax / n;
    r.axis[1] = ay / n;
    r.axis[2] = az / n;
    r.size = value;
    r.z = zscale;
    const float s = (zscale - .25f) / 1.75f;
    r.s = s < 0 ? 0 : s > 1 ? 1 : s;
    r.body = -1;
    r.flags = std::uint16_t(unsigned(ee::white) << ee::cluster_shift);
    r.node_handle = 7;
    r.model = 20000;
    return r;
}
// A side-view plume whose projected nozzle width is `nozzle_px` (the look's nozzle width 0.5 of the value), pointing
// left from (x_ndc, 0) at depth Z.
ee::Record side_plume(const Targets& t, float nozzle_px, float x_ndc = .3f, float z_scale = 2.f, float Z = 2000.f) {
    const float value = nozzle_px / (ep::default_look.nozzle_width * t.ppu(Z));
    return record(x_ndc * Z / t.m00(), 0.f, Z, -1.f, 0.f, 0.f, value, z_scale);
}
unsigned collect(const Targets& t, const ee::Record* r, unsigned n, es::Rect* out, es::Stats* st) {
    return es::collect(r, n, nullptr, view_of(t), projection_of(t), ep::Preset::standard, 0.f, nullptr, nullptr, nullptr,
                       nullptr, out, st);
}
rr::EngineShimmerFrame frame_for(Targets& t, const es::Rect* rects, unsigned n, bool lane = false, float px = 1.5f) {
    rr::EngineShimmerFrame f;
    f.width = t.w;
    f.height = t.h;
    f.target = t.target.p;
    f.target_surface = t.target_s.p;
    f.lane = lane ? t.lane.p : nullptr;
    f.rects = rects;
    f.rect_count = n;
    es::amplitude_px(px, float(t.h), &f.amplitude_px);
    f.seconds = 1.5f;
    f.caller_scene_open = true;
    return f;
}
rr::EngineShimmerReport run(IDirect3DDevice9* d, rr::EngineShimmerPass& pass, const rr::EngineShimmerFrame& f, bool scene = true) {
    rr::EngineShimmerReport r{};
    if (scene) check("scene begin", d->BeginScene());
    pass.run(f, &r);
    if (scene) check("scene end", d->EndScene());
    return r;
}
float wrap(float v) {
    while (v >= float(period) * .5f) v -= float(period);
    while (v < -float(period) * .5f) v += float(period);
    return v;
}
// Per pixel: the measured displacement (valid away from the ramps' wraps), the replica's mask sum, and whether any rect
// is within one pixel (the boundary band excluded from the byte-equality inside the scissor).
struct Field {
    std::vector<float> dx, dy, mask;
    std::vector<unsigned char> valid, near_rect;
};
Field measure(const Targets& t, const std::vector<unsigned short>& out, const es::Rect* rects, unsigned n) {
    Field f;
    const std::size_t count = std::size_t(t.w) * t.h;
    f.dx.assign(count, 0.f);
    f.dy.assign(count, 0.f);
    f.mask.assign(count, 0.f);
    f.valid.assign(count, 0);
    f.near_rect.assign(count, 0);
    for (UINT y = 0; y < t.h; ++y)
        for (UINT x = 0; x < t.w; ++x) {
            const std::size_t i = std::size_t(y) * t.w + x;
            const unsigned short* a = &t.pristine[i * 4];
            const unsigned short* b = &out[i * 4];
            f.dx[i] = wrap(half_to_float(b[0]) - half_to_float(a[0]));
            f.dy[i] = wrap(half_to_float(b[1]) - half_to_float(a[1]));
            const unsigned mx = x % period, my = y % period;
            f.valid[i] = mx >= 4 && mx < period - 4 && my >= 4 && my < period - 4;
            float m = 0.f;
            bool beside = false;
            for (unsigned k = 0; k < n; ++k) {
                float v = 0.f;
                es::mask_at(rects[k], float(x) + .5f, float(y) + .5f, &v);
                m += v;
                const es::Rect& r = rects[k];
                const float rx = float(x) + .5f - r.origin[0], ry = float(y) + .5f - r.origin[1];
                const float s = rx * r.axis[0] + ry * r.axis[1], tt = std::fabs(-rx * r.axis[1] + ry * r.axis[0]);
                if (s > -r.back - 1.5f && s < r.length + 1.5f && tt < r.half_width + 1.5f) beside = true;
            }
            f.mask[i] = m;
            f.near_rect[i] = beside;
        }
    return f;
}
bool same_pixel(const std::vector<unsigned short>& a, const std::vector<unsigned short>& b, std::size_t i) {
    return !std::memcmp(&a[i * 4], &b[i * 4], 8);
}

void attach_case(IDirect3DDevice9* d, const D3DCAPS9& caps, D3DFORMAT format, rr::EngineShimmerPass& pass) {
    pass.set_faults(1);
    const HRESULT refused = pass.attach(d, *reinterpret_cast<void* const* const*>(d), caps, format);
    const unsigned held = pass.references();
    const char* reason = pass.caps().reason;
    pass.set_faults(0);
    const HRESULT hr = pass.attach(d, *reinterpret_cast<void* const* const*>(d), caps, format);
    std::printf("ATTACH refused=%08lx refused_reason=%s held=%u result=%08lx reason=%s ps_slots=%u max_ps_slots=%lu fp16_filter=%08lx references=%u\n",
                refused, reason, held, hr, pass.caps().reason, pass.caps().ps_slots,
                static_cast<unsigned long>(caps.MaxPixelShader30InstructionSlots), pass.caps().fp16_filter, pass.references());
    report("attach_fp16_filter_refusal", refused == D3DERR_NOTAVAILABLE && held == 0 && !std::strcmp(reason, "fp16_filter"));
    report("attach", SUCCEEDED(hr) && pass.caps().enabled && pass.references() == 3);
}

void displace_case(IDirect3DDevice9* d, Targets& t, rr::EngineShimmerPass& pass) {
    const ee::Record r = side_plume(t, 60.f);
    es::Rect rects[es::max_rects];
    es::Stats st{};
    const unsigned n = collect(t, &r, 1, rects, &st);
    report("displace_one_rect", n == 1);
    if (n != 1) return;
    t.upload(d);
    const auto f = frame_for(t, rects, n);
    const auto rep = run(d, pass, f);
    const auto out = t.read(d);
    const Field m = measure(t, out, rects, n);
    const float amp = f.amplitude_px, tol = .05f;
    double worst = 0, worst_over_mask = -1e9, mean_strong = 0;
    std::size_t inside = 0, strong = 0, outside_diff = 0, outside = 0, band_diff = 0, band = 0, moved = 0;
    const es::Rect& q = rects[0];
    double sums[4] = {0, 0, 0, 0};
    std::size_t counts[4] = {0, 0, 0, 0};
    for (UINT y = 0; y < t.h; ++y)
        for (UINT x = 0; x < t.w; ++x) {
            const std::size_t i = std::size_t(y) * t.w + x;
            const bool in_scissor = int(x) >= rep.scissor[0] && int(x) < rep.scissor[2] && int(y) >= rep.scissor[1] &&
                                    int(y) < rep.scissor[3];
            if (!in_scissor) {
                ++outside;
                outside_diff += !same_pixel(out, t.pristine, i);
                continue;
            }
            if (!m.near_rect[i]) {
                ++band;
                band_diff += !same_pixel(out, t.pristine, i);
            }
            if (!m.valid[i]) continue;
            const double len = std::sqrt(double(m.dx[i]) * m.dx[i] + double(m.dy[i]) * m.dy[i]);
            if (m.mask[i] > 0.f) {
                ++inside;
                worst = std::max(worst, len);
                worst_over_mask = std::max(worst_over_mask, len - double(amp) * std::min(1.f, m.mask[i]));
            }
            if (m.mask[i] > .25f) { // where the mask is strong the image visibly moves
                ++strong;
                mean_strong += len;
                moved += len > .01;
            }
            // The fade: along the axis on the centre line (|t| < 0.3 half-width) at s / length 0.05-0.25, 0.45-0.55,
            // 0.85-0.97; across at 0.85-0.97 of the half-width between 0.1 and 0.5 of the length.
            const float rx = float(x) + .5f - q.origin[0], ry = float(y) + .5f - q.origin[1];
            const float s = (rx * q.axis[0] + ry * q.axis[1]) / q.length;
            const float tt = std::fabs(-rx * q.axis[1] + ry * q.axis[0]) / q.half_width;
            int b = -1;
            if (tt < .3f && s >= .05f && s < .25f) b = 0;
            else if (tt < .3f && s >= .45f && s < .55f) b = 1;
            else if (tt < .3f && s >= .85f && s < .97f) b = 2;
            else if (tt >= .85f && tt < .97f && s >= .1f && s < .5f) b = 3;
            if (b >= 0) {
                sums[b] += len;
                ++counts[b];
            }
        }
    mean_strong = strong ? mean_strong / double(strong) : 0;
    double band_mean[4];
    for (int b = 0; b < 4; ++b) band_mean[b] = counts[b] ? sums[b] / double(counts[b]) : 0;
    // The revert: the whole target back to the input.
    check("scene begin", d->BeginScene());
    const HRESULT reverted = pass.revert();
    check("scene end", d->EndScene());
    const auto back = t.read(d);
    const bool restored = back == t.pristine;
    std::printf("DISPLACE width=%u height=%u nozzle_px=%.2f length_px=%.1f half_width_px=%.1f back_px=%.1f amplitude_px=%.4f result=%08lx scissor_px=%u copy_px=%u inside=%zu strong=%zu moved=%zu mean_strong_px=%.4f max_px=%.4f max_over_mask_px=%.4f near_px=%.4f mid_px=%.4f far_px=%.4f side_px=%.4f outside=%zu outside_diff=%zu band=%zu band_diff=%zu revert=%08lx restored=%u pending_after=%u\n",
                t.w, t.h, double(q.nozzle_px), double(q.length), double(q.half_width), double(q.back), double(amp),
                rep.operation, rep.scissor_px, rep.copy_px, inside, strong, moved, mean_strong, worst, worst_over_mask, band_mean[0],
                band_mean[1], band_mean[2], band_mean[3], outside, outside_diff, band, band_diff, reverted, unsigned(restored),
                unsigned(pass.revert_pending()));
    report("displace_drew", rep.operation == S_OK && rep.drew && rep.restore == S_OK);
    report("displace_within_amplitude", worst <= amp + tol);
    report("displace_within_mask", worst_over_mask <= tol);
    report("displace_moves", strong > 0 && double(moved) >= .6 * double(strong) && mean_strong >= .2 * amp);
    report("displace_outside_byte_equal", outside > 0 && outside_diff == 0);
    report("displace_unmasked_byte_equal", band_diff == 0);
    report("displace_fade_along", band_mean[0] > band_mean[1] && band_mean[1] > band_mean[2] && band_mean[2] < .25 * amp);
    report("displace_fade_side", band_mean[3] < .5 * band_mean[0]);
    report("displace_revert_byte_equal", reverted == S_OK && restored && !pass.revert_pending());
}

void gate_case(IDirect3DDevice9* d, Targets& t, rr::EngineShimmerPass& pass) {
    const float sizes[4] = {20.f, 23.9f, 24.5f, 30.f};
    unsigned kept[4];
    for (int i = 0; i < 4; ++i) {
        const ee::Record r = side_plume(t, sizes[i]);
        es::Rect rects[es::max_rects];
        es::Stats st{};
        kept[i] = collect(t, &r, 1, rects, &st);
    }
    // No rect: S_FALSE, no device call, the target untouched.
    t.upload(d);
    const auto f = frame_for(t, nullptr, 0);
    const auto rep = run(d, pass, f);
    const bool same = t.read(d) == t.pristine;
    // The cap and the rank: 20 nozzles of 30..49 px: the 16 largest are kept, largest first.
    std::vector<ee::Record> crowd;
    for (int i = 0; i < 20; ++i) crowd.push_back(side_plume(t, 30.f + float(i), -.6f + .06f * float(i)));
    es::Rect rects[es::max_rects];
    es::Stats st{};
    const unsigned n = collect(t, crowd.data(), unsigned(crowd.size()), rects, &st);
    bool ranked = n == es::max_rects;
    for (unsigned i = 1; i < n; ++i) ranked = ranked && rects[i - 1].nozzle_px >= rects[i].nozzle_px;
    std::printf("GATE width=%u kept_20=%u kept_23_9=%u kept_24_5=%u kept_30=%u empty=%08lx empty_calls=%u empty_skipped=%s unchanged=%u crowd=%u capped=%u largest=%.2f smallest=%.2f\n",
                t.w, kept[0], kept[1], kept[2], kept[3], rep.operation, rep.calls, rep.skipped ? rep.skipped : "-",
                unsigned(same), n, st.capped, double(n ? rects[0].nozzle_px : 0), double(n ? rects[n - 1].nozzle_px : 0));
    report("gate_24px", kept[0] == 0 && kept[1] == 0 && kept[2] == 1 && kept[3] == 1);
    report("gate_empty_no_call", rep.operation == S_FALSE && rep.calls == 0 && same);
    report("gate_cap_16_ranked", ranked && st.capped == 4 && rects[n - 1].nozzle_px >= 33.5f);
}

void occlusion_case(IDirect3DDevice9* d, Targets& t, rr::EngineShimmerPass& pass) {
    const float Z = 2000.f;
    const ee::Record r = side_plume(t, 60.f, .3f, 2.f, Z);
    es::Rect rects[es::max_rects];
    es::Stats st{};
    const unsigned n = collect(t, &r, 1, rects, &st);
    if (n != 1) {
        report("occlusion_rect", false);
        return;
    }
    const es::Rect& q = rects[0];
    // An occluder at half the plume's depth over the far half of the rect (the plume points left: x < the middle).
    const int split = int(q.origin[0] - .5f * q.length);
    t.upload(d);
    t.lane_fill(d, 0, 0, split, int(t.h), Z * .5f);
    const auto rep = run(d, pass, frame_for(t, rects, n, true));
    const auto out = t.read(d);
    const Field m = measure(t, out, rects, n);
    std::size_t hidden = 0, hidden_diff = 0, front = 0, front_moved = 0;
    for (UINT y = 0; y < t.h; ++y)
        for (UINT x = 0; x < t.w; ++x) {
            const std::size_t i = std::size_t(y) * t.w + x;
            if (m.mask[i] <= .2f) continue;
            if (int(x) < split - 2) {
                ++hidden;
                hidden_diff += !same_pixel(out, t.pristine, i);
            } else if (int(x) > split + 2 && m.valid[i]) {
                ++front;
                front_moved += std::sqrt(m.dx[i] * m.dx[i] + m.dy[i] * m.dy[i]) > .01f;
            }
        }
    check("scene begin", d->BeginScene());
    pass.revert();
    check("scene end", d->EndScene());
    t.lane_fill(d, 0, 0, 0, 0, Z);
    std::printf("OCCLUSION width=%u result=%08lx depth=%.6f hidden=%zu hidden_diff=%zu front=%zu front_moved=%zu\n", t.w,
                rep.operation, double(q.depth), hidden, hidden_diff, front, front_moved);
    report("occlusion_hidden_byte_equal", rep.drew && hidden > 0 && hidden_diff == 0);
    report("occlusion_front_displaced", front > 0 && double(front_moved) >= .5 * double(front));
}

void state_case(IDirect3DDevice9* d, Targets& t, rr::EngineShimmerPass& pass) {
    const ee::Record r = side_plume(t, 60.f);
    es::Rect rects[es::max_rects];
    es::Stats st{};
    const unsigned n = collect(t, &r, 1, rects, &st);
    t.upload(d);
    // Hostile caller state.
    check("rt0", d->SetRenderTarget(0, t.other_s.p));
    check("rt1", d->SetRenderTarget(1, t.lane_s.p));
    check("ds", d->SetDepthStencilSurface(t.depth.p));
    const D3DVIEWPORT9 vp{3, 5, 100, 70, .25f, .75f};
    check("viewport", d->SetViewport(&vp));
    const RECT sc{7, 9, 211, 113};
    check("scissor", d->SetScissorRect(&sc));
    check("scissor on", d->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE));
    check("blend", d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE));
    check("srcblend", d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR));
    check("z", d->SetRenderState(D3DRS_ZENABLE, TRUE));
    check("colorwrite", d->SetRenderState(D3DRS_COLORWRITEENABLE, 7));
    check("srgb", d->SetRenderState(D3DRS_SRGBWRITEENABLE, TRUE));
    check("texture", d->SetTexture(0, t.rgba8.p));
    check("sampler", d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_ANISOTROPIC));
    const float sentinel[4] = {11.f, 22.f, 33.f, 44.f};
    check("constant", d->SetPixelShaderConstantF(30, sentinel, 1));
    check("vs", d->SetVertexShader(nullptr));
    check("ps", d->SetPixelShader(nullptr));
    check("fvf", d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE));
    rr::EngineShimmerFrame f = frame_for(t, rects, n);
    f.caller_scene_open = false; // the pass opens and closes its own scene
    rr::EngineShimmerReport rep{};
    pass.run(f, &rep);
    Com<IDirect3DSurface9> rt0, rt1, ds;
    D3DVIEWPORT9 vp2{};
    RECT sc2{};
    DWORD scissor_on = 0, blend = 0, src = 0, z = 0, cw = 0, srgb = 0, mag = 0, fvf = 0;
    Com<IDirect3DBaseTexture9> tex;
    Com<IDirect3DVertexDeclaration9> decl;
    float c30[4]{};
    d->GetRenderTarget(0, &rt0.p);
    d->GetRenderTarget(1, &rt1.p);
    d->GetDepthStencilSurface(&ds.p);
    d->GetViewport(&vp2);
    d->GetScissorRect(&sc2);
    d->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissor_on);
    d->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
    d->GetRenderState(D3DRS_SRCBLEND, &src);
    d->GetRenderState(D3DRS_ZENABLE, &z);
    d->GetRenderState(D3DRS_COLORWRITEENABLE, &cw);
    d->GetRenderState(D3DRS_SRGBWRITEENABLE, &srgb);
    d->GetSamplerState(0, D3DSAMP_MAGFILTER, &mag);
    d->GetTexture(0, &tex.p);
    d->GetPixelShaderConstantF(30, c30, 1);
    d->GetFVF(&fvf);
    d->GetVertexDeclaration(&decl.p);
    const bool restored = rt0.p == t.other_s.p && rt1.p == t.lane_s.p && ds.p == t.depth.p && vp2.X == vp.X && vp2.Y == vp.Y &&
                          vp2.Width == vp.Width && vp2.Height == vp.Height && vp2.MinZ == vp.MinZ && vp2.MaxZ == vp.MaxZ &&
                          !std::memcmp(&sc2, &sc, sizeof sc) && scissor_on == TRUE && blend == TRUE && src == D3DBLEND_DESTCOLOR &&
                          z == TRUE && cw == 7 && srgb == TRUE && mag == D3DTEXF_ANISOTROPIC &&
                          tex.p == static_cast<IDirect3DBaseTexture9*>(t.rgba8.p) && !std::memcmp(c30, sentinel, sizeof c30) &&
                          fvf == (D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    // The draw happened despite the hostile state (RT0 was another target).
    const auto out = t.read(d);
    std::size_t changed = 0;
    for (std::size_t i = 0; i < std::size_t(t.w) * t.h; ++i) changed += !same_pixel(out, t.pristine, i);
    pass.revert();
    const bool reverted = t.read(d) == t.pristine;
    std::printf("STATE width=%u result=%08lx restore=%08lx restored=%u own_scene=1 changed=%zu reverted=%u calls=%u\n", t.w,
                rep.operation, rep.restore, unsigned(restored), changed, unsigned(reverted), rep.calls);
    report("state_restored", rep.operation == S_OK && rep.restore == S_OK && restored);
    report("state_own_scene_drew", rep.drew && changed > 0 && reverted);
    // Back to a plain state for the next cases.
    check("rt1 off", d->SetRenderTarget(1, nullptr));
    check("ds off", d->SetDepthStencilSurface(nullptr));
    check("rt0", d->SetRenderTarget(0, t.target_s.p));
    check("tex off", d->SetTexture(0, nullptr));
    check("scissor off", d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE));
    check("srgb off", d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE));
}

void refusal_case(IDirect3DDevice9* d, Targets& t, rr::EngineShimmerPass& pass) {
    const ee::Record r = side_plume(t, 60.f);
    es::Rect rects[es::max_rects];
    es::Stats st{};
    const unsigned n = collect(t, &r, 1, rects, &st);
    t.upload(d);
    rr::EngineShimmerFrame f = frame_for(t, rects, n);
    f.target = t.rgba8.p;
    f.target_surface = t.rgba8_s.p;
    const auto format = run(d, pass, f);
    f = frame_for(t, rects, n);
    f.caller_stateblock_recording = true;
    const auto recording = run(d, pass, f);
    f = frame_for(t, rects, n, false, 0.f);
    const auto zero = run(d, pass, f);
    const bool same = t.read(d) == t.pristine && !pass.revert_pending();
    std::printf("REFUSAL width=%u format=%08lx/%s/%u recording=%08lx/%s/%u amplitude=%08lx/%s/%u unchanged=%u\n", t.w,
                format.operation, format.skipped ? format.skipped : "-", format.calls, recording.operation,
                recording.skipped ? recording.skipped : "-", recording.calls, zero.operation, zero.skipped ? zero.skipped : "-",
                zero.calls, unsigned(same));
    report("refusal_format", format.operation == S_FALSE && format.skipped && !std::strcmp(format.skipped, "format") && format.calls == 0);
    report("refusal_recording", recording.operation == S_FALSE && recording.calls == 0);
    report("refusal_zero_amplitude", zero.operation == S_FALSE && zero.calls == 0 && same);
}

void fault_case(IDirect3DDevice9* d, Targets& t, rr::EngineShimmerPass& pass) {
    const ee::Record r = side_plume(t, 60.f);
    es::Rect rects[es::max_rects];
    es::Stats st{};
    const unsigned n = collect(t, &r, 1, rects, &st);
    t.upload(d);
    check("rt0", d->SetRenderTarget(0, t.other_s.p));
    pass.set_faults(2);
    const auto failed = run(d, pass, frame_for(t, rects, n));
    Com<IDirect3DSurface9> rt0;
    d->GetRenderTarget(0, &rt0.p);
    const bool state_back = rt0.p == t.other_s.p;
    const bool pending = pass.revert_pending();
    const HRESULT reverted = pass.revert();
    const bool same = t.read(d) == t.pristine;
    pass.set_faults(0);
    // The scratch's creation fails once (before_reset / after_reset without a device Reset only drop the scratch).
    pass.before_reset();
    pass.after_reset(S_OK);
    pass.set_faults(4);
    const auto scratch = run(d, pass, frame_for(t, rects, n));
    const bool none_pending = !pass.revert_pending();
    pass.set_faults(0);
    const auto again = run(d, pass, frame_for(t, rects, n));
    pass.revert();
    std::printf("FAULT width=%u draw=%08lx step=%u restore=%08lx state_back=%u pending=%u revert=%08lx reverted=%u scratch=%08lx scratch_step=%u none_pending=%u next=%08lx drew=%u\n",
                t.w, failed.operation, unsigned(failed.failed), failed.restore, unsigned(state_back), unsigned(pending), reverted,
                unsigned(same), scratch.operation, unsigned(scratch.failed), unsigned(none_pending), again.operation,
                unsigned(again.drew));
    report("fault_draw_step", failed.operation == E_FAIL && failed.failed == rr::EngineShimmerStep::Draw &&
                                  failed.restore == S_OK && state_back);
    report("fault_draw_revert", pending && reverted == S_OK && same);
    report("fault_scratch", scratch.operation == E_OUTOFMEMORY && scratch.failed == rr::EngineShimmerStep::Resources && none_pending);
    report("fault_next_frame_draws", again.operation == S_OK && again.drew);
    check("rt0", d->SetRenderTarget(0, t.target_s.p));
}

void reset_case(IDirect3DDevice9* d, D3DPRESENT_PARAMETERS& pp, rr::EngineShimmerPass& pass, Targets*& t) {
    const ee::Record r = side_plume(*t, 60.f);
    es::Rect rects[es::max_rects];
    es::Stats st{};
    const unsigned n = collect(*t, &r, 1, rects, &st);
    t->upload(d);
    run(d, pass, frame_for(*t, rects, n));
    const unsigned before = pass.references();
    const bool pending = pass.revert_pending();
    pass.before_reset();
    const unsigned released = pass.references();
    const bool forgotten = !pass.revert_pending();
    const auto during = run(d, pass, frame_for(*t, rects, n));
    const UINT w = t->w, h = t->h;
    delete t;
    t = nullptr;
    const HRESULT reset = d->Reset(&pp);
    pass.after_reset(reset);
    t = new Targets(d, w, h);
    t->upload(d);
    const auto after_run = run(d, pass, frame_for(*t, rects, n));
    const unsigned after = pass.references();
    const auto out = t->read(d);
    std::size_t changed = 0;
    for (std::size_t i = 0; i < std::size_t(w) * h; ++i) changed += !same_pixel(out, t->pristine, i);
    pass.revert();
    const bool reverted = t->read(d) == t->pristine;
    std::printf("RESET before=%u pending=%u released=%u forgotten=%u during=%08lx reset=%08lx after=%u result=%08lx drew=%u changed=%zu reverted=%u\n",
                before, unsigned(pending), released, unsigned(forgotten), during.operation, reset, after,
                after_run.operation, unsigned(after_run.drew), changed, unsigned(reverted));
    report("reset_released", before == 6 && pending && released == 3 && forgotten && during.operation == E_FAIL);
    report("reset_recreated", SUCCEEDED(reset) && after == 6 && after_run.drew && changed > 0 && reverted);
}

struct Fence {
    Com<IDirect3DQuery9> q;
    explicit Fence(IDirect3DDevice9* d) { check("event query", d->CreateQuery(D3DQUERYTYPE_EVENT, &q.p)); }
};
// n rects of 10 % of the screen each (half-width H / 10, length W / 2), spread over the screen.
std::vector<es::Rect> tenth_rects(const Targets& t, unsigned n) {
    std::vector<es::Rect> out;
    for (unsigned i = 0; i < n; ++i) {
        const float hw = float(t.h) * .1f, length = float(t.w) * .5f;
        const float origin[2] = {float(t.w) * (.6f + .3f * float(i % 4) / 3.f), float(t.h) * (.15f + .7f * float(i / 4) / 3.f)};
        const float axis[2] = {-1.f, 0.f};
        es::Rect r;
        if (es::make_rect(origin, axis, length, hw, .25f * hw, hw, float(i) / 16.f, 0.f, float(t.w), float(t.h), &r))
            out.push_back(r);
    }
    return out;
}
// One frame tail: a clear of the target as the resolve's write, optionally the shimmer (run + revert), then a first
// target change with a clear of the other target. Fenced wall time; the pass's CPU submit separately.
double tail_ms(IDirect3DDevice9* d, Targets& t, rr::EngineShimmerPass& pass, Fence& fence, const rr::EngineShimmerFrame* f,
               double* submit) {
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    auto ms = [&](LONGLONG a, LONGLONG b) { return double(b - a) * 1000. / double(freq.QuadPart); };
    check("scene begin", d->BeginScene());
    check("lead fence", complete_fence(fence.q.p));
    LARGE_INTEGER a{}, s0{}, s1{}, c{};
    QueryPerformanceCounter(&a);
    check("rt0", d->SetRenderTarget(0, t.target_s.p));
    check("clear", d->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 40, 60, 80), 1.f, 0));
    QueryPerformanceCounter(&s0);
    if (f) {
        rr::EngineShimmerReport r;
        check("shimmer", pass.run(*f, &r));
        check("revert", pass.revert());
    }
    QueryPerformanceCounter(&s1);
    check("rt other", d->SetRenderTarget(0, t.other_s.p));
    check("clear other", d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0));
    check("rt target", d->SetRenderTarget(0, t.target_s.p));
    check("tail fence", complete_fence(fence.q.p));
    QueryPerformanceCounter(&c);
    check("scene end", d->EndScene());
    if (submit) *submit = ms(s0.QuadPart, s1.QuadPart);
    return ms(a.QuadPart, c.QuadPart);
}
void timing_case(IDirect3DDevice9* d, Targets& t, rr::EngineShimmerPass& pass, unsigned n) {
    Fence fence(d);
    const auto rects = tenth_rects(t, n);
    rr::EngineShimmerFrame f = frame_for(t, rects.data(), unsigned(rects.size()));
    std::vector<double> on, off, submit;
    for (unsigned i = 0; i < 126; ++i) {
        const bool with = i % 2 == 0;
        double s = 0;
        const double v = tail_ms(d, t, pass, fence, with ? &f : nullptr, &s);
        if (i < 6) continue;
        (with ? on : off).push_back(v);
        if (with) submit.push_back(s);
    }
    unsigned area = 0;
    for (const auto& r : rects) area += unsigned(2.f * r.half_width * (r.length + r.back));
    rr::EngineShimmerReport rep{};
    check("scene begin", d->BeginScene());
    pass.run(f, &rep);
    pass.revert();
    check("scene end", d->EndScene());
    const double chain = median(on) - median(off), cpu = median(submit);
    std::printf("TIMING width=%u height=%u rects=%u rect_area_share=%.3f scissor_px=%u copy_px=%u fenced_on_ms=%.4f fenced_off_ms=%.4f submit_ms=%.4f chain_ms=%.4f gpu_ms=%.4f samples=%zu calls=%u method=tail_event_fenced\n",
                t.w, t.h, unsigned(rects.size()), rects.empty() ? 0. : double(area) / double(rects.size()) / (double(t.w) * t.h),
                rep.scissor_px, rep.copy_px, median(on), median(off), cpu, chain, std::max(0., chain - cpu), on.size(), rep.calls);
}
} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        std::string only = argc >= 3 && !std::strcmp(argv[1], "--only") ? argv[2] : "";
        auto wanted = [&](const char* name) { return only.empty() || only.find(name) != std::string::npos; };
        WNDCLASSA cls{};
        cls.lpfnWndProc = DefWindowProcA;
        cls.hInstance = GetModuleHandleA(nullptr);
        cls.lpszClassName = "X3EngineShimmerFixture";
        RegisterClassA(&cls);
        HWND window = CreateWindowA(cls.lpszClassName, "X3 engine shimmer fixture", WS_OVERLAPPEDWINDOW, 90, 90, 128, 128,
                                    nullptr, nullptr, cls.hInstance, nullptr);
        if (!window) throw std::runtime_error("window");
        HMODULE runtime = LoadLibraryA("d3d9.dll");
        if (!runtime) throw std::runtime_error("d3d9.dll");
        auto address = GetProcAddress(runtime, "Direct3DCreate9");
        IDirect3D9*(WINAPI * create)(UINT) = nullptr;
        std::memcpy(&create, &address, sizeof create);
        if (!create) throw std::runtime_error("Direct3DCreate9");
        IDirect3D9* api = create(D3D_SDK_VERSION);
        if (!api) throw std::runtime_error("Create9");
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.BackBufferWidth = 64;
        pp.BackBufferHeight = 64;
        pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        pp.hDeviceWindow = window;
        Com<IDirect3DDevice9> device;
        check("device", api->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                          D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device.p));
        IDirect3DDevice9* d = device.p;
        D3DCAPS9 caps{};
        check("caps", d->GetDeviceCaps(&caps));
        D3DDISPLAYMODE mode{};
        check("mode", d->GetDisplayMode(0, &mode));
        rr::EngineShimmerPass pass;
        attach_case(d, caps, mode.Format, pass);
        for (const auto size : {std::pair{1920u, 1080u}, std::pair{5120u, 1440u}}) {
            Targets* t = new Targets(d, size.first, size.second);
            std::printf("SIZE width=%u height=%u\n", size.first, size.second);
            if (wanted("displace")) displace_case(d, *t, pass);
            if (wanted("occlusion")) occlusion_case(d, *t, pass);
            if (size.first == 1920) {
                if (wanted("gate")) gate_case(d, *t, pass);
                if (wanted("state")) state_case(d, *t, pass);
                if (wanted("refusal")) refusal_case(d, *t, pass);
                if (wanted("fault")) fault_case(d, *t, pass);
                if (wanted("reset")) reset_case(d, pp, pass, t);
            }
            if (wanted("timing"))
                for (const unsigned n : {1u, 4u, 16u}) timing_case(d, *t, pass, n);
            delete t;
        }
        pass.detach();
        report("detach_released", pass.references() == 0);
        std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
        std::fflush(stdout);
        TerminateProcess(GetCurrentProcess(), failures ? 1u : 0u);
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        std::printf("RESULT FAIL exception=%s checks=%u failures=%u\n", error.what(), checks, failures + 1);
        std::fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 1u);
        return 1;
    }
}
