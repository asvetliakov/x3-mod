#pragma once
// ------------------------------------------------------------------ hull hit re-shade dump (--dump DIR --dump-case hull)
// The hull hit re-shade's look evidence (docs/architecture/hull-hit-look.md section 4), included by
// weapon_effects_projectile_dump.cpp after its helpers (dev, Com, half_to_float, ProjTarget, make_proj_target). The impact
// elements drawn the way the game draws them (weapon_hull_reshade_fixture.h: the game's standard_lighting DEFAULT pair from
// the working directory's classic_*.bin, the effects DEFAULT stand-in) into the 1920 x 1080 FP16 scene at the fixture's
// projection (m00 1.6), once vanilla and once through the production sequence with our program, under the game's blend,
// on black and on a hull plate (linear 0.6 grey). Real assets only (run_weapon_effects.py --dump-case hull prepares them
// from the local lab textures and the FL catalogues into <assets>: the flipbook pages, the glows, tex/true/380 and
// v/00587's mesh); absent, the dump fails. Each PFM <dir>\fixture-hull-<frame>.pfm is four crops side by side: vanilla
// black, vanilla plate, re-shade black, re-shade plate. Sizes: the element's Effects row (census) at the frame's age, body
// units to metres by the muzzle dump's 500 units per metre over the bodies' +-100000-unit extent (an inference). DUMP_HULL
// rows per frame; DUMP_HULL_GATE rows: the hot core (sparks f02, glow peak: re-shade peak >= 1 on black), the fade (v/10661
// at f 0.1 against f 1: dark sum ratio <= 0.15), no cell edge (sparks f28: the outer 2 % of the cell <= 0.02 of the
// frame's peak). No record.
#include "weapon_hull_reshade_fixture.h"

namespace hull_dump_fx {
namespace hf = hull_fx;
namespace hh = x3m::weapon_effects::hull_reshade;
struct Texture {
    char key[24] = {};
    unsigned side = 0;
    std::vector<unsigned char> rgba;
};
inline bool load_textures(const char* dir, std::vector<Texture>& out) {
    char path[MAX_PATH];
    std::snprintf(path, sizeof path, "%s\\textures.txt", dir);
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    Texture t;
    while (std::fscanf(f, "%23s %u", t.key, &t.side) == 2) out.push_back(t);
    std::fclose(f);
    for (auto& x : out) {
        std::snprintf(path, sizeof path, "%s\\%s.rgba", dir, x.key);
        std::FILE* r = std::fopen(path, "rb");
        if (!r) return false;
        x.rgba.resize(std::size_t(x.side) * x.side * 4);
        const bool read = std::fread(x.rgba.data(), 1, x.rgba.size(), r) == x.rgba.size();
        std::fclose(r);
        if (!read) return false;
    }
    return !out.empty();
}
inline bool load_mesh(const char* dir, std::vector<float>& out) {
    char path[MAX_PATH];
    std::snprintf(path, sizeof path, "%s\\m587.txt", dir);
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    float q[5];
    while (std::fscanf(f, "%f %f %f %f %f", &q[0], &q[1], &q[2], &q[3], &q[4]) == 5) out.insert(out.end(), q, q + 5);
    std::fclose(f);
    return out.size() == 60u * 5u;
}
// One element of a frame: the body, its texture key, the flipbook frame (-1 static), the half extent (metres), the fade,
// the roll; mesh: v/00587's crossed quads at the frame's yaw instead of a camera-facing quad.
struct Element {
    std::uint32_t body;
    const char* texture;
    int cell;
    float half, fade, roll;
    bool mesh;
};
struct Frame {
    const char* name;
    float distance;
    Element e[2];
    unsigned elements;
};
} // namespace hull_dump_fx

int hull_dump(const char* dir, const char* d3dx_path, const char* assets, const char* keys = nullptr) {
    namespace hd = hull_dump_fx;
    namespace hf = hull_fx;
    namespace hh = x3m::weapon_effects::hull_reshade;
    namespace rf = reshade_fx;
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
    ID3DXBuffer *vs_code = nullptr, *ps_code = nullptr, *vanilla_code = nullptr;
    const bool compiled = compile_fn && compile(rf::lone_vs_source, "vs_2_0", &vs_code) &&
                          compile(rf::lone_ps_source, "ps_2_0", &ps_code) &&
                          compile(hf::vanilla_classic_ps_source, "ps_3_0", &vanilla_code);
    hf::Programs p;
    const bool created =
        compiled && hf::create(dev, static_cast<const DWORD*>(vs_code->GetBufferPointer()),
                               static_cast<const DWORD*>(ps_code->GetBufferPointer()), p,
                               static_cast<const DWORD*>(vanilla_code->GetBufferPointer()));
    if (vs_code) vs_code->Release();
    if (ps_code) ps_code->Release();
    if (vanilla_code) vanilla_code->Release();
    std::vector<hd::Texture> textures;
    std::vector<float> mesh;
    const bool real = assets && *assets && hd::load_textures(assets, textures) && hd::load_mesh(assets, mesh);
    ProjTarget t;
    if (!created || !real || !make_proj_target(t, 1920, 1080)) {
        std::printf("DUMP_FAIL reason=hull_%s loaded=%u game_hashes=%u\nRESULT FAIL checks=1 failures=1\n",
                    !created ? "programs" : !real ? "assets" : "target", unsigned(p.loaded), unsigned(p.hashes));
        p.release();
        return 1;
    }
    CreateDirectoryA(dir, nullptr);
    std::vector<IDirect3DTexture9*> tex(textures.size(), nullptr);
    for (std::size_t i = 0; i < textures.size(); ++i) tex[i] = rf::make_texture(dev, textures[i].rgba.data(), textures[i].side);
    const auto find = [&](const char* key) -> IDirect3DTexture9* {
        for (std::size_t i = 0; i < textures.size(); ++i)
            if (!std::strcmp(textures[i].key, key)) return tex[i];
        return nullptr;
    };
    Com<IDirect3DTexture9> light;
    const unsigned char zero[4] = {0, 0, 0, 0};
    light.p = hf::flat(dev, zero);
    auto tune = x3m::weapon_effects::projectile_family::baked;
    if (keys && *keys) std::sscanf(keys, "%f,%f", &tune.hh_gain, &tune.hh_core); // the look sweep (clamped as the file)
    float clip[16];
    rf::clip_rows(1.6f, 1.6f * 1920.f / 1080.f, clip);
    const float px_per_m = 960.f * 1.6f; // at 1 m distance
    // the frames (hull-hit-look.md section 4): ages and sizes from the census rows (row 15 IRE: v/10663 GROW 0.04 ->
    // 0.06 over 800 ms, v/10661 GROW 0.01 -> 0.2 over 250 ms, both FADEOUT; row 18: v/10658 GROWANDSHRINK 0.01 .. 0.1
    // over 500 ms, no FADEOUT; row 53 PPC: v/00587 GROW 0 -> 0.5 over 550 ms, FADEOUT; row 17 HEPT: v/10659 GROW 0.01 ->
    // 0.04 over 500 ms; row 23: v/12000 GROW 0.04 -> 0.12 over 1440 ms, no FADEOUT)
    const auto sparks = [&](int k, float roll) {
        const float age = 25.f * float(k);
        return hd::Element{10663u, k < 16 ? "sparks1" : "sparks2", k, hf::half_metres(0.04f + 0.02f * age / 800.f),
                           hf::fadeout(age, 800.f), roll, false};
    };
    const auto mesh587 = [&](float f) {
        const float k = (254.f - 255.f * f) / 253.f; // the age share whose fade is f
        return hd::Element{587u, "t380", -1, hf::half_metres(0.5f * k), f, 0.3f, true};
    };
    const float glow_late = 0.01f + 0.09f * (1.f - (2.f * 450.f / 500.f - 1.f) * (2.f * 450.f / 500.f - 1.f));
    const hd::Frame frames[] = {
        {"sparks-f02", 50.f, {sparks(2, 0.3f)}, 1},
        {"sparks-f08", 50.f, {sparks(8, 0.3f)}, 1},
        {"sparks-f16", 50.f, {sparks(16, 0.3f)}, 1},
        {"sparks-f28", 50.f, {sparks(28, 0.f)}, 1}, // roll 0: the cell-edge gate's pixel-to-cell map
        {"sparks-far", 300.f, {sparks(8, 0.3f)}, 1},
        {"ire-100ms", 50.f,
         {sparks(4, 0.3f), {10661u, "glow", -1, hf::half_metres(0.01f + 0.19f * 100.f / 250.f), hf::fadeout(100.f, 250.f),
                            0.f, false}},
         2},
        {"ire-400ms", 50.f, {sparks(16, 0.3f)}, 1}, // v/10661 ended at 250 ms
        {"glow-peak", 200.f, {{10658u, "impact_glow", -1, hf::half_metres(0.1f), 1.f, 0.f, false}}, 1},
        {"glow-late", 200.f, {{10658u, "impact_glow", -1, hf::half_metres(glow_late), 1.f, 0.f, false}}, 1},
        {"mesh-090", 300.f, {mesh587(0.9f)}, 1},
        {"mesh-050", 300.f, {mesh587(0.5f)}, 1},
        {"mesh-010", 300.f, {mesh587(0.1f)}, 1},
        {"hept", 50.f, {{10659u, "pl_imp", -1, hf::half_metres(0.01f + 0.03f * 150.f / 500.f), hf::fadeout(150.f, 500.f),
                         0.5f, false}},
         1},
        {"blue-f05", 100.f, {{12000u, "blue1", 5, hf::half_metres(0.04f + 0.08f * 0.1f), 1.f, 0.4f, false}}, 1},
        {"blue-f20", 100.f, {{12000u, "blue2", 20, hf::half_metres(0.04f + 0.08f * 0.45f), 1.f, 0.4f, false}}, 1},
        {"fade-f100", 200.f, {{10661u, "glow", -1, hf::half_metres(0.1f), 1.f, 0.f, false}}, 1},
        {"fade-f010", 200.f, {{10661u, "glow", -1, hf::half_metres(0.1f), 0.1f, 0.f, false}}, 1},
    };
    Com<IDirect3DVertexBuffer9> vb;
    if (FAILED(dev->CreateVertexBuffer(64 * sizeof(hf::ClassicVertex), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0,
                                       D3DPOOL_DEFAULT, &vb.p, nullptr))) {
        std::printf("DUMP_FAIL reason=hull_vb\nRESULT FAIL checks=1 failures=1\n");
        return 1;
    }
    dev->SetRenderTarget(0, t.scene_s.p);
    dev->SetDepthStencilSurface(t.depth_s.p);
    std::printf("DUMP_HULL assets=game vanilla_classic=texel_law textures=%u mesh_vertices=%u gain=%.3g edge=%.3g core=%.3g cool=%.3g plate=0.6\n",
                unsigned(textures.size()), unsigned(mesh.size() / 5), double(tune.hh_gain), double(tune.hh_edge),
                double(tune.hh_core), double(tune.hh_cool));
    int code = 0;
    unsigned gate_failures = 0;
    double fade_sum[2][2] = {}; // [vanilla, re-shade][f 1, f 0.1]
    const float app[8] = {};
    for (const auto& fr : frames) {
        float half_px = 0.f;
        for (unsigned i = 0; i < fr.elements; ++i)
            half_px = std::max(half_px, fr.e[i].half * px_per_m / fr.distance * (fr.e[i].mesh ? 1.2f : 1.42f));
        UINT crop = UINT(std::min(1080.f, std::max(48.f, 2.2f * half_px)));
        crop &= ~1u;
        const UINT ow = crop * 4, oh = crop;
        std::vector<float> out(std::size_t(ow) * oh * 3, 0.f);
        float peak[2] = {}, edge_band[2] = {};
        double dark_sum[2] = {};
        for (unsigned shade = 0; shade < 2; ++shade)
            for (unsigned bg = 0; bg < 2; ++bg) {
                dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                           bg ? D3DCOLOR_ARGB(0, 153, 153, 153) : D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
                dev->BeginScene();
                for (unsigned i = 0; i < fr.elements; ++i) {
                    const hd::Element& el = fr.e[i];
                    const unsigned b = hh::body_index(el.body, hh::edition_fl);
                    if (b >= hh::table_count) {
                        code = 1;
                        continue;
                    }
                    const hh::Body& body = hh::bodies[b];
                    float block[8];
                    hh::constants(body, tune, block);
                    float tm[8] = {1, 0, 0, 0, 0, 1, 0, 0};
                    if (el.cell >= 0) hf::cell_matrix(unsigned(el.cell), tm);
                    hf::ClassicVertex v[64];
                    unsigned n = 6;
                    const float centre[3] = {0.f, 0.f, fr.distance};
                    if (el.mesh) {
                        n = unsigned(mesh.size() / 5);
                        const float k = el.half / hf::body_half_units; // metres per body unit
                        const float yaw = 0.4363f, cy = std::cos(yaw), sy = std::sin(yaw), cr = std::cos(el.roll),
                                    sr = std::sin(el.roll);
                        for (unsigned j = 0; j < n; ++j) {
                            const float* q = &mesh[j * 5];
                            const float x0 = q[0] * k, y0 = q[1] * k, z0 = q[2] * k;
                            const float x1 = cr * x0 - sr * y0, y1 = sr * x0 + cr * y0;
                            v[j] = hf::ClassicVertex{centre[0] + x1 * cy + z0 * sy, centre[1] + y1,
                                                     centre[2] - x1 * sy + z0 * cy, q[3], q[4], 0.f, 0.f, -1.f};
                        }
                    } else {
                        hf::quad(centre, el.half, el.roll, 0.f, 0.f, el.cell >= 0 ? 0.25f : 1.f, el.cell >= 0 ? 0.25f : 1.f,
                                 v);
                    }
                    void* data = nullptr;
                    if (FAILED(vb.p->Lock(0, 0, &data, D3DLOCK_DISCARD))) return 1;
                    if (body.pair == hh::pair_classic)
                        std::memcpy(data, v, n * sizeof v[0]);
                    else
                        hf::effects_vertices(v, n, static_cast<rf::LoneVertex*>(data));
                    vb.p->Unlock();
                    const DWORD dst = body.blend == hh::blend_screen ? D3DBLEND_INVSRCCOLOR : D3DBLEND_ONE;
                    hf::bind(dev, p, body.pair, el.mesh, clip, tm, el.fade, 0.f, find(el.texture), light.p, dst);
                    dev->SetStreamSource(0, vb.p, 0,
                                         body.pair == hh::pair_classic ? sizeof(hf::ClassicVertex) : sizeof(rf::LoneVertex));
                    // the game's program the sequence puts back; the vanilla panel draws the texel law (classic pair)
                    IDirect3DPixelShader9* game = body.pair == hh::pair_classic ? p.classic_ps[el.mesh ? 1 : 0] : p.effects_ps;
                    if (body.pair == hh::pair_classic && !shade) dev->SetPixelShader(p.classic_vanilla);
                    bool restored = true;
                    const HRESULT hr =
                        shade ? hf::reshaded_draw(
                                    dev, body.pair, p.ours[body.pair], game, block, app,
                                    [&] { return dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, n / 3); }, &restored)
                              : dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, n / 3);
                    if (FAILED(hr) || !restored || !find(el.texture)) code = 1;
                }
                dev->EndScene();
                if (FAILED(dev->GetRenderTargetData(t.scene_s.p, t.scene_sys.p))) code = 1;
                D3DLOCKED_RECT lr{};
                if (FAILED(t.scene_sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return 1;
                const UINT x0 = 960 - crop / 2, y0 = 540 - crop / 2;
                const unsigned panel = shade * 2 + bg;
                const hd::Element& e0 = fr.e[0];
                for (UINT y = 0; y < crop; ++y) {
                    const auto* src = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) +
                                                                            (y0 + y) * lr.Pitch) + std::size_t(x0) * 4;
                    for (UINT x = 0; x < crop; ++x) {
                        float c[3];
                        for (unsigned k = 0; k < 3; ++k) {
                            c[k] = half_to_float(src[x * 4 + k]);
                            out[(std::size_t(y) * ow + panel * crop + x) * 3 + k] = c[k];
                        }
                        if (bg) continue;
                        const float l = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
                        dark_sum[shade] += l;
                        peak[shade] = std::max(peak[shade], l);
                        // the cell-edge band: the pixel centre back to the first element's quad (roll 0) -> its cell
                        if (e0.cell >= 0 && !e0.mesh && std::fabs(e0.roll) < 1e-6f) {
                            const float mx = (float(x0 + x) + 0.5f - 960.f) / px_per_m * fr.distance,
                                        my = (540.f - float(y0 + y) - 0.5f) / px_per_m * fr.distance;
                            const float qx = 0.5f + 0.5f * mx / e0.half, qy = 0.5f + 0.5f * my / e0.half;
                            const float m = 2.f * std::max(std::fabs(qx - 0.5f), std::fabs(qy - 0.5f));
                            if (m >= 0.96f && m <= 1.f) edge_band[shade] = std::max(edge_band[shade], l);
                        }
                    }
                }
                t.scene_sys.p->UnlockRect();
            }
        char path[MAX_PATH];
        std::snprintf(path, sizeof path, "%s\\fixture-hull-%s.pfm", dir, fr.name);
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
        std::printf("DUMP_HULL name=fixture-hull-%s distance_m=%.0f elements=%u body=%lu half_m=%.2f fade=%.3f crop=%u "
                    "vanilla_sum=%.2f vanilla_peak=%.4f reshade_sum=%.2f reshade_peak=%.4f energy_ratio=%.3f written=%u\n",
                    fr.name, double(fr.distance), fr.elements, static_cast<unsigned long>(fr.e[0].body),
                    double(fr.e[0].half), double(fr.e[0].fade), crop, dark_sum[0], double(peak[0]), dark_sum[1],
                    double(peak[1]), dark_sum[0] > 0.0 ? dark_sum[1] / dark_sum[0] : 0.0, unsigned(written));
        if (!std::strcmp(fr.name, "sparks-f02") || !std::strcmp(fr.name, "glow-peak")) {
            const bool hot = peak[1] >= 1.f;
            gate_failures += !hot;
            std::printf("DUMP_HULL_GATE gate=hot_core frame=%s reshade_peak=%.4f vanilla_peak=%.4f result=%s\n", fr.name,
                        double(peak[1]), double(peak[0]), hot ? "pass" : "FAIL");
        }
        if (!std::strcmp(fr.name, "sparks-f28")) {
            const float ratio = peak[1] > 0.f ? edge_band[1] / peak[1] : 1.f;
            const bool clean = peak[1] > 0.f && ratio <= 0.02f;
            gate_failures += !clean;
            std::printf("DUMP_HULL_GATE gate=no_cell_edge frame=%s band_peak=%.5f frame_peak=%.4f ratio=%.4f "
                        "vanilla_ratio=%.4f result=%s\n",
                        fr.name, double(edge_band[1]), double(peak[1]), double(ratio),
                        peak[0] > 0.f ? double(edge_band[0] / peak[0]) : 0.0, clean ? "pass" : "FAIL");
        }
        if (!std::strcmp(fr.name, "fade-f100") || !std::strcmp(fr.name, "fade-f010")) {
            const unsigned k = fr.name[6] == '1' ? 0u : 1u;
            fade_sum[0][k] = dark_sum[0];
            fade_sum[1][k] = dark_sum[1];
        }
    }
    const double fade = fade_sum[1][0] > 0.0 ? fade_sum[1][1] / fade_sum[1][0] : 1.0,
                 vanilla_fade = fade_sum[0][0] > 0.0 ? fade_sum[0][1] / fade_sum[0][0] : 1.0;
    const bool faded = fade_sum[1][0] > 0.0 && fade <= 0.15;
    gate_failures += !faded;
    std::printf("DUMP_HULL_GATE gate=fade_visible body=10661 ratio=%.4f vanilla_ratio=%.4f result=%s\n", fade, vanilla_fade,
                faded ? "pass" : "FAIL");
    for (auto* x : tex)
        if (x) x->Release();
    p.release();
    std::printf("RESULT %s checks=4 failures=%u\n", code || gate_failures ? "FAIL" : "PASS", gate_failures + unsigned(code));
    return code || gate_failures ? 1 : 0;
}
