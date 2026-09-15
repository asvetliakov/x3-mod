// ---- one-cascade depth replay (shadow_replay_depth.h) ------------------------
//
// docs/architecture/shadow-replay-gates.md, "Implemented: cascade-0 depth
// replay fixture"; X3M_SHADOW_REPLAY_DEPTH=1. Included by motion_output.cpp
// inside namespace x3m. Off: no code runs (every site tests
// depth_replay_requested_). On: per leased slice-0 candidate one GetVertexDeclaration
// and three AddRefs at the draw (the geometry lease), a 64-byte rows copy and a
// 16-byte sun copy; at the scene end the pass transaction on the quiet records,
// the lease releases and one log line. No allocation after attach.

// The geometry lease of the record just made by note_candidate_draw. The
// application's own bindings are retained by native AddRef (VB, IB) and the
// documented GetVertexDeclaration reference; nothing is copied or Locked. A
// record whose rows window is unknown or whose declaration cannot be read is
// left unleased (counted skipped_state at the scene end). LastError is kept.
void MotionOutput::note_depth_geometry(const MotionRoute& route, unsigned index) noexcept {
    if (index >= shadow_replay::record_capacity) return;
    auto& g = depth_geometry_[index];
    g = {};
    const UINT matrix_register = shadow_.vs_row ? shadow_.vs_row->matrix_register : shadow_.vs_prepass ? shadow_.vs_prepass->matrix_register : ~0u;
    const std::size_t window = matrix_register == ~0u ? motion_matrix_windows_max : window_of(matrix_register);
    if (window >= motion_matrix_windows_max || !shadow_.rows_known[window]) return;
    if (!shadow_.stream0_identity || (route.key.indexed && !shadow_.indices_identity)) return;
    std::memcpy(g.rows, shadow_.rows[window], sizeof g.rows);
    const auto& key = route.key;
    g.stream_offset = key.stream_offset; g.stride = key.stride; g.topology = static_cast<D3DPRIMITIVETYPE>(key.topology);
    g.primitives = key.primitives; g.first = key.first; g.base_vertex = key.base_vertex; g.min_vertex = key.min_vertex;
    g.vertex_count = key.vertex_count; g.indexed = key.indexed;
    const DWORD error = GetLastError();
    DWORD cull = D3DCULL_NONE;
    if (SUCCEEDED(render_state(D3DRS_CULLMODE, &cull))) g.cull_mode = cull;
    if (depth_sun_written_) { std::memcpy(g.sun, depth_sun_constant_, sizeof g.sun); g.sun_known = true; }
    IDirect3DVertexDeclaration9* declaration = nullptr;
    if (FAILED(native<GetDeclarationFn>(GetVertexDeclaration)(device_, &declaration)) || !declaration) { release(declaration); SetLastError(error); return; }
    // Only stream 0 is leased: a declaration that reads another stream, or an
    // instanced stream 0, would replay against whatever is bound at the scene
    // end, so such a record is refused (skipped_state, detail multistream).
    D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH + 1]{};
    UINT count = MAXD3DDECLLENGTH + 1, frequency = 0;
    bool stream0_only = SUCCEEDED(declaration->GetDeclaration(elements, &count)) && count >= 2 && count <= MAXD3DDECLLENGTH + 1;
    for (UINT i = 0; stream0_only && i + 1 < count; ++i) stream0_only = elements[i].Stream == 0;
    if (stream0_only) stream0_only = SUCCEEDED(native<GetStreamFreqFn>(GetStreamSourceFreq)(device_, 0, &frequency)) && frequency == 1;
    if (!stream0_only) { release(declaration); g.multistream = true; SetLastError(error); return; }
    g.declaration = declaration;
    g.vertex_buffer = reinterpret_cast<IDirect3DVertexBuffer9*>(shadow_.stream0_identity); g.vertex_buffer->AddRef();
    if (g.indexed) { g.index_buffer = reinterpret_cast<IDirect3DIndexBuffer9*>(shadow_.indices_identity); g.index_buffer->AddRef(); }
    g.leased = true;
    SetLastError(error);
}
// Retires every lease (after the scene end, at a frame begin without a scene
// end, before Reset and at teardown). The caller holds the capture mutex, so a
// final Release reaching the hooked buffer paths reenters safely.
void MotionOutput::release_depth_leases() noexcept {
    for (auto& g : depth_geometry_) {
        if (!g.leased && !g.declaration && !g.vertex_buffer && !g.index_buffer) continue;
        release(g.declaration); release(g.vertex_buffer); release(g.index_buffer);
        g.leased = false;
    }
}
void MotionOutput::log_depth_refusal(shadow_replay::DepthReason reason, const char* detail, HRESULT result, unsigned stage) noexcept {
    unsigned& logged = depth_refusal_logs_[unsigned(reason)];
    if (logged >= shadow_replay::depth_refusal_log_limit) return;
    ++logged;
    log("shadow_replay_depth_refused device=%llu frame=%llu reason=%s detail=%s result=%08lx stage=%u",
        id_, frame_, shadow_replay::depth_reason_name(reason), detail, result, stage);
}
// The pass, attached once per device (a refusal is final until Reset).
bool MotionOutput::ensure_shadow_replay_depth() noexcept {
    if (depth_replay_ && depth_replay_->caps().enabled) return true;
    if (depth_replay_attach_failed_) return false;
    depth_replay_attach_failed_ = true;
    if (!depth_replay_) { try { depth_replay_ = std::make_unique<renderer::ShadowReplayPass>(); } catch (...) { depth_replay_attach_result_ = E_OUTOFMEMORY; return false; } }
    D3DDISPLAYMODE display{};
    HRESULT hr = native<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &display);
    const char* reason = "adapter_query";
    if (SUCCEEDED(hr)) {
        taa_call([&] { hr = depth_replay_->attach(device_, native_, caps_, display.Format, depth_replay_size_); });
        reason = depth_replay_->caps().reason;
        depth_replay_attach_failed_ = FAILED(hr) || !depth_replay_->caps().enabled;
    }
    depth_replay_attach_result_ = hr;
    const auto& caps = depth_replay_->caps();
    log("shadow_replay_depth_device device=%llu attached=%u reason=%s result=%08lx size=%u map_format=%u depth_format=%u readable=%u adapter_format=%u",
        id_, !depth_replay_attach_failed_, depth_replay_attach_failed_ ? reason : "ok", hr, depth_replay_size_,
        unsigned(caps.map_format), unsigned(caps.depth_format), caps.readable, unsigned(display.Format));
    return !depth_replay_attach_failed_;
}
// The scene-end transaction on this frame's records. `quiet[i]` is the
// counter's verdict for record i (bookends unchanged, not stale, every buffer
// quiet). Any refusal refuses the whole frame (the map keeps its previous
// content); the counts name the reason per record: replayed == draws or 0.
void MotionOutput::run_shadow_replay_depth(const bool* quiet) noexcept {
    shadow_replay::DepthCounts c{};
    const unsigned n = candidates_.record_count;
    renderer::ShadowReplayDraw draws[shadow_replay::record_capacity];
    unsigned admitted = 0;
    const float* sun = nullptr;
    const char* unleased = nullptr;
    for (unsigned i = 0; i < n; ++i) {
        const auto& g = depth_geometry_[i];
        ++c.draws;
        if (!g.leased) { ++c.skipped_state; if (!unleased) unleased = g.multistream ? "multistream" : "geometry"; continue; }
        if (!quiet[i]) { ++c.skipped_lease; continue; }
        if (!sun && g.sun_known && renderer::shadow_replay_sun_valid(g.sun)) sun = g.sun;
        ++admitted;
    }
    bool refused = c.draws == 0;
    if (!refused && c.skipped_lease) { refused = true; log_depth_refusal(shadow_replay::DepthReason::Lease, "bookends", S_OK, 0); }
    if (!refused && c.skipped_state) { refused = true; log_depth_refusal(shadow_replay::DepthReason::State, unleased, S_OK, 0); }
    if (!refused && !ensure_shadow_replay_depth()) {
        refused = true; c.skipped_caps = c.draws;
        log_depth_refusal(shadow_replay::DepthReason::Caps, depth_replay_ ? depth_replay_->caps().reason : "allocation", depth_replay_attach_result_, 0);
    }
    const char* state = nullptr;
    if (!refused) {
        if (!camera_scene_.valid) state = "camera";
        else if (!sun) state = "no_sun"; // no c4 write this frame (cleared at begin_frame), or none finite and unit
        else if (shadow_.recording) state = "recording";
        else if (active_queries_) state = "queries";
        else if (depth_replay_->reset_pending()) state = "reset_pending";
    }
    renderer::ShadowReplayBasis basis{};
    if (!refused && !state && !renderer::shadow_replay_basis(camera_scene_, sun, depth_cascade_, basis)) state = "basis";
    if (!refused && !state) {
        unsigned k = 0;
        for (unsigned i = 0; i < n && !state; ++i) {
            const auto& g = depth_geometry_[i];
            auto& d = draws[k];
            d = {};
            d.vertex_buffer = g.vertex_buffer; d.index_buffer = g.index_buffer; d.declaration = g.declaration;
            d.stream_offset = g.stream_offset; d.stride = g.stride; d.topology = g.topology; d.primitives = g.primitives; d.first = g.first;
            d.min_vertex = g.min_vertex; d.vertex_count = g.vertex_count; d.base_vertex = g.base_vertex; d.indexed = g.indexed; d.cull_mode = g.cull_mode;
            if (!renderer::shadow_replay_light_rows(camera_scene_, g.rows, basis, depth_cascade_, d.light_rows)) state = "rows";
            ++k;
        }
    }
    if (!refused && state) { refused = true; c.skipped_state = c.draws; log_depth_refusal(shadow_replay::DepthReason::State, state, S_OK, 0); }
    if (!refused) {
        renderer::ShadowReplayResult out{};
        const unsigned allocations = depth_replay_->allocations();
        LARGE_INTEGER t0{}, t1{}, f{};
        HRESULT hr = E_FAIL;
        QueryPerformanceCounter(&t0);
        taa_call([&] { hr = depth_replay_->execute(draws, admitted, scene_open_, shadow_.recording, &out); });
        QueryPerformanceCounter(&t1);
        QueryPerformanceFrequency(&f);
        c.us = f.QuadPart ? double(t1.QuadPart - t0.QuadPart) * 1e6 / double(f.QuadPart) : 0.;
        if (depth_replay_->allocations() != allocations) {
            const auto& caps = depth_replay_->caps();
            log("shadow_replay_depth_target device=%llu frame=%llu size=%u map_format=%u depth_format=%u allocations=%u",
                id_, frame_, depth_replay_->size(), unsigned(caps.map_format), unsigned(caps.depth_format), depth_replay_->allocations());
        }
        if (FAILED(out.restore)) invalidate_render_states();
        if (FAILED(hr)) { c.skipped_state = c.draws; log_depth_refusal(shadow_replay::DepthReason::State, "transaction", out.operation, unsigned(out.failed)); }
        else { c.replayed = out.drawn; depth_basis_ = basis; }
    }
    release_depth_leases();
    log("shadow_replay_depth device=%llu frame=%llu replayed=%u skipped_lease=%u skipped_state=%u skipped_caps=%u draws=%u us=%.1f",
        id_, frame_, c.replayed, c.skipped_lease, c.skipped_state, c.skipped_caps, c.draws, c.us);
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
// Seam: the map as floats (R32F only) and the last replayed frame's basis:
// right, up, forward, center (world), half_extent, depth_half_range, size, valid.
HRESULT MotionOutput::fixture_shadow_replay_readback(float* out, std::size_t floats, UINT* width, UINT* height, float* params, unsigned param_floats) noexcept {
    const unsigned size = depth_replay_ ? depth_replay_->size() : 0;
    if (width) *width = size;
    if (height) *height = size;
    if (params && param_floats >= 16) {
        for (unsigned i = 0; i < 3; ++i) { params[i] = depth_basis_.right[i]; params[3 + i] = depth_basis_.up[i]; params[6 + i] = depth_basis_.forward[i]; params[9 + i] = depth_basis_.center[i]; }
        params[12] = depth_cascade_.half_extent; params[13] = depth_cascade_.depth_half_range; params[14] = float(size); params[15] = depth_basis_.valid ? 1.f : 0.f;
    }
    if (!depth_replay_ || !depth_replay_->map_surface()) return D3DERR_NOTFOUND;
    if (!depth_replay_->caps().readable) return D3DERR_NOTAVAILABLE;
    if (!out || floats < std::size_t(size) * size) return D3DERR_MOREDATA;
    IDirect3DSurface9* copy = nullptr;
    HRESULT hr = native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, size, size, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &copy, nullptr);
    if (SUCCEEDED(hr)) hr = native<GetRtDataFn>(GetRenderTargetData)(device_, depth_replay_->map_surface(), copy);
    D3DLOCKED_RECT lock{};
    if (SUCCEEDED(hr)) hr = copy->LockRect(&lock, nullptr, D3DLOCK_READONLY);
    if (SUCCEEDED(hr)) {
        for (UINT y = 0; y < size; ++y) std::memcpy(out + std::size_t(y) * size, static_cast<const char*>(lock.pBits) + y * lock.Pitch, std::size_t(size) * 4);
        copy->UnlockRect();
    }
    release(copy);
    return hr;
}
#endif
