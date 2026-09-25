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
        fog_->configure_sync_timing(gpu_sync_); // --gpu-sync-timing only: the motes' pair
    }
    if (fog_->caps().enabled) return true;
    D3DDISPLAYMODE display{};
    HRESULT hr = native<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &display);
    const bool queried = SUCCEEDED(hr);
    if (queried) taa_call([&] { hr = fog_->attach(device_, native_, caps_, display.Format); });
    const bool attached = SUCCEEDED(hr) && fog_->caps().enabled;
    log("volumetric_fog_device device=%llu frame=%llu attached=%u reason=%s result=%08lx slots=%u retry=reset", id_, frame_, unsigned(attached),
        attached ? "ok" : queried ? fog_->caps().reason : "adapter_query", hr, fog_->caps().largest_program_slots);
    if (!attached) { fog_attach_failed_ = true; fog_->release_density_worker(); } // a prefill's worker does not wait for Reset with its 8.5 MB
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
    } else if (prepared == renderer::FogPass::field_row_disabled) {
        // A file family's packet failed: that row is disabled and the next scan leaves native
        // cards (family_unsupported), or a first allocation failure is retried at the next latch.
        // Not a session fault; at most one line per row for each outcome.
        const auto* table = renderer::fog_field::family_table();
        const auto* row = table ? table->row(profile) : nullptr;
        const char* why = row ? row->disabled.load(std::memory_order_relaxed) : nullptr;
        if (row && why && !row->reported.exchange(true, std::memory_order_relaxed))
            log("volumetric_fog_family device=%llu frame=%llu event=row_disabled name=\"%s\" profile=%u reason=%s fallback=native_cards",
                id_, frame_, row->name, fog_sector_.profile, why);
        else if (row && !why && !row->retry_reported.exchange(true, std::memory_order_relaxed))
            log("volumetric_fog_family device=%llu frame=%llu event=switch_retry name=\"%s\" profile=%u reason=allocation",
                id_, frame_, row->name, fog_sector_.profile);
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
    const bool rekeyed = placement.key != fog_density_key_; // 0 before the first key; a confirmed prefill already set it
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
        log("volumetric_fog_cache device=%llu frame=%llu event=config mode=stored result=%08lx profile=%u sigma=%.4g chroma=%.4f,%.4f,%.4f atlas_bytes=%u upload_budget_bytes=%u upload_rects=%u ramp_frames=%u march_scale=%u",
            id_, frame_, hr, fog_sector_.profile, double(fog_density_config_.sigma), double(fog_density_config_.chroma[0]), double(fog_density_config_.chroma[1]), double(fog_density_config_.chroma[2]),
            unsigned(fog::kAtlasBytes), unsigned(fog::kDefaultUploadBudget), fog::kDefaultUploadRects, fog::kReadinessRampFrames, fog_density_config_.march_scale);
    }
    if (status.march_scale_refused && !fog_march_scale_refused_logged_) {
        // The quarter-resolution march was asked but the half-resolution one draws (programs or the quarter target could
        // not be built; fog-gpu-cost.md step C).
        fog_march_scale_refused_logged_ = true;
        log("fog_march_scale_refused device=%llu frame=%llu reason=%s requested=%u drawn=%u", id_, frame_, status.march_scale_refused,
            fog_density_config_.march_scale, fog_ ? fog_->density_march_scale() : 0u);
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
    // One line per cold start (sector re-key, load gap), in the frame the far readiness reached 1 (fog-handover.md,
    // "Implementation"): the hand-over and its parts, so a flight settles fill versus upload cadence versus starvation.
    const auto& h = status.handover;
    // The cold step lands in this frame: arm the cards now (before this frame's cards) so they are masked in the
    // same frame the medium reaches full density, instead of after one warm-up frame of cards over the full medium.
    if (h.due && h.step && fog_cards_replace_ && status.ready_far >= 1.f) fog_cards_.arm_on_cold_step();
    if (h.due && fog_handover_logs_ < 256u) {
        ++fog_handover_logs_;
        // Integer microseconds to ms through an int32 convert, in place: no int64 or returned double (x87 on i686).
        const std::int64_t us[6] = {h.ready_us, h.drawable_us, h.fill_us, h.fill_busy_us, h.fill_cpu_us, h.busy_us};
        double ms[6];
        for (unsigned i = 0; i < 6; ++i) ms[i] = us[i] < 0 ? -1. : double(std::int32_t(std::min<std::int64_t>(us[i] / 100, 0x7fffffff))) * .1;
        log("volumetric_fog_handover device=%llu frame=%llu step=%u coldfill=%u whole_atlas=%u arm_frame=%llu frames=%llu ms=%.1f drawable_frame=%llu drawable_ms=%.1f fill_ms=%.1f fill_busy_ms=%.1f fill_cpu_ms=%.1f busy_ms=%.1f latches=%u upload_bytes=%llu",
            id_, frame_, unsigned(h.step), unsigned(h.cold_fill), unsigned(h.whole_atlas), h.arm_frame, h.ready_frame - h.arm_frame, ms[0], h.drawable_frame, ms[1],
            ms[2], ms[3], ms[4], ms[5], h.latches, h.upload_bytes);
    }
}
void MotionOutput::volumetric_fog_sector_sample(std::uint64_t frame, const sector_background::Sample& sample) noexcept {
    if (!fog_requested_ || frame != frame_ || fog_sector_.frame == frame) return;
    if (!fog_families_checked_) {
        // Once per process, before the first name scan (fog-family-data.md, "Implementation"): the
        // caller's CPU boundary and LastError restore cover the file read; nothing per frame after.
        fog_families_checked_ = true;
        if (renderer::fog_field::load_family_table()) {
            const auto& t = *renderer::fog_field::family_table(); // loaded: never null here
            static const char* const names[] = {"not_loaded", "absent", "disabled", "loaded", "rejected"};
            log("volumetric_fog_families device=%llu frame=%llu event=%s families=%u packets=%u rows_disabled=%u bytes=%llu reason=%s fallback=%s path=\"%s\"",
                id_, frame_, names[unsigned(t.status) < 5 ? unsigned(t.status) : 0], t.families, t.packets, t.rows_disabled,
                static_cast<unsigned long long>(t.file_bytes), t.reason, t.families > t.rows_disabled ? "compiled_first" : "compiled_only",
                renderer::fog_field::family_table_path());
            for (std::uint32_t i = 0, shown = 0; i < t.families && shown < 8; ++i)
                if (const char* why = t.rows[i].disabled.load(std::memory_order_relaxed)) {
                    ++shown; t.rows[i].reported.store(true, std::memory_order_relaxed);
                    log("volumetric_fog_family device=%llu frame=%llu event=row_disabled row=%u name=\"%s\" profile=%u reason=%s",
                        id_, frame_, i, t.rows[i].name, t.rows[i].profile, why);
                }
        }
    }
    auto next = fog_sector_frame(sample, frame, generation_, fog_strength_, fog_enabled_ && !fog_disabled_, fog_everywhere_,
                                 renderer::fog_field::family_table()); // nullptr until loaded
    if (fog_density_requested_) {
        // A gap in scene samples longer than fog_density_gap_ms of wall clock is a load or a sector
        // transit: refill and ramp instead of popping in. A shorter gap (a stutter, a skipped sample)
        // keeps the cache. A sector change re-keys the cache by itself; cuts and first-person/chase
        // switches are residency questions the cache answers per frame (world-anchored field).
        LARGE_INTEGER now{}, f{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&f);
        const bool gap = fog_density_sample_frame_ != ~std::uint64_t(0) && fog_density_sample_frame_ + 1 < frame && f.QuadPart > 0 &&
            (now.QuadPart - fog_density_sample_qpc_) * 1000 > static_cast<long long>(fog_density_gap_ms) * f.QuadPart;
        fog_density_sample_frame_ = frame; fog_density_sample_qpc_ = now.QuadPart;
        // A Ready sample of another sector than the last Ready one (its id [sector+8], both known: the destination can
        // reuse the freed source's address; fog_prefill::other_sector) is a transit, gap or not (run273 case A: the stall
        // frame's no_cockpit sample kept the frames consecutive, so the 5.4 s transit was no gap). A heap-token change
        // with the same or an unread id is a reallocation: it keeps the field, the image and the camera (Run75 bridge).
        const bool ready = sample.status == sector_background::Status::Ready;
        const bool transit = ready && fog_density_ready_sector_ != 0 && fog_prefill::other_sector(sample.sector_id, fog_density_ready_id_);
        if (ready) { fog_density_ready_sector_ = sample.sector; fog_density_ready_id_ = sample.sector_id; }
        // R3: a pending prefill owns the transit's gap until the first Ready sample decides it (confirmed keeps the fill).
        const auto prefill = fog_prefill_confirm(next, sample);
        const bool cold = fog_ && fog_density_config_.enabled && !fog_density_refused_ && prefill == fog_prefill::Decision::None && !fog_prefill_.pending;
        if (gap && cold) { fog_->invalidate_density(); fog_density_epoch("sample_gap"); }
        else if (transit && cold && next.profile && fog_sector_placement(next).key == fog_density_key_) {
            // The same placement key in another sector (run273 case A): the resident window is centred on the source's
            // position, which means nothing in the destination. A cold start (step, cold fill) instead of the warm
            // residency ramp the first step would otherwise run; a different key re-keys at the latch by itself.
            fog_->invalidate_density(); fog_density_epoch("transit");
        }
        // The camera the next latch would post is the previous scene end's, in the sector just left (run273 cases B and
        // D: a first fill around it was 1.09 M wasted nodes and discarded the prefilled box). This frame's latch skips
        // the post; the scene end re-validates it with this sector's camera and the next latch starts the fill there.
        if (transit || gap || prefill != fog_prefill::Decision::None) fog_density_camera_valid_ = false;
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
// R3 (fog-handover.md, "R3 implementation"): a walk result from a stalled frame's resource-creation hook. A found
// fog sector starts the far fill of its placement identity centred at the sector origin (the arrival position is
// written last), recorded as prefilled and unconfirmed; nothing draws before the detector's first Ready sample.
void MotionOutput::volumetric_fog_prefill(const fog_prefill::Result& w, std::uint64_t stall_ms) noexcept {
    // The same conditions as the latch (a worker at strength 0 would fill for nothing).
    if (!fog_prefill_launch_ || !fog_density_active() || !fog_enabled_ || fog_disabled_ || fog_attach_failed_ || !(fog_strength_ > 0.f)) return;
    const char* action = "none";
    std::uint64_t key = 0;
    if (w.status == fog_prefill::Walk::Found) {
        const FogSectorFrame f = fog_sector_frame(w.sample, frame_, generation_, fog_strength_, true, fog_everywhere_, renderer::fog_field::family_table());
        if (!f.profile) action = f.reason; // clear, unsupported family, invalid name: nothing to fill
        else {
            const FogSectorPlacement placement = fog_sector_placement(f);
            key = placement.key;
            static constexpr double origin[3] = {0., 0., 0.};
            // The first fogged sector of a session (run273 case D) has no pass yet: the object alone (no device
            // call) lets prefill_density start the worker (its one allocation; a failed start is refused for the
            // rest of the stall); the first stored frame attaches and keeps it.
            if (!fog_) { try { fog_ = std::make_unique<renderer::FogPass>(); fog_->configure_sync_timing(gpu_sync_); } catch (...) { fog_.reset(); } }
            // The resident key (run273 case A, a same-family gate): its window follows the source's position, so it
            // is re-centred at the destination's origin as a cold start; the confirmation keeps that fill. The flown
            // sector itself (the same id, or an id unread: a hitch or a reallocation) keeps its field.
            const auto plan = fog_prefill::plan(fog_prefill_, key, f.recipe, fog_density_config_.enabled && key == fog_density_key_, w.id, fog_density_ready_id_);
            if (plan == fog_prefill::Plan::AlreadyStarted || plan == fog_prefill::Plan::CurrentSector) action = fog_prefill::name(plan);
            else if (!fog_ || fog_->prefill_refused()) action = "worker_refused"; // the worker could not start in this stall: no further attempt
            else if (!fog_->prefill_density(key, f.recipe, placement.offset, origin, fog_density_config_.handover_step, fog_density_config_.handover_coldfill)) action = fog_->prefill_refused() ? "worker_refused" : "not_posted"; // not_posted: retried at the next poll
            else {
                LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
                action = fog_prefill::name(plan);
                fog_prefill_ = {true, w.node, w.id, w.sample.index, f.profile, f.recipe, key, now.QuadPart};
                fog_density_epoch("prefill"); // the cold start's clock: far_ready and the hand-over line count from here
            }
        }
    }
    const bool refused = action[0] == 'w' && !std::strcmp(action, "worker_refused"); // one line per stall for a refused worker
    if (refused ? !fog_prefill_refused_logged_ : fog_prefill_logs_ < 512u) {
        if (refused) fog_prefill_refused_logged_ = true; else ++fog_prefill_logs_;
        log("volumetric_fog_prefill device=%llu frame=%llu event=poll walk=%s steps=%u reads=%u node=%08x id=%u index=%d family=\"%s\" stall_ms=%llu action=%s key=%016llx",
            id_, frame_, fog_prefill::name(w.status), w.steps, w.reads, w.node, w.id, w.sample.index, w.sample.name_valid ? w.sample.family : "",
            static_cast<unsigned long long>(stall_ms), action, static_cast<unsigned long long>(key));
    }
}
// The detector's first Ready sample after a prefill: the same placement key keeps the fill (its configure is then a
// no-op), anything else invalidates as a load gap does. The sector pointer and id are reported, not required.
fog_prefill::Decision MotionOutput::fog_prefill_confirm(const FogSectorFrame& next, const sector_background::Sample& sample) noexcept {
    if (!fog_prefill_.pending || sample.status != sector_background::Status::Ready) return fog_prefill::Decision::None;
    const bool usable = next.profile != 0;
    const std::uint64_t key = usable ? fog_sector_placement(next).key : 0;
    const auto decision = fog_prefill::decide(fog_prefill_, usable, key, next.recipe);
    fog_prefill_.pending = false;
    if (decision == fog_prefill::Decision::Confirmed) fog_density_key_ = key; // adopted: the latch's configure is a no-op and logs no sector_key epoch
    LARGE_INTEGER now{}, f{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&f);
    const long long tenths = f.QuadPart > 0 ? (now.QuadPart - fog_prefill_.qpc) * 10000 / f.QuadPart : -10; // 0.1 ms, integer (no x87)
    const double lead_ms = double(std::int32_t(tenths > 0x7fffffffLL ? 0x7fffffffLL : tenths)) * .1;
    if (fog_prefill_logs_ < 512u) {
        ++fog_prefill_logs_;
        log("volumetric_fog_prefill device=%llu frame=%llu event=%s lead_ms=%.1f same_sector=%u same_id=%u key=%016llx prefill_key=%016llx index=%d prefill_index=%d profile=%u adopted=%u",
            id_, frame_, fog_prefill::name(decision), lead_ms, unsigned(sample.sector == fog_prefill_.sector), unsigned(sample.sector_id != 0 && sample.sector_id == fog_prefill_.id),
            static_cast<unsigned long long>(key), static_cast<unsigned long long>(fog_prefill_.key), sample.index, fog_prefill_.index, next.profile, unsigned(decision == fog_prefill::Decision::Confirmed));
    }
    if (decision == fog_prefill::Decision::Discarded && fog_ && fog_density_config_.enabled && !fog_density_refused_) {
        fog_->invalidate_density(); fog_density_epoch("prefill_discarded");
    }
    return decision;
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
    fog_card_ready_checked_ = fog_card_ready_ = false; fog_card_refusal_ = nullptr; fog_card_refusal_ready_ = false;
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
    static_assert(D3DPT_TRIANGLELIST == 4 && D3DDECLTYPE_FLOAT16_4 == 16 && D3DZB_FALSE == 0 && D3DZB_TRUE == 1 && D3DCULL_NONE == 1 &&
        D3DCULL_CW == 2 && D3DFILL_SOLID == 3 && D3DBLEND_ONE == 2 && D3DBLEND_INVSRCCOLOR == 4 && D3DBLENDOP_ADD == 1, "captured D3D9 enums");
    FogCardShape shape{call.indexed, call.user_memory, shadow_.stream0 != 0, shadow_.indices != 0,
        shadow_.declaration_stream0_only, false, unsigned(call.topology), call.primitives, call.vertex_count,
        shadow_.stream0_stride, shadow_.position_offset, shadow_.position_type, 0, shadow_.declaration};
    // Cached identity, geometry and caller gates precede all card-specific
    // reads. Keep production's hybrid unhook: no global setter observation is
    // needed for the few strict cards in a frame. The first refusal of a frame
    // names its gate on the volumetric_fog_cards line (run273 case C); the
    // checks are the same, in the same order, one branch each.
    // Gate names carry "gate:"; a readiness verdict is printed as "ready:" + the component's own name.
    auto refuse = [&](const char* why, bool ready = false) { fog_cards_.reject(); if (!fog_card_refusal_) { fog_card_refusal_ = why; fog_card_refusal_ready_ = ready; } };
    const char* gate = nullptr;
    if (!fog_enabled_ || fog_disabled_ || fog_attach_failed_) gate = "gate:fog_off";
    else if (!shadow_.fog_card_pair) gate = "gate:pair";
    else if (!shape.static_matches()) gate = "gate:shape";
    else if (!scene_open_ || !scene_bound()) gate = "gate:scene";
    else if (shadow_.recording || active_queries_) gate = "gate:queries";
    else if (composition_busy_ || composition_state_lost_ || motion_state_lost_) gate = "gate:composition";
    else if (hdr_state_ != HdrState::Active || !hdr_ || !hdr_->target() || main_msaa_) gate = "gate:owner";
    else if (!taa_enabled_ || taa_failed_ || counters_.taa.attempted || !jitter_active_) gate = "gate:taa";
    else if (!counters_.filled || !depth_surface_ || !depth_enabled_ || lane_depth_format() != D3DFMT_A32B32G32R32F) gate = "gate:linear_depth";
    else if (sun_lane_failed_ || sun_frame_.failed) gate = "gate:sun_lane";
    else if (fog_frame_ == frame_) gate = "gate:pass_done";
    if (gate) { refuse(gate); return; }
    // Hooks on: validated shadow; hooks off: the existing current-draw cache,
    // invalidated by before_draw. Never reuse another draw's stream frequency.
    shape.frequency_known = SUCCEEDED(direct_call<GetStreamFreqFn>(GetStreamSourceFreq, 0, &shape.frequency));
    if (!shape.matches()) { refuse("gate:frequency"); return; }
    const FogCardStates states{state_field(0), state_field(1), state_field(2), state_field(3), state_field(4),
        state_field(30), state_field(29), state_field(31),
        blend_known(0) ? composition_blend_field(0) : -1, blend_known(1) ? composition_blend_field(1) : -1,
        blend_known(2) ? composition_blend_field(2) : -1, blend_known(3) ? composition_blend_field(3) : -1};
    if (!states.matches()) {
        // Run278 case C: a docked-at-load card fails here with no state row in the log. The
        // refused vector, at most once per 300 frames, on the refusal path only.
        if (frame_ >= fog_card_states_log_frame_) {
            fog_card_states_log_frame_ = frame_ + 300u;
            call_preserved([&] { log("volumetric_fog_card_states device=%llu frame=%llu z=%ld zwrite=%ld atest=%ld blend=%ld mask=%ld cull=%ld stencil=%ld fill=%ld src=%ld dst=%ld op=%ld sepalpha=%ld",
                id_, frame_, states.z, states.zwrite, states.alpha_test, states.blend, states.color_mask, states.cull, states.stencil, states.fill,
                states.source, states.destination, states.operation, states.separate_alpha); });
        }
        refuse("gate:states"); return;
    }
    if (!fog_card_ready_checked_) {
        fog_card_ready_checked_ = true;
        // Shared parameter/sun validation includes floating ABI returns. Keep
        // it behind the existing full CPU envelope for these light draw roots.
        call_preserved([&] {
            renderer::FogFrame in{}; bool sun = false;
            in.width = target_width_; in.height = target_height_;
            const char* why = fog_frame_prerequisite();
            if (!why && !(fog_ && fog_->resources_ready(in.width, in.height, static_cast<renderer::fog_field::Profile>(fog_sector_.profile), fog_sector_.recipe, fog_sector_.field_generation))) why = "resources";
            if (!why) why = fog_frame_parameters(in, 1.f, sun);
            if (!why && fog_density_active()) {
                if (!fog_density_prepared_) why = "density_unprepared";
                else if (!(fog_->density_status().ready_far >= 1.f)) why = "density_ramp";
                else if (!fog_->density_drawable(in.params.world.origin)) why = "density_drawable";
            }
            fog_card_ready_ = !why; fog_card_ready_reason_ = why;
        });
    }
    if (!fog_card_ready_) { refuse(fog_card_ready_reason_ ? fog_card_ready_reason_ : "unknown", true); return; }
    call_preserved([&] {
        route.fog_card_mask.begin(7, [&](DWORD mask) { return native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE, mask); });
    });
    if (route.fog_card_mask.masked) {
        ++fog_cards_.suppressed;
        fog_card_transition(2);
    } else {
        refuse("gate:mask");
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
        if (gpu_sync_) gpu_sync_->begin(gpu_sync_timing::FogRoute); // --gpu-sync-timing only (the motes' pair nests inside)
        taa_call([&] { hr = fog_->execute(in, &out); });
        if (gpu_sync_) gpu_sync_->end(gpu_sync_timing::FogRoute);
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
    const bool periodic = fog_timing_ || changed || frame_ % 600u == 0u;
    if (periodic) {
        if (!fog_timing_) ++fog_logs_;
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
        log("volumetric_fog_frame device=%llu frame=%llu applied=%u reason=%s strength=%.4f density_scale=%.3f cards=%u profile=%u field_generation=%llu sun=%s shadow_maps=%u cpu_us=%.1f calls=%u result=%08lx restore=%08lx stage=%u%s",
            id_, frame_, unsigned(!skip && out.applied), reason, double(fog_strength_), double(fog_sector_.density_scale), unsigned(fog_latch_.cards_recent(frame_)), fog_sector_.profile, fog_sector_.field_generation,
            skip ? "none" : sun_tracked ? "tracked" : "fallback", out.cascades_bound, us, out.device_calls, out.operation, out.restore, unsigned(out.failed), mote_fields);
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
    // The refusal name is part of the change key (string literals: a pointer compare), under the same 60-frame spacing.
    const bool card_changed = card_report != fog_card_last_report_ || fog_card_refusal_ != fog_card_last_refusal_;
    if (fog_cards_replace_ && (fog_timing_ || (changed || (card_changed && frame_ - fog_card_logged_frame_ >= 60u) || frame_ % 600u == 0u))) {
        if (!fog_timing_) ++fog_card_logs_;
        fog_card_logged_frame_ = frame_; fog_card_last_refusal_ = fog_card_refusal_;
        log("volumetric_fog_cards device=%llu frame=%llu observed=%u suppressed=%u refused=%u ready=%u warmup=%u applied=%u fault=%u reason=%s mode=%u observed_total=%llu suppressed_total=%llu refused_total=%llu refusal=%s%s",
            id_, frame_, fog_cards_.observed, fog_cards_.suppressed, unsigned(fog_cards_.refused), unsigned(fog_card_ready_),
            unsigned(fog_cards_.warmup), unsigned(!skip && out.applied), unsigned(fog_cards_.fault), fog_card_fault_reason_, fog_card_mode_, fog_card_observed_total_, fog_card_suppressed_total_, fog_card_refused_total_,
            fog_card_refusal_ready_ ? "ready:" : "", fog_card_refusal_ ? fog_card_refusal_ : "none");
    }
    fog_card_last_report_ = card_report;
    fog_last_reason_ = reason;
}
