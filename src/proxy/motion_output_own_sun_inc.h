// Own sun, step 1 (included by motion_output.cpp inside namespace x3m, both editions; own_sun_core.h,
// renderer/own_sun_pass.h; docs/architecture/own-sun-pass.md). X3M_OWN_SUN=1: the mod draws its own sun disc and corona
// as an act of the temporal resolve's stage bracket (TemporalPass FrameInputs::stage_callback: after the fog pass, RT1/RT2
// and the depth surface unbound, RT0 the FP16 scene the resolve reads), beside the engine plumes, while the flare probe
// answers "hidden" for the sun's eligible record (AP own_sun_hook.cpp, FL src/engine/fl/sun_occlusion_fl.cpp) so the
// engine destroys its own lens chain.
//
// The frame's decision (own_sun::core::frame_active) is latched at Present for the next frame by capture.cpp from the
// setting and own_sun_ready(): this device's stage armed in its last resolve. Both the probe answer and the stage read
// that one latch, so they switch together. Arming, at each resolve: the route on, the
// HDR/TAA FP16 path (as the plumes', jitter not required), the scene camera latched, the completed lane at the target's
// size in the lane format, the attached pass (programs and declaration; FP16 post-pixel-shader blending; refused until
// Reset when it fails), not within 64 frames of a failed stage frame (the third consecutive failure refuses until
// Reset) and this frame's sun poll Ok (the shadow lane's, else one at the scene end). An unarmed device answers nothing
// (the engine's chain), so a refused stage or a missing poll never leaves the sky without a sun. The stage draws only in
// a frame whose main-view probe the own sun answered "hidden" (own_sun_hook::own_sun_hidden()): a sun the probe did not
// reach (another view, the flare video option off, the main view unresolved) stays the engine's or none, never two;
// such armed, active frames count no_probe on the own_sun_frame row and the first one writes own_sun_no_probe.
// One own_sun_state row per change of the armed state or its reason; one own_sun_device row per attach; --debug: one
// own_sun_frame row every 10 s.
void MotionOutput::configure_own_sun(bool requested, float radius_u, float radiance, float rays) noexcept {
    own_sun_requested_ = requested;
    own_sun_radius_u_ = own_sun::core::radius_default_u;
    own_sun::core::accept_radius(radius_u, &own_sun_radius_u_);
    own_sun_radiance_ = own_sun::core::radiance_default;
    own_sun::core::accept_radiance(radiance, &own_sun_radiance_);
    own_sun_rays_ = own_sun::core::rays_default;
    own_sun::core::accept_rays(rays, &own_sun_rays_);
    own_sun_armed_ = own_sun_ready_ = own_sun_frame_active_ = false;
    own_sun_failures_ = 0;
    own_sun_failed_out_ = false;
    own_sun_reason_ = requested ? "pending" : "off";
    own_sun_state_logged_ = false;
    own_sun_no_probe_logged_ = false;
}
void MotionOutput::note_own_sun_state(bool armed, const char* reason) noexcept {
    own_sun_armed_ = armed;
    own_sun_reason_ = reason;
    if (own_sun_state_logged_ && armed == own_sun_logged_armed_ && !std::strcmp(reason, own_sun_logged_reason_)) return;
    own_sun_state_logged_ = true;
    own_sun_logged_armed_ = armed;
    own_sun_logged_reason_ = reason;
    log("own_sun_state device=%llu frame=%llu armed=%u reason=%s sun=%s", id_, frame_, unsigned(armed), reason,
        armed ? "own" : "engine");
}
// The pass at the arming: attached once per device, refused until Reset on failure with one own_sun_device row (the
// adapter's MaxPixelShader30InstructionSlots beside the programs' own slot counts).
bool MotionOutput::attach_own_sun() noexcept {
    if (own_sun_attach_failed_) return false;
    if (!own_sun_) {
        own_sun_.reset(new (std::nothrow) renderer::OwnSunPass);
        if (!own_sun_) {
            own_sun_attach_failed_ = true;
            log("own_sun_device device=%llu frame=%llu attached=0 reason=allocation retry=reset", id_, frame_);
            return false;
        }
    }
    if (own_sun_->caps().enabled) return true;
    D3DDISPLAYMODE display{};
    HRESULT hr = native<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &display);
    const bool queried = SUCCEEDED(hr);
    // The arming runs inside the resolve's taa_call (the plumes' rule): no nested accounting.
    if (queried) {
        if (taa_busy_)
            hr = own_sun_->attach(device_, native_, caps_, display.Format);
        else
            taa_call([&] { hr = own_sun_->attach(device_, native_, caps_, display.Format); });
    }
    const bool attached = queried && SUCCEEDED(hr) && own_sun_->caps().enabled;
    const auto& c = own_sun_->caps();
    log("own_sun_device device=%llu frame=%llu attached=%u reason=%s result=%08lx fp16_blending=%08lx vs_slots=%u ps_slots=%u max_ps_slots=%lu retry=reset",
        id_, frame_, unsigned(attached), attached ? "ok" : queried ? c.reason : "adapter_query", hr, c.fp16_blending,
        c.vs_slots, c.ps_slots, static_cast<unsigned long>(caps_.MaxPixelShader30InstructionSlots));
    if (!attached) own_sun_attach_failed_ = true;
    return attached;
}
void MotionOutput::release_own_sun() noexcept {
    if (own_sun_) {
        taa_call([&] { own_sun_->detach(); });
        own_sun_.reset();
    }
    own_sun_lane_ = nullptr;
    own_sun_armed_ = own_sun_ready_ = false;
}
bool MotionOutput::own_sun_arm(bool fp16_route, IDirect3DTexture9* lane, UINT width, UINT height) noexcept {
    own_sun_ready_ = false;
    own_sun_lane_ = nullptr;
    if (!enabled_) {
        note_own_sun_state(false, "route");
        return false;
    }
    if (!taa_enabled_ || taa_failed_ || main_msaa_ || hdr_state_ != HdrState::Active || !hdr_ || !fp16_route) {
        note_own_sun_state(false, "hdr_taa_path");
        return false;
    }
    if (!camera_scene_.valid || !(camera_scene_.m00 > 0.f) || !(camera_scene_.m11 > 0.f)) {
        note_own_sun_state(false, "camera");
        return false;
    }
    D3DSURFACE_DESC lane_desc{};
    if (!lane || !width || !height || FAILED(lane->GetLevelDesc(0, &lane_desc)) || lane_desc.Width != width ||
        lane_desc.Height != height || lane_desc.Format != lane_depth_format()) {
        note_own_sun_state(false, "lane");
        return false;
    }
    if (!attach_own_sun()) {
        note_own_sun_state(false, own_sun_ && own_sun_->caps().reason ? own_sun_->caps().reason : "attach");
        return false;
    }
    if (own_sun_failed_out_) {
        note_own_sun_state(false, "failed_until_reset");
        return false;
    }
    if (frame_ < own_sun_disarmed_until_) {
        note_own_sun_state(false, "disarmed");
        return false;
    }
    // The sun: this frame's poll when the shadow lane took it (its first routed z-writing draw), else one poll here at
    // the scene end (the main view was the last view rendered before the hook: sun_light_poll.h). Not Ok (no directional
    // light, unavailable): not ready, so the next frame's probe forwards to the engine.
    own_sun_poll_source_ = "lane";
    if (candidates_requested_ && point_sun_.polled)
        own_sun_sample_ = point_sun_sample_;
    else if (sun_light_poll::available()) {
        own_sun_poll_source_ = "stage";
        sun_light_poll::read(&own_sun_sample_);
    } else {
        own_sun_poll_source_ = "none";
        own_sun_sample_ = sun_light_poll::Sample{};
        own_sun_sample_.status = sun_light_poll::Status::Disabled;
    }
    if (own_sun_sample_.status != sun_light_poll::Status::Ok) {
        note_own_sun_state(false, "poll");
        return false;
    }
    own_sun_lane_ = lane;
    own_sun_width_ = width;
    own_sun_height_ = height;
    own_sun_ready_ = true;
    note_own_sun_state(true, "armed");
    return true;
}
// The stage callback while the own sun draws this frame: the sun, then the plumes when they armed with work too (one
// callback slot; each pass sets every state it draws with). A lost device ends it; otherwise the first failure is the
// result (the resolve normalizes again after anything but S_FALSE).
HRESULT MotionOutput::own_sun_stage_callback(void* context, IDirect3DDevice9*) noexcept {
    MotionOutput& self = *static_cast<MotionOutput*>(context);
    const HRESULT sun = self.run_own_sun();
    if (sun == D3DERR_DEVICELOST || sun == D3DERR_DEVICENOTRESET || !self.own_sun_with_plumes_) return sun;
    const HRESULT plumes = self.run_engine_plumes();
    if (plumes == D3DERR_DEVICELOST || plumes == D3DERR_DEVICENOTRESET || FAILED(sun)) return FAILED(sun) ? sun : plumes;
    if (FAILED(plumes)) return plumes;
    return sun == S_OK || plumes == S_OK ? S_OK : S_FALSE;
}
// An armed, active frame whose main-view probe was not answered "hidden": nothing drawn (the resolve's wiring).
void MotionOutput::own_sun_no_probe() noexcept {
    ++own_sun_period_.no_probe;
    own_sun_skip_ = "no_probe";
    if (own_sun_no_probe_logged_) return;
    own_sun_no_probe_logged_ = true;
    log("own_sun_no_probe device=%llu frame=%llu note=chain_not_hidden_stage_off", id_, frame_);
}
HRESULT MotionOutput::run_own_sun() noexcept {
    ++own_sun_period_.active;
    own_sun_skip_ = "none";
    own_sun::core::Camera camera;
    for (unsigned i = 0; i < 9; ++i) camera.r[i] = camera_scene_.r[i];
    for (unsigned i = 0; i < 3; ++i) camera.t[i] = camera_scene_.t[i];
    camera.m00 = camera_scene_.m00;
    camera.m11 = camera_scene_.m11;
    camera.m20 = camera_scene_.m20;
    camera.m21 = camera_scene_.m21;
    renderer::OwnSunFrame f;
    f.width = own_sun_width_;
    f.height = own_sun_height_;
    f.lane = own_sun_lane_;
    f.rays = own_sun_rays_;
    f.disc = own_sun::core::project(own_sun_sample_.position, camera, f.width, f.height, own_sun_radius_u_,
                                    jitter_active_ ? jitter_[0] : 0.f, jitter_active_ ? jitter_[1] : 0.f);
    renderer::FogTransmittanceLaw law{};
    engine_plumes_fog(&law); // off (T = 1) unless this frame's density composite applied
    own_sun::core::fog_scalar(law, &own_sun_fog_t_);
    own_sun::core::radiance(own_sun_sample_.colour, own_sun_radiance_, own_sun_fog_t_, &f.radiance);
    own_sun_disc_ = f.disc;
    own_sun_colour_ = f.radiance;
    if (f.disc.skip != own_sun::core::Skip::None) {
        own_sun_skip_ = own_sun::core::skip_name(f.disc.skip);
        // A camera or target the projection refuses: not ready, the next frame's probe forwards to the engine (behind,
        // small, offscreen are a sun the engine would not show either).
        if (f.disc.skip == own_sun::core::Skip::Camera || f.disc.skip == own_sun::core::Skip::Size) own_sun_ready_ = false;
        return S_FALSE;
    }
    if (f.radiance.black) {
        own_sun_skip_ = own_sun_fog_t_ > 0.f ? "black" : "fog";
        return S_FALSE;
    }
    LARGE_INTEGER begin{}, end{};
    QueryPerformanceCounter(&begin);
    renderer::OwnSunReport report{};
    const HRESULT hr = own_sun_->run(f, &report);
    QueryPerformanceCounter(&end);
    const std::uint64_t frequency = engine_qpc_frequency();
    own_sun_stage_us_ = frequency ? float(double(end.QuadPart - begin.QuadPart) * 1e6 / double(frequency)) : 0.f;
    own_sun_report_ = report;
    if (report.drew) {
        ++own_sun_period_.drawn;
        own_sun_failures_ = 0;
    }
    if (FAILED(hr) && hr != D3DERR_DEVICELOST && hr != D3DERR_DEVICENOTRESET) {
        // A failed stage frame: this frame's sun is lost and the stage disarms for 64 frames (the answer returns to
        // the engine's chain from the next frame on); the third consecutive one refuses until Reset.
        own_sun_skip_ = "failed";
        const bool final = ++own_sun_failures_ >= own_sun_failure_limit;
        if (final)
            own_sun_failed_out_ = true;
        else
            own_sun_disarmed_until_ = frame_ + own_sun_disarm_frames;
        log("own_sun_failed device=%llu frame=%llu result=%08lx step=%u calls=%u consecutive=%u until=%llu final=%u retry=%s",
            id_, frame_, hr, unsigned(report.failed), report.calls, own_sun_failures_,
            static_cast<unsigned long long>(final ? 0 : own_sun_disarmed_until_), unsigned(final), final ? "reset" : "frames");
    }
    return hr;
}
// Present (capture.cpp), once per frame after own_sun_ready() was read for the latch: the next frame's latch, and under
// --debug the own_sun_frame row every 10 s (this period's frames, the stage's last frame, the probe's answers since the
// previous row).
void MotionOutput::own_sun_frame(bool active) noexcept {
    ++own_sun_period_.frames;
    own_sun_frame_active_ = active;
    // Readiness is earned again by the next frame's resolve (own_sun_arm): a frame whose resolve never reaches the
    // arming (TAA failed, a menu) answers nothing from the frame after it, so the sky is never left without a sun.
    own_sun_ready_ = false;
    bool due = false;
    if (log_tier::cached_debug) { // one QueryPerformanceCounter per frame, only under --debug
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const std::uint64_t frequency = engine_qpc_frequency();
        if (!own_sun_row_qpc_) own_sun_row_qpc_ = std::uint64_t(now.QuadPart);
        due = frequency && std::uint64_t(now.QuadPart) - own_sun_row_qpc_ >= own_sun_row_seconds * frequency;
        if (due) own_sun_row_qpc_ = std::uint64_t(now.QuadPart);
    }
    if (!due) return;
    const own_sun_hook::Counters c = own_sun_hook::counters();
    const auto& d = own_sun_disc_;
    const auto& r = own_sun_colour_;
    // The meter's reaction (note section 2): the EV the tonemap consumes and the highlight guard's limit of the last meter.
    const float ev = hdr_ ? hdr_->exposure().ev() : 0.f, ev_limit = hdr_ ? hdr_->exposure().ev_limit() : 0.f;
    if (log_tier::cached_debug)
        log("own_sun_frame device=%llu frame=%llu active=%u armed=%u reason=%s frames=%u active_frames=%u drawn=%u no_probe=%u answered=%lu forwarded=%lu late=%lu ev=%.3f ev_limit=%.3f poll=%s poll_source=%s skip=%s u=%.5f v=%.5f view_z=%.0f radius_px=%.1f colour=%.4f,%.4f,%.4f fog_t=%.4f core=%.3f,%.3f,%.3f glow=%.3f,%.3f,%.3f rays=%.2f result=%08lx calls=%u stage_us=%.1f",
        id_, frame_, unsigned(active), unsigned(own_sun_armed_), own_sun_reason_, own_sun_period_.frames,
        own_sun_period_.active, own_sun_period_.drawn, own_sun_period_.no_probe,
        static_cast<unsigned long>(c.own_sun_hidden - own_sun_answered_seen_),
        static_cast<unsigned long>(c.original - own_sun_forwarded_seen_),
        static_cast<unsigned long>(c.late - own_sun_late_seen_), double(ev), double(ev_limit),
        sun_light_poll::status_name(own_sun_sample_.status), own_sun_poll_source_, own_sun_skip_, double(d.u), double(d.v),
        double(d.view_z), double(d.radius_px), double(r.colour[0]), double(r.colour[1]), double(r.colour[2]),
        double(own_sun_fog_t_), double(r.core[0]), double(r.core[1]), double(r.core[2]), double(r.glow[0]),
        double(r.glow[1]), double(r.glow[2]), double(own_sun_rays_),
        static_cast<unsigned long>(own_sun_report_.operation), own_sun_report_.calls, double(own_sun_stage_us_));
    own_sun_answered_seen_ = c.own_sun_hidden;
    own_sun_forwarded_seen_ = c.original;
    own_sun_late_seen_ = c.late;
    own_sun_period_ = {};
}
