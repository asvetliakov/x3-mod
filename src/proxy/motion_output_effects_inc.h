// Included by motion_output.cpp inside namespace x3m (as the fog fragment is): the proxy-owned effects stage, phase 1
// (docs/architecture/effects-modernisation-opus.md sections 2, 3.1, 3.3, 8 and 9; X3M_EFFECTS_STAGE=1, launcher
// --effects-stage). Recognised game effect draws become records; the stage draws them once per frame as the first act
// of the temporal resolve's state bracket (TemporalPass FrameInputs::stage_callback: after RT1/RT2 and the depth
// surface are unbound, RT0 still the FP16 scene), ONE/ONE on the FP16 target, occluded softly by the completed lane.
// Phase 1 does NOT suppress the recorded draws (review B1, 2026-09-25): whether the stage will run is not known at
// draw time (the resolve can be skipped at scene end, the state lost mid-frame, the run can fail before the callback),
// and a suppressed sprite cannot be forwarded natively later without retaining game resources. The native draw
// therefore always goes through (the bullets through the additive route as before, only without the footprint rewrite
// on a recorded draw; the sprites unchanged) and the stage adds its capsules and shells on top; a frame the stage
// does not reach shows the game's own effects. Bolts come from the admitted additive bullet draws (the locked-prefix
// copy the bolt footprint already reads); shield hits from the DEFAULT effect pair's draws whose stage-0 texture
// carries a key the shipped table classes as shield_hit (--effects-shields). Fail closed: an unknown key, a class the
// table does not name, a disarmed stage or an overflow records nothing. A failed stage loses that frame's stage draws
// (the game's stay) and disarms the stage for 64 frames (one effects_stage_failed row).
namespace {
constexpr std::uint64_t effects_vs_default = 0xd5e1c75351ed3f04ull, effects_vs_instance = 0x89193868c61c3846ull, effects_ps = 0x8360f422de08b5bdull;
constexpr unsigned effects_window_frames = 300;
constexpr float effects_spatial_cell = 8.f;      // world units: the spatial identity's quantum of an unscoped sprite
constexpr float effects_default_decal_radius = 24.f; // world units when the key entry carries no extent
constexpr float effects_min_dt = 1.f / 240.f, effects_max_dt = 0.1f;
}
void MotionOutput::configure_effects_stage(const EffectsStageConfig& config) noexcept {
    effects_requested_ = config.requested; effects_shields_ = config.shields; effects_census_ = config.census;
    effects_bolt_views_all_ = config.bolt_views_all; effects_bolt_views_default_ = config.bolt_views_default;
    effects_tuning_ = config.tuning;
    if (!renderer::valid_tuning(effects_tuning_)) { effects_tuning_ = renderer::EffectsStageTuning{}; effects_tuning_reset_ = true; }
    if (effects_requested_ && !effects_bolt_vertices_) {
        effects_bolt_vertices_.reset(new (std::nothrow) effects_stage::BoltVertex[effects_stage::max_bolts * effects_stage::bolt_vertices_per_instance]);
        effects_bolts_[0].reset(new (std::nothrow) effects_stage::BoltInstance[effects_stage::max_bolts]);
        effects_bolts_[1].reset(new (std::nothrow) effects_stage::BoltInstance[effects_stage::max_bolts]);
        effects_raw_boxes_.reset(new (std::nothrow) EffectsRawBox[effects_stage::max_boxes]);
        if (!effects_bolt_vertices_ || !effects_bolts_[0] || !effects_bolts_[1] || !effects_raw_boxes_) { effects_requested_ = false; effects_allocation_failed_ = true; }
    }
    effects_enabled_ = true;
    log("effects_stage_config device=%llu requested=%u enabled=%u shields=%u census=%u bolt_views=%s bolt_views_default=%u keys=%u key_duplicates=%u key_refused=%u table=%s w_min=%g l_min=%g halo=%g stretch=%g core=%g halo_gain=%g soft=%g tuning=%s allocation=%s key=ctrl_alt_f5",
        id_, unsigned(config.requested), unsigned(effects_requested_), unsigned(effects_shields_), unsigned(effects_census_), effects_bolt_views_all_ ? "all" : "chase", unsigned(effects_bolt_views_default_),
        effects_keys_.count, effects_keys_.duplicates, effects_keys_.refused, effects_keys_status_, double(effects_tuning_.min_width_px), double(effects_tuning_.min_length_px), double(effects_tuning_.halo),
        double(effects_tuning_.stretch), double(effects_tuning_.core_intensity), double(effects_tuning_.halo_intensity), double(effects_tuning_.soft), effects_tuning_reset_ ? "reset_to_defaults" : "ok",
        effects_allocation_failed_ ? "failed" : "ok");
}
// The shipped key table (tools/effects/effect_keys.py -> effect_keys.json): parsed once, before configure_effects_stage.
void MotionOutput::configure_effects_key_table(const char* text, std::size_t length, const char* status) noexcept {
    std::size_t fault = 0;
    const bool ok = text && effects_stage::parse_key_table(text, length, &effects_keys_, &fault);
    effects_keys_status_ = !text ? status : ok ? "ok" : "malformed";
    if (text && !ok) log("effects_stage_keys device=%llu status=malformed fault_offset=%lu entries_kept=%u", id_, static_cast<unsigned long>(fault), effects_keys_.count);
}
int MotionOutput::effects_stage_toggle() noexcept {
    if (!effects_requested_) return -1;
    effects_enabled_ = !effects_enabled_;
    log("effects_stage_toggle device=%llu frame=%llu enabled=%u key=ctrl_alt_f5", id_, frame_, unsigned(effects_enabled_));
    return effects_enabled_ ? 1 : 0;
}
// Resources at the latch (the fog's rule): attached once per device, refused until Reset on failure.
bool MotionOutput::attach_effects_stage() noexcept {
    if (effects_attach_failed_) return false;
    if (!effects_) {
        try { effects_ = std::make_unique<renderer::EffectsStagePass>(); }
        catch (...) { effects_attach_failed_ = true; log("effects_stage_device device=%llu frame=%llu attached=0 reason=allocation retry=reset", id_, frame_); return false; }
    }
    if (effects_->caps().enabled) return true;
    D3DDISPLAYMODE display{};
    HRESULT hr = native<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &display);
    const bool queried = SUCCEEDED(hr);
    if (queried) taa_call([&] { hr = effects_->attach(device_, native_, caps_, display.Format); });
    const bool attached = SUCCEEDED(hr) && effects_->caps().enabled;
    const auto& c = effects_->caps();
    log("effects_stage_device device=%llu frame=%llu attached=%u reason=%s result=%08lx fp16_blending=%08lx slots=%u bolt=%u/%u shell=%u/%u decal=%u/%u retry=reset", id_, frame_, unsigned(attached),
        attached ? "ok" : queried ? c.reason : "adapter_query", hr, c.fp16_blending, c.largest_program_slots, c.bolt_vs_slots, c.bolt_ps_slots, c.shell_vs_slots, c.shell_ps_slots, c.decal_vs_slots, c.decal_ps_slots);
    if (!attached) effects_attach_failed_ = true;
    return attached;
}
void MotionOutput::release_effects_stage() noexcept {
    if (effects_) { taa_call([&] { effects_->detach(); }); effects_.reset(); }
    release_effects_atlas();
    effects_frame_armed_ = false;
}
void MotionOutput::release_effects_atlas() noexcept {
    if (effects_bolt_atlas_) { effects_bolt_atlas_->Release(); effects_bolt_atlas_ = nullptr; }
    effects_bolt_atlas_identity_ = 0;
}
// Frame start: the arming decision of this frame (section 2.1) and the record buffers. The stage needs the HDR
// redirect, the jittered TAA resolve (it draws inside its bracket), the lane and the attached pass; a frame within the
// 64-frame disarm window after a failure stays native.
void MotionOutput::effects_begin_frame() noexcept {
    effects_frame_.clear();
    effects_hit_count_ = 0; effects_raw_box_count_ = 0; effects_bolt_draw_hash_ = 0;
    effects_bolt_current_ ^= 1u; effects_bolt_count_[effects_bolt_current_] = 0;
    release_effects_atlas();
    effects_frame_armed_ = false;
    effects_arm_pending_ = effects_requested_ && effects_enabled_;
    effects_frame_reason_ = effects_arm_pending_ ? "pending" : effects_requested_ ? "toggled_off" : "off";
}
// The arming decision, taken once per frame at the first draw after the latching clear (the HDR redirect, the depth
// surface and the jitter are latched inside the frame, not at Present): the prerequisites of section 2.1, the
// attached pass with its buffers, FP16 blending, and not within the 64-frame window after a failed stage. Before the
// latch nothing is decidable and nothing is recorded.
bool MotionOutput::effects_armed_now() noexcept {
    if (!effects_arm_pending_) return effects_frame_armed_;
    if (!counters_.filled) return false;
    effects_arm_pending_ = false;
    const bool prerequisites = taa_enabled_ && !taa_failed_ && jitter_active_ && !main_msaa_ && hdr_state_ == HdrState::Active && hdr_ && hdr_->target() && depth_enabled_ && depth_surface_ && !composition_state_lost_ && !motion_state_lost_;
    if (!prerequisites) { effects_frame_reason_ = "prerequisites"; return false; }
    if (!attach_effects_stage()) { effects_frame_reason_ = "attach"; return false; }
    bool resources = effects_->resources_ready();
    if (!resources) taa_call([&] { resources = SUCCEEDED(effects_->ensure_resources()); }); // after a Reset: the buffers come back under the reference accounting
    effects_frame_armed_ = effects_arming_.armed(frame_, resources, effects_->caps().fp16_blending == D3D_OK, hdr_state_ == HdrState::Active);
    effects_frame_reason_ = effects_frame_armed_ ? "armed" : !resources ? "resources" : "disarmed";
    return effects_frame_armed_;
}
bool MotionOutput::effects_pair_bound() const noexcept {
    return shadow_.ps_hash == effects_ps && (shadow_.vs_hash == effects_vs_default || shadow_.vs_hash == effects_vs_instance);
}
// The scoped identity of the current draw (node serial through --object-trace), else 0.
std::uint64_t MotionOutput::effects_draw_identity() const noexcept {
    object_trace::Snapshot scope{};
    if (!object_trace::current(&scope, false)) return 0;
    constexpr std::uint32_t required = object_trace::Node | object_trace::Registry;
    if ((scope.valid & required) != required || !scope.node) return 0;
    object_lifetime::Snapshot lifetime{};
    if (!object_lifetime::current(scope.registry, scope.node, scope.node_handle, scope.camera, scope.camera_handle, &lifetime) || !lifetime.known) return 0;
    return lifetime.node_serial;
}
// The census row of an effect-pair draw on a capture frame (--effects-census): what the flight needs to pin the keys.
void MotionOutput::log_effect_draw_census(const ownership::TextureKeyView& key, const char* verdict, const char* cls, const float* rows, bool scoped, std::uint64_t identity, const MotionDrawCall& call) noexcept {
    float origin[3] = {0.f, 0.f, 0.f}, scale = 0.f;
    if (rows) effects_stage::world_rows_origin(rows, origin, &scale);
    log("effect_draw device=%llu frame=%llu index=%lu verdict=%s class=%s key=%016llx key_known=%u key_source=%u uploads=%u size=%lux%lu format=%lu vs=%016llx ps=%016llx blend=%lu/%lu/%lu scoped=%u serial=%llu phase=%u origin=%.3f,%.3f,%.3f scale=%.5g primitives=%u indexed=%u",
        id_, frame_, counters_.draws, verdict, cls, static_cast<unsigned long long>(key.key), unsigned(key.known), key.source, key.uploads, static_cast<unsigned long>(key.width), static_cast<unsigned long>(key.height),
        static_cast<unsigned long>(key.format), static_cast<unsigned long long>(shadow_.vs_hash), static_cast<unsigned long long>(shadow_.ps_hash), static_cast<unsigned long>(shadow_.composition_blend[0]),
        static_cast<unsigned long>(shadow_.composition_blend[1]), static_cast<unsigned long>(shadow_.composition_blend[2]), unsigned(scoped), static_cast<unsigned long long>(identity), unsigned(selector_.state()),
        double(origin[0]), double(origin[1]), double(origin[2]), double(scale), unsigned(call.primitives), unsigned(call.indexed));
}
// The recogniser of the DEFAULT/INSTANCE effect pair (section 2.1): one bool test per draw for every other pair.
// Returns true when the draw was taken (route.submit cleared, S_OK returned to the engine).
bool MotionOutput::record_effect_draw(const MotionDrawCall& call, MotionRoute& route) noexcept {
    if (!effects_pair_bound()) return false;
    auto& f = effects_frame_;
    ++f.recognised;
    const bool scene = selector_.state() == renderer::BoundaryState::Scene && counters_.filled && !counters_.hook_scene_end;
    const bool census = effects_census_ && capture_;
    if (!effects_shields_ || !effects_frame_armed_ || !scene) {
        ++f.forwarded;
        if (census) { ownership::TextureKeyView none{}; log_effect_draw_census(none, !effects_shields_ ? "forwarded_shields_off" : !effects_frame_armed_ ? "forwarded_disarmed" : "forwarded_phase", "unknown", nullptr, false, 0, call); }
        return false;
    }
    // The stage-0 texture's upload-time key (section 8.2); the read-only fallback once for a texture whose upload was
    // not observed (a DEFAULT-pool texture stays unknown for good).
    ownership::TextureKeyView key{};
    IDirect3DBaseTexture9* texture = samplers_[0].texture; // the application's stage-0 texture (the wrapper pointer the game bound)
    if (texture) {
        ownership::get_texture_key_view(texture, &key);
        if (key.requested && !key.known && key.uploads == 0) ownership::compute_texture_key_readonly(texture, &key);
    }
    const effects_stage::KeyEntry* entry = key.known ? effects_keys_.find(key.key) : nullptr;
    if (!entry) {
        ++f.unknown_keys; ++f.forwarded;
        if (census) log_effect_draw_census(key, key.known ? "forwarded_unlisted_key" : "forwarded_unknown_key", "unknown", nullptr, false, 0, call);
        return false;
    }
    if (entry->cls != effects_stage::Class::ShieldHit) { // phase 1 classes only; the table may name more
        ++f.forwarded;
        if (census) log_effect_draw_census(key, "forwarded_class", effects_stage::class_name(entry->cls), nullptr, false, 0, call);
        return false;
    }
    if (effects_hit_count_ >= effects_stage::max_hits) { ++f.overflow; ++f.forwarded; return false; }
    float rows[12];
    if (FAILED(native<GetConstantsFFn>(GetVertexShaderConstantF)(device_, 4, rows, 3))) { ++f.forwarded; return false; }
    effects_stage::Record r{};
    if (!effects_stage::world_rows_origin(rows, r.origin, &r.scale)) { ++f.forwarded; if (census) log_effect_draw_census(key, "forwarded_rows", "shield_hit", rows, false, 0, call); return false; }
    r.cls = entry->cls; r.key = key.key; r.primitives = std::uint32_t(call.primitives);
    std::memcpy(r.tint, entry->tint, sizeof r.tint);
    const std::uint64_t serial = effects_draw_identity();
    r.scoped = serial != 0;
    r.identity = serial ? serial : effects_stage::spatial_identity(key.key, r.origin, effects_spatial_cell);
    effects_hits_[effects_hit_count_++] = r;
    ++f.recorded; ++f.hits;
    if (census) log_effect_draw_census(key, "recorded", "shield_hit", rows, r.scoped, r.identity, call);
    (void)route; // phase 1: the native sprite draws as well (header comment); nothing is suppressed
    return true;
}
// The bullet draws (section 3.1): called by prepare_screen_additive after its admission checks; a recorded draw
// still takes the additive route (gain and alpha law) and is forwarded, only the footprint rewrite is skipped for it
// (the capsule is its footprint). The same shape and prefix rules as the footprint (one refusal leaves the draw to
// the footprint path). Both bullet draws of a frame (draw 9 and draw 90) are seen; a second draw whose vertices hash
// like the first adds no second record (unknown 4, logged by the frame row's bolt_draws / bolt_sets). Returns true
// when the draw was recorded (route untouched).
bool MotionOutput::record_bolt_draw(const MotionDrawCall& call, MotionRoute& route) noexcept {
    using namespace bolt_footprint;
    if (!effects_frame_armed_) return false;
    if (!screen_emission::admitted_vertex_shader(shadow_.vs_hash)) return false;
    const bool scene = selector_.state() == renderer::BoundaryState::Scene && counters_.filled && !counters_.hook_scene_end;
    if (!scene) return false;
    if (!effects_bolt_views_all_ && !chase_camera::pose_applied_since(chase_pose_mark_)) return false; // --effects-bolt-views chase: first person keeps the game's bolts
    if (call.topology != D3DPT_TRIANGLELIST || call.first != 0 || call.primitives > max_vertices / 3u || !shadow_.stream0 || !shadow_.stream0_identity
        || shadow_.stream0_stride != stride || shadow_.stream0_offset != 0 || !shadow_.declaration || shadow_.position_offset != 0 || shadow_.position_type != D3DDECLTYPE_FLOAT3) return false;
    const std::uint32_t count = call.primitives * 3u;
    const fade_region::Query query{shadow_.stream0, shadow_.indices, shadow_.stream0_identity, shadow_.indices_identity};
    const float* positions = nullptr; const std::uint32_t* extras = nullptr; std::uint64_t revision = 0; unsigned refusal = 0;
    if (!fade_region::locked_prefix_vertices(query, count, &positions, &extras, &revision, &refusal)) return false;
    UINT frequency = 0;
    if (FAILED(direct_call<GetStreamFreqFn>(GetStreamSourceFreq, 0, &frequency)) || frequency != 1) return false;
    const std::uint32_t period = detect_period(extras, count);
    if (!period) return false;
    // The set's identity: count and the first / last vertex words (a second draw of the same instances this frame).
    std::uint64_t hash = effects_stage::fnv_offset;
    hash = effects_stage::fnv_u32(hash, count);
    hash = effects_stage::fnv_bytes(hash, reinterpret_cast<const unsigned char*>(positions), 12);
    hash = effects_stage::fnv_bytes(hash, reinterpret_cast<const unsigned char*>(positions + std::size_t(count - 1) * 3u), 12);
    auto& f = effects_frame_;
    ++f.bolt_draws;
    (void)route;
    if (effects_bolt_draw_hash_ && hash == effects_bolt_draw_hash_) { ++f.recorded; return true; } // the same set again: one record
    const unsigned cur = effects_bolt_current_;
    unsigned refused = 0;
    const unsigned capacity = effects_stage::max_bolts - effects_bolt_count_[cur];
    if (!capacity) { ++f.overflow; return false; }
    const unsigned n = effects_stage::derive_instances(positions, extras, count, period, effects_bolts_[cur].get() + effects_bolt_count_[cur], capacity, &refused);
    if (!fade_region::recheck_locked_prefix(query, count, revision)) return false; // rewritten under the copy: the game draws it
    if (!n) return false;
    if (count / period > n + refused) { ++f.overflow; return false; } // more instances than the record holds: the game draws them all
    // The bullet atlas for the tint: the game's texture at stage 0, held natively for the frame.
    IDirect3DBaseTexture9* wrapper = samplers_[0].texture; // the application's stage-0 texture (the wrapper pointer the game bound)
    const std::uint64_t wrapper_identity = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(wrapper));
    if (!effects_bolt_atlas_ || effects_bolt_atlas_identity_ != wrapper_identity) {
        release_effects_atlas();
        // The application-visible pointer, held through its own AddRef: the stage binds it through the same SetTexture the
        // game uses, so the ownership layer unwraps it (a native pointer would be an unobserved route).
        if (wrapper) { wrapper->AddRef(); effects_bolt_atlas_ = wrapper; effects_bolt_atlas_identity_ = wrapper_identity; }
    }
    effects_bolt_count_[cur] += n; effects_bolt_draw_hash_ = hash; ++effects_bolt_sets_;
    ++f.recorded;
    return true;
}
// An owner box for the shell association (section 3.3): the caster-candidate route's object-space extent with the
// draw's clip rows and node serial; per node the largest box of the frame is kept (the hull, not a turret).
void MotionOutput::note_owner_box(const MotionRoute& route, const float* rows, const float* lo, const float* hi) noexcept {
    if (!effects_frame_armed_ || !effects_shields_ || !rows || !lo || !hi || !effects_raw_boxes_) return;
    const std::uint64_t serial = route.key.object_lifetime;
    if (!serial) return;
    const float volume = (hi[0] - lo[0]) * (hi[1] - lo[1]) * (hi[2] - lo[2]);
    if (!(volume > 0.f) || !effects_stage::finite_f(volume)) return;
    EffectsRawBox* slot = nullptr;
    for (unsigned i = 0; i < effects_raw_box_count_; ++i) if (effects_raw_boxes_[i].serial == serial) { slot = effects_raw_boxes_.get() + i; break; }
    if (slot) { if (volume <= slot->volume) return; }
    else if (effects_raw_box_count_ < effects_stage::max_boxes) slot = effects_raw_boxes_.get() + effects_raw_box_count_++;
    else return;
    std::memcpy(slot->rows, rows, sizeof slot->rows); std::memcpy(slot->lo, lo, sizeof slot->lo); std::memcpy(slot->hi, hi, sizeof slot->hi);
    slot->serial = serial; slot->volume = volume; slot->used = true;
}
// The temporal resolve's callback (FrameInputs::stage_callback): the stage runs inside the resolve's bracket.
HRESULT MotionOutput::effects_stage_callback(void* context, IDirect3DDevice9*) noexcept {
    return static_cast<MotionOutput*>(context)->run_effects_stage();
}
HRESULT MotionOutput::run_effects_stage() noexcept {
    auto& f = effects_frame_;
    effects_stage_ran_ = true;
    if (!effects_ || !effects_frame_armed_ || !effects_lane_) return S_FALSE;
    // The frame clock (seconds between stage frames, clamped) for the ripple ages.
    LARGE_INTEGER now{}, freq{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&freq);
    float dt = 1.f / 60.f;
    if (effects_last_qpc_ && freq.QuadPart > 0) dt = float(double(now.QuadPart - effects_last_qpc_) / double(freq.QuadPart));
    effects_last_qpc_ = now.QuadPart;
    if (!(dt >= effects_min_dt)) dt = effects_min_dt;
    if (dt > effects_max_dt) dt = effects_max_dt;
    effects_dt_ = dt;
    const unsigned cur = effects_bolt_current_, prev = cur ^ 1u;
    // Bolts: the velocity match against the previous frame (a cut resets), then the vertices.
    effects_stage::match_instances(effects_bolts_[prev].get(), effects_bolt_count_[prev], effects_bolts_[cur].get(), effects_bolt_count_[cur],
                                   effects_stage::default_match_s_max, effects_stage::default_match_eps, counters_.cut || effects_cut_pending_, &effects_match_);
    effects_cut_pending_ = false;
    const unsigned bolts = effects_stage::write_bolt_vertices(effects_bolts_[cur].get(), effects_bolt_count_[cur], effects_bolt_vertices_.get(), effects_stage::max_bolts);
    // Shells and decals: the owner boxes of this frame (object -> world from the clip rows and the camera), the
    // association, the age map and the four-slot policy.
    const std::uint64_t ripple_frames = std::uint64_t(effects_tuning_.ripple_seconds / dt) + 1u;
    bool live_ships = false;
    for (unsigned s = 0; s < effects_ship_hit_count_; ++s) { effects_ship_hits_[s].expire(frame_, ripple_frames); live_ships = live_ships || effects_ship_hits_[s].live(); }
    if (!bolts && !effects_hit_count_ && !live_ships) return S_FALSE; // nothing to draw: no box work, no device call
    double scratch[9]; const double* wv = renderer::camera_world_basis(camera_scene_, scratch);
    effects_stage::OwnerBox boxes[effects_stage::max_boxes]; unsigned box_count = 0;
    if (wv && camera_scene_.valid && camera_scene_.m00 > 0.f && camera_scene_.m11 > 0.f) {
        const float wvf[9] = {float(wv[0]), float(wv[1]), float(wv[2]), float(wv[3]), float(wv[4]), float(wv[5]), float(wv[6]), float(wv[7]), float(wv[8])};
        for (unsigned i = 0; i < effects_raw_box_count_; ++i) {
            const EffectsRawBox& raw = effects_raw_boxes_[i];
            effects_stage::OwnerBox& b = boxes[box_count];
            float object_rows[12];
            if (!effects_stage::object_to_world_from_clip(raw.rows, camera_scene_.m00, camera_scene_.m11, camera_scene_.m20, camera_scene_.m21, wvf, camera_scene_.t, object_rows)) continue;
            // The box centre in object space moves the rows' translation; the half-extents stay object-space.
            const float centre[3] = {.5f * (raw.lo[0] + raw.hi[0]), .5f * (raw.lo[1] + raw.hi[1]), .5f * (raw.lo[2] + raw.hi[2])};
            b = effects_stage::OwnerBox{};
            for (unsigned r = 0; r < 3; ++r) { std::memcpy(b.rows + r * 4, object_rows + r * 4, 16); b.rows[r * 4 + 3] += object_rows[r * 4] * centre[0] + object_rows[r * 4 + 1] * centre[1] + object_rows[r * 4 + 2] * centre[2]; }
            for (unsigned k = 0; k < 3; ++k) b.half[k] = .5f * (raw.hi[k] - raw.lo[k]);
            b.identity = raw.serial; b.rows_valid = true;
            ++box_count;
        }
    }
    unsigned decals = 0; effects_shell_count_ = 0;
    for (unsigned i = 0; i < effects_hit_count_; ++i) {
        const effects_stage::Record& r = effects_hits_[i];
        const std::uint64_t first = effects_ages_.touch(r.identity, r.key, frame_);
        const float age = first ? float(frame_ - first) * dt : 0.f;
        const effects_stage::Association a = effects_stage::associate_hit(r.origin, boxes, box_count);
        if (a.kind == effects_stage::HitKind::Shell || a.kind == effects_stage::HitKind::Sphere) {
            const effects_stage::OwnerBox& owner = boxes[unsigned(a.index)];
            effects_stage::ShipHits* ship = nullptr;
            for (unsigned s = 0; s < effects_ship_hit_count_; ++s) if (effects_ship_hits_[s].owner == owner.identity) { ship = effects_ship_hits_ + s; break; }
            if (!ship) {
                // A free or fully expired entry, else the entry with the oldest newest hit.
                for (unsigned s = 0; s < effects_ship_hit_count_; ++s) if (!effects_ship_hits_[s].live()) { ship = effects_ship_hits_ + s; break; }
                if (!ship && effects_ship_hit_count_ < effects_stage::max_shells) ship = effects_ship_hits_ + effects_ship_hit_count_++;
                if (!ship) { ship = effects_ship_hits_; for (unsigned s = 1; s < effects_ship_hit_count_; ++s) if (effects_ship_hits_[s].slots[0].first_frame < ship->slots[0].first_frame) ship = effects_ship_hits_ + s; }
                *ship = effects_stage::ShipHits{}; ship->owner = owner.identity;
            }
            ship->insert(r.identity, a.local, first ? first : frame_);
            std::memcpy(ship->tint, r.tint, sizeof ship->tint); // the last recorded hit's tint stays with the ship
            if (effects_census_ && capture_) log("shield_hit device=%llu frame=%llu owner=%llu distance=%.3f kind=%s identity=%llu age=%.3f", id_, frame_, static_cast<unsigned long long>(owner.identity), double(a.distance), a.kind == effects_stage::HitKind::Shell ? "shell" : "sphere", static_cast<unsigned long long>(r.identity), double(age));
        } else if (decals < effects_stage::max_hits) {
            const effects_stage::KeyEntry* entry = effects_keys_.find(r.key);
            float radius = effects_default_decal_radius;
            if (entry) { const float mean = (entry->extent[0] + entry->extent[1] + entry->extent[2]) * (1.f / 3.f); if (mean > 0.f) radius = mean * r.scale; }
            effects_stage::write_decal_vertices(r.origin, radius, age, r.alpha, effects_decals_ + decals * 4u);
            ++decals;
            if (effects_census_ && capture_) log("shield_hit device=%llu frame=%llu owner=0 distance=%.3f kind=decal identity=%llu age=%.3f", id_, frame_, double(a.distance), static_cast<unsigned long long>(r.identity), double(age));
        }
    }
    // The shell instances of every ship with a live slot.
    for (unsigned s = 0; s < effects_ship_hit_count_ && effects_shell_count_ < effects_stage::max_shells; ++s) {
        const effects_stage::ShipHits& ship = effects_ship_hits_[s];
        if (!ship.live()) continue;
        const effects_stage::OwnerBox* owner = nullptr;
        for (unsigned b = 0; b < box_count; ++b) if (boxes[b].identity == ship.owner) { owner = boxes + b; break; }
        if (!owner) continue; // the ship left the candidate route this frame: no shell this frame
        effects_stage::ShellInstance& inst = effects_shells_[effects_shell_count_];
        if (!effects_stage::shell_from_box(*owner, &inst)) continue;
        inst.hit_count = 0;
        for (unsigned k = 0; k < effects_stage::hit_slots; ++k) {
            const effects_stage::HitSlot& slot = ship.slots[k];
            if (!slot.live) continue;
            float* h = inst.hits[inst.hit_count++];
            h[0] = slot.local[0]; h[1] = slot.local[1]; h[2] = slot.local[2]; h[3] = float(frame_ - slot.first_frame) * dt;
        }
        std::memcpy(inst.tint, ship.tint, sizeof inst.tint); // the ship's last recorded hit's tint, also on frames without a new record
        ++effects_shell_count_;
    }
    if (!bolts && !effects_shell_count_ && !decals) return S_FALSE;
    renderer::EffectsFrame in{};
    in.width = effects_lane_width_; in.height = effects_lane_height_;
    for (unsigned j = 0; j < 3; ++j) { in.view_rows[j * 4] = camera_scene_.r[j]; in.view_rows[j * 4 + 1] = camera_scene_.r[3 + j]; in.view_rows[j * 4 + 2] = camera_scene_.r[6 + j]; in.view_rows[j * 4 + 3] = camera_scene_.t[j]; }
    in.m00 = camera_scene_.m00; in.m11 = camera_scene_.m11;
    in.m20 = camera_scene_.m20 + (in.width ? 2.f * jitter_[0] / float(in.width) : 0.f);
    in.m21 = camera_scene_.m21 + (in.height ? -2.f * jitter_[1] / float(in.height) : 0.f);
    in.m22 = camera_scene_.m22 != 0.f ? camera_scene_.m22 : projection_default_m22; in.m32 = camera_scene_.m32 != 0.f ? camera_scene_.m32 : projection_default_m32;
    in.near_z = 1.f;
    in.lane_four_channel = lane_depth_format() == D3DFMT_A32B32G32R32F; in.lane = effects_lane_;
    in.bolt_vertices = effects_bolt_vertices_.get(); in.bolt_instances = bolts; in.bolt_atlas = effects_bolt_atlas_;
    in.shells = effects_shells_; in.shell_count = effects_shell_count_;
    in.decal_vertices = effects_decals_; in.decal_count = decals;
    in.tuning = &effects_tuning_;
    renderer::EffectsReport report{};
    const HRESULT hr = effects_->run(in, &report);
    effects_report_ = report;
    f.bolts = report.bolts; f.shells = report.shells; f.decals = report.decals;
    if (FAILED(hr)) {
        effects_arming_.fail(frame_);
        log("effects_stage_failed device=%llu frame=%llu result=%08lx step=%u calls=%u until=%llu", id_, frame_, hr, unsigned(report.failed), report.calls, static_cast<unsigned long long>(effects_arming_.disarmed_until));
    }
    return hr;
}
// Present: the frame row (every frame under --telemetry, else every 300 frames as a window sum) and the atlas release.
void MotionOutput::log_effects_stage_frame() noexcept {
    const auto& f = effects_frame_;
    const bool window_due = ++effects_window_frames_ >= effects_window_frames;
    effects_window_.recognised += f.recognised; effects_window_.recorded += f.recorded; effects_window_.forwarded += f.forwarded; effects_window_.unknown_keys += f.unknown_keys;
    effects_window_.overflow += f.overflow; effects_window_.bolts += f.bolts; effects_window_.shells += f.shells; effects_window_.decals += f.decals; effects_window_.bolt_draws += f.bolt_draws; effects_window_.hits += f.hits;
    if (telemetry_ || window_due) {
        const auto& w = telemetry_ ? f : effects_window_;
        log("effects_stage_frame device=%llu frame=%llu scope=%s armed=%u reason=%s ran=%u recognised=%u recorded=%u forwarded=%u unknown_keys=%u overflow=%u bolt_draws=%u bolt_sets=%u bolts=%u hits=%u shells=%u decals=%u matched=%u refused_ambiguous=%u median_speed=%.2f result=%08lx step=%u calls=%u stage_us=%.1f depth_bound=%u enabled=%u mode=additive",
            id_, frame_, telemetry_ ? "frame" : "window", unsigned(effects_frame_armed_), effects_frame_reason_, unsigned(effects_stage_ran_), w.recognised, w.recorded, w.forwarded, w.unknown_keys, w.overflow, w.bolt_draws, effects_bolt_sets_, w.bolts, w.hits, w.shells, w.decals,
            effects_match_.matched, effects_match_.refused_ambiguous, double(effects_match_.median_speed), effects_report_.operation, unsigned(effects_report_.failed), effects_report_.calls, double(effects_stage_us_), unsigned(effects_depth_bound_), unsigned(effects_enabled_));
        if (window_due) { effects_window_.clear(); effects_window_frames_ = 0; effects_bolt_sets_ = 0; }
    }
    effects_stage_ran_ = false; effects_report_ = {};
    release_effects_atlas();
}
