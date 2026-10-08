// Detached GPU fixture of the engine light on the hull (docs/architecture/engine-light.md): the pixel twins of three
// reviewed original pairs (hull DEFAULT and BUMPMAP with the light-loop vertex layout, hull DEFAULT with the
// single-light layout) drawn over a plate in front of a synthetic engine light, against their light-less base
// variants; the light goes the production CPU path (engine_light_core.h: a glow-jet record -> the ship table -> the
// node table from a logged draw -> the draw's block) and is uploaded with the ship's nozzle plates and their lights'
// colours (the tier's run, ending at c197) and c200-c202. Prints CASE / SAMPLE / INVARIANT rows for the runner's float64
// oracle (verification/probe/run_engine_light.py), the DUAL rows (ships of two, ten and 72 nozzles: a light per plate,
// the per-pixel selection, every plate lit, none dropped), the
// nozzle-plate PLATE / P rows (the light-map term's gain near the ship's main nozzles: one nozzle as twin, gain 1,
// light absent, no gain; three, eight, ten and 72 nozzles as twin), then the Reset witness,
// the per-draw CPU cost of the lookup + constants + upload (ships of 1, 8 and 72 nozzles), the twins' creation time,
// and the GPU cost of the term over a full-screen hull at 1920x1080 and 5120x1440 for ships of 1, 3, 8, 10 and 72
// nozzles (EVENT-fenced). Local original programs only
// (/tmp/x3-shader-sweep/programs); no game bytes in the repository. Never launches the game.
#define WIN32_LEAN_AND_MEAN
#include "../../src/proxy/engine_light_core.h"
#include "../../src/renderer/linear_material.h"
#include "../../src/renderer/material_motion.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <d3d9.h>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
using namespace x3m::renderer;
namespace el = x3m::engine_light::core;
namespace ee = x3m::engine_effects::core;
namespace ep = x3m::engine_plumes;
using Words = std::vector<std::uint32_t>;
static void api(HRESULT h, const char* what) {
    if (FAILED(h)) {
        std::printf("API_FAIL %s %08lx\n", what, h);
        throw std::runtime_error(what);
    }
}
static void require(bool c, const char* what) {
    if (!c) {
        std::printf("REQUIRE_FAIL %s\n", what);
        throw std::runtime_error(what);
    }
}
template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    ~Com() { reset(); }
    void reset() {
        if (p) p->Release();
        p = nullptr;
    }
};
static Words read_program(const std::string& dir, const char* name) {
    std::ifstream f(dir + "/" + name + ".bin", std::ios::binary | std::ios::ate);
    require(bool(f), "open original program");
    const auto bytes = f.tellg();
    Words w(std::size_t(bytes) / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(w.data()), bytes);
    require(bool(f), "read original program");
    return w;
}
static std::uint64_t now() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return std::uint64_t(c.QuadPart);
}
static double frequency() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return double(f.QuadPart);
}
struct Vertex {
    float p[3], uv[2], n[3], b[3], t[3];
};
struct Pair {
    const char* vs;
    const char* ps;
    bool bump, loop;
};
static const Pair pairs[] = {{"vs_53a0a641107ed76c", "ps_8759c7838bbc86c2", false, true},
                             {"vs_4944d81dfe531b37", "ps_64bac8bb307eb896", true, true},
                             {"vs_badefd5143b3024f", "ps_f6a501717c3e5ca8", false, false}};
// Kinds: 1 = fill only (K), 6 = the production share producer with the dynamic light-map gain and the widening.
struct Programs {
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> base[2], twin[2];
    unsigned base_slots[2]{}, twin_words[2]{}, base_words[2]{};
};
constexpr float fill = .01f, gain = 4.f;
static const HullLightmapWiden widen{4.f, 4.f};
static OriginalVariantOptions options(unsigned kind) {
    OriginalVariantOptions o;
    o.fill = fill;
    if (kind == 6) {
        o.share = true;
        o.lightmap_gain = gain;
        o.lightmap_dynamic = true;
        o.widen = &widen;
    }
    return o;
}
struct Fixture {
    IDirect3DDevice9* d;
    D3DPRESENT_PARAMETERS pp;
    std::string dir;
    unsigned size = 256;
    Com<IDirect3DSurface9> back, color32, color16, motion, depth;
    Com<IDirect3DTexture9> white, black, flat_normal, white_rgb;
    IDirect3DTexture9* lightmap = nullptr; // the light-map stage's texture (null: black)
    float gain_lane = gain;                // c217.w, the dynamic light-map gain
    Com<IDirect3DCubeTexture9> cube;
    Com<IDirect3DVertexDeclaration9> declaration;
    Com<IDirect3DVertexBuffer9> plate;
    Programs programs[3];
    double create_us = 0.;
    unsigned created = 0;
    static constexpr unsigned grid = 16, triangles = grid * grid * 2;
    void targets(unsigned w, unsigned h) {
        api(d->CreateRenderTarget(w, h, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &color32.p, nullptr), "rt32");
        api(d->CreateRenderTarget(w, h, D3DFMT_A16B16G16R16F, D3DMULTISAMPLE_NONE, 0, FALSE, &color16.p, nullptr), "rt16");
        api(d->CreateRenderTarget(w, h, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &motion.p, nullptr), "motion");
        api(d->CreateRenderTarget(w, h, D3DFMT_R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &depth.p, nullptr), "depth");
    }
    void release_targets() {
        d->SetRenderTarget(1, nullptr);
        d->SetRenderTarget(2, nullptr);
        d->SetRenderTarget(0, back.p);
        color32.reset();
        color16.reset();
        motion.reset();
        depth.reset();
    }
    void texture(Com<IDirect3DTexture9>& t, float r, float g, float b, float a) {
        api(d->CreateTexture(1, 1, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED, &t.p, nullptr), "texture");
        D3DLOCKED_RECT lock{};
        api(t.p->LockRect(0, &lock, nullptr, 0), "lock");
        const float v[4] = {r, g, b, a};
        std::memcpy(lock.pBits, v, 16);
        api(t.p->UnlockRect(0), "unlock");
    }
    void setup() {
        api(d->GetRenderTarget(0, &back.p), "back");
        targets(size, size);
        texture(white, 1, 1, 1, 1);
        texture(black, 0, 0, 0, 0);
        texture(flat_normal, .5f, .5f, .5f, .5f); // AG normal (0, 0): the geometric normal
        texture(white_rgb, 1, 1, 1, 0);           // the nozzle-plate case's light map: white RGB, alpha as black's
        api(d->CreateCubeTexture(1, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED, &cube.p, nullptr), "cube");
        for (unsigned f = 0; f < 6; ++f) {
            D3DLOCKED_RECT lock{};
            api(cube.p->LockRect(D3DCUBEMAP_FACES(f), 0, &lock, nullptr, 0), "cube lock");
            std::memset(lock.pBits, 0, 16);
            api(cube.p->UnlockRect(D3DCUBEMAP_FACES(f), 0), "cube unlock");
        }
        const D3DVERTEXELEMENT9 elements[] = {
            {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
            {0, 20, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
            {0, 32, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BINORMAL, 0},
            {0, 44, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT, 0},
            D3DDECL_END()};
        api(d->CreateVertexDeclaration(elements, &declaration.p), "declaration");
        // The plate: model x, y in [-120, 120], z = 0, facing model -z, tessellated 16 x 16.
        api(d->CreateVertexBuffer(triangles * 3 * sizeof(Vertex), 0, 0, D3DPOOL_MANAGED, &plate.p, nullptr), "vb");
        Vertex* v = nullptr;
        api(plate.p->Lock(0, 0, reinterpret_cast<void**>(&v), 0), "vb lock");
        auto corner = [](unsigned i, unsigned j) {
            Vertex x{};
            x.p[0] = -120.f + 240.f * float(i) / grid;
            x.p[1] = -120.f + 240.f * float(j) / grid;
            x.uv[0] = float(i) / grid;
            x.uv[1] = float(j) / grid;
            x.n[2] = -1.f;
            x.b[1] = 1.f;
            x.t[0] = 1.f;
            return x;
        };
        unsigned k = 0;
        for (unsigned j = 0; j < grid; ++j)
            for (unsigned i = 0; i < grid; ++i) {
                const Vertex a = corner(i, j), b = corner(i + 1, j), c = corner(i + 1, j + 1), e = corner(i, j + 1);
                v[k++] = a, v[k++] = b, v[k++] = c, v[k++] = a, v[k++] = c, v[k++] = e;
            }
        api(plate.p->Unlock(), "vb unlock");
        for (unsigned p = 0; p < 3; ++p) build(p);
    }
    void build(unsigned p) {
        const Words vs = read_program(dir, pairs[p].vs), ps = read_program(dir, pairs[p].ps);
        require(linear_material_pair_reviewed(material_motion_fingerprint(vs.data(), vs.size()),
                                              material_motion_fingerprint(ps.data(), ps.size())),
                "reviewed pair");
        Words motion;
        require(material_motion_vertex_variant(vs.data(), vs.size(), motion, true) == MaterialMotionResult::Applied,
                "VS motion variant");
        api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(motion.data()), &programs[p].vs.p), "VS");
        for (unsigned k = 0; k < 2; ++k) {
            const unsigned kind = k ? 6 : 1;
            Words base, twin;
            bool fill_applied = false, share = false, gain_applied = false, widen_applied = false, engine = false;
            if (kind == 1)
                require(linear_material_original_fill_pixel_variant(ps.data(), ps.size(), fill, base, true,
                                                                    fill_applied) == LinearMaterialResult::Applied &&
                            fill_applied,
                        "fill base");
            else
                require(linear_material_original_sun_share_pixel_variant(ps.data(), ps.size(), fill, base, true, share,
                                                                         gain, &gain_applied, true, &widen,
                                                                         &widen_applied) ==
                                LinearMaterialResult::Applied &&
                            share && gain_applied && widen_applied,
                        "production base");
            require(linear_material_original_engine_light_pixel_variant(ps.data(), ps.size(), options(kind), twin, true,
                                                                        engine) == LinearMaterialResult::Applied &&
                        engine,
                    "twin");
            const auto begin = now();
            api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(twin.data()), &programs[p].twin[k].p), "twin PS");
            create_us += double(now() - begin) * 1e6 / frequency();
            ++created;
            api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(base.data()), &programs[p].base[k].p), "base PS");
            programs[p].base_words[k] = unsigned(base.size());
            programs[p].twin_words[k] = unsigned(twin.size());
        }
    }
    // Scene: camera at C looking along +z (view = world - C), m00 = m11 = 1, near 1, far 10000. The plate's world rows:
    // a rotation by `tilt` about y, translation C + (0, 0, 50).
    static constexpr double C[3] = {1000., 2000., 3000.};
    static constexpr double m22 = 10000. / 9999., m32 = -10000. / 9999.;
    struct Scene {
        float world[12], view_inverse[12], wvp[16];
        double tilt;
    };
    static Scene scene(double tilt) {
        Scene s{};
        s.tilt = tilt;
        const double c = std::cos(tilt), n = std::sin(tilt);
        const double r[9] = {c, 0, n, 0, 1, 0, -n, 0, c};
        const double t[3] = {C[0], C[1], C[2] + 50.};
        for (unsigned i = 0; i < 3; ++i) {
            for (unsigned j = 0; j < 3; ++j) s.world[i * 4 + j] = float(r[i * 3 + j]);
            s.world[i * 4 + 3] = float(t[i]);
        }
        const float vi[12] = {1, 0, 0, float(C[0]), 0, 1, 0, float(C[1]), 0, 0, 1, float(C[2])};
        std::memcpy(s.view_inverse, vi, sizeof vi);
        // WVP rows (clip_k = dot(row_k, (p, 1))): view rows are the world rows minus C, x/y scaled by 1, z by m22 + m32.
        for (unsigned j = 0; j < 4; ++j) {
            const double rel[3] = {j < 3 ? r[0 * 3 + j] : t[0] - C[0], j < 3 ? r[1 * 3 + j] : t[1] - C[1],
                                   j < 3 ? r[2 * 3 + j] : t[2] - C[2]};
            s.wvp[0 + j] = float(rel[0]);
            s.wvp[4 + j] = float(rel[1]);
            s.wvp[8 + j] = float(m22 * rel[2] + (j == 3 ? m32 : 0.));
            s.wvp[12 + j] = float(rel[2]);
        }
        return s;
    }
    void constants(unsigned p, const Scene& s, float emissive) {
        // Vertex: identity texture rows, alpha 1, no point light, the emissive (the lobe sum S), fog off.
        float vc[256][4] = {};
        const bool loop = pairs[p].loop;
        const unsigned wvp = loop ? 24 : 0, world = loop ? 28 : 7, wit = loop ? 31 : 10, vi = loop ? 34 : 13,
                       tex = loop ? 37 : 16, alpha = loop ? 39 : 18, emit = loop ? 40 : 19;
        std::memcpy(vc[wvp], s.wvp, 64);
        std::memcpy(vc[world], s.world, 48);
        // WorldIT rows: the rotation (orthonormal) with no translation.
        for (unsigned i = 0; i < 3; ++i) {
            for (unsigned j = 0; j < 3; ++j) vc[wit + i][j] = s.world[i * 4 + j];
            vc[wit + i][3] = 0.f;
        }
        std::memcpy(vc[vi], s.view_inverse, 48);
        vc[tex][0] = 1.f;
        vc[tex + 1][1] = 1.f;
        vc[alpha][0] = 1.f;
        for (unsigned k = 0; k < 3; ++k) vc[emit][k] = emissive;
        if (!loop) vc[6][0] = 1.f; // the single light's attenuation (its colour c5 stays 0)
        std::memcpy(vc[252], s.wvp, 64);       // previous rows = current (motion 0)
        api(d->SetVertexShaderConstantF(0, &vc[0][0], 256), "VS constants");
        const int i0[4] = {0, 0, 1, 0};
        api(d->SetVertexShaderConstantI(0, i0, 1), "i0");
        const BOOL b0 = FALSE;
        api(d->SetVertexShaderConstantB(0, &b0, 1), "b0");
        // Pixel: identity grading, no glow, zero light colours, unit strengths; the motion ABI; c200-c202 per draw.
        float pc[224][4] = {};
        if (loop) {
            pc[0][0] = pc[1][1] = pc[2][2] = 1.f;
            pc[4][2] = pc[6][2] = -1.f;
            pc[8][0] = 3.f;
            pc[9][0] = 6.f;
            pc[10][0] = 1.f;
            pc[11][0] = .5f;
        } else {
            pc[1][2] = -1.f;
            pc[3][0] = 3.f;
            pc[4][0] = 6.f;
            pc[5][0] = 1.f;
            pc[6][0] = .5f;
        }
        pc[216][0] = 1.f / float(size);
        pc[216][1] = 1.f / float(size);
        pc[217][3] = gain_lane; // the dynamic light-map gain lane (the light map is black but in the plate case)
        // c0-c51 only: c55-c197 and c200-c202 per twin draw (upload), c52-c54 and c198-c199 never written by the API
        // (the DEFs).
        api(d->SetPixelShaderConstantF(0, &pc[0][0], EngineLightAbi::tier_constant), "PS constants");
        api(d->SetPixelShaderConstantF(203, &pc[203][0], 21), "PS constants high");
    }
    void state(unsigned p, IDirect3DSurface9* rt, unsigned w, unsigned h) {
        api(d->SetRenderTarget(0, rt), "rt0");
        api(d->SetRenderTarget(1, motion.p), "rt1");
        api(d->SetRenderTarget(2, depth.p), "rt2");
        api(d->SetDepthStencilSurface(nullptr), "ds");
        D3DVIEWPORT9 viewport{0, 0, w, h, 0, 1};
        api(d->SetViewport(&viewport), "viewport");
        for (auto s : {D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_ZENABLE,
                       D3DRS_ZWRITEENABLE, D3DRS_STENCILENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_FOGENABLE,
                       D3DRS_LIGHTING})
            api(d->SetRenderState(s, FALSE), "rs");
        for (auto s : {D3DRS_COLORWRITEENABLE, D3DRS_COLORWRITEENABLE1, D3DRS_COLORWRITEENABLE2})
            api(d->SetRenderState(s, 15), "write");
        api(d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE), "cull");
        api(d->SetVertexDeclaration(declaration.p), "decl");
        api(d->SetStreamSource(0, plate.p, 0, sizeof(Vertex)), "stream");
        for (unsigned i = 0; i < 8; ++i) {
            api(d->SetTexture(i, nullptr), "tex");
            for (auto st : {D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER})
                api(d->SetSamplerState(i, st, D3DTEXF_POINT), "sampler");
        }
        // DEFAULT: s0 albedo, s1 specular mask, s2 light map, s3 cube; BUMPMAP: s0, s1 normal, s2 mask, s3 light map,
        // s4 cube. Albedo white, everything else zero (the specular mask zeroes the reflection).
        api(d->SetTexture(0, white.p), "s0");
        if (pairs[p].bump) {
            api(d->SetTexture(1, flat_normal.p), "s1");
            api(d->SetTexture(2, black.p), "s2");
            api(d->SetTexture(3, lightmap ? lightmap : black.p), "s3");
            api(d->SetTexture(4, cube.p), "s4");
        } else {
            api(d->SetTexture(1, black.p), "s1");
            api(d->SetTexture(2, lightmap ? lightmap : black.p), "s2");
            api(d->SetTexture(3, cube.p), "s3");
        }
        api(d->SetVertexShader(programs[p].vs.p), "set VS");
    }
    std::vector<float> read(IDirect3DSurface9* rt, D3DFORMAT format, unsigned w, unsigned h, unsigned channels) {
        Com<IDirect3DSurface9> sys;
        api(d->CreateOffscreenPlainSurface(w, h, format, D3DPOOL_SYSTEMMEM, &sys.p, nullptr), "sysmem");
        api(d->GetRenderTargetData(rt, sys.p), "readback");
        D3DLOCKED_RECT lock{};
        api(sys.p->LockRect(&lock, nullptr, D3DLOCK_READONLY), "lock sys");
        std::vector<float> out(std::size_t(w) * h * channels);
        for (unsigned y = 0; y < h; ++y) {
            const auto* row = static_cast<const unsigned char*>(lock.pBits) + std::size_t(y) * lock.Pitch;
            if (format == D3DFMT_A16B16G16R16F) {
                const auto* hrow = reinterpret_cast<const std::uint16_t*>(row);
                for (unsigned i = 0; i < w * 4; ++i) {
                    const std::uint16_t hb = hrow[i];
                    const unsigned e = (hb >> 10) & 31, m = hb & 1023;
                    float v = e == 0 ? std::ldexp(float(m), -24) : e == 31 ? INFINITY : std::ldexp(float(m | 1024), int(e) - 25);
                    out[std::size_t(y) * w * 4 + i] = (hb & 0x8000) ? -v : v;
                }
            } else
                std::memcpy(&out[std::size_t(y) * w * channels], row, std::size_t(w) * channels * 4);
        }
        sys.p->UnlockRect();
        return out;
    }
    struct Image {
        std::vector<float> color, motion, depth;
    };
    // The route's two uploads of a twin draw: the run of the draw's tier (its plates and their lights' colours, the
    // 2 run - 1 registers ending at c197: c197 alone for a one-plate ship) and the light c200-c202 (never c52-c54 or
    // c198-c199, the DEFs).
    void upload(const float* block) {
        const unsigned run = el::block_upload_run(block), first = EngineLightAbi::upload_first(run);
        api(d->SetPixelShaderConstantF(first, block + (first - EngineLightAbi::light_constant) * 4,
                                       EngineLightAbi::upload_count(run)),
            "plates");
        api(d->SetPixelShaderConstantF(EngineLightAbi::pixel_constant, block + EngineLightAbi::block_light * 4,
                                       EngineLightAbi::pixel_constant_count),
            "c200");
    }
    // `block`: the staging block of a twin draw (EngineLightAbi::block_registers rows from c55: the plates and their
    // colours, two rows never uploaded, the light at block + light_at), uploaded by upload().
    Image draw(unsigned p, unsigned k, bool twin, const Scene& s, float emissive, const float* block, bool fp16) {
        IDirect3DSurface9* rt = fp16 ? color16.p : color32.p;
        state(p, rt, size, size);
        constants(p, s, emissive);
        api(d->SetPixelShader(twin ? programs[p].twin[k].p : programs[p].base[k].p), "set PS");
        if (twin) upload(block);
        api(d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0), "clear");
        api(d->BeginScene(), "begin");
        api(d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, triangles), "draw");
        api(d->EndScene(), "end");
        Image i;
        i.color = read(rt, fp16 ? D3DFMT_A16B16G16R16F : D3DFMT_A32B32G32R32F, size, size, 4);
        i.motion = read(motion.p, D3DFMT_A32B32G32R32F, size, size, 4);
        i.depth = read(depth.p, D3DFMT_R32F, size, size, 1);
        return i;
    }
};
constexpr unsigned block_floats = EngineLightAbi::block_registers * 4, light_at = EngineLightAbi::block_light * 4;
static_assert(EngineLightAbi::light_constant == el::block_first && block_floats == el::block_floats &&
                  light_at == el::block_light && EngineLightAbi::plate_count == el::plate_slots &&
                  EngineLightAbi::plate_constant == el::plate_register(0) && el::same_runs(EngineLightAbi::plate_runs),
              "the fixture's upload block (engine_light::core::block_constants)");
constexpr unsigned max_nozzles = 80;
// The production CPU path for one ship (root 0x4000) of `count` main jets: the ship table, the plate's draw logged
// under the root, the node table, the draw's block (block_constants: slot p's plate register at
// block + plate_offset(p), its colour at block + colour_offset(p), the light at block + light_at).
static bool ship_block(const Fixture::Scene& s, const ee::Record* records, unsigned count, ep::Preset preset,
                       float out[block_floats], el::ShipTable& ships, el::NodeTable& nodes, el::DrawLog& log) {
    std::uint32_t parents[max_nozzles];
    if (count > max_nozzles) return false;
    for (unsigned i = 0; i < count; ++i) parents[i] = 0x4000;
    float scale = 1.f;
    ep::preset_scale(preset, &scale);
    el::build_ships(records, parents, nullptr, nullptr, nullptr, 0, count, nullptr, ep::default_look, scale, &ships);
    log.clear();
    log.push(0x5000, 0x4000, 9, s.world);
    el::build_nodes(ships, log, &nodes);
    const el::NodeLight* n = el::find_node(nodes, 0x5000, 9);
    for (unsigned i = 0; i < block_floats; ++i) out[i] = 0.f;
    return n && el::block_constants(*n, s.world, s.view_inverse, out);
}
static ee::Record jet_record(const double nozzle[3], const double axis[3], float size, float throttle, unsigned cluster,
                             std::uint32_t handle) {
    ee::Record r{};
    for (unsigned i = 0; i < 3; ++i) {
        r.origin[i] = float(nozzle[i]);
        r.axis[i] = float(axis[i]);
    }
    r.size = size;
    r.s = throttle;
    r.z = .25f + 1.75f * throttle;
    r.node_handle = handle;
    r.body = -1;
    r.flags = std::uint16_t(cluster << ee::cluster_shift);
    return r;
}
// The production CPU path for one light: a glow-jet record of ship root 0x4000 (its nozzle at `nozzle` world, plume
// along `axis`, value `size`, throttle s, cluster tint), the ship table, the plate's draw logged under the root, the
// node table, the draw's constants from the plate's rows.
static bool light_constants(const Fixture::Scene& s, const double nozzle[3], const double axis[3], float size, float throttle,
                            unsigned cluster, ep::Preset preset, float out[block_floats], el::ShipTable& ships,
                            el::NodeTable& nodes, el::DrawLog& log) {
    const ee::Record r = jet_record(nozzle, axis, size, throttle, cluster, 5);
    return ship_block(s, &r, 1, preset, out, ships, nodes, log);
}
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        require(argc == 2, "usage: engine_light_fixture <programs directory>");
        WNDCLASSA cls{};
        cls.lpfnWndProc = DefWindowProcA;
        cls.hInstance = GetModuleHandleA(nullptr);
        cls.lpszClassName = "X3EngineLightFixture";
        require(RegisterClassA(&cls) != 0, "window class");
        HWND window = CreateWindowA(cls.lpszClassName, "Engine light", WS_OVERLAPPEDWINDOW, 0, 0, 32, 32, nullptr,
                                    nullptr, cls.hInstance, nullptr);
        require(window != nullptr, "window");
        Com<IDirect3D9> factory;
        factory.p = Direct3DCreate9(D3D_SDK_VERSION);
        require(factory.p != nullptr, "factory");
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window;
        pp.BackBufferWidth = pp.BackBufferHeight = 32;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        Com<IDirect3DDevice9> device;
        api(factory.p->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device.p),
            "device");
        auto f = std::make_unique<Fixture>();
        f->d = device.p;
        f->pp = pp;
        f->dir = argv[1];
        f->setup();
        auto ships = std::make_unique<el::ShipTable>();
        auto nodes = std::make_unique<el::NodeTable>();
        auto log = std::make_unique<el::DrawLog>();
        for (unsigned p = 0; p < 3; ++p)
            for (unsigned k = 0; k < 2; ++k)
                std::printf("PROGRAM pair=%u vs=%s ps=%s kind=%u base_words=%u twin_words=%u\n", p, pairs[p].vs,
                            pairs[p].ps, k ? 6u : 1u, f->programs[p].base_words[k], f->programs[p].twin_words[k]);
        std::printf("CREATE twins=%u us_total=%.1f us_each=%.1f\n", f->created, f->create_us,
                    f->create_us / double(f->created));
        // Cases: pair, kind, tilt (rad), emissive S, nozzle offset from C, axis, value, throttle, cluster, preset, fp16.
        struct CaseSpec {
            unsigned pair, kind;
            double tilt;
            float emissive;
            double nozzle[3], axis[3];
            float size, s;
            unsigned cluster;
            ep::Preset preset;
            bool fp16;
        };
        const CaseSpec specs[] = {
            {0, 1, 0., 0.f, {6., -4., 35.}, {0., 0., 1.}, 10.f, .6f, ee::orange, ep::Preset::standard, false},
            {0, 1, .6, .3f, {-8., 5., 30.}, {.2, 0., .98}, 12.f, 1.f, ee::lightblue, ep::Preset::standard, false},
            {0, 6, .6, .3f, {-8., 5., 30.}, {.2, 0., .98}, 12.f, 1.f, ee::lightblue, ep::Preset::standard, false},
            {0, 1, 0., 0.f, {0., 0., 44.}, {0., 0., 1.}, 6.f, 1.f, ee::white, ep::Preset::strong, false}, // cap
            {1, 1, .6, 0.f, {-8., 5., 30.}, {.2, 0., .98}, 12.f, .8f, ee::red, ep::Preset::standard, false},
            {1, 6, .6, .2f, {10., -6., 30.}, {0., 0., 1.}, 12.f, .8f, ee::red, ep::Preset::standard, false},
            {2, 1, -.5, .2f, {5., 5., 32.}, {0., .3, .95}, 9.f, .5f, ee::green, ep::Preset::standard, false},
            {2, 1, -.5, .2f, {5., 5., 32.}, {0., .3, .95}, 9.f, .5f, ee::green, ep::Preset::standard, true},
            {1, 6, .6, .2f, {10., -6., 30.}, {0., 0., 1.}, 12.f, .8f, ee::red, ep::Preset::standard, true}};
        std::vector<float> reset_reference;
        unsigned case_id = 0;
        for (const auto& c : specs) {
            const Fixture::Scene s = Fixture::scene(c.tilt);
            double nozzle[3], axis[3], length = 0.;
            for (unsigned i = 0; i < 3; ++i) {
                nozzle[i] = Fixture::C[i] + c.nozzle[i];
                length += c.axis[i] * c.axis[i];
            }
            for (unsigned i = 0; i < 3; ++i) axis[i] = c.axis[i] / std::sqrt(length);
            float block[block_floats];
            require(light_constants(s, nozzle, axis, c.size, c.s, c.cluster, c.preset, block, *ships, *nodes, *log),
                    "light constants");
            const float* light = block + light_at;
            const unsigned k = c.kind == 6 ? 1 : 0;
            const auto base = f->draw(c.pair, k, false, s, c.emissive, block, c.fp16);
            const auto twin = f->draw(c.pair, k, true, s, c.emissive, block, c.fp16);
            if (case_id == 0) reset_reference = twin.color;
            unsigned alpha_bad = 0, motion_bad = 0, depth_bad = 0, finite_bad = 0;
            for (unsigned i = 0; i < f->size * f->size; ++i) {
                alpha_bad += std::memcmp(&base.color[i * 4 + 3], &twin.color[i * 4 + 3], 4) != 0;
                motion_bad += std::memcmp(&base.motion[i * 4], &twin.motion[i * 4], 16) != 0;
                depth_bad += std::memcmp(&base.depth[i], &twin.depth[i], 4) != 0;
                for (unsigned ch = 0; ch < 3; ++ch)
                    finite_bad += !std::isfinite(twin.color[i * 4 + ch]) || twin.color[i * 4 + ch] < 0.f;
            }
            std::printf("CASE id=%u pair=%u kind=%u tilt=%.9g emissive=%.9g fp16=%u size=%u nozzle=%.9g,%.9g,%.9g axis=%.9g,%.9g,%.9g value=%.9g s=%.9g cluster=%u preset=%u light=",
                        case_id, c.pair, c.kind, c.tilt, double(c.emissive), unsigned(c.fp16), f->size, nozzle[0],
                        nozzle[1], nozzle[2], axis[0], axis[1], axis[2], double(c.size), double(c.s), c.cluster,
                        unsigned(c.preset));
            for (unsigned i = 0; i < 12; ++i) std::printf("%s%.9g", i ? "," : "", double(light[i]));
            std::printf(" camera=%.9g,%.9g,%.9g m22=%.17g m32=%.17g\n", Fixture::C[0], Fixture::C[1], Fixture::C[2],
                        Fixture::m22, Fixture::m32);
            std::printf("INVARIANT id=%u pixels=%u alpha_bad=%u motion_bad=%u depth_bad=%u finite_bad=%u\n", case_id,
                        f->size * f->size, alpha_bad, motion_bad, depth_bad, finite_bad);
            // Every pixel: the runner's oracle (one row per pixel would be 64k rows; a stride of 2 keeps 16,384).
            for (unsigned y = 0; y < f->size; y += 2)
                for (unsigned x = 0; x < f->size; x += 2) {
                    const unsigned i = y * f->size + x;
                    std::printf("S %u %u %u %.9g %.9g %.9g %.9g %.9g %.9g %.9g %u\n", case_id, x, y,
                                double(base.color[i * 4]), double(base.color[i * 4 + 1]), double(base.color[i * 4 + 2]),
                                double(twin.color[i * 4]), double(twin.color[i * 4 + 1]), double(twin.color[i * 4 + 2]),
                                double(twin.depth[i]),
                                unsigned(std::memcmp(&base.color[i * 4], &twin.color[i * 4], 12) == 0));
                }
            ++case_id;
        }
        // A light per plate (engine-light.md "A light per plate"): ships of main nozzles in front of the facing plate
        // (tilt 0, emissive 0), axis +z, so each light sits 0.5 x value in front of its nozzle. "apart": two equal
        // nozzles (value 6, R 18) 40 units apart, beyond one reach (the Split Ocelot's head-on case scaled: the brightness
        // tie's lower handle is plate 0; before, the other nozzle's hull stayed dark); "overlap": unequal nozzles (6 and
        // 4) 16 apart, their reaches overlapping, so the per-pixel selection switches between them; "ten": the Ocelot's
        // ten main nozzles scaled (two of value 6, eight equal of 3.5, a 5 x 2 grid 20 x 30 apart, the lights 6 units in
        // front of the plate; eight plates before 2026-10-08 left two without one); "cap": 72 nozzles (values 2.9 down
        // to 2.01, a 9 x 8 grid 11 apart, the lights 3 units in front), the plate cap. The fill-only kind (no light-map
        // gain: the selecting form without plates) and the production kind (the plate form), three pairs. Rows DUAL
        // (per plate its light: c200-c201 for plate 0, else the plate register's point k.xyz / k.w, R^2 = 1 / colour.w
        // and the colour; the plate registers; the tier; the ship's plate count and drops) and S (stride 2).
        {
            struct DualSpec {
                unsigned pair, kind;
                const char* name;
            };
            const DualSpec duals[] = {{0, 1, "apart"}, {0, 6, "apart"}, {1, 6, "overlap"}, {2, 1, "overlap"},
                                      {0, 6, "ten"},   {2, 1, "ten"},   {1, 6, "cap"},     {0, 1, "cap"}};
            const Fixture::Scene s = Fixture::scene(0.);
            unsigned dual_id = 100;
            for (const auto& c : duals) {
                double x[max_nozzles], y[max_nozzles], z[max_nozzles];
                float value[max_nozzles];
                unsigned count = 2;
                if (!std::strcmp(c.name, "apart")) {
                    x[0] = 20., x[1] = -20., y[0] = y[1] = 0., z[0] = z[1] = 38., value[0] = value[1] = 6.f;
                } else if (!std::strcmp(c.name, "overlap")) {
                    x[0] = -8., x[1] = 8., y[0] = y[1] = 0., z[0] = z[1] = 40., value[0] = 6.f, value[1] = 4.f;
                } else if (!std::strcmp(c.name, "ten")) {
                    count = 10;
                    for (unsigned i = 0; i < count; ++i) {
                        value[i] = i < 2 ? 6.f : 3.5f;
                        x[i] = -40. + 20. * double(i % 5);
                        y[i] = i < 5 ? -15. : 15.;
                        z[i] = 44. - .5 * double(value[i]);
                    }
                } else {
                    count = el::plate_slots;
                    for (unsigned i = 0; i < count; ++i) {
                        value[i] = 2.9f - .0125f * float(i);
                        x[i] = -44. + 11. * double(i % 9);
                        y[i] = -38.5 + 11. * double(i / 9);
                        z[i] = 47. - .5 * double(value[i]);
                    }
                }
                static ee::Record r[max_nozzles];
                for (unsigned i = 0; i < count; ++i) {
                    const double nozzle[3] = {Fixture::C[0] + x[i], Fixture::C[1] + y[i], Fixture::C[2] + z[i]},
                                 axis[3] = {0., 0., 1.};
                    r[i] = jet_record(nozzle, axis, value[i], .8f, ee::orange, 5 + i);
                }
                float block[block_floats];
                require(ship_block(s, r, count, ep::Preset::standard, block, *ships, *nodes, *log), "dual ship");
                const unsigned plates = ships->count ? ships->lights[0].plate_count : 0u;
                const unsigned k = c.kind == 6 ? 1 : 0;
                const auto base = f->draw(c.pair, k, false, s, 0.f, block, false);
                const auto twin = f->draw(c.pair, k, true, s, 0.f, block, false);
                unsigned alpha_bad = 0, motion_bad = 0, depth_bad = 0, finite_bad = 0;
                for (unsigned i = 0; i < f->size * f->size; ++i) {
                    alpha_bad += std::memcmp(&base.color[i * 4 + 3], &twin.color[i * 4 + 3], 4) != 0;
                    motion_bad += std::memcmp(&base.motion[i * 4], &twin.motion[i * 4], 16) != 0;
                    depth_bad += std::memcmp(&base.depth[i], &twin.depth[i], 4) != 0;
                    for (unsigned ch = 0; ch < 3; ++ch)
                        finite_bad += !std::isfinite(twin.color[i * 4 + ch]) || twin.color[i * 4 + ch] < 0.f;
                }
                std::printf("DUAL id=%u pair=%u kind=%u name=%s size=%u cluster=%u s=%.9g preset=%u alpha_bad=%u motion_bad=%u depth_bad=%u finite_bad=%u count=%u plates=%u plates_dropped=%u nozzles=",
                            dual_id, c.pair, c.kind, c.name, f->size, unsigned(ee::orange), .8, unsigned(ep::Preset::standard),
                            alpha_bad, motion_bad, depth_bad, finite_bad, count, plates, ships->stats.plates_dropped);
                for (unsigned i = 0; i < count; ++i)
                    std::printf("%s%.9g,%.9g,%.9g,%.9g", i ? ";" : "", x[i], y[i], z[i], double(value[i]));
                std::printf(" lights=");
                for (unsigned p = 0; p < plates; ++p) {
                    const float* key = block + el::plate_offset(p);
                    const float* colour = p ? block + el::colour_offset(p) : block + light_at + 4;
                    if (!p)
                        for (unsigned i = 0; i < 4; ++i) std::printf("%s%.9g", i ? "," : "", double(block[light_at + i]));
                    else
                        std::printf(";%.9g,%.9g,%.9g,%.9g", double(key[0]) / double(key[3]), double(key[1]) / double(key[3]),
                                    double(key[2]) / double(key[3]), 1. / double(colour[3]));
                    for (unsigned i = 0; i < 4; ++i) std::printf(",%.9g", double(colour[i]));
                }
                std::printf(" keys=");
                for (unsigned p = 0; p < plates; ++p)
                    for (unsigned i = 0; i < 4; ++i)
                        std::printf("%s%.9g", p || i ? "," : "", double(block[el::plate_offset(p) + i]));
                std::printf(" tier=%.9g\n", double(block[light_at + 11]));
                for (unsigned yy = 0; yy < f->size; yy += 2)
                    for (unsigned xx = 0; xx < f->size; xx += 2) {
                        const unsigned i = yy * f->size + xx;
                        std::printf("S %u %u %u %.9g %.9g %.9g %.9g %.9g %.9g %.9g %u\n", dual_id, xx, yy,
                                    double(base.color[i * 4]), double(base.color[i * 4 + 1]), double(base.color[i * 4 + 2]),
                                    double(twin.color[i * 4]), double(twin.color[i * 4 + 1]), double(twin.color[i * 4 + 2]),
                                    double(twin.depth[i]),
                                    unsigned(std::memcmp(&base.color[i * 4], &twin.color[i * 4], 12) == 0));
                    }
                ++dual_id;
            }
        }
        // Nozzle plates (engine-light.md "Nozzle plates"): the light-map term's gain near the light. The plate faces the
        // camera at 50 units (tilt 0), the light placed on it at the centre pixel (128, 128: d = 0; nozzle 0.5 x value
        // in front of the plate, axis +z), value 12 (R 36). Per pair (DEFAULT: light map s2, BUMPMAP: s3) and mode, the
        // same program and constants drawn with the light map white (RGB 1) and black: the difference is the light-map
        // term's contribution (the final adds it). Modes: twin = the production twin (kind 6) with c217.w 4; gain1 =
        // the same twin with c217.w 1; nolight = its base (no twin bound, no c200-c202), c217.w 4; nogain = the twin of
        // the fill-only kind (no light-map gain). Rows P pair mode x y r g b (stride 4).
        {
            // Ships of several main nozzles (twin3, twin8, twin10, twin72: the production twin at gain 4): each nozzle at camera-relative
            // (x, y, 50 - 0.5 value), axis +z, s 1, white, so its plate point lies on the plate; distinct values (the
            // plate order is by brightness).
            const Fixture::Scene s = Fixture::scene(0.);
            const float value = 12.f;
            struct Nozzle {
                double x, y;
                float value;
            };
            const Nozzle one[] = {{0., 0., value}};
            const Nozzle three[] = {{-25., -20., 12.f}, {22., -18., 9.f}, {0., 24., 7.f}};
            Nozzle eight[8];
            for (unsigned i = 0; i < 8; ++i) {
                const double angle = (22.5 + 45. * i) * 3.14159265358979323846 / 180.;
                eight[i] = {32. * std::cos(angle), 32. * std::sin(angle), 4.f + .5f * float(i)};
            }
            // Ten (the Ocelot's count: two of 6, eight equal of 3.5, a 5 x 2 grid 18 x 36 apart) and the cap of 72
            // (values 2.9 down to 2.01, a 9 x 8 grid 11 apart).
            Nozzle ten[10];
            for (unsigned i = 0; i < 10; ++i) ten[i] = {-36. + 18. * double(i % 5), i < 5 ? -18. : 18., i < 2 ? 6.f : 3.5f};
            static Nozzle cap[el::plate_slots];
            for (unsigned i = 0; i < el::plate_slots; ++i)
                cap[i] = {-44. + 11. * double(i % 9), -38.5 + 11. * double(i / 9), 2.9f - .0125f * float(i)};
            struct Ship {
                const Nozzle* nozzles;
                unsigned count;
                float block[block_floats];
            } ships_of[5] = {{one, 1, {}}, {three, 3, {}}, {eight, 8, {}}, {ten, 10, {}}, {cap, el::plate_slots, {}}};
            for (auto& sh : ships_of) {
                static ee::Record r[max_nozzles];
                for (unsigned i = 0; i < sh.count; ++i) {
                    const double nozzle[3] = {Fixture::C[0] + sh.nozzles[i].x, Fixture::C[1] + sh.nozzles[i].y,
                                              Fixture::C[2] + 50. - 0.5 * double(sh.nozzles[i].value)},
                                 axis[3] = {0., 0., 1.};
                    r[i] = jet_record(nozzle, axis, sh.nozzles[i].value, 1.f, ee::white, 5 + i);
                }
                require(ship_block(s, r, sh.count, ep::Preset::standard, sh.block, *ships, *nodes, *log), "plate ship");
            }
            struct Mode {
                const char* name;
                unsigned k;
                bool twin;
                float gain;
                unsigned ship;
            };
            const Mode modes[] = {{"twin", 1, true, 4.f, 0},   {"gain1", 1, true, 1.f, 0}, {"nolight", 1, false, 4.f, 0},
                                  {"nogain", 0, true, 4.f, 0}, {"twin3", 1, true, 4.f, 1}, {"twin8", 1, true, 4.f, 2},
                                  {"twin10", 1, true, 4.f, 3}, {"twin72", 1, true, 4.f, 4}};
            for (unsigned p = 0; p < 2; ++p)
                for (const auto& m : modes) {
                    const Ship& sh = ships_of[m.ship];
                    const float* light = sh.block + light_at;
                    f->gain_lane = m.gain;
                    f->lightmap = f->white_rgb.p;
                    const auto lit = f->draw(p, m.k, m.twin, s, 0.f, sh.block, false);
                    f->lightmap = nullptr;
                    const auto dark = f->draw(p, m.k, m.twin, s, 0.f, sh.block, false);
                    unsigned finite_bad = 0;
                    for (float v : lit.color) finite_bad += !std::isfinite(v);
                    std::printf("PLATE pair=%u mode=%s twin=%u gain=%.9g value=%.9g finite_bad=%u light=", p, m.name,
                                unsigned(m.twin), double(m.gain), double(sh.nozzles[0].value), finite_bad);
                    for (unsigned i = 0; i < 12; ++i) std::printf("%s%.9g", i ? "," : "", double(light[i]));
                    std::printf(" nozzles=");
                    for (unsigned i = 0; i < sh.count; ++i)
                        std::printf("%s%.9g,%.9g,%.9g,%.9g", i ? ";" : "", sh.nozzles[i].x, sh.nozzles[i].y,
                                    50. - 0.5 * double(sh.nozzles[i].value), double(sh.nozzles[i].value));
                    // The plate registers of the run (slot order; pads after the ship's plates).
                    std::printf(" plates=");
                    const unsigned run = el::block_upload_run(sh.block);
                    for (unsigned p = 0; p < run; ++p)
                        for (unsigned i = 0; i < 4; ++i)
                            std::printf("%s%.9g", p || i ? "," : "", double(sh.block[el::plate_offset(p) + i]));
                    std::printf("\n");
                    for (unsigned y = 0; y < f->size; y += 4)
                        for (unsigned x = 0; x < f->size; x += 4) {
                            const unsigned i = (y * f->size + x) * 4;
                            std::printf("P %u %s %u %u %.9g %.9g %.9g\n", p, m.name, x, y,
                                        double(lit.color[i] - dark.color[i]), double(lit.color[i + 1] - dark.color[i + 1]),
                                        double(lit.color[i + 2] - dark.color[i + 2]));
                        }
                }
            f->gain_lane = gain;
            f->lightmap = nullptr;
        }
        // Reset: the render targets go (D3DPOOL_DEFAULT), the device resets, the targets come back; the shaders and
        // managed resources survive. Case 0 drawn again must match its readback bit for bit.
        {
            f->release_targets();
            f->back.reset();
            api(device.p->Reset(&pp), "Reset");
            api(device.p->GetRenderTarget(0, &f->back.p), "back after Reset");
            f->targets(f->size, f->size);
            const auto& c = specs[0];
            const Fixture::Scene s = Fixture::scene(c.tilt);
            double nozzle[3];
            for (unsigned i = 0; i < 3; ++i) nozzle[i] = Fixture::C[i] + c.nozzle[i];
            float block[block_floats];
            require(light_constants(s, nozzle, c.axis, c.size, c.s, c.cluster, c.preset, block, *ships, *nodes, *log),
                    "light after Reset");
            const auto twin = f->draw(c.pair, 0, true, s, c.emissive, block, false);
            unsigned differ = 0;
            for (std::size_t i = 0; i < twin.color.size(); ++i)
                differ += std::memcmp(&twin.color[i], &reset_reference[i], 4) != 0;
            std::printf("RESET channels=%u differ=%u\n", unsigned(twin.color.size()), differ);
        }
        // Per-draw CPU cost of the production path (i686 build, the device's own SetPixelShaderConstantF): a table of
        // 256 ships x 4 nodes, lookups half hits; a hit computes the constants and the plate and uploads four
        // registers (c197, c200-c202). The ship table's build over 1,024 records of 128 ships x 8 main nozzles (the plates full; best of
        // 20 builds).
        double ships_us = 1e30;
        {
            static ee::Record r[1024];
            static std::uint32_t parent[1024];
            for (unsigned i = 0; i < 1024; ++i) {
                const double nozzle[3] = {double(i / 8) * 1000. + double(i % 8) * 40., double(i % 8) * 7., 0.},
                             axis[3] = {0., 0., 1.};
                r[i] = jet_record(nozzle, axis, 10.f + float(i % 8), .5f, ee::white, 1000 + i);
                parent[i] = 0x100000u + (i / 8) * 64u;
            }
            for (unsigned rep = 0; rep < 20; ++rep) {
                const auto begin = now();
                el::build_ships(r, parent, nullptr, nullptr, nullptr, 0, 1024, nullptr, ep::default_look, 1.f,
                                ships.get());
                const double us = double(now() - begin) * 1e6 / frequency();
                ships_us = us < ships_us ? us : ships_us;
            }
            require(ships->count == 128 && ships->lights[0].plate_count == 8, "ship table of 128 x 8 plates");
        }
        {
            static ee::Record r[256];
            static std::uint32_t parent[256];
            for (unsigned i = 0; i < 256; ++i) {
                r[i] = ee::Record{};
                r[i].origin[0] = float(i) * 100.f;
                r[i].axis[2] = 1.f;
                r[i].size = 10.f;
                r[i].s = .5f;
                r[i].node_handle = 1000 + i;
                r[i].body = -1;
                parent[i] = 0x100000u + i * 64u;
            }
            el::build_ships(r, parent, nullptr, nullptr, nullptr, 0, 256, nullptr, ep::default_look, 1.f, ships.get());
            const Fixture::Scene s = Fixture::scene(.3);
            log->clear();
            for (unsigned i = 0; i < 256; ++i)
                for (unsigned k = 0; k < 4; ++k) log->push(0x900000u + i * 64u + k * 4u, parent[i], k, s.world);
            auto log_cost = std::make_unique<el::DrawLog>(*log); // the same 1,024 draws for the plate cases below
            const auto build_begin = now();
            el::build_nodes(*ships, *log, nodes.get());
            const double build_us = double(now() - build_begin) * 1e6 / frequency();
            float c[block_floats] = {};
            constexpr unsigned rounds = 200000;
            unsigned hits = 0;
            const auto hit_begin = now();
            for (unsigned j = 0; j < rounds; ++j) {
                const unsigned i = (j * 2654435761u) % 256u;
                const el::NodeLight* n = el::find_node(*nodes, 0x900000u + i * 64u + (j & 3u) * 4u, j & 3u);
                if (n && el::block_constants(*n, s.world, s.view_inverse, c)) {
                    ++hits;
                    const unsigned run = el::block_upload_run(c), first = EngineLightAbi::upload_first(run);
                    device.p->SetPixelShaderConstantF(first, c + (first - EngineLightAbi::light_constant) * 4,
                                                      EngineLightAbi::upload_count(run));
                    device.p->SetPixelShaderConstantF(EngineLightAbi::pixel_constant, c + light_at, EngineLightAbi::pixel_constant_count);
                }
            }
            const double hit_ns = double(now() - hit_begin) * 1e9 / frequency() / rounds;
            unsigned misses = 0;
            const auto miss_begin = now();
            for (unsigned j = 0; j < rounds; ++j)
                misses += el::find_node(*nodes, 0x7f00000u + j * 8u, 0u) == nullptr;
            const double miss_ns = double(now() - miss_begin) * 1e9 / frequency() / rounds;
            log->clear();
            const auto log_begin = now();
            for (unsigned j = 0; j < rounds; ++j) {
                if (log->count >= el::log_capacity) log->clear();
                log->push(0x900000u + (j % 4000u) * 4u, 0x100000u, j & 3u, s.world);
            }
            const double log_ns = double(now() - log_begin) * 1e9 / frequency() / rounds;
            std::printf("COST rounds=%u nodes=%u ships=%u build_us=%.2f hits=%u hit_ns=%.1f misses=%u miss_ns=%.1f log_ns=%.1f ships_us=%.2f\n",
                        rounds, nodes->count, nodes->ships, build_us, hits, hit_ns, misses, miss_ns, log_ns, ships_us);
            // The same lookups on ships of 8 and of 72 main nozzles (each hit computes and uploads the tier's run: 15 and
            // 143 registers below c198): the per-draw cost grows with the plates in use. The node table's build over
            // the same 1,024 logged draws, and the ship table's build over the ring's 1,024 records (128 ships x 8,
            // 14 ships x 72; best of 20).
            static ee::Record rn[256 * el::plate_slots];
            static std::uint32_t pn[256 * el::plate_slots];
            for (const unsigned m : {8u, el::plate_slots}) {
                for (unsigned i = 0; i < 256; ++i)
                    for (unsigned k = 0; k < m; ++k) {
                        const double nozzle[3] = {double(i) * 1000. + double(k) * 9., double(k) * 3., 0.},
                                     axis[3] = {0., 0., 1.};
                        rn[i * m + k] = jet_record(nozzle, axis, 10.f - float(k) / float(m), .5f, ee::white, 1000 + i * m + k);
                        pn[i * m + k] = 0x100000u + i * 64u;
                    }
                double ring_us = 1e30;
                for (unsigned rep = 0; rep < 20; ++rep) {
                    const auto begin = now();
                    el::build_ships(rn, pn, nullptr, nullptr, nullptr, 0, (1024 / m) * m, nullptr, ep::default_look, 1.f,
                                    ships.get());
                    const double us = double(now() - begin) * 1e6 / frequency();
                    ring_us = us < ring_us ? us : ring_us;
                }
                el::build_ships(rn, pn, nullptr, nullptr, nullptr, 0, 256 * m, nullptr, ep::default_look, 1.f, ships.get());
                const auto node_begin = now();
                el::build_nodes(*ships, *log_cost, nodes.get());
                const double node_us = double(now() - node_begin) * 1e6 / frequency();
                unsigned hits_m = 0, plates_m = 0;
                const auto begin = now();
                for (unsigned j = 0; j < rounds; ++j) {
                    const unsigned i = (j * 2654435761u) % 256u;
                    const el::NodeLight* n = el::find_node(*nodes, 0x900000u + i * 64u + (j & 3u) * 4u, j & 3u);
                    if (n && el::block_constants(*n, s.world, s.view_inverse, c)) {
                        ++hits_m;
                        plates_m = n->plate_count;
                        const unsigned run = el::block_upload_run(c), first = EngineLightAbi::upload_first(run);
                        device.p->SetPixelShaderConstantF(first, c + (first - EngineLightAbi::light_constant) * 4,
                                                          EngineLightAbi::upload_count(run));
                        device.p->SetPixelShaderConstantF(EngineLightAbi::pixel_constant, c + light_at,
                                                          EngineLightAbi::pixel_constant_count);
                    }
                }
                const double ns = double(now() - begin) * 1e9 / frequency() / rounds;
                std::printf("COSTN plates=%u nodes=%u hits=%u rounds=%u hit_ns=%.1f build_us=%.2f ships_us=%.2f\n",
                            plates_m, nodes->count, hits_m, rounds, ns, node_us, ring_us);
            }
        }
        // GPU cost of the term over a full-screen hull: the production kind (share, gain, widening) of the BUMPMAP
        // pair, base against twin, each drawn 40 times per batch, five batches, EVENT-fenced; the plate fills the target
        // (tilt 0, 1:1 projection at 50 units: +-120 covers NDC +-2.4). Ships of 1, 3, 8, 10 and 72 main nozzles (the
        // twin's uniform branches run 1, 4, 8, 12 and 72 plate slots).
        {
            const Fixture::Scene s = Fixture::scene(0.);
            constexpr unsigned timed = 5;
            static float blocks[timed][block_floats];
            const unsigned counts[timed] = {1, 3, 8, 10, el::plate_slots};
            for (unsigned sh = 0; sh < timed; ++sh) {
                static ee::Record r[max_nozzles];
                for (unsigned i = 0; i < counts[sh]; ++i) {
                    const double nozzle[3] = {Fixture::C[0] + 20. * double(i % 8), Fixture::C[1] + 20. * double(i / 8),
                                              Fixture::C[2] + 40.},
                                 axis[3] = {0., 0., 1.};
                    r[i] = jet_record(nozzle, axis, 10.f - .5f * float(i % 8) - .01f * float(i / 8), 1.f, ee::white, 5 + i);
                }
                require(ship_block(s, r, counts[sh], ep::Preset::standard, blocks[sh], *ships, *nodes, *log), "timing light");
            }
            for (unsigned sh = 0; sh < timed; ++sh)
            for (const auto& size : {std::pair<unsigned, unsigned>{1920, 1080}, std::pair<unsigned, unsigned>{5120, 1440}}) {
                const float* block = blocks[sh];
                f->release_targets();
                f->targets(size.first, size.second);
                Com<IDirect3DQuery9> query;
                api(device.p->CreateQuery(D3DQUERYTYPE_EVENT, &query.p), "query");
                double ms[2][5] = {};
                for (unsigned batch = 0; batch < 6; ++batch)
                    for (unsigned twin = 0; twin < 2; ++twin) {
                        f->state(1, f->color16.p, size.first, size.second);
                        f->constants(1, s, .2f);
                        // Additive blending: every draw shades every pixel (a tile-based GPU removes fully overdrawn
                        // opaque fragments, which would time one draw out of forty).
                        api(device.p->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE), "blend");
                        api(device.p->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE), "src");
                        api(device.p->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE), "dst");
                        api(device.p->SetPixelShader(twin ? f->programs[1].twin[1].p : f->programs[1].base[1].p), "PS");
                        if (twin)
                            f->upload(block);
                        api(device.p->BeginScene(), "begin");
                        query.p->Issue(D3DISSUE_END);
                        while (query.p->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {
                        }
                        const auto begin = now();
                        for (unsigned n = 0; n < 20; ++n) api(device.p->DrawPrimitive(D3DPT_TRIANGLELIST, 0, Fixture::triangles), "draw");
                        query.p->Issue(D3DISSUE_END);
                        while (query.p->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {
                        }
                        api(device.p->EndScene(), "end");
                        if (batch) ms[twin][batch - 1] = double(now() - begin) * 1e3 / frequency() / 20.;
                    }
                std::printf("GPU width=%u height=%u nozzles=%u tier=%.0f base_ms=", size.first, size.second, counts[sh],
                            double(block[light_at + 11]));
                for (unsigned b = 0; b < 5; ++b) std::printf("%s%.4f", b ? "," : "", ms[0][b]);
                std::printf(" twin_ms=");
                for (unsigned b = 0; b < 5; ++b) std::printf("%s%.4f", b ? "," : "", ms[1][b]);
                std::printf("\n");
            }
            f->release_targets();
            f->targets(f->size, f->size);
        }
        api(device.p->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE), "blend off");
        std::printf("DONE\n");
        // Teardown: unbind everything, release the fixture's objects, then the device and the factory.
        for (unsigned i = 0; i < 8; ++i) device.p->SetTexture(i, nullptr);
        device.p->SetRenderTarget(2, nullptr);
        device.p->SetRenderTarget(1, nullptr);
        device.p->SetRenderTarget(0, f->back.p);
        device.p->SetStreamSource(0, nullptr, 0, 0);
        device.p->SetVertexDeclaration(nullptr);
        device.p->SetVertexShader(nullptr);
        device.p->SetPixelShader(nullptr);
        f.reset();
        std::printf("TEARDOWN fixture\n");
        const ULONG left = device.p->Release();
        device.p = nullptr;
        std::printf("TEARDOWN device references=%lu\n", left);
        factory.reset();
        DestroyWindow(window);
        std::printf("TEARDOWN done\n");
        // Every object is released (device references 0). The process exit itself hung under Wine/FEX on 2 of 5 runs
        // after this point (2026-10-03, the runner's timeout); end the process without the loader's detach path.
        std::fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 0);
    } catch (const std::exception& e) {
        std::printf("FAIL %s\n", e.what());
        return 1;
    }
    return 0;
}
