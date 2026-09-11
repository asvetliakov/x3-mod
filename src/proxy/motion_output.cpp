#include "motion_output.h"
#include "capture.h"
#include "capture_state.h"
#include "scene_capture.h"
#include "telemetry.h"
#include "object_trace.h"
#include "object_lifetime.h"
#include "../renderer/material_motion.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace x3m {
namespace {
// The shadow tracks one clip-row window and gate 4 applies one light-loop
// bound for every routed pair, so every profile row must name exactly these;
// a regenerated table with another matrix register or loop bound fails here
// rather than routing draws whose rows the shadow never captured.
constexpr UINT matrix_register = 24, matrix_register_end = 28;
constexpr int light_loop_max_count = 8;
constexpr bool rows_match_shadow() noexcept {
    for (const auto& row : renderer::motion_output_profiles)
        if (row.matrix_register != matrix_register || !row.light_loop_bound_required ||
            row.light_loop_max_count != light_loop_max_count)
            return false;
    return true;
}
static_assert(rows_match_shadow(), "every profile row must use the shadowed c24-27 window and the i0.x <= 8 bound");
// IDirect3DDevice9 vtable slots, verified against the MinGW d3d9.h method order
// by verification/probe/abi_check.cpp (compile-time offsetof assertions).
enum Slot : unsigned {
    GetDirect3D = 6, GetDisplayMode = 8, GetCreationParameters = 9,
    CreateTexture = 23, CreateRenderTarget = 28, GetRenderTargetData = 32,
    CreateOffscreenPlainSurface = 36, SetRenderTarget = 37, GetRenderTarget = 38,
    SetDepthStencilSurface = 39, GetDepthStencilSurface = 40, BeginScene = 41, EndScene = 42,
    SetViewport = 47, GetViewport = 48, SetRenderState = 57, GetRenderState = 58,
    SetScissorRect = 75, GetScissorRect = 76, DrawPrimitiveUP = 83,
    SetVertexDeclaration = 87, GetVertexDeclaration = 88, SetFVF = 89, GetFVF = 90,
    CreateVertexShader = 91, SetVertexShader = 92, GetVertexShader = 93,
    SetVertexShaderConstantF = 94, GetVertexShaderConstantF = 95, GetVertexShaderConstantI = 97,
    SetStreamSource = 100, GetStreamSource = 101, GetStreamSourceFreq = 103, GetIndices = 105,
    CreatePixelShader = 106, SetPixelShader = 107, GetPixelShader = 108,
    SetPixelShaderConstantF = 109, GetPixelShaderConstantF = 110
};
using D = IDirect3DDevice9*;
using SetRenderTargetFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DSurface9*);
using GetRenderTargetFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DSurface9**);
using SetDepthFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*);
using GetDepthFn = HRESULT(WINAPI*)(D, IDirect3DSurface9**);
using SceneFn = HRESULT(WINAPI*)(D);
using SetViewportFn = HRESULT(WINAPI*)(D, const D3DVIEWPORT9*);
using GetViewportFn = HRESULT(WINAPI*)(D, D3DVIEWPORT9*);
using SetRenderStateFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD);
using GetRenderStateFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD*);
using SetScissorFn = HRESULT(WINAPI*)(D, const RECT*);
using GetScissorFn = HRESULT(WINAPI*)(D, RECT*);
using DrawUpFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using SetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9*);
using GetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9**);
using SetFvfFn = HRESULT(WINAPI*)(D, DWORD);
using GetFvfFn = HRESULT(WINAPI*)(D, DWORD*);
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using GetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9**);
using SetConstantsFFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
using GetConstantsFFn = HRESULT(WINAPI*)(D, UINT, float*, UINT);
using GetConstantsIFn = HRESULT(WINAPI*)(D, UINT, int*, UINT);
using SetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
using GetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9**, UINT*, UINT*);
using GetStreamFreqFn = HRESULT(WINAPI*)(D, UINT, UINT*);
using GetIndicesFn = HRESULT(WINAPI*)(D, IDirect3DIndexBuffer9**);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using GetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9**);
using CreateTextureFn = HRESULT(WINAPI*)(D, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
using CreateRtFn = HRESULT(WINAPI*)(D, UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9**, HANDLE*);
using CreateOffscreenFn = HRESULT(WINAPI*)(D, UINT, UINT, D3DFORMAT, D3DPOOL, IDirect3DSurface9**, HANDLE*);
using GetRtDataFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*, IDirect3DSurface9*);
using GetDirect3DFn = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetDisplayModeFn = HRESULT(WINAPI*)(D, UINT, D3DDISPLAYMODE*);
using GetCreationFn = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);

// ps_2_0: def c0, 0, 0, 0, -1 ; mov oC0, c0 ; end. Writes the invalid-history
// sentinel of the RGBA32F motion ABI (alpha -1) to every covered texel.
constexpr DWORD sentinel_program[] = {
    0xffff0200u, 0x05000051u, 0xa00f0000u, 0x00000000u, 0x00000000u, 0x00000000u, 0xbf800000u,
    0x02000001u, 0x800f0800u, 0xa0e40000u, 0x0000ffffu};
// ps_2_0 self-test: oC0 = (0.25, 0.5, 0.75, 1) into A8R8G8B8, oC1 = (1, 2, 3, -1)
// into A32B32G32R32F. Both targets are read back to prove mixed-format MRT.
constexpr DWORD self_test_program[] = {
    0xffff0200u,
    0x05000051u, 0xa00f0000u, 0x3e800000u, 0x3f000000u, 0x3f400000u, 0x3f800000u,
    0x05000051u, 0xa00f0001u, 0x3f800000u, 0x40000000u, 0x40400000u, 0xbf800000u,
    0x02000001u, 0x800f0800u, 0xa0e40000u,
    0x02000001u, 0x800f0801u, 0xa0e40001u,
    0x0000ffffu};
// Render states the injected fullscreen draws set; each is saved and restored.
constexpr D3DRENDERSTATETYPE touched_states[] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHABLENDENABLE,
    D3DRS_CULLMODE, D3DRS_FILLMODE, D3DRS_COLORWRITEENABLE, D3DRS_SCISSORTESTENABLE,
    D3DRS_STENCILENABLE, D3DRS_FOGENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_CLIPPLANEENABLE};
constexpr DWORD touched_values[] = {FALSE, FALSE, FALSE, FALSE, D3DCULL_NONE, D3DFILL_SOLID, 15, FALSE, FALSE, FALSE, FALSE, 0};
constexpr unsigned touched_count = sizeof(touched_states) / sizeof(touched_states[0]);
static_assert(touched_count == sizeof(touched_values) / sizeof(touched_values[0]));
constexpr unsigned failure_log_limit = 16;

std::uint64_t hash_bytes(const void* data, std::size_t size) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    auto bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
    return hash;
}
// Clears the member before the COM call so a re-entrant path never sees it.
template<class T> void release(T*& object) noexcept { if (T* held = object) { object = nullptr; held->Release(); } }
bool same(const renderer::Surface& a, const renderer::Surface& b) noexcept {
    return a.known && b.known && a.identity && a.identity == b.identity && a.container == b.container &&
           a.width == b.width && a.height == b.height && a.format == b.format && a.msaa == b.msaa;
}
} // namespace

// Everything the fullscreen draws touch. COM references returned by the getters
// are released by the destructor after restoration.
struct MotionOutput::SavedState {
    IDirect3DSurface9* targets[4]{};
    IDirect3DSurface9* depth = nullptr;
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    DWORD fvf = 0;
    IDirect3DVertexDeclaration9* declaration = nullptr;
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexBuffer9* stream = nullptr;
    UINT offset = 0, stride = 0;
    DWORD states[touched_count]{};
    unsigned target_count = 1;
    ~SavedState() {
        for (auto& target : targets) release(target);
        release(depth); release(declaration); release(vs); release(ps); release(stream);
    }
};

MotionOutput::MotionOutput() noexcept = default;
MotionOutput::~MotionOutput() { release_resources(); }

unsigned MotionOutput::device_references() const noexcept {
    if (releasing_) return 0;
    unsigned count = target_ ? 1 : 0;
    if (sentinel_ps_) ++count;
    for (const auto& entry : vertex_) if (entry.second.variant) ++count;
    for (const auto& entry : pixel_) if (entry.second.variant) ++count;
    return count;
}

// Releases every owned device object. Called before the application's final
// device Release, before destruction, and never while a draw is routed.
// Re-entrant by construction: each child's final Release calls the device's
// Release through the hooked vtable, so members are cleared before the call
// and the release hook sees zero held references meanwhile.
void MotionOutput::release_resources() noexcept {
    if (releasing_) return;
    releasing_ = true;
    release_target();
    release(sentinel_ps_);
    for (auto& entry : vertex_) release(entry.second.variant);
    for (auto& entry : pixel_) release(entry.second.variant);
    shadow_.vs_variant = nullptr; shadow_.ps_variant = nullptr;
    history_.invalidate();
    fill_pending_ = false;
    releasing_ = false;
}

void MotionOutput::release_target() noexcept {
    release(target_surface_);
    release(target_);
    target_width_ = target_height_ = 0;
}

// Lazily (re)creates the RGBA32F motion target at the latched main dimensions.
bool MotionOutput::ensure_target(UINT width, UINT height) noexcept {
    if (target_ && target_surface_ && target_width_ == width && target_height_ == height) return true;
    release_target();
    if (target_failed_ || !width || !height) return false;
    const HRESULT hr = native<CreateTextureFn>(CreateTexture)(device_, width, height, 1, D3DUSAGE_RENDERTARGET,
        D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &target_, nullptr);
    HRESULT level = E_FAIL;
    if (SUCCEEDED(hr) && target_) level = target_->GetSurfaceLevel(0, &target_surface_);
    if (FAILED(hr) || FAILED(level) || !target_surface_) {
        log("motion_output_target device=%llu width=%u height=%u create=%08lx level=%08lx", id_, width, height, hr, level);
        release_target();
        target_failed_ = true; // Retry only after Reset; do not spam allocation per frame.
        return false;
    }
    target_width_ = width; target_height_ = height;
    history_.invalidate();
    log("motion_output_target device=%llu width=%u height=%u create=%08lx level=%08lx", id_, width, height, hr, level);
    return true;
}

void MotionOutput::attach(IDirect3DDevice9* device, void** native_table, std::uint64_t device_id,
                          const D3DCAPS9& caps, bool requested) noexcept {
    device_ = device; native_ = native_table; id_ = device_id; caps_ = caps; requested_ = requested;
    enabled_ = false;
    if (!requested) return;
    history_ = renderer::MotionRowHistory(4096); // Reserves both tables once; ready() false on failure.
    history_available_ = object_trace::active() && object_lifetime::active();
    const char* reason = "ok";
    char detail[160] = "";
    HRESULT format_result = S_OK;
    if (caps.NumSimultaneousRTs < 2) reason = "mrt_count";
    else if (caps.MaxVertexShaderConst < 256) reason = "vs_constants";
    else if (!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS)) reason = "mrt_bit_depths";
    else if (D3DSHADER_VERSION_MAJOR(caps.VertexShaderVersion) < 3 ||
             D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion) < 3) reason = "shader_model";
    else {
        IDirect3D9* factory = nullptr;
        D3DDEVICE_CREATION_PARAMETERS creation{};
        D3DDISPLAYMODE mode{};
        if (FAILED(native<GetDirect3DFn>(GetDirect3D)(device_, &factory)) || !factory) reason = "factory";
        else if (FAILED(native<GetCreationFn>(GetCreationParameters)(device_, &creation)) ||
                 FAILED(native<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &mode))) reason = "adapter_query";
        else {
            format_result = factory->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, mode.Format,
                D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A32B32G32R32F);
            if (FAILED(format_result)) reason = "rgba32f_target";
        }
        release(factory);
    }
    if (!std::strcmp(reason, "ok")) {
        const HRESULT hr = native<CreatePsFn>(CreatePixelShader)(device_, sentinel_program, &sentinel_ps_);
        if (FAILED(hr) || !sentinel_ps_) { reason = "sentinel_shader"; std::snprintf(detail, sizeof detail, "%08lx", hr); }
    }
    if (!std::strcmp(reason, "ok") && !self_test(detail, sizeof detail)) reason = "self_test";
    enabled_ = !std::strcmp(reason, "ok");
    if (!enabled_) release(sentinel_ps_);
    else { resync_shadow(); begin_frame(0, false); } // The first frame has no preceding Present.
    if (!history_available_) history_.invalidate();
    log("motion_output_device device=%llu enabled=%u reason=%s detail=%s mrt=%lu vs_constants=%lu misc=%08lx vs=%08lx ps=%08lx rgba32f=%08lx history_available=%u history_capacity=%u",
        id_, enabled_, reason, detail[0] ? detail : "-", caps.NumSimultaneousRTs, caps.MaxVertexShaderConst,
        caps.PrimitiveMiscCaps, caps.VertexShaderVersion, caps.PixelShaderVersion, format_result,
        history_available_, unsigned(history_.stats().capacity));
}

// One actual mixed-format MRT draw into a 4x4 A8R8G8B8 + A32B32G32R32F pair,
// read back through system memory. A device that reports the capability but
// cannot execute the combination is refused here rather than during gameplay.
bool MotionOutput::self_test(char* reason, std::size_t reason_size) noexcept {
    IDirect3DSurface9* color = nullptr; IDirect3DTexture9* motion = nullptr; IDirect3DSurface9* motion_surface = nullptr;
    IDirect3DSurface9* color_copy = nullptr; IDirect3DSurface9* motion_copy = nullptr;
    IDirect3DPixelShader9* shader = nullptr;
    bool ok = false;
    HRESULT hr = S_OK, restore = S_OK, draw = S_OK, scene = S_OK;
    unsigned color_errors = 0, motion_errors = 0;
    const char* stage = "create";
    do {
        if (FAILED(hr = native<CreateRtFn>(CreateRenderTarget)(device_, 4, 4, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &color, nullptr))) break;
        if (FAILED(hr = native<CreateTextureFn>(CreateTexture)(device_, 4, 4, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &motion, nullptr))) break;
        if (FAILED(hr = motion->GetSurfaceLevel(0, &motion_surface))) break;
        if (FAILED(hr = native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, 4, 4, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &color_copy, nullptr))) break;
        if (FAILED(hr = native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, 4, 4, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &motion_copy, nullptr))) break;
        if (FAILED(hr = native<CreatePsFn>(CreatePixelShader)(device_, self_test_program, &shader))) break;
        stage = "scene";
        // Attach runs right after CreateDevice, outside any application scene.
        if (FAILED(scene = native<SceneFn>(BeginScene)(device_))) { hr = scene; break; }
        stage = "draw";
        draw = draw_quad(color, motion_surface, shader, 4, 4, &restore);
        scene = native<SceneFn>(EndScene)(device_);
        if (FAILED(draw)) { hr = draw; break; }
        if (FAILED(restore)) { hr = restore; stage = "restore"; break; }
        if (FAILED(scene)) { hr = scene; break; }
        stage = "readback";
        if (FAILED(hr = native<GetRtDataFn>(GetRenderTargetData)(device_, color, color_copy))) break;
        if (FAILED(hr = native<GetRtDataFn>(GetRenderTargetData)(device_, motion_surface, motion_copy))) break;
        D3DLOCKED_RECT lock{};
        if (FAILED(hr = color_copy->LockRect(&lock, nullptr, D3DLOCK_READONLY))) break;
        for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
            DWORD value = 0; std::memcpy(&value, static_cast<const char*>(lock.pBits) + y * lock.Pitch + x * 4, 4);
            const int a = int(value >> 24), r = int((value >> 16) & 255), g = int((value >> 8) & 255), b = int(value & 255);
            if (a != 255 || r < 62 || r > 66 || g < 126 || g > 130 || b < 189 || b > 193) ++color_errors;
        }
        color_copy->UnlockRect();
        if (FAILED(hr = motion_copy->LockRect(&lock, nullptr, D3DLOCK_READONLY))) break;
        for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
            float value[4]; std::memcpy(value, static_cast<const char*>(lock.pBits) + y * lock.Pitch + x * 16, 16);
            if (value[0] != 1.f || value[1] != 2.f || value[2] != 3.f || value[3] != -1.f) ++motion_errors;
        }
        motion_copy->UnlockRect();
        stage = "compare";
        ok = !color_errors && !motion_errors;
    } while (false);
    release(shader); release(motion_copy); release(color_copy); release(motion_surface); release(motion); release(color);
    std::snprintf(reason, reason_size, "stage=%s result=%08lx draw=%08lx restore=%08lx scene=%08lx color_errors=%u motion_errors=%u",
                  stage, hr, draw, restore, scene, color_errors, motion_errors);
    return ok;
}

HRESULT MotionOutput::save_state(SavedState& saved) noexcept {
    saved.target_count = caps_.NumSimultaneousRTs < 4 ? unsigned(caps_.NumSimultaneousRTs) : 4;
    if (!saved.target_count) saved.target_count = 1;
    HRESULT hr;
    for (unsigned i = 0; i < saved.target_count; ++i) {
        hr = native<GetRenderTargetFn>(GetRenderTarget)(device_, i, &saved.targets[i]);
        // Unbound extra targets report D3DERR_NOTFOUND with a null surface.
        if (FAILED(hr) && !(i && hr == D3DERR_NOTFOUND && !saved.targets[i])) return hr;
        if (!i && !saved.targets[0]) return E_FAIL;
    }
    hr = native<GetDepthFn>(GetDepthStencilSurface)(device_, &saved.depth);
    if (FAILED(hr) && !(hr == D3DERR_NOTFOUND && !saved.depth)) return hr;
    if (FAILED(hr = native<GetViewportFn>(GetViewport)(device_, &saved.viewport))) return hr;
    if (FAILED(hr = native<GetScissorFn>(GetScissorRect)(device_, &saved.scissor))) return hr;
    if (FAILED(hr = native<GetFvfFn>(GetFVF)(device_, &saved.fvf))) return hr;
    if (FAILED(hr = native<GetDeclarationFn>(GetVertexDeclaration)(device_, &saved.declaration))) return hr;
    if (FAILED(hr = native<GetVsFn>(GetVertexShader)(device_, &saved.vs))) return hr;
    if (FAILED(hr = native<GetPsFn>(GetPixelShader)(device_, &saved.ps))) return hr;
    if (FAILED(hr = native<GetStreamFn>(GetStreamSource)(device_, 0, &saved.stream, &saved.offset, &saved.stride))) return hr;
    for (unsigned i = 0; i < touched_count; ++i)
        if (FAILED(hr = native<GetRenderStateFn>(GetRenderState)(device_, touched_states[i], &saved.states[i]))) return hr;
    return S_OK;
}

// Restores in an order that is correct under D3D9 semantics: SetRenderTarget(0)
// resets viewport and scissor, so those come after the targets; FVF and
// declaration are two views of one binding, restored through whichever the
// application used; DrawPrimitiveUP clears stream 0, so it is rebound.
HRESULT MotionOutput::restore_state(const SavedState& saved) noexcept {
    HRESULT first = S_OK;
    auto step = [&](HRESULT hr) { if (SUCCEEDED(first) && FAILED(hr)) first = hr; };
    step(native<SetRenderTargetFn>(SetRenderTarget)(device_, 0, saved.targets[0]));
    for (unsigned i = 1; i < saved.target_count; ++i)
        step(native<SetRenderTargetFn>(SetRenderTarget)(device_, i, saved.targets[i]));
    step(native<SetDepthFn>(SetDepthStencilSurface)(device_, saved.depth));
    step(native<SetViewportFn>(SetViewport)(device_, &saved.viewport));
    step(native<SetScissorFn>(SetScissorRect)(device_, &saved.scissor));
    if (saved.fvf) step(native<SetFvfFn>(SetFVF)(device_, saved.fvf));
    else step(native<SetDeclarationFn>(SetVertexDeclaration)(device_, saved.declaration));
    step(native<SetVsFn>(SetVertexShader)(device_, saved.vs));
    step(native<SetPsFn>(SetPixelShader)(device_, saved.ps));
    step(native<SetStreamFn>(SetStreamSource)(device_, 0, saved.stream, saved.offset, saved.stride));
    for (unsigned i = 0; i < touched_count; ++i)
        step(native<SetRenderStateFn>(SetRenderState)(device_, touched_states[i], saved.states[i]));
    return first;
}

// Fullscreen XYZRHW strip through the fixed-function vertex path with `shader`
// bound, RT0/RT1 as given, no depth, full viewport. Returns the operation
// result; *restore receives the restoration result separately. Nothing is
// changed if the initial state query fails.
HRESULT MotionOutput::draw_quad(IDirect3DSurface9* rt0, IDirect3DSurface9* rt1, IDirect3DPixelShader9* shader,
                                UINT width, UINT height, HRESULT* restore) noexcept {
    *restore = S_OK;
    SavedState saved;
    HRESULT hr = save_state(saved);
    if (FAILED(hr)) return hr;
    HRESULT op = S_OK;
    auto step = [&](HRESULT result) { if (SUCCEEDED(op) && FAILED(result)) op = result; };
    step(native<SetRenderTargetFn>(SetRenderTarget)(device_, 0, rt0));
    for (unsigned i = 1; i < saved.target_count; ++i)
        step(native<SetRenderTargetFn>(SetRenderTarget)(device_, i, i == 1 ? rt1 : nullptr));
    step(native<SetDepthFn>(SetDepthStencilSurface)(device_, nullptr));
    const D3DVIEWPORT9 viewport{0, 0, width, height, 0.f, 1.f};
    step(native<SetViewportFn>(SetViewport)(device_, &viewport));
    step(native<SetFvfFn>(SetFVF)(device_, D3DFVF_XYZRHW));
    step(native<SetVsFn>(SetVertexShader)(device_, nullptr));
    step(native<SetPsFn>(SetPixelShader)(device_, shader));
    for (unsigned i = 0; i < touched_count; ++i)
        step(native<SetRenderStateFn>(SetRenderState)(device_, touched_states[i], touched_values[i]));
    if (SUCCEEDED(op)) {
        // Integer raster sample positions: shift by -0.5 so every texel center is covered.
        const float w = float(width) - .5f, h = float(height) - .5f;
        const float quad[4][4] = {{-.5f, -.5f, 0.f, 1.f}, {w, -.5f, 0.f, 1.f}, {-.5f, h, 0.f, 1.f}, {w, h, 0.f, 1.f}};
        step(native<DrawUpFn>(DrawPrimitiveUP)(device_, D3DPT_TRIANGLESTRIP, 2, quad, sizeof quad[0]));
    }
    *restore = restore_state(saved);
    return op;
}

// Writes the invalid sentinel over the whole motion target. Deferred from the
// latching Clear to the next draw hook so it always runs inside the
// application's BeginScene/EndScene, on both Windows and Wine.
void MotionOutput::fill_sentinel() noexcept {
    fill_pending_ = false;
    if (!target_surface_ || !sentinel_ps_ || shadow_.recording) { counters_.fill_result = E_ABORT; return; }
    HRESULT restore = S_OK;
    const HRESULT hr = draw_quad(target_surface_, nullptr, sentinel_ps_, target_width_, target_height_, &restore);
    counters_.fill_result = hr; counters_.fill_restore = restore;
    counters_.filled = SUCCEEDED(hr) && SUCCEEDED(restore);
    if ((FAILED(hr) || FAILED(restore)) && logged_failures_ < failure_log_limit) {
        ++logged_failures_;
        log("motion_output_fill_failed device=%llu frame=%llu result=%08lx restore=%08lx", id_, frame_, hr, restore);
    }
    if (FAILED(restore)) ++counters_.restore_failures;
}

void MotionOutput::before_reset() noexcept {
    // D3DPOOL_DEFAULT objects must not exist across Reset; shaders survive it.
    release_target();
    target_failed_ = false;
    history_.invalidate();
    selector_.invalidate();
    fill_pending_ = false; pending_valid_ = false;
    main_ = {}; main_depth_ = {};
}
void MotionOutput::after_reset(HRESULT result) noexcept {
    ++generation_;
    if (!enabled_) return;
    // The interrupted frame continues after a successful Reset; capture is off.
    if (SUCCEEDED(result)) { resync_shadow(); begin_frame(frame_, false); }
    log("motion_output_reset device=%llu result=%08lx generation=%llu", id_, result, generation_);
}

// ---- shader registry -------------------------------------------------------

void MotionOutput::register_vertex_shader(IDirect3DVertexShader9* shader, const DWORD* code,
                                          std::size_t bytes, std::uint64_t hash) noexcept {
    if (!requested_ || !shader) return;
    try {
        auto& entry = vertex_[shader];
        release(entry.variant);
        entry.hash = hash;
        if (shadow_.vs == shader) { shadow_.vs_hash = hash; shadow_.vs_variant = nullptr; }
        if (!enabled_ || !code || bytes % 4) return;
        // One variant per original program: rows sharing this VS agree on its
        // side of the splice (static_assert in motion_output_profiles.h), so
        // the same variant serves every reviewed pair it belongs to. Pair
        // eligibility is decided per draw in before_draw (gate 3).
        bool candidate = false;
        for (const auto& pair : renderer::material_motion_reviewed_pairs) candidate |= pair.vertex_fingerprint == hash;
        if (!candidate) return;
        std::vector<std::uint32_t> words;
        const auto result = renderer::material_motion_vertex_variant(reinterpret_cast<const std::uint32_t*>(code), bytes / 4, words);
        IDirect3DVertexShader9* variant = nullptr;
        HRESULT hr = E_FAIL;
        if (result == renderer::MaterialMotionResult::Applied)
            hr = native<CreateVsFn>(CreateVertexShader)(device_, reinterpret_cast<const DWORD*>(words.data()), &variant);
        if (SUCCEEDED(hr) && variant) entry.variant = variant;
        log("motion_output_variant device=%llu kind=vs original=%016llx transform=%u create=%08lx words=%u",
            id_, hash, unsigned(result), hr, unsigned(words.size()));
    } catch (...) {}
}
void MotionOutput::register_pixel_shader(IDirect3DPixelShader9* shader, const DWORD* code,
                                         std::size_t bytes, std::uint64_t hash) noexcept {
    if (!requested_ || !shader) return;
    try {
        auto& entry = pixel_[shader];
        release(entry.variant);
        entry.hash = hash;
        if (shadow_.ps == shader) { shadow_.ps_hash = hash; shadow_.ps_variant = nullptr; }
        if (!enabled_ || !code || bytes % 4) return;
        bool candidate = false;
        for (const auto& pair : renderer::material_motion_reviewed_pairs) candidate |= pair.pixel_fingerprint == hash;
        if (!candidate) return;
        std::vector<std::uint32_t> words;
        const auto result = renderer::material_motion_pixel_variant(reinterpret_cast<const std::uint32_t*>(code), bytes / 4, words);
        IDirect3DPixelShader9* variant = nullptr;
        HRESULT hr = E_FAIL;
        if (result == renderer::MaterialMotionResult::Applied)
            hr = native<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(words.data()), &variant);
        if (SUCCEEDED(hr) && variant) entry.variant = variant;
        log("motion_output_variant device=%llu kind=ps original=%016llx transform=%u create=%08lx words=%u",
            id_, hash, unsigned(result), hr, unsigned(words.size()));
    } catch (...) {}
}

// ---- shadow ----------------------------------------------------------------

void MotionOutput::set_vertex_shader(IDirect3DVertexShader9* shader) noexcept {
    if (!enabled_ || shadow_.recording) return;
    shadow_.vs = shader; shadow_.vs_hash = 0; shadow_.vs_variant = nullptr;
    if (!shader) return;
    const auto it = vertex_.find(shader);
    if (it == vertex_.end()) return;
    shadow_.vs_hash = it->second.hash;
    shadow_.vs_variant = static_cast<IDirect3DVertexShader9*>(it->second.variant);
}
void MotionOutput::set_pixel_shader(IDirect3DPixelShader9* shader) noexcept {
    if (!enabled_ || shadow_.recording) return;
    shadow_.ps = shader; shadow_.ps_hash = 0; shadow_.ps_variant = nullptr;
    if (!shader) return;
    const auto it = pixel_.find(shader);
    if (it == pixel_.end()) return;
    shadow_.ps_hash = it->second.hash;
    shadow_.ps_variant = static_cast<IDirect3DPixelShader9*>(it->second.variant);
}
void MotionOutput::set_vertex_constants_f(UINT start, const float* data, UINT count) noexcept {
    if (!enabled_ || shadow_.recording || !data || !count || start > 4096 || count > 4096) return;
    const UINT end = start + count;
    // Rows c24-27 (every row's matrix register): the exact submitted position rows.
    if (start < matrix_register_end && end > matrix_register) {
        const UINT lo = start > matrix_register ? start : matrix_register;
        const UINT hi = end < matrix_register_end ? end : matrix_register_end;
        std::memcpy(shadow_.rows + (lo - matrix_register) * 4, data + (lo - start) * 4, (hi - lo) * 16);
        // A partial row update keeps prior knowledge of the other rows.
        shadow_.rows_known = shadow_.rows_known || (lo == matrix_register && hi == matrix_register_end);
    }
    // Reserved c252-255: remember the application values so a routed draw can put them back.
    if (start < 256 && end > 252) {
        const UINT lo = start > 252 ? start : 252, hi = end < 256 ? end : 256;
        std::memcpy(shadow_.vs_reserved + (lo - 252) * 4, data + (lo - start) * 4, (hi - lo) * 16);
        shadow_.vs_reserved_written = true;
    }
}
void MotionOutput::set_vertex_constants_i(UINT start, const int* data, UINT count) noexcept {
    if (!enabled_ || shadow_.recording || !data || !count || start) return;
    std::memcpy(shadow_.integer0, data, 16);
    shadow_.integer0_known = true;
}
void MotionOutput::set_pixel_constants_f(UINT start, const float* data, UINT count) noexcept {
    if (!enabled_ || shadow_.recording || !data || !count || start > 4096 || count > 4096) return;
    const UINT end = start + count;
    if (start < 218 && end > 216) {
        const UINT lo = start > 216 ? start : 216, hi = end < 218 ? end : 218;
        std::memcpy(shadow_.ps_reserved + (lo - 216) * 4, data + (lo - start) * 4, (hi - lo) * 16);
        shadow_.ps_reserved_written = true;
    }
}
void MotionOutput::set_stream_source(UINT stream, IDirect3DVertexBuffer9* buffer, UINT offset, UINT stride) noexcept {
    if (!enabled_ || shadow_.recording || stream) return;
    shadow_.stream0 = buffer ? resource_id(buffer) : 0;
    shadow_.stream0_offset = offset; shadow_.stream0_stride = stride;
}
void MotionOutput::set_indices(IDirect3DIndexBuffer9* buffer) noexcept {
    if (!enabled_ || shadow_.recording) return;
    shadow_.indices = buffer ? resource_id(buffer) : 0;
}
// Declaration identity is the hash of its elements (as draw_input does), plus
// the stream-0 POSITION0 layout the key records.
void MotionOutput::set_vertex_declaration(IDirect3DVertexDeclaration9* declaration) noexcept {
    if (!enabled_ || shadow_.recording) return;
    shadow_.declaration = 0; shadow_.position_offset = shadow_.position_type = 0;
    if (!declaration) return;
    D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH + 1]{};
    UINT count = MAXD3DDECLLENGTH + 1;
    if (FAILED(declaration->GetDeclaration(elements, &count)) || count < 2 || count > MAXD3DDECLLENGTH + 1) return;
    bool position = false;
    for (UINT i = 0; i + 1 < count; ++i)
        if (elements[i].Stream == 0 && elements[i].Usage == D3DDECLUSAGE_POSITION && elements[i].UsageIndex == 0) {
            position = true; shadow_.position_offset = elements[i].Offset; shadow_.position_type = elements[i].Type;
        }
    if (!position) return;
    shadow_.declaration = hash_bytes(elements, count * sizeof(elements[0]));
    if (!shadow_.declaration) shadow_.declaration = 1;
}
void MotionOutput::set_fvf(DWORD) noexcept {
    if (!enabled_ || shadow_.recording) return;
    // SetFVF binds a runtime-owned declaration; identify it the same way.
    IDirect3DVertexDeclaration9* declaration = nullptr;
    if (SUCCEEDED(native<GetDeclarationFn>(GetVertexDeclaration)(device_, &declaration))) set_vertex_declaration(declaration);
    else { shadow_.declaration = 0; shadow_.position_offset = shadow_.position_type = 0; }
    release(declaration);
}
void MotionOutput::set_viewport(const D3DVIEWPORT9* viewport) noexcept {
    if (!enabled_ || shadow_.recording || !viewport) return;
    shadow_.viewport = {true, viewport->X, viewport->Y, viewport->Width, viewport->Height, viewport->MinZ, viewport->MaxZ};
}
void MotionOutput::begin_stateblock() noexcept { if (enabled_) shadow_.recording = true; }
void MotionOutput::end_stateblock() noexcept { if (enabled_) { shadow_.recording = false; resync_shadow(); } }
void MotionOutput::stateblock_applied() noexcept { if (enabled_ && !shadow_.recording) resync_shadow(); }

void MotionOutput::describe_binding(DWORD index, IDirect3DSurface9* surface) noexcept {
    if (index == 0) {
        shadow_.rt0 = describe_surface(surface);
        // SetRenderTarget(0) resets the viewport to the new target: read it once.
        D3DVIEWPORT9 viewport{};
        if (SUCCEEDED(native<GetViewportFn>(GetViewport)(device_, &viewport)))
            shadow_.viewport = {true, viewport.X, viewport.Y, viewport.Width, viewport.Height, viewport.MinZ, viewport.MaxZ};
        else shadow_.viewport = {};
    } else if (index < 4) shadow_.extra_rt[index] = surface != nullptr;
}

// Full shadow resynchronization from public getters. Used at attach, after
// Reset, after EndStateBlock and after every state block Apply. Reserved
// constant ranges are conservatively treated as application-written.
void MotionOutput::resync_shadow() noexcept {
    shadow_ = Shadow{};
    IDirect3DVertexShader9* vs = nullptr; IDirect3DPixelShader9* ps = nullptr;
    if (SUCCEEDED(native<GetVsFn>(GetVertexShader)(device_, &vs))) set_vertex_shader(vs);
    release(vs);
    if (SUCCEEDED(native<GetPsFn>(GetPixelShader)(device_, &ps))) set_pixel_shader(ps);
    release(ps);
    shadow_.rows_known = SUCCEEDED(native<GetConstantsFFn>(GetVertexShaderConstantF)(device_, matrix_register, shadow_.rows, 4));
    shadow_.vs_reserved_written = SUCCEEDED(native<GetConstantsFFn>(GetVertexShaderConstantF)(device_, 252, shadow_.vs_reserved, 4));
    shadow_.integer0_known = SUCCEEDED(native<GetConstantsIFn>(GetVertexShaderConstantI)(device_, 0, shadow_.integer0, 1));
    shadow_.ps_reserved_written = SUCCEEDED(native<GetConstantsFFn>(GetPixelShaderConstantF)(device_, 216, shadow_.ps_reserved, 2));
    IDirect3DVertexBuffer9* stream = nullptr; UINT offset = 0, stride = 0;
    if (SUCCEEDED(native<GetStreamFn>(GetStreamSource)(device_, 0, &stream, &offset, &stride))) set_stream_source(0, stream, offset, stride);
    release(stream);
    IDirect3DIndexBuffer9* indices = nullptr;
    if (SUCCEEDED(native<GetIndicesFn>(GetIndices)(device_, &indices))) set_indices(indices);
    release(indices);
    IDirect3DVertexDeclaration9* declaration = nullptr;
    if (SUCCEEDED(native<GetDeclarationFn>(GetVertexDeclaration)(device_, &declaration))) set_vertex_declaration(declaration);
    release(declaration);
    IDirect3DSurface9* surface = nullptr;
    if (SUCCEEDED(native<GetRenderTargetFn>(GetRenderTarget)(device_, 0, &surface))) shadow_.rt0 = describe_surface(surface);
    release(surface);
    const unsigned targets = caps_.NumSimultaneousRTs < 4 ? unsigned(caps_.NumSimultaneousRTs) : 4;
    for (unsigned i = 1; i < targets; ++i) {
        const HRESULT hr = native<GetRenderTargetFn>(GetRenderTarget)(device_, i, &surface);
        shadow_.extra_rt[i] = surface || (FAILED(hr) && hr != D3DERR_NOTFOUND);
        release(surface);
    }
    const HRESULT depth_hr = native<GetDepthFn>(GetDepthStencilSurface)(device_, &surface);
    if (SUCCEEDED(depth_hr)) shadow_.depth = describe_surface(surface);
    else if (depth_hr == D3DERR_NOTFOUND && !surface) shadow_.depth.known = true;
    release(surface);
    D3DVIEWPORT9 viewport{};
    if (SUCCEEDED(native<GetViewportFn>(GetViewport)(device_, &viewport)))
        shadow_.viewport = {true, viewport.X, viewport.Y, viewport.Width, viewport.Height, viewport.MinZ, viewport.MaxZ};
}

// ---- scene selector --------------------------------------------------------

renderer::SceneSignatures MotionOutput::signatures() const noexcept {
    renderer::SceneSignatures result{};
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if (fixture_configured_)
        for (unsigned i = 0; i < 3; ++i) result.background[i] = {fixture_.background_vs[i], fixture_.background_ps[i]};
#endif
    return result;
}
renderer::Event MotionOutput::event(renderer::EventKind kind) noexcept {
    renderer::Event result{}; result.kind = kind; result.sequence = ++sequence_; return result;
}
void MotionOutput::bindings(renderer::Event& e) const noexcept {
    e.rt = shadow_.rt0; e.depth = shadow_.depth; e.viewport = shadow_.viewport;
    e.only_rt0 = !(shadow_.extra_rt[1] || shadow_.extra_rt[2] || shadow_.extra_rt[3]);
}
void MotionOutput::observe(renderer::Event& e, HRESULT result) noexcept {
    e.result_known = true; e.result = static_cast<std::uint32_t>(result);
    selector_.observe(e);
}
bool MotionOutput::scene_bound() const noexcept {
    const auto& v = shadow_.viewport;
    return selector_.state() == renderer::BoundaryState::Scene && same(shadow_.rt0, main_) &&
           same(shadow_.depth, main_depth_) && v.known && !v.x && !v.y && v.width == main_.width &&
           v.height == main_.height && v.min_z == 0 && v.max_z == 1 &&
           !(shadow_.extra_rt[1] || shadow_.extra_rt[2] || shadow_.extra_rt[3]);
}

void MotionOutput::begin_frame(std::uint64_t frame, bool capture) noexcept {
    frame_ = frame; capture_ = capture; telemetry_ = telemetry::enabled();
    counters_ = {};
    sequence_ = 0; pending_valid_ = false; fill_pending_ = false;
    if (!enabled_) return;
    selector_ = renderer::SceneBoundarySelector{signatures()};
    selector_.begin_frame(id_, generation_, frame + 1);
}

void MotionOutput::before_clear(DWORD count, DWORD flags, float z) noexcept {
    if (!enabled_) return;
    pending_ = event(renderer::EventKind::Clear);
    bindings(pending_);
    pending_.clear_flags = flags; pending_.rect_count = count; pending_.clear_z = z;
    pending_valid_ = true;
}
void MotionOutput::after_clear(HRESULT result) noexcept {
    if (!enabled_) return;
    if (!pending_valid_) { selector_.invalidate(); return; }
    pending_valid_ = false;
    const auto before = selector_.state();
    observe(pending_, result);
    if (before == renderer::BoundaryState::AwaitInitialClear && selector_.state() == renderer::BoundaryState::Background) {
        // The frame's main color/depth pair is latched: own a matching motion
        // target and schedule the sentinel fill for the next draw.
        main_ = pending_.rt; main_depth_ = pending_.depth;
        counters_.latched = true;
        if (ensure_target(main_.width, main_.height)) {
            fill_pending_ = true;
            history_.begin_frame({generation_, main_.width, main_.height});
        } else history_.invalidate();
    }
}
void MotionOutput::after_set_render_target(DWORD index, IDirect3DSurface9* surface, HRESULT result) noexcept {
    if (!enabled_) return;
    // Render-target bindings are not state-block state: they apply even while recording.
    if (SUCCEEDED(result)) describe_binding(index, surface);
    auto e = event(renderer::EventKind::SetRenderTarget);
    e.rt_index = index;
    e.rt = index == 0 ? shadow_.rt0 : describe_surface(surface);
    observe(e, result);
}
void MotionOutput::after_set_depth(IDirect3DSurface9* surface, HRESULT result) noexcept {
    if (!enabled_) return;
    if (SUCCEEDED(result)) shadow_.depth = describe_surface(surface);
    auto e = event(renderer::EventKind::SetDepth);
    e.depth = shadow_.depth;
    observe(e, result);
}
void MotionOutput::after_stretch(IDirect3DSurface9* source, const RECT* source_rect,
                                 IDirect3DSurface9* destination, const RECT* destination_rect, HRESULT result) noexcept {
    if (!enabled_) return;
    auto e = event(renderer::EventKind::Copy);
    if (SUCCEEDED(result)) { e.source = describe_surface(source); e.destination = describe_surface(destination); }
    e.source_rect_null = source_rect == nullptr; e.destination_rect_null = destination_rect == nullptr;
    observe(e, result);
}
void MotionOutput::after_color_fill(IDirect3DSurface9* destination, const RECT* rect, HRESULT result) noexcept {
    if (!enabled_) return;
    auto e = event(renderer::EventKind::ColorFill);
    if (SUCCEEDED(result)) e.destination = describe_surface(destination);
    e.destination_rect_null = rect == nullptr;
    observe(e, result);
}
void MotionOutput::unsupported(HRESULT result) noexcept {
    if (!enabled_) return;
    auto e = event(renderer::EventKind::Unsupported);
    observe(e, result);
}

// ---- object scope ----------------------------------------------------------

bool MotionOutput::sample_scope(MotionRoute& route) noexcept {
    auto& key = route.key;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if (fixture_configured_) {
        const auto& s = fixture_.scope;
        if (!s.known) return false;
        key.object_lifetime = s.node_serial; key.camera_lifetime = s.camera_serial;
        key.node = s.node; key.camera = s.camera; key.mesh = s.mesh;
        key.node_handle = s.node_handle; key.camera_handle = s.camera_handle; key.model = s.model; key.lod = s.lod;
        route.load_epoch = s.load_epoch; route.registry_epoch = s.registry_epoch;
        key.draw_domain = (((s.load_epoch & 0xffffffffull) << 32) | (s.registry_epoch & 0xffffffffull)) + 1;
        return true;
    }
#endif
    if (!history_available_) return false;
    object_trace::Snapshot scope{};
    if (!object_trace::current(&scope)) return false;
    constexpr std::uint32_t required = object_trace::Node | object_trace::Camera | object_trace::Registry;
    if ((scope.valid & required) != required || !scope.node || !scope.camera) return false;
    object_lifetime::Snapshot lifetime{};
    if (!object_lifetime::current(scope.registry, scope.node, scope.node_handle, scope.camera, scope.camera_handle, &lifetime) ||
        !lifetime.known || !lifetime.node_serial || !lifetime.camera_serial) return false;
    key.object_lifetime = lifetime.node_serial; key.camera_lifetime = lifetime.camera_serial;
    key.node = scope.node; key.camera = scope.camera; key.mesh = scope.mesh;
    key.node_handle = scope.node_handle; key.camera_handle = scope.camera_handle; key.model = scope.model; key.lod = scope.lod;
    route.load_epoch = lifetime.load_epoch; route.registry_epoch = lifetime.registry_epoch;
    key.draw_domain = (((lifetime.load_epoch & 0xffffffffull) << 32) | (lifetime.registry_epoch & 0xffffffffull)) + 1;
    return true;
}

// ---- per-draw route --------------------------------------------------------

// Undo whatever before_draw already applied, in reverse order.
void MotionOutput::undo(MotionRoute& route) noexcept {
    HRESULT first = S_OK;
    auto step = [&](HRESULT hr) { if (SUCCEEDED(first) && FAILED(hr)) first = hr; };
    if (route.write_set) step(native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE1, route.saved_write1));
    if (route.rt_set) step(native<SetRenderTargetFn>(SetRenderTarget)(device_, 1, nullptr));
    if (route.ps_set) step(native<SetPsFn>(SetPixelShader)(device_, shadow_.ps));
    if (route.vs_set) step(native<SetVsFn>(SetVertexShader)(device_, shadow_.vs));
    // Reserved ranges go back only if this draw changed them and the shadow
    // has seen the application write them; otherwise the application never
    // depends on their contents and the values are left as set.
    if (route.vs_constants_set && shadow_.vs_reserved_written)
        step(native<SetConstantsFFn>(SetVertexShaderConstantF)(device_, 252, shadow_.vs_reserved, 4));
    if (route.ps_constants_set && shadow_.ps_reserved_written)
        step(native<SetConstantsFFn>(SetPixelShaderConstantF)(device_, 216, shadow_.ps_reserved, 2));
    route.write_set = route.rt_set = route.ps_set = route.vs_set = false;
    route.vs_constants_set = route.ps_constants_set = false;
    if (FAILED(first)) {
        ++counters_.restore_failures;
        if (logged_failures_ < failure_log_limit) {
            ++logged_failures_;
            log("motion_output_restore_failed device=%llu frame=%llu index=%lu result=%08lx", id_, frame_, counters_.draws, first);
        }
    }
}

MotionRoute MotionOutput::before_draw(const MotionDrawCall& call) noexcept {
    MotionRoute route{};
    ++counters_.draws;
    if (!enabled_) { route.gate = MotionGate::Feature; ++counters_.gates[1]; return route; }
    // Selector event for this draw; z states are the only per-draw getters and
    // only while the selector can still use them.
    const auto state = selector_.state();
    const bool tracking = state == renderer::BoundaryState::Background || state == renderer::BoundaryState::Scene;
    pending_ = event(renderer::EventKind::Draw);
    DWORD z = 0, write = 0;
    HRESULT z_hr = E_FAIL, write_hr = E_FAIL;
    if (tracking) {
        bindings(pending_);
        pending_.topology = call.topology; pending_.primitives = call.primitives;
        pending_.vs = shadow_.vs_hash; pending_.ps = shadow_.ps_hash; pending_.texture0 = 0;
        z_hr = native<GetRenderStateFn>(GetRenderState)(device_, D3DRS_ZENABLE, &z);
        write_hr = native<GetRenderStateFn>(GetRenderState)(device_, D3DRS_ZWRITEENABLE, &write);
        pending_.z_enable = z; pending_.z_write = write;
        pending_.draw_state_known = SUCCEEDED(z_hr) && SUCCEEDED(write_hr) && pending_.vs && pending_.ps &&
                                    shadow_.rt0.known && shadow_.depth.known && shadow_.viewport.known;
    }
    pending_valid_ = true;
    if (fill_pending_) fill_sentinel();
    // Gate 1: feature/capability, target owned, not recording a state block.
    if (!target_surface_ || shadow_.recording) { route.gate = MotionGate::Feature; ++counters_.gates[1]; return route; }
    // Gate 2: scene phase with the latched main color/depth bound.
    if (!scene_bound()) { route.gate = MotionGate::Scene; ++counters_.gates[2]; return route; }
    route.scene = true;
    // Gate 3: exact reviewed pair (one profile-table row) with both variants
    // registered. Variants are per program; the pair check is what keys
    // eligibility, so a VS alias shared with an unreviewed PS never routes.
    if (!shadow_.vs_variant || !shadow_.ps_variant ||
        !renderer::material_motion_pair_reviewed(shadow_.vs_hash, shadow_.ps_hash)) {
        route.gate = MotionGate::Pair; ++counters_.gates[3]; return route;
    }
    // Gate 4: opaque ordinary draw state, known rows, bounded light loop, no
    // user-memory geometry, no instancing, known declaration/stream identity.
    DWORD blend = 1, test = 1, srgb = 1, color = 0; UINT frequency = 0;
    const bool draw_state_ok = !call.user_memory && SUCCEEDED(z_hr) && SUCCEEDED(write_hr) && z == 1 && write == 1 &&
        SUCCEEDED(native<GetRenderStateFn>(GetRenderState)(device_, D3DRS_ALPHABLENDENABLE, &blend)) && !blend &&
        SUCCEEDED(native<GetRenderStateFn>(GetRenderState)(device_, D3DRS_ALPHATESTENABLE, &test)) && !test &&
        SUCCEEDED(native<GetRenderStateFn>(GetRenderState)(device_, D3DRS_SRGBWRITEENABLE, &srgb)) && !srgb &&
        SUCCEEDED(native<GetRenderStateFn>(GetRenderState)(device_, D3DRS_COLORWRITEENABLE, &color)) && color == 15 &&
        SUCCEEDED(native<GetStreamFreqFn>(GetStreamSourceFreq)(device_, 0, &frequency)) &&
        !(frequency & D3DSTREAMSOURCE_INDEXEDDATA) && (frequency & 0x3fffffffu) <= 1 &&
        shadow_.rows_known && shadow_.integer0_known && shadow_.integer0[0] >= 0 && shadow_.integer0[0] <= light_loop_max_count &&
        shadow_.stream0 && shadow_.stream0_stride && shadow_.declaration && call.primitives &&
        (!call.indexed || shadow_.indices);
    if (!draw_state_ok) { route.gate = MotionGate::DrawState; ++counters_.gates[4]; return route; }
    // Key geometry fields come from the shadowed bindings and draw arguments.
    auto& key = route.key;
    key.vertex_buffer = shadow_.stream0; key.stream_offset = shadow_.stream0_offset; key.stride = shadow_.stream0_stride;
    key.index_buffer = call.indexed ? shadow_.indices : 0; key.declaration = shadow_.declaration;
    key.position_program = shadow_.vs_hash; key.position_offset = shadow_.position_offset; key.position_type = shadow_.position_type;
    key.topology = call.topology; key.first = call.first; key.primitives = call.primitives;
    key.base_vertex = call.base_vertex; key.min_vertex = call.min_vertex; key.vertex_count = call.vertex_count;
    key.indexed = call.indexed;
    renderer::SubmittedMatrix rows{}, previous{};
    std::memcpy(rows.data(), shadow_.rows, sizeof shadow_.rows);
    if (capture_) route.rows_hash = hash_bytes(rows.data(), sizeof rows); // Diagnostics only.
    // Gate 5: verified object/camera scope. Failure still routes with mode 0 so
    // covered pixels of this material carry the sentinel, never stale history.
    bool matched = false;
    if (!sample_scope(route)) { route.gate = MotionGate::Scope; ++counters_.gates[5]; }
    else if (!history_.lookup_and_record(key, rows, previous)) { route.gate = MotionGate::History; ++counters_.gates[6]; }
    else { matched = true; route.gate = MotionGate::None; ++counters_.gates[0]; }
    // Apply: variant pair, previous rows (or zeros), pixel ABI, RT1 and its write mask.
    static const float zeros[16]{};
    const float pixel[8] = {1.f / float(target_width_), 1.f / float(target_height_), 0.f, 0.f,
                            matched ? 1.f : 0.f, 0.f, 0.f, 0.f};
    HRESULT hr = native<GetRenderStateFn>(GetRenderState)(device_, D3DRS_COLORWRITEENABLE1, &route.saved_write1);
    if (SUCCEEDED(hr)) { hr = native<SetVsFn>(SetVertexShader)(device_, shadow_.vs_variant); route.vs_set = SUCCEEDED(hr); }
    if (SUCCEEDED(hr)) { hr = native<SetPsFn>(SetPixelShader)(device_, shadow_.ps_variant); route.ps_set = SUCCEEDED(hr); }
    if (SUCCEEDED(hr)) {
        hr = native<SetConstantsFFn>(SetVertexShaderConstantF)(device_,
            renderer::MaterialMotionAbi::previous_vertex_constant, matched ? previous.data() : zeros, 4);
        route.vs_constants_set = SUCCEEDED(hr);
    }
    if (SUCCEEDED(hr)) {
        hr = native<SetConstantsFFn>(SetPixelShaderConstantF)(device_,
            renderer::MaterialMotionAbi::pixel_coordinates_constant, pixel, 2);
        route.ps_constants_set = SUCCEEDED(hr);
    }
    if (SUCCEEDED(hr)) { hr = native<SetRenderTargetFn>(SetRenderTarget)(device_, 1, target_surface_); route.rt_set = SUCCEEDED(hr); }
    if (SUCCEEDED(hr)) { hr = native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE1, 15); route.write_set = SUCCEEDED(hr); }
    if (FAILED(hr)) {
        // Partial application: put back what was set and draw the original.
        undo(route);
        ++counters_.apply_failures;
        if (logged_failures_ < failure_log_limit) {
            ++logged_failures_;
            log("motion_output_apply_failed device=%llu frame=%llu index=%lu result=%08lx", id_, frame_, counters_.draws, hr);
        }
        return route;
    }
    route.routed = true; route.matched = matched;
    ++counters_.routed; if (matched) ++counters_.matched;
    return route;
}

void MotionOutput::after_draw(MotionRoute& route, HRESULT result) noexcept {
    if (!enabled_) return;
    if (route.routed) undo(route);
    if (pending_valid_) { pending_valid_ = false; observe(pending_, result); }
    if (capture_ && route.scene) {
        const auto& k = route.key;
        log("motion_route device=%llu frame=%llu index=%lu gate=%u routed=%u matched=%u vs=%016llx ps=%016llx node=%p camera=%p node_handle=%lu camera_handle=%lu node_serial=%llu camera_serial=%llu load_epoch=%llu registry_epoch=%llu model=%08lx lod=%08lx vb=%llu ib=%llu declaration=%016llx offset=%u stride=%u position_offset=%u position_type=%u topology=%u first=%u primitives=%u base_vertex=%d min_vertex=%u vertex_count=%u indexed=%u rows_hash=%016llx result=%08lx",
            id_, frame_, counters_.draws, unsigned(route.gate), route.routed, route.matched, shadow_.vs_hash, shadow_.ps_hash,
            reinterpret_cast<void*>(k.node), reinterpret_cast<void*>(k.camera), static_cast<unsigned long>(k.node_handle),
            static_cast<unsigned long>(k.camera_handle), k.object_lifetime, k.camera_lifetime, route.load_epoch, route.registry_epoch,
            static_cast<unsigned long>(k.model), static_cast<unsigned long>(k.lod), k.vertex_buffer, k.index_buffer, k.declaration,
            k.stream_offset, k.stride, k.position_offset, k.position_type, k.topology, k.first, k.primitives, k.base_vertex,
            k.min_vertex, k.vertex_count, k.indexed, route.rows_hash, result);
    }
}

// ---- frame end -------------------------------------------------------------

void MotionOutput::readback() noexcept {
    if (!target_surface_ || !counters_.filled) return;
    IDirect3DSurface9* copy = nullptr;
    HRESULT hr = native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, target_width_, target_height_,
        D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &copy, nullptr);
    if (SUCCEEDED(hr)) hr = native<GetRtDataFn>(GetRenderTargetData)(device_, target_surface_, copy);
    std::size_t written = 0;
    // Fixed buffers: this runs inside a noexcept hook path, so no std::wstring.
    wchar_t name[96]{}, path[MAX_PATH + 96]{};
    const wchar_t* dir = capture_directory();
    const std::size_t dir_length = std::wcslen(dir);
    swprintf(name, 96, L"\\motion_%llu_%llu.rgba32f", static_cast<unsigned long long>(id_), static_cast<unsigned long long>(frame_));
    if (dir_length + std::wcslen(name) + 1 > sizeof path / sizeof path[0]) hr = E_FAIL;
    else { std::wmemcpy(path, dir, dir_length); std::wcscpy(path + dir_length, name); }
    if (SUCCEEDED(hr)) {
        D3DLOCKED_RECT lock{};
        hr = copy->LockRect(&lock, nullptr, D3DLOCK_READONLY);
        if (SUCCEEDED(hr)) {
            FILE* file = _wfopen(path, L"wb");
            if (file) {
                for (UINT y = 0; y < target_height_; ++y)
                    written += std::fwrite(static_cast<const char*>(lock.pBits) + y * lock.Pitch, 1, std::size_t(target_width_) * 16, file);
                if (std::fclose(file)) hr = E_FAIL;
            } else hr = E_FAIL;
            copy->UnlockRect();
        }
    }
    release(copy);
    log("motion_output_readback device=%llu frame=%llu file=motion_%llu_%llu.rgba32f width=%u height=%u format=rgba32f_row_major result=%08lx bytes=%u",
        id_, frame_, id_, frame_, target_width_, target_height_, hr, unsigned(written));
}

void MotionOutput::before_present() noexcept {
    if (enabled_ && capture_) readback();
}
void MotionOutput::after_present(HRESULT result) noexcept {
    if (!enabled_) return;
    const bool committed = history_.commit(SUCCEEDED(result) && counters_.filled);
    const auto stats = history_.stats();
    if (capture_ || (telemetry_ && frame_ % 60 == 0))
        log("motion_output_frame device=%llu frame=%llu latched=%u filled=%u fill_result=%08lx fill_restore=%08lx draws=%lu routed=%lu matched=%lu gate1=%lu gate2=%lu gate3=%lu gate4=%lu gate5=%lu gate6=%lu apply_failures=%lu restore_failures=%lu history_previous=%u history_current=%u committed=%u selector_state=%u present=%08lx",
            id_, frame_, counters_.latched, counters_.filled, counters_.fill_result, counters_.fill_restore,
            static_cast<unsigned long>(counters_.draws), static_cast<unsigned long>(counters_.routed), static_cast<unsigned long>(counters_.matched),
            static_cast<unsigned long>(counters_.gates[1]), static_cast<unsigned long>(counters_.gates[2]), static_cast<unsigned long>(counters_.gates[3]),
            static_cast<unsigned long>(counters_.gates[4]), static_cast<unsigned long>(counters_.gates[5]), static_cast<unsigned long>(counters_.gates[6]),
            static_cast<unsigned long>(counters_.apply_failures), static_cast<unsigned long>(counters_.restore_failures),
            unsigned(stats.previous), unsigned(stats.current), committed, unsigned(selector_.state()), result);
}

#ifdef X3M_MOTION_OUTPUT_FIXTURE
void MotionOutput::fixture_configure(const MotionOutputFixtureConfig& config) noexcept {
    fixture_ = config; fixture_configured_ = true;
}
HRESULT MotionOutput::fixture_readback(float* out, std::size_t floats, UINT* width, UINT* height) noexcept {
    if (width) *width = target_width_;
    if (height) *height = target_height_;
    if (!target_surface_) return D3DERR_NOTFOUND;
    if (!out || floats < std::size_t(target_width_) * target_height_ * 4) return D3DERR_MOREDATA;
    IDirect3DSurface9* copy = nullptr;
    HRESULT hr = native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, target_width_, target_height_,
        D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &copy, nullptr);
    if (SUCCEEDED(hr)) hr = native<GetRtDataFn>(GetRenderTargetData)(device_, target_surface_, copy);
    D3DLOCKED_RECT lock{};
    if (SUCCEEDED(hr)) hr = copy->LockRect(&lock, nullptr, D3DLOCK_READONLY);
    if (SUCCEEDED(hr)) {
        for (UINT y = 0; y < target_height_; ++y)
            std::memcpy(out + std::size_t(y) * target_width_ * 4, static_cast<const char*>(lock.pBits) + y * lock.Pitch, std::size_t(target_width_) * 16);
        copy->UnlockRect();
    }
    release(copy);
    return hr;
}
#endif
} // namespace x3m
