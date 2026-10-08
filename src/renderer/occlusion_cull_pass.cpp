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
// vs_3_0: dcl_position v0; dcl_position o0; mov o0, v0 (hand-encoded; the block's rectangles are written in clip
// space, w = 1, four strip vertices per test).
constexpr DWORD rect_vs_words[] = {0xfffe0300u, 0x0200001fu, 0x80000000u, 0x900f0000u, 0x0200001fu, 0x80000000u,
                                   0xe00f0000u, 0x02000001u, 0xe00f0000u, 0x90e40000u, 0x0000ffffu};
// ps_3_0: def c0, 0, 0, 0, 0; mov oC0..oC(n-1), c0, n = the device's simultaneous targets (at most 4). Every output a
// bound target can receive is written: under the ZERO/ONE blend each keeps its value (an unwritten output is undefined
// in D3D9, and 0 x NaN = NaN). The motion route's lazy mode keeps its RT1 (A32B32G32R32F) and RT2 (R32F) bound between
// routed draws; measured over 60 frames (gate.json "mrt"): this costs 1.3 us (DXVK) / 17 us (wined3d) of pipeline per
// test and leaves RT1/RT2 byte-identical, against 64-67 us for masking or unbinding them (render-pass splits).
constexpr DWORD rect_ps_head[] = {0xffff0300u, 0x05000051u, 0xa00f0000u, 0x00000000u,
                                  0x00000000u, 0x00000000u, 0x00000000u};
// The constants mode (no buffer write per frame): vs_3_0 dcl_position v0; dcl_position o0; mad o0, v0, c253, c252 over
// a four-vertex MANAGED unit-square strip, the rectangle in c252 = (x0, y0, z, 1), c253 = (x1 - x0, y1 - y0, 0, 0).
constexpr DWORD rect_mad_vs_words[] = {0xfffe0300u, 0x0200001fu, 0x80000000u, 0x900f0000u, 0x0200001fu,
                                       0x80000000u, 0xe00f0000u, 0x04000004u, 0xe00f0000u, 0x90e40000u,
                                       0xa0e400fdu, 0xa0e400fcu, 0x0000ffffu};
constexpr D3DVERTEXELEMENT9 strip_elements[] = {
    {0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
constexpr float unit_strip[8] = {0, 0, 1, 0, 0, 1, 1, 1};
constexpr D3DVERTEXELEMENT9 rect_elements[] = {
    {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
// One test's rectangle: four FLOAT4 vertices (a two-triangle strip; the block draws with D3DCULL_NONE).
constexpr UINT rect_vertex_bytes = 16, rect_bytes = 4 * rect_vertex_bytes;
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
    retest_skipped += o.retest_skipped;
    blocks += o.blocks;
    stale += o.stale;
    cadence += o.cadence;
    forced += o.forced;
    test_ns += o.test_ns;
}

OcclusionCullPass::~OcclusionCullPass() {
    detach();
}
HRESULT OcclusionCullPass::attach(IDirect3DDevice9* device, void* const* native, OcclusionCullBuffer buffer) noexcept {
    detach();
    if (!device || !native) {
        reason_ = "no_device";
        return E_INVALIDARG;
    }
    device_ = device;
    native_ = native;
    buffer_ = buffer;
    LARGE_INTEGER frequency{};
    qpc_frequency_ = QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 ? frequency.QuadPart : 0;
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
    const bool constants = buffer_ == OcclusionCullBuffer::constants;
    hr = reinterpret_cast<CreateVsFn>(native_[CreateVertexShader])(device_, constants ? rect_mad_vs_words : rect_vs_words,
                                                                    &vs_);
    if (SUCCEEDED(hr)) hr = reinterpret_cast<CreatePsFn>(native_[CreatePixelShader])(device_, ps_words, &ps_);
    if (SUCCEEDED(hr))
        hr = reinterpret_cast<CreateDeclFn>(native_[CreateVertexDeclaration])(device_, constants ? strip_elements : rect_elements,
                                                                               &declaration_);
    if (SUCCEEDED(hr)) hr = create_buffer();
    if (SUCCEEDED(hr)) hr = create_queries();
    if (FAILED(hr) || !vs_ || !ps_ || !declaration_ || !rects_) {
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
// The rectangle buffer, sized to the pool (four vertices per query): dynamic and discard = DEFAULT pool, recreated after
// a Reset; managed = MANAGED pool, created once; constants = the MANAGED unit-square strip, written once.
HRESULT OcclusionCullPass::create_buffer() noexcept {
    if (rects_) return S_OK;
    cursor_ = 0;
    if (buffer_ == OcclusionCullBuffer::constants) {
        HRESULT hr = reinterpret_cast<CreateVbFn>(native_[CreateVertexBuffer])(device_, UINT(sizeof unit_strip),
                                                                                D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED,
                                                                                &rects_, nullptr);
        void* p = nullptr;
        if (SUCCEEDED(hr)) hr = rects_->Lock(0, 0, &p, 0);
        if (SUCCEEDED(hr)) {
            std::memcpy(p, unit_strip, sizeof unit_strip);
            hr = rects_->Unlock();
        }
        if (FAILED(hr)) drop(rects_);
        return hr;
    }
    const bool dynamic = default_pool();
    return reinterpret_cast<CreateVbFn>(native_[CreateVertexBuffer])(
        device_, UINT(core::pool_size * rect_bytes), D3DUSAGE_WRITEONLY | (dynamic ? D3DUSAGE_DYNAMIC : 0u), 0,
        dynamic ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED, &rects_, nullptr);
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
    drop(rects_);
    cursor_ = 0;
    drop(declaration_);
    drop(ps_);
    drop(vs_);
    ring_.clear();
    for (auto& f : formats_) f = FormatVerdict{};
    targets_frame_ = 0;
    device_ = nullptr;
    native_ = nullptr;
    reason_ = "detached";
}
void OcclusionCullPass::before_reset() noexcept {
    if (!device_) return;
    release_queries();
    if (default_pool()) drop(rects_); // DEFAULT pool: before Reset; MANAGED survives
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
    targets_frame_ = 0;
    HRESULT hr = create_buffer();                  // dynamic: released before the Reset; managed: still held
    if (SUCCEEDED(hr)) hr = create_queries();
    if (FAILED(hr) || !rects_) {
        release_queries();
        if (default_pool()) drop(rects_);
        reason_ = "create_failed";
        return FAILED(hr) ? hr : E_POINTER;
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
// ready or an error = no ready result from that frame. Every read result also goes to the batcher (the cadence's last
// result), older first.
void OcclusionCullPass::read_previous(core::Batcher& batcher) noexcept {
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
            batcher.on_result(rec.entry, rec.key, r);
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
// The block's rectangles into the buffer, one Lock: managed = the records' own region of this frame's slot (Lock flags
// 0); dynamic = appended at the cursor with NOOVERWRITE, or from the start with DISCARD when the block does not fit;
// discard = from the start with DISCARD every block.
// *base_vertex is the first vertex of the block.
HRESULT OcclusionCullPass::write_rects(unsigned first_record, unsigned n, UINT* base_vertex) noexcept {
    UINT first = 0;
    DWORD flags = 0;
    if (buffer_ == OcclusionCullBuffer::managed) {
        first = (ring_.frame & 1) * core::pool_per_frame + first_record;
    } else if (buffer_ == OcclusionCullBuffer::discard) {
        cursor_ = 0;
        flags = D3DLOCK_DISCARD;
    } else {
        if (!cursor_ || cursor_ + n > core::pool_size) {
            cursor_ = 0;
            flags = D3DLOCK_DISCARD;
        } else {
            flags = D3DLOCK_NOOVERWRITE;
        }
        first = cursor_;
    }
    void* p = nullptr;
    HRESULT hr = rects_->Lock(first * rect_bytes, n * rect_bytes, &p, flags);
    if (FAILED(hr) || !p) return FAILED(hr) ? hr : E_POINTER;
    float* v = static_cast<float*>(p);
    for (unsigned i = 0; i < n; ++i, v += 16) {
        const core::Rect& r = items_[i].rect;
        const float x0 = r.c252[0], y0 = r.c252[1], z = r.c252[2], x1 = x0 + r.c253[0], y1 = y0 + r.c253[1];
        const float quad[16] = {x0, y0, z, 1.f, x1, y0, z, 1.f, x0, y1, z, 1.f, x1, y1, z, 1.f};
        std::memcpy(v, quad, sizeof quad);
    }
    hr = rects_->Unlock();
    if (FAILED(hr)) return hr;
    if (buffer_ == OcclusionCullBuffer::dynamic) cursor_ = first + n;
    *base_vertex = first * 4;
    return S_OK;
}
// One ship's block: the batcher's tests for the ship (its previous frame's parts that are due), one state swap, one
// Lock, a query around each rectangle, one restore. Every change is recorded so the restore puts back exactly what was
// changed, even after a failure; a failed block issues nothing (its parts have no result and draw).
void OcclusionCullPass::block(core::Batcher& batcher, const OcclusionCullPart& part, const OcclusionCullState& state,
                              HRESULT* restore) noexcept {
    LARGE_INTEGER t0{}, t1{};
    QueryPerformanceCounter(&t0);
    core::PlanStats plan{};
    std::uint32_t sig = 0;
    unsigned n = batcher.plan(part.ship, part.vp_width, part.vp_height, part.zfunc, items_, ring_.free_records(), &plan,
                              &sig);
    frame_.retest_skipped += plan.retest_skipped;
    frame_.refused += plan.refused;
    frame_.pool_truncated += plan.truncated;
    auto done = [&] {
        QueryPerformanceCounter(&t1);
        const long long ticks = t1.QuadPart - t0.QuadPart; // integer (no x87): ticks under ~9 s at 1 GHz never overflow
        if (qpc_frequency_ > 0 && ticks > 0) frame_.test_ns += std::uint64_t(ticks * 1000000000LL / qpc_frequency_);
    };
    if (!n) return done();
    if (targets_frame_ != ring_.frame || targets_key_ != part.targets_key) {
        targets_ok_ = targets_blend(); // once per frame and target binding (GetRenderTarget and GetDesc per target)
        targets_frame_ = ring_.frame;
        targets_key_ = part.targets_key;
    }
    if (!targets_ok_) {
        frame_.refused += n;
        return done();
    }
    const auto set_rs = reinterpret_cast<SetRsFn>(native_[SetRenderState]);
    // What the block rebinds, read from the device (native getters: no hook sees them).
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexDeclaration9* decl = nullptr;
    IDirect3DVertexBuffer9* stream = nullptr;
    UINT offset = 0, stride = 0, frequency = 1;
    DWORD fvf = 0;
    HRESULT hr = reinterpret_cast<GetFreqFn>(native_[GetStreamSourceFreq])(device_, 0, &frequency);
    if (SUCCEEDED(hr) && frequency != 1) {
        frame_.refused += n; // instanced: the rectangles would draw as instances
        return done();
    }
    if (SUCCEEDED(hr)) hr = reinterpret_cast<GetVsFn>(native_[GetVertexShader])(device_, &vs);
    if (SUCCEEDED(hr)) hr = reinterpret_cast<GetPsFn>(native_[GetPixelShader])(device_, &ps);
    if (SUCCEEDED(hr)) hr = reinterpret_cast<GetFvfFn>(native_[GetFVF])(device_, &fvf);
    if (SUCCEEDED(hr)) hr = reinterpret_cast<GetDeclFn>(native_[GetVertexDeclaration])(device_, &decl);
    if (SUCCEEDED(hr))
        hr = reinterpret_cast<GetStreamFn>(native_[GetStreamSource])(device_, 0, &stream, &offset, &stride);
    // The block's records: contiguous in this frame's slot (plan() kept n within the free records).
    unsigned first_record = 0;
    for (unsigned i = 0; SUCCEEDED(hr) && i < n; ++i) {
        core::Ring::Record* rec = ring_.reserve(items_[i].key, items_[i].rect, items_[i].entry);
        if (!rec) {
            frame_.pool_truncated += n - i;
            n = i;
            break;
        }
        if (!i) first_record = ring_.index_of(rec);
    }
    UINT base_vertex = 0;
    const bool constants = buffer_ == OcclusionCullBuffer::constants;
    if (SUCCEEDED(hr) && n && !constants) hr = write_rects(first_record, n, &base_vertex);
    if (FAILED(hr) || !n) {
        drop(stream);
        drop(decl);
        drop(ps);
        drop(vs);
        if (FAILED(hr)) ++frame_.failed;
        return done(); // nothing changed yet
    }
    HRESULT op = S_OK;
    auto step = [&op](HRESULT r) {
        if (SUCCEEDED(op) && FAILED(r)) op = r;
    };
    bool set_z = false, set_test = false, set_stencil = false, set_op = false, set_separate = false, set_cull = false;
    step(reinterpret_cast<SetDeclFn>(native_[SetVertexDeclaration])(device_, declaration_));
    step(reinterpret_cast<SetStreamFn>(native_[SetStreamSource])(device_, 0, rects_, 0,
                                                                   constants ? UINT(2 * sizeof(float)) : rect_vertex_bytes));
    step(reinterpret_cast<SetVsFn>(native_[SetVertexShader])(device_, vs_));
    step(reinterpret_cast<SetPsFn>(native_[SetPixelShader])(device_, ps_));
    if (state.z_write != FALSE) set_z = true, step(set_rs(device_, D3DRS_ZWRITEENABLE, FALSE));
    if (state.alpha_test != FALSE) set_test = true, step(set_rs(device_, D3DRS_ALPHATESTENABLE, FALSE));
    if (state.stencil != FALSE) set_stencil = true, step(set_rs(device_, D3DRS_STENCILENABLE, FALSE));
    if (state.separate_alpha != FALSE) set_separate = true, step(set_rs(device_, D3DRS_SEPARATEALPHABLENDENABLE, FALSE));
    if (state.blend_op != D3DBLENDOP_ADD) set_op = true, step(set_rs(device_, D3DRS_BLENDOP, D3DBLENDOP_ADD));
    if (state.cull != D3DCULL_NONE) set_cull = true, step(set_rs(device_, D3DRS_CULLMODE, D3DCULL_NONE));
    step(set_rs(device_, D3DRS_ALPHABLENDENABLE, TRUE));
    step(set_rs(device_, D3DRS_SRCBLEND, D3DBLEND_ZERO));
    step(set_rs(device_, D3DRS_DESTBLEND, D3DBLEND_ONE));
    const auto draw = reinterpret_cast<DrawFn>(native_[DrawPrimitive]);
    IDirect3DQuery9* const* query = queries_ + (ring_.frame & 1) * core::pool_per_frame + first_record;
    const auto set_constants = reinterpret_cast<SetConstFn>(native_[SetVertexShaderConstantF]);
    for (unsigned i = 0; SUCCEEDED(op) && i < n; ++i) {
        if (constants) {
            float c[8];
            std::memcpy(c, items_[i].rect.c252, 16);
            std::memcpy(c + 4, items_[i].rect.c253, 16);
            step(set_constants(device_, 252, c, 2));
        }
        step(query[i]->Issue(D3DISSUE_BEGIN));
        if (FAILED(op)) break;
        const HRESULT drawn = draw(device_, D3DPT_TRIANGLESTRIP, constants ? 0u : base_vertex + 4 * i, 2);
        const HRESULT end = query[i]->Issue(D3DISSUE_END); // closes the query even after a failed draw
        step(drawn);
        step(end);
    }
    // Restore, in reverse.
    HRESULT back = S_OK;
    auto put = [&back](HRESULT r) {
        if (SUCCEEDED(back) && FAILED(r)) back = r;
    };
    put(set_rs(device_, D3DRS_DESTBLEND, state.dest_blend));
    put(set_rs(device_, D3DRS_SRCBLEND, state.src_blend));
    put(set_rs(device_, D3DRS_ALPHABLENDENABLE, state.alpha_blend));
    if (set_cull) put(set_rs(device_, D3DRS_CULLMODE, state.cull));
    if (set_op) put(set_rs(device_, D3DRS_BLENDOP, state.blend_op));
    if (set_separate) put(set_rs(device_, D3DRS_SEPARATEALPHABLENDENABLE, state.separate_alpha));
    if (set_stencil) put(set_rs(device_, D3DRS_STENCILENABLE, state.stencil));
    if (set_test) put(set_rs(device_, D3DRS_ALPHATESTENABLE, state.alpha_test));
    if (set_z) put(set_rs(device_, D3DRS_ZWRITEENABLE, state.z_write));
    if (constants && state.reserved) put(set_constants(device_, 252, state.reserved, 2));
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
        ++frame_.failed; // the records stay un-issued: no result next frame, the parts draw
        return done();
    }
    for (unsigned i = 0; i < n; ++i) {
        ring_.records[ring_.frame & 1][first_record + i].issued = true;
        batcher.tested(items_[i], sig, std::uint16_t(first_record + i));
    }
    frame_.tested += n;
    frame_.stale += plan.stale;
    frame_.cadence += plan.cadence;
    frame_.forced += plan.forced;
    ++frame_.blocks;
    done();
}
// The part's decision: its most recent ready result (one or two frames old) read hidden, its hull drew this frame and
// its rectangle is stable against that test's. A skip marks this frame's test of the part (if any) so a visible read
// of it next frame counts as drawn late.
OcclusionCullVerdict OcclusionCullPass::decide(core::Batcher& batcher, std::uint16_t entry,
                                               const OcclusionCullPart& part) noexcept {
    const core::Ring::ResultSlot* previous = ring_.lookup(part.key);
    const bool ready_previous = previous && core::ready(previous->result);
    if (!ready_previous) ++frame_.age_none;
    else if (previous->age == 1) ++frame_.age1;
    else ++frame_.age2;
    const bool skip = core::may_skip(ready_previous ? previous : nullptr, part.rect, part.hull_drawn);
    if (ready_previous && previous->result == core::Result::hidden && part.hull_drawn && !skip) ++frame_.unstable;
    if (!skip) return OcclusionCullVerdict::draw;
    ++frame_.skipped;
    const int record = batcher.record_of(entry, part.key);
    if (record >= 0) ring_.records[ring_.frame & 1][record].skipped = true;
    return OcclusionCullVerdict::skip;
}
OcclusionCullVerdict OcclusionCullPass::part(core::Batcher& batcher, const OcclusionCullPart& part,
                                             const OcclusionCullState& state, HRESULT* restore) noexcept {
    *restore = S_OK;
    ++frame_.candidates;
    if (!available_) {
        ++frame_.refused;
        return OcclusionCullVerdict::refused;
    }
    if (!ring_.previous_read) read_previous(batcher);
    if (!part.hull_drawn) {
        ++frame_.no_hull; // its hull draws after it (or not at all): never listed, never tested, drawn
        return OcclusionCullVerdict::draw;
    }
    const bool less = part.zfunc == core::cmp_less || part.zfunc == core::cmp_lessequal;
    const std::uint16_t entry =
        batcher.note_part(part.key, part.ship, part.box, part.rect, part.rows, less, part.model);
    if (entry == core::no_entry) {
        ++frame_.pool_truncated;
        return OcclusionCullVerdict::truncated;
    }
    if (batcher.block_pending(part.ship)) {
        block(batcher, part, state, restore);
        if (FAILED(*restore)) return OcclusionCullVerdict::failed;
    }
    return decide(batcher, entry, part);
}
}
