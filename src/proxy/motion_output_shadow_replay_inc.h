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
// The geometry of one draw without its lease: the rows, keys and cull mode,
// and the declaration's own GetVertexDeclaration reference (the caller
// releases it or keeps it as the lease); the buffer pointers are the
// identities, not AddRef'd. False (declaration released, nothing held) when
// the rows window is unknown, a binding identity is missing or the
// declaration cannot be read.
bool MotionOutput::fill_depth_geometry(const MotionRoute& route, shadow_replay::DepthGeometry& g) noexcept {
    g = {};
    const UINT matrix_register = shadow_.vs_row ? shadow_.vs_row->matrix_register : shadow_.vs_prepass ? shadow_.vs_prepass->matrix_register : ~0u;
    const std::size_t window = matrix_register == ~0u ? motion_matrix_windows_max : window_of(matrix_register);
    if (window >= motion_matrix_windows_max || !shadow_.rows_known[window]) return false;
    if (!shadow_.stream0_identity || (route.key.indexed && !shadow_.indices_identity)) return false;
    std::memcpy(g.rows, shadow_.rows[window], sizeof g.rows);
    const auto& key = route.key;
    g.stream_offset = key.stream_offset; g.stride = key.stride; g.topology = static_cast<D3DPRIMITIVETYPE>(key.topology);
    g.primitives = key.primitives; g.first = key.first; g.base_vertex = key.base_vertex; g.min_vertex = key.min_vertex;
    g.vertex_count = key.vertex_count; g.indexed = key.indexed;
    const DWORD error = GetLastError();
    DWORD cull = D3DCULL_NONE;
    if (SUCCEEDED(render_state(D3DRS_CULLMODE, &cull))) g.cull_mode = cull;
    IDirect3DVertexDeclaration9* declaration = nullptr;
    if (FAILED(native<GetDeclarationFn>(GetVertexDeclaration)(device_, &declaration)) || !declaration) { release(declaration); SetLastError(error); return false; }
    // Only stream 0 is leased: a declaration that reads another stream, or an
    // instanced stream 0, would replay against whatever is bound at the scene
    // end, so such a record is refused (skipped_state, detail multistream).
    // The element verdict comes from the declaration hook's own GetDeclaration
    // read (shadow_.declaration_stream0_only, the same elements the key hashes;
    // the gate required the hashed declaration) and the frequency from gate 4's
    // read of this draw when it has one: no per-lease GetDeclaration copy or
    // second GetStreamSourceFreq (route-bench: 1.9 us per leased draw before).
    UINT frequency = 0;
    bool stream0_only = shadow_.declaration_stream0_only;
    if (stream0_only) stream0_only = (route.stream0_frequency_known ? (frequency = route.stream0_frequency, true)
        : SUCCEEDED(direct_call<GetStreamFreqFn>(GetStreamSourceFreq, 0, &frequency))) && frequency == 1;
    if (!stream0_only) { release(declaration); g.multistream = true; SetLastError(error); return false; }
    g.declaration = declaration;
    g.vertex_buffer = reinterpret_cast<IDirect3DVertexBuffer9*>(shadow_.stream0_identity);
    if (g.indexed) g.index_buffer = reinterpret_cast<IDirect3DIndexBuffer9*>(shadow_.indices_identity);
    SetLastError(error);
    return true;
}
void MotionOutput::note_depth_geometry(const MotionRoute& route, unsigned index) noexcept {
    if (index >= candidate_capacity_) return;
    auto& g = depth_geometry_[index];
    if (!fill_depth_geometry(route, g)) return;
    const DWORD error = GetLastError();
    g.vertex_buffer->AddRef();
    if (g.indexed) g.index_buffer->AddRef();
    g.leased = true;
    SetLastError(error);
}
// A draw the static gate refused from every cascade it met (no record, no
// lease) is still a sighting of its node for the retention store
// (directional-shadows.md, "Run 40 A (run116) diagnosis", cause 1): the same
// sighting a record would give it (rows, extent, cull mode, the buffer views
// and the declaration identity), from which the store takes its own
// references in live mode as for any sighting; the geometry lease is not
// taken and nothing is issued. A range the live store already holds reuses
// its declaration identity (rows and cull mode only); otherwise the geometry
// is queried once and the declaration's reference released here.
// LastError is kept by the callees.
void MotionOutput::note_refused_sighting(const MotionRoute& route, const ownership::BufferLockView& vb, const shadow_replay::ExtentEntry* extent) noexcept {
    ownership::BufferLockView ib{};
    if (route.key.indexed && !(shadow_.indices_identity && SUCCEEDED(ownership::get_buffer_lock_view_light(reinterpret_cast<IDirect3DResource9*>(shadow_.indices_identity), &ib)) && ib.known)) return;
    auto& st = *retention_;
    LARGE_INTEGER t0{}, t1{}; // retention_ticks lives in the retention include, which follows this one
    if (st.timing) QueryPerformanceCounter(&t0);
    const auto& k = route.key;
    shadow_replay::DepthGeometry g{};
    bool queried = false;
    // A range the live store already holds for this node keeps its declaration identity, and the
    // census holds none: only the rows and the cull mode are read (no device query); a first
    // refused sighting of a range under the live store pays the geometry queries once (measured
    // 4.2 us per sighting in the pool fixture with them, 1.2 us without).
    bool declaration_needed = st.mode == shadow_retention::Mode::Live;
    if (declaration_needed) {
        shadow_retention::DrawKey key; key.vb = k.vertex_buffer; key.ib = k.indexed ? k.index_buffer : 0; key.declaration = k.declaration;
        key.stream_offset = k.stream_offset; key.stride = k.stride; key.topology = k.topology; key.primitives = k.primitives; key.first = k.first;
        key.min_vertex = k.min_vertex; key.vertex_count = k.vertex_count; key.base_vertex = k.base_vertex; key.indexed = k.indexed;
        const shadow_retention::BufferStamp vs{shadow_.stream0_identity, vb.allocation_id, vb.generation, vb.revision}, is{k.indexed ? shadow_.indices_identity : 0, k.indexed ? ib.allocation_id : 0, k.indexed ? ib.generation : 0, k.indexed ? ib.revision : 0};
        g.declaration = reinterpret_cast<IDirect3DVertexDeclaration9*>(st.store.known_declaration(k.object_lifetime, key, vs, is));
        declaration_needed = g.declaration == nullptr;
    }
    if (!declaration_needed) {
        const UINT matrix_register = shadow_.vs_row ? shadow_.vs_row->matrix_register : shadow_.vs_prepass ? shadow_.vs_prepass->matrix_register : ~0u;
        const std::size_t window = matrix_register == ~0u ? motion_matrix_windows_max : window_of(matrix_register);
        if (window >= motion_matrix_windows_max || !shadow_.rows_known[window] || !shadow_.stream0_identity) return;
        std::memcpy(g.rows, shadow_.rows[window], sizeof g.rows);
        DWORD cull = D3DCULL_NONE;
        if (SUCCEEDED(render_state(D3DRS_CULLMODE, &cull))) g.cull_mode = cull;
    } else {
        if (!fill_depth_geometry(route, g)) return;
        queried = true;
    }
    shadow_replay::Record r{};
    r.serial = route.key.object_lifetime;
    r.vb = route.key.vertex_buffer; r.vb_identity = shadow_.stream0_identity;
    r.ib = route.key.indexed ? route.key.index_buffer : 0; r.ib_identity = route.key.indexed ? shadow_.indices_identity : 0;
    r.vb_view = static_cast<const ownership::BufferLockObservation&>(vb);
    r.ib_view = static_cast<const ownership::BufferLockObservation&>(ib);
    r.vb_generation = vb.generation; r.ib_generation = ib.generation;
    g.leased = true; // the sighting is complete (rows, declaration); no lease is held, the draw is not issued
    note_retention_draw(route, r, g, extent);
    ++st.store.frame.gate_sightings;
    if (queried) release(g.declaration); // a reused identity is the store's reference, not ours
    if (st.timing) { QueryPerformanceCounter(&t1); st.gate_ticks += t1.QuadPart - t0.QuadPart; ++st.gate_calls; } // the whole refused-draw path (the IB view, the geometry queries, the store)
}
// Retires every lease (after the scene end, at a frame begin without a scene
// end, before Reset and at teardown). The caller holds the capture mutex, so a
// final Release reaching the hooked buffer paths reenters safely.
void MotionOutput::release_depth_leases() noexcept {
    for (unsigned i = 0; i < candidate_capacity_; ++i) {
        auto& g = depth_geometry_[i];
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
        if (depth_cascades_on()) {
            // One pass, N maps and the shared attachment; a size above
            // MaxTextureWidth/Height was halved by the pass and is adopted
            // here (the basis snaps to the texel the map really has).
            unsigned sizes[renderer::shadow_cascade_max]{};
            for (unsigned i = 0; i < depth_cascades_.count; ++i) sizes[i] = depth_cascades_.cascades[i].size;
            taa_call([&] { hr = depth_replay_->attach_cascades(device_, native_, caps_, display.Format, sizes, depth_cascades_.count); });
            if (SUCCEEDED(hr)) for (unsigned i = 0; i < depth_cascades_.count; ++i) depth_cascades_.cascades[i].size = depth_cascade_base_.cascades[i].size = depth_replay_->size(i);
        } else
        taa_call([&] { hr = depth_replay_->attach(device_, native_, caps_, display.Format, depth_replay_size_); });
        reason = depth_replay_->caps().reason;
        depth_replay_attach_failed_ = FAILED(hr) || !depth_replay_->caps().enabled;
    }
    depth_replay_attach_result_ = hr;
    const auto& caps = depth_replay_->caps();
    log("shadow_replay_depth_device device=%llu attached=%u reason=%s result=%08lx size=%u map_format=%u depth_format=%u readable=%u adapter_format=%u",
        id_, !depth_replay_attach_failed_, depth_replay_attach_failed_ ? reason : "ok", hr, depth_replay_size_,
        unsigned(caps.map_format), unsigned(caps.depth_format), caps.readable, unsigned(display.Format));
    if (depth_cascades_on()) {
        // The caps line of the cascade attach (shadow-cascades.md, "Unknown"):
        // the device limits beside what the pass kept.
        static_assert(renderer::shadow_cascade_max == 5, "the device line lists five cascades");
        log("shadow_replay_cascades_device device=%llu attached=%u cascades=%u sizes=%u,%u,%u,%u,%u depth_size=%u halved=%u max_texture=%lux%lu budget=%u caps=%u,%u,%u,%u,%u",
            id_, !depth_replay_attach_failed_, depth_cascades_.count, depth_replay_->size(0), depth_replay_->size(1), depth_replay_->size(2), depth_replay_->size(3), depth_replay_->size(4),
            depth_replay_->depth_size(), caps.halved, static_cast<unsigned long>(caps_.MaxTextureWidth), static_cast<unsigned long>(caps_.MaxTextureHeight), depth_cascades_.budget,
            depth_cascades_.caps[0], depth_cascades_.caps[1], depth_cascades_.caps[2], depth_cascades_.caps[3], depth_cascades_.caps[4]);
    }
    return !depth_replay_attach_failed_;
}
// The scene-end transaction on this frame's records. `quiet[i]` is the
// counter's verdict for record i (bookends unchanged, not stale, every buffer
// quiet). Any refusal refuses the whole frame (the map keeps its previous
// content); the counts name the reason per record: replayed == draws or 0.
void MotionOutput::run_shadow_replay_depth(const bool* quiet) noexcept {
    shadow_replay::DepthCounts c{};
    // Once per frame: a second scene-end signal (a second EndScene) must not
    // rerun the replay with no records after the quad consumed this frame's
    // map; the first result's rows and counts stand for the frame.
    if (depth_replayed_frame_ == frame_) return;
    depth_replayed_ = 0; depth_replayed_frame_ = frame_;
    if (depth_replay_) depth_replay_->set_view_rows(nullptr); // a refused frame leaves no rows for the apply quad
    const unsigned n = candidates_.record_count;
    renderer::ShadowReplayDraw* const draws = depth_draws_;
    unsigned admitted = 0;
    // The frame's one validated sun (shadow_replay_sun.h), never a record's own register.
    const float* sun = shadow_replay::sun_verdict_usable(sun_verdict_) ? sun_latch_.frame_sun() : nullptr;
    const char* unleased = nullptr;
    for (unsigned i = 0; i < n; ++i) {
        const auto& g = depth_geometry_[i];
        ++c.draws;
        if (!g.leased) { ++c.skipped_state; if (!unleased) unleased = g.multistream ? "multistream" : "geometry"; continue; }
        if (!quiet[i]) { ++c.skipped_lease; continue; }
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
        else if (!sun) state = sun_verdict_ == shadow_replay::SunVerdict::None ? "no_sun" : sun_verdict_ == shadow_replay::SunVerdict::Relatched ? "sun_relatched" : "sun_changing"; // never validated, or every sample of this frame disagrees with the validated sun
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
        else {
            c.replayed = out.drawn; depth_basis_ = basis; sun_shadow_force_replay_ = false; // the single map replays whole every frame; the demand is spent here too
            // This frame's view -> sun rows for the scene-end apply quad
            // (legacy-sun-application.md section 2); a row failure leaves the
            // map unconsumable this frame (the quad skips with reason replay).
            float rows[12];
            if (out.drawn && renderer::shadow_replay_view_rows(camera_scene_, basis, depth_cascade_, rows)) { depth_replay_->set_view_rows(rows); depth_replayed_ = out.drawn; }
        }
    }
    release_depth_leases();
    // Sun-shadow apply cost (legacy-sun-application.md, 2): the previous
    // frame's apply, its QPC micros and the cascade maps it sampled, on this
    // line so the replay's cost and the apply's are read together. The fields
    // appear under the same condition that runs the pass (the lane requested
    // and the shadows switched on: one branch, nothing while it is off), and
    // read 0 whenever the previous frame ran no apply at all - never a stale
    // repeat of an older frame's numbers.
    char apply_text[48]; apply_text[0] = 0;
    if (sun_apply_requested_ && sun_shadow_enabled_) {
        const bool previous = sun_apply_frame_ != ~std::uint64_t(0) && sun_apply_frame_ + 1 == frame_;
        const int n = std::snprintf(apply_text, sizeof apply_text, " apply_us=%.1f apply_cascades=%u",
                                    previous ? sun_apply_us_ : 0., previous ? sun_apply_sampled_ : 0u);
        if (n < 0 || n >= int(sizeof apply_text)) apply_text[0] = 0;
    }
    log("shadow_replay_depth device=%llu frame=%llu replayed=%u skipped_lease=%u skipped_state=%u skipped_caps=%u draws=%u us=%.1f shadow_toggle=%u%s",
        id_, frame_, c.replayed, c.skipped_lease, c.skipped_state, c.skipped_caps, c.draws, c.us, unsigned(sun_shadow_enabled_), apply_text);
}
// The cascade transaction (docs/architecture/shadow-cascades.md, section 1):
// the single-map admission and refusal rules on the same records, then per
// cascade the records carrying its bit. The far cascade of a set replays every
// frame while the frame's issues fit the budget, otherwise on even frames only
// and always in full (shadow_cascade_replays); a skipped far map keeps the
// basis it was replayed with (the pass retains it) for the apply. A refused or
// failed frame voids every retained basis (absent cascade = lit); a cascade
// without casters is absent too. Per draw the matrix products run once
// (shadow_cascade_draw_rows) and each issue is 12 scaled rows; no allocation,
// one transaction.
void MotionOutput::run_shadow_replay_cascades(const bool* quiet) noexcept {
    shadow_replay::DepthCounts c{};
    if (depth_replayed_frame_ == frame_) return; // once per frame, as the single map
    depth_replayed_ = 0; depth_replayed_frame_ = frame_; depth_cascade_frame_ok_ = false;
    const unsigned n = candidates_.record_count, cascades = depth_cascades_.count;
    // Live caster retention (shadow-caster-retention.md): the unseen static
    // records the store admitted this frame (masks against the current boxes,
    // inside each cascade's cap behind the live records) are issued after the
    // live ones from the store's own references and retained world rows; they
    // count in the issues and the budget. Off: m = 0 and nothing below differs.
    const bool retained_on = retention_live();
    const unsigned m = retained_on ? retention_->store.admitted_count : 0;
    renderer::ShadowReplayDraw* const draws = retained_on ? retention_->draws.get() : depth_draws_;
    // The latch's verdict gates the frame; each cascade's basis takes its own sun (cascade_sun: one held direction per
    // cascade from the polled sun position, else the latch's sun for all).
    const float* sun = shadow_replay::sun_verdict_usable(sun_verdict_) ? sun_latch_.frame_sun() : nullptr;
    const char* unleased = nullptr;
    unsigned per_cascade[renderer::shadow_cascade_max]{}, issues = 0;
    for (unsigned i = 0; i < n; ++i) {
        const auto& g = depth_geometry_[i];
        ++c.draws;
        if (!g.leased) { ++c.skipped_state; if (!unleased) unleased = g.multistream ? "multistream" : "geometry"; continue; }
        if (!quiet[i]) { ++c.skipped_lease; continue; }
        for (unsigned k = 0; k < cascades; ++k) if (candidates_.records[i].cascades & (1u << k)) { ++per_cascade[k]; ++issues; }
    }
    const unsigned live_issues = issues;
    for (unsigned q = 0; q < m; ++q) {
        const unsigned mask = retention_->store.draws[retention_->store.admitted[q]].cascades;
        for (unsigned k = 0; k < cascades; ++k) if (mask & (1u << k)) { ++per_cascade[k]; ++issues; }
    }
    bool refused = c.draws == 0 && m == 0;
    if (!refused && c.skipped_lease) { refused = true; log_depth_refusal(shadow_replay::DepthReason::Lease, "bookends", S_OK, 0); }
    if (!refused && c.skipped_state) { refused = true; log_depth_refusal(shadow_replay::DepthReason::State, unleased, S_OK, 0); }
    if (!refused && !ensure_shadow_replay_depth()) {
        refused = true; c.skipped_caps = c.draws;
        log_depth_refusal(shadow_replay::DepthReason::Caps, depth_replay_ ? depth_replay_->caps().reason : "allocation", depth_replay_attach_result_, 0);
    }
    const char* state = nullptr;
    if (!refused) {
        if (!camera_scene_.valid) state = "camera";
        else if (!sun) state = sun_verdict_ == shadow_replay::SunVerdict::None ? "no_sun" : sun_verdict_ == shadow_replay::SunVerdict::Relatched ? "sun_relatched" : "sun_changing";
        else if (shadow_.recording) state = "recording";
        else if (active_queries_) state = "queries";
        else if (depth_replay_->reset_pending()) state = "reset_pending";
        else if (issues > depth_issue_capacity_) state = "issues";
    }
    renderer::ShadowReplayBasis bases[renderer::shadow_cascade_max]{};
    bool replays[renderer::shadow_cascade_max]{};
    renderer::ShadowReplayMapList lists[renderer::shadow_cascade_max]{};
    unsigned list_count = 0, offsets[renderer::shadow_cascade_max]{};
    if (!refused && !state) {
        unsigned offset = 0;
        for (unsigned k = 0; k < cascades && !state; ++k) {
            const float* own = cascade_sun(k); // decides the frame's source: the grid anchor below is the same source's
            if (!renderer::shadow_replay_basis(camera_scene_, own, depth_cascades_.cascades[k], bases[k], point_sun_.grid_anchor(k))) { state = "basis"; break; }
            // The first frame after the A/B came back on replays every cascade
            // with casters: the far map's retained basis was voided by the
            // press, so the budget's alternate-frame rule must not leave it
            // absent for a frame (comparison-hotkeys.md, "Sun shadows at rest").
            replays[k] = per_cascade[k] != 0 && (sun_shadow_force_replay_ || renderer::shadow_cascade_replays(k, cascades, issues, depth_cascades_.budget, frame_));
            offsets[k] = offset;
            // Back-face cascades (shadow_cascade_backface_texel_default): the map holds the casters' far sides, so a
            // lit receiver never compares against its own depth (directional-shadows.md, "Run 40 A", cause 2).
            if (replays[k]) { lists[list_count].map = k; lists[list_count].issues = depth_issues_.get() + offset; lists[list_count].count = 0; lists[list_count].invert_cull = (depth_cascade_backface_mask_ >> k & 1u) != 0; ++list_count; offset += per_cascade[k]; }
        }
    }
    // Back-face cascades: the issued records by cull mode (a D3DCULL_NONE caster has no back side to
    // invert and keeps the compare's knife edge: the residual run 41 must show; cull_none<k>= / cull_inverted<k>=).
    unsigned cull_none[renderer::shadow_cascade_max]{}, cull_inverted[renderer::shadow_cascade_max]{};
    const auto count_cull = [&](unsigned k, DWORD cull) { if (depth_cascade_backface_mask_ >> k & 1u) ++(cull == D3DCULL_NONE ? cull_none : cull_inverted)[k]; };
    if (!refused && !state) {
        unsigned fill[renderer::shadow_cascade_max]{};
        for (unsigned i = 0; i < n && !state; ++i) {
            const auto& g = depth_geometry_[i];
            auto& d = draws[i];
            d = {};
            d.vertex_buffer = g.vertex_buffer; d.index_buffer = g.index_buffer; d.declaration = g.declaration;
            d.stream_offset = g.stream_offset; d.stride = g.stride; d.topology = g.topology; d.primitives = g.primitives; d.first = g.first;
            d.min_vertex = g.min_vertex; d.vertex_count = g.vertex_count; d.base_vertex = g.base_vertex; d.indexed = g.indexed; d.cull_mode = g.cull_mode;
            double base[3][4];
            unsigned base_of = 0; // the cascade whose axes `base` was built with (one product per draw while the cascades share a sun)
            if (!renderer::shadow_cascade_draw_rows(camera_scene_, g.rows, bases[0], base)) { state = "rows"; break; }
            for (unsigned k = 0; k < cascades; ++k) {
                if (!replays[k] || !(candidates_.records[i].cascades & (1u << k))) continue;
                if (k != base_of && !renderer::shadow_replay_axes_equal(bases[k], bases[base_of])) {
                    if (!renderer::shadow_cascade_draw_rows(camera_scene_, g.rows, bases[k], base)) { state = "rows"; break; }
                    base_of = k;
                }
                auto& issue = depth_issues_[offsets[k] + fill[k]++];
                issue.draw = std::uint16_t(i); count_cull(k, g.cull_mode);
                if (!renderer::shadow_cascade_light_rows(base, bases[k], depth_cascades_.cascades[k], issue.rows)) { state = "rows"; break; }
            }
        }
        unsigned live_fill[renderer::shadow_cascade_max]{};
        for (unsigned k = 0; k < cascades; ++k) live_fill[k] = fill[k];
        for (unsigned q = 0; q < m && !state; ++q) {
            const auto& r = retention_->store.draws[retention_->store.admitted[q]];
            auto& d = draws[n + q];
            d = {};
            d.vertex_buffer = reinterpret_cast<IDirect3DVertexBuffer9*>(r.vb.identity); d.declaration = reinterpret_cast<IDirect3DVertexDeclaration9*>(r.declaration);
            d.index_buffer = r.key.indexed ? reinterpret_cast<IDirect3DIndexBuffer9*>(r.ib.identity) : nullptr;
            d.stream_offset = r.key.stream_offset; d.stride = r.key.stride; d.topology = static_cast<D3DPRIMITIVETYPE>(r.key.topology); d.primitives = r.key.primitives; d.first = r.key.first;
            d.min_vertex = r.key.min_vertex; d.vertex_count = r.key.vertex_count; d.base_vertex = r.key.base_vertex; d.indexed = r.key.indexed != 0; d.cull_mode = r.cull_mode;
            double base[3][4];
            unsigned base_of = 0; // as the live loop: one product while the cascades share their axes, another per cascade whose sun differs
            shadow_retention::sun_rows(r.world, bases[0], base); // S(sun basis) . W_retained: no camera enters
            for (unsigned k = 0; k < cascades; ++k) {
                if (!replays[k] || !(r.cascades & (1u << k))) continue;
                if (k != base_of && !renderer::shadow_replay_axes_equal(bases[k], bases[base_of])) { shadow_retention::sun_rows(r.world, bases[k], base); base_of = k; }
                auto& issue = depth_issues_[offsets[k] + fill[k]++];
                issue.draw = std::uint16_t(n + q); count_cull(k, r.cull_mode);
                if (!renderer::shadow_cascade_light_rows(base, bases[k], depth_cascades_.cascades[k], issue.rows)) { state = "rows"; break; }
            }
        }
        if (retained_on) for (unsigned k = 0; k < cascades; ++k) { retention_->replayed_live[k] = live_fill[k]; retention_->replayed_retained[k] = fill[k] - live_fill[k]; retention_->retained_issues += fill[k] - live_fill[k]; }
        for (unsigned l = 0; l < list_count; ++l) lists[l].count = fill[lists[l].map];
    }
    if (!refused && state) { refused = true; c.skipped_state = c.draws; log_depth_refusal(shadow_replay::DepthReason::State, state, S_OK, 0); }
    renderer::ShadowReplayResult out{};
    bool far_replayed = false;
    if (!refused && list_count) {
        const unsigned allocations = depth_replay_->allocations();
        LARGE_INTEGER t0{}, t1{}, f{};
        HRESULT hr = E_FAIL;
        QueryPerformanceCounter(&t0);
        taa_call([&] { hr = depth_replay_->execute_cascades(draws, n + m, lists, list_count, scene_open_, shadow_.recording, &out); });
        QueryPerformanceCounter(&t1);
        QueryPerformanceFrequency(&f);
        c.us = f.QuadPart ? double(t1.QuadPart - t0.QuadPart) * 1e6 / double(f.QuadPart) : 0.;
        if (depth_replay_->allocations() != allocations) {
            const auto& caps = depth_replay_->caps();
            log("shadow_replay_depth_target device=%llu frame=%llu size=%u map_format=%u depth_format=%u allocations=%u",
                id_, frame_, depth_replay_->depth_size(), unsigned(caps.map_format), unsigned(caps.depth_format), depth_replay_->allocations());
        }
        if (FAILED(out.restore)) invalidate_render_states();
        if (FAILED(hr)) { refused = true; c.skipped_state = c.draws; log_depth_refusal(shadow_replay::DepthReason::State, "transaction", out.operation, unsigned(out.failed)); }
    }
    if (depth_replay_) {
        if (refused) depth_replay_->invalidate_retained(); // a refused frame, a failed transaction, a lost device: nothing retained survives
        else {
            // Replayed: this frame's basis. Without casters: absent. The far
            // cascade skipped by the budget keeps what it had.
            for (unsigned k = 0; k < cascades; ++k) {
                if (replays[k]) depth_replay_->retain(k, bases[k], frame_, out.drawn_map[k]);
                else if (!per_cascade[k]) depth_replay_->invalidate_retained(k);
            }
            far_replayed = cascades > 1 && replays[cascades - 1];
            // Retained issues count into the far cascade's budget: a far cascade skipped this frame that
            // the live issues alone would have replayed is charged to retention (shadow-caster-retention.md).
            if (retained_on && cascades > 1 && !far_replayed && per_cascade[cascades - 1] && issues > depth_cascades_.budget && live_issues <= depth_cascades_.budget)
                ++retention_->store.frame.far_alternate_due_to_retained, ++retention_->store.totals.far_alternate_due_to_retained;
            c.replayed = c.draws; depth_replayed_ = out.drawn; depth_cascade_frame_ok_ = true; depth_basis_ = bases[0];
            sun_shadow_force_replay_ = false; // consumed: a refused frame keeps the demand for the next transaction
        }
    }
    release_depth_leases();
    char text[192]; int used = 0;
    for (unsigned k = 0; k < cascades && used >= 0 && used < int(sizeof text); ++k) used += std::snprintf(text + used, sizeof text - used, " draws%u=%u", k, refused ? 0u : out.drawn_map[k]);
    if (used < 0 || used >= int(sizeof text)) text[0] = 0;
    const auto* far_kept = depth_replay_ && cascades ? depth_replay_->retained(cascades - 1) : nullptr;
    // Live retention: the issues each replayed cascade took from live and from retained records (absent while the option is off).
    char retained_text[192]; retained_text[0] = 0;
    if (retained_on) {
        int length = 0;
        if (refused) { retention_->retained_issues = 0; for (unsigned k = 0; k < cascades; ++k) retention_->replayed_live[k] = retention_->replayed_retained[k] = 0; }
        for (unsigned k = 0; k < cascades && length >= 0 && length < int(sizeof retained_text); ++k)
            length += std::snprintf(retained_text + length, sizeof retained_text - length, " replayed_live%u=%u replayed_retained%u=%u", k, retention_->replayed_live[k], k, retention_->replayed_retained[k]);
        if (length < 0 || length >= int(sizeof retained_text)) retained_text[0] = 0;
    }
    char cull_text[224]; cull_text[0] = 0; // only while a cascade replays back faces
    if (depth_cascade_backface_mask_) {
        int length = 0;
        for (unsigned k = 0; k < cascades && length >= 0 && length < int(sizeof cull_text); ++k)
            length += std::snprintf(cull_text + length, sizeof cull_text - length, " cull_none%u=%u cull_inverted%u=%u", k, refused ? 0u : cull_none[k], k, refused ? 0u : cull_inverted[k]);
        if (length < 0 || length >= int(sizeof cull_text)) cull_text[0] = 0;
    }
    // Sun-shadow apply cost (legacy-sun-application.md, 2): the previous
    // frame's apply, its QPC micros and the cascade maps it sampled, on this
    // line so the replay's cost and the apply's are read together. The fields
    // appear under the same condition that runs the pass (the lane requested
    // and the shadows switched on: one branch, nothing while it is off), and
    // read 0 whenever the previous frame ran no apply at all - never a stale
    // repeat of an older frame's numbers.
    char apply_text[48]; apply_text[0] = 0;
    if (sun_apply_requested_ && sun_shadow_enabled_) {
        const bool previous = sun_apply_frame_ != ~std::uint64_t(0) && sun_apply_frame_ + 1 == frame_;
        const int n = std::snprintf(apply_text, sizeof apply_text, " apply_us=%.1f apply_cascades=%u",
                                    previous ? sun_apply_us_ : 0., previous ? sun_apply_sampled_ : 0u);
        if (n < 0 || n >= int(sizeof apply_text)) apply_text[0] = 0;
    }
    log("shadow_replay_depth device=%llu frame=%llu replayed=%u skipped_lease=%u skipped_state=%u skipped_caps=%u draws=%u us=%.1f shadow_toggle=%u%s far_replayed=%u far_frame=%lld issues=%u budget=%u%s%s%s",
        id_, frame_, c.replayed, c.skipped_lease, c.skipped_state, c.skipped_caps, c.draws, c.us, unsigned(sun_shadow_enabled_), text, unsigned(far_replayed),
        far_kept ? static_cast<long long>(far_kept->frame) : -1ll, issues, depth_cascades_.budget, retained_text, cull_text, apply_text);
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
// Seam: the map as floats (R32F only) and the last replayed frame's basis:
// right, up, forward, center (world), half_extent, depth half range, size, valid.
// Cascades on: `cascade` selects the map; the basis is the one retained for it
// (valid 0 while absent) and params[16..18] are depth_toward_light,
// depth_behind and the frame it was replayed on.
HRESULT MotionOutput::fixture_shadow_replay_readback(float* out, std::size_t floats, UINT* width, UINT* height, float* params, unsigned param_floats, unsigned cascade) noexcept {
    const bool cascades = depth_cascades_on();
    if (cascade && (!cascades || cascade >= depth_cascades_.count)) return D3DERR_INVALIDCALL;
    const unsigned size = depth_replay_ ? depth_replay_->size(cascade) : 0;
    if (width) *width = size;
    if (height) *height = size;
    const auto* kept = cascades && depth_replay_ ? depth_replay_->retained(cascade) : nullptr;
    const renderer::ShadowReplayBasis none{};
    const auto& basis = cascades ? (kept ? kept->basis : none) : depth_basis_;
    const auto& box = cascades ? depth_cascades_.cascades[cascade] : depth_cascade_;
    if (params && param_floats >= 16) {
        for (unsigned i = 0; i < 3; ++i) { params[i] = basis.right[i]; params[3 + i] = basis.up[i]; params[6 + i] = basis.forward[i]; params[9 + i] = basis.center[i]; }
        params[12] = box.half_extent; params[13] = float(box.depth_half()); params[14] = float(size); params[15] = basis.valid ? 1.f : 0.f;
        if (param_floats >= 19) { params[16] = box.depth_toward_light; params[17] = box.depth_behind; params[18] = kept ? float(kept->frame) : -1.f; }
    }
    if (!depth_replay_ || !depth_replay_->map_surface(cascade)) return D3DERR_NOTFOUND;
    if (!depth_replay_->caps().readable) return D3DERR_NOTAVAILABLE;
    if (!out || floats < std::size_t(size) * size) return D3DERR_MOREDATA;
    IDirect3DSurface9* copy = nullptr;
    HRESULT hr = native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, size, size, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &copy, nullptr);
    if (SUCCEEDED(hr)) hr = native<GetRtDataFn>(GetRenderTargetData)(device_, depth_replay_->map_surface(cascade), copy);
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
