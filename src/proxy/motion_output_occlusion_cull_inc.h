// ---- occlusion cull of ship sub-parts (X3M_OCCLUSION_CULL=on|off, default on; occlusion_cull_core.h) ----
//
// Included by motion_output.cpp inside namespace x3m (docs/architecture/occlusion-cull.md). A main-scene draw (gate 2
// passed, after the small-prop cull, before the jitter and every other binding) with z test on and blending off is
// classified by its scope node's body (Classifier::evaluate: hull pieces record their node and parent as this frame's
// hull owners; parts look for an owner among their ancestors). A part draw with a known vertex extent and clip rows
// gets this frame's occlusion test at its own draw site (renderer::OcclusionCullPass::candidate), so the test sees the
// depth of everything drawn before it, the hull included; when last frame's test of the same draw read 0 samples, its
// hull drew this frame and its rectangle is stable, the draw is not forwarded (the hook returns D3D_OK, as the
// small-prop cull does). Fail closed: no scope, no extent of the buffer's current revision, no rows, a rectangle
// reaching the near plane, an unknown depth function, sRGB writes, a non-solid fill mode, depth bias, user clip planes,
// a bound target that cannot blend, an open application query, a full pool slot or any failed call draws the part.
// Without the shadow-replay candidate counter (no cascade set) there are no extents: one row, off on that device.
// The engine's state is never written.
// Per scene draw with the option on: one scope read and one memo probe; the first draw of a node per frame adds one
// model read, a class-cache probe and (hull) one parent read or (part) a walk of at most walk_depth parent reads. A
// part draw adds the extent probe, eight corner transforms, three native state reads (depth bias, slope bias, clip
// planes) and the test (six native getters, about twenty state calls, one query pair and one two-triangle draw;
// 3.0-3.4 us of CPU and 4.2-5.4 us of pipeline time per test on DXVK, measured: occlusion-cull/gate.json). No
// allocation after the first scene draw.

__attribute__((noinline)) bool MotionOutput::attach_occlusion_cull() noexcept {
    if (occlusion_attach_failed_) return false;
    occlusion_classifier_ = new (std::nothrow) occlusion_cull::core::Classifier();
    if (!occlusion_classifier_) {
        occlusion_attach_failed_ = true;
        occlusion_on_ = false;
        log("occlusion_cull_device device=%llu attached=0 reason=out_of_memory", id_);
        return false;
    }
    return true;
}
void MotionOutput::release_occlusion_cull() noexcept {
    if (occlusion_pass_) {
        taa_call([&] { occlusion_pass_->detach(); }, "occlusion_cull_detach");
        occlusion_pass_.reset();
    }
    occlusion_pass_failed_ = false;
}
bool MotionOutput::cull_occluded(const MotionDrawCall& call, MotionRoute& route) {
    namespace oc = occlusion_cull::core;
    if (call.user_memory || shadow_.recording || active_queries_ || occlusion_pass_failed_) return false;
    if (!candidates_requested_) {
        // The vertex extents come from the shadow-replay candidate counter, which runs only with a cascade set (or the
        // diagnostic counter): without it no part has bounds, so the cull is off on this device (one row).
        occlusion_on_ = false;
        log("occlusion_cull_device device=%llu attached=0 configured=0 reason=no_bounds_source", id_);
        return false;
    }
    std::uintptr_t descriptor = 0, node = 0;
    if (!object_trace::executable_verified() || !object_trace::scope_node(&descriptor, &node)) return false;
    DWORD z = 0, blend = 1;
    if (FAILED(render_state(D3DRS_ZENABLE, &z)) || z != D3DZB_TRUE || FAILED(render_state(D3DRS_ALPHABLENDENABLE, &blend)) ||
        blend)
        return false;
    if (!occlusion_classifier_ && !attach_occlusion_cull()) return false;
    auto& k = *occlusion_classifier_;
    const DWORD error = GetLastError();
    const std::uint32_t frame = std::uint32_t(frame_) + 1u; // a stamp, never 0
    if (k.frame != frame) {
        k.begin(frame);
        occlusion_frame_ = frame;
        if (occlusion_pass_) occlusion_pass_->begin_frame(frame);
    }
    auto read = [](std::uintptr_t p, void* out, std::size_t n) { return engine_memory::read(p, out, n); };
    std::uint32_t model = 0;
    bool hull = false;
    const oc::Class cls = k.evaluate(read, x3m::cull_census::core::body_global_va, std::uint32_t(node), &model, &hull);
    if (cls != oc::Class::part) {
        SetLastError(error);
        return false;
    }
    // A part draw: the vertex extent (the small-prop cull's lookup) and the draw's own clip rows.
    cull_small_props::core::Box extent_box{};
    const cull_small_props::core::Box* extent = small_prop_extent(call, extent_box, false); // current revision only
    const UINT matrix_register = shadow_.vs_row       ? shadow_.vs_row->matrix_register
                                 : shadow_.vs_prepass ? shadow_.vs_prepass->matrix_register
                                                      : ~0u;
    const std::size_t window = matrix_register == ~0u ? motion_matrix_windows_max : window_of(matrix_register);
    const float* rows = window < motion_matrix_windows_max && shadow_.rows_known[window] ? shadow_.rows[window] : nullptr;
    if (!extent || !rows) {
        ++occlusion_refused_.no_bounds;
        SetLastError(error);
        return false;
    }
    oc::Box box{};
    for (unsigned i = 0; i < 3; ++i) {
        box.lo[i] = extent->lo[i];
        box.hi[i] = extent->hi[i];
    }
    DWORD zfunc = 0, srgb = 1, depth_bias = 1, slope_bias = 1, clip = 1, fill = 0;
    const float vp_w = shadow_.viewport.known ? float(shadow_.viewport.width) : float(target_width_);
    const float vp_h = shadow_.viewport.known ? float(shadow_.viewport.height) : float(target_height_);
    oc::Rect rect{};
    if (FAILED(render_state(D3DRS_ZFUNC, &zfunc)) ||
        oc::test_rect(rows, box, vp_w, vp_h, unsigned(zfunc), &rect) != oc::RectStatus::ok) {
        ++occlusion_refused_.unbounded;
        SetLastError(error);
        return false;
    }
    // States under which the rectangle's verdict would not be the part's: sRGB writes (the ZERO/ONE blend round-trips
    // through linear), a fill mode other than solid (the part covers only its edges or points), depth bias (the part's
    // offset depth), user clip planes. The bound targets and their formats are the pass's check (from the device).
    renderer::OcclusionCullState state{};
    const bool ok = SUCCEEDED(render_state(D3DRS_SRGBWRITEENABLE, &srgb)) && !srgb &&
                    SUCCEEDED(render_state(D3DRS_FILLMODE, &fill)) && fill == D3DFILL_SOLID &&
                    SUCCEEDED(get_render_state_native(D3DRS_DEPTHBIAS, &depth_bias)) && !depth_bias &&
                    SUCCEEDED(get_render_state_native(D3DRS_SLOPESCALEDEPTHBIAS, &slope_bias)) && !slope_bias &&
                    SUCCEEDED(get_render_state_native(D3DRS_CLIPPLANEENABLE, &clip)) && !clip &&
                    SUCCEEDED(render_state(D3DRS_ZWRITEENABLE, &state.z_write)) &&
                    SUCCEEDED(render_state(D3DRS_ALPHATESTENABLE, &state.alpha_test)) &&
                    SUCCEEDED(render_state(D3DRS_STENCILENABLE, &state.stencil)) &&
                    SUCCEEDED(render_state(D3DRS_CULLMODE, &state.cull));
    DWORD blend_states[4]{};
    bool blend_ok = ok;
    for (unsigned i = 0; blend_ok && i < 4; ++i) // SRCBLEND, DESTBLEND, BLENDOP, SEPARATEALPHABLENDENABLE
        blend_ok = blend_known(i) ? (blend_states[i] = shadow_.composition_blend[i], true)
                                  : SUCCEEDED(get_render_state_native(composition_blend_states[i], &blend_states[i]));
    if (!blend_ok) {
        ++occlusion_refused_.state;
        SetLastError(error);
        return false;
    }
    state.alpha_blend = FALSE;
    state.src_blend = blend_states[0];
    state.dest_blend = blend_states[1];
    state.blend_op = blend_states[2];
    state.separate_alpha = blend_states[3];
    state.reserved = shadow_.vs_reserved_written ? shadow_.vs_reserved : nullptr;
    if (!occlusion_pass_ && !occlusion_pass_failed_) {
        HRESULT hr = E_OUTOFMEMORY;
        occlusion_pass_.reset(new (std::nothrow) renderer::OcclusionCullPass());
        if (occlusion_pass_) taa_call([&] { hr = occlusion_pass_->attach(device_, native_); }, "occlusion_cull_attach");
        log("occlusion_cull_device device=%llu attached=%u reason=%s result=%08lx pool=%u", id_,
            SUCCEEDED(hr) ? 1u : 0u, occlusion_pass_ ? occlusion_pass_->reason() : "out_of_memory", hr,
            occlusion_cull::core::pool_size);
        if (FAILED(hr)) {
            release_occlusion_cull();
            occlusion_pass_failed_ = true; // one row; the device refuses queries or creation failed: off until Reset
            SetLastError(error);
            return false;
        }
        occlusion_pass_->begin_frame(frame);
    }
    if (occlusion_pass_->recreate_pending()) { // after a successful Reset: the queries, under the accounting
        HRESULT hr = S_OK;
        taa_call([&] { hr = occlusion_pass_->recreate(); }, "occlusion_cull_recreate");
        if (FAILED(hr)) {
            log("occlusion_cull_device device=%llu attached=0 reason=%s result=%08lx", id_, occlusion_pass_->reason(), hr);
            release_occlusion_cull();
            occlusion_pass_failed_ = true;
            SetLastError(error);
            return false;
        }
        occlusion_pass_->begin_frame(frame);
    }
    const std::int64_t first = call.indexed ? std::int64_t(call.base_vertex) + call.min_vertex : std::int64_t(call.first);
    const std::uint32_t count = call.indexed ? call.vertex_count : shadow_replay::vertices_of(call.topology, call.primitives);
    const std::uint64_t key = oc::draw_key(std::uint32_t(node), shadow_.stream0, std::uint32_t(first), count, model);
    HRESULT restore = S_OK;
    const auto verdict =
        occlusion_pass_->candidate(key, rect, hull, state, &restore);
    if (FAILED(restore)) {
        // The device state is not known to be the application's: the route's restore-failure path.
        ++occlusion_refused_.restore_failed;
        ++counters_.restore_failures;
        invalidate_render_states();
        if (!motion_state_lost_) {
            motion_state_lost_ = true;
            motion_state_error_ = restore;
        }
        invalidate_taa(TaaInvalidateSite::RestoreFailed);
    }
    SetLastError(error);
    if (verdict != renderer::OcclusionCullVerdict::skip) return false;
    route.submit = false;
    route.submission_error = D3D_OK;
    route.sun_color_writer = false; // not drawn: no stamp, no untracked-writer bookkeeping
    route.sun_stamp = false;
    route.unmatched = UnmatchedReason::None;
    return true;
}
// After each Present: the frame's occlusion_cull row (--debug), the session totals, and every 300 frames one
// occlusion_cull_session row in every tier (the plain flight's evidence that the cull acted).
void MotionOutput::occlusion_cull_frame_end() noexcept {
    if (!occlusion_classifier_) return;
    renderer::OcclusionCullFrameStats f{};
    if (occlusion_pass_) {
        f = occlusion_pass_->frame_stats();
        occlusion_pass_->frame_stats() = renderer::OcclusionCullFrameStats{};
    }
    const auto& r = occlusion_refused_;
    if (log_tier::cached_debug && (f.candidates || r.no_bounds || r.unbounded || r.state))
        log("occlusion_cull device=%llu frame=%llu candidates=%u tested=%u hidden=%u skipped=%u pool=%u ready=%u "
            "not_ready=%u ready_lag2=%u ready_age=%u,%u,%u errors=%u drawn_late=%u pool_truncated=%u unstable=%u "
            "no_hull=%u refused=%u failed=%u no_bounds=%u unbounded=%u state=%u",
            id_, frame_, f.candidates, f.tested, f.hidden, f.skipped, occlusion_cull::core::pool_per_frame,
            f.hidden + f.visible, f.not_ready, f.ready_lag2, f.age1, f.age2, f.age_none, f.errors, f.drawn_late,
            f.pool_truncated, f.unstable, f.no_hull, f.refused, f.failed, r.no_bounds, r.unbounded, r.state);
    occlusion_session_.add(f);
    occlusion_session_refused_.no_bounds += r.no_bounds;
    occlusion_session_refused_.unbounded += r.unbounded;
    occlusion_session_refused_.state += r.state;
    occlusion_session_refused_.restore_failed += r.restore_failed;
    occlusion_refused_ = OcclusionRefusals{};
    ++occlusion_frames_;
    if (++occlusion_window_frames_ < occlusion_cull::core::window_frames) return;
    occlusion_window_frames_ = 0;
    const auto& s = occlusion_session_;
    const auto& sr = occlusion_session_refused_;
    log("occlusion_cull_session device=%llu frame=%llu frames=%u attached=%u candidates=%u tested=%u hidden=%u "
        "skipped=%u ready=%u not_ready=%u ready_lag2=%u ready_age=%u,%u,%u errors=%u drawn_late=%u pool_truncated=%u "
        "unstable=%u no_hull=%u refused=%u failed=%u no_bounds=%u unbounded=%u state=%u restore_failed=%u resolves=%u "
        "walks=%u",
        id_, frame_, occlusion_frames_, occlusion_pass_ && occlusion_pass_->available() ? 1u : 0u, s.candidates,
        s.tested, s.hidden, s.skipped, s.hidden + s.visible, s.not_ready, s.ready_lag2, s.age1, s.age2, s.age_none,
        s.errors, s.drawn_late, s.pool_truncated,
        s.unstable, s.no_hull, s.refused, s.failed, sr.no_bounds, sr.unbounded, sr.state, sr.restore_failed,
        occlusion_classifier_->resolves, occlusion_classifier_->walks);
    occlusion_classifier_->flush_classes(); // a body id reused after a reload is re-read within 300 frames
}
