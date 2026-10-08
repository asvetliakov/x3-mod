#include "occlusion_cull_pass.h"
#include <cstring>

namespace x3m::renderer {
namespace {
namespace core = occlusion_cull::core;
template <class T> void drop(T*& value) noexcept {
    if (value) {
        T* old = value;
        value = nullptr; // before the call: a final Release re-enters the hooked device Release
        old->Release();
    }
}
// IDirect3DDevice9 vtable slots (d3d9.h order; verification/probe/abi_check.cpp).
// clang-format off
enum Slot : unsigned {
    GetDirect3D = 6, GetDeviceCaps = 7, GetDisplayMode = 8, GetCreationParameters = 9, CreateVertexBuffer = 26,
    GetRenderTarget = 38, SetRenderState = 57, DrawPrimitive = 81, CreateVertexDeclaration = 86, SetVertexDeclaration = 87,
    GetVertexDeclaration = 88, SetFVF = 89, GetFVF = 90, CreateVertexShader = 91, SetVertexShader = 92,
    GetVertexShader = 93, SetVertexShaderConstantF = 94, SetStreamSource = 100, GetStreamSource = 101,
    GetStreamSourceFreq = 103, CreatePixelShader = 106, SetPixelShader = 107, GetPixelShader = 108, CreateQuery = 118
};
// clang-format on
using D = IDirect3DDevice9*;
using GetD3DFn = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetCapsFn = HRESULT(WINAPI*)(D, D3DCAPS9*);
using GetModeFn = HRESULT(WINAPI*)(D, UINT, D3DDISPLAYMODE*);
using GetCreationFn = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);
using CreateVbFn = HRESULT(WINAPI*)(D, UINT, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer9**, HANDLE*);
using GetRtFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DSurface9**);
using SetRsFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD);
using DrawFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, UINT, UINT);
using CreateDeclFn = HRESULT(WINAPI*)(D, const D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9**);
using SetDeclFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9*);
using GetDeclFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9**);
using SetFvfFn = HRESULT(WINAPI*)(D, DWORD);
using GetFvfFn = HRESULT(WINAPI*)(D, DWORD*);
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using GetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9**);
using SetConstFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
using SetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
using GetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9**, UINT*, UINT*);
using GetFreqFn = HRESULT(WINAPI*)(D, UINT, UINT*);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using GetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9**);
using CreateQueryFn = HRESULT(WINAPI*)(D, D3DQUERYTYPE, IDirect3DQuery9**);
// vs_3_0: dcl_position v0; dcl_position o0; mad o0, v0, c253, c252 (hand-encoded; the strip's (0|1, 0|1) corners
// become the rectangle c252.xy + v0.xy * c253.xy at depth c252.z, w = c252.w = 1).
constexpr DWORD rect_vs_words[] = {0xfffe0300u, 0x0200001fu, 0x80000000u, 0x900f0000u, 0x0200001fu,
                                   0x80000000u, 0xe00f0000u, 0x04000004u, 0xe00f0000u, 0x90e40000u,
                                   0xa0e400fdu, 0xa0e400fcu, 0x0000ffffu};
// ps_3_0: def c0, 0, 0, 0, 0; mov oC0..oC(n-1), c0, n = the device's simultaneous targets (at most 4). Every output a
// bound target can receive is written: under the ZERO/ONE blend each keeps its value (an unwritten output is undefined
// in D3D9, and 0 x NaN = NaN). The motion route's lazy mode keeps its RT1 (A32B32G32R32F) and RT2 (R32F) bound between
// routed draws; measured over 60 frames (gate.json "mrt"): this costs 1.3 us (DXVK) / 17 us (wined3d) of pipeline per
// test and leaves RT1/RT2 byte-identical, against 64-67 us for masking or unbinding them (render-pass splits).
constexpr DWORD rect_ps_head[] = {0xffff0300u, 0x05000051u, 0xa00f0000u, 0x00000000u,
                                  0x00000000u, 0x00000000u, 0x00000000u};
constexpr D3DVERTEXELEMENT9 rect_elements[] = {
    {0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
// The unit square twice in opposite windings: D3DCULL_CCW culls vertices 0-3 and D3DCULL_CW culls 4-7 (both
// backends, measured by occlusion_cull_fixture's cull-mode cycle); the test picks the winding the application's cull
// mode keeps.
constexpr float strip_vertices[16] = {0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 1, 1, 0, 1};
}

void OcclusionCullFrameStats::add(const OcclusionCullFrameStats& o) noexcept {
    candidates += o.candidates;
    tested += o.tested;
    skipped += o.skipped;
    hidden += o.hidden;
    visible += o.visible;
    not_ready += o.not_ready;
    errors += o.errors;
    ready_lag2 += o.ready_lag2;
    age1 += o.age1;
    age2 += o.age2;
    age_none += o.age_none;
    drawn_late += o.drawn_late;
    pool_truncated += o.pool_truncated;
    refused += o.refused;
    failed += o.failed;
    unstable += o.unstable;
    no_hull += o.no_hull;
}

OcclusionCullPass::~OcclusionCullPass() {
    detach();
}
HRESULT OcclusionCullPass::attach(IDirect3DDevice9* device, void* const* native) noexcept {
    detach();
    if (!device || !native) {
        reason_ = "no_device";
        return E_INVALIDARG;
    }
    device_ = device;
    native_ = native;
    const auto create_query = reinterpret_cast<CreateQueryFn>(native_[CreateQuery]);
    // Documented capability probe: a null out-pointer returns S_OK or D3DERR_NOTAVAILABLE without creating anything.
    HRESULT hr = create_query(device_, D3DQUERYTYPE_OCCLUSION, nullptr);
    if (hr != S_OK) {
        reason_ = "unsupported";
        device_ = nullptr;
        native_ = nullptr;
        return FAILED(hr) ? hr : D3DERR_NOTAVAILABLE;
    }
    D3DCAPS9 caps{};
    hr = reinterpret_cast<GetCapsFn>(native_[GetDeviceCaps])(device_, &caps);
    targets_ = SUCCEEDED(hr) && caps.NumSimultaneousRTs ? (caps.NumSimultaneousRTs < 4 ? unsigned(caps.NumSimultaneousRTs) : 4u) : 1u;
    mrt_blending_ = SUCCEEDED(hr) && (caps.PrimitiveMiscCaps & D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING);
    DWORD ps_words[sizeof rect_ps_head / sizeof rect_ps_head[0] + 3 * 4 + 1];
    unsigned n = 0;
    for (DWORD w : rect_ps_head) ps_words[n++] = w;
    for (unsigned t = 0; t < targets_; ++t) {
        ps_words[n++] = 0x02000001u;        // mov
        ps_words[n++] = 0x800f0800u | t;   // oCt.xyzw
        ps_words[n++] = 0xa0e40000u;        // c0
    }
    ps_words[n++] = 0x0000ffffu;
    hr = reinterpret_cast<CreateVsFn>(native_[CreateVertexShader])(device_, rect_vs_words, &vs_);
    if (SUCCEEDED(hr)) hr = reinterpret_cast<CreatePsFn>(native_[CreatePixelShader])(device_, ps_words, &ps_);
    if (SUCCEEDED(hr))
        hr = reinterpret_cast<CreateDeclFn>(native_[CreateVertexDeclaration])(device_, rect_elements, &declaration_);
    if (SUCCEEDED(hr))
        hr = reinterpret_cast<CreateVbFn>(native_[CreateVertexBuffer])(device_, UINT(sizeof strip_vertices),
                                                                        D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED,
                                                                        &strip_, nullptr);
    if (SUCCEEDED(hr)) {
        void* p = nullptr;
        hr = strip_->Lock(0, 0, &p, 0);
        if (SUCCEEDED(hr)) {
            std::memcpy(p, strip_vertices, sizeof strip_vertices);
            hr = strip_->Unlock();
        }
    }
    if (SUCCEEDED(hr)) hr = create_queries();
    if (FAILED(hr) || !vs_ || !ps_ || !declaration_ || !strip_) {
        const HRESULT result = FAILED(hr) ? hr : E_POINTER;
        detach();
        reason_ = "create_failed";
        return result;
    }
    ring_.clear();
    available_ = true;
    reason_ = "ok";
    return S_OK;
}
HRESULT OcclusionCullPass::create_queries() noexcept {
    const auto create_query = reinterpret_cast<CreateQueryFn>(native_[CreateQuery]);
    for (IDirect3DQuery9*& q : queries_) {
        const HRESULT hr = create_query(device_, D3DQUERYTYPE_OCCLUSION, &q);
        if (FAILED(hr) || !q) {
            release_queries(); // partial creation rolled back
            return FAILED(hr) ? hr : E_POINTER;
        }
    }
    return S_OK;
}
void OcclusionCullPass::release_queries() noexcept {
    for (IDirect3DQuery9*& q : queries_) drop(q);
}
unsigned OcclusionCullPass::queries() const noexcept {
    unsigned n = 0;
    for (IDirect3DQuery9* q : queries_) n += q ? 1u : 0u;
    return n;
}
void OcclusionCullPass::detach() noexcept {
    available_ = false;
    reset_pending_ = recreate_pending_ = false;
    release_queries();
    drop(strip_);
    drop(declaration_);
    drop(ps_);
    drop(vs_);
    ring_.clear();
    for (auto& f : formats_) f = FormatVerdict{};
    device_ = nullptr;
    native_ = nullptr;
    reason_ = "detached";
}
void OcclusionCullPass::before_reset() noexcept {
    if (!device_) return;
    release_queries();
    ring_.clear();
    available_ = false;
    reset_pending_ = true;
    recreate_pending_ = false;
    reason_ = "reset_pending";
}
void OcclusionCullPass::after_reset(HRESULT reset) noexcept {
    if (!device_ || !reset_pending_) return;
    if (FAILED(reset)) {
        reason_ = "reset_failed"; // still released; the next successful Reset arms the recreation
        return;
    }
    reset_pending_ = false;
    recreate_pending_ = true;
    reason_ = "recreate_pending";
}
HRESULT OcclusionCullPass::recreate() noexcept {
    if (!device_ || !recreate_pending_) return S_FALSE;
    recreate_pending_ = false;
    for (auto& f : formats_) f = FormatVerdict{}; // a mode change may change what the device blends
    const HRESULT hr = create_queries();
    if (FAILED(hr)) {
        reason_ = "create_failed";
        return hr;
    }
    ring_.clear(); // the first frame after a Reset has no previous result: everything draws
    available_ = true;
    reason_ = "ok";
    return S_OK;
}
void OcclusionCullPass::begin_frame(std::uint32_t frame) noexcept {
    ring_.begin(frame);
}
// The two older frames' queries, without a flush, before this frame takes its slot: frame - 2's records whose result
// was not ready at the last read are polled again (its ready ones are re-entered as age-2 results), then frame - 1's.
// Ready and zero samples = hidden, ready and any sample = visible (a skipped part read visible is drawn late), not
// ready or an error = no ready result from that frame.
void OcclusionCullPass::read_previous() noexcept {
    for (unsigned lag = 2; lag >= 1; --lag) {
        const unsigned count = ring_.lag_count(lag);
        const unsigned base = ring_.lag_slot(lag) * core::pool_per_frame;
        for (unsigned i = 0; i < count; ++i) {
            core::Ring::Record& rec = ring_.lag_record(lag, i);
            if (!rec.issued) continue;
            if (lag == 2 && rec.result != core::Result::not_ready) {
                if (core::ready(rec.result)) ring_.ingest(2, i, rec.result); // read ready at frame - 1
                continue;
            }
            DWORD samples = 0;
            const HRESULT hr = queries_[base + i]->GetData(&samples, sizeof samples, 0);
            core::Result r;
            if (hr == S_OK) {
                r = samples ? core::Result::visible : core::Result::hidden;
                ++(samples ? frame_.visible : frame_.hidden);
                if (lag == 2) ++frame_.ready_lag2;
                if (samples && rec.skipped) ++frame_.drawn_late;
            } else if (hr == S_FALSE) {
                r = core::Result::not_ready;
                if (lag == 1) ++frame_.not_ready;
            } else {
                r = core::Result::error;
                ++frame_.errors;
            }
            ring_.ingest(lag, i, r);
        }
    }
    ring_.start();
}
bool OcclusionCullPass::blendable(D3DFORMAT format) noexcept {
    for (const auto& f : formats_)
        if (f.known && f.format == format) return f.blendable;
    bool ok = false;
    IDirect3D9* d3d = nullptr;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    D3DDISPLAYMODE mode{};
    if (SUCCEEDED(reinterpret_cast<GetD3DFn>(native_[GetDirect3D])(device_, &d3d)) && d3d &&
        SUCCEEDED(reinterpret_cast<GetCreationFn>(native_[GetCreationParameters])(device_, &creation)) &&
        SUCCEEDED(reinterpret_cast<GetModeFn>(native_[GetDisplayMode])(device_, 0, &mode)))
        ok = d3d->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, mode.Format,
                                    D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_TEXTURE,
                                    format) == D3D_OK;
    drop(d3d);
    formats_[format_next_] = FormatVerdict{format, true, ok};
    format_next_ = (format_next_ + 1) % (sizeof formats_ / sizeof formats_[0]);
    return ok;
}
// The targets bound now (the device's, not anyone's shadow: under the HDR redirect RT0 is the FP16 scene target and
// the route's lazy RT1/RT2 may be bound): every one must blend, and more than one needs MRT blending.
bool OcclusionCullPass::targets_blend() noexcept {
    const auto get_rt = reinterpret_cast<GetRtFn>(native_[GetRenderTarget]);
    unsigned bound = 0;
    bool ok = true;
    for (unsigned t = 0; t < targets_ && ok; ++t) {
        IDirect3DSurface9* surface = nullptr;
        const HRESULT hr = get_rt(device_, t, &surface);
        if (FAILED(hr) || !surface) {
            ok = t != 0 && hr == D3DERR_NOTFOUND; // an unbound extra target; RT0 must exist
            drop(surface);
            continue;
        }
        D3DSURFACE_DESC desc{};
        ok = SUCCEEDED(surface->GetDesc(&desc)) && blendable(desc.Format);
        drop(surface);
        ++bound;
    }
    return ok && bound >= 1 && (bound == 1 || mrt_blending_);
}
OcclusionCullVerdict OcclusionCullPass::candidate(std::uint64_t key, const core::Rect& rect, bool hull_drawn,
                                                  const OcclusionCullState& state, HRESULT* restore) noexcept {
    *restore = S_OK;
    ++frame_.candidates;
    if (!available_) {
        ++frame_.refused;
        return OcclusionCullVerdict::refused;
    }
    if (!ring_.previous_read) read_previous();
    if (!targets_blend()) {
        ++frame_.refused;
        return OcclusionCullVerdict::refused;
    }
    const core::Ring::ResultSlot* previous = ring_.lookup(key);
    const bool ready_previous = previous && core::ready(previous->result);
    if (!ready_previous) ++frame_.age_none;
    else if (previous->age == 1) ++frame_.age1;
    else ++frame_.age2;
    core::Ring::Record* rec = ring_.reserve(key, rect);
    if (!rec) {
        ++frame_.pool_truncated;
        return OcclusionCullVerdict::truncated;
    }
    const auto set_rs = reinterpret_cast<SetRsFn>(native_[SetRenderState]);
    // What the test rebinds, read from the device (native getters: no hook sees them).
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexDeclaration9* decl = nullptr;
    IDirect3DVertexBuffer9* stream = nullptr;
    UINT offset = 0, stride = 0, frequency = 1;
    DWORD fvf = 0;
    HRESULT hr = reinterpret_cast<GetFreqFn>(native_[GetStreamSourceFreq])(device_, 0, &frequency);
    if (SUCCEEDED(hr) && frequency != 1) {
        ++frame_.refused; // instanced: the rectangle would draw as instances
        return OcclusionCullVerdict::refused;
    }
    if (SUCCEEDED(hr)) hr = reinterpret_cast<GetVsFn>(native_[GetVertexShader])(device_, &vs);
    if (SUCCEEDED(hr)) hr = reinterpret_cast<GetPsFn>(native_[GetPixelShader])(device_, &ps);
    if (SUCCEEDED(hr)) hr = reinterpret_cast<GetFvfFn>(native_[GetFVF])(device_, &fvf);
    if (SUCCEEDED(hr)) hr = reinterpret_cast<GetDeclFn>(native_[GetVertexDeclaration])(device_, &decl);
    if (SUCCEEDED(hr))
        hr = reinterpret_cast<GetStreamFn>(native_[GetStreamSource])(device_, 0, &stream, &offset, &stride);
    if (FAILED(hr)) {
        drop(stream);
        drop(decl);
        drop(ps);
        drop(vs);
        ++frame_.failed;
        return OcclusionCullVerdict::failed; // nothing changed yet
    }
    // Set: every change is recorded so the restore puts back exactly what was changed, even after a failure.
    HRESULT op = S_OK;
    auto step = [&op](HRESULT r) {
        if (SUCCEEDED(op) && FAILED(r)) op = r;
    };
    bool set_z = false, set_test = false, set_stencil = false, set_op = false, set_separate = false;
    step(reinterpret_cast<SetDeclFn>(native_[SetVertexDeclaration])(device_, declaration_));
    step(reinterpret_cast<SetStreamFn>(native_[SetStreamSource])(device_, 0, strip_, 0, 8));
    step(reinterpret_cast<SetVsFn>(native_[SetVertexShader])(device_, vs_));
    step(reinterpret_cast<SetPsFn>(native_[SetPixelShader])(device_, ps_));
    float constants[8];
    std::memcpy(constants, rect.c252, 16);
    std::memcpy(constants + 4, rect.c253, 16);
    step(reinterpret_cast<SetConstFn>(native_[SetVertexShaderConstantF])(device_, 252, constants, 2));
    if (state.z_write != FALSE) set_z = true, step(set_rs(device_, D3DRS_ZWRITEENABLE, FALSE));
    if (state.alpha_test != FALSE) set_test = true, step(set_rs(device_, D3DRS_ALPHATESTENABLE, FALSE));
    if (state.stencil != FALSE) set_stencil = true, step(set_rs(device_, D3DRS_STENCILENABLE, FALSE));
    if (state.separate_alpha != FALSE) set_separate = true, step(set_rs(device_, D3DRS_SEPARATEALPHABLENDENABLE, FALSE));
    if (state.blend_op != D3DBLENDOP_ADD) set_op = true, step(set_rs(device_, D3DRS_BLENDOP, D3DBLENDOP_ADD));
    step(set_rs(device_, D3DRS_ALPHABLENDENABLE, TRUE));
    step(set_rs(device_, D3DRS_SRCBLEND, D3DBLEND_ZERO));
    step(set_rs(device_, D3DRS_DESTBLEND, D3DBLEND_ONE));
    IDirect3DQuery9* const query = queries_[(ring_.frame & 1) * core::pool_per_frame + ring_.index_of(rec)];
    if (SUCCEEDED(op)) {
        step(query->Issue(D3DISSUE_BEGIN));
        if (SUCCEEDED(op)) {
            const UINT first = state.cull == D3DCULL_CCW ? 4u : 0u; // the winding the cull mode keeps
            const HRESULT draw = reinterpret_cast<DrawFn>(native_[DrawPrimitive])(device_, D3DPT_TRIANGLESTRIP, first, 2);
            const HRESULT end = query->Issue(D3DISSUE_END); // closes the query even after a failed draw
            step(draw);
            step(end);
        }
    }
    // Restore, in reverse.
    HRESULT back = S_OK;
    auto put = [&back](HRESULT r) {
        if (SUCCEEDED(back) && FAILED(r)) back = r;
    };
    put(set_rs(device_, D3DRS_DESTBLEND, state.dest_blend));
    put(set_rs(device_, D3DRS_SRCBLEND, state.src_blend));
    put(set_rs(device_, D3DRS_ALPHABLENDENABLE, state.alpha_blend));
    if (set_op) put(set_rs(device_, D3DRS_BLENDOP, state.blend_op));
    if (set_separate) put(set_rs(device_, D3DRS_SEPARATEALPHABLENDENABLE, state.separate_alpha));
    if (set_stencil) put(set_rs(device_, D3DRS_STENCILENABLE, state.stencil));
    if (set_test) put(set_rs(device_, D3DRS_ALPHATESTENABLE, state.alpha_test));
    if (set_z) put(set_rs(device_, D3DRS_ZWRITEENABLE, state.z_write));
    if (state.reserved)
        put(reinterpret_cast<SetConstFn>(native_[SetVertexShaderConstantF])(device_, 252, state.reserved, 2));
    put(reinterpret_cast<SetPsFn>(native_[SetPixelShader])(device_, ps));
    put(reinterpret_cast<SetVsFn>(native_[SetVertexShader])(device_, vs));
    put(reinterpret_cast<SetStreamFn>(native_[SetStreamSource])(device_, 0, stream, offset, stride));
    // The declaration first, then the FVF when the application's binding was one (SetFVF re-establishes its own
    // declaration over it); a null declaration (none was ever bound) is not set.
    if (decl) put(reinterpret_cast<SetDeclFn>(native_[SetVertexDeclaration])(device_, decl));
    if (fvf) put(reinterpret_cast<SetFvfFn>(native_[SetFVF])(device_, fvf));
    drop(stream);
    drop(decl);
    drop(ps);
    drop(vs);
    *restore = back;
    if (FAILED(op) || FAILED(back)) {
        ++frame_.failed;
        return OcclusionCullVerdict::failed; // the record stays un-issued: no result next frame, drawn
    }
    rec->issued = true;
    ++frame_.tested;
    if (!hull_drawn) ++frame_.no_hull;
    const bool skip = core::may_skip(ready_previous ? previous : nullptr, rect, hull_drawn);
    if (ready_previous && previous->result == core::Result::hidden && hull_drawn && !skip) ++frame_.unstable;
    rec->skipped = skip;
    if (skip) ++frame_.skipped;
    return skip ? OcclusionCullVerdict::skip : OcclusionCullVerdict::draw;
}
}
