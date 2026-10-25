// Merged-LOD overlay material strength (included by motion_output.cpp; renderer/linear_material.h
// linear_material_strength_twin_pixel_variant; docs/architecture/overlay-material-strength.md, "option B"): the
// option, the marked-texture table the D3DX texture hook fills, the twins of the original-shading hull variants (and
// of their engine-light twins) created lazily at a kind's first marked selection, the per-draw selection and the
// frame row.
//
// Costs (design section 8; verification/results/overlay-material-strength/perf): per routed draw one byte load and
// compare (`shadow_.strength_stage`, 0 for every program without a specular stage or with the option off); only on
// a marked draw the sampler byte and at most 7 pointer compares. Per SetTexture of a non-null texture one load of
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
// deterministic transform the registration created it with) and the twin is that form plus the two insertions. A
// refused twin (linear_material.h StrengthTwinRefusal), a regenerated form whose out-flags differ from the created
// one, or a failed create leaves the kind without a twin for good (no retry): the draw keeps the parent (counted
// no_twin). One debug-tier lod_strength_variant row per attempt.
IDirect3DPixelShader9* MotionOutput::strength_twin(IDirect3DPixelShader9* ps, bool engine) noexcept {
    if (!ps) return nullptr;
    const IDirect3DPixelShader9* bases[engine_light_kinds] = {
        shadow_.ps_variant,          shadow_.ps_original_fill_variant, shadow_.ps_hull_lightmap_variant,
        shadow_.ps_hull_lightmap_widen, shadow_.ps_sun_original,    shadow_.ps_sun_original_lightmap,
        shadow_.ps_sun_original_lightmap_widen};
    const unsigned e = engine ? 1u : 0u;
    unsigned kind = engine_light_kinds;
    for (unsigned k = 0; k < engine_light_kinds; ++k)
        if (ps == (engine ? shadow_.engine_twin[k] : bases[k])) kind = k;
    if (kind == engine_light_kinds) return nullptr;
    if (shadow_.strength_twin[e][kind]) return shadow_.strength_twin[e][kind];
    // First marked selection of this kind: create the twin once (or record the attempt).
    const auto it = pixel_.find(shadow_.ps);
    if (it == pixel_.end()) return nullptr;
    ShaderEntry& entry = it->second;
    if (entry.strength_attempted[e] & (1u << kind) || entry.strength_code.empty()) return nullptr;
    entry.strength_attempted[e] |= std::uint8_t(1u << kind);
    const float fill = original_fill_requested_ ? original_fill_ : 0.f;
    const renderer::HullLightmapWiden widen{lightmap_widen_k_, lightmap_widen_b_};
    renderer::OriginalVariantOptions options;
    if (kind == 1) options.fill = original_fill_;
    if (kind >= 2) options.fill = fill;
    if (kind == 2 || kind == 3 || kind == 5 || kind == 6) {
        options.lightmap_gain = hull_lightmap_gain_;
        options.lightmap_dynamic = lightmap_far_fade_;
    }
    if (kind == 3 || kind == 6) options.widen = &widen;
    if (kind >= 4) options.share = true;
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
        bool fill_applied = false, share = false, gain = false, widened = false, engine_applied = false;
        const auto result = renderer::linear_material_original_variant_pixel(
            entry.strength_code.data(), entry.strength_code.size(), options, engine, parent, depth_enabled_,
            &fill_applied, &share, &gain, &widened, &engine_applied);
        // The regenerated form must be the one the registration created: the same out-flags.
        const bool same = result == renderer::LinearMaterialResult::Applied && share == options.share &&
                          gain == (options.lightmap_gain != 1.f) && widened == (options.widen != nullptr) &&
                          engine_applied == engine;
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
    } else if (std::strcmp(refusal, "none"))
        ++strength_twins_refused_;
    else
        ++strength_twins_failed_;
    if (log_tier::cached_debug)
        log("lod_strength_variant device=%llu frame=%llu original=%016llx kind=%u engine=%u stage=%u refusal=%s create=%08lx words=%u slots=%lu slot_budget=%lu depth=%u transform_us=%u create_us=%u",
            id_, frame_, entry.hash, kind, e, unsigned(entry.strength_stage), refusal, hr, unsigned(words),
            static_cast<unsigned long>(slots), static_cast<unsigned long>(renderer::ps3_slot_budget()),
            unsigned(depth_enabled_), us(transform_ticks), us(create_ticks));
    return object;
}
// Frame row (debug tier, family frames, only when something changed since the last row): the twin draws and the
// marked draws without a twin this frame, the marked textures seen so far by atlas slot (diffuse, light, bump,
// specular, other), the table size, stale and released drops, the twins created, refused and failed. Settles section
// 10's question whether every overlay atlas reaches the device through the D3DX hook: `seen` per slot against the
// marker's atlas members.
// Milliseconds x 100 of the lazy twin creation so far (integer arithmetic: no x87 on a hook-reachable path).
std::uint32_t MotionOutput::strength_twin_ms100(std::uint64_t ticks) noexcept {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    return f.QuadPart ? std::uint32_t(ticks * 100000ull / std::uint64_t(f.QuadPart)) : 0u;
}
void MotionOutput::strength_frame(bool family) noexcept {
    if (!strength_requested_) return;
    if (family && log_tier::cached_debug) {
        const std::uint32_t totals[6] = {strength_marks_seen_[0] + strength_marks_seen_[1] + strength_marks_seen_[2] +
                                             strength_marks_seen_[3] + strength_marks_seen_[4],
                                         strength_marks_stale_, strength_marks_dropped_, strength_twins_created_,
                                         strength_twins_refused_, strength_twins_failed_};
        bool changed = strength_draws_ || strength_marked_draws_;
        for (unsigned i = 0; i < 6; ++i) changed |= totals[i] != strength_logged_[i];
        if (changed) {
            const std::uint32_t ms100 = strength_twin_ms100(strength_transform_ticks_ + strength_create_ticks_);
            log("lod_strength_frame device=%llu frame=%llu draws=%u marked=%u no_twin=%u seen=%u,%u,%u,%u,%u table=%u stale=%u released=%u twins=%u refused=%u failed=%u twin_ms=%lu.%02lu session_draws=%u",
                id_, frame_, strength_draws_, strength_marked_draws_, strength_no_twin_, strength_marks_seen_[0],
                strength_marks_seen_[1], strength_marks_seen_[2], strength_marks_seen_[3], strength_marks_seen_[4],
                strength_marks_ ? strength_marks_->count : 0u, strength_marks_stale_, strength_marks_dropped_,
                strength_twins_created_, strength_twins_refused_, strength_twins_failed_,
                static_cast<unsigned long>(ms100 / 100), static_cast<unsigned long>(ms100 % 100), strength_session_draws_);
            for (unsigned i = 0; i < 6; ++i) strength_logged_[i] = totals[i];
        }
    }
    strength_draws_ = 0;
    strength_marked_draws_ = 0;
    strength_no_twin_ = 0;
}
// Session total (debug or perf tier, at the device's release): twins created / refused / failed and the time spent
// on their transforms and CreatePixelShader calls; nothing when nothing was attempted.
void MotionOutput::strength_session_row() noexcept {
    if (!(log_tier::cached_debug || log_tier::cached_perf)) return;
    if (!(strength_twins_created_ || strength_twins_refused_ || strength_twins_failed_)) return;
    const std::uint32_t t = strength_twin_ms100(strength_transform_ticks_), c = strength_twin_ms100(strength_create_ticks_);
    log("lod_strength_session device=%llu twins=%u refused=%u failed=%u transform_ms=%lu.%02lu create_ms=%lu.%02lu draws=%u marked_seen=%u,%u,%u,%u,%u",
        id_, strength_twins_created_, strength_twins_refused_, strength_twins_failed_, static_cast<unsigned long>(t / 100),
        static_cast<unsigned long>(t % 100), static_cast<unsigned long>(c / 100), static_cast<unsigned long>(c % 100),
        strength_session_draws_, strength_marks_seen_[0], strength_marks_seen_[1], strength_marks_seen_[2],
        strength_marks_seen_[3], strength_marks_seen_[4]);
}
