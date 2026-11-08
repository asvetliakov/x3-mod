// The projectile stage's look frames (docs/architecture/projectile-look.md, "Implementation (2026-11-08)"): a standalone
// native D3D9 program beside weapon_effects_fixture.cpp (whose record binds its source), built and run only by
// run_weapon_effects.py --host native --dump DIR. It creates a plain HAL device, attaches the production
// WeaponEffectsPass with the projectile programs and draws through the production builder and run_projectiles; each
// frame's FP16 scene is written as linear RGB PFM <dir>\<name>.pfm, which the runner encodes as PNG. No check, no record.
// Never launches the game.
#include <windows.h>
#include <d3d9.h>
#include "../../src/renderer/weapon_effects_pass.h"
#include "../../src/proxy/weapon_projectile_stage.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace rr = x3m::renderer;
namespace wc = x3m::weapon_effects::core;

namespace {
template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { release(); }
    void release() { if (p) { p->Release(); p = nullptr; } }
};
float half_to_float(std::uint16_t h) {
    const std::uint32_t s = (h & 0x8000u) << 16;
    std::uint32_t e = (h >> 10) & 0x1f, m = h & 0x3ff, out;
    if (e == 0) {
        if (m == 0) out = s;
        else { while (!(m & 0x400)) { m <<= 1; --e; } ++e; m &= 0x3ff; out = s | (e + 112) << 23 | m << 13; }
    } else if (e == 0x1f) out = s | 0x7f800000u | m << 13;
    else out = s | (e + 112) << 23 | m << 13;
    float f;
    std::memcpy(&f, &out, 4);
    return f;
}
IDirect3DDevice9* dev = nullptr;
D3DCAPS9 caps{};
D3DFORMAT adapter_format = D3DFMT_X8R8G8B8;
void device_guard(const char*) {}
namespace ps = x3m::weapon_effects::projectile_stage;
namespace pf = x3m::weapon_effects::projectile_family;
struct ProjTarget {
    UINT w = 0, h = 0;
    Com<IDirect3DTexture9> scene_tex, lane_tex;
    Com<IDirect3DSurface9> scene_s, lane_s, depth_s, scene_sys, lane_sys;
    void release() {
        scene_s.release(); lane_s.release(); depth_s.release(); scene_sys.release(); lane_sys.release();
        scene_tex.release(); lane_tex.release();
    }
};
bool make_proj_target(ProjTarget& t, UINT w, UINT h) {
    device_guard("make_proj_target");
    t.w = w;
    t.h = h;
    return SUCCEEDED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &t.scene_tex.p, nullptr)) &&
           SUCCEEDED(t.scene_tex.p->GetSurfaceLevel(0, &t.scene_s.p)) &&
           SUCCEEDED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &t.lane_tex.p, nullptr)) &&
           SUCCEEDED(t.lane_tex.p->GetSurfaceLevel(0, &t.lane_s.p)) &&
           SUCCEEDED(dev->CreateDepthStencilSurface(w, h, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &t.depth_s.p, nullptr)) &&
           SUCCEEDED(dev->CreateOffscreenPlainSurface(w, h, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &t.scene_sys.p, nullptr)) &&
           SUCCEEDED(dev->CreateOffscreenPlainSurface(w, h, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &t.lane_sys.p, nullptr));
}
ps::View proj_view(const ProjTarget& t, float m00) {
    ps::View v;
    v.r[0] = v.r[4] = v.r[8] = 1.f;
    v.m00 = m00;
    v.m11 = m00 * float(t.w) / float(t.h);
    const float n = 1.f, f = 1e6f;
    v.m22 = f / (f - n);
    v.m32 = -n * f / (f - n);
    v.width = float(t.w);
    v.height = float(t.h);
    v.near_z = n;
    return v;
}
ps::BodyLook ire_look() {
    ps::BodyLook l;
    ps::tint_of(wc::pack_rgb(121, 82, 39), l.tint); // the IRE row's texture_rgb (weapon_effects.json row 0)
    l.valid = 1;
    return l;
}
// ------------------------------------------------------------------ projectile look dump (--dump <dir>)
// The look frames of the projectile stage (docs/architecture/projectile-look.md, "Implementation"): the production
// builder and run_projectiles at 1920x1080 and the fixture's projection (m00 1.6), the IRE row's look and the
// fighter-tracer family's baked Tune, on black with a dim hull panel (linear 0.02) in the lower right. Each frame's
// FP16 scene is written as linear RGB PFM <dir>\<name>.pfm (run_weapon_effects.py --dump encodes the PNGs); no check
// and no record. Each bolt is a first frame (its streak from P - v dt to the head P, the bridged law's, no multiplier;
// past length_cap_m dimmed by cap / length). Frames: near (IRE at 200 m, 500 m, 1 km, a 16.7 ms step), seta (the same
// at 100 ms: SETA 6), lod (3 km beside 300 m, 16.7 ms).
bool write_pfm(const char* path, ProjTarget& t) {
    if (FAILED(dev->GetRenderTargetData(t.scene_s.p, t.scene_sys.p))) return false;
    D3DLOCKED_RECT lr{};
    if (FAILED(t.scene_sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
    std::FILE* f = std::fopen(path, "wb");
    bool ok = f != nullptr;
    if (ok) {
        std::fprintf(f, "PF\n%u %u\n-1.0\n", t.w, t.h);
        std::vector<float> row(std::size_t(t.w) * 3);
        for (UINT y = t.h; y-- > 0;) { // PFM rows run bottom to top
            const auto* src = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) + y * lr.Pitch);
            for (UINT x = 0; x < t.w; ++x)
                for (unsigned k = 0; k < 3; ++k) row[std::size_t(x) * 3 + k] = half_to_float(src[x * 4 + k]);
            ok = ok && std::fwrite(row.data(), sizeof(float), row.size(), f) == row.size();
        }
        std::fclose(f);
    }
    t.scene_sys.p->UnlockRect();
    return ok;
}
int projectile_dump(const char* dir) {
    rr::WeaponEffectsPass proj;
    proj.configure_projectiles(true);
    proj.attach(dev, *reinterpret_cast<void* const* const*>(dev), caps, adapter_format);
    ProjTarget t;
    if (!proj.caps().projectile_enabled || !make_proj_target(t, 1920, 1080)) {
        std::printf("DUMP_FAIL reason=%s\nRESULT FAIL checks=1 failures=1\n", proj.caps().projectile_reason);
        return 1;
    }
    CreateDirectoryA(dir, nullptr);
    const ps::View view = proj_view(t, 1.6f);
    const pf::Tune tune = pf::families[pf::fighter_tracer].tune;
    const ps::BodyLook look = ire_look();
    struct Bolt { float d_m, ndc_x, ndc_y; };
    struct Frame { const char* name; float step_ms; unsigned count; Bolt bolts[3]; };
    const Frame frames[3] = {{"near", 16.7f, 3, {{200.f, -0.35f, 0.25f}, {500.f, 0.f, 0.f}, {1000.f, 0.35f, -0.25f}}},
                             {"seta", 100.f, 3, {{200.f, -0.35f, 0.25f}, {500.f, 0.f, 0.f}, {1000.f, 0.35f, -0.25f}}},
                             {"lod", 16.7f, 2, {{3000.f, -0.2f, 0.f}, {300.f, 0.2f, 0.f}}}};
    unsigned failures = 0;
    for (const Frame& fr : frames) {
        rr::WeaponProjectileVertex v[12];
        unsigned n = 0;
        for (unsigned i = 0; i < fr.count; ++i) {
            const Bolt& b = fr.bolts[i];
            ps::BoltIn in;
            const float z = b.d_m * ps::view_units_per_metre;
            in.p[0] = b.ndc_x * z / view.m00;
            in.p[1] = b.ndc_y * z / view.m11;
            in.p[2] = z;
            const float speed = 1162.f * ps::view_units_per_metre; // IRE: 581,000 engine units/s
            in.v[0] = 0.8f * speed;                                // mostly across the view, receding
            in.v[2] = 0.6f * speed;
            in.step_s = fr.step_ms * 1e-3f;
            in.age_s = 10.f;
            ps::BoltShape shape;
            if (ps::bolt_vertices(view, tune, look, in, v + 4 * n, &shape)) {
                ++n;
                std::printf("DUMP_BOLT frame=%s distance_m=%.0f core_px=%.2f dim=%.3f length_px=%.1f streak_m=%.1f energy=%.3f glow_sigma_px=%.2f "
                            "reach_px=%.2f dot=%u\n",
                            fr.name, double(b.d_m), double(shape.core_px), double(shape.dim), double(shape.length_px),
                            double(shape.streak_m), double(shape.energy), double(shape.glow_sigma_px), double(shape.ext_px),
                            unsigned(shape.dot));
            }
        }
        dev->BeginScene();
        dev->SetDepthStencilSurface(t.depth_s.p);
        dev->SetRenderTarget(0, t.lane_s.p);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
        dev->SetRenderTarget(0, t.scene_s.p);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.f, 0);
        const D3DRECT hull{LONG(t.w * 0.62f), LONG(t.h * 0.58f), LONG(t.w), LONG(t.h)};
        dev->Clear(1, &hull, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 5, 5, 6), 1.f, 0); // a dim hull panel, linear ~0.02
        dev->SetDepthStencilSurface(nullptr);
        rr::WeaponProjectileFrame f;
        f.width = t.w;
        f.height = t.h;
        f.vertices = v;
        f.bolts = n;
        f.lane = t.lane_s.p;
        f.depth = t.depth_s.p;
        rr::WeaponEffectsReport r{};
        const HRESULT hr = proj.run_projectiles(f, &r);
        dev->EndScene();
        char path[MAX_PATH];
        std::snprintf(path, sizeof path, "%s\\%s.pfm", dir, fr.name);
        const bool ok = hr == S_OK && r.bolts == n && write_pfm(path, t);
        failures += !ok;
        std::printf("DUMP name=%s bolts=%u step_ms=%.1f result=%08lx written=%u\n", fr.name, n, double(fr.step_ms),
                    static_cast<unsigned long>(hr), unsigned(ok));
    }
    t.release();
    proj.detach();
    std::printf("RESULT %s checks=3 failures=%u\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
} // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char* dir = argc > 1 ? argv[1] : "dump";
    HMODULE runtime = LoadLibraryA("d3d9.dll");
    auto* create = runtime ? reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(runtime, "Direct3DCreate9")) : nullptr;
    Com<IDirect3D9> api;
    if (create) api.p = create(D3D_SDK_VERSION);
    if (!api.p) {
        std::printf("DUMP_FAIL reason=d3d\nRESULT FAIL checks=1 failures=1\n");
        return 1;
    }
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "x3mprojdump";
    RegisterClassA(&wc);
    HWND window = CreateWindowA("x3mprojdump", "x3m", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.hDeviceWindow = window;
    Com<IDirect3DDevice9> device;
    if (FAILED(api.p->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device.p))) {
        std::printf("DUMP_FAIL reason=device\nRESULT FAIL checks=1 failures=1\n");
        return 1;
    }
    dev = device.p;
    dev->GetDeviceCaps(&caps);
    D3DDISPLAYMODE mode{};
    dev->GetDisplayMode(0, &mode);
    adapter_format = mode.Format;
    const int code = projectile_dump(dir);
    dev = nullptr;
    return code;
}
