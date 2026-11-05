// D3D fixture of the weapon-effects shield-hit stage (src/renderer/weapon_effects_pass.cpp, the production
// WeaponEffectsPass and its shell / decal programs; docs/architecture/weapon-effects.md 5.1 acceptance (b)). It creates
// a plain D3D9 HAL device, attaches the pass, and draws shells and decals onto an A16B16G16R16F scene target with a
// controlled four-channel depth lane (.b = view depth, a sentinel .r < 0 = sky), built by a small FP lane shader
// compiled through the game's d3dx9_37.dll (argv[1], run_weapon_effects.py's contract). It reads the FP16 target back
// and reports the structural relations the note lists: the Fresnel rim rises with the view angle, the rings are
// present on and absent off their widths, the shell's hull cut is 0 behind a near occluder, the decal draws on a hull
// and 0 over sky, the shell core keeps >= 0.95 of its value through the production TemporalPass at 0 / 4 / 8 px per
// frame (the engine plumes fixture's flown resolve configuration), the winding cull keeps a mirrored shell, the
// game-time age law (young draws, past the ripple fades, and a 600 ms ripple at SETA 6 measured to end after 100 ms of
// wall time through the production stage builder), a retired owner draws nothing (the builder retires a dying owner's
// record), Reset recovery, the fault cases (a failed draw, the program-create refusal), and the stage cost at 1080p and
// 5120x1440 (an empty fence, two shells and a full-screen shell, each with the winding cull and with CULLMODE NONE as
// the same-run comparison). Every draw and readback runs inside BeginScene/EndScene (native D3D9 refuses a draw outside
// a scene). Never launches the game.
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include "../../src/renderer/weapon_effects_pass.h"
#include "../../src/proxy/weapon_effects_stage.h"
#include "../../src/renderer/temporal_pass.h"
#include "../../src/renderer/temporal_resolve_program.h"
#include "../../src/renderer/hdr_writeback_program.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <vector>

namespace rr = x3m::renderer;
namespace ws = x3m::weapon_effects::stage;
namespace wc = x3m::weapon_effects::core;

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
IDirect3DPixelShader9* motion_ps = nullptr;

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
    // The motion target of an all-unrouted frame (the route's fill sentinel, alpha -1): the resolve's camera-only path.
    const DWORD* mw = nullptr;
    static Com<ID3DXBuffer> mh;
    if (!compile("float4 main(float2 p:VPOS):COLOR0{ return float4(0,0,0,-1); }", "ps_3_0", &mw, &mh.p) ||
        FAILED(dev->CreatePixelShader(mw, &motion_ps)))
        return false;
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
void fence(IDirect3DQuery9* q) {
    if (!q) return;
    q->Issue(D3DISSUE_END);
    const DWORD start = GetTickCount();
    while (q->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE && GetTickCount() - start < 5000) {
    }
}
// The production stage builder's input: one owner in the fixture's own memory (object block and node block), read
// through a reader bounded to those two blocks (engine_memory::read's contract: false outside readable memory).
alignas(16) unsigned char fake_obj[0x100], fake_node[0x200];
bool fake_read(std::uintptr_t a, void* out, std::size_t n) {
    for (const unsigned char* base : {static_cast<const unsigned char*>(fake_obj), static_cast<const unsigned char*>(fake_node)}) {
        const std::uintptr_t b = reinterpret_cast<std::uintptr_t>(base);
        const std::size_t size = base == fake_obj ? sizeof fake_obj : sizeof fake_node;
        if (a >= b && n <= size && a - b <= size - n) {
            std::memcpy(out, reinterpret_cast<const void*>(a), n);
            return true;
        }
    }
    return false;
}
void put32(unsigned char* b, unsigned off, std::uint32_t v) {
    std::memcpy(b + off, &v, 4);
}
std::uint32_t addr(const void* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
// A live owner at view (0, 0, z) with radius r (view units; identity camera, identity rows), and the ring holding one
// stage-owned shield hit on its front (t0 game ms, the header's ripple lifetime).
wc::Ring stage_ring;
void stage_owner(float z, float r, std::uint32_t t0) {
    std::memset(fake_obj, 0, sizeof fake_obj);
    std::memset(fake_node, 0, sizeof fake_node);
    const auto engine = [](float v) { return std::uint32_t(std::int32_t(v / wc::world_scale)); };
    put32(fake_obj, wc::layout::obj_node, addr(fake_node));
    put32(fake_obj, wc::layout::obj_radius, engine(r));
    put32(fake_node, wc::layout::node_handle, 0xd00du);
    put32(fake_node, wc::layout::node_pos + 8, engine(z));
    for (unsigned k = 0; k < 3; ++k) put32(fake_node, wc::layout::node_rot + 16 * k + 4 * k, 0x10000u);
    wc::ring_clear(stage_ring);
    wc::Event* e = wc::ring_acquire(stage_ring);
    e->role = wc::role_shield_hit;
    e->style = wc::shape_slug;
    e->flags = 0;
    e->t0_game_ms = t0;
    e->lifetime_ms = std::uint16_t(wc::ripple_ms);
    e->damage = 1200;
    e->tint_rgb = 0xffffffu | wc::rgb_present;
    e->owner = addr(fake_obj);
    e->node = addr(fake_node);
    e->handle = 0xd00du;
    e->pos[2] = -std::int32_t(engine(r));
    e->radius = std::int32_t(engine(r));
}
ws::Camera identity_camera() {
    ws::Camera c;
    c.r[0] = c.r[4] = c.r[8] = 1.f;
    return c;
}
unsigned ring_live(const wc::Ring& r) {
    unsigned n = 0;
    for (unsigned i = 0; i < wc::ring_capacity; ++i) n += r.slots[i].sequence != 0;
    return n;
}
double halton(unsigned i, unsigned b) {
    double f = 1, r = 0;
    while (i) {
        f /= b;
        r += f * (i % b);
        i /= b;
    }
    return r;
}
std::vector<float> read_luma(Target& t, IDirect3DSurface9* source) {
    std::vector<float> out;
    if (FAILED(dev->GetRenderTargetData(source, t.sysmem.p))) return out;
    D3DLOCKED_RECT lr{};
    if (FAILED(t.sysmem.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return out;
    out.resize(std::size_t(t.w) * t.h);
    for (UINT y = 0; y < t.h; ++y) {
        const auto* row = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) + y * lr.Pitch);
        for (UINT x = 0; x < t.w; ++x)
            out[std::size_t(y) * t.w + x] =
                std::max(half_to_float(row[x * 4]), std::max(half_to_float(row[x * 4 + 1]), half_to_float(row[x * 4 + 2])));
    }
    t.sysmem.p->UnlockRect();
    return out;
}
struct StageContext {
    rr::WeaponEffectsPass* pass;
    rr::WeaponEffectsFrame frame;
    HRESULT result;
};
HRESULT stage_callback(void* context, IDirect3DDevice9*) noexcept {
    auto* c = static_cast<StageContext*>(context);
    rr::WeaponEffectsReport r{};
    c->result = c->pass->run(c->frame, &r);
    return c->result;
}
// The shell's core through the production resolve (the engine plumes fixture's flown configuration: PerPixel with the
// all-unrouted motion target, sentinel camera with strict sky, exit 0.25, far camera + camera gate): 16 frames of
// Halton jitter with the shell moving +x at `speed_px` per frame, once with the stage and once without. The core is the
// current frame's shell pixels at half its peak or more; the ratio is their resolved share (on - off) over their
// current share. -1 when the resolve could not be driven.
double resolve_core(rr::WeaponEffectsPass& pass, Com<IDirect3DTexture9>& motion, Com<IDirect3DSurface9>& motion_s,
                    float speed_px, float* peak_out) {
    constexpr unsigned frames = 16;
    const float z = 400.f, m00 = base_frame(g).m00;
    const float units_per_px = 2.f * z / (m00 * float(g.w));
    std::vector<float> cur[2], res[2];
    for (unsigned on = 0; on < 2; ++on) {
        rr::TemporalPass taa;
        HRESULT hr = taa.initialize(dev, nullptr, reinterpret_cast<const DWORD*>(rr::temporal_resolve_program()), nullptr,
                                    nullptr, reinterpret_cast<const DWORD*>(rr::hdr_writeback_program()));
        if (SUCCEEDED(hr)) hr = taa.configure_far();
        if (FAILED(hr)) {
            std::printf("RESOLVE_FAIL step=initialize result=%08lx\n", hr);
            return -1;
        }
        rr::Output o{};
        float pjx = 0, pjy = 0;
        for (unsigned n = 0; n < frames; ++n) {
            const float jx = float(halton(n % 8 + 1, 2) - .5), jy = float(halton(n % 8 + 1, 3) - .5);
            draw_lane(g, 0.f, -1.f); // sky everywhere
            const float quad[4][4] = {{-1, 1, 0.5f, 1}, {1, 1, 0.5f, 1}, {-1, -1, 0.5f, 1}, {1, -1, 0.5f, 1}};
            dev->BeginScene();
            dev->SetRenderTarget(0, motion_s.p);
            dev->SetVertexDeclaration(quad_decl);
            dev->SetVertexShader(quad_vs);
            dev->SetPixelShader(motion_ps);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(float) * 4);
            dev->SetRenderTarget(0, g.scene_s.p);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
            dev->EndScene();
            rr::WeaponShell shell = front_shell(0.15f, 100.f, z);
            shell.centre[0] = (float(n) - float(frames - 1)) * speed_px * units_per_px;
            StageContext ctx{&pass, base_frame(g), S_FALSE};
            ctx.frame.m20 = 2.f * jx / float(g.w);
            ctx.frame.m21 = -2.f * jy / float(g.h);
            ctx.frame.shells = &shell;
            ctx.frame.shell_count = 1;
            rr::FrameInputs in;
            in.color = g.scene_tex.p;
            in.current_depth = g.lane_tex.p;
            in.width = g.w;
            in.height = g.h;
            in.epoch = 1;
            const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            std::copy(identity, identity + 16, in.clip_to_previous);
            in.current_jitter[0] = jx;
            in.current_jitter[1] = jy;
            in.previous_jitter[0] = pjx;
            in.previous_jitter[1] = pjy;
            in.history_allowed = true;
            in.caller_scene_open = true;
            in.caller_queries_idle = true;
            in.motion = motion.p;
            in.motion_policy = rr::MotionPolicy::PerPixel;
            in.reactive_policy = rr::ReactivePolicy::DerivedFromDepthSentinel;
            in.sentinel_camera = true;
            in.sentinel_strict_sky = true;
            in.sky_history_exit_px = .25f;
            in.far_weight = .985f;
            in.far_d0 = .9995f;
            in.far_inv = 1.f / (.9999f - .9995f);
            in.far_speed_lo = x3::temporal::kFarSpeedLo;
            in.far_speed_hi = x3::temporal::kFarSpeedHi;
            in.thin_region_weight = .97f;
            in.thin_region_relax = 1;
            in.thin_region_camera_gate = true;
            if (on) {
                in.stage_callback = &stage_callback;
                in.stage_context = &ctx;
            }
            dev->BeginScene();
            hr = taa.run(in, &o);
            dev->EndScene();
            if (FAILED(hr) || (on && (!taa.diagnostics().stage_ran || FAILED(ctx.result)))) {
                std::printf("RESOLVE_FAIL step=run frame=%u result=%08lx stage=%08lx ran=%u\n", n, hr, ctx.result,
                            unsigned(taa.diagnostics().stage_ran));
                return -1;
            }
            pjx = jx;
            pjy = jy;
            if (n + 1 == frames) {
                cur[on] = read_luma(g, g.scene_s.p);
                Com<IDirect3DSurface9> resolved;
                if (!o.color || FAILED(o.color->GetSurfaceLevel(0, &resolved.p))) return -1;
                res[on] = read_luma(g, resolved.p);
            }
        }
        taa.shutdown();
    }
    if (cur[0].empty() || cur[1].empty() || res[0].empty() || res[1].empty()) return -1;
    float peak = 0.f;
    for (std::size_t i = 0; i < cur[1].size(); ++i) peak = std::max(peak, cur[1][i] - cur[0][i]);
    double num = 0, den = 0;
    for (std::size_t i = 0; i < cur[1].size(); ++i) {
        const double c = double(cur[1][i]) - double(cur[0][i]);
        if (c >= .5 * double(peak)) {
            num += double(res[1][i]) - double(res[0][i]);
            den += c;
        }
    }
    *peak_out = peak;
    return den > 0 ? num / den : 0;
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
        // The winding cull (the outward faces kept from det A's sign): a mirrored shell (det < 0) draws its front too.
        s.axes[0] = -s.axes[0];
        auto mirrored = draw(pass, f);
        const float luma_mirrored = max_luma(g);
        std::printf("SHELL sky_max_luma=%.4f mirrored_max_luma=%.4f\n", luma, luma_mirrored);
        report("shell_core_positive", SUCCEEDED(res.first) && res.second.shells == 1 && luma > 0.05f, "");
        report("winding_mirrored_axes_draw", SUCCEEDED(mirrored.first) && mirrored.second.shells == 1 &&
                                                 luma_mirrored > 0.5f * luma && luma_mirrored < 2.f * luma, "");
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
        report("age_law_young_brighter_than_expired", a0 > a_old, "");
        // SETA 6, measured: the game clock advances six game ms per wall ms (QueryPerformanceCounter); every frame the
        // production builder ages the ring's one hit in game time and the pass draws it, fenced like a frame, until the
        // builder has nothing to draw. The ripple's 600 game ms end after 100 wall ms (within one frame).
        stage_owner(400.f, 100.f, 0);
        Com<IDirect3DQuery9> q;
        dev->CreateQuery(D3DQUERYTYPE_EVENT, &q.p);
        LARGE_INTEGER freq{}, start{}, now{};
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
        rr::WeaponShell shells[wc::max_shells];
        rr::WeaponDecal decals[wc::max_decals];
        double wall_ms = 0, last_ms = 0, max_frame_ms = 0, last_drawn_ms = 0;
        unsigned frames = 0;
        for (;;) {
            QueryPerformanceCounter(&now);
            wall_ms = ticks_us(start, now, freq) * 1e-3;
            const std::uint32_t game = std::uint32_t(wall_ms * 6.0);
            const ws::Counts n = ws::build(stage_ring, game, &fake_read, identity_camera(), ws::Tuning{}, shells,
                                           wc::max_shells, decals, wc::max_decals);
            max_frame_ms = std::max(max_frame_ms, wall_ms - last_ms);
            last_ms = wall_ms;
            if (!n.shells || wall_ms > 2000.0) break;
            rr::WeaponEffectsFrame sf = base_frame(g);
            sf.shells = shells;
            sf.shell_count = n.shells;
            draw(pass, sf);
            fence(q.p);
            ++frames;
            last_drawn_ms = wall_ms;
        }
        std::printf("AGE young=%.4f expired=%.4f ripple_s=%.3f seta6_wall_ms=%.2f last_drawn_ms=%.2f frames=%u max_frame_ms=%.3f\n",
                    a0, a_old, f.look.ripple_s, wall_ms, last_drawn_ms, frames, max_frame_ms);
        report("age_law_seta6_100ms_wall", frames > 0 && last_drawn_ms <= 100.2 && wall_ms >= 100.0 &&
                                                wall_ms <= 100.2 + max_frame_ms + 0.5, "");
    }
    // Retired owner, through the production builder: the live owner's hit gives one shell that draws; once the owner
    // carries the kill routine's dying bit the builder retires its record (the ring is empty) and nothing draws.
    {
        rr::WeaponShell shells[wc::max_shells];
        rr::WeaponDecal decals[wc::max_decals];
        stage_owner(400.f, 100.f, 1000);
        const ws::Counts live = ws::build(stage_ring, 1050, &fake_read, identity_camera(), ws::Tuning{}, shells,
                                          wc::max_shells, decals, wc::max_decals);
        rr::WeaponEffectsFrame f = base_frame(g);
        f.shells = shells;
        f.shell_count = live.shells;
        auto drawn = draw(pass, f);
        const float live_max = max_luma(g);
        put32(fake_obj, wc::layout::obj_flags, wc::layout::obj_dying);
        const ws::Counts dead = ws::build(stage_ring, 1050, &fake_read, identity_camera(), ws::Tuning{}, shells,
                                          wc::max_shells, decals, wc::max_decals);
        f.shell_count = dead.shells;
        f.decal_count = dead.decals;
        auto res = draw(pass, f);
        const float dead_max = max_luma(g);
        std::printf("RETIRED live_shells=%u live_drew=%u live_max=%.4f shells=%u decals=%u retired=%u ring_live=%u result=%08lx drew=%u max=%.4f\n",
                    live.shells, unsigned(drawn.second.drew), live_max, dead.shells, dead.decals, dead.retired,
                    ring_live(stage_ring), res.first, unsigned(res.second.drew), dead_max);
        report("stage_build_live_owner_draws", live.shells == 1 && drawn.second.drew && live_max > 0.05f, "");
        report("retired_owner_draws_nothing", dead.shells == 0 && dead.decals == 0 && dead.retired == 1 &&
                                                  ring_live(stage_ring) == 0 && res.first == S_FALSE && !res.second.drew &&
                                                  dead_max < 0.02f, "");
    }
    // The core through the production resolve at 0 / 4 / 8 px per frame.
    {
        Com<IDirect3DTexture9> motion;
        Com<IDirect3DSurface9> motion_s;
        const bool made = SUCCEEDED(dev->CreateTexture(g.w, g.h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F,
                                                       D3DPOOL_DEFAULT, &motion.p, nullptr)) &&
                          SUCCEEDED(motion.p->GetSurfaceLevel(0, &motion_s.p));
        for (const float speed : {0.f, 4.f, 8.f}) {
            float peak = 0.f;
            const double ratio = made ? resolve_core(pass, motion, motion_s, speed, &peak) : -1.0;
            std::printf("RESOLVE width=%u height=%u speed_px=%.0f frames=16 core=%.4f peak=%.3f\n", g.w, g.h, double(speed),
                        ratio, double(peak));
            char label[48];
            std::snprintf(label, sizeof label, "resolve_core_%.0fpx_%u", double(speed), g.w);
            report(label, ratio >= 0.95, "");
        }
        draw_lane(g, 0.f, -1.f);
    }
    // Program creation refused (fault bit 3): attach refuses with program_create and keeps no object; run refuses at once.
    {
        rr::WeaponEffectsPass refused;
        refused.set_faults(8);
        const HRESULT hr = refused.attach(dev, *reinterpret_cast<void* const* const*>(dev), caps, adapter_format);
        rr::WeaponEffectsFrame f = base_frame(g);
        rr::WeaponShell s = front_shell(0.0f);
        f.shells = &s;
        f.shell_count = 1;
        rr::WeaponEffectsReport rep{};
        const HRESULT run = refused.run(f, &rep);
        std::printf("REFUSED result=%08lx enabled=%u reason=%s references=%u run=%08lx calls=%u\n", hr,
                    unsigned(refused.caps().enabled), refused.caps().reason, refused.references(), run, rep.calls);
        report("program_create_refused", FAILED(hr) && !refused.caps().enabled &&
                                             !std::strcmp(refused.caps().reason, "program_create") &&
                                             refused.references() == 0 && run == E_INVALIDARG && rep.calls == 0, "");
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
    // Timing, EVENT-fenced (best of 60): an empty fence (the fence's own round trip), two shells (204 / 544 px radius
    // at 1080p) and a shell projecting past every screen edge, each with the winding cull and with CULLMODE NONE (fault
    // bit 4, the former draw) in the same run, at 1080p and 5120x1440.
    for (auto size : {std::pair<UINT, UINT>{1920, 1080}, std::pair<UINT, UINT>{5120, 1440}}) {
        Target tt;
        if (!make_target(tt, size.first, size.second)) { report("timing_target", false, ""); continue; }
        rr::WeaponShell two[2] = {front_shell(0.1f, 150.f, 400.f), front_shell(0.2f, 400.f, 500.f)};
        rr::WeaponShell full = front_shell(0.1f, 100.f, 115.f);
        struct Case {
            const char* name;
            const rr::WeaponShell* shells;
            unsigned count, faults;
        } cases[] = {{"fence", nullptr, 0, 0}, {"two", two, 2, 0}, {"two_cull_none", two, 2, 16},
                     {"fullscreen", &full, 1, 0}, {"fullscreen_cull_none", &full, 1, 16}};
        Com<IDirect3DQuery9> ev;
        dev->CreateQuery(D3DQUERYTYPE_EVENT, &ev.p);
        LARGE_INTEGER freq, a, b;
        QueryPerformanceFrequency(&freq);
        for (const Case& c : cases) {
            rr::WeaponEffectsFrame f = base_frame(tt);
            f.shells = c.shells;
            f.shell_count = c.count;
            pass.set_faults(c.faults);
            double best = 1e30;
            unsigned drawn = 0;
            for (int i = 0; i < 60; ++i) {
                dev->BeginScene();
                dev->SetRenderTarget(0, tt.scene_s.p);
                dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
                fence(ev.p);
                QueryPerformanceCounter(&a);
                rr::WeaponEffectsReport r{};
                if (c.count) pass.run(f, &r);
                fence(ev.p);
                QueryPerformanceCounter(&b);
                dev->EndScene();
                drawn = r.shells;
                const double us = ticks_us(a, b, freq);
                if (us < best) best = us;
            }
            pass.set_faults(0);
            std::printf("TIMING width=%u height=%u case=%s shells=%u drawn=%u cull=%s gpu_us=%.1f\n", size.first,
                        size.second, c.name, c.count, drawn, !c.count ? "-" : c.faults ? "none" : "winding", best);
        }
        tt.release();
    }

    pass.detach();
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
