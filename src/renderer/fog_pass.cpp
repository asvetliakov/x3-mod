#include "fog_pass.h"
#include "ambient_occlusion_caps.h" // ambient_occlusion_program_slots: the shared conservative ps_3_0 slot count
#include "quad_vertex_program.h"
#include <cmath>
#include <iterator>
#include <utility>

namespace x3m::renderer {
namespace {
template<class T> void drop(T*& value) noexcept { if (value) { value->Release(); value = nullptr; } }
bool lost(HRESULT hr) noexcept { return hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET; }
// IDirect3DDevice9 vtable slots (verification/probe/abi_check.cpp), as in AmbientOcclusionPass.
enum Slot : unsigned {
    GetDirect3D = 6, GetCreationParameters = 9, CreateTexture = 23, StretchRect = 34, SetRenderTarget = 37, GetRenderTarget = 38,
    SetDepthStencilSurface = 39, GetDepthStencilSurface = 40, BeginScene = 41, EndScene = 42,
    SetViewport = 47, GetViewport = 48, SetRenderState = 57, CreateStateBlock = 59, SetTexture = 65,
    SetTextureStageState = 67, SetSamplerState = 69, SetScissorRect = 75, GetScissorRect = 76, DrawPrimitiveUP = 83,
    CreateVertexDeclaration = 86, SetVertexDeclaration = 87, CreateVertexShader = 91, SetVertexShader = 92,
    SetStreamSourceFreq = 102, SetIndices = 104, CreatePixelShader = 106, SetPixelShader = 107, SetPixelShaderConstantF = 109
};
using D = IDirect3DDevice9*;
using GetD3DFn = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetCreationFn = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);
using CreateTextureFn = HRESULT(WINAPI*)(D, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
using StretchRectFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE);
using SetRtFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DSurface9*);
using GetRtFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DSurface9**);
using SetDepthFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*);
using GetDepthFn = HRESULT(WINAPI*)(D, IDirect3DSurface9**);
using SceneFn = HRESULT(WINAPI*)(D);
using SetViewportFn = HRESULT(WINAPI*)(D, const D3DVIEWPORT9*);
using GetViewportFn = HRESULT(WINAPI*)(D, D3DVIEWPORT9*);
using SetRsFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD);
using CreateBlockFn = HRESULT(WINAPI*)(D, D3DSTATEBLOCKTYPE, IDirect3DStateBlock9**);
using SetTextureFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DBaseTexture9*);
using SetStageFn = HRESULT(WINAPI*)(D, DWORD, D3DTEXTURESTAGESTATETYPE, DWORD);
using SetSamplerFn = HRESULT(WINAPI*)(D, DWORD, D3DSAMPLERSTATETYPE, DWORD);
using SetScissorFn = HRESULT(WINAPI*)(D, const RECT*);
using GetScissorFn = HRESULT(WINAPI*)(D, RECT*);
using DrawUpFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using CreateDeclarationFn = HRESULT(WINAPI*)(D, const D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9**);
using SetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9*);
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using SetFreqFn = HRESULT(WINAPI*)(D, UINT, UINT);
using SetIndicesFn = HRESULT(WINAPI*)(D, IDirect3DIndexBuffer9*);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using SetPsConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
// Only our authored programs are embedded (tools/shaders/generate_rigid_motion_pixel.py).
constexpr DWORD march_words[] = {
#include "fog_march_program_inc.h"
};
constexpr DWORD composite_words[] = {
#include "fog_composite_program_inc.h"
};
constexpr DWORD sky_level0_words[] = {
#include "fog_sky_level0_program_inc.h"
};
constexpr DWORD sky_reduce_words[] = {
#include "fog_sky_reduce_program_inc.h"
};
constexpr UINT sky_level_size = 8, fog_samplers = 4;
template<class Resource> HRESULT same_device(IDirect3DDevice9* device, Resource* resource) noexcept {
    IDirect3DDevice9* owner = nullptr;
    HRESULT hr = resource->GetDevice(&owner);
    if (FAILED(hr)) return hr;
    const bool same = owner == device;
    drop(owner);
    return same ? S_OK : E_INVALIDARG;
}
bool valid_params(const FogParams& p) noexcept {
    for (float v : {p.m00, p.m11, p.m20, p.m21, p.m22, p.m32, p.tau_max, p.radius, p.anisotropy, p.margin, p.sun_view[0], p.sun_view[1], p.sun_view[2],
                    p.sun_radiance[0], p.sun_radiance[1], p.sun_radiance[2], p.decode_exponent, p.sky_blend})
        if (!std::isfinite(v)) return false;
    const float length = p.sun_view[0] * p.sun_view[0] + p.sun_view[1] * p.sun_view[1] + p.sun_view[2] * p.sun_view[2];
    return p.m00 > 0.f && p.m11 > 0.f && p.m22 > 1.f && p.m32 < 0.f && p.tau_max > 0.f && p.tau_max <= fog_strength_max && std::isfinite(1.f / p.tau_max) &&
           p.radius >= 1.f && p.anisotropy >= fog_anisotropy_min && p.anisotropy <= fog_anisotropy_max && p.margin > 0.f && p.margin <= 1.f &&
           std::fabs(length - 1.f) < 1e-3f && p.sun_radiance[0] >= 0.f && p.sun_radiance[1] >= 0.f && p.sun_radiance[2] >= 0.f &&
           p.decode_exponent >= 1.f && p.decode_exponent <= 3.f && p.sky_blend > 0.f && p.sky_blend <= 1.f;
}
} // namespace
// Everything a transaction touches beyond the state block: the target and
// depth bindings, viewport and scissor (SetRenderTarget resets the latter two).
struct FogPass::SavedState {
    const FogPass& pass;
    IDirect3DStateBlock9* block;
    IDirect3DSurface9* targets[4]{};
    IDirect3DSurface9* depth = nullptr;
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    UINT count;
    SavedState(const FogPass& p, IDirect3DStateBlock9* b, UINT n) : pass(p), block(b), count(n) {}
    ~SavedState() { for (auto& t : targets) drop(t); drop(depth); }
    HRESULT capture() noexcept {
        HRESULT hr = block->Capture();
        if (FAILED(hr)) return hr;
        for (UINT i = 0; i < count; ++i) {
            hr = pass.call<GetRtFn>(GetRenderTarget)(pass.device_, i, &targets[i]);
            if (FAILED(hr) && hr != D3DERR_NOTFOUND) return hr;
        }
        hr = pass.call<GetDepthFn>(GetDepthStencilSurface)(pass.device_, &depth);
        if (FAILED(hr) && hr != D3DERR_NOTFOUND) return hr;
        hr = pass.call<GetViewportFn>(GetViewport)(pass.device_, &viewport);
        if (FAILED(hr)) return hr;
        return pass.call<GetScissorFn>(GetScissorRect)(pass.device_, &scissor);
    }
    HRESULT restore() noexcept {
        HRESULT first = S_OK;
        auto attempt = [&](HRESULT hr) { if (lost(hr) || (FAILED(hr) && SUCCEEDED(first))) first = hr; return !lost(hr); };
        D d = pass.device_;
        for (UINT i = 0; i < fog_samplers; ++i) if (!attempt(pass.call<SetTextureFn>(SetTexture)(d, i, nullptr))) return first;
        if (!attempt(pass.call<SetDepthFn>(SetDepthStencilSurface)(d, nullptr))) return first;
        for (UINT i = 1; i < count; ++i) if (!attempt(pass.call<SetRtFn>(SetRenderTarget)(d, i, nullptr))) return first;
        for (UINT i = 0; i < count; ++i) if (!attempt(pass.call<SetRtFn>(SetRenderTarget)(d, i, targets[i]))) return first;
        if (!attempt(pass.call<SetDepthFn>(SetDepthStencilSurface)(d, depth))) return first;
        if (!attempt(block->Apply())) return first;
        if (!attempt(pass.call<SetViewportFn>(SetViewport)(d, &viewport))) return first;
        attempt(pass.call<SetScissorFn>(SetScissorRect)(d, &scissor));
        return first;
    }
};
FogPass::~FogPass() { detach(); }
unsigned FogPass::references() const noexcept {
    unsigned n = 0;
    for (const void* p : {static_cast<const void*>(block_), static_cast<const void*>(march_), static_cast<const void*>(composite_),
                          static_cast<const void*>(sky_level0_), static_cast<const void*>(sky_reduce_), static_cast<const void*>(quad_vs_),
                          static_cast<const void*>(quad_declaration_), static_cast<const void*>(lit_), static_cast<const void*>(scratch_),
                          static_cast<const void*>(sky_level_), static_cast<const void*>(sky_), static_cast<const void*>(lit_surface_),
                          static_cast<const void*>(scratch_surface_), static_cast<const void*>(sky_level_surface_), static_cast<const void*>(sky_surface_)})
        n += p != nullptr;
    return n;
}
void FogPass::release_targets() noexcept {
    drop(lit_surface_); drop(scratch_surface_); drop(sky_level_surface_); drop(sky_surface_);
    drop(lit_); drop(scratch_); drop(sky_level_); drop(sky_);
    width_ = height_ = half_width_ = half_height_ = 0; sky_seeded_ = false;
}
void FogPass::detach() noexcept {
    release_targets(); drop(block_);
    drop(march_); drop(composite_); drop(sky_level0_); drop(sky_reduce_); drop(quad_vs_); drop(quad_declaration_);
    device_ = nullptr; vtable_ = nullptr; render_targets_ = streams_ = 0; reset_pending_ = false;
    caps_ = {};
}
void FogPass::before_reset() noexcept { release_targets(); drop(block_); reset_pending_ = device_ != nullptr; }
void FogPass::after_reset(HRESULT result) noexcept { if (SUCCEEDED(result)) reset_pending_ = false; }
HRESULT FogPass::attach(IDirect3DDevice9* d, void* const* native, const D3DCAPS9& caps, D3DFORMAT adapter_format) noexcept {
    detach();
    if (!d) { caps_.reason = "device"; return E_INVALIDARG; }
    device_ = d; vtable_ = native;
    FogCapabilityInputs in{};
    in.pixel_shader_version = caps.PixelShaderVersion; in.vertex_shader_version = caps.VertexShaderVersion;
    in.ps30_instruction_slots = caps.MaxPixelShader30InstructionSlots;
    in.simultaneous_targets = caps.NumSimultaneousRTs; in.max_streams = caps.MaxStreams;
    for (auto& program : {std::pair{march_words, std::size(march_words)}, std::pair{composite_words, std::size(composite_words)},
                          std::pair{sky_level0_words, std::size(sky_level0_words)}, std::pair{sky_reduce_words, std::size(sky_reduce_words)}}) {
        const std::uint32_t slots = ambient_occlusion_program_slots(reinterpret_cast<const std::uint32_t*>(program.first), program.second);
        if (slots == 0) { in.largest_program_slots = 0; break; }
        if (slots > in.largest_program_slots) in.largest_program_slots = slots;
    }
    caps_.largest_program_slots = in.largest_program_slots;
    in.blend_srcalpha = (caps.SrcBlendCaps & D3DPBLENDCAPS_SRCALPHA) != 0;
    in.blend_invsrcalpha = (caps.DestBlendCaps & D3DPBLENDCAPS_INVSRCALPHA) != 0;
    in.stretch_rect = (caps.DevCaps2 & D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES) != 0;
    IDirect3D9* api = nullptr;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    HRESULT hr = call<GetD3DFn>(GetDirect3D)(d, &api);
    if (SUCCEEDED(hr) && !api) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = call<GetCreationFn>(GetCreationParameters)(d, &creation);
    if (SUCCEEDED(hr)) {
        in.a8r8g8b8_target = api->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, adapter_format, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8);
        in.fp16_target_blending = api->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, adapter_format,
                                                         D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F);
        in.r32f_texture = api->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, adapter_format, 0, D3DRTYPE_TEXTURE, D3DFMT_R32F);
    }
    drop(api);
    caps_.formats = hr;
    if (FAILED(hr)) { caps_.reason = "format_query"; device_ = nullptr; vtable_ = nullptr; return hr; }
    if (const char* reason = fog_capability(in)) {
        caps_.reason = reason; caps_.formats = D3DERR_NOTAVAILABLE; device_ = nullptr; vtable_ = nullptr;
        return D3DERR_NOTAVAILABLE;
    }
    render_targets_ = caps.NumSimultaneousRTs; streams_ = caps.MaxStreams;
    hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(quad_vertex_program()), &quad_vs_);
    if (SUCCEEDED(hr)) hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, quad_declaration, &quad_declaration_);
    if (SUCCEEDED(hr)) hr = call<CreatePsFn>(CreatePixelShader)(d, march_words, &march_);
    if (SUCCEEDED(hr)) hr = call<CreatePsFn>(CreatePixelShader)(d, composite_words, &composite_);
    if (SUCCEEDED(hr)) hr = call<CreatePsFn>(CreatePixelShader)(d, sky_level0_words, &sky_level0_);
    if (SUCCEEDED(hr)) hr = call<CreatePsFn>(CreatePixelShader)(d, sky_reduce_words, &sky_reduce_);
    caps_.programs = hr;
    if (FAILED(hr)) { const unsigned slots = caps_.largest_program_slots; detach(); caps_.reason = "programs"; caps_.programs = hr; caps_.largest_program_slots = slots; return hr; }
    caps_.enabled = true; caps_.reason = "";
    return S_OK;
}
HRESULT FogPass::prepare(UINT w, UINT h) noexcept {
    if (!device_ || !caps_.enabled) return E_INVALIDARG;
    if (reset_pending_) return D3DERR_DEVICENOTRESET;
    if (!w || !h) return E_INVALIDARG;
    if (width_ == w && height_ == h) return S_OK;
    release_targets();
    const UINT hw = (w + 1) / 2, hh = (h + 1) / 2;
    auto create = [&](UINT tw, UINT th, D3DFORMAT format, IDirect3DTexture9*& texture, IDirect3DSurface9*& surface) {
        HRESULT hr = call<CreateTextureFn>(CreateTexture)(device_, tw, th, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, &texture, nullptr);
        if (SUCCEEDED(hr)) hr = texture->GetSurfaceLevel(0, &surface);
        return hr;
    };
    HRESULT hr = create(hw, hh, D3DFMT_A8R8G8B8, lit_, lit_surface_);
    if (SUCCEEDED(hr)) hr = create(w, h, D3DFMT_A16B16G16R16F, scratch_, scratch_surface_);
    if (SUCCEEDED(hr)) hr = create(sky_level_size, sky_level_size, D3DFMT_A16B16G16R16F, sky_level_, sky_level_surface_);
    if (SUCCEEDED(hr)) hr = create(1, 1, D3DFMT_A16B16G16R16F, sky_, sky_surface_);
    if (FAILED(hr)) { release_targets(); return hr; }
    width_ = w; height_ = h; half_width_ = hw; half_height_ = hh; ++allocations_;
    return S_OK;
}
HRESULT FogPass::ensure_block() noexcept {
    if (block_) return S_OK;
    const HRESULT hr = call<CreateBlockFn>(CreateStateBlock)(device_, D3DSBT_ALL, &block_);
    if (FAILED(hr)) drop(block_);
    return hr;
}
HRESULT FogPass::quad(UINT w, UINT h) noexcept {
    QuadVertex vertices[4];
    quad_vertices(w, h, vertices);
    return call<DrawUpFn>(DrawPrimitiveUP)(device_, D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(QuadVertex));
}
HRESULT FogPass::normalize() noexcept {
    D d = device_;
#define STEP(call) do { const HRESULT hresult = (call); if (FAILED(hresult)) return hresult; } while (false)
    for (UINT i = 0; i < 16; ++i) STEP(call<SetTextureFn>(SetTexture)(d, i, nullptr));
    for (UINT i = 0; i < 4; ++i) STEP(call<SetTextureFn>(SetTexture)(d, D3DVERTEXTEXTURESAMPLER0 + i, nullptr));
    STEP(call<SetDepthFn>(SetDepthStencilSurface)(d, nullptr));
    for (UINT i = 1; i < render_targets_; ++i) STEP(call<SetRtFn>(SetRenderTarget)(d, i, nullptr));
    STEP(call<SetDeclarationFn>(SetVertexDeclaration)(d, quad_declaration_));
    STEP(call<SetVsFn>(SetVertexShader)(d, quad_vs_));
    STEP(call<SetIndicesFn>(SetIndices)(d, nullptr));
    for (UINT i = 0; i < streams_; ++i) STEP(call<SetFreqFn>(SetStreamSourceFreq)(d, i, 1));
    for (auto state : {D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_STENCILENABLE, D3DRS_ALPHATESTENABLE,
                       D3DRS_ALPHABLENDENABLE, D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_FOGENABLE,
                       D3DRS_SRGBWRITEENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_CLIPPLANEENABLE,
                       D3DRS_CLIPPING, D3DRS_LIGHTING, D3DRS_INDEXEDVERTEXBLENDENABLE,
                       D3DRS_POINTSPRITEENABLE, D3DRS_DITHERENABLE, D3DRS_ANTIALIASEDLINEENABLE})
        STEP(call<SetRsFn>(SetRenderState)(d, state, FALSE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_VERTEXBLEND, D3DVBF_DISABLE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_WRAP0, 0));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_FILLMODE, D3DFILL_SOLID));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, D3DCULL_NONE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_COLORWRITEENABLE, 15));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_MULTISAMPLEMASK, 0xffffffff));
    // The sky history's blend; enabled for that one quad only.
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_BLENDOP, D3DBLENDOP_ADD));
    STEP(call<SetStageFn>(SetTextureStageState)(d, 0, D3DTSS_TEXCOORDINDEX, 0));
    STEP(call<SetStageFn>(SetTextureStageState)(d, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE));
    for (UINT i = 0; i < fog_samplers; ++i) {
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MINFILTER, D3DTEXF_POINT));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MAGFILTER, D3DTEXF_POINT));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_SRGBTEXTURE, FALSE));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MAXMIPLEVEL, 0));
    }
#undef STEP
    return S_OK;
}
// Unbind the samplers (a previous quad's output may become this target), bind
// the target with its full viewport and the program.
HRESULT FogPass::bind_target(IDirect3DSurface9* target, UINT w, UINT h, IDirect3DPixelShader9* program) noexcept {
    D d = device_;
    HRESULT hr = S_OK;
    for (UINT i = 0; i < fog_samplers && SUCCEEDED(hr); ++i) hr = call<SetTextureFn>(SetTexture)(d, i, nullptr);
    if (SUCCEEDED(hr)) hr = call<SetRtFn>(SetRenderTarget)(d, 0, target);
    D3DVIEWPORT9 viewport{0, 0, w, h, 0, 1};
    if (SUCCEEDED(hr)) hr = call<SetViewportFn>(SetViewport)(d, &viewport);
    if (SUCCEEDED(hr)) hr = call<SetPsFn>(SetPixelShader)(d, program);
    return hr;
}
HRESULT FogPass::execute(const FogFrame& in, FogResult* out) noexcept {
    if (!out) return E_INVALIDARG;
    *out = {};
    calls_ = 0;
    auto fail = [&](FogStage stage, HRESULT hr) { out->failed = stage; out->operation = hr; out->device_calls = calls_; return hr; };
    if (!device_ || !caps_.enabled || !in.depth_share || !in.target || !in.width || !in.height || in.count > fog_cascade_max ||
        in.caller_stateblock_recording || !in.caller_queries_idle || !valid_params(in.params))
        return fail(FogStage::Validate, E_INVALIDARG);
    if (reset_pending_) return fail(FogStage::Validate, D3DERR_DEVICENOTRESET);
    if (in.depth_share == lit_ || in.depth_share == scratch_ || in.depth_share == sky_ || in.depth_share == sky_level_ ||
        in.target == lit_surface_ || in.target == scratch_surface_ || in.target == sky_surface_ || in.target == sky_level_surface_)
        return fail(FogStage::Validate, E_INVALIDARG);
    D3DSURFACE_DESC desc{};
    HRESULT hr = in.depth_share->GetLevelDesc(0, &desc);
    if (SUCCEEDED(hr) && (desc.Width != in.width || desc.Height != in.height || desc.MultiSampleType != D3DMULTISAMPLE_NONE ||
                          (desc.Format != D3DFMT_R32F && desc.Format != D3DFMT_G32R32F && desc.Format != D3DFMT_A32B32G32R32F)))
        hr = E_INVALIDARG;
    const bool view_depth_lane = desc.Format == D3DFMT_A32B32G32R32F;
    if (SUCCEEDED(hr)) hr = same_device(device_, in.depth_share);
    if (SUCCEEDED(hr)) hr = in.target->GetDesc(&desc);
    if (SUCCEEDED(hr) && (desc.Width != in.width || desc.Height != in.height || desc.Format != D3DFMT_A16B16G16R16F ||
                          desc.MultiSampleType != D3DMULTISAMPLE_NONE || !(desc.Usage & D3DUSAGE_RENDERTARGET)))
        hr = E_INVALIDARG;
    if (SUCCEEDED(hr)) hr = same_device(device_, in.target);
    float sizes[fog_cascade_max]{};
    unsigned bound = 0;
    for (unsigned i = 0; i < in.count && SUCCEEDED(hr); ++i) {
        const FogCascadeInput& k = in.cascades[i];
        if (!k.valid || !k.map) continue;
        hr = k.map->GetLevelDesc(0, &desc);
        if (SUCCEEDED(hr) && (desc.Format != D3DFMT_R32F || desc.Width != desc.Height || !desc.Width || !std::isfinite(k.bias) || k.bias < 0.f)) hr = E_INVALIDARG;
        for (float v : k.rows) if (!std::isfinite(v)) hr = E_INVALIDARG;
        sizes[i] = float(desc.Width); ++bound;
    }
    if (FAILED(hr)) return fail(FogStage::Validate, hr);
    hr = prepare(in.width, in.height);
    if (FAILED(hr)) return fail(FogStage::Targets, hr);
    hr = ensure_block();
    if (FAILED(hr)) return fail(FogStage::Block, hr);
    SavedState saved(*this, block_, render_targets_);
    hr = saved.capture();
    if (FAILED(hr)) return fail(FogStage::Capture, hr);
    const UINT w = in.width, h = in.height, hw = half_width_, hh = half_height_;
    const FogParams& p = in.params;
    D d = device_;
    FogStage stage = FogStage::Normalize;
    hr = normalize();
    bool own_scene = false;
    // Every call below is guarded by SUCCEEDED(hr): no device call follows a failure.
    auto step = [&](FogStage s, HRESULT value) { stage = s; hr = value; return SUCCEEDED(hr); };
    if (SUCCEEDED(hr) && !in.caller_scene_open) own_scene = step(FogStage::Scene, call<SceneFn>(BeginScene)(d));
    // 1. March (c0-c16).
    float march[17][4] = {
        {p.m00, p.m11, p.m20, p.m21},
        {p.m22, p.m32, view_depth_lane ? 1.f : 0.f, p.margin},
        {p.tau_max, p.radius, 1.f / p.tau_max, 5.588238f * float(p.jitter_index % 8u)},
        {float(hw), float(hh), 0.f, 0.f},
        {float(2. / double(w)), float(2. / double(h)), float(.5 / double(w)), float(.5 / double(h))},
    };
    for (unsigned i = 0; i < fog_cascade_max; ++i) {
        float* rows = march[5 + i * 4];
        const bool valid = i < in.count && in.cascades[i].valid && in.cascades[i].map;
        if (valid) {
            for (unsigned k = 0; k < 12; ++k) rows[k] = in.cascades[i].rows[k];
            rows[12] = sizes[i]; rows[13] = 1.f / sizes[i]; rows[14] = in.cascades[i].bias; rows[15] = 1.f;
        } else { for (unsigned k = 0; k < 16; ++k) rows[k] = 0.f; rows[3] = 2.f; rows[12] = rows[13] = 1.f; } // never contains a sample
    }
    if (SUCCEEDED(hr)) step(FogStage::March, bind_target(lit_surface_, hw, hh, march_));
    if (SUCCEEDED(hr)) step(FogStage::March, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 0, &march[0][0], 17));
    if (SUCCEEDED(hr)) step(FogStage::March, call<SetTextureFn>(SetTexture)(d, 0, in.depth_share));
    for (unsigned i = 0; i < in.count && SUCCEEDED(hr); ++i)
        if (in.cascades[i].valid && in.cascades[i].map) step(FogStage::March, call<SetTextureFn>(SetTexture)(d, 1 + i, in.cascades[i].map));
    if (SUCCEEDED(hr)) step(FogStage::March, quad(hw, hh));
    // 2. Scene copy: the composite reads what it overwrites. RT0 is the F target here, so the scene target is unbound.
    if (SUCCEEDED(hr)) step(FogStage::Copy, call<StretchRectFn>(StretchRect)(d, in.target, nullptr, scratch_surface_, nullptr, D3DTEXF_NONE));
    // 3. Sky hue: scratch + RT2 -> 8x8 -> the 1x1 history. The first update
    // after creation writes without blending (the new target's contents are undefined).
    bool sky_updated = false;
    if (SUCCEEDED(hr) && (p.update_sky || !sky_seeded_)) {
        const float level0[4] = {p.decode_exponent, 0.f, 0.f, 0.f};
        const float reduce[4] = {sky_seeded_ ? p.sky_blend : 1.f, 0.f, 0.f, 0.f};
        step(FogStage::SkyLevel, bind_target(sky_level_surface_, sky_level_size, sky_level_size, sky_level0_));
        if (SUCCEEDED(hr)) step(FogStage::SkyLevel, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 0, level0, 1));
        if (SUCCEEDED(hr)) step(FogStage::SkyLevel, call<SetTextureFn>(SetTexture)(d, 0, scratch_));
        if (SUCCEEDED(hr)) step(FogStage::SkyLevel, call<SetTextureFn>(SetTexture)(d, 1, in.depth_share));
        if (SUCCEEDED(hr)) step(FogStage::SkyLevel, quad(sky_level_size, sky_level_size));
        if (SUCCEEDED(hr)) step(FogStage::SkyReduce, bind_target(sky_surface_, 1, 1, sky_reduce_));
        if (SUCCEEDED(hr)) step(FogStage::SkyReduce, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 0, reduce, 1));
        if (SUCCEEDED(hr)) step(FogStage::SkyReduce, call<SetTextureFn>(SetTexture)(d, 0, sky_level_));
        if (SUCCEEDED(hr) && sky_seeded_) step(FogStage::SkyReduce, call<SetRsFn>(SetRenderState)(d, D3DRS_ALPHABLENDENABLE, TRUE));
        if (SUCCEEDED(hr)) step(FogStage::SkyReduce, quad(1, 1));
        if (SUCCEEDED(hr) && sky_seeded_) step(FogStage::SkyReduce, call<SetRsFn>(SetRenderState)(d, D3DRS_ALPHABLENDENABLE, FALSE));
        sky_updated = SUCCEEDED(hr);
    }
    // 4. Composite (c0-c6) into the scene target.
    const float composite[7][4] = {
        {p.m00, p.m11, p.m20, p.m21},
        {p.m22, p.m32, view_depth_lane ? 1.f : 0.f, p.decode_exponent},
        {p.tau_max, p.radius, p.anisotropy, 1.f / p.decode_exponent},
        {float(hw), float(hh), 1.f / float(hw), 1.f / float(hh)},
        {float(w), float(h), 1.f / float(w), 1.f / float(h)},
        {p.sun_view[0], p.sun_view[1], p.sun_view[2], 0.f},
        {p.sun_radiance[0], p.sun_radiance[1], p.sun_radiance[2], 0.f},
    };
    bool applied = false;
    if (SUCCEEDED(hr)) step(FogStage::Composite, bind_target(in.target, w, h, composite_));
    if (SUCCEEDED(hr)) step(FogStage::Composite, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 0, &composite[0][0], 7));
    if (SUCCEEDED(hr)) step(FogStage::Composite, call<SetTextureFn>(SetTexture)(d, 0, scratch_));
    if (SUCCEEDED(hr)) step(FogStage::Composite, call<SetTextureFn>(SetTexture)(d, 1, lit_));
    if (SUCCEEDED(hr)) step(FogStage::Composite, call<SetTextureFn>(SetTexture)(d, 2, in.depth_share));
    if (SUCCEEDED(hr)) step(FogStage::Composite, call<SetTextureFn>(SetTexture)(d, 3, sky_));
    if (SUCCEEDED(hr)) applied = step(FogStage::Composite, quad(w, h));
    if (own_scene && !lost(hr)) { const HRESULT end = call<SceneFn>(EndScene)(d); if (SUCCEEDED(hr) || lost(end)) { if (FAILED(end)) stage = FogStage::EndScene; hr = end; } }
    // The seed is final only when its quad was submitted; a later failure keeps it (the history holds a whole mean either way).
    if (sky_updated) sky_seeded_ = true;
    out->operation = hr;
    out->restore = lost(hr) ? hr : saved.restore();
    out->device_calls = calls_ + 2; // + the block's Capture and Apply
    if (FAILED(hr) || FAILED(out->restore)) {
        out->failed = FAILED(hr) ? stage : FogStage::Restore;
        return FAILED(out->restore) ? out->restore : hr;
    }
    out->applied = applied; out->sky_updated = sky_updated; out->cascades_bound = bound;
    out->lit = lit_; out->half_width = hw; out->half_height = hh;
    return S_OK;
}
} // namespace x3m::renderer
