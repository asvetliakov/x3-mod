// Sky stars, Decision A (included by motion_output.cpp inside namespace x3m, both editions; sky_stars_core.h,
// renderer/sky_stars_pass.h; docs/architecture/sector-background-modern.md, "Implementation A"). X3M_SKY_STARS=1: the
// painted star body of the sector background is replaced in place by one additive batch of procedural star quads.
//
// Match: the D3DX texture hook stores the packed seed of every created texture whose DDS header carries the baker's
// payload-2 mark (capture.cpp texture_created); the SetTexture hook keeps the seed of the stage-0 texture
// (sky_stars_stage0_, 0 for any other texture) and whether it was bound since the last draw. A draw matches when that
// seed is non-zero, the selector is in the background phase (after the latching Clear, before the depth-only Clear),
// the HDR redirect is active on its FP16 target (the latch), the texture was bound for this draw (or the draw repeats
// the frame's first match's program pair: a stale stage-0 binding on an untextured draw never matches), only RT0 is
// bound with a full viewport, no state block is recording, no query is active, the route left the draw native and
// the frame's background camera is valid. A marked texture bound for a draw outside the background phase or the latch
// is only counted (outside=, the first four logged: invariant 3); it is never masked.
//
// Action: sky_stars_mask=1 (default) forwards the native draw with COLORWRITEENABLE 0 (the fog card's own mask,
// route.fog_card_mask, so every other preparation skips the draw as for a masked card; the actual HRESULT returned, Z
// and stencil untouched) and, after it, draws the batch once per frame
// (renderer::SkyStarsPass: ONE/ONE, RGB, Z off, cull none, every touched state restored); sky_stars_mask=0 draws the
// batch over the unmasked native draw. The pass and the seed's vertex buffer are made ready before the mask is set, so a
// refused or failed preparation leaves the native draw (and the painted stars) untouched.
//
// Costs: per draw one bool test with the setting off; with it on, one compare of sky_stars_stage0_ (zero for every draw
// without a marked stage-0 texture) and, in the background phase, one counter. Per matched frame: one mask pair, the
// constants (12 multiplies; c251's wall time: one QueryPerformanceCounter and a 64-bit modulo), one Get/Set/restore
// bracket of 50 device calls (measured: the fixture's DRAW row) including the DrawIndexedPrimitive. Per seed
// change: one Lock of the static MANAGED buffer and the generator (N <= 16384 stars). No allocation per frame.
//
// Rows: sky_stars (status, mark, stars, matched, masked, draw, table_full) at the depth-only Clear when the status, the
// seed, a refusal reason or the table's refusal count changed since the last row; sky_stars_match once per new seed at its first match (invariant 1: the
// pair, blend and z states, the index in the background phase, the texture's size and levels, the latching Clear's
// colour, the background camera); sky_stars_outside for the first four outside-phase binds; sky_stars_device at attach.
//
// Per-sector seed (sector_key.h, docs/architecture/sector-background-modern.md "Implementation A, per-sector seed"): the
// frame's cockpit-sector sample (capture.cpp) runs the sector key reader once per frame; a known key that differs from
// the held one becomes the held key, an unknown read keeps it (the field of the last known sector; 0, the mark's own
// field, until one is known). The matched star body prepares the pass with (seed, look, held key): the pass rebuilds
// only when one of the three differs from its buffer's (sky_stars::core::same_build). Rows: sky_stars_sector on every
// change of the read (status, reason, sector, key); key= on the sky_stars row; sky_stars_phase_draw for up to 16
// background-phase draws of the frame after a field's first match (one-shot per field; the star body's row adds its
// c0-c3 rows and the background camera's rotation).
//
// Decision C2 (sector-background-modern.md "Decision C"), sky_stars_nebula_dim = k > 0 and the pass's dim side
// available (caps().dim): the matched star body is masked as before but the batch is not drawn there (pending). In
// before_clear (RT1/RT2 unbound), a Clear in the background phase with D3DCLEAR_ZBUFFER, without D3DCLEAR_TARGET and
// without rectangles is predicted to be the phase end (the selector's rule 2: such a Clear ends the phase or rejects the
// frame); on the phase's depth (main_depth_), the HDR latch, RT0 only and the matched viewport the route StretchRects
// the FP16 target into the 1/8-size dim texture (SkyStarsPass::copy_dim) and draws the batch Z-tested at the far plane
// with each star's energy times 1 - k saturate(L / L0) (draw_deferred, c250). A copy that fails draws with k = 0 (Z
// still tested); a dim texture that cannot be created draws the in-place batch there. The Background -> Scene transition
// in after_clear confirms the prediction (sky_stars_phase_end); a prediction still open at the next latching Clear is
// unconfirmed, a pending batch never drawn is missed. k = 0 or no dim side: the in-place batch of Implementation A,
// unchanged. Row sky_stars_defer (predicted, confirmed, missed, unconfirmed, the copy's filter and result, the copy and
// batch CPU cost) at the first deferral, with each sky_stars row, on an unconfirmed prediction and every 4096 phase
// ends (at most 1024 per device).
void MotionOutput::configure_sky_stars(bool requested, const sky_stars::core::Look& look, float twinkle, bool mask,
                                       float dim, float dim_level) noexcept {
    sky_stars_requested_ = requested;
    sky_stars_dim_ = 0.f;
    sky_stars_dim_level_ = sky_stars::core::nebula_dim_level_default;
    sky_stars::core::accept_nebula_dim(dim, &sky_stars_dim_);
    sky_stars::core::accept_nebula_dim_level(dim_level, &sky_stars_dim_level_);
    sky_stars_look_ = sky_stars::core::sanitized(look);
    sky_stars_twinkle_ = sky_stars::core::twinkle_default;
    sky_stars::core::accept_twinkle(twinkle, &sky_stars_twinkle_);
    sky_stars_mask_mode_ = mask;
    if (requested && !sky_stars_marks_) sky_stars_marks_ = new (std::nothrow) lod_texture_mark::Table<1024>();
    if (!sky_stars_marks_) sky_stars_requested_ = false; // no table: nothing is stored or matched (fail closed)
}
bool MotionOutput::sky_stars_mark_insert(IDirect3DBaseTexture9* texture, std::uint64_t identity,
                                         std::uint32_t seed) noexcept {
    if (!sky_stars_requested_ || !sky_stars_marks_ || !texture || !(seed & sky_stars::core::seed_valid)) return false;
    return sky_stars_marks_->insert(texture, identity, std::uint8_t(sky_stars::core::seed_lane(seed)),
                                    std::uint8_t(sky_stars::core::seed_slot(seed)), nullptr,
                                    std::uint8_t(sky_stars::core::seed_k(seed)));
}
bool MotionOutput::sky_stars_mark_forget(const void* texture) noexcept {
    if (!sky_stars_marks_ || !sky_stars_marks_->count || !sky_stars_marks_->find(texture)) return false;
    sky_stars_marks_->erase(texture);
    if (texture == samplers_[0].texture) sky_stars_stage0_ = 0;
    return true;
}
// The seed of a texture the application binds on stage 0: the table's descriptor when the pointer is known and its
// identity still matches (a reused address of a freed texture is dropped), else 0.
std::uint32_t MotionOutput::sky_stars_lookup(IDirect3DBaseTexture9* texture) noexcept {
    if (!sky_stars_marks_ || !sky_stars_marks_->count || !texture) return 0;
    auto* entry = sky_stars_marks_->find(texture);
    if (!entry) return 0;
    const std::uint64_t identity = resource_id(texture);
    if (!identity || identity != entry->identity) {
        sky_stars_marks_->erase(texture);
        return 0;
    }
    return std::uint32_t(entry->slot) | std::uint32_t(entry->lane) << 8 | std::uint32_t(entry->k) << 16 |
           sky_stars::core::seed_valid;
}
// The pass at the first match: attached once per device, refused until Reset on failure with one sky_stars_device row.
bool MotionOutput::attach_sky_stars() noexcept {
    if (sky_stars_attach_failed_) return false;
    if (!sky_stars_) {
        sky_stars_.reset(new (std::nothrow) renderer::SkyStarsPass);
        if (!sky_stars_) {
            sky_stars_attach_failed_ = true;
            call_preserved([&] { log("sky_stars_device device=%llu frame=%llu attached=0 reason=allocation retry=reset", id_, frame_); });
            return false;
        }
    }
    if (sky_stars_->caps().enabled) return true;
    D3DDISPLAYMODE display{};
    HRESULT hr = native<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &display);
    const bool queried = SUCCEEDED(hr);
    if (queried) taa_call([&] { hr = sky_stars_->attach(device_, native_, caps_, display.Format); }, "sky_stars_attach");
    const bool attached = queried && SUCCEEDED(hr) && sky_stars_->caps().enabled;
    const auto& c = sky_stars_->caps();
    call_preserved([&] {
        log("sky_stars_device device=%llu frame=%llu attached=%u reason=%s result=%08lx fp16_blending=%08lx vs_slots=%u ps_slots=%u max_ps_slots=%lu references=%u retry=reset",
            id_, frame_, unsigned(attached), attached ? "ok" : queried ? c.reason : "adapter_query", hr, c.fp16_blending,
            c.vs_slots, c.ps_slots, static_cast<unsigned long>(caps_.MaxPixelShader30InstructionSlots),
            sky_stars_->references());
    });
    if (!attached) sky_stars_attach_failed_ = true;
    return attached;
}
void MotionOutput::release_sky_stars() noexcept {
    if (sky_stars_) {
        taa_call([&] { sky_stars_->detach(); }, "sky_stars_detach");
        sky_stars_.reset();
    }
    sky_stars_stage0_ = 0;
}
// Reset (before_reset): the MANAGED buffers and the programs survive; the stage-0 seed and both bind bits are forgotten
// (the application rebinds after Reset) and a refusal or three failed batches are retried after it (the own sun's rule).
void MotionOutput::sky_stars_before_reset() noexcept {
    if (sky_stars_) sky_stars_->before_reset();
    sky_stars_stage0_ = 0;
    sky_stars_fresh_ = sky_stars_draw_fresh_ = false;
    sky_stars_attach_failed_ = false;
    sky_stars_failures_ = 0;
    sky_stars_frame_pending_ = sky_stars_frame_predicted_ = false; // the dim texture went with the pass's before_reset
}
// The latching Clear (AwaitInitialClear -> Background): the frame's record starts over.
void MotionOutput::sky_stars_latch() noexcept {
    // Decision C2: last frame's deferred batch never reached a phase end (missed), or its prediction was not followed by
    // the Background -> Scene transition (unconfirmed: the frame was rejected after the batch, like a masked star body).
    if (sky_stars_frame_pending_) ++sky_stars_missed_;
    if (sky_stars_frame_predicted_) {
        ++sky_stars_unconfirmed_;
        sky_stars_log_defer("unconfirmed");
    }
    sky_stars_frame_pending_ = sky_stars_frame_predicted_ = false;
    sky_stars_phase_draws_ = sky_stars_frame_matched_ = sky_stars_frame_masked_ = 0;
    sky_stars_frame_seed_ = 0;
    sky_stars_frame_pair_[0] = sky_stars_frame_pair_[1] = 0;
    sky_stars_frame_drawn_ = false;
    sky_stars_frame_hr_ = S_FALSE;
    sky_stars_frame_reason_ = "none";
    sky_stars_clear_colour_ = clear_colour_pending_;
    if (sky_stars_phase_log_ == 1) { // a field's first match armed this phase: its draws are logged
        sky_stars_phase_log_ = 2;
        sky_stars_phase_logged_ = 0;
    }
}
// Every draw with the setting on: the classification, then (only in a logging background phase) its phase row.
void MotionOutput::sky_stars_classify(const MotionDrawCall& call, MotionRoute& route) noexcept {
    const unsigned matched = sky_stars_frame_matched_;
    sky_stars_classify_draw(call, route);
    if (sky_stars_phase_log_ == 2 && sky_stars_phase_logged_ < sky_stars_phase_rows_max &&
        selector_.state() == renderer::BoundaryState::Background)
        sky_stars_log_phase_draw(call, sky_stars_frame_matched_ != matched);
}
void MotionOutput::sky_stars_classify_draw(const MotionDrawCall& call, MotionRoute& route) noexcept {
    const bool background = selector_.state() == renderer::BoundaryState::Background;
    if (background) ++sky_stars_phase_draws_;
    const std::uint32_t seed = sky_stars_stage0_;
    const bool fresh = sky_stars_draw_fresh_; // taken at the top of before_draw (sky_stars_draw_begin)
    sky_stars_draw_fresh_ = false;
    if (!seed) return;
    const bool latched = background && hdr_state_ == HdrState::Active && hdr_ && hdr_->target() && !main_msaa_;
    if (!latched) {
        // Invariant 3: a marked texture bound for a draw outside the background phase or the HDR latch. Counted, the
        // first four logged; never masked. A stale binding (not set for this draw) is not counted.
        if (!fresh) return;
        ++sky_stars_outside_;
        if (sky_stars_outside_logged_ < 4) {
            ++sky_stars_outside_logged_;
            call_preserved([&] {
                log("sky_stars_outside device=%llu frame=%llu selector=%u hdr=%u vs=%016llx ps=%016llx primitives=%u mark=%u/%u/%u count=%llu",
                    id_, frame_, unsigned(selector_.state()), unsigned(hdr_state_), shadow_.vs_hash, shadow_.ps_hash,
                    call.primitives, sky_stars::core::seed_slot(seed), sky_stars::core::seed_lane(seed),
                    sky_stars::core::seed_k(seed), static_cast<unsigned long long>(sky_stars_outside_));
            });
        }
        return;
    }
    // A stage-0 binding left over from an earlier draw matches only on the frame's matched pair (the star body drawn in
    // more than one call); any other draw with a stale binding is not the star body.
    const bool same_pair = sky_stars_frame_matched_ && shadow_.vs_hash == sky_stars_frame_pair_[0] &&
                           shadow_.ps_hash == sky_stars_frame_pair_[1] && seed == sky_stars_frame_seed_;
    if (!fresh && !same_pair) return;
    if (!sky_stars_frame_matched_) {
        sky_stars_frame_seed_ = seed;
        sky_stars_frame_pair_[0] = shadow_.vs_hash;
        sky_stars_frame_pair_[1] = shadow_.ps_hash;
    }
    ++sky_stars_frame_matched_;
    const auto& v = pending_.viewport;
    const char* why = nullptr;
    if (!route.submit || route.routed) why = "routed";
    else if (shadow_.recording || active_queries_) why = "queries";
    else if (composition_busy_ || composition_state_lost_ || motion_state_lost_) why = "composition";
    else if (!call.primitives) why = "empty";
    else if (seed != sky_stars_frame_seed_) why = "second_seed";
    else if (!pending_.only_rt0 || !v.known || v.x || v.y || v.width != pending_.rt.width || v.height != pending_.rt.height ||
             !v.width || !v.height)
        why = "targets";
    else if (!camera_background_.valid) why = "camera";
    else if (!attach_sky_stars()) why = "attach";
    if (!why && !sky_stars_frame_drawn_) {
        // The seed's buffer before any mask: a failed rebuild leaves the painted stars.
        bool rebuilt = false;
        LARGE_INTEGER begin{}, end{};
        unsigned stars = 0;
        call_preserved([&] {
            QueryPerformanceCounter(&begin);
            stars = sky_stars_->prepare(seed, sky_stars_look_, sky_stars_hold_.key, &rebuilt);
            QueryPerformanceCounter(&end);
        });
        if (rebuilt) {
            ++sky_stars_rebuilds_;
            sky_stars_build_ticks_ = std::uint64_t(end.QuadPart - begin.QuadPart);
        }
        if (!stars) why = "build";
    }
    if (why) {
        ++sky_stars_refused_;
        if (!sky_stars_frame_drawn_) sky_stars_frame_reason_ = why;
        return;
    }
    route.sky_stars = true;
    sky_stars_width_ = v.width;
    sky_stars_height_ = v.height;
    if (seed != sky_stars_match_logged_seed_) {
        sky_stars_match_logged_seed_ = seed;
        sky_stars_log_match(call);
    }
    if (!sky_stars_phase_log_) sky_stars_arm_phase_log(sky_stars::core::generator_seed(seed, sky_stars_hold_.key));
}
// A field (the generator seed of mark and held key) matched for the first time arms the next background phase's
// sky_stars_phase_draw rows; one compare per matched draw while the field holds, a ring scan when it changes.
void MotionOutput::sky_stars_arm_phase_log(std::uint32_t field) noexcept {
    if (field == sky_stars_phase_last_field_) return;
    sky_stars_phase_last_field_ = field;
    const unsigned known = sky_stars_phase_fields_ < sky_stars_phase_fields_max ? sky_stars_phase_fields_
                                                                                 : sky_stars_phase_fields_max;
    for (unsigned i = 0; i < known; ++i)
        if (sky_stars_phase_seen_[i] == field) return;
    sky_stars_phase_seen_[sky_stars_phase_fields_++ % sky_stars_phase_fields_max] = field;
    sky_stars_phase_field_ = field;
    sky_stars_phase_log_ = 1;
}
// One background-phase draw of a logging phase (read-only evidence for "stars dimmer under the nebula"): its pair,
// blend and depth states, the stage-0 texture, and for the star body (star: this draw matched) the c0-c3 rows as
// submitted and the background camera's rotation.
void MotionOutput::sky_stars_log_phase_draw(const MotionDrawCall& call, bool star) noexcept {
    static constexpr D3DRENDERSTATETYPE keys[] = {D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND,
                                                  D3DRS_BLENDOP, D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_SRCBLENDALPHA,
                                                  D3DRS_DESTBLENDALPHA, D3DRS_COLORWRITEENABLE, D3DRS_ZENABLE,
                                                  D3DRS_ZWRITEENABLE};
    constexpr unsigned count = sizeof keys / sizeof keys[0];
    long states[count];
    for (unsigned i = 0; i < count; ++i) {
        DWORD value = 0;
        states[i] = SUCCEEDED(render_state(keys[i], &value)) ? long(value) : -1L;
    }
    ++sky_stars_phase_logged_;
    IDirect3DBaseTexture9* texture = samplers_[0].texture;
    const std::uint32_t mark = sky_stars_stage0_;
    const float* c0 = star ? sky_stars_c0_rows(shadow_.rows, shadow_.rows_known) : nullptr;
    call_preserved([&] {
        unsigned width = 0, height = 0, levels = 0, format = 0;
        sky_stars_texture_desc(texture, &width, &height, &levels, &format);
        const std::uint64_t identity = texture ? resource_id(texture) : 0;
        char extra[640] = "";
        if (star) {
            const auto& b = camera_background_;
            int used = 0;
            if (c0)
                used = std::snprintf(extra, sizeof extra,
                                     " c0=%.6g,%.6g,%.6g,%.6g c1=%.6g,%.6g,%.6g,%.6g c2=%.6g,%.6g,%.6g,%.6g"
                                     " c3=%.6g,%.6g,%.6g,%.6g",
                                     double(c0[0]), double(c0[1]), double(c0[2]), double(c0[3]), double(c0[4]),
                                     double(c0[5]), double(c0[6]), double(c0[7]), double(c0[8]), double(c0[9]),
                                     double(c0[10]), double(c0[11]), double(c0[12]), double(c0[13]), double(c0[14]),
                                     double(c0[15]));
            else
                used = std::snprintf(extra, sizeof extra, " c0=unknown");
            if (used > 0 && unsigned(used) < sizeof extra)
                std::snprintf(extra + used, sizeof extra - unsigned(used),
                              " background_valid=%u r=%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g", unsigned(b.valid),
                              double(b.r[0]), double(b.r[1]), double(b.r[2]), double(b.r[3]), double(b.r[4]),
                              double(b.r[5]), double(b.r[6]), double(b.r[7]), double(b.r[8]));
        }
        log("sky_stars_phase_draw device=%llu frame=%llu field=%08x index=%u star=%u vs=%016llx ps=%016llx blend=%ld src=%ld"
            " dst=%ld op=%ld sepalpha=%ld srca=%ld dsta=%ld mask=%ld z=%ld zwrite=%ld texture=%ux%u identity=%llu"
            " mark=%u/%u/%u primitives=%u%s",
            id_, frame_, static_cast<unsigned>(sky_stars_phase_field_), sky_stars_phase_draws_ - 1u, unsigned(star),
            shadow_.vs_hash, shadow_.ps_hash, states[0], states[1], states[2], states[3], states[4], states[5],
            states[6], states[7], states[8], states[9], width, height, static_cast<unsigned long long>(identity),
            sky_stars::core::seed_slot(mark), sky_stars::core::seed_lane(mark), sky_stars::core::seed_k(mark),
            call.primitives, extra);
    });
}
// The frame's cockpit-sector sample: the sector key, the held key, one sky_stars_sector row per change.
void MotionOutput::sky_stars_sector_sample(const sector_background::Sample& sample, std::uint64_t now_ms) noexcept {
    if (!sky_stars_requested_) return;
    // A load: an empty background table, or a gap of 250 ms or more between samples (a stall), drops the reader's cache,
    // so the chain is walked again even when (S, [S+8], [S+0x94]) repeat.
    const bool stalled = sky_stars_sector_ms_ && now_ms - sky_stars_sector_ms_ >= 250;
    sky_stars_sector_ms_ = now_ms;
    sector_key::core::Result r;
    r.sector = sample.class48 == 1 ? sample.sector : 0;
    if (sample.status == sector_background::Status::Loading) {
        sector_key::invalidate();
        r.reason = sector_key::core::Reason::loading;
    } else if (!r.sector) {
        r.reason = sector_key::core::Reason::no_sector;
    } else {
        if (stalled) sector_key::invalidate();
        r = sector_key::sample(r.sector);
    }
    sky_stars_hold_.apply(r);
    const auto& last = sky_stars_sector_logged_;
    if (sky_stars_sector_rows_ && r.known == last.known && r.reason == last.reason && r.sector == last.sector &&
        r.key == last.key)
        return;
    sky_stars_sector_logged_ = r;
    if (sky_stars_sector_rows_ >= 1024) return; // a flickering read: the first 1024 changes per device
    ++sky_stars_sector_rows_;
    const char* status = sector_background::name(sample.status);
    call_preserved([&] {
        log("sky_stars_sector device=%llu frame=%llu status=%s reason=%s edition=%s S=%08x id=%u kc=%d x=%d y=%d key=%llu"
            " rebuilds=%u held=%llu links=%u cached=%u sample=%s check=%s",
            id_, frame_, r.known ? "known" : "unknown", sector_key::core::name(r.reason), sector_key::edition(),
            static_cast<unsigned>(r.sector), static_cast<unsigned>(r.id), static_cast<int>(r.kc), r.x, r.y,
            static_cast<unsigned long long>(r.key), sky_stars_rebuilds_,
            static_cast<unsigned long long>(sky_stars_hold_.key), r.links, unsigned(r.cached), status,
            sector_key::status());
    });
}
// Invariant 1, once per seed at its first match: what the star body is and where it sits.
void MotionOutput::sky_stars_log_match(const MotionDrawCall& call) noexcept {
    static constexpr D3DRENDERSTATETYPE keys[] = {D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP,
                                                  D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_COLORWRITEENABLE, D3DRS_ZENABLE,
                                                  D3DRS_ZWRITEENABLE, D3DRS_CULLMODE, D3DRS_ALPHATESTENABLE};
    long states[10];
    for (unsigned i = 0; i < 10; ++i) {
        DWORD value = 0;
        states[i] = SUCCEEDED(render_state(keys[i], &value)) ? long(value) : -1L;
    }
    IDirect3DBaseTexture9* texture = samplers_[0].texture;
    call_preserved([&] {
        unsigned width = 0, height = 0, levels = 0, format = 0;
        sky_stars_texture_desc(texture, &width, &height, &levels, &format);
        const auto& b = camera_background_;
        const auto& s = camera_scene_; // the previous frame's scene view (this frame's is read at the depth-only Clear)
        log("sky_stars_match device=%llu frame=%llu mark=%u/%u/%u vs=%016llx ps=%016llx index=%u blend=%ld src=%ld dst=%ld op=%ld sepalpha=%ld mask=%ld z=%ld zwrite=%ld cull=%ld atest=%ld"
            " primitives=%u vertices=%u indexed=%u texture=%ux%u levels=%u format=%u clear=%08lx target=%ux%u outside=%llu"
            " camera_p00=%.6g camera_p11=%.6g camera_p20=%.6g camera_p21=%.6g scene_p00=%.6g scene_p11=%.6g rotation_deg=%.4f",
            id_, frame_, sky_stars::core::seed_slot(sky_stars_frame_seed_), sky_stars::core::seed_lane(sky_stars_frame_seed_),
            sky_stars::core::seed_k(sky_stars_frame_seed_), shadow_.vs_hash, shadow_.ps_hash, sky_stars_phase_draws_ - 1u,
            states[0], states[1], states[2], states[3], states[4], states[5], states[6], states[7], states[8], states[9],
            call.primitives, call.vertex_count, unsigned(call.indexed), width, height, levels, format,
            static_cast<unsigned long>(sky_stars_clear_colour_), pending_.viewport.width, pending_.viewport.height,
            static_cast<unsigned long long>(sky_stars_outside_), double(b.m00), double(b.m11), double(b.m20),
            double(b.m21), double(s.m00), double(s.m11),
            b.valid && s.valid ? double(renderer::camera_rotation_degrees(s, b)) : -1.);
    });
}
// The mask (sky_stars_mask=1), set before the forwarded call like the fog card's.
void MotionOutput::prepare_sky_stars(MotionRoute& route) noexcept {
    if (!sky_stars_mask_mode_) return;
    DWORD mask = 0;
    if (FAILED(render_state(D3DRS_COLORWRITEENABLE, &mask))) {
        route.sky_stars = false;
        ++sky_stars_refused_;
        if (!sky_stars_frame_drawn_) sky_stars_frame_reason_ = "mask_state";
        return;
    }
    call_preserved([&] {
        route.fog_card_mask.begin(mask, [&](DWORD m) {
            return native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE, m);
        });
    });
    if (route.fog_card_mask.masked) {
        route.sky_stars_masked = true;
        ++sky_stars_frame_masked_;
        return;
    }
    route.sky_stars = false;
    ++sky_stars_refused_;
    if (!sky_stars_frame_drawn_) sky_stars_frame_reason_ = "mask";
    route.preparation_error = route.fog_card_mask.operation;
    if (route.fog_card_mask.restore < 0) {
        motion_state_lost_ = true;
        motion_state_error_ = route.fog_card_mask.restore;
        invalidate_render_states();
        route.submit = false;
        route.submission_error = motion_state_error_;
    }
}
// After the forwarded call (also after a failed one): the mask back, then the batch once per frame.
void MotionOutput::finish_sky_stars(MotionRoute& route, HRESULT result) noexcept {
    if (route.sky_stars_masked) {
        route.sky_stars_masked = false;
        call_preserved([&] {
            route.fog_card_mask.end([&](DWORD m) {
                return native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE, m);
            });
        });
        if (route.fog_card_mask.restore < 0) {
            motion_state_lost_ = true;
            motion_state_error_ = route.fog_card_mask.restore;
            invalidate_render_states();
            invalidate_taa(TaaInvalidateSite::RestoreFailed);
            sky_stars_frame_reason_ = "mask_restore";
            return;
        }
    }
    if (!route.sky_stars || sky_stars_frame_drawn_) return;
    // Draw-over mode leaves the native draw to the other preparations; one that took it (a composition bracket, the
    // additive route, a fog card's mask) owns RT0 or the mask until its own finish, after this one: no batch then.
    if (route.composition || route.screen_additive || route.fog_card_mask.masked) {
        sky_stars_frame_reason_ = "composition";
        return;
    }
    if (FAILED(result)) {
        sky_stars_frame_hr_ = result;
        sky_stars_frame_reason_ = "source_draw";
        return;
    }
    if (sky_stars_dim_ > 0.f && sky_stars_ && sky_stars_->caps().dim) {
        // Decision C2: the batch waits for the predicted phase end (sky_stars_phase_end_draw).
        if (!sky_stars_frame_pending_ && !sky_stars_defer_rows_) sky_stars_log_defer("first");
        sky_stars_frame_pending_ = true;
        sky_stars_frame_reason_ = "deferred";
        return;
    }
    const auto& b = camera_background_;
    float constants[16];
    if (!sky_stars::core::vertex_constants(b.r, b.m00, b.m11, b.m20, b.m21, jitter_active_ ? jitter_[0] : 0.f,
                                           jitter_active_ ? jitter_[1] : 0.f, sky_stars_width_, sky_stars_height_,
                                           constants)) {
        sky_stars_frame_reason_ = "constants";
        return;
    }
    renderer::SkyStarsReport report{};
    LARGE_INTEGER begin{}, end{};
    call_preserved([&] {
        QueryPerformanceCounter(&begin);
        // c251: the wall clock (QPC), not the game clock, so SETA does not speed the twinkle up. A failed conversion
        // (no frequency) leaves (0, 0): every star steady.
        float twinkle[4];
        sky_stars::core::twinkle_constants(std::uint64_t(begin.QuadPart), engine_qpc_frequency(), sky_stars_twinkle_,
                                           twinkle);
        sky_stars_->draw(constants, twinkle, &report);
        QueryPerformanceCounter(&end);
    });
    sky_stars_draw_ticks_ = std::uint64_t(end.QuadPart - begin.QuadPart);
    sky_stars_batch_done(report);
}
// A batch's report, in place or at the phase end: the frame's result, the three-failures stop, the restore quarantine.
void MotionOutput::sky_stars_batch_done(const renderer::SkyStarsReport& report) noexcept {
    sky_stars_frame_hr_ = report.operation;
    sky_stars_frame_drawn_ = report.drew;
    if (report.drew)
        sky_stars_failures_ = 0;
    else {
        sky_stars_frame_reason_ = "draw";
        // Three failed batches in a row: no further match (the painted stars come back) until a successful Reset.
        if (++sky_stars_failures_ >= 3 && !sky_stars_attach_failed_) {
            sky_stars_attach_failed_ = true;
            call_preserved([&] {
                log("sky_stars_device device=%llu frame=%llu attached=0 reason=draw_failures result=%08lx step=%u retry=reset",
                    id_, frame_, report.operation, unsigned(report.failed));
            });
        }
    }
    if (FAILED(report.restore)) {
        // The device's state is unknown: quarantine like a failed route restore; the pass stays off until Reset.
        if (!motion_state_lost_) {
            motion_state_lost_ = true;
            motion_state_error_ = report.restore;
        }
        invalidate_render_states();
        invalidate_taa(TaaInvalidateSite::RestoreFailed);
        ++counters_.restore_failures;
        sky_stars_attach_failed_ = true;
        sky_stars_frame_reason_ = "restore";
        call_preserved([&] {
            log("motion_output_restore_failed device=%llu frame=%llu index=%lu result=%08lx what=sky_stars", id_, frame_,
                counters_.draws, report.restore);
        });
    }
}
// Decision C2, before_clear: the predicted phase end draws the frame's pending batch (the header comment).
void MotionOutput::sky_stars_phase_end_draw(DWORD count, DWORD flags) noexcept {
    if (sky_stars_frame_drawn_ || selector_.state() != renderer::BoundaryState::Background ||
        !(flags & D3DCLEAR_ZBUFFER) || (flags & D3DCLEAR_TARGET) || count)
        return; // not the phase end: the batch keeps waiting
    sky_stars_frame_pending_ = false;
    sky_stars_frame_predicted_ = true;
    ++sky_stars_predicted_;
    const auto& v = pending_.viewport;
    const char* why = nullptr;
    if (!pending_.depth.known || !pending_.depth.identity || pending_.depth.identity != main_depth_.identity) why = "depth";
    else if (hdr_state_ != HdrState::Active || !hdr_ || !hdr_->target() || main_msaa_) why = "latch";
    else if (!pending_.only_rt0 || !v.known || v.x || v.y || v.width != sky_stars_width_ || v.height != sky_stars_height_)
        why = "targets";
    else if (shadow_.recording || active_queries_) why = "queries";
    else if (composition_busy_ || composition_state_lost_ || motion_state_lost_) why = "composition";
    else if (!sky_stars_ || !camera_background_.valid) why = "attach"; // a pass that is not ready fails its batch
    float constants[16];
    const auto& b = camera_background_;
    if (!why && !sky_stars::core::vertex_constants(b.r, b.m00, b.m11, b.m20, b.m21, jitter_active_ ? jitter_[0] : 0.f,
                                                   jitter_active_ ? jitter_[1] : 0.f, sky_stars_width_,
                                                   sky_stars_height_, constants))
        why = "constants";
    if (why) {
        ++sky_stars_refused_;
        sky_stars_frame_reason_ = why;
        return;
    }
    renderer::SkyStarsReport report{};
    LARGE_INTEGER begin{}, copied{}, end{};
    call_preserved([&] {
        QueryPerformanceCounter(&begin);
        float twinkle[4], dim[4];
        sky_stars::core::twinkle_constants(std::uint64_t(begin.QuadPart), engine_qpc_frequency(), sky_stars_twinkle_,
                                           twinkle);
        const HRESULT prepared = sky_stars_->prepare_dim(sky_stars_width_, sky_stars_height_);
        sky_stars_copy_hr_ = SUCCEEDED(prepared) ? sky_stars_->copy_dim(hdr_->target()) : prepared;
        const unsigned dw = sky_stars_->dim_width(), dh = sky_stars_->dim_height();
        QueryPerformanceCounter(&copied);
        if (FAILED(prepared)) {
            sky_stars_copy_mode_ = "no_texture";
            sky_stars_->draw(constants, twinkle, &report); // no dim texture: the in-place batch, here
        } else {
            const bool dimmed = SUCCEEDED(sky_stars_copy_hr_);
            sky_stars_copy_mode_ = !dimmed ? "failed" : sky_stars_->caps().copy_filter == D3DTEXF_LINEAR ? "linear" : "point";
            sky_stars::core::dim_constants(dimmed ? sky_stars_dim_ : 0.f, sky_stars_dim_level_, dw, dh, dim);
            sky_stars_->draw_deferred(constants, twinkle, dim, &report);
        }
        QueryPerformanceCounter(&end);
    });
    sky_stars_copy_ticks_ = std::uint64_t(copied.QuadPart - begin.QuadPart);
    sky_stars_draw_ticks_ = std::uint64_t(end.QuadPart - copied.QuadPart);
    if (report.drew) hdr_dirty_ = true;
    sky_stars_batch_done(report);
}
// The sky_stars_defer row (Decision C2, invariant 7): predictions and their confirmation, the copy, the cost.
void MotionOutput::sky_stars_log_defer(const char* why) noexcept {
    if (sky_stars_defer_rows_ >= 1024) return;
    ++sky_stars_defer_rows_;
    const auto& c = sky_stars_ ? sky_stars_->caps() : renderer::SkyStarsCaps{};
    const unsigned dw = sky_stars_ ? sky_stars_->dim_width() : 0u, dh = sky_stars_ ? sky_stars_->dim_height() : 0u;
    call_preserved([&] {
        const std::uint64_t frequency = engine_qpc_frequency() ? engine_qpc_frequency() : 1;
        const double ratio = sky_stars_predicted_ ? double(sky_stars_confirmed_) / double(sky_stars_predicted_) : 0.;
        log("sky_stars_defer device=%llu frame=%llu why=%s predicted=%llu confirmed=%llu ratio=%.4f missed=%llu"
            " unconfirmed=%llu k=%.2f level=%.2f dim=%u copy=%s copy_hr=%08lx dim_texture=%ux%u dim_linear=%u"
            " dim_reason=%s copy_us=%.1f draw_us=%.1f",
            id_, frame_, why, static_cast<unsigned long long>(sky_stars_predicted_),
            static_cast<unsigned long long>(sky_stars_confirmed_), ratio,
            static_cast<unsigned long long>(sky_stars_missed_), static_cast<unsigned long long>(sky_stars_unconfirmed_),
            double(sky_stars_dim_), double(sky_stars_dim_level_), unsigned(c.dim), sky_stars_copy_mode_,
            static_cast<unsigned long>(sky_stars_copy_hr_), dw, dh, unsigned(c.dim_linear), c.dim_reason,
            double(sky_stars_copy_ticks_) * 1e6 / double(frequency), double(sky_stars_draw_ticks_) * 1e6 / double(frequency));
    });
}
// The depth-only Clear (Background -> Scene): the frame's status; one row when it differs from the last row's.
void MotionOutput::sky_stars_phase_end() noexcept {
    if (sky_stars_phase_log_ == 2) sky_stars_phase_log_ = 0; // one logged phase per armed field
    if (sky_stars_frame_predicted_) { // Decision C2: the prediction made in before_clear is confirmed
        sky_stars_frame_predicted_ = false;
        ++sky_stars_confirmed_;
    }
    if (sky_stars_predicted_ && ++sky_stars_defer_frames_ % 4096 == 0) sky_stars_log_defer("periodic");
    ++sky_stars_frames_;
    if (sky_stars_frame_drawn_) ++sky_stars_drawn_frames_;
    const char* status = sky_stars_frame_drawn_ ? "ready" : sky_stars_frame_matched_ ? "refused" : "no_mark";
    const char* reason = sky_stars_frame_drawn_ ? "ok" : sky_stars_frame_matched_ ? sky_stars_frame_reason_ : "no_mark";
    const unsigned full = sky_stars_marks_ ? sky_stars_marks_->refused_full : 0u;
    const bool built_now = sky_stars_ && sky_stars_frame_seed_ && sky_stars_frame_seed_ == sky_stars_->seed();
    const std::uint64_t key = built_now ? sky_stars_->sector_key() : sky_stars_hold_.key;
    if (status == sky_stars_logged_status_ && sky_stars_frame_seed_ == sky_stars_logged_seed_ &&
        reason == sky_stars_logged_reason_ && full == sky_stars_logged_full_ && key == sky_stars_logged_key_)
        return;
    sky_stars_logged_full_ = full;
    sky_stars_logged_key_ = key;
    sky_stars_logged_status_ = status;
    sky_stars_logged_seed_ = sky_stars_frame_seed_;
    sky_stars_logged_reason_ = reason;
    if (sky_stars_predicted_) sky_stars_log_defer("status");
    const std::uint32_t seed = sky_stars_frame_seed_;
    const bool built = sky_stars_ && seed == sky_stars_->seed();
    const unsigned stars = built ? sky_stars_->stars() : 0u;
    const sky_stars::core::BuildStats stats = built ? sky_stars_->stats() : sky_stars::core::BuildStats{};
    call_preserved([&] {
        const std::uint64_t frequency = engine_qpc_frequency() ? engine_qpc_frequency() : 1;
        char draw[16];
        if (sky_stars_frame_drawn_)
            std::snprintf(draw, sizeof draw, "ok");
        else
            std::snprintf(draw, sizeof draw, "%08lx", static_cast<unsigned long>(sky_stars_frame_hr_));
        log("sky_stars device=%llu frame=%llu status=%s mark=%u/%u/%u stars=%u matched=%u masked=%u draw=%s reason=%s"
            " phase_draws=%u frames=%llu drawn_frames=%llu outside=%llu refused=%llu rebuilds=%u table_full=%u density=%.2f"
            " mask_mode=%u draw_us=%.1f build_us=%.1f peak_max=%.4f support_max=%.4f twinkling=%u clustering=%.2f"
            " cluster_max_min=%.2f reject_rate=%.3f key=%llu",
            id_, frame_, status, sky_stars::core::seed_slot(seed), sky_stars::core::seed_lane(seed),
            sky_stars::core::seed_k(seed), stars, sky_stars_frame_matched_, sky_stars_frame_masked_, draw, reason,
            sky_stars_phase_draws_, static_cast<unsigned long long>(sky_stars_frames_),
            static_cast<unsigned long long>(sky_stars_drawn_frames_), static_cast<unsigned long long>(sky_stars_outside_),
            static_cast<unsigned long long>(sky_stars_refused_), sky_stars_rebuilds_, full,
            double(sky_stars_look_.density), unsigned(sky_stars_mask_mode_),
            double(sky_stars_draw_ticks_) * 1e6 / double(frequency), double(sky_stars_build_ticks_) * 1e6 / double(frequency),
            stats.peak_max, stats.support_max, stats.twinkling, double(sky_stars_look_.clustering), stats.cluster_max_min,
            stats.reject_rate, static_cast<unsigned long long>(key));
    });
    sky_stars_frames_ = sky_stars_drawn_frames_ = 0;
}
