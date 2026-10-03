// Engine plumes, phase 2 (included by motion_output.cpp inside namespace x3m; engine_plumes_core.h,
// renderer/engine_plumes_pass.h; docs/architecture/engine-effects-modern.md sections 3-6). X3M_ENGINE_EFFECTS=plumes:
// the glow-jet draws the phase-1 recogniser suppressed this frame (the record ring, motion_output_engine_effects_inc.h)
// are drawn by the proxy as plumes, one indexed draw for every nozzle, as the first act of the temporal resolve's state
// bracket (TemporalPass FrameInputs::stage_callback: RT1/RT2 and the depth surface unbound, RT0 the FP16 scene the
// resolve reads), ONE/ONE, softly occluded by the completed lane.
//
// Arming, decided at each resolve (everything is latched by then): the suppression armed (hook, suppress, both call
// redirects live), the HDR/TAA path (--motion-output --hdr --taa: the FP16 route, jitter, no MSAA, the scene camera
// latched), the lane (the completed RT2 at the target's size in the lane format), the attached pass (programs, buffers,
// FP16 post-pixel-shader blending: refused until Reset when it fails) and not within 64 frames of a failed stage frame;
// the third consecutive failed stage frame (no drawn frame between) refuses until Reset. An unarmed frame draws nothing
// while the glow stays suppressed: the phase-1 look (off). One engine_plumes_state row per change of the armed state or
// its reason. Frames the resolve does not reach show no engine effect (design section 1). Only the scene view's records
// are drawn (engine_plumes_core.h ViewFilter); the rest count skipped_other_view in engine_stage.
void MotionOutput::configure_engine_plumes(bool requested, engine_plumes::Preset preset, float nozzle_width) noexcept {
    plumes_requested_ = requested && engine_hook_ && engine_suppress_ && engine_ring_;
    plumes_preset_ = preset;
    // The look: the chosen constants with the configured nozzle width (engine_effects::plume_nozzle(): parsed and
    // range-checked there); out of range here (no caller does that) keeps the default.
    plumes_look_ = engine_plumes::default_look;
    if (nozzle_width >= engine_plumes::nozzle_min && nozzle_width <= engine_plumes::nozzle_max) plumes_look_.nozzle_width = nozzle_width;
    engine_plumes::flow_rate(plumes_look_, &plumes_flow_rate_);
    plumes_armed_ = plumes_ran_ = plumes_fenced_ = false;
    plumes_failures_ = 0;
    plumes_failed_out_ = false;
    plumes_reason_ = plumes_requested_ ? "pending" : "off";
    plumes_state_logged_ = false;
    if (requested && !plumes_requested_)
        log("engine_plumes_state device=%llu frame=%llu armed=0 reason=suppression_off glow=native drawn=none", id_, frame_);
}
int MotionOutput::engine_plumes_cycle_preset() noexcept {
    if (!plumes_requested_) return -1;
    const engine_plumes::Preset previous = plumes_preset_;
    plumes_preset_ = engine_plumes::next_preset(previous);
    log("engine_plumes_preset device=%llu frame=%llu preset=%s previous=%s source=hotkey", id_, frame_,
        engine_plumes::preset_name(plumes_preset_), engine_plumes::preset_name(previous));
    return int(plumes_preset_);
}
void MotionOutput::note_engine_plumes_state(bool armed, const char* reason) noexcept {
    plumes_armed_ = armed;
    plumes_reason_ = reason;
    if (plumes_state_logged_ && armed == plumes_logged_armed_ && !std::strcmp(reason, plumes_logged_reason_)) return;
    plumes_state_logged_ = true;
    plumes_logged_armed_ = armed;
    plumes_logged_reason_ = reason;
    // Unarmed: nothing is drawn and the recognised glow jets stay suppressed (the phase-1 look, off).
    log("engine_plumes_state device=%llu frame=%llu armed=%u reason=%s glow=suppressed drawn=%s preset=%s", id_, frame_,
        unsigned(armed), reason, armed ? "plumes" : "none", engine_plumes::preset_name(plumes_preset_));
}
// The pass at the latch: attached once per device (programs and buffers at the arming, not at the first plume),
// refused until Reset on failure with one engine_plumes_device row.
bool MotionOutput::attach_engine_plumes() noexcept {
    if (plumes_attach_failed_) return false;
    if (!plumes_) {
        plumes_.reset(new (std::nothrow) renderer::EnginePlumesPass);
        if (!plumes_) {
            plumes_attach_failed_ = true;
            log("engine_plumes_device device=%llu frame=%llu attached=0 reason=allocation retry=reset", id_, frame_);
            return false;
        }
    }
    if (plumes_->caps().enabled) return true;
    D3DDISPLAYMODE display{};
    HRESULT hr = native<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &display);
    const bool queried = SUCCEEDED(hr);
    // The arming runs inside the resolve's taa_call, whose reference accounting covers the objects created here; a
    // nested taa_call would count them twice.
    if (queried) {
        if (taa_busy_)
            hr = plumes_->attach(device_, native_, caps_, display.Format);
        else
            taa_call([&] { hr = plumes_->attach(device_, native_, caps_, display.Format); });
    }
    const bool attached = queried && SUCCEEDED(hr) && plumes_->caps().enabled;
    const auto& c = plumes_->caps();
    log("engine_plumes_device device=%llu frame=%llu attached=%u reason=%s result=%08lx fp16_blending=%08lx vs_slots=%u ps_slots=%u max_ps_slots=%lu vb_bytes=%u retry=reset",
        id_, frame_, unsigned(attached), attached ? "ok" : queried ? c.reason : "adapter_query", hr, c.fp16_blending,
        c.vs_slots, c.ps_slots, static_cast<unsigned long>(caps_.MaxPixelShader30InstructionSlots),
        renderer::EnginePlumesPass::vertex_bytes);
    if (!attached) plumes_attach_failed_ = true;
    return attached;
}
void MotionOutput::release_engine_plumes() noexcept {
    if (plumes_) {
        taa_call([&] { plumes_->detach(); });
        plumes_.reset();
    }
    if (ribbons_) { // phase 3: the ribbon pass and its pool (motion_output_engine_ribbons_inc.h)
        taa_call([&] { ribbons_->detach(); });
        ribbons_.reset();
    }
    plumes_lane_ = nullptr;
    plumes_armed_ = false;
}
bool MotionOutput::engine_plumes_arm(bool fp16_route, IDirect3DTexture9* lane, UINT width, UINT height) noexcept {
    plumes_evaluated_ = true;
    if (!engine_suppress_ || !engine_redirects_ || !enabled_) {
        note_engine_plumes_state(false, "suppression_off");
        return false;
    }
    if (!taa_enabled_ || taa_failed_ || !jitter_active_ || main_msaa_ || hdr_state_ != HdrState::Active || !hdr_ ||
        !fp16_route) {
        note_engine_plumes_state(false, "hdr_taa_path");
        return false;
    }
    if (!camera_scene_.valid || !(camera_scene_.m00 > 0.f) || !(camera_scene_.m11 > 0.f)) {
        note_engine_plumes_state(false, "camera");
        return false;
    }
    // The lane the pixel program samples at the pixel: the target's size and the format the pass is told.
    D3DSURFACE_DESC lane_desc{};
    if (!lane || !width || !height || FAILED(lane->GetLevelDesc(0, &lane_desc)) || lane_desc.Width != width ||
        lane_desc.Height != height || lane_desc.Format != lane_depth_format()) {
        note_engine_plumes_state(false, "lane");
        return false;
    }
    if (!attach_engine_plumes()) {
        note_engine_plumes_state(false, plumes_ && plumes_->caps().reason ? plumes_->caps().reason : "attach");
        return false;
    }
    if (plumes_failed_out_) {
        note_engine_plumes_state(false, "failed_until_reset");
        return false;
    }
    attach_engine_ribbons(); // phase 3: a refusal leaves the plumes drawing without ribbons
    if (frame_ < plumes_disarmed_until_) {
        note_engine_plumes_state(false, "disarmed");
        return false;
    }
    plumes_lane_ = lane;
    plumes_width_ = width;
    plumes_height_ = height;
    note_engine_plumes_state(true, "armed");
    return true;
}
HRESULT MotionOutput::engine_plumes_callback(void* context, IDirect3DDevice9*) noexcept {
    return static_cast<MotionOutput*>(context)->run_engine_plumes();
}
namespace {
const engine_effects::core::Body* engine_plumes_body(int index) {
    return engine_effects::body(index);
}
}
// The performance-counter frequency, read once per device (qpc_frequency_, shared with the other timers; fixed at
// boot): the plume stage's timing and its clock (engine_clock_) take it without a call per frame.
std::uint64_t MotionOutput::engine_qpc_frequency() noexcept {
    if (!qpc_frequency_) {
        LARGE_INTEGER f{};
        if (QueryPerformanceFrequency(&f) && f.QuadPart > 0) qpc_frequency_ = std::uint64_t(f.QuadPart);
    }
    return qpc_frequency_;
}
HRESULT MotionOutput::run_engine_plumes() noexcept {
    plumes_ran_ = true;
    if (!plumes_ || !plumes_armed_ || !plumes_lane_ || !engine_ring_ || (!engine_ring_->count && !engine_ribbons_live()))
        return S_FALSE; // phase 3: a frame without records still runs while ribbons fade
    // The scene view: the camera handle the own ship's jets were recorded under, else the most frequent camera handle
    // among the scene-phase records; no scene-phase record, nothing to draw (no device call) unless ribbons are fading
    // (they then take no record: every one fails the filter).
    std::uint32_t scene_camera = 0;
    const bool viewed = engine_plumes::scene_view_camera(engine_ring_->camera, engine_ring_->scene, engine_ring_->own,
                                                         engine_ring_->count, &scene_camera, &plumes_view_rule_);
    if (plumes_view_rule_ == engine_plumes::ViewRule::own)
        ++plumes_view_own_total_;
    else if (plumes_view_rule_ == engine_plumes::ViewRule::majority)
        ++plumes_view_majority_total_;
    if (!viewed && !engine_ribbons_live()) {
        plumes_report_ = {};
        plumes_report_.stats.skipped_other_view = engine_ring_->count;
        return S_FALSE;
    }
    renderer::EnginePlumesFrame in{};
    in.width = plumes_width_;
    in.height = plumes_height_;
    for (unsigned j = 0; j < 3; ++j) {
        in.view.rows[j * 4] = camera_scene_.r[j];
        in.view.rows[j * 4 + 1] = camera_scene_.r[3 + j];
        in.view.rows[j * 4 + 2] = camera_scene_.r[6 + j];
        in.view.rows[j * 4 + 3] = camera_scene_.t[j];
    }
    in.view.m00 = camera_scene_.m00;
    in.view.m11 = camera_scene_.m11;
    in.view.height = float(in.height);
    // The jitter the routed draws were rasterised under (the engine's projection latch carries none).
    in.m20 = camera_scene_.m20 + (in.width ? 2.f * jitter_[0] / float(in.width) : 0.f);
    in.m21 = camera_scene_.m21 + (in.height ? -2.f * jitter_[1] / float(in.height) : 0.f);
    in.m22 = camera_scene_.m22 != 0.f ? camera_scene_.m22 : projection_default_m22;
    in.m32 = camera_scene_.m32 != 0.f ? camera_scene_.m32 : projection_default_m32;
    // The engine's near plane (6 in the game) from the latch's depth law, z_clip = m22 + m32 / z.
    in.view.near_z = -in.m32 / in.m22;
    if (!(in.view.near_z > 0.f) || !(in.view.near_z < 1e30f)) in.view.near_z = -projection_default_m32 / projection_default_m22;
    in.lane_four_channel = lane_depth_format() == D3DFMT_A32B32G32R32F;
    in.lane = plumes_lane_;
    in.records = engine_ring_->records;
    in.record_count = engine_ring_->count;
    in.body = &engine_plumes_body;
    in.filter.camera = engine_ring_->camera;
    in.filter.scene = engine_ring_->scene;
    in.filter.handle = scene_camera;
    in.preset = plumes_preset_;
    // The stage's clock (the plumes' flow and pulse, the ribbons' pool): the performance counter between stage runs, a
    // step on or after an F8 capture frame held to the last ordinary one (engine_plumes_core.h StageClock).
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    engine_clock_.step(std::uint64_t(counter.QuadPart), engine_qpc_frequency(), capture_);
    engine_clock_.wrapped(&in.seconds);
    // The flow phase advances by the same step at a constant speed (engine_plumes_core.h FlowPhase).
    engine_flow_.advance(engine_clock_.last_step, plumes_flow_rate_);
    engine_flow_.wrapped(&in.phase);
    in.look = &plumes_look_;
    in.parents = engine_ring_->parent; // the capital sub-engines' floor (engine_plumes_core.h ShipFloor)
    engine_plumes_fog(&in.view.fog); // phase 3: the density fog's mean transmittance per nozzle, off unless it applied
    // stage_us: the build and the draw; with --gpu-sync-timing the EnginePlumes pair fences both sides (EVENT queries),
    // so it includes the GPU's completion of the draw.
    plumes_fenced_ = gpu_sync_ != nullptr;
    if (gpu_sync_) gpu_sync_->begin(gpu_sync_timing::EnginePlumes);
    LARGE_INTEGER begin{}, end{};
    QueryPerformanceCounter(&begin);
    renderer::EnginePlumesReport report{};
    HRESULT hr = plumes_->run(in, &report);
    if (SUCCEEDED(hr)) {
        // Phase 3: the ribbons, the stage's second draw after the plumes (motion_output_engine_ribbons_inc.h); a failed
        // ribbon draw fails the stage frame.
        const HRESULT ribbons = run_engine_ribbons(in);
        if (FAILED(ribbons) || ribbons == S_OK) hr = ribbons;
    }
    if (gpu_sync_) gpu_sync_->end(gpu_sync_timing::EnginePlumes);
    QueryPerformanceCounter(&end);
    const std::uint64_t frequency = engine_qpc_frequency();
    plumes_stage_us_ = frequency ? float(double(end.QuadPart - begin.QuadPart) * 1e6 / double(frequency)) : 0.f;
    plumes_report_ = report;
    if (FAILED(hr) && hr != D3DERR_DEVICELOST && hr != D3DERR_DEVICENOTRESET) {
        // A failed stage frame: this frame's plumes are lost (the glow stays suppressed) and the stage disarms for 64
        // frames; the third consecutive one refuses until Reset. One row per failure (final=1 on the last).
        const bool final = ++plumes_failures_ >= plumes_failure_limit;
        if (final)
            plumes_failed_out_ = true;
        else
            plumes_disarmed_until_ = frame_ + plumes_disarm_frames;
        log("engine_plumes_failed device=%llu frame=%llu result=%08lx step=%u calls=%u ribbon_step=%u consecutive=%u until=%llu final=%u retry=%s",
            id_, frame_, hr, unsigned(report.failed), report.calls, unsigned(ribbons_report_.failed), plumes_failures_,
            static_cast<unsigned long long>(final ? 0 : plumes_disarmed_until_), unsigned(final), final ? "reset" : "frames");
    } else if (hr == S_OK && report.drew)
        plumes_failures_ = 0;
    return hr;
}
// --debug, at the engine_frame cadence (engine_effects_frame_end): the frame's stage.
void MotionOutput::log_engine_stage() noexcept {
    const auto& s = plumes_report_.stats;
    // Phase 3: the ribbons (this frame's update and draw; the pool's session totals) and the fog transmittance.
    const auto& ru = ribbons_report_.update;
    const auto& rb = ribbons_report_.stats;
    const engine_ribbons::Pool* pool = ribbons_ ? &ribbons_->pool() : nullptr;
    const auto total = [](std::uint64_t v) { return static_cast<unsigned long long>(v); };
    if (log_tier::cached_debug)
        log("engine_stage device=%llu frame=%llu armed=%u reason=%s ran=%u records=%u nozzles=%u vertices=%u discs=%u floored=%u ships=%u steering=%u capped=%u faded=%u culled_small=%u culled_behind=%u culled_rows=%u culled_idle=%u skipped_other_view=%u view_rule=%s view_own_total=%llu view_majority_total=%llu result=%08lx step=%u calls=%u stage_us=%.1f fenced=%u preset=%s ribbons=%u ribbon_samples=%u ribbons_live=%u ribbons_fading=%u ribbon_created=%u ribbon_appended=%u ribbon_overflow=%u ribbon_skipped_other_view=%u ribbon_evicted=%u ribbon_cut_clear=%u ribbon_load_clear=%u ribbon_result=%08lx ribbon_step=%u ribbon_calls=%u ribbon_evictions_total=%llu ribbon_cut_clears_total=%llu ribbon_load_clears_total=%llu ribbon_reset_clears_total=%llu ribbon_jumps_total=%llu ribbon_overflow_total=%llu fog=%u fogged=%u fog_min=%.4f",
            id_, frame_, unsigned(plumes_armed_), plumes_reason_, unsigned(plumes_ran_), engine_ring_ ? engine_ring_->count : 0u,
            s.nozzles, s.vertices, s.discs, s.floored, s.ships, s.steering, s.capped, s.faded, s.culled_small, s.culled_behind, s.culled_rows,
            s.culled_idle, s.skipped_other_view, engine_plumes::view_rule_name(plumes_view_rule_),
            total(plumes_view_own_total_), total(plumes_view_majority_total_), plumes_report_.operation, unsigned(plumes_report_.failed), plumes_report_.calls,
            double(plumes_stage_us_), unsigned(plumes_fenced_), engine_plumes::preset_name(plumes_preset_), rb.ribbons,
            rb.samples, ru.live, ru.fading, ru.created, ru.appended, ru.overflow, ru.skipped_other_view, ru.evicted, unsigned(ru.cut_clear),
            unsigned(ru.load_clear), ribbons_report_.operation, unsigned(ribbons_report_.failed), ribbons_report_.calls,
            total(pool ? pool->evictions : 0), total(pool ? pool->cut_clears : 0), total(pool ? pool->load_clears : 0),
            total(pool ? pool->reset_clears : 0), total(pool ? pool->jumps : 0), total(pool ? pool->overflows : 0),
            unsigned(fog_density_applied_frame_ == frame_), s.fogged + rb.fogged, double(s.fog_min));
}
