// Included by motion_output.cpp inside namespace x3m (as the shadow-replay
// fragments are): the volumetric sun fog at the scene end
// (docs/architecture/volumetric-fog.md, "Stage 1 implementation").
const char* MotionOutput::fog_frame_prerequisite() const noexcept {
    if (!taa_enabled_ || taa_failed_ || counters_.taa.attempted || main_msaa_ || !jitter_active_) return "taa"; // the jittered march needs this frame's resolve
    if (composition_state_lost_ || motion_state_lost_) return "state_lost";
    if (hdr_state_ != HdrState::Active || !hdr_ || !hdr_->target()) return "owner";
    if (!counters_.filled || !depth_surface_ || !depth_enabled_ || lane_depth_format() != D3DFMT_A32B32G32R32F) return "linear_depth";
    if (sun_lane_failed_ || sun_frame_.failed) return "depth_producer_failed";
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
    q.m22 = projection_default_m22; q.m32 = projection_default_m32;
    q.density_scale = fog_sector_.density_scale * weight; q.anisotropy = fog_anisotropy_; q.margin = renderer::shadow_cascade_select_margin;
    q.decode_exponent = hdr_config_.decode == x3::temporal::AgxDecode::none ? 1.f : 2.2f;
    unsigned slots[renderer::shadow_cascade_max]{};
    const unsigned count = renderer::shadow_cascade_apply_slots(depth_cascades_, slots), first = count > 1 ? 1u : 0u;
    float sun_rows[12]{}; bool sun_known = false;
    renderer::ShadowReplayBasis current{};
    if (!skip && !sun_known && count) {
        // No valid map: the direction from this frame's would-be basis of the first slot (the apply's absent-cascade path).
        const unsigned i = slots[first < count ? first : 0];
        const float* sun = cascade_sun(i);
        sun_known = sun && renderer::shadow_replay_basis(camera_scene_, sun, depth_cascades_.cascades[i], current, point_sun_.grid_anchor(i)) &&
                    renderer::shadow_replay_view_rows(camera_scene_, current, depth_cascades_.cascades[i], sun_rows);
    }
    // View-space direction toward the sun: the basis' world depth axis (pointing away from it) through the latch's FORWARD
    // rotation, d_view = d_world . R. Not the view rows' depth row: that is the covector axis . R^-1^T (it measures depth
    // of a view position), which equals the direction only for an orthonormal R; on the engine's 16.16 basis the two
    // differ by ~2e-5 rad and fog_world_basis below would apply R^-1 a second time. With the direction,
    // fog_world_basis' d_view . R^-1 returns the world axis exactly: one world for the maps and the volume.
    if (sun_known) for (unsigned j = 0; j < 3; ++j) { double v = 0; for (unsigned i = 0; i < 3; ++i) v += current.axes[2][i] * double(camera_scene_.r[i * 3 + j]); sun_rows[8 + j] = float(v); }
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
    if (!skip) {
        double rotation[9], translation[3], sun[3];
        for (unsigned i = 0; i < 9; ++i) rotation[i] = camera_scene_.r[i];
        for (unsigned i = 0; i < 3; ++i) { translation[i] = camera_scene_.t[i]; sun[i] = q.sun_view[i]; }
        if (!renderer::fog_world_basis(rotation, translation, sun, q.world)) skip = "world_basis";
    }
    // Sampling is independent of surface-shadow application/refusal. Retained
    // replay publication plus an exact frame match authorizes each map; an
    // alternate-frame far map is deliberately unavailable to this volume.
    in.frame = frame_; in.count = 0;
    for (unsigned s = 0; !skip && s < count && in.count < renderer::fog_cascade_max; ++s) {
        const unsigned i = slots[s];
        if (i == 0) continue; // own-ship-only map is not a general scene volume
        auto& k = in.cascades[in.count++];
        const auto* kept = depth_replay_->retained(i);
        const auto& cascade = depth_cascades_.cascades[i];
        renderer::SunShadowBias bias{};
        if (!kept || !renderer::fog_shadow_current(frame_, kept->frame) || !depth_replay_->map_texture(i) ||
            !renderer::shadow_replay_view_rows(camera_scene_, kept->basis, cascade, k.rows) ||
            !renderer::sun_shadow_apply_bias(sun_apply_bias_units_, sun_apply_clamp_texels_,
                double(cascade.half_extent), cascade.depth_half(), depth_replay_->size(i), bias)) continue;
        k.map = depth_replay_->map_texture(i); k.bias = bias.constant; k.frame = kept->frame; k.valid = true;
        // The visibility pass's penumbra: the map's world texel (2 E / N of the map actually bound) and its depth range.
        const unsigned size = depth_replay_->size(i);
        k.texel_world = size ? float(2. * double(cascade.half_extent) / double(size)) : 0.f; k.depth_range = float(cascade.depth_range());
    }
    if (!skip && !renderer::fog_valid_params(q)) skip = "parameters";
    return skip;
}
void MotionOutput::fog_transition_invalidate() noexcept {
    if (fog_transition_frame_ == frame_) return;
    fog_transition_frame_ = frame_;
    invalidate_taa(TaaInvalidateSite::FogTransition);
}
void MotionOutput::fog_card_transition(unsigned mode) noexcept {
    if (fog_card_mode_ == mode) return;
    fog_card_mode_ = mode;
    fog_transition_invalidate();
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
bool MotionOutput::attach_volumetric_fog() noexcept {
    if (fog_attach_failed_) return false;
    if (!fog_) {
        try { fog_ = std::make_unique<renderer::FogPass>(); }
        catch (...) { disable_volumetric_fog("allocation", E_OUTOFMEMORY); return false; }
    }
    if (fog_->caps().enabled) return true;
    D3DDISPLAYMODE display{};
    HRESULT hr = native<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &display);
    const bool queried = SUCCEEDED(hr);
    if (queried) taa_call([&] { hr = fog_->attach(device_, native_, caps_, display.Format); });
    const bool attached = SUCCEEDED(hr) && fog_->caps().enabled;
    log("volumetric_fog_device device=%llu frame=%llu attached=%u reason=%s result=%08lx slots=%u retry=reset", id_, frame_, unsigned(attached),
        attached ? "ok" : queried ? fog_->caps().reason : "adapter_query", hr, fog_->caps().largest_program_slots);
    if (!attached) fog_attach_failed_ = true;
    return attached;
}
void MotionOutput::prepare_volumetric_fog_targets(UINT width, UINT height) noexcept {
    // Only the owner latch prepares; no decode/upload/Get* in card brackets.
    if (!fog_sector_.current(frame_) || fog_cards_.fault || fog_disabled_ || fog_attach_failed_) return;
    const auto profile = static_cast<renderer::fog_field::Profile>(fog_sector_.profile);
    if (fog_ && fog_->resources_ready(width, height, profile, fog_sector_.recipe, fog_sector_.field_generation)) return;
    LARGE_INTEGER t0{}, t1{}, frequency{};
    QueryPerformanceCounter(&t0);
    if (!attach_volumetric_fog()) return;
    HRESULT prepared = S_OK;
    taa_call([&] {
        HMODULE module = nullptr;
        static const char resource_anchor = 0;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&resource_anchor), &module)) prepared = E_FAIL;
        else prepared = fog_->prepare_field(module, profile);
        if (SUCCEEDED(prepared)) prepared = fog_->prepare_targets(width, height);
    });
    QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&frequency);
    const double us = frequency.QuadPart ? double(t1.QuadPart - t0.QuadPart) * 1e6 / double(frequency.QuadPart) : 0.;
    log("volumetric_fog_prepare device=%llu frame=%llu profile=%u field_generation=%llu width=%u height=%u result=%08lx cpu_us=%.1f",
        id_, frame_, fog_sector_.profile, fog_->field_generation(), width, height, prepared, us);
    if (SUCCEEDED(prepared)) {
        if (fog_sector_.field_generation != fog_->field_generation()) {
            fog_sector_.field_generation = fog_->field_generation();
            fog_cards_.armed = false; fog_cards_.warmup = fog_cards_.active;
        }
    } else if (prepared != D3DERR_DEVICELOST && prepared != D3DERR_DEVICENOTRESET) fault_fog_cards("prepare");
}
// Stored-density range: a new readiness epoch (enable, sector re-key, load gap, residency loss).
// Bounded: at most 64 lines a session outside timing mode.
void MotionOutput::fog_density_epoch(const char* reason) noexcept {
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    fog_density_epoch_qpc_ = now.QuadPart; fog_density_ready_logged_[0] = fog_density_ready_logged_[1] = false;
    if (fog_timing_ || fog_density_logs_ < 64u) {
        if (!fog_timing_) ++fog_density_logs_;
        log("volumetric_fog_cache device=%llu frame=%llu event=epoch reason=%s sector_key=%016llx offset=%.0f,%.0f,%.0f", id_, frame_, reason,
            fog_density_config_.sector_key, fog_density_config_.world_offset[0], fog_density_config_.world_offset[1], fog_density_config_.world_offset[2]);
    }
}
// Owner latch, after the family field and targets: posts the previous scene end's camera,
// uploads under the budget and advances the ramps. Never in a draw bracket; never waits.
// Toggled off, refused, faulted or without a current fogged sector it does nothing, and the
// worker parks by itself once its window is complete.
void MotionOutput::prepare_volumetric_fog_density(UINT width, UINT height) noexcept {
    fog_density_prepared_ = false;
    if (fog_density_refused_ || !fog_ || !fog_enabled_ || fog_disabled_ || fog_attach_failed_ || fog_cards_.fault || !fog_density_camera_valid_ ||
        !fog_sector_.current(frame_) || !(fog_strength_ > 0.f)) return;
    const auto profile = static_cast<renderer::fog_field::Profile>(fog_sector_.profile);
    if (!fog_->resources_ready(width, height, profile, fog_sector_.recipe, fog_sector_.field_generation)) return;
    HRESULT hr = E_FAIL; bool family = false;
    const FogSectorPlacement placement = fog_sector_placement(fog_sector_);
    const bool rekeyed = !fog_density_config_.enabled || placement.key != fog_density_key_;
    taa_call([&] {
        auto& c = fog_density_config_;
        family = fog_->field_family(c.chroma, &c.sigma);
        if (!family) return;
        c.enabled = true; c.sector_key = placement.key; c.recipe = fog_sector_.recipe;
        for (unsigned i = 0; i < 3; ++i) c.world_offset[i] = placement.offset[i];
        hr = fog_->prepare_density(c, fog_density_camera_, frame_);
    });
    if (!family) {
        // No tracked family constants for this profile: say so once and keep the legacy path.
        fog_density_refused_ = true;
        log("volumetric_fog_cache device=%llu frame=%llu event=refused reason=family_constants profile=%u fallback=legacy result=%08lx", id_, frame_, fog_sector_.profile, static_cast<unsigned long>(E_FAIL));
        return;
    }
    const auto& status = fog_->density_status();
    if (hr == D3DERR_NOTAVAILABLE) {
        // Capability refusal: the legacy family path stays exactly as without the option, until release.
        fog_density_refused_ = true;
        log("volumetric_fog_cache device=%llu frame=%llu event=refused reason=%s fallback=legacy result=%08lx", id_, frame_, status.reason, hr);
        return;
    }
    if (!fog_density_config_logged_) {
        fog_density_config_logged_ = true;
        log("volumetric_fog_cache device=%llu frame=%llu event=config mode=stored result=%08lx profile=%u sigma=%.4g chroma=%.4f,%.4f,%.4f atlas_bytes=%u upload_budget_bytes=%u upload_rects=%u ramp_frames=%u shadow_pass=%u",
            id_, frame_, hr, fog_sector_.profile, double(fog_density_config_.sigma), double(fog_density_config_.chroma[0]), double(fog_density_config_.chroma[1]), double(fog_density_config_.chroma[2]),
            unsigned(fog::kAtlasBytes), unsigned(fog::kDefaultUploadBudget), fog::kDefaultUploadRects, fog::kReadinessRampFrames, unsigned(fog_density_config_.shadow_pass));
    }
    if (status.shadow_pass_refused && !fog_shadow_pass_refused_logged_) {
        // The grid could not be built (or its column cap is unusable): the stored fog draws with the in-march programs.
        fog_shadow_pass_refused_logged_ = true;
        log("fog_shadow_pass_refused device=%llu frame=%llu reason=%s fallback=in_march_lookup", id_, frame_, status.shadow_pass_refused);
    }
    if (status.motes_refused && !fog_motes_refused_logged_) {
        // The mote stage could not be built or drew once without success (not a lost device): the fog draws without it.
        fog_motes_refused_logged_ = true;
        log("fog_dust_motes_refused device=%llu frame=%llu reason=%s fallback=fog_without_motes", id_, frame_, status.motes_refused);
    }
    if (FAILED(hr)) return; // device loss or a transient failure: no fog this frame, retried at the next latch
    fog_density_prepared_ = true;
    if (rekeyed) { fog_density_key_ = placement.key; fog_density_epoch("sector_key"); }
    const float ready[2] = {status.ready_far, status.ready_fine};
    for (unsigned i = 0; i < 2; ++i) {
        if (ready[i] >= 1.f && !fog_density_ready_logged_[i]) {
            fog_density_ready_logged_[i] = true;
            if (fog_timing_ || fog_density_logs_ < 64u) {
                if (!fog_timing_) ++fog_density_logs_;
                LARGE_INTEGER now{}, f{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&f);
                log("volumetric_fog_cache device=%llu frame=%llu event=%s ms=%.1f nodes=%llu worker_busy_ms=%.1f upload_bytes_total=%llu missed_locks=%llu", id_, frame_, i ? "fine_ready" : "far_ready",
                    f.QuadPart ? double(now.QuadPart - fog_density_epoch_qpc_) * 1e3 / double(f.QuadPart) : 0., status.nodes_generated, double(status.worker_busy_us) * 1e-3, status.upload_bytes_total, status.missed_locks);
            }
        } else if (i == 0 && ready[0] == 0.f && fog_density_ready_logged_[0]) fog_density_epoch("residency"); // cut, jump or Reset: far refills and ramps again
    }
}
void MotionOutput::volumetric_fog_sector_sample(std::uint64_t frame, const sector_background::Sample& sample) noexcept {
    if (!fog_requested_ || frame != frame_ || fog_sector_.frame == frame) return;
    auto next = fog_sector_frame(sample, frame, generation_, fog_strength_, fog_enabled_ && !fog_disabled_, fog_everywhere_);
    if (fog_density_requested_) {
        // A gap in scene samples longer than fog_density_gap_ms of wall clock is a load or a sector
        // transit: refill and ramp instead of popping in. A shorter gap (a stutter, a skipped sample)
        // keeps the cache. A sector change re-keys the cache by itself; cuts and first-person/chase
        // switches are residency questions the cache answers per frame (world-anchored field).
        LARGE_INTEGER now{}, f{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&f);
        const bool gap = fog_density_sample_frame_ != ~std::uint64_t(0) && fog_density_sample_frame_ + 1 < frame && f.QuadPart > 0 &&
            (now.QuadPart - fog_density_sample_qpc_) * 1000 > static_cast<long long>(fog_density_gap_ms) * f.QuadPart;
        fog_density_sample_frame_ = frame; fog_density_sample_qpc_ = now.QuadPart;
        if (gap && fog_ && fog_density_config_.enabled && !fog_density_refused_) { fog_->invalidate_density(); fog_density_epoch("sample_gap"); }
    }
    // Atlas generation remains usable across same-family sectors, but their
    // replacement warm-up/history key must still change.
    if (next.profile == fog_sector_.profile && next.generation == fog_sector_.generation) next.field_generation = fog_sector_.field_generation;
    if (!next.same_key(fog_sector_)) {
        fog_cards_.armed = false;
        fog_transition_invalidate();
        log("volumetric_fog_sector device=%llu frame=%llu profile=%u reason=%s sector=%08x index=%d generation=%llu density_scale=%.3f forced_profile=%s",
            id_, frame_, next.profile, next.reason, next.sector, next.index, next.generation, double(next.density_scale), next.forced ? "bluewell" : "none");
    }
    fog_sector_ = next;
    fog_cards_.active = next.enabled && !fog_cards_.fault;
    fog_cards_.warmup = fog_cards_.active && !fog_cards_.armed;
    if (!fog_cards_.active) fog_card_transition(fog_cards_.fault ? 3u : 0u);
}
void MotionOutput::reconcile_volumetric_fog(const renderer::FogFrame& in, const renderer::FogResult& out, HRESULT hr) noexcept {
    if (out.scene_known) scene_open_ = out.scene_open;
    if (out.route_poisoned || !out.scene_known || out.scene_open != in.caller_scene_open || !out.caller_state_restored || FAILED(out.restore)) {
        // State/scene loss poisons keep and replacement alike. Existing
        // resolve/writeback guards and draw submission stop until Reset.
        motion_state_lost_ = true;
        motion_state_error_ = FAILED(out.restore) ? out.restore : FAILED(hr) ? hr : E_FAIL;
        invalidate_render_states(); invalidate_taa(TaaInvalidateSite::StateLost);
    }
}
void MotionOutput::complete_volumetric_fog(const char* skip, HRESULT result, const renderer::FogResult& out) noexcept {
    const bool success = !skip && result == S_OK && out.applied && out.restore == S_OK && out.caller_state_restored && !out.route_poisoned;
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
    if (fog_sector_.frame + 1 < frame_) fog_cards_.armed = false;
    // Stored range: while the far ramp is incomplete the cards stay and the ramping medium stacks
    // on them (warm-up), so the hand-over never shows less fog than either medium alone.
    if (fog_density_requested_ && !fog_density_refused_ && !(fog_ && fog_density_prepared_ && fog_->density_status().ready_far >= 1.f)) fog_cards_.armed = false;
    fog_cards_.begin(fog_enabled_ && !fog_disabled_ && !fog_attach_failed_ && fog_strength_ > 0.f);
    if (!fog_cards_.active) fog_card_transition(fog_cards_.fault ? 3u : 0u);
}
void MotionOutput::prepare_fog_card(const MotionDrawCall& call, MotionRoute& route) noexcept {
    // This cached source identity is set only by shader setters/resync. Source
    // observation precedes every admission choice, including reused bindings.
    fog_latch_.card(frame_);
    ++fog_cards_.observed;
    if (!fog_sector_.current(frame_) || !fog_cards_.may_replace()) return;
    static_assert(D3DPT_TRIANGLELIST == 4 && D3DDECLTYPE_FLOAT16_4 == 16 && D3DZB_FALSE == 0 && D3DCULL_NONE == 1 &&
        D3DFILL_SOLID == 3 && D3DBLEND_ONE == 2 && D3DBLEND_INVSRCCOLOR == 4 && D3DBLENDOP_ADD == 1, "captured D3D9 enums");
    FogCardShape shape{call.indexed, call.user_memory, shadow_.stream0 != 0, shadow_.indices != 0,
        shadow_.declaration_stream0_only, false, unsigned(call.topology), call.primitives, call.vertex_count,
        shadow_.stream0_stride, shadow_.position_offset, shadow_.position_type, 0, shadow_.declaration};
    // Cached identity, geometry and caller gates precede all card-specific
    // reads. Keep production's hybrid unhook: no global setter observation is
    // needed for the few strict cards in a frame.
    if (!fog_enabled_ || fog_disabled_ || fog_attach_failed_ || !shadow_.fog_card_pair || !shape.static_matches() || !scene_open_ || !scene_bound() || shadow_.recording || active_queries_ ||
        composition_busy_ || composition_state_lost_ || motion_state_lost_ || hdr_state_ != HdrState::Active ||
        !hdr_ || !hdr_->target() || main_msaa_ || !taa_enabled_ || taa_failed_ || counters_.taa.attempted ||
        !jitter_active_ || !counters_.filled || !depth_surface_ || !depth_enabled_ || lane_depth_format() != D3DFMT_A32B32G32R32F || sun_lane_failed_ || sun_frame_.failed || fog_frame_ == frame_) {
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
            fog_card_ready_ = !fog_frame_prerequisite() && fog_ && fog_->resources_ready(in.width, in.height, static_cast<renderer::fog_field::Profile>(fog_sector_.profile), fog_sector_.recipe, fog_sector_.field_generation) &&
                !fog_frame_parameters(in, 1.f, sun) && (!fog_density_active() || (fog_density_prepared_ && fog_->density_status().ready_far >= 1.f && fog_->density_drawable(in.params.world.origin)));
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
    log("volumetric_fog_toggle device=%llu frame=%llu enabled=%u strength=%.4f density_scale=%.3f anisotropy=%.2f disabled=%u", id_, frame_, unsigned(fog_enabled_), double(fog_strength_), double(fog_strength_ / .02f), double(fog_anisotropy_), unsigned(fog_disabled_));
    return fog_enabled_ ? 1 : 0;
}
int MotionOutput::volumetric_fog_shadow_pass_toggle() noexcept {
    if (!fog_shadow_pass_launch_) return -1;
    // Only the proxy's copy changes here, at the frame boundary; FogPass latches it at the next prepare_density, so a
    // frame never mixes the variants. Off draws the in-march programs exactly as a launch without the pass; the grid
    // target and programs stay allocated (a Reset while off releases the target with the others, on re-creates it).
    fog_density_config_.shadow_pass = !fog_density_config_.shadow_pass;
    const char* refused = fog_ && fog_->grid_refused() ? fog_->density_status().shadow_pass_refused : nullptr;
    log("fog_shadow_pass_toggle device=%llu frame=%llu enabled=%u refused=%s key=ctrl_shift_f11", id_, frame_, unsigned(fog_density_config_.shadow_pass), refused ? refused : "none");
    return fog_density_config_.shadow_pass ? 1 : 0;
}
int MotionOutput::volumetric_fog_dust_motes_toggle() noexcept {
    if (!fog_dust_motes_launch_) return -1;
    // The proxy's copy only, at the frame boundary; FogPass latches it at the next prepare_density. Off skips the stage
    // (the transaction is the launch-off one); the mote programs and buffers stay allocated.
    fog_density_config_.dust_motes = !fog_density_config_.dust_motes;
    const char* refused = fog_ && fog_->motes_refused() ? fog_->density_status().motes_refused : nullptr;
    log("fog_dust_motes_toggle device=%llu frame=%llu enabled=%u refused=%s key=ctrl_alt_f11", id_, frame_, unsigned(fog_density_config_.dust_motes), refused ? refused : "none");
    return fog_density_config_.dust_motes ? 1 : 0;
}
int MotionOutput::volumetric_fog_step() noexcept {
    if (!fog_requested_) return -1;
    fog_strength_ = renderer::fog_strength_next(fog_strength_);
    fog_transition_invalidate();
    if (!(fog_strength_ > 0.f)) { fog_cards_.armed = false; fog_card_transition(fog_cards_.fault ? 3u : 0u); }
    log("volumetric_fog_strength device=%llu frame=%llu strength=%.4f density_scale=%.3f enabled=%u", id_, frame_, double(fog_strength_), double(fog_strength_ / .02f), unsigned(fog_enabled_));
    return int(fog_strength_ * 1000.f + .5f);
}
void MotionOutput::disable_volumetric_fog(const char* why, HRESULT result) noexcept {
    fog_disabled_ = true;
    log("volumetric_fog_disabled device=%llu frame=%llu reason=%s result=%08lx session=1", id_, frame_, why, result);
}
// Once per owning scene/frame, after the sun-shadow apply and before TAA. The frozen engine
// family is authoritative; the shader-source latch remains diagnostic only.
// A late refusal after card suppression cannot recreate their colors and
// therefore trips the existing Reset-only replacement fault latch.
void MotionOutput::run_volumetric_fog() noexcept {
    if (fog_frame_ == frame_) return; // the hook and the bloom-copy sites both qualify
    fog_frame_ = frame_;
    // A cut expires only the diagnostic source hold; current engine authority
    // and successful spatial replacement readiness remain independent of it.
    if (cut_finished_ && counters_.cut) fog_latch_.cut(frame_);
    fog_latch_.update(frame_, fog_everywhere_); // source observation only
    if (fog_density_requested_ && camera_scene_.valid) {
        // The next owner latch posts this camera. Independent of every refusal below: a refused
        // or filling frame must still move the cache's window, or it could never become ready.
        double rotation[9], translation[3], camera[3];
        for (unsigned i = 0; i < 9; ++i) rotation[i] = camera_scene_.r[i];
        for (unsigned i = 0; i < 3; ++i) translation[i] = camera_scene_.t[i];
        if (renderer::fog_world_camera(rotation, translation, camera)) {
            for (unsigned i = 0; i < 3; ++i) fog_density_camera_[i] = camera[i];
            fog_density_camera_valid_ = true;
        }
    }
    const float weight = 1.f;
    const char* skip = nullptr;
    HRESULT hr = S_FALSE;
    IDirect3DSurface9* rt0 = nullptr; IDirect3DTexture9* depth = nullptr;
    renderer::FogResult out{};
    renderer::FogFrame in{};
    double us = 0.;
    bool sun_tracked = false;
    if (fog_cards_.fault) skip = "card_fault";
    else if (fog_cards_replace_ && !fog_cards_.medium_allowed()) skip = "card_refused";
    else if (!fog_enabled_) skip = "toggled_off";
    else if (fog_disabled_) skip = "disabled";
    else if (fog_attach_failed_) skip = "attach"; // until the next Reset, like the sun-apply pass
    else if (!fog_sector_.current(frame_)) skip = fog_sector_.frame == frame_ ? fog_sector_.reason : "sample_missing";
    else if (!(fog_strength_ * weight > 0.f)) skip = "strength";
    else if ((skip = fog_frame_prerequisite()) != nullptr) {}
    else if (!sun_frame_.published) skip = "depth_unpublished";
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
    if (!skip && (!fog_ || !fog_->resources_ready(in.width, in.height,
        static_cast<renderer::fog_field::Profile>(fog_sector_.profile), fog_sector_.recipe, fog_sector_.field_generation))) skip = "unprepared";
    if (fog_density_requested_ && !skip) {
        for (unsigned i = 0; i < 3; ++i) in.camera_world[i] = in.params.world.origin[i]; // execute re-checks residency for it
        if (fog_density_active()) {
            // Not resident yet (sector entry, jump, Reset): no fog and no card suppression, never the legacy field.
            if (!fog_density_prepared_ || !fog_->density_ready(in.width, in.height)) skip = "density_unprepared";
            else if (!fog_->density_drawable(in.camera_world)) skip = "density_filling";
            else { in.density = true; in.look_resolved = jitter_active_ && taa_enabled_ && !taa_failed_; in.look_phase = in.look_resolved ? counters_.jitter_index : 0u; } // no resolve to average it: hold the shaft lookup at the bin centres
        }
    }
    if (!skip) {
        in.profile = static_cast<renderer::fog_field::Profile>(fog_sector_.profile);
        in.recipe_id = fog_sector_.recipe; in.field_generation = fog_sector_.field_generation;
        in.main_target = true; in.linear_depth_current = true; in.caller_scene_known = true;
        in.caller_scene_open = scene_open_; in.caller_stateblock_recording = shadow_.recording; in.caller_queries_idle = active_queries_ == 0;
        if (fog_dust_motes_launch_ && in.density) { // the motes' drift clock: seconds since the first mote frame
            LARGE_INTEGER now{}, frequency{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
            if (!fog_motes_epoch_qpc_) fog_motes_epoch_qpc_ = now.QuadPart;
            in.mote_seconds = frequency.QuadPart > 0 ? double(now.QuadPart - fog_motes_epoch_qpc_) / double(frequency.QuadPart) : 0.;
            in.mote_cut = cut_finished_ && counters_.cut; // the cut detector's verdict (view switch, roll-only cut): no streak
        }
        LARGE_INTEGER t0{}, t1{}, f{};
        if (fog_timing_) QueryPerformanceCounter(&t0);
        taa_call([&] { hr = fog_->execute(in, &out); });
        if (fog_timing_) { QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&f); us = f.QuadPart ? double(t1.QuadPart - t0.QuadPart) * 1e6 / double(f.QuadPart) : 0.; }
        reconcile_volumetric_fog(in, out, hr);
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
        } else if (hr == S_OK && out.applied) {
            fog_failures_ = 0; ++fog_applied_frames_;
        }
    }
    release(depth); release(rt0);
    complete_volumetric_fog(skip, hr, out);
    if (fog_dust_motes_launch_) fog_motes_drawn_ = !skip && out.motes; // the overlay's " MOTES"
    // Camera cuts invalidate TAA history, not readiness of this current-frame
    // spatial pass. Authority/resource changes and failures own rewarming;
    // disarming on a cut would stack native cards and volume on the next frame.
    const char* reason = skip ? skip : "ok";
    // A change of state is one line (bounded); timing mode logs every frame.
    const bool changed = std::strcmp(reason, fog_last_reason_) != 0;
    // Launched with the grid pass (fog-shadow-pass.md, "A/B toggle and log row"): which march variant this frame drew
    // and why, from FogPass's own latch and its per-frame grid report. A frame whose density transaction issued no
    // device call (skipped, refused at validation, or the zero-call S_FALSE exit) reads march=none
    // fallback=not_executed whatever the variant. A variant change forces a line at most once per
    // fog_grid_change_frames frames and at most fog_grid_change_cap times a session (its own counter, never the density
    // lines' budget); timing mode logs every frame anyway.
    const char* grid_march = "none"; const char* grid_fallback = "none"; const renderer::FogGridReport* grid = nullptr;
    bool grid_changed = false;
    if (fog_shadow_pass_launch_) {
        if (!fog_ || !in.density || out.device_calls == 0) grid_fallback = "not_executed";
        else if (fog_->grid_refused()) { grid_march = "in_march"; grid_fallback = "refused"; }
        else if (!fog_->grid_variant()) { grid_march = "in_march"; grid_fallback = "toggled_off"; }
        else if (fog_->grid_report().frame == frame_) { grid = &fog_->grid_report(); grid_march = grid->drawn ? "grid" : "grid_unshadowed"; grid_fallback = grid->unshadowed; }
        else grid_fallback = "not_executed";
        grid_changed = !fog_timing_ && std::strcmp(grid_march, fog_grid_last_march_) != 0 &&
            frame_ - fog_grid_logged_frame_ >= fog_grid_change_frames && fog_grid_change_logs_ < fog_grid_change_cap;
    }
    const bool periodic = fog_timing_ || changed || frame_ % 600u == 0u;
    if (periodic || grid_changed) {
        if (!fog_timing_) ++fog_logs_;
        if (grid_changed && !periodic) { ++fog_grid_change_logs_; fog_grid_logged_frame_ = frame_; }
        if (fog_shadow_pass_launch_) fog_grid_last_march_ = grid_march; // the variant the last line printed
        char grid_fields[320]; grid_fields[0] = '\0';
        if (fog_shadow_pass_launch_) {
            const renderer::FogGridReport none{};
            const auto& g = grid ? *grid : none;
            const char* refused = fog_ && fog_->grid_refused() ? fog_->density_status().shadow_pass_refused : nullptr;
            std::snprintf(grid_fields, sizeof grid_fields, " grid_pass=%u grid_built=%u grid_bind=%08lx march=%s fallback=%s grid_refused=%s grid_cascades=%u grid_kernel=%.3g,%.3g,%.3g grid_far_width=%.1f grid_frame_term=%.4f grid_calls=%u grid_net_calls=%d",
                unsigned(fog_density_config_.shadow_pass), unsigned(g.drawn), static_cast<unsigned long>(g.bind), grid_march, grid_fallback, refused ? refused : "none", g.cascades,
                double(g.kernel[0]), double(g.kernel[1]), double(g.kernel[2]), double(g.far_width), double(g.frame_term), g.calls, g.net_calls);
        }
        // Launched with the dust motes (fog-dust-motes.md section 4): the stage's report of this frame, zeros when the
        // density transaction did not take the stage (toggled off, refused, not executed).
        char mote_fields[200]; mote_fields[0] = '\0';
        if (fog_dust_motes_launch_) {
            const renderer::FogMoteReport none{};
            const auto& m = fog_ && in.density && out.device_calls && fog_->mote_report().frame == frame_ ? fog_->mote_report() : none;
            const char* refused = fog_ && fog_->motes_refused() ? fog_->density_status().motes_refused : nullptr;
            std::snprintf(mote_fields, sizeof mote_fields, " motes=%u mote_count=%u mote_calls=%u mote_shift_px=%.1f mote_streak=%u mote_shadow=%s mote_refused=%s",
                unsigned(!skip && out.motes), m.count, m.calls, double(m.shift_px), unsigned(m.streak), m.shadow, refused ? refused : "none");
        }
        log("volumetric_fog_frame device=%llu frame=%llu applied=%u reason=%s strength=%.4f density_scale=%.3f cards=%u profile=%u field_generation=%llu sun=%s shadow_maps=%u cpu_us=%.1f calls=%u result=%08lx restore=%08lx stage=%u%s%s",
            id_, frame_, unsigned(!skip && out.applied), reason, double(fog_strength_), double(fog_sector_.density_scale), unsigned(fog_latch_.cards_recent(frame_)), fog_sector_.profile, fog_sector_.field_generation,
            skip ? "none" : sun_tracked ? "tracked" : "fallback", out.cascades_bound, us, out.device_calls, out.operation, out.restore, unsigned(out.failed), grid_fields, mote_fields);
    }
    if (fog_timing_ && fog_density_requested_ && fog_) {
        const auto& d = fog_->density_status();
        log("volumetric_fog_cache_frame device=%llu frame=%llu density=%u refused=%u prepared=%u ready_far=%.4f ready_fine=%.4f upload_bytes=%u upload_rects=%u nodes=%llu worker_nodes_per_s=%.0f missed_locks=%llu",
            id_, frame_, unsigned(in.density && !skip), unsigned(fog_density_refused_), unsigned(fog_density_prepared_), double(d.ready_far), double(d.ready_fine), d.upload_bytes, d.upload_rects,
            d.nodes_generated, d.worker_busy_us ? double(d.nodes_generated) * 1e6 / double(d.worker_busy_us) : 0., d.missed_locks);
    }
    const std::uint64_t card_report = std::uint64_t(fog_cards_.observed) | std::uint64_t(fog_cards_.suppressed) << 24 |
        std::uint64_t(fog_cards_.refused) << 48 | std::uint64_t(fog_card_ready_) << 49 | std::uint64_t(fog_cards_.warmup) << 50 |
        std::uint64_t(fog_cards_.fault) << 51 | std::uint64_t(!skip && out.applied) << 52;
    fog_card_observed_total_ += fog_cards_.observed; fog_card_suppressed_total_ += fog_cards_.suppressed; fog_card_refused_total_ += unsigned(fog_cards_.refused);
    if (fog_cards_replace_ && (fog_timing_ || (changed || ((card_report != fog_card_last_report_) && frame_ - fog_card_logged_frame_ >= 60u) || frame_ % 600u == 0u))) {
        if (!fog_timing_) ++fog_card_logs_;
        fog_card_logged_frame_ = frame_;
        log("volumetric_fog_cards device=%llu frame=%llu observed=%u suppressed=%u refused=%u ready=%u warmup=%u applied=%u fault=%u reason=%s mode=%u observed_total=%llu suppressed_total=%llu refused_total=%llu",
            id_, frame_, fog_cards_.observed, fog_cards_.suppressed, unsigned(fog_cards_.refused), unsigned(fog_card_ready_),
            unsigned(fog_cards_.warmup), unsigned(!skip && out.applied), unsigned(fog_cards_.fault), fog_card_fault_reason_, fog_card_mode_, fog_card_observed_total_, fog_card_suppressed_total_, fog_card_refused_total_);
    }
    fog_card_last_report_ = card_report;
    fog_last_reason_ = reason;
}
