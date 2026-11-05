// Sky nebula deband, Decision C1 (included by motion_output.cpp inside namespace x3m, both editions; sky_nebula_core.h,
// renderer/sky_nebula_pass.h; docs/architecture/sector-background-modern.md, "Decision C"). X3M_SKY_NEBULA_DEBAND=1: the
// nebula part draws of the sector background run the own vs_3_0/ps_3_0 pair, which reproduces the engine's SM1.1 pair
// 7b6393fe2d3e1d85 / 6109cf64c03529dd (background-temporal-coverage.md, "The background pair's bytecode") and debands
// the sampled part in texture space; the engine's pair goes back after each draw.
//
// Match (sky_nebula_classify, after evaluate_draw, one compare of the shadow's pair for every draw): the pair is the
// background pair; ALPHABLENDENABLE on with SRCALPHA/ONE; ZENABLE off; COLORWRITEENABLE 7; the stage-0 texture is a 2D
// texture with a level-0 width >= 1024 (cached per pointer and identity) and is not a star mark (payload 2). A draw that
// passes those tests outside the background phase or the HDR latch is counted (outside=), never swapped. In the phase
// and the latch it is a part (parts=) and is swapped unless: the route took the draw or it is not submitted (routed), a
// state block is recording or a query is active (queries), composition is busy or a restore failed (composition), RT0
// is not the only target or the viewport is not full (targets), FOGENABLE is on (fog: ps_3_0 has no fixed-function fog,
// the SM1 pair has), stage 0 projects its coordinates (projected: D3DTTFF_PROJECTED changes an SM1 tex, not texld), or
// the pass is refused (attach). This admits FL's 2048x1024 and 2048x2048 DXT1 parts and AP's 2048x1024, 2048x2048 and
// 1024x512 parts; it excludes the star body (blend off), the 64x64 additive sun draw, the planets (blend off, Z on), the
// fog cards (other PS) and every UI use of the pair (outside the phase).
//
// Action (prepare_sky_nebula, last of the submit branch's preparations, never on a composed, additive or masked draw):
// renderer::SkyNebulaPass::begin reads the bound programs, VS c255 and PS c200-c201, sets its pair, c255 = the frame's
// jitter (sky_nebula::core::jitter_constant; 0 with sky_nebula_jitter = 0 or the jitter inactive) and c200-c201 =
// (on, 1 / threshold, radius, hash), (width, height, 1 / width, 1 / height) of the part (pixel_constants); the
// application's draw is forwarded; finish_sky_nebula (first in after_draw) puts everything back. A failed begin is undone
// at once and leaves the native draw; a failed restoration quarantines the route (motion_state_lost_) and the pass until
// Reset; three failed swaps in a row stop the swapping until Reset.
//
// Costs: per draw one compare of the pair with the setting on (zero otherwise: one bool test). Per part draw: five
// cached render-state reads, one GetTextureStageState, two QPC reads and the bracket (4 Get + 4 Set + 4 restore Set + 2
// Release, measured by the fixture's BRACKET row); a texture size query only when the stage-0 texture changes. No
// allocation per frame.
//
// Rows: sky_nebula_config once (capture.cpp); sky_nebula_device at attach; sky_nebula_match for the first four swapped
// draws per device (the texture's size and format, the states, VS c4/c5 when the engine-effects hook shadows them);
// sky_nebula at the depth-only Clear when parts, swapped or the reason changed since the last row (at most 1024 per
// device): status, parts, swapped, reason, draw/restore results, outside, frames, swapped_frames, refused and the
// bracket's CPU cost (us = this frame's, us_max = the largest since the last row).
void MotionOutput::configure_sky_nebula(bool requested, float threshold, float radius, bool jitter, bool hash) noexcept {
    sky_nebula_requested_ = requested;
    sky_nebula_threshold_ = sky_nebula::core::threshold_default;
    sky_nebula_radius_ = sky_nebula::core::radius_default;
    sky_nebula::core::accept_threshold(threshold, &sky_nebula_threshold_);
    sky_nebula::core::accept_radius(radius, &sky_nebula_radius_);
    sky_nebula_jitter_ = jitter;
    sky_nebula_hash_ = hash;
}
bool MotionOutput::attach_sky_nebula() noexcept {
    if (sky_nebula_attach_failed_) return false;
    if (!sky_nebula_) {
        sky_nebula_.reset(new (std::nothrow) renderer::SkyNebulaPass);
        if (!sky_nebula_) {
            sky_nebula_attach_failed_ = true;
            call_preserved([&] { log("sky_nebula_device device=%llu frame=%llu attached=0 reason=allocation retry=reset", id_, frame_); });
            return false;
        }
    }
    if (sky_nebula_->caps().enabled) return true;
    HRESULT hr = E_FAIL;
    taa_call([&] { hr = sky_nebula_->attach(device_, native_, caps_); }, "sky_nebula_attach");
    const bool attached = SUCCEEDED(hr) && sky_nebula_->caps().enabled;
    const auto& c = sky_nebula_->caps();
    call_preserved([&] {
        log("sky_nebula_device device=%llu frame=%llu attached=%u reason=%s result=%08lx vs_slots=%u ps_slots=%u"
            " max_ps_slots=%lu references=%u retry=reset",
            id_, frame_, unsigned(attached), attached ? "ok" : c.reason, hr, c.vs_slots, c.ps_slots,
            static_cast<unsigned long>(caps_.MaxPixelShader30InstructionSlots), sky_nebula_->references());
    });
    if (!attached) sky_nebula_attach_failed_ = true;
    return attached;
}
void MotionOutput::release_sky_nebula() noexcept {
    if (sky_nebula_) {
        taa_call([&] { sky_nebula_->detach(); }, "sky_nebula_detach");
        sky_nebula_.reset();
    }
    sky_nebula_texture_ = nullptr;
    sky_nebula_texture_id_ = 0;
}
// Reset: the shader objects survive; the texture cache is forgotten and a refusal is retried after it.
void MotionOutput::sky_nebula_before_reset() noexcept {
    sky_nebula_texture_ = nullptr;
    sky_nebula_texture_id_ = 0;
    sky_nebula_attach_failed_ = false;
    sky_nebula_failures_ = 0;
}
// The latching Clear (AwaitInitialClear -> Background): the frame's record starts over.
void MotionOutput::sky_nebula_latch() noexcept {
    sky_nebula_frame_parts_ = sky_nebula_frame_swapped_ = 0;
    sky_nebula_frame_reason_ = "none";
    sky_nebula_frame_hr_ = sky_nebula_frame_restore_ = S_OK;
    sky_nebula_frame_ticks_ = 0;
}
// The stage-0 texture's level-0 size, queried only when the pointer or its identity changes (a reused address of a
// freed texture is queried again). False when it is not a 2D texture.
bool MotionOutput::sky_nebula_part_size(IDirect3DBaseTexture9* texture) noexcept {
    if (!texture) return false;
    const std::uint64_t identity = resource_id(texture);
    if (texture != sky_nebula_texture_ || identity != sky_nebula_texture_id_) {
        unsigned width = 0, height = 0, levels = 0, format = 0;
        call_preserved([&] { sky_stars_texture_desc(texture, &width, &height, &levels, &format); });
        sky_nebula_texture_ = texture;
        sky_nebula_texture_id_ = identity;
        sky_nebula_texture_width_ = width;
        sky_nebula_texture_height_ = height;
        sky_nebula_texture_format_ = format;
    }
    return sky_nebula_texture_width_ && sky_nebula_texture_height_;
}
void MotionOutput::sky_nebula_classify(const MotionDrawCall& call, MotionRoute& route) noexcept {
    if (shadow_.vs_hash != sky_nebula::core::part_vs || shadow_.ps_hash != sky_nebula::core::part_ps) return;
    DWORD blend = 0, src = 0, dst = 0, z = 0, mask = 0;
    if (FAILED(render_state(D3DRS_ALPHABLENDENABLE, &blend)) || !blend ||
        FAILED(render_state(D3DRS_SRCBLEND, &src)) || src != D3DBLEND_SRCALPHA ||
        FAILED(render_state(D3DRS_DESTBLEND, &dst)) || dst != D3DBLEND_ONE ||
        FAILED(render_state(D3DRS_ZENABLE, &z)) || z != D3DZB_FALSE ||
        FAILED(render_state(D3DRS_COLORWRITEENABLE, &mask)) || mask != 7)
        return;
    if (sky_stars_stage0_) return; // a star mark (payload 2) is the star body's texture, never a part
    IDirect3DBaseTexture9* texture = samplers_[0].texture;
    if (!sky_nebula_part_size(texture) || sky_nebula_texture_width_ < sky_nebula::core::part_width_min) return;
    const bool latched = selector_.state() == renderer::BoundaryState::Background && hdr_state_ == HdrState::Active &&
                         hdr_ && hdr_->target() && !main_msaa_;
    if (!latched) {
        ++sky_nebula_outside_; // invariant 3: the pair outside the phase or the latch is never swapped
        return;
    }
    ++sky_nebula_frame_parts_;
    const auto& v = pending_.viewport;
    const char* why = nullptr;
    DWORD fog = 0, projected = 0;
    if (!route.submit || route.routed) why = "routed";
    else if (shadow_.recording || active_queries_) why = "queries";
    else if (composition_busy_ || composition_state_lost_ || motion_state_lost_) why = "composition";
    else if (!call.primitives) why = "empty";
    else if (!pending_.only_rt0 || !v.known || v.x || v.y || v.width != pending_.rt.width || v.height != pending_.rt.height ||
             !v.width || !v.height)
        why = "targets";
    else if (FAILED(render_state(D3DRS_FOGENABLE, &fog)) || fog) why = "fog";
    else if (FAILED(native<GetStageFn>(GetTextureStageState)(device_, 0, D3DTSS_TEXTURETRANSFORMFLAGS, &projected)) ||
             (projected & D3DTTFF_PROJECTED))
        why = "projected";
    else if (!attach_sky_nebula()) why = "attach";
    if (why) {
        ++sky_nebula_refused_;
        sky_nebula_frame_reason_ = why;
        return;
    }
    route.sky_nebula = true;
}
// The swap, set before the forwarded call (the last preparation of the submit branch).
void MotionOutput::prepare_sky_nebula(MotionRoute& route) noexcept {
    float jitter[4], pixel[8];
    sky_nebula::core::jitter_constant(jitter_active_ && sky_nebula_jitter_, jitter_[0], jitter_[1], pending_.rt.width,
                                      pending_.rt.height, jitter);
    if (!sky_nebula::core::pixel_constants(sky_nebula_threshold_, sky_nebula_radius_, sky_nebula_texture_width_,
                                           sky_nebula_texture_height_, pixel, sky_nebula_hash_)) {
        route.sky_nebula = false;
        ++sky_nebula_refused_;
        sky_nebula_frame_reason_ = "size";
        return;
    }
    renderer::SkyNebulaReport report{};
    LARGE_INTEGER begin{}, end{};
    call_preserved([&] {
        QueryPerformanceCounter(&begin);
        sky_nebula_->begin(jitter, pixel, &route.sky_nebula_saved, &report);
        QueryPerformanceCounter(&end);
    });
    sky_nebula_frame_ticks_ += std::uint64_t(end.QuadPart - begin.QuadPart);
    if (SUCCEEDED(report.operation)) {
        route.sky_nebula_swapped = true;
        return;
    }
    route.sky_nebula = false;
    ++sky_nebula_refused_;
    sky_nebula_frame_reason_ = "begin";
    sky_nebula_frame_hr_ = report.operation;
    if (FAILED(report.restore)) {
        // The undo of a partial swap failed: the device's state is unknown; the draw is not forwarded.
        sky_nebula_frame_restore_ = report.restore;
        motion_state_lost_ = true;
        motion_state_error_ = report.restore;
        invalidate_render_states();
        invalidate_taa(TaaInvalidateSite::RestoreFailed);
        ++counters_.restore_failures;
        sky_nebula_attach_failed_ = true;
        route.submit = false;
        route.submission_error = motion_state_error_;
        call_preserved([&] {
            log("motion_output_restore_failed device=%llu frame=%llu index=%lu result=%08lx what=sky_nebula_begin", id_,
                frame_, counters_.draws, report.restore);
        });
    } else if (++sky_nebula_failures_ >= 3 && !sky_nebula_attach_failed_) {
        // Three failed swaps in a row: the parts stay native until the next Reset.
        sky_nebula_attach_failed_ = true;
        call_preserved([&] {
            log("sky_nebula_device device=%llu frame=%llu attached=0 reason=begin_failures result=%08lx retry=reset", id_,
                frame_, report.operation);
        });
    }
}
// After the forwarded call (also after a failed one): the engine's pair and constants back.
void MotionOutput::finish_sky_nebula(MotionRoute& route, HRESULT result) noexcept {
    if (!route.sky_nebula_swapped) return;
    route.sky_nebula_swapped = false;
    HRESULT restored = S_OK;
    LARGE_INTEGER begin{}, end{};
    call_preserved([&] {
        QueryPerformanceCounter(&begin);
        restored = sky_nebula_ ? sky_nebula_->end(&route.sky_nebula_saved) : E_FAIL;
        QueryPerformanceCounter(&end);
    });
    sky_nebula_frame_ticks_ += std::uint64_t(end.QuadPart - begin.QuadPart);
    if (FAILED(result)) sky_nebula_frame_hr_ = result;
    if (FAILED(restored)) {
        sky_nebula_frame_restore_ = restored;
        sky_nebula_frame_reason_ = "restore";
        if (!motion_state_lost_) {
            motion_state_lost_ = true;
            motion_state_error_ = restored;
        }
        invalidate_render_states();
        invalidate_taa(TaaInvalidateSite::RestoreFailed);
        ++counters_.restore_failures;
        sky_nebula_attach_failed_ = true;
        call_preserved([&] {
            log("motion_output_restore_failed device=%llu frame=%llu index=%lu result=%08lx what=sky_nebula", id_, frame_,
                counters_.draws, restored);
        });
        return;
    }
    sky_nebula_failures_ = 0;
    ++sky_nebula_frame_swapped_;
    if (sky_nebula_match_rows_ < 4) {
        ++sky_nebula_match_rows_;
        DWORD fog = 0;
        render_state(D3DRS_FOGENABLE, &fog);
        const bool uv_known = (shadow_.world46_known & 3u) == 3u;
        const float* c4 = shadow_.world46;
        call_preserved([&] {
            char uv[160] = " c4=unknown";
            if (uv_known)
                std::snprintf(uv, sizeof uv, " c4=%.6g,%.6g,%.6g c5=%.6g,%.6g,%.6g", double(c4[0]), double(c4[1]),
                              double(c4[2]), double(c4[4]), double(c4[5]), double(c4[6]));
            log("sky_nebula_match device=%llu frame=%llu index=%u texture=%ux%u format=%u fog=%lu target=%ux%u"
                " threshold=%.2f radius=%.2f jitter=%u hash=%u result=%08lx%s",
                id_, frame_, sky_nebula_frame_parts_ - 1u, sky_nebula_texture_width_, sky_nebula_texture_height_,
                sky_nebula_texture_format_, static_cast<unsigned long>(fog), pending_.rt.width, pending_.rt.height,
                double(sky_nebula_threshold_), double(sky_nebula_radius_), unsigned(jitter_active_ && sky_nebula_jitter_),
                unsigned(sky_nebula_hash_),
                static_cast<unsigned long>(result), uv);
        });
    }
}
// The depth-only Clear (Background -> Scene): the frame's record; one row when parts, swapped or the reason changed.
void MotionOutput::sky_nebula_phase_end() noexcept {
    ++sky_nebula_frames_;
    if (sky_nebula_frame_swapped_) ++sky_nebula_swapped_frames_;
    sky_nebula_ticks_sum_ += sky_nebula_frame_ticks_;
    if (sky_nebula_frame_ticks_ > sky_nebula_ticks_max_) sky_nebula_ticks_max_ = sky_nebula_frame_ticks_;
    const char* reason = sky_nebula_frame_reason_;
    if (sky_nebula_frame_parts_ == sky_nebula_logged_parts_ && sky_nebula_frame_swapped_ == sky_nebula_logged_swapped_ &&
        reason == sky_nebula_logged_reason_)
        return;
    sky_nebula_logged_parts_ = sky_nebula_frame_parts_;
    sky_nebula_logged_swapped_ = sky_nebula_frame_swapped_;
    sky_nebula_logged_reason_ = reason;
    if (sky_nebula_rows_ >= 1024) return; // a flickering phase: the first 1024 changes per device
    ++sky_nebula_rows_;
    const char* status = !sky_nebula_frame_parts_                                    ? "none"
                         : sky_nebula_frame_swapped_ == sky_nebula_frame_parts_     ? "swapped"
                         : sky_nebula_frame_swapped_                                ? "partial"
                                                                                    : "refused";
    call_preserved([&] {
        const std::uint64_t frequency = engine_qpc_frequency() ? engine_qpc_frequency() : 1;
        const double frames = sky_nebula_frames_ ? double(sky_nebula_frames_) : 1.;
        log("sky_nebula device=%llu frame=%llu status=%s parts=%u swapped=%u reason=%s draw=%08lx restore=%08lx"
            " outside=%llu frames=%llu swapped_frames=%llu refused=%llu us=%.2f us_mean=%.2f us_max=%.2f threshold=%.2f"
            " radius=%.2f",
            id_, frame_, status, sky_nebula_frame_parts_, sky_nebula_frame_swapped_, reason,
            static_cast<unsigned long>(sky_nebula_frame_hr_), static_cast<unsigned long>(sky_nebula_frame_restore_),
            static_cast<unsigned long long>(sky_nebula_outside_), static_cast<unsigned long long>(sky_nebula_frames_),
            static_cast<unsigned long long>(sky_nebula_swapped_frames_),
            static_cast<unsigned long long>(sky_nebula_refused_),
            double(sky_nebula_frame_ticks_) * 1e6 / double(frequency),
            double(sky_nebula_ticks_sum_) * 1e6 / double(frequency) / frames,
            double(sky_nebula_ticks_max_) * 1e6 / double(frequency), double(sky_nebula_threshold_),
            double(sky_nebula_radius_));
    });
    sky_nebula_frames_ = sky_nebula_swapped_frames_ = 0;
    sky_nebula_ticks_sum_ = sky_nebula_ticks_max_ = 0;
}
