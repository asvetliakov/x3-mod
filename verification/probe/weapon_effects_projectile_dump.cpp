// The re-shades' look frames (the bullet re-shade, docs/reverse-engineering/fl/bullet-draw-batch.md sections 6 and 7;
// the muzzle flash re-shade, docs/architecture/muzzle-flash-look.md): a standalone native D3D9 program beside
// weapon_effects_fixture.cpp (whose record binds its source), built and run only by run_weapon_effects.py --host native
// --dump DIR --dump-case reshade|muzzle. It creates a plain HAL device and draws the game-draw stand-ins vanilla and
// re-shaded with the production programs; each frame's FP16 scene is written as linear RGB PFM <dir>\<name>.pfm, which
// the runner encodes as PNG. No check, no record. Never launches the game. The custom projectile stage's frames left
// with the stage on 2026-11-13 (docs/archive/effects-look-2026-11-13-custom-projectile/index.md).
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include "../../src/renderer/weapon_effects_pass.h"
#include "../../src/proxy/weapon_projectile_family.h"
#include "weapon_bullet_reshade_fixture.h"
#include "weapon_muzzle_reshade_fixture.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
// ------------------------------------------------------------------ bullet re-shade dump (--dump DIR --dump-case reshade)
// The bullet re-shade's look evidence (bullet-draw-batch.md section 6; weapon_bullet_reshade_fixture.h): the IRE bolt
// drawn the way the game's INSTANCE_BULLETS pass draws it (stand-in vs_1_1 / ps_1_1 doing the game programs' work,
// ONE / INVSRCCOLOR, Z test, no Z write) into the 1920x1080 FP16 scene at the fixture's projection (m00 1.6), once
// vanilla and once with the production re-shade program swapped in, at 50 m (near) and 1500 m (far), its axis 25
// degrees out of the screen plane and its fins rolled 20 degrees, on the dark background (black) and a bright nebula
// stand-in (a flat linear 0.30 / 0.22 / 0.38). The mesh and texture are the game's IRE bolt and fx_bullets_diff when
// run_weapon_effects.py prepared them from the local lab assets (untracked), else the procedural stand-ins. Each
// output PFM <dir>\fixture-ire-{vanilla,reshade}-{near,far}.pfm is the two backgrounds' crops side by side (near
// 640x256, far 64x32 around the bolt). The geometric size (projectile-look.md "Geometric size (2026-11-10)"): each body's
// UV layout from <assets>\<name>_layout.txt (the generator's uv_layout of the lab mesh as the eight 16.16 table words,
// run_weapon_effects.py; absent: no layout) goes through bullet_reshade_core.h layout_constants with the baked keys (the
// `reshade` frames) and with the proposal rs_head 0.5 / rs_width 0.6 (the `reshade-...-h050-w060` frames).
// DUMP_RESHADE rows; no check, no record.
namespace rf = reshade_fx;
// The eight layout words of <dir>\<name>_layout.txt into w; false (w the zero words: no layout) when absent.
bool load_layout(const char* dir, const char* name, std::int32_t w[8]) {
    for (unsigned k = 0; k < 8; ++k) w[k] = 0;
    if (!dir || !*dir) return false;
    char path[MAX_PATH];
    std::snprintf(path, sizeof path, "%s\\%s_layout.txt", dir, name);
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    bool ok = true;
    for (unsigned k = 0; k < 8 && ok; ++k) {
        long v = 0;
        ok = std::fscanf(f, "%ld", &v) == 1;
        w[k] = std::int32_t(v);
    }
    std::fclose(f);
    if (!ok)
        for (unsigned k = 0; k < 8; ++k) w[k] = 0;
    return ok;
}
// The proposal frames' keys (not baked: the user tunes in flight).
constexpr float proposal_head = 0.5f, proposal_width = 0.6f;
bool load_assets(const char* dir, rf::Mesh* mesh, std::vector<unsigned char>* rgba, unsigned* side,
                 float* p99 = nullptr) {
    if (!dir || !*dir) return false;
    char path[MAX_PATH];
    std::snprintf(path, sizeof path, "%s\\ire_mesh.txt", dir);
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    rf::Mesh m;
    float q[5];
    while (m.n < 66 && std::fscanf(f, "%f %f %f %f %f", &q[0], &q[1], &q[2], &q[3], &q[4]) == 5) {
        for (unsigned k = 0; k < 5; ++k) m.p[m.n][k] = q[k];
        ++m.n;
    }
    std::fclose(f);
    std::snprintf(path, sizeof path, "%s\\ire_tex.rgba", dir);
    f = std::fopen(path, "rb");
    if (!f || m.n != 66) {
        if (f) std::fclose(f);
        return false;
    }
    std::vector<unsigned char> data(512u * 512u * 4u);
    const bool read = std::fread(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    if (!read) return false;
    *mesh = m;
    rgba->swap(data);
    *side = 512;
    if (p99) { // the texture's level-0 luminance p99 (0: none, the IRE's 2.75)
        *p99 = 0.f;
        std::snprintf(path, sizeof path, "%s\\ire_tex_p99.txt", dir);
        if ((f = std::fopen(path, "rb")) != nullptr) {
            if (std::fscanf(f, "%f", p99) != 1) *p99 = 0.f;
            std::fclose(f);
        }
    }
    return true;
}
// edge: an rs_edge override (null or empty: the baked tune); with it the re-shaded frames only, named
// fixture-ire-reshade-<range>-e<edge x 100, three digits> (the vanilla frames are the same as without it).
int reshade_dump(const char* dir, const char* d3dx_path, const char* assets, const char* edge = nullptr) {
    HMODULE d3dx = LoadLibraryA(d3dx_path && *d3dx_path ? d3dx_path : "d3dx9_37.dll");
    using CompileFn = HRESULT(WINAPI*)(LPCSTR, UINT, const void*, void*, LPCSTR, LPCSTR, DWORD, ID3DXBuffer**,
                                       ID3DXBuffer**, void*);
    const auto compile_fn = d3dx ? reinterpret_cast<CompileFn>(GetProcAddress(d3dx, "D3DXCompileShader")) : nullptr;
    const auto compile = [&](const char* src, const char* profile, ID3DXBuffer** out) {
        ID3DXBuffer* err = nullptr;
        const HRESULT hr = compile_fn(src, UINT(std::strlen(src)), nullptr, nullptr, "main", profile, 0, out, &err, nullptr);
        if (err) err->Release();
        return SUCCEEDED(hr) && *out;
    };
    ID3DXBuffer *vs_code = nullptr, *ps_code = nullptr;
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> vanilla, ours;
    Com<IDirect3DVertexDeclaration9> decl;
    const bool created = compile_fn && compile(rf::vs_source, "vs_1_1", &vs_code) &&
                         rf::assemble(d3dx, rf::vanilla_ps_asm, &ps_code) &&
                         SUCCEEDED(dev->CreateVertexShader(static_cast<const DWORD*>(vs_code->GetBufferPointer()), &vs.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(static_cast<const DWORD*>(ps_code->GetBufferPointer()), &vanilla.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(rf::reshade_program, &ours.p)) &&
                         SUCCEEDED(dev->CreateVertexDeclaration(rf::declaration(), &decl.p));
    if (vs_code) vs_code->Release();
    if (ps_code) ps_code->Release();
    ProjTarget t;
    if (!created || !make_proj_target(t, 1920, 1080)) {
        std::printf("DUMP_FAIL reason=reshade_programs\nRESULT FAIL checks=1 failures=1\n");
        return 1;
    }
    CreateDirectoryA(dir, nullptr);
    rf::Mesh mesh;
    std::vector<unsigned char> rgba;
    unsigned side = 0;
    float ire_p99 = 0.f;
    const bool real = load_assets(assets, &mesh, &rgba, &side, &ire_p99);
    if (!real) {
        mesh = rf::standin_mesh();
        rgba = rf::standin_texture(&side);
    }
    Com<IDirect3DTexture9> tex;
    tex.p = rf::make_texture(dev, rgba.data(), side);
    Com<IDirect3DVertexBuffer9> vb;
    if (!tex.p || FAILED(dev->CreateVertexBuffer(66 * sizeof(rf::Vertex), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                                 &vb.p, nullptr))) {
        std::printf("DUMP_FAIL reason=reshade_resources\nRESULT FAIL checks=1 failures=1\n");
        return 1;
    }
    const auto tune = pf::baked;
    float constants[4];
    x3m::weapon_effects::reshade::constants(tune, constants);
    const bool edge_override = edge && *edge;
    char suffix[16] = "";
    if (edge_override) {
        auto t2 = tune;
        t2.rs_edge = std::strtof(edge, nullptr);
        x3m::weapon_effects::reshade::constants(t2, constants); // clamped like the tuning file
        std::snprintf(suffix, sizeof suffix, "-e%03d", int(constants[1] * 100.f + 0.5f));
    }
    const float app_block[12] = {}; // the application's c29..c31 put back after each re-shaded draw
    // c31.w as production uploads it (bullet_reshade_core.h texture_scale of the bound texture's table p99; the
    // stand-in texture has no entry: the IRE constant)
    const auto p99_q = [](float p99) {
        return std::uint16_t(p99 > 0.f && p99 <= 1.f ? p99 * 65535.f + 0.5f : 0.f);
    };
    constants[3] = x3m::weapon_effects::reshade::texture_scale(p99_q(ire_p99));
    float rows[16];
    rf::clip_rows(1.6f, 1.6f * 1920.f / 1080.f, rows);
    // the layout constants of a body's words at the baked keys (kept) and at the proposal
    auto proposal = tune;
    proposal.rs_head = proposal_head;
    proposal.rs_width = proposal_width;
    const auto layouts = [&](const std::int32_t* w, bool have, float kept[8], float shrunk[8]) {
        x3m::weapon_effects::reshade::layout_constants(have ? w : nullptr, tune, kept);
        x3m::weapon_effects::reshade::layout_constants(have ? w : nullptr, proposal, shrunk);
    };
    std::int32_t ire_words[8];
    const bool ire_layout = real && load_layout(assets, "ire", ire_words);
    float ire_kept[8], ire_shrunk[8];
    layouts(ire_words, ire_layout, ire_kept, ire_shrunk);
    std::printf("DUMP_RESHADE assets=%s mesh_vertices=%u texture=%u metres_per_body_unit=%.3f bolt_length_m=%.1f gain=%.3g "
                "edge=%.3g core=%.3g normaliser=%.4g texture_p99=%.4f layout=%u rs_head=%.3g rs_width=%.3g "
                "proposal_rs_head=%.3g proposal_rs_width=%.3g\n",
                real ? "game" : "standin", mesh.n, side, double(rf::metres_per_body_unit),
                double(rf::metres_per_body_unit * 2.f * 0.2473f), double(constants[0]), double(constants[1]),
                double(constants[2]), double(constants[3]), double(ire_p99), unsigned(ire_layout), double(tune.rs_head),
                double(tune.rs_width), double(proposal_head), double(proposal_width));
    dev->SetRenderTarget(0, t.scene_s.p);
    dev->SetDepthStencilSurface(t.depth_s.p);
    const struct { const char* name; float distance; UINT cw, ch; } ranges[2] = {{"near", 50.f, 640, 256}, {"far", 1500.f, 64, 32}};
    int code = 0;
    for (const auto& r : ranges) {
        void* p = nullptr;
        if (FAILED(vb.p->Lock(0, 0, &p, D3DLOCK_DISCARD))) return 1;
        const float centre[3] = {0.f, 0.f, r.distance};
        rf::place(mesh, centre, 0.4363f, 0.3491f, 255, static_cast<rf::Vertex*>(p));
        vb.p->Unlock();
        // shade 0 vanilla, 1 the baked keys, 2 the proposal (-h050-w060; not with an rs_edge override)
        for (unsigned shade = edge_override ? 1u : 0u; shade < (edge_override ? 2u : 3u); ++shade) {
            const char* const tag = shade == 2 ? "-h050-w060" : "";
            const float* const layout = shade == 2 ? ire_shrunk : ire_kept;
            const UINT ow = r.cw * 2, oh = r.ch;
            std::vector<float> out(std::size_t(ow) * oh * 3, 0.f);
            double sums[2]{};
            float peaks[2]{};
            for (unsigned bg = 0; bg < 2; ++bg) {
                dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                           bg ? D3DCOLOR_ARGB(0, 77, 56, 97) : D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
                dev->BeginScene();
                rf::bind_pass(dev, vs.p, decl.p, tex.p, rows, true);
                dev->SetStreamSource(0, vb.p, 0, sizeof(rf::Vertex));
                dev->SetPixelShader(vanilla.p);
                bool restored = true;
                const HRESULT hr = shade ? rf::reshaded_draw(dev, ours.p, vanilla.p, constants, app_block, 22, &restored,
                                                             layout)
                                         : dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 22);
                dev->EndScene();
                if (FAILED(hr) || !restored || FAILED(dev->GetRenderTargetData(t.scene_s.p, t.scene_sys.p))) code = 1;
                D3DLOCKED_RECT lr{};
                if (FAILED(t.scene_sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return 1;
                const UINT x0 = 960 - r.cw / 2, y0 = 540 - r.ch / 2;
                const float base = bg ? 0.2126f * 77.f / 255.f + 0.7152f * 56.f / 255.f + 0.0722f * 97.f / 255.f : 0.f;
                for (UINT y = 0; y < r.ch; ++y) {
                    const auto* src = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) +
                                                                            (y0 + y) * lr.Pitch) + std::size_t(x0) * 4;
                    for (UINT x = 0; x < r.cw; ++x) {
                        float* d = &out[(std::size_t(y) * ow + bg * r.cw + x) * 3];
                        for (unsigned k = 0; k < 3; ++k) d[k] = half_to_float(src[x * 4 + k]);
                        const float l = 0.2126f * d[0] + 0.7152f * d[1] + 0.0722f * d[2] - base;
                        sums[bg] += l;
                        peaks[bg] = l > peaks[bg] ? l : peaks[bg];
                    }
                }
                t.scene_sys.p->UnlockRect();
            }
            char path[MAX_PATH];
            std::snprintf(path, sizeof path, "%s\\fixture-ire-%s-%s%s%s.pfm", dir, shade ? "reshade" : "vanilla", r.name,
                          suffix, tag);
            std::FILE* f = std::fopen(path, "wb");
            bool written = f != nullptr;
            if (f) {
                std::fprintf(f, "PF\n%u %u\n-1.0\n", ow, oh);
                for (UINT y = oh; y-- > 0;)
                    written = written && std::fwrite(&out[std::size_t(y) * ow * 3], sizeof(float), std::size_t(ow) * 3, f) ==
                                             std::size_t(ow) * 3;
                std::fclose(f);
            }
            if (!written) code = 1;
            std::printf("DUMP_RESHADE name=fixture-ire-%s-%s%s%s distance_m=%.0f edge=%.3g crop=%ux%u dark_sum=%.3f "
                        "dark_peak=%.4f nebula_sum=%.3f nebula_peak=%.4f written=%u\n",
                        shade ? "reshade" : "vanilla", r.name, suffix, tag, double(r.distance), double(constants[1]), r.cw,
                        r.ch, sums[0], double(peaks[0]), sums[1], double(peaks[1]), unsigned(written));
        }
    }
    // The other bodies the batch draws, at the same (baked) values, for judging whether one set serves every family:
    // <assets>\family_<name>.txt (a "vertices side p99" line, then x y z u v per vertex, body units as the IRE's) and
    // family_<name>.rgba (side x side RGBA), prepared by run_weapon_effects.py from the lab assets; 50 m, the IRE's pose,
    // dark background only, vanilla and re-shaded: fixture-<name>-{vanilla,reshade}-near.pfm (640x256). Absent files: no
    // frame and one DUMP_RESHADE family=<name> missing=1 row.
    if (!edge_override && assets && *assets) {
        static const char* const families[3] = {"pac", "hept", "ppc"};
        for (const char* name : families) {
            char path[MAX_PATH];
            std::snprintf(path, sizeof path, "%s\\family_%s.txt", assets, name);
            std::FILE* f = std::fopen(path, "rb");
            unsigned count = 0, fside = 0;
            float fp99 = 0.f;
            std::vector<float> pts;
            if (f && std::fscanf(f, "%u %u %f", &count, &fside, &fp99) == 3 && count && count <= 4096 && fside &&
                fside <= 2048) {
                pts.resize(std::size_t(count) * 5);
                for (unsigned i = 0; i < count * 5; ++i)
                    if (std::fscanf(f, "%f", &pts[i]) != 1) {
                        pts.clear();
                        break;
                    }
            }
            if (f) std::fclose(f);
            std::vector<unsigned char> frgba(std::size_t(fside) * fside * 4);
            std::snprintf(path, sizeof path, "%s\\family_%s.rgba", assets, name);
            f = std::fopen(path, "rb");
            const bool read = f && !pts.empty() && std::fread(frgba.data(), 1, frgba.size(), f) == frgba.size();
            if (f) std::fclose(f);
            if (!read) {
                std::printf("DUMP_RESHADE family=%s missing=1\n", name);
                continue;
            }
            Com<IDirect3DTexture9> ftex;
            ftex.p = rf::make_texture(dev, frgba.data(), fside);
            Com<IDirect3DVertexBuffer9> fvb;
            if (!ftex.p || FAILED(dev->CreateVertexBuffer(count * sizeof(rf::Vertex), D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                                          &fvb.p, nullptr))) {
                code = 1;
                continue;
            }
            void* p = nullptr;
            if (FAILED(fvb.p->Lock(0, 0, &p, 0))) return 1;
            {
                // rf::place's pose for any vertex count: the centre at 50 m, the axis yawed 25 degrees, rolled 20
                auto* v = static_cast<rf::Vertex*>(p);
                const float yaw = 0.4363f, roll = 0.3491f, s = rf::metres_per_body_unit;
                const float cy = std::cos(yaw), sy = std::sin(yaw), cr = std::cos(roll), sr = std::sin(roll);
                for (unsigned i = 0; i < count; ++i) {
                    const float* q = &pts[std::size_t(i) * 5];
                    const float x0 = q[0] * s, y0 = q[1] * s, z0 = q[2] * s;
                    const float x1 = cr * x0 - sr * y0, y1 = sr * x0 + cr * y0;
                    v[i] = rf::Vertex{z0 * cy - x1 * sy, y1, 50.f + z0 * sy + x1 * cy, q[3], q[4], 0xff000000u};
                }
            }
            fvb.p->Unlock();
            float fconstants[4] = {constants[0], constants[1], constants[2],
                                   x3m::weapon_effects::reshade::texture_scale(p99_q(fp99))};
            std::int32_t fwords[8];
            const bool flayout = load_layout(assets, name, fwords);
            float fkept[8], fshrunk[8];
            layouts(fwords, flayout, fkept, fshrunk);
            std::vector<float> batch_out[2]; // the batch frames (vanilla, re-shaded) for the lone path's comparison
            for (unsigned shade = 0; shade < 3; ++shade) { // vanilla, the baked keys, the proposal (-h050-w060)
                const char* const tag = shade == 2 ? "-h050-w060" : "";
                const UINT cw = 640, ch = 256;
                std::vector<float> out(std::size_t(cw) * ch * 3, 0.f);
                double sum = 0.0;
                float peak = 0.f;
                dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
                dev->BeginScene();
                rf::bind_pass(dev, vs.p, decl.p, ftex.p, rows, true);
                dev->SetStreamSource(0, fvb.p, 0, sizeof(rf::Vertex));
                dev->SetPixelShader(vanilla.p);
                bool restored = true;
                const HRESULT hr = shade ? rf::reshaded_draw(dev, ours.p, vanilla.p, fconstants, app_block, count / 3,
                                                             &restored, shade == 2 ? fshrunk : fkept)
                                         : dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, count / 3);
                dev->EndScene();
                if (FAILED(hr) || !restored || FAILED(dev->GetRenderTargetData(t.scene_s.p, t.scene_sys.p))) code = 1;
                D3DLOCKED_RECT lr{};
                if (FAILED(t.scene_sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return 1;
                const UINT x0 = 960 - cw / 2, y0 = 540 - ch / 2;
                for (UINT y = 0; y < ch; ++y) {
                    const auto* src = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) +
                                                                            (y0 + y) * lr.Pitch) + std::size_t(x0) * 4;
                    for (UINT x = 0; x < cw; ++x) {
                        float* d = &out[(std::size_t(y) * cw + x) * 3];
                        for (unsigned k = 0; k < 3; ++k) d[k] = half_to_float(src[x * 4 + k]);
                        const float l = 0.2126f * d[0] + 0.7152f * d[1] + 0.0722f * d[2];
                        sum += l;
                        peak = l > peak ? l : peak;
                    }
                }
                t.scene_sys.p->UnlockRect();
                std::snprintf(path, sizeof path, "%s\\fixture-%s-%s-near%s.pfm", dir, name, shade ? "reshade" : "vanilla",
                              tag);
                f = std::fopen(path, "wb");
                bool written = f != nullptr;
                if (f) {
                    std::fprintf(f, "PF\n%u %u\n-1.0\n", cw, ch);
                    for (UINT y = ch; y-- > 0;)
                        written = written && std::fwrite(&out[std::size_t(y) * cw * 3], sizeof(float), std::size_t(cw) * 3, f) ==
                                                 std::size_t(cw) * 3;
                    std::fclose(f);
                }
                if (!written) code = 1;
                std::printf("DUMP_RESHADE name=fixture-%s-%s-near%s family=%s vertices=%u texture=%u texture_p99=%.4f "
                            "normaliser=%.4g layout=%u distance_m=50 crop=%ux%u dark_sum=%.3f dark_peak=%.4f written=%u\n",
                            name, shade ? "reshade" : "vanilla", tag, name, count, fside, double(fp99),
                            double(fconstants[3]), unsigned(flayout), cw, ch, sum, double(peak), unsigned(written));
                if (shade < 2) batch_out[shade].swap(out);
            }
            // The lone path (bullet-draw-batch.md section 7) for the PPC: the same body, pose and texture drawn the way
            // the material submit draws a bolt the batch does not take (the effects DEFAULT pair's stand-ins, indexed from
            // a static buffer, the material's ONE / INVSRCCOLOR, g_AlphaValue 1, the identity texture and colour
            // matrices), vanilla and re-shaded with the production lone program through the plain sequence (the baked
            // keys, the body's layout): fixture-ppc-lone-{vanilla,reshade}-near.pfm, each with its batch twin's figures
            // and the largest per-pixel difference to it.
            if (!std::strcmp(name, "ppc")) {
                ID3DXBuffer *lvs_code = nullptr, *lps_code = nullptr;
                Com<IDirect3DVertexShader9> lvs;
                Com<IDirect3DPixelShader9> lvanilla, lours;
                Com<IDirect3DVertexDeclaration9> ldecl;
                Com<IDirect3DVertexBuffer9> lvb;
                Com<IDirect3DIndexBuffer9> lib;
                bool lok = compile(rf::lone_vs_source, "vs_2_0", &lvs_code) && compile(rf::lone_ps_source, "ps_2_0", &lps_code) &&
                           SUCCEEDED(dev->CreateVertexShader(static_cast<const DWORD*>(lvs_code->GetBufferPointer()), &lvs.p)) &&
                           SUCCEEDED(dev->CreatePixelShader(static_cast<const DWORD*>(lps_code->GetBufferPointer()), &lvanilla.p)) &&
                           SUCCEEDED(dev->CreatePixelShader(rf::lone_program, &lours.p)) &&
                           SUCCEEDED(dev->CreateVertexDeclaration(rf::lone_declaration(), &ldecl.p)) && count <= 65535u &&
                           SUCCEEDED(dev->CreateVertexBuffer(count * sizeof(rf::LoneVertex), D3DUSAGE_WRITEONLY, 0,
                                                             D3DPOOL_MANAGED, &lvb.p, nullptr)) &&
                           SUCCEEDED(dev->CreateIndexBuffer(count * sizeof(std::uint16_t), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16,
                                                            D3DPOOL_MANAGED, &lib.p, nullptr));
                if (lvs_code) lvs_code->Release();
                if (lps_code) lps_code->Release();
                void* lp = nullptr;
                if (lok && SUCCEEDED(lvb.p->Lock(0, 0, &lp, 0))) { // the batch's placement above, without the colour
                    auto* d = static_cast<rf::LoneVertex*>(lp);
                    const float yaw = 0.4363f, roll = 0.3491f, s = rf::metres_per_body_unit;
                    const float cy = std::cos(yaw), sy = std::sin(yaw), cr = std::cos(roll), sr = std::sin(roll);
                    for (unsigned i = 0; i < count; ++i) {
                        const float* q = &pts[std::size_t(i) * 5];
                        const float x0 = q[0] * s, y0 = q[1] * s, z0 = q[2] * s;
                        const float x1 = cr * x0 - sr * y0, y1 = sr * x0 + cr * y0;
                        d[i] = rf::LoneVertex{z0 * cy - x1 * sy, y1, 50.f + z0 * sy + x1 * cy, q[3], q[4]};
                    }
                    lvb.p->Unlock();
                } else
                    lok = false;
                if (lok && SUCCEEDED(lib.p->Lock(0, 0, &lp, 0))) {
                    auto* d = static_cast<std::uint16_t*>(lp);
                    for (unsigned i = 0; i < count; ++i) d[i] = std::uint16_t(i);
                    lib.p->Unlock();
                } else
                    lok = false;
                if (!lok) {
                    std::printf("DUMP_RESHADE name=fixture-ppc-lone failed=1\n");
                    code = 1;
                    continue;
                }
                for (unsigned shade = 0; shade < 2; ++shade) {
                    const UINT cw = 640, ch = 256;
                    std::vector<float> out(std::size_t(cw) * ch * 3, 0.f);
                    double sum = 0.0;
                    float peak = 0.f, diff = 0.f;
                    dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
                    dev->BeginScene();
                    rf::bind_lone_pass(dev, lvs.p, ldecl.p, ftex.p, rows, true, 1.f);
                    dev->SetStreamSource(0, lvb.p, 0, sizeof(rf::LoneVertex));
                    dev->SetIndices(lib.p);
                    dev->SetPixelShader(lvanilla.p);
                    bool restored = true;
                    const HRESULT hr = shade ? rf::lone_reshaded_draw(dev, lours.p, lvanilla.p, fconstants, app_block, count,
                                                                      count / 3, &restored, fkept)
                                             : dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, count, 0, count / 3);
                    dev->EndScene();
                    dev->SetIndices(nullptr);
                    if (FAILED(hr) || !restored || FAILED(dev->GetRenderTargetData(t.scene_s.p, t.scene_sys.p))) code = 1;
                    D3DLOCKED_RECT lr{};
                    if (FAILED(t.scene_sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return 1;
                    const UINT x0 = 960 - cw / 2, y0 = 540 - ch / 2;
                    const std::vector<float>& twin = batch_out[shade];
                    for (UINT y = 0; y < ch; ++y) {
                        const auto* row = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) +
                                                                                (y0 + y) * lr.Pitch) + std::size_t(x0) * 4;
                        for (UINT x = 0; x < cw; ++x) {
                            float* d = &out[(std::size_t(y) * cw + x) * 3];
                            for (unsigned k = 0; k < 3; ++k) {
                                d[k] = half_to_float(row[x * 4 + k]);
                                if (twin.size() == out.size()) diff = std::max(diff, std::fabs(d[k] - twin[(std::size_t(y) * cw + x) * 3 + k]));
                            }
                            const float l = 0.2126f * d[0] + 0.7152f * d[1] + 0.0722f * d[2];
                            sum += l;
                            peak = l > peak ? l : peak;
                        }
                    }
                    t.scene_sys.p->UnlockRect();
                    double twin_sum = 0.0;
                    float twin_peak = 0.f;
                    for (std::size_t i = 0; i + 2 < twin.size(); i += 3) {
                        const float l = 0.2126f * twin[i] + 0.7152f * twin[i + 1] + 0.0722f * twin[i + 2];
                        twin_sum += l;
                        twin_peak = l > twin_peak ? l : twin_peak;
                    }
                    std::snprintf(path, sizeof path, "%s\\fixture-ppc-lone-%s-near.pfm", dir, shade ? "reshade" : "vanilla");
                    f = std::fopen(path, "wb");
                    bool written = f != nullptr;
                    if (f) {
                        std::fprintf(f, "PF\n%u %u\n-1.0\n", cw, ch);
                        for (UINT y = ch; y-- > 0;)
                            written = written && std::fwrite(&out[std::size_t(y) * cw * 3], sizeof(float), std::size_t(cw) * 3,
                                                             f) == std::size_t(cw) * 3;
                        std::fclose(f);
                    }
                    if (!written) code = 1;
                    std::printf("DUMP_RESHADE name=fixture-ppc-lone-%s-near family=ppc path=lone vertices=%u indexed=1 "
                                "normaliser=%.4g layout=%u distance_m=50 crop=%ux%u dark_sum=%.3f dark_peak=%.4f "
                                "batch_sum=%.3f batch_peak=%.4f max_abs_diff_vs_batch=%.5f written=%u\n",
                                shade ? "reshade" : "vanilla", count, double(fconstants[3]), unsigned(flayout), cw, ch, sum,
                                double(peak), twin_sum, double(twin_peak), double(diff), unsigned(written));
                }
            }
        }
    }
    dev->SetPixelShader(nullptr);
    dev->SetVertexShader(nullptr);
    dev->SetTexture(0, nullptr);
    dev->SetStreamSource(0, nullptr, 0, 0);
    std::printf("RESULT %s checks=0 failures=%u\n", code ? "FAIL" : "PASS", unsigned(code != 0));
    return code;
}
// ------------------------------------------------------------------ muzzle flash re-shade dump (--dump-case muzzle)
// The muzzle re-shade's look evidence (muzzle-flash-look.md; weapon_muzzle_reshade_fixture.h): body 445 drawn the way the
// game draws it (stand-in vs_3_0 / ps_3_0 with the game pair's ABI and emissive law, ADD ONE / ONE, Z test, no Z write,
// the material's emissive 0.588) into the 1920x1080 FP16 scene at the fixture's projection (m00 1.6), once vanilla and
// once with the production program swapped in through the production sequence (plain path), its +z axis 25 degrees out
// of the screen plane and rolled 20 degrees, on the dark background (black) and a nebula stand-in (a flat linear
// 0.30 / 0.22 / 0.38). Frames: near (30 m) and far (300 m) at the scale peak (0.031, 75 ms), grow (30 m) at 20 % of it,
// and at 30 m and the peak the axis along the view ray (axis) and in the screen plane (side): the per-draw overlap's
// two ends.
// The mesh and texture are body 445 and tex/true/384 when run_weapon_effects.py prepared them (untracked), else the
// procedural stand-ins. Each PFM <dir>\fixture-muzzle-{vanilla,reshade}-{near,far,grow,axis,side}.pfm is the two
// backgrounds' crops side by side. DUMP_MUZZLE rows (energy, peak, the equivalent radii at scene white and at 0.1) and one
// DUMP_MUZZLE_GATE row per range (the design's gates: re-shaded energy <= mz_gain x vanilla's, the extent at scene white
// > 0 and <= mz_size x vanilla's; the 0.1 extent ratio is reported, not gated). c192 is the production draw_constants
// from the rows the placement gives (the gun's position and the world scale). keys: "size,core,gain,smooth" overrides
// the baked keys (clamped as the tuning file; the calibration sweep); no record.
namespace mf = muzzle_fx;
bool load_muzzle_assets(const char* dir, mf::Mesh* mesh, std::vector<unsigned char>* rgba) {
    if (!dir || !*dir) return false;
    char path[MAX_PATH];
    std::snprintf(path, sizeof path, "%s\\mz_mesh.txt", dir);
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    mf::Mesh m;
    float q[5];
    while (m.n < mf::max_vertices && std::fscanf(f, "%f %f %f %f %f", &q[0], &q[1], &q[2], &q[3], &q[4]) == 5) {
        for (unsigned k = 0; k < 5; ++k) m.p[m.n][k] = q[k];
        ++m.n;
    }
    std::fclose(f);
    std::snprintf(path, sizeof path, "%s\\mz_tex.rgba", dir);
    f = std::fopen(path, "rb");
    if (!f || m.n != mf::max_vertices) {
        if (f) std::fclose(f);
        return false;
    }
    std::vector<unsigned char> data(256u * 256u * 4u);
    const bool read = std::fread(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    if (!read) return false;
    *mesh = m;
    rgba->swap(data);
    return true;
}
int muzzle_dump(const char* dir, const char* d3dx_path, const char* assets, const char* keys = nullptr) {
    namespace mz = x3m::weapon_effects::muzzle_reshade;
    HMODULE d3dx = LoadLibraryA(d3dx_path && *d3dx_path ? d3dx_path : "d3dx9_37.dll");
    using CompileFn = HRESULT(WINAPI*)(LPCSTR, UINT, const void*, void*, LPCSTR, LPCSTR, DWORD, ID3DXBuffer**,
                                       ID3DXBuffer**, void*);
    const auto compile_fn = d3dx ? reinterpret_cast<CompileFn>(GetProcAddress(d3dx, "D3DXCompileShader")) : nullptr;
    const auto compile = [&](const char* src, const char* profile, ID3DXBuffer** out) {
        ID3DXBuffer* err = nullptr;
        const HRESULT hr = compile_fn(src, UINT(std::strlen(src)), nullptr, nullptr, "main", profile, 0, out, &err, nullptr);
        if (err) err->Release();
        return SUCCEEDED(hr) && *out;
    };
    ID3DXBuffer *vs_code = nullptr, *ps_code = nullptr;
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> vanilla, ours;
    Com<IDirect3DVertexDeclaration9> decl;
    const bool created = compile_fn && compile(mf::vs_source, "vs_3_0", &vs_code) &&
                         compile(mf::vanilla_ps_source, "ps_3_0", &ps_code) &&
                         SUCCEEDED(dev->CreateVertexShader(static_cast<const DWORD*>(vs_code->GetBufferPointer()), &vs.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(static_cast<const DWORD*>(ps_code->GetBufferPointer()), &vanilla.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(mf::program, &ours.p)) &&
                         SUCCEEDED(dev->CreateVertexDeclaration(mf::declaration(), &decl.p));
    if (vs_code) vs_code->Release();
    if (ps_code) ps_code->Release();
    ProjTarget t;
    if (!created || !make_proj_target(t, 1920, 1080)) {
        std::printf("DUMP_FAIL reason=muzzle_programs\nRESULT FAIL checks=1 failures=1\n");
        return 1;
    }
    CreateDirectoryA(dir, nullptr);
    mf::Mesh mesh;
    std::vector<unsigned char> rgba;
    unsigned side = 256;
    const bool real = load_muzzle_assets(assets, &mesh, &rgba);
    if (!real) {
        mesh = mf::standin_mesh();
        rgba = mf::standin_texture(&side);
    }
    Com<IDirect3DTexture9> tex, light;
    tex.p = rf::make_texture(dev, rgba.data(), side);
    const unsigned char zero[4] = {0, 0, 0, 0};
    light.p = mf::flat_texture(dev, zero);
    Com<IDirect3DVertexBuffer9> vb;
    if (!tex.p || !light.p ||
        FAILED(dev->CreateVertexBuffer(mf::max_vertices * sizeof(mf::Vertex), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0,
                                       D3DPOOL_DEFAULT, &vb.p, nullptr))) {
        std::printf("DUMP_FAIL reason=muzzle_resources\nRESULT FAIL checks=1 failures=1\n");
        return 1;
    }
    auto tune = pf::baked;
    if (keys && *keys)
        std::sscanf(keys, "%f,%f,%f,%f", &tune.mz_size, &tune.mz_core, &tune.mz_gain, &tune.mz_smooth);
    float base[16], app[16]{};
    mz::constants(mz::bodies[0], tune, base);
    const float size = mz::clamp_key(tune.mz_size, pf::key_mz_size), gain = mz::clamp_key(tune.mz_gain, pf::key_mz_gain);
    float rows[16];
    rf::clip_rows(1.6f, 1.6f * 1920.f / 1080.f, rows);
    float mid[3];
    mf::mesh_centre(mesh, mid);
    std::printf("DUMP_MUZZLE assets=%s mesh_vertices=%u texture=%u metres_per_body_unit_at_peak=%.3g emissive=%.3f "
                "size=%.3g core=%.3g gain=%.3g smooth=%.3g centre_body_units=%.0f,%.0f,%.0f lit_radius_units=%.0f\n",
                real ? "game" : "standin", mesh.n, side, double(mf::peak_scale / mf::engine_units_per_metre),
                double(mf::emissive[0]), double(size), double(mz::clamp_key(tune.mz_core, pf::key_mz_core)), double(gain), double(1.f - base[4]),
                double(mid[0]), double(mid[1]), double(mid[2]), double(mz::bodies[0].lit_radius));
    dev->SetRenderTarget(0, t.scene_s.p);
    dev->SetDepthStencilSurface(t.depth_s.p);
    const struct {
        const char* name;
        float distance, scale, yaw;
        UINT cw, ch;
    } ranges[5] = {{"near", 30.f, mf::peak_scale, 0.4363f, 640, 360},
                   {"far", 300.f, mf::peak_scale, 0.4363f, 96, 54},
                   {"grow", 30.f, 0.2f * mf::peak_scale, 0.4363f, 640, 360},
                   {"axis", 30.f, mf::peak_scale, 1.5707963f, 640, 360},
                   {"side", 30.f, mf::peak_scale, 0.f, 640, 360}};
    int code = 0;
    unsigned gate_failures = 0;
    for (const auto& r : ranges) {
        void* p = nullptr;
        if (FAILED(vb.p->Lock(0, 0, &p, D3DLOCK_DISCARD))) return 1;
        const float centre[3] = {0.f, 0.f, r.distance};
        float vs_rows[36], constants[16];
        mf::place(mesh, centre, r.scale, r.yaw, 0.3491f, static_cast<mf::Vertex*>(p), vs_rows);
        vb.p->Unlock();
        std::memcpy(constants, base, sizeof constants);
        if (!mz::draw_constants(mz::bodies[0], vs_rows, constants)) code = 1;
        mf::Figures dark[2];
        for (unsigned shade = 0; shade < 2; ++shade) {
            const UINT ow = r.cw * 2, oh = r.ch;
            std::vector<float> out(std::size_t(ow) * oh * 3, 0.f);
            mf::Figures figs[2];
            for (unsigned bg = 0; bg < 2; ++bg) {
                dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                           bg ? D3DCOLOR_ARGB(0, 77, 56, 97) : D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
                dev->BeginScene();
                mf::bind_pass(dev, vs.p, decl.p, tex.p, light.p, rows, true);
                dev->SetStreamSource(0, vb.p, 0, sizeof(mf::Vertex));
                dev->SetPixelShader(vanilla.p);
                bool restored = true;
                const HRESULT hr = shade ? mf::reshaded_draw(dev, ours.p, vanilla.p, constants, app, mesh.n / 3, &restored)
                                         : dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, mesh.n / 3);
                dev->EndScene();
                if (FAILED(hr) || !restored || FAILED(dev->GetRenderTargetData(t.scene_s.p, t.scene_sys.p))) code = 1;
                D3DLOCKED_RECT lr{};
                if (FAILED(t.scene_sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return 1;
                const UINT x0 = 960 - r.cw / 2, y0 = 540 - r.ch / 2;
                std::vector<float> crop(std::size_t(r.cw) * r.ch * 3);
                for (UINT y = 0; y < r.ch; ++y) {
                    const auto* src = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) +
                                                                            (y0 + y) * lr.Pitch) + std::size_t(x0) * 4;
                    for (UINT x = 0; x < r.cw; ++x)
                        for (unsigned k = 0; k < 3; ++k) {
                            const float v = half_to_float(src[x * 4 + k]);
                            out[(std::size_t(y) * ow + bg * r.cw + x) * 3 + k] = v;
                            crop[(std::size_t(y) * r.cw + x) * 3 + k] = v;
                        }
                }
                t.scene_sys.p->UnlockRect();
                const float base = bg ? 0.2126f * 77.f / 255.f + 0.7152f * 56.f / 255.f + 0.0722f * 97.f / 255.f : 0.f;
                figs[bg] = mf::figures(crop.data(), r.cw, r.ch, base);
            }
            dark[shade] = figs[0];
            char path[MAX_PATH];
            std::snprintf(path, sizeof path, "%s\\fixture-muzzle-%s-%s.pfm", dir, shade ? "reshade" : "vanilla", r.name);
            std::FILE* f = std::fopen(path, "wb");
            bool written = f != nullptr;
            if (f) {
                std::fprintf(f, "PF\n%u %u\n-1.0\n", ow, oh);
                for (UINT y = oh; y-- > 0;)
                    written = written && std::fwrite(&out[std::size_t(y) * ow * 3], sizeof(float), std::size_t(ow) * 3, f) ==
                                             std::size_t(ow) * 3;
                std::fclose(f);
            }
            if (!written) code = 1;
            std::printf("DUMP_MUZZLE name=fixture-muzzle-%s-%s distance_m=%.0f scale=%.4f crop=%ux%u dark_sum=%.3f "
                        "dark_peak=%.4f dark_white_r=%.1f dark_tenth_r=%.1f dark_peak_tenth_r=%.1f nebula_sum=%.3f "
                        "nebula_peak=%.4f written=%u\n",
                        shade ? "reshade" : "vanilla", r.name, double(r.distance), double(r.scale), r.cw, r.ch,
                        figs[0].sum, double(figs[0].peak), double(figs[0].white_r), double(figs[0].tenth_r),
                        double(figs[0].peak_tenth_r), figs[1].sum, double(figs[1].peak), unsigned(written));
        }
        const double energy = dark[0].sum > 0 ? dark[1].sum / dark[0].sum : 0.0;
        const bool energy_ok = dark[0].sum > 0 && energy <= double(gain);
        // the extent gate at 0.1 linear (FL 65 A baked no white core: the white radius and the extent at a tenth of each
        // image's own peak are reported)
        // lit: `visible`; and the flash's own-peak tenth no wider than vanilla's
        const bool extent_ok = dark[1].tenth_r <= size * dark[0].tenth_r + 1.f &&
                               dark[1].peak_tenth_r <= dark[0].peak_tenth_r;
        const bool visible = dark[1].peak > 0.f;
        if (!energy_ok || !extent_ok || !visible) ++gate_failures;
        std::printf("DUMP_MUZZLE_GATE range=%s yaw_deg=%.1f overlap=%.3f energy_ratio=%.3f gain=%.3g "
                    "tenth_r=%.1f/%.1f size=%.3g reshade_peak=%.4f vanilla_peak=%.4f energy=%s extent_tenth=%s "
                    "visible=%s white_r=%.1f/%.1f peak_tenth_r=%.1f/%.1f tenth_r_ratio=%.3f\n",
                    r.name, double(r.yaw) * 57.29578, constants[8] > 0.f ? double(constants[10] / constants[8]) : 0.0,
                    energy, double(gain), double(dark[1].tenth_r), double(dark[0].tenth_r), double(size),
                    double(dark[1].peak), double(dark[0].peak), energy_ok ? "pass" : "FAIL", extent_ok ? "pass" : "FAIL",
                    visible ? "pass" : "FAIL", double(dark[1].white_r), double(dark[0].white_r),
                    double(dark[1].peak_tenth_r), double(dark[0].peak_tenth_r),
                    dark[0].tenth_r > 0.f ? double(dark[1].tenth_r / dark[0].tenth_r) : 0.0);
    }
    // The view-angle sweep (no frame, dark background, 30 m, the scale peak): the flash axis yawed 0..90 degrees out of
    // the screen plane. One DUMP_MUZZLE_SWEEP row each with the draw's cos^2 and N, the energy ratio, the peaks and the
    // white radii; verification/results/muzzle-reshade/overlap_by_angle.py --rendered fits N(cos^2) from them.
    static const float sweep_deg[] = {0.f, 10.f, 20.f, 25.f, 30.f, 40.f, 50.f, 60.f, 70.f, 75.f, 80.f, 85.f, 88.f, 90.f};
    for (const float deg : sweep_deg) {
        void* p = nullptr;
        if (FAILED(vb.p->Lock(0, 0, &p, D3DLOCK_DISCARD))) return 1;
        const float centre[3] = {0.f, 0.f, 30.f};
        float vs_rows[36], constants[16];
        mf::place(mesh, centre, mf::peak_scale, deg * 0.01745329f, 0.3491f, static_cast<mf::Vertex*>(p), vs_rows);
        vb.p->Unlock();
        std::memcpy(constants, base, sizeof constants);
        if (!mz::draw_constants(mz::bodies[0], vs_rows, constants)) code = 1;
        const float vx = vs_rows[27] - vs_rows[3], vy = vs_rows[31] - vs_rows[7], vz = vs_rows[35] - vs_rows[11];
        const float zx = vs_rows[2], zy = vs_rows[6], zz = vs_rows[10], dz = vx * zx + vy * zy + vz * zz;
        const float c2 = dz * dz / ((vx * vx + vy * vy + vz * vz) * (zx * zx + zy * zy + zz * zz));
        mf::Figures figs[2];
        const UINT cw = 640, ch = 360;
        std::vector<float> crop(std::size_t(cw) * ch * 3);
        for (unsigned shade = 0; shade < 2; ++shade) {
            dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
            dev->BeginScene();
            mf::bind_pass(dev, vs.p, decl.p, tex.p, light.p, rows, true);
            dev->SetStreamSource(0, vb.p, 0, sizeof(mf::Vertex));
            dev->SetPixelShader(vanilla.p);
            bool restored = true;
            const HRESULT hr = shade ? mf::reshaded_draw(dev, ours.p, vanilla.p, constants, app, mesh.n / 3, &restored)
                                     : dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, mesh.n / 3);
            dev->EndScene();
            if (FAILED(hr) || !restored || FAILED(dev->GetRenderTargetData(t.scene_s.p, t.scene_sys.p))) code = 1;
            D3DLOCKED_RECT lr{};
            if (FAILED(t.scene_sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return 1;
            const UINT x0 = 960 - cw / 2, y0 = 540 - ch / 2;
            for (UINT y = 0; y < ch; ++y) {
                const auto* src = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) +
                                                                        (y0 + y) * lr.Pitch) + std::size_t(x0) * 4;
                for (UINT x = 0; x < cw; ++x)
                    for (unsigned k = 0; k < 3; ++k) crop[(std::size_t(y) * cw + x) * 3 + k] = half_to_float(src[x * 4 + k]);
            }
            t.scene_sys.p->UnlockRect();
            figs[shade] = mf::figures(crop.data(), cw, ch, 0.f);
        }
        std::printf("DUMP_MUZZLE_SWEEP yaw_deg=%.0f c2=%.5f overlap=%.4f energy_ratio=%.4f reshade_peak=%.4f "
                    "vanilla_peak=%.4f white_r=%.1f vanilla_white_r=%.1f\n",
                    double(deg), double(c2), double(constants[10] / constants[8]),
                    figs[0].sum > 0 ? figs[1].sum / figs[0].sum : 0.0, double(figs[1].peak), double(figs[0].peak),
                    double(figs[1].white_r), double(figs[0].white_r));
    }
    dev->SetPixelShader(nullptr);
    dev->SetVertexShader(nullptr);
    dev->SetTexture(0, nullptr);
    dev->SetTexture(2, nullptr);
    dev->SetStreamSource(0, nullptr, 0, 0);
    std::printf("RESULT %s checks=5 failures=%u\n", code || gate_failures ? "FAIL" : "PASS",
                unsigned(code != 0) + gate_failures);
    return code || gate_failures ? 1 : 0;
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
    // argv: <dir> reshade|muzzle <d3dx9_37.dll> <assets dir> (run_weapon_effects.py --dump DIR --dump-case ...)
    const bool reshade = argc > 2 && !std::strcmp(argv[2], "reshade");
    const bool muzzle = argc > 2 && !std::strcmp(argv[2], "muzzle");
    const int code = reshade ? reshade_dump(dir, argc > 3 ? argv[3] : nullptr, argc > 4 ? argv[4] : nullptr,
                                            argc > 5 ? argv[5] : nullptr)
                     : muzzle ? muzzle_dump(dir, argc > 3 ? argv[3] : nullptr, argc > 4 ? argv[4] : nullptr,
                                            argc > 5 ? argv[5] : nullptr)
                              : (std::printf("DUMP_FAIL reason=case (reshade or muzzle)\nRESULT FAIL checks=1 failures=1\n"), 1);
    dev = nullptr;
    return code;
}
