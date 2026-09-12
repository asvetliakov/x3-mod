// Original synthetic D3D9 program for the live same-draw motion route through
// the actual proxy DLL. The reviewed shader pair is read from LOCAL files at
// runtime; no game bytes are embedded. Geometry, textures and constants are
// original. Modes: "production" (plain build/d3d9.dll: fill, restoration,
// Reset, sentinel-only routing) and "seam" (fixture DLL exporting the
// X3M_MOTION_OUTPUT_FIXTURE seam: synthetic scope, RT1/RT2 readback, last
// pixel ABI upload). With X3M_MOTION_JITTER=1 the route jitters every scene
// draw; the fixture then expects the rasterized coverage at the jittered
// sample positions (Halton 2,3 in raster pixels, +X right, +Y down) and the
// motion readback from the UNJITTERED previous rows with zero prior jitter.
// With X3M_TAA=1 (temporal step 3) every frame ends like the game's: the depth
// surface is unbound and the main target is copied into a bloom source with
// StretchRect; the route resolves at that copy. The fixture reads the main
// target before and after the copy, proves the copy received the resolved
// image, that every touched state is restored, that a frame without usable
// history (first frame, cut, after Reset) leaves the 8-bit color bit-identical,
// and (seam) that the resolved image equals a reference TemporalPass run on a
// plain second device from the same read-back inputs, byte for byte, with the
// reference FP16 output written beside the DLL's X3M_TAA_DEBUG files.
// "bench WxH" times the boundary StretchRect (EVENT-synchronized, QPC) with
// the resolve on or off at a game-like size; CPU-inclusive timing.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#define X3M_MOTION_OUTPUT_FIXTURE
#include "../../src/proxy/motion_output.h"
#include "../../src/renderer/temporal_pass.h"
#include "../../src/renderer/temporal_resolve_program.h"
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
unsigned depth_checked = 0, depth_written = 0, coverage_checked = 0, coverage_ambiguous = 0, coverage_frames = 0;
unsigned taa_frames = 0, taa_history_frames = 0, taa_reference_frames = 0, taa_changed_pixels = 0;
double max_uv_pixels = 0, max_depth_error = 0, max_current_depth_error = 0;
// Halton(2,3) sample `index` (1-based) centred on zero; the route's sequence.
double halton(unsigned index, unsigned base) { double f = 1, r = 0; while (index) { f /= base; r += f * (index % base); index /= base; } return r; }
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
    D3DRS_CLIPPLANEENABLE, D3DRS_COLORWRITEENABLE1, D3DRS_ZFUNC, D3DRS_LIGHTING, D3DRS_COLORWRITEENABLE2,
    D3DRS_MULTISAMPLEMASK, D3DRS_VERTEXBLEND, D3DRS_WRAP0, D3DRS_CLIPPING};
constexpr unsigned watched_count = sizeof(watched_states) / sizeof(watched_states[0]);
// Sampler states the resolve normalizes on s0-s6; compared on stages 0-7.
constexpr D3DSAMPLERSTATETYPE watched_samplers[] = {D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER, D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_SRGBTEXTURE, D3DSAMP_MAXMIPLEVEL};
constexpr unsigned sampler_stages = 8, sampler_count = sizeof(watched_samplers) / sizeof(watched_samplers[0]);

// Every state the route or its fill may touch. Getters add references that
// are dropped immediately: only pointer identity is compared.
struct Snapshot {
    IDirect3DSurface9* rt[3]{}; IDirect3DSurface9* depth = nullptr;
    D3DVIEWPORT9 viewport{}; RECT scissor{}; DWORD fvf = 0;
    IDirect3DVertexDeclaration9* declaration = nullptr; IDirect3DVertexShader9* vs = nullptr; IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexBuffer9* stream = nullptr; UINT offset = 0, stride = 0;
    DWORD states[watched_count]{};
    float rows[16]{}, reserved[16]{}, pixel[8]{}; int integer0[4]{};
    // Touched by the resolve (restored through its state block): textures and
    // sampler states of stages 0-7, PS c0-7, stream-0 frequency, indices.
    IDirect3DBaseTexture9* textures[sampler_stages]{};
    DWORD samplers[sampler_stages][sampler_count]{};
    float ps_low[32]{}; UINT frequency0 = 0; IDirect3DIndexBuffer9* indices = nullptr;
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
// `flat`: drawn with the flat pixel program (never routed, flat colour);
// `jittered`: the route jitters this draw's rows (scene draw with a table VS).
struct DrawRecord { Object* object; float t, p, zo; bool routed, matched; float pt, pp, pzo; bool flat = false; bool jittered = false; bool keyed = false; };
enum class Alter { None, Blend, FlatPixel };

// The reference resolve: the production TemporalPass (same embedded bytecode
// as the DLL) on a plain device of the system d3d9, fed with the DLL's own
// read-back inputs. Its history evolves exactly like the route's when every
// frame's inputs, jitter, cut verdict and Reset points are the same, so its
// output must be bit-identical.
struct Reference {
    HMODULE module = nullptr; HWND window = nullptr;
    Com<IDirect3D9> factory; Com<IDirect3DDevice9> d;
    x3m::renderer::TemporalPass pass;
    Com<IDirect3DSurface9> color, output8, sys8, sys16;   // lockable A8R8G8B8 RT inputs/outputs and readback surfaces
    Com<IDirect3DTexture9> motion, depth, depth_staging;   // MANAGED RGBA32F (sampled only); DEFAULT R32F (StretchRect source) filled through a SYSTEMMEM copy
    UINT W = 0, H = 0; std::uint64_t epoch = 1;
    void create(HMODULE proxy, HWND owner, UINT w, UINT h) {
        W = w; H = h; window = owner;
        char path[MAX_PATH]{}; GetSystemDirectoryA(path, MAX_PATH);
        std::string full = std::string(path) + "\\d3d9.dll";
        module = LoadLibraryExA(full.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        require(module && module != proxy, "reference device uses the system d3d9, not the proxy");
        auto create = symbol<IDirect3D9* (WINAPI*)(UINT)>(module, "Direct3DCreate9", true);
        factory.p = create(D3D_SDK_VERSION); if (!factory.p) throw std::runtime_error("reference factory");
        D3DPRESENT_PARAMETERS pp{}; pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = owner;
        pp.BackBufferWidth = 16; pp.BackBufferHeight = 16; pp.BackBufferFormat = D3DFMT_A8R8G8B8; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        api(factory->CreateDevice(0, D3DDEVTYPE_HAL, owner, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &d.p), "reference CreateDevice");
        api(d->CreateRenderTarget(W, H, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, TRUE, &color.p, nullptr), "reference color RT");
        api(d->CreateRenderTarget(W, H, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, TRUE, &output8.p, nullptr), "reference output RT");
        api(d->CreateOffscreenPlainSurface(W, H, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys8.p, nullptr), "reference sys8");
        api(d->CreateOffscreenPlainSurface(W, H, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &sys16.p, nullptr), "reference sys16");
        api(d->CreateTexture(W, H, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED, &motion.p, nullptr), "reference motion");
        api(d->CreateTexture(W, H, 1, 0, D3DFMT_R32F, D3DPOOL_DEFAULT, &depth.p, nullptr), "reference depth");
        api(d->CreateTexture(W, H, 1, 0, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &depth_staging.p, nullptr), "reference depth staging");
        api(pass.initialize(d.p, nullptr, reinterpret_cast<const DWORD*>(x3m::renderer::temporal_resolve_program())), "reference initialize");
    }
    void upload(const std::vector<DWORD>& image, const std::vector<float>& motion_data, const std::vector<float>& depth_data) {
        D3DLOCKED_RECT lock{};
        api(color->LockRect(&lock, nullptr, 0), "lock reference color");
        for (UINT y = 0; y < H; ++y) std::memcpy(static_cast<char*>(lock.pBits) + y * lock.Pitch, &image[std::size_t(y) * W], W * 4);
        api(color->UnlockRect(), "unlock reference color");
        api(motion->LockRect(0, &lock, nullptr, 0), "lock reference motion");
        for (UINT y = 0; y < H; ++y) std::memcpy(static_cast<char*>(lock.pBits) + y * lock.Pitch, &motion_data[std::size_t(y) * W * 4], W * 16);
        api(motion->UnlockRect(0), "unlock reference motion");
        api(depth_staging->LockRect(0, &lock, nullptr, 0), "lock reference depth");
        for (UINT y = 0; y < H; ++y) std::memcpy(static_cast<char*>(lock.pBits) + y * lock.Pitch, &depth_data[std::size_t(y) * W], W * 4);
        api(depth_staging->UnlockRect(0), "unlock reference depth");
        api(d->UpdateTexture(depth_staging.p, depth.p), "reference depth upload");
    }
    // Runs one frame exactly as the route does and returns the copied-back
    // 8-bit image; `half` receives the FP16 output bytes.
    x3m::renderer::Output run(double jx, double jy, double pjx, double pjy, bool cut, std::vector<DWORD>& image8, std::vector<unsigned char>& half) {
        x3m::renderer::FrameInputs in{};
        in.color_surface = color.p; in.current_depth = depth.p; in.motion = motion.p;
        in.width = W; in.height = H; in.epoch = epoch;
        std::memcpy(in.clip_to_previous, identity, sizeof identity);
        in.current_jitter[0] = float(jx); in.current_jitter[1] = float(jy); in.previous_jitter[0] = float(pjx); in.previous_jitter[1] = float(pjy);
        in.motion_policy = x3m::renderer::MotionPolicy::PerPixel; in.reactive_policy = x3m::renderer::ReactivePolicy::DerivedFromDepthSentinel;
        in.history_allowed = true; in.cut = cut; in.caller_scene_open = false; in.caller_queries_idle = true;
        x3m::renderer::Output out{};
        api(pass.run(in, &out), "reference run");
        api(d->StretchRect(out.color_surface, nullptr, output8.p, nullptr, D3DTEXF_POINT), "reference copy-back");
        api(d->GetRenderTargetData(output8.p, sys8.p), "reference readback 8");
        D3DLOCKED_RECT lock{};
        api(sys8->LockRect(&lock, nullptr, D3DLOCK_READONLY), "lock reference 8");
        image8.resize(std::size_t(W) * H);
        for (UINT y = 0; y < H; ++y) std::memcpy(&image8[std::size_t(y) * W], static_cast<const char*>(lock.pBits) + y * lock.Pitch, W * 4);
        sys8->UnlockRect();
        api(d->GetRenderTargetData(out.color_surface, sys16.p), "reference readback 16");
        api(sys16->LockRect(&lock, nullptr, D3DLOCK_READONLY), "lock reference 16");
        half.resize(std::size_t(W) * H * 8);
        for (UINT y = 0; y < H; ++y) std::memcpy(&half[std::size_t(y) * W * 8], static_cast<const char*>(lock.pBits) + y * lock.Pitch, W * 8);
        sys16->UnlockRect();
        return out;
    }
    void reset() { pass.invalidate(); ++epoch; }
    void destroy() {
        pass.shutdown();
        color.reset(); output8.reset(); sys8.reset(); sys16.reset(); motion.reset(); depth.reset(); depth_staging.reset();
        if (d.p) { const ULONG refs = d.p->Release(); d.p = nullptr; require(refs == 0, "reference device final Release reaches zero"); }
        if (factory.p) { const ULONG refs = factory.p->Release(); factory.p = nullptr; require(refs == 0, "reference factory final Release reaches zero"); }
        if (module) { FreeLibrary(module); module = nullptr; }
    }
};

struct Fixture {
    static inline UINT W = 64, H = 64;
    HMODULE runtime = nullptr;
    void (*configure)(const x3m::MotionOutputFixtureConfig*) = nullptr;
    HRESULT (*readback)(IDirect3DDevice9*, float*, unsigned, unsigned*, unsigned*) = nullptr;
    HRESULT (*readback_depth)(IDirect3DDevice9*, float*, unsigned, unsigned*, unsigned*) = nullptr;
    HRESULT (*last_pixel_abi)(IDirect3DDevice9*, float*, unsigned) = nullptr;
    bool seam = false, enabled = false, jitter = false, taa = false, bench = false;
    unsigned jitter_samples = 8;
    Reference reference; bool reference_ready = false;
    std::uint64_t frames_since_reset = 0;
    std::vector<double> bench_ms;
    // This frame's and the previous frame's route jitter in raster pixels.
    double jx = 0, jy = 0, pjx = 0, pjy = 0;
    HWND window = nullptr; D3DPRESENT_PARAMETERS pp{};
    Com<IDirect3D9> factory; Com<IDirect3DDevice9> d;
    Com<IDirect3DVertexShader9> vs; Com<IDirect3DPixelShader9> ps, flat;
    Com<IDirect3DVertexDeclaration9> declaration; Com<IDirect3DVertexBuffer9> vb_a, vb_b;
    Com<IDirect3DTexture9> textures[3]; Com<IDirect3DCubeTexture9> cube;
    Com<IDirect3DSurface9> back, depth;
    Com<IDirect3DTexture9> bloom; Com<IDirect3DSurface9> bloom_surface; // The application's bloom source (A8R8G8B8 RT texture)
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
        if (taa) {
            api(d->CreateTexture(W, H, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &bloom.p, nullptr), "CreateTexture bloom");
            api(bloom->GetSurfaceLevel(0, &bloom_surface.p), "bloom level");
        }
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
        api(d->SetRenderState(D3DRS_COLORWRITEENABLE1, 15), "write1"); api(d->SetRenderState(D3DRS_COLORWRITEENABLE2, 15), "write2"); api(d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE), "cull");
        api(d->SetRenderState(D3DRS_ZENABLE, TRUE), "z"); api(d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE), "zwrite");
        api(d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL), "zfunc");
    }
    // Deliberately awkward state before the initial Clear: the fill must put
    // every one of these back, including scissor, stream 0 and the declaration.
    // The scissor test itself is enabled after the Clear (frame_begin), so the
    // Clear covers the whole target and the coverage oracle can predict every
    // pixel no scene draw touches; the fill still sees and restores it.
    void hostile_states() {
        api(d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE), "h1"); api(d->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE), "h2");
        api(d->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW), "h3"); api(d->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME), "h4");
        api(d->SetRenderState(D3DRS_COLORWRITEENABLE, 7), "h5"); api(d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE), "h6");
        api(d->SetRenderState(D3DRS_STENCILENABLE, TRUE), "h7"); api(d->SetRenderState(D3DRS_FOGENABLE, TRUE), "h8");
        // SRGBWRITEENABLE stays off here: this backend defers a full Clear and
        // encodes its colour with the sRGB state of the first draw that follows,
        // which with the route on is the fill (sRGB off) and with it off the
        // hostile background draw, so a hostile TRUE would make the clear colour
        // differ between route off and on (ff203040 versus its sRGB encoding
        // ff637889) although no routed pixel changes. The fill's restoration of
        // the state is still compared; its TRUE case was proven while the Clear
        // was scissored (checkpoint B1).
        api(d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE), "h9"); api(d->SetRenderState(D3DRS_CLIPPLANEENABLE, 1), "h10");
        api(d->SetRenderState(D3DRS_ZENABLE, FALSE), "h11"); api(d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE), "h12");
        api(d->SetRenderState(D3DRS_COLORWRITEENABLE1, 3), "h13"); api(d->SetRenderState(D3DRS_COLORWRITEENABLE2, 5), "h14");
        const RECT scissor{8, 8, 40, 40}; api(d->SetScissorRect(&scissor), "SetScissorRect");
        const D3DVIEWPORT9 vp{0, 0, W, H, 0, 1}; api(d->SetViewport(&vp), "SetViewport");
    }
    Snapshot snapshot() {
        Snapshot s{};
        for (UINT i = 0; i < sampler_stages; ++i) {
            if (SUCCEEDED(d->GetTexture(i, &s.textures[i])) && s.textures[i]) s.textures[i]->Release();
            for (unsigned k = 0; k < sampler_count; ++k) api(d->GetSamplerState(i, watched_samplers[k], &s.samplers[i][k]), "GetSamplerState");
        }
        api(d->GetPixelShaderConstantF(0, s.ps_low, 8), "GetPixelShaderConstantF low");
        api(d->GetStreamSourceFreq(0, &s.frequency0), "GetStreamSourceFreq");
        if (SUCCEEDED(d->GetIndices(&s.indices)) && s.indices) s.indices->Release();
        api(d->GetRenderTarget(0, &s.rt[0]), "GetRenderTarget0"); s.rt[0]->Release();
        if (SUCCEEDED(d->GetRenderTarget(1, &s.rt[1])) && s.rt[1]) s.rt[1]->Release();
        if (SUCCEEDED(d->GetRenderTarget(2, &s.rt[2])) && s.rt[2]) s.rt[2]->Release();
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
        differs(x.rt[0] != y.rt[0], "rt0"); differs(x.rt[1] != y.rt[1], "rt1"); differs(x.rt[2] != y.rt[2], "rt2"); differs(x.depth != y.depth, "depth");
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
        for (unsigned i = 0; i < sampler_stages; ++i) {
            char what[32]; std::snprintf(what, sizeof what, "texture_%u", i); differs(x.textures[i] != y.textures[i], what);
            std::snprintf(what, sizeof what, "samplers_%u", i); differs(std::memcmp(x.samplers[i], y.samplers[i], sizeof x.samplers[i]) != 0, what);
        }
        differs(std::memcmp(x.ps_low, y.ps_low, sizeof x.ps_low) != 0, "ps_c0_7");
        differs(x.frequency0 != y.frequency0, "frequency0"); differs(x.indices != y.indices, "indices");
        ++restorations;
        std::printf("RESTORE frame=%llu label=%s differences=%u\n", frame, label, differences);
        if (differences) throw std::runtime_error(label);
    }
    void frame_begin() {
        records.clear(); draw_index = 0;
        // The route advances its Halton sequence at every latching Clear; every
        // fixture frame latches, so frame f uses sample (f % samples) + 1.
        pjx = jx; pjy = jy;
        if (jitter) { const unsigned index = unsigned(frame % jitter_samples) + 1; jx = halton(index, 2) - .5; jy = halton(index, 3) - .5; }
        else jx = jy = 0;
        hostile_states();
        api(d->SetStreamSource(0, vb_a.p, 0, 24), "SetStreamSource"); api(d->SetVertexDeclaration(declaration.p), "SetVertexDeclaration");
        api(d->SetVertexShader(vs.p), "SetVertexShader"); api(d->SetPixelShader(flat.p), "SetPixelShader flat");
        rows(0, 0, 0);
        api(d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff203040, 1, 0), "Clear initial");
        api(d->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE), "h6 scissor");
        const Snapshot before = snapshot();
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
        records.push_back({&o, t, p, zo, live && routed, live && matched, o.rt, o.rp, o.rzo, alter == Alter::FlatPixel, enabled && jitter, live && routed && known});
        std::printf("EXPECT frame=%llu index=%u object=%s routed=%u matched=%u jittered=%u\n", frame, draw_index, o.name, live && routed, live && matched, enabled && jitter);
        if (live && routed && known) { o.recorded = true; o.rt = t; o.rp = p; o.rzo = zo; }
    }
    void write_reserved() {
        float v[16]; for (unsigned i = 0; i < 16; ++i) v[i] = 100.f + float(i);
        float q[8]; for (unsigned i = 0; i < 8; ++i) q[i] = 200.f + float(i);
        api(d->SetVertexShaderConstantF(252, v, 4), "write c252"); api(d->SetPixelShaderConstantF(216, q, 2), "write c216");
        reserved_written = true;
    }
    std::vector<DWORD> color_image(IDirect3DSurface9* source = nullptr) {
        Com<IDirect3DSurface9> sys; api(d->CreateOffscreenPlainSurface(W, H, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys.p, nullptr), "CreateOffscreenPlainSurface");
        api(d->GetRenderTargetData(source ? source : back.p, sys.p), "GetRenderTargetData color");
        D3DLOCKED_RECT lock{}; api(sys->LockRect(&lock, nullptr, D3DLOCK_READONLY), "LockRect color");
        std::vector<DWORD> image(std::size_t(W) * H);
        for (UINT y = 0; y < H; ++y) std::memcpy(&image[std::size_t(y) * W], static_cast<const unsigned char*>(lock.pBits) + y * lock.Pitch, W * 4);
        sys->UnlockRect();
        return image;
    }
    static std::uint64_t color_hash(const std::vector<DWORD>& image) {
        std::uint64_t h = 14695981039346656037ull;
        for (DWORD value : image) for (unsigned k = 0; k < 4; ++k) { h ^= (value >> (8 * k)) & 255u; h *= 1099511628211ull; }
        return h;
    }
    // Object-space point behind raster sample (px, py) under rows (t, p).
    static void object_point(double px, double py, float t, float p, double& ox, double& oy, double& wc) {
        const double nx = 2 * px / W - 1, ny = 1 - 2 * py / H;
        ox = (nx - t) / (1 - p * nx); wc = 1 + p * ox; oy = ny * wc;
    }
    // Signed distance (NDC units, object space) to the nearest edge of the
    // object's triangle: pixels within `eps` of an edge are ambiguous.
    static double edge_distance(const Object& o, double ox, double oy) {
        const bool a = o.covers == covers_a;
        const double e1 = ox - (a ? -1. : -.9), e2 = (a ? 1. : .9) - oy, e3 = (a ? 2. : -1.2) - (ox - oy);
        return std::min(std::fabs(e1), std::min(std::fabs(e2), std::fabs(e3) / 1.4142135623730951));
    }
    // Rasterized coverage against the CPU reference at the jittered sample
    // positions: the route moves every scene draw's rows by (jx, jy) raster
    // pixels (+X right, +Y down), so pixel (x, y) sees what the unjittered
    // geometry has at (x - jx, y - jy). Per pixel the front-most passing scene
    // draw decides the colour class: material program (any colour other than
    // the background and flat colours), flat program (exactly its colour) or
    // none. The background colour is the initial Clear as this backend stores
    // it (a deferred full Clear takes the sRGB write state of the next draw),
    // read from the frame's own first uncovered pixel; the hostile background draw
    // is a wireframe of the A triangle at rows 0, whose lines lie on the x=0
    // column, the y=0 row and the lower-right corner, all outside the hostile
    // scissor rect (8,8)-(40,40) it is drawn under, so no other pixel differs
    // from the Clear. Pixels within 0.03 px of any scene-draw edge are
    // skipped; all others must agree. Runs in every case: with the route off
    // or jitter off the offsets are zero and the oracle is its own control; a
    // wrong jitter sign or scale would move every scene edge by up to 1 px
    // and fail here.
    void verify_coverage(const std::vector<DWORD>& image) {
        constexpr double eps = 1e-3; // NDC units at 64 px: about 0.03 px.
        constexpr DWORD flat_color = 0xff8040bf; // (.5, .25, .75) under scene states.
        std::vector<unsigned char> expected(std::size_t(W) * H, 3); // 3 = ambiguous edge pixel.
        unsigned ambiguous = 0;
        bool have_background = false; DWORD background_color = 0;
        for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
            double depth_value = 1; unsigned kind = 0; bool edge = false;
            for (const auto& r : records) {
                double ox, oy, wc;
                object_point(x - (r.jittered ? jx : 0), y - (r.jittered ? jy : 0), r.t, r.p, ox, oy, wc);
                if (edge_distance(*r.object, ox, oy) < eps) edge = true;
                if (!r.object->covers(ox, oy)) continue;
                const double z = (.5 + r.zo) / wc;
                if (z > depth_value + 1e-7) continue;
                depth_value = z; kind = r.flat ? 2 : 1;
            }
            if (edge) { ++ambiguous; continue; }
            expected[std::size_t(y) * W + x] = static_cast<unsigned char>(kind);
            if (kind == 0 && !have_background) { have_background = true; background_color = image[std::size_t(y) * W + x]; }
        }
        require(have_background && background_color != flat_color, "an uncovered pixel gives the frame's background colour");
        unsigned checked = 0, mismatches = 0, material = 0, background = 0;
        for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
            const unsigned kind = expected[std::size_t(y) * W + x];
            if (kind == 3) continue;
            const DWORD actual = image[std::size_t(y) * W + x];
            const unsigned gpu = actual == flat_color ? 2 : actual == background_color ? 0 : 1;
            ++checked; material += gpu == 1; background += gpu == 0;
            if (gpu != kind) { if (++mismatches <= 8) std::printf("COVERAGE_DIFF frame=%llu x=%u y=%u actual=%08lx expected_kind=%u\n", frame, x, y, actual, kind); }
        }
        std::printf("COVERAGE frame=%llu jitter=%u jx=%.6f jy=%.6f background_color=%08lx checked=%u ambiguous=%u material=%u background=%u mismatches=%u\n", frame, jitter, jx, jy, background_color, checked, ambiguous, material, background, mismatches);
        require(!mismatches && checked > 0 && material > 0 && background > 0 && ambiguous < W * H / 8, "rasterized coverage matches the CPU reference at the jittered sample positions");
        coverage_checked += checked; coverage_ambiguous += ambiguous; ++coverage_frames;
    }
    // CPU oracle: per pixel, replay the frame's draws in order with the depth
    // test, writing the previous-UV/depth ABI for matched routed draws and the
    // sentinel for routed-unmatched ones; unrouted draws only touch depth.
    void verify_motion() {
        if (!(enabled && seam)) return;
        std::vector<float> data(std::size_t(W) * H * 4), depth_data(std::size_t(W) * H); unsigned w = 0, h = 0;
        api(readback(d.p, data.data(), unsigned(data.size()), &w, &h), "fixture readback");
        require(w == W && h == H, "motion target matches the main dimensions");
        api(readback_depth(d.p, depth_data.data(), unsigned(depth_data.size()), &w, &h), "fixture depth readback");
        require(w == W && h == H, "depth target matches the main dimensions");
        // The route must upload zero prior jitter in c216.zw: history rows are
        // unjittered, so the fragment's UV is already the previous unjittered UV.
        if (records.size() && std::any_of(records.begin(), records.end(), [](const DrawRecord& r) { return r.routed; })) {
            float abi[8]{}; api(last_pixel_abi(d.p, abi, 8), "last pixel ABI");
            require(abi[0] == 1.f / W && abi[1] == 1.f / H && abi[2] == 0 && abi[3] == 0, "routed draws upload inverse size and zero prior jitter in c216");
        }
        unsigned checked = 0, skipped = 0, sentinel = 0, matched = 0, mismatches = 0, depth_mismatches = 0, written = 0;
        for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
            double depth_value = 1, expected_depth = -1; double expected[4] = {0, 0, 0, -1}; bool ambiguous = false, is_match = false;
            for (const auto& r : records) {
                // D3D9 raster samples sit at integer window coordinates (pixel i is
                // NDC 2i/W-1); a jittered draw's geometry is displaced by (jx, jy)
                // pixels, so its sample is taken at (x - jx, y - jy). Coverage must
                // agree at the sample and at +-1.5 pixel offsets; edge pixels are skipped.
                const double sx = x - (r.jittered ? jx : 0), sy = y - (r.jittered ? jy : 0);
                bool covered = false, all_same = true;
                for (int k = 0; k < 5; ++k) {
                    const double px = sx + (k == 1 ? 1.5 : k == 2 ? -1.5 : 0), py = sy + (k == 3 ? 1.5 : k == 4 ? -1.5 : 0);
                    double ox, oy, wc; object_point(px, py, r.t, r.p, ox, oy, wc);
                    const bool c = r.object->covers(ox, oy);
                    if (k == 0) covered = c; else all_same = all_same && c == covered;
                }
                if (!all_same) { ambiguous = true; break; }
                if (!covered) continue;
                double ox, oy, wc; object_point(sx, sy, r.t, r.p, ox, oy, wc);
                const double z = (.5 + r.zo) / wc;
                if (z > depth_value + 1e-7) continue; // LESSEQUAL failed
                depth_value = z;
                if (!r.routed) continue;
                expected_depth = z; // Every routed draw writes RT2 (the reviewed row has depth_output).
                if (!r.matched) { expected[0] = expected[1] = expected[2] = 0; expected[3] = -1; is_match = false; continue; }
                // Previous rows are the unjittered rows of the earlier frame and the
                // prior jitter uploaded is zero, so the expected UV carries no jitter term.
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
            // RT2: device depth z/w of the front-most routed draw, sentinel elsewhere.
            const float current = depth_data[std::size_t(y) * W + x];
            ++depth_checked;
            bool depth_ok;
            if (expected_depth < 0) depth_ok = current == -1;
            else { ++written; ++depth_written; const double e = std::fabs(current - expected_depth); depth_ok = std::isfinite(current) && e <= 4e-6; if (depth_ok) max_current_depth_error = std::max(max_current_depth_error, e); }
            if (!depth_ok) { if (++depth_mismatches <= 8) std::printf("DEPTH_DIFF frame=%llu x=%u y=%u actual=%.9g expected=%.9g\n", frame, x, y, current, expected_depth); }
        }
        std::printf("MOTION frame=%llu checked=%u skipped=%u sentinel=%u matched=%u mismatches=%u depth_written=%u depth_mismatches=%u\n", frame, checked, skipped, sentinel, matched, mismatches, written, depth_mismatches);
        require(!mismatches && checked > 0, "motion target matches the CPU oracle");
        require(!depth_mismatches, "depth target matches the CPU oracle (z/w of the front-most routed draw, sentinel elsewhere)");
        ++frames_verified;
    }
    // The DLL's cut rule on this frame's script: the fraction of keyed routed
    // draws (scope known: gates 0 and 6) that found no previous entry, against
    // the 0.25 bound; the origin displacements of the script (1.6 px at 64 px)
    // never reach the median bound.
    bool expected_cut() const {
        unsigned keyed = 0, matched = 0;
        for (const auto& r : records) { keyed += r.keyed; matched += r.keyed && r.matched; }
        return keyed && float(keyed - matched) / float(keyed) > .25f;
    }
    // The game's pre-bloom boundary inside the scene: depth unbound, then the
    // main target copied into the bloom source. The route resolves inside the
    // StretchRect hook; the fixture reads the main target on both sides.
    void boundary() {
        api(d->SetDepthStencilSurface(nullptr), "SetDepthStencilSurface null");
        const auto before_image = color_image();
        const Snapshot before = snapshot();
        api(d->StretchRect(back.p, nullptr, bloom_surface.p, nullptr, D3DTEXF_NONE), "StretchRect bloom copy");
        compare(before, snapshot(), "boundary");
        const auto after_image = color_image(), bloom_image = color_image(bloom_surface.p);
        require(bloom_image == after_image, "the application's bloom copy receives the main target as resolved");
        unsigned changed = 0;
        for (std::size_t i = 0; i < after_image.size(); ++i) if (after_image[i] != before_image[i]) { if (++changed <= 4) std::printf("CHANGED frame=%llu x=%u y=%u before=%08lx after=%08lx\n", frame, unsigned(i % W), unsigned(i / W), before_image[i], after_image[i]); }
        const bool live = enabled && seam;
        bool history = false;
        if (live) {
            // Reference resolve from the DLL's own inputs: RT1/RT2 read back
            // through the seam, the 8-bit main target read back before the copy.
            std::vector<float> motion_data(std::size_t(W) * H * 4), depth_data(std::size_t(W) * H); unsigned w = 0, h = 0;
            api(readback(d.p, motion_data.data(), unsigned(motion_data.size()), &w, &h), "reference motion readback");
            api(readback_depth(d.p, depth_data.data(), unsigned(depth_data.size()), &w, &h), "reference depth readback");
            reference.upload(before_image, motion_data, depth_data);
            std::vector<DWORD> expected; std::vector<unsigned char> half;
            const auto out = reference.run(jx, jy, pjx, pjy, expected_cut(), expected, half);
            history = out.used_history;
            unsigned mismatches = 0;
            for (std::size_t i = 0; i < expected.size(); ++i) if (expected[i] != after_image[i]) { if (++mismatches <= 4) std::printf("TAA_DIFF frame=%llu index=%u actual=%08lx reference=%08lx\n", frame, unsigned(i), after_image[i], expected[i]); }
            require(!mismatches, "main target after the copy equals the reference resolve of the same inputs, byte for byte");
            char name[64]; std::snprintf(name, sizeof name, "reference_taa_%llu.rgba16f", frame);
            FILE* file = std::fopen(name, "wb"); require(file != nullptr, "reference FP16 output written");
            std::fwrite(half.data(), 1, half.size(), file); std::fclose(file);
            ++taa_reference_frames;
        } else {
            // Production DLL: every routed pixel is sentinel (no history
            // correspondence), so the resolve is current-only everywhere.
            history = false;
        }
        const bool expect_history = live && frames_since_reset > 0 && !expected_cut();
        require(history == expect_history, "history use follows the script (first frame, Reset and cut frames run current-only)");
        if (!history) require(!changed, "a frame without history leaves the 8-bit main target bit-identical through the FP16 round trip");
        std::printf("TAA frame=%llu history=%u cut=%u changed=%u\n", frame, history, expected_cut(), changed);
        ++taa_frames; taa_history_frames += history; taa_changed_pixels += changed;
        std::printf("COLOR_BEFORE frame=%llu hash=%016llx\n", frame, static_cast<unsigned long long>(color_hash(before_image)));
        verify_coverage(before_image);
    }
    void frame_end() {
        if (taa) boundary();
        api(d->EndScene(), "EndScene");
        const auto image = color_image();
        std::printf("COLOR frame=%llu hash=%016llx\n", frame, static_cast<unsigned long long>(color_hash(image)));
        if (!taa) verify_coverage(image);
        verify_motion();
        // The game rebinds its depth surface after the bloom passes; without
        // the bloom sequence the selector rejects the rest of this frame,
        // which must not disturb the resolved history.
        if (taa) api(d->SetDepthStencilSurface(depth.p), "SetDepthStencilSurface rebind");
        api(d->Present(nullptr, nullptr, nullptr, nullptr), "Present");
        ++frame; ++frames_since_reset;
    }
    void reset() {
        back.reset(); depth.reset(); bloom_surface.reset(); bloom.reset();
        for (UINT i = 0; i < 4; ++i) api(d->SetTexture(i, nullptr), "SetTexture null");
        api(d->Reset(&pp), "Reset");
        std::puts("RESET PASS");
        acquire_swapchain_surfaces();
        a.recorded = b.recorded = false;
        frames_since_reset = 0;
        if (reference_ready) reference.reset();
    }
    // EVENT-synchronized wall-clock time of the boundary StretchRect (the
    // route's resolve and copies run inside its hook) at a game-like size.
    void wait(IDirect3DQuery9* event) {
        BOOL done = FALSE; const DWORD limit = GetTickCount() + 10000;
        for (;;) { const HRESULT hr = event->GetData(&done, sizeof done, D3DGETDATA_FLUSH); if (hr == S_OK && done) return; if (FAILED(hr) || LONG(GetTickCount() - limit) >= 0) throw std::runtime_error("event completion"); Sleep(0); }
    }
    void run_bench(unsigned frames) {
        Com<IDirect3DQuery9> event; api(d->CreateQuery(D3DQUERYTYPE_EVENT, &event.p), "CreateQuery EVENT");
        LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
        for (unsigned i = 0; i < frames; ++i) {
            frame_begin();
            const bool alternate = i % 2;
            draw(a, alternate ? .8f : .75f, alternate ? .125f : 0, 0, true, true, i > 0); draw(b, alternate ? -.05f : 0, 0, alternate ? .1f : 0, true, true, i > 0);
            api(d->SetDepthStencilSurface(nullptr), "SetDepthStencilSurface null");
            api(event->Issue(D3DISSUE_END), "Issue"); wait(event.p);
            LARGE_INTEGER begin, end; QueryPerformanceCounter(&begin);
            api(d->StretchRect(back.p, nullptr, bloom_surface.p, nullptr, D3DTEXF_NONE), "StretchRect bloom copy");
            api(event->Issue(D3DISSUE_END), "Issue"); wait(event.p);
            QueryPerformanceCounter(&end);
            const double ms = 1000. * double(end.QuadPart - begin.QuadPart) / double(frequency.QuadPart);
            std::printf("BENCH frame=%llu width=%u height=%u boundary_ms=%.4f\n", frame, W, H, ms);
            if (i >= 4) bench_ms.push_back(ms);
            api(d->EndScene(), "EndScene");
            api(d->SetDepthStencilSurface(depth.p), "SetDepthStencilSurface rebind");
            api(d->Present(nullptr, nullptr, nullptr, nullptr), "Present");
            ++frame; ++frames_since_reset;
        }
        std::sort(bench_ms.begin(), bench_ms.end());
        std::printf("BENCH_SUMMARY width=%u height=%u frames=%u min_ms=%.4f median_ms=%.4f max_ms=%.4f timing=cpu_inclusive_event_synchronized\n",
                    W, H, unsigned(bench_ms.size()), bench_ms.front(), bench_ms[bench_ms.size() / 2], bench_ms.back());
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
        records.push_back({&a, .75f, 0, 0, false, false, a.rt, a.rp, a.rzo, true, enabled && jitter});
        std::printf("EXPECT frame=%llu index=%u object=A routed=0 matched=0 jittered=%u\n", frame, draw_index, enabled && jitter);
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
        if (taa) {
            require(taa_frames == 12, "every frame ran the boundary");
            // Seam: history on frames 1, 2, 4, 7, 10, 11 (frames 3, 5, 6, 8 are
            // cuts; 0 and 9 have no history); production: never (sentinel only).
            require(taa_history_frames == (live ? 6u : 0u), "history frames follow the script");
            require(!live || taa_reference_frames == 12, "every seam frame compared against the reference resolve");
        }
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
        if ((argc != 4 && argc != 5) || !window || !runtime) throw std::runtime_error("usage: fixture <vs.bin> <ps.bin> production|seam|bench [WxH]");
        Fixture f;
        f.runtime = runtime; f.window = window;
        const std::string mode = argv[3];
        f.bench = mode == "bench";
        if (f.bench) {
            unsigned w = 0, h = 0;
            if (argc != 5 || std::sscanf(argv[4], "%ux%u", &w, &h) != 2 || !w || !h || w > 8192 || h > 8192) throw std::runtime_error("bench needs WxH");
            Fixture::W = w; Fixture::H = h;
        }
        f.configure = symbol<void (*)(const x3m::MotionOutputFixtureConfig*)>(runtime, "x3m_motion_output_fixture_configure", false);
        f.readback = symbol<HRESULT (*)(IDirect3DDevice9*, float*, unsigned, unsigned*, unsigned*)>(runtime, "x3m_motion_output_fixture_readback", false);
        f.readback_depth = symbol<HRESULT (*)(IDirect3DDevice9*, float*, unsigned, unsigned*, unsigned*)>(runtime, "x3m_motion_output_fixture_readback_depth", false);
        f.last_pixel_abi = symbol<HRESULT (*)(IDirect3DDevice9*, float*, unsigned)>(runtime, "x3m_motion_output_fixture_last_pixel_abi", false);
        f.seam = f.configure && f.readback && f.readback_depth && f.last_pixel_abi;
        require(f.bench || f.seam == (mode == "seam"), "DLL seam presence matches the requested mode");
        char setting[8]{}; f.enabled = GetEnvironmentVariableA("X3M_MOTION_OUTPUT", setting, sizeof setting) == 1 && setting[0] == '1';
        f.taa = f.enabled && GetEnvironmentVariableA("X3M_TAA", setting, sizeof setting) == 1 && setting[0] == '1';
        // The DLL implies the jitter with the resolve on.
        f.jitter = f.enabled && (f.taa || (GetEnvironmentVariableA("X3M_MOTION_JITTER", setting, sizeof setting) == 1 && setting[0] == '1'));
        if (f.bench) f.taa = true; // The bench always runs the game-like boundary; the resolve follows X3M_TAA.
        if (GetEnvironmentVariableA("X3M_MOTION_JITTER_SAMPLES", setting, sizeof setting) > 0) { const unsigned n = unsigned(std::atoi(setting)); if (n >= 2 && n <= 64) f.jitter_samples = n; }
        char path[MAX_PATH]{}; GetModuleFileNameA(runtime, path, MAX_PATH);
        std::printf("MODE seam=%u enabled=%u jitter=%u jitter_samples=%u taa=%u bench=%u width=%u height=%u dll=%s\n", f.seam, f.enabled, f.jitter, f.jitter_samples, f.taa, f.bench, Fixture::W, Fixture::H, path);
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
        if (f.taa && f.enabled && f.seam && !f.bench) { f.reference.create(runtime, window, Fixture::W, Fixture::H); f.reference_ready = true; }
        if (f.bench) f.run_bench(24); else f.run();
        if (f.reference_ready) { f.reference.destroy(); f.reference_ready = false; }
        // Teardown: every fixture object released, then the device must reach zero.
        f.back.reset(); f.depth.reset(); f.bloom_surface.reset(); f.bloom.reset();
        for (UINT i = 0; i < 4; ++i) api(f.d->SetTexture(i, nullptr), "SetTexture null");
        api(f.d->SetVertexShader(nullptr), "unbind"); api(f.d->SetPixelShader(nullptr), "unbind"); api(f.d->SetStreamSource(0, nullptr, 0, 0), "unbind"); api(f.d->SetVertexDeclaration(nullptr), "unbind");
        f.vs.reset(); f.ps.reset(); f.flat.reset(); f.declaration.reset(); f.vb_a.reset(); f.vb_b.reset(); f.cube.reset();
        for (auto& t : f.textures) t.reset();
        const ULONG device_refs = f.d.p->Release(); f.d.p = nullptr;
        require(device_refs == 0, "device final Release reaches zero with the route's objects released");
        const ULONG api_refs = f.factory.p->Release(); f.factory.p = nullptr;
        require(api_refs == 0, "factory final Release reaches zero");
        std::printf("RESULT PASS checks=%u restorations=%u frames=%llu motion_pixels=%u matched_pixels=%u max_uv_pixels=%.9g max_depth_error=%.9g depth_pixels=%u depth_written=%u max_current_depth_error=%.9g coverage_frames=%u coverage_pixels=%u coverage_ambiguous=%u jitter=%u taa=%u taa_frames=%u taa_history_frames=%u taa_reference_frames=%u taa_changed_pixels=%u\n",
                    checks, restorations, f.frame, motion_checked, motion_matched, max_uv_pixels, max_depth_error, depth_checked, depth_written, max_current_depth_error, coverage_frames, coverage_checked, coverage_ambiguous, f.jitter, f.taa, taa_frames, taa_history_frames, taa_reference_frames, taa_changed_pixels);
        exit_code = 0;
    } catch (const std::exception& e) { std::printf("RESULT FAIL %s\n", e.what()); }
    if (runtime) FreeLibrary(runtime);
    if (window) DestroyWindow(window);
    UnregisterClassA(cls.lpszClassName, cls.hInstance);
    return exit_code;
}
