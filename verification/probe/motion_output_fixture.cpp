// Original synthetic D3D9 program for the live same-draw motion route through
// the actual proxy DLL. The reviewed shader pair is read from LOCAL files at
// runtime; no game bytes are embedded. Geometry, textures and constants are
// original. Modes: "production" (plain build/d3d9.dll: fill, restoration,
// Reset, no scene recognition) and "seam" (fixture DLL exporting the
// X3M_MOTION_OUTPUT_FIXTURE seam: synthetic scope + own background signature).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#define X3M_MOTION_OUTPUT_FIXTURE
#include "../../src/proxy/motion_output.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using Words = std::vector<std::uint32_t>;
namespace {
unsigned checks = 0, restorations = 0, motion_checked = 0, motion_matched = 0, frames_verified = 0;
double max_uv_pixels = 0, max_depth_error = 0;
void api(HRESULT h, const char* what) {
    if (FAILED(h)) { std::printf("API FAIL %s %08lx\n", what, h); throw std::runtime_error(what); }
}
void require(bool ok, const char* label) {
    ++checks; std::printf("CHECK %s %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) throw std::runtime_error(label);
}
template<class T> struct Com {
    T* p = nullptr;
    Com() = default; Com(const Com&) = delete; Com& operator=(const Com&) = delete;
    ~Com() { reset(); }
    void reset() { if (p) { p->Release(); p = nullptr; } }
    T* operator->() const { return p; }
};
template<class T> T symbol(HMODULE m, const char* name, bool required) {
    auto raw = GetProcAddress(m, name); T fn = nullptr; std::memcpy(&fn, &raw, sizeof fn);
    if (!fn && required) throw std::runtime_error(name);
    return fn;
}
Words load(const char* name) {
    std::ifstream f(name, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("local shader missing");
    const auto size = f.tellg();
    if (size <= 0 || size % 4 || size > 65536) throw std::runtime_error("shader size");
    Words w(std::size_t(size) / 4); f.seekg(0); f.read(reinterpret_cast<char*>(w.data()), size);
    if (!f) throw std::runtime_error("shader read");
    return w;
}
std::uint64_t fnv(const void* data, std::size_t size) {
    std::uint64_t h = 14695981039346656037ull; auto b = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}
unsigned short half(float x) { // Fixture values are exactly half-representable.
    unsigned b; std::memcpy(&b, &x, 4); unsigned e = (b >> 23) & 255;
    return static_cast<unsigned short>((b >> 16 & 0x8000) | (e ? ((e - 112) << 10) | (b >> 13 & 1023) : 0));
}
// ps_3_0: def c0, .5, .25, .75, 1 ; mov oC0, c0. Original placeholder pixel
// program; its hash with the reviewed VS is the fixture's "background" pair.
constexpr DWORD flat_program[] = {0xffff0300u, 0x05000051u, 0xa00f0000u, 0x3f000000u, 0x3e800000u, 0x3f400000u, 0x3f800000u,
                                  0x02000001u, 0x800f0800u, 0xa0e40000u, 0x0000ffffu};
const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
constexpr D3DRENDERSTATETYPE watched_states[] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_CULLMODE, D3DRS_FILLMODE,
    D3DRS_COLORWRITEENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_STENCILENABLE, D3DRS_FOGENABLE, D3DRS_SRGBWRITEENABLE,
    D3DRS_CLIPPLANEENABLE, D3DRS_COLORWRITEENABLE1, D3DRS_ZFUNC, D3DRS_LIGHTING};
constexpr unsigned watched_count = sizeof(watched_states) / sizeof(watched_states[0]);

// Every state the route or its fill may touch. Getters add references that
// are dropped immediately: only pointer identity is compared.
struct Snapshot {
    IDirect3DSurface9* rt[2]{}; IDirect3DSurface9* depth = nullptr;
    D3DVIEWPORT9 viewport{}; RECT scissor{}; DWORD fvf = 0;
    IDirect3DVertexDeclaration9* declaration = nullptr; IDirect3DVertexShader9* vs = nullptr; IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexBuffer9* stream = nullptr; UINT offset = 0, stride = 0;
    DWORD states[watched_count]{};
    float rows[16]{}, reserved[16]{}, pixel[8]{}; int integer0[4]{};
};
struct Object {
    const char* name; IDirect3DVertexBuffer9* vb = nullptr;
    x3m::MotionOutputFixtureScope scope{};
    bool (*covers)(double, double) = nullptr;
    // Rows of the last draw the route recorded (scope known and routed).
    bool recorded = false; float rt = 0, rp = 0, rzo = 0;
};
bool covers_a(double ox, double oy) { return ox >= -1 && oy <= 1 && ox - oy <= 2; }
bool covers_b(double ox, double oy) { return ox >= -.9 && oy <= .9 && ox - oy <= -1.2; }
struct DrawRecord { Object* object; float t, p, zo; bool routed, matched; float pt, pp, pzo; };
enum class Alter { None, Blend, FlatPixel };

struct Fixture {
    static constexpr UINT W = 64, H = 64;
    HMODULE runtime = nullptr;
    void (*configure)(const x3m::MotionOutputFixtureConfig*) = nullptr;
    HRESULT (*readback)(IDirect3DDevice9*, float*, unsigned, unsigned*, unsigned*) = nullptr;
    bool seam = false, enabled = false;
    HWND window = nullptr; D3DPRESENT_PARAMETERS pp{};
    Com<IDirect3D9> factory; Com<IDirect3DDevice9> d;
    Com<IDirect3DVertexShader9> vs; Com<IDirect3DPixelShader9> ps, flat;
    Com<IDirect3DVertexDeclaration9> declaration; Com<IDirect3DVertexBuffer9> vb_a, vb_b;
    Com<IDirect3DTexture9> textures[3]; Com<IDirect3DCubeTexture9> cube;
    Com<IDirect3DSurface9> back, depth;
    Words vs_words, ps_words;
    std::uint64_t vs_hash = 0, ps_hash = 0, flat_hash = 0;
    unsigned long long frame = 0; unsigned draw_index = 0;
    bool reserved_written = false;
    Object a{"A"}, b{"B"};
    std::vector<DrawRecord> records;

    void create_shaders() {
        api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(vs_words.data()), &vs.p), "CreateVertexShader");
        api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(ps_words.data()), &ps.p), "CreatePixelShader");
    }
    void acquire_swapchain_surfaces() {
        api(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back.p), "GetBackBuffer");
        api(d->GetDepthStencilSurface(&depth.p), "GetDepthStencilSurface");
    }
    void create(bool production_expected_seam) {
        (void)production_expected_seam;
        api(d->CreatePixelShader(flat_program, &flat.p), "CreatePixelShader flat");
        create_shaders();
        const D3DVERTEXELEMENT9 elements[] = {
            {0, 0, D3DDECLTYPE_FLOAT16_4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 8, D3DDECLTYPE_FLOAT16_4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
            {0, 16, D3DDECLTYPE_FLOAT16_4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0}, D3DDECL_END()};
        api(d->CreateVertexDeclaration(elements, &declaration.p), "CreateVertexDeclaration");
        const float tri_a[3][2] = {{-1, 1}, {3, 1}, {-1, -3}}, tri_b[3][2] = {{-.9f, .9f}, {-.3f, .9f}, {-.9f, .3f}};
        for (unsigned which = 0; which < 2; ++which) {
            auto& vb = which ? vb_b : vb_a; const auto& tri = which ? tri_b : tri_a;
            api(d->CreateVertexBuffer(24 * 3, 0, 0, D3DPOOL_MANAGED, &vb.p, nullptr), "CreateVertexBuffer");
            void* dst = nullptr; api(vb->Lock(0, 0, &dst, 0), "Lock");
            for (UINT i = 0; i < 3; ++i) {
                unsigned short data[12] = {half(tri[i][0]), half(tri[i][1]), half(.5f), half(7), half(float(i & 1)), half(float(i >> 1)), 0, half(7), 0, 0, half(1), half(7)};
                std::memcpy(static_cast<char*>(dst) + i * 24, data, 24);
            }
            api(vb->Unlock(), "Unlock");
        }
        const DWORD texels[3][4] = {{0x99704020, 0xcc208050, 0xaa508020, 0xee403080}, {0x80302010, 0x90401020, 0xa0205030, 0xb0402060}, {0x20100804, 0x30201008, 0x40201018, 0x50182010}};
        for (UINT i = 0; i < 3; ++i) {
            api(d->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &textures[i].p, nullptr), "CreateTexture");
            D3DLOCKED_RECT lock{}; api(textures[i]->LockRect(0, &lock, nullptr, 0), "LockRect");
            for (UINT y = 0; y < 2; ++y) std::memcpy(static_cast<char*>(lock.pBits) + y * lock.Pitch, &texels[i][y * 2], 8);
            api(textures[i]->UnlockRect(0), "UnlockRect");
        }
        api(d->CreateCubeTexture(2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &cube.p, nullptr), "CreateCubeTexture");
        for (UINT face = 0; face < 6; ++face) {
            D3DLOCKED_RECT lock{}; api(cube->LockRect(D3DCUBEMAP_FACES(face), 0, &lock, nullptr, 0), "LockRect cube");
            for (UINT y = 0; y < 2; ++y) for (UINT x = 0; x < 2; ++x) { DWORD v = 0xff102030 + face * 0x00050301; std::memcpy(static_cast<char*>(lock.pBits) + y * lock.Pitch + x * 4, &v, 4); }
            api(cube->UnlockRect(D3DCUBEMAP_FACES(face), 0), "UnlockRect cube");
        }
        acquire_swapchain_surfaces();
        a.vb = vb_a.p; a.covers = covers_a; a.scope = {1, 3, 5, 11, 21, 0x1000, 0x2000, 0x3000, 0x4000, 7, 9, 0x11, 0x2};
        b.vb = vb_b.p; b.covers = covers_b; b.scope = {1, 3, 5, 12, 21, 0x1100, 0x2000, 0x3000, 0x4100, 8, 9, 0x12, 0x2};
    }
    // Seam: fixture background family = (reviewed VS, flat PS); scope per draw.
    void scope(const Object* object) {
        if (!seam) return;
        x3m::MotionOutputFixtureConfig config{};
        config.background_vs[0] = vs_hash; config.background_ps[0] = flat_hash;
        if (object) config.scope = object->scope;
        configure(&config);
    }
    void rows(float t, float p, float zo) {
        float m[16]; std::memcpy(m, identity, sizeof m); m[3] = t; m[11] = zo; m[12] = p;
        api(d->SetVertexShaderConstantF(24, m, 4), "SetVertexShaderConstantF rows");
    }
    void material_state() {
        float values[48][4]{};
        for (UINT base : {28u, 31u, 34u}) for (UINT i = 0; i < 3; ++i) values[base + i][i] = 1;
        values[36][3] = 4; values[37][0] = 1; values[38][1] = 1; values[39][0] = .625f; values[40][0] = .2f; values[40][1] = .1f; values[40][2] = .05f; values[41][0] = 1;
        for (UINT i = 0; i < 8; ++i) { values[3 * i][0] = float(i % 3) - 1; values[3 * i][1] = 1; values[3 * i][2] = 3; values[3 * i + 1][0] = .04f; values[3 * i + 1][1] = .025f; values[3 * i + 1][2] = .015f; values[3 * i + 2][0] = 1; values[3 * i + 2][1] = .05f; }
        api(d->SetVertexShaderConstantF(0, values[0], 24), "SetVertexShaderConstantF lights");
        api(d->SetVertexShaderConstantF(28, values[28], 14), "SetVertexShaderConstantF material");
        int count[4] = {1, 0, 1, 0}; api(d->SetVertexShaderConstantI(0, count, 1), "SetVertexShaderConstantI");
        BOOL fog = FALSE; api(d->SetVertexShaderConstantB(0, &fog, 1), "SetVertexShaderConstantB");
        float pixel[8][4]{}; pixel[0][0] = pixel[1][1] = pixel[2][2] = 1; pixel[3][0] = .25f; pixel[4][2] = 1; pixel[5][0] = .3f; pixel[5][1] = .2f; pixel[5][2] = .1f; pixel[6][0] = .5f; pixel[6][2] = .5f; pixel[7][0] = .1f; pixel[7][1] = .15f; pixel[7][2] = .2f;
        api(d->SetPixelShaderConstantF(0, pixel[0], 8), "SetPixelShaderConstantF");
        for (UINT i = 0; i < 4; ++i) {
            api(d->SetTexture(i, i == 3 ? static_cast<IDirect3DBaseTexture9*>(cube.p) : textures[i].p), "SetTexture");
            for (auto s : {D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER}) api(d->SetSamplerState(i, s, D3DTEXF_POINT), "SetSamplerState");
            api(d->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_NONE), "SetSamplerState mip");
            api(d->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP), "SetSamplerState u");
            api(d->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP), "SetSamplerState v");
            api(d->SetSamplerState(i, D3DSAMP_SRGBTEXTURE, FALSE), "SetSamplerState srgb");
        }
    }
    void scene_states() {
        for (auto s : {D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_FOGENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_STENCILENABLE, D3DRS_DITHERENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_LIGHTING})
            api(d->SetRenderState(s, FALSE), "SetRenderState off");
        api(d->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID), "fill"); api(d->SetRenderState(D3DRS_COLORWRITEENABLE, 15), "write");
        api(d->SetRenderState(D3DRS_COLORWRITEENABLE1, 15), "write1"); api(d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE), "cull");
        api(d->SetRenderState(D3DRS_ZENABLE, TRUE), "z"); api(d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE), "zwrite");
        api(d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL), "zfunc");
    }
    // Deliberately awkward state before the initial Clear: the fill must put
    // every one of these back, including scissor, stream 0 and the declaration.
    void hostile_states() {
        api(d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE), "h1"); api(d->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE), "h2");
        api(d->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW), "h3"); api(d->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME), "h4");
        api(d->SetRenderState(D3DRS_COLORWRITEENABLE, 7), "h5"); api(d->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE), "h6");
        api(d->SetRenderState(D3DRS_STENCILENABLE, TRUE), "h7"); api(d->SetRenderState(D3DRS_FOGENABLE, TRUE), "h8");
        api(d->SetRenderState(D3DRS_SRGBWRITEENABLE, TRUE), "h9"); api(d->SetRenderState(D3DRS_CLIPPLANEENABLE, 1), "h10");
        api(d->SetRenderState(D3DRS_ZENABLE, FALSE), "h11"); api(d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE), "h12");
        api(d->SetRenderState(D3DRS_COLORWRITEENABLE1, 3), "h13");
        const RECT scissor{8, 8, 40, 40}; api(d->SetScissorRect(&scissor), "SetScissorRect");
        const D3DVIEWPORT9 vp{0, 0, W, H, 0, 1}; api(d->SetViewport(&vp), "SetViewport");
    }
    Snapshot snapshot() {
        Snapshot s{};
        api(d->GetRenderTarget(0, &s.rt[0]), "GetRenderTarget0"); s.rt[0]->Release();
        if (SUCCEEDED(d->GetRenderTarget(1, &s.rt[1])) && s.rt[1]) s.rt[1]->Release();
        if (SUCCEEDED(d->GetDepthStencilSurface(&s.depth)) && s.depth) s.depth->Release();
        api(d->GetViewport(&s.viewport), "GetViewport"); api(d->GetScissorRect(&s.scissor), "GetScissorRect");
        api(d->GetFVF(&s.fvf), "GetFVF");
        api(d->GetVertexDeclaration(&s.declaration), "GetVertexDeclaration"); if (s.declaration) s.declaration->Release();
        api(d->GetVertexShader(&s.vs), "GetVertexShader"); if (s.vs) s.vs->Release();
        api(d->GetPixelShader(&s.ps), "GetPixelShader"); if (s.ps) s.ps->Release();
        api(d->GetStreamSource(0, &s.stream, &s.offset, &s.stride), "GetStreamSource"); if (s.stream) s.stream->Release();
        for (unsigned i = 0; i < watched_count; ++i) api(d->GetRenderState(watched_states[i], &s.states[i]), "GetRenderState");
        api(d->GetVertexShaderConstantF(24, s.rows, 4), "GetVertexShaderConstantF rows");
        api(d->GetVertexShaderConstantF(252, s.reserved, 4), "GetVertexShaderConstantF reserved");
        api(d->GetPixelShaderConstantF(216, s.pixel, 2), "GetPixelShaderConstantF reserved");
        api(d->GetVertexShaderConstantI(0, s.integer0, 1), "GetVertexShaderConstantI");
        return s;
    }
    void compare(const Snapshot& x, const Snapshot& y, const char* label) {
        unsigned differences = 0;
        auto differs = [&](bool condition, const char* what) { if (condition) { ++differences; std::printf("RESTORE_DIFF %s %s\n", label, what); } };
        differs(x.rt[0] != y.rt[0], "rt0"); differs(x.rt[1] != y.rt[1], "rt1"); differs(x.depth != y.depth, "depth");
        differs(std::memcmp(&x.viewport, &y.viewport, sizeof x.viewport) != 0, "viewport");
        differs(std::memcmp(&x.scissor, &y.scissor, sizeof x.scissor) != 0, "scissor");
        differs(x.fvf != y.fvf, "fvf"); differs(x.declaration != y.declaration, "declaration");
        differs(x.vs != y.vs, "vs"); differs(x.ps != y.ps, "ps");
        differs(x.stream != y.stream || x.offset != y.offset || x.stride != y.stride, "stream0");
        for (unsigned i = 0; i < watched_count; ++i) { char what[32]; std::snprintf(what, sizeof what, "state_%u", unsigned(watched_states[i])); differs(x.states[i] != y.states[i], what); }
        differs(std::memcmp(x.rows, y.rows, sizeof x.rows) != 0, "c24_27");
        if (reserved_written) {
            differs(std::memcmp(x.reserved, y.reserved, sizeof x.reserved) != 0, "c252_255");
            differs(std::memcmp(x.pixel, y.pixel, sizeof x.pixel) != 0, "ps_c216_217");
        }
        differs(std::memcmp(x.integer0, y.integer0, sizeof x.integer0) != 0, "i0");
        ++restorations;
        std::printf("RESTORE frame=%llu label=%s differences=%u\n", frame, label, differences);
        if (differences) throw std::runtime_error(label);
    }
    void frame_begin() {
        records.clear(); draw_index = 0;
        hostile_states();
        api(d->SetStreamSource(0, vb_a.p, 0, 24), "SetStreamSource"); api(d->SetVertexDeclaration(declaration.p), "SetVertexDeclaration");
        api(d->SetVertexShader(vs.p), "SetVertexShader"); api(d->SetPixelShader(flat.p), "SetPixelShader flat");
        rows(0, 0, 0);
        const Snapshot before = snapshot();
        api(d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff203040, 1, 0), "Clear initial");
        api(d->BeginScene(), "BeginScene");
        // Background draw: the pending sentinel fill runs inside this hook.
        scope(nullptr);
        api(d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1), "DrawPrimitive background"); ++draw_index;
        compare(before, snapshot(), "fill");
        scene_states(); material_state();
        api(d->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1, 0), "Clear depth");
    }
    // One scene draw. `routed`/`matched` are the fixture's expectations from the
    // script; the DLL's own per-draw log is cross-checked by the runner.
    void draw(Object& o, float t, float p, float zo, bool known, bool routed, bool matched, Alter alter = Alter::None) {
        scope(known ? &o : nullptr);
        api(d->SetStreamSource(0, o.vb, 0, 24), "SetStreamSource object");
        api(d->SetVertexShader(vs.p), "SetVertexShader"); api(d->SetPixelShader(alter == Alter::FlatPixel ? flat.p : ps.p), "SetPixelShader");
        if (alter == Alter::Blend) api(d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE), "blend on");
        rows(t, p, zo);
        const Snapshot before = snapshot();
        api(d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1), "DrawPrimitive object");
        ++draw_index;
        compare(before, snapshot(), "draw");
        if (alter == Alter::Blend) api(d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE), "blend off");
        const bool live = enabled && seam;
        records.push_back({&o, t, p, zo, live && routed, live && matched, o.rt, o.rp, o.rzo});
        std::printf("EXPECT frame=%llu index=%u object=%s routed=%u matched=%u\n", frame, draw_index, o.name, live && routed, live && matched);
        if (live && routed && known) { o.recorded = true; o.rt = t; o.rp = p; o.rzo = zo; }
    }
    void write_reserved() {
        float v[16]; for (unsigned i = 0; i < 16; ++i) v[i] = 100.f + float(i);
        float q[8]; for (unsigned i = 0; i < 8; ++i) q[i] = 200.f + float(i);
        api(d->SetVertexShaderConstantF(252, v, 4), "write c252"); api(d->SetPixelShaderConstantF(216, q, 2), "write c216");
        reserved_written = true;
    }
    std::uint64_t color_hash() {
        Com<IDirect3DSurface9> sys; api(d->CreateOffscreenPlainSurface(W, H, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys.p, nullptr), "CreateOffscreenPlainSurface");
        api(d->GetRenderTargetData(back.p, sys.p), "GetRenderTargetData color");
        D3DLOCKED_RECT lock{}; api(sys->LockRect(&lock, nullptr, D3DLOCK_READONLY), "LockRect color");
        std::uint64_t h = 14695981039346656037ull;
        for (UINT y = 0; y < H; ++y) { auto row = static_cast<const unsigned char*>(lock.pBits) + y * lock.Pitch; for (UINT i = 0; i < W * 4; ++i) { h ^= row[i]; h *= 1099511628211ull; } }
        sys->UnlockRect();
        return h;
    }
    // CPU oracle: per pixel, replay the frame's draws in order with the depth
    // test, writing the previous-UV/depth ABI for matched routed draws and the
    // sentinel for routed-unmatched ones; unrouted draws only touch depth.
    void verify_motion() {
        if (!(enabled && seam)) return;
        std::vector<float> data(std::size_t(W) * H * 4); unsigned w = 0, h = 0;
        api(readback(d.p, data.data(), unsigned(data.size()), &w, &h), "fixture readback");
        require(w == W && h == H, "motion target matches the main dimensions");
        unsigned checked = 0, skipped = 0, sentinel = 0, matched = 0, mismatches = 0;
        for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
            double depth_value = 1; double expected[4] = {0, 0, 0, -1}; bool ambiguous = false, is_match = false;
            for (const auto& r : records) {
                // D3D9 raster samples sit at integer window coordinates (pixel i is
                // NDC 2i/W-1). Coverage must agree at the sample and at +-1.5 pixel
                // offsets; edge pixels are skipped.
                bool covered = false, all_same = true;
                for (int k = 0; k < 5; ++k) {
                    const double px = x + (k == 1 ? 1.5 : k == 2 ? -1.5 : 0), py = y + (k == 3 ? 1.5 : k == 4 ? -1.5 : 0);
                    const double nx = 2 * px / W - 1, ny = 1 - 2 * py / H;
                    const double ox = (nx - r.t) / (1 - r.p * nx), wc = 1 + r.p * ox, oy = ny * wc;
                    const bool c = r.object->covers(ox, oy);
                    if (k == 0) covered = c; else all_same = all_same && c == covered;
                }
                if (!all_same) { ambiguous = true; break; }
                if (!covered) continue;
                const double nx = 2.0 * x / W - 1, ny = 1 - 2.0 * y / H;
                const double ox = (nx - r.t) / (1 - r.p * nx), wc = 1 + r.p * ox, oy = ny * wc;
                const double z = (.5 + r.zo) / wc;
                if (z > depth_value + 1e-7) continue; // LESSEQUAL failed
                depth_value = z;
                if (!r.routed) continue;
                if (!r.matched) { expected[0] = expected[1] = expected[2] = 0; expected[3] = -1; is_match = false; continue; }
                const double xp = ox + r.pt, wp = 1 + r.pp * ox, yp = oy, zp = .5 + r.pzo;
                expected[0] = .5 * xp / wp + .5 + .5 / W; expected[1] = -.5 * yp / wp + .5 + .5 / H; expected[2] = zp / wp; expected[3] = 1;
                is_match = true;
            }
            if (ambiguous) { ++skipped; continue; }
            const float* actual = &data[(std::size_t(y) * W + x) * 4];
            ++checked; motion_checked++;
            bool ok = true;
            if (!is_match) { ok = actual[0] == 0 && actual[1] == 0 && actual[2] == 0 && actual[3] == -1; ++sentinel; }
            else {
                ++matched; ++motion_matched;
                const double eu = std::fabs(actual[0] - expected[0]) * W, ev = std::fabs(actual[1] - expected[1]) * H, ed = std::fabs(actual[2] - expected[2]);
                ok = std::isfinite(actual[0]) && std::isfinite(actual[1]) && std::isfinite(actual[2]) && eu <= .01 && ev <= .01 && ed <= 4e-6 && actual[3] == 1;
                if (ok) { max_uv_pixels = std::max(max_uv_pixels, std::max(eu, ev)); max_depth_error = std::max(max_depth_error, ed); }
            }
            if (!ok) { if (++mismatches <= 8) std::printf("MOTION_DIFF frame=%llu x=%u y=%u actual=%.9g,%.9g,%.9g,%.9g expected=%.9g,%.9g,%.9g,%.9g\n", frame, x, y, actual[0], actual[1], actual[2], actual[3], expected[0], expected[1], expected[2], expected[3]); }
        }
        std::printf("MOTION frame=%llu checked=%u skipped=%u sentinel=%u matched=%u mismatches=%u\n", frame, checked, skipped, sentinel, matched, mismatches);
        require(!mismatches && checked > 0, "motion target matches the CPU oracle");
        ++frames_verified;
    }
    void frame_end() {
        api(d->EndScene(), "EndScene");
        std::printf("COLOR frame=%llu hash=%016llx\n", frame, static_cast<unsigned long long>(color_hash()));
        verify_motion();
        api(d->Present(nullptr, nullptr, nullptr, nullptr), "Present");
        ++frame;
    }
    void reset() {
        back.reset(); depth.reset();
        for (UINT i = 0; i < 4; ++i) api(d->SetTexture(i, nullptr), "SetTexture null");
        api(d->Reset(&pp), "Reset");
        std::puts("RESET PASS");
        acquire_swapchain_surfaces();
        a.recorded = b.recorded = false;
    }
    void stateblock_case() {
        // A state block Apply rebinding the flat PS must be seen by the route:
        // the draw is not routed and the flat PS is still bound afterwards.
        api(d->SetPixelShader(flat.p), "SetPixelShader flat");
        Com<IDirect3DStateBlock9> block; api(d->CreateStateBlock(D3DSBT_ALL, &block.p), "CreateStateBlock");
        api(d->SetPixelShader(ps.p), "SetPixelShader reviewed");
        api(block->Apply(), "StateBlock Apply");
        scope(&a); api(d->SetStreamSource(0, a.vb, 0, 24), "SetStreamSource A"); rows(.75f, 0, 0);
        const Snapshot before = snapshot();
        api(d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1), "DrawPrimitive after Apply"); ++draw_index;
        compare(before, snapshot(), "stateblock");
        IDirect3DPixelShader9* bound = nullptr; api(d->GetPixelShader(&bound), "GetPixelShader"); if (bound) bound->Release();
        require(bound == flat.p, "state block Apply rebinding is honored (flat PS still bound)");
        records.push_back({&a, .75f, 0, 0, false, false, a.rt, a.rp, a.rzo});
        std::printf("EXPECT frame=%llu index=%u object=A routed=0 matched=0\n", frame, draw_index);
    }
    void recreate_shaders() {
        api(d->SetVertexShader(nullptr), "unbind vs"); api(d->SetPixelShader(nullptr), "unbind ps");
        vs.reset(); ps.reset();
        create_shaders();
    }
    void run() {
        const bool live = enabled && seam;
        // f0: no previous frame; f1: matched with f0 rows; (e) reserved constants written before A.
        frame_begin(); draw(a, .75f, 0, 0, true, true, false); draw(b, 0, 0, 0, true, true, false); frame_end();
        frame_begin(); write_reserved(); draw(a, .8f, .125f, 0, true, true, true); draw(b, -.05f, 0, .1f, true, true, true); frame_end();
        // f2: scope withheld -> sentinel-only mode (routed, gate 5). f3: nothing recorded in f2.
        frame_begin(); draw(a, .8f, .125f, 0, false, true, false); draw(b, -.05f, 0, .1f, false, true, false); frame_end();
        frame_begin(); draw(a, .75f, 0, 0, true, true, false); draw(b, 0, 0, 0, true, true, false); frame_end();
        // f4: matched, plus gate-4 (blend) and gate-3 (flat PS) negatives that must not route.
        frame_begin(); draw(a, .8f, .125f, 0, true, true, true); draw(b, -.05f, 0, .1f, true, true, true);
        draw(a, .8f, .125f, 0, true, false, false, Alter::Blend); draw(a, .8f, .125f, 0, true, false, false, Alter::FlatPixel); frame_end();
        // f5: duplicate key consumes once; f6: the duplicate poisoned A.
        frame_begin(); draw(a, .75f, 0, 0, true, true, true); draw(a, .75f, 0, 0, true, true, false); draw(b, 0, 0, 0, true, true, true); frame_end();
        frame_begin(); draw(a, .8f, .125f, 0, true, true, false); draw(b, -.05f, 0, .1f, true, true, true); frame_end();
        // f7: state block Apply resynchronizes the shadow; f8: A had no f7 record.
        frame_begin(); stateblock_case(); draw(b, 0, 0, 0, true, true, true); frame_end();
        frame_begin(); draw(a, .75f, 0, 0, true, true, false); draw(b, -.05f, 0, .1f, true, true, true); frame_end();
        // Reset with the motion target owned; history restarts, then resumes.
        reset();
        frame_begin(); draw(a, .8f, .125f, 0, true, true, false); draw(b, 0, 0, 0, true, true, false); frame_end();
        frame_begin(); draw(a, .75f, 0, 0, true, true, true); draw(b, -.05f, 0, .1f, true, true, true); frame_end();
        // Shader release/recreate (pointer reuse path); identity is the hash, so history survives.
        recreate_shaders();
        frame_begin(); draw(a, .8f, .125f, 0, true, true, true); draw(b, 0, 0, 0, true, true, true); frame_end();
        require(!live || frames_verified == 12, "every live frame verified against the oracle");
    }
};
} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    int exit_code = 1;
    WNDCLASSA cls{}; cls.lpfnWndProc = DefWindowProcA; cls.hInstance = GetModuleHandleA(nullptr); cls.lpszClassName = "X3MotionOutput";
    RegisterClassA(&cls);
    HWND window = CreateWindowA(cls.lpszClassName, "Live motion route fixture", WS_OVERLAPPEDWINDOW, 0, 0, 96, 96, nullptr, nullptr, cls.hInstance, nullptr);
    HMODULE runtime = LoadLibraryA("d3d9.dll");
    try {
        if (argc != 4 || !window || !runtime) throw std::runtime_error("usage: fixture <vs.bin> <ps.bin> production|seam");
        Fixture f;
        f.runtime = runtime; f.window = window;
        const std::string mode = argv[3];
        f.configure = symbol<void (*)(const x3m::MotionOutputFixtureConfig*)>(runtime, "x3m_motion_output_fixture_configure", false);
        f.readback = symbol<HRESULT (*)(IDirect3DDevice9*, float*, unsigned, unsigned*, unsigned*)>(runtime, "x3m_motion_output_fixture_readback", false);
        f.seam = f.configure && f.readback;
        require(f.seam == (mode == "seam"), "DLL seam presence matches the requested mode");
        char setting[8]{}; f.enabled = GetEnvironmentVariableA("X3M_MOTION_OUTPUT", setting, sizeof setting) == 1 && setting[0] == '1';
        char path[MAX_PATH]{}; GetModuleFileNameA(runtime, path, MAX_PATH);
        std::printf("MODE seam=%u enabled=%u dll=%s\n", f.seam, f.enabled, path);
        f.vs_words = load(argv[1]); f.ps_words = load(argv[2]);
        f.vs_hash = fnv(f.vs_words.data(), f.vs_words.size() * 4); f.ps_hash = fnv(f.ps_words.data(), f.ps_words.size() * 4);
        f.flat_hash = fnv(flat_program, sizeof flat_program);
        require(f.vs_hash == 0x53a0a641107ed76cull && f.ps_hash == 0x8759c7838bbc86c2ull, "local files are the reviewed pair");
        auto create = symbol<IDirect3D9* (WINAPI*)(UINT)>(runtime, "Direct3DCreate9", true);
        f.factory.p = create(D3D_SDK_VERSION); if (!f.factory.p) throw std::runtime_error("factory");
        f.pp.Windowed = TRUE; f.pp.SwapEffect = D3DSWAPEFFECT_DISCARD; f.pp.hDeviceWindow = window;
        f.pp.BackBufferWidth = Fixture::W; f.pp.BackBufferHeight = Fixture::H; f.pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        f.pp.EnableAutoDepthStencil = TRUE; f.pp.AutoDepthStencilFormat = D3DFMT_D24X8; f.pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        // The seam's background signature must exist before the device attaches,
        // because the first frame's selector is seeded at attach time.
        f.scope(nullptr);
        api(f.factory->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &f.pp, &f.d.p), "CreateDevice");
        f.create(mode == "production");
        f.run();
        // Teardown: every fixture object released, then the device must reach zero.
        f.back.reset(); f.depth.reset();
        for (UINT i = 0; i < 4; ++i) api(f.d->SetTexture(i, nullptr), "SetTexture null");
        api(f.d->SetVertexShader(nullptr), "unbind"); api(f.d->SetPixelShader(nullptr), "unbind"); api(f.d->SetStreamSource(0, nullptr, 0, 0), "unbind"); api(f.d->SetVertexDeclaration(nullptr), "unbind");
        f.vs.reset(); f.ps.reset(); f.flat.reset(); f.declaration.reset(); f.vb_a.reset(); f.vb_b.reset(); f.cube.reset();
        for (auto& t : f.textures) t.reset();
        const ULONG device_refs = f.d.p->Release(); f.d.p = nullptr;
        require(device_refs == 0, "device final Release reaches zero with the route's objects released");
        const ULONG api_refs = f.factory.p->Release(); f.factory.p = nullptr;
        require(api_refs == 0, "factory final Release reaches zero");
        std::printf("RESULT PASS checks=%u restorations=%u frames=%llu motion_pixels=%u matched_pixels=%u max_uv_pixels=%.9g max_depth_error=%.9g\n",
                    checks, restorations, f.frame, motion_checked, motion_matched, max_uv_pixels, max_depth_error);
        exit_code = 0;
    } catch (const std::exception& e) { std::printf("RESULT FAIL %s\n", e.what()); }
    if (runtime) FreeLibrary(runtime);
    if (window) DestroyWindow(window);
    UnregisterClassA(cls.lpszClassName, cls.hInstance);
    return exit_code;
}
