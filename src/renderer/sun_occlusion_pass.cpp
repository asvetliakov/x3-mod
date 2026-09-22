#include "sun_occlusion_pass.h"
#include "ps3_program_slots.h"
#include "quad_vertex_program.h"
#include <cmath>
#include <cstring>
#include <iterator>
#include <new>
#include <utility>

namespace x3m::renderer {
namespace {
template<class T> void drop(T*& value) noexcept { if (value) { value->Release(); value = nullptr; } }
bool lost(HRESULT hr) noexcept { return hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET; }
// IDirect3DDevice9 vtable slots (verification/probe/abi_check.cpp), as in SunShadowApplyPass.
enum Slot : unsigned {
    GetDirect3D = 6, GetCreationParameters = 9, CreateTexture = 23, GetRenderTargetData = 32, ColorFill = 35, CreateOffscreenPlainSurface = 36,
    SetRenderTarget = 37, GetRenderTarget = 38, SetDepthStencilSurface = 39, GetDepthStencilSurface = 40, BeginScene = 41, EndScene = 42,
    SetViewport = 47, SetRenderState = 57, BeginStateBlock = 60, EndStateBlock = 61, SetTexture = 65,
    SetSamplerState = 69, SetScissorRect = 75, DrawPrimitiveUP = 83,
    CreateVertexDeclaration = 86, SetVertexDeclaration = 87, GetVertexDeclaration = 88, SetFVF = 89, GetFVF = 90,
    CreateVertexShader = 91, SetVertexShader = 92, SetStreamSource = 100, SetStreamSourceFreq = 102, SetIndices = 104, CreatePixelShader = 106,
    SetPixelShader = 107, SetPixelShaderConstantF = 109
};
using D = IDirect3DDevice9*;
using GetD3DFn = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetCreationFn = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);
using CreateTextureFn = HRESULT(WINAPI*)(D, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
using GetDataFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*, IDirect3DSurface9*);
using ColorFillFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*, const RECT*, D3DCOLOR);
using CreateOffscreenFn = HRESULT(WINAPI*)(D, UINT, UINT, D3DFORMAT, D3DPOOL, IDirect3DSurface9**, HANDLE*);
using SetRtFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DSurface9*);
using GetRtFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DSurface9**);
using SetDepthFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*);
using GetDepthFn = HRESULT(WINAPI*)(D, IDirect3DSurface9**);
using SceneFn = HRESULT(WINAPI*)(D);
using SetViewportFn = HRESULT(WINAPI*)(D, const D3DVIEWPORT9*);
using SetRsFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD);
using SetTextureFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DBaseTexture9*);
using SetSamplerFn = HRESULT(WINAPI*)(D, DWORD, D3DSAMPLERSTATETYPE, DWORD);
using SetScissorFn = HRESULT(WINAPI*)(D, const RECT*);
using DrawUpFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using CreateDeclarationFn = HRESULT(WINAPI*)(D, const D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9**);
using SetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9*);
using GetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9**);
using SetFvfFn = HRESULT(WINAPI*)(D, DWORD);
using GetFvfFn = HRESULT(WINAPI*)(D, DWORD*);
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using SetFreqFn = HRESULT(WINAPI*)(D, UINT, UINT);
using SetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
using EndBlockFn = HRESULT(WINAPI*)(D, IDirect3DStateBlock9**);
using SetIndicesFn = HRESULT(WINAPI*)(D, IDirect3DIndexBuffer9*);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using SetPsConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
// Only our authored program is embedded (tools/shaders/generate_rigid_motion_pixel.py).
constexpr DWORD visibility_words[] = {
#include "sun_visibility_program_inc.h"
};
constexpr D3DFORMAT fraction_format = D3DFMT_A16B16G16R16F;
bool depth_format(D3DFORMAT f) noexcept { return f == D3DFMT_R32F || f == D3DFMT_G32R32F || f == D3DFMT_A32B32G32R32F; }
// The sampler states a lens draw needs on the wrap's sampler, and ours. A 1x1 level-0 texture
// returns its one texel under every other state; FP16 filtering is not assumed.
constexpr D3DSAMPLERSTATETYPE lens_states[6] = {D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER, D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE};
constexpr DWORD lens_values[6] = {D3DTADDRESS_CLAMP, D3DTADDRESS_CLAMP, D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_NONE, FALSE};
// Step 2: the core clip's tap step in RT2 pixels (the fragment and the eight knight moves (+-2, +-1) / (+-1, +-2) steps,
// every one on a texel centre: lens_visibility_variant.h, "Clip pair").
constexpr float soft_edge_pixels = 1.f;
// The jitter offset is at most half an RT2 texel: a value beyond this in uv (a 10-texel-wide target) is a caller bug.
constexpr float jitter_limit = .05f;
template<class Resource> HRESULT same_device(IDirect3DDevice9* device, Resource* resource) noexcept {
    IDirect3DDevice9* owner = nullptr;
    HRESULT hr = resource->GetDevice(&owner);
    if (FAILED(hr)) return hr;
    const bool same = owner == device;
    drop(owner);
    return same ? S_OK : E_INVALIDARG;
}
} // namespace
const char* lens_verdict_name(LensVerdict v) noexcept {
    switch (v) {
    case LensVerdict::Applied: return "applied"; case LensVerdict::NotReady: return "not_ready"; case LensVerdict::NoShader: return "no_shader";
    case LensVerdict::Unhashed: return "unhashed"; case LensVerdict::Blend: return "blend"; case LensVerdict::Variant: return "variant";
    case LensVerdict::CacheFull: return "cache_full"; case LensVerdict::Device: return "device"; case LensVerdict::Body: return "body";
    }
    return "invalid";
}
// The caller's state around the quad. `saved` is a block recorded with exactly the quad's state
// set (record_quad): Capture re-reads the application's values of that set, Apply puts them back.
// Render targets and the depth surface are in no state block; the declaration / FVF pair is
// re-set explicitly (a caller in FVF mode is not restored by a block alone, as SunShadowApplyPass).
struct SunOcclusionPass::SavedState {
    SunOcclusionPass& pass;
    IDirect3DSurface9* targets[4]{};
    IDirect3DSurface9* depth = nullptr;
    IDirect3DVertexDeclaration9* declaration = nullptr;
    DWORD fvf = 0;
    UINT count;
    SavedState(SunOcclusionPass& p, UINT n) : pass(p), count(n > 4 ? 4 : n) {}
    ~SavedState() { for (auto& t : targets) drop(t); drop(depth); drop(declaration); }
    HRESULT capture() noexcept {
        ++pass.device_calls_;
        HRESULT hr = pass.saved_->Capture();
        if (FAILED(hr)) return hr;
        hr = pass.call<GetDeclarationFn>(GetVertexDeclaration)(pass.device_, &declaration);
        if (FAILED(hr)) return hr;
        hr = pass.call<GetFvfFn>(GetFVF)(pass.device_, &fvf);
        if (FAILED(hr)) return hr;
        for (UINT i = 0; i < count; ++i) {
            hr = pass.call<GetRtFn>(GetRenderTarget)(pass.device_, i, &targets[i]);
            if (FAILED(hr) && hr != D3DERR_NOTFOUND) return hr;
        }
        hr = pass.call<GetDepthFn>(GetDepthStencilSurface)(pass.device_, &depth);
        return FAILED(hr) && hr != D3DERR_NOTFOUND ? hr : S_OK;
    }
    HRESULT restore() noexcept {
        HRESULT first = S_OK;
        auto attempt = [&](HRESULT hr) { if (lost(hr) || (FAILED(hr) && SUCCEEDED(first))) first = hr; return !lost(hr); };
        D d = pass.device_;
        for (UINT i = 0; i < 2; ++i) if (!attempt(pass.call<SetTextureFn>(SetTexture)(d, i, nullptr))) return first; // RT2 leaves the sampler before it is a target again
        if (!attempt(pass.call<SetDepthFn>(SetDepthStencilSurface)(d, nullptr))) return first;
        for (UINT i = 1; i < count; ++i) if (!attempt(pass.call<SetRtFn>(SetRenderTarget)(d, i, nullptr))) return first;
        for (UINT i = 0; i < count; ++i) if (targets[i] || i) if (!attempt(pass.call<SetRtFn>(SetRenderTarget)(d, i, targets[i]))) return first;
        if (!attempt(pass.call<SetDepthFn>(SetDepthStencilSurface)(d, depth))) return first;
        ++pass.device_calls_;
        if (!attempt(pass.saved_->Apply())) return first; // after the targets: SetRenderTarget(0) rewrote viewport and scissor, the block holds the caller's
        if (declaration) attempt(pass.call<SetDeclarationFn>(SetVertexDeclaration)(d, declaration));
        else if (fvf) attempt(pass.call<SetFvfFn>(SetFVF)(d, fvf));
        else attempt(pass.call<SetDeclarationFn>(SetVertexDeclaration)(d, nullptr));
        return first;
    }
};
SunOcclusionPass::~SunOcclusionPass() { detach(); }
unsigned SunOcclusionPass::references() const noexcept {
    unsigned n = 0;
    for (const void* p : {static_cast<const void*>(normal_), static_cast<const void*>(saved_), static_cast<const void*>(program_), static_cast<const void*>(quad_vs_),
                          static_cast<const void*>(quad_declaration_), static_cast<const void*>(textures_[0]), static_cast<const void*>(textures_[1]),
                          static_cast<const void*>(surfaces_[0]), static_cast<const void*>(surfaces_[1]), static_cast<const void*>(readback_)})
        n += p != nullptr;
    for (unsigned i = 0; i < variant_count_; ++i) for (auto* s : variant_[i].shader) n += s != nullptr;
    for (unsigned i = 0; i < pair_count_; ++i) { n += pair_[i].vertex != nullptr; for (auto* s : pair_[i].pixel) n += s != nullptr; }
    for (const auto& b : blocks_) n += (b.saved != nullptr) + (b.ours != nullptr);
    return n;
}
void SunOcclusionPass::before_reset() noexcept {
    drop(normal_); drop(saved_); drop(readback_); // every state block is a DEFAULT-pool citizen: gone before Reset
    for (auto& b : blocks_) { drop(b.saved); drop(b.ours); b = Blocks{}; }
    for (auto& s : surfaces_) drop(s);
    for (auto& t : textures_) drop(t);
    valid_ = false; reset_pending_ = device_ != nullptr;
}
void SunOcclusionPass::after_reset(HRESULT result) noexcept { if (SUCCEEDED(result)) reset_pending_ = false; }
void SunOcclusionPass::detach() noexcept {
    before_reset();
    for (unsigned i = 0; i < variant_count_; ++i) { for (auto*& s : variant_[i].shader) drop(s); variant_[i] = Variant{}; }
    for (unsigned i = 0; i < pair_count_; ++i) { drop(pair_[i].vertex); for (auto*& s : pair_[i].pixel) drop(s); pair_[i] = Pair{}; }
    variant_count_ = 0; pair_count_ = 0; variant_refusal_ = "";
    drop(program_); drop(quad_vs_); drop(quad_declaration_);
    device_ = nullptr; vtable_ = nullptr; render_targets_ = streams_ = 0; reset_pending_ = false; current_ = 0;
    caps_ = {};
}
HRESULT SunOcclusionPass::attach(IDirect3DDevice9* d, void* const* native, const D3DCAPS9& caps, D3DFORMAT adapter_format) noexcept {
    detach();
    if (!d) { caps_.reason = "device"; return E_INVALIDARG; }
    device_ = d; vtable_ = native;
    auto refuse = [&](const char* reason, HRESULT hr) { caps_.reason = reason; device_ = nullptr; vtable_ = nullptr; return hr; };
    if ((caps.VertexShaderVersion & 0xffffu) < 0x0300u || (caps.PixelShaderVersion & 0xffffu) < 0x0300u) return refuse("shader_model", D3DERR_NOTAVAILABLE);
    caps_.program_slots = ps3_program_slots(reinterpret_cast<const std::uint32_t*>(visibility_words), std::size(visibility_words));
    if (caps_.program_slots == 0 || caps.MaxPixelShader30InstructionSlots < caps_.program_slots) return refuse("ps_slots", D3DERR_NOTAVAILABLE);
    IDirect3D9* api = nullptr;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    HRESULT hr = call<GetD3DFn>(GetDirect3D)(d, &api);
    if (SUCCEEDED(hr) && !api) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = call<GetCreationFn>(GetCreationParameters)(d, &creation);
    const char* gate = nullptr;
    if (SUCCEEDED(hr) && FAILED(api->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, adapter_format, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, fraction_format)))
        gate = "a16b16g16r16f";
    drop(api);
    caps_.formats = hr;
    if (FAILED(hr)) return refuse("format_query", hr);
    if (gate) { caps_.formats = D3DERR_NOTAVAILABLE; return refuse(gate, D3DERR_NOTAVAILABLE); }
    render_targets_ = caps.NumSimultaneousRTs ? caps.NumSimultaneousRTs : 1; streams_ = caps.MaxStreams;
    try { words_.reserve(4096); wrapped_.reserve(4096 + 96); vertex_words_.reserve(4096); } catch (const std::bad_alloc&) { return refuse("memory", E_OUTOFMEMORY); }
    hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(quad_vertex_program()), &quad_vs_);
    if (SUCCEEDED(hr)) hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, quad_declaration, &quad_declaration_);
    if (SUCCEEDED(hr)) hr = call<CreatePsFn>(CreatePixelShader)(d, visibility_words, &program_);
    caps_.programs = hr;
    if (FAILED(hr)) { const SunOcclusionCaps kept = caps_; detach(); caps_ = kept; caps_.reason = "programs"; return hr; }
    caps_.enabled = true; caps_.reason = "";
    return S_OK;
}
// Both targets are created together and cleared to a defined value (a seed never reads the
// history, but a lens draw must never sample an unset texel).
HRESULT SunOcclusionPass::ensure_targets() noexcept {
    if (textures_[0] && textures_[1] && surfaces_[0] && surfaces_[1]) return S_OK;
    for (auto& s : surfaces_) drop(s);
    for (auto& t : textures_) drop(t);
    valid_ = false;
    HRESULT hr = S_OK;
    for (unsigned i = 0; i < 2 && SUCCEEDED(hr); ++i) {
        hr = call<CreateTextureFn>(CreateTexture)(device_, 1, 1, 1, D3DUSAGE_RENDERTARGET, fraction_format, D3DPOOL_DEFAULT, &textures_[i], nullptr);
        if (SUCCEEDED(hr)) hr = textures_[i]->GetSurfaceLevel(0, &surfaces_[i]);
        if (SUCCEEDED(hr)) hr = call<ColorFillFn>(ColorFill)(device_, surfaces_[i], nullptr, D3DCOLOR_ARGB(255, 255, 255, 255));
    }
    if (FAILED(hr)) { for (auto& s : surfaces_) drop(s); for (auto& t : textures_) drop(t); }
    return hr;
}
// Records the calls of `sets` into a new block without applying them (BeginStateBlock /
// EndStateBlock, documented). EndStateBlock always runs, so a failure never leaves the device
// recording. The caller has established that the application is not recording.
template<class Sets> HRESULT SunOcclusionPass::record(IDirect3DStateBlock9** out, Sets&& sets) noexcept {
    HRESULT hr = call<SceneFn>(BeginStateBlock)(device_);
    if (FAILED(hr)) return hr;
    const HRESULT body = sets();
    IDirect3DStateBlock9* block = nullptr;
    hr = call<EndBlockFn>(EndStateBlock)(device_, &block);
    if (FAILED(body) || FAILED(hr) || !block) { drop(block); return FAILED(body) ? body : FAILED(hr) ? hr : E_FAIL; }
    *out = block;
    return S_OK;
}
// The quad's whole state set, and nothing else: every state the 1-pixel draw depends on or
// that a transaction step rewrites (DrawPrimitiveUP unbinds stream 0; SetRenderTarget(0)
// rewrites viewport and scissor). Recorded twice: `normal_` keeps these values and is applied
// each frame (one call instead of ~80 setters), `saved_` is re-captured each frame and carries
// the caller's values back.
HRESULT SunOcclusionPass::record_quad() noexcept {
    D d = device_;
#define STEP(call) do { const HRESULT hresult = (call); if (FAILED(hresult)) return hresult; } while (false)
    for (UINT i = 0; i < 2; ++i) STEP(call<SetTextureFn>(SetTexture)(d, i, nullptr));
    STEP(call<SetDeclarationFn>(SetVertexDeclaration)(d, quad_declaration_));
    STEP(call<SetVsFn>(SetVertexShader)(d, quad_vs_));
    STEP(call<SetPsFn>(SetPixelShader)(d, program_));
    STEP(call<SetIndicesFn>(SetIndices)(d, nullptr));
    STEP(call<SetStreamFn>(SetStreamSource)(d, 0, nullptr, 0, 0));
    STEP(call<SetFreqFn>(SetStreamSourceFreq)(d, 0, 1));
    for (auto state : {D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_STENCILENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHABLENDENABLE,
                       D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_FOGENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_SCISSORTESTENABLE,
                       D3DRS_CLIPPLANEENABLE, D3DRS_CLIPPING, D3DRS_LIGHTING, D3DRS_INDEXEDVERTEXBLENDENABLE,
                       D3DRS_POINTSPRITEENABLE, D3DRS_DITHERENABLE, D3DRS_ANTIALIASEDLINEENABLE})
        STEP(call<SetRsFn>(SetRenderState)(d, state, FALSE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_VERTEXBLEND, D3DVBF_DISABLE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_WRAP0, 0));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_FILLMODE, D3DFILL_SOLID));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, D3DCULL_NONE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_COLORWRITEENABLE, 15));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_MULTISAMPLEMASK, 0xffffffff));
    for (UINT i = 0; i < 2; ++i) {
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MINFILTER, D3DTEXF_POINT));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MAGFILTER, D3DTEXF_POINT));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_SRGBTEXTURE, FALSE));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MAXMIPLEVEL, 0));
    }
    const float zero[12]{};
    STEP(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 0, zero, 3));
    const D3DVIEWPORT9 viewport{0, 0, 1, 1, 0.f, 1.f};
    STEP(call<SetViewportFn>(SetViewport)(d, &viewport));
    const RECT scissor{0, 0, 1, 1};
    STEP(call<SetScissorFn>(SetScissorRect)(d, &scissor));
#undef STEP
    return S_OK;
}
HRESULT SunOcclusionPass::ensure_blocks() noexcept {
    if (normal_ && saved_) return S_OK;
    drop(normal_); drop(saved_);
    HRESULT hr = record(&normal_, [&] { return record_quad(); });
    if (SUCCEEDED(hr)) hr = record(&saved_, [&] { return record_quad(); });
    if (FAILED(hr)) { drop(normal_); drop(saved_); }
    return hr;
}
HRESULT SunOcclusionPass::execute(const SunVisibilityFrame& in, SunVisibilityResult* out) noexcept {
    if (!out) return E_INVALIDARG;
    *out = {};
    auto skip = [&](const char* reason) { out->skipped = true; out->skipped_reason = reason; out->operation = S_FALSE; return S_FALSE; };
    if (!device_ || !caps_.enabled) return skip("detached");
    if (reset_pending_) return skip("reset_pending");
    if (!in.depth || in.caller_stateblock_recording) return skip("input");
    for (float v : {in.u, in.v, in.radius_u, in.radius_v, in.alpha, in.curve, in.jitter_u, in.jitter_v}) if (!std::isfinite(v)) return skip("params");
    if (!(in.radius_u > 0.f) || !(in.radius_v > 0.f) || in.radius_u > 1.f || in.radius_v > 1.f || in.alpha < 0.f || in.alpha > 1.f ||
        in.curve < .25f || in.curve > 4.f || in.u < -4.f || in.u > 5.f || in.v < -4.f || in.v > 5.f ||
        in.jitter_u < -jitter_limit || in.jitter_u > jitter_limit || in.jitter_v < -jitter_limit || in.jitter_v > jitter_limit) return skip("params");
    D3DSURFACE_DESC desc{};
    if (FAILED(in.depth->GetLevelDesc(0, &desc)) || !depth_format(desc.Format) || desc.MultiSampleType != D3DMULTISAMPLE_NONE) return skip("format");
    if (FAILED(same_device(device_, in.depth))) return skip("device");
    auto fail = [&](SunOcclusionStage stage, HRESULT hr) { valid_ = false; out->failed = stage; out->operation = hr; return hr; };
    HRESULT hr = ensure_targets();
    if (FAILED(hr)) return fail(SunOcclusionStage::Targets, hr);
    hr = ensure_blocks();
    if (FAILED(hr)) return fail(SunOcclusionStage::Block, hr);
    device_calls_ = 0;
    SavedState saved(*this, render_targets_);
    hr = saved.capture();
    if (FAILED(hr)) return fail(SunOcclusionStage::Capture, hr);
    D d = device_;
    const unsigned next = current_ ^ 1u;
    const bool seed = in.seed || !valid_;
    SunOcclusionStage stage = SunOcclusionStage::Normalize;
    bool own_scene = false, drew = false;
    auto step = [&](SunOcclusionStage s, HRESULT value) { stage = s; hr = value; return SUCCEEDED(hr); };
    // Targets first (one size: the others off before the 1x1 goes on), then the recorded set.
    hr = call<SetDepthFn>(SetDepthStencilSurface)(d, nullptr);
    for (UINT i = 1; i < render_targets_ && i < 4 && SUCCEEDED(hr); ++i) hr = call<SetRtFn>(SetRenderTarget)(d, i, nullptr);
    if (SUCCEEDED(hr)) hr = call<SetRtFn>(SetRenderTarget)(d, 0, surfaces_[next]);
    if (SUCCEEDED(hr)) { ++device_calls_; hr = normal_->Apply(); }
    if (SUCCEEDED(hr) && !in.caller_scene_open) own_scene = step(SunOcclusionStage::Scene, call<SceneFn>(BeginScene)(d));
    // c0 the disc, c1 the smoothing, c2.xy the jitter (sun_visibility_ps.hlsl); the recorded set covers all three.
    const float block[3][4] = {{in.u, in.v, in.radius_u, in.radius_v}, {in.alpha, seed ? 1.f : 0.f, in.curve, 0.f}, {in.jitter_u, in.jitter_v, 0.f, 0.f}};
    if (SUCCEEDED(hr)) step(SunOcclusionStage::Constants, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 0, &block[0][0], 3));
    if (SUCCEEDED(hr)) step(SunOcclusionStage::Draw, call<SetTextureFn>(SetTexture)(d, 0, in.depth));
    if (SUCCEEDED(hr)) step(SunOcclusionStage::Draw, call<SetTextureFn>(SetTexture)(d, 1, textures_[current_]));
    if (SUCCEEDED(hr)) {
        QuadVertex vertices[4];
        quad_vertices(1, 1, vertices);
        drew = step(SunOcclusionStage::Draw, call<DrawUpFn>(DrawPrimitiveUP)(d, D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(QuadVertex)));
    }
    if (own_scene && !lost(hr)) { const HRESULT end = call<SceneFn>(EndScene)(d); if (SUCCEEDED(hr) || lost(end)) { if (FAILED(end)) stage = SunOcclusionStage::EndScene; hr = end; } }
    out->operation = hr;
    out->restore = lost(hr) ? hr : saved.restore();
    out->device_calls = device_calls_;
    if (FAILED(hr) || FAILED(out->restore)) {
        valid_ = false; // a half-written target is never sampled: lens draws are NotReady until the next seed
        out->failed = FAILED(hr) ? stage : SunOcclusionStage::Restore;
        return FAILED(out->restore) ? out->restore : hr;
    }
    current_ = next; valid_ = drew; out->ran = drew; out->seeded = seed;
    return S_OK;
}
SunOcclusionPass::Variant* SunOcclusionPass::find(std::uint64_t hash) noexcept {
    for (unsigned i = 0; i < variant_count_; ++i) if (variant_[i].hash == hash) return &variant_[i];
    if (variant_count_ == variant_capacity) return nullptr;
    Variant& fresh = variant_[variant_count_++];
    fresh = Variant{}; fresh.hash = hash;
    return &fresh;
}
// A program's bytecode into `out` (one of vs / ps). False names the refusal.
bool SunOcclusionPass::read_function(IDirect3DVertexShader9* vs, IDirect3DPixelShader9* ps, std::vector<std::uint32_t>& out) noexcept {
    UINT bytes = 0;
    const HRESULT size = vs ? vs->GetFunction(nullptr, &bytes) : ps->GetFunction(nullptr, &bytes);
    if (FAILED(size) || bytes < 8 || (bytes & 3u) || bytes > (1u << 18)) { variant_refusal_ = "function_size"; return false; }
    try { out.resize(bytes / 4); } catch (const std::bad_alloc&) { variant_refusal_ = "memory"; return false; }
    const HRESULT read = vs ? vs->GetFunction(out.data(), &bytes) : ps->GetFunction(out.data(), &bytes);
    if (FAILED(read)) { variant_refusal_ = "function_read"; return false; }
    return true;
}
// Once per program and scale mode. Any refusal is final for this device generation of the pass.
bool SunOcclusionPass::build(Variant& v, unsigned mode, IDirect3DPixelShader9* original) noexcept {
    v.state[mode] = 2;
    if (!read_function(nullptr, original, words_)) return false;
    LensVisibilityLayout layout{};
    const LensVisibilityResult result = lens_visibility_pixel_variant(words_.data(), words_.size(), LensVisibilityScale(mode + 1), wrapped_, &layout);
    if (result != LensVisibilityResult::Applied) { variant_refusal_ = lens_visibility_result_name(result); return false; }
    IDirect3DPixelShader9* created = nullptr;
    if (FAILED(call<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(wrapped_.data()), &created)) || !created) { drop(created); variant_refusal_ = "create"; return false; }
    v.shader[mode] = created; v.sampler[mode] = std::uint8_t(layout.sampler); v.constant[mode] = std::uint8_t(layout.constant); v.state[mode] = 1;
    return true;
}
SunOcclusionPass::Pair* SunOcclusionPass::find_pair(std::uint64_t vertex_hash, std::uint64_t pixel_hash) noexcept {
    for (unsigned i = 0; i < pair_count_; ++i) if (pair_[i].vertex_hash == vertex_hash && pair_[i].pixel_hash == pixel_hash) return &pair_[i];
    if (pair_count_ == pair_capacity) return nullptr;
    Pair& fresh = pair_[pair_count_++];
    fresh = Pair{}; fresh.vertex_hash = vertex_hash; fresh.pixel_hash = pixel_hash;
    return &fresh;
}
// Once per pair, no device object: the texcoord free in both programs, the vertex program's matrix register and
// whether its local origin is the rows' .w column (the wrap itself is discarded here and rebuilt on first use).
bool SunOcclusionPass::scan_pair(Pair& p, const LensState& state) noexcept {
    p.state = 2;
    if (!read_function(state.vertex_shader, nullptr, vertex_words_) || !read_function(nullptr, state.shader, words_)) return false;
    const unsigned texcoord = lens_visibility_free_texcoord(vertex_words_.data(), vertex_words_.size(), words_.data(), words_.size());
    if (texcoord >= 8) { variant_refusal_ = "no_free_texcoord"; return false; }
    unsigned matrix = 0; bool origin = false;
    const LensVisibilityResult result = lens_visibility_vertex_variant(vertex_words_.data(), vertex_words_.size(), texcoord, wrapped_, &matrix, &origin);
    if (result != LensVisibilityResult::Applied) { variant_refusal_ = lens_visibility_result_name(result); return false; }
    p.texcoord = std::uint8_t(texcoord); p.matrix_register = matrix; p.origin_known = origin; p.state = 1;
    return true;
}
// The vertex wrap, created on the first core draw of the pair.
bool SunOcclusionPass::build_pair_vertex(Pair& p, const LensState& state) noexcept {
    p.vertex_state = 2;
    if (!read_function(state.vertex_shader, nullptr, vertex_words_)) return false;
    unsigned matrix = 0; bool origin = false;
    const LensVisibilityResult result = lens_visibility_vertex_variant(vertex_words_.data(), vertex_words_.size(), p.texcoord, wrapped_, &matrix, &origin);
    if (result != LensVisibilityResult::Applied || matrix != p.matrix_register) { variant_refusal_ = result != LensVisibilityResult::Applied ? lens_visibility_result_name(result) : "vertex_changed"; return false; }
    IDirect3DVertexShader9* created = nullptr;
    if (FAILED(call<CreateVsFn>(CreateVertexShader)(device_, reinterpret_cast<const DWORD*>(wrapped_.data()), &created)) || !created) { drop(created); variant_refusal_ = "create_vertex"; return false; }
    p.vertex = created; p.vertex_state = 1;
    return true;
}
// Once per pair and scale mode, for the RT2 size of the frame (the soft-edge offsets are baked).
bool SunOcclusionPass::build_pair_pixel(Pair& p, unsigned mode, const LensState& state) noexcept {
    if (p.width != state.depth_width || p.height != state.depth_height || p.core_f != core_f_) { // another RT2 size or product mode: the pixel wraps are rebuilt
        for (unsigned m = 0; m < 3; ++m) { drop(p.pixel[m]); p.pixel_state[m] = 0; }
        p.width = state.depth_width; p.height = state.depth_height; p.core_f = core_f_;
    }
    p.pixel_state[mode] = 2;
    if (!p.width || !p.height) { variant_refusal_ = "depth_size"; return false; }
    if (!read_function(nullptr, state.shader, words_)) return false;
    LensVisibilityLayout layout{};
    const LensVisibilityResult result = lens_visibility_pixel_clip_variant(words_.data(), words_.size(), LensVisibilityScale(mode + 1), p.texcoord,
                                                                           soft_edge_pixels / float(p.width), soft_edge_pixels / float(p.height), core_f_, wrapped_, &layout);
    if (result != LensVisibilityResult::Applied) { variant_refusal_ = lens_visibility_result_name(result); return false; }
    IDirect3DPixelShader9* created = nullptr;
    if (FAILED(call<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(wrapped_.data()), &created)) || !created) { drop(created); variant_refusal_ = "create_clip"; return false; }
    p.pixel[mode] = created; p.sampler[mode] = std::uint8_t(layout.sampler); p.depth_sampler[mode] = std::uint8_t(layout.depth_sampler); p.pixel_state[mode] = 1;
    for (unsigned i = 0; i < 4; ++i) p.constants[mode][i] = std::uint8_t(layout.constants[i]);
    return true;
}
// The blocks of one wrap layout, recorded once and found by key afterwards: `saved` (re-captured per draw: the
// application's pixel shader, the vertex shader too for the clip pair, each wrap sampler's texture and six states,
// and the wrap's `def` registers, which native D3D9 loads into the constant file when the program is set; the block
// holds the references) and `ours` (the six states a point / clamp fetch needs, per sampler). No getter runs per
// draw: the blend, alpha-test and fog state and the bound program come from the caller's shadow. Documented
// state-block recording: SetPixelShaderConstantF is a recordable pixel state, Capture re-reads it.
HRESULT SunOcclusionPass::ensure_blocks(unsigned sampler, unsigned depth_sampler, const std::uint8_t constants[4], unsigned* index) noexcept {
    Blocks* slot = nullptr;
    for (unsigned i = 0; i < block_capacity; ++i) {
        Blocks& b = blocks_[i];
        if (b.saved && b.ours && b.sampler == sampler && b.depth_sampler == depth_sampler && std::memcmp(b.constants, constants, 4) == 0) { *index = i; return S_OK; }
        if (!slot && !b.saved && !b.ours) slot = &b;
    }
    if (!slot) return E_OUTOFMEMORY;
    const bool clip = depth_sampler < 16;
    D d = device_;
    auto states = [&]() -> HRESULT {
        for (unsigned s : {sampler, depth_sampler}) {
            if (s >= 16) continue;
            for (unsigned i = 0; i < 6; ++i) { const HRESULT hr = call<SetSamplerFn>(SetSamplerState)(d, s, lens_states[i], lens_values[i]); if (FAILED(hr)) return hr; }
        }
        return S_OK;
    };
    HRESULT hr = record(&slot->ours, states);
    if (SUCCEEDED(hr)) hr = record(&slot->saved, [&]() -> HRESULT {
        HRESULT inner = call<SetPsFn>(SetPixelShader)(d, nullptr);
        if (SUCCEEDED(inner) && clip) inner = call<SetVsFn>(SetVertexShader)(d, nullptr);
        if (SUCCEEDED(inner)) inner = call<SetTextureFn>(SetTexture)(d, sampler, nullptr);
        if (SUCCEEDED(inner) && clip) inner = call<SetTextureFn>(SetTexture)(d, depth_sampler, nullptr);
        const float zero[4]{};
        for (unsigned i = 0; i < 4 && SUCCEEDED(inner); ++i) if (constants[i] != 255) inner = call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, constants[i], zero, 1);
        return SUCCEEDED(inner) ? states() : inner;
    });
    if (FAILED(hr)) { drop(slot->saved); drop(slot->ours); *slot = Blocks{}; return hr; }
    slot->sampler = std::uint8_t(sampler); slot->depth_sampler = std::uint8_t(depth_sampler); std::memcpy(slot->constants, constants, 4);
    *index = unsigned(slot - blocks_);
    return S_OK;
}
LensVerdict SunOcclusionPass::lens_prepare(const LensState& state, Prepared* out) noexcept {
    if (out) *out = Prepared{};
    if (!device_ || !caps_.enabled) return LensVerdict::NotReady;
    if (!state.known) return LensVerdict::Unhashed;
    if (!state.shader || !state.vertex_shader) return LensVerdict::NoShader;
    if (!state.hash || !state.vertex_hash) return LensVerdict::Unhashed;
    Pair* const p = find_pair(state.vertex_hash, state.hash);
    if (!p) return LensVerdict::CacheFull;
    const bool first = p->state == 0;
    if (first) scan_pair(*p, state);
    if (out) out->first = first;
    if (p->state != 1) return LensVerdict::Variant;
    if (out) { out->matrix_register = p->matrix_register; out->origin_known = p->origin_known; }
    return LensVerdict::Applied;
}
LensVerdict SunOcclusionPass::lens_begin(const LensState& state, LensDraw& out) noexcept {
    out = LensDraw{};
    if (!device_ || !caps_.enabled || reset_pending_ || !valid_ || !textures_[current_]) return LensVerdict::NotReady;
    if (!state.known) return LensVerdict::Unhashed;
    namespace core = x3m::sun_occlusion::core;
    const core::Scale scale = core::classify_blend(state.blend);
    if (scale == core::Scale::Refuse) return LensVerdict::Blend;
    if (!state.shader) return LensVerdict::NoShader;
    if (!state.hash) return LensVerdict::Unhashed;
    if (state.body != core::Body::Core && state.body != core::Body::Ghost) return LensVerdict::Body;
    const unsigned mode = unsigned(scale) - 1u;
    D d = device_;
    if (state.body == core::Body::Ghost) {
        Variant* const v = find(state.hash);
        if (!v) return LensVerdict::CacheFull;
        if (v->state[mode] == 0) build(*v, mode, state.shader);
        if (v->state[mode] != 1) return LensVerdict::Variant;
        const unsigned sampler = v->sampler[mode];
        const std::uint8_t constants[4] = {v->constant[mode], 255, 255, 255};
        unsigned block = 0;
        if (FAILED(ensure_blocks(sampler, 255, constants, &block))) return LensVerdict::Device;
        device_calls_ = 1;
        if (FAILED(blocks_[block].saved->Capture())) return LensVerdict::Device; // nothing changed yet
        out.sampler = std::uint8_t(sampler); out.block = std::uint8_t(block); out.applied = true;
        ++device_calls_;
        HRESULT hr = blocks_[block].ours->Apply();
        if (SUCCEEDED(hr)) hr = call<SetTextureFn>(SetTexture)(d, sampler, textures_[current_]);
        if (SUCCEEDED(hr)) hr = call<SetPsFn>(SetPixelShader)(d, v->shader[mode]);
        if (FAILED(hr)) { lens_end(out); return LensVerdict::Device; } // everything back; the draw goes out unwrapped
        return LensVerdict::Applied;
    }
    // Core: the clip pair.
    if (!state.vertex_shader || !state.vertex_hash || !state.depth) return LensVerdict::NoShader;
    Pair* const p = find_pair(state.vertex_hash, state.hash);
    if (!p) return LensVerdict::CacheFull;
    if (p->state == 0) scan_pair(*p, state);
    if (p->state != 1 || !p->origin_known) return LensVerdict::Variant; // a core body needs a classifiable origin: the caller never sends one otherwise
    if (p->vertex_state == 0) build_pair_vertex(*p, state);
    if (p->vertex_state != 1) return LensVerdict::Variant;
    if (p->pixel_state[mode] == 0 || p->width != state.depth_width || p->height != state.depth_height || p->core_f != core_f_) build_pair_pixel(*p, mode, state);
    if (p->pixel_state[mode] != 1) return LensVerdict::Variant;
    const unsigned sampler = p->sampler[mode], depth_sampler = p->depth_sampler[mode];
    unsigned block = 0;
    if (FAILED(ensure_blocks(sampler, depth_sampler, p->constants[mode], &block))) return LensVerdict::Device;
    device_calls_ = 1;
    if (FAILED(blocks_[block].saved->Capture())) return LensVerdict::Device; // nothing changed yet
    out.sampler = std::uint8_t(sampler); out.depth_sampler = std::uint8_t(depth_sampler); out.block = std::uint8_t(block); out.clipped = true; out.applied = true;
    ++device_calls_;
    HRESULT hr = blocks_[block].ours->Apply();
    if (SUCCEEDED(hr)) hr = call<SetTextureFn>(SetTexture)(d, sampler, textures_[current_]);
    if (SUCCEEDED(hr)) hr = call<SetTextureFn>(SetTexture)(d, depth_sampler, state.depth);
    if (SUCCEEDED(hr)) hr = call<SetPsFn>(SetPixelShader)(d, p->pixel[mode]);
    if (SUCCEEDED(hr)) hr = call<SetVsFn>(SetVertexShader)(d, p->vertex);
    if (FAILED(hr)) { lens_end(out); return LensVerdict::Device; }
    return LensVerdict::Applied;
}
HRESULT SunOcclusionPass::lens_end(LensDraw& draw) noexcept {
    if (!draw.applied) return S_OK;
    const unsigned block = draw.block;
    draw.applied = false; draw.clipped = false;
    if (!device_ || block >= block_capacity || !blocks_[block].saved) return E_FAIL;
    ++device_calls_;
    return blocks_[block].saved->Apply(); // the application's shaders, textures, sampler states and the wrap's def registers, as captured for this draw
}
HRESULT SunOcclusionPass::readback(float out[4]) noexcept {
    if (!out || !device_ || reset_pending_ || !valid_ || !surfaces_[current_]) return E_FAIL;
    HRESULT hr = S_OK;
    if (!readback_) hr = call<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, 1, 1, fraction_format, D3DPOOL_SYSTEMMEM, &readback_, nullptr);
    if (SUCCEEDED(hr)) hr = call<GetDataFn>(GetRenderTargetData)(device_, surfaces_[current_], readback_);
    D3DLOCKED_RECT lr{};
    if (SUCCEEDED(hr)) hr = readback_->LockRect(&lr, nullptr, D3DLOCK_READONLY);
    if (FAILED(hr)) return hr;
    const auto* h = static_cast<const std::uint16_t*>(lr.pBits);
    for (unsigned i = 0; i < 4; ++i) { // binary16 -> float, integer and SSE only
        const unsigned bits = h[i], e = (bits >> 10) & 31u, m = bits & 1023u;
        float v = e == 0 ? float(m) * (1.f / 16777216.f) : e == 31 ? 65504.f : float(1024u + m) * (1.f / 1024.f) * float(1u << e) * (1.f / 32768.f);
        out[i] = bits & 0x8000u ? -v : v;
    }
    readback_->UnlockRect();
    return S_OK;
}
} // namespace x3m::renderer
