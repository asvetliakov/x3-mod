#include "hdr_pass.h"
#include "hdr_writeback_program.h"
#include <cstdio>
#include <cstring>

namespace x3m::renderer {
namespace {
// IDirect3DDevice9 vtable slots, verified against the MinGW d3d9.h method order
// by verification/probe/abi_check.cpp (compile-time offsetof assertions).
enum Slot : unsigned {
    GetDirect3D = 6, GetDisplayMode = 8, GetCreationParameters = 9, CreateTexture = 23,
    CreateRenderTarget = 28, GetRenderTargetData = 32, StretchRect = 34, ColorFill = 35, CreateOffscreenPlainSurface = 36,
    SetRenderTarget = 37, GetRenderTarget = 38, SetDepthStencilSurface = 39, GetDepthStencilSurface = 40,
    BeginScene = 41, EndScene = 42, SetViewport = 47, GetViewport = 48, SetRenderState = 57, GetRenderState = 58,
    GetTexture = 64, SetTexture = 65, GetTextureStageState = 66, SetTextureStageState = 67,
    GetSamplerState = 68, SetSamplerState = 69, SetScissorRect = 75, GetScissorRect = 76,
    DrawPrimitiveUP = 83, SetVertexDeclaration = 87, GetVertexDeclaration = 88, SetFVF = 89, GetFVF = 90,
    SetVertexShader = 92, GetVertexShader = 93, SetStreamSource = 100, GetStreamSource = 101,
    SetStreamSourceFreq = 102, GetStreamSourceFreq = 103, CreatePixelShader = 106, SetPixelShader = 107,
    GetPixelShader = 108
};
using D = IDirect3DDevice9*;
using GetDirect3DFn = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetDisplayModeFn = HRESULT(WINAPI*)(D, UINT, D3DDISPLAYMODE*);
using GetCreationFn = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);
using CreateTextureFn = HRESULT(WINAPI*)(D, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
using CreateRtFn = HRESULT(WINAPI*)(D, UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9**, HANDLE*);
using GetRtDataFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*, IDirect3DSurface9*);
using StretchFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE);
using ColorFillFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*, const RECT*, D3DCOLOR);
using CreateOffscreenFn = HRESULT(WINAPI*)(D, UINT, UINT, D3DFORMAT, D3DPOOL, IDirect3DSurface9**, HANDLE*);
using SetRtFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DSurface9*);
using GetRtFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DSurface9**);
using SetDepthFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*);
using GetDepthFn = HRESULT(WINAPI*)(D, IDirect3DSurface9**);
using SceneFn = HRESULT(WINAPI*)(D);
using SetViewportFn = HRESULT(WINAPI*)(D, const D3DVIEWPORT9*);
using GetViewportFn = HRESULT(WINAPI*)(D, D3DVIEWPORT9*);
using SetRsFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD);
using GetRsFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD*);
using GetTextureFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DBaseTexture9**);
using SetTextureFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DBaseTexture9*);
using GetStageFn = HRESULT(WINAPI*)(D, DWORD, D3DTEXTURESTAGESTATETYPE, DWORD*);
using SetStageFn = HRESULT(WINAPI*)(D, DWORD, D3DTEXTURESTAGESTATETYPE, DWORD);
using GetSamplerFn = HRESULT(WINAPI*)(D, DWORD, D3DSAMPLERSTATETYPE, DWORD*);
using SetSamplerFn = HRESULT(WINAPI*)(D, DWORD, D3DSAMPLERSTATETYPE, DWORD);
using SetScissorFn = HRESULT(WINAPI*)(D, const RECT*);
using GetScissorFn = HRESULT(WINAPI*)(D, RECT*);
using DrawUpFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using SetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9*);
using GetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9**);
using SetFvfFn = HRESULT(WINAPI*)(D, DWORD);
using GetFvfFn = HRESULT(WINAPI*)(D, DWORD*);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using GetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9**);
using SetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
using GetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9**, UINT*, UINT*);
using SetFreqFn = HRESULT(WINAPI*)(D, UINT, UINT);
using GetFreqFn = HRESULT(WINAPI*)(D, UINT, UINT*);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using GetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9**);

template<class T> void drop(T*& value) noexcept { if (T* held = value) { value = nullptr; held->Release(); } }
bool lost(HRESULT hr) noexcept { return hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET; }

// Self test, ps_2_0, three outputs: oC0 = (2, 8, 0.5, 0.25) into the FP16
// target (values above one, all exactly representable), oC1 = (1, 2, 3, -1)
// into A32B32G32R32F, oC2 = (0.625, ...) into R32F. `def c0, 2, 8, .5, .25 ;
// def c1, 1, 2, 3, -1 ; def c2, .625, .375, .125, 1 ; mov oC0, c0 ; mov oC1,
// c1 ; mov oC2, c2`.
constexpr DWORD self_test_mrt_program[] = {
    0xffff0200u,
    0x05000051u, 0xa00f0000u, 0x40000000u, 0x41000000u, 0x3f000000u, 0x3e800000u,
    0x05000051u, 0xa00f0001u, 0x3f800000u, 0x40000000u, 0x40400000u, 0xbf800000u,
    0x05000051u, 0xa00f0002u, 0x3f200000u, 0x3ec00000u, 0x3e000000u, 0x3f800000u,
    0x02000001u, 0x800f0800u, 0xa0e40000u,
    0x02000001u, 0x800f0801u, 0xa0e40001u,
    0x02000001u, 0x800f0802u, 0xa0e40002u,
    0x0000ffffu};
// Two-output form (a device without RT2): oC0 and oC1 as above.
constexpr DWORD self_test_pair_program[] = {
    0xffff0200u,
    0x05000051u, 0xa00f0000u, 0x40000000u, 0x41000000u, 0x3f000000u, 0x3e800000u,
    0x05000051u, 0xa00f0001u, 0x3f800000u, 0x40000000u, 0x40400000u, 0xbf800000u,
    0x02000001u, 0x800f0800u, 0xa0e40000u,
    0x02000001u, 0x800f0801u, 0xa0e40001u,
    0x0000ffffu};
// Single output (the additive pass of the self test, RT0 alone): oC0 = (2, 8, 0.5, 0.25).
constexpr DWORD self_test_single_program[] = {
    0xffff0200u,
    0x05000051u, 0xa00f0000u, 0x40000000u, 0x41000000u, 0x3f000000u, 0x3e800000u,
    0x02000001u, 0x800f0800u, 0xa0e40000u,
    0x0000ffffu};
// Expected halves: 2.0, 8.0, 0.5, 0.25 after the first pass; 4.0, 16.0, 1.0,
// 0.5 after the ONE/ONE additive pass; the 8-bit copy of the sum clamps the
// first three to 255 and stores alpha 0.5 as 127 or 128.
constexpr unsigned short self_test_half[4] = {0x4000u, 0x4800u, 0x3800u, 0x3400u};
constexpr unsigned short self_test_half_sum[4] = {0x4400u, 0x4c00u, 0x3c00u, 0x3800u};
constexpr float self_test_depth_value = 0.625f;

#ifdef X3M_MOTION_OUTPUT_FIXTURE
float half_to_float(unsigned short h) noexcept {
    const unsigned sign = (h >> 15) & 1u, exponent = (h >> 10) & 31u, mantissa = h & 1023u;
    unsigned bits;
    if (exponent == 0) {
        if (!mantissa) bits = sign << 31;
        else { // subnormal: normalize
            unsigned e = 113, m = mantissa;
            while (!(m & 1024u)) { m <<= 1; --e; }
            bits = (sign << 31) | (e << 23) | ((m & 1023u) << 13);
        }
    } else if (exponent == 31) bits = (sign << 31) | 0x7f800000u | (mantissa << 13);
    else bits = (sign << 31) | ((exponent + 112u) << 23) | (mantissa << 13);
    float value; std::memcpy(&value, &bits, 4); return value;
}
#endif

// Render states the write-back and the self-test draws set (each saved and
// restored explicitly). Blend factors are saved always and set only by the
// additive self-test pass.
constexpr D3DRENDERSTATETYPE touched_states[] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_SEPARATEALPHABLENDENABLE,
    D3DRS_CULLMODE, D3DRS_FILLMODE, D3DRS_COLORWRITEENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_STENCILENABLE,
    D3DRS_FOGENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_DITHERENABLE, D3DRS_WRAP0,
    D3DRS_MULTISAMPLEMASK, D3DRS_CLIPPING, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP,
    D3DRS_COLORWRITEENABLE1, D3DRS_COLORWRITEENABLE2, D3DRS_COLORWRITEENABLE3};
constexpr DWORD touched_values[] = {
    FALSE, FALSE, FALSE, FALSE, FALSE, D3DCULL_NONE, D3DFILL_SOLID, 15, FALSE, FALSE, FALSE, FALSE, 0, FALSE, 0,
    0xffffffffu, FALSE, D3DBLEND_ONE, D3DBLEND_ZERO, D3DBLENDOP_ADD, 15, 15, 15};
constexpr unsigned touched_count = sizeof(touched_states) / sizeof(touched_states[0]);
static_assert(touched_count == sizeof(touched_values) / sizeof(touched_values[0]));
constexpr D3DSAMPLERSTATETYPE touched_samplers[] = {
    D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER, D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV,
    D3DSAMP_SRGBTEXTURE, D3DSAMP_MAXMIPLEVEL, D3DSAMP_MIPMAPLODBIAS};
constexpr DWORD sampler_values[] = {D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_NONE, D3DTADDRESS_CLAMP, D3DTADDRESS_CLAMP, FALSE, 0, 0};
constexpr unsigned sampler_count = sizeof(touched_samplers) / sizeof(touched_samplers[0]);
static_assert(sampler_count == sizeof(sampler_values) / sizeof(sampler_values[0]));
// The quad is pre-transformed with no vertex shader, so stage 0's
// fixed-function coordinate index and texture transform shape the TEXCOORD0
// the copy reads (found by the temporal pass fixture on this backend).
constexpr D3DTEXTURESTAGESTATETYPE touched_stages[] = {D3DTSS_TEXCOORDINDEX, D3DTSS_TEXTURETRANSFORMFLAGS};
constexpr DWORD stage_values[] = {0, D3DTTFF_DISABLE};
struct Vertex { float x, y, z, rhw, u, v; };
} // namespace

// Everything the injected draws touch. COM references returned by the getters
// are released by the destructor after restoration.
struct HdrPass::SavedState {
    IDirect3DSurface9* targets[4]{};
    IDirect3DSurface9* depth = nullptr;
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    DWORD fvf = 0;
    IDirect3DVertexDeclaration9* declaration = nullptr;
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexBuffer9* stream = nullptr;
    UINT offset = 0, stride = 0, frequency = 1;
    IDirect3DBaseTexture9* texture = nullptr;
    DWORD samplers[sampler_count]{}, stages[2]{}, states[touched_count]{};
    unsigned target_count = 1;
    ~SavedState() {
        for (auto& target : targets) drop(target);
        drop(depth); drop(declaration); drop(vs); drop(ps); drop(stream); drop(texture);
    }
};

HdrPass::~HdrPass() { shutdown(); }

std::uint64_t HdrPass::stamp(bool timing) const noexcept {
    if (!timing) return 0;
    LARGE_INTEGER t{}; QueryPerformanceCounter(&t); return std::uint64_t(t.QuadPart);
}

void HdrPass::shutdown() noexcept {
    release_target();
    drop(shader_);
    device_ = nullptr; native_ = nullptr;
    caps_ = HdrCaps{};
}

void HdrPass::release_target() noexcept {
    drop(target_);
    width_ = height_ = 0;
}

// Lazily (re)creates the FP16 render-target texture; only its level-0
// surface is retained (the level keeps its container alive natively and
// through the ownership wrapper, so one owned object is one device reference).
HRESULT HdrPass::ensure_target(UINT width, UINT height) noexcept {
    if (target_ && width_ == width && height_ == height) return S_OK;
    release_target();
    if (!device_ || !width || !height) return E_INVALIDARG;
    IDirect3DTexture9* texture = nullptr;
    HRESULT hr = fault(HdrFault::TargetCreate) ? E_OUTOFMEMORY
        : call<CreateTextureFn>(CreateTexture)(device_, width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &texture, nullptr);
    if (SUCCEEDED(hr) && texture) hr = texture->GetSurfaceLevel(0, &target_);
    else if (SUCCEEDED(hr)) hr = E_FAIL;
    drop(texture);
    if (FAILED(hr) || !target_) { drop(target_); return FAILED(hr) ? hr : E_FAIL; }
    width_ = width; height_ = height;
    return S_OK;
}

// SetRenderTarget(0) resets the viewport and the scissor rectangle to the
// new target: both are read before and written back after, so the
// application's values survive the substitution (same dimensions).
HRESULT HdrPass::bind(IDirect3DSurface9* surface, std::uint64_t* ticks) noexcept {
    const std::uint64_t begin = stamp(ticks != nullptr);
    D3DVIEWPORT9 viewport{}; RECT scissor{};
    const HRESULT viewport_hr = call<GetViewportFn>(GetViewport)(device_, &viewport);
    const HRESULT scissor_hr = call<GetScissorFn>(GetScissorRect)(device_, &scissor);
    HRESULT hr = fault(HdrFault::Bind) ? E_FAIL : call<SetRtFn>(SetRenderTarget)(device_, 0, surface);
    if (SUCCEEDED(hr) && SUCCEEDED(viewport_hr)) hr = call<SetViewportFn>(SetViewport)(device_, &viewport);
    if (SUCCEEDED(hr) && SUCCEEDED(scissor_hr)) hr = call<SetScissorFn>(SetScissorRect)(device_, &scissor);
    if (ticks) *ticks = stamp(true) - begin;
    return hr;
}

HRESULT HdrPass::save(SavedState& saved) noexcept {
    saved.target_count = caps9_.NumSimultaneousRTs < 4 ? unsigned(caps9_.NumSimultaneousRTs) : 4;
    if (!saved.target_count) saved.target_count = 1;
    HRESULT hr;
    for (unsigned i = 0; i < saved.target_count; ++i) {
        hr = call<GetRtFn>(GetRenderTarget)(device_, i, &saved.targets[i]);
        if (FAILED(hr) && !(i && hr == D3DERR_NOTFOUND && !saved.targets[i])) return hr;
        if (!i && !saved.targets[0]) return E_FAIL;
    }
    hr = call<GetDepthFn>(GetDepthStencilSurface)(device_, &saved.depth);
    if (FAILED(hr) && !(hr == D3DERR_NOTFOUND && !saved.depth)) return hr;
    if (FAILED(hr = call<GetViewportFn>(GetViewport)(device_, &saved.viewport))) return hr;
    if (FAILED(hr = call<GetScissorFn>(GetScissorRect)(device_, &saved.scissor))) return hr;
    if (FAILED(hr = call<GetFvfFn>(GetFVF)(device_, &saved.fvf))) return hr;
    if (FAILED(hr = call<GetDeclarationFn>(GetVertexDeclaration)(device_, &saved.declaration))) return hr;
    if (FAILED(hr = call<GetVsFn>(GetVertexShader)(device_, &saved.vs))) return hr;
    if (FAILED(hr = call<GetPsFn>(GetPixelShader)(device_, &saved.ps))) return hr;
    if (FAILED(hr = call<GetStreamFn>(GetStreamSource)(device_, 0, &saved.stream, &saved.offset, &saved.stride))) return hr;
    if (FAILED(hr = call<GetFreqFn>(GetStreamSourceFreq)(device_, 0, &saved.frequency))) return hr;
    if (FAILED(hr = call<GetTextureFn>(GetTexture)(device_, 0, &saved.texture))) return hr;
    for (unsigned i = 0; i < sampler_count; ++i)
        if (FAILED(hr = call<GetSamplerFn>(GetSamplerState)(device_, 0, touched_samplers[i], &saved.samplers[i]))) return hr;
    for (unsigned i = 0; i < 2; ++i)
        if (FAILED(hr = call<GetStageFn>(GetTextureStageState)(device_, 0, touched_stages[i], &saved.stages[i]))) return hr;
    for (unsigned i = 0; i < touched_count; ++i)
        if (FAILED(hr = call<GetRsFn>(GetRenderState)(device_, touched_states[i], &saved.states[i]))) return hr;
    return S_OK;
}

// Restores in an order that is correct under D3D9 semantics: the source
// texture is unbound before a target that may alias it is rebound;
// SetRenderTarget(0) resets viewport and scissor, so those come after the
// targets; FVF and declaration are two views of one binding, restored through
// whichever the application used; DrawPrimitiveUP clears stream 0, so it is
// rebound. `rt0` replaces the saved RT0 (the caller's final binding).
HRESULT HdrPass::restore(const SavedState& saved, IDirect3DSurface9* rt0) noexcept {
    HRESULT first = S_OK;
    auto step = [&](HRESULT hr) { if (SUCCEEDED(first) && FAILED(hr)) first = hr; };
    step(call<SetTextureFn>(SetTexture)(device_, 0, nullptr));
    step(call<SetRtFn>(SetRenderTarget)(device_, 0, rt0));
    for (unsigned i = 1; i < saved.target_count; ++i) step(call<SetRtFn>(SetRenderTarget)(device_, i, saved.targets[i]));
    step(call<SetDepthFn>(SetDepthStencilSurface)(device_, saved.depth));
    step(call<SetViewportFn>(SetViewport)(device_, &saved.viewport));
    step(call<SetScissorFn>(SetScissorRect)(device_, &saved.scissor));
    if (saved.fvf) step(call<SetFvfFn>(SetFVF)(device_, saved.fvf));
    else step(call<SetDeclarationFn>(SetVertexDeclaration)(device_, saved.declaration));
    step(call<SetVsFn>(SetVertexShader)(device_, saved.vs));
    step(call<SetPsFn>(SetPixelShader)(device_, saved.ps));
    step(call<SetStreamFn>(SetStreamSource)(device_, 0, saved.stream, saved.offset, saved.stride));
    step(call<SetFreqFn>(SetStreamSourceFreq)(device_, 0, saved.frequency));
    step(call<SetTextureFn>(SetTexture)(device_, 0, saved.texture));
    for (unsigned i = 0; i < sampler_count; ++i) step(call<SetSamplerFn>(SetSamplerState)(device_, 0, touched_samplers[i], saved.samplers[i]));
    for (unsigned i = 0; i < 2; ++i) step(call<SetStageFn>(SetTextureStageState)(device_, 0, touched_stages[i], saved.stages[i]));
    for (unsigned i = 0; i < touched_count; ++i) step(call<SetRsFn>(SetRenderState)(device_, touched_states[i], saved.states[i]));
    return first;
}

// The identity copy: `source_texture` (level 0 = `source_target`, which must
// not be bound as a target meanwhile) sampled point-wise into `destination`
// through the embedded ps_3_0. Returns the operation result; *restoration
// receives the restoration result separately; RT0 ends bound to final_rt0.
HRESULT HdrPass::copy_draw(IDirect3DSurface9* source_target, IDirect3DTexture9* source_texture, IDirect3DSurface9* destination,
                           UINT width, UINT height, IDirect3DSurface9* final_rt0, HRESULT* restoration,
                           HRESULT injected_draw, bool injected_restore) noexcept {
    (void)source_target;
    *restoration = S_OK;
    SavedState saved;
    HRESULT hr = save(saved);
    if (FAILED(hr)) return hr;
    HRESULT op = S_OK;
    auto step = [&](HRESULT result) { if (SUCCEEDED(op) && FAILED(result)) op = result; };
    step(call<SetTextureFn>(SetTexture)(device_, 0, nullptr));
    // RT1.. are unbound before RT0 changes: D3D9 requires every bound target
    // to match RT0's dimensions, and the destination may differ from RT1's.
    for (unsigned i = 1; i < saved.target_count; ++i) step(call<SetRtFn>(SetRenderTarget)(device_, i, nullptr));
    step(call<SetRtFn>(SetRenderTarget)(device_, 0, destination));
    step(call<SetDepthFn>(SetDepthStencilSurface)(device_, nullptr));
    const D3DVIEWPORT9 viewport{0, 0, width, height, 0.f, 1.f};
    step(call<SetViewportFn>(SetViewport)(device_, &viewport));
    step(call<SetFvfFn>(SetFVF)(device_, D3DFVF_XYZRHW | D3DFVF_TEX1));
    step(call<SetVsFn>(SetVertexShader)(device_, nullptr));
    step(call<SetPsFn>(SetPixelShader)(device_, shader_));
    step(call<SetFreqFn>(SetStreamSourceFreq)(device_, 0, 1));
    step(call<SetTextureFn>(SetTexture)(device_, 0, source_texture));
    for (unsigned i = 0; i < sampler_count; ++i) step(call<SetSamplerFn>(SetSamplerState)(device_, 0, touched_samplers[i], sampler_values[i]));
    for (unsigned i = 0; i < 2; ++i) step(call<SetStageFn>(SetTextureStageState)(device_, 0, touched_stages[i], stage_values[i]));
    for (unsigned i = 0; i < touched_count; ++i) step(call<SetRsFn>(SetRenderState)(device_, touched_states[i], touched_values[i]));
    if (SUCCEEDED(op)) {
        // Integer raster sample positions: shift by -0.5 so every texel centre is
        // covered and TEXCOORD0 lands on texel centres under point sampling.
        const float w = float(width) - .5f, h = float(height) - .5f;
        const Vertex quad[4] = {{-.5f, -.5f, 0.f, 1.f, 0.f, 0.f}, {w, -.5f, 0.f, 1.f, 1.f, 0.f},
                                {-.5f, h, 0.f, 1.f, 0.f, 1.f}, {w, h, 0.f, 1.f, 1.f, 1.f}};
        if (FAILED(injected_draw)) op = injected_draw;   // fixture seam: the draw "failed" and nothing was written
        else step(call<DrawUpFn>(DrawPrimitiveUP)(device_, D3DPT_TRIANGLESTRIP, 2, quad, sizeof quad[0]));
    }
    *restoration = restore(saved, final_rt0);
    if (injected_restore && SUCCEEDED(*restoration)) *restoration = E_FAIL;  // fixture seam: reported after the actual restoration
    return op;
}

// Fullscreen XYZRHW strip with `shader` bound into RT0/RT1/RT2 (null unbinds),
// no depth, full viewport; `additive` blends ONE/ONE. State saved and restored.
HRESULT HdrPass::mrt_draw(IDirect3DSurface9* rt0, IDirect3DSurface9* rt1, IDirect3DSurface9* rt2, IDirect3DPixelShader9* shader,
                          UINT width, UINT height, bool additive, HRESULT* restoration) noexcept {
    *restoration = S_OK;
    SavedState saved;
    HRESULT hr = save(saved);
    if (FAILED(hr)) return hr;
    HRESULT op = S_OK;
    auto step = [&](HRESULT result) { if (SUCCEEDED(op) && FAILED(result)) op = result; };
    step(call<SetTextureFn>(SetTexture)(device_, 0, nullptr));
    // The application's RT1.. (possibly larger than the 4x4 set) are unbound
    // before the small RT0 is bound; the set's own RT1/RT2 follow it.
    for (unsigned i = 1; i < saved.target_count; ++i) step(call<SetRtFn>(SetRenderTarget)(device_, i, nullptr));
    step(call<SetRtFn>(SetRenderTarget)(device_, 0, rt0));
    for (unsigned i = 1; i < saved.target_count; ++i) if (i == 1 ? rt1 != nullptr : i == 2 && rt2 != nullptr) step(call<SetRtFn>(SetRenderTarget)(device_, i, i == 1 ? rt1 : rt2));
    step(call<SetDepthFn>(SetDepthStencilSurface)(device_, nullptr));
    const D3DVIEWPORT9 viewport{0, 0, width, height, 0.f, 1.f};
    step(call<SetViewportFn>(SetViewport)(device_, &viewport));
    step(call<SetFvfFn>(SetFVF)(device_, D3DFVF_XYZRHW));
    step(call<SetVsFn>(SetVertexShader)(device_, nullptr));
    step(call<SetPsFn>(SetPixelShader)(device_, shader));
    step(call<SetFreqFn>(SetStreamSourceFreq)(device_, 0, 1));
    for (unsigned i = 0; i < touched_count; ++i) step(call<SetRsFn>(SetRenderState)(device_, touched_states[i], touched_values[i]));
    if (additive) {
        step(call<SetRsFn>(SetRenderState)(device_, D3DRS_ALPHABLENDENABLE, TRUE));
        step(call<SetRsFn>(SetRenderState)(device_, D3DRS_SRCBLEND, D3DBLEND_ONE));
        step(call<SetRsFn>(SetRenderState)(device_, D3DRS_DESTBLEND, D3DBLEND_ONE));
        step(call<SetRsFn>(SetRenderState)(device_, D3DRS_BLENDOP, D3DBLENDOP_ADD));
    }
    if (SUCCEEDED(op)) {
        const float w = float(width) - .5f, h = float(height) - .5f;
        const float quad[4][4] = {{-.5f, -.5f, 0.f, 1.f}, {w, -.5f, 0.f, 1.f}, {-.5f, h, 0.f, 1.f}, {w, h, 0.f, 1.f}};
        step(call<DrawUpFn>(DrawPrimitiveUP)(device_, D3DPT_TRIANGLESTRIP, 2, quad, sizeof quad[0]));
    }
    *restoration = restore(saved, saved.targets[0]);
    return op;
}

// The four-format MRT self test on a 4x4 set: (1) one draw into FP16 +
// RGBA32F (+ R32F) with values above one, (2) an additive ONE/ONE draw into
// the FP16 target alone (the single-RT FP16 blend capability for real, not by
// HRESULT), (3) the write-back shader copying the sum into a 4x4 A8R8G8B8
// target (clamping and the alpha carry). Every target is read back through
// system memory and compared exactly (halves and floats) or within two codes
// (the 8-bit copy). A device that reports the capabilities but cannot execute
// the combination is refused here rather than mid-scene. (4) When the format
// conversion is granted, the emergency rung is exercised too: the 8-bit
// target is filled black and StretchRect(FP16 -> A8R8G8B8, POINT) must
// reproduce the copy; a failure demotes the rung (the ladder skips a call
// known not to work) without refusing the feature, so both copy rungs of the
// unwind ladder are proven at attach wherever they are available.
bool HdrPass::self_test(bool with_depth, bool scene_open, char* detail, std::size_t detail_size) noexcept {
    IDirect3DTexture9* scene = nullptr; IDirect3DSurface9* scene_surface = nullptr;
    IDirect3DTexture9* motion = nullptr; IDirect3DSurface9* motion_surface = nullptr;
    IDirect3DTexture9* depth = nullptr; IDirect3DSurface9* depth_surface = nullptr;
    IDirect3DSurface9* color = nullptr;
    IDirect3DSurface9* scene_copy = nullptr; IDirect3DSurface9* motion_copy = nullptr; IDirect3DSurface9* depth_copy = nullptr; IDirect3DSurface9* color_copy = nullptr;
    IDirect3DPixelShader9* mrt_shader = nullptr; IDirect3DPixelShader9* single_shader = nullptr;
    bool ok = false;
    HRESULT hr = S_OK, restore_hr = S_OK, draw = S_OK, blend = S_OK, copy = S_OK, copy_restore = S_OK, scene_hr = S_OK, stretch = S_FALSE;
    unsigned scene_errors = 0, sum_errors = 0, motion_errors = 0, depth_errors = 0, copy_errors = 0, stretch_errors = 0;
    const char* stage = "create";
    bool own_scene = false;
    do {
        if (FAILED(hr = call<CreateTextureFn>(CreateTexture)(device_, 4, 4, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &scene, nullptr))) break;
        if (FAILED(hr = scene->GetSurfaceLevel(0, &scene_surface))) break;
        if (FAILED(hr = call<CreateTextureFn>(CreateTexture)(device_, 4, 4, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &motion, nullptr))) break;
        if (FAILED(hr = motion->GetSurfaceLevel(0, &motion_surface))) break;
        if (with_depth) {
            if (FAILED(hr = call<CreateTextureFn>(CreateTexture)(device_, 4, 4, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &depth, nullptr))) break;
            if (FAILED(hr = depth->GetSurfaceLevel(0, &depth_surface))) break;
            if (FAILED(hr = call<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, 4, 4, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &depth_copy, nullptr))) break;
        }
        if (FAILED(hr = call<CreateRtFn>(CreateRenderTarget)(device_, 4, 4, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &color, nullptr))) break;
        if (FAILED(hr = call<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, 4, 4, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &scene_copy, nullptr))) break;
        if (FAILED(hr = call<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, 4, 4, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &motion_copy, nullptr))) break;
        if (FAILED(hr = call<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, 4, 4, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &color_copy, nullptr))) break;
        if (FAILED(hr = call<CreatePsFn>(CreatePixelShader)(device_, with_depth ? self_test_mrt_program : self_test_pair_program, &mrt_shader))) break;
        if (FAILED(hr = call<CreatePsFn>(CreatePixelShader)(device_, self_test_single_program, &single_shader))) break;
        stage = "scene";
        if (!scene_open) { if (FAILED(scene_hr = call<SceneFn>(BeginScene)(device_))) { hr = scene_hr; break; } own_scene = true; }
        stage = "draw";
        draw = mrt_draw(scene_surface, motion_surface, depth_surface, mrt_shader, 4, 4, false, &restore_hr);
        if (FAILED(draw)) { hr = draw; break; }
        if (FAILED(restore_hr)) { hr = restore_hr; stage = "restore"; break; }
        stage = "readback";
        if (FAILED(hr = call<GetRtDataFn>(GetRenderTargetData)(device_, scene_surface, scene_copy))) break;
        if (FAILED(hr = call<GetRtDataFn>(GetRenderTargetData)(device_, motion_surface, motion_copy))) break;
        if (with_depth && FAILED(hr = call<GetRtDataFn>(GetRenderTargetData)(device_, depth_surface, depth_copy))) break;
        D3DLOCKED_RECT lock{};
        if (FAILED(hr = scene_copy->LockRect(&lock, nullptr, D3DLOCK_READONLY))) break;
        for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
            unsigned short value[4]; std::memcpy(value, static_cast<const char*>(lock.pBits) + y * lock.Pitch + x * 8, 8);
            if (std::memcmp(value, self_test_half, sizeof value)) ++scene_errors;
        }
        scene_copy->UnlockRect();
        if (FAILED(hr = motion_copy->LockRect(&lock, nullptr, D3DLOCK_READONLY))) break;
        for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
            float value[4]; std::memcpy(value, static_cast<const char*>(lock.pBits) + y * lock.Pitch + x * 16, 16);
            if (value[0] != 1.f || value[1] != 2.f || value[2] != 3.f || value[3] != -1.f) ++motion_errors;
        }
        motion_copy->UnlockRect();
        if (with_depth) {
            if (FAILED(hr = depth_copy->LockRect(&lock, nullptr, D3DLOCK_READONLY))) break;
            for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
                float value; std::memcpy(&value, static_cast<const char*>(lock.pBits) + y * lock.Pitch + x * 4, 4);
                if (value != self_test_depth_value) ++depth_errors;
            }
            depth_copy->UnlockRect();
        }
        stage = "blend";
        blend = mrt_draw(scene_surface, nullptr, nullptr, single_shader, 4, 4, true, &restore_hr);
        if (FAILED(blend)) { hr = blend; break; }
        if (FAILED(restore_hr)) { hr = restore_hr; stage = "restore"; break; }
        if (FAILED(hr = call<GetRtDataFn>(GetRenderTargetData)(device_, scene_surface, scene_copy))) break;
        if (FAILED(hr = scene_copy->LockRect(&lock, nullptr, D3DLOCK_READONLY))) break;
        for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
            unsigned short value[4]; std::memcpy(value, static_cast<const char*>(lock.pBits) + y * lock.Pitch + x * 8, 8);
            if (std::memcmp(value, self_test_half_sum, sizeof value)) ++sum_errors;
        }
        scene_copy->UnlockRect();
        stage = "copy";
        {
            IDirect3DSurface9* current = nullptr;
            if (FAILED(hr = call<GetRtFn>(GetRenderTarget)(device_, 0, &current)) || !current) { if (SUCCEEDED(hr)) hr = E_FAIL; break; }
            copy = copy_draw(scene_surface, scene, color, 4, 4, current, &copy_restore, S_OK, false);
            drop(current);
        }
        if (FAILED(copy)) { hr = copy; break; }
        if (FAILED(copy_restore)) { hr = copy_restore; stage = "restore"; break; }
        if (FAILED(hr = call<GetRtDataFn>(GetRenderTargetData)(device_, color, color_copy))) break;
        if (FAILED(hr = color_copy->LockRect(&lock, nullptr, D3DLOCK_READONLY))) break;
        for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
            DWORD value = 0; std::memcpy(&value, static_cast<const char*>(lock.pBits) + y * lock.Pitch + x * 4, 4);
            const int a = int(value >> 24), r = int((value >> 16) & 255), g = int((value >> 8) & 255), b = int(value & 255);
            if (r != 255 || g != 255 || b != 255 || a < 126 || a > 130) ++copy_errors;
        }
        color_copy->UnlockRect();
        if (SUCCEEDED(caps_.stretch_conversion)) {
            stage = "stretch";
            if (FAILED(hr = call<ColorFillFn>(ColorFill)(device_, color, nullptr, 0))) break;
            stretch = call<StretchFn>(StretchRect)(device_, scene_surface, nullptr, color, nullptr, D3DTEXF_POINT);
            if (SUCCEEDED(stretch)) {
                if (FAILED(hr = call<GetRtDataFn>(GetRenderTargetData)(device_, color, color_copy))) break;
                if (FAILED(hr = color_copy->LockRect(&lock, nullptr, D3DLOCK_READONLY))) break;
                for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
                    DWORD value = 0; std::memcpy(&value, static_cast<const char*>(lock.pBits) + y * lock.Pitch + x * 4, 4);
                    const int a = int(value >> 24), r = int((value >> 16) & 255), g = int((value >> 8) & 255), b = int(value & 255);
                    if (r != 255 || g != 255 || b != 255 || a < 126 || a > 130) ++stretch_errors;
                }
                color_copy->UnlockRect();
            }
            // The rung is demoted, not the feature: the ladder never calls a copy known not to work.
            if (FAILED(stretch) || stretch_errors) caps_.stretch_conversion = FAILED(stretch) ? stretch : E_FAIL;
        }
        stage = "compare";
        ok = !scene_errors && !sum_errors && !motion_errors && !depth_errors && !copy_errors && !fault(HdrFault::SelfTest);
    } while (false);
    if (own_scene) { const HRESULT end = call<SceneFn>(EndScene)(device_); if (FAILED(end) && SUCCEEDED(hr)) { hr = end; ok = false; stage = "end_scene"; } }
    drop(single_shader); drop(mrt_shader);
    drop(color_copy); drop(depth_copy); drop(motion_copy); drop(scene_copy);
    drop(color); drop(depth_surface); drop(depth); drop(motion_surface); drop(motion); drop(scene_surface); drop(scene);
    std::snprintf(detail, detail_size, "stage=%s result=%08lx draw=%08lx blend=%08lx copy=%08lx restore=%08lx copy_restore=%08lx scene=%08lx stretch=%08lx scene_errors=%u sum_errors=%u motion_errors=%u depth_errors=%u copy_errors=%u stretch_errors=%u targets=%u",
                  stage, hr, draw, blend, copy, restore_hr, copy_restore, scene_hr, stretch, scene_errors, sum_errors, motion_errors, depth_errors, copy_errors, stretch_errors, with_depth ? 3u : 2u);
    return ok;
}

bool HdrPass::recheck(bool scene_open, char* detail, std::size_t detail_size) noexcept {
    if (!caps_.enabled) { std::snprintf(detail, detail_size, "disabled"); return false; }
    return self_test(with_depth_, scene_open, detail, detail_size);
}

void HdrPass::attach(IDirect3DDevice9* device, void* const* native, const D3DCAPS9& caps, D3DFORMAT main_format, bool with_depth) noexcept {
    shutdown();
    device_ = device; native_ = native; caps9_ = caps; main_format_ = main_format; with_depth_ = with_depth;
    caps_ = HdrCaps{};
    const char* reason = "ok";
    if (!device || !native) reason = "device";
    else if (D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion) < 3) reason = "ps_version";
    else if (caps.NumSimultaneousRTs < 2) reason = "mrt_count";
    else if (!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS)) reason = "mrt_bit_depths";
    else {
        IDirect3D9* factory = nullptr;
        D3DDEVICE_CREATION_PARAMETERS creation{};
        D3DDISPLAYMODE mode{};
        if (FAILED(call<GetDirect3DFn>(GetDirect3D)(device_, &factory)) || !factory) reason = "factory";
        else if (FAILED(call<GetCreationFn>(GetCreationParameters)(device_, &creation)) ||
                 FAILED(call<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &mode))) reason = "adapter_query";
        else {
            const UINT adapter = creation.AdapterOrdinal; const D3DDEVTYPE type = creation.DeviceType;
            caps_.fp16_target = factory->CheckDeviceFormat(adapter, type, mode.Format, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F);
            caps_.fp16_blending = factory->CheckDeviceFormat(adapter, type, mode.Format, D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F);
            caps_.fp16_filter = factory->CheckDeviceFormat(adapter, type, mode.Format, D3DUSAGE_QUERY_FILTER, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F);
            caps_.fp16_sampling = factory->CheckDeviceFormat(adapter, type, mode.Format, 0, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F);
            caps_.stretch_conversion = factory->CheckDeviceFormatConversion(adapter, type, D3DFMT_A16B16G16R16F, main_format);
            caps_.mrt_blending = (caps.PrimitiveMiscCaps & D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING) != 0;
            if (fault(HdrFault::CapsTarget)) caps_.fp16_target = E_FAIL;
            if (fault(HdrFault::CapsBlending)) caps_.fp16_blending = E_FAIL;
            if (FAILED(caps_.fp16_target)) reason = "fp16_target";
            else if (FAILED(caps_.fp16_blending)) reason = "fp16_blending";
            else if (FAILED(caps_.fp16_sampling)) reason = "fp16_sampling";
        }
        drop(factory);
    }
    if (!std::strcmp(reason, "ok")) {
        const HRESULT hr = call<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(hdr_writeback_program()), &shader_);
        if (FAILED(hr) || !shader_) { reason = "shader"; std::snprintf(caps_.self_test_detail, sizeof caps_.self_test_detail, "create=%08lx", hr); drop(shader_); }
    }
    if (!std::strcmp(reason, "ok")) {
        caps_.self_test_targets = with_depth ? 3u : 2u;
        if (!self_test(with_depth, false, caps_.self_test_detail, sizeof caps_.self_test_detail)) reason = "self_test";
    }
    caps_.reason = reason;
    caps_.enabled = !std::strcmp(reason, "ok");
    if (!caps_.enabled) drop(shader_);
}

// The must-unwind ladder. The shader copy is the normal path; a failed draw
// falls to the point StretchRect (when the conversion is granted); a failed
// restoration or StretchRect ends with an explicit rebind of final_rt0 so the
// device never keeps the FP16 surface as RT0 past this call.
HdrWriteback HdrPass::write_back(IDirect3DSurface9* main, IDirect3DSurface9* final_rt0, bool scene_open, bool write, bool timing) noexcept {
    HdrWriteback result{};
    if (!main || !final_rt0) { result.unwind = true; result.unwind_reason = "arguments"; return result; }
    // Fixture seam: one injected failure per write, consumed only when a copy
    // is attempted so a rebind-only call never eats an armed fault
    // (production: never).
    HRESULT injected_draw = S_OK;
    if (write && target_ && shader_) {
        injected_draw = fault(HdrFault::Draw) ? E_FAIL : fault(HdrFault::Lost) ? D3DERR_DEVICELOST : fault(HdrFault::Stretch) ? E_ABORT : S_OK;
        const bool injected_restore = fault(HdrFault::Restore);
        IDirect3DTexture9* texture = nullptr;
        const std::uint64_t begin = stamp(timing);
        HRESULT hr = target_->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&texture));
        if (SUCCEEDED(hr) && !texture) hr = E_NOINTERFACE;
        bool own_scene = false;
        if (SUCCEEDED(hr) && !scene_open) { hr = call<SceneFn>(BeginScene)(device_); own_scene = SUCCEEDED(hr); }
        if (SUCCEEDED(hr)) hr = copy_draw(target_, texture, main, width_, height_, final_rt0, &result.restore, injected_draw, injected_restore);
        result.draw = hr;
        // A scene we opened is always closed, lost device included: the runtime
        // keeps its in-scene flag otherwise and the application's next
        // BeginScene would be refused.
        if (own_scene) { const HRESULT end = call<SceneFn>(EndScene)(device_); if (FAILED(end) && SUCCEEDED(result.draw)) result.draw = end; }
        drop(texture);
        result.ticks_draw = stamp(timing) - begin;
        if (SUCCEEDED(result.draw) && SUCCEEDED(result.restore)) { result.source = HdrWritebackSource::Shader; return result; }
        result.unwind = true;
        if (SUCCEEDED(result.draw)) { result.source = HdrWritebackSource::Shader; result.unwind_reason = "restore"; }
        else {
            result.unwind_reason = lost(result.draw) ? "lost" : "draw";
            const std::uint64_t stretch_begin = stamp(timing);
            if (injected_draw == E_ABORT) result.stretch = E_FAIL;      // fixture seam: both copy rungs fail
            else if (lost(result.draw)) result.stretch = result.draw;
            else if (FAILED(caps_.stretch_conversion)) result.stretch = D3DERR_NOTAVAILABLE;
            else result.stretch = call<StretchFn>(StretchRect)(device_, target_, nullptr, main, nullptr, D3DTEXF_POINT);
            result.ticks_stretch = stamp(timing) - stretch_begin;
            if (SUCCEEDED(result.stretch)) result.source = HdrWritebackSource::Stretch;
            else { result.source = HdrWritebackSource::Restore; if (!lost(result.draw)) result.unwind_reason = "stretch"; }
        }
    } else if (write) { result.unwind = true; result.unwind_reason = "resources"; }
    // Reached only when the copy did not end cleanly (or nothing was to be
    // written): the binding is re-established explicitly.
    result.bind = bind(final_rt0, timing ? &result.ticks_bind : nullptr);
    if (FAILED(result.bind)) { result.unwind = true; if (!std::strcmp(result.unwind_reason, "none")) result.unwind_reason = "bind"; }
    return result;
}

#ifdef X3M_MOTION_OUTPUT_FIXTURE
HRESULT HdrPass::fixture_readback(float* out, std::size_t floats, UINT* width, UINT* height) noexcept {
    if (width) *width = width_;
    if (height) *height = height_;
    if (!target_) return D3DERR_NOTFOUND;
    if (!out || floats < std::size_t(width_) * height_ * 4) return D3DERR_MOREDATA;
    IDirect3DSurface9* copy = nullptr;
    HRESULT hr = call<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, width_, height_, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &copy, nullptr);
    if (SUCCEEDED(hr)) hr = call<GetRtDataFn>(GetRenderTargetData)(device_, target_, copy);
    D3DLOCKED_RECT lock{};
    if (SUCCEEDED(hr)) hr = copy->LockRect(&lock, nullptr, D3DLOCK_READONLY);
    if (SUCCEEDED(hr)) {
        for (UINT y = 0; y < height_; ++y) {
            const unsigned short* row = reinterpret_cast<const unsigned short*>(static_cast<const char*>(lock.pBits) + y * lock.Pitch);
            for (UINT x = 0; x < width_ * 4; ++x) out[(std::size_t(y) * width_) * 4 + x] = half_to_float(row[x]);
        }
        copy->UnlockRect();
    }
    drop(copy);
    return hr;
}
#endif
} // namespace x3m::renderer
