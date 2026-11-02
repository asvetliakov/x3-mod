// The route's side of the lens bracket (src/proxy/own_sun_hook.h): the lens-flare gain and its fog transmittance.
// Included inside namespace x3m by motion_output.cpp.
//
// lens_bracket_begin runs on the render thread inside the engine's `call 0x0047e6e0` for the lens scene (FL: the lens
// draw at base+0x8bcb7), under the capture lock and a full CPU-state boundary, after the scene end of the same frame.
// Outside the bracket the draw hooks pay one flag test (own_sun_hook::bracket_open()); inside it, prepare_lens is the
// gain's per-draw step only (the lens wraps, scan and visibility pass of the partial sun occlusion were deleted in own
// sun step 2, docs/architecture/own-sun-pass.md section 14).
void MotionOutput::release_lens_depth() noexcept {
    if (!lens_depth_) return;
    IDirect3DTexture9* const depth = lens_depth_;
    lens_depth_ = nullptr;
    taa_call([&] { depth->Release(); }, "lens_depth_release");
}
// RT2's container (null: no RT2 or the container is unreadable), now acquired only by the retention fixture's seam
// below (the bracket's RT2 reference went with the step-2 deletion; the seam keeps the accounting case of run336).
// Under the ownership wrapper GetContainer creates a fresh device-referencing texture node (create_target released the
// texture's own node after GetSurfaceLevel), which release_lens_depth drops under taa_call: the acquisition runs under
// the same accounting, so taa_references_ carries +1 for the node's lifetime and -1 at its release
// (shadow-caster-retention.md, "Final-Release probe drift (2026-09-25)").
IDirect3DTexture9* MotionOutput::acquire_lens_depth() noexcept {
    IDirect3DTexture9* depth = nullptr;
    if (!depth_surface_) return nullptr;
    taa_call([&] { if (FAILED(depth_surface_->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&depth)))) depth = nullptr; }, "lens_depth_acquire");
    return depth;
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
// The seam's mid-session lifecycle of an RT2 reference: 1 = the accounted acquisition after releasing the previous one
// (begin), 0 = release (end); 2 = the acquisition outside the accounting (main's shape before run336: its release under
// taa_call is one-sided and drives taa_references_ down, the clamp's test); 3 = the reverse one-sided shape (acquired
// under the accounting, released outside), which restores one. Returns 1 while a reference is held.
int MotionOutput::fixture_lens_depth(unsigned mode) noexcept {
    release_lens_depth();
    if (mode == 1) lens_depth_ = acquire_lens_depth();
    else if (mode == 2 && depth_surface_ && FAILED(depth_surface_->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&lens_depth_)))) lens_depth_ = nullptr;
    else if (mode == 3) { IDirect3DTexture9* const depth = acquire_lens_depth(); if (depth) depth->Release(); }
    return lens_depth_ ? 1 : 0;
}
#endif
void MotionOutput::lens_bracket_begin() noexcept {
    // Lens-flare gain through the fog (both editions; only with the volumetric fog configured): this bracket's draws
    // take G x T_sun, T_sun the frame's fog transmittance toward the sky at the look's column cap (the engine plumes'
    // law, engine_plumes_fog); T = 1 and G exactly in a frame without the density composite. Fixed for the bracket
    // (the per-draw restore replays the same law); the lens_flare_gain_frame row carries it.
    if (!lens_gain_fog_) return;
    renderer::FogTransmittanceLaw law{};
    engine_plumes_fog(&law);
    float applied = lens_gain_base_;
    renderer::fog_sky_lens_gain(law, lens_gain_base_, &lens_gain_fog_t_, &applied);
    if (applied != lens_gain_.gain) {
        lens_gain_ = lens_flare_gain::law(applied);
        lens_gain_caps_ = lens_gain_.active && (!lens_gain_.constant || lens_gain_blendfactor_);
    }
}
// One lens-scene draw, after before_draw returned with the application's bindings on the device (an unrouted draw:
// restore_bindings_checked ran): the lens-flare gain only. Integer only, no device getter: a draw another feature owns
// is refused by the gain itself.
void MotionOutput::prepare_lens(MotionRoute& route) noexcept {
    if (!lens_gain_.active) return;
    ++lens_gain_window_draws_;
    if (!route.submit) return;
    const bool plain = !route.routed && !route.composition && !route.fog_card_mask.masked && !route.source_gain && !route.hull_gain && !route.screen_additive;
    apply_lens_gain(route, plain);
}
// Lens-flare gain (src/proxy/lens_flare_gain.h), one lens-scene draw that goes out: SRCBLEND = BLENDFACTOR with the
// quantised G in every lane (at G = 0 the draw is not submitted), on the admitted ONE / ONE / ADD law only. Everything is checked before the
// first setter; the values put back after the draw are the route's shadow of the application's own (blend_known: the
// setter hooks' shadow, or this draw's Get* cache with the hooks off, which begin_draw_reads refreshed for this draw
// because the route is on). Direct calls: the setter hook's shadow keeps the application's values. Integer only.
void MotionOutput::refuse_lens_gain(unsigned reason) noexcept {
    if (reason < lens_flare_gain::refusal_count) ++lens_gain_window_refused_[reason];
    if (reason >= lens_flare_gain::refusal_count || (lens_gain_logged_ & (1u << reason))) return;
    lens_gain_logged_ |= 1u << reason;
    log("lens_flare_gain_refused device=%llu frame=%llu index=%lu reason=%s src=%lu dst=%lu op=%lu note=draw_left_native", id_, frame_,
        static_cast<unsigned long>(counters_.draws), lens_flare_gain::refusal_name(reason),
        static_cast<unsigned long>(shadow_.composition_blend_known[0] ? shadow_.composition_blend[0] : 0),
        static_cast<unsigned long>(shadow_.composition_blend_known[1] ? shadow_.composition_blend[1] : 0),
        static_cast<unsigned long>(shadow_.composition_blend_known[2] ? shadow_.composition_blend[2] : 0));
}
void MotionOutput::apply_lens_gain(MotionRoute& route, bool plain) noexcept {
    namespace lfg = lens_flare_gain;
    if (!lens_gain_caps_) return refuse_lens_gain(lfg::Caps);
    if (!enabled_ || !device_) return refuse_lens_gain(lfg::Route);         // no fresh state shadow without the route
    if (shadow_.recording) return refuse_lens_gain(lfg::Recording);        // a setter would be recorded, not executed
    if (!plain || route.lens_gain) return refuse_lens_gain(lfg::RoutedDraw); // another feature owns this draw's blend
    DWORD enable = 0;
    if (!blend_known(0) || !blend_known(1) || !blend_known(2) || (lens_gain_.constant && !blend_known(7)) ||
        FAILED(render_state(D3DRS_ALPHABLENDENABLE, &enable)))
        return refuse_lens_gain(lfg::StateUnknown);
    if (!lfg::admits(enable, shadow_.composition_blend[0], shadow_.composition_blend[1], shadow_.composition_blend[2]))
        return refuse_lens_gain(lfg::BlendLaw);
    if (lens_gain_.skip) {
        // G = 0: 0 * src + dst is dst, so the admitted draw is not submitted at all. The hook returns S_OK to the game
        // without the native call; no state was changed, after_draw restores only what the route applied.
        route.submit = false;
        route.submission_error = D3D_OK;
        ++lens_gain_window_skipped_;
        return;
    }
    D3DRENDERSTATETYPE states[2]{};
    DWORD values[2]{};
    const unsigned count = lfg::steps(lens_gain_, states, values);
    for (unsigned i = 0; i < count; ++i) {
        if (FAILED(direct_call<SetRenderStateFn>(SetRenderState, states[i], values[i]))) {
            restore_lens_gain(route); // exactly what was applied; the draw goes out native
            return refuse_lens_gain(lfg::Device);
        }
        route.lens_gain = std::uint8_t(i + 1);
    }
    ++lens_gain_window_gained_;
}
// Every 300 frames while G < 1, --perf/--debug since 2026-10-21 (the window's counters reset in every tier).
void MotionOutput::log_lens_gain_window() noexcept {
    const unsigned* r = lens_gain_window_refused_;
    if (log_tier::perf_rows())
    log("lens_flare_gain_frame device=%llu frame=%llu frames=%u gain=%.4f base=%.4f fog=%u fog_t=%.4f draws=%u gained=%u skipped=%u refused_caps=%u refused_route=%u refused_recording=%u refused_routed_draw=%u refused_state_unknown=%u refused_blend_law=%u refused_device=%u",
        id_, frame_, lens_gain_window_frames_, double(lens_gain_.gain), double(lens_gain_base_), unsigned(lens_gain_fog_), double(lens_gain_fog_t_),
        lens_gain_window_draws_, lens_gain_window_gained_, lens_gain_window_skipped_,
        r[lens_flare_gain::Caps], r[lens_flare_gain::Route], r[lens_flare_gain::Recording], r[lens_flare_gain::RoutedDraw],
        r[lens_flare_gain::StateUnknown], r[lens_flare_gain::BlendLaw], r[lens_flare_gain::Device]);
    lens_gain_window_frames_ = lens_gain_window_draws_ = lens_gain_window_gained_ = lens_gain_window_skipped_ = 0;
    for (unsigned& v : lens_gain_window_refused_) v = 0;
}
void MotionOutput::restore_lens_gain(MotionRoute& route) noexcept {
    D3DRENDERSTATETYPE states[2]{};
    DWORD values[2]{};
    lens_flare_gain::steps(lens_gain_, states, values);
    HRESULT first = S_OK;
    for (unsigned i = route.lens_gain; i; --i) {
        const unsigned index = composition_blend_index(states[i - 1]);
        const HRESULT hr = direct_call<SetRenderStateFn>(SetRenderState, states[i - 1], shadow_.composition_blend[index]);
        if (SUCCEEDED(first) && FAILED(hr)) first = hr;
    }
    route.lens_gain = 0;
    if (SUCCEEDED(first)) return;
    if (!motion_state_lost_) { motion_state_lost_ = true; motion_state_error_ = first; }
    ++counters_.restore_failures; invalidate_taa(TaaInvalidateSite::RestoreFailed);
    if (logged_failures_ < failure_log_limit) {
        ++logged_failures_;
        log("motion_output_restore_failed device=%llu frame=%llu index=%lu result=%08lx what=lens_flare_gain", id_, frame_, counters_.draws, first);
    }
}
