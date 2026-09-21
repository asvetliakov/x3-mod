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
        reason = sun_occlusion_pass_->caps().reason;
        sun_occlusion_attach_failed_ = FAILED(hr) || !sun_occlusion_pass_->caps().enabled;
    }
    const auto& caps = sun_occlusion_pass_->caps();
    log("sun_occlusion_device device=%llu attached=%u reason=%s result=%08lx formats=%08lx programs=%08lx slots=%u adapter_format=%u",
        id_, !sun_occlusion_attach_failed_, sun_occlusion_attach_failed_ ? reason : "ok", hr, caps.formats, caps.programs, caps.program_slots, unsigned(display.Format));
    return !sun_occlusion_attach_failed_;
}
void MotionOutput::sun_occlusion_begin() noexcept {
    lens_frame_active_ = lens_suppress_ = false;
    lens_draws_ = lens_wrapped_ = lens_refused_ = lens_dropped_ = 0;
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
    if (!skip) {
        disc = so::core::footprint(in.latch, target_width_, target_height_, sun_occlusion_.radius_scale);
        const bool same = in.latch.record == lens_record_;
        if (!disc.valid) skip = "footprint";
        else {
            // A record's first frames carry no size (accumulator 0): the radius last derived from the
            // same record, else the configured one.
            if (disc.radius_known) lens_radius_u_ = disc.radius_u;
            else so::core::apply_radius(disc, same && lens_radius_u_ > 0.f ? lens_radius_u_ : sun_occlusion_.default_radius * sun_occlusion_.radius_scale, target_width_, target_height_);
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
    if (depth) taa_call([&] { depth->Release(); });
    const bool ok = !skip && out.ran;
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
        log("sun_visibility device=%llu frame=%llu record=%08lx single=%u answered=%u ran=%u skip=%s held=%u calls=%u seeded=%u acc=%ld size=%ld x=%ld y=%ld fov=%lu scale_x=%ld u=%.5f v=%.5f radius_u=%.5f radius_v=%.5f radius_known=%u alpha=%.4f wrap=%u drop=%u readback=%08lx f_smoothed=%.4f f_used=%.4f f_raw=%.4f valid_taps=%.3f",
            id_, frame_, static_cast<unsigned long>(in.latch.record), in.single ? 1u : 0u, in.answered ? 1u : 0u, ok ? 1u : 0u, skip ? skip : "none", usable && !ok ? lens_hold_.frames : 0u, out.device_calls, out.seeded ? 1u : 0u,
            static_cast<long>(in.latch.accumulator), static_cast<long>(in.latch.size), static_cast<long>(in.latch.x), static_cast<long>(in.latch.y),
            static_cast<unsigned long>(in.latch.fov), static_cast<long>(in.latch.scale_x), double(disc.u), double(disc.v), double(disc.radius_u), double(disc.radius_v),
            disc.radius_known ? 1u : 0u, double(frame.alpha), lens_frame_active_ ? 1u : 0u, lens_suppress_ ? 1u : 0u, read, double(f[0]), double(f[1]), double(f[2]), double(f[3]));
    }
}
void MotionOutput::sun_occlusion_end() noexcept {
    if (sun_occlusion_.log && (lens_draws_ || lens_frame_active_ || lens_suppress_))
        log("sun_lens_bracket device=%llu frame=%llu draws=%u wrapped=%u refused=%u dropped=%u variants=%u", id_, frame_, lens_draws_, lens_wrapped_, lens_refused_, lens_dropped_,
            sun_occlusion_pass_ ? sun_occlusion_pass_->variants() : 0u);
    lens_frame_active_ = lens_suppress_ = false;
}
// One lens-scene draw, after before_draw returned with the application's bindings on the device
// (an unrouted draw: restore_bindings_checked ran). Integer only: the draw hooks' light CPU
// boundary reaches this (the log formats integers and goes through log()'s own full boundary).
// The draw's state comes from the route's shadow (render_state / blend_known: a hit is a load, a
// miss one native read that refills the shadow); the pass itself issues no getter.
void MotionOutput::prepare_lens(const MotionDrawCall& call, MotionRoute& route) noexcept {
    ++lens_draws_;
    renderer::LensVerdict verdict = renderer::LensVerdict::NotReady;
    renderer::LensState state{};
    const bool plain = route.submit && !route.routed && !route.composition && !route.fog_card_mask.masked && !route.source_gain && !route.hull_gain && !route.screen_additive;
    const char* refusal = nullptr;
    if ((lens_frame_active_ || sun_occlusion_.log) && !shadow_.recording) {
        namespace core = sun_occlusion::core;
        DWORD v[6]{};
        static constexpr D3DRENDERSTATETYPE indexed[6] = {D3DRS_ALPHABLENDENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHAREF, D3DRS_ALPHAFUNC, D3DRS_FOGENABLE};
        bool known = blend_known(0) && blend_known(1) && blend_known(2);
        for (unsigned i = 0; i < 6 && known; ++i) known = SUCCEEDED(render_state(indexed[i], &v[i]));
        state.known = known;
        state.blend = core::BlendState{v[0], shadow_.composition_blend[0], shadow_.composition_blend[1], shadow_.composition_blend[2], v[1], v[2], v[3], v[4], v[5]};
        state.shader = shadow_.ps; state.hash = shadow_.ps_hash;
    }
    if (lens_frame_active_ && route.submit) {
        if (!plain) refusal = "routed_draw";
        else if (!state.known) refusal = "state_unknown";
        else {
            taa_call([&] { verdict = sun_occlusion_pass_->lens_begin(state, route.lens); });
            if (verdict != renderer::LensVerdict::Applied) refusal = verdict == renderer::LensVerdict::Variant ? sun_occlusion_pass_->last_variant_refusal() : renderer::lens_verdict_name(verdict);
        }
        if (!refusal) ++lens_wrapped_;
        else {
            // The fraction is on the GPU only, so this frame cannot know whether f is 1. A draw that cannot
            // carry it is therefore never shown at full strength: it and the rest of the bracket are dropped
            // for this frame (the elements already drawn were scaled), and the engine's probe decides from
            // the next frame on. A refusal that belongs to the program or the material (fixed function, the
            // blend law, fog, the wrap, a routed draw) blocks for the process, so a Reset cannot replay it;
            // a transient one (unknown state, device error, no fraction) costs this frame and the next.
            ++lens_refused_;
            lens_frame_active_ = false; lens_suppress_ = true;
            const bool transient = !state.known || verdict == renderer::LensVerdict::Device || (plain && verdict == renderer::LensVerdict::NotReady);
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
    log("sun_lens_draw device=%llu frame=%llu index=%lu lens_index=%u vs=%016llx ps=%016llx ps_major=%u verdict=%s submit=%u indexed=%u up=%u topology=%u primitives=%u vertices=%u state_known=%u blend=%lu src=%lu dst=%lu op=%lu srgbwrite=%lu alphatest=%lu alpharef=%lu alphafunc=%lu fog=%lu z=%lu zwrite=%lu zfunc=%lu colorwrite=%lx tex0=%08lx tex0_size=%lux%lu tex0_format=%lu tex1=%08lx tex1_size=%lux%lu tex1_format=%lu rt0=%08lx rt0_size=%ux%u rt0_format=%u",
        id_, frame_, static_cast<unsigned long>(counters_.draws), lens_draws_ - 1u, static_cast<unsigned long long>(shadow_.vs_hash), static_cast<unsigned long long>(shadow_.ps_hash), unsigned(shadow_.ps_major),
        refusal ? refusal : renderer::lens_verdict_name(verdict), route.submit ? 1u : 0u, call.indexed ? 1u : 0u, call.user_memory ? 1u : 0u, unsigned(call.topology), call.primitives, call.vertex_count,
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
