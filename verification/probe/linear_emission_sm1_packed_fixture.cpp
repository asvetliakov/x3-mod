// Mathematical packed-screen prototype only. No live return/publication policy.
// Native game programs remain local; fixture shaders/resources are authored.
// Step E law (docs/architecture/screen-emission-region.md): the source writes
// the red|blue plane lanes only (native B accumulation and the modified
// flag), the green lane keeps decode(A) from the initialization, and the C
// assembly decodes once: C = encode(g decode(B) + (1 - g) decode(A)); at
// g = 1 C is the native B exactly. Schedule 3 draws an 8-layer overlapping
// chain in one DIP.
#define WIN32_LEAN_AND_MEAN
#include "../../src/renderer/linear_emission_sm1.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <d3d9.h>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
namespace {
using Words = std::vector<std::uint32_t>;
constexpr unsigned W = 32, H = 32;
void need(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}
void api(HRESULT hr, const char* why) {
    if (FAILED(hr)) {
        std::printf("SM1_API hr=%08lx label=%s\n", hr, why);
        throw std::runtime_error(why);
    }
}
template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { reset(); }
    void reset() {
        if (p) {
            p->Release();
            p = nullptr;
        }
    }
    T* operator->() const { return p; }
};
struct Pair {
    const char *vs, *ps;
    unsigned layout;
}; // 0 DEFAULT, 1 INSTANCE, 2 bullet.
constexpr Pair pairs[] = {{"0d44b36d48d24f7a", "078494828322bcca", 0}, {"1b6863a088a177af", "84d3de8887c963c5", 2},
                          {"21a2c13be7f989c3", "d4a26efb7c603931", 2}, {"5e484a06672e28fb", "ec1f5c4a2f4e1445", 2},
                          {"637dadcb5efa3288", "078494828322bcca", 1}, {"6da1b1b6ed63ec82", "2ea025492d370c8e", 0},
                          {"88620f88d6e0a00e", "a5c3495e27270b4a", 0}, {"ed42e0742e47dca4", "2ea025492d370c8e", 1},
                          {"f9755e1154244f58", "a5c3495e27270b4a", 1}};
std::uint64_t hash(const Words& w) {
    std::uint64_t h = 14695981039346656037ull;
    for (auto v : w)
        for (unsigned k = 0; k < 4; ++k) {
            h ^= (v >> (k * 8)) & 255;
            h *= 1099511628211ull;
        }
    return h;
}
Words load(const std::string& folder, const char* kind, const char* id) {
    std::ifstream in(folder + "/" + kind + "_" + id + ".bin", std::ios::binary | std::ios::ate);
    need(bool(in), "local original missing");
    auto n = in.tellg();
    need(n > 0 && n % 4 == 0, "original word framing");
    Words w(std::size_t(n) / 4);
    in.seekg(0);
    in.read(reinterpret_cast<char*>(w.data()), n);
    need(bool(in), "original read");
    need(hash(w) == std::stoull(id, nullptr, 16), "whole original fingerprint");
    return w;
}
float fp16(unsigned short h) {
    unsigned s = unsigned(h & 0x8000) << 16, e = (h >> 10) & 31, m = h & 1023, b;
    if (!e) {
        if (!m)
            b = s;
        else {
            int shift = 0;
            while (!(m & 1024)) {
                m <<= 1;
                ++shift;
            }
            b = s | unsigned(127 - 14 - shift) << 23 | (m & 1023) << 13;
        }
    } else
        b = s | ((e == 31 ? 255 : e + 112) << 23) | (m << 13);
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}
unsigned short half(float f) {
    unsigned b;
    std::memcpy(&b, &f, 4);
    unsigned s = (b >> 16) & 0x8000, m = b & 0x7fffff;
    int e = int((b >> 23) & 255) - 127 + 15;
    if ((b & 0x7fffffffu) > 0x7f800000u) return static_cast<unsigned short>(s | 0x7e00);
    if (e >= 31) return static_cast<unsigned short>(s | 0x7c00);
    if (e <= 0) {
        if (e < -10) return static_cast<unsigned short>(s);
        m |= 0x800000;
        unsigned sh = unsigned(14 - e), v = m >> sh;
        unsigned rem = m & ((1u << sh) - 1);
        v += rem > (1u << (sh - 1)) || (rem == (1u << (sh - 1)) && (v & 1));
        return static_cast<unsigned short>(s | v);
    }
    unsigned v = m >> 13, rem = m & 8191;
    v += rem > 4096 || (rem == 4096 && (v & 1));
    return static_cast<unsigned short>(s + (unsigned(e) << 10) + v);
}
DWORD reg(unsigned t, unsigned i) {
    return 0x80000000u | ((t & 7) << 28) | ((t & 24) << 8) | i;
}
DWORD dst(unsigned t, unsigned i, unsigned mask = 15, bool pp = false) {
    return reg(t, i) | (mask << 16) | (pp ? 0x200000u : 0);
}
DWORD src(unsigned t, unsigned i, unsigned sw = 0xe4) {
    return reg(t, i) | (sw << 16);
}
void ins(Words& w, unsigned op, std::initializer_list<DWORD> a) {
    w.push_back(op | (unsigned(a.size()) << 24));
    w.insert(w.end(), a.begin(), a.end());
}
// Independently authored measurement shader: sample, raw COLOR multiplier and
// survival mask only. No gamma, gain, sanitization or native source
// multiplication.
Words witness(bool bullet, bool pp) {
    Words w{0xffff0200u};
    ins(w, 31, {0x80000000u, dst(3, 0, 3)});
    ins(w, 31, {0x80000000u, dst(1, 0, bullet ? 8 : 7)});
    ins(w, 31, {0x90000000u, dst(10, 0)});
    ins(w, 81, {dst(2, 0), 0x3f800000u, 0, 0, 0});
    ins(w, 66, {dst(0, 0, 15, pp), src(3, 0), src(10, 0)});
    ins(w, 1, {dst(8, 0), src(0, 0)});
    ins(w, 1, {dst(0, 1, 7), src(1, 0, bullet ? 0xff : 0)});
    ins(w, 1, {dst(0, 1, 8), src(0, 0, 0xff)});
    ins(w, 1, {dst(8, 1), src(0, 1)});
    ins(w, 1, {dst(0, 2), src(2, 0, 0)});
    ins(w, 1, {dst(8, 2), src(0, 2)});
    w.push_back(0xffffu);
    return w;
}
using Image = std::vector<unsigned short>;
struct Vertex {
    float x, y, z, u, v;
    DWORD color;
};
constexpr const char* cases[] = {"asymmetric",
                                 "zero_channel",
                                 "zero_rgb",
                                 "q_one",
                                 "gain_zero",
                                 "gain_quarter",
                                 "gain_2_5",
                                 "alpha_ge127_128",
                                 "alpha_ge128_129",
                                 "alpha_gt127_128",
                                 "alpha_gt128_129",
                                 "accepted_alpha_zero",
                                 "depth_one",
                                 "depth_none",
                                 "mask_seed",
                                 "signed_q_boundary",
                                 "q_gt1_boundary",
                                 "hdr_boundary",
                                 "overflow_boundary",
                                 "flat",
                                 "clip",
                                 "perspective",
                                 "uv_flip",
                                 "fog_layout"};
constexpr unsigned CASES = sizeof cases / sizeof cases[0];
bool boundary(unsigned c) {
    return c >= 15 && c <= 18;
}
float gain_for(unsigned c) {
    return c == 4 ? 0.f : c == 5 || c == 17 ? .25f : c == 6 ? 2.5f : 1.f;
}
unsigned gain_index(unsigned c) {
    return c == 4 ? 1 : c == 5 || c == 17 ? 2 : c == 6 ? 3 : 0;
}
constexpr float gains[] = {1, 0, .25f, 2.5f};
constexpr unsigned SCHEDULES = 4, CHAIN_LAYERS = 8;
unsigned layers_of(unsigned schedule) {
    return schedule == 3 ? CHAIN_LAYERS : 2;
}
void literal(Words& w, unsigned regno, float a, float b, float c, float d) {
    DWORD v[4];
    const float f[] = {a, b, c, d};
    std::memcpy(v, f, sizeof v);
    ins(w, 81, {dst(2, regno), v[0], v[1], v[2], v[3]});
}
Words fullscreen_vs() {
    Words w{0xfffe0200u};
    ins(w, 31, {0x80000000u, dst(1, 0)});
    ins(w, 31, {0x80000005u, dst(1, 1, 3)});
    ins(w, 1, {dst(4, 0), src(1, 0)});
    ins(w, 1, {dst(6, 0, 3), src(1, 1)});
    w.push_back(0xffffu);
    return w;
}
// Init=0, native B assembly=1, C assembly=2 (step E: decode(P.x) once, gain
// as the `def c1` literal (g, 1 - g, 2.2, 1e-10)). All operations consume
// owned immutable planes/A only; no source geometry is replayed for assembly.
Words screen_ps(unsigned kind, float gain = 1.f) {
    Words w{0xffff0200u};
    const unsigned samples = kind == 0 ? 1 : kind == 1 ? 4 : 5, output = kind == 1 ? 4 : 5;
    ins(w, 31, {0x80000000u, dst(3, 0, 3)});
    for (unsigned i = 0; i < samples; ++i) ins(w, 31, {0x90000000u, dst(10, i)});
    if (kind != 1) literal(w, 0, 0, kind ? 1.f / 2.2f : 2.2f, kind ? 1e-22f : 1e-10f, 1);
    if (kind == 2) literal(w, 1, gain, 1.f - gain, 2.2f, 1e-10f);
    for (unsigned i = 0; i < samples; ++i) ins(w, 66, {dst(0, i), src(3, 0), src(10, i)});
    if (!kind) {
        ins(w, 1, {dst(8, 0), src(0, 0)});
        for (unsigned c = 0; c < 3; ++c) {
            ins(w, 1, {dst(0, 1, 1), src(0, 0, c * 0x55)});
            ins(w, 11, {dst(0, 1, 2), src(0, 1, 0), src(2, 0, 0xaa)});
            ins(w, 32, {dst(0, 1, 2), src(0, 1, 0x55), src(2, 0, 0x55)});
            ins(w, 1, {dst(0, 1, 12), src(2, 0, 0)});
            ins(w, 1, {dst(8, c + 1), src(0, 1)});
        }
    } else {
        for (unsigned c = 0; c < 3; ++c) {
            if (kind == 1)
                ins(w, 1, {dst(0, output, 1u << c), src(0, c, 0)});
            else {
                // r6.x = g decode(max(P.x, 1e-10)) + (1 - g) P.y, clamped at 0
                ins(w, 11, {dst(0, 6, 1), src(0, c, 0), src(2, 1, 0xff)});
                ins(w, 32, {dst(0, 6, 1), src(0, 6, 0), src(2, 1, 0xaa)});
                ins(w, 5, {dst(0, 6, 1), src(0, 6, 0), src(2, 1, 0)});
                ins(w, 4, {dst(0, 6, 1), src(0, c, 0x55), src(2, 1, 0x55), src(0, 6, 0)});
                ins(w, 11, {dst(0, 6, 1), src(0, 6, 0), src(2, 0, 0)});
                // r6.y = encode(r6.x) with an exact zero; unmodified channels copy A
                ins(w, 11, {dst(0, 6, 2), src(0, 6, 0), src(2, 0, 0xaa)});
                ins(w, 32, {dst(0, 6, 2), src(0, 6, 0x55), src(2, 0, 0x55)});
                ins(w, 88, {dst(0, 6, 2), src(0, 6, 0) | 0x1000000u, src(2, 0, 0), src(0, 6, 0x55)});
                ins(w, 35, {dst(0, 6, 4), src(0, c, 0xaa)});
                ins(w, 88,
                    {dst(0, output, 1u << c), src(0, 6, 0xaa) | 0x1000000u, src(0, 4, c * 0x55), src(0, 6, 0x55)});
            }
        }
        ins(w, 1, {dst(0, output, 8), src(0, 3, 0xff)});
        ins(w, 1, {dst(8, 0), src(0, output)});
    }
    w.push_back(0xffffu);
    return w;
}
void shader_info(const char* name, const Words& w) {
    unsigned alu = 0, tex = 0, temps = 0, samplers = 0, outputs = 0, constants = 0;
    for (std::size_t at = 1; w[at] != 0xffffu;) {
        unsigned op = w[at] & 0xffff, n = (w[at] >> 24) & 15;
        if (op == 81) ++constants;
        if (op != 31 && op != 81) {
            if (op == 66)
                ++tex;
            else
                alu += op == 32 ? 3 : 1;
            for (unsigned j = 1; j <= n; ++j) {
                auto v = w[at + j];
                unsigned type = ((v >> 28) & 7) | ((v >> 8) & 24), index = v & 2047;
                if (type == 0) temps = std::max(temps, index + 1);
                if (type == 10) samplers = std::max(samplers, index + 1);
                if (j == 1 && type == 8) outputs |= 1u << index;
            }
        }
        at += n + 1;
    }
    std::printf("PACKED_HELPER name=%s words=%zu alu=%u tex=%u temps=%u "
                "samplers=%u outputs=%u constants=%u\n",
                name, w.size(), alu, tex, temps, samplers, outputs, constants);
}
struct Target {
    Com<IDirect3DTexture9> texture;
    Com<IDirect3DSurface9> surface;
    void release() {
        surface.reset();
        texture.reset();
    }
};
struct Fixture {
    unsigned width, height;
    HWND window = nullptr;
    D3DPRESENT_PARAMETERS pp{};
    D3DCAPS9 caps{};
    Com<IDirect3D9> d3d;
    Com<IDirect3DDevice9> d;
    Target a, m, planes[3], b, c, native;
    Com<IDirect3DSurface9> readback, depth;
    Com<IDirect3DVertexDeclaration9> decl, full_decl;
    Com<IDirect3DVertexBuffer9> vb;
    Com<IDirect3DIndexBuffer9> ib;
    Com<IDirect3DTexture9> atlas;
    Com<IDirect3DVertexShader9> full_vs;
    Com<IDirect3DPixelShader9> screen[3], composite[4]; // composite[g]: C assembly at gains[g]
    Com<IDirect3DQuery9> event;
    LARGE_INTEGER frequency{};
    Fixture(unsigned w, unsigned h)
        : width(w)
        , height(h) {
        window = CreateWindowA("STATIC", "Packed screen mathematical probe", WS_OVERLAPPEDWINDOW, 0, 0, 80, 80, nullptr,
                               nullptr, GetModuleHandleA(nullptr), nullptr);
        need(window != nullptr, "window");
        d3d.p = Direct3DCreate9(D3D_SDK_VERSION);
        need(d3d.p != nullptr, "D3D9");
        pp.BackBufferWidth = w;
        pp.BackBufferHeight = h;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        pp.BackBufferCount = 1;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window;
        pp.Windowed = TRUE;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        api(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp,
                              &d.p),
            "device");
        api(d->GetDeviceCaps(&caps), "caps");
        const bool masks = caps.PrimitiveMiscCaps & D3DPMISCCAPS_INDEPENDENTWRITEMASKS,
                   post = caps.PrimitiveMiscCaps & D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING;
        std::printf("PACKED_CAPS width=%u height=%u mrt=%lu masks=%u post_blend=%u "
                    "src_one=%u dst_invsrcalpha=%u ps1_max=%.9g\n",
                    w, h, caps.NumSimultaneousRTs, unsigned(masks), unsigned(post),
                    unsigned(bool(caps.SrcBlendCaps & D3DPBLENDCAPS_ONE)),
                    unsigned(bool(caps.DestBlendCaps & D3DPBLENDCAPS_INVSRCALPHA)), double(caps.PixelShader1xMaxValue));
        need(caps.NumSimultaneousRTs >= 4 && masks && post && (caps.SrcBlendCaps & D3DPBLENDCAPS_ONE) &&
                 (caps.DestBlendCaps & D3DPBLENDCAPS_INVSRCALPHA),
             "four-MRT packed blend caps");
        D3DDISPLAYMODE display{};
        api(d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &display), "display format");
        api(d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, display.Format,
                                   D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_TEXTURE,
                                   D3DFMT_A16B16G16R16F),
            "FP16 render blend");
        const D3DVERTEXELEMENT9 el[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                        {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                        {0, 20, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
                                        D3DDECL_END()};
        const D3DVERTEXELEMENT9 full[] = {{0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                          {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                          D3DDECL_END()};
        api(d->CreateVertexDeclaration(el, &decl.p), "native declaration");
        api(d->CreateVertexDeclaration(full, &full_decl.p), "assembly declaration");
        api(d->CreateVertexBuffer(4 * CHAIN_LAYERS * sizeof(Vertex), 0, 0, D3DPOOL_MANAGED, &vb.p, nullptr),
            "source VB");
        api(d->CreateIndexBuffer(12 * CHAIN_LAYERS, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib.p, nullptr), "source IB");
        const auto code = fullscreen_vs();
        api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()), &full_vs.p), "assembly VS");
        for (unsigned i = 0; i < 3; ++i) {
            auto ps = screen_ps(i);
            const char* names[] = {"initialize", "assemble_b", "assemble_c"};
            shader_info(names[i], ps);
            api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(ps.data()), &screen[i].p), "init/assembly PS");
        }
        for (unsigned g = 0; g < 4; ++g) {
            auto ps = screen_ps(2, gains[g]);
            api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(ps.data()), &composite[g].p), "gain C assembly PS");
        }
        need(QueryPerformanceFrequency(&frequency), "QPC");
        targets();
    }
    ~Fixture() {
        if (window) DestroyWindow(window);
    }
    void target(Target& t) {
        api(d->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT,
                             &t.texture.p, nullptr),
            "owned target");
        api(t.texture->GetSurfaceLevel(0, &t.surface.p), "target surface");
    }
    void targets() {
        for (auto* t : {&a, &m, &planes[0], &planes[1], &planes[2], &b, &c, &native}) target(*t);
        api(d->CreateOffscreenPlainSurface(width, height, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &readback.p,
                                           nullptr),
            "readback");
        api(d->CreateDepthStencilSurface(width, height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &depth.p, nullptr),
            "depth");
        api(d->CreateQuery(D3DQUERYTYPE_EVENT, &event.p), "EVENT");
        D3DLOCKED_RECT lock{};
        api(readback->LockRect(&lock, nullptr, 0), "A upload lock");
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
                auto* row = reinterpret_cast<unsigned short*>(static_cast<unsigned char*>(lock.pBits) + y * lock.Pitch);
                const float rgba[] = {.2f + float(x % 3) / 128, .4f + float(y % 3) / 128, .6f, .375f};
                for (unsigned k = 0; k < 4; ++k) row[x * 4 + k] = half(rgba[k]);
            }
        api(readback->UnlockRect(), "A upload unlock");
        api(d->UpdateSurface(readback.p, nullptr, a.surface.p, nullptr), "immutable A upload");
    }
    void detach() {
        for (unsigned i = 0; i < 5; ++i) api(d->SetTexture(i, nullptr), "detach sample");
        for (unsigned i = 1; i < 4; ++i) api(d->SetRenderTarget(i, nullptr), "detach MRT");
    }
    void reset() {
        detach();
        Com<IDirect3DSurface9> back;
        api(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back.p), "Reset backbuffer");
        api(d->SetRenderTarget(0, back.p), "Reset RT0");
        back.reset();
        api(d->SetDepthStencilSurface(nullptr), "Reset depth");
        for (auto* t : {&a, &m, &planes[0], &planes[1], &planes[2], &b, &c, &native}) t->release();
        readback.reset();
        depth.reset();
        event.reset();
        api(d->Reset(&pp), "Reset");
        targets();
        std::puts("PACKED_RESET passed=1");
    }
    Image image(Target& t) {
        api(d->GetRenderTargetData(t.surface.p, readback.p), "read target");
        D3DLOCKED_RECT lock{};
        api(readback->LockRect(&lock, nullptr, D3DLOCK_READONLY), "read lock");
        Image im(width * height * 4);
        for (unsigned y = 0; y < height; ++y)
            std::memcpy(im.data() + y * width * 4, static_cast<unsigned char*>(lock.pBits) + y * lock.Pitch, width * 8);
        api(readback->UnlockRect(), "read unlock");
        return im;
    }
    void states(bool source, unsigned which, bool packed) {
        const D3DVIEWPORT9 viewport{0, 0, width, height, 0, 1};
        api(d->SetViewport(&viewport), "viewport");
        const std::pair<D3DRENDERSTATETYPE, DWORD> states[] = {
            {D3DRS_ZENABLE, source},
            {D3DRS_ZWRITEENABLE, FALSE},
            {D3DRS_ZFUNC, D3DCMP_LESSEQUAL},
            {D3DRS_ALPHABLENDENABLE, source},
            {D3DRS_SRCBLEND, D3DBLEND_ONE},
            {D3DRS_DESTBLEND, packed ? D3DBLEND_INVSRCALPHA : D3DBLEND_INVSRCCOLOR},
            {D3DRS_BLENDOP, D3DBLENDOP_ADD},
            {D3DRS_SEPARATEALPHABLENDENABLE, FALSE},
            {D3DRS_ALPHATESTENABLE, source && which >= 7 && which <= 10},
            {D3DRS_ALPHAFUNC, which == 9 || which == 10 ? D3DCMP_GREATER : D3DCMP_GREATEREQUAL},
            {D3DRS_ALPHAREF, 128},
            {D3DRS_SHADEMODE, which == 19 ? D3DSHADE_FLAT : D3DSHADE_GOURAUD},
            {D3DRS_CULLMODE, D3DCULL_NONE},
            {D3DRS_FILLMODE, D3DFILL_SOLID},
            {D3DRS_STENCILENABLE, FALSE},
            {D3DRS_SCISSORTESTENABLE, FALSE},
            {D3DRS_FOGENABLE, FALSE},
            {D3DRS_DITHERENABLE, FALSE},
            {D3DRS_SRGBWRITEENABLE, FALSE},
            {D3DRS_CLIPPING, TRUE},
            {D3DRS_CLIPPLANEENABLE, 0},
            {D3DRS_WRAP0, 0}};
        for (auto s : states) api(d->SetRenderState(s.first, s.second), "state");
        for (unsigned stage = 0; stage < 5; ++stage) {
            for (auto s : {D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER})
                api(d->SetSamplerState(stage, s, D3DTEXF_POINT), "point sample");
            api(d->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE), "no mip");
            api(d->SetSamplerState(stage, D3DSAMP_MIPMAPLODBIAS, 0), "bias zero");
            api(d->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP), "clamp U");
            api(d->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP), "clamp V");
            api(d->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, FALSE), "no sRGB");
        }
        api(d->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE), "unprojected");
    }
    void masks(DWORD zero, DWORD planes_mask) {
        const D3DRENDERSTATETYPE rs[] = {D3DRS_COLORWRITEENABLE, D3DRS_COLORWRITEENABLE1, D3DRS_COLORWRITEENABLE2,
                                         D3DRS_COLORWRITEENABLE3};
        for (unsigned i = 0; i < 4; ++i)
            api(d->SetRenderState(rs[i], i ? planes_mask : zero), "independent output mask");
    }
    void full_draw(unsigned kind) {
        api(d->SetVertexShader(full_vs.p), "full VS");
        api(d->SetPixelShader(screen[kind].p), "full PS");
        api(d->SetVertexDeclaration(full_decl.p), "full declaration");
        const float l = -1 - 1.f / width, r = 1 - 1.f / width, t = 1 + 1.f / height, bot = -1 + 1.f / height;
        const float quad[] = {l, t, 0, 1, 0, 0, r, t, 0, 1, 1, 0, l, bot, 0, 1, 0, 1, r, bot, 0, 1, 1, 1};
        api(d->BeginScene(), "full BeginScene");
        api(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, 24), "owned full draw");
        api(d->EndScene(), "full EndScene");
    }
    void seed(unsigned which) {
        detach();
        api(d->ColorFill(m.surface.p, nullptr, 0), "M initial clear");
        if (which == 14) {
            RECT left{0, 0, LONG(width / 2), LONG(height)};
            api(d->ColorFill(m.surface.p, &left, D3DCOLOR_ARGB(0, 255, 0, 0)), "M prior red seed");
        }
        for (auto& p : planes)
            api(d->ColorFill(p.surface.p, nullptr, D3DCOLOR_ARGB(64, 0, 0, 0)), "plane alpha sentinel");
    }
    void initialize() {
        detach();
        api(d->SetDepthStencilSurface(nullptr), "init no depth");
        api(d->SetRenderTarget(0, m.surface.p), "init M");
        for (unsigned i = 0; i < 3; ++i) api(d->SetRenderTarget(i + 1, planes[i].surface.p), "init plane");
        states(false, 0, true);
        masks(8, 7);
        api(d->SetTexture(0, a.texture.p), "init A");
        full_draw(0);
    }
    void assemble(bool enhanced, unsigned gain = 0) {
        detach();
        api(d->SetDepthStencilSurface(nullptr), "assembly no depth");
        api(d->SetRenderTarget(0, enhanced ? c.surface.p : b.surface.p), "assembly target");
        states(false, 0, false);
        masks(15, 15);
        for (unsigned i = 0; i < 3; ++i) api(d->SetTexture(i, planes[i].texture.p), "assembly plane");
        api(d->SetTexture(3, m.texture.p), "assembly M");
        if (enhanced) api(d->SetTexture(4, a.texture.p), "assembly immutable A");
        if (enhanced) {
            api(d->SetVertexShader(full_vs.p), "full VS");
            api(d->SetPixelShader(composite[gain].p), "gain C assembly");
            api(d->SetVertexDeclaration(full_decl.p), "full declaration");
            const float l = -1 - 1.f / width, r = 1 - 1.f / width, t = 1 + 1.f / height, bot = -1 + 1.f / height;
            const float quad[] = {l, t, 0, 1, 0, 0, r, t, 0, 1, 1, 0, l, bot, 0, 1, 0, 1, r, bot, 0, 1, 1, 1};
            api(d->BeginScene(), "full BeginScene");
            api(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, 24), "owned full draw");
            api(d->EndScene(), "full EndScene");
        } else
            full_draw(1);
    }
    void prepare(unsigned which, unsigned layout, bool reverse, unsigned layers = 2) {
        atlas.reset();
        const bool unorm = which >= 7 && which <= 10;
        api(d->CreateTexture(2, 1, 1, 0, unorm ? D3DFMT_A8R8G8B8 : D3DFMT_A16B16G16R16F, D3DPOOL_MANAGED, &atlas.p,
                             nullptr),
            "particle atlas");
        float texels[2][4] = {{.5f, .25f, .125f, .25f}, {.125f, .625f, .375f, .75f}};
        if (which == 1) texels[0][2] = texels[1][2] = 0;
        if (which == 2)
            for (auto& t : texels) t[0] = t[1] = t[2] = 0;
        if (which == 3)
            for (auto& t : texels) t[0] = t[1] = t[2] = 1;
        if (which == 11) texels[0][3] = texels[1][3] = 0;
        if (which == 15) {
            texels[0][0] = -.25f;
            texels[1][1] = -.5f;
        }
        if (which == 16) {
            texels[0][0] = 1.5f;
            texels[1][0] = 2.f;
            texels[1][1] = 2.f;
        }
        if (which == 17)
            for (auto& t : texels) t[0] = t[1] = t[2] = 256.f;
        if (which == 18)
            for (auto& t : texels) t[0] = t[1] = t[2] = 65504.f;
        D3DLOCKED_RECT lock{};
        api(atlas->LockRect(0, &lock, nullptr, 0), "atlas lock");
        for (unsigned i = 0; i < 2; ++i) {
            if (unorm) {
                const unsigned alpha = (which == 8 || which == 10 ? 128 : 127) + i;
                DWORD color = (alpha << 24) | (DWORD(std::lround(texels[i][0] * 255)) << 16) |
                              (DWORD(std::lround(texels[i][1] * 255)) << 8) | DWORD(std::lround(texels[i][2] * 255));
                std::memcpy(static_cast<unsigned char*>(lock.pBits) + i * 4, &color, 4);
            } else
                for (unsigned k = 0; k < 4; ++k)
                    static_cast<unsigned short*>(lock.pBits)[i * 4 + k] = half(texels[i][k]);
        }
        api(atlas->UnlockRect(0), "atlas unlock");
        // Two particles (schedules 0-2) or the 8-layer chain (schedule 3): quad i
        // spans [-.9 + .1 i, .1 + .1 i], so the strip x in [-.2, .1] carries all
        // eight layers and the coverage falls off to one layer at both ends;
        // particle i uses atlas texel i % 2 and the alternating vertex alpha.
        Vertex vertices[4 * CHAIN_LAYERS];
        for (unsigned i = 0; i < layers; ++i) {
            float l = layers > 2 ? -.9f + .1f * float(i)
                      : i        ? -.65f
                                 : -.85f,
                  r = layers > 2 ? .1f + .1f * float(i)
                      : i        ? .85f
                                 : .65f;
            if (which == 20) {
                l -= .7f;
                r += .1f;
            }
            float z = (which == 13 || (which == 12 && i == 1)) ? .8f : i ? .3f : .2f;
            unsigned alpha = (which == 3 || which == 15 || which == 16 || which == 18) ? 255
                             : which == 17                                             ? 32
                             : i % 2                                                   ? 192
                                                                                       : 128;
            const float u = i % 2 ? .75f : .25f;
            DWORD color = (alpha << 24) | 0x3070d0u;
            Vertex quad[] = {{l, .8f, z, u, .5f, color},
                             {r, .8f, z, u, .5f, color},
                             {l, -.8f, z, u, .5f, color},
                             {r, -.8f, z, u, .5f, color}};
            std::memcpy(vertices + i * 4, quad, sizeof quad);
        }
        void* data = nullptr;
        api(vb->Lock(0, 0, &data, 0), "source vertices lock");
        std::memcpy(data, vertices, sizeof vertices);
        api(vb->Unlock(), "source vertices unlock");
        unsigned short indices[6 * CHAIN_LAYERS];
        const unsigned short quad[] = {0, 1, 2, 2, 1, 3};
        for (unsigned i = 0; i < layers; ++i)
            for (unsigned k = 0; k < 6; ++k)
                indices[i * 6 + k] = static_cast<unsigned short>(quad[k] + 4 * (reverse && layers == 2 ? 1 - i : i));
        api(ib->Lock(0, 0, &data, 0), "source indices lock");
        std::memcpy(data, indices, sizeof indices);
        api(ib->Unlock(), "source indices unlock");
        const float matrix[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, which == 21 ? .25f : 0, 0, 0, 1};
        api(d->SetVertexShaderConstantF(0, matrix, 4), "native VP/WVP");
        if (layout != 2) {
            const float world[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1}, camera[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 3},
                        uv[] = {which == 22 ? -1.f : 1.f, 0, which == 22 ? 1.f : 0.f, 0, 0, 1, 0, 0};
            const float flag[] = {float(which == 23), 0, 0, 0},
                        fade[] = {which == 3 || which == 15 || which == 16 || which == 18 ? 1.f
                                  : which == 17                                           ? .125f
                                                                                          : .5f,
                                  0, 0, 0},
                        fog[] = {1.f, .3f, 0, 0};
            api(d->SetVertexShaderConstantF(4, world, 3), "native world");
            api(d->SetVertexShaderConstantF(7, camera, 3), "native camera");
            if (layout == 0) api(d->SetVertexShaderConstantF(10, uv, 2), "native DEFAULT UV");
            unsigned base = layout == 0 ? 12 : 10;
            api(d->SetVertexShaderConstantF(base, flag, 1), "native float fog flag");
            api(d->SetVertexShaderConstantF(base + 1, fade, 1), "native alpha multiplier");
            api(d->SetVertexShaderConstantF(base + 2, fog, 1), "native fog clip");
        }
    }
    void bind_source(unsigned which, IDirect3DVertexShader9* vs, IDirect3DPixelShader9* ps, bool packed,
                     bool measuring = false) {
        states(true, which, packed);
        if (measuring) api(d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE), "raw measurement no blend");
        masks(packed && !measuring ? 9 : 15, packed && !measuring ? 5 : 15);
        api(d->SetTexture(0, atlas.p), "original atlas");
        api(d->SetVertexShader(vs), "untouched VS");
        api(d->SetPixelShader(ps), "source PS");
        api(d->SetVertexDeclaration(decl.p), "native declaration");
        api(d->SetStreamSource(0, vb.p, 0, sizeof(Vertex)), "source stream");
        api(d->SetStreamSourceFreq(0, 1), "source stream frequency");
        api(d->SetIndices(ib.p), "source indices");
        api(d->SetDepthStencilSurface(depth.p), "source depth");
    }
    void clear_depth() {
        api(d->SetDepthStencilSurface(depth.p), "depth clear bind");
        api(d->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, .5f, 0), "depth initialize");
    }
    void submit(unsigned schedule, int single = -1) {
        const unsigned vertices = 4 * layers_of(schedule);
        api(d->BeginScene(), "source BeginScene");
        if (single >= 0)
            api(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, vertices, unsigned(single) * 6, 2),
                "measurement particle DIP");
        else if (schedule == 1) {
            api(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 8, 0, 2), "ordered source DIP1");
            api(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 8, 6, 2), "ordered source DIP2");
        } else
            api(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, vertices, 0, 2 * layers_of(schedule)),
                "one original overlapping DIP");
        api(d->EndScene(), "source EndScene");
    }
    std::array<Image, 3> measure(unsigned which, IDirect3DVertexShader9* vs, IDirect3DPixelShader9* ps,
                                 unsigned particle, unsigned schedule) {
        detach();
        for (auto& p : planes) api(d->ColorFill(p.surface.p, nullptr, 0), "raw witness clear");
        for (unsigned i = 0; i < 3; ++i) api(d->SetRenderTarget(i, planes[i].surface.p), "measurement output");
        clear_depth();
        bind_source(which, vs, ps, false, true);
        submit(schedule, int(particle));
        return {image(planes[0]), image(planes[1]), image(planes[2])};
    }
    void native_begin(unsigned which, IDirect3DVertexShader9* vs, IDirect3DPixelShader9* ps) {
        detach();
        api(d->StretchRect(a.surface.p, nullptr, native.surface.p, nullptr, D3DTEXF_NONE), "native A seed");
        api(d->SetRenderTarget(0, native.surface.p), "native B target");
        clear_depth();
        bind_source(which, vs, ps, false);
    }
    void packed_begin(unsigned which, IDirect3DVertexShader9* vs, IDirect3DPixelShader9* ps) {
        detach();
        api(d->SetRenderTarget(0, m.surface.p), "packed M");
        for (unsigned i = 0; i < 3; ++i) api(d->SetRenderTarget(i + 1, planes[i].surface.p), "packed channel");
        clear_depth();
        bind_source(which, vs, ps, true);
    }
    void fence() {
        api(event->Issue(D3DISSUE_END), "EVENT issue");
        HRESULT hr;
        while ((hr = event->GetData(nullptr, 0, D3DGETDATA_FLUSH)) == S_FALSE) Sleep(0);
        api(hr, "EVENT complete");
    }
    template <class F> double timed(F action) {
        fence();
        LARGE_INTEGER start, end;
        QueryPerformanceCounter(&start);
        action();
        fence();
        QueryPerformanceCounter(&end);
        return 1000. * double(end.QuadPart - start.QuadPart) / frequency.QuadPart;
    }
};
void raw(const std::string& name, const Image& image) {
    FILE* f = std::fopen(name.c_str(), "wb");
    need(f != nullptr, "raw witness open");
    need(std::fwrite(image.data(), 2, image.size(), f) == image.size(), "raw witness bytes");
    std::fclose(f);
}
double store(double value) {
    return fp16(half(float(value)));
}
// Step E publication law on the stored lanes: C = encode(g decode(B) +
// (1 - g) decode(A)), B the stored native lane, decode(A) the stored
// initialization, exact zero preserved.
double composed(double stored_b, double stored_linear_a, double gain) {
    const double l = std::max(gain * std::pow(std::max(stored_b, 1e-10), 2.2) + (1 - gain) * stored_linear_a, 0.);
    return l > 0 ? store(std::pow(l, 1. / 2.2)) : 0.;
}
struct Metrics {
    unsigned b_diff = 0, alpha_diff = 0, c_diff = 0, plane_diff = 0, mask_diff = 0, alpha_mask_diff = 0,
             unchanged_diff = 0, init_diff = 0, nonfinite = 0, surviving = 0, overlap = 0, flag_zero_changed = 0,
             unchanged_covered = 0, max_layers = 0, native_c_diff = 0, native_c_off_by_one = 0;
    double max_fraction = 0;
    bool failed() const {
        return b_diff || alpha_diff || c_diff || plane_diff || mask_diff || alpha_mask_diff || unchanged_diff ||
               init_diff || nonfinite;
    }
};
void close_value(double got, double wanted, unsigned& failures, Metrics& m, double factor = 1) {
    if (!std::isfinite(got) || !std::isfinite(wanted)) {
        ++m.nonfinite;
        if (!(std::isnan(got) && std::isnan(wanted)) && got != wanted) ++failures;
        return;
    }
    const double fraction = std::abs(got - wanted) / (factor * (.00008 + .006 * std::abs(wanted)));
    m.max_fraction = std::max(m.max_fraction, fraction);
    if (fraction > 1) ++failures;
}
struct Result {
    Image native, b, c, mask, initial_mask;
    std::array<Image, 3> planes, initial_planes;
    std::array<Image, 3> ref[CHAIN_LAYERS];
    unsigned layers = 2;
    Metrics metrics;
};
void check(Result& r, const Image& a, unsigned which) {
    auto& m = r.metrics;
    const double gain = gain_for(which);
    for (unsigned p = 0; p < W * H; ++p) {
        unsigned i = p * 4;
        double mask = which == 14 && p % W < W / 2 ? 1 : 0;
        const double seed = mask;
        double alpha = fp16(a[i + 3]);
        if (r.initial_mask[i] != half(float(seed)) || r.initial_mask[i + 3] != a[i + 3]) ++m.init_diff;
        bool live[CHAIN_LAYERS] = {};
        unsigned count = 0;
        for (unsigned s = 0; s < r.layers; ++s) {
            live[s] = r.ref[s][2][i] != 0;
            count += live[s];
        }
        m.surviving += count;
        m.overlap += count >= 2;
        m.max_layers = std::max(m.max_layers, count);
        for (unsigned s = 0; s < r.layers; ++s)
            if (live[s]) {
                double source_a = fp16(r.ref[s][0][i + 3]);
                mask = store(1 + (1 - source_a) * mask);
                alpha = store(source_a + (1 - source_a) * alpha);
            }
        close_value(fp16(r.mask[i]), mask, m.mask_diff, m);
        close_value(fp16(r.mask[i + 3]), alpha, m.alpha_diff, m);
        if (r.b[i + 3] != r.native[i + 3] || r.c[i + 3] != r.native[i + 3]) ++m.alpha_diff;
        for (unsigned k = 1; k < 3; ++k)
            if (r.mask[i + k] != r.initial_mask[i + k]) ++m.mask_diff;
        for (unsigned k = 0; k < 3; ++k) {
            const auto& plane = r.planes[k];
            const auto& initial = r.initial_planes[k];
            double encoded = fp16(a[i + k]), linear = store(std::pow(encoded, 2.2)), modified = 0;
            if (initial[i] != a[i + k] || initial[i + 2] != 0) ++m.init_diff;
            close_value(fp16(initial[i + 1]), linear, m.init_diff, m);
            // The green lane is never written by the source (plane masks 5): it
            // stays the independently validated stored decode(A), the pre-draw
            // decoded value the publication subtracts.
            linear = fp16(initial[i + 1]);
            for (unsigned s = 0; s < r.layers; ++s)
                if (live[s]) {
                    double t = fp16(r.ref[s][0][i + k]), h = fp16(r.ref[s][1][i]), q = t * h;
                    encoded = store(q + (1 - q) * encoded);
                    modified = store(q + (1 - q) * modified);
                }
            close_value(fp16(plane[i]), encoded, m.plane_diff, m);
            if (plane[i + 1] != initial[i + 1]) ++m.plane_diff;
            close_value(fp16(plane[i + 2]), modified, m.plane_diff, m);
            if (plane[i + 3] != initial[i + 3]) ++m.alpha_mask_diff;
            if (r.b[i + k] != r.native[i + k]) ++m.b_diff;
            if (plane[i + 2] == 0 && plane[i + 1] != initial[i + 1]) ++m.flag_zero_changed;
            if (count && modified == 0) ++m.unchanged_covered;
            if (plane[i + 2] == 0) {
                if (r.c[i + k] != a[i + k]) ++m.unchanged_diff;
            } else {
                // Publication on the stored lanes; at gain 1 in domain C must be the
                // native B within one FP16 code (the GPU's POW round trip
                // encode(decode(B)) lands one code low on a few percent of the
                // values; exact matches and off-by-one codes are counted apart).
                const double wanted = composed(fp16(plane[i]), linear, gain);
                close_value(fp16(r.c[i + k]), wanted, m.c_diff, m, boundary(which) ? 2 : 1);
                if (gain == 1 && !boundary(which)) {
                    const int codes = int(r.c[i + k]) - int(r.native[i + k]);
                    if (codes > 1 || codes < -1 || (r.c[i + k] & 0x8000) != (r.native[i + k] & 0x8000))
                        ++m.native_c_diff;
                    else if (codes)
                        ++m.native_c_off_by_one;
                }
            }
        }
    }
}
Result run_case(Fixture& f, unsigned which, unsigned layout, unsigned schedule, IDirect3DVertexShader9* vs,
                IDirect3DPixelShader9* original, IDirect3DPixelShader9* promoted, IDirect3DPixelShader9* witness_ps) {
    Result r;
    r.layers = layers_of(schedule);
    f.prepare(which, layout, schedule == 2, r.layers);
    for (unsigned s = 0; s < r.layers; ++s) r.ref[s] = f.measure(which, vs, witness_ps, s, schedule);
    f.native_begin(which, vs, original);
    f.submit(schedule);
    r.native = f.image(f.native);
    f.seed(which);
    f.initialize();
    r.initial_mask = f.image(f.m);
    for (unsigned k = 0; k < 3; ++k) r.initial_planes[k] = f.image(f.planes[k]);
    f.packed_begin(which, vs, promoted);
    f.submit(schedule);
    r.mask = f.image(f.m);
    for (unsigned k = 0; k < 3; ++k) r.planes[k] = f.image(f.planes[k]);
    f.assemble(false);
    r.b = f.image(f.b);
    f.assemble(true, gain_index(which));
    r.c = f.image(f.c);
    check(r, f.image(f.a), which);
    return r;
}
void failure_files(const std::string& prefix, const Result& r) {
    raw(prefix + "_native.rgba16f", r.native);
    raw(prefix + "_b.rgba16f", r.b);
    raw(prefix + "_c.rgba16f", r.c);
    raw(prefix + "_mask.rgba16f", r.mask);
    raw(prefix + "_initial_mask.rgba16f", r.initial_mask);
    for (unsigned k = 0; k < 3; ++k) {
        raw(prefix + "_plane" + std::to_string(k) + ".rgba16f", r.planes[k]);
        raw(prefix + "_initial_plane" + std::to_string(k) + ".rgba16f", r.initial_planes[k]);
        for (unsigned s = 0; s < r.layers; ++s)
            raw(prefix + "_ref" + std::to_string(s) + "_" + std::to_string(k) + ".rgba16f", r.ref[s][k]);
    }
}
void benchmark(const char* programs) {
    Fixture f(1920, 1080);
    auto vw = load(programs, "vs", pairs[0].vs), pw = load(programs, "ps", pairs[0].ps);
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> original, packed;
    api(f.d->CreateVertexShader(reinterpret_cast<const DWORD*>(vw.data()), &vs.p), "benchmark native VS");
    api(f.d->CreatePixelShader(reinterpret_cast<const DWORD*>(pw.data()), &original.p), "benchmark native PS");
    x3m::renderer::LinearEmissionSm1Config config;
    config.outputs = x3m::renderer::LinearEmissionSm1Outputs::PackedScreen;
    Words code;
    need(x3m::renderer::linear_emission_sm1_pixel_variant(pw.data(), pw.size(), config, code) ==
             x3m::renderer::LinearEmissionResult::Applied,
         "benchmark promotion");
    HRESULT hr = f.d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &packed.p);
    if (FAILED(hr)) {
        std::printf("PACKED_BENCH_UNSUPPORTED hr=%08lx\n", hr);
        return;
    }
    f.prepare(0, pairs[0].layout, false);
    const char* phases[] = {"native_dip", "initialize", "packed_dip", "assemble_b", "assemble_c"};
    for (unsigned sample = 0; sample < 12; ++sample) {
        double times[5];
        f.native_begin(0, vs.p, original.p);
        times[0] = f.timed([&] { f.submit(0); });
        f.seed(0);
        times[1] = f.timed([&] { f.initialize(); });
        f.packed_begin(0, vs.p, packed.p);
        times[2] = f.timed([&] { f.submit(0); });
        times[3] = f.timed([&] { f.assemble(false); });
        times[4] = f.timed([&] { f.assemble(true); });
        if (sample >= 4)
            for (unsigned phase = 0; phase < 5; ++phase)
                std::printf("PACKED_TIMING sample=%u phase=%s ms=%.9g\n", sample - 4, phases[phase], times[phase]);
    }
    std::printf("PACKED_BENCH_COMPLETE samples=8 warmups=4 width=1920 height=1080 "
                "original_dips=1 packed_dips=1 init_draws=1 b_draws=1 c_draws=1 layers=2 "
                "owned_targets=8 target_bytes=%llu live_publication=0\n",
                8ull * 1920 * 1080 * 8);
}
} // namespace
int main(int argc, char** argv) {
    try {
        need(argc == 2 || (argc == 3 && !std::strcmp(argv[2], "benchmark")),
             "usage: packed.exe local-program-directory [benchmark]");
        if (argc == 3) {
            benchmark(argv[1]);
            return 0;
        }
        Fixture f(W, H);
        const auto a = f.image(f.a);
        unsigned rows = 0, unsupported = 0, failures = 0, boundary_failures = 0, order_changed = 0;
        bool saved[2]{};
        for (unsigned p = 0; p < 9; ++p) {
            const auto& pair = pairs[p];
            auto vw = load(argv[1], "vs", pair.vs), pw = load(argv[1], "ps", pair.ps);
            need(vw[0] == 0xfffe0101u && pw[0] == 0xffff0101u, "untouched native models");
            need(x3m::renderer::linear_emission_sm1_pair_reviewed(hash(vw), hash(pw)), "exact native pair");
            Com<IDirect3DVertexShader9> vs;
            Com<IDirect3DPixelShader9> original, measure, packed[4];
            HRESULT created[4];
            api(f.d->CreateVertexShader(reinterpret_cast<const DWORD*>(vw.data()), &vs.p), "native VS");
            api(f.d->CreatePixelShader(reinterpret_cast<const DWORD*>(pw.data()), &original.p), "native PS");
            auto measuring = witness(pair.layout == 2, false);
            api(f.d->CreatePixelShader(reinterpret_cast<const DWORD*>(measuring.data()), &measure.p),
                "raw sample/multiplier/coverage witness");
            for (unsigned g = 0; g < 4; ++g) {
                x3m::renderer::LinearEmissionSm1Config config;
                config.outputs = x3m::renderer::LinearEmissionSm1Outputs::PackedScreen;
                config.gain = gains[g];
                Words code;
                need(x3m::renderer::linear_emission_sm1_pixel_variant(pw.data(), pw.size(), config, code) ==
                         x3m::renderer::LinearEmissionResult::Applied,
                     "packed transformer");
                created[g] = f.d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &packed[g].p);
                std::printf("PACKED_CREATE pair=%u gain_index=%u words=%zu hr=%08lx\n", p, g, code.size(), created[g]);
            }
            for (unsigned which = 0; which < CASES; ++which) {
                Image previous_b, previous_c, previous_mask;
                std::array<Image, 3> previous_planes;
                for (unsigned schedule = 0; schedule < SCHEDULES; ++schedule) {
                    ++rows;
                    const unsigned g = gain_index(which);
                    if (FAILED(created[g])) {
                        ++unsupported;
                        std::printf("PACKED_CASE pair=%u case=%u name=%s schedule=%u "
                                    "status=unsupported hr=%08lx boundary=%u\n",
                                    p, which, cases[which], schedule, created[g], unsigned(boundary(which)));
                        continue;
                    }
                    auto r = run_case(f, which, pair.layout, schedule, vs.p, original.p, packed[g].p, measure.p);
                    auto& m = r.metrics;
                    unsigned same_dip_diff = 0, changed = 0;
                    if (schedule == 1) {
                        same_dip_diff = unsigned(r.b != previous_b) + unsigned(r.c != previous_c) +
                                        unsigned(r.mask != previous_mask);
                        for (unsigned k = 0; k < 3; ++k) same_dip_diff += unsigned(r.planes[k] != previous_planes[k]);
                    }
                    if (schedule == 2) {
                        for (unsigned i = 0; i < r.c.size(); ++i)
                            if (i % 4 < 3 && r.c[i] != previous_c[i]) ++changed;
                        if (which == 0) order_changed += changed;
                    }
                    if (schedule == 0) {
                        previous_b = r.b;
                        previous_c = r.c;
                        previous_mask = r.mask;
                        previous_planes = r.planes;
                    }
                    const bool failed = m.failed() || same_dip_diff || m.native_c_diff;
                    if (boundary(which))
                        boundary_failures += failed;
                    else
                        failures += failed;
                    std::printf("PACKED_CASE pair=%u case=%u name=%s schedule=%u status=measured "
                                "boundary=%u b_diff=%u alpha_diff=%u c_diff=%u plane_diff=%u "
                                "mask_diff=%u alpha_mask_diff=%u unchanged_diff=%u init_diff=%u "
                                "nonfinite=%u surviving=%u overlap=%u same_dip_diff=%u "
                                "order_changed=%u flag_zero_changed=%u unchanged_covered=%u "
                                "max_fraction=%.9g "
                                "original_dips=%u packed_dips=%u layers=%u max_layers=%u "
                                "native_c_diff=%u native_c_off_by_one=%u\n",
                                p, which, cases[which], schedule, unsigned(boundary(which)), m.b_diff, m.alpha_diff,
                                m.c_diff, m.plane_diff, m.mask_diff, m.alpha_mask_diff, m.unchanged_diff, m.init_diff,
                                m.nonfinite, m.surviving, m.overlap, same_dip_diff, changed, m.flag_zero_changed,
                                m.unchanged_covered, m.max_fraction, schedule == 1 ? 2 : 1, schedule == 1 ? 2 : 1,
                                r.layers, m.max_layers, m.native_c_diff, m.native_c_off_by_one);
                    if (failed && !saved[boundary(which)]) {
                        saved[boundary(which)] = true;
                        const std::string prefix = "failure_p" + std::to_string(p) + "_c" + std::to_string(which) +
                                                   "_s" + std::to_string(schedule);
                        failure_files(prefix, r);
                        std::printf("PACKED_WITNESS pair=%u case=%u schedule=%u "
                                    "boundary=%u prefix=%s\n",
                                    p, which, schedule, unsigned(boundary(which)), prefix.c_str());
                    }
                    // Preserve one operational >1q witness whether or not this backend
                    // clamps the source-alpha factor. Its flag cancellation must not
                    // enter qualification.
                    if (p == 0 && which == 16 && schedule == 0) {
                        failure_files("range_q2", r);
                        std::puts("PACKED_RANGE_WITNESS prefix=range_q2 "
                                  "q_clamped_by_fixture=0 qualification=0");
                    }
                }
            }
            if (p == 4) {
                f.reset();
                need(f.image(f.a) == a, "Reset restores authored immutable A");
            }
        }
        std::printf("PACKED_COMPLETE pairs=9 cases=%u schedules=%u rows=%u unsupported=%u "
                    "failures=%u boundary_failures=%u order_changed=%u reset=1 "
                    "owned_targets=8 target_bytes=%llu live_publication=0\n",
                    CASES, SCHEDULES, rows, unsupported, failures, boundary_failures, order_changed, 8ull * W * H * 8);
        return 0;
    } catch (const std::exception& e) {
        std::printf("PACKED_ABORT reason=%s\n", e.what());
        return 1;
    }
}
