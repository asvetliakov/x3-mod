// Partial sun occlusion, step 1 (docs/architecture/sun-partial-occlusion.md): the route's side of
// the lens bracket. Included inside namespace x3m by motion_output.cpp.
//
// sun_occlusion_begin / _end run on the render thread inside the engine's `call 0x0047e6e0` for the
// lens scene (src/proxy/sun_occlusion.h), under the capture lock and a full CPU-state boundary,
// after the scene end of the same frame: RT2 is complete and no routed draw follows. Outside the
// bracket the draw hooks pay one flag test (sun_occlusion::bracket_open()).
bool MotionOutput::ensure_sun_occlusion() noexcept {
    if (sun_occlusion_pass_ && sun_occlusion_pass_->caps().enabled) return true;
    if (sun_occlusion_attach_failed_) return false;
    sun_occlusion_attach_failed_ = true;
    if (!sun_occlusion_pass_) { try { sun_occlusion_pass_ = std::make_unique<renderer::SunOcclusionPass>(); } catch (...) { return false; } }
    D3DDISPLAYMODE display{};
    HRESULT hr = native<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &display);
    const char* reason = "adapter_query";
    if (SUCCEEDED(hr)) {
        taa_call([&] { hr = sun_occlusion_pass_->attach(device_, native_, caps_, display.Format); });
        sun_occlusion_pass_->set_core_fraction(sun_occlusion_.core_fraction);
        reason = sun_occlusion_pass_->caps().reason;
        sun_occlusion_attach_failed_ = FAILED(hr) || !sun_occlusion_pass_->caps().enabled;
    }
    const auto& caps = sun_occlusion_pass_->caps();
    log("sun_occlusion_device device=%llu attached=%u reason=%s result=%08lx formats=%08lx programs=%08lx slots=%u adapter_format=%u",
        id_, !sun_occlusion_attach_failed_, sun_occlusion_attach_failed_ ? reason : "ok", hr, caps.formats, caps.programs, caps.program_slots, unsigned(display.Format));
    return !sun_occlusion_attach_failed_;
}
void MotionOutput::release_lens_depth() noexcept {
    if (!lens_depth_) return;
    IDirect3DTexture9* const depth = lens_depth_;
    lens_depth_ = nullptr; lens_depth_width_ = lens_depth_height_ = 0;
    taa_call([&] { depth->Release(); });
}
// The sun shadow lane's direction (the frame's validated LightDir_Dir0, world space, object -> light) projected with the
// scene camera to back-buffer uv: an independent cross-check of the record's uv in the log. False when either is
// unavailable or the sun is behind the camera.
static bool sun_lane_uv(const shadow_replay::SunLatch& latch, const renderer::CameraState& camera, float* u, float* v) noexcept {
    if (!latch.valid || !camera.valid) return false;
    float view[3] = {0.f, 0.f, 0.f};
    for (unsigned i = 0; i < 3; ++i) for (unsigned j = 0; j < 3; ++j) view[j] += latch.sun[i] * camera.r[i * 3 + j];
    if (!(view[2] > 1e-6f)) return false;
    const float x = (view[0] * camera.m00 + view[2] * camera.m20) / view[2], y = (view[1] * camera.m11 + view[2] * camera.m21) / view[2];
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    *u = .5f + .5f * x; *v = .5f - .5f * y;
    return true;
}
void MotionOutput::sun_occlusion_begin() noexcept {
    lens_frame_active_ = lens_suppress_ = false;
    lens_draws_ = lens_wrapped_ = lens_clipped_ = lens_refused_ = lens_dropped_ = lens_other_ = 0;
    release_lens_depth();
    if (!sun_occlusion_.requested && !sun_occlusion_.log) return;
    namespace so = sun_occlusion;
    const so::FrameInputs in = so::frame_inputs();
    const char* skip = nullptr;
    IDirect3DTexture9* depth = nullptr;
    if (!enabled_) skip = "route";
    else if (!in.single) skip = "record";                                   // no sun, or more than one in the main view
    else if (!counters_.hook_scene_end) skip = "scene";                    // the scene end has not run in this frame: RT2 is not this frame's
    else if (!counters_.filled || !depth_surface_ || !depth_enabled_) skip = "depth";
    else if (shadow_.recording) skip = "recording";
    else if (active_queries_) skip = "queries";
    else if (motion_state_lost_ || composition_state_lost_) skip = "state";
    else if (!ensure_sun_occlusion()) skip = "attach";
    else if (sun_occlusion_pass_->reset_pending()) skip = "reset_pending";
    else if (FAILED(depth_surface_->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&depth))) || !depth) skip = "depth_container";
    so::core::Footprint disc{};
    renderer::SunVisibilityResult out{};
    renderer::SunVisibilityFrame frame{};
    HRESULT hr = S_FALSE;
    float radius_derived_u = 0.f;
    if (!skip) {
        disc = so::core::footprint(in.latch, target_width_, target_height_, 1.f);
        const bool same = in.latch.record == lens_record_;
        if (!disc.valid) skip = "footprint";
        else {
            // The engine's record size saturates for the sun (run223), so the disc radius is the configured
            // absolute one; the derived value, where the size is not saturated, is logged beside it.
            radius_derived_u = disc.radius_known ? disc.radius_u : 0.f;
            so::core::apply_radius(disc, sun_occlusion_.radius_u, target_width_, target_height_);
            LARGE_INTEGER now{}, frequency{};
            QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
            const double dt = lens_pass_qpc_ && frequency.QuadPart > 0 ? double(std::uint64_t(now.QuadPart) - lens_pass_qpc_) / double(frequency.QuadPart) : 0.;
            frame.depth = depth; frame.u = disc.u; frame.v = disc.v; frame.radius_u = disc.radius_u; frame.radius_v = disc.radius_v;
            frame.alpha = so::core::smoothing_alpha(dt); frame.curve = sun_occlusion_.curve;
            frame.seed = !same || !sun_occlusion_pass_->valid() || dt <= 0. || dt > .5; // a new record, a Reset, a failed pass or a gap: no smoothing against a stale fraction
            frame.caller_scene_open = scene_open_; frame.caller_stateblock_recording = shadow_.recording;
            taa_call([&] { hr = sun_occlusion_pass_->execute(frame, &out); });
            if (out.ran) { lens_record_ = in.latch.record; lens_pass_qpc_ = std::uint64_t(now.QuadPart); }
            else skip = out.skipped ? out.skipped_reason : "failed";
        }
    }
    const bool ok = !skip && out.ran;
    // Step 2: RT2 stays referenced for the bracket (the core bodies' clip source; also in a held frame, whose RT2 is
    // complete) with its size and the sun's uv.
    if (depth) {
        D3DSURFACE_DESC desc{};
        if (SUCCEEDED(depth->GetLevelDesc(0, &desc)) && desc.Width && desc.Height) { lens_depth_ = depth; depth = nullptr; lens_depth_width_ = desc.Width; lens_depth_height_ = desc.Height; }
    }
    lens_sun_u_ = disc.u; lens_sun_v_ = disc.v;
    if (depth) taa_call([&] { depth->Release(); });
    // A transient skip keeps the last smoothed fraction in use for a bounded number of frames
    // (core::Hold) instead of dropping the chain and handing two frames to the vanilla probe.
    const bool holdable = !ok && !FAILED(hr) && in.single && in.latch.record == lens_record_ && sun_occlusion_pass_ && sun_occlusion_pass_->valid();
    const bool usable = lens_hold_.step(ok, holdable);
    if (!usable && sun_occlusion_pass_) sun_occlusion_pass_->invalidate();
    so::report_pass(usable);
    // Failure direction (design): where the override kept the chain alive in this frame and no
    // fraction exists, the chain is dropped for the frame, never shown at full strength.
    lens_frame_active_ = sun_occlusion_.requested && in.answered && usable;
    lens_suppress_ = sun_occlusion_.requested && in.answered && !usable;
    if (FAILED(hr) && logged_failures_ < failure_log_limit) {
        ++logged_failures_;
        log("sun_occlusion_failed device=%llu frame=%llu stage=%u operation=%08lx restore=%08lx", id_, frame_, unsigned(out.failed), out.operation, out.restore);
    }
    if (FAILED(out.restore) && out.restore != S_FALSE && !motion_state_lost_) { motion_state_lost_ = true; motion_state_error_ = out.restore; invalidate_taa(TaaInvalidateSite::RestoreFailed); }
    if (sun_occlusion_.log) {
        float f[4] = {-1.f, -1.f, -1.f, -1.f};
        HRESULT read = S_FALSE;
        if (ok) taa_call([&] { read = sun_occlusion_pass_->readback(f); }); // diagnostic only: a synchronous 1x1 GetRenderTargetData
        float lane_u = 0.f, lane_v = 0.f;
        const bool lane = sun_lane_uv(sun_latch_, camera_scene_, &lane_u, &lane_v);
        log("sun_visibility device=%llu frame=%llu record=%08lx owner=%08lx owner_layer=%ld owner_flags270=%08lx single=%u answered=%u ran=%u skip=%s held=%u calls=%u seeded=%u acc=%ld size=%ld saturated=%u x=%ld y=%ld fov=%lu scale_x=%ld u=%.5f v=%.5f lane=%u lane_u=%.5f lane_v=%.5f radius_u=%.5f radius_v=%.5f radius_px=%.1f radius_derived_u=%.5f alpha=%.4f wrap=%u drop=%u readback=%08lx f_smoothed=%.4f f_used=%.4f f_raw=%.4f valid_taps=%.3f",
            id_, frame_, static_cast<unsigned long>(in.latch.record), static_cast<unsigned long>(in.latch.owner), static_cast<long>(in.latch.owner_layer), static_cast<unsigned long>(in.latch.owner_flags),
            in.single ? 1u : 0u, in.answered ? 1u : 0u, ok ? 1u : 0u, skip ? skip : "none", usable && !ok ? lens_hold_.frames : 0u, out.device_calls, out.seeded ? 1u : 0u,
            static_cast<long>(in.latch.accumulator), static_cast<long>(in.latch.size), disc.saturated ? 1u : 0u, static_cast<long>(in.latch.x), static_cast<long>(in.latch.y),
            static_cast<unsigned long>(in.latch.fov), static_cast<long>(in.latch.scale_x), double(disc.u), double(disc.v), lane ? 1u : 0u, double(lane_u), double(lane_v),
            double(disc.radius_u), double(disc.radius_v), double(disc.radius_u * float(target_width_)), double(radius_derived_u),
            double(frame.alpha), lens_frame_active_ ? 1u : 0u, lens_suppress_ ? 1u : 0u, read, double(f[0]), double(f[1]), double(f[2]), double(f[3]));
    }
}
void MotionOutput::sun_occlusion_end() noexcept {
    if (sun_occlusion_.log && (lens_draws_ || lens_frame_active_ || lens_suppress_))
        log("sun_lens_bracket device=%llu frame=%llu draws=%u wrapped=%u clipped=%u other=%u refused=%u dropped=%u variants=%u pairs=%u", id_, frame_, lens_draws_, lens_wrapped_, lens_clipped_, lens_other_, lens_refused_, lens_dropped_,
            sun_occlusion_pass_ ? sun_occlusion_pass_->variants() : 0u, sun_occlusion_pass_ ? sun_occlusion_pass_->pairs() : 0u);
    lens_chain_drawn_ = lens_chain_drawn_ || lens_draws_ != 0;
    lens_frame_active_ = lens_suppress_ = false;
    release_lens_depth();
}
// --sun-occlusion-log, capture frames: the presented back buffer, which holds the lens chain (drawn after the scene end
// and every other readback of the frame), as lens_<device>_<frame>.bgra8. 32-bit formats only; nothing else runs.
void MotionOutput::sun_lens_present_readback() noexcept {
    if (!sun_occlusion_.log || !capture_ || !lens_chain_drawn_ || !device_) return;
    IDirect3DSurface9* rt0 = nullptr;
    if (FAILED(native<GetRenderTargetFn>(GetRenderTarget)(device_, 0, &rt0)) || !rt0) return;
    D3DSURFACE_DESC desc{};
    if (SUCCEEDED(rt0->GetDesc(&desc)) && (desc.Format == D3DFMT_X8R8G8B8 || desc.Format == D3DFMT_A8R8G8B8) && desc.MultiSampleType == D3DMULTISAMPLE_NONE)
        readback_surface(rt0, desc.Format, 4, L"lens", L"bgra8", "sun_lens_readback", "bgra8_row_major", desc.Width, desc.Height);
    else log("sun_lens_readback device=%llu frame=%llu result=%08lx format=%u multisample=%u note=unsupported_back_buffer", id_, frame_, static_cast<unsigned long>(E_NOTIMPL), unsigned(desc.Format), unsigned(desc.MultiSampleType));
    rt0->Release();
}
// One lens-scene draw, after before_draw returned with the application's bindings on the device
// (an unrouted draw: restore_bindings_checked ran). No x87: the draw hooks' light CPU boundary
// reaches this (the body classification is SSE float; the log goes through log()'s own full boundary).
// The draw's state comes from the route's shadow (render_state / blend_known: a hit is a load, a
// miss one native read that refills the shadow); the pass itself issues no getter.
void MotionOutput::prepare_lens(const MotionDrawCall& call, MotionRoute& route) noexcept {
    namespace core = sun_occlusion::core;
    ++lens_draws_;
    renderer::LensVerdict verdict = renderer::LensVerdict::NotReady;
    renderer::LensState state{};
    const bool plain = route.submit && !route.routed && !route.composition && !route.fog_card_mask.masked && !route.source_gain && !route.hull_gain && !route.screen_additive;
    const char* refusal = nullptr;
    if ((lens_frame_active_ || sun_occlusion_.log) && !shadow_.recording) {
        DWORD v[6]{};
        static constexpr D3DRENDERSTATETYPE indexed[6] = {D3DRS_ALPHABLENDENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHAREF, D3DRS_ALPHAFUNC, D3DRS_FOGENABLE};
        bool known = blend_known(0) && blend_known(1) && blend_known(2);
        for (unsigned i = 0; i < 6 && known; ++i) known = SUCCEEDED(render_state(indexed[i], &v[i]));
        state.known = known;
        state.blend = core::BlendState{v[0], shadow_.composition_blend[0], shadow_.composition_blend[1], shadow_.composition_blend[2], v[1], v[2], v[3], v[4], v[5]};
        state.shader = shadow_.ps; state.hash = shadow_.ps_hash;
        state.vertex_shader = shadow_.vs; state.vertex_hash = shadow_.vs_hash;
        state.depth = lens_depth_; state.depth_width = lens_depth_width_; state.depth_height = lens_depth_height_;
    }
    // Step 2: which body this draw is, decided BEFORE any wrap is built. lens_prepare only scans the vertex program
    // (once per pair, no device object): its matrix register K and whether the local origin maps to (cK.w .. cK+3.w),
    // read from the route's clip-row shadow of that window and compared with the sun's uv (core::classify_body).
    // Fail closed towards "not ours": a vertex program that is not the four-dp4 shape, whose origin is not the rows'
    // .w column, or whose rows are in no shadowed window cannot be the fingerprinted sun program; its body is
    // Other (untouched, never wrapped, never blocking), logged once per pair. Only a transient state (the rows not
    // set yet in this device generation, a centre behind the camera) is a refusal, and a transient one.
    core::BodyCentre centre{};
    renderer::SunOcclusionPass::Prepared prepared{};
    std::size_t window = motion_matrix_windows_max;
    bool rows_known = false, body_transient = false;
    const char* body_refusal = nullptr; const char* body_other = nullptr;
    if ((lens_frame_active_ || sun_occlusion_.log) && state.known && state.shader && state.vertex_shader && sun_occlusion_pass_ && !shadow_.recording) {
        renderer::LensVerdict scanned = renderer::LensVerdict::NotReady;
        taa_call([&] { scanned = sun_occlusion_pass_->lens_prepare(state, &prepared); });
        if (scanned == renderer::LensVerdict::Applied) {
            window = window_of(prepared.matrix_register);
            rows_known = window < motion_matrix_windows_max && shadow_.rows_known[window];
            if (!prepared.origin_known) body_other = "origin_unknown";
            else if (window >= motion_matrix_windows_max) body_other = "matrix_window";
            else if (!rows_known) { body_refusal = "rows_unknown"; body_transient = true; }
            else {
                centre = core::classify_body(shadow_.rows[window], true, lens_sun_u_, lens_sun_v_, target_height_ ? float(target_width_) / float(target_height_) : 0.f);
                if (centre.body == core::Body::Unknown) { body_refusal = "body_unknown"; body_transient = true; }
            }
        } else if (scanned == renderer::LensVerdict::Variant) body_other = sun_occlusion_pass_->last_variant_refusal();
        else if (scanned == renderer::LensVerdict::CacheFull) body_other = "cache_full";
        else { body_refusal = renderer::lens_verdict_name(scanned); body_transient = scanned == renderer::LensVerdict::NotReady; }
        if (body_other) {
            centre.body = core::Body::Other;
            if (prepared.first || scanned == renderer::LensVerdict::CacheFull)
                log("sun_lens_body_unclassifiable device=%llu frame=%llu vs=%016llx ps=%016llx reason=%s matrix_register=%ld note=left_untouched", id_, frame_,
                    static_cast<unsigned long long>(state.vertex_hash), static_cast<unsigned long long>(state.hash), body_other,
                    prepared.matrix_register == ~0u ? -1l : long(prepared.matrix_register));
        }
        state.body = centre.body;
    }
    if (lens_frame_active_ && route.submit) {
        if (!plain) refusal = "routed_draw";
        else if (!state.known) refusal = "state_unknown";
        else if (body_refusal) refusal = body_refusal;
        else if (state.body == core::Body::Other) { verdict = renderer::LensVerdict::Body; ++lens_other_; }
        else {
            taa_call([&] { verdict = sun_occlusion_pass_->lens_begin(state, route.lens); });
            if (verdict != renderer::LensVerdict::Applied) refusal = verdict == renderer::LensVerdict::Variant ? sun_occlusion_pass_->last_variant_refusal() : renderer::lens_verdict_name(verdict);
        }
        if (!refusal) { if (state.body != core::Body::Other) ++lens_wrapped_; if (route.lens.clipped) ++lens_clipped_; }
        else {
            // The fraction is on the GPU only, so this frame cannot know whether f is 1. A draw that cannot
            // carry it is therefore never shown at full strength: it and the rest of the bracket are dropped
            // for this frame (the elements already drawn were scaled), and the engine's probe decides from
            // the next frame on. A refusal that belongs to the program or the material (fixed function, the
            // blend law, fog, the wrap, a routed draw) blocks for the process, so a Reset cannot replay it;
            // a transient one (unknown state, device error, no fraction) costs this frame and the next.
            ++lens_refused_;
            lens_frame_active_ = false; lens_suppress_ = true;
            const bool transient = !state.known || body_transient || verdict == renderer::LensVerdict::Device || (plain && verdict == renderer::LensVerdict::NotReady);
            if (transient && plain) sun_occlusion::report_pass(false); else sun_occlusion::block(refusal);
        }
    }
    if (lens_suppress_ && route.submit) { route.submit = false; route.submission_error = D3D_OK; ++lens_dropped_; }
    if (!sun_occlusion_.log) return;
    DWORD z[4]{};
    static constexpr D3DRENDERSTATETYPE depth_states[4] = {D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC, D3DRS_COLORWRITEENABLE};
    for (unsigned i = 0; i < 4; ++i) render_state(depth_states[i], &z[i]);
    unsigned long texture_id[2]{}, texture_w[2]{}, texture_h[2]{}, texture_format[2]{};
    for (DWORD stage = 0; stage < 2; ++stage) { // the shadow's stage textures; GetLevelDesc is a resource call, not a device getter
        IDirect3DBaseTexture9* const bound = samplers_[stage].texture;
        if (!bound) continue;
        texture_id[stage] = static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(bound));
        D3DSURFACE_DESC desc{};
        if (bound->GetType() == D3DRTYPE_TEXTURE && SUCCEEDED(static_cast<IDirect3DTexture9*>(bound)->GetLevelDesc(0, &desc))) { texture_w[stage] = desc.Width; texture_h[stage] = desc.Height; texture_format[stage] = desc.Format; }
    }
    IDirect3DSurface9* rt0 = nullptr; D3DSURFACE_DESC target{};
    if (SUCCEEDED(native<GetRenderTargetFn>(GetRenderTarget)(device_, 0, &rt0)) && rt0) { rt0->GetDesc(&target); rt0->Release(); }
    static constexpr const char* body_names[4] = {"unknown", "core", "ghost", "other"};
    log("sun_lens_draw device=%llu frame=%llu index=%lu lens_index=%u vs=%016llx ps=%016llx ps_major=%u verdict=%s body=%s other_reason=%s clipped=%u matrix_register=%ld rows_known=%u centre_u=%.5f centre_v=%.5f centre_dist_u=%.5f sun_u=%.5f sun_v=%.5f submit=%u indexed=%u up=%u topology=%u primitives=%u vertices=%u state_known=%u blend=%lu src=%lu dst=%lu op=%lu srgbwrite=%lu alphatest=%lu alpharef=%lu alphafunc=%lu fog=%lu z=%lu zwrite=%lu zfunc=%lu colorwrite=%lx tex0=%08lx tex0_size=%lux%lu tex0_format=%lu tex1=%08lx tex1_size=%lux%lu tex1_format=%lu rt0=%08lx rt0_size=%ux%u rt0_format=%u",
        id_, frame_, static_cast<unsigned long>(counters_.draws), lens_draws_ - 1u, static_cast<unsigned long long>(shadow_.vs_hash), static_cast<unsigned long long>(shadow_.ps_hash), unsigned(shadow_.ps_major),
        refusal ? refusal : state.body == core::Body::Other && verdict == renderer::LensVerdict::Body ? "other" : body_refusal ? body_refusal : renderer::lens_verdict_name(verdict),
        body_names[unsigned(centre.body) & 3u], body_other ? body_other : "none", route.lens.clipped ? 1u : 0u, prepared.matrix_register == ~0u ? -1l : long(prepared.matrix_register), rows_known ? 1u : 0u,
        double(centre.u), double(centre.v), double(centre.distance_u), double(lens_sun_u_), double(lens_sun_v_),
        route.submit ? 1u : 0u, call.indexed ? 1u : 0u, call.user_memory ? 1u : 0u, unsigned(call.topology), call.primitives, call.vertex_count,
        state.known ? 1u : 0u, static_cast<unsigned long>(state.blend.enable), static_cast<unsigned long>(state.blend.src), static_cast<unsigned long>(state.blend.dst), static_cast<unsigned long>(state.blend.op),
        static_cast<unsigned long>(state.blend.srgb_write), static_cast<unsigned long>(state.blend.alpha_test), static_cast<unsigned long>(state.blend.alpha_ref), static_cast<unsigned long>(state.blend.alpha_func),
        static_cast<unsigned long>(state.blend.fog), z[0], z[1], z[2], z[3],
        texture_id[0], texture_w[0], texture_h[0], texture_format[0], texture_id[1], texture_w[1], texture_h[1], texture_format[1],
        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(rt0)), unsigned(target.Width), unsigned(target.Height), unsigned(target.Format));
}
void MotionOutput::finish_lens(MotionRoute& route) noexcept {
    HRESULT hr = S_OK;
    taa_call([&] { hr = sun_occlusion_pass_ ? sun_occlusion_pass_->lens_end(route.lens) : S_OK; });
    if (SUCCEEDED(hr)) return;
    if (!motion_state_lost_) { motion_state_lost_ = true; motion_state_error_ = hr; }
    ++counters_.restore_failures; invalidate_taa(TaaInvalidateSite::RestoreFailed);
    sun_occlusion::block("restore_failed");
    if (logged_failures_ < failure_log_limit) {
        ++logged_failures_;
        log("motion_output_restore_failed device=%llu frame=%llu index=%lu result=%08lx what=sun_lens", id_, frame_, counters_.draws, hr);
    }
}
