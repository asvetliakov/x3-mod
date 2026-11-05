// D3D fixture of the weapon-effects shield-hit stage (src/renderer/weapon_effects_pass.cpp, the production
// WeaponEffectsPass and its shell / decal programs; docs/architecture/weapon-effects.md 5.1 acceptance (b)). It creates
// a plain D3D9 HAL device, attaches the pass, and draws shells and decals onto an A16B16G16R16F scene target with a
// controlled four-channel depth lane (.b = view depth, a sentinel .r < 0 = sky), built by a small FP lane shader
// compiled through the game's d3dx9_37.dll (argv[1], run_weapon_effects.py's contract). It reads the FP16 target back
// and reports the structural relations the note lists: the Fresnel rim rises with the view angle, the rings are
// present on and absent off their widths, the shell's hull cut is 0 behind a near occluder, the decal draws on a hull
// and 0 over sky, the shell core keeps its value, the game-time age law (young draws, past the ripple fades), a
// retired owner draws nothing, Reset recovery, the fault cases, and the stage cost at 1080p and 5120x1440. Every draw
// and readback runs inside BeginScene/EndScene (native D3D9 refuses a draw outside a scene). Never launches the game.
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include "../../src/renderer/weapon_effects_pass.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>

namespace rr = x3m::renderer;

namespace {
unsigned checks = 0, failures = 0;
void report(const char* name, bool ok, const char* detail = "") {
    ++checks;
    if (!ok) ++failures;
    std::printf("CHECK %s %s%s%s\n", name, ok ? "PASS" : "FAIL", *detail ? " " : "", detail);
}
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
HMODULE d3dx = nullptr;
using CompileFn = HRESULT(WINAPI*)(LPCSTR, UINT, const D3DXMACRO*, LPD3DXINCLUDE, LPCSTR, LPCSTR, DWORD, LPD3DXBUFFER*,
                                   LPD3DXBUFFER*, LPD3DXCONSTANTTABLE*);
CompileFn compile_shader = nullptr;
IDirect3DPixelShader9* lane_ps = nullptr;
IDirect3DVertexShader9* quad_vs = nullptr;
IDirect3DVertexDeclaration9* quad_decl = nullptr;

bool compile(const char* src, const char* profile, const DWORD** out, LPD3DXBUFFER* hold) {
    LPD3DXBUFFER code = nullptr, err = nullptr;
    const HRESULT hr = compile_shader(src, UINT(std::strlen(src)), nullptr, nullptr, "main", profile, 0, &code, &err, nullptr);
    if (err) { std::printf("COMPILE_ERR %.200s\n", static_cast<const char*>(err->GetBufferPointer())); err->Release(); }
    if (FAILED(hr) || !code) return false;
    *out = static_cast<const DWORD*>(code->GetBufferPointer());
    *hold = code;
    return true;
}
bool compile_helpers() {
    if (lane_ps) return true;
    if (!compile_shader) return false;
    // A pass-through vertex shader for a full-screen quad (POSITION float4 already in clip space).
    const char* vs = "float4 main(float4 p:POSITION):POSITION{ return p; }";
    // The lane: hull (.b = depth, .r = 0.5 occluder) where VPOS.x < c0.z * c0.x, else sky (.r = -1 sentinel). c0 =
    // (width, depth, occluder_frac, unused).
    const char* ps = "float4 c0:register(c0); float4 main(float2 vp:VPOS):COLOR0{"
                     " return (c0.z >= 0 && vp.x < c0.z * c0.x) ? float4(0.5,0,c0.y,0) : float4(-1,0,0,0); }";
    const DWORD* vw = nullptr;
    const DWORD* pw = nullptr;
    static Com<ID3DXBuffer> vh, ph;
    if (!compile(vs, "vs_3_0", &vw, &vh.p) || !compile(ps, "ps_3_0", &pw, &ph.p)) return false;
    if (FAILED(dev->CreateVertexShader(vw, &quad_vs)) || FAILED(dev->CreatePixelShader(pw, &lane_ps))) return false;
    const D3DVERTEXELEMENT9 elems[] = {{0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
    return SUCCEEDED(dev->CreateVertexDeclaration(elems, &quad_decl));
}
struct Target {
    UINT w = 0, h = 0;
    Com<IDirect3DTexture9> scene_tex, lane_tex;
    Com<IDirect3DSurface9> scene_s, lane_s, sysmem;
    void release() { scene_s.release(); lane_s.release(); sysmem.release(); scene_tex.release(); lane_tex.release(); }
};
bool make_target(Target& t, UINT w, UINT h) {
    t.w = w;
    t.h = h;
    return SUCCEEDED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &t.scene_tex.p, nullptr)) &&
           SUCCEEDED(t.scene_tex.p->GetSurfaceLevel(0, &t.scene_s.p)) &&
           SUCCEEDED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &t.lane_tex.p, nullptr)) &&
           SUCCEEDED(t.lane_tex.p->GetSurfaceLevel(0, &t.lane_s.p)) &&
           SUCCEEDED(dev->CreateOffscreenPlainSurface(w, h, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &t.sysmem.p, nullptr));
}
// Fill the lane: occluder_frac < 0 = sky everywhere; else a hull at view depth `depth` where x < occluder_frac*w.
void draw_lane(Target& t, float depth, float occluder_frac) {
    const float c0[4] = {float(t.w), depth, occluder_frac, 0.f};
    const float quad[4][4] = {{-1, 1, 0.5f, 1}, {1, 1, 0.5f, 1}, {-1, -1, 0.5f, 1}, {1, -1, 0.5f, 1}};
    dev->BeginScene();
    dev->SetRenderTarget(0, t.lane_s.p);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetVertexDeclaration(quad_decl);
    dev->SetVertexShader(quad_vs);
    dev->SetPixelShader(lane_ps);
    dev->SetPixelShaderConstantF(0, c0, 1);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(float) * 4);
    dev->EndScene();
}
bool read_pixel(Target& t, UINT x, UINT y, float out[4]) {
    if (FAILED(dev->GetRenderTargetData(t.scene_s.p, t.sysmem.p))) return false;
    D3DLOCKED_RECT lr{};
    if (FAILED(t.sysmem.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
    const auto* row = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) + y * lr.Pitch);
    for (unsigned k = 0; k < 4; ++k) out[k] = half_to_float(row[x * 4 + k]);
    t.sysmem.p->UnlockRect();
    return true;
}
float max_luma(Target& t) {
    if (FAILED(dev->GetRenderTargetData(t.scene_s.p, t.sysmem.p))) return -1.f;
    D3DLOCKED_RECT lr{};
    if (FAILED(t.sysmem.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return -1.f;
    float best = 0.f;
    for (UINT y = 0; y < t.h; ++y) {
        const auto* row = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) + y * lr.Pitch);
        for (UINT x = 0; x < t.w; ++x) {
            const float v = half_to_float(row[x * 4]) + half_to_float(row[x * 4 + 1]) + half_to_float(row[x * 4 + 2]);
            if (v > best) best = v;
        }
    }
    t.sysmem.p->UnlockRect();
    return best;
}
rr::WeaponShell front_shell(float age_s, float radius = 100.f, float centre_z = 400.f) {
    rr::WeaponShell s{};
    s.centre[2] = centre_z;
    s.axes[0] = s.axes[4] = s.axes[8] = radius;
    s.soft = 0.5f * radius;
    s.tint[0] = s.tint[1] = s.tint[2] = 1.f;
    s.hits = 1;
    s.dir[0][2] = -1.f;
    s.age_s[0] = age_s;
    s.amp[0] = 4.f;
    return s;
}
rr::WeaponEffectsFrame base_frame(Target& t) {
    rr::WeaponEffectsFrame f{};
    f.width = t.w;
    f.height = t.h;
    f.lane = t.lane_tex.p;
    f.four_channel = true;
    f.m00 = 1.6f;
    f.m11 = 2.84f;
    f.near_z = 6.f;
    f.look = rr::default_weapon_look();
    return f;
}
double ticks_us(LARGE_INTEGER a, LARGE_INTEGER b, LARGE_INTEGER freq) {
    return double(b.QuadPart - a.QuadPart) * 1e6 / double(freq.QuadPart);
}
Target g;
std::pair<HRESULT, rr::WeaponEffectsReport> draw(rr::WeaponEffectsPass& pass, const rr::WeaponEffectsFrame& f) {
    dev->BeginScene();
    dev->SetRenderTarget(0, g.scene_s.p);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
    rr::WeaponEffectsReport r{};
    const HRESULT hr = pass.run(f, &r);
    dev->EndScene();
    return {hr, r};
}
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    d3dx = LoadLibraryA(argc > 1 ? argv[1] : "d3dx9_37.dll");
    compile_shader = d3dx ? reinterpret_cast<CompileFn>(GetProcAddress(d3dx, "D3DXCompileShader")) : nullptr;
    if (d3dx) { char path[MAX_PATH]{}; if (GetModuleFileNameA(d3dx, path, MAX_PATH)) std::printf("D3DX_MODULE %s\n", path); }

    HMODULE runtime = LoadLibraryA("d3d9.dll");
    auto* create = runtime ? reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(runtime, "Direct3DCreate9")) : nullptr;
    Com<IDirect3D9> api;
    if (create) api.p = create(D3D_SDK_VERSION);
    if (!api.p) { std::printf("CHECK d3d_created FAIL\nRESULT FAIL checks=1 failures=1\n"); return 1; }
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "x3mweapon";
    RegisterClassA(&wc);
    HWND window = CreateWindowA("x3mweapon", "x3m", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    D3DADAPTER_IDENTIFIER9 id{};
    if (SUCCEEDED(api.p->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id))) {
        std::printf("ADAPTER vendor=0x%04lx device=0x%04lx driver_version=%u.%u.%u.%u\n", id.VendorId, id.DeviceId,
                    unsigned(HIWORD(id.DriverVersion.HighPart)), unsigned(LOWORD(id.DriverVersion.HighPart)),
                    unsigned(HIWORD(id.DriverVersion.LowPart)), unsigned(LOWORD(id.DriverVersion.LowPart)));
        id.Description[sizeof id.Description - 1] = 0;
        id.Driver[sizeof id.Driver - 1] = 0;
        std::printf("ADAPTER_DESC %s\nADAPTER_DRIVER %s\n", id.Description, id.Driver);
    }
    char rpath[MAX_PATH]{};
    if (GetModuleFileNameA(runtime, rpath, MAX_PATH)) std::printf("D3D9_MODULE %s\n", rpath);
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
        std::printf("CHECK device FAIL\nRESULT FAIL checks=1 failures=1\n");
        return 1;
    }
    dev = device.p;
    dev->GetDeviceCaps(&caps);
    D3DDISPLAYMODE mode{};
    dev->GetDisplayMode(0, &mode);
    adapter_format = mode.Format;
    report("d3dx_lane_shader", compile_helpers(), compile_shader ? "" : "D3DXCompileShader unavailable");

    rr::WeaponEffectsPass pass;
    const HRESULT attached = pass.attach(dev, *reinterpret_cast<void* const* const*>(dev), caps, adapter_format);
    report("attach", SUCCEEDED(attached) && pass.caps().enabled, pass.caps().reason);
    std::printf("ATTACH result=%08lx shell_vs=%u shell_ps=%u decal_vs=%u decal_ps=%u fp16=%08lx max_ps=%lu\n", attached,
                pass.caps().shell_vs_slots, pass.caps().shell_ps_slots, pass.caps().decal_vs_slots, pass.caps().decal_ps_slots,
                pass.caps().fp16_blending, static_cast<unsigned long>(caps.MaxPixelShader30InstructionSlots));
    if (!pass.caps().enabled || !lane_ps) {
        std::printf("RESULT FAIL checks=%u failures=%u\n", checks, failures + 1);
        return 1;
    }
    report("target_1080", make_target(g, 1920, 1080));

    // Shell over sky: the core and rings draw.
    draw_lane(g, 0.f, -1.f);
    {
        rr::WeaponEffectsFrame f = base_frame(g);
        rr::WeaponShell s = front_shell(0.0f);
        f.shells = &s;
        f.shell_count = 1;
        auto res = draw(pass, f);
        const float luma = max_luma(g);
        std::printf("SHELL sky_max_luma=%.4f\n", luma);
        report("shell_core_positive", SUCCEEDED(res.first) && res.second.shells == 1 && luma > 0.05f, "");
    }
    // Rim rises with angle: compare a near-silhouette annulus (70 deg) to an inner one (45 deg) with the ripple off.
    {
        rr::WeaponEffectsFrame f = base_frame(g);
        rr::WeaponShell s = front_shell(0.30f);
        s.amp[0] = 0.f;
        f.shells = &s;
        f.shell_count = 1;
        draw(pass, f);
        float sum70 = 0, sum45 = 0;
        int n70 = 0, n45 = 0;
        const int cx = int(g.w / 2), cy = int(g.h / 2);
        const float R = f.m11 * 100.f / 400.f * g.h * 0.5f;
        for (int a = 0; a < 360; a += 4) {
            const float rad = a * 3.14159265f / 180.f;
            const int p70x = cx + int(std::sin(70 * 3.14159f / 180) * R * std::cos(rad)), p70y = cy + int(std::sin(70 * 3.14159f / 180) * R * std::sin(rad));
            const int p45x = cx + int(std::sin(45 * 3.14159f / 180) * R * std::cos(rad)), p45y = cy + int(std::sin(45 * 3.14159f / 180) * R * std::sin(rad));
            float rgba[4];
            if (p70x >= 0 && p70y >= 0 && p70x < int(g.w) && p70y < int(g.h) && read_pixel(g, UINT(p70x), UINT(p70y), rgba)) { sum70 += rgba[0] + rgba[1] + rgba[2]; ++n70; }
            if (p45x >= 0 && p45y >= 0 && p45x < int(g.w) && p45y < int(g.h) && read_pixel(g, UINT(p45x), UINT(p45y), rgba)) { sum45 += rgba[0] + rgba[1] + rgba[2]; ++n45; }
        }
        const float rim70 = n70 ? sum70 / n70 : 0.f, rim45 = n45 ? sum45 / n45 : 0.f;
        std::printf("RIM at70=%.5f at45=%.5f\n", rim70, rim45);
        report("rim_rises_with_angle", rim70 > rim45, "");
    }
    // Rings present at the hit vs between.
    {
        rr::WeaponEffectsFrame f = base_frame(g);
        rr::WeaponShell s = front_shell(0.05f);
        f.shells = &s;
        f.shell_count = 1;
        draw(pass, f);
        float c[4], e[4];
        read_pixel(g, g.w / 2, g.h / 2, c);
        read_pixel(g, g.w / 2 + 80, g.h / 2, e);
        std::printf("RINGS on=%.4f off=%.4f\n", c[0] + c[1] + c[2], e[0] + e[1] + e[2]);
        report("rings_present_on_absent_off", (c[0] + c[1] + c[2]) > (e[0] + e[1] + e[2]), "");
    }
    // Hull cut: a near hull in front of the shell cuts it to 0.
    {
        draw_lane(g, 10.f, 1.0f); // hull at view depth 10 (in front of the shell at 400) everywhere
        rr::WeaponEffectsFrame f = base_frame(g);
        rr::WeaponShell s = front_shell(0.0f);
        f.shells = &s;
        f.shell_count = 1;
        draw(pass, f);
        const float cut = max_luma(g);
        std::printf("HULL cut_max=%.4f\n", cut);
        report("hull_cut_zero_behind_occluder", cut < 0.05f, "");
        draw_lane(g, 0.f, -1.f);
    }
    // Decal 0 over sky, draws on a hull at its depth.
    {
        rr::WeaponDecal d{};
        d.centre[2] = 400.f;
        d.radius = 60.f;
        d.age_s = 0.05f;
        d.tint[0] = d.tint[1] = d.tint[2] = 1.f;
        d.amp = 4.f;
        rr::WeaponEffectsFrame f = base_frame(g);
        f.decals = &d;
        f.decal_count = 1;
        draw_lane(g, 0.f, -1.f);
        draw(pass, f);
        const float over_sky = max_luma(g);
        draw_lane(g, 400.f, 1.0f); // hull at the decal's view depth everywhere
        draw(pass, f);
        const float on_hull = max_luma(g);
        std::printf("DECAL over_sky=%.4f on_hull=%.4f\n", over_sky, on_hull);
        report("decal_zero_over_sky", over_sky < 0.05f && on_hull > over_sky, "");
        draw_lane(g, 0.f, -1.f);
    }
    // Age law: a young shell draws brighter than one past its ripple life (both at the same hit).
    {
        rr::WeaponEffectsFrame f = base_frame(g);
        rr::WeaponShell young = front_shell(0.0f), old = front_shell(f.look.ripple_s + 0.1f);
        f.shells = &young;
        f.shell_count = 1;
        draw(pass, f);
        const float a0 = max_luma(g);
        f.shells = &old;
        draw(pass, f);
        const float a_old = max_luma(g);
        std::printf("AGE young=%.4f expired=%.4f ripple_s=%.3f seta6_wall_ms=%.0f\n", a0, a_old, f.look.ripple_s,
                    f.look.ripple_s * 1000.f / 6.f);
        report("age_law_young_brighter_than_expired", a0 > a_old, "");
    }
    // Retired owner: shell_count 0 draws nothing.
    {
        rr::WeaponEffectsFrame f = base_frame(g);
        auto res = draw(pass, f);
        std::printf("RETIRED result=%08lx drew=%u max=%.4f\n", res.first, unsigned(res.second.drew), max_luma(g));
        report("retired_owner_draws_nothing", res.first == S_FALSE && !res.second.drew && max_luma(g) < 0.02f, "");
    }
    // Fault once, then recover; then Reset.
    {
        rr::WeaponEffectsFrame f = base_frame(g);
        rr::WeaponShell s = front_shell(0.0f);
        f.shells = &s;
        f.shell_count = 1;
        pass.set_faults(2);
        auto res = draw(pass, f);
        report("fault_reported", FAILED(res.first) && res.second.failed != rr::WeaponEffectsStep::None, "");
        pass.set_faults(0);
        auto res2 = draw(pass, f);
        report("recovers_after_fault", SUCCEEDED(res2.first) && res2.second.drew, "");
        pass.before_reset();
        g.release();                       // only DEFAULT-pool targets are lost across Reset; shaders survive
        D3DPRESENT_PARAMETERS pp2 = pp;
        const HRESULT reset = dev->Reset(&pp2);
        pass.after_reset(reset);
        std::printf("RESET result=%08lx\n", reset);
        const bool mt = SUCCEEDED(reset) && make_target(g, 1920, 1080);
        std::printf("RESET make_target=%u\n", unsigned(mt));
        const HRESULT er = pass.ensure_resources();
        std::printf("RESET ensure=%08lx\n", er);
        // Reset recreated the device, the targets and the pass's programs; resources_ready() is the production
        // contract (the pass draws again from the next armed resolve). A post-reset readback is not re-run here.
        report("reset_recovers", mt && SUCCEEDED(er) && pass.resources_ready(), "");
        draw_lane(g, 0.f, -1.f);
    }
    // Timing: two shells at 1080p and 5120x1440, EVENT-fenced.
    for (auto size : {std::pair<UINT, UINT>{1920, 1080}, std::pair<UINT, UINT>{5120, 1440}}) {
        Target tt;
        if (!make_target(tt, size.first, size.second)) { report("timing_target", false, ""); continue; }
        rr::WeaponEffectsFrame f = base_frame(tt);
        rr::WeaponShell shells[2] = {front_shell(0.1f, 150.f, 400.f), front_shell(0.2f, 400.f, 500.f)};
        f.shells = shells;
        f.shell_count = 2;
        Com<IDirect3DQuery9> ev;
        dev->CreateQuery(D3DQUERYTYPE_EVENT, &ev.p);
        LARGE_INTEGER freq, a, b;
        QueryPerformanceFrequency(&freq);
        double best = 1e30;
        for (int i = 0; i < 60; ++i) {
            dev->BeginScene();
            dev->SetRenderTarget(0, tt.scene_s.p);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
            if (ev.p) { ev.p->Issue(D3DISSUE_END); while (ev.p->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {} }
            QueryPerformanceCounter(&a);
            rr::WeaponEffectsReport r{};
            pass.run(f, &r);
            if (ev.p) { ev.p->Issue(D3DISSUE_END); while (ev.p->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {} }
            QueryPerformanceCounter(&b);
            dev->EndScene();
            const double us = ticks_us(a, b, freq);
            if (us < best) best = us;
        }
        std::printf("TIMING width=%u height=%u shells=2 gpu_us=%.1f\n", size.first, size.second, best);
        tt.release();
    }

    pass.detach();
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
