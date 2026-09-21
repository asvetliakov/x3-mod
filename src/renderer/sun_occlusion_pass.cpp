#include "sun_occlusion_pass.h"
#include "ambient_occlusion_caps.h"
#include "quad_vertex_program.h"
#include <cmath>
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
    case LensVerdict::CacheFull: return "cache_full"; case LensVerdict::Device: return "device";
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
    for (unsigned i = 0; i < 16; ++i) n += (lens_saved_[i] != nullptr) + (lens_ours_[i] != nullptr);
    return n;
}
void SunOcclusionPass::before_reset() noexcept {
    drop(normal_); drop(saved_); drop(readback_); // every state block is a DEFAULT-pool citizen: gone before Reset
    for (auto& b : lens_saved_) drop(b);
    for (auto& b : lens_ours_) drop(b);
    for (auto& s : surfaces_) drop(s);
    for (auto& t : textures_) drop(t);
    valid_ = false; reset_pending_ = device_ != nullptr;
}
void SunOcclusionPass::after_reset(HRESULT result) noexcept { if (SUCCEEDED(result)) reset_pending_ = false; }
void SunOcclusionPass::detach() noexcept {
    before_reset();
    for (unsigned i = 0; i < variant_count_; ++i) { for (auto*& s : variant_[i].shader) drop(s); variant_[i] = Variant{}; }
    variant_count_ = 0; variant_refusal_ = "";
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
    caps_.program_slots = ambient_occlusion_program_slots(reinterpret_cast<const std::uint32_t*>(visibility_words), std::size(visibility_words));
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
    try { words_.reserve(4096); wrapped_.reserve(4096 + 32); } catch (const std::bad_alloc&) { return refuse("memory", E_OUTOFMEMORY); }
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
    const float zero[8]{};
    STEP(call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 0, zero, 2));
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
    for (float v : {in.u, in.v, in.radius_u, in.radius_v, in.alpha, in.curve}) if (!std::isfinite(v)) return skip("params");
    if (!(in.radius_u > 0.f) || !(in.radius_v > 0.f) || in.radius_u > 1.f || in.radius_v > 1.f || in.alpha < 0.f || in.alpha > 1.f ||
        in.curve < .25f || in.curve > 4.f || in.u < -4.f || in.u > 5.f || in.v < -4.f || in.v > 5.f) return skip("params");
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
    const float block[2][4] = {{in.u, in.v, in.radius_u, in.radius_v}, {in.alpha, seed ? 1.f : 0.f, in.curve, 0.f}};
    if (SUCCEEDED(hr)) step(SunOcclusionStage::Constants, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 0, &block[0][0], 2));
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
// Once per program and scale mode. Any refusal is final for this device generation of the pass.
bool SunOcclusionPass::build(Variant& v, unsigned mode, IDirect3DPixelShader9* original) noexcept {
    v.state[mode] = 2;
    UINT bytes = 0;
    if (FAILED(original->GetFunction(nullptr, &bytes)) || bytes < 8 || (bytes & 3u) || bytes > (1u << 18)) { variant_refusal_ = "function_size"; return false; }
    try { words_.resize(bytes / 4); } catch (const std::bad_alloc&) { variant_refusal_ = "memory"; return false; }
    if (FAILED(original->GetFunction(words_.data(), &bytes))) { variant_refusal_ = "function_read"; return false; }
    LensVisibilityLayout layout{};
    const LensVisibilityResult result = lens_visibility_pixel_variant(words_.data(), words_.size(), LensVisibilityScale(mode + 1), wrapped_, &layout);
    if (result != LensVisibilityResult::Applied) { variant_refusal_ = lens_visibility_result_name(result); return false; }
    IDirect3DPixelShader9* created = nullptr;
    if (FAILED(call<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(wrapped_.data()), &created)) || !created) { drop(created); variant_refusal_ = "create"; return false; }
    v.shader[mode] = created; v.sampler[mode] = std::uint8_t(layout.sampler); v.state[mode] = 1;
    return true;
}
// Per wrap sampler, recorded once: `lens_saved_` (re-captured per draw: the application's pixel
// shader, the sampler's texture and six states, with references held by the block) and
// `lens_ours_` (the six states a 1x1 FP16 fetch needs). No getter runs per draw: the blend,
// alpha-test and fog state and the bound program come from the caller's shadow.
HRESULT SunOcclusionPass::ensure_lens_blocks(unsigned sampler) noexcept {
    if (lens_saved_[sampler] && lens_ours_[sampler]) return S_OK;
    drop(lens_saved_[sampler]); drop(lens_ours_[sampler]);
    D d = device_;
    auto states = [&]() -> HRESULT {
        for (unsigned i = 0; i < 6; ++i) { const HRESULT hr = call<SetSamplerFn>(SetSamplerState)(d, sampler, lens_states[i], lens_values[i]); if (FAILED(hr)) return hr; }
        return S_OK;
    };
    HRESULT hr = record(&lens_ours_[sampler], states);
    if (SUCCEEDED(hr)) hr = record(&lens_saved_[sampler], [&]() -> HRESULT {
        HRESULT inner = call<SetPsFn>(SetPixelShader)(d, nullptr);
        if (SUCCEEDED(inner)) inner = call<SetTextureFn>(SetTexture)(d, sampler, nullptr);
        return SUCCEEDED(inner) ? states() : inner;
    });
    if (FAILED(hr)) { drop(lens_saved_[sampler]); drop(lens_ours_[sampler]); }
    return hr;
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
    Variant* const v = find(state.hash);
    if (!v) return LensVerdict::CacheFull;
    const unsigned mode = unsigned(scale) - 1u;
    if (v->state[mode] == 0) build(*v, mode, state.shader);
    if (v->state[mode] != 1) return LensVerdict::Variant;
    const unsigned sampler = v->sampler[mode];
    if (FAILED(ensure_lens_blocks(sampler))) return LensVerdict::Device;
    D d = device_;
    device_calls_ = 1;
    if (FAILED(lens_saved_[sampler]->Capture())) return LensVerdict::Device; // nothing changed yet
    out.sampler = std::uint8_t(sampler); out.applied = true;
    ++device_calls_;
    HRESULT hr = lens_ours_[sampler]->Apply();
    if (SUCCEEDED(hr)) hr = call<SetTextureFn>(SetTexture)(d, sampler, textures_[current_]);
    if (SUCCEEDED(hr)) hr = call<SetPsFn>(SetPixelShader)(d, v->shader[mode]);
    if (FAILED(hr)) { lens_end(out); return LensVerdict::Device; } // everything back; the draw goes out unwrapped
    return LensVerdict::Applied;
}
HRESULT SunOcclusionPass::lens_end(LensDraw& draw) noexcept {
    if (!draw.applied) return S_OK;
    draw.applied = false;
    if (!device_ || draw.sampler >= 16 || !lens_saved_[draw.sampler]) return E_FAIL;
    ++device_calls_;
    return lens_saved_[draw.sampler]->Apply(); // the application's shader, texture and sampler states, as captured for this draw
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
