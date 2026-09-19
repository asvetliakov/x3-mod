// Included by motion_output.cpp inside namespace x3m (as the shadow-replay
// fragments are): the volumetric sun fog at the scene end
// (docs/architecture/volumetric-fog.md, "Stage 1 implementation").
int MotionOutput::volumetric_fog_toggle() noexcept {
    if (!fog_requested_) return -1;
    fog_enabled_ = !fog_enabled_;
    log("volumetric_fog_toggle device=%llu frame=%llu enabled=%u strength=%.4f anisotropy=%.2f disabled=%u", id_, frame_, unsigned(fog_enabled_), double(fog_strength_), double(fog_anisotropy_), unsigned(fog_disabled_));
    return fog_enabled_ ? 1 : 0;
}
int MotionOutput::volumetric_fog_step() noexcept {
    if (!fog_requested_) return -1;
    fog_strength_ = renderer::fog_strength_next(fog_strength_);
    log("volumetric_fog_strength device=%llu frame=%llu strength=%.4f enabled=%u", id_, frame_, double(fog_strength_), unsigned(fog_enabled_));
    return int(fog_strength_ * 1000.f + .5f);
}
void MotionOutput::disable_volumetric_fog(const char* why, HRESULT result) noexcept {
    fog_disabled_ = true;
    log("volumetric_fog_disabled device=%llu frame=%llu reason=%s result=%08lx session=1", id_, frame_, why, result);
}
// Once per frame. Every precondition is this frame's; a miss skips with
// nothing touched (the frame is byte-identical to the option being off). The
// sector rule's weight ramps the optical depth, so neither edge pops. The
// cascades are the apply quad's slots 1-3 (slot 0 is the own-ship map; a
// single-cascade set uses slot 0), valid exactly as the apply rules them; the
// pass runs whether or not the apply quad did (the sun lane may refuse the
// frame): without a valid map the veil is drawn unshadowed.
void MotionOutput::run_volumetric_fog() noexcept {
    if (fog_frame_ == frame_) return; // the hook and the bloom-copy sites both qualify
    fog_frame_ = frame_;
    const float weight = fog_latch_.update(frame_, fog_everywhere_);
    const char* skip = nullptr;
    HRESULT hr = S_FALSE;
    IDirect3DSurface9* rt0 = nullptr; IDirect3DTexture9* depth = nullptr;
    renderer::FogResult out{};
    renderer::FogFrame in{};
    double us = 0.;
    bool sun_tracked = false;
    if (!fog_enabled_) skip = "toggled_off";
    else if (fog_disabled_) skip = "disabled";
    else if (!(weight > 0.f)) skip = "sector";
    else if (!(fog_strength_ * weight > 0.f)) skip = "strength";
    else if (!taa_enabled_ || taa_failed_ || counters_.taa.attempted || main_msaa_ || !jitter_active_) skip = "taa"; // the jittered march needs this frame's resolve
    else if (composition_state_lost_ || motion_state_lost_) skip = "state_lost";
    else if (hdr_state_ != HdrState::Active || !hdr_ || !hdr_->target()) skip = "owner";
    else if (!counters_.filled || !depth_surface_ || !depth_enabled_) skip = "depth";
    else if (shadow_.recording) skip = "recording";
    else if (active_queries_) skip = "queries";
    else if (!camera_scene_.valid) skip = "camera";
    else if (!depth_cascades_on() || !depth_replay_) skip = "cascades";
    else {
        hr = native<GetRenderTargetFn>(GetRenderTarget)(device_, 0, &rt0);
        if (FAILED(hr) || !rt0 || rt0 != hdr_->target()) skip = "target";
        else {
            hr = depth_surface_->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&depth));
            if (SUCCEEDED(hr) && !depth) hr = E_NOINTERFACE;
            if (FAILED(hr)) skip = "depth_container";
        }
    }
    if (!skip) {
        in.depth_share = depth; in.target = rt0; in.width = target_width_; in.height = target_height_;
        auto& q = in.params;
        const float jitter_x = in.width ? 2.f * jitter_[0] / float(in.width) : 0.f, jitter_y = in.height ? -2.f * jitter_[1] / float(in.height) : 0.f;
        q.m00 = camera_scene_.m00; q.m11 = camera_scene_.m11;
        q.m20 = camera_scene_.m20 + jitter_x + (in.width ? renderer::quad_pixel_centre_m20(in.width) : 0.f);
        q.m21 = camera_scene_.m21 + jitter_y + (in.height ? renderer::quad_pixel_centre_m21(in.height) : 0.f);
        q.m22 = ao_default_m22; q.m32 = ao_default_m32;
        q.tau_max = fog_strength_ * weight; q.anisotropy = fog_anisotropy_; q.margin = renderer::shadow_cascade_select_margin;
        q.decode_exponent = hdr_config_.decode == x3::temporal::AgxDecode::none ? 1.f : 2.2f;
        q.jitter_index = counters_.jitter_index; q.update_sky = frame_ % 8u == 0u;
        unsigned slots[renderer::shadow_cascade_max]{};
        const unsigned count = renderer::shadow_cascade_apply_slots(depth_cascades_, slots), first = count > 1 ? 1u : 0u;
        const bool replayed = depth_replayed_frame_ == frame_ && depth_cascade_frame_ok_;
        float sun_rows[12]{}; bool sun_known = false;
        for (unsigned s = first; s < count && in.count < renderer::fog_cascade_max && !skip; ++s) {
            const unsigned i = slots[s];
            const auto& cascade = depth_cascades_.cascades[i];
            auto& k = in.cascades[in.count++];
            const auto* kept = depth_replay_->retained(i);
            const bool far_kept = s + 1 == count && count > 1;
            k.valid = replayed && kept && (kept->frame == frame_ || (far_kept && kept->frame + 1 == frame_)) && depth_replay_->map_texture(i);
            if (!k.valid) continue;
            renderer::SunShadowBias bias{};
            if (!renderer::shadow_replay_view_rows(camera_scene_, kept->basis, cascade, k.rows)) { skip = "rows"; break; }
            if (!renderer::sun_shadow_apply_bias(sun_apply_bias_units_, sun_apply_clamp_texels_, double(cascade.half_extent), cascade.depth_half(), depth_replay_->size(i), bias)) { skip = "bias"; break; }
            k.bias = bias.max; k.map = depth_replay_->map_texture(i);
            if (!sun_known) { for (unsigned r = 0; r < 12; ++r) sun_rows[r] = k.rows[r]; sun_known = true; }
        }
        if (!skip && !sun_known && count) {
            // No valid map: the direction from this frame's would-be basis of the first slot (the apply's absent-cascade path).
            const unsigned i = slots[first < count ? first : 0];
            renderer::ShadowReplayBasis current{};
            const float* sun = cascade_sun(i);
            sun_known = sun && renderer::shadow_replay_basis(camera_scene_, sun, depth_cascades_.cascades[i], current, point_sun_.grid_anchor(i)) &&
                        renderer::shadow_replay_view_rows(camera_scene_, current, depth_cascades_.cascades[i], sun_rows);
        }
        // View-space direction toward the sun: the rows' depth axis points away from it.
        const float length = std::sqrt(sun_rows[8] * sun_rows[8] + sun_rows[9] * sun_rows[9] + sun_rows[10] * sun_rows[10]);
        if (!skip && (!sun_known || !(length > 0.f) || !std::isfinite(length))) skip = "sun";
        if (!skip) {
            for (unsigned r = 0; r < 3; ++r) q.sun_view[r] = -sun_rows[8 + r] / length;
            sun_tracked = point_sun_sample_.status == sun_light_poll::Status::Ok && renderer::fog_sun_radiance(point_sun_sample_.colour, q.sun_radiance);
            if (!sun_tracked) {
                renderer::fog_sun_radiance_fallback(q.sun_radiance);
                if (!fog_sun_fallback_logged_) {
                    fog_sun_fallback_logged_ = true;
                    log("volumetric_fog_sun device=%llu frame=%llu source=fallback poll=%s radiance=%.4f,%.4f,%.4f", id_, frame_, sun_light_poll::status_name(point_sun_sample_.status),
                        double(q.sun_radiance[0]), double(q.sun_radiance[1]), double(q.sun_radiance[2]));
                }
            }
        }
    }
    if (!skip) {
        if (!fog_) { try { fog_ = std::make_unique<renderer::FogPass>(); } catch (...) { disable_volumetric_fog("allocation", E_OUTOFMEMORY); skip = "disabled"; } }
        if (!skip && !fog_->caps().enabled) {
            D3DDISPLAYMODE display{};
            hr = native<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &display);
            if (SUCCEEDED(hr)) taa_call([&] { hr = fog_->attach(device_, native_, caps_, display.Format); });
            log("volumetric_fog_device device=%llu attached=%u reason=%s result=%08lx slots=%u strength=%.4f anisotropy=%.2f everywhere=%u", id_, unsigned(SUCCEEDED(hr) && fog_->caps().enabled),
                SUCCEEDED(hr) ? "ok" : fog_->caps().reason, hr, fog_->caps().largest_program_slots, double(fog_strength_), double(fog_anisotropy_), unsigned(fog_everywhere_));
            if (FAILED(hr) || !fog_->caps().enabled) { disable_volumetric_fog("attach", hr); skip = "disabled"; }
        }
        if (!skip && fog_->reset_pending()) skip = "reset_pending";
    }
    if (!skip) {
        in.caller_scene_open = scene_open_; in.caller_stateblock_recording = shadow_.recording; in.caller_queries_idle = active_queries_ == 0;
        LARGE_INTEGER t0{}, t1{}, f{};
        if (fog_timing_) QueryPerformanceCounter(&t0);
        taa_call([&] { hr = fog_->execute(in, &out); });
        if (fog_timing_) { QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&f); us = f.QuadPart ? double(t1.QuadPart - t0.QuadPart) * 1e6 / double(f.QuadPart) : 0.; }
        if (FAILED(out.restore)) invalidate_render_states();
        if (FAILED(hr)) {
            skip = "failed";
            // A target allocation failure, or three consecutive failed frames: off for the session, one line.
            if (out.failed == renderer::FogStage::Targets) disable_volumetric_fog("targets", hr);
            else if (++fog_failures_ >= 3) disable_volumetric_fog("failures", hr);
        } else { fog_failures_ = 0; ++fog_applied_frames_; }
    }
    release(depth); release(rt0);
    const char* reason = skip ? skip : "ok";
    // A change of state is one line (bounded); timing mode logs every frame.
    const bool changed = std::strcmp(reason, fog_last_reason_) != 0;
    if (fog_timing_ || (changed && fog_logs_ < 64)) {
        if (!fog_timing_) ++fog_logs_;
        log("volumetric_fog_frame device=%llu frame=%llu applied=%u reason=%s strength=%.4f weight=%.3f cards=%u cascades=%u sky=%u sun=%s cpu_us=%.1f calls=%u result=%08lx restore=%08lx stage=%u",
            id_, frame_, unsigned(!skip && out.applied), reason, double(fog_strength_), double(weight), unsigned(fog_latch_.cards_recent(frame_)), out.cascades_bound, unsigned(out.sky_updated),
            skip ? "none" : sun_tracked ? "tracked" : "fallback", us, out.device_calls, out.operation, out.restore, unsigned(out.failed));
    }
    fog_last_reason_ = reason;
}
