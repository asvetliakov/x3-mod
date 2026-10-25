// Merged-LOD overlay material strength (included by motion_output.cpp; renderer/linear_material.h
// linear_material_strength_twin_pixel_variant; docs/architecture/overlay-material-strength.md, "option B"): the
// option, the marked-texture table the D3DX texture hook fills, the twins of the original-shading hull variants (and
// of their engine-light twins) created lazily at a kind's first marked selection, the per-draw selection and the
// frame row.
//
// Costs (design section 8; verification/results/overlay-material-strength/perf): per routed draw one byte load and
// compare (`shadow_.strength_stage`, 0 for every program without a specular stage or with the option off); only on
// a marked draw the sampler byte and at most 8 pointer compares. Per SetTexture of a non-null texture one load of
// the table count (an empty table ends there) and, with marks stored, one open-addressing probe; a hit alone pays
// one GetPrivateData read of the resource identity (a pointer reused by a later allocation is dropped and counted).
// Per marked texture load one table insert; per final texture release one probe (capture.cpp texture_released).
// Twin creation: once per (program, kind) at the first marked draw that selects it (a transform over the parent's
// regenerated words and one CreatePixelShader, on the render thread like the driver's own first-draw compile); a
// session that never binds a marked atlas creates nothing and keeps only the original words of the programs with a
// specular stage (about 100 x 6 KB).
void MotionOutput::configure_material_strength(bool requested) noexcept {
    if (device_) return; // Creation-time shader variant: immutable after attach.
    strength_requested_ = requested;
    if (requested && !strength_marks_) strength_marks_ = new (std::nothrow) lod_texture_mark::Table<4096>();
    if (!strength_marks_) strength_requested_ = false; // no table: no marks, no twins (fail closed)
}
bool MotionOutput::strength_mark_insert(IDirect3DBaseTexture9* texture, std::uint64_t identity, std::uint8_t lane,
                                        std::uint8_t slot) noexcept {
    if (!strength_requested_ || !strength_marks_ || !texture) return false;
    ++strength_marks_seen_[slot < lod_texture_mark::slot_count ? slot : lod_texture_mark::slot_count];
    if (lane == lod_texture_mark::lane_none) return false; // counted (the other atlases); only the ratio carrier is stored
    const bool stored = strength_marks_->insert(texture, identity, lane, slot);
    if (!stored && !strength_full_logged_) {
        // Standard tier, once: the table refused (half full); the texture draws at the merged strength.
        strength_full_logged_ = true;
        log("lod_strength_table_full device=%llu entries=%u refused=%u", id_, strength_marks_->count,
            strength_marks_->refused_full);
    }
    return stored;
}
bool MotionOutput::strength_mark_forget(const void* texture) noexcept {
    if (!strength_marks_ || !strength_marks_->count || !strength_marks_->find(texture)) return false;
    strength_marks_->erase(texture);
    ++strength_marks_dropped_;
    return true;
}
// The shadow byte for a texture the application binds: the table's lane when the pointer is known and its identity
// still matches (a reused address of a freed node is dropped), else none.
std::uint8_t MotionOutput::strength_lane(IDirect3DBaseTexture9* texture) noexcept {
    if (!strength_marks_ || !strength_marks_->count || !texture) return lod_texture_mark::lane_none;
    auto* entry = strength_marks_->find(texture);
    if (!entry) return lod_texture_mark::lane_none;
    const std::uint64_t identity = resource_id(texture);
    if (!identity || identity != entry->identity) {
        strength_marks_->erase(texture);
        ++strength_marks_stale_;
        return lod_texture_mark::lane_none;
    }
    return entry->lane;
}
// Registration: a program with a specular stage keeps its original words for the lazy twins; nothing is created here.
void MotionOutput::strength_prepare(ShaderEntry& entry, const void* code, UINT bytes, std::uint64_t hash) noexcept {
    strength_release(entry);
    if (!strength_requested_ || !entry.variant) return;
    const unsigned stage = renderer::linear_material_strength_stage(hash, bytes / 4);
    if (!stage) return; // asteroid / glass layouts or an unreviewed program: no specular stage, no twins
    try {
        entry.strength_code.assign(static_cast<const std::uint32_t*>(code),
                                   static_cast<const std::uint32_t*>(code) + bytes / 4);
        entry.strength_stage = std::uint8_t(stage);
    } catch (...) {
        entry.strength_code.clear();
        entry.strength_stage = 0;
    }
}
void MotionOutput::strength_release(ShaderEntry& entry) noexcept {
    for (auto& row : entry.strength_twin)
        for (auto*& twin : row) release(twin);
    entry.strength_stage = 0;
    entry.strength_attempted[0] = entry.strength_attempted[1] = 0;
    std::vector<std::uint32_t>().swap(entry.strength_code);
}
// The twin of the program bind_variant_pair chose (`engine`: it is an engine-light twin), created at its first marked
// selection: the parent form is regenerated from its options (engine_light_create_twins' kind order; the same
// deterministic transform the registration created it with) and the twin is that form plus the two insertions.
// Kind 7 is the sun-lane motion form (sun_motion_variant: material_motion_invalid_sun_share of the motion variant, a
// leading `def c221` and a trailing `mov oC2.g, c221.x`), which the fade owner binds on the sun lane (and the lane
// binds for a program without a share producer); its twin is the motion twin with the same two additions. A
// refused twin (linear_material.h StrengthTwinRefusal), a regenerated form whose out-flags differ from the created
// one, or a failed create leaves the kind without a twin for good (no retry): the draw keeps the parent (counted
// no_twin). One debug-tier lod_strength_variant row per attempt. `reason`: why the result is null (strength_gap_*).
IDirect3DPixelShader9* MotionOutput::strength_twin(IDirect3DPixelShader9* ps, bool engine, std::uint8_t& reason) noexcept {
    static_assert(sizeof(shadow_.strength_twin[0]) / sizeof(shadow_.strength_twin[0][0]) == strength_kinds &&
                      strength_kinds == engine_light_kinds + 1 && strength_sun_motion == engine_light_kinds,
                  "the strength twins: the engine-light kinds and the sun-lane motion form");
    reason = strength_gap_form;
    if (!ps) return nullptr;
    const IDirect3DPixelShader9* bases[strength_kinds] = {
        shadow_.ps_variant,          shadow_.ps_original_fill_variant, shadow_.ps_hull_lightmap_variant,
        shadow_.ps_hull_lightmap_widen, shadow_.ps_sun_original,    shadow_.ps_sun_original_lightmap,
        shadow_.ps_sun_original_lightmap_widen, shadow_.ps_sun_motion};
    const unsigned e = engine ? 1u : 0u;
    unsigned kind = strength_kinds;
    for (unsigned k = 0; k < strength_kinds; ++k)
        if (ps == (engine ? (k < engine_light_kinds ? shadow_.engine_twin[k] : nullptr) : bases[k])) kind = k;
    if (kind == strength_kinds) return nullptr;
    if (shadow_.strength_twin[e][kind]) return shadow_.strength_twin[e][kind];
    // First marked selection of this kind: create the twin once (or record the attempt).
    const auto it = pixel_.find(shadow_.ps);
    reason = strength_gap_entry;
    if (it == pixel_.end()) return nullptr;
    ShaderEntry& entry = it->second;
    reason = entry.strength_code.empty() ? strength_gap_code : strength_gap_refused;
    if (entry.strength_attempted[e] & (1u << kind) || entry.strength_code.empty()) return nullptr;
    entry.strength_attempted[e] |= std::uint8_t(1u << kind);
    const float fill = original_fill_requested_ ? original_fill_ : 0.f;
    const renderer::HullLightmapWiden widen{lightmap_widen_k_, lightmap_widen_b_};
    renderer::OriginalVariantOptions options;
    if (kind == 1) options.fill = original_fill_;
    if (kind >= 2 && kind != strength_sun_motion) options.fill = fill;
    if (kind == 2 || kind == 3 || kind == 5 || kind == 6) {
        options.lightmap_gain = hull_lightmap_gain_;
        options.lightmap_dynamic = lightmap_far_fade_;
    }
    if (kind == 3 || kind == 6) options.widen = &widen;
    if (kind >= 4 && kind != strength_sun_motion) options.share = true;
    const char* refusal = "none";
    HRESULT hr = E_FAIL;
    std::size_t words = 0;
    std::uint32_t slots = 0;
    IDirect3DPixelShader9* object = nullptr;
    // Timing of the one-time creation (QPC; the perf flight shows whether lazy creation is a visible hitch).
    static LONGLONG frequency = 0;
    if (!frequency) {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        frequency = f.QuadPart ? f.QuadPart : 1;
    }
    LARGE_INTEGER t0{}, t1{}, t2{};
    QueryPerformanceCounter(&t0);
    t1 = t2 = t0;
    try {
        std::vector<std::uint32_t> parent, twin;
        bool same = false;
        if (kind == strength_sun_motion) {
            // The registration's two steps (sun_motion_variant): the motion variant, then the invalid share.
            same = renderer::material_motion_pixel_variant(entry.strength_code.data(), entry.strength_code.size(),
                                                           parent, depth_enabled_) ==
                       renderer::MaterialMotionResult::Applied &&
                   renderer::material_motion_invalid_sun_share(parent);
        } else {
            bool fill_applied = false, share = false, gain = false, widened = false, engine_applied = false;
            const auto result = renderer::linear_material_original_variant_pixel(
                entry.strength_code.data(), entry.strength_code.size(), options, engine, parent, depth_enabled_,
                &fill_applied, &share, &gain, &widened, &engine_applied);
            // The regenerated form must be the one the registration created: the same out-flags.
            same = result == renderer::LinearMaterialResult::Applied && share == options.share &&
                   gain == (options.lightmap_gain != 1.f) && widened == (options.widen != nullptr) &&
                   engine_applied == engine;
        }
        if (!same)
            refusal = "form_mismatch";
        else {
            renderer::StrengthTwinRefusal why = renderer::StrengthTwinRefusal::None;
            const auto twin_result = renderer::linear_material_strength_twin_pixel_variant(
                parent.data(), parent.size(), entry.strength_code.data(), entry.strength_code.size(), twin, nullptr,
                &why);
            if (twin_result != renderer::LinearMaterialResult::Applied)
                refusal = renderer::linear_material_strength_refusal_name(why);
            else {
                words = twin.size();
                slots = renderer::ps3_program_slots(twin.data(), twin.size());
                QueryPerformanceCounter(&t1);
                hr = native<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(twin.data()), &object);
                QueryPerformanceCounter(&t2);
                if (FAILED(hr) || !object) release(object);
            }
        }
    } catch (...) {
        refusal = "allocation";
        release(object);
    }
    if (t1.QuadPart == t0.QuadPart) QueryPerformanceCounter(&t1), t2 = t1; // refused before the create: all transform
    const std::uint64_t transform_ticks = std::uint64_t(t1.QuadPart - t0.QuadPart),
                        create_ticks = std::uint64_t(t2.QuadPart - t1.QuadPart);
    strength_transform_ticks_ += transform_ticks;
    strength_create_ticks_ += create_ticks;
    // Integer arithmetic only (the x87 audit walks this path from the draw hooks): microseconds as ticks * 10^6 / f.
    const auto us = [&](std::uint64_t ticks) { return unsigned(ticks * 1000000ull / std::uint64_t(frequency)); };
    if (object) {
        entry.strength_twin[e][kind] = object;
        shadow_.strength_twin[e][kind] = object;
        ++strength_twins_created_;
        // Follow-up rows at this frame's end (after its Present) and the next one's: whether the first use of the
        // new program costs a driver compile there (Run 203 A: about 42 ms in scene_end on an engine twin's frame).
        if (log_tier::cached_debug) {
            if (!strength_followup_left_ || strength_followup_frame_ != frame_) {
                strength_followup_frame_ = frame_;
                strength_followup_qpc_ = std::uint64_t(t2.QuadPart);
                strength_followup_twins_ = 0;
            }
            ++strength_followup_twins_;
            strength_followup_left_ = 2;
        }
    } else {
        if (std::strcmp(refusal, "none"))
            ++strength_twins_refused_;
        else
            ++strength_twins_failed_;
        reason = strength_gap_refused;
    }
    static const char* const forms[strength_kinds] = {"motion", "fill", "gain", "widen", "share", "sgain", "swiden",
                                                      "smotion"};
    if (log_tier::cached_debug)
        log("lod_strength_variant device=%llu frame=%llu original=%016llx kind=%u engine=%u stage=%u refusal=%s create=%08lx words=%u slots=%lu slot_budget=%lu depth=%u transform_us=%u create_us=%u form=%s%s",
            id_, frame_, entry.hash, kind, e, unsigned(entry.strength_stage), refusal, hr, unsigned(words),
            static_cast<unsigned long>(slots), static_cast<unsigned long>(renderer::ps3_slot_budget()),
            unsigned(depth_enabled_), us(transform_ticks), us(create_ticks), e ? "e" : "", forms[kind]);
    return object;
}
// A marked draw without a twin (debug tier): one lod_strength_no_twin row per distinct (bound program hash, the form
// of the program bind_variant_pair chose, reason), the first strength_gap_limit; later distinct keys only count
// (dropped= on the session row). Per no_twin draw at most strength_gap_limit 10-byte compares; the form's name and
// the row only for a new key. Forms: the strength kinds by name ("e" prefix: the engine-light twin), sun_xt / xt
// (XT DEFAULT ordinary, linear materials only), sun_material / material (the material route), other (anything else).
void MotionOutput::strength_gap(IDirect3DPixelShader9* ps, bool engine, std::uint8_t reason,
                                const MotionRoute& route) noexcept {
    static const char* const names[] = {"motion", "fill", "gain", "widen", "share", "sgain", "swiden", "smotion",
                                        "emotion", "efill", "egain", "ewiden", "eshare", "esgain", "eswiden",
                                        "sun_xt", "xt", "sun_material", "material", "other"};
    constexpr unsigned other = 19;
    const IDirect3DPixelShader9* bases[strength_kinds] = {
        shadow_.ps_variant,          shadow_.ps_original_fill_variant, shadow_.ps_hull_lightmap_variant,
        shadow_.ps_hull_lightmap_widen, shadow_.ps_sun_original,    shadow_.ps_sun_original_lightmap,
        shadow_.ps_sun_original_lightmap_widen, shadow_.ps_sun_motion};
    unsigned form = other;
    if (ps) {
        for (unsigned k = 0; k < strength_kinds && form == other; ++k)
            if (engine ? (k < engine_light_kinds && ps == shadow_.engine_twin[k]) : ps == bases[k])
                form = engine ? engine_light_kinds + 1 + k : k;
        if (form == other)
            form = ps == shadow_.ps_sun_xt                ? 15u
                   : ps == shadow_.ps_xt_default_ordinary ? 16u
                   : ps == shadow_.ps_sun_material        ? 17u
                   : ps == shadow_.ps_material_variant    ? 18u
                                                          : other;
    }
    for (std::uint32_t i = 0; i < strength_gap_count_; ++i)
        if (strength_gaps_[i].ps_hash == shadow_.ps_hash && strength_gaps_[i].form == form &&
            strength_gaps_[i].reason == reason)
            return;
    if (strength_gap_count_ == strength_gap_limit) {
        ++strength_gap_dropped_;
        return;
    }
    strength_gaps_[strength_gap_count_++] = {shadow_.ps_hash, std::uint8_t(form), reason};
    static const char* const reasons[] = {"form", "refused", "no_entry", "no_code", "material"};
    log("lod_strength_no_twin device=%llu frame=%llu ps=%016llx form=%s reason=%s stage=%u fade_arm=%u fade_owner=%u sun_lane=%u sun_receiver=%u depth=%u distinct=%u",
        id_, frame_, shadow_.ps_hash, names[form], reason < 5 ? reasons[reason] : "?", unsigned(shadow_.strength_stage),
        unsigned(route.fade_arm), unsigned(route.fade_owner), unsigned(sun_lane_active_), unsigned(route.sun_receiver),
        unsigned(route.depth), strength_gap_count_);
}
// QPC frequency, read once (the frame row's cadence and the follow-up rows; integer arithmetic only).
static std::uint64_t strength_frequency() noexcept {
    static std::uint64_t frequency = 0;
    if (!frequency) {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        frequency = f.QuadPart > 0 ? std::uint64_t(f.QuadPart) : 1u;
    }
    return frequency;
}
// Milliseconds x 100 of the lazy twin creation so far (integer arithmetic: no x87 on a hook-reachable path).
std::uint32_t MotionOutput::strength_twin_ms100(std::uint64_t ticks) noexcept {
    return std::uint32_t(ticks * 100000ull / strength_frequency());
}
// Frame row (debug tier, family frames): the twin draws, the marked draws and the marked draws without a twin summed
// over the `frames` since the previous row, the marked textures seen so far by atlas slot (diffuse, light, bump,
// specular, other), the table size, stale and released drops, the twins created, refused and failed. Written only
// when one of the non-cumulative values moved (seen, table, stale, released, twins, refused, failed: reason=change),
// at the first frame with a no_twin draw after one without (reason=onset), or, with draws pending, once per
// strength_row_seconds of real time (reason=cadence; one QPC per debug frame while draws are pending). Settles section
// 10's question whether every overlay atlas reaches the device through the D3DX hook: `seen` per slot against the
// marker's atlas members. Then the twin-creation follow-up rows (lod_strength_variant_frame).
void MotionOutput::strength_frame(bool family) noexcept {
    if (!strength_requested_) return;
    strength_session_marked_ += strength_marked_draws_;
    strength_session_no_twin_ += strength_no_twin_;
    if (log_tier::cached_debug) {
        constexpr std::uint64_t strength_row_seconds = 10;
        strength_interval_[0] += strength_draws_;
        strength_interval_[1] += strength_marked_draws_;
        strength_interval_[2] += strength_no_twin_;
        ++strength_interval_frames_;
        // Latched: an onset on a frame without a row (X3M_MOTION_FRAME_LOG cadence > 1) is reported at the next family
        // frame.
        if (strength_no_twin_ && !strength_no_twin_previous_) strength_onset_pending_ = true;
        strength_no_twin_previous_ = strength_no_twin_ != 0;
        if (family) {
            const bool onset = strength_onset_pending_;
            strength_onset_pending_ = false;
            const std::uint32_t totals[7] = {strength_marks_seen_[0] + strength_marks_seen_[1] + strength_marks_seen_[2] +
                                                 strength_marks_seen_[3] + strength_marks_seen_[4],
                                             strength_marks_ ? strength_marks_->count : 0u, strength_marks_stale_,
                                             strength_marks_dropped_, strength_twins_created_, strength_twins_refused_,
                                             strength_twins_failed_};
            bool changed = false;
            for (unsigned i = 0; i < 7; ++i) changed |= totals[i] != strength_logged_[i];
            const char* reason = changed ? "change" : onset ? "onset" : nullptr;
            std::uint64_t now = strength_row_qpc_;
            if (reason || strength_interval_[0] || strength_interval_[1]) {
                LARGE_INTEGER stamp{};
                QueryPerformanceCounter(&stamp);
                now = std::uint64_t(stamp.QuadPart);
                if (!reason && now - strength_row_qpc_ >= strength_row_seconds * strength_frequency()) reason = "cadence";
            }
            if (reason) {
                const std::uint32_t ms100 = strength_twin_ms100(strength_transform_ticks_ + strength_create_ticks_);
                log("lod_strength_frame device=%llu frame=%llu draws=%u marked=%u no_twin=%u seen=%u,%u,%u,%u,%u table=%u stale=%u released=%u twins=%u refused=%u failed=%u twin_ms=%lu.%02lu session_draws=%llu frames=%u reason=%s",
                    id_, frame_, strength_interval_[0], strength_interval_[1], strength_interval_[2],
                    strength_marks_seen_[0], strength_marks_seen_[1], strength_marks_seen_[2], strength_marks_seen_[3],
                    strength_marks_seen_[4], totals[1], strength_marks_stale_, strength_marks_dropped_,
                    strength_twins_created_, strength_twins_refused_, strength_twins_failed_,
                    static_cast<unsigned long>(ms100 / 100), static_cast<unsigned long>(ms100 % 100),
                    static_cast<unsigned long long>(strength_session_draws_), strength_interval_frames_, reason);
                for (unsigned i = 0; i < 7; ++i) strength_logged_[i] = totals[i];
                strength_interval_[0] = strength_interval_[1] = strength_interval_[2] = 0;
                strength_interval_frames_ = 0;
                strength_row_qpc_ = now;
            }
        }
        if (strength_followup_left_) {
            // After this frame's Present: since_create_us from the first twin's CreatePixelShader of the creation
            // frame to now; step 1 adds frame_us, the whole next frame (Present to Present). Integer microseconds.
            LARGE_INTEGER stamp{};
            QueryPerformanceCounter(&stamp);
            const std::uint64_t now = std::uint64_t(stamp.QuadPart), frequency = strength_frequency();
            const unsigned step = strength_followup_left_ == 2 ? 0u : 1u;
            const auto us = [frequency](std::uint64_t ticks) { return unsigned(ticks * 1000000ull / frequency); };
            log("lod_strength_variant_frame device=%llu frame=%llu created_frame=%llu twins=%u step=%u since_create_us=%u frame_us=%u",
                id_, frame_, strength_followup_frame_, strength_followup_twins_, step,
                us(now - strength_followup_qpc_), step ? us(now - strength_followup_end_qpc_) : 0u);
            strength_followup_end_qpc_ = now;
            --strength_followup_left_;
        }
    }
    strength_draws_ = 0;
    strength_marked_draws_ = 0;
    strength_no_twin_ = 0;
}
// Session total (debug or perf tier, once: at the device's release, or at process exit through exit_rows when the
// game ends without releasing it): twins created / refused / failed and the time spent on their transforms and
// CreatePixelShader calls, the twin draws, the marked and no_twin draws (summed per frame), the marked textures seen
// by slot, the no_twin diagnostic keys logged and dropped; nothing when nothing was attempted or marked.
void MotionOutput::strength_session_row(const char* at) noexcept {
    if (strength_session_logged_ || !(log_tier::cached_debug || log_tier::cached_perf)) return;
    if (!(strength_twins_created_ || strength_twins_refused_ || strength_twins_failed_ || strength_session_marked_))
        return;
    strength_session_logged_ = true;
    const std::uint32_t t = strength_twin_ms100(strength_transform_ticks_), c = strength_twin_ms100(strength_create_ticks_);
    log("lod_strength_session device=%llu twins=%u refused=%u failed=%u transform_ms=%lu.%02lu create_ms=%lu.%02lu draws=%llu marked_seen=%u,%u,%u,%u,%u marked=%llu no_twin=%llu no_twin_keys=%u no_twin_keys_dropped=%u at=%s",
        id_, strength_twins_created_, strength_twins_refused_, strength_twins_failed_, static_cast<unsigned long>(t / 100),
        static_cast<unsigned long>(t % 100), static_cast<unsigned long>(c / 100), static_cast<unsigned long>(c % 100),
        static_cast<unsigned long long>(strength_session_draws_), strength_marks_seen_[0], strength_marks_seen_[1],
        strength_marks_seen_[2], strength_marks_seen_[3], strength_marks_seen_[4],
        static_cast<unsigned long long>(strength_session_marked_),
        static_cast<unsigned long long>(strength_session_no_twin_), strength_gap_count_, strength_gap_dropped_, at);
}
