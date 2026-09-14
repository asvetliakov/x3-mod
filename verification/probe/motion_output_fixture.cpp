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
// glow-off frames; with X3M_SCENE_HOOK=0 the same script runs unpatched, and
// with the switch unset (the default: on with the route) it patches as with 1.
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
#include "../../src/renderer/hdr_writeback_program.h"
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
// Pixel loops retain every assertion and the same first-failure witness, but
// avoid millions of successful printf calls. RESULT still reports the count.
void require_quiet(bool ok, const char* label) {
    if (!ok) require(false, label);
    else ++checks;
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
    D3DRS_MULTISAMPLEMASK, D3DRS_VERTEXBLEND, D3DRS_WRAP0, D3DRS_CLIPPING,
    D3DRS_WRAP1, D3DRS_WRAP2, D3DRS_WRAP3, D3DRS_WRAP4, D3DRS_WRAP5, D3DRS_WRAP6, D3DRS_WRAP7,
    D3DRS_WRAP8, D3DRS_WRAP9, D3DRS_WRAP10, D3DRS_WRAP11, D3DRS_WRAP12, D3DRS_WRAP13, D3DRS_WRAP14, D3DRS_WRAP15};
constexpr unsigned watched_count = sizeof(watched_states) / sizeof(watched_states[0]);
// Sampler states the resolve normalizes on s0-s6; compared on stages 0-7.
// MIPMAPLODBIAS is watched too: with the mip bias on (X3M_TAA_MIP_BIAS) the
// regular scripts bind no mip-mapped texture, so a bias left on a stage
// across any restore point would fail the state comparisons.
constexpr D3DSAMPLERSTATETYPE watched_samplers[] = {D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER, D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_SRGBTEXTURE, D3DSAMP_MAXMIPLEVEL, D3DSAMP_MIPMAPLODBIAS};
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
// `jittered`: the route jitters this draw's rows (scene draw with a table VS
// or a depth-only prepass program). `depth_only`: the draw writes depth and no
// colour (z_only prepass, zonly script): the coverage oracle keeps the colour
// class of the earlier draws and only advances the depth test.
// A record with a null object marks an application depth-only Clear between
// draws (burst script): the oracles restart the depth test there.
struct DrawRecord { Object* object; float t, p, zo; bool routed, matched; float pt, pp, pzo; bool flat = false; bool jittered = false; bool keyed = false; bool depth_only = false; };
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
// IEEE binary32 -> binary16, round to nearest even (the DLL's FP16 readback
// values are exact halves, so the reference's upload reproduces them exactly).
unsigned short float_to_half(float f) {
    unsigned bits; std::memcpy(&bits, &f, 4);
    const unsigned sign = (bits >> 16) & 0x8000; const int exponent = int((bits >> 23) & 255) - 127; unsigned mantissa = bits & 0x7fffff;
    if (exponent == 128) return static_cast<unsigned short>(sign | 0x7c00 | (mantissa ? 0x200 | (mantissa >> 13) : 0));
    if (exponent > 15) return static_cast<unsigned short>(sign | 0x7c00);
    if (exponent >= -14) {
        unsigned half = sign | (unsigned(exponent + 15) << 10) | (mantissa >> 13); const unsigned rest = mantissa & 0x1fff;
        if (rest > 0x1000 || (rest == 0x1000 && (half & 1))) ++half;
        return static_cast<unsigned short>(half);
    }
    if (exponent < -25) return static_cast<unsigned short>(sign);
    mantissa |= 0x800000; const int shift = -exponent - 1;
    unsigned half = mantissa >> shift; const unsigned rest = mantissa & ((1u << shift) - 1), mid = 1u << (shift - 1);
    if (rest > mid || (rest == mid && (half & 1))) ++half;
    return static_cast<unsigned short>(sign | half);
}
struct Reference {
    HMODULE module = nullptr; HWND window = nullptr;
    Com<IDirect3D9> factory; Com<IDirect3DDevice9> d;
    x3m::renderer::TemporalPass pass;
    Com<IDirect3DSurface9> color, output8, sys8, sys16;   // lockable A8R8G8B8 RT inputs/outputs and readback surfaces
    Com<IDirect3DTexture9> reactive16;                     // Optional supplemental-emission reference input.
    Com<IDirect3DTexture9> color16;                        // MANAGED A16B16G16R16F: the HDR route's FP16 scene, sampled directly (stage 3)
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
        api(d->CreateTexture(W, H, 1, 0, D3DFMT_A16B16G16R16F, D3DPOOL_MANAGED, &color16.p, nullptr), "reference color16");
        api(d->CreateTexture(W, H, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED, &motion.p, nullptr), "reference motion");
        api(d->CreateTexture(W, H, 1, 0, D3DFMT_R32F, D3DPOOL_DEFAULT, &depth.p, nullptr), "reference depth");
        api(d->CreateTexture(W, H, 1, 0, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &depth_staging.p, nullptr), "reference depth staging");
        // The reference resolves with the copy mode the DLL was made to take:
        // X3M_FIXTURE_STRETCH_FAULT=1 fails the DLL's round-trip self test
        // (taa_copy=draw), so the reference copies by draw too (the identity
        // program is the production write-back's).
        char setting[8]{}; copy_by_draw = GetEnvironmentVariableA("X3M_FIXTURE_STRETCH_FAULT", setting, sizeof setting) == 1 && setting[0] == '1';
        api(pass.initialize(d.p, nullptr, reinterpret_cast<const DWORD*>(x3m::renderer::temporal_resolve_program()), nullptr, nullptr,
                            reinterpret_cast<const DWORD*>(x3m::renderer::hdr_writeback_program())), "reference initialize");
        pass.configure_copy(copy_by_draw);
    }
    bool copy_by_draw = false;
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
    // The FP16 scene (4 floats per pixel, row-major) for the HDR route's reference.
    void upload16(const std::vector<float>& rgba) {
        D3DLOCKED_RECT lock{}; api(color16->LockRect(0, &lock, nullptr, 0), "lock reference color16");
        for (UINT y = 0; y < H; ++y) {
            auto* row = reinterpret_cast<unsigned short*>(static_cast<char*>(lock.pBits) + y * lock.Pitch);
            for (UINT i = 0; i < W * 4; ++i) row[i] = float_to_half(rgba[std::size_t(y) * W * 4 + i]);
        }
        api(color16->UnlockRect(0), "unlock reference color16");
    }
    void upload_reactive(const std::vector<float>& rgba) {
        require(rgba.size()==std::size_t(W)*H*4,"reference supplemental input dimensions");
        if(!reactive16.p)api(d->CreateTexture(W,H,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&reactive16.p,nullptr),"reference supplemental texture");
        D3DLOCKED_RECT lock{};api(reactive16->LockRect(0,&lock,nullptr,0),"reference supplemental lock");
        for(UINT y=0;y<H;++y){auto* row=reinterpret_cast<unsigned short*>(static_cast<char*>(lock.pBits)+y*lock.Pitch);
            for(UINT x=0;x<W*4;++x)row[x]=float_to_half(rgba[std::size_t(y)*W*4+x]);}
        api(reactive16->UnlockRect(0),"reference supplemental unlock");
    }
    // Runs one frame exactly as the route does and returns the copied-back
    // 8-bit image; `half` receives the FP16 output bytes. `hdr`: the FP16
    // scene (upload16) is the colour input with the weighting constant `k`.
    x3m::renderer::Output run(double jx, double jy, double pjx, double pjy, bool cut, const float* clip_to_previous, bool sentinel_camera,
                              bool hdr, float k, std::vector<DWORD>& image8, std::vector<unsigned char>& half,
                              x3m::renderer::ReactivePolicy reactive_policy=x3m::renderer::ReactivePolicy::DerivedFromDepthSentinel) {
        x3m::renderer::FrameInputs in{};
        if (hdr) { in.color = color16.p; in.luminance_k = k; } else in.color_surface = color.p;
        in.current_depth = depth.p; in.motion = motion.p;
        in.width = W; in.height = H; in.epoch = epoch;
        std::memcpy(in.clip_to_previous, clip_to_previous, 16 * sizeof(float));
        in.sentinel_camera = sentinel_camera;
        in.current_jitter[0] = float(jx); in.current_jitter[1] = float(jy); in.previous_jitter[0] = float(pjx); in.previous_jitter[1] = float(pjy);
        in.motion_policy = x3m::renderer::MotionPolicy::PerPixel; in.reactive_policy = reactive_policy;
        if(reactive_policy==x3m::renderer::ReactivePolicy::SupplementalMaskWithDepthSentinel)in.reactive=reactive16.p;
        in.history_allowed = true; in.cut = cut; in.caller_scene_open = false; in.caller_queries_idle = true;
        x3m::renderer::Output out{};
        api(pass.run(in, &out), "reference run");
        // Draw mode: the pass wrote the display into its 8-bit input (color);
        // stretch mode: the route's copy-back StretchRect into output8.
        if (out.display_written) api(d->GetRenderTargetData(color.p, sys8.p), "reference readback 8 (draw mode)");
        else {
            api(d->StretchRect(out.color_surface, nullptr, output8.p, nullptr, D3DTEXF_POINT), "reference copy-back");
            api(d->GetRenderTargetData(output8.p, sys8.p), "reference readback 8");
        }
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
        color.reset(); color16.reset(); reactive16.reset(); output8.reset(); sys8.reset(); sys16.reset(); motion.reset(); depth.reset(); depth_staging.reset();
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
    float sharpen = 0.f;   // X3M_TAA_SHARPEN: the presented image is RCAS of the resolved one (the runner compares it against the Python reference)
    bool hook = false, wrap = false, state_shadow = true, hdr = false, hdrvalues = false, hdrfault = false;
    // aohook script (ambient occlusion at the scene-end hook): the DLL's switches
    // as the fixture reads them (X3M_AMBIENT_OCCLUSION, X3M_FIXTURE_AO_FAULT=attach,
    // X3M_AO_DEBUG, X3M_AO_STRENGTH) decide the pixel law of the crease frames.
    bool aohook = false, ao_env = false, ao_fault = false, ao_debug = false, ao_toggle_script = false;
    float ao_strength = .5f;
    int (*ao_toggle)(IDirect3DDevice9*) = nullptr; // x3m_ambient_occlusion_fixture_toggle: the Ctrl+Shift+F11 action
    bool hdrramp = false, hdrexposure = false, hdrtonemapfault = false; // stage-2 scripts
    bool emissions = false, emission_bench = false, emissions_enabled = false, emission_mask_valid = false;
    unsigned reactive_uploads = 0; // reference reactive-mask uploads (supplemental policy frames)
    // New fade mode reuses the supplemental scene/reference transport; its
    // effective producer set is latched from the runtime status each frame.
    bool distancefade = false, distancefade_bench = false;
    bool cutout = false, cutout_bench = false;
    bool faderoute = false; // fade-band motion arm script (motion_output_fade_route_inc.h, X3M_FIXTURE_FADE_SCRIPT)
    bool distancefade_enabled = false, distancefade_emissions_enabled = false;
    // Packed screen emission script (screen-emission-region.md step C): the
    // fade/emission transport plus the bullet pair; X3M_SCREEN_EMISSION as read.
    bool screenemission = false, screenemission_bench = false, screen_enabled = false;
    std::vector<float> emission_reference_color, emission_reference_mask;
    unsigned (*emission_status)(IDirect3DDevice9*, unsigned) = nullptr;
    void (*emission_fault)(IDirect3DDevice9*, unsigned, unsigned) = nullptr;
    HRESULT (*emission_readback)(IDirect3DDevice9*, unsigned, float*, unsigned, unsigned*, unsigned*) = nullptr;
    HRESULT (*wrap_snapshot)(IDirect3DDevice9*, x3m::MotionOutputFixtureWrapSnapshot*) = nullptr;
    bool materialwrap = false, materialwrap_depth = true, materialxt = false, materialglass = false;
    bool linearmaterials = false; // Focused live combined-route/Reset/refcount script.
    bool hdr_agx = false;           // X3M_HDR_TONEMAP=agx: the presented image is AgX (the runner holds the reference)
    // Mip LOD bias script ("mipbias" mode) and the DLL's X3M_TAA_MIP_BIAS as
    // the fixture read it (0: the DLL biases nothing, every stage must read 0).
    bool mipbias = false; float mip_bias = 0;
    bool zonly = false;                    // depth-only prepass script (run_zonly)
    Com<IDirect3DVertexShader9> zonly_vs;  // the engine's z_only vs_1_1 program (clip rows in c0-3)
    bool msaa = false; unsigned msaa_samples = 0; // "msaa" script: a multisampled main target the route must refuse (D3)
    unsigned capture_start = 1, capture_frames = 8; // X3M_CAPTURE_START/FRAMES: capture frames restore before every draw's diagnostics
    Com<IDirect3DTexture9> ramp;    // 1024x1024 full mip chain, level i a constant grey 16 + 20 i (a LOD ramp)
    bool history_dropped = false;   // the route skipped or failed a resolve: its history is gone until the next resolve
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
        if (taa || cutout) { // the cutout script publishes through the bloom copy without TAA too
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
        // The LOD ramp: a full 1024 -> 1 chain (eleven levels), every level a
        // constant grey that grows with the level, so a trilinear sample is
        // linear in the LOD the hardware computes and a finer level is darker.
        // Object A maps one texture repeat over 128 raster pixels (4 NDC units
        // at 32 px each), so 1024 texels give 8 texels per pixel: LOD 3, where
        // a negative bias moves the sample (at 128 texels it would be LOD 0,
        // where the bias is clamped away and changes nothing).
        api(d->CreateTexture(1024, 1024, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &ramp.p, nullptr), "CreateTexture ramp");
        if (ramp->GetLevelCount() != 11) throw std::runtime_error("the ramp texture does not hold the complete mip chain"); // not a counted check: every script creates it
        for (UINT level = 0; level < 11; ++level) {
            D3DLOCKED_RECT lock{}; api(ramp->LockRect(level, &lock, nullptr, 0), "LockRect ramp");
            const DWORD grey = 16 + 20 * level, texel = 0xff000000u | (grey << 16) | (grey << 8) | grey;
            const UINT size = 1024 >> level;
            for (UINT y = 0; y < size; ++y) for (UINT x = 0; x < size; ++x) std::memcpy(static_cast<char*>(lock.pBits) + y * lock.Pitch + x * 4, &texel, 4);
            api(ramp->UnlockRect(level), "UnlockRect ramp");
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
        config.emission_scene_owner = emissions;
        config.force_taa_readback = emissions && !emission_bench;
        config.observe_native_wrap = materialwrap || materialxt || materialglass;
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
            // In the focused material script, initial attach and first Reset
            // admission must rely on resync getters, not setter repopulation.
            if (!(linearmaterials && (frame == 0 || frame == 10 || frame == 21)) &&
                !(materialglass && frame == 23))
                api(d->SetSamplerState(i, D3DSAMP_SRGBTEXTURE, FALSE), "SetSamplerState srgb");
        }
    }
    // Warm the application side of the render-state shadow for the WRAP states
    // the route reads once per routed draw (its motion and depth texcoord
    // indices, WRAP4/WRAP5 for the reference pair). With X3M_FIXTURE_WRAP=0 no
    // other fixture setter writes them, so the route's first read of each is a
    // cold shadow miss with no resynchronization behind it: frame 0 reported
    // rs_hits = rs_queries - 2 once the per-draw wrap save became unconditional.
    // The written value is the D3D9 default the device already holds, so only
    // the shadow changes. With X3M_FIXTURE_WRAP=1 scene_states owns these
    // states (hostile values) and this seed stays out of the way.
    void seed_wrap_states() {
        if (wrap) return;
        for (unsigned i = 0; i < 16; ++i)
            api(d->SetRenderState(D3DRENDERSTATETYPE(i < 8 ? D3DRS_WRAP0 + i : D3DRS_WRAP8 + i - 8), 0), "seed WRAP");
    }
    void scene_states() {
        for (auto s : {D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_FOGENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_STENCILENABLE, D3DRS_DITHERENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_LIGHTING})
            api(d->SetRenderState(s, FALSE), "SetRenderState off");
        api(d->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID), "fill"); api(d->SetRenderState(D3DRS_COLORWRITEENABLE, 15), "write");
        api(d->SetRenderState(D3DRS_COLORWRITEENABLE1, 15), "write1"); api(d->SetRenderState(D3DRS_COLORWRITEENABLE2, 15), "write2"); api(d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE), "cull");
        api(d->SetRenderState(D3DRS_ZENABLE, TRUE), "z"); api(d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE), "zwrite");
        api(d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL), "zfunc");
        if (wrap) {
            // The reference Argon pair generates motion TEX4 and depth TEX5.
            // Its original TEX0 keeps an independent application WRAP value.
            api(d->SetRenderState(D3DRS_WRAP0, D3DWRAPCOORD_0), "native WRAP0");
            api(d->SetRenderState(D3DRS_WRAP4, 15), "generated motion WRAP4 hostile");
            api(d->SetRenderState(D3DRS_WRAP5, 15), "generated depth WRAP5 hostile");
        }
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
        seed_wrap_states();
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
        if(distancefade) {
            // New composition witness exercises camera rotation + translation;
            // every older fixture retains its exact camera sequence.
            fake_view[12] += .125f*float(f%5);
            fake_view[13] -= .0625f*float(f%3);
            x3m::renderer::camera_state_from_matrices(fake_projection,fake_view,camera_current);
            if(!distancefade_bench)std::printf("FADE_CAMERA frame=%llu view_translation=%.9g,%.9g,%.9g\n",f,double(fake_view[12]),double(fake_view[13]),double(fake_view[14]));
        }
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
    void draw(Object& o, float t, float p, float zo, bool known, bool routed, bool matched, Alter alter = Alter::None, bool verify = true, UINT stride = 24) {
        scope(known ? &o : nullptr);
        api(d->SetStreamSource(0, o.vb, 0, stride), "SetStreamSource object");
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
                depth_value = z; if (!r.depth_only) kind = r.flat ? 2 : 1;
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
        if(!(materialwrap||materialxt||materialglass) || materialwrap_depth){
            api(readback_depth(d.p, depth_data.data(), unsigned(depth_data.size()), &w, &h), "fixture depth readback");
            require(w == W && h == H, "depth target matches the main dimensions");
        }else require(readback_depth(d.p,depth_data.data(),unsigned(depth_data.size()),&w,&h)==D3DERR_NOTFOUND,"motion-only has no depth target");
        std::printf("MOTION_HASH frame=%llu motion=%016llx depth=%016llx\n", frame,
                    static_cast<unsigned long long>(fnv(data.data(), data.size() * 4)), static_cast<unsigned long long>((materialwrap||materialxt||materialglass)&&!materialwrap_depth?0:fnv(depth_data.data(), depth_data.size() * 4)));
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
            if((materialwrap||materialxt||materialglass) && !materialwrap_depth)continue;
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
        // Reading logical main before terminal publication is a real export;
        // emission mode snapshots its owned FP16 input through the native seam.
        const auto before_image = emissions ? std::vector<DWORD>(std::size_t(W)*H) : color_image();
        Snapshot before = snapshot();
        api(d->StretchRect(back.p, nullptr, bloom_surface.p, nullptr, D3DTEXF_NONE), "StretchRect bloom copy");
        const Snapshot after_copy = snapshot();
        if (bias_live()) {
            // A routed draw may still hold the route's bias here (the mip-bias
            // script's model): the copy is a restore point, so afterwards every
            // stage holds the application's own value again.
            expect_bias("boundary copy restores the application's bias", 0);
            for (unsigned s = 0; s < sampler_stages; ++s)
                if (before.samplers[s][sampler_count - 1] == float_bits(mip_bias)) before.samplers[s][sampler_count - 1] = after_copy.samplers[s][sampler_count - 1];
        }
        compare(before, after_copy, "boundary");
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
            reference.pass.invalidate(); // the route dropped its history with the skip (invalidate_taa)
            ++taa_skipped_frames;
        } else if (live) {
            // Reference resolve from the DLL's own inputs: RT1/RT2 read back
            // through the seam, the 8-bit main target read back before the copy.
            std::vector<float> motion_data(std::size_t(W) * H * 4), depth_data(std::size_t(W) * H); unsigned w = 0, h = 0;
            api(readback(d.p, motion_data.data(), unsigned(motion_data.size()), &w, &h), "reference motion readback");
            api(readback_depth(d.p, depth_data.data(), unsigned(depth_data.size()), &w, &h), "reference depth readback");
            reference.upload(before_image, motion_data, depth_data);
            const float k = hdr_reference_input();
            std::vector<DWORD> expected; std::vector<unsigned char> half;
            const auto reactive = emissions&&emissions_enabled ? (emission_mask_valid?x3m::renderer::ReactivePolicy::SupplementalMaskWithDepthSentinel:x3m::renderer::ReactivePolicy::Unavailable) : x3m::renderer::ReactivePolicy::DerivedFromDepthSentinel;
            if(emissions&&emissions_enabled&&emission_mask_valid){reference.upload_reactive(emission_reference_mask);++reactive_uploads;}
            const auto out = reference.run(jx, jy, pjx, pjy, expected_cut(), decision.matrix, decision.policy == 2, hdr, k, expected, half,reactive);
            history = out.used_history;
            const unsigned mismatches = presented_mismatches(after_image, expected, k);
            require(!mismatches, "main target after the copy equals the reference resolve of the same inputs (byte for byte; HDR: within one code of the reference conversion)");
            char name[64]; std::snprintf(name, sizeof name, "reference_taa_%llu.rgba16f", frame);
            FILE* file = std::fopen(name, "wb"); require(file != nullptr, "reference FP16 output written");
            std::fwrite(half.data(), 1, half.size(), file); std::fclose(file);
            ++taa_reference_frames;
        } else {
            // Production DLL: every routed pixel is sentinel (no history
            // correspondence), so the resolve is current-only everywhere.
            history = false;
        }
        const bool expect_history = live && resolve_expected && frames_since_reset > 0 && !expected_cut() && !history_dropped && !(emissions&&emissions_enabled&&!emission_mask_valid);
        require(history == expect_history, "history use follows the script (first frame, Reset, cut and post-skip frames run current-only)");
        history_dropped = !resolve_expected || (emissions&&emissions_enabled&&!emission_mask_valid);
        if (!emissions && !history && sharpen <= 0.f) require(!changed, "a frame without history leaves the 8-bit main target bit-identical through the FP16 round trip");
        std::printf("TAA frame=%llu history=%u cut=%u changed=%u policy=%u skipped=%u\n", frame, history, expected_cut(), changed, decision.policy, !resolve_expected);
        ++taa_frames; taa_history_frames += history; taa_changed_pixels += changed;
        // The history now holds this frame's scene view (the route records it
        // after a successful resolve); a frame that did not resolve drops the
        // history and its view (the route's invalidate_taa).
        camera_history = resolve_expected ? camera_current : x3m::renderer::CameraState{};
        resolve_expected = true;
        std::printf("COLOR_BEFORE frame=%llu hash=%016llx\n", frame, static_cast<unsigned long long>(color_hash(before_image)));
        if (!hdr_agx) verify_coverage(before_image); // the oracle reads raster colours; an AgX write-back presents tonemapped ones
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
        if (!taa && !skip_coverage && !hdr_agx) verify_coverage(image);
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
            if (wrap) api(d->SetRenderState(D3DRS_WRAP4, 6), "application WRAP4 between lazy draws");
            burst_draw(b, -.05f, 0, .1f);
            DWORD mask = 0; api(d->GetRenderState(D3DRS_COLORWRITEENABLE1, &mask), "application COLORWRITEENABLE1 read between routed draws");
            require(mask == 7, "the application reads back its own COLORWRITEENABLE1 between routed draws");
            api(d->SetRenderState(D3DRS_COLORWRITEENABLE1, 15), "application COLORWRITEENABLE1 restore");
            if (wrap) {
                DWORD value = 0; api(d->GetRenderState(D3DRS_WRAP4, &value), "application WRAP4 read");
                require(value == 6, "application reads exact WRAP4 after routed draw");
                api(d->SetRenderState(D3DRS_WRAP4, 15), "application WRAP4 restore");
            }
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
    // ---- mip LOD bias script ("mipbias" mode; X3M_TAA_MIP_BIAS, jitter on) ----
    //
    // Stages after bind_mip_stages: 0 ramp/LINEAR mip (eligible), 1 ramp/NONE
    // (mip chain, filter off: untouched), 2 unmipped/LINEAR (untouched),
    // 3 unmipped cube/NONE, 4 ramp/POINT (eligible), 5 unbound/LINEAR
    // (untouched). The bias is observed through GetSamplerState, which the DLL
    // does not hook (the game never reads sampler state back). The script's
    // own model of the DLL's restore points (`model_*`) predicts the per-frame
    // SetSamplerState counts the runner compares against the trace.
    static DWORD float_bits(float value) { DWORD bits = 0; std::memcpy(&bits, &value, 4); return bits; }
    bool bias_live() const { return enabled && mip_bias != 0; }
    bool capturing() const { return frame >= capture_start && frame < capture_start + capture_frames; }
    unsigned model_sets = 0, model_restores = 0, model_biased = 0, model_draws = 0; // per frame
    unsigned model_reads = 0;                                                       // per frame: native GetSamplerState fills
    bool model_saved_known[8]{};                                                    // the DLL knows the value to restore
    void model_restore() { if (model_biased) { model_restores += __builtin_popcount(model_biased); model_biased = 0; } }
    void model_routed(unsigned eligible) {
        if (!bias_live()) return;
        if (capturing()) model_restore(); // capture frames: the draw diagnostics restore first
        const unsigned drop = model_biased & ~eligible, add = eligible & ~model_biased;
        model_restores += __builtin_popcount(drop);
        for (unsigned s = 0; s < 8; ++s) if ((add >> s) & 1 && !model_saved_known[s]) { ++model_reads; model_saved_known[s] = true; }
        model_sets += __builtin_popcount(add);
        model_biased = eligible;
        if (model_biased) ++model_draws;
    }
    void model_unrouted() { if (bias_live()) model_restore(); }
    void model_game_write(unsigned stage) { if (model_biased & (1u << stage)) ++model_restores; model_biased &= ~(1u << stage); model_saved_known[stage] = true; }
    DWORD sampler_bias(UINT stage) { DWORD value = ~0ul; api(d->GetSamplerState(stage, D3DSAMP_MIPMAPLODBIAS, &value), "GetSamplerState MIPMAPLODBIAS"); return value; }
    // Stages 0-7 holding exactly the DLL's bias (a bit mask) against the script's expectation.
    void expect_bias(const char* label, unsigned expected) {
        unsigned actual = 0, nonzero = 0;
        for (UINT s = 0; s < 8; ++s) { const DWORD v = sampler_bias(s); if (v == float_bits(mip_bias) && mip_bias != 0) actual |= 1u << s; if (v) nonzero |= 1u << s; }
        const unsigned want = bias_live() ? expected : 0;
        std::printf("MIPBIAS frame=%llu label=%s expected=%02x actual=%02x nonzero=%02x ok=%u\n", frame, label, want, actual, nonzero, actual == want);
        require(actual == want, "the DLL's mip bias sits on exactly the expected stages");
    }
    void bind_mip_stages() {
        api(d->SetTexture(0, ramp.p), "SetTexture ramp 0"); api(d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR), "mip 0 linear");
        api(d->SetTexture(1, ramp.p), "SetTexture ramp 1"); api(d->SetSamplerState(1, D3DSAMP_MIPFILTER, D3DTEXF_NONE), "mip 1 none");
        api(d->SetSamplerState(2, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR), "mip 2 linear (unmipped texture)");
        api(d->SetTexture(4, ramp.p), "SetTexture ramp 4"); api(d->SetSamplerState(4, D3DSAMP_MIPFILTER, D3DTEXF_POINT), "mip 4 point");
        api(d->SetTexture(5, nullptr), "SetTexture 5 null"); api(d->SetSamplerState(5, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR), "mip 5 linear (unbound)");
    }
    void routed_draw(Object& o, float t, float p, float zo, unsigned eligible) { draw(o, t, p, zo, false, true, false, Alter::None, false); model_routed(eligible); }
    void unrouted_draw(Object& o, float t, float p, float zo, Alter alter = Alter::FlatPixel) { draw(o, t, p, zo, false, false, false, alter, false); model_unrouted(); }
    // Mean of the RGB average over A's interior pixels at the jittered sample positions.
    void object_stats(const std::vector<DWORD>& image, float t, float p, unsigned& pixels, double& mean, const std::vector<DWORD>* other, unsigned& differing) {
        pixels = 0; differing = 0; double sum = 0;
        for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
            double ox, oy, wc; object_point(x - jx, y - jy, t, p, ox, oy, wc);
            if (!a.covers(ox, oy) || edge_distance(a, ox, oy) < 2e-2) continue;
            const DWORD v = image[std::size_t(y) * W + x];
            sum += (((v >> 16) & 255) + ((v >> 8) & 255) + (v & 255)) / 3.;
            ++pixels;
            if (other && (*other)[std::size_t(y) * W + x] != v) ++differing;
        }
        mean = pixels ? sum / pixels : 0;
    }
    void run_mipbias(unsigned frames) {
        const DWORD app_bias = float_bits(.25f); // the "game's own" MIPMAPLODBIAS write
        for (unsigned i = 0; i < frames; ++i) {
            frame_begin();
            model_sets = model_restores = model_draws = model_reads = 0; model_biased = 0;
            bind_mip_stages();
            expect_bias("before_routed", 0);
            routed_draw(a, .75f, 0, 0, 0x11); expect_bias("routed", 0x11);
            routed_draw(b, 0, 0, 0, 0x11); expect_bias("routed_again", 0x11);
            unrouted_draw(a, .75f, 0, 0); expect_bias("unrouted", 0);
            routed_draw(a, .75f, 0, 0, 0x11); expect_bias("rerouted", 0x11);
            // Stage 4 rebound to an unmipped texture: the bias comes off that stage.
            api(d->SetTexture(4, textures[1].p), "SetTexture unmipped 4");
            routed_draw(b, 0, 0, 0, 0x01); expect_bias("stage4_unmipped", 0x01);
            // Stage 0's mip filter off: nothing left to bias.
            api(d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE), "mip 0 none");
            routed_draw(a, .75f, 0, 0, 0x00); expect_bias("stage0_mipfilter_none", 0x00);
            api(d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR), "mip 0 linear again");
            api(d->SetTexture(4, ramp.p), "SetTexture ramp 4 again");
            routed_draw(b, 0, 0, 0, 0x11); expect_bias("eligible_again", 0x11);
            // An application write of the bias while the DLL's is on: the
            // application's value stands, is re-biased by the next routed draw
            // and is what the following restore puts back.
            api(d->SetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, app_bias), "application MIPMAPLODBIAS write"); model_game_write(0);
            require(sampler_bias(0) == app_bias, "an application write of the bias reaches the device");
            expect_bias("after_game_write", 0x10);
            routed_draw(a, .75f, 0, 0, 0x11); expect_bias("reapplied_after_game_write", 0x11);
            unrouted_draw(a, .75f, 0, 0);
            require(sampler_bias(0) == app_bias && sampler_bias(4) == 0, "the restore puts the application's own value back");
            expect_bias("restored_to_game_value", 0);
            api(d->SetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, 0), "application MIPMAPLODBIAS write back to 0"); model_game_write(0);
            routed_draw(b, 0, 0, 0, 0x11); expect_bias("routed_after_second_write", 0x11);
            if (i % 2 == 0) {
                // Evidence: the same material draw of A, routed (biased) and
                // unrouted (gate 4: blending on, ONE/ZERO so the colour is the
                // unblended material), both at this frame's jitter. Off, the
                // two images are identical; on, the routed one samples finer
                // (darker) levels of the ramp.
                routed_draw(a, .75f, 0, 0, 0x11);
                const auto routed_image = color_image();
                if (lazy) model_unrouted(); // lazy mode hooks GetRenderTargetData: a restore point
                unrouted_draw(a, .75f, 0, 0, Alter::Blend);
                const auto unrouted_image = color_image();
                unsigned pixels = 0, differing = 0, unused_count = 0, unused = 0; double routed_mean = 0, unrouted_mean = 0;
                object_stats(routed_image, .75f, 0, pixels, routed_mean, &unrouted_image, differing);
                object_stats(unrouted_image, .75f, 0, unused_count, unrouted_mean, nullptr, unused);
                std::printf("MIPBIAS_EVIDENCE frame=%llu bias=%g pixels=%u differing=%u routed_mean=%.4f unrouted_mean=%.4f delta=%.4f\n",
                            frame, mip_bias, pixels, differing, routed_mean, unrouted_mean, unrouted_mean - routed_mean);
                require(pixels > 500, "A covers enough interior pixels for the evidence");
                if (bias_live()) require(differing > pixels / 2 && unrouted_mean - routed_mean > 1., "the biased routed draw samples finer, darker ramp levels");
                else require(differing == 0, "without the bias the routed and unrouted material draws are identical");
            }
            routed_draw(a, .75f, 0, 0, 0x11); expect_bias("last_routed", 0x11);
            std::printf("MIPBIAS_EXPECT frame=%llu sets=%u restores=%u draws=%u reads=%u capture=%u\n", frame, model_sets,
                        model_restores + (bias_live() ? __builtin_popcount(model_biased) : 0), model_draws, model_reads, capturing()); // EndScene restores the rest
            frame_end();
            expect_bias("after_present", 0);
            if (i == 3) { reset(); for (auto& known : model_saved_known) known = false; } // Reset: the shadow re-reads
        }
        require(!(enabled && seam) || frames_verified == frames, "every live mipbias frame verified against the oracle");
    }
    // ---- depth-only prepass script ("zonly" mode; jitter on) ----
    //
    // The engine's fogged-asteroid sequence (asteroid-fog-temporal.md, run 47):
    // a depth-only prepass with the z_only vs_1_1 program c78b4c68a87fce74
    // (null PS, ZWRITEENABLE on, COLORWRITEENABLE 0, LESSEQUAL, clip rows in
    // c0-3), then the blended material draw of the same geometry (ZWRITEENABLE
    // off, LESSEQUAL: gate 4 refuses it, the route still jitters it). Rows
    // with perspective p = .125 give the triangle a depth slope along x
    // (z = .5 / (1 + p ox)), so an x offset between the two draws changes the
    // depth compared under LESSEQUAL by ~2e-3 per pixel, far above D24
    // precision. Regular frames: the coverage oracle must see the material on
    // every interior pixel (a dropped facet shows the background) and the
    // fixture counts interior background pixels (holes) itself. Control
    // frames (jx > 0 in the Halton sequence): the prepass rows are pre-shifted
    // by the negative jitter so the route's own jitter cancels and the prepass
    // lands unjittered, as the game's did before the fix; the jittered
    // material fragment at p then carries the content of p - j, closer to
    // the left, hence farther (larger z) on this slope, and fails LESSEQUAL
    // over the whole interior: holes must appear, proving the oracle sees the
    // failure mode. Reset after frame 3: the prepass identity must survive the
    // shadow resynchronization.
    void prepass_draw(Object& o, float t, float p, float zo, bool counter_jitter) {
        scope(nullptr);
        api(d->SetStreamSource(0, o.vb, 0, 24), "SetStreamSource prepass");
        api(d->SetVertexShader(zonly_vs.p), "SetVertexShader z_only"); api(d->SetPixelShader(nullptr), "SetPixelShader null");
        api(d->SetRenderState(D3DRS_COLORWRITEENABLE, 0), "prepass colour mask off");
        float m[16]; std::memcpy(m, identity, sizeof m); m[3] = t; m[11] = zo; m[12] = p;
        if (counter_jitter) { // rows[0] -= jx_ndc rows[3], rows[1] -= jy_ndc rows[3]: the route adds them back (fade_region::jitter_rows)
            const float jx_ndc = float(2 * jx / W), jy_ndc = float(-2 * jy / H);
            for (unsigned i = 0; i < 4; ++i) { m[i] -= jx_ndc * m[12 + i]; m[4 + i] -= jy_ndc * m[12 + i]; }
        }
        api(d->SetVertexShaderConstantF(0, m, 4), "SetVertexShaderConstantF z_only rows");
        float rows_before[16], rows_after[16];
        api(d->GetVertexShaderConstantF(0, rows_before, 4), "GetVertexShaderConstantF c0-3 before");
        const Snapshot before = snapshot();
        api(d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1), "DrawPrimitive prepass"); ++draw_index;
        compare(before, snapshot(), "prepass");
        api(d->GetVertexShaderConstantF(0, rows_after, 4), "GetVertexShaderConstantF c0-3 after");
        require(std::memcmp(rows_before, rows_after, sizeof rows_before) == 0, "z_only clip rows c0-3 restored bit-exactly after the prepass");
        api(d->SetRenderState(D3DRS_COLORWRITEENABLE, 15), "prepass colour mask back");
        material_state(); // c0-23 hold the lights again for the material draw
        const bool jittered = enabled && jitter;
        records.push_back({&o, t, p, zo, false, false, o.rt, o.rp, o.rzo, false, jittered, false, true});
        std::printf("EXPECT frame=%llu index=%u object=%s routed=0 matched=0 jittered=%u prepass=1\n", frame, draw_index, o.name, jittered);
    }
    // Interior pixels of A at the jittered sample positions (2e-2 NDC inside
    // every edge) and how many of them show the frame's background colour.
    void interior_holes(const std::vector<DWORD>& image, float t, float p, unsigned& pixels, unsigned& holes) {
        pixels = holes = 0; bool have_background = false; DWORD background = 0;
        for (UINT y = 0; y < H && !have_background; ++y) for (UINT x = 0; x < W; ++x) {
            double ox, oy, wc; object_point(x - jx, y - jy, t, p, ox, oy, wc);
            if (!a.covers(ox, oy) && edge_distance(a, ox, oy) >= 2e-2) { background = image[std::size_t(y) * W + x]; have_background = true; break; }
        }
        require(have_background, "an uncovered pixel gives the frame's background colour");
        for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
            double ox, oy, wc; object_point(x - jx, y - jy, t, p, ox, oy, wc);
            if (!a.covers(ox, oy) || edge_distance(a, ox, oy) < 2e-2) continue;
            ++pixels; holes += image[std::size_t(y) * W + x] == background;
        }
    }
    void run_zonly(const char* vs_path, unsigned frames) {
        const std::string supplied(vs_path);
        const auto slash = supplied.find_last_of("/\\");
        require(slash != std::string::npos, "z_only program directory");
        const auto code = load((supplied.substr(0, slash + 1) + "vs_c78b4c68a87fce74.bin").c_str());
        require(code.size() == 89 && fnv(code.data(), code.size() * 4) == 0xc78b4c68a87fce74ull, "local z_only program is the reviewed alias");
        api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()), &zonly_vs.p), "CreateVertexShader z_only");
        for (unsigned i = 0; i < frames; ++i) {
            frame_begin();
            const bool control = jitter && (i == 2 || i == 4 || i == 6); // Halton indices 3, 5, 7: jx = .25, .125, .375
            require(!control || jx > 0, "control frames carry a positive x jitter");
            prepass_draw(a, .8f, .125f, 0, control);
            api(d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE), "material z-write off");
            draw(a, .8f, .125f, 0, false, false, false, Alter::Blend);
            api(d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE), "material z-write back");
            if (control) skip_coverage = true; // the oracle expects the material everywhere; the control drops it on purpose
            const auto image = color_image();
            unsigned pixels = 0, holes = 0; interior_holes(image, .8f, .125f, pixels, holes);
            std::printf("ZONLY frame=%llu control=%u jx=%.6f jy=%.6f pixels=%u holes=%u\n", frame, control, jx, jy, pixels, holes);
            require(pixels > 500, "A covers enough interior pixels for the depth-parity evidence");
            if (control) require(holes > pixels / 2, "control: an unjittered prepass drops the interior of the jittered material draw");
            else require(holes == 0, "the jittered prepass depth and the jittered material draw agree: no dropped pixels");
            frame_end();
            if (i == 3) reset(); // the prepass identity survives the shadow resynchronization
        }
        require(!(enabled && seam) || frames_verified == frames, "every live zonly frame verified against the oracle");
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
            const float k = hdr_reference_input();
            std::vector<DWORD> expected; std::vector<unsigned char> half;
            const auto out = reference.run(jx, jy, pjx, pjy, expected_cut(), decision.matrix, decision.policy == 2, hdr, k, expected, half);
            history = out.used_history;
            const unsigned mismatches = presented_mismatches(after_image, expected, k);
            require(!mismatches, "main target after the hook/copy equals the reference resolve of the same inputs (byte for byte; HDR: within one code of the reference conversion)");
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
        if (!emissions && !history && sharpen <= 0.f) require(!changed, "a frame without history leaves the 8-bit main target bit-identical through the FP16 round trip");
        history_valid = resolves;
        camera_history = resolves ? camera_current : x3m::renderer::CameraState{};
        std::printf("TAA frame=%llu history=%u cut=%u changed=%u policy=%u skipped=%u source=%s glow=%u outside=%u\n", frame, history, expected_cut(), changed, decision.policy, !resolves,
                    at_hook ? "hook" : glow ? "stretchrect" : "none", glow, outside);
        ++taa_frames; taa_history_frames += history; taa_changed_pixels += changed;
        std::printf("COLOR_BEFORE frame=%llu hash=%016llx\n", frame, static_cast<unsigned long long>(color_hash(before_image)));
        if (!hdr_agx) verify_coverage(before_image); // the oracle reads raster colours; an AgX write-back presents tonemapped ones
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
        // The DLL's rule (scene_hook::wanted): "0" off, "1" on, unset on with the route (this script runs with X3M_MOTION_OUTPUT=1).
        char setting[8]{}; const bool want = !(GetEnvironmentVariableA("X3M_SCENE_HOOK", setting, sizeof setting) == 1 && setting[0] == '0');
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
    // ---- ambient occlusion at the scene-end hook (aohook) ---------------------
    // One crease frame: object A drawn twice through the route with opposite
    // perspective terms p = +-1.5 (vertex z 0.5, zo 0), so the device depth is
    // d = 0.5 / (1 +- 1.5 ox) and the Z test keeps the nearer plane on each side
    // of ox = 0: the visible surface is a concave crease (the crossing is the
    // farthest line, both planes come towards the viewer away from it, slope
    // about 1.2 in the reconstructed view), the columns beyond |ndc x| > 2/3
    // stay sentinel. The hook runs the AO chain before the resolve. On a frame
    // without history the presented image is the AO-multiplied scene (the FP16
    // round trip is bit-identical), so the pixel law is checked against the
    // 8-bit main target read before the hook: sentinel pixels unchanged, every
    // channel in [floor(b * (1 - s)^(1/2.2)) - 1, b], darkening present and
    // concentrated in the centre band; the debug view writes the grayscale
    // factor instead (sentinel 255, gray, above the floor); with AO off or the
    // attach refused the frame is bit-identical.
    void ao_crease_frame(bool pixel_check) {
        frame_begin(nullptr);
        const bool matched = frames_since_reset > 0;
        draw(a, 0, 1.5f, 0, true, true, matched); draw(a, 0, -1.5f, 0, true, true, false);
        decide();
        const auto before_image = color_image();
        std::vector<float> depth_data(std::size_t(W) * H); unsigned w = 0, h = 0;
        api(readback_depth(d.p, depth_data.data(), unsigned(depth_data.size()), &w, &h), "reference depth readback");
        const Snapshot before = snapshot();
        stub_call(true);
        Snapshot after = snapshot();
        require(after.depth == nullptr, "the compositor unbound the depth surface"); after.depth = before.depth;
        compare(before, after, "hook");
        const auto after_image = color_image();
        require(color_image(bloom_surface.p) == after_image, "the bloom copy receives the main target as resolved");
        unsigned changed = 0;
        for (std::size_t i = 0; i < after_image.size(); ++i) changed += after_image[i] != before_image[i];
        {   // Depth witness of the crease (RT2 as the route wrote it) and the raw images for offline analysis.
            float dmin = 2.f, dmax = -2.f; double dsum = 0; unsigned covered = 0, crease_column = 0;
            for (std::size_t i = 0; i < depth_data.size(); ++i) if (depth_data[i] >= 0.f) { ++covered; dsum += depth_data[i]; if (depth_data[i] < dmin) dmin = depth_data[i]; if (depth_data[i] > dmax) { dmax = depth_data[i]; crease_column = unsigned(i % W); } }
            std::printf("AO_DEPTH frame=%llu covered=%u min=%.6f max=%.6f mean=%.6f max_column=%u\n", frame, covered, dmin, dmax, covered ? dsum / covered : 0., crease_column);
            char name[64];
            std::snprintf(name, sizeof name, "ao_before_%llu.bgra8", frame); if (FILE* f = std::fopen(name, "wb")) { std::fwrite(before_image.data(), 4, before_image.size(), f); std::fclose(f); }
            std::snprintf(name, sizeof name, "ao_after_%llu.bgra8", frame); if (FILE* f = std::fopen(name, "wb")) { std::fwrite(after_image.data(), 4, after_image.size(), f); std::fclose(f); }
            std::snprintf(name, sizeof name, "ao_depth_%llu.r32f", frame); if (FILE* f = std::fopen(name, "wb")) { std::fwrite(depth_data.data(), 4, depth_data.size(), f); std::fclose(f); }
        }
        const bool active = ao_env && !ao_fault;
        const char* law = !active ? "identity" : ao_debug ? "debug" : "multiply";
        if (pixel_check) {
            const double floor_factor = std::pow(1. - double(ao_strength), 1. / 2.2);
            const unsigned floor_code = unsigned(std::floor(255. * floor_factor));
            unsigned darkened = 0, sentinel = 0, violations = 0, max_drop = 0, centre_n = 0, outer_n = 0;
            double centre = 0, outer = 0;
            for (std::size_t i = 0; i < after_image.size(); ++i) {
                const DWORD b = before_image[i], v = after_image[i];
                const bool sent = depth_data[i] < 0.f; sentinel += sent;
                const unsigned x = unsigned(i % W);
                unsigned drop = 0; bool bad = false;
                if (!active) bad = v != b;
                else if (ao_debug) {
                    const unsigned r = (v >> 16) & 255u, g = (v >> 8) & 255u, bl = v & 255u;
                    bad = r != g || g != bl || (sent && r != 255u) || r + 1u < floor_code;
                    drop = 255u - r;
                } else if (sent) bad = v != b;
                else for (unsigned k = 0; k < 3; ++k) {
                    const unsigned bc = (b >> (8 * k)) & 255u, ac = (v >> (8 * k)) & 255u;
                    if (ac > bc || ac + 1u < unsigned(std::floor(double(bc) * floor_factor))) bad = true;
                    if (ac < bc) drop += bc - ac;
                }
                if (bad && ++violations <= 4) std::printf("AO_VIOLATION frame=%llu x=%u y=%u before=%08lx after=%08lx sentinel=%u\n", frame, x, unsigned(i / W), b, v, sent);
                if (drop) { ++darkened; if (drop > max_drop) max_drop = drop; }
                if (!sent) {
                    if (x + 8 > W / 2 && x < W / 2 + 8) { centre += drop; ++centre_n; }
                    else if (x + 24 <= W / 2 || x >= W / 2 + 24) { outer += drop; ++outer_n; }
                }
            }
            const double centre_mean = centre_n ? centre / centre_n : 0., outer_mean = outer_n ? outer / outer_n : 0.;
            std::printf("AO_CREASE frame=%llu law=%s darkened=%u sentinel=%u violations=%u max_drop=%u centre_mean=%.3f outer_mean=%.3f changed=%u\n",
                        frame, law, darkened, sentinel, violations, max_drop, centre_mean, outer_mean, changed);
            require(!violations, "every crease pixel obeys the AO law (sentinel unchanged; channels within the strength floor; debug view gray)");
            require(sentinel > 0, "the crease frame keeps sentinel columns");
            if (active) require(darkened > 0 && centre_mean > outer_mean, "the crease darkens, most at its centre");
            else require(!changed, "AO off or refused: the main target is bit-identical through the hook");
        } else std::printf("AO_CREASE frame=%llu law=%s changed=%u pixel_check=0\n", frame, law, changed);
        api(d->EndScene(), "EndScene");
        const auto image = color_image();
        std::printf("COLOR frame=%llu hash=%016llx\n", frame, static_cast<unsigned long long>(color_hash(image)));
        write_presented(image);
        api(d->SetDepthStencilSurface(depth.p), "SetDepthStencilSurface rebind");
        api(d->Present(nullptr, nullptr, nullptr, nullptr), "Present");
        ++frame; ++frames_since_reset;
        ++taa_frames; history_valid = true; camera_history = camera_current;
    }
    // Script. Multiply/identity twins: f0-f2 flat hook frames (planes at
    // constant device depth: the AO factor is the exact identity and the main
    // target after the hook equals the reference resolve byte for byte, i.e.
    // the AO-off image), Reset, f3 crease without history (pixel law), f4-f5
    // crease with history, Reset, f6 crease without history (pixel law), f7 crease.
    // Debug twin: crease frames only (the flat frames would present the white
    // factor). HDR: the flat frames prove attach and identity on the FP16
    // target; the crease frames run without the pixel law (the 8-bit main
    // target holds the previous write-back, not the scene).
    void run_ao_hook() {
        require(enabled && seam && taa && camera && hook_install && hook_shutdown && hook_signals && hook_status, "aohook needs the seam, TAA, the fake camera and the scene-hook exports");
        hook_create();
        auto compositor = reinterpret_cast<void*>(&fixture_compositor);
        require(hook_install(hook_site, compositor) == 1 && !std::strcmp(hook_status(), "active"), "install on the verified callsite");
        hook_installed = true;
        std::printf("HOOK installed=%u status=%s ao=%u fault=%u debug=%u strength=%.3f hdr=%u\n", hook_installed, hook_status(), ao_env, ao_fault, ao_debug, ao_strength, hdr);
        const bool pixels = !hdr;
        // f1 signals before the scene: the copy path is the scene end (the AO
        // chain runs at the bloom copy under the same contract).
        if (!ao_debug) { hook_frame(true, false); hook_frame(true, true); hook_frame(false, false); }
        if (ao_toggle_script) {
            // Ctrl+Shift+F11 twin: off for two flat frames after a Reset (byte-exact
            // with the reference resolve, reason=disabled), on again for the crease.
            require(ao_toggle != nullptr, "toggle export");
            int state = ao_toggle(d.p); std::printf("AO_TOGGLE frame=%llu enabled=%d\n", frame, state); require(state == (ao_env ? 0 : -1), "toggle off");
            reset(); history_valid = false;
            hook_frame(true, false); hook_frame(true, false);
            state = ao_toggle(d.p); std::printf("AO_TOGGLE frame=%llu enabled=%d\n", frame, state); require(state == (ao_env ? 1 : -1), "toggle on");
            reset(); history_valid = false;
            ao_crease_frame(pixels); ao_crease_frame(false);
        } else {
            if (!ao_debug) { reset(); history_valid = false; }
            ao_crease_frame(pixels); ao_crease_frame(false); ao_crease_frame(false);
            reset(); history_valid = false;
            ao_crease_frame(pixels); ao_crease_frame(false);
        }
        require(hook_shutdown() == 1 && !std::strcmp(hook_status(), "restored"), "shutdown restores the callsite");
        hook_installed = false;
        VirtualFree(hook_code, 0, MEM_RELEASE); hook_code = nullptr; hook_fixture = nullptr;
    }
    // ---- actual live linear-material route, sharing the original Argon VS ----
    // All lighting is zero except a unit lightmap. Startup lightmap gain four
    // produces encode_gamma22(4)>1 in FP16; ordinary motion produces one.
    // This deliberately simple witness distinguishes actual combined binding
    // from ordinary motion while the existing RT1/RT2 oracle verifies both.
    void linear_material_inputs() {
        const DWORD texels[] = {0xbf804020u, 0u, 0x40ffffffu};
        for (UINT stage=0; stage<3; ++stage) {
            D3DLOCKED_RECT lock{};api(textures[stage]->LockRect(0,&lock,nullptr,0),"material texture lock");
            for(UINT y=0;y<2;++y)for(UINT x=0;x<2;++x)
                std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4,&texels[stage],4);
            api(textures[stage]->UnlockRect(0),"material texture unlock");
        }
        float zero[4]{};
        api(d->SetVertexShaderConstantF(40,zero,1),"zero material emissive");
        int count[4]={0,0,1,0};api(d->SetVertexShaderConstantI(0,count,1),"zero point count");
        api(d->SetPixelShaderConstantF(5,zero,1),"zero directional zero");
        api(d->SetPixelShaderConstantF(7,zero,1),"zero directional one");
    }
    void run_linear_materials(const char* shared_path, const char* split_path,
                              const char* bump_vs_path, const char* bump_ps_path, const char* bump_negative_path) {
        require(seam&&enabled&&hdr&&hdr_agx&&hdr_readback,"linear materials needs the HDR AgX seam");
        char setting[32]{};
        const bool material=GetEnvironmentVariableA("X3M_LINEAR_MATERIALS",setting,sizeof setting)==1&&setting[0]=='1';
        require(!material || (GetEnvironmentVariableA("X3M_LIGHTMAP_EMISSIVE_GAIN",setting,sizeof setting)>0&&std::atof(setting)==4.),"linear material witness uses startup lightmap gain four");
        const auto shared_words=load(shared_path);
        require(fnv(shared_words.data(),shared_words.size()*4)==0x3b94320087e81945ull,"shared VS positive uses reviewed shared DEFAULT PS");
        const auto split_words=load(split_path);
        require(fnv(split_words.data(),split_words.size()*4)==0x5f82ecacd39529cdull,"retained XT BUMP control uses reviewed class-C PS");
        Com<IDirect3DPixelShader9> shared, split;
        api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(shared_words.data()),&shared.p),"CreatePixelShader shared DEFAULT");
        api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(split_words.data()),&split.p),"CreatePixelShader class-C XT control");
        // This real class-C pair is now XT material-covered; retain its original inputs.
        // Preserve its complete XT BUMP input ABI, including detail and occlusion.
        const std::string supplied(bump_negative_path);
        const auto slash=supplied.find_last_of("/\\");
        require(slash!=std::string::npos,"corpus program directory");
        const auto directory=supplied.substr(0,slash+1);
        const auto negative_vs_words=load((directory+"vs_37c34a7478544c14.bin").c_str());
        require(fnv(negative_vs_words.data(),negative_vs_words.size()*4)==0x37c34a7478544c14ull,"class-C original VS identity");
        Com<IDirect3DVertexShader9> negative_vs;
        api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(negative_vs_words.data()),&negative_vs.p),"create class-C original VS");
        Com<IDirect3DVertexDeclaration9> bump_declaration;
        const D3DVERTEXELEMENT9 elements[]={
            {0,0,D3DDECLTYPE_FLOAT16_4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
            {0,8,D3DDECLTYPE_FLOAT16_4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
            {0,16,D3DDECLTYPE_FLOAT16_4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},
            {0,24,D3DDECLTYPE_FLOAT16_4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_BINORMAL,0},
            {0,32,D3DDECLTYPE_FLOAT16_4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TANGENT,0},D3DDECL_END()};
        api(d->CreateVertexDeclaration(elements,&bump_declaration.p),"Create BUMP declaration");
        Com<IDirect3DVertexBuffer9> bump_vb;
        api(d->CreateVertexBuffer(40*3,0,0,D3DPOOL_MANAGED,&bump_vb.p,nullptr),"Create BUMP buffer");
        void *original_data=nullptr,*bump_data=nullptr;
        api(vb_a->Lock(0,0,&original_data,D3DLOCK_READONLY),"read original geometry");
        api(bump_vb->Lock(0,0,&bump_data,0),"write BUMP geometry");
        const unsigned short basis[]={0,half(1),0,0,half(1),0,0,0};
        for(unsigned vertex=0;vertex<3;++vertex){
            auto* target=static_cast<char*>(bump_data)+vertex*40;
            std::memcpy(target,static_cast<char*>(original_data)+vertex*24,24);
            std::memcpy(target+24,basis,sizeof basis);
        }
        api(bump_vb->Unlock(),"unlock BUMP geometry");api(vb_a->Unlock(),"unlock original geometry");
        Com<IDirect3DTexture9> normal;
        api(d->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&normal.p,nullptr),"Create normal map");
        D3DLOCKED_RECT normal_lock{};api(normal->LockRect(0,&normal_lock,nullptr,0),"normal map lock");
        const DWORD normal_texel=0x80008000u;
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)
            std::memcpy(static_cast<char*>(normal_lock.pBits)+y*normal_lock.Pitch+x*4,&normal_texel,4);
        api(normal->UnlockRect(0),"normal map unlock");
        Object bump=a;bump.name="BUMP";bump.vb=bump_vb.p;bump.recorded=false;
        auto negative_inputs=[&]() {
            // Valid native XT BUMP linkage, seven samplers, and the original
            // false decal/color-mixing branches. Both ordinary and linear paths are valid.
            const BOOL branches[2]={FALSE,FALSE};
            api(d->SetPixelShaderConstantB(0,branches,2),"XT original static branches");
            float vertex[8][4]{};
            vertex[0][0]=1;vertex[1][0]=.625f; // c39 reflection, c40 alpha
            vertex[3][0]=2;vertex[4][0]=.1f; // c42 exponent, c43 minimum
            vertex[5][0]=1;vertex[6][0]=12;vertex[7][0]=.9f; // fog, highlight, preshader complement
            api(d->SetVertexShaderConstantF(39,vertex[0],8),"XT native VS c39..46");
            float c[24][4]{};c[0][0]=c[1][1]=c[2][2]=1;c[3][0]=.5f;
            c[5][2]=c[7][2]=1;c[9][0]=1;c[10][0]=10;
            for(unsigned row:{11u,12u,13u,14u,15u,16u})c[row][0]=1;
            c[22][0]=9;c[23][0]=.5f;
            api(d->SetPixelShaderConstantF(0,c[0],24),"XT native PS constants");
            IDirect3DBaseTexture9* samplers[]={textures[0].p,normal.p,textures[1].p,textures[2].p,cube.p,textures[2].p,normal.p};
            for(unsigned stage=0;stage<7;++stage){
                api(d->SetTexture(stage,samplers[stage]),"XT native sampler");
                api(d->SetSamplerState(stage,D3DSAMP_SRGBTEXTURE,FALSE),"XT native SRGB state");
                for(auto filter:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})api(d->SetSamplerState(stage,filter,D3DTEXF_POINT),"XT point filter");
                api(d->SetSamplerState(stage,D3DSAMP_MIPFILTER,D3DTEXF_NONE),"XT no mip");
                api(d->SetSamplerState(stage,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP),"XT clamp u");
                api(d->SetSamplerState(stage,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP),"XT clamp v");
            }
            // Detail RG and AG normal must both reconstruct finite unit normals.
            D3DLOCKED_RECT lock{};api(normal->LockRect(0,&lock,nullptr,0),"XT normal lock");
            const DWORD texel=0x80808080u;
            for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4,&texel,4);
            api(normal->UnlockRect(0),"XT normal unlock");
            api(d->SetVertexDeclaration(bump_declaration.p),"XT native basis declaration");
        };
        require(!material || (GetEnvironmentVariableA("X3M_MATERIAL_EMISSIVE_GAIN",setting,sizeof setting)>0&&std::atof(setting)==4.),"Asteroid witness uses startup material gain four");
        float first[4]{};
        for(unsigned i=0;i<12&&!materialwrap;++i) {
            frame_begin();linear_material_inputs();write_reserved();
            if(i>=2&&i<=5)api(d->SetSamplerState(i-2,D3DSAMP_SRGBTEXTURE,TRUE),"refuse sampler decode");
            if(i==6||i==7) {
                api(d->BeginStateBlock(),"material BeginStateBlock");
                api(d->SetSamplerState(1,D3DSAMP_SRGBTEXTURE,TRUE),"record sampler decode");
                Com<IDirect3DStateBlock9> block;api(d->EndStateBlock(&block.p),"material EndStateBlock");
                if(i==7)api(block->Apply(),"material recorded Apply");
            }
            if(i==8) {
                Com<IDirect3DStateBlock9> block;api(d->CreateStateBlock(D3DSBT_ALL,&block.p),"material capture stateblock");
                api(d->SetSamplerState(1,D3DSAMP_SRGBTEXTURE,TRUE),"temporary sampler decode");
                api(block->Apply(),"material restore sampler Apply");
            }
            const bool eligible=i<2||i==6||i==8||i>=9;
            // Keep the exact same VS object while alternating covered Argon
            // and shared DEFAULT PS programs; class-C now exercises XT material coverage.
            if(i==1)std::swap(ps.p,shared.p);
            if(i==9){negative_inputs();std::swap(vs.p,negative_vs.p);std::swap(ps.p,split.p);}
            draw(i==9?bump:a,0,0,0,true,true,i!=0&&i!=9&&i!=10,Alter::None,true,i==9?40:24);
            if(i==9){std::swap(vs.p,negative_vs.p);std::swap(ps.p,split.p);}
            if(i==1)std::swap(ps.p,shared.p);
            unsigned w=0,h=0;const auto image=hdr_image(&w,&h);
            require(w==W&&h==H,"material FP16 dimensions");
            const float* center=&image[(std::size_t(H/2)*W+W/2)*4];
            const double expected=material&&eligible?std::pow(4.,1./2.2):1.;
            // Zero lighting plus unit lightmap gives a finite native XT control.
            for(unsigned lane=0;lane<3;++lane)
                require(std::isfinite(center[lane])&&std::fabs(center[lane]-expected)<.005,"actual material/ordinary FP16 color witness");
            if(i==0)std::memcpy(first,center,sizeof first);
            if(i==10||i==11)require(!std::memcmp(first,center,sizeof first),"Reset retains cached shader gains and alpha");
            std::printf("LINEAR_LIVE frame=%llu combined=%u refusal=%u vs=%016llx ps=%016llx rgba=%.9g,%.9g,%.9g,%.9g native_hash=%016llx\n",frame,material&&eligible,
                eligible?0u:i==9?1u:4u,i==9?0x37c34a7478544c14ull:vs_hash,i==1?0x3b94320087e81945ull:i==9?0x5f82ecacd39529cdull:ps_hash,double(center[0]),double(center[1]),double(center[2]),double(center[3]),static_cast<unsigned long long>(fnv(image.data(),image.size()*sizeof(float))));
            frame_end();
            if(i==9) {
                api(SetEnvironmentVariableA("X3M_LIGHTMAP_EMISSIVE_GAIN","16")?S_OK:E_FAIL,"change environment after attach");
                reset();
            }
        }
        // Extend the unchanged twelve-frame DEFAULT prefix with the class-B
        // route. Separate managed geometry adds real tangent/binormal inputs;
        // position/UV/normal remain exactly the original full-screen triangle.
        const auto bump_vs_words=load(bump_vs_path), bump_ps_words=load(bump_ps_path), negative_words=load(bump_negative_path);
        require(fnv(bump_vs_words.data(),bump_vs_words.size()*4)==0x4944d81dfe531b37ull,"reviewed BUMP VS");
        require(fnv(bump_ps_words.data(),bump_ps_words.size()*4)==0xca6bfa4a6cca7e2aull,"reviewed BUMP PS");
        require(fnv(negative_words.data(),negative_words.size()*4)==0x5f82ecacd39529cdull,"retained class-C XT control PS");
        Com<IDirect3DVertexShader9> bump_vs;
        Com<IDirect3DPixelShader9> bump_ps;
        api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(bump_vs_words.data()),&bump_vs.p),"Create BUMP VS");
        api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(bump_ps_words.data()),&bump_ps.p),"Create BUMP PS");
        api(normal->LockRect(0,&normal_lock,nullptr,0),"restore BUMP normal lock");
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)
            std::memcpy(static_cast<char*>(normal_lock.pBits)+y*normal_lock.Pitch+x*4,&normal_texel,4);
        api(normal->UnlockRect(0),"restore BUMP normal unlock");
        float bump_first[4]{};
        for(unsigned i=12;i<24&&!materialwrap;++i){
            frame_begin();linear_material_inputs();write_reserved();
            const bool negative=i==19, use_bump=i!=17&&i!=23&&!negative;
            const bool eligible=i!=13&&i!=15;
            // Fresh attach/Reset SRGB knowledge must come from getters. All
            // other frames explicitly establish the actual state at s4.
            if(i!=21)api(d->SetSamplerState(4,D3DSAMP_SRGBTEXTURE,FALSE),"s4 linear state");
            if(use_bump){
                api(normal->LockRect(0,&normal_lock,nullptr,0),"BUMP normal reset lock");
                for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)std::memcpy(static_cast<char*>(normal_lock.pBits)+y*normal_lock.Pitch+x*4,&normal_texel,4);
                api(normal->UnlockRect(0),"BUMP normal reset unlock");
                api(d->SetTexture(1,normal.p),"BUMP normal sample");
                api(d->SetTexture(2,textures[1].p),"BUMP scalar mask");
                api(d->SetTexture(3,textures[2].p),"BUMP lightmap");
                api(d->SetTexture(4,cube.p),"BUMP cube");
                for(auto filter:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})api(d->SetSamplerState(4,filter,D3DTEXF_POINT),"BUMP cube filter");
                api(d->SetSamplerState(4,D3DSAMP_MIPFILTER,D3DTEXF_NONE),"BUMP cube mip");
                api(d->SetVertexDeclaration(bump_declaration.p),"BUMP declaration");
            }else api(d->SetTexture(4,nullptr),"DEFAULT ignores unused s4");
            if(i==13||i==17||i==23)api(d->SetSamplerState(4,D3DSAMP_SRGBTEXTURE,TRUE),"s4 true witness");
            if(i==14||i==15){
                api(d->BeginStateBlock(),"BUMP BeginStateBlock");
                api(d->SetSamplerState(4,D3DSAMP_SRGBTEXTURE,TRUE),"record s4 true");
                Com<IDirect3DStateBlock9> block;api(d->EndStateBlock(&block.p),"BUMP EndStateBlock");
                if(i==15)api(block->Apply(),"BUMP recorded Apply");
            }
            if(i==16){
                Com<IDirect3DStateBlock9> block;api(d->CreateStateBlock(D3DSBT_ALL,&block.p),"BUMP capture stateblock");
                api(d->SetSamplerState(4,D3DSAMP_SRGBTEXTURE,TRUE),"temporary s4 true");
                api(block->Apply(),"restore s4 stateblock");
            }
            // Distinct VS/resource keys lose correspondence after an absent
            // frame; a PS-only change keeps the same position program/key.
            const bool matched=i==13||i==14||i==15||i==16||i==22;
            if(use_bump){std::swap(vs.p,bump_vs.p);std::swap(ps.p,bump_ps.p);}
            if(negative){negative_inputs();std::swap(vs.p,negative_vs.p);std::swap(ps.p,split.p);}
            draw(use_bump||negative?bump:a,0,0,0,true,true,matched,Alter::None,true,use_bump||negative?40:24);
            if(negative){std::swap(vs.p,negative_vs.p);std::swap(ps.p,split.p);}
            if(use_bump){std::swap(vs.p,bump_vs.p);std::swap(ps.p,bump_ps.p);}
            unsigned w=0,h=0;const auto image=hdr_image(&w,&h);
            require(w==W&&h==H,"BUMP FP16 dimensions");
            const float* center=&image[(std::size_t(H/2)*W+W/2)*4];
            const double expected=material&&eligible?std::pow(4.,1./2.2):1.;
            for(unsigned lane=0;lane<3;++lane)
                require(std::isfinite(center[lane])&&std::fabs(center[lane]-expected)<.005,"BUMP/DEFAULT actual FP16 witness");
            if(i==12)std::memcpy(bump_first,center,sizeof bump_first);
            if(i==21||i==22)require(!std::memcmp(bump_first,center,sizeof bump_first),"BUMP Reset retains cached gains and alpha");
            std::printf("LINEAR_LIVE frame=%llu combined=%u refusal=%u vs=%016llx ps=%016llx rgba=%.9g,%.9g,%.9g,%.9g native_hash=%016llx\n",frame,material&&eligible,
                eligible?0u:negative?1u:4u,negative?0x37c34a7478544c14ull:use_bump?0x4944d81dfe531b37ull:vs_hash,
                negative?0x5f82ecacd39529cdull:use_bump?0xca6bfa4a6cca7e2aull:ps_hash,
                double(center[0]),double(center[1]),double(center[2]),double(center[3]),static_cast<unsigned long long>(fnv(image.data(),image.size()*sizeof(float))));
            frame_end();
            if(i==20){reset();bump.recorded=false;}
        }
        // Complete pair publication is qualified in one corpus. Reuse the six
        // already-created covered originals, so each identity owns one variant.
        const char* corpus_vs[]={"53a0a641107ed76c","719856ce0c213220","badefd5143b3024f","4944d81dfe531b37","19a246a56e9d9700","44c4a41ca92ae2e3","494fe349b8bc12ec","b0602757fce6e870","0c223ad11bce02d5","233d17d26ce0c1fc","167eb2d5629ab9d3","330ceb9dd874ede2","12b8a13f13fe8cfe","29d7c575396ed280","2a560f246c90fa64","2e0254dd999841c2","33388c8897d428a5","37e6956afd8b8d76","57392213f62fef19","5c17a381b149b3b9","a420a010b0271479","a7cddf2c98d61117","a804f173f693944a","b4059ab6af8fc529","ea3d15b287892410"};
        const char* corpus_ps[]={"8759c7838bbc86c2","63f96eba9eea7880","593e5dea9b3457d5","7a0bb00a8070496a","8d5b2ba0fb4d13bf","dab93928f26906f7","3b94320087e81945","e3b7acc16da9932d","7a14d4dcb28f27e5","8ab6188a40ca15ea","8df6143d0e77d92e","e16a9806ee3544c3","ca6bfa4a6cca7e2a","5e0a10fe752b6140","63379470db8d2a86","68915563dd0aac9a","d086fde54698070c","f17fffd88d134b04","462342e3e5781384","827d8d2d617bedce","02606104fa59fb29","1d638938d93421b3","bd4d51c08486c6e0","de2dd381fa64193d","7c83ed50c9894e44","e70adc744a38ca59","db644b73b68c0547","ff32b602a271c327","f6a501717c3e5ca8","55826dc176afe464","0c1f3f0f440e4a0c","64bac8bb307eb896","789449ffd931d23e","4f052209611387f0","abf3c0fad53456d8","cf449bcb069aec4f","99153c144030c396","c1452981fd0bff64","b0f9313b77cc78ee","d514bf852d8a9c58","dff6a3d360603fa2","f1d14a7dbf7c6173","1f26d41bcb7dac1e","bdcdb3ab996ae4e0","78963cdc7c710e04","1ed1bf0fdec00e1a","2b04461d0dae038b","acc83ed2509d84a1","3006f8030a467739","d6e8bdde0e4c515f","e5ea78b8b0b0fe07","f42202faf57a3c89","769c3814fc0efba8","22cc5b05a55ef61e","ef2bf556f207b8bd","91b6c09eb47f8555","cc09f17db377fd9e","3755809bd40afc13","61418505e5d8f998","b5f1d4145171026b","3602b05ce11ca6ff","8e58ac79b59b02b1","042c9ae16f41feff","68f0dd6791fd7d3d","5c823b8507fa1442","a6e1328c0bb3f401","517540ae6d5e5410","7a0c3388065bb08d","d44db87778a43b61","550c2a4d4d3ed70f","188c5ab9dbb98393","18d372968af4a480","39eb3c2258a516e1","43c9405568d2226f","57acf59d19c73791","5e056627e9ff3a8d","62c180abe017e239","675f9077d8fd21c4","77a5b2d62fb3be48","7e5e41276b3d7514","9d27e7ba242f3831","a910daef935891ce","c997a37560e266df","e1acf8a03850acaf","ebf41e1ace7af45b","ed44232013f67072","f286856c3f400377","f646f03be5a8708d","f917d48ee826da1f","fce465befff2f623"};
        struct CorpusPair { unsigned vertex,pixel; bool bump,affine,standard,low; };
        const CorpusPair corpus[]={
            {0,0,false,true,false,false},
            {0,1,false,true,false,false},
            {1,2,false,false,false,false},
            {1,3,false,true,false,false},
            {1,4,false,true,false,false},
            {1,5,false,false,false,false},
            {2,2,false,false,false,false},
            {2,3,false,true,false,false},
            {2,4,false,true,false,false},
            {2,5,false,false,false,false},
            {0,6,false,true,false,false},
            {0,7,false,true,false,false},
            {1,8,false,true,false,false},
            {1,9,false,true,false,false},
            {1,10,false,false,false,false},
            {1,11,false,false,false,false},
            {2,8,false,true,false,false},
            {2,9,false,true,false,false},
            {2,10,false,false,false,false},
            {2,11,false,false,false,false},
            {3,12,true,true,false,false},
            {3,13,true,true,false,false},
            {4,14,true,true,false,false},
            {4,15,true,false,false,false},
            {4,16,true,true,false,false},
            {4,17,true,false,false,false},
            {5,14,true,true,false,false},
            {5,15,true,false,false,false},
            {5,16,true,true,false,false},
            {5,17,true,false,false,false},
            {0,18,false,true,false,false},
            {0,19,false,true,false,false},
            {1,20,false,true,false,false},
            {1,21,false,true,false,false},
            {1,22,false,false,false,false},
            {1,23,false,false,false,false},
            {2,20,false,true,false,false},
            {2,21,false,true,false,false},
            {2,22,false,false,false,false},
            {2,23,false,false,false,false},
            {1,26,false,true,true,false},
            {1,27,false,true,true,false},
            {1,28,false,false,true,false},
            {1,29,false,false,true,false},
            {2,26,false,true,true,false},
            {2,27,false,true,true,false},
            {2,28,false,false,true,false},
            {2,29,false,false,true,false},
            {6,24,false,true,true,false},
            {6,25,false,true,true,false},
            {3,30,true,true,true,false},
            {3,31,true,true,true,false},
            {4,32,true,true,true,false},
            {4,33,true,true,true,false},
            {4,34,true,false,true,false},
            {4,35,true,false,true,false},
            {5,32,true,true,true,false},
            {5,33,true,true,true,false},
            {5,34,true,false,true,false},
            {5,35,true,false,true,false},
            {3,36,true,true,true,true},
            {3,37,true,true,true,true},
            {4,38,true,true,true,true},
            {4,39,true,true,true,true},
            {4,40,true,false,true,true},
            {4,41,true,false,true,true},
            {5,38,true,true,true,true},
            {5,39,true,true,true,true},
            {5,40,true,false,true,true},
            {5,41,true,false,true,true},
            {3,42,true,true,false,false},
            {3,43,true,true,false,false},
            {4,44,true,true,false,false},
            {4,45,true,true,false,false},
            {4,46,true,false,false,false},
            {4,47,true,false,false,false},
            {5,44,true,true,false,false},
            {5,45,true,true,false,false},
            {5,46,true,false,false,false},
            {5,47,true,false,false,false},
            {3,48,true,true,false,false},
            {3,49,true,true,false,false},
            {4,50,true,true,false,false},
            {4,51,true,true,false,false},
            {4,52,true,false,false,false},
            {4,53,true,false,false,false},
            {5,50,true,true,false,false},
            {5,51,true,true,false,false},
            {5,52,true,false,false,false},
            {5,53,true,false,false,false},
            {0,54,false,true,false,false},
            {0,55,false,true,false,false},
            {1,56,false,true,false,false},
            {1,57,false,true,false,false},
            {1,58,false,false,false,false},
            {1,59,false,false,false,false},
            {2,56,false,true,false,false},
            {2,57,false,true,false,false},
            {2,58,false,false,false,false},
            {2,59,false,false,false,false},
            {3,60,true,true,false,false},
            {3,61,true,true,false,false},
            {4,62,true,true,false,false},
            {4,63,true,true,false,false},
            {4,64,true,false,false,false},
            {4,65,true,false,false,false},
            {5,62,true,true,false,false},
            {5,63,true,true,false,false},
            {5,64,true,false,false,false},
            {5,65,true,false,false,false},
            {7,66,false,false,false,false},
            {8,67,false,false,false,false},
            {9,67,false,false,false,false},
            {10,68,true,false,false,false},
            {11,69,true,false,false,false},
            {12,69,true,false,false,false},
            {13,72,false,false,false,false},
            {13,74,false,false,false,false},
            {14,73,true,true,false,false},
            {14,75,true,false,false,false},
            {14,79,true,true,false,false},
            {14,89,true,false,false,false},
            {15,77,false,false,false,false},
            {15,82,false,false,false,false},
            {15,84,false,true,false,false},
            {15,87,false,true,false,false},
            {16,70,true,true,false,false},
            {16,71,true,true,false,false},
            {17,80,false,true,false,false},
            {17,83,false,true,false,false},
            {18,76,true,false,false,false},
            {18,81,true,false,false,false},
            {19,85,true,false,false,false},
            {19,86,true,false,false,false},
            {20,78,false,false,false,false},
            {20,88,false,false,false,false},
            {21,77,false,false,false,false},
            {21,82,false,false,false,false},
            {21,84,false,true,false,false},
            {21,87,false,true,false,false},
            {22,85,true,false,false,false},
            {22,86,true,false,false,false},
            {23,73,true,true,false,false},
            {23,75,true,false,false,false},
            {23,79,true,true,false,false},
            {23,89,true,false,false,false},
            {24,78,false,false,false,false},
            {24,88,false,false,false,false},
        };
        Com<IDirect3DVertexShader9> vertex_bank[25];
        Com<IDirect3DPixelShader9> pixel_bank[90];
        for(unsigned i=0;i<25;++i) {
            if(i==0)vertex_bank[i].p=vs.p;
            else if(i==3)vertex_bank[i].p=bump_vs.p;
            if(vertex_bank[i].p)vertex_bank[i]->AddRef();
            else {
                const auto code=load((directory+"vs_"+corpus_vs[i]+".bin").c_str());
                require(fnv(code.data(),code.size()*4)==std::strtoull(corpus_vs[i],nullptr,16),"corpus original VS identity");
                api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()),&vertex_bank[i].p),"create corpus VS");
            }
        }
        for(unsigned i=0;i<90;++i) {
            const std::string id(corpus_ps[i]);
            if(id=="8759c7838bbc86c2")pixel_bank[i].p=ps.p;
            else if(id=="3b94320087e81945")pixel_bank[i].p=shared.p;
            else if(id=="ca6bfa4a6cca7e2a")pixel_bank[i].p=bump_ps.p;
            if(pixel_bank[i].p)pixel_bank[i]->AddRef();
            else {
                const auto code=load((directory+"ps_"+id+".bin").c_str());
                require(fnv(code.data(),code.size()*4)==std::strtoull(id.c_str(),nullptr,16),"corpus original PS identity");
                api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&pixel_bank[i].p),"create corpus PS");
            }
        }
        if(materialwrap){
            require(wrap_snapshot&&!taa,"native WRAP short mode requires observer and TAA off");
            // Two equal triangles at distinct start indices give each source
            // its own history key; duplicate keys are intentionally poisoned.
            Com<IDirect3DIndexBuffer9> indices;
            api(d->CreateIndexBuffer(12,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&indices.p,nullptr),"WRAP indices");
            void* data=nullptr;api(indices->Lock(0,0,&data,0),"WRAP indices lock");
            const unsigned short triangle[]={0,1,2,0,1,2};std::memcpy(data,triangle,sizeof triangle);api(indices->Unlock(),"WRAP indices unlock");
            // Metadata-independent source semantic witnesses read from the
            // three original VS declarations: Boron TEX6, Paranid TEX7.
            struct WrapPair { const char *vs,*ps; unsigned source,motion,scalars; };
            const WrapPair representatives[]={
                {"57392213f62fef19","a910daef935891ce",6,7,2},
                {"5c17a381b149b3b9","ed44232013f67072",6,7,1},
                {"33388c8897d428a5","18d372968af4a480",7,5,1}};
            std::uint64_t sequence=0;
            const auto wrap_state=[](unsigned index){return D3DRENDERSTATETYPE(index<8?D3DRS_WRAP0+index:D3DRS_WRAP8+index-8);};
            for(unsigned representative=0;representative<3;++representative){
                const auto& contract=representatives[representative];
                unsigned vi=25,pi=90;
                for(unsigned i=0;i<25;++i)if(!std::strcmp(corpus_vs[i],contract.vs))vi=i;
                for(unsigned i=0;i<90;++i)if(!std::strcmp(corpus_ps[i],contract.ps))pi=i;
                require(vi<25&&pi<90,"WRAP representative original inventory");
                Object object=bump;object.scope.node_serial=4000+representative;object.recorded=false;
                for(unsigned step=0;step<6;++step){
                    frame_begin();linear_material_inputs();write_reserved();
                    const bool fallback=step==2,combined=material&&!fallback,matched=step!=0&&step!=4;
                    const float emissive[4]={.2f,.4f,.6f,0};
                    api(d->SetVertexShaderConstantF(40,emissive,1),"WRAP nonzero palette lighting");
                    float pixel[8][4]{};
                    if(representative==2){pixel[0][0]=pixel[1][1]=pixel[2][2]=1;pixel[3][0]=.25f;pixel[4][2]=1;}
                    else {pixel[0][0]=.25f;pixel[1][2]=pixel[3][2]=1;}
                    api(d->SetPixelShaderConstantF(0,pixel[0],8),"WRAP native palette constants");
                    // Nonzero mask exposes the original J reflection term;
                    // geometric normal+camera give nonzero J and u^11 across
                    // this finite triangle. Palette DEFs are never overwritten.
                    D3DLOCKED_RECT mask{};api(textures[1]->LockRect(0,&mask,nullptr,0),"WRAP mask lock");
                    const DWORD mask_texel=0xff404040u;
                    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)std::memcpy(static_cast<char*>(mask.pBits)+y*mask.Pitch+x*4,&mask_texel,4);
                    api(textures[1]->UnlockRect(0),"WRAP mask unlock");
                    api(d->SetTexture(1,normal.p),"WRAP normal");api(d->SetTexture(2,textures[1].p),"WRAP nonzero mask");
                    api(d->SetTexture(3,textures[2].p),"WRAP lightmap");api(d->SetTexture(4,cube.p),"WRAP native reflection");
                    api(d->SetSamplerState(4,D3DSAMP_SRGBTEXTURE,FALSE),"WRAP s4 linear");
                    for(auto filter:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})api(d->SetSamplerState(4,filter,D3DTEXF_POINT),"WRAP cube filter");
                    api(d->SetSamplerState(4,D3DSAMP_MIPFILTER,D3DTEXF_NONE),"WRAP cube mip");
                    api(d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,fallback),"WRAP clean material refusal");
                    DWORD caller[16]{};
                    if(step!=4){
                        const bool reverse=step==1||step==3;
                        caller[1]=reverse?11:5;caller[2]=reverse?6:10;
                        caller[contract.source]=reverse?2:1;caller[contract.motion]=15;caller[8]=15;
                        for(unsigned index=0;index<16;++index)api(d->SetRenderState(wrap_state(index),caller[index]),"WRAP caller pattern");
                    } // First post-Reset draw deliberately omits all WRAP setters.
                    if(step==3){
                        api(d->BeginStateBlock(),"WRAP begin recorded state");api(d->SetRenderState(D3DRS_WRAP1,0),"WRAP recorded setter");
                        Com<IDirect3DStateBlock9> recorded;api(d->EndStateBlock(&recorded.p),"WRAP end recorded state");
                        DWORD untouched=0;api(d->GetRenderState(D3DRS_WRAP1,&untouched),"WRAP recording did not apply");require(untouched==caller[1],"recorded setter leaves native state");
                        Com<IDirect3DStateBlock9> block;api(d->CreateStateBlock(D3DSBT_ALL,&block.p),"WRAP stateblock capture");
                        for(unsigned index=0;index<16;++index)api(d->SetRenderState(wrap_state(index),0),"WRAP before Apply");
                        api(block->Apply(),"WRAP restore from stateblock");
                    }
                    api(d->SetIndices(indices.p),"WRAP native indices");api(d->SetVertexDeclaration(bump_declaration.p),"WRAP basis declaration");
                    scope(&object);api(d->SetStreamSource(0,object.vb,0,40),"WRAP native stream");
                    api(d->SetVertexShader(vertex_bank[vi].p),"WRAP original VS");api(d->SetPixelShader(pixel_bank[pi].p),"WRAP original PS");rows(0,0,0);
                    const Snapshot before=snapshot();
                    for(unsigned draw_number=0;draw_number<2;++draw_number){
                        api(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,3*draw_number,1),"WRAP original indexed source");++draw_index;
                        records.push_back({&object,0,0,0,true,matched,object.rt,object.rp,object.rzo,false,jitter,true});
                        x3m::MotionOutputFixtureWrapSnapshot observed{};api(wrap_snapshot(d.p,&observed),"WRAP snapshot export");
                        ++sequence;
                        DWORD expected[16];std::memcpy(expected,caller,sizeof expected);
                        expected[contract.motion]=0;if(materialwrap_depth)expected[8]=0;
                        if(combined){expected[1]=(caller[1]&7)|((caller[contract.source]&1)?8:0);if(contract.scalars==2)expected[2]=(caller[2]&7)|((caller[contract.source]&2)?8:0);}
                        std::printf("MATERIAL_WRAP frame=%llu representative=%u step=%u draw=%u sequence=%llu valid=%u result=%08lx combined=%u source=%u motion=%u scalars=%u values=",frame,representative,step,draw_number,static_cast<unsigned long long>(observed.sequence),observed.valid,observed.result,combined,contract.source,contract.motion,contract.scalars);
                        for(unsigned index=0;index<16;++index)std::printf("%s%lu",index?",":"",observed.values[index]);
                        std::puts("");
                        require(observed.valid&&observed.result==S_OK&&observed.sequence==sequence,"WRAP native observer success and source-once sequence");
                        require(!std::memcmp(observed.values,expected,sizeof expected),"actual native WRAP transport and fallback cancellation");
                    }
                    compare(before,snapshot(),"WRAP caller state after consecutive draws");
                    object.recorded=true;object.rt=object.rp=object.rzo=0;
                    unsigned width=0,height=0;const auto image=hdr_image(&width,&height);
                    require(width==W&&height==H,"WRAP FP16 image size");
                    const float* center=&image[(std::size_t(H/2)*W+W/2)*4];
                    for(float value:std::array<float,4>{center[0],center[1],center[2],center[3]})require(std::isfinite(value),"WRAP finite native palette witness");
                    std::vector<float> alpha(std::size_t(W)*H);
                    for(std::size_t pixel=0;pixel<alpha.size();++pixel)alpha[pixel]=image[pixel*4+3];
                    std::printf("MATERIAL_WRAP_IMAGE frame=%llu rgba=%.9g,%.9g,%.9g,%.9g alpha_hash=%016llx native_hash=%016llx\n",frame,double(center[0]),double(center[1]),double(center[2]),double(center[3]),static_cast<unsigned long long>(fnv(alpha.data(),alpha.size()*sizeof(float))),static_cast<unsigned long long>(fnv(image.data(),image.size()*sizeof(float))));
                    frame_end();
                    if(step==3){reset();object.recorded=false;}
                }
            }
            return;
        }
        auto unknown_controls=[&](){
            // Valid shader linkage: the synthetic PS consumes the original COLOR0. The
            // same covered VS cannot make this unknown PS a material/motion pair.
            const DWORD unknown_program[]={0xffff0300u,0x5000051u,0xa00f0000u,0x3f000000u,0x3e800000u,0x3f400000u,0x3f800000u,0x200001fu,0x8000000au,0x900f0000u,0x2000001u,0x800f0800u,0xa0e40000u,0x3000005u,0x80080800u,0xa0ff0000u,0x90ff0000u,0xffffu};
            Com<IDirect3DPixelShader9> unknown;
            api(d->CreatePixelShader(unknown_program,&unknown.p),"create valid unknown PS");
            for(unsigned repeat=0;repeat<2;++repeat){
                frame_begin();linear_material_inputs();write_reserved();
                std::swap(ps.p,unknown.p);
                draw(a,0,0,0,true,false,false);
                std::swap(ps.p,unknown.p);
                unsigned w=0,h=0;const auto image=hdr_image(&w,&h);
                const float* center=&image[(std::size_t(H/2)*W+W/2)*4];
                require(center[0]==.5f&&center[1]==.25f&&center[2]==.75f&&center[3]==.625f,"valid unknown PS native output");
                std::printf("LINEAR_LIVE frame=%llu combined=0 refusal=0 vs=%016llx ps=%016llx rgba=%.9g,%.9g,%.9g,%.9g native_hash=%016llx\n",frame,vs_hash,static_cast<unsigned long long>(fnv(unknown_program,sizeof unknown_program)),double(center[0]),double(center[1]),double(center[2]),double(center[3]),static_cast<unsigned long long>(fnv(image.data(),image.size()*sizeof(float))));
                frame_end();
            }
        };
        Object corpus_object=a;corpus_object.name="CORPUS";
        for(unsigned pair=0;pair<148;++pair) {
            if(pair==116)unknown_controls();
            const auto& contract=corpus[pair];
            corpus_object.vb=contract.bump?bump_vb.p:vb_a.p;
            corpus_object.scope.node_serial=1000+pair;
            corpus_object.scope.node=0x100000+pair*0x100;
            corpus_object.scope.mesh=0x200000+pair*0x100;
            corpus_object.recorded=false;
            for(unsigned repeat=0;repeat<2;++repeat) {
                frame_begin();linear_material_inputs();write_reserved();
                const bool asteroid=pair>=110&&pair<116;
                const bool fixed=contract.vertex==2||contract.vertex==4||contract.vertex==9||contract.vertex==12||contract.vertex==24||contract.vertex==22||contract.vertex==21||contract.vertex==14;
                // Fixed-point originals use c0..3 for position and c7..20 for
                // world/normal/view/UV/alpha. rows() still writes c24 harmlessly.
                if(fixed) {
                    float values[21][4]{};std::memcpy(values,identity,sizeof identity);
                    for(unsigned base:{7u,10u,13u})for(unsigned lane=0;lane<3;++lane)values[base+lane][lane]=1;
                    values[4][2]=4;values[6][0]=1;values[15][3]=4;
                    values[16][0]=values[17][1]=1;values[18][0]=.625f;values[20][0]=1;
                    api(d->SetVertexShaderConstantF(0,values[0],21),"fixed corpus VS constants");
                }
                float pixel[12][4]{};
                if(contract.affine) {
                    pixel[0][0]=pixel[1][1]=pixel[2][2]=1;pixel[3][0]=.25f;
                    pixel[4][2]=pixel[6][2]=1;
                } else { pixel[0][0]=.25f;pixel[1][2]=1; }
                if(contract.standard) {
                    const unsigned base=contract.pixel%6<2?8:contract.affine?6:3;
                    for(unsigned n=0;n<4;++n)pixel[base+n][0]=n==1?10.f:1.f;
                }
                api(d->SetPixelShaderConstantF(0,pixel[0],12),"corpus PS constants");
                api(d->SetSamplerState(4,D3DSAMP_SRGBTEXTURE,FALSE),"corpus s4 state");
                if(contract.bump) {
                    D3DLOCKED_RECT lock{};api(normal->LockRect(0,&lock,nullptr,0),"corpus normal lock");
                    const DWORD sample=contract.low?0x808080ffu:0x80008000u;
                    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)
                        std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4,&sample,4);
                    api(normal->UnlockRect(0),"corpus normal unlock");
                    api(d->SetTexture(1,normal.p),"corpus normal");
                    api(d->SetTexture(2,textures[1].p),"corpus mask");
                    api(d->SetTexture(3,textures[2].p),"corpus lightmap");
                    api(d->SetTexture(4,cube.p),"corpus reflection");
                    api(d->SetVertexDeclaration(bump_declaration.p),"corpus basis declaration");
                } else api(d->SetTexture(4,nullptr),"corpus DEFAULT s4 unused");
                if(asteroid) {
                    // Distinct authored base/detail RGB, explicit complementary
                    // preshader weights, zero directional/specular/point terms.
                    const BOOL branches[2]={FALSE,FALSE};
                    api(d->SetPixelShaderConstantB(0,branches,2),"Asteroid native false booleans");
                    const float emissive[4]={1,1,1,0};
                    api(d->SetVertexShaderConstantF(fixed?19:40,emissive,1),"Asteroid unit material emissive");
                    float values[6][4]{};values[0][2]=values[2][2]=1;
                    const unsigned weights=contract.pixel==66||contract.pixel==68?4:2;
                    values[weights][0]=.75f;values[weights+1][0]=.25f;
                    api(d->SetPixelShaderConstantF(0,values[0],6),"Asteroid native directions and weights");
                    api(d->SetTexture(contract.bump?3:2,textures[2].p),"Asteroid detail RGB");
                    api(d->SetTexture(4,nullptr),"Asteroid has no reflection");
                }
                std::swap(vs.p,vertex_bank[contract.vertex].p);std::swap(ps.p,pixel_bank[contract.pixel].p);
                draw(corpus_object,0,0,0,true,true,repeat!=0,Alter::None,true,contract.bump?40:24);
                std::swap(vs.p,vertex_bank[contract.vertex].p);std::swap(ps.p,pixel_bank[contract.pixel].p);
                unsigned w=0,h=0;const auto image=hdr_image(&w,&h);
                require(w==W&&h==H,"corpus FP16 dimensions");
                const float* center=&image[(std::size_t(H/2)*W+W/2)*4];
                const double base[]={128./255.,64./255.,32./255.};
                for(unsigned lane=0;lane<3;++lane){
                    const double expected=asteroid?(material?std::pow(4.*(.25*std::pow(base[lane],2.2)+.75),1./2.2):.25*base[lane]+.75):(material?std::pow(4.,1./2.2):1.);
                    require(std::isfinite(center[lane])&&std::fabs(center[lane]-expected)<.005,"corpus combined binding witness");
                }
                std::printf("LINEAR_LIVE frame=%llu combined=%u refusal=0 vs=%s ps=%s rgba=%.9g,%.9g,%.9g,%.9g native_hash=%016llx\n",frame,material,corpus_vs[contract.vertex],corpus_ps[contract.pixel],double(center[0]),double(center[1]),double(center[2]),double(center[3]),static_cast<unsigned long long>(fnv(image.data(),image.size()*sizeof(float))));
                frame_end();
            }
        }
        // Extra originals/resources are released before final device Release;
        // their cached variants remain route-owned and must also retire.
        shared.reset();split.reset();
    }

    #include "material_glass_live_inc.h"

    // XT live admission/transport witness. Full shader mathematics and authored
    // DEFAULT UV/weight policy are qualified by the detached reference fixture.
    // Disabled DEFAULT is never submitted: its original linkage is malformed.
    void run_xt_materials(const char* original_path) {
        require(seam&&enabled&&hdr&&hdr_agx&&hdr_readback&&wrap_snapshot&&emission_status&&!taa,"XT live needs HDR/readback/WRAP seam and TAA off");
        char setting[32]{};
        const bool material=GetEnvironmentVariableA("X3M_LINEAR_MATERIALS",setting,sizeof setting)==1&&setting[0]=='1';
        const std::string supplied(original_path);const auto slash=supplied.find_last_of("/\\");require(slash!=std::string::npos,"XT program directory");
        const auto directory=supplied.substr(0,slash+1);
        const char* xt_vs[]={"37c34a7478544c14","494fe349b8bc12ec"};
        const char* xt_ps[]={"5f82ecacd39529cd","f1b0e820c7b488c3","6733b119142c8d42","496049cec2066ed3",
            "d51cf763125cb85a","31445adb0a62d134","d22f2ce2c740e6a7","1de3d2dde345a7e3","75fb9c6b05e28ea2","edaef099780fcafe",
            "fffdabd910793aba","e6794b6ec37ff71a","fd58e6b7e8cf969c","dd87737d697c6764","7c83ed50c9894e44"};
        Com<IDirect3DVertexShader9> vertex[2];Com<IDirect3DPixelShader9> pixel[15],unknown;
        for(unsigned i=0;i<2;++i){const auto code=load((directory+"vs_"+xt_vs[i]+".bin").c_str());require(fnv(code.data(),code.size()*4)==std::strtoull(xt_vs[i],nullptr,16),"XT VS identity");api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()),&vertex[i].p),"create XT VS");}
        for(unsigned i=0;i<15;++i){const auto code=load((directory+"ps_"+xt_ps[i]+".bin").c_str());require(fnv(code.data(),code.size()*4)==std::strtoull(xt_ps[i],nullptr,16),"XT PS identity");api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&pixel[i].p),"create XT PS");}
        // Whole COLOR0 is exported by D. This original synthetic PS requests
        // only COLOR0 and therefore remains a valid, uncovered native mate.
        const DWORD unknown_program[]={0xffff0300u,0x5000051u,0xa00f0000u,0x3f000000u,0x3e800000u,0x3f400000u,0x3f800000u,0x200001fu,0x8000000au,0x900f0000u,0x2000001u,0x800f0800u,0xa0e40000u,0x3000005u,0x80080800u,0xa0ff0000u,0x90ff0000u,0xffffu};
        api(d->CreatePixelShader(unknown_program,&unknown.p),"create valid XT unknown mate");
        Com<IDirect3DVertexDeclaration9> basis_declaration;
        const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT16_4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,8,D3DDECLTYPE_FLOAT16_4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,16,D3DDECLTYPE_FLOAT16_4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},{0,24,D3DDECLTYPE_FLOAT16_4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_BINORMAL,0},{0,32,D3DDECLTYPE_FLOAT16_4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TANGENT,0},D3DDECL_END()};
        api(d->CreateVertexDeclaration(elements,&basis_declaration.p),"XT basis declaration");
        Com<IDirect3DVertexBuffer9> geometry;api(d->CreateVertexBuffer(120,0,0,D3DPOOL_MANAGED,&geometry.p,nullptr),"XT geometry");
        void *source=nullptr,*target=nullptr;api(vb_a->Lock(0,0,&source,D3DLOCK_READONLY),"XT source geometry lock");api(geometry->Lock(0,0,&target,0),"XT geometry lock");
        const unsigned short basis[]={0,half(1),0,0,half(1),0,0,0};
        for(unsigned i=0;i<3;++i){auto*out=static_cast<char*>(target)+40*i;std::memcpy(out,static_cast<char*>(source)+24*i,24);std::memcpy(out+24,basis,sizeof basis);auto*uv=reinterpret_cast<unsigned short*>(out+8);uv[2]=half(.125f+.25f*i);uv[3]=half(.875f-.125f*i);}
        api(geometry->Unlock(),"XT geometry unlock");api(vb_a->Unlock(),"XT source geometry unlock");
        // Preserve consecutive identical geometry without duplicating the
        // history key: startIndex 0 and3 each address their own index triple.
        Com<IDirect3DIndexBuffer9> indices;api(d->CreateIndexBuffer(12,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&indices.p,nullptr),"XT indices");
        api(indices->Lock(0,0,&target,0),"XT index lock");const unsigned short triangle[]={0,1,2,0,1,2};std::memcpy(target,triangle,sizeof triangle);api(indices->Unlock(),"XT index unlock");
        Com<IDirect3DTexture9> maps[5];const DWORD texels[]={0x80804020u,0x80808080u,0xff000000u,0xffffffffu,0xff000000u};
        for(unsigned i=0;i<5;++i){api(d->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&maps[i].p,nullptr),"XT texture");D3DLOCKED_RECT lock{};api(maps[i]->LockRect(0,&lock,nullptr,0),"XT texture lock");for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+4*x,&texels[i],4);api(maps[i]->UnlockRect(0),"XT texture unlock");}
        const auto wrap_state=[](unsigned i){return D3DRENDERSTATETYPE(i<8?D3DRS_WRAP0+i:D3DRS_WRAP8+i-8);};
        std::uint64_t sequence=0;Object object=a;object.vb=geometry.p;object.name="XT";
        // 84 complete-pair samples, four shared-D alternations and two native
        // unknown controls, then two separate perspective transport diagnostics.
        // Planned IDs stay stable when DEFAULT off is skipped.
        for(unsigned plan=0;plan<92;++plan){
            const bool transport=plan>=90;const float perspective=transport?.125f:0.f;
            const unsigned pair=plan<84?plan/6:plan<88?(plan%2?10u:14u):plan<90?15u:plan==90?0u:10u;
            const unsigned step=plan<84?plan%6:plan<88?plan-84:plan<90?plan-88:6u;
            const bool bump=pair<10,corrected=pair>=10&&pair<14,legacy=pair==14,native_unknown=pair==15;
            const bool reset_after=plan==3||plan==63;
            if(corrected&&!material){std::printf("XT_SKIP plan=%u pair=%u step=%u reason=native_invalid_linkage\n",plan,pair,step);if(reset_after)reset();continue;}
            frame_begin();write_reserved();
            const bool fallback=plan<84&&step==2,combined=material&&!fallback&&!native_unknown;
            const bool post_reset=plan==4||plan==64;
            const bool new_object=(plan<84&&step==0)||plan==84||plan==88||transport;
            if(new_object){object.scope.node_serial=8000+(plan<84?pair:plan);object.scope.node=0x800000+unsigned(object.scope.node_serial)*0x100;object.scope.mesh=0x900000+unsigned(object.scope.node_serial)*0x100;object.recorded=false;}
            if(post_reset)object.recorded=false;
            const bool matched=object.recorded&&!native_unknown;
            // Finite witness: zero lighting/specular/palette/O RGB and a unit
            // lightmap. Ordinary RGB=1; linear RGB=encode_gamma22(4). These
            // values make hardware sampler decode refusal an RGB identity.
            float vc[8][4]{};if(bump){vc[0][0]=1;vc[1][0]=.625f;vc[3][0]=2;vc[4][0]=.1f;vc[5][0]=1;vc[6][0]=12;vc[7][0]=.9f;}else{vc[0][0]=.625f;vc[2][0]=1;for(unsigned i=3;i<8;++i)vc[i][0]=777.f+float(i);}
            if(transport&&bump)vc[0][0]=.125f;
            if(transport){
                // Exact half inputs: V endpoints (0,0,1),(-4,0,1),(0,4,1).
                // q^12=(1,17^-6,17^-6): WRAP6.Y crosses a physical seam.
                const float camera[3][4]={{1,0,0,-1},{0,1,0,1},{0,0,1,1.5f}};
                api(d->SetVertexShaderConstantF(34,camera[0],3),"XT calibrated camera");
                const float point[3][4]={{0,0,4,0},{1,1,1,0},{16,16,16,0}};
                api(d->SetVertexShaderConstantF(0,point[0],3),"XT calibrated point");
            }
            api(d->SetVertexShaderConstantF(39,vc[0],8),"XT VS owned constants");int lights[4]={transport?1:0,0,1,0};api(d->SetVertexShaderConstantI(0,lights,1),"XT point count");
            float pc[24][4]{};pc[0][0]=pc[1][1]=pc[2][2]=1;pc[3][0]=.5f;pc[4][0]=.25f;pc[5][2]=pc[7][2]=1;pc[9][0]=1;pc[10][0]=10;pc[11][0]=1;pc[12][0]=1;pc[13][0]=bump?1.f:0.f;
            if(bump){pc[14][0]=0;pc[15][0]=1;pc[16][0]=0;pc[22][0]=9;pc[23][0]=.5f;}else{pc[19][0]=9;pc[20][0]=.5f;}
            if(legacy){std::memset(pc,0,sizeof pc);pc[0][0]=pc[1][1]=pc[2][2]=1;pc[3][0]=.25f;pc[4][2]=pc[6][2]=1;for(unsigned i=8;i<12;++i)pc[i][0]=i==9?10.f:1.f;}
            if(transport){const unsigned palette=bump?17:14;const float colors[5][3]={{1,0,0},{0,1,0},{0,0,1},{1,1,0},{0,1,1}};pc[9][0]=pc[11][0]=.125f;for(unsigned i=0;i<5;++i)for(unsigned lane=0;lane<3;++lane)pc[palette+i][lane]=colors[i][lane];}
            api(d->SetPixelShaderConstantF(0,pc[0],24),"XT pixel constants");
            BOOL branches[2]={step==1||transport,step==1||transport};api(d->SetPixelShaderConstantB(0,branches,2),"XT original branch choices");
            if(transport){
                // All decoded RGB inputs are endpoints. LM/emissive are zero,
                // so ordinary working RGB equals linear working RGB even with
                // gain four. One attenuated white point stays below COLOR0's
                // native clamp: 1/(16+16/d+16/d^2) is at most 1/16.
                const DWORD calibrated[]={0x80ffffffu,0x80808080u,0xffffffffu,0xff000000u,0xff000000u};
                for(unsigned i=0;i<5;++i){D3DLOCKED_RECT lock{};api(maps[i]->LockRect(0,&lock,nullptr,0),"XT calibrated map lock");for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+4*x,&calibrated[i],4);api(maps[i]->UnlockRect(0),"XT calibrated map unlock");}
                const DWORD faces[]={0xffff0000u,0xff00ff00u,0xff0000ffu,0xffffff00u,0xff00ffffu,0xffff00ffu};
                for(unsigned face=0;face<6;++face){D3DLOCKED_RECT lock{};api(cube->LockRect(D3DCUBEMAP_FACES(face),0,&lock,nullptr,0),"XT calibrated cube lock");for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+4*x,&faces[face],4);api(cube->UnlockRect(D3DCUBEMAP_FACES(face),0),"XT calibrated cube unlock");}
            }
            IDirect3DBaseTexture9* samplers[7]={maps[0].p,bump?maps[1].p:maps[2].p,bump?maps[2].p:maps[3].p,bump?static_cast<IDirect3DBaseTexture9*>(maps[3].p):cube.p,bump?static_cast<IDirect3DBaseTexture9*>(cube.p):maps[4].p,bump?maps[4].p:nullptr,bump?maps[1].p:nullptr};
            if(legacy){samplers[1]=maps[2].p;samplers[2]=maps[3].p;samplers[3]=cube.p;samplers[4]=nullptr;}
            for(unsigned i=0;i<7;++i){api(d->SetTexture(i,samplers[i]),"XT sampler role");for(auto filter:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})api(d->SetSamplerState(i,filter,D3DTEXF_POINT),"XT point sampling");api(d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE),"XT no mip");api(d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP),"XT clamp u");api(d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP),"XT clamp v");if(!post_reset)api(d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE),"XT decode false");}
            const unsigned refusal_stage=bump?5:4;if(fallback)api(d->SetSamplerState(refusal_stage,D3DSAMP_SRGBTEXTURE,TRUE),"XT color sampler refusal");
            DWORD caller[16]{};if(!post_reset){caller[0]=13;caller[1]=step%2?11:5;caller[2]=step%2?6:10;caller[5]=7;caller[6]=transport?2:step%2?2:1;caller[bump?7:4]=15;caller[bump?8:7]=15;for(unsigned i=0;i<16;++i)api(d->SetRenderState(wrap_state(i),caller[i]),"XT hostile WRAP");}
            if(plan<84&&step==3){api(d->BeginStateBlock(),"XT begin recorded");api(d->SetSamplerState(refusal_stage,D3DSAMP_SRGBTEXTURE,TRUE),"XT record decode");Com<IDirect3DStateBlock9>recorded;api(d->EndStateBlock(&recorded.p),"XT end recorded");DWORD value=1;api(d->GetSamplerState(refusal_stage,D3DSAMP_SRGBTEXTURE,&value),"XT record remains unapplied");require(value==FALSE,"XT recorded decode no mutation");Com<IDirect3DStateBlock9>all;api(d->CreateStateBlock(D3DSBT_ALL,&all.p),"XT capture all");api(d->SetSamplerState(refusal_stage,D3DSAMP_SRGBTEXTURE,TRUE),"XT transient decode");api(d->SetRenderState(wrap_state(0),0),"XT transient WRAP");api(all->Apply(),"XT Apply restore");}
            // Hostile shader-reserved device constants must survive even though
            // local DEFs inside the transformed programs override their reads.
            float vr[8][4],pr[11][4];for(unsigned i=0;i<32;++i)vr[i/4][i%4]=100.f+i;for(unsigned i=0;i<44;++i)pr[i/4][i%4]=200.f+i;
            api(d->SetVertexShaderConstantF(244,vr[0],8),"XT poison VS reserved");api(d->SetPixelShaderConstantF(210,pr[0],11),"XT poison PS reserved");write_reserved();
            api(d->SetIndices(indices.p),"XT bind indices");api(d->SetVertexDeclaration(basis_declaration.p),"XT bind basis");scope(&object);api(d->SetStreamSource(0,object.vb,0,40),"XT bind stream");api(d->SetVertexShader(vertex[bump?0:1].p),"XT original VS bind");api(d->SetPixelShader(native_unknown?unknown.p:pixel[pair].p),"XT original PS bind");rows(0,perspective,0);
            float before_ps[11][4];api(d->GetPixelShaderConstantF(210,before_ps[0],11),"XT reserved snapshot");const Snapshot before=snapshot();
            std::array<std::uint64_t,3> transport_baseline{};std::vector<float> transport_rgb;
            for(unsigned draw_number=0;draw_number<2;++draw_number){if(transport)api(d->SetSamplerState(refusal_stage,D3DSAMP_SRGBTEXTURE,draw_number==0),"XT paired ordinary then linear admission");api(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,3*draw_number,1),"XT original source draw");++draw_index;records.push_back({&object,0,perspective,0,!native_unknown,matched,object.rt,object.rp,object.rzo,false,jitter,true});x3m::MotionOutputFixtureWrapSnapshot observed{};api(wrap_snapshot(d.p,&observed),"XT native WRAP snapshot");++sequence;
                DWORD expected[16];std::memcpy(expected,caller,sizeof expected);if(!native_unknown){expected[bump?7:4]=0;if(materialwrap_depth)expected[bump?8:7]=0;if(combined&&bump&&(!transport||draw_number)){expected[1]=(caller[1]&7)|((caller[6]&1)?8:0);expected[2]=(caller[2]&7)|((caller[6]&2)?8:0);}}
                std::printf("XT_WRAP plan=%u frame=%llu draw=%u sequence=%llu valid=%u result=%08lx values=",plan,frame,draw_number,static_cast<unsigned long long>(observed.sequence),observed.valid,observed.result);for(unsigned i=0;i<16;++i)std::printf("%s%lu",i?",":"",static_cast<unsigned long>(observed.values[i]));std::puts("");require(observed.valid&&observed.result==S_OK&&observed.sequence==sequence&&!std::memcmp(expected,observed.values,sizeof expected),"XT native-once and pair WRAP transport");require(emission_status(d.p,12)==draw_number+1,"XT original source submitted once");
                if(transport){
                    // Deliberate getters separate only these final diagnostics.
                    // Both source draws have the same unmatched history and
                    // projective matrix; alpha/depth/motion must be exact twins.
                    unsigned w=0,h=0;const auto image=hdr_image(&w,&h);require(w==W&&h==H,"XT paired image dimensions");
                    std::vector<float>alpha_image(std::size_t(W)*H),motion_image(std::size_t(W)*H*4),depth_image(std::size_t(W)*H);
                    for(std::size_t i=0;i<alpha_image.size();++i)alpha_image[i]=image[4*i+3];
                    for(float value:image)require_quiet(std::isfinite(value),"XT paired finite image");
                    api(readback(d.p,motion_image.data(),unsigned(motion_image.size()),&w,&h),"XT paired motion readback");if(materialwrap_depth)api(readback_depth(d.p,depth_image.data(),unsigned(depth_image.size()),&w,&h),"XT paired depth readback");
                    const std::array<std::uint64_t,3> hashes={fnv(alpha_image.data(),alpha_image.size()*4),fnv(motion_image.data(),motion_image.size()*4),materialwrap_depth?fnv(depth_image.data(),depth_image.size()*4):0};
                    unsigned pixels=0;double max_error=0,low=1e30,high=-1e30;
                    if(!draw_number){transport_baseline=hashes;transport_rgb=image;}
                    else {
                        require(hashes==transport_baseline,"XT perspective ordinary/linear alpha and temporal twins");
                        const double alpha=.625*(.25+.75*128./255.);
                        for(std::size_t i=0;i<alpha_image.size();++i){
                            if(std::fabs(transport_rgb[4*i+3]-alpha)>.001)continue;
                            ++pixels;
                            for(unsigned lane=0;lane<3;++lane){const double ordinary=transport_rgb[4*i+lane];require_quiet(ordinary>=0,"XT calibrated positive working RGB");low=std::min(low,ordinary);high=std::max(high,ordinary);const double wanted=material?std::pow(ordinary,1./2.2):ordinary;const double error=std::fabs(image[4*i+lane]-wanted)/(.006*std::fabs(wanted)+.00002);max_error=std::max(max_error,error);}
                        }
                        require(pixels>W*H/4&&high-low>.001&&max_error<=1,"XT physical scalar WRAP calibrated RGB relation");
                    }
                    const double highlight_low=std::pow(17.,-6.);
                    require(1-highlight_low>.5,"XT authored highlight crosses WRAP seam");
                    std::printf("XT_TRANSPORT plan=%u frame=%llu draw=%u combined=%u alpha=%016llx motion=%016llx depth=%016llx perspective=0.125 pixels=%u max_error=%.9g rgb_range=%.9g highlight_low=%.9g highlight_high=1\n",plan,frame,draw_number,material&&draw_number,static_cast<unsigned long long>(hashes[0]),static_cast<unsigned long long>(hashes[1]),static_cast<unsigned long long>(hashes[2]),pixels,max_error,draw_number?high-low:0,highlight_low);
                }
            }
            compare(before,snapshot(),"XT state after consecutive pair draws");float after_vs[8][4],after_ps[11][4];api(d->GetVertexShaderConstantF(244,after_vs[0],8),"XT VS reserved readback");api(d->GetPixelShaderConstantF(210,after_ps[0],11),"XT PS reserved readback");require(!std::memcmp(vr,after_vs,sizeof vr)&&!std::memcmp(before_ps,after_ps,sizeof before_ps),"XT all reserved constants restored");
            object.recorded=true;object.rt=object.rzo=0;object.rp=perspective;
            unsigned width=0,height=0;const auto image=hdr_image(&width,&height);require(width==W&&height==H,"XT FP16 dimensions");const float*center=&image[(std::size_t(H/2)*W+W/2)*4];
            const double alpha=.625*(.25+.75*128./255.);if(!transport)for(unsigned lane=0;lane<3;++lane){const double expected=native_unknown?(lane==0?.5:lane==1?.25:.75):combined?std::pow(4.,1./2.2):1.;require(std::isfinite(center[lane])&&std::fabs(center[lane]-expected)<.005,"XT actual ordinary/linear RGB witness");}require(std::fabs(center[3]-(native_unknown?.625:alpha))<.001,"XT preserved native alpha");
            for(float value:image)require_quiet(std::isfinite(value),"XT finite full FP16 image");
            std::vector<float>alpha_pixels(std::size_t(W)*H);for(std::size_t i=0;i<alpha_pixels.size();++i)alpha_pixels[i]=image[i*4+3];
            std::printf("XT_LIVE plan=%u frame=%llu pair=%u step=%u vs=%s ps=%s combined=%u refusal=%u matched=%u rgba=%.9g,%.9g,%.9g,%.9g alpha_hash=%016llx image_hash=%016llx\n",plan,frame,pair,step,xt_vs[bump?0:1],native_unknown?"fed278e46915d6da":xt_ps[pair],combined,native_unknown?1u:fallback?4u:0u,matched,double(center[0]),double(center[1]),double(center[2]),double(center[3]),static_cast<unsigned long long>(fnv(alpha_pixels.data(),alpha_pixels.size()*4)),static_cast<unsigned long long>(fnv(image.data(),image.size()*4)));
            frame_end();if(reset_after){reset();object.recorded=false;}
        }
    }

    // ---- FP16 HDR scene path, stage 3 (X3M_HDR=1 with X3M_TAA=1) ----
    // The reference resolves the same FP16 scene the DLL resolves (the target
    // read through the seam before the boundary; the flush of the fixture's
    // main-target read has already written the unresolved scene back) with
    // the k the DLL derived at this frame's latch (the exposure export's last
    // field). Returns k; 0 without the HDR switch.
    float hdr_reference_input() {
        if (!hdr) return 0.f;
        // Preconditions, not counted checks: the twins compare check counts.
        if (!hdr_readback || !hdr_exposure) throw std::runtime_error("the HDR TAA reference needs the readback and exposure exports");
        unsigned w = W, h = H; const auto image = emissions ? emission_reference_color : hdr_image(&w, &h);
        if (w != W || h != H) throw std::runtime_error("the FP16 target does not have the frame size");
        reference.upload16(image);
        float e[8]{}; api(hdr_exposure(d.p, e, 8), "hdr exposure readback (k)");
        return e[7];
    }
    // The presented 8-bit image against the reference's copy-back. 8-bit
    // route: byte for byte. HDR route: the DLL's identity draw and the
    // reference's StretchRect convert the same resolved FP16 values (the
    // FP16 outputs themselves are compared byte for byte by the runner), so
    // one code per channel is the tolerance (the conversions' rounding);
    // with the AgX write-back only the alpha carry is checked here and the
    // runner compares the colour against the Python AgX reference.
    // With the post-resolve sharpen on (either route) only the alpha carry
    // is checked here, like the AgX case: the runner compares the colour
    // against the Python RCAS reference of the resolved (and, HDR, AgX
    // tonemapped) image, and the FP16 history against the reference pass.
    unsigned presented_mismatches(const std::vector<DWORD>& actual, const std::vector<DWORD>& expected, float k) {
        unsigned mismatches = 0, worst = 0;
        const bool colour = !hdr_agx && sharpen <= 0.f;
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (!hdr && colour) { if (expected[i] != actual[i] && ++mismatches <= 4) std::printf("TAA_DIFF frame=%llu index=%u actual=%08lx reference=%08lx\n", frame, unsigned(i), actual[i], expected[i]); continue; }
            unsigned difference = 0;
            for (unsigned c = 0; c < 4; ++c) {
                const unsigned a = (actual[i] >> (8 * c)) & 255, e = (expected[i] >> (8 * c)) & 255;
                if (c == 3 || colour) difference = std::max(difference, a > e ? a - e : e - a);
            }
            worst = std::max(worst, difference);
            if (difference > 1 && ++mismatches <= 4) std::printf("TAA_DIFF frame=%llu index=%u actual=%08lx reference=%08lx\n", frame, unsigned(i), actual[i], expected[i]);
        }
        if (hdr) std::printf("TAA_HDR frame=%llu k=%.5f agx=%u worst_code=%u mismatches=%u\n", frame, double(k), hdr_agx, worst, mismatches);
        if (sharpen > 0.f) std::printf("TAA_SHARPEN frame=%llu sharpen=%g hdr=%u agx=%u worst_alpha=%u mismatches=%u\n", frame, double(sharpen), hdr, hdr_agx, worst, mismatches);
        return mismatches;
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
    // The state the DLL's tonemap consumes this frame (after the latch): the
    // EVs, the old 1x1 statistic (avg_log_l) and the space-aware one (the
    // lit fraction, the lit median, the p99 maximum, ev_key, ev_limit, the
    // tile counts, the lit mean; all log2 units).
    void exposure_state_line(const char* tag) {
        if (!hdr_exposure) return;
        float e[16]{}; api(hdr_exposure(d.p, e, 16), "hdr exposure readback");
        std::printf("%s frame=%llu ev=%.6f ev_adapted=%.6f ev_target=%.6f avg_log_l=%.6f dt=%.6f exposure=%.6f steps=%u k=%.6f"
                    " lit_fraction=%.6f lit_median_log=%.6f p99_max_log=%.6f ev_key=%.6f ev_limit=%.6f tiles=%u lit=%u lit_mean_log=%.6f\n",
                    tag, frame, e[0], e[1], e[2], e[3], e[4], e[5], unsigned(e[6]), e[7],
                    e[8], e[9], e[10], e[11], e[12], unsigned(e[13]), unsigned(e[14]), e[15]);
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
        // Finite values print with %.9g; a non-finite value prints its IEEE
        // bits (0x%08x): under FEX's reduced-precision x87 the CRT prints NaN
        // and the infinities as finite numbers (docs/verification/bottles.md,
        // limitation 2), and the runner's parse_float decodes the hex form.
        char text[4][80];
        for (unsigned i = 0; i < 4; ++i) {
            char* p = text[i];
            for (unsigned c = 0; c < 4; ++c) {
                const float v = blocks[i][c];
                if (std::isfinite(v)) p += std::snprintf(p, std::size_t(text[i] + sizeof text[i] - p), "%s%.9g", c ? "," : "", double(v));
                else { unsigned bits; std::memcpy(&bits, &v, 4); p += std::snprintf(p, std::size_t(text[i] + sizeof text[i] - p), "%s0x%08x", c ? "," : "", bits); }
            }
        }
        std::printf("EXPOSURE_BLOCKS frame=%llu b0=%s b1=%s b2=%s b3=%s\n", frame, text[0], text[1], text[2], text[3]);
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
    // hdrexposure, the space-aware scenes: a background value over the whole
    // target and up to eight rectangular patches of constant engine-space
    // values (pixel rectangles, [x0, x1) x [y0, y1)); the description, the
    // state the tonemap consumed and the presented background/patch witnesses
    // are printed like the block frames.
    struct Patch { unsigned x0, y0, x1, y1; float rgba[4]; };
    struct Scene { float bg[4]; unsigned count; Patch patches[8]; };
    static void format_rgba(char* p, std::size_t size, const float* v) {
        char* end = p + size;
        for (unsigned c = 0; c < 4; ++c) {
            if (std::isfinite(v[c])) p += std::snprintf(p, std::size_t(end - p), "%s%.9g", c ? "," : "", double(v[c]));
            else { unsigned bits; std::memcpy(&bits, &v[c], 4); p += std::snprintf(p, std::size_t(end - p), "%s0x%08x", c ? "," : "", bits); }
        }
    }
    void hdrexposure_scene_frame(const Scene& s) {
        frame_begin();
        exposure_state_line("EXPOSURE_STATE");
        constant_state();
        constant_quad(0, 0, float(W), float(H), s.bg[0], s.bg[1], s.bg[2], s.bg[3]);
        for (unsigned i = 0; i < s.count; ++i) {
            const Patch& q = s.patches[i];
            constant_quad(float(q.x0), float(q.y0), float(q.x1), float(q.y1), q.rgba[0], q.rgba[1], q.rgba[2], q.rgba[3]);
        }
        char text[80]; format_rgba(text, sizeof text, s.bg);
        std::printf("EXPOSURE_SCENE frame=%llu bg=%s patches=%u", frame, text, s.count);
        for (unsigned i = 0; i < s.count; ++i) {
            const Patch& q = s.patches[i]; format_rgba(text, sizeof text, q.rgba);
            std::printf(" p%u=%u,%u,%u,%u,%s", i, q.x0, q.y0, q.x1, q.y1, text);
        }
        std::printf("\n");
        api(d->EndScene(), "EndScene");
        const auto image = color_image();
        std::printf("COLOR frame=%llu hash=%016llx\n", frame, static_cast<unsigned long long>(color_hash(image)));
        write_presented(image);
        // A later rectangle can cover an earlier rectangle's centre. Select
        // a witness from the final visible footprint of every region instead;
        // geometric ownership is independent of the observed colour value.
        DWORD codes[9]{}; bool seen[9]{};
        unsigned visible = 0, nonuniform = 0;
        auto region = [&](unsigned x, unsigned y) {
            unsigned owner = 0;
            for (unsigned i = 0; i < s.count; ++i)
                if (x >= s.patches[i].x0 && x < s.patches[i].x1 && y >= s.patches[i].y0 && y < s.patches[i].y1) owner = i + 1;
            return owner;
        };
        for (unsigned y = 0; y < H; ++y) for (unsigned x = 0; x < W; ++x) {
            const unsigned owner = region(x, y);
            if (!seen[owner]) { codes[owner] = image[std::size_t(y) * W + x]; seen[owner] = true; ++visible; }
            nonuniform += image[std::size_t(y) * W + x] != codes[owner];
        }
        std::printf("EXPOSURE_SCENE_PRESENTED frame=%llu bg=%08lx", frame, codes[0]);
        for (unsigned i = 0; i < s.count; ++i) std::printf(" p%u=%08lx", i, codes[i + 1]);
        std::printf(" nonuniform=%u\n", nonuniform);
        require(!nonuniform && visible == s.count + 1, "the background and every visible patch present one code");
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
        // The space-aware scenes (the tile image is 16x16 tiles of 4x4 pixels):
        // 40-49 a black sky with one 16x16 lit patch at 0.3 (16 of 256 tiles
        // lit: the key rule lifts the patch to the key, +1.35 EV, where the
        // whole-frame log mean would have asked for +8); 50-59 a white
        // full frame, the menu (pulled down by key_pull to -0.62 EV); 60-69
        // mid-grey with 0.5 % super-bright pixels in five tiles (the p99 tile
        // maximum is the clip: the highlight limit holds -2.13 EV although the
        // key rule asks for +2); 70-79 uniform mid-grey (the key rule: +2.97,
        // clamped to +2, as the 1x1 meter would have said).
        const Scene sky{{0, 0, 0, 1}, 1, {{8, 8, 24, 24, {.3f, .3f, .3f, 1}}}};
        const Scene menu{{1, 1, 1, 1}, 0, {}};
        const Scene sparks{{.18f, .18f, .18f, 1}, 5, {{4, 4, 6, 6, {100, 100, 100, 1}}, {24, 16, 26, 18, {100, 100, 100, 1}}, {20, 32, 22, 34, {100, 100, 100, 1}},
                                                     {16, 48, 18, 50, {100, 100, 100, 1}}, {56, 56, 58, 58, {100, 100, 100, 1}}}};
        const Scene grey{{.18f, .18f, .18f, 1}, 0, {}};
        for (unsigned i = 0; i < 10; ++i) hdrexposure_scene_frame(sky);
        for (unsigned i = 0; i < 10; ++i) hdrexposure_scene_frame(menu);
        for (unsigned i = 0; i < 10; ++i) hdrexposure_scene_frame(sparks);
        for (unsigned i = 0; i < 10; ++i) hdrexposure_scene_frame(grey);
        // The dead band (80-109): uniform frames whose key-rule targets sit
        // inside the range, 0.38 (+0.60 EV), then 0.40 (+0.43) and 0.37 (+0.68),
        // within 0.25 EV of the held target (it must not move), then 0.6
        // (-0.21: it must). The emitter (110-119): a white left half over
        // black with a mid-grey object (engine 0.459: decoded 0.18, the key)
        // in the centre; the centre weighting keeps the object at the key
        // (EV 0) where the unweighted median would sit on the emitter (-0.62).
        // After the preceding +2 clamp, A must move by >0.25 even in the
        // +1-offset twin. Its fresh target is ~0.597 (1.597 with offset),
        // then B/C are within the band (~0.434/~0.682); D moves beyond it.
        const Scene level_a{{.38f, .38f, .38f, 1}, 0, {}}, level_b{{.4f, .4f, .4f, 1}, 0, {}}, level_c{{.37f, .37f, .37f, 1}, 0, {}}, level_d{{.6f, .6f, .6f, 1}, 0, {}};
        for (unsigned i = 0; i < 10; ++i) hdrexposure_scene_frame(level_a);
        for (unsigned i = 0; i < 5; ++i) hdrexposure_scene_frame(level_b);
        for (unsigned i = 0; i < 5; ++i) hdrexposure_scene_frame(level_c);
        for (unsigned i = 0; i < 10; ++i) hdrexposure_scene_frame(level_d);
        const Scene emitter{{0, 0, 0, 1}, 2, {{0, 0, 32, 64, {1, 1, 1, 1}}, {16, 16, 48, 48, {.459f, .459f, .459f, 1}}}};
        for (unsigned i = 0; i < 10; ++i) hdrexposure_scene_frame(emitter);
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
        if (fault == 14) resolve_expected = false; // stage 3: the resolve on the FP16 scene fails; the write-back presents the unresolved scene
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
        // f2 readback UnlockRect reports failure AFTER its real cleanup (fault
        // 15): exposure must hold; f3 succeeds at readback but its meter draw
        // fails (tonemap applied, no step next); f4 normal;
        // f5, f6 tonemap draw fails again (the third failure disables the
        // tonemap for the device); f7, f8 identity from then on.
        const unsigned script[] = {0, 11, 15, 13, 0, 11, 11, 0, 0};
        // With X3M_TAA=1 (stage 3): f1 and f7 the resolve on the FP16 scene
        // fails (fault 14: the unresolved scene is written back, the history
        // drops, f2/f8 resolve current-only); f3 the tonemap draw fails
        // (identity fallback samples the resolved image); f5 the meter fails.
        const unsigned taa_script[] = {0, 14, 0, 11, 0, 13, 0, 14, 0};
        const unsigned* frames = taa ? taa_script : script;
        for (unsigned i = 0; i < 9; ++i) hdrtonemapfault_frame(frames[i]);
        if (!taa) return;
        // A Reset with the resolve on the FP16 scene: the target, the pass's
        // histories and the exposure state go; f9 resolves current-only on
        // the re-created target, f10/f11 accumulate again.
        reset();
        for (unsigned i = 0; i < 3; ++i) hdrtonemapfault_frame(0);
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
        if (wrap) api(d->SetRenderState(D3DRS_WRAP4, 3), "WRAP4 before StateBlock Apply");
        api(restore_block->Apply(), "StateBlock Apply (blend off again)");
    }
    // Render-state shadow: a write recorded between BeginStateBlock and
    // EndStateBlock never reaches the device; the block is dropped unapplied.
    // The draws that follow (frame 8) must still pass gate 4 with z writes on.
    void recorded_write_case() {
        api(d->BeginStateBlock(), "BeginStateBlock");
        api(d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE), "recorded z-write off (not applied)");
        if (wrap) api(d->SetRenderState(D3DRS_WRAP4, 0), "recorded WRAP4 (not applied)");
        Com<IDirect3DStateBlock9> recorded; api(d->EndStateBlock(&recorded.p), "EndStateBlock");
        DWORD value = 1; api(d->GetRenderState(D3DRS_ZWRITEENABLE, &value), "GetRenderState after recording");
        require(value == TRUE, "a recorded render-state write does not reach the device");
        if (wrap) { api(d->GetRenderState(D3DRS_WRAP4, &value), "WRAP4 after recording"); require(value == 15, "recorded WRAP4 leaves application state intact"); }
    }
    void recreate_shaders() {
        api(d->SetVertexShader(nullptr), "unbind vs"); api(d->SetPixelShader(nullptr), "unbind ps");
        vs.reset(); ps.reset();
        create_shaders();
    }
    // ---- multisampled main target ("msaa" script; D3 of the native-Windows audit) ----
    // Three plain frames on a device whose back buffer carries
    // X3M_FIXTURE_MSAA samples (default 2): the selector never latches it
    // (latched=0, state Rejected); the route refuses it by name once in the
    // log (motion_output_msaa_refused), routes and
    // jitters nothing, the resolve skips (taa_skip 11). No readback: a
    // multisampled surface cannot be read with GetRenderTargetData, and the
    // runner reads the DLL's per-frame lines instead.
    void run_msaa() {
        require(enabled && msaa, "msaa needs the route and the multisampled device");
        for (unsigned i = 0; i < 3; ++i) {
            frame_begin();
            draw(a, .75f, 0, 0, true, false, false);
            draw(b, 0, 0, 0, true, false, false);
            api(d->EndScene(), "EndScene");
            api(d->Present(nullptr, nullptr, nullptr, nullptr), "Present");
            ++frame; ++frames_since_reset;
        }
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
#include "motion_output_emission_inc.h"
#include "motion_output_distance_fade_inc.h"
#include "motion_output_screen_emission_inc.h"
#include "motion_output_cutout_inc.h"
#include "motion_output_fade_route_inc.h"
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
        if ((argc != 4 && argc != 5 && argc != 6 && argc != 9) || !window || !runtime) throw std::runtime_error("usage: fixture <vs.bin> <ps.bin> production|seam|bench|burst|mipbias|zonly|envmap|hook|aohook|hdrvalues|hdrfault|hdrramp|hdrexposure|hdrtonemapfault|msaa|linearmaterials|materialwrap|materialxt|materialglass|emissions|emissionsbench|distancefade|distancefadebench|screenemission|screenemissionbench|cutout|cutoutbench|faderoute [WxH|shared-PS Split-PS BUMP-VS BUMP-PS BUMP-negative-PS]");
        Fixture f;
        f.runtime = runtime; f.window = window;
        const std::string mode = argv[3];
        f.bench = mode == "bench";
        f.burst = mode == "burst";
        f.mipbias = mode == "mipbias";
        f.zonly = mode == "zonly";
        f.envmap = mode == "envmap";
        f.hook = mode == "hook";
        f.aohook = mode == "aohook";
        f.hdrvalues = mode == "hdrvalues";
        f.hdrfault = mode == "hdrfault";
        f.hdrramp = mode == "hdrramp"; f.hdrexposure = mode == "hdrexposure"; f.hdrtonemapfault = mode == "hdrtonemapfault";
        f.msaa = mode == "msaa";
        f.materialwrap = mode == "materialwrap"; f.materialxt = mode == "materialxt"; f.materialglass = mode == "materialglass";
        f.linearmaterials = mode == "linearmaterials" || f.materialwrap;
        f.cutout_bench = mode == "cutoutbench"; f.cutout = mode == "cutout" || f.cutout_bench;
        f.faderoute = mode == "faderoute";
        f.distancefade_bench = mode == "distancefadebench";
        f.distancefade = mode == "distancefade" || f.distancefade_bench;
        f.screenemission_bench = mode == "screenemissionbench";
        f.screenemission = mode == "screenemission" || f.screenemission_bench;
        f.emission_bench = mode == "emissionsbench" || f.distancefade_bench || f.screenemission_bench;
        f.emissions = mode == "emissions" || f.emission_bench || f.distancefade || f.screenemission || f.cutout || f.faderoute;
        if(f.emission_bench&&!f.distancefade&&!f.screenemission){Fixture::W=1920;Fixture::H=1080;}
        if(f.faderoute && argc!=4)throw std::runtime_error("faderoute uses bootstrap originals");
        if(f.emissions && !f.distancefade && !f.screenemission && !f.cutout && !f.faderoute && argc!=6)throw std::runtime_error("emissions needs original emission VS/PS paths");
        if(f.screenemission && argc!=(f.screenemission_bench?5:4))throw std::runtime_error("screenemission uses bootstrap originals and optional benchmark WxH");
        if(f.cutout && argc!=(f.cutout_bench?5:4))throw std::runtime_error("cutout uses bootstrap originals and optional benchmark WxH");
        if(f.distancefade && argc!=(f.distancefade_bench?5:4))throw std::runtime_error("distancefade uses bootstrap originals and optional benchmark WxH");
        if(f.linearmaterials && argc!=9)throw std::runtime_error("linearmaterials needs DEFAULT and BUMP positive/negative shader paths");
        if(!f.emissions && argc==6)throw std::runtime_error("unexpected emission program paths");
        if(!f.linearmaterials && argc==9)throw std::runtime_error("unexpected additional program path");
        if (f.msaa) { char samples[8]{}; f.msaa_samples = GetEnvironmentVariableA("X3M_FIXTURE_MSAA", samples, sizeof samples) > 0 ? unsigned(std::atoi(samples)) : 2u; if (f.msaa_samples < 2 || f.msaa_samples > 16) throw std::runtime_error("X3M_FIXTURE_MSAA must be 2..16"); }
        if (f.hdrramp) { Fixture::W = 64; Fixture::H = ramp_rows; }
        if (f.bench || f.distancefade_bench || f.screenemission_bench || f.cutout_bench) {
            unsigned w = 0, h = 0;
            if (argc != 5 || std::sscanf(argv[4], "%ux%u", &w, &h) != 2 || !w || !h || w > 8192 || h > 8192) throw std::runtime_error("bench needs WxH");
            Fixture::W = w; Fixture::H = h;
        }
        f.configure = symbol<void (*)(const x3m::MotionOutputFixtureConfig*)>(runtime, "x3m_motion_output_fixture_configure", false);
        f.readback = symbol<HRESULT (*)(IDirect3DDevice9*, float*, unsigned, unsigned*, unsigned*)>(runtime, "x3m_motion_output_fixture_readback", false);
        f.readback_depth = symbol<HRESULT (*)(IDirect3DDevice9*, float*, unsigned, unsigned*, unsigned*)>(runtime, "x3m_motion_output_fixture_readback_depth", false);
        f.wrap_snapshot = symbol<HRESULT (*)(IDirect3DDevice9*,x3m::MotionOutputFixtureWrapSnapshot*)>(runtime,"x3m_motion_output_fixture_wrap_snapshot",false);
        f.last_pixel_abi = symbol<HRESULT (*)(IDirect3DDevice9*, float*, unsigned)>(runtime, "x3m_motion_output_fixture_last_pixel_abi", false);
        f.camera_install = symbol<void (*)(const float* const*, const float* const*)>(runtime, "x3m_camera_state_fixture_install", false);
        f.hook_install = symbol<int (*)(void*, void*)>(runtime, "x3m_scene_hook_fixture_install", false);
        f.hook_shutdown = symbol<int (*)()>(runtime, "x3m_scene_hook_fixture_shutdown", false);
        f.hook_signals = symbol<unsigned (*)()>(runtime, "x3m_scene_hook_fixture_signals", false);
        f.hook_status = symbol<const char* (*)()>(runtime, "x3m_scene_hook_fixture_status", false);
        f.hdr_fault = symbol<void (*)(IDirect3DDevice9*, unsigned, unsigned)>(runtime, "x3m_hdr_fixture_fault", false);
        f.hdr_readback = symbol<HRESULT (*)(IDirect3DDevice9*, float*, unsigned, unsigned*, unsigned*)>(runtime, "x3m_hdr_fixture_readback", false);
        f.hdr_exposure = symbol<HRESULT (*)(IDirect3DDevice9*, float*, unsigned)>(runtime, "x3m_hdr_fixture_exposure", false);
        f.emission_status = symbol<unsigned (*)(IDirect3DDevice9*, unsigned)>(runtime,"x3m_linear_emission_fixture_status",false);
        f.emission_fault = symbol<void (*)(IDirect3DDevice9*, unsigned, unsigned)>(runtime,"x3m_linear_emission_fixture_fault",false);
        f.emission_readback = symbol<HRESULT (*)(IDirect3DDevice9*, unsigned, float*, unsigned, unsigned*, unsigned*)>(runtime,"x3m_motion_output_fixture_readback_target",false);
        f.seam = f.configure && f.readback && f.readback_depth && f.last_pixel_abi && f.camera_install;
        require(f.bench || f.burst || f.mipbias || f.zonly || f.envmap || f.hook || f.aohook || f.hdrvalues || f.hdrfault || f.hdrramp || f.hdrexposure || f.hdrtonemapfault || f.msaa || f.linearmaterials || f.materialxt || f.materialglass || f.emissions || f.seam == (mode == "seam"), "DLL seam presence matches the requested mode");
        char setting[8]{}; f.enabled = GetEnvironmentVariableA("X3M_MOTION_OUTPUT", setting, sizeof setting) == 1 && setting[0] == '1';
        f.materialwrap_depth = !(GetEnvironmentVariableA("X3M_FIXTURE_MOTION_DEPTH",setting,sizeof setting)==1&&setting[0]=='0');
        f.emissions_enabled = GetEnvironmentVariableA("X3M_LINEAR_EMISSIONS",setting,sizeof setting)==1&&setting[0]=='1';
        f.distancefade_emissions_enabled = f.emissions_enabled;
        f.distancefade_enabled = GetEnvironmentVariableA("X3M_LINEAR_DISTANCE_FADE",setting,sizeof setting)==1&&setting[0]=='1';
        f.screen_enabled = GetEnvironmentVariableA("X3M_SCREEN_EMISSION",setting,sizeof setting)==1&&setting[0]=='1';
        f.taa = f.enabled && GetEnvironmentVariableA("X3M_TAA", setting, sizeof setting) == 1 && setting[0] == '1';
        // The DLL implies the jitter with the resolve on.
        f.jitter = f.enabled && (f.taa || (GetEnvironmentVariableA("X3M_MOTION_JITTER", setting, sizeof setting) == 1 && setting[0] == '1'));
        if (f.bench) f.taa = true; // The bench always runs the game-like boundary; the resolve follows X3M_TAA.
        if (GetEnvironmentVariableA("X3M_MOTION_JITTER_SAMPLES", setting, sizeof setting) > 0) { const unsigned n = unsigned(std::atoi(setting)); if (n >= 2 && n <= 64) f.jitter_samples = n; }
        char rt_mode[8]{}; f.lazy = GetEnvironmentVariableA("X3M_MOTION_RT_MODE", rt_mode, sizeof rt_mode) == 4 && !std::strcmp(rt_mode, "lazy");
        char camera_mode[8]{}; f.camera = f.seam && GetEnvironmentVariableA("X3M_FIXTURE_CAMERA", camera_mode, sizeof camera_mode) == 6 && !std::strcmp(camera_mode, "rotate");
        if (GetEnvironmentVariableA("X3M_TAA_SENTINEL", setting, sizeof setting) > 0) f.sentinel = !std::strcmp(setting, "1") ? 1 : !std::strcmp(setting, "2") ? 2 : 0;
        f.state_shadow = !(GetEnvironmentVariableA("X3M_STATE_SHADOW", setting, sizeof setting) == 1 && setting[0] == '0');
        f.wrap = GetEnvironmentVariableA("X3M_FIXTURE_WRAP", setting, sizeof setting) == 1 && setting[0] == '1';
        if (f.wrap) std::printf("WRAP mode=hostile motion_texcoord=4 depth_texcoord=5 native_texcoord=0\n");
        f.hdr = f.enabled && GetEnvironmentVariableA("X3M_HDR", setting, sizeof setting) == 1 && setting[0] == '1';
        f.ao_env = f.taa && GetEnvironmentVariableA("X3M_AMBIENT_OCCLUSION", setting, sizeof setting) == 1 && setting[0] == '1';
        f.ao_fault = GetEnvironmentVariableA("X3M_FIXTURE_AO_FAULT", setting, sizeof setting) == 6 && !std::strcmp(setting, "attach");
        f.ao_toggle_script = GetEnvironmentVariableA("X3M_FIXTURE_AO_TOGGLE", setting, sizeof setting) == 1 && setting[0] == '1';
        f.ao_toggle = symbol<int (*)(IDirect3DDevice9*)>(runtime, "x3m_ambient_occlusion_fixture_toggle", false);
        f.ao_debug = f.ao_env && GetEnvironmentVariableA("X3M_AO_DEBUG", setting, sizeof setting) == 1 && setting[0] == '1';
        { char strength[32]{}; if (GetEnvironmentVariableA("X3M_AO_STRENGTH", strength, sizeof strength) > 0) { char* end = nullptr; const float v = std::strtof(strength, &end); if (end != strength && *end == '\0' && v >= 0.f && v <= 1.f) f.ao_strength = v; } }
        f.hdr_agx = f.hdr && GetEnvironmentVariableA("X3M_HDR_TONEMAP", setting, sizeof setting) > 0 && (!std::strcmp(setting, "agx") || !std::strcmp(setting, "1"));
        if (f.taa && GetEnvironmentVariableA("X3M_TAA_SHARPEN", setting, sizeof setting) > 0) { const float v = float(std::atof(setting)); if (v > 0.f && v <= 1.f) f.sharpen = v; }
        // A caps/self-test fault must be queued before the device is created (attach).
        if (f.hdr_fault && GetEnvironmentVariableA("X3M_FIXTURE_HDR_FAULT", setting, sizeof setting) > 0) f.hdr_fault(nullptr, unsigned(std::atoi(setting)), 1);
        if (f.camera) { fake_camera_pose(0); f.camera_install(&fake_projection_slot, &fake_view_slot); }
        // The DLL's mip bias (X3M_TAA_MIP_BIAS, parsed as the DLL does: whole
        // string, |bias| <= 8, only with the jitter on) and its capture window.
        char bias_text[32]{};
        if (f.jitter && GetEnvironmentVariableA("X3M_TAA_MIP_BIAS", bias_text, sizeof bias_text) > 0) { char* end = nullptr; const float v = std::strtof(bias_text, &end); if (end != bias_text && *end == '\0' && v >= -8.f && v <= 8.f) f.mip_bias = v; }
        char window_text[16]{};
        if (GetEnvironmentVariableA("X3M_CAPTURE_START", window_text, sizeof window_text) > 0) f.capture_start = unsigned(std::atoi(window_text));
        if (GetEnvironmentVariableA("X3M_CAPTURE_FRAMES", window_text, sizeof window_text) > 0) f.capture_frames = unsigned(std::atoi(window_text));
        char path[MAX_PATH]{}; GetModuleFileNameA(runtime, path, MAX_PATH);
        std::printf("MODE seam=%u enabled=%u jitter=%u jitter_samples=%u taa=%u bench=%u width=%u height=%u dll=%s burst=%u rt_mode=%s camera=%u sentinel=%u envmap=%u hook=%u state_shadow=%u hdr=%u hdrvalues=%u hdrfault=%u hdrramp=%u hdrexposure=%u hdrtonemapfault=%u mipbias=%u mip_bias=%g sharpen=%g msaa=%u\n", f.seam, f.enabled, f.jitter, f.jitter_samples, f.taa, f.bench, Fixture::W, Fixture::H, path, f.burst, f.lazy ? "lazy" : "perdraw", f.camera, f.sentinel, f.envmap, f.hook, f.state_shadow, f.hdr, f.hdrvalues, f.hdrfault, f.hdrramp, f.hdrexposure, f.hdrtonemapfault, f.mipbias, f.mip_bias, double(f.sharpen), f.msaa ? f.msaa_samples : 0u);
        f.vs_words = load(argv[1]); f.ps_words = load(argv[2]);
        f.vs_hash = fnv(f.vs_words.data(), f.vs_words.size() * 4); f.ps_hash = fnv(f.ps_words.data(), f.ps_words.size() * 4);
        f.flat_hash = fnv(flat_program, sizeof flat_program);
        require(f.vs_hash == 0x53a0a641107ed76cull && f.ps_hash == 0x8759c7838bbc86c2ull, "local files are the reviewed pair");
        auto create = symbol<IDirect3D9* (WINAPI*)(UINT)>(runtime, "Direct3DCreate9", true);
        f.factory.p = create(D3D_SDK_VERSION); if (!f.factory.p) throw std::runtime_error("factory");
        f.pp.Windowed = TRUE; f.pp.SwapEffect = D3DSWAPEFFECT_DISCARD; f.pp.hDeviceWindow = window;
        f.pp.BackBufferWidth = Fixture::W; f.pp.BackBufferHeight = Fixture::H; f.pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        f.pp.EnableAutoDepthStencil = TRUE; f.pp.AutoDepthStencilFormat = D3DFMT_D24X8; f.pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        if (f.msaa) {
            // The multisampled back buffer and depth buffer the "msaa" script
            // presents to the route; the adapter must support the type for both.
            const auto type = D3DMULTISAMPLE_TYPE(f.msaa_samples);
            api(f.factory->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, TRUE, type, nullptr), "CheckDeviceMultiSampleType back buffer");
            api(f.factory->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, D3DFMT_D24X8, TRUE, type, nullptr), "CheckDeviceMultiSampleType depth");
            f.pp.MultiSampleType = type;
        }
        // The seam's background signature must exist before the device attaches,
        // because the first frame's selector is seeded at attach time.
        f.scope(nullptr);
        api(f.factory->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &f.pp, &f.d.p), "CreateDevice");
        f.create(mode == "production");
        if ((f.taa || f.cutout || f.faderoute) && f.enabled && f.seam && !f.bench && !f.emission_bench && !f.msaa) { f.reference.create(runtime, window, Fixture::W, Fixture::H); f.reference_ready = true; }
        if (f.cutout) run_cutout_integration(f,argv[1]); else if (f.faderoute) run_fade_route_integration(f,argv[1]); else if (f.screenemission) run_screen_emission_integration(f,argv[1]); else if (f.distancefade) run_distance_fade_integration(f,argv[1]); else if (f.materialglass) f.run_glass_materials(argv[1]); else if (f.materialxt) f.run_xt_materials(argv[1]); else if (f.emissions) run_emission_integration(f,argv[4],argv[5]); else if (f.linearmaterials) f.run_linear_materials(argv[4],argv[5],argv[6],argv[7],argv[8]); else if (f.bench) f.run_bench(24); else if (f.burst) f.run_burst(9); else if (f.mipbias) f.run_mipbias(8); else if (f.zonly) f.run_zonly(argv[1], 9); else if (f.envmap) f.run_envmap(); else if (f.hook) f.run_hook(); else if (f.aohook) f.run_ao_hook();
        else if (f.hdrvalues) f.run_hdrvalues(); else if (f.hdrfault) f.run_hdrfault();
        else if (f.hdrramp) f.run_hdrramp(); else if (f.hdrexposure) f.run_hdrexposure(); else if (f.hdrtonemapfault) f.run_hdrtonemapfault(); else if (f.msaa) f.run_msaa(); else f.run();
        if (f.reference_ready) { f.reference.destroy(); f.reference_ready = false; }
        // Teardown: every fixture object released, then the device must reach zero.
        f.back.reset(); f.depth.reset(); f.bloom_surface.reset(); f.bloom.reset();
        for (UINT i = 0; i < 8; ++i) api(f.d->SetTexture(i, nullptr), "SetTexture null"); // the mipbias script binds stages 4 and 5 too
        api(f.d->SetVertexShader(nullptr), "unbind"); api(f.d->SetPixelShader(nullptr), "unbind"); api(f.d->SetStreamSource(0, nullptr, 0, 0), "unbind"); api(f.d->SetVertexDeclaration(nullptr), "unbind");
        f.vs.reset(); f.ps.reset(); f.flat.reset(); f.zonly_vs.reset(); f.hdr2.reset(); f.hdr8.reset(); f.hdrmid.reset(); f.hdrconst.reset(); f.declaration.reset(); f.vb_a.reset(); f.vb_b.reset(); f.cube.reset(); f.ramp.reset();
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
