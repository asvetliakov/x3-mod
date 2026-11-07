// Weapon effects, step 1: the shield hit (included by motion_output.cpp inside namespace x3m; AP only;
// weapon_effects_patch.h, weapon_effects_stage.h, renderer/weapon_effects_pass.h; docs/architecture/weapon-effects.md
// 2.6, 5.1 and 5.1.1). X3M_WEAPON_EFFECTS=shields: the impact hook records every hit into the proxy ring and marks a
// shield hit stage-owned (flag_stage) while the stage owned the previous frame and skips the engine's own impact for it
// (core::suppress_native_shield_hit, coordinator decision 2026-11-06: the patch alone). This
// stage draws the hex patches and flat decals of the ring's
// stage-owned records as an act of the temporal resolve's stage bracket, after the plumes and the own sun when they
// draw (one callback slot: the stage chains the callback it found, own_sun_with_plumes_'s pattern), reading the lane the
// resolve reads (occlusion by the lane alone; the depth surface is unbound before the callback).
// Step 2 (X3M_WEAPON_EFFECTS=shields,hull; docs/architecture/weapon-look.md 3.1, weapon_effects_hull.h): a stage-owned
// hull hit likewise skips the engine's impact (core::suppress_native_hull_hit) and the stage draws its flash and sparks
// (the sprite batch) and its glow and ember (the glow primitive) after the patches' budget share, with the previous
// frame's view for the streaks and the frame's game step (cfg+0x714) for the three-frame floor.
//
// Ownership (note 2.6): armed = the route on, the HDR/TAA FP16 path, the scene camera latched, the completed lane at
// the target's size and format, the attached pass, not within 64 frames of a failed stage frame (the third consecutive
// failure refuses until Reset). Present latches weapon_effects_owned() for the next frame's hooks: armed, the resolve
// completed and, when the callback was set, it ran and returned S_OK / S_FALSE (core::stage_owned, review F8), so a
// frame whose resolve never reaches the arming (a menu, TAA off), fails before the callback, or whose stage returns
// DEVICELOST or an error makes the next frame's hits native only; stage-owned records
// stay in the ring with their own game-time timelines and are drawn when the stage returns, if still
// within age. A cut verdict or a Reset clears the stage-owned records. One weapon_effects_state row per change of the
// armed state or its reason; one weapon_effects_device row per attach; weapon_effects_failed per failed stage frame.
void MotionOutput::configure_weapon_effects(bool requested) noexcept {
    weapon_requested_ = requested;
    weapon_hull_requested_ = requested && weapon_effects::hook_state().mode == weapon_effects::core::mode_shields_hull;
    weapon_shimmer_count_ = 0;
    weapon_armed_ = false;
    weapon_callback_set_ = weapon_callback_ok_ = weapon_owned_ = false;
    weapon_failures_ = 0;
    weapon_failed_out_ = false;
    weapon_reason_ = requested ? "pending" : "off";
    weapon_state_logged_ = false;
}
void MotionOutput::note_weapon_state(bool armed, const char* reason) noexcept {
    weapon_armed_ = armed;
    weapon_reason_ = reason;
    if (weapon_state_logged_ && armed == weapon_logged_armed_ && !std::strcmp(reason, weapon_logged_reason_)) return;
    weapon_state_logged_ = true;
    weapon_logged_armed_ = armed;
    weapon_logged_reason_ = reason;
    log("weapon_effects_state device=%llu frame=%llu armed=%u reason=%s shields=%s", id_, frame_, unsigned(armed), reason,
        armed ? "stage" : "engine");
}
bool MotionOutput::attach_weapon_effects() noexcept {
    if (weapon_attach_failed_) return false;
    if (!weapon_) {
        weapon_.reset(new (std::nothrow) renderer::WeaponEffectsPass);
        if (!weapon_) {
            weapon_attach_failed_ = true;
            log("weapon_effects_device device=%llu frame=%llu attached=0 reason=allocation retry=reset", id_, frame_);
            return false;
        }
    }
    if (weapon_->caps().enabled) return true;
    D3DDISPLAYMODE display{};
    HRESULT hr = native<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &display);
    const bool queried = SUCCEEDED(hr);
    // The hull programs only in shields,hull mode (their refusal keeps the shield effect: caps().hull_enabled false).
    const bool hull = weapon_effects::hook_state().mode == weapon_effects::core::mode_shields_hull;
    if (queried) {
        if (taa_busy_)
            hr = weapon_->attach(device_, native_, caps_, display.Format, hull);
        else
            taa_call([&] { hr = weapon_->attach(device_, native_, caps_, display.Format, hull); });
    }
    const bool attached = queried && SUCCEEDED(hr) && weapon_->caps().enabled;
    const auto& c = weapon_->caps();
    log("weapon_effects_device device=%llu frame=%llu attached=%u reason=%s result=%08lx fp16_blending=%08lx shell_vs_slots=%u shell_ps_slots=%u decal_vs_slots=%u decal_ps_slots=%u sprite_vs_slots=%u sprite_ps_slots=%u glow_vs_slots=%u glow_ps_slots=%u fireball_vs_slots=%u fireball_ps_slots=%u filament_ps_slots=%u lightning_vs_slots=%u lightning_vtf=%u,%u hull=%u hull_reason=%s max_ps_slots=%lu retry=reset",
        id_, frame_, unsigned(attached), attached ? "ok" : queried ? c.reason : "adapter_query", hr, c.fp16_blending,
        c.shell_vs_slots, c.shell_ps_slots, c.decal_vs_slots, c.decal_ps_slots, c.sprite_vs_slots, c.sprite_ps_slots,
        c.glow_vs_slots, c.glow_ps_slots, c.fireball_vs_slots, c.fireball_ps_slots, c.filament_ps_slots, c.lightning_vs_slots,
        unsigned(c.lightning_vtf_four), unsigned(c.lightning_vtf_r32f), unsigned(c.hull_enabled), c.hull_reason,
        static_cast<unsigned long>(caps_.MaxPixelShader30InstructionSlots));
    if (!attached) weapon_attach_failed_ = true;
    return attached;
}
void MotionOutput::release_weapon_effects() noexcept {
    if (weapon_) {
        taa_call([&] { weapon_->detach(); });
        weapon_.reset();
    }
    weapon_lane_ = nullptr;
    weapon_armed_ = false;
    weapon_owned_ = false;
}
bool MotionOutput::weapon_effects_arm(bool fp16_route, IDirect3DTexture9* lane, UINT width, UINT height) noexcept {
    weapon_armed_ = false;
    weapon_lane_ = nullptr;
    if (!enabled_) {
        note_weapon_state(false, "route");
        return false;
    }
    if (!taa_enabled_ || taa_failed_ || main_msaa_ || hdr_state_ != HdrState::Active || !hdr_ || !fp16_route) {
        note_weapon_state(false, "hdr_taa_path");
        return false;
    }
    if (!camera_scene_.valid || !(camera_scene_.m00 > 0.f) || !(camera_scene_.m11 > 0.f)) {
        note_weapon_state(false, "camera");
        return false;
    }
    D3DSURFACE_DESC lane_desc{};
    if (!lane || !width || !height || FAILED(lane->GetLevelDesc(0, &lane_desc)) || lane_desc.Width != width ||
        lane_desc.Height != height || lane_desc.Format != lane_depth_format()) {
        note_weapon_state(false, "lane");
        return false;
    }
    if (!attach_weapon_effects()) {
        note_weapon_state(false, weapon_ && weapon_->caps().reason ? weapon_->caps().reason : "attach");
        return false;
    }
    if (weapon_failed_out_) {
        note_weapon_state(false, "failed_until_reset");
        return false;
    }
    if (frame_ < weapon_disarmed_until_) {
        note_weapon_state(false, "disarmed");
        return false;
    }
    weapon_lane_ = lane;
    weapon_width_ = width;
    weapon_height_ = height;
    note_weapon_state(true, "armed");
    return true;
}
// The stage callback while the weapon stage draws this frame: the callback it replaced first (the plumes, or the own
// sun with the plumes), then the patches. A lost device ends it; otherwise the first failure is the result.
HRESULT MotionOutput::weapon_effects_stage_callback(void* context, IDirect3DDevice9* device) noexcept {
    MotionOutput& self = *static_cast<MotionOutput*>(context);
    HRESULT prior = S_FALSE;
    if (self.weapon_prior_callback_) {
        prior = self.weapon_prior_callback_(self.weapon_prior_context_, device);
        if (prior == D3DERR_DEVICELOST || prior == D3DERR_DEVICENOTRESET) return prior;
    }
    const HRESULT weapons = self.run_weapon_effects();
    if (weapons == D3DERR_DEVICELOST || weapons == D3DERR_DEVICENOTRESET) return weapons;
    if (FAILED(prior)) return prior;
    if (FAILED(weapons)) return weapons;
    return prior == S_OK || weapons == S_OK ? S_OK : S_FALSE;
}
namespace {
bool weapon_effects_read(std::uintptr_t address, void* out, std::size_t size) {
    return engine_memory::read(address, out, size);
}
// The cockpit view's freshness (review F2): the chase camera's visit count and the frame it last moved (one game).
weapon_effects::stage::CockpitClock weapon_cockpit_clock;
// The ship bubbles' hull walks (5.1.1 "Placement"; one game, the game thread only): 64 owners' bounds, at most
// core::bubble_walks_per_frame walks per stage call (the rest on the fallback sphere until their turn), cleared when the
// ring's scope changes (weapon_effects::scope_changes: a load, a sector change, a load epoch). CPU data only: a device
// Reset leaves it valid (it names engine objects, never a device resource).
weapon_effects::stage::BubbleCache weapon_bubble_cache;
// The hull hits' per-record state (step 2, weapon_effects_hull.h: first stage frame, world contact point, the owner's
// velocity) and the owners' last positions; one game, the game thread only; CPU data (a Reset leaves it valid; the
// ring's sequence recognises a reused slot). The frame's sprite quads and glow rectangles (about 330 KB and 80 KB at the
// caps: static, off the game thread's stack; run() consumes them before returning).
weapon_effects::stage::HullState weapon_hull_state;
renderer::WeaponSpriteVertex weapon_sprites[renderer::weapon_sprite_quads * 4];
renderer::WeaponGlowVertex weapon_glows[renderer::weapon_glow_boxes * 4];
// The puffs (fireball impostors, about 61 KB), the surface rings and filament glows (24 KB and 12 KB) and the scorches
// (20 KB), static as the sprites.
renderer::WeaponFireballVertex weapon_fireballs[renderer::weapon_fireball_boxes * 4];
renderer::WeaponShapeVertex weapon_rings[renderer::weapon_ring_quads * 4];
renderer::WeaponLightningVertex weapon_filaments[renderer::weapon_filament_boxes * 4];
renderer::WeaponGlowVertex weapon_scorches[renderer::weapon_scorch_boxes * 4];
// The frame's game step (cfg+0x714, game ms; 0 when unreadable), clamped to 5 s (ship_avoidance's reader's bound).
std::uint32_t weapon_game_step_ms() {
    std::uint32_t cfg = 0, step = 0;
    if (!engine_memory::read(weapon_effects::hook_state().globals.cfg_slot, &cfg, 4) || !cfg ||
        !engine_memory::read(cfg + 0x714, &step, 4))
        return 0;
    return step < 5000u ? step : 5000u;
}
}
HRESULT MotionOutput::run_weapon_effects() noexcept {
    namespace wcore = weapon_effects::core;
    LARGE_INTEGER begin{}, end{};
    QueryPerformanceCounter(&begin);
    weapon_effects::stage::Camera camera;
    for (unsigned i = 0; i < 9; ++i) camera.r[i] = camera_scene_.r[i];
    for (unsigned i = 0; i < 3; ++i) camera.t[i] = camera_scene_.t[i];
    weapon_shimmer_count_ = 0; // this frame's hull shimmer rects, built below in shields,hull mode
    renderer::WeaponEffectsFrame f;
    f.width = weapon_width_;
    f.height = weapon_height_;
    f.lane = weapon_lane_;
    f.four_channel = lane_depth_format() == D3DFMT_A32B32G32R32F;
    f.m00 = camera_scene_.m00;
    f.m11 = camera_scene_.m11;
    f.m20 = camera_scene_.m20 + (f.width ? 2.f * jitter_[0] / float(f.width) : 0.f);
    f.m21 = camera_scene_.m21 + (f.height ? -2.f * jitter_[1] / float(f.height) : 0.f);
    f.m22 = camera_scene_.m22 != 0.f ? camera_scene_.m22 : projection_default_m22;
    f.m32 = camera_scene_.m32 != 0.f ? camera_scene_.m32 : projection_default_m32;
    f.near_z = -f.m32 / f.m22;
    if (!(f.near_z > 0.f) || !(f.near_z < 1e30f)) f.near_z = -projection_default_m32 / projection_default_m22;
    camera.m00 = f.m00; // the jittered projection: the screen budget's box areas
    camera.m11 = f.m11;
    camera.m20 = f.m20;
    camera.m21 = f.m21;
    weapon_effects::stage::Tuning tuning{};
    tuning.near_z = f.near_z;
    // The frame's draws (64 x sizeof(WeaponPatch), about 124 KB: static, off the game thread's stack; the game thread is
    // the only caller and run() consumes them before returning).
    static renderer::WeaponPatch patches[wcore::max_patches];
    static renderer::WeaponDecal decals[wcore::max_decals];
    weapon_bubble_cache.rescope(weapon_effects::scope_changes());
    {
        // the hull stage's spots and scorch marks leave with the ring's records on a scope change
        static std::uint32_t hull_scope = 0;
        const std::uint32_t changes = weapon_effects::scope_changes();
        if (changes != hull_scope) {
            hull_scope = changes;
            weapon_effects::stage::hull_rescope(weapon_hull_state);
        }
    }
    // The own ship in the cockpit view (5.1.1, Run236 Flight C): its patches at core::own_cockpit_gain, and (Run240
    // Flight G) its bubble the eye sphere, so both are known before the build. The own ship is resolve_own_ship's root
    // node (cached per frame); the view is the chase camera's last active-control cockpit visit this frame or the
    // previous one (cockpit+0x150, 1 = the cockpit; stage::CockpitClock stamps the frame its visit count last moved: a
    // camera no longer visiting is stale, no dim), read only when a stage-owned record belongs to the own ship (in
    // shields,hull mode whenever the own ship is known: the hull hits' elements below take the same view). Without the
    // chase camera installed the view is unknown: nothing is dimmed and the own ship keeps its hull bubble.
    weapon_cockpit_ = false;
    const bool hull_mode = weapon_effects::hook_state().mode == wcore::mode_shields_hull && weapon_->caps().hull_enabled;
    std::uint32_t own = 0;
    {
        const wcore::Ring& ring = weapon_effects::hook_state().ring;
        bool staged = false;
        for (unsigned i = 0; i < wcore::ring_capacity && !staged; ++i)
            staged = ring.slots[i].sequence && (ring.slots[i].flags & wcore::flag_stage);
        if (staged || hull_mode) {
            resolve_own_ship();
            own = std::uint32_t(own_ship_node_);
            bool any_own = hull_mode && own;
            for (unsigned i = 0; i < wcore::ring_capacity && own && !any_own; ++i)
                any_own = ring.slots[i].sequence && (ring.slots[i].flags & wcore::flag_stage) && ring.slots[i].node == own;
            if (any_own && chase_camera::installed()) {
                const chase_camera::Stats cs = chase_camera::stats();
                weapon_cockpit_ = weapon_cockpit_clock.cockpit(cs.frames, cs.view_mode, frame_);
            }
        }
    }
    tuning.own_node = own;
    tuning.cockpit = weapon_cockpit_;
    // The owners this frame draws and those in view (both builds add theirs): after the builds every live record is
    // stamped (core::ring_stamp_hidden), so a full ring evicts an undrawn owner's records first and the next frame's
    // builds pick on-screen owners first. The far plane from the projection (m22 = f / (f - n), m32 = -n f / (f - n):
    // f = m32 / (1 - m22)); none when m22 <= 1; reset keeps it VisibleOwners::far_margin (5 %) out, since m22 near 1
    // leaves the float recovery about 2 % off. Its located table reads each owner past a cap once a frame for both
    // stages. Static (about 82 KB: the located table 2048 x 36 B; off the game thread's stack; reset every frame, the
    // table by its generation).
    static wcore::VisibleOwners visible;
    {
        const float far_z = f.m22 > 1.f ? f.m32 / (1.f - f.m22) : 0.f;
        visible.reset(far_z > f.near_z && far_z < 1e30f ? far_z : 0.f);
    }
    const weapon_effects::stage::Counts n =
        weapon_effects::stage::build(weapon_effects::hook_state().ring, weapon_effects::game_ms(), &weapon_effects_read,
                                     camera, tuning, patches, wcore::max_patches, decals, wcore::max_decals,
                                     &weapon_bubble_cache, &visible);
    weapon_effects::stage::dim_own_cockpit(patches, n.patches, own, weapon_cockpit_);
    f.patches = patches;
    f.patch_count = n.patches;
    f.decals = decals;
    f.decal_count = n.decals;
    // The hull hits (weapon_effects_hull.h): the previous frame's view when the history holds the previous frame (its
    // streaks start there), the frame's game step for the three-frame floor (Present keeps a floored record that long:
    // HookState::life_floor_ms), the screens of the shared budget the patches left.
    weapon_effects::stage::HullCounts hn{};
    if (hull_mode) {
        weapon_effects::stage::HullView hv;
        hv.cam = camera;
        hv.prev_valid = camera_previous_.valid && camera_previous_frame_ + 1 == frame_;
        if (hv.prev_valid) {
            for (unsigned i = 0; i < 9; ++i) hv.prev.r[i] = camera_previous_.r[i];
            for (unsigned i = 0; i < 3; ++i) hv.prev.t[i] = camera_previous_.t[i];
        }
        hv.width = float(f.width);
        hv.height = float(f.height);
        hv.near_z = f.near_z;
        hv.now = weapon_effects::game_ms();
        hv.step_ms = weapon_game_step_ms();
        hv.own_node = own;
        hv.cockpit = weapon_cockpit_;
        hv.budget_left = tuning.screen_budget - n.screens;
        weapon_effects::stage::HullBuffers hb;
        hb.sprites = weapon_sprites;
        hb.sprite_cap = renderer::weapon_sprite_quads;
        hb.glows = weapon_glows;
        hb.glow_cap = renderer::weapon_glow_boxes;
        hb.fireballs = weapon_fireballs;
        hb.fireball_cap = renderer::weapon_fireball_boxes;
        hb.rings = weapon_rings;
        hb.ring_cap = renderer::weapon_ring_quads;
        hb.filaments = weapon_filaments;
        hb.filament_cap = renderer::weapon_filament_boxes;
        // The scorches need the multiply blend (the pass's attach caps); without it none are built.
        hb.scorches = weapon_->caps().scorch_enabled ? weapon_scorches : nullptr;
        hb.scorch_cap = renderer::weapon_scorch_boxes;
        // The shimmer rects go to the engine shimmer pass after the resolve (motion_output_engine_shimmer_inc.h), this
        // frame only.
        hb.shimmer = weapon_shimmer_rects_;
        hb.shimmer_cap = shimmer_requested_ ? weapon_effects::core::hull::shimmer_max : 0u;
        hn = weapon_effects::stage::build_hull(weapon_effects::hook_state().ring, weapon_hull_state, &weapon_effects_read,
                                               hv, weapon_effects::stage::HullTuning{}, hb, &visible);
        const unsigned quads = hb.sprite_quads, boxes = hb.glow_boxes, fireballs = hb.fireball_boxes, rings = hb.ring_quads;
        weapon_shimmer_count_ = hb.shimmer_count;
        weapon_shimmer_frame_ = frame_;
        // Present's keep past a record's lifetime: the largest floor a live record latched, or the floor a record
        // latched now would take, plus a frame.
        const std::uint32_t latched = std::uint32_t(hn.max_floor_ms), now_floor = wcore::look::min_frames * hv.step_ms;
        weapon_effects::hook_state().life_floor_ms = (latched > now_floor ? latched : now_floor) + hv.step_ms;
        f.sprites = weapon_sprites;
        f.sprite_quads = quads;
        f.glows = weapon_glows;
        f.glow_boxes = boxes;
        f.fireballs = weapon_fireballs;
        f.fireball_boxes = fireballs;
        f.fireball_octaves = hn.octaves;
        f.rings = weapon_rings;
        f.ring_quads = rings;
        f.filaments = weapon_filaments;
        f.filament_boxes = hb.filament_boxes;
        f.scorches = weapon_scorches;
        f.scorch_boxes = hb.scorch_boxes;
        // F8 (a capture frame): the frame's hull-hit figures, the step and the views the streaks used.
        if (capture_)
            log("weapon_hull_frame device=%llu frame=%llu now=%lu step_ms=%lu prev_view=%u own=%08lx cockpit=%u hits=%u "
                "flashes=%u sparks=%u glows=%u embers=%u shed=%u orphans=%u retired=%u floored=%u capped=%u "
                "owners_dropped=%u quads=%u rects=%u glow_screens=%.3f patch_screens=%.3f bursts=%u rings=%u "
                "bursts_shed=%u octaves=%u burst_screens=%.3f streaks=%u chips=%u filaments=%u scorches=%u marks=%u "
                "spots=%u merges=%u spot_flashes=%u shimmer=%u shimmer_shed=%u scorches_shed=%u rings_shed=%u "
                "filaments_shed=%u jet_no_dir=%u energy=%u ballistic=%u plasma=%u heavy=%u ion=%u beam=%u missile=%u "
                "asteroid=%u repair=%u tug=%u done=%u flashes_shed=%u pulses=%u out_of_reach=%u owners_out_of_reach=%u "
                "owners_located=%u ion_segments=%u",
                id_, frame_, static_cast<unsigned long>(hv.now), static_cast<unsigned long>(hv.step_ms),
                unsigned(hv.prev_valid), static_cast<unsigned long>(own), unsigned(hv.cockpit), hn.hits, hn.flashes,
                hn.sparks, hn.glows, hn.embers, hn.shed, hn.orphans, hn.retired, hn.floored, hn.capped, hn.owners_dropped,
                quads, boxes, double(hn.screens), double(n.screens), hn.bursts, hn.rings, hn.bursts_shed, hn.octaves,
                double(hn.burst_screens), hn.streaks, hn.chips, hn.filaments, hn.scorches, hn.marks, hn.spots, hn.merges,
                hn.spot_flashes, hn.shimmer, hn.shimmer_shed, hn.scorches_shed, hn.rings_shed, hn.filaments_shed,
                hn.dir_fallback, hn.anatomy[0], hn.anatomy[1], hn.anatomy[2], hn.anatomy[3], hn.anatomy[4], hn.anatomy[5],
                hn.anatomy[6], hn.anatomy[7], hn.anatomy[8], hn.anatomy[9], hn.done, hn.flashes_shed, hn.pulses,
                hn.out_of_reach, hn.owners_out_of_reach, visible.located_reads, hn.ion_segments);
    }
    wcore::ring_stamp_hidden(weapon_effects::hook_state().ring, visible);
    weapon_retired_ += hn.retired;
    weapon_drops_[13] += hn.hits;
    weapon_drops_[14] += hn.flashes;
    weapon_drops_[15] += hn.sparks;
    weapon_drops_[16] += hn.glows;
    weapon_drops_[17] += hn.embers;
    weapon_drops_[18] += hn.shed;
    weapon_drops_[19] += hn.orphans;
    weapon_drops_[20] += hn.floored;
    weapon_drops_[21] += hn.owners_dropped;
    weapon_drops_[22] += hn.capped;
    weapon_drops_[23] += hn.bursts;
    weapon_drops_[24] += hn.rings;
    weapon_drops_[25] += hn.bursts_shed;
    weapon_drops_[26] += hn.bursts && hn.octaves < wcore::look::fireball_octaves;
    weapon_drops_[28] += hn.streaks;
    weapon_drops_[29] += hn.chips;
    weapon_drops_[30] += hn.filaments;
    weapon_drops_[31] += hn.scorches;
    weapon_drops_[32] += hn.spots;
    weapon_drops_[33] += hn.merges;
    weapon_drops_[34] += hn.shimmer;
    weapon_drops_[35] += hn.dir_fallback;
    weapon_retired_ += n.retired;
    weapon_drops_[0] += n.dropped;
    weapon_drops_[1] += n.cells_dropped;
    weapon_drops_[2] += n.hits_dropped;
    weapon_drops_[3] += n.spills;
    weapon_drops_[4] += n.faces_culled;
    weapon_drops_[5] += n.area_dropped;
    weapon_drops_[6] += n.bubbles;
    weapon_drops_[7] += n.bubble_fallback;
    weapon_drops_[8] += n.bubble.walks;
    weapon_drops_[9] += n.bubble.failures;
    weapon_drops_[10] += n.bubble.held;
    weapon_drops_[11] += n.bubble.deferred;
    weapon_drops_[12] += n.bubble.clamped;
    if (capture_) log_weapon_patches(patches, n.patches);
    renderer::WeaponEffectsReport report{};
    const HRESULT hr = weapon_->run(f, &report);
    QueryPerformanceCounter(&end);
    weapon_callback_ok_ = SUCCEEDED(hr); // S_OK drew, S_FALSE nothing to draw; DEVICELOST and errors leave it false
    // 32-bit operands only (an int64 -> double conversion is x87 fild on i686): a stage call never spans 2^31 ticks, and
    // a QPC frequency above 2^31 Hz reads as unavailable.
    const std::uint64_t frequency = engine_qpc_frequency();
    const std::uint64_t ticks = std::uint64_t(end.QuadPart - begin.QuadPart);
    weapon_stage_us_ = frequency && frequency < 0x80000000ull && ticks < 0x80000000ull
                           ? float(double(std::int32_t(ticks)) * 1e6 / double(std::int32_t(frequency)))
                           : 0.f;
    weapon_patches_ += report.patches;
    weapon_decals_ += report.decals;
    if (report.drew) {
        weapon_drew_ = true;
        weapon_failures_ = 0;
    }
    if (FAILED(hr) && hr != D3DERR_DEVICELOST && hr != D3DERR_DEVICENOTRESET) {
        // A failed stage frame: this frame's patches are lost (their records stay and draw again when the stage
        // returns, within age); the stage disarms for 64 frames, so the hooks forward from the next frame on.
        const bool final = ++weapon_failures_ >= weapon_failure_limit;
        if (final)
            weapon_failed_out_ = true;
        else
            weapon_disarmed_until_ = frame_ + wcore::disarm_frames;
        weapon_armed_ = false; // Present must not latch ownership for this frame
        log("weapon_effects_failed device=%llu frame=%llu result=%08lx step=%u calls=%u consecutive=%u until=%llu final=%u retry=%s",
            id_, frame_, hr, unsigned(report.failed), report.calls, weapon_failures_,
            static_cast<unsigned long long>(final ? 0 : weapon_disarmed_until_), unsigned(final), final ? "reset" : "frames");
    }
    return hr;
}
// One weapon_patch row per staged patch draw on a capture frame (docs/architecture/weapon-effects.md 5.1.1, Run235
// Flight B diagnostics): its owner, whether that is the own ship (own: resolve_own_ship's root node, the ship the player
// flies), whether the view is the cockpit and the gain the own ship's patch took for it (Flight C), the lattice (0 z,
// 2 x, 4 y; odd: the - faces), its cells (lit and a ring's not yet lit) and ring hits, the anchors' range along the axis and the
// eye's distance from the owner's centre, both over the owner's radius (eye_r < 1: the eye inside the bounding sphere),
// the extent (the footprint test's radius), and for a ship its bubble (bubble=1: centre and half-extents in the owner
// frame; 0: the station's hull decal), metres = view units / 5.
void MotionOutput::log_weapon_patches(const renderer::WeaponPatch* patches, unsigned count) noexcept {
    namespace wcore = weapon_effects::core;
    resolve_own_ship();
    const float metre = wcore::engine_units_per_metre * wcore::world_scale;
    for (unsigned i = 0; i < count; ++i) {
        const renderer::WeaponPatch& p = patches[i];
        const float lo = p.box[2] + p.extent, hi = p.box[5] - p.extent; // the anchors' range along the face's axis
        unsigned lit = 0;
        for (unsigned k = 0; k < p.cells; ++k) lit += p.cell_since[k] >= 0.f;
        const float eye = scalar::sqrt(p.centre[0] * p.centre[0] + p.centre[1] * p.centre[1] + p.centre[2] * p.centre[2]);
        const float r = p.owner_radius > 0.f ? p.owner_radius : 1.f;
        const bool own = own_ship_node_ && p.owner_node == own_ship_node_;
        log("weapon_patch device=%llu frame=%llu index=%u owner=%08lx node=%08lx own=%u cockpit=%u gain=%.2f region=%u "
            "cells=%u lit=%u hits=%u radius_m=%.1f depth_min_r=%.3f depth_max_r=%.3f eye_r=%.3f eye_m=%.1f "
            "view_z_m=%.1f cell_m=%.2f extent_m=%.2f bubble=%u bubble_c_m=%.1f,%.1f,%.1f bubble_e_m=%.1f,%.1f,%.1f "
            "eye_sphere=%u eye_r_m=%.1f",
            id_, frame_, i, static_cast<unsigned long>(p.owner), static_cast<unsigned long>(p.owner_node), unsigned(own),
            unsigned(weapon_cockpit_), double(own && weapon_cockpit_ ? wcore::own_cockpit_gain : 1.f),
            unsigned(int(p.seed)), p.cells, lit, p.hits,
            double(p.owner_radius / metre), double(lo / r), double(hi / r), double(eye / r), double(eye / metre),
            double(p.centre[2] / metre), double(p.cell / metre), double(p.extent / metre), unsigned(p.bubble),
            double(p.bubble_c[0] / metre), double(p.bubble_c[1] / metre), double(p.bubble_c[2] / metre),
            double(p.bubble_e[0] / metre), double(p.bubble_e[1] / metre), double(p.bubble_e[2] / metre),
            unsigned(p.eye_sphere), double(p.eye_sphere ? p.bubble_e[0] / metre : 0.f));
    }
}
void MotionOutput::weapon_effects_take_frame(unsigned* patches, unsigned* decals, unsigned* retired, float* stage_us,
                                             bool* drew, unsigned drops[36]) noexcept {
    *patches = weapon_patches_;
    *decals = weapon_decals_;
    *retired = weapon_retired_;
    *stage_us = weapon_drew_ ? weapon_stage_us_ : 0.f;
    *drew = weapon_drew_;
    for (unsigned i = 0; i < 36; ++i) {
        drops[i] = weapon_drops_[i];
        weapon_drops_[i] = 0;
    }
    drops[27] = weapon_ && weapon_->caps().hull_enabled; // the hull latch (Present's HookState::hull_ready)
    weapon_patches_ = weapon_decals_ = weapon_retired_ = 0;
    weapon_stage_us_ = 0.f;
    weapon_drew_ = false;
    // Ownership is earned again by the next frame's resolve (the own sun's rule).
    weapon_armed_ = false;
    weapon_callback_set_ = weapon_callback_ok_ = weapon_owned_ = false;
}
