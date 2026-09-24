#include "hdr_pass.h"
#include "hdr_writeback_program.h"
#include "hdr_writeback_dither_program.h"
#include "quad_vertex_program.h"
#include "hdr_tonemap_program.h"
#include "taa_sharpen_program.h"
#include "hdr_tonemap_sharpen_program.h"
#include "hdr_meter_program.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

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
    DrawPrimitiveUP = 83, CreateVertexDeclaration = 86, SetVertexDeclaration = 87, GetVertexDeclaration = 88, SetFVF = 89, GetFVF = 90,
    CreateVertexShader = 91, SetVertexShader = 92, GetVertexShader = 93, SetStreamSource = 100, GetStreamSource = 101,
    SetStreamSourceFreq = 102, GetStreamSourceFreq = 103, CreatePixelShader = 106, SetPixelShader = 107,
    GetPixelShader = 108, SetPixelShaderConstantF = 109, GetPixelShaderConstantF = 110
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
using CreateDeclarationFn = HRESULT(WINAPI*)(D, const D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9**);
using SetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9*);
using GetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9**);
using SetFvfFn = HRESULT(WINAPI*)(D, DWORD);
using GetFvfFn = HRESULT(WINAPI*)(D, DWORD*);
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using GetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9**);
using SetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
using GetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9**, UINT*, UINT*);
using SetFreqFn = HRESULT(WINAPI*)(D, UINT, UINT);
using GetFreqFn = HRESULT(WINAPI*)(D, UINT, UINT*);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using GetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9**);
using SetPsConstFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
using GetPsConstFn = HRESULT(WINAPI*)(D, UINT, float*, UINT);

template<class T> void drop(T*& value) noexcept { if (T* held = value) { value = nullptr; held->Release(); } }
// COM guarantees stable pointer equality for canonical IUnknown, not arbitrary
// interface aliases. Keep failed/null queries distinct from valid inequality,
// and balance even hostile non-null outputs accompanying a failed HRESULT.
HRESULT same_com_object(IUnknown* left, IUnknown* right, bool& equal) noexcept {
    equal = false;
    if (!left || !right) return E_INVALIDARG;
    IUnknown* left_identity = nullptr;
    IUnknown* right_identity = nullptr;
    HRESULT hr = left->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&left_identity));
    if (SUCCEEDED(hr) && !left_identity) hr = E_NOINTERFACE;
    if (SUCCEEDED(hr)) {
        hr = right->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&right_identity));
        if (SUCCEEDED(hr) && !right_identity) hr = E_NOINTERFACE;
    }
    if (SUCCEEDED(hr)) equal = left_identity == right_identity;
    drop(right_identity); drop(left_identity);
    return hr;
}
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
// Stage 0's fixed-function coordinate index and texture transform shaped the
// TEXCOORD0 of the pre-transformed quad on the Preview backend (found by the
// temporal pass fixture); inert with the vertex program bound, still needed by
// the fixture's XYZRHW twin, saved and restored either way.
constexpr D3DTEXTURESTAGESTATETYPE touched_stages[] = {D3DTSS_TEXCOORDINDEX, D3DTSS_TEXTURETRANSFORMFLAGS};
constexpr DWORD stage_values[] = {0, D3DTTFF_DISABLE};
// The meter chain's constant block, c0..c3 (hdr_meter_level0_ps.hlsl).
struct MeterConstants { float source[4]; float decode[4]; float meter[4]; float output[4]; };
} // namespace

const char* hdr_tonemap_name(HdrTonemap tonemap) noexcept { return tonemap == HdrTonemap::Agx ? "agx" : "identity"; }
const char* hdr_look_name(x3::temporal::AgxLook look) noexcept {
    return look == x3::temporal::AgxLook::golden ? "golden" : look == x3::temporal::AgxLook::punchy ? "punchy" : "none";
}
const char* hdr_decode_name(x3::temporal::AgxDecode decode) noexcept {
    return decode == x3::temporal::AgxDecode::srgb ? "srgb" : decode == x3::temporal::AgxDecode::none ? "none" : "gamma2.2";
}
const char* hdr_exposure_name(ExposureMode mode) noexcept { return mode == ExposureMode::Manual ? "manual" : "auto"; }

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
    // Stage 2: the meter's c0..c3 and the tonemap's c8..c21 (saved as one
    // block, only when a stage-2 program runs; the identity path is unchanged).
    bool constants_saved = false;
    float constants[HdrPass::constant_count][4]{};
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
    drop(shader_); drop(writeback_dither_shader_); drop(tonemap_shader_); drop(meter_level0_shader_); drop(meter_reduce_shader_);
    drop(sharpen_shader_); drop(tonemap_sharpen_shader_); drop(quad_vs_); drop(quad_declaration_);
    device_ = nullptr; native_ = nullptr;
    caps_ = HdrCaps{};
    tonemap_failures_ = 0; sharpen_failures_ = 0; latch_ticks_ = 0;
    exposure_.reset();
}

void HdrPass::release_target() noexcept {
    drop(target_);
    width_ = height_ = 0;
    release_chain();
}

void HdrPass::release_chain() noexcept {
    for (unsigned i = 0; i < chain_max_levels; ++i) { drop(chain_[i]); chain_width_[i] = chain_height_[i] = 0; }
    chain_count_ = 0; tile_width_ = tile_height_ = 0;
    for (unsigned i = 0; i < 2; ++i) { drop(chain_ring_[i]); drop(chain_readback_[i]); chain_pending_[i] = false; }
}

// Levels, the two ring targets and the two readback surfaces, at the chain
// format's texel size (the host copies are not device memory).
std::uint64_t HdrPass::chain_bytes() const noexcept {
    std::uint64_t bytes = 0;
    for (unsigned i = 0; i < chain_count_; ++i) bytes += std::uint64_t(chain_width_[i]) * chain_height_[i] * chain_texel_bytes_;
    if (chain_ring_[0]) bytes += 4ull * tile_width_ * tile_height_ * chain_texel_bytes_;
    return bytes;
}

// The chain's geometry for a `width` x `height` scene: 4x per axis per
// level until neither axis exceeds kMeterTileMax; that level is the tile
// image (the ring), the ones before it the chain levels. Returns the number
// of chain levels (max_levels + 1 when the scene needs more).
static unsigned chain_geometry(UINT width, UINT height, UINT levels_w[], UINT levels_h[], unsigned max_levels, UINT* tile_w, UINT* tile_h) noexcept {
    UINT w = width, h = height; unsigned count = 0;
    for (;;) {
        const UINT nw = (w + 3) / 4, nh = (h + 3) / 4;
        if (nw <= kMeterTileMax && nh <= kMeterTileMax) { *tile_w = nw; *tile_h = nh; return count; }
        if (count >= max_levels) return max_levels + 1;
        levels_w[count] = nw; levels_h[count] = nh; ++count;
        w = nw; h = nh;
    }
}

unsigned HdrPass::references() const noexcept {
    unsigned n = (target_ ? 1u : 0u) + (shader_ ? 1u : 0u) + (writeback_dither_shader_ ? 1u : 0u) + (tonemap_shader_ ? 1u : 0u)
        + (meter_level0_shader_ ? 1u : 0u) + (meter_reduce_shader_ ? 1u : 0u) + chain_count_
        + (sharpen_shader_ ? 1u : 0u) + (tonemap_sharpen_shader_ ? 1u : 0u)
        + (quad_vs_ ? 1u : 0u) + (quad_declaration_ ? 1u : 0u);
    for (unsigned i = 0; i < 2; ++i) n += (chain_ring_[i] ? 1u : 0u) + (chain_readback_[i] ? 1u : 0u);
    return n;
}

// The meter chain for a `width` x `height` scene: two-channel float
// render-target levels of ceil(size / 4) per axis down to the level before
// the tile image, then the two tile-image ring targets, their system-memory
// readback surfaces and the host copies. Only level-0 surfaces are retained
// (one device reference each, in both reference models); a failure releases
// everything and disables the meter.
HRESULT HdrPass::ensure_chain(UINT width, UINT height) noexcept {
    if (!caps_.meter || !device_) return S_FALSE;
    UINT levels_w[chain_max_levels]{}, levels_h[chain_max_levels]{}, tile_w = 0, tile_h = 0;
    const unsigned count = chain_geometry(width, height, levels_w, levels_h, chain_max_levels, &tile_w, &tile_h);
    if (chain_ring_[0] && chain_ring_[1] && chain_readback_[0] && chain_readback_[1]) {
        // Sized for this scene already?
        bool same = count == chain_count_ && tile_w == tile_width_ && tile_h == tile_height_;
        for (unsigned i = 0; same && i < count; ++i) same = chain_[i] && chain_width_[i] == levels_w[i] && chain_height_[i] == levels_h[i];
        if (same) return S_OK;
    }
    release_chain();
    HRESULT hr = count > chain_max_levels ? E_OUTOFMEMORY : S_OK;
    auto level = [&](UINT w, UINT h, IDirect3DSurface9** out) {
        IDirect3DTexture9* texture = nullptr;
        HRESULT r = call<CreateTextureFn>(CreateTexture)(device_, w, h, 1, D3DUSAGE_RENDERTARGET, caps_.chain_format, D3DPOOL_DEFAULT, &texture, nullptr);
        if (SUCCEEDED(r) && texture) r = texture->GetSurfaceLevel(0, out);
        else if (SUCCEEDED(r)) r = E_FAIL;
        drop(texture);
        return r;
    };
    for (unsigned i = 0; SUCCEEDED(hr) && i < count; ++i) {
        hr = level(levels_w[i], levels_h[i], &chain_[chain_count_]);
        if (SUCCEEDED(hr)) { chain_width_[chain_count_] = levels_w[i]; chain_height_[chain_count_] = levels_h[i]; ++chain_count_; }
    }
    for (unsigned i = 0; SUCCEEDED(hr) && i < 2; ++i) {
        hr = level(tile_w, tile_h, &chain_ring_[i]);
        if (SUCCEEDED(hr)) hr = call<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, tile_w, tile_h, caps_.chain_format, D3DPOOL_SYSTEMMEM, &chain_readback_[i], nullptr);
    }
    if (SUCCEEDED(hr)) {
        tile_width_ = tile_w; tile_height_ = tile_h;
        const unsigned tiles = tile_w * tile_h;
        if (tiles > tile_capacity_) {
            tile_mean_.reset(new (std::nothrow) float[tiles]); tile_max_.reset(new (std::nothrow) float[tiles]);
            tile_weight_.reset(new (std::nothrow) float[tiles]); tile_scratch_.reset(new (std::nothrow) TileSample[tiles]);
            if (!tile_mean_ || !tile_max_ || !tile_weight_ || !tile_scratch_) { tile_mean_.reset(); tile_max_.reset(); tile_weight_.reset(); tile_scratch_.reset(); tile_capacity_ = 0; hr = E_OUTOFMEMORY; }
            else tile_capacity_ = tiles;
        }
        if (SUCCEEDED(hr)) tile_weights(tile_weight_.get(), tile_w, tile_h, config_.params.meter_edge_weight);
    }
    if (FAILED(hr)) { release_chain(); caps_.meter = false; caps_.meter_reason = "chain"; }
    return hr;
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
    // A dimension change invalidates the exposure state (section 5); a Reset
    // at the same size keeps it.
    if (last_width_ && (last_width_ != width || last_height_ != height)) exposure_.reset();
    width_ = last_width_ = width; height_ = last_height_ = height;
    if (caps_.meter) ensure_chain(width, height); // a failure disables the meter, not the feature
    return S_OK;
}

// Surface-only ownership matches ensure_target. Validate everything before the
// pointer exchange; unlike release_target/ensure_target this must not discard a
// pending meter sample, its chain, the exposure state or any shader resources.
HRESULT HdrPass::exchange_target(IDirect3DSurface9*& candidate) noexcept {
    if (!device_ || !native_ || !enabled() || !target_ || !width_ || !height_
            || !candidate) return E_INVALIDARG;
    bool equal = false;
    HRESULT hr = same_com_object(candidate, target_, equal);
    if (FAILED(hr)) return hr;
    if (equal) return E_INVALIDARG;
    const auto compatible = [&](const D3DSURFACE_DESC& d) noexcept {
        return d.Type == D3DRTYPE_SURFACE && d.Format == D3DFMT_A16B16G16R16F
            && d.Usage == D3DUSAGE_RENDERTARGET && d.Pool == D3DPOOL_DEFAULT
            && d.MultiSampleType == D3DMULTISAMPLE_NONE && !d.MultiSampleQuality
            && d.Width == width_ && d.Height == height_;
    };
    D3DSURFACE_DESC surface_desc{};
    hr = candidate->GetDesc(&surface_desc);
    if (FAILED(hr)) return hr;
    if (!compatible(surface_desc)) return E_INVALIDARG;
    IDirect3DDevice9* owner = nullptr;
    hr = candidate->GetDevice(&owner);
    if (SUCCEEDED(hr)) hr = same_com_object(owner, device_, equal);
    drop(owner);
    if (FAILED(hr)) return hr;
    if (!equal) return E_INVALIDARG;
    IDirect3DTexture9* texture = nullptr;
    hr = candidate->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&texture));
    if (SUCCEEDED(hr) && !texture) hr = E_NOINTERFACE;
    D3DSURFACE_DESC texture_desc{};
    IDirect3DSurface9* level = nullptr;
    if (SUCCEEDED(hr) && texture->GetLevelCount() != 1) hr = E_INVALIDARG;
    if (SUCCEEDED(hr)) hr = texture->GetLevelDesc(0, &texture_desc);
    if (SUCCEEDED(hr) && !compatible(texture_desc)) hr = E_INVALIDARG;
    if (SUCCEEDED(hr)) {
        hr = texture->GetDevice(&owner);
        if (SUCCEEDED(hr)) hr = same_com_object(owner, device_, equal);
        if (SUCCEEDED(hr) && !equal) hr = E_INVALIDARG;
    }
    if (SUCCEEDED(hr)) {
        hr = texture->GetSurfaceLevel(0, &level);
        if (SUCCEEDED(hr)) hr = same_com_object(level, candidate, equal);
        if (SUCCEEDED(hr) && !equal) hr = E_INVALIDARG;
    }
    drop(level); drop(owner); drop(texture);
    if (FAILED(hr)) return hr;
    IDirect3DSurface9* previous = target_;
    target_ = candidate; candidate = previous;
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
    if (saved.constants_saved && FAILED(hr = call<GetPsConstFn>(GetPixelShaderConstantF)(device_, 0, &saved.constants[0][0], constant_count))) return hr;
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
    if (saved.constants_saved) step(call<SetPsConstFn>(SetPixelShaderConstantF)(device_, 0, &saved.constants[0][0], constant_count));
    return first;
}

// The write-back draw: `source_texture` (level 0 = `source_target`, which must
// not be bound as a target meanwhile) sampled point-wise into `destination`
// through the embedded ps_3_0 identity copy or, with `program`, the given
// program (the AgX tonemap with its c8..c21 block) preceded by the meter
// chain when the program asks for it. Returns the operation result;
// *restoration receives the restoration result separately; RT0 ends bound
// to final_rt0. The meter's failure is reported in the program, never as the
// draw's (the image does not depend on it).
HRESULT HdrPass::copy_draw(IDirect3DSurface9* source_target, IDirect3DTexture9* source_texture, IDirect3DSurface9* destination,
                           UINT width, UINT height, IDirect3DSurface9* final_rt0, HRESULT* restoration,
                           HRESULT injected_draw, bool injected_restore, Program* program) noexcept {
    (void)source_target;
    *restoration = S_OK;
    SavedState saved;
    // Only a program that uploads constants (AgX c8..c21, sharpen c23, the
    // meter's c0..c3) saves and restores them; the identity programs read none.
    saved.constants_saved = program && (program->constants || program->sharpen || program->meter);
    HRESULT hr = save(saved);
    if (FAILED(hr)) return hr;
    HRESULT op = S_OK;
    auto step = [&](HRESULT result) { if (SUCCEEDED(op) && FAILED(result)) op = result; };
    step(call<SetTextureFn>(SetTexture)(device_, 0, nullptr));
    // RT1.. are unbound before RT0 changes: D3D9 requires every bound target
    // to match RT0's dimensions, and the destination may differ from RT1's.
    for (unsigned i = 1; i < saved.target_count; ++i) step(call<SetRtFn>(SetRenderTarget)(device_, i, nullptr));
    step(call<SetDepthFn>(SetDepthStencilSurface)(device_, nullptr));
    step(bind_quad_program());
    step(call<SetFreqFn>(SetStreamSourceFreq)(device_, 0, 1));
    for (unsigned i = 0; i < sampler_count; ++i) step(call<SetSamplerFn>(SetSamplerState)(device_, 0, touched_samplers[i], sampler_values[i]));
    for (unsigned i = 0; i < 2; ++i) step(call<SetStageFn>(SetTextureStageState)(device_, 0, touched_stages[i], stage_values[i]));
    for (unsigned i = 0; i < touched_count; ++i) step(call<SetRsFn>(SetRenderState)(device_, touched_states[i], touched_values[i]));
    // Stage 2, section 4 order: the meter chain reads the FP16 scene before
    // the tonemap writes the 8-bit image (its result is consumed next frame).
    if (SUCCEEDED(op) && program && program->meter) {
        const std::uint64_t begin = stamp(program->timing);
        if (sync_marks_) sync_marks_->begin(gpu_sync_timing::Meter); // --gpu-sync-timing only
        program->meter_result = meter_chain(source_texture, width, height, fault(HdrFault::Meter) ? E_FAIL : S_OK);
        if (sync_marks_) sync_marks_->end(gpu_sync_timing::Meter);
        program->ticks_meter = stamp(program->timing) - begin;
        step(call<SetTextureFn>(SetTexture)(device_, 0, nullptr));
    }
    step(call<SetRtFn>(SetRenderTarget)(device_, 0, destination));
    const D3DVIEWPORT9 viewport{0, 0, width, height, 0.f, 1.f};
    step(call<SetViewportFn>(SetViewport)(device_, &viewport));
    step(call<SetPsFn>(SetPixelShader)(device_, program && program->shader ? program->shader : shader_));
    if (program && program->constants)
        step(call<SetPsConstFn>(SetPixelShaderConstantF)(device_, x3::temporal::kAgxFirstRegister, program->constants, x3::temporal::kAgxRegisterCount));
    if (program && program->sharpen)
        step(call<SetPsConstFn>(SetPixelShaderConstantF)(device_, x3::temporal::kSharpenRegister, program->sharpen, 1));
    step(call<SetTextureFn>(SetTexture)(device_, 0, source_texture));
    if (SUCCEEDED(op)) {
        // The -0.5 pixel shift of quad_vertices covers every texel centre and
        // lands TEXCOORD0 on texel centres under point sampling.
        if (FAILED(injected_draw)) op = injected_draw;   // fixture seam: the draw "failed" and nothing was written
        else step(quad(width, height));
    }
    *restoration = restore(saved, final_rt0);
    if (injected_restore && SUCCEEDED(*restoration)) *restoration = E_FAIL;  // fixture seam: reported after the actual restoration
    return op;
}

// The exposure meter (section 3, the space-aware statistic): level 0 folds
// the log2 luminance of the decoded scene into the first 4x reduction (mean
// and maximum), the reduce program reduces the remaining levels by four per
// axis, the last draw lands in the current ring target (the tile image) and
// GetRenderTargetData queues its copy into the ring's system-memory surface
// (locked at the next latch: never on this frame).
// Runs inside copy_draw's state bracket: FVF, samplers, stage and render
// states are already set; RT1.. and the depth surface are unbound; every
// level binds RT0, viewport, program, constants and the previous level (its
// texture container obtained per use) and draws the -0.5 quad.
HRESULT HdrPass::meter_chain(IDirect3DTexture9* scene_texture, UINT width, UINT height, HRESULT injected) noexcept {
    if (!chain_ring_[chain_slot_] || !chain_readback_[chain_slot_] || !meter_level0_shader_ || !meter_reduce_shader_) return D3DERR_NOTAVAILABLE;
    if (FAILED(injected)) return injected;
    HRESULT op = S_OK;
    auto step = [&](HRESULT result) { if (SUCCEEDED(op) && FAILED(result)) op = result; };
    IDirect3DBaseTexture9* source = scene_texture;
    IDirect3DTexture9* previous = nullptr;
    UINT sw = width, sh = height;
    for (unsigned i = 0; SUCCEEDED(op) && i <= chain_count_; ++i) {
        const bool last = i == chain_count_;
        IDirect3DSurface9* dst = last ? chain_ring_[chain_slot_] : chain_[i];
        const UINT dw = last ? tile_width_ : chain_width_[i], dh = last ? tile_height_ : chain_height_[i];
        step(call<SetRtFn>(SetRenderTarget)(device_, 0, dst));
        const D3DVIEWPORT9 viewport{0, 0, dw, dh, 0.f, 1.f};
        step(call<SetViewportFn>(SetViewport)(device_, &viewport));
        step(call<SetPsFn>(SetPixelShader)(device_, i == 0 ? meter_level0_shader_ : meter_reduce_shader_));
        MeterConstants c{};
        c.source[0] = float(sw); c.source[1] = float(sh); c.source[2] = 1.f / float(sw); c.source[3] = 1.f / float(sh);
        for (unsigned k = 0; k < 4; ++k) c.decode[k] = agx_.decode[k];
        c.meter[0] = config_.params.meter_floor; c.meter[1] = config_.params.meter_clip;
        c.output[0] = float(dw); c.output[1] = float(dh);
        step(call<SetPsConstFn>(SetPixelShaderConstantF)(device_, 0, &c.source[0], 4));
        step(call<SetTextureFn>(SetTexture)(device_, 0, source));
        if (SUCCEEDED(op)) step(quad(dw, dh));
        drop(previous);
        if (!last) {
            HRESULT hr = dst->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&previous));
            if (SUCCEEDED(hr) && !previous) hr = E_NOINTERFACE;
            step(hr);
            source = previous;
        }
        sw = dw; sh = dh;
    }
    drop(previous);
    // The ring slot now holds this frame's meter; its copy to system memory
    // is issued at the next latch (begin_frame), a Present later, so the
    // backend's download never waits on this frame's queued work (measured:
    // an immediate GetRenderTargetData here cost ~0.7 ms per frame on
    // WineD3D at every size, the deferred one microseconds).
    chain_pending_[chain_slot_] = SUCCEEDED(op);
    return op;
}

// Binds the quad's vertex program and declaration (or, in fixture builds that
// selected the twin, the pre-transformed fixed-function path). Saved and
// restored by SavedState like every other binding.
HRESULT HdrPass::bind_quad_program() noexcept {
    if (quad_fvf_) {
        const HRESULT hr = call<SetVsFn>(SetVertexShader)(device_, nullptr);
        return FAILED(hr) ? hr : call<SetFvfFn>(SetFVF)(device_, quad_fvf);
    }
    const HRESULT hr = call<SetDeclarationFn>(SetVertexDeclaration)(device_, quad_declaration_);
    return FAILED(hr) ? hr : call<SetVsFn>(SetVertexShader)(device_, quad_vs_);
}
// The full-target strip (quad_vertex_program.h: clip space with the -0.5
// pixel shift, or the twin's raster coordinates) into the bound viewport.
HRESULT HdrPass::quad(UINT width, UINT height) noexcept {
    QuadVertex vertices[4];
    if (quad_fvf_) quad_vertices_xyzrhw(width, height, vertices); else quad_vertices(width, height, vertices);
    return call<DrawUpFn>(DrawPrimitiveUP)(device_, D3DPT_TRIANGLESTRIP, 2, vertices, sizeof vertices[0]);
}

// Fullscreen strip with `shader` bound into RT0/RT1/RT2 (null unbinds),
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
    step(bind_quad_program());
    step(call<SetPsFn>(SetPixelShader)(device_, shader));
    step(call<SetFreqFn>(SetStreamSourceFreq)(device_, 0, 1));
    for (unsigned i = 0; i < touched_count; ++i) step(call<SetRsFn>(SetRenderState)(device_, touched_states[i], touched_values[i]));
    if (additive) {
        step(call<SetRsFn>(SetRenderState)(device_, D3DRS_ALPHABLENDENABLE, TRUE));
        step(call<SetRsFn>(SetRenderState)(device_, D3DRS_SRCBLEND, D3DBLEND_ONE));
        step(call<SetRsFn>(SetRenderState)(device_, D3DRS_DESTBLEND, D3DBLEND_ONE));
        step(call<SetRsFn>(SetRenderState)(device_, D3DRS_BLENDOP, D3DBLENDOP_ADD));
    }
    if (SUCCEEDED(op)) step(quad(width, height));
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
    HRESULT tonemap = S_FALSE, meter = S_FALSE;
    unsigned scene_errors = 0, sum_errors = 0, motion_errors = 0, depth_errors = 0, copy_errors = 0, stretch_errors = 0, tonemap_errors = 0, meter_errors = 0;
    float meter_value = 0.f, meter_max = 0.f, meter_expected = 0.f;
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
        // Stage 2 (gated inside the feature): the AgX program must draw the
        // sum with the alpha carried and one value on every pixel; the meter
        // chain must reduce the 4x4 sum to the host reference of its clipped
        // log2 luminance in both channels, the mean and the maximum (exactly
        // 6.0 for gamma2.2/sRGB: 322 clipped to 64).
        // A failure demotes the tonemap or the meter to identity/manual and
        // never refuses the feature.
        if (caps_.tonemap && tonemap_shader_) {
            stage = "tonemap";
            IDirect3DSurface9* current = nullptr;
            if (FAILED(hr = call<GetRtFn>(GetRenderTarget)(device_, 0, &current)) || !current) { if (SUCCEEDED(hr)) hr = E_FAIL; break; }
            x3::temporal::AgxConstants constants{};
            x3::temporal::prepare(constants, 1.f, config_.clamp_max, config_.decode, config_.look);
            Program program; program.shader = tonemap_shader_; program.constants = &constants.exposure[0];
            HRESULT tonemap_restore = S_OK;
            tonemap = copy_draw(scene_surface, scene, color, 4, 4, current, &tonemap_restore, S_OK, false, &program);
            drop(current);
            if (FAILED(tonemap_restore)) { hr = tonemap_restore; stage = "restore"; break; }
            if (SUCCEEDED(tonemap)) {
                if (FAILED(hr = call<GetRtDataFn>(GetRenderTargetData)(device_, color, color_copy))) break;
                if (FAILED(hr = color_copy->LockRect(&lock, nullptr, D3DLOCK_READONLY))) break;
                DWORD first = 0;
                for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
                    DWORD value = 0; std::memcpy(&value, static_cast<const char*>(lock.pBits) + y * lock.Pitch + x * 4, 4);
                    if (!x && !y) first = value;
                    const int a = int(value >> 24);
                    if (value != first || a < 126 || a > 130 || !(value & 0x00ffffffu)) ++tonemap_errors;
                }
                color_copy->UnlockRect();
            }
            if (FAILED(tonemap) || tonemap_errors) { caps_.tonemap = false; caps_.tonemap_reason = "self_test"; }
        }
        if (caps_.meter && meter_level0_shader_) {
            stage = "meter";
            IDirect3DTexture9* one = nullptr; IDirect3DSurface9* one_surface = nullptr; IDirect3DSurface9* one_copy = nullptr;
            IDirect3DSurface9* saved_ring = chain_ring_[chain_slot_]; IDirect3DSurface9* saved_readback = chain_readback_[chain_slot_];
            IDirect3DSurface9* saved_levels[chain_max_levels]; const unsigned saved_count = chain_count_;
            for (unsigned i = 0; i < chain_max_levels; ++i) saved_levels[i] = chain_[i];
            const bool saved_pending = chain_pending_[chain_slot_];
            const UINT saved_tile_w = tile_width_, saved_tile_h = tile_height_;
            meter = call<CreateTextureFn>(CreateTexture)(device_, 1, 1, 1, D3DUSAGE_RENDERTARGET, caps_.chain_format, D3DPOOL_DEFAULT, &one, nullptr);
            if (SUCCEEDED(meter) && one) meter = one->GetSurfaceLevel(0, &one_surface);
            if (SUCCEEDED(meter)) meter = call<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, 1, 1, caps_.chain_format, D3DPOOL_SYSTEMMEM, &one_copy, nullptr);
            if (SUCCEEDED(meter)) {
                // A 4x4 scene reduces in one level-0 draw straight into a 1x1
                // tile image: run the production chain code on a temporary ring slot.
                chain_ring_[chain_slot_] = one_surface; chain_readback_[chain_slot_] = one_copy; chain_count_ = 0; tile_width_ = tile_height_ = 1;
                IDirect3DSurface9* current = nullptr;
                if (FAILED(meter = call<GetRtFn>(GetRenderTarget)(device_, 0, &current)) || !current) { if (SUCCEEDED(meter)) meter = E_FAIL; }
                else {
                    Program program; program.shader = shader_; program.meter = true;
                    HRESULT meter_restore = S_OK;
                    meter = copy_draw(scene_surface, scene, color, 4, 4, current, &meter_restore, S_OK, false, &program);
                    if (SUCCEEDED(meter)) meter = program.meter_result;
                    if (SUCCEEDED(meter) && FAILED(meter_restore)) meter = meter_restore;
                    drop(current);
                }
                chain_ring_[chain_slot_] = saved_ring; chain_readback_[chain_slot_] = saved_readback; chain_count_ = saved_count;
                for (unsigned i = 0; i < chain_max_levels; ++i) chain_[i] = saved_levels[i];
                chain_pending_[chain_slot_] = saved_pending; tile_width_ = saved_tile_w; tile_height_ = saved_tile_h;
                if (SUCCEEDED(meter)) meter = call<GetRtDataFn>(GetRenderTargetData)(device_, one_surface, one_copy);
                if (SUCCEEDED(meter) && SUCCEEDED(meter = one_copy->LockRect(&lock, nullptr, D3DLOCK_READONLY))) {
                    float channels[2] = {0.f, 0.f};
                    std::memcpy(channels, lock.pBits, 8);
                    meter = one_copy->UnlockRect();
                    if (SUCCEEDED(meter) && fault(HdrFault::MeterTestUnlock)) meter = E_FAIL;
                    meter_value = channels[0]; meter_max = channels[1];
                    const float sum[3] = {4.f, 16.f, 1.f};
                    const ExposureDecode decode = config_.decode == x3::temporal::AgxDecode::none ? ExposureDecode::None
                        : config_.decode == x3::temporal::AgxDecode::srgb ? ExposureDecode::Srgb : ExposureDecode::Gamma22;
                    meter_expected = meter_level0(sum, decode, config_.params);
                    if (!(std::fabs(meter_value - meter_expected) <= 1e-4f)) ++meter_errors;
                    if (!(std::fabs(meter_max - meter_expected) <= 1e-4f)) ++meter_errors;
                }
            }
            drop(one_copy); drop(one_surface); drop(one);
            if (FAILED(meter) || meter_errors) { caps_.meter = false; caps_.meter_reason = "self_test"; }
        }
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
    std::snprintf(detail, detail_size, "stage=%s result=%08lx draw=%08lx blend=%08lx copy=%08lx restore=%08lx copy_restore=%08lx scene=%08lx stretch=%08lx scene_errors=%u sum_errors=%u motion_errors=%u depth_errors=%u copy_errors=%u stretch_errors=%u targets=%u tonemap=%08lx tonemap_errors=%u meter=%08lx meter_errors=%u meter_value=%.5f meter_max=%.5f meter_expected=%.5f",
                  stage, hr, draw, blend, copy, restore_hr, copy_restore, scene_hr, stretch, scene_errors, sum_errors, motion_errors, depth_errors, copy_errors, stretch_errors, with_depth ? 3u : 2u,
                  tonemap, tonemap_errors, meter, meter_errors, double(meter_value), double(meter_max), double(meter_expected));
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
    if (!std::strcmp(reason, "ok") && D3DSHADER_VERSION_MAJOR(caps.VertexShaderVersion) < 3) reason = "vs_version";
    if (!std::strcmp(reason, "ok")) {
        // The quad's vs_3_0 pass-through and its declaration (every draw below
        // and every write-back binds them; the fixture twin keeps the XYZRHW path).
        quad_fvf_ = quad_fvf_requested();
        HRESULT hr = call<CreateVsFn>(CreateVertexShader)(device_, reinterpret_cast<const DWORD*>(quad_vertex_program()), &quad_vs_);
        if (SUCCEEDED(hr)) hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(device_, quad_declaration, &quad_declaration_);
        if (FAILED(hr) || !quad_vs_ || !quad_declaration_) { reason = "quad_shader"; std::snprintf(caps_.self_test_detail, sizeof caps_.self_test_detail, "create=%08lx", hr); drop(quad_vs_); drop(quad_declaration_); }
    }
    if (!std::strcmp(reason, "ok")) {
        const HRESULT hr = call<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(hdr_writeback_program()), &shader_);
        if (FAILED(hr) || !shader_) { reason = "shader"; std::snprintf(caps_.self_test_detail, sizeof caps_.self_test_detail, "create=%08lx", hr); drop(shader_); }
    }
    // Stage 2: the tonemap program and, with auto exposure, the meter chain
    // (two-channel float render-target textures sampled by the reduce
    // program: G32R32F, else A32B32G32R32F). Each gates itself; a refusal
    // keeps the identity write-back (X3M_HDR stays on).
    if (!std::strcmp(reason, "ok") && config_.tonemap == HdrTonemap::Agx) {
        caps_.tonemap_shader = fault(HdrFault::TonemapShader) ? E_FAIL
            : call<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(hdr_tonemap_program()), &tonemap_shader_);
        if (FAILED(caps_.tonemap_shader) || !tonemap_shader_) { drop(tonemap_shader_); caps_.tonemap = false; caps_.tonemap_reason = "shader"; }
        else { caps_.tonemap = true; caps_.tonemap_reason = "ok"; }
        if (caps_.tonemap && config_.meter_requested()) {
            IDirect3D9* factory = nullptr; D3DDEVICE_CREATION_PARAMETERS creation{}; D3DDISPLAYMODE mode{};
            if (SUCCEEDED(call<GetDirect3DFn>(GetDirect3D)(device_, &factory)) && factory
                && SUCCEEDED(call<GetCreationFn>(GetCreationParameters)(device_, &creation)) && SUCCEEDED(call<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &mode))) {
                const D3DFORMAT candidates[2] = {D3DFMT_G32R32F, D3DFMT_A32B32G32R32F};
                const char* names[2] = {"G32R32F", "A32B32G32R32F"};
                for (unsigned i = 0; i < 2; ++i) {
                    caps_.chain_target = factory->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, mode.Format, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, candidates[i]);
                    caps_.chain_sampling = SUCCEEDED(caps_.chain_target) ? factory->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, mode.Format, 0, D3DRTYPE_TEXTURE, candidates[i]) : S_FALSE;
                    if (SUCCEEDED(caps_.chain_target) && SUCCEEDED(caps_.chain_sampling)) { caps_.chain_format = candidates[i]; caps_.chain_format_name = names[i]; break; }
                }
            } else { caps_.chain_target = E_FAIL; caps_.chain_sampling = E_FAIL; }
            drop(factory);
            chain_texel_bytes_ = caps_.chain_format == D3DFMT_G32R32F ? 8u : 16u;
            if (FAILED(caps_.chain_target)) caps_.meter_reason = "chain_target";
            else if (FAILED(caps_.chain_sampling)) caps_.meter_reason = "chain_sampling";
            else {
                caps_.meter_shader = call<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(hdr_meter_level0_program()), &meter_level0_shader_);
                if (SUCCEEDED(caps_.meter_shader)) caps_.meter_shader = call<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(hdr_meter_reduce_program()), &meter_reduce_shader_);
                if (FAILED(caps_.meter_shader) || !meter_level0_shader_ || !meter_reduce_shader_) { drop(meter_level0_shader_); drop(meter_reduce_shader_); caps_.meter_reason = "shader"; }
                else { caps_.meter = true; caps_.meter_reason = "ok"; }
            }
        }
    }
    // Post-resolve sharpen: the identity+RCAS program always, the AgX+RCAS
    // program with the tonemap; a creation failure keeps both write-backs
    // unsharpened (X3M_HDR and the tonemap stay on).
    if (!std::strcmp(reason, "ok") && config_.sharpen > 0.f) {
        caps_.sharpen_shader = call<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(taa_sharpen_program()), &sharpen_shader_);
        if (SUCCEEDED(caps_.sharpen_shader) && caps_.tonemap)
            caps_.sharpen_shader = call<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(hdr_tonemap_sharpen_program()), &tonemap_sharpen_shader_);
        if (FAILED(caps_.sharpen_shader) || !sharpen_shader_ || (caps_.tonemap && !tonemap_sharpen_shader_)) {
            drop(sharpen_shader_); drop(tonemap_sharpen_shader_); caps_.sharpen = false; caps_.sharpen_reason = "shader";
        } else { caps_.sharpen = true; caps_.sharpen_reason = "ok"; }
    }
    // Display dither: the AgX and RCAS programs read their amplitude from
    // c8.z / c23.w; only the identity write-back needs its dithered twin.
    // A creation failure keeps the identity draws undithered.
    if (!std::strcmp(reason, "ok") && config_.dither) {
        caps_.dither = true;
        caps_.dither_shader = call<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(hdr_writeback_dither_program()), &writeback_dither_shader_);
        if (FAILED(caps_.dither_shader) || !writeback_dither_shader_) { drop(writeback_dither_shader_); caps_.dither_reason = "shader"; }
        else caps_.dither_reason = "ok";
    }
    exposure_.configure(config_.params, config_.exposure, config_.ev_manual);
    exposure_.reset();
    tonemap_failures_ = 0; sharpen_failures_ = 0; latch_ticks_ = 0; chain_slot_ = 0;
    if (!std::strcmp(reason, "ok")) {
        caps_.self_test_targets = with_depth ? 3u : 2u;
        if (!self_test(with_depth, false, caps_.self_test_detail, sizeof caps_.self_test_detail)) reason = "self_test";
    }
    if (!caps_.tonemap) {
        drop(tonemap_shader_); caps_.meter = false;
        if (config_.tonemap == HdrTonemap::Agx && config_.meter_requested() && (!std::strcmp(caps_.meter_reason, "ok") || !std::strcmp(caps_.meter_reason, "off"))) caps_.meter_reason = "tonemap";
    }
    if (!caps_.meter) { drop(meter_level0_shader_); drop(meter_reduce_shader_); }
    prepare_constants();
    caps_.reason = reason;
    caps_.enabled = !std::strcmp(reason, "ok");
    if (!caps_.enabled) { drop(shader_); drop(writeback_dither_shader_); drop(tonemap_shader_); drop(meter_level0_shader_); drop(meter_reduce_shader_); drop(sharpen_shader_); drop(tonemap_sharpen_shader_); drop(quad_vs_); drop(quad_declaration_); caps_.tonemap = caps_.meter = caps_.sharpen = caps_.dither = false; }
}

// The tonemap's constant block for the coming write-back: exp2 of the EV the
// state resolves (manual or adapted), the clamp, decode mode and look. An
// out-of-range exposure (never from the clamped EV range) falls back to 1.
void HdrPass::prepare_constants() noexcept {
    if (!x3::temporal::prepare(agx_, exposure_.exposure(), config_.clamp_max, config_.decode, config_.look))
        x3::temporal::prepare(agx_, 1.f, config_.clamp_max, config_.decode, config_.look);
    x3::temporal::set_dither(agx_, caps_.dither);
}

bool HdrPass::comparison_exposure(ExposureMode mode) noexcept {
    if (!tonemap_active() || !caps_.meter) return false;
    config_.exposure = mode; config_.ev_manual = 0.f;
    exposure_.configure(config_.params, mode, 0.f);
    exposure_.reset();
    // Discard old-mode meter results rather than applying them when AUTO
    // resumes. The first fresh meter then adapts from neutral exposure.
    chain_pending_[0] = chain_pending_[1] = false;
    chain_slot_ = 0; latch_ticks_ = 0;
    prepare_constants();
    return true;
}

// At the latch: the previous frame's tile image (queued into the ring's
// system-memory surface by that frame's chain) is copied and locked now -- a
// frame later, so the lock does not wait on this frame's work -- reduced to
// the space-aware statistic, and the host adaptation step runs on it with
// the QPC interval between the two latches (or the fixed fixture dt). The
// ring slot then advances for this frame's chain and the tonemap constants
// take the new EV.
HdrFrameBegin HdrPass::begin_frame(std::uint64_t now_ticks, std::uint64_t frequency, bool timing) noexcept {
    HdrFrameBegin r{};
    float dt = config_.fixed_dt > 0.f ? config_.fixed_dt
        : (latch_ticks_ && frequency && now_ticks > latch_ticks_) ? float(double(now_ticks - latch_ticks_) / double(frequency)) : config_.params.dt_max;
    latch_ticks_ = now_ticks;
    if (meter_active() && chain_pending_[chain_slot_] && chain_ring_[chain_slot_] && chain_readback_[chain_slot_]) {
        r.readback_timing.begin(timing, stamp(timing));
        D3DLOCKED_RECT lock{};
        const unsigned tiles = tile_width_ * tile_height_;
        r.readback = tiles && tiles <= tile_capacity_ ? call<GetRtDataFn>(GetRenderTargetData)(device_, chain_ring_[chain_slot_], chain_readback_[chain_slot_]) : E_FAIL;
        if (SUCCEEDED(r.readback)) r.readback = chain_readback_[chain_slot_]->LockRect(&lock, nullptr, D3DLOCK_READONLY);
        r.readback_timing.end(ReadbackTiming::TransferLock, stamp(timing));
        if (SUCCEEDED(r.readback)) {
            // Texel .r = the tile's mean, .g = its maximum (the chain format's stride).
            for (UINT y = 0; y < tile_height_; ++y) {
                const char* row = static_cast<const char*>(lock.pBits) + y * lock.Pitch;
                for (UINT x = 0; x < tile_width_; ++x) {
                    float texel[2]; std::memcpy(texel, row + std::size_t(x) * chain_texel_bytes_, 8);
                    tile_mean_[std::size_t(y) * tile_width_ + x] = texel[0]; tile_max_[std::size_t(y) * tile_width_ + x] = texel[1];
                }
            }

            // Publish the statistic only after the complete readback operation
            // succeeds. A failed unlock must not advance adaptation using the
            // copied candidate, even though the earlier copy/lock succeeded.
            r.readback = chain_readback_[chain_slot_]->UnlockRect();
            // Test-only returned-HRESULT injection after actual cleanup; never
            // leave a synthetic mapping locked or overwrite a native failure.
            if (SUCCEEDED(r.readback) && fault(HdrFault::ReadbackUnlock)) r.readback = E_FAIL;
        }
        chain_pending_[chain_slot_] = false;
        r.readback_timing.end(ReadbackTiming::ExtractUnlock, stamp(timing));
        if (SUCCEEDED(r.readback)) {
            // Non-finite tiles read as the floor inside; the statistic is finite.
            const MeterStatistics m = meter_statistics(tile_mean_.get(), tile_max_.get(), tile_weight_.get(), tiles, tile_scratch_.get(), config_.params);
            if (std::isfinite(m.avg_log_l) && std::isfinite(m.lit_median_log) && std::isfinite(m.p99_max_log)) {
                exposure_.step(m, dt);
                r.stepped = true; r.avg_log_l = m.avg_log_l; r.dt = exposure_.dt();
            }
        }
        r.readback_timing.end(ReadbackTiming::StatisticsAdapt, stamp(timing));
        r.ticks_readback = r.readback_timing.total();
    }
    chain_slot_ ^= 1u;
    prepare_constants();
    return r;
}

// The must-unwind ladder. The shader copy is the normal path; a failed draw
// falls to the point StretchRect (when the conversion is granted); a failed
// restoration or StretchRect ends with an explicit rebind of final_rt0 so the
// device never keeps the FP16 surface as RT0 past this call.
HdrWriteback HdrPass::write_back(IDirect3DSurface9* main, IDirect3DSurface9* final_rt0, bool scene_open, bool write, bool timing,
                                 IDirect3DTexture9* source, HdrDisplaySnapshot* display) noexcept {
    if (display) *display = {};
    HdrWriteback result{};
    if (!main || !final_rt0) { result.unwind = true; result.unwind_reason = "arguments"; return result; }
    // Fixture seam: one injected failure per write, consumed only when a copy
    // is attempted so a rebind-only call never eats an armed fault
    // (production: never).
    HRESULT injected_draw = S_OK;
    if (write && target_ && shader_) {
        injected_draw = fault(HdrFault::Draw) ? E_FAIL : fault(HdrFault::Lost) ? D3DERR_DEVICELOST : fault(HdrFault::Stretch) ? E_ABORT : S_OK;
        const bool injected_restore = fault(HdrFault::Restore);
        // The sampled image: the caller's resolved FP16 texture (one reference
        // taken and dropped here, symmetric with the container path) or the
        // target's container.
        IDirect3DTexture9* texture = source;
        const std::uint64_t begin = stamp(timing);
        HRESULT hr = S_OK;
        if (texture) texture->AddRef();
        else {
            hr = target_->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&texture));
            if (SUCCEEDED(hr) && !texture) hr = E_NOINTERFACE;
        }
        bool own_scene = false;
        if (SUCCEEDED(hr) && !scene_open) { hr = call<SceneFn>(BeginScene)(device_); own_scene = SUCCEEDED(hr); }
        // Stage 2: the AgX program with its constants (and the meter chain
        // before it in auto exposure); a failed tonemap draw with a clean
        // restoration takes rung 1b, the identity draw, and is reported as
        // an unwind (reason "tonemap") so the recovery self test runs at the
        // next latch; repeated failures disable the tonemap for the device.
        const bool use_tonemap = tonemap_active();
        // Post-resolve sharpen: only a resolved TAA image is sharpened (the
        // unresolved scene of a failed or absent resolve is written back as
        // it is), with the RCAS variant of the program in use and c23.
        const float sharpen_strength = config_.sharpen;
        const bool sharpen = source != nullptr && sharpen_active() && (!use_tonemap || tonemap_sharpen_shader_)
            && x3::temporal::prepare_sharpen(sharpen_, sharpen_strength, width_, height_);
        // Display dither: c8.z of agx_ (prepare_constants) for the AgX
        // programs, c23.w for the identity+RCAS program, the dithered twin
        // for the identity copy. The AgX+RCAS program ignores c23.w, so the
        // snapshot's c23 stays the plain one.
        if (sharpen && !use_tonemap) sharpen_.values[3] = caps_.dither ? x3::temporal::kDisplayDitherAmplitude : 0.f;
        Program identity; identity.shader = identity_shader();
        if (SUCCEEDED(hr)) {
            Program program;
            if (use_tonemap || sharpen) {
                program.shader = use_tonemap ? (sharpen ? tonemap_sharpen_shader_ : tonemap_shader_) : sharpen_shader_;
                program.constants = use_tonemap ? &agx_.exposure[0] : nullptr;
                program.sharpen = sharpen ? sharpen_.values : nullptr;
                program.meter = use_tonemap && meter_active() && chain_ring_[0] != nullptr; program.timing = timing;
                const HRESULT injected_tonemap = fault(HdrFault::TonemapDraw) ? E_FAIL : injected_draw;
                hr = copy_draw(target_, texture, main, width_, height_, final_rt0, &result.restore, injected_tonemap, injected_restore, &program);
                result.tonemap = use_tonemap; result.sharpened = sharpen; result.tonemap_draw = use_tonemap ? hr : S_FALSE;
                result.meter = program.meter_result; result.ticks_meter = program.ticks_meter;
                if (sharpen && FAILED(hr) && !lost(hr) && SUCCEEDED(result.restore) && SUCCEEDED(injected_draw)) {
                    // The sharpened draw failed with a clean restoration: count it
                    // against the sharpen and redraw unsharpened (the meter, if it
                    // ran, is not repeated); repeated failures disable the sharpen.
                    ++sharpen_failures_; result.sharpened = false; result.sharpen_fallback = true;
                    program.shader = use_tonemap ? tonemap_shader_ : identity.shader; program.sharpen = nullptr; program.meter = false;
                    hr = copy_draw(target_, texture, main, width_, height_, final_rt0, &result.restore, injected_draw, injected_restore, &program);
                    if (use_tonemap) result.tonemap_draw = hr;
                }
                if (use_tonemap && FAILED(hr) && !lost(hr) && SUCCEEDED(result.restore) && SUCCEEDED(injected_draw)) {
                    ++tonemap_failures_;
                    result.tonemap = false; result.fallback = true;
                    hr = copy_draw(target_, texture, main, width_, height_, final_rt0, &result.restore, injected_draw, injected_restore, &identity);
                }
            } else hr = copy_draw(target_, texture, main, width_, height_, final_rt0, &result.restore, injected_draw, injected_restore, &identity);
        }
        result.draw = hr;
        // A scene we opened is always closed, lost device included: the runtime
        // keeps its in-scene flag otherwise and the application's next
        // BeginScene would be refused.
        if (own_scene) { const HRESULT end = call<SceneFn>(EndScene)(device_); if (FAILED(end) && SUCCEEDED(result.draw)) result.draw = end; }
        drop(texture);
        result.ticks_draw = stamp(timing) - begin;
        if (SUCCEEDED(result.draw) && SUCCEEDED(result.restore)) {
            result.source = HdrWritebackSource::Shader;
            // The fallback image is complete and RT0 is final_rt0 already: no rebind.
            if (result.fallback) { result.unwind = true; result.unwind_reason = "tonemap"; }
            if (display && result.tonemap) {
                display->agx = agx_;
                // Derive the specialization from the consumed c9, not config
                // that might have changed since prepare_constants at the latch.
                display->decode = agx_.decode[2] == 1.f ? x3::temporal::AgxDecode::none
                    : agx_.decode[1] == 1.f ? x3::temporal::AgxDecode::srgb : x3::temporal::AgxDecode::gamma22;
                if (result.sharpened) {
                    display->sharpen = sharpen_strength;
                    display->sharpen_constants = sharpen_;
                }
                display->resolved = source != nullptr;
                display->width = width_; display->height = height_;
                display->valid = true;
            }
            return result;
        }
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
