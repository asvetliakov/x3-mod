// Included by motion_output.cpp inside namespace x3m (as the shadow-replay
// fragments are): the volumetric sun fog at the scene end
// (docs/architecture/volumetric-fog.md, "Stage 1 implementation").
const char* MotionOutput::fog_frame_prerequisite() const noexcept {
    if (!taa_enabled_ || taa_failed_ || counters_.taa.attempted || main_msaa_ || !jitter_active_) return "taa"; // the jittered march needs this frame's resolve
    if (composition_state_lost_ || motion_state_lost_) return "state_lost";
    if (hdr_state_ != HdrState::Active || !hdr_ || !hdr_->target()) return "owner";
    if (!counters_.filled || !depth_surface_ || !depth_enabled_) return "depth";
    if (shadow_.recording) return "recording";
    if (active_queries_) return "queries";
    if (!camera_scene_.valid || !(camera_scene_.m00 > 0.f) || !(camera_scene_.m11 > 0.f)) return "camera";
    if (!depth_cascades_on() || !depth_replay_) return "cascades";
    return nullptr;
}
const char* MotionOutput::fog_frame_parameters(renderer::FogFrame& in, float weight, bool& sun_tracked) noexcept {
    const char* skip = nullptr;
    auto& q = in.params;
    const float jitter_x = in.width ? 2.f * jitter_[0] / float(in.width) : 0.f, jitter_y = in.height ? -2.f * jitter_[1] / float(in.height) : 0.f;
    q.m00 = camera_scene_.m00; q.m11 = camera_scene_.m11;
    q.m20 = camera_scene_.m20 + jitter_x + (in.width ? renderer::quad_pixel_centre_m20(in.width) : 0.f);
    q.m21 = camera_scene_.m21 + jitter_y + (in.height ? renderer::quad_pixel_centre_m21(in.height) : 0.f);
    q.m22 = ao_default_m22; q.m32 = ao_default_m32;
    q.tau_max = fog_strength_ * weight; q.anisotropy = fog_anisotropy_; q.margin = renderer::shadow_cascade_select_margin;
    q.decode_exponent = hdr_config_.decode == x3::temporal::AgxDecode::none ? 1.f : 2.2f;
    q.jitter_index = counters_.jitter_index; q.update_sky = frame_ % 32u == 0u; q.sky_blend = .5f; // the hue is slow: two tiny quads (~0.2-0.4 ms fenced, two render-pass switches) on 1 frame in 32, ~1 s time constant
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
    return skip;
}
void MotionOutput::fog_card_transition(unsigned mode) noexcept {
    if (fog_card_mode_ == mode) return;
    fog_card_mode_ = mode;
    invalidate_taa(TaaInvalidateSite::FogTransition);
}
void MotionOutput::fault_fog_cards(const char* reason) noexcept {
    if (!fog_cards_.fault) {
        fog_card_fault_reason_ = reason;
        // One fault line per Reset interval, even after the bounded frame
        // diagnostics have exhausted their budget. Draw roots stay light.
        call_preserved([&] { log("volumetric_fog_cards_fault device=%llu frame=%llu reason=%s observed=%u suppressed=%u until=reset",
            id_, frame_, reason, fog_cards_.observed, fog_cards_.suppressed); });
    }
    fog_cards_.fail();
    fog_card_transition(3);
}
void MotionOutput::prepare_volumetric_fog_targets(UINT width, UINT height) noexcept {
    // Size/caps work belongs at the HDR owner latch. Steady frames issue no
    // calls here, including no reference probes. Device loss retries on Reset.
    if (!fog_ || !fog_->caps().enabled || fog_cards_.fault || fog_->resources_ready(width, height)) return;
    HRESULT prepared = S_OK;
    taa_call([&] { prepared = fog_->prepare(width, height); });
    if (FAILED(prepared) && prepared != D3DERR_DEVICELOST && prepared != D3DERR_DEVICENOTRESET)
        fault_fog_cards("targets");
}
void MotionOutput::complete_volumetric_fog(const char* skip, HRESULT result, const renderer::FogResult& out) noexcept {
    const bool success = !skip && result == S_OK && out.applied && out.restore == S_OK;
    const char* reason = skip ? skip : "pass_incomplete";
    if (fog_cards_replace_) {
        if (fog_cards_.suppressed && !success) fault_fog_cards(reason);
        fog_cards_.finish(success);
        if (!fog_cards_.fault) fog_card_transition(success ? (!fog_cards_.warmup && !fog_cards_.refused ? 2u : 1u) : 0u);
    } else fog_card_transition(success ? 1u : 0u);
}
void MotionOutput::volumetric_fog_begin_frame() noexcept {
    if (!fog_requested_) return;
    fog_card_ready_checked_ = fog_card_ready_ = false;
    fog_cards_.begin(fog_enabled_ && !fog_disabled_ && !fog_attach_failed_ && fog_strength_ > 0.f);
    if (!fog_cards_.active) fog_card_transition(fog_cards_.fault ? 3u : 0u);
}
void MotionOutput::prepare_fog_card(const MotionDrawCall& call, MotionRoute& route) noexcept {
    // This cached source identity is set only by shader setters/resync. Source
    // observation precedes every admission choice, including reused bindings.
    fog_latch_.card(frame_);
    ++fog_cards_.observed;
    if (!fog_cards_.may_replace()) return;
    static_assert(D3DPT_TRIANGLELIST == 4 && D3DDECLTYPE_FLOAT16_4 == 16 && D3DZB_FALSE == 0 && D3DCULL_NONE == 1 &&
        D3DFILL_SOLID == 3 && D3DBLEND_ONE == 2 && D3DBLEND_INVSRCCOLOR == 4 && D3DBLENDOP_ADD == 1, "captured D3D9 enums");
    FogCardShape shape{call.indexed, call.user_memory, shadow_.stream0 != 0, shadow_.indices != 0,
        shadow_.declaration_stream0_only, false, unsigned(call.topology), call.primitives, call.vertex_count,
        shadow_.stream0_stride, shadow_.position_offset, shadow_.position_type, 0, shadow_.declaration};
    // Cached identity, geometry and caller gates precede all card-specific
    // reads. Keep production's hybrid unhook: no global setter observation is
    // needed for the few strict cards in a frame.
    if (!fog_enabled_ || fog_disabled_ || fog_attach_failed_ || !shadow_.fog_card_pair || !shape.static_matches() || !scene_bound() || shadow_.recording || active_queries_ ||
        composition_busy_ || composition_state_lost_ || motion_state_lost_ || hdr_state_ != HdrState::Active ||
        !hdr_ || !hdr_->target() || main_msaa_ || !taa_enabled_ || taa_failed_ || counters_.taa.attempted ||
        !jitter_active_ || !counters_.filled || !depth_surface_ || !depth_enabled_ || fog_frame_ == frame_) {
        fog_cards_.reject(); return;
    }
    // Hooks on: validated shadow; hooks off: the existing current-draw cache,
    // invalidated by before_draw. Never reuse another draw's stream frequency.
    shape.frequency_known = SUCCEEDED(direct_call<GetStreamFreqFn>(GetStreamSourceFreq, 0, &shape.frequency));
    if (!shape.matches()) { fog_cards_.reject(); return; }
    const FogCardStates states{state_field(0), state_field(1), state_field(2), state_field(3), state_field(4),
        state_field(30), state_field(29), state_field(31),
        blend_known(0) ? composition_blend_field(0) : -1, blend_known(1) ? composition_blend_field(1) : -1,
        blend_known(2) ? composition_blend_field(2) : -1, blend_known(3) ? composition_blend_field(3) : -1};
    if (!states.matches()) { fog_cards_.reject(); return; }
    if (!fog_card_ready_checked_) {
        fog_card_ready_checked_ = true;
        // Shared parameter/sun validation includes floating ABI returns. Keep
        // it behind the existing full CPU envelope for these light draw roots.
        call_preserved([&] {
            renderer::FogFrame in{}; bool sun = false;
            in.width = target_width_; in.height = target_height_;
            fog_card_ready_ = !fog_frame_prerequisite() && fog_ && fog_->resources_ready(in.width, in.height) &&
                !fog_frame_parameters(in, fog_latch_.weight(), sun);
        });
    }
    if (!fog_card_ready_) { fog_cards_.reject(); return; }
    call_preserved([&] {
        route.fog_card_mask.begin(7, [&](DWORD mask) { return native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE, mask); });
    });
    if (route.fog_card_mask.masked) {
        ++fog_cards_.suppressed;
        fog_card_transition(2);
    } else {
        fog_cards_.reject();
        route.preparation_error = route.fog_card_mask.operation;
        if (route.fog_card_mask.restore < 0) {
            motion_state_lost_ = true; motion_state_error_ = route.fog_card_mask.restore;
            invalidate_render_states(); fault_fog_cards("mask_prepare_restore");
            route.submit = false; route.submission_error = motion_state_error_;
        }
    }
}
void MotionOutput::finish_fog_card(MotionRoute& route, HRESULT result) noexcept {
    // Runs even after a failed native draw, before after_draw's early returns.
    call_preserved([&] {
        route.fog_card_mask.end([&](DWORD mask) { return native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE, mask); });
    });
    if (route.fog_card_mask.restore < 0) {
        motion_state_lost_ = true; motion_state_error_ = route.fog_card_mask.restore;
        invalidate_render_states(); fault_fog_cards("mask_restore");
    } else if (FAILED(result)) fault_fog_cards("source_draw");
}
int MotionOutput::volumetric_fog_toggle() noexcept {
    if (!fog_requested_) return -1;
    fog_enabled_ = !fog_enabled_;
    fog_cards_.armed = false;
    fog_card_transition(fog_cards_.fault ? 3u : 0u);
    log("volumetric_fog_toggle device=%llu frame=%llu enabled=%u strength=%.4f anisotropy=%.2f disabled=%u", id_, frame_, unsigned(fog_enabled_), double(fog_strength_), double(fog_anisotropy_), unsigned(fog_disabled_));
    return fog_enabled_ ? 1 : 0;
}
int MotionOutput::volumetric_fog_step() noexcept {
    if (!fog_requested_) return -1;
    fog_strength_ = renderer::fog_strength_next(fog_strength_);
    if (!(fog_strength_ > 0.f)) { fog_cards_.armed = false; fog_card_transition(fog_cards_.fault ? 3u : 0u); }
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
    // A camera cut (gate jump, load, view switch) ends the sector hold at once; cards bound this frame keep it.
    if (cut_finished_ && counters_.cut) fog_latch_.cut(frame_);
    const float weight = fog_latch_.update(frame_, fog_everywhere_);
    const char* skip = nullptr;
    HRESULT hr = S_FALSE;
    IDirect3DSurface9* rt0 = nullptr; IDirect3DTexture9* depth = nullptr;
    renderer::FogResult out{};
    renderer::FogFrame in{};
    double us = 0.;
    bool sun_tracked = false;
    if (fog_cards_replace_ && fog_cards_.fault) skip = "card_fault";
    else if (fog_cards_replace_ && !fog_cards_.medium_allowed()) skip = "card_refused";
    else if (!fog_enabled_) skip = "toggled_off";
    else if (fog_disabled_) skip = "disabled";
    else if (fog_attach_failed_) skip = "attach"; // until the next Reset, like the AO and sun-apply passes
    else if (!(weight > 0.f)) skip = "sector";
    else if (!(fog_strength_ * weight > 0.f)) skip = "strength";
    else if ((skip = fog_frame_prerequisite()) != nullptr) {}
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
        skip = fog_frame_parameters(in, weight, sun_tracked);
    }
    if (!skip) {
        if (!fog_) { try { fog_ = std::make_unique<renderer::FogPass>(); } catch (...) { disable_volumetric_fog("allocation", E_OUTOFMEMORY); skip = "disabled"; } }
        if (!skip && !fog_->caps().enabled) {
            // A refused or failed attach (the adapter query included) is retried after the next Reset, not held for the session.
            D3DDISPLAYMODE display{};
            hr = native<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &display);
            const bool queried = SUCCEEDED(hr);
            if (queried) taa_call([&] { hr = fog_->attach(device_, native_, caps_, display.Format); });
            const bool attached = SUCCEEDED(hr) && fog_->caps().enabled;
            if (fog_logs_ < 64) {
                ++fog_logs_;
                log("volumetric_fog_device device=%llu frame=%llu attached=%u reason=%s result=%08lx slots=%u strength=%.4f anisotropy=%.2f everywhere=%u retry=reset", id_, frame_, unsigned(attached),
                    attached ? "ok" : queried ? fog_->caps().reason : "adapter_query", hr, fog_->caps().largest_program_slots, double(fog_strength_), double(fog_anisotropy_), unsigned(fog_everywhere_));
            }
            if (!attached) { fog_attach_failed_ = true; skip = "attach"; }
        }
        if (!skip && fog_->reset_pending()) skip = "reset_pending";
    }
    if (!skip) {
        in.caller_scene_open = scene_open_; in.caller_stateblock_recording = shadow_.recording; in.caller_queries_idle = active_queries_ == 0;
        LARGE_INTEGER t0{}, t1{}, f{};
        if (fog_timing_) QueryPerformanceCounter(&t0);
        taa_call([&] { hr = fog_->execute(in, &out); });
        if (fog_timing_) { QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&f); us = f.QuadPart ? double(t1.QuadPart - t0.QuadPart) * 1e6 / double(f.QuadPart) : 0.; }
        if (FAILED(out.restore)) {
            invalidate_render_states();
            if (fog_cards_replace_ && fog_cards_.suppressed) { motion_state_lost_ = true; motion_state_error_ = out.restore; }
        }
        if (FAILED(hr)) {
            skip = "failed";
            const bool device_lost = hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET || out.operation == D3DERR_DEVICELOST || out.operation == D3DERR_DEVICENOTRESET;
            // Once cards were suppressed the replacement's one-failure latch
            // owns recovery; do not also set the older session-sticky disable.
            if (fog_cards_replace_ && fog_cards_.suppressed) {
                if (device_lost) skip = "device_lost";
            } else switch (renderer::fog_failure_action(device_lost, out.failed == renderer::FogStage::Targets, fog_failures_)) {
            case renderer::FogFailureAction::Retry: skip = "device_lost"; break; // not counted: the frame after Reset retries
            case renderer::FogFailureAction::Count: ++fog_failures_; break;
            case renderer::FogFailureAction::DisableSession: disable_volumetric_fog(out.failed == renderer::FogStage::Targets ? "targets" : "failures", hr); break;
            }
        } else { fog_failures_ = 0; ++fog_applied_frames_; }
    }
    release(depth); release(rt0);
    complete_volumetric_fog(skip, hr, out);
    const char* reason = skip ? skip : "ok";
    // A change of state is one line (bounded); timing mode logs every frame.
    const bool changed = std::strcmp(reason, fog_last_reason_) != 0;
    if (fog_timing_ || (changed && fog_logs_ < 64)) {
        if (!fog_timing_) ++fog_logs_;
        log("volumetric_fog_frame device=%llu frame=%llu applied=%u reason=%s strength=%.4f weight=%.3f cards=%u cascades=%u sky=%u sun=%s cpu_us=%.1f calls=%u result=%08lx restore=%08lx stage=%u",
            id_, frame_, unsigned(!skip && out.applied), reason, double(fog_strength_), double(weight), unsigned(fog_latch_.cards_recent(frame_)), out.cascades_bound, unsigned(out.sky_updated),
            skip ? "none" : sun_tracked ? "tracked" : "fallback", us, out.device_calls, out.operation, out.restore, unsigned(out.failed));
    }
    const std::uint64_t card_report = std::uint64_t(fog_cards_.observed) | std::uint64_t(fog_cards_.suppressed) << 24 |
        std::uint64_t(fog_cards_.refused) << 48 | std::uint64_t(fog_card_ready_) << 49 | std::uint64_t(fog_cards_.warmup) << 50 |
        std::uint64_t(fog_cards_.fault) << 51 | std::uint64_t(!skip && out.applied) << 52;
    if (fog_cards_replace_ && (fog_timing_ || ((changed || card_report != fog_card_last_report_) && fog_card_logs_ < 64))) {
        if (!fog_timing_) ++fog_card_logs_;
        log("volumetric_fog_cards device=%llu frame=%llu observed=%u suppressed=%u refused=%u ready=%u warmup=%u applied=%u fault=%u reason=%s mode=%u",
            id_, frame_, fog_cards_.observed, fog_cards_.suppressed, fog_cards_.observed - fog_cards_.suppressed, unsigned(fog_card_ready_),
            unsigned(fog_cards_.warmup), unsigned(!skip && out.applied), unsigned(fog_cards_.fault), fog_card_fault_reason_, fog_card_mode_);
    }
    fog_card_last_report_ = card_report;
    fog_last_reason_ = reason;
}
