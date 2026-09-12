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
// "burst" (either DLL; X3M_MOTION_RT_MODE=perdraw|lazy) runs nine frames of
// consecutive routed draws without application getters between them,
// interleaved with non-routed draws and an application SetRenderTarget or
// depth Clear, and prints per-frame colour, state-signature and (seam)
// RT1/RT2 hashes so the runner can prove the lazy binding mode equivalent.
// X3M_FIXTURE_CAMERA=rotate (seam) installs the fixture's own projection and
// view buffers as the engine camera globals (x3m_camera_state_fixture_install):
// the camera yaws one degree per frame with a 30-degree jump at frame 7, the
// reference resolve is driven by the same far-plane builder and the DLL's
// resolved image must still equal it byte for byte (X3M_TAA_SENTINEL selects
// the policy; 2 is strict and skips the resolve on frames without a
// transform). "envmap" (seam, TAA, camera) runs the environment-map sequence
// of the frame routine (mid-frame EndScene, six cube-face target changes with
// their own Clear, view and draws, BeginScene) between routed frames, before
// the scene's depth Clear and before the initial Clear, and prints per-frame
// expectations for the runner (nothing routed, camera state unread, no resolve).
// "hook" (seam, TAA) is the engine scene-end hook script: a VirtualAlloc'd
// frame-routine stub CALLs a fake compositor through a five-byte E8 callsite
// the seam DLL patches (x3m_scene_hook_fixture_install, the same mechanism as
// the game's 0x004721b1 -> 0x004c4750 patch); the compositor stands in for the
// glow pass (depth unbind plus the bloom copy, or nothing with glow off). The
// script checks refused installs on mismatched bytes, the patched and
// restored bytes, one signal per call before the compositor, ESI/EDI/EBX/EBP
// across the trampoline, and that the resolve at the hook equals the reference
// resolve (and the copy path's output) in glow-on frames and still runs in
// glow-off frames; with X3M_SCENE_HOOK=0 the same script runs unpatched.
// X3M_STATE_SHADOW=0 runs any script with the route's render-state shadow off.
// X3M_HDR=1 (stage 1 of the FP16 HDR scene path) runs any script with the
// route's FP16 redirect on: the presented frames must equal the run without it
// (the runner compares the per-frame presented_<frame>.bgra8 dumps every mode
// writes), the route's own GetRenderTarget(0) answers must stay the back
// buffer (the state snapshots compare it), and the seam's RT1/RT2 readbacks
// must be unchanged. "hdrvalues" (seam, HDR) draws original ps_3_0 programs
// emitting 2.0 and 8.0, additively and plainly, and checks the FP16 target
// (x3m_hdr_fixture_readback) holds the unclamped sums while the presented
// 8-bit frame holds the clamped values with alpha carried, across a Reset
// with a dimension change and a mid-scene RT0 switch to another surface.
// "hdrfault" (seam, HDR) injects one failure per frame into the write-back
// ladder (x3m_hdr_fixture_fault: draw, restore, both copy rungs, device lost,
// target creation, the latch bind, the recovery self test) and requires a
// valid presented frame every time (the written-back image, or the previous
// frame's image when no copy rung was available) and recovery afterwards.
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
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using Words = std::vector<std::uint32_t>;
namespace {
// Fake engine camera globals: the seam DLL reads the projection and view
// buffers through these two pointer slots exactly as it reads *0x00608a38 and
// *0x00608a40 in the game (camera_state::fixture_install takes the slots).
float fake_projection[16]{}, fake_view[16]{};
const float* fake_projection_slot = fake_projection;
const float* fake_view_slot = fake_view;
constexpr double PI = 3.14159265358979323846;
// Row-vector, left-handed view (V's columns are the camera basis) yawed about
// +Y, and the game's projection terms (m00 0.8, m11 4/3, m23 1).
x3m::renderer::CameraState fake_camera_pose(double yaw_degrees) {
    const double a = yaw_degrees * PI / 180, right[3] = {std::cos(a), 0, -std::sin(a)}, up[3] = {0, 1, 0}, forward[3] = {std::sin(a), 0, std::cos(a)};
    float view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, projection[16]{};
    for (unsigned i = 0; i < 3; ++i) { view[i * 4] = float(right[i]); view[i * 4 + 1] = float(up[i]); view[i * 4 + 2] = float(forward[i]); }
    view[12] = 12.5f; view[13] = -3.f; view[14] = 1000.f; // translation: ignored by the far-plane transform
    projection[0] = .8f; projection[5] = 4.f / 3.f; projection[10] = 1.000003f; projection[11] = 1.f; projection[14] = -6.0000184f;
    std::memcpy(fake_projection, projection, sizeof projection); std::memcpy(fake_view, view, sizeof view);
    x3m::renderer::CameraState state; x3m::renderer::camera_state_from_matrices(projection, view, state);
    return state;
}
unsigned checks = 0, restorations = 0, motion_checked = 0, motion_matched = 0, frames_verified = 0;
unsigned depth_checked = 0, depth_written = 0, coverage_checked = 0, coverage_ambiguous = 0, coverage_frames = 0;
unsigned taa_frames = 0, taa_history_frames = 0, taa_reference_frames = 0, taa_changed_pixels = 0, taa_skipped_frames = 0;
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
// A record with a null object marks an application depth-only Clear between
// draws (burst script): the oracles restart the depth test there.
struct DrawRecord { Object* object; float t, p, zo; bool routed, matched; float pt, pp, pzo; bool flat = false; bool jittered = false; bool keyed = false; };
enum class Alter { None, Blend, FlatPixel, Hdr2, Hdr8, Hdr8Additive, HdrMid };
// ps_3_0: def c0, 2, 2, 2, 1 ; mov oC0, c0 and def c0, 8, 8, 8, .5 ; mov oC0,
// c0: original programs whose output exceeds the 8-bit range (hdrvalues).
constexpr DWORD hdr2_program[] = {0xffff0300u, 0x05000051u, 0xa00f0000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x3f800000u,
                                  0x02000001u, 0x800f0800u, 0xa0e40000u, 0x0000ffffu};
constexpr DWORD hdr8_program[] = {0xffff0300u, 0x05000051u, 0xa00f0000u, 0x41000000u, 0x41000000u, 0x41000000u, 0x3f000000u,
                                  0x02000001u, 0x800f0800u, 0xa0e40000u, 0x0000ffffu};
// def c0, .75, .25, .375, .625 ; mov oC0, c0: in-range values exact in FP16
// whose 8-bit codes are not integers (191.25, 63.75, 95.625, 159.375): the
// write-back must produce exactly 191, 64, 96, 159 (no double rounding, no
// bias, no sampling offset on the flat area).
constexpr DWORD hdrmid_program[] = {0xffff0300u, 0x05000051u, 0xa00f0000u, 0x3f400000u, 0x3e800000u, 0x3ec00000u, 0x3f200000u,
                                    0x02000001u, 0x800f0800u, 0xa0e40000u, 0x0000ffffu};
constexpr DWORD hdrmid_presented = 0x9fbf4060u; // A8R8G8B8: a=159 r=191 g=64 b=96
// ps_3_0: mov oC0, c0: the constant-colour program of the stage-2 scripts
// (hdrramp, hdrexposure): every quad's engine-space value is uploaded to c0.
constexpr DWORD hdrconst_program[] = {0xffff0300u, 0x02000001u, 0x800f0800u, 0xa0e40000u, 0x0000ffffu};
// Stage-2 ramp: exposure_reference/agx_reference ramp_values(): 0.001..64,
// four samples per octave, 65 rows; columns neutral, red, green, blue.
constexpr unsigned ramp_rows = 65, ramp_columns = 4, ramp_column_width = 16;
double ramp_value(unsigned row) { return 0.001 * std::pow(2.0, double(row) * (std::log2(64.0 / 0.001) / 64.0)); }

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
    x3m::renderer::Output run(double jx, double jy, double pjx, double pjy, bool cut, const float* clip_to_previous, bool sentinel_camera,
                              std::vector<DWORD>& image8, std::vector<unsigned char>& half) {
        x3m::renderer::FrameInputs in{};
        in.color_surface = color.p; in.current_depth = depth.p; in.motion = motion.p;
        in.width = W; in.height = H; in.epoch = epoch;
        std::memcpy(in.clip_to_previous, clip_to_previous, 16 * sizeof(float));
        in.sentinel_camera = sentinel_camera;
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

struct Fixture;
} // namespace
// Stand-ins for the engine's compositing callee 0x004c4750 (glow pass) and for
// an unrelated function, reached through the fixture stub's E8 callsites.
extern "C" void __cdecl fixture_compositor();
extern "C" void __cdecl fixture_other_target();
namespace {
struct Fixture {
    static inline UINT W = 64, H = 64;
    HMODULE runtime = nullptr;
    void (*configure)(const x3m::MotionOutputFixtureConfig*) = nullptr;
    HRESULT (*readback)(IDirect3DDevice9*, float*, unsigned, unsigned*, unsigned*) = nullptr;
    HRESULT (*readback_depth)(IDirect3DDevice9*, float*, unsigned, unsigned*, unsigned*) = nullptr;
    HRESULT (*last_pixel_abi)(IDirect3DDevice9*, float*, unsigned) = nullptr;
    bool seam = false, enabled = false, jitter = false, taa = false, bench = false, burst = false, lazy = false, envmap = false;
    bool hook = false, state_shadow = true, hdr = false, hdrvalues = false, hdrfault = false;
    bool hdrramp = false, hdrexposure = false, hdrtonemapfault = false; // stage-2 scripts
    unsigned jitter_samples = 8;
    // HDR seam exports (X3M_HDR scripts).
    void (*hdr_fault)(IDirect3DDevice9*, unsigned, unsigned) = nullptr;
    HRESULT (*hdr_readback)(IDirect3DDevice9*, float*, unsigned, unsigned*, unsigned*) = nullptr;
    HRESULT (*hdr_exposure)(IDirect3DDevice9*, float*, unsigned) = nullptr;
    Com<IDirect3DPixelShader9> hdr2, hdr8, hdrmid; // original ps_3_0 programs emitting (2,2,2,1), (8,8,8,.5) and (.75,.25,.375,.625)
    Com<IDirect3DPixelShader9> hdrconst;           // mov oC0, c0 (stage-2 scripts)
    bool skip_coverage = false;             // the presented image is deliberately a previous frame's (fault script)
    std::vector<DWORD> previous_presented;  // the last presented image (fault script)
    // Engine scene-end hook script ("hook" mode): the seam exports, the stub
    // code page and its three sites (the verified one, one calling another
    // target, one that is not a CALL), the original bytes, and what the
    // compositor observed on its last call (see fixture_compositor below).
    int (*hook_install)(void*, void*) = nullptr; int (*hook_shutdown)() = nullptr;
    unsigned (*hook_signals)() = nullptr; const char* (*hook_status)() = nullptr;
    static inline Fixture* hook_fixture = nullptr;
    static inline bool hook_glow = true, hook_installed = false, hook_compositor_failed = false;
    static inline unsigned hook_compositor_calls = 0, hook_compositor_signals = 0;
    static inline std::uint32_t hook_registers[4]{};
    unsigned char* hook_code = nullptr; unsigned char* hook_site = nullptr; unsigned char* hook_site_other = nullptr; unsigned char* hook_site_plain = nullptr;
    unsigned char hook_original[5]{};
    void (*hook_stub)() = nullptr;
    bool history_valid = false; // the route holds a resolved previous frame (hook script)
    // Camera: the fake engine globals installed (X3M_FIXTURE_CAMERA=rotate),
    // the policy switch (X3M_TAA_SENTINEL: 0 auto, 1, 2 strict), this frame's
    // scene view, the view of the last frame that resolved (the history's) and
    // the decision the route must have taken at this frame's boundary.
    void (*camera_install)(const float* const*, const float* const*) = nullptr;
    bool camera = false; unsigned sentinel = 0;
    x3m::renderer::CameraState camera_current, camera_history;
    x3m::renderer::SentinelDecision decision;
    bool resolve_expected = true; // false on frames the route cannot resolve (environment map, strict mode without a camera)
    bool scene_rejected = false;  // the selector rejected this frame before its scene draws: the route neither routes nor jitters them
    Com<IDirect3DCubeTexture9> face_cube; Com<IDirect3DSurface9> face_depth, faces[6];
    Reference reference; bool reference_ready = false;
    std::uint64_t frames_since_reset = 0, latches = 0; // the route advances its Halton sequence once per latched frame
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
        api(d->CreatePixelShader(hdr2_program, &hdr2.p), "CreatePixelShader hdr2");
        api(d->CreatePixelShader(hdr8_program, &hdr8.p), "CreatePixelShader hdr8");
        api(d->CreatePixelShader(hdrmid_program, &hdrmid.p), "CreatePixelShader hdrmid");
        api(d->CreatePixelShader(hdrconst_program, &hdrconst.p), "CreatePixelShader hdrconst");
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
    // Process-independent signature of a snapshot (values plus the identity
    // class of each bound object), so two runs can be compared for an
    // identical final state without comparing pointers.
    std::uint64_t state_hash(const Snapshot& x) const {
        std::uint64_t h = 14695981039346656037ull;
        auto mix = [&](const void* data, std::size_t size) { auto b = static_cast<const unsigned char*>(data); for (std::size_t i = 0; i < size; ++i) { h ^= b[i]; h *= 1099511628211ull; } };
        auto tag = [&](unsigned value) { mix(&value, sizeof value); };
        tag(x.rt[0] == back.p ? 1 : x.rt[0] ? 2 : 0); tag(x.rt[1] ? 2 : 0); tag(x.rt[2] ? 2 : 0); tag(x.depth == depth.p ? 1 : x.depth ? 2 : 0);
        mix(&x.viewport, sizeof x.viewport); mix(&x.scissor, sizeof x.scissor); mix(&x.fvf, sizeof x.fvf);
        tag(x.declaration == declaration.p ? 1 : x.declaration ? 2 : 0); tag(x.vs == vs.p ? 1 : x.vs ? 2 : 0);
        tag(x.ps == ps.p ? 1 : x.ps == flat.p ? 3 : x.ps ? 2 : 0); tag(x.stream == vb_a.p ? 1 : x.stream == vb_b.p ? 3 : x.stream ? 2 : 0);
        mix(&x.offset, sizeof x.offset); mix(&x.stride, sizeof x.stride); mix(x.states, sizeof x.states);
        mix(x.rows, sizeof x.rows); mix(x.reserved, sizeof x.reserved); mix(x.pixel, sizeof x.pixel); mix(x.integer0, sizeof x.integer0);
        for (unsigned i = 0; i < sampler_stages; ++i) { tag(x.textures[i] ? 1 : 0); mix(x.samplers[i], sizeof x.samplers[i]); }
        mix(x.ps_low, sizeof x.ps_low); mix(&x.frequency0, sizeof x.frequency0); tag(x.indices ? 1 : 0);
        return h;
    }
    // `before_scene` runs after the background draw and before the scene's
    // depth Clear (the selector's Background phase); the hook script signals there.
    void frame_begin(void (*before_scene)(Fixture&) = nullptr) {
        records.clear(); draw_index = 0; scene_rejected = false;
        // The route advances its Halton sequence at every latching Clear; every
        // regular fixture frame latches, so the n-th latch uses sample (n % samples) + 1.
        pjx = jx; pjy = jy;
        if (jitter) { const unsigned index = unsigned(latches % jitter_samples) + 1; jx = halton(index, 2) - .5; jy = halton(index, 3) - .5; }
        else jx = jy = 0;
        ++latches;
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
        set_camera(frame);
        if (before_scene) before_scene(*this);
        api(d->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1, 0), "Clear depth");
    }
    // The scene view of this frame: one degree of yaw per frame with a
    // 30-degree jump at frame 7 (a camera cut under the 20-degree bound).
    // Written before the depth-only Clear, where the route reads it.
    void set_camera(unsigned long long f) {
        if (!camera) { camera_current = {}; return; }
        camera_current = fake_camera_pose(double(f) + (f >= 7 ? 30. : 0.));
        require(camera_current.valid, "fake camera pose validates");
    }
    // The route's policy decision for this frame's resolve, from the same builder.
    void decide() {
        const auto mode = sentinel == 1 ? x3m::renderer::SentinelMode::CurrentOnly : sentinel == 2 ? x3m::renderer::SentinelMode::Camera : x3m::renderer::SentinelMode::Auto;
        decision = x3m::renderer::camera_sentinel_policy(mode, camera_current, camera_history, 20.f);
        if (sentinel == 2 && (decision.reason == x3m::renderer::SentinelReason::CurrentInvalid || decision.reason == x3m::renderer::SentinelReason::TransformFailed)) resolve_expected = false;
        std::printf("CAMERA_EXPECT frame=%llu installed=%u policy=%u reason=%u cut=%u rotation_deg=%.4f p00=%.7g p11=%.7g r00=%.7g r01=%.7g r02=%.7g r10=%.7g r11=%.7g r12=%.7g r20=%.7g r21=%.7g r22=%.7g resolve=%u\n",
                    frame, camera, decision.policy, unsigned(decision.reason), decision.cut, decision.rotation_degrees, camera_current.m00, camera_current.m11,
                    camera_current.r[0], camera_current.r[1], camera_current.r[2], camera_current.r[3], camera_current.r[4], camera_current.r[5],
                    camera_current.r[6], camera_current.r[7], camera_current.r[8], resolve_expected);
    }
    // One scene draw. `routed`/`matched` are the fixture's expectations from the
    // script; the DLL's own per-draw log is cross-checked by the runner.
    // `verify`: snapshot the state around the draw (every getter is a
    // restore point of the lazy RT mode, so the burst script passes false to
    // keep consecutive routed draws free of application getters).
    void draw(Object& o, float t, float p, float zo, bool known, bool routed, bool matched, Alter alter = Alter::None, bool verify = true) {
        scope(known ? &o : nullptr);
        api(d->SetStreamSource(0, o.vb, 0, 24), "SetStreamSource object");
        IDirect3DPixelShader9* program = alter == Alter::FlatPixel ? flat.p : alter == Alter::Hdr2 ? hdr2.p : (alter == Alter::Hdr8 || alter == Alter::Hdr8Additive) ? hdr8.p : alter == Alter::HdrMid ? hdrmid.p : ps.p;
        api(d->SetVertexShader(vs.p), "SetVertexShader"); api(d->SetPixelShader(program), "SetPixelShader");
        if (alter == Alter::Blend) api(d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE), "blend on");
        if (alter == Alter::Hdr8Additive) {
            api(d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE), "additive on"); api(d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE), "src one");
            api(d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE), "dest one"); api(d->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD), "blend add");
        }
        rows(t, p, zo);
        if (verify) {
            const Snapshot before = snapshot();
            api(d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1), "DrawPrimitive object");
            ++draw_index;
            compare(before, snapshot(), "draw");
        } else {
            api(d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1), "DrawPrimitive object");
            ++draw_index;
        }
        if (alter == Alter::Blend || alter == Alter::Hdr8Additive) api(d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE), "blend off");
        if (alter == Alter::Hdr8Additive) { api(d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE), "src restore"); api(d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO), "dest restore"); }
        const bool live = enabled && seam;
        const bool jittered = enabled && jitter && !scene_rejected;
        records.push_back({&o, t, p, zo, live && routed, live && matched, o.rt, o.rp, o.rzo, alter == Alter::FlatPixel, jittered, live && routed && known});
        std::printf("EXPECT frame=%llu index=%u object=%s routed=%u matched=%u jittered=%u\n", frame, draw_index, o.name, live && routed, live && matched, jittered);
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
                if (!r.object) { depth_value = 1; continue; } // application depth-only Clear
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
        std::printf("MOTION_HASH frame=%llu motion=%016llx depth=%016llx\n", frame,
                    static_cast<unsigned long long>(fnv(data.data(), data.size() * 4)), static_cast<unsigned long long>(fnv(depth_data.data(), depth_data.size() * 4)));
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
                if (!r.object) { depth_value = 1; continue; } // application depth-only Clear: RT1/RT2 keep their values
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
        return (keyed && float(keyed - matched) / float(keyed) > .25f) || (taa && decision.cut);
    }
    // The game's pre-bloom boundary inside the scene: depth unbound, then the
    // main target copied into the bloom source. The route resolves inside the
    // StretchRect hook; the fixture reads the main target on both sides.
    void boundary() {
        decide();
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
        if (!resolve_expected) {
            // The route skipped the resolve (strict policy without a transform,
            // or a rejected environment-map frame): the copy carries the raster.
            require(!changed, "a frame the route cannot resolve leaves the 8-bit main target untouched");
            ++taa_skipped_frames;
        } else if (live) {
            // Reference resolve from the DLL's own inputs: RT1/RT2 read back
            // through the seam, the 8-bit main target read back before the copy.
            std::vector<float> motion_data(std::size_t(W) * H * 4), depth_data(std::size_t(W) * H); unsigned w = 0, h = 0;
            api(readback(d.p, motion_data.data(), unsigned(motion_data.size()), &w, &h), "reference motion readback");
            api(readback_depth(d.p, depth_data.data(), unsigned(depth_data.size()), &w, &h), "reference depth readback");
            reference.upload(before_image, motion_data, depth_data);
            std::vector<DWORD> expected; std::vector<unsigned char> half;
            const auto out = reference.run(jx, jy, pjx, pjy, expected_cut(), decision.matrix, decision.policy == 2, expected, half);
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
        const bool expect_history = live && resolve_expected && frames_since_reset > 0 && !expected_cut();
        require(history == expect_history, "history use follows the script (first frame, Reset and cut frames run current-only)");
        if (!history) require(!changed, "a frame without history leaves the 8-bit main target bit-identical through the FP16 round trip");
        std::printf("TAA frame=%llu history=%u cut=%u changed=%u policy=%u skipped=%u\n", frame, history, expected_cut(), changed, decision.policy, !resolve_expected);
        ++taa_frames; taa_history_frames += history; taa_changed_pixels += changed;
        // The history now holds this frame's scene view (the route records it
        // after a successful resolve); a frame that did not resolve drops the
        // history and its view (the route's invalidate_taa).
        camera_history = resolve_expected ? camera_current : x3m::renderer::CameraState{};
        resolve_expected = true;
        std::printf("COLOR_BEFORE frame=%llu hash=%016llx\n", frame, static_cast<unsigned long long>(color_hash(before_image)));
        verify_coverage(before_image);
    }
    // Every presented frame beside the executable, for the runner's per-pixel
    // comparisons between runs (the HDR twins): row-major BGRA8, no header.
    void write_presented(const std::vector<DWORD>& image) {
        char name[64]; std::snprintf(name, sizeof name, "presented_%llu.bgra8", frame);
        FILE* file = std::fopen(name, "wb"); require(file != nullptr, "presented image written");
        std::fwrite(image.data(), 4, image.size(), file); std::fclose(file);
    }
    void frame_end() {
        if (taa) boundary();
        api(d->EndScene(), "EndScene");
        const auto image = color_image();
        std::printf("COLOR frame=%llu hash=%016llx\n", frame, static_cast<unsigned long long>(color_hash(image)));
        write_presented(image);
        if (!taa && !skip_coverage) verify_coverage(image);
        skip_coverage = false;
        previous_presented = image;
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
        camera_history = {};
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
    // Lazy RT mode equivalence script (both modes run it; the runner compares
    // the colour, the STATE signature, the seam's RT1/RT2 hashes, the DLL's
    // readback files and the per-frame SetRenderTarget counts). Consecutive
    // routed draws have no application getter between them, interleaved with
    // a flat-PS draw (gate 3), a blend draw (gate 4) and, alternating per
    // frame, an application SetRenderTarget(0) or a depth-only Clear inside
    // the scene (both reject the selector: the last draw stops at gate 2).
    // Scope is withheld so every routed draw is sentinel-only (gate 5) on
    // both DLLs. The final state must equal the pre-burst snapshot.
    void burst_draw(Object& o, float t, float p, float zo, Alter alter = Alter::None, bool routed = true) {
        draw(o, t, p, zo, false, routed, false, alter, false);
    }
    void run_burst(unsigned frames) {
        for (unsigned i = 0; i < frames; ++i) {
            frame_begin();
            // The pre-burst snapshot carries the bindings the last draw of the
            // burst leaves behind (object A, reviewed PS, its rows), so the
            // final comparison isolates what the route did.
            api(d->SetStreamSource(0, a.vb, 0, 24), "SetStreamSource A"); api(d->SetPixelShader(ps.p), "SetPixelShader reviewed"); rows(.75f, 0, 0);
            const Snapshot before = snapshot();
            burst_draw(a, .75f, 0, 0); burst_draw(b, 0, 0, 0);
            burst_draw(a, .75f, 0, 0, Alter::FlatPixel, false);
            burst_draw(a, .8f, .125f, 0); burst_draw(b, -.05f, 0, .1f);
            // The lazy-mode hole: an application write of COLORWRITEENABLE1
            // while the route holds RT1 (the SetRenderState hook flushes first),
            // a routed draw under the application's mask, then the application
            // reads the mask back (the GetRenderState hook flushes first) and
            // must see its own value; both modes must produce the same image.
            api(d->SetRenderState(D3DRS_COLORWRITEENABLE1, 7), "application COLORWRITEENABLE1 write between routed draws");
            burst_draw(a, .8f, .125f, 0, Alter::Blend, false);
            burst_draw(b, -.05f, 0, .1f);
            DWORD mask = 0; api(d->GetRenderState(D3DRS_COLORWRITEENABLE1, &mask), "application COLORWRITEENABLE1 read between routed draws");
            require(mask == 7, "the application reads back its own COLORWRITEENABLE1 between routed draws");
            api(d->SetRenderState(D3DRS_COLORWRITEENABLE1, 15), "application COLORWRITEENABLE1 restore");
            if (i % 2 == 0) {
                // Same target rebound: the viewport and scissor rectangle reset with it.
                api(d->SetRenderTarget(0, back.p), "SetRenderTarget 0 (application)");
                const D3DVIEWPORT9 vp{0, 0, W, H, 0, 1}; api(d->SetViewport(&vp), "SetViewport (application)");
                const RECT scissor{8, 8, 40, 40}; api(d->SetScissorRect(&scissor), "SetScissorRect (application)");
            } else {
                api(d->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1, 0), "Clear depth (application, inside the scene)");
                records.push_back({nullptr, 0, 0, 0, false, false, 0, 0, 0});
            }
            burst_draw(a, .75f, 0, 0, Alter::None, false);
            const Snapshot after = snapshot();
            compare(before, after, "burst");
            std::printf("STATE frame=%llu label=burst hash=%016llx\n", frame, static_cast<unsigned long long>(state_hash(after)));
            frame_end();
        }
        require(!(enabled && seam) || frames_verified == frames, "every live burst frame verified against the oracle");
    }
    // Environment-map sequence of the frame routine (camera-state-and-frame-
    // routine.md section 7): mid-frame EndScene, six cube-face target changes
    // each with its own Clear(TARGET|ZBUFFER), a per-face view written to the
    // camera globals and a full material draw, then BeginScene. `before_initial`
    // places it before the frame's first Clear (the marker on the first view),
    // otherwise between the background draw and the scene's depth Clear.
    void env_faces() {
        api(d->EndScene(), "EndScene before the environment map");
        for (UINT face = 0; face < 6; ++face) {
            api(d->SetRenderTarget(0, faces[face].p), "SetRenderTarget face");
            api(d->SetDepthStencilSurface(face_depth.p), "SetDepthStencilSurface face");
            api(d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1, 0), "Clear face");
            fake_camera_pose(90. * face); // the per-face view must never reach the route's camera state
            scope(&a); api(d->SetStreamSource(0, a.vb, 0, 24), "SetStreamSource face"); api(d->SetVertexShader(vs.p), "SetVertexShader face"); api(d->SetPixelShader(ps.p), "SetPixelShader face");
            rows(.75f, 0, 0);
            api(d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1), "DrawPrimitive face"); ++draw_index;
            std::printf("EXPECT frame=%llu index=%u object=A routed=0 matched=0 jittered=0 face=%u\n", frame, draw_index, face);
        }
        api(d->SetRenderTarget(0, back.p), "SetRenderTarget main"); api(d->SetDepthStencilSurface(depth.p), "SetDepthStencilSurface main");
        api(d->BeginScene(), "BeginScene after the environment map");
    }
    void frame_begin_envmap(bool before_initial) {
        records.clear(); draw_index = 0; scene_rejected = true;
        // A frame whose environment map precedes the initial Clear never latches:
        // the route's jitter sequence (and its previous-jitter record) stay put.
        if (!before_initial) {
            pjx = jx; pjy = jy;
            if (jitter) { const unsigned index = unsigned(latches % jitter_samples) + 1; jx = halton(index, 2) - .5; jy = halton(index, 3) - .5; }
            else jx = jy = 0;
            ++latches;
        }
        hostile_states();
        api(d->SetStreamSource(0, vb_a.p, 0, 24), "SetStreamSource"); api(d->SetVertexDeclaration(declaration.p), "SetVertexDeclaration");
        api(d->SetVertexShader(vs.p), "SetVertexShader"); api(d->SetPixelShader(flat.p), "SetPixelShader flat");
        rows(0, 0, 0);
        api(d->BeginScene(), "BeginScene");
        if (before_initial) env_faces();
        api(d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff203040, 1, 0), "Clear initial");
        scope(nullptr);
        api(d->SetPixelShader(flat.p), "SetPixelShader flat");
        api(d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1), "DrawPrimitive background"); ++draw_index;
        if (!before_initial) env_faces();
        scene_states(); material_state();
        set_camera(frame);
        api(d->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1, 0), "Clear depth");
    }
    void run_envmap() {
        require(enabled && seam && taa && camera, "envmap needs the seam, TAA and the camera");
        api(d->CreateCubeTexture(16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &face_cube.p, nullptr), "CreateCubeTexture faces");
        for (UINT face = 0; face < 6; ++face) api(face_cube->GetCubeMapSurface(D3DCUBEMAP_FACES(face), 0, &faces[face].p), "GetCubeMapSurface");
        api(d->CreateDepthStencilSurface(16, 16, D3DFMT_D24X8, D3DMULTISAMPLE_NONE, 0, TRUE, &face_depth.p, nullptr), "CreateDepthStencilSurface face");
        // f0: routed. f1: environment map between background and depth Clear;
        // its scene draws are not routed (the selector rejected the frame).
        // f2: routed again, a cut (f1 recorded no rows). f3: environment map
        // before the initial Clear. f4: routed, a cut again.
        frame_begin(); draw(a, .75f, 0, 0, true, true, false); draw(b, 0, 0, 0, true, true, false); frame_end();
        for (unsigned variant = 0; variant < 2; ++variant) {
            frame_begin_envmap(variant == 1);
            resolve_expected = false;
            draw(a, .8f, .125f, 0, true, false, false); draw(b, -.05f, 0, .1f, true, false, false);
            if (taa) boundary();
            api(d->EndScene(), "EndScene");
            const auto image = color_image();
            std::printf("COLOR frame=%llu hash=%016llx\n", frame, static_cast<unsigned long long>(color_hash(image)));
            write_presented(image);
            api(d->SetDepthStencilSurface(depth.p), "SetDepthStencilSurface rebind");
            api(d->Present(nullptr, nullptr, nullptr, nullptr), "Present");
            ++frame; ++frames_since_reset;
            frame_begin(); draw(a, .75f, 0, 0, true, true, false); draw(b, 0, 0, 0, true, true, false); frame_end();
        }
        require(taa_frames == 5 && taa_skipped_frames == 2 && taa_history_frames == 0, "environment-map script: two rejected frames, no history anywhere");
        for (auto& f : faces) f.reset();
        face_depth.reset(); face_cube.reset();
    }
    // ---- engine scene-end hook script ("hook" mode) ----
    // The frame-routine stub (a VirtualAlloc'd code page): saves the
    // callee-saved registers, loads ESI/EDI/EBX/EBP with markers, CALLs the
    // compositor through a five-byte E8 site (the one the seam patches, like
    // 0x004721b1), stores the four registers after the call, restores and
    // returns. Two more sites: an E8 to another function (target mismatch)
    // and five NOPs (not a CALL); both installs must be refused untouched.
    void hook_create() {
        hook_code = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        require(hook_code != nullptr, "hook stub memory");
        unsigned char* p = hook_code;
        auto byte = [&](unsigned char b) { *p++ = b; };
        auto dword = [&](std::uint32_t v) { std::memcpy(p, &v, 4); p += 4; };
        auto address = [](const void* x) { return std::uint32_t(reinterpret_cast<std::uintptr_t>(x)); };
        auto call = [&](void* target) { unsigned char* site = p; byte(0xe8); dword(address(target) - (address(site) + 5)); return site; };
        byte(0x55); byte(0x56); byte(0x57); byte(0x53);                       // push ebp; push esi; push edi; push ebx
        byte(0xbe); dword(0x51515151); byte(0xbf); dword(0x61616161);         // mov esi/edi, markers
        byte(0xbb); dword(0x71717171); byte(0xbd); dword(0x81818181);         // mov ebx/ebp, markers
        hook_site = call(reinterpret_cast<void*>(&fixture_compositor));
        byte(0x89); byte(0x35); dword(address(&hook_registers[0]));           // mov [regs+0], esi
        byte(0x89); byte(0x3d); dword(address(&hook_registers[1]));           // mov [regs+4], edi
        byte(0x89); byte(0x1d); dword(address(&hook_registers[2]));           // mov [regs+8], ebx
        byte(0x89); byte(0x2d); dword(address(&hook_registers[3]));           // mov [regs+12], ebp
        byte(0x5b); byte(0x5f); byte(0x5e); byte(0x5d); byte(0xc3);           // pop ebx; pop edi; pop esi; pop ebp; ret
        hook_site_other = call(reinterpret_cast<void*>(&fixture_other_target)); byte(0xc3);
        hook_site_plain = p; for (int i = 0; i < 5; ++i) byte(0x90); byte(0xc3);
        std::memcpy(hook_original, hook_site, 5);
        DWORD old = 0; require(VirtualProtect(hook_code, 4096, PAGE_EXECUTE_READ, &old) != FALSE, "hook stub protection");
        FlushInstructionCache(GetCurrentProcess(), hook_code, 4096);
        hook_stub = reinterpret_cast<void (*)()>(hook_code);
        hook_fixture = this;
    }
    unsigned signals() const { return hook_installed ? hook_signals() : 0; }
    // Calls the stub with the compositor idle (glow off) and checks the
    // register/signal contract; `before_scene` is the Background-phase signal
    // of frame 2 (the hook must not end a scene that has not started).
    void stub_call(bool glow) {
        hook_glow = glow; hook_compositor_calls = 0; hook_compositor_failed = false; std::memset(hook_registers, 0, sizeof hook_registers);
        const unsigned before = signals();
        hook_stub();
        require(!hook_compositor_failed && hook_compositor_calls == 1, "the compositor ran exactly once per callsite call");
        require(hook_registers[0] == 0x51515151 && hook_registers[1] == 0x61616161 && hook_registers[2] == 0x71717171 && hook_registers[3] == 0x81818181,
                "ESI/EDI/EBX/EBP preserved across the patched callsite");
        require(signals() - before == (hook_installed ? 1u : 0u), "exactly one signal per callsite call (none unpatched)");
        if (hook_installed) require(hook_compositor_signals == before + 1, "the signal precedes the compositor");
    }
    static void hook_signal_before_scene(Fixture& f) { f.stub_call(false); }
    // One hook-script frame. glow: the compositor unbinds depth and copies the
    // main target (the selector's scene end); off: it does nothing.
    // outside: the only callsite call of this frame happens before the scene's
    // depth Clear and the compositor is then called directly (no signal), so
    // the copy path must resolve as the fallback.
    void hook_frame(bool glow, bool outside) {
        frame_begin(outside ? &Fixture::hook_signal_before_scene : nullptr);
        const bool alternate = frame % 2, matched = frames_since_reset > 0;
        draw(a, alternate ? .8f : .75f, alternate ? .125f : 0, 0, true, true, matched); draw(b, alternate ? -.05f : 0, 0, alternate ? .1f : 0, true, true, matched);
        decide();
        const auto before_image = color_image();
        std::vector<float> motion_data(std::size_t(W) * H * 4), depth_data(std::size_t(W) * H); unsigned w = 0, h = 0;
        api(readback(d.p, motion_data.data(), unsigned(motion_data.size()), &w, &h), "reference motion readback");
        api(readback_depth(d.p, depth_data.data(), unsigned(depth_data.size()), &w, &h), "reference depth readback");
        const Snapshot before = snapshot();
        if (outside) { hook_glow = glow; hook_compositor_calls = 0; hook_compositor_failed = false; fixture_compositor(); require(!hook_compositor_failed && hook_compositor_calls == 1, "the direct compositor ran"); }
        else stub_call(glow);
        Snapshot after = snapshot();
        if (glow) { require(after.depth == nullptr, "the compositor unbound the depth surface"); after.depth = before.depth; }
        compare(before, after, "hook");
        const auto after_image = color_image();
        if (glow) require(color_image(bloom_surface.p) == after_image, "the bloom copy receives the main target as resolved");
        unsigned changed = 0;
        for (std::size_t i = 0; i < after_image.size(); ++i) if (after_image[i] != before_image[i]) { if (++changed <= 4) std::printf("CHANGED frame=%llu x=%u y=%u before=%08lx after=%08lx\n", frame, unsigned(i % W), unsigned(i / W), before_image[i], after_image[i]); }
        const bool at_hook = hook_installed && !outside, resolves = at_hook || glow;
        bool history = false;
        if (resolves) {
            reference.upload(before_image, motion_data, depth_data);
            std::vector<DWORD> expected; std::vector<unsigned char> half;
            const auto out = reference.run(jx, jy, pjx, pjy, expected_cut(), decision.matrix, decision.policy == 2, expected, half);
            history = out.used_history;
            unsigned mismatches = 0;
            for (std::size_t i = 0; i < expected.size(); ++i) if (expected[i] != after_image[i]) { if (++mismatches <= 4) std::printf("TAA_DIFF frame=%llu index=%u actual=%08lx reference=%08lx\n", frame, unsigned(i), after_image[i], expected[i]); }
            require(!mismatches, "main target after the hook/copy equals the reference resolve of the same inputs, byte for byte");
            char name[64]; std::snprintf(name, sizeof name, "reference_taa_%llu.rgba16f", frame);
            FILE* file = std::fopen(name, "wb"); require(file != nullptr, "reference FP16 output written");
            std::fwrite(half.data(), 1, half.size(), file); std::fclose(file);
            ++taa_reference_frames;
        } else {
            require(!changed, "a glow-off frame without the hook leaves the 8-bit main target untouched");
            reference.pass.invalidate(); ++taa_skipped_frames;
        }
        const bool expect_history = resolves && history_valid && !expected_cut();
        require(history == expect_history, "history use follows the script (the route drops history on a frame it cannot resolve)");
        if (!history) require(!changed, "a frame without history leaves the 8-bit main target bit-identical through the FP16 round trip");
        history_valid = resolves;
        camera_history = resolves ? camera_current : x3m::renderer::CameraState{};
        std::printf("TAA frame=%llu history=%u cut=%u changed=%u policy=%u skipped=%u source=%s glow=%u outside=%u\n", frame, history, expected_cut(), changed, decision.policy, !resolves,
                    at_hook ? "hook" : glow ? "stretchrect" : "none", glow, outside);
        ++taa_frames; taa_history_frames += history; taa_changed_pixels += changed;
        std::printf("COLOR_BEFORE frame=%llu hash=%016llx\n", frame, static_cast<unsigned long long>(color_hash(before_image)));
        verify_coverage(before_image);
        api(d->EndScene(), "EndScene");
        const auto image = color_image();
        std::printf("COLOR frame=%llu hash=%016llx\n", frame, static_cast<unsigned long long>(color_hash(image)));
        write_presented(image);
        verify_motion();
        if (glow) api(d->SetDepthStencilSurface(depth.p), "SetDepthStencilSurface rebind");
        api(d->Present(nullptr, nullptr, nullptr, nullptr), "Present");
        ++frame; ++frames_since_reset;
    }
    void run_hook() {
        require(enabled && seam && taa && hook_install && hook_shutdown && hook_signals && hook_status, "hook needs the seam, TAA and the scene-hook exports");
        hook_create();
        char setting[8]{}; const bool want = GetEnvironmentVariableA("X3M_SCENE_HOOK", setting, sizeof setting) == 1 && setting[0] == '1';
        auto compositor = reinterpret_cast<void*>(&fixture_compositor);
        require(hook_install(hook_site_other, compositor) == 0 && !std::strcmp(hook_status(), "target_mismatch"), "install refused on a CALL to another target");
        require(hook_install(hook_site_plain, compositor) == 0 && !std::strcmp(hook_status(), "callsite_mismatch"), "install refused on a site that is not a CALL");
        require(!std::memcmp(hook_site, hook_original, 5) && hook_site_other[0] == 0xe8 && hook_site_plain[0] == 0x90, "refused installs change no bytes");
        if (want) {
            require(hook_install(hook_site, compositor) == 1 && !std::strcmp(hook_status(), "active"), "install on the verified callsite");
            std::uint32_t rel = 0; std::memcpy(&rel, hook_site + 1, 4);
            const auto target = reinterpret_cast<std::uintptr_t>(hook_site) + 5 + rel;
            HMODULE owner = nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(target), &owner);
            require(hook_site[0] == 0xe8 && target != reinterpret_cast<std::uintptr_t>(compositor) && owner == runtime, "the patched site CALLs the DLL's trampoline");
            require(hook_install(hook_site, compositor) == 0 && !std::strcmp(hook_status(), "active"), "a second install is refused while the patch is live");
            hook_installed = true;
        }
        std::printf("HOOK installed=%u status=%s\n", hook_installed, hook_status());
        // f0-f1 glow on (f1 with history); f2 the only signal arrives in the
        // Background phase and the copy path resolves; f3 glow on; f4-f5 glow
        // off (the assessment's case: no bloom copy, the hook still resolves);
        // f6 glow on again.
        hook_frame(true, false); hook_frame(true, false); hook_frame(true, true); hook_frame(true, false);
        hook_frame(false, false); hook_frame(false, false); hook_frame(true, false);
        require(taa_frames == 7, "every hook-script frame ran the boundary");
        require(taa_skipped_frames == (hook_installed ? 0u : 2u) && taa_history_frames == (hook_installed ? 6u : 3u), "hook script: the glow-off frames resolve only through the hook");
        if (hook_installed) {
            require(hook_shutdown() == 1 && !std::strcmp(hook_status(), "restored"), "shutdown restores the callsite");
            require(!std::memcmp(hook_site, hook_original, 5), "restored bytes are the original CALL");
            hook_installed = false;
            stub_call(false);
            require(hook_shutdown() == 1, "shutdown without a patch is a no-op");
        }
        VirtualFree(hook_code, 0, MEM_RELEASE); hook_code = nullptr; hook_fixture = nullptr;
    }
    // ---- FP16 HDR scene path scripts (X3M_HDR=1, seam) ----
    // The FP16 target as floats through the seam.
    std::vector<float> hdr_image(unsigned* w, unsigned* h) {
        std::vector<float> data(std::size_t(W) * H * 4 * 4); // room for a larger target after a dimension change
        api(hdr_readback(d.p, data.data(), unsigned(data.size()), w, h), "hdr readback");
        data.resize(std::size_t(*w) * *h * 4);
        return data;
    }
    // One hdrvalues frame: A drawn with 2.0 (plain), A again with 8.0
    // additively (10.0, alpha 1.5), B with 8.0 plain (8.0, alpha 0.5; B lies
    // inside A at equal depth, LESSEQUAL) or, with `mid`, with the in-range
    // (.75, .25, .375, .625) whose exact 8-bit codes are checked.
    // `switch_target`: between the A draws and B the application binds
    // another surface, clears and draws it, then rebinds the main target (the
    // environment-map shape mid-scene).
    void hdrvalues_frame(bool switch_target, IDirect3DSurface9* other, IDirect3DSurface9* other_depth, bool mid = false) {
        frame_begin();
        draw(a, .75f, 0, 0, true, false, false, Alter::Hdr2);
        draw(a, .75f, 0, 0, true, false, false, Alter::Hdr8Additive);
        if (switch_target) {
            api(d->SetRenderTarget(0, other), "SetRenderTarget other"); api(d->SetDepthStencilSurface(other_depth), "SetDepthStencilSurface other");
            api(d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff00ff00, 1, 0), "Clear other");
            api(d->SetPixelShader(flat.p), "SetPixelShader flat other"); api(d->SetStreamSource(0, a.vb, 0, 24), "SetStreamSource other"); rows(.75f, 0, 0);
            api(d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1), "DrawPrimitive other"); ++draw_index;
            std::printf("EXPECT frame=%llu index=%u object=A routed=0 matched=0 jittered=0 face=0\n", frame, draw_index);
            IDirect3DSurface9* bound = nullptr; api(d->GetRenderTarget(0, &bound), "GetRenderTarget other"); bound->Release();
            require(bound == other, "the application's other target is reported while the redirect is suspended");
            api(d->SetRenderTarget(0, back.p), "SetRenderTarget main"); api(d->SetDepthStencilSurface(depth.p), "SetDepthStencilSurface main");
            api(d->GetRenderTarget(0, &bound), "GetRenderTarget main"); bound->Release();
            require(bound == back.p, "the application's main target is reported after the rebind");
            scene_rejected = true; // the selector rejected the frame at the switch: no routing, no jitter
        }
        draw(b, 0, 0, 0, true, false, false, mid ? Alter::HdrMid : Alter::Hdr8);
        api(d->EndScene(), "EndScene");
        const auto image = color_image();
        std::printf("COLOR frame=%llu hash=%016llx\n", frame, static_cast<unsigned long long>(color_hash(image)));
        write_presented(image);
        unsigned w = 0, h = 0;
        const auto fp16 = hdr_image(&w, &h);
        require(w == W && h == H, "the FP16 target matches the main dimensions");
        unsigned checked = 0, a_pixels = 0, b_pixels = 0, background = 0, mismatches = 0, alpha_b = 0;
        double max_error = 0;
        for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
            double ox, oy, wc; object_point(x, y, .75f, 0, ox, oy, wc);
            if (edge_distance(a, ox, oy) < 1e-3) continue;
            double bx, by, bw; object_point(x, y, 0, 0, bx, by, bw);
            if (edge_distance(b, bx, by) < 1e-3) continue;
            const bool in_b = b.covers(bx, by), in_a = a.covers(ox, oy);
            const float* v = &fp16[(std::size_t(y) * W + x) * 4];
            const DWORD c = image[std::size_t(y) * W + x];
            double expected[4]; DWORD expected8;
            if (in_b && mid) { expected[0] = .75; expected[1] = .25; expected[2] = .375; expected[3] = .625; expected8 = hdrmid_presented; ++b_pixels; }
            else if (in_b) { expected[0] = expected[1] = expected[2] = 8; expected[3] = .5; expected8 = 0x80ffffff; ++b_pixels; }
            else if (in_a) { expected[0] = expected[1] = expected[2] = 10; expected[3] = 1.5; expected8 = 0xffffffff; ++a_pixels; }
            else { expected[0] = 0x20 / 255.; expected[1] = 0x30 / 255.; expected[2] = 0x40 / 255.; expected[3] = 1; expected8 = 0xff203040; ++background; }
            ++checked;
            bool ok = true;
            for (unsigned k = 0; k < 4; ++k) { const double e = std::fabs(v[k] - expected[k]); max_error = std::max(max_error, e); ok = ok && e <= 2e-3; }
            // FP16 readback order is RGBA; the presented DWORD is BGRA8. Alpha 0.5 stores as 127 or 128.
            const DWORD rgb = c & 0x00ffffffu, alpha = c >> 24;
            if (in_b && !mid) { ok = ok && rgb == 0x00ffffffu && (alpha == 127 || alpha == 128); alpha_b += alpha == 128; }
            else ok = ok && c == expected8; // mid: the exact codes 191, 64, 96, 159
            if (!ok && ++mismatches <= 8) std::printf("HDR_DIFF frame=%llu x=%u y=%u fp16=%.6g,%.6g,%.6g,%.6g presented=%08lx expected=%08lx\n", frame, x, y, v[0], v[1], v[2], v[3], c, expected8);
        }
        std::printf("HDR_VALUES frame=%llu width=%u height=%u checked=%u a=%u b=%u background=%u mismatches=%u max_error=%.6g alpha_b_128=%u switch=%u mid=%u\n",
                    frame, w, h, checked, a_pixels, b_pixels, background, mismatches, max_error, alpha_b, switch_target, mid);
        require(!mismatches && a_pixels > 500 && b_pixels > 50 && background > 100, "FP16 target holds the unclamped sums (10.0, 8.0, alpha carried) and the presented frame the clamped values");
        api(d->Present(nullptr, nullptr, nullptr, nullptr), "Present");
        ++frame; ++frames_since_reset;
    }
    void run_hdrvalues() {
        require(enabled && seam && hdr && hdr_readback && hdr_fault, "hdrvalues needs the seam, the HDR switch and its exports");
        Com<IDirect3DTexture9> other_texture; Com<IDirect3DSurface9> other, other_depth;
        api(d->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &other_texture.p, nullptr), "CreateTexture other");
        api(other_texture->GetSurfaceLevel(0, &other.p), "other level");
        api(d->CreateDepthStencilSurface(16, 16, D3DFMT_D24X8, D3DMULTISAMPLE_NONE, 0, TRUE, &other_depth.p, nullptr), "CreateDepthStencilSurface other");
        hdrvalues_frame(false, nullptr, nullptr);
        hdrvalues_frame(false, nullptr, nullptr, true);
        // Reset with a dimension change: the FP16 target is released before
        // the Reset and re-created at the new size at the next latch.
        other.reset(); other_depth.reset(); other_texture.reset();
        W = 48; H = 40; pp.BackBufferWidth = W; pp.BackBufferHeight = H;
        reset();
        api(d->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &other_texture.p, nullptr), "CreateTexture other");
        api(other_texture->GetSurfaceLevel(0, &other.p), "other level");
        api(d->CreateDepthStencilSurface(16, 16, D3DFMT_D24X8, D3DMULTISAMPLE_NONE, 0, TRUE, &other_depth.p, nullptr), "CreateDepthStencilSurface other");
        hdrvalues_frame(false, nullptr, nullptr);
        hdrvalues_frame(true, other.p, other_depth.p);
        hdrvalues_frame(false, nullptr, nullptr, true);
        other.reset(); other_depth.reset(); other_texture.reset();
        // Reset while the redirect is active (no Present between the latch and
        // the Reset): the route must hand RT0 back to the application's main
        // surface and drop its objects before the Reset, which must succeed;
        // the frame after it redirects again (a third hdr_target line) and the
        // teardown's zero-reference check covers the released main surface.
        frame_begin();
        draw(a, .75f, 0, 0, true, false, false, Alter::Hdr2);
        api(d->EndScene(), "EndScene");
        IDirect3DSurface9* bound = nullptr; api(d->GetRenderTarget(0, &bound), "GetRenderTarget before Reset"); bound->Release();
        require(bound == back.p, "the application's main target is reported before the Reset");
        reset();
        std::printf("HDR_RESET_ACTIVE frame=%llu\n", frame);
        hdrvalues_frame(false, nullptr, nullptr);
    }
    // One hdrfault frame: `fault` (renderer::HdrFault kind, 0 none) injected
    // before the frame's draws; even frames draw A and B, odd frames A only,
    // so a stale image is distinguishable from a fresh one. `stale`: the
    // presented image must be the previous frame's (no copy rung available);
    // `ldr`: the frame is expected to run without the redirect.
    void hdrfault_frame(unsigned fault, bool stale, bool ldr) {
        if (fault) hdr_fault(d.p, fault, 1);
        frame_begin();
        const bool even = frame % 2 == 0;
        // A with the material pair (scope withheld: routed sentinel-only, so
        // the four-format MRT is written in every frame of this script), B flat.
        draw(a, .75f, 0, 0, false, true, false);
        if (even) draw(b, 0, 0, 0, false, false, false, Alter::FlatPixel);
        skip_coverage = stale;
        const auto expected_stale = previous_presented;
        frame_end();
        // Without a copy rung the main target keeps whatever it held: the
        // previous frame's image only if the swap chain retained it. This
        // fixture presents with D3DSWAPEFFECT_DISCARD like the game, so the
        // content is undefined after Present (this backend returns zeros); the
        // ladder's third rung guarantees the binding, not an image. Measured,
        // not asserted; the frame after must recover (the script's next frame).
        unsigned previous_equal = 0, black = 0;
        if (stale) {
            previous_equal = !expected_stale.empty() && previous_presented == expected_stale;
            for (DWORD v : previous_presented) black += v == 0;
        }
        std::printf("HDR_FAULT frame=%llu fault=%u stale=%u ldr=%u previous_equal=%u black=%u pixels=%u\n", frame - 1, fault, stale, ldr, previous_equal, black, unsigned(previous_presented.size()));
    }
    void run_hdrfault() {
        require(enabled && seam && hdr && hdr_readback && hdr_fault, "hdrfault needs the seam, the HDR switch and its exports");
        // f0 normal; f1 the copy draw fails (StretchRect rung); f2 the
        // restoration fails after a good draw; f3 both copy rungs fail (the
        // binding rung: previous image); f4 device lost reported by the draw
        // (previous image); f5 normal again after the recheck.
        hdrfault_frame(0, false, false);
        hdrfault_frame(4, false, false);
        hdrfault_frame(7, false, false);
        hdrfault_frame(6, true, false);
        hdrfault_frame(5, true, false);
        hdrfault_frame(0, false, false);
        // f6/f7: the target cannot be created after a Reset (no redirect until
        // the next Reset); f8 redirected again; f9 the latch bind fails (no
        // redirect for that frame only); f10 normal; f11 a draw failure
        // followed by f12 whose recovery self test fails (blocked); f13 after
        // a Reset (the block is cleared).
        reset();
        hdrfault_frame(8, false, true);
        hdrfault_frame(0, false, true);
        reset();
        hdrfault_frame(0, false, false);
        hdrfault_frame(9, false, true);
        hdrfault_frame(0, false, false);
        hdrfault_frame(4, false, false);
        hdrfault_frame(3, false, true);
        reset();
        hdrfault_frame(0, false, false);
        // f14: the latching Clear "fails" (seam kind 10): the binding goes back
        // without a write-back (the never-cleared target must not reach the
        // main target), the frame runs LDR on an uncleared main target.
        hdrfault_frame(10, true, false);
    }
    // ---- Stage 2 scripts (AgX tonemap, exposure) ----------------------------
    // One pre-transformed quad of a constant engine-space value through the
    // hdrconst program (never routed: DrawPrimitiveUP is not a route hook,
    // and the latch already marked the FP16 target pending).
    void constant_quad(float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
        struct V { float x, y, z, rhw; };
        const V quad[4] = {{x0 - .5f, y0 - .5f, 0, 1}, {x1 - .5f, y0 - .5f, 0, 1}, {x0 - .5f, y1 - .5f, 0, 1}, {x1 - .5f, y1 - .5f, 0, 1}};
        const float c0[4] = {r, g, b, a};
        api(d->SetPixelShaderConstantF(0, c0, 1), "SetPixelShaderConstantF c0");
        api(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof quad[0]), "DrawPrimitiveUP constant quad");
    }
    void constant_state() {
        api(d->SetVertexShader(nullptr), "SetVertexShader null"); api(d->SetPixelShader(hdrconst.p), "SetPixelShader hdrconst");
        api(d->SetFVF(D3DFVF_XYZRHW), "SetFVF XYZRHW");
        for (auto s : {D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_FOGENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_STENCILENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_SRGBWRITEENABLE})
            api(d->SetRenderState(s, FALSE), "constant state off");
        api(d->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID), "fill"); api(d->SetRenderState(D3DRS_COLORWRITEENABLE, 15), "write"); api(d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE), "cull");
        const D3DVIEWPORT9 vp{0, 0, W, H, 0, 1}; api(d->SetViewport(&vp), "SetViewport full");
    }
    void exposure_state_line(const char* tag) {
        if (!hdr_exposure) return;
        float e[8]{}; api(hdr_exposure(d.p, e, 8), "hdr exposure readback");
        std::printf("%s frame=%llu ev=%.6f ev_adapted=%.6f ev_target=%.6f avg_log_l=%.6f dt=%.6f exposure=%.6f steps=%u k=%.6f\n",
                    tag, frame, e[0], e[1], e[2], e[3], e[4], e[5], unsigned(e[6]), e[7]);
    }
    // hdrramp: 64x65 target, row r = ramp_value(r) (0.001..64), columns
    // neutral / red / green / blue, alpha r/64. The presented cell centre and
    // its uniformity are printed; the FP16 input is in the DLL's capture-frame
    // readback (hdr_1_<frame>.rgba16f), so the runner compares the compiled
    // program against the Python reference on the exact FP16 inputs.
    void hdrramp_frame() {
        frame_begin();
        constant_state();
        for (unsigned row = 0; row < ramp_rows; ++row) {
            const float x = float(ramp_value(row)), a = float(row) / 64.f;
            const float values[ramp_columns][3] = {{x, x, x}, {x, 0, 0}, {0, x, 0}, {0, 0, x}};
            for (unsigned col = 0; col < ramp_columns; ++col)
                constant_quad(float(col * ramp_column_width), float(row), float((col + 1) * ramp_column_width), float(row + 1), values[col][0], values[col][1], values[col][2], a);
        }
        api(d->EndScene(), "EndScene");
        const auto image = color_image();
        std::printf("COLOR frame=%llu hash=%016llx\n", frame, static_cast<unsigned long long>(color_hash(image)));
        write_presented(image);
        unsigned nonuniform = 0;
        for (unsigned row = 0; row < ramp_rows; ++row) for (unsigned col = 0; col < ramp_columns; ++col) {
            const DWORD centre = image[std::size_t(row) * W + col * ramp_column_width + ramp_column_width / 2];
            bool uniform = true;
            for (unsigned x = 0; x < ramp_column_width; ++x) uniform = uniform && image[std::size_t(row) * W + col * ramp_column_width + x] == centre;
            nonuniform += !uniform;
            std::printf("RAMP frame=%llu row=%u col=%u input=%.9g presented=%08lx uniform=%u\n", frame, row, col, ramp_value(row), centre, uniform);
        }
        require(!nonuniform, "every ramp cell presents one code on all its pixels");
        exposure_state_line("EXPOSURE_STATE");
        api(d->Present(nullptr, nullptr, nullptr, nullptr), "Present");
        ++frame; ++frames_since_reset;
    }
    void run_hdrramp() {
        require(enabled && hdr, "hdrramp needs the route and the HDR switch");
        for (unsigned i = 0; i < 3; ++i) hdrramp_frame();
    }
    // hdrexposure: four 32x32 blocks of constant engine-space values per
    // frame (a dark scene, a bright one, the dark one with a block far above
    // the meter clip, then the hazard and NaN blocks); the state the tonemap
    // consumed this frame is printed after the latch, the presented block
    // centres after the write-back.
    void hdrexposure_frame(const float (&blocks)[4][4]) {
        frame_begin();
        exposure_state_line("EXPOSURE_STATE");
        constant_state();
        for (unsigned i = 0; i < 4; ++i) {
            const float x0 = float((i % 2) * (W / 2)), y0 = float((i / 2) * (H / 2));
            constant_quad(x0, y0, x0 + float(W / 2), y0 + float(H / 2), blocks[i][0], blocks[i][1], blocks[i][2], blocks[i][3]);
        }
        std::printf("EXPOSURE_BLOCKS frame=%llu b0=%.9g,%.9g,%.9g,%.9g b1=%.9g,%.9g,%.9g,%.9g b2=%.9g,%.9g,%.9g,%.9g b3=%.9g,%.9g,%.9g,%.9g\n", frame,
                    blocks[0][0], blocks[0][1], blocks[0][2], blocks[0][3], blocks[1][0], blocks[1][1], blocks[1][2], blocks[1][3],
                    blocks[2][0], blocks[2][1], blocks[2][2], blocks[2][3], blocks[3][0], blocks[3][1], blocks[3][2], blocks[3][3]);
        api(d->EndScene(), "EndScene");
        const auto image = color_image();
        std::printf("COLOR frame=%llu hash=%016llx\n", frame, static_cast<unsigned long long>(color_hash(image)));
        write_presented(image);
        DWORD centre[4]; unsigned nonuniform = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const unsigned x0 = (i % 2) * (W / 2), y0 = (i / 2) * (H / 2);
            centre[i] = image[std::size_t(y0 + H / 4) * W + x0 + W / 4];
            for (unsigned y = y0; y < y0 + H / 2; ++y) for (unsigned x = x0; x < x0 + W / 2; ++x) nonuniform += image[std::size_t(y) * W + x] != centre[i];
        }
        std::printf("EXPOSURE_PRESENTED frame=%llu p0=%08lx p1=%08lx p2=%08lx p3=%08lx nonuniform=%u\n", frame, centre[0], centre[1], centre[2], centre[3], nonuniform);
        require(!nonuniform, "every block presents one code");
        api(d->Present(nullptr, nullptr, nullptr, nullptr), "Present");
        ++frame; ++frames_since_reset;
    }
    void run_hdrexposure() {
        require(enabled && hdr && hdr_exposure, "hdrexposure needs the route, the HDR switch and the exposure export");
        const float dark[4][4] = {{.18f, .18f, .18f, 1}, {.18f, .18f, .18f, 1}, {.18f, .18f, .18f, 1}, {.18f, .18f, .18f, 1}};
        const float bright[4][4] = {{1, .8f, .9f, 1}, {.9f, 1, .8f, .5f}, {.8f, .9f, 1, 1}, {1, 1, 1, .25f}};
        const float sun[4][4] = {{.18f, .18f, .18f, 1}, {.18f, .18f, .18f, 1}, {.18f, .18f, .18f, 1}, {100, 100, 100, 1}};
        for (unsigned i = 0; i < 10; ++i) hdrexposure_frame(dark);
        for (unsigned i = 0; i < 10; ++i) hdrexposure_frame(bright);
        for (unsigned i = 0; i < 10; ++i) hdrexposure_frame(sun);
        // Hazard pixels (review 23): a negative block and +Inf / -Inf blocks
        // are deterministic (the decode floors the negatives, the meter clips
        // the infinity to meter_clip, the tonemap clamps it to 65504); a NaN
        // block's treatment by the backend's min/max is unspecified, so the
        // host-side isfinite check on the 1x1 readback must keep the exposure
        // state finite whether the NaN is swallowed or reaches the result.
        const float inf = std::numeric_limits<float>::infinity(), nan = std::numeric_limits<float>::quiet_NaN();
        const float hazard[4][4] = {{-1, -1, -1, 1}, {inf, inf, inf, 1}, {-inf, -inf, -inf, 1}, {.18f, .18f, .18f, 1}};
        const float poison[4][4] = {{nan, nan, nan, 1}, {.18f, .18f, .18f, 1}, {.18f, .18f, .18f, 1}, {.18f, .18f, .18f, 1}};
        for (unsigned i = 0; i < 5; ++i) hdrexposure_frame(hazard);
        for (unsigned i = 0; i < 5; ++i) hdrexposure_frame(poison);
    }
    // hdrtonemapfault: the regular material/flat scene with the AgX write-back;
    // one injected fault per frame (11: the tonemap draw fails -> the identity
    // draw, an unwind; 13: the meter chain fails -> the exposure holds).
    // Coverage is not asserted (the presented colours are tonemapped); the
    // runner compares the presented image against the reference from the
    // FP16 capture readback. With X3M_FIXTURE_HDR_FAULT=12 queued at attach
    // the tonemap program "fails to create": every frame is identity.
    void hdrtonemapfault_frame(unsigned fault) {
        if (fault) hdr_fault(d.p, fault, 1);
        frame_begin();
        exposure_state_line("EXPOSURE_STATE");
        draw(a, .75f, 0, 0, false, true, false);
        draw(b, 0, 0, 0, false, false, false, Alter::FlatPixel);
        skip_coverage = true;
        frame_end();
        std::printf("HDR_TONEMAP_FAULT frame=%llu fault=%u\n", frame - 1, fault);
    }
    void run_hdrtonemapfault() {
        require(enabled && seam && hdr && hdr_fault && hdr_exposure, "hdrtonemapfault needs the seam, the HDR switch and its exports");
        char setting[8]{};
        if (GetEnvironmentVariableA("X3M_FIXTURE_HDR_FAULT", setting, sizeof setting) > 0) { for (unsigned i = 0; i < 3; ++i) hdrtonemapfault_frame(0); return; }
        // f0 normal; f1 tonemap draw fails (identity fallback, recheck next);
        // f2 normal; f3 meter fails (tonemap applied, no step next); f4 normal;
        // f5, f6 tonemap draw fails again (the third failure disables the
        // tonemap for the device); f7, f8 identity from then on.
        const unsigned script[] = {0, 11, 0, 13, 0, 11, 11, 0, 0};
        for (unsigned fault : script) hdrtonemapfault_frame(fault);
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
        // Render-state shadow: a second block captures the reviewed PS with
        // blending off, the application then enables blending through
        // SetRenderState, and Apply puts it back without a SetRenderState
        // call. The next draw (B, routed and matched by the script) passes
        // gate 4 only if the shadow dropped the recorded TRUE at the Apply.
        api(d->SetPixelShader(ps.p), "SetPixelShader reviewed for the second block");
        Com<IDirect3DStateBlock9> restore_block; api(d->CreateStateBlock(D3DSBT_ALL, &restore_block.p), "CreateStateBlock (blend off)");
        api(d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE), "blend on before Apply");
        api(restore_block->Apply(), "StateBlock Apply (blend off again)");
    }
    // Render-state shadow: a write recorded between BeginStateBlock and
    // EndStateBlock never reaches the device; the block is dropped unapplied.
    // The draws that follow (frame 8) must still pass gate 4 with z writes on.
    void recorded_write_case() {
        api(d->BeginStateBlock(), "BeginStateBlock");
        api(d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE), "recorded z-write off (not applied)");
        Com<IDirect3DStateBlock9> recorded; api(d->EndStateBlock(&recorded.p), "EndStateBlock");
        DWORD value = 1; api(d->GetRenderState(D3DRS_ZWRITEENABLE, &value), "GetRenderState after recording");
        require(value == TRUE, "a recorded render-state write does not reach the device");
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
        frame_begin(); recorded_write_case(); draw(a, .75f, 0, 0, true, true, false); draw(b, -.05f, 0, .1f, true, true, true); frame_end();
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
            // The camera script adds a cut at frame 7 (30 degrees); strict mode
            // skips frames 0, 7 and 9 (no transform: no previous view, a cut, Reset).
            require(taa_history_frames == (live && !(sentinel == 2 && !camera) ? (camera && sentinel != 1 ? 5u : 6u) : 0u), "history frames follow the script");
            require(taa_skipped_frames == (sentinel == 2 && !camera ? 12u : 0u), "strict mode skips exactly the frames whose camera cannot be read");
            require(!live || taa_reference_frames + taa_skipped_frames == 12, "every seam frame compared against the reference resolve");
        }
    }
};
} // namespace
// The glow pass stand-in: records the signal count at entry (the trampoline's
// signal must precede it), then with glow the depth unbind and the bloom copy
// of the frame routine's 0x004c4750, without glow nothing. Exceptions stay
// inside (the stub frame has no unwind information).
extern "C" void __cdecl fixture_compositor() {
    auto& f = *Fixture::hook_fixture;
    ++Fixture::hook_compositor_calls;
    Fixture::hook_compositor_signals = f.signals();
    if (!Fixture::hook_glow) return;
    try {
        api(f.d->SetDepthStencilSurface(nullptr), "compositor SetDepthStencilSurface null");
        api(f.d->StretchRect(f.back.p, nullptr, f.bloom_surface.p, nullptr, D3DTEXF_NONE), "compositor StretchRect bloom copy");
    } catch (...) { Fixture::hook_compositor_failed = true; }
}
extern "C" void __cdecl fixture_other_target() { ++Fixture::hook_compositor_calls; }

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    int exit_code = 1;
    WNDCLASSA cls{}; cls.lpfnWndProc = DefWindowProcA; cls.hInstance = GetModuleHandleA(nullptr); cls.lpszClassName = "X3MotionOutput";
    RegisterClassA(&cls);
    HWND window = CreateWindowA(cls.lpszClassName, "Live motion route fixture", WS_OVERLAPPEDWINDOW, 0, 0, 96, 96, nullptr, nullptr, cls.hInstance, nullptr);
    HMODULE runtime = LoadLibraryA("d3d9.dll");
    try {
        if ((argc != 4 && argc != 5) || !window || !runtime) throw std::runtime_error("usage: fixture <vs.bin> <ps.bin> production|seam|bench|burst|envmap|hook|hdrvalues|hdrfault|hdrramp|hdrexposure|hdrtonemapfault [WxH]");
        Fixture f;
        f.runtime = runtime; f.window = window;
        const std::string mode = argv[3];
        f.bench = mode == "bench";
        f.burst = mode == "burst";
        f.envmap = mode == "envmap";
        f.hook = mode == "hook";
        f.hdrvalues = mode == "hdrvalues";
        f.hdrfault = mode == "hdrfault";
        f.hdrramp = mode == "hdrramp"; f.hdrexposure = mode == "hdrexposure"; f.hdrtonemapfault = mode == "hdrtonemapfault";
        if (f.hdrramp) { Fixture::W = 64; Fixture::H = ramp_rows; }
        if (f.bench) {
            unsigned w = 0, h = 0;
            if (argc != 5 || std::sscanf(argv[4], "%ux%u", &w, &h) != 2 || !w || !h || w > 8192 || h > 8192) throw std::runtime_error("bench needs WxH");
            Fixture::W = w; Fixture::H = h;
        }
        f.configure = symbol<void (*)(const x3m::MotionOutputFixtureConfig*)>(runtime, "x3m_motion_output_fixture_configure", false);
        f.readback = symbol<HRESULT (*)(IDirect3DDevice9*, float*, unsigned, unsigned*, unsigned*)>(runtime, "x3m_motion_output_fixture_readback", false);
        f.readback_depth = symbol<HRESULT (*)(IDirect3DDevice9*, float*, unsigned, unsigned*, unsigned*)>(runtime, "x3m_motion_output_fixture_readback_depth", false);
        f.last_pixel_abi = symbol<HRESULT (*)(IDirect3DDevice9*, float*, unsigned)>(runtime, "x3m_motion_output_fixture_last_pixel_abi", false);
        f.camera_install = symbol<void (*)(const float* const*, const float* const*)>(runtime, "x3m_camera_state_fixture_install", false);
        f.hook_install = symbol<int (*)(void*, void*)>(runtime, "x3m_scene_hook_fixture_install", false);
        f.hook_shutdown = symbol<int (*)()>(runtime, "x3m_scene_hook_fixture_shutdown", false);
        f.hook_signals = symbol<unsigned (*)()>(runtime, "x3m_scene_hook_fixture_signals", false);
        f.hook_status = symbol<const char* (*)()>(runtime, "x3m_scene_hook_fixture_status", false);
        f.hdr_fault = symbol<void (*)(IDirect3DDevice9*, unsigned, unsigned)>(runtime, "x3m_hdr_fixture_fault", false);
        f.hdr_readback = symbol<HRESULT (*)(IDirect3DDevice9*, float*, unsigned, unsigned*, unsigned*)>(runtime, "x3m_hdr_fixture_readback", false);
        f.hdr_exposure = symbol<HRESULT (*)(IDirect3DDevice9*, float*, unsigned)>(runtime, "x3m_hdr_fixture_exposure", false);
        f.seam = f.configure && f.readback && f.readback_depth && f.last_pixel_abi && f.camera_install;
        require(f.bench || f.burst || f.envmap || f.hook || f.hdrvalues || f.hdrfault || f.hdrramp || f.hdrexposure || f.hdrtonemapfault || f.seam == (mode == "seam"), "DLL seam presence matches the requested mode");
        char setting[8]{}; f.enabled = GetEnvironmentVariableA("X3M_MOTION_OUTPUT", setting, sizeof setting) == 1 && setting[0] == '1';
        f.taa = f.enabled && GetEnvironmentVariableA("X3M_TAA", setting, sizeof setting) == 1 && setting[0] == '1';
        // The DLL implies the jitter with the resolve on.
        f.jitter = f.enabled && (f.taa || (GetEnvironmentVariableA("X3M_MOTION_JITTER", setting, sizeof setting) == 1 && setting[0] == '1'));
        if (f.bench) f.taa = true; // The bench always runs the game-like boundary; the resolve follows X3M_TAA.
        if (GetEnvironmentVariableA("X3M_MOTION_JITTER_SAMPLES", setting, sizeof setting) > 0) { const unsigned n = unsigned(std::atoi(setting)); if (n >= 2 && n <= 64) f.jitter_samples = n; }
        char rt_mode[8]{}; f.lazy = GetEnvironmentVariableA("X3M_MOTION_RT_MODE", rt_mode, sizeof rt_mode) == 4 && !std::strcmp(rt_mode, "lazy");
        char camera_mode[8]{}; f.camera = f.seam && GetEnvironmentVariableA("X3M_FIXTURE_CAMERA", camera_mode, sizeof camera_mode) == 6 && !std::strcmp(camera_mode, "rotate");
        if (GetEnvironmentVariableA("X3M_TAA_SENTINEL", setting, sizeof setting) > 0) f.sentinel = !std::strcmp(setting, "1") ? 1 : !std::strcmp(setting, "2") ? 2 : 0;
        f.state_shadow = !(GetEnvironmentVariableA("X3M_STATE_SHADOW", setting, sizeof setting) == 1 && setting[0] == '0');
        f.hdr = f.enabled && GetEnvironmentVariableA("X3M_HDR", setting, sizeof setting) == 1 && setting[0] == '1';
        // A caps/self-test fault must be queued before the device is created (attach).
        if (f.hdr_fault && GetEnvironmentVariableA("X3M_FIXTURE_HDR_FAULT", setting, sizeof setting) > 0) f.hdr_fault(nullptr, unsigned(std::atoi(setting)), 1);
        if (f.camera) { fake_camera_pose(0); f.camera_install(&fake_projection_slot, &fake_view_slot); }
        char path[MAX_PATH]{}; GetModuleFileNameA(runtime, path, MAX_PATH);
        std::printf("MODE seam=%u enabled=%u jitter=%u jitter_samples=%u taa=%u bench=%u width=%u height=%u dll=%s burst=%u rt_mode=%s camera=%u sentinel=%u envmap=%u hook=%u state_shadow=%u hdr=%u hdrvalues=%u hdrfault=%u hdrramp=%u hdrexposure=%u hdrtonemapfault=%u\n", f.seam, f.enabled, f.jitter, f.jitter_samples, f.taa, f.bench, Fixture::W, Fixture::H, path, f.burst, f.lazy ? "lazy" : "perdraw", f.camera, f.sentinel, f.envmap, f.hook, f.state_shadow, f.hdr, f.hdrvalues, f.hdrfault, f.hdrramp, f.hdrexposure, f.hdrtonemapfault);
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
        if (f.bench) f.run_bench(24); else if (f.burst) f.run_burst(9); else if (f.envmap) f.run_envmap(); else if (f.hook) f.run_hook();
        else if (f.hdrvalues) f.run_hdrvalues(); else if (f.hdrfault) f.run_hdrfault();
        else if (f.hdrramp) f.run_hdrramp(); else if (f.hdrexposure) f.run_hdrexposure(); else if (f.hdrtonemapfault) f.run_hdrtonemapfault(); else f.run();
        if (f.reference_ready) { f.reference.destroy(); f.reference_ready = false; }
        // Teardown: every fixture object released, then the device must reach zero.
        f.back.reset(); f.depth.reset(); f.bloom_surface.reset(); f.bloom.reset();
        for (UINT i = 0; i < 4; ++i) api(f.d->SetTexture(i, nullptr), "SetTexture null");
        api(f.d->SetVertexShader(nullptr), "unbind"); api(f.d->SetPixelShader(nullptr), "unbind"); api(f.d->SetStreamSource(0, nullptr, 0, 0), "unbind"); api(f.d->SetVertexDeclaration(nullptr), "unbind");
        f.vs.reset(); f.ps.reset(); f.flat.reset(); f.hdr2.reset(); f.hdr8.reset(); f.hdrmid.reset(); f.hdrconst.reset(); f.declaration.reset(); f.vb_a.reset(); f.vb_b.reset(); f.cube.reset();
        for (auto& t : f.textures) t.reset();
        const ULONG device_refs = f.d.p->Release(); f.d.p = nullptr;
        require(device_refs == 0, "device final Release reaches zero with the route's objects released");
        const ULONG api_refs = f.factory.p->Release(); f.factory.p = nullptr;
        require(api_refs == 0, "factory final Release reaches zero");
        std::printf("RESULT PASS checks=%u restorations=%u frames=%llu motion_pixels=%u matched_pixels=%u max_uv_pixels=%.9g max_depth_error=%.9g depth_pixels=%u depth_written=%u max_current_depth_error=%.9g coverage_frames=%u coverage_pixels=%u coverage_ambiguous=%u jitter=%u taa=%u taa_frames=%u taa_history_frames=%u taa_reference_frames=%u taa_changed_pixels=%u taa_skipped_frames=%u\n",
                    checks, restorations, f.frame, motion_checked, motion_matched, max_uv_pixels, max_depth_error, depth_checked, depth_written, max_current_depth_error, coverage_frames, coverage_checked, coverage_ambiguous, f.jitter, f.taa, taa_frames, taa_history_frames, taa_reference_frames, taa_changed_pixels, taa_skipped_frames);
        exit_code = 0;
    } catch (const std::exception& e) { std::printf("RESULT FAIL %s\n", e.what()); }
    if (runtime) FreeLibrary(runtime);
    if (window) DestroyWindow(window);
    UnregisterClassA(cls.lpszClassName, cls.hInstance);
    return exit_code;
}
