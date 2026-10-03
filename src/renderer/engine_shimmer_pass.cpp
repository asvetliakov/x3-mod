#include "engine_shimmer_pass.h"
#include "ps3_program_slots.h"
#include "quad_vertex_program.h"
#include "../proxy/cpu_state.h"
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <utility>
namespace x3m::renderer {
namespace {
template <class T> void drop(T*& value) noexcept {
    if (value) {
        value->Release();
        value = nullptr;
    }
}
bool lost(HRESULT hr) noexcept {
    return hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET;
}
// IDirect3DDevice9 vtable slots (verification/probe/abi_check.cpp).
// clang-format off
enum Slot : unsigned {
    GetDirect3D = 6, GetCreationParameters = 9, CreateTexture = 23, StretchRect = 34, SetRenderTarget = 37,
    GetRenderTarget = 38, SetDepthStencilSurface = 39, GetDepthStencilSurface = 40, BeginScene = 41, EndScene = 42,
    SetViewport = 47, GetViewport = 48, SetRenderState = 57, CreateStateBlock = 59, SetTexture = 65,
    SetTextureStageState = 67, SetSamplerState = 69, SetScissorRect = 75, GetScissorRect = 76, DrawPrimitiveUP = 83,
    CreateVertexDeclaration = 86, SetVertexDeclaration = 87, GetVertexDeclaration = 88, SetFVF = 89, GetFVF = 90,
    CreateVertexShader = 91, SetVertexShader = 92, SetStreamSourceFreq = 102, SetIndices = 104, CreatePixelShader = 106,
    SetPixelShader = 107, SetPixelShaderConstantF = 109
};
// clang-format on
using D = IDirect3DDevice9*;
using GetD3DFn = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetCreationFn = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);
using CreateTextureFn = HRESULT(WINAPI*)(D, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
using StretchFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE);
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
using GetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9**);
using SetFvfFn = HRESULT(WINAPI*)(D, DWORD);
using GetFvfFn = HRESULT(WINAPI*)(D, DWORD*);
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using SetFreqFn = HRESULT(WINAPI*)(D, UINT, UINT);
using SetIndicesFn = HRESULT(WINAPI*)(D, IDirect3DIndexBuffer9*);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using SetPsConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
// Only our authored shader is embedded (provenance: verification/results/engine-shimmer-pixel-program.json).
constexpr std::uint32_t ps_words[] = {
#include "engine_shimmer_pixel_program_inc.h"
};
static_assert(sizeof(engine_shimmer::QuadVertex) == sizeof(QuadVertex), "the core's quad vertex is the shared layout");
bool finite(float v) noexcept {
    return v == v && v <= 3.4e38f && v >= -3.4e38f;
}
// The lane's forms: device depth in .r (renderer/temporal_pass.h FrameInputs::current_depth).
bool lane_format(D3DFORMAT f) noexcept {
    return f == D3DFMT_R32F || f == D3DFMT_G32R32F || f == D3DFMT_A32B32G32R32F;
}
constexpr UINT samplers = 3; // s0 the copy (linear), s1 the copy (point), s2 the lane (point)
} // namespace

// Everything the draw touches beyond the state block: the render targets and depth, viewport and scissor rect
// (SetRenderTarget resets both) and the vertex input mode (a D3DSBT_ALL block records the declaration, but a caller
// without one, in FVF mode, is restored through its FVF). The sun-shadow apply pass's shape.
struct EngineShimmerPass::SavedState {
    const EngineShimmerPass& pass;
    IDirect3DStateBlock9* block;
    IDirect3DSurface9* targets[4]{};
    IDirect3DSurface9* depth = nullptr;
    IDirect3DVertexDeclaration9* declaration = nullptr;
    DWORD fvf = 0;
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    UINT count;
    SavedState(const EngineShimmerPass& p, IDirect3DStateBlock9* b, UINT n)
        : pass(p)
        , block(b)
        , count(n > 4 ? 4 : n) {}
    ~SavedState() {
        for (auto& t : targets) drop(t);
        drop(depth);
        drop(declaration);
    }
    HRESULT capture() noexcept {
        HRESULT hr = block->Capture();
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
        if (FAILED(hr) && hr != D3DERR_NOTFOUND) return hr;
        hr = pass.call<GetViewportFn>(GetViewport)(pass.device_, &viewport);
        if (FAILED(hr)) return hr;
        return pass.call<GetScissorFn>(GetScissorRect)(pass.device_, &scissor);
    }
    HRESULT restore() noexcept {
        HRESULT first = S_OK;
        auto attempt = [&](HRESULT hr) {
            if (lost(hr) || (FAILED(hr) && SUCCEEDED(first))) first = hr;
            return !lost(hr);
        };
        D d = pass.device_;
        for (UINT i = 0; i < samplers; ++i)
            if (!attempt(pass.call<SetTextureFn>(SetTexture)(d, i, nullptr))) return first;
        if (!attempt(pass.call<SetDepthFn>(SetDepthStencilSurface)(d, nullptr))) return first;
        for (UINT i = 1; i < count; ++i)
            if (!attempt(pass.call<SetRtFn>(SetRenderTarget)(d, i, nullptr))) return first;
        for (UINT i = 0; i < count; ++i)
            if (!attempt(pass.call<SetRtFn>(SetRenderTarget)(d, i, targets[i]))) return first;
        if (!attempt(pass.call<SetDepthFn>(SetDepthStencilSurface)(d, depth))) return first;
        if (!attempt(block->Apply())) return first;
        if (declaration) {
            if (!attempt(pass.call<SetDeclarationFn>(SetVertexDeclaration)(d, declaration))) return first;
        } else if (fvf) {
            if (!attempt(pass.call<SetFvfFn>(SetFVF)(d, fvf))) return first;
        } else if (!attempt(pass.call<SetDeclarationFn>(SetVertexDeclaration)(d, nullptr)))
            return first;
        if (!attempt(pass.call<SetViewportFn>(SetViewport)(d, &viewport))) return first;
        attempt(pass.call<SetScissorFn>(SetScissorRect)(d, &scissor));
        return first;
    }
};

EngineShimmerPass::~EngineShimmerPass() {
    detach();
}
void EngineShimmerPass::release_frame_objects() noexcept {
    drop(block_);
    drop(scratch_surface_);
    drop(scratch_);
    scratch_width_ = scratch_height_ = 0;
    pending_ = nullptr;
}
void EngineShimmerPass::detach() noexcept {
    PreserveCpuState guard;
    release_frame_objects();
    drop(ps_);
    drop(vs_);
    drop(declaration_);
    device_ = nullptr;
    vtable_ = nullptr;
    caps_ = {};
    reset_pending_ = false;
}
void EngineShimmerPass::before_reset() noexcept {
    PreserveCpuState guard;
    release_frame_objects();
    reset_pending_ = device_ != nullptr;
}
void EngineShimmerPass::after_reset(HRESULT hr) noexcept {
    if (SUCCEEDED(hr)) reset_pending_ = false;
}
unsigned EngineShimmerPass::references() const noexcept {
    unsigned n = 0;
    for (const void* p : {static_cast<const void*>(vs_), static_cast<const void*>(ps_), static_cast<const void*>(declaration_),
                          static_cast<const void*>(block_), static_cast<const void*>(scratch_),
                          static_cast<const void*>(scratch_surface_)})
        n += p != nullptr;
    return n;
}
HRESULT EngineShimmerPass::attach(D d, void* const* native, const D3DCAPS9& caps, D3DFORMAT format) noexcept {
    PreserveCpuState guard;
    detach();
    auto refuse = [&](const char* reason, HRESULT hr = D3DERR_NOTAVAILABLE) {
        drop(ps_);
        drop(vs_);
        drop(declaration_);
        device_ = nullptr;
        vtable_ = nullptr;
        caps_.enabled = false;
        caps_.reason = reason;
        return hr;
    };
    if (!d || !native) return refuse("device_native_table", E_INVALIDARG);
    device_ = d;
    vtable_ = native;
    if (caps.PixelShaderVersion < D3DPS_VERSION(3, 0) || caps.VertexShaderVersion < D3DVS_VERSION(3, 0))
        return refuse("shader_model3");
    caps_.ps_slots = ps3_program_slots(ps_words, std::size(ps_words));
    if (!caps_.ps_slots) return refuse("compiled_program");
    // No refusal on the reported slot cap (docs/architecture/platform-portability.md, "Shader slot budget"): creation is
    // the capability test.
    if (!(caps.DevCaps2 & D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES)) return refuse("stretch_from_textures");
    if (!(caps.RasterCaps & D3DPRASTERCAPS_SCISSORTEST)) return refuse("scissor_test");
    if (!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_CULLNONE)) return refuse("cull_none");
    if (!(caps.TextureFilterCaps & D3DPTFILTERCAPS_MINFLINEAR) || !(caps.TextureFilterCaps & D3DPTFILTERCAPS_MAGFLINEAR))
        return refuse("linear_filter");
    if (caps.MaxPrimitiveCount < 2u * engine_shimmer::max_rects || caps.MaxStreams < 1) return refuse("limits");
    // The copy and the target are FP16 render-target textures sampled with linear filtering.
    IDirect3D9* api = nullptr;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    HRESULT hr = call<GetD3DFn>(GetDirect3D)(d, &api);
    if (SUCCEEDED(hr) && !api) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = call<GetCreationFn>(GetCreationParameters)(d, &creation);
    if (SUCCEEDED(hr))
        hr = api->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, format,
                                    D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_FILTER, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F);
    drop(api);
    caps_.fp16_filter = hr;
    if (faults_ & 1u) {
        caps_.fp16_filter = D3DERR_NOTAVAILABLE;
        hr = D3DERR_NOTAVAILABLE;
    }
    if (hr != D3D_OK) return refuse("fp16_filter", D3DERR_NOTAVAILABLE);
    render_targets_ = caps.NumSimultaneousRTs ? caps.NumSimultaneousRTs : 1;
    streams_ = caps.MaxStreams;
    hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(quad_vertex_program()), &vs_);
    if (SUCCEEDED(hr)) hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, quad_declaration, &declaration_);
    if (SUCCEEDED(hr)) hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(ps_words), &ps_);
    if (SUCCEEDED(hr) && !(vs_ && ps_ && declaration_)) hr = E_FAIL;
    if (FAILED(hr)) return refuse("program_create", hr);
    caps_.enabled = true;
    caps_.reason = "ok";
    return S_OK;
}
// The state block and the scratch at the target's size (A16B16G16R16F render-target texture, one level).
HRESULT EngineShimmerPass::ensure_scratch(UINT width, UINT height) noexcept {
    HRESULT hr = S_OK;
    if (!block_) {
        hr = call<CreateBlockFn>(CreateStateBlock)(device_, D3DSBT_ALL, &block_);
        if (SUCCEEDED(hr) && !block_) hr = E_FAIL;
        if (FAILED(hr)) {
            drop(block_);
            return hr;
        }
    }
    if (scratch_ && scratch_width_ == width && scratch_height_ == height) return S_OK;
    pending_ = nullptr; // a revert reads the scratch: never across a recreation (the caller reverts before the next run)
    drop(scratch_surface_);
    drop(scratch_);
    scratch_width_ = scratch_height_ = 0;
    hr = call<CreateTextureFn>(CreateTexture)(device_, width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F,
                                              D3DPOOL_DEFAULT, &scratch_, nullptr);
    if (faults_ & 4u) {
        faults_ &= ~4u;
        drop(scratch_);
        hr = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr) && !scratch_) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = scratch_->GetSurfaceLevel(0, &scratch_surface_);
    if (SUCCEEDED(hr) && !scratch_surface_) hr = E_FAIL;
    if (FAILED(hr)) {
        drop(scratch_surface_);
        drop(scratch_);
        return hr;
    }
    scratch_width_ = width;
    scratch_height_ = height;
    return S_OK;
}
// The draw's whole device state: the target alone (depth and the other targets unbound, RT0 before its full viewport),
// the quad program pair, no blending, the scissor at the rects' union, point samplers on the lane and the copy's own
// pixel, linear on the copy's displaced sample.
HRESULT EngineShimmerPass::normalize(IDirect3DSurface9* target, UINT w, UINT h, const int scissor[4]) noexcept {
    D d = device_;
#define STEP(call)                                                                                                     \
    do {                                                                                                               \
        const HRESULT hresult = (call);                                                                                \
        if (FAILED(hresult)) return hresult;                                                                           \
    } while (false)
    for (UINT i = 0; i < 16; ++i) STEP(call<SetTextureFn>(SetTexture)(d, i, nullptr));
    STEP(call<SetDepthFn>(SetDepthStencilSurface)(d, nullptr));
    for (UINT i = 1; i < render_targets_ && i < 4; ++i) STEP(call<SetRtFn>(SetRenderTarget)(d, i, nullptr));
    STEP(call<SetRtFn>(SetRenderTarget)(d, 0, target));
    STEP(call<SetDeclarationFn>(SetVertexDeclaration)(d, declaration_));
    STEP(call<SetVsFn>(SetVertexShader)(d, vs_));
    STEP(call<SetPsFn>(SetPixelShader)(d, ps_));
    STEP(call<SetIndicesFn>(SetIndices)(d, nullptr));
    for (UINT i = 0; i < streams_; ++i) STEP(call<SetFreqFn>(SetStreamSourceFreq)(d, i, 1));
    for (auto state : {D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_STENCILENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHABLENDENABLE,
                       D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_FOGENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_CLIPPLANEENABLE,
                       D3DRS_LIGHTING, D3DRS_INDEXEDVERTEXBLENDENABLE, D3DRS_POINTSPRITEENABLE, D3DRS_DITHERENABLE,
                       D3DRS_ANTIALIASEDLINEENABLE, D3DRS_MULTISAMPLEANTIALIAS})
        STEP(call<SetRsFn>(SetRenderState)(d, state, FALSE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_CLIPPING, TRUE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_SCISSORTESTENABLE, TRUE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_VERTEXBLEND, D3DVBF_DISABLE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_WRAP0, 0));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_FILLMODE, D3DFILL_SOLID));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, D3DCULL_NONE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_COLORWRITEENABLE, 15));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_MULTISAMPLEMASK, 0xffffffff));
    STEP(call<SetStageFn>(SetTextureStageState)(d, 0, D3DTSS_TEXCOORDINDEX, 0));
    STEP(call<SetStageFn>(SetTextureStageState)(d, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE));
    for (UINT i = 0; i < samplers; ++i) {
        const DWORD filter = i == 0 ? D3DTEXF_LINEAR : D3DTEXF_POINT;
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MINFILTER, filter));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MAGFILTER, filter));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_SRGBTEXTURE, FALSE));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MAXMIPLEVEL, 0));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MIPMAPLODBIAS, 0));
    }
    const D3DVIEWPORT9 viewport{0, 0, w, h, 0.f, 1.f};
    STEP(call<SetViewportFn>(SetViewport)(d, &viewport));
    const RECT rect{scissor[0], scissor[1], scissor[2], scissor[3]};
    STEP(call<SetScissorFn>(SetScissorRect)(d, &rect));
#undef STEP
    return S_OK;
}
HRESULT EngineShimmerPass::revert() noexcept {
    if (!pending_) return S_FALSE;
    IDirect3DSurface9* target = pending_;
    pending_ = nullptr;
    if (reset_pending_) return D3DERR_DEVICELOST; // a lost device set it (before_reset forgets the revert)
    if (!device_ || !caps_.enabled || !scratch_surface_) return E_FAIL;
    PreserveCpuState guard;
    const HRESULT hr =
        call<StretchFn>(StretchRect)(device_, scratch_surface_, &pending_rect_, target, &pending_rect_, D3DTEXF_POINT);
    if (lost(hr)) reset_pending_ = true;
    return hr;
}
HRESULT EngineShimmerPass::run(const EngineShimmerFrame& f, EngineShimmerReport* output) noexcept {
    PreserveCpuState guard;
    EngineShimmerReport r{};
    const unsigned initial = calls_;
    auto finish = [&](HRESULT hr) {
        r.operation = hr;
        r.calls = calls_ - initial;
        if (output) *output = r;
        return hr;
    };
    auto refuse = [&](EngineShimmerStep step, HRESULT hr) {
        r.failed = step;
        if (lost(hr)) reset_pending_ = true;
        return finish(hr);
    };
    auto skip = [&](const char* reason) {
        r.skipped = reason;
        return finish(S_FALSE);
    };
    if (!device_ || !caps_.enabled) return refuse(EngineShimmerStep::Validate, E_INVALIDARG);
    if (reset_pending_) return refuse(EngineShimmerStep::Validate, E_FAIL);
    if (!f.width || !f.height || !f.target || !f.target_surface || (f.rect_count && !f.rects) ||
        f.rect_count > engine_shimmer::max_rects || !finite(f.amplitude_px) || !finite(f.phase) || !finite(f.seconds))
        return refuse(EngineShimmerStep::Validate, E_INVALIDARG);
    // A revert the caller has not issued yet (it reverts before Present): the copy goes back before this frame's.
    if (pending_) {
        r.stale_revert = true;
        const HRESULT hr = revert();
        if (FAILED(hr)) return refuse(EngineShimmerStep::Restore, hr);
    }
    if (!f.rect_count) return skip("no_rects");
    if (!(f.amplitude_px > 0.f)) return skip("amplitude");
    if (f.caller_stateblock_recording) return skip("recording");
    D3DSURFACE_DESC desc{};
    if (FAILED(f.target_surface->GetDesc(&desc)) || desc.Width != f.width || desc.Height != f.height ||
        desc.Format != D3DFMT_A16B16G16R16F || desc.MultiSampleType != D3DMULTISAMPLE_NONE ||
        !(desc.Usage & D3DUSAGE_RENDERTARGET) || desc.Pool != D3DPOOL_DEFAULT)
        return skip("format");
    if (f.lane) {
        D3DSURFACE_DESC lane{};
        if (FAILED(f.lane->GetLevelDesc(0, &lane)) || lane.Width != f.width || lane.Height != f.height ||
            !lane_format(lane.Format))
            return skip("lane");
    }
    // The union of the rects (the scissor) and the copied area: the union grown by the amplitude plus the filter's
    // pixel, so every displaced sample reads copied texels.
    int scissor[4], copy[4];
    const int margin = int(f.amplitude_px) + 2;
    if (!engine_shimmer::union_bounds(f.rects, f.rect_count, 0, int(f.width), int(f.height), scissor) ||
        !engine_shimmer::union_bounds(f.rects, f.rect_count, margin, int(f.width), int(f.height), copy))
        return skip("offscreen");
    for (unsigned j = 0; j < 4; ++j) {
        r.scissor[j] = scissor[j];
        r.copy[j] = copy[j];
    }
    r.scissor_px = unsigned(scissor[2] - scissor[0]) * unsigned(scissor[3] - scissor[1]);
    r.copy_px = unsigned(copy[2] - copy[0]) * unsigned(copy[3] - copy[1]);
    r.rects = f.rect_count;
    HRESULT hr = ensure_scratch(f.width, f.height);
    if (FAILED(hr)) return refuse(EngineShimmerStep::Resources, hr);
    D d = device_;
    const RECT copy_rect{copy[0], copy[1], copy[2], copy[3]};
    hr = call<StretchFn>(StretchRect)(d, f.target_surface, &copy_rect, scratch_surface_, &copy_rect, D3DTEXF_POINT);
    if (FAILED(hr)) return refuse(EngineShimmerStep::Copy, hr);
    pending_ = f.target_surface;
    pending_rect_ = copy_rect;
    float constants[engine_shimmer::constant_vectors * 4];
    engine_shimmer::constants(f.rects, f.rect_count, f.amplitude_px, f.phase, f.seconds, float(f.width), float(f.height),
                              constants);
    // Without a lane the program reads the copy at s2 and every rect's occlusion depth (c36+i.y) is 0: nothing occluded.
    if (!f.lane)
        for (unsigned i = 0; i < f.rect_count; ++i) constants[(4 + 2 * engine_shimmer::max_rects + i) * 4 + 1] = 0.f;
    engine_shimmer::QuadVertex vertices[engine_shimmer::max_rects * engine_shimmer::vertices_per_rect];
    engine_shimmer::vertices(f.rects, f.rect_count, float(f.width), float(f.height), vertices);
    SavedState saved(*this, block_, render_targets_);
    hr = saved.capture();
    if (FAILED(hr)) return refuse(EngineShimmerStep::Capture, hr);
    EngineShimmerStep stage = EngineShimmerStep::State;
    auto step = [&](EngineShimmerStep s, HRESULT value) {
        stage = s;
        hr = value;
        return SUCCEEDED(hr);
    };
    bool own_scene = false;
    step(EngineShimmerStep::State, normalize(f.target_surface, f.width, f.height, scissor));
    if (SUCCEEDED(hr) && !f.caller_scene_open) own_scene = step(EngineShimmerStep::State, call<SceneFn>(BeginScene)(d));
    if (SUCCEEDED(hr))
        step(EngineShimmerStep::State, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 0, constants,
                                                                                     engine_shimmer::constant_vectors));
    if (SUCCEEDED(hr)) step(EngineShimmerStep::State, call<SetTextureFn>(SetTexture)(d, 0, scratch_));
    if (SUCCEEDED(hr)) step(EngineShimmerStep::State, call<SetTextureFn>(SetTexture)(d, 1, scratch_));
    if (SUCCEEDED(hr)) {
        IDirect3DBaseTexture9* lane = f.lane ? static_cast<IDirect3DBaseTexture9*>(f.lane) : scratch_;
        step(EngineShimmerStep::State, call<SetTextureFn>(SetTexture)(d, 2, lane));
    }
    if (SUCCEEDED(hr)) {
        HRESULT draw = call<DrawUpFn>(DrawPrimitiveUP)(d, D3DPT_TRIANGLELIST, 2u * f.rect_count, vertices,
                                                        UINT(sizeof(engine_shimmer::QuadVertex)));
        if (faults_ & 2u) {
            faults_ &= ~2u;
            draw = E_FAIL;
        }
        if (step(EngineShimmerStep::Draw, draw)) r.drew = true;
    }
    if (own_scene && !lost(hr)) {
        const HRESULT end = call<SceneFn>(EndScene)(d);
        if (SUCCEEDED(hr) || lost(end)) {
            if (FAILED(end)) stage = EngineShimmerStep::Draw;
            hr = end;
        }
    }
    r.restore = lost(hr) ? hr : saved.restore();
    if (FAILED(hr) || FAILED(r.restore)) {
        r.failed = FAILED(hr) ? stage : EngineShimmerStep::Restore;
        r.drew = false;
        return refuse(r.failed, FAILED(r.restore) ? r.restore : hr);
    }
    return finish(S_OK);
}
} // namespace x3m::renderer
