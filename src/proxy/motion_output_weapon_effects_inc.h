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
    weapon_hull_requested_ = requested && weapon_effects::core::hull_mode(weapon_effects::hook_state().mode);
    // Step 4: the in-place bolt draw (shields,hull,bolts, the hooks installed in that mode).
    weapon_bolts_requested_ = requested && weapon_effects::core::bolts_mode(weapon_effects::mode());
    weapon_bolt_failed_out_ = false;
    weapon_bolt_failures_ = 0;
    weapon_bolt_reason_ = weapon_bolts_requested_ ? "pending" : "off";
    weapon_bolt_reason_logged_ = false;
    weapon_shimmer_count_ = 0;
    weapon_wave_shimmer_count_ = 0;
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
    const bool hull = weapon_effects::core::hull_mode(weapon_effects::hook_state().mode);
    const bool bolts = weapon_bolts_requested_; // step 4: the capsule programs and buffers too
    // step 3: the death programs (the shock) in the explosions modes, beside the hull's
    const bool booms = hull && weapon_effects::core::explosions_mode(weapon_effects::hook_state().mode);
    if (queried) {
        if (taa_busy_)
            hr = weapon_->attach(device_, native_, caps_, display.Format, hull, bolts, booms);
        else
            taa_call([&] { hr = weapon_->attach(device_, native_, caps_, display.Format, hull, bolts, booms); });
    }
    const bool attached = queried && SUCCEEDED(hr) && weapon_->caps().enabled;
    const auto& c = weapon_->caps();
    log("weapon_effects_device device=%llu frame=%llu attached=%u reason=%s result=%08lx fp16_blending=%08lx shell_vs_slots=%u shell_ps_slots=%u decal_vs_slots=%u decal_ps_slots=%u sprite_vs_slots=%u sprite_ps_slots=%u glow_vs_slots=%u glow_ps_slots=%u fireball_vs_slots=%u fireball_ps_slots=%u filament_ps_slots=%u lightning_vs_slots=%u lightning_vtf=%u,%u hull=%u hull_reason=%s bolt=%u bolt_reason=%s bolt_vs_slots=%u bolt_ps_slots=%u bolt_flag_ps_slots=%u boom=%u boom_reason=%s shock=%u shock_reason=%s shock_ps_slots=%u halo_ps_slots=%u wave=%u wave_reason=%s wave_ps_slots=%u compose=%u compose_reason=%s compose_ps_slots=%u max_ps_slots=%lu retry=reset",
        id_, frame_, unsigned(attached), attached ? "ok" : queried ? c.reason : "adapter_query", hr, c.fp16_blending,
        c.shell_vs_slots, c.shell_ps_slots, c.decal_vs_slots, c.decal_ps_slots, c.sprite_vs_slots, c.sprite_ps_slots,
        c.glow_vs_slots, c.glow_ps_slots, c.fireball_vs_slots, c.fireball_ps_slots, c.filament_ps_slots, c.lightning_vs_slots,
        unsigned(c.lightning_vtf_four), unsigned(c.lightning_vtf_r32f), unsigned(c.hull_enabled), c.hull_reason,
        unsigned(c.bolt_enabled), c.bolt_reason, c.bolt_vs_slots, c.bolt_ps_slots, c.bolt_flag_ps_slots,
        unsigned(c.boom_enabled), c.boom_reason, unsigned(c.shock_enabled), c.shock_reason, c.shock_ps_slots, c.halo_ps_slots,
        unsigned(c.wave_enabled), c.wave_reason, c.wave_ps_slots, unsigned(c.compose_enabled), c.compose_reason,
        c.compose_ps_slots, static_cast<unsigned long>(caps_.MaxPixelShader30InstructionSlots));
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
// The bursts and their smoke (fireball impostors, about 78 KB each), the surface rings and filament glows (24 KB and
// 12 KB) and the scorches (20 KB), static as the sprites.
renderer::WeaponFireballVertex weapon_fireballs[renderer::weapon_fireball_boxes * 4];
renderer::WeaponFireballVertex weapon_smokes[renderer::weapon_smoke_boxes * 4];
renderer::WeaponShapeVertex weapon_rings[renderer::weapon_ring_quads * 4];
renderer::WeaponLightningVertex weapon_filaments[renderer::weapon_filament_boxes * 4];
renderer::WeaponGlowVertex weapon_scorches[renderer::weapon_scorch_boxes * 4];
// Step 3 (weapon_effects_boom.h): the death records' per-slot state (first frame, floor, world point, seed cones; CPU
// data: a Reset leaves it valid, the sequence recognises a reused slot) and the frame's shock boxes (static as the
// sprites).
weapon_effects::stage::BoomState weapon_boom_state;
renderer::WeaponFireballVertex weapon_shocks[renderer::weapon_shock_boxes * 4];
renderer::WeaponGlowVertex weapon_halos[renderer::weapon_halo_boxes * 4];
renderer::WeaponFireballVertex weapon_waves[renderer::weapon_wave_boxes * 4 * renderer::weapon_wave_quads]; // 13.1: the fire waves' quads
renderer::WeaponComposeConstants weapon_compose[weapon_effects::core::boom::max_bodies]; // section 13.2: the tables
// Step 4: the bullet rows' looks, one read per row per frame (the in-place bolt draw and the muzzles; CPU data).
weapon_effects::stage::BoltRowCache& weapon_bolt_rows() {
    static weapon_effects::stage::BoltRowCache cache;
    return cache;
}
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
    const bool hull_mode = wcore::hull_mode(weapon_effects::hook_state().mode) && weapon_->caps().hull_enabled;
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
    weapon_effects::stage::BoomCounts bn{}; // step 3: the deaths (built in the explosions modes)
    unsigned boom_shocks = 0, boom_halos = 0, boom_waves = 0, boom_composed = 0, boom_first = 0;
    const renderer::WeaponWaveConstants* boom_wave_constants = nullptr;
    weapon_wave_shimmer_count_ = 0; // the newest fire wave's refraction request, built below in the explosions modes
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
        hb.smokes = weapon_->caps().scorch_enabled ? weapon_smokes : nullptr; // the smoke multiplies as the scorches
        hb.smoke_cap = renderer::weapon_smoke_boxes;
        // The shimmer rects go to the engine shimmer pass after the resolve (motion_output_engine_shimmer_inc.h), this
        // frame only.
        hb.shimmer = weapon_shimmer_rects_;
        hb.shimmer_cap = shimmer_requested_ ? weapon_effects::core::hull::shimmer_max : 0u;
        // Step 4: the muzzle flashes (bolt-muzzle-look.md section 3) first into the shared batches (at most 128 records x
        // 7 sprites, 128 glows, fireballs and puffs: always room), the hull hit's elements after them, so a muzzle whose
        // native effect the hook skipped is always drawn (finding 2); only the records the hook marked ours.
        unsigned mz_sprites = 0, mz_glows = 0, mz_fireballs = 0, mz_scorches = 0, mz_smokes = 0;
        if (weapon_bolts_requested_ && weapon_effects::core::bolts_mode(weapon_effects::mode())) {
            const wcore::BoltHookState& bs = weapon_effects::bolt_hook_state();
            weapon_effects::stage::MuzzleBuffers mb;
            mb.sprites = weapon_sprites;
            mb.sprite_cap = renderer::weapon_sprite_quads;
            mb.glows = weapon_glows;
            mb.glow_cap = renderer::weapon_glow_boxes;
            mb.fireballs = weapon_fireballs;
            mb.fireball_cap = renderer::weapon_fireball_boxes;
            mb.scorches = hb.scorches;
            mb.scorch_cap = hb.scorches ? renderer::weapon_scorch_boxes : 0u;
            const std::uint32_t stamp = bs.generation ? bs.generation : 1u;
            const weapon_effects::core::HookState& hs = weapon_effects::hook_state();
            const auto look = [&](unsigned row) -> const weapon_effects::stage::BoltLook& {
                return weapon_effects::stage::bolt_look_cached(weapon_bolt_rows(), stamp, &weapon_effects_read, hs.globals,
                                                               hs.table, row);
            };
            const weapon_effects::stage::MuzzleCounts mc = weapon_effects::stage::build_muzzles(
                &weapon_effects_read, bs.muzzles, wcore::muzzle::record_capacity, hv,
                weapon_effects::stage::MuzzleTuning{}, look, mb);
            mz_sprites = mb.sprite_quads;
            mz_glows = mb.glow_boxes;
            mz_fireballs = mb.fireball_boxes;
            mz_scorches = mb.scorch_boxes;
            weapon_bolt_muzzles_ += mc.drawn;
            weapon_bolt_muzzle_elements_ += mb.sprite_quads + mb.glow_boxes + mb.fireball_boxes + mb.scorch_boxes;
            weapon_bolt_muzzle_glows_ += mc.glows;
            weapon_bolt_muzzle_puffs_ += mc.puffs;
            weapon_bolt_muzzle_native_ += mc.native;
            weapon_bolt_muzzle_endon_ += mc.endon;
            weapon_bolt_muzzle_starved_ += mc.starved;
            weapon_bolt_muzzle_built_ = true;
            hb.sprites = weapon_sprites + 4 * mz_sprites;
            hb.sprite_cap -= mz_sprites;
            hb.glows = weapon_glows + 4 * mz_glows;
            hb.glow_cap -= mz_glows;
            hb.fireballs = weapon_fireballs + 4 * mz_fireballs;
            hb.fireball_cap -= mz_fireballs;
            if (hb.scorches) {
                hb.scorches = weapon_scorches + 4 * mz_scorches;
                hb.scorch_cap -= mz_scorches;
            }
        }
        // Step 3 (explosions modes, the pass holding the death programs): the deaths before the hull hits, into the shared
        // batches after the muzzles, charged first to the budget the patches left (a death is rarer and larger than a
        // hit; the hull hits shed against what the deaths leave).
        if (weapon_effects::core::explosions_mode(weapon_effects::mode()) && weapon_->caps().boom_enabled) {
            weapon_effects::stage::BoomBuffers bb;
            bb.sprites = hb.sprites;
            bb.sprite_cap = hb.sprite_cap;
            bb.glows = hb.glows;
            bb.glow_cap = hb.glow_cap;
            bb.fireballs = hb.fireballs;
            bb.fireball_cap = hb.fireball_cap;
            bb.smokes = hb.smokes;
            bb.smoke_cap = hb.smokes ? hb.smoke_cap : 0u;
            bb.scorches = hb.scorches;
            bb.scorch_cap = hb.scorches ? hb.scorch_cap : 0u;
            bb.shocks = weapon_shocks;
            bb.shock_cap = renderer::weapon_shock_boxes;
            bb.halos = weapon_halos;
            bb.shock_cap = weapon_->caps().shock_enabled ? bb.shock_cap : 0u; // the shells only with their program
            bb.halo_cap = renderer::weapon_halo_boxes;
            bb.waves = weapon_waves;
            bb.wave_cap = weapon_->caps().wave_enabled ? renderer::weapon_wave_boxes : 0u; // the waves only with their program
            bb.wave_shimmer = &weapon_wave_shimmer_;
            bb.wave_strip_room = shimmer_rects_before_wave_ < engine_shimmer::max_rects
                                     ? engine_shimmer::max_rects - shimmer_rects_before_wave_ : 0u;
            bb.compose = weapon_->caps().compose_enabled ? weapon_compose : nullptr; // composed only with its program
            bn = weapon_effects::stage::build_boom(weapon_effects::hook_state().ring, weapon_boom_state, hv,
                                                   weapon_effects::stage::BoomTuning{}, bb, &visible, &weapon_bubble_cache);
            boom_shocks = bb.shock_boxes;
            boom_halos = bb.halo_boxes;
            boom_waves = bb.wave_boxes;
            boom_wave_constants = bb.wave_constants;
            boom_composed = bb.composed_boxes;
            boom_first = mz_fireballs; // before the boom's boxes join the count below
            weapon_wave_shimmer_count_ = bb.wave_shimmer_count;
            hb.sprites += 4 * bb.sprite_quads;
            hb.sprite_cap -= bb.sprite_quads;
            hb.glows += 4 * bb.glow_boxes;
            hb.glow_cap -= bb.glow_boxes;
            hb.fireballs += 4 * bb.fireball_boxes;
            hb.fireball_cap -= bb.fireball_boxes;
            if (hb.smokes) {
                hb.smokes += 4 * bb.smoke_boxes;
                hb.smoke_cap -= bb.smoke_boxes;
            }
            if (hb.scorches) {
                hb.scorches += 4 * bb.scorch_boxes;
                hb.scorch_cap -= bb.scorch_boxes;
            }
            mz_sprites += bb.sprite_quads;
            mz_glows += bb.glow_boxes;
            mz_fireballs += bb.fireball_boxes;
            mz_scorches += bb.scorch_boxes;
            mz_smokes = bb.smoke_boxes;
            hv.budget_left -= bn.screens;
            const std::uint32_t boom_latched = std::uint32_t(bn.max_floor_ms),
                                boom_now = wcore::look::min_frames * hv.step_ms;
            weapon_effects::hook_state().boom_floor_ms = (boom_latched > boom_now ? boom_latched : boom_now) + hv.step_ms;
            weapon_drops_[36] += bn.deaths;
            weapon_drops_[37] += bn.bodies;
            weapon_drops_[38] += bn.stars;
            weapon_drops_[39] += bn.shocks;
            weapon_drops_[40] += bn.embers;
            weapon_drops_[41] += bn.fragments;
            weapon_drops_[42] += bn.smokes;
            weapon_drops_[43] += bn.glows;
            weapon_drops_[44] += bn.shed;
            weapon_drops_[45] += bn.bodies && bn.octaves < wcore::look::fireball_octaves;
            weapon_drops_[46] += bn.out_of_reach;
            weapon_drops_[47] += bn.capped;
            weapon_drops_[49] += bn.secondaries;
            weapon_drops_[50] += bn.hull_lights;
            if (capture_ && (bn.deaths || bn.out_of_reach))
                log("weapon_boom_capture device=%llu frame=%llu now=%lu step_ms=%lu deaths=%u bodies=%u stars=%u shocks=%u "
                    "embers=%u fragments=%u darks=%u glows=%u smokes=%u shed=%u octaves=%u fine=%u out_of_reach=%u "
                    "capped=%u floored=%u screens=%.3f body_screens=%.3f max_cluster=%.2f halos=%u embers_hidden=%u "
                    "secondaries=%u secondaries_shed=%u secondaries_born=%u hull_lights=%u bubble_cached=%u capsules=%u "
                    "waves=%u waves_shed=%u wave_plane=%u wave_strips=%u wave_strips_capped=%u wave_strips_dropped=%u composed=%u "
                    "compose_shared=%u compose_dropped=%u compose_eye=%u",
                    id_, frame_, static_cast<unsigned long>(hv.now), static_cast<unsigned long>(hv.step_ms),
                    bn.deaths, bn.bodies, bn.stars, bn.shocks,
                    bn.embers, bn.fragments, bn.darks, bn.glows,
                    bn.smokes, bn.shed, bn.octaves, bn.fine,
                    bn.out_of_reach, bn.capped, bn.floored,
                    double(bn.screens), double(bn.body_screens), double(bn.max_cluster), bn.halos, bn.embers_hidden,
                    bn.secondaries, bn.secondaries_shed, bn.secondaries_born, bn.hull_lights, bn.bubble_cached, bn.capsules,
                    bn.waves, bn.waves_shed, bn.wave_view_plane, bn.wave_strips, bn.wave_strips_capped, bn.wave_strips_dropped, bn.composed,
                    bn.compose_shared, bn.compose_dropped, bn.compose_eye);
        }
        hn = weapon_effects::stage::build_hull(weapon_effects::hook_state().ring, weapon_hull_state, &weapon_effects_read,
                                               hv, weapon_effects::stage::HullTuning{}, hb, &visible);
        const unsigned quads = mz_sprites + hb.sprite_quads, boxes = mz_glows + hb.glow_boxes,
                       fireballs = mz_fireballs + hb.fireball_boxes, rings = hb.ring_quads;
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
        f.fireball_octaves = hn.octaves < bn.octaves ? hn.octaves : bn.octaves;
        f.shocks = weapon_shocks;
        f.shock_boxes = boom_shocks;
        f.halos = weapon_halos;
        f.halo_boxes = boom_halos;
        f.waves = weapon_waves;
        f.wave_boxes = boom_waves;
        f.wave_constants = boom_wave_constants;
        f.compose = weapon_compose;
        f.composed_first = boom_first; // the boom's bodies follow the muzzles' fireballs
        f.composed_boxes = boom_composed;
        f.rings = weapon_rings;
        f.ring_quads = rings;
        f.filaments = weapon_filaments;
        f.filament_boxes = hb.filament_boxes;
        f.scorches = weapon_scorches;
        f.scorch_boxes = mz_scorches + hb.scorch_boxes;
        f.smokes = weapon_smokes;
        f.smoke_boxes = mz_smokes + hb.smoke_boxes;
        // F8 (a capture frame): the frame's hull-hit figures, the step and the views the streaks used.
        if (capture_)
            log("weapon_hull_frame device=%llu frame=%llu now=%lu step_ms=%lu prev_view=%u own=%08lx cockpit=%u hits=%u "
                "flashes=%u sparks=%u glows=%u embers=%u shed=%u orphans=%u retired=%u floored=%u capped=%u "
                "owners_dropped=%u quads=%u rects=%u glow_screens=%.3f patch_screens=%.3f bursts=%u rings=%u "
                "bursts_shed=%u octaves=%u burst_screens=%.3f streaks=%u chips=%u filaments=%u scorches=%u marks=%u "
                "spots=%u merges=%u spot_flashes=%u shimmer=%u shimmer_shed=%u scorches_shed=%u rings_shed=%u "
                "filaments_shed=%u jet_no_dir=%u energy=%u ballistic=%u plasma=%u heavy=%u ion=%u beam=%u missile=%u "
                "asteroid=%u repair=%u tug=%u done=%u flashes_shed=%u pulses=%u out_of_reach=%u owners_out_of_reach=%u "
                "owners_located=%u smokes=%u smokes_shed=%u fine=%u ion_segments=%u",
                id_, frame_, static_cast<unsigned long>(hv.now), static_cast<unsigned long>(hv.step_ms),
                unsigned(hv.prev_valid), static_cast<unsigned long>(own), unsigned(hv.cockpit), hn.hits, hn.flashes,
                hn.sparks, hn.glows, hn.embers, hn.shed, hn.orphans, hn.retired, hn.floored, hn.capped, hn.owners_dropped,
                quads, boxes, double(hn.screens), double(n.screens), hn.bursts, hn.rings, hn.bursts_shed, hn.octaves,
                double(hn.burst_screens), hn.streaks, hn.chips, hn.filaments, hn.scorches, hn.marks, hn.spots, hn.merges,
                hn.spot_flashes, hn.shimmer, hn.shimmer_shed, hn.scorches_shed, hn.rings_shed, hn.filaments_shed,
                hn.dir_fallback, hn.anatomy[0], hn.anatomy[1], hn.anatomy[2], hn.anatomy[3], hn.anatomy[4], hn.anatomy[5],
                hn.anatomy[6], hn.anatomy[7], hn.anatomy[8], hn.anatomy[9], hn.done, hn.flashes_shed, hn.pulses,
                hn.out_of_reach, hn.owners_out_of_reach, visible.located_reads, hn.smokes, hn.smokes_shed, hn.fine,
                hn.ion_segments);
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
    weapon_bolt_muzzle_drew_ = weapon_bolt_muzzle_built_ && SUCCEEDED(hr); // the muzzle latch's "the stage drew them"
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
                                             bool* drew, unsigned drops[51]) noexcept {
    *patches = weapon_patches_;
    *decals = weapon_decals_;
    *retired = weapon_retired_;
    *stage_us = weapon_drew_ ? weapon_stage_us_ : 0.f;
    *drew = weapon_drew_;
    for (unsigned i = 0; i < 51; ++i) {
        drops[i] = weapon_drops_[i];
        weapon_drops_[i] = 0;
    }
    drops[27] = weapon_ && weapon_->caps().hull_enabled; // the hull latch (Present's HookState::hull_ready)
    drops[48] = weapon_ && weapon_->caps().boom_enabled; // the death latch (Present's HookState::boom_ready)
    weapon_patches_ = weapon_decals_ = weapon_retired_ = 0;
    weapon_stage_us_ = 0.f;
    weapon_drew_ = false;
    // Ownership is earned again by the next frame's resolve (the own sun's rule).
    weapon_armed_ = false;
    weapon_callback_set_ = weapon_callback_ok_ = weapon_owned_ = false;
}
// Step 4 (shields,hull,bolts): the in-place bolt draw's figures for weapon_effects::present_bolts, the frame's arming
// reason (weapon_bolts_armed evaluated at least once a frame: its weapon_bolts_state row on every change, the reason in
// every weapon_bolts_frame row) and the muzzle latch (stage::muzzle_latch: armed, the stage drew this frame's muzzles, no
// element short of room). The frame's ownership starts over (stage::bolt_take_frame_end).
namespace {
weapon_effects::stage::BoltOwnership weapon_bolt_own;
}
void MotionOutput::weapon_bolts_take_frame(weapon_effects::BoltFrame* bolt_frame) noexcept {
    const bool armed = weapon_bolts_armed();
    *bolt_frame = weapon_effects::BoltFrame{};
    bolt_frame->drew = weapon_bolt_drew_;
    bolt_frame->armed = armed;
    bolt_frame->reason = weapon_bolt_reason_;
    bolt_frame->muzzle_owned = weapon_effects::stage::muzzle_latch(armed, weapon_bolt_muzzle_drew_, weapon_bolt_muzzle_starved_);
    bolt_frame->taken = weapon_bolt_own.decision == 1;
    bolt_frame->bolts = weapon_bolt_count_;
    bolt_frame->suppressed = weapon_bolt_suppressed_;
    bolt_frame->culled = weapon_bolt_culled_;
    bolt_frame->beams = weapon_bolt_beams_;
    bolt_frame->parity_mismatch = weapon_bolt_parity_;
    bolt_frame->build_us = weapon_bolt_build_us_;
    bolt_frame->muzzles = weapon_bolt_muzzles_;
    bolt_frame->muzzle_elements = weapon_bolt_muzzle_elements_;
    bolt_frame->muzzle_glows = weapon_bolt_muzzle_glows_;
    bolt_frame->muzzle_puffs = weapon_bolt_muzzle_puffs_;
    bolt_frame->muzzle_native = weapon_bolt_muzzle_native_;
    bolt_frame->muzzle_endon = weapon_bolt_muzzle_endon_;
    bolt_frame->muzzle_starved = weapon_bolt_muzzle_starved_;
    bolt_frame->rims = weapon_bolt_rims_;
    bolt_frame->refused = weapon_bolt_refused_;
    bolt_frame->lost_early = weapon_bolt_own.decision == 1 && weapon_bolt_own.suppressed && !weapon_bolt_own.drew;
    weapon_effects::stage::bolt_take_frame_end(weapon_bolt_own);
    weapon_bolt_drew_ = weapon_bolt_muzzle_built_ = weapon_bolt_muzzle_drew_ = false;
    weapon_bolt_count_ = weapon_bolt_suppressed_ = weapon_bolt_culled_ = weapon_bolt_beams_ = weapon_bolt_parity_ = 0;
    weapon_bolt_muzzles_ = weapon_bolt_muzzle_elements_ = weapon_bolt_muzzle_glows_ = weapon_bolt_muzzle_puffs_ = 0;
    weapon_bolt_muzzle_native_ = weapon_bolt_muzzle_endon_ = weapon_bolt_muzzle_starved_ = 0;
    weapon_bolt_rims_ = weapon_bolt_refused_ = 0;
    weapon_bolt_build_us_ = 0.f;
}
// The in-place draw's arming (bolt-muzzle-look.md 5.1, the stage's ownership rule): the screen_emission_additive route
// (the only path that sees the bullet-producer draws: without it every bolt stays native), the HDR/TAA FP16 path, the
// scene camera latched this frame, the pass attached with the capsule objects, the bolt hooks armed, not within 64 frames
// of a failed draw (the third consecutive failure: off until Reset). Evaluated once a frame (cached by frame_); one
// weapon_bolts_state row per change of the reason.
bool MotionOutput::weapon_bolts_armed() noexcept {
    if (weapon_bolt_armed_frame_ == frame_ && weapon_bolt_reason_logged_) return weapon_bolt_armed_;
    const char* reason = "armed";
    if (!enabled_)
        reason = "route";
    else if (!screen_additive_requested_)
        reason = "screen_emission_additive";
    else if (!taa_enabled_ || taa_failed_ || main_msaa_ || hdr_state_ != HdrState::Active || !hdr_)
        reason = "hdr_taa_path";
    else if (!camera_scene_.valid || !(camera_scene_.m00 > 0.f) || !(camera_scene_.m11 > 0.f))
        reason = "camera";
    else if (!weapon_ || !weapon_->caps().enabled)
        reason = "not_attached";
    else if (!weapon_->caps().bolt_enabled)
        reason = weapon_->caps().bolt_reason ? weapon_->caps().bolt_reason : "bolt_attach";
    else if (!weapon_effects::bolt_hook_state().armed)
        reason = "hooks";
    else if (weapon_bolt_failed_out_)
        reason = "failed_until_reset";
    else if (frame_ < weapon_bolt_disarmed_until_)
        reason = "disarmed";
    if (!weapon_bolt_reason_logged_ || std::strcmp(reason, weapon_bolt_reason_)) {
        weapon_bolt_reason_logged_ = true;
        weapon_bolt_reason_ = reason;
        log("weapon_bolts_state device=%llu frame=%llu armed=%u reason=%s", id_, frame_, unsigned(!std::strcmp(reason, "armed")),
            reason);
    }
    weapon_bolt_armed_frame_ = frame_;
    weapon_bolt_armed_ = !std::strcmp(reason, "armed");
    return weapon_bolt_armed_;
}
// Every bullet-producer draw of a frame (the per-draw path, after the bindings were restored and before every other
// preparation, so the additive route's refusals never decide it; review 2026-11-07, finding 3): stage::bolt_take decides
// the frame's ownership at its first such draw and each draw's action. Ours: the frame's tap list (and the previous one)
// through stage::build_bolts with the scene camera and this frame's jitter, the parity rule against the draw's instances
// when the locked-prefix scan holds them, the far flag's targets bound as the route binds them
// (bind_bolt_flag_targets), then WeaponEffectsPass::draw_bolts (it saves, sets and restores every state it touches), the
// flag targets unbound. True: the draw was handled (route.submit false, D3D_OK), ours drawn or the copy suppressed. A
// draw that cannot take ours here (an open scene, no state-block recording, no active query) or a failed draw forwards
// the game's copy and leaves the rest of the frame native; a failed restore drops the shadow (motion_state_lost_), as
// every other route undo does.
namespace {
weapon_effects::stage::BoltScratch weapon_bolt_scratch;
renderer::WeaponBoltQuad weapon_bolt_quads[renderer::weapon_bolt_quads];
weapon_effects::stage::BoltPrevIndex weapon_bolt_prev;
weapon_effects::stage::BoltRowPeriods weapon_bolt_periods;
weapon_effects::stage::BoltParityScratch weapon_bolt_parity_scratch;
std::uint8_t weapon_bolt_keep[weapon_effects::core::bolt::list_capacity];
float weapon_bolt_rec_px[2 * weapon_effects::core::bolt::list_capacity];
std::uint16_t weapon_bolt_rec_row[weapon_effects::core::bolt::list_capacity];
weapon_effects::stage::BoltInstance weapon_bolt_instances[bolt_footprint::max_instances];
static_assert(bolt_footprint::max_instances <= weapon_effects::core::bolt::parity_instances,
              "the parity scratch holds every instance the scan can return");
}
bool MotionOutput::weapon_bolts_in_place(const MotionDrawCall& call, MotionRoute& route) noexcept {
    namespace wcore = weapon_effects::core;
    namespace ws = weapon_effects::stage;
    wcore::BoltHookState& b = weapon_effects::bolt_hook_state();
    const bool late = selector_.scene_depth_written();
    const bool armed = weapon_bolts_armed();
    const wcore::BoltList& cur = b.lists[b.generation & 1u];
    const unsigned listed = cur.generation == b.generation ? cur.count : 0u;
    // ours can draw in this draw's place: an open bound scene, no query or state-block recording in flight
    const bool drawable = call.primitives && scene_open_ && !active_queries_ && !shadow_.recording && scene_bound();
    const ws::BoltAction action = ws::bolt_take(weapon_bolt_own, armed, listed > 0, late, drawable);
    if (action == ws::BoltAction::native) return false;
    if (action == ws::BoltAction::suppress) {
        route.submit = false;
        route.submission_error = D3D_OK;
        ++weapon_bolt_suppressed_;
        return true;
    }
    LARGE_INTEGER begin{}, end{};
    QueryPerformanceCounter(&begin);
    const DWORD saved_error = GetLastError();
    // The own ship and the eye for the tap's next passes (own records, the far-drop order).
    resolve_own_ship();
    b.own_owner = std::uint32_t(own_ship_object_);
    {
        const float* r = camera_scene_.r;
        const float* t = camera_scene_.t;
        for (unsigned i = 0; i < 3; ++i) {
            const float e = -(t[0] * r[i * 3] + t[1] * r[i * 3 + 1] + t[2] * r[i * 3 + 2]) / wcore::world_scale;
            b.eye[i] = e > 2.0e9f ? 2000000000 : e < -2.0e9f ? -2000000000 : std::int32_t(e);
        }
    }
    ws::BoltView v;
    for (unsigned i = 0; i < 9; ++i) v.cam.r[i] = camera_scene_.r[i];
    for (unsigned i = 0; i < 3; ++i) v.cam.t[i] = camera_scene_.t[i];
    v.width = float(main_.width);
    v.height = float(main_.height);
    v.cam.m00 = camera_scene_.m00;
    v.cam.m11 = camera_scene_.m11;
    v.cam.m20 = camera_scene_.m20 + (main_.width ? 2.f * jitter_[0] / v.width : 0.f);
    v.cam.m21 = camera_scene_.m21 + (main_.height ? -2.f * jitter_[1] / v.height : 0.f);
    v.m22 = camera_scene_.m22 != 0.f ? camera_scene_.m22 : projection_default_m22;
    v.m32 = camera_scene_.m32 != 0.f ? camera_scene_.m32 : projection_default_m32;
    v.near_z = -v.m32 / v.m22;
    if (!(v.near_z > 0.f) || !(v.near_z < 1e30f)) v.near_z = -projection_default_m32 / projection_default_m22;
    v.prev_valid = camera_previous_.valid && camera_previous_frame_ + 1 == frame_;
    if (v.prev_valid) {
        for (unsigned i = 0; i < 9; ++i) v.prev.r[i] = camera_previous_.r[i];
        for (unsigned i = 0; i < 3; ++i) v.prev.t[i] = camera_previous_.t[i];
    }
    v.now = weapon_effects::game_ms();
    v.step_ms = weapon_game_step_ms();
    v.cockpit = weapon_cockpit_;
    // The stamp of this frame's look cache and of the previous list's index: the hooks' Present generation (monotonic,
    // one per frame; finding 1).
    const std::uint32_t stamp = b.generation ? b.generation : 1u;
    const wcore::HookState& hs = weapon_effects::hook_state();
    const auto look = [&](unsigned row) -> const ws::BoltLook& {
        return ws::bolt_look_cached(weapon_bolt_rows(), stamp, &weapon_effects_read, hs.globals, hs.table, row);
    };
    const unsigned n = listed < wcore::bolt::list_capacity ? listed : wcore::bolt::list_capacity;
    // The parity rule (unknown 14): the draw's bodies from the locked-prefix scan when it holds this draw's vertices, every
    // frame (the period learning runs on matched records whatever the counts).
    const std::uint8_t* keep = nullptr;
    unsigned parity = 0;
    {
        const std::uint32_t count = call.primitives * 3u;
        const fade_region::Query query{shadow_.stream0, shadow_.indices, shadow_.stream0_identity,
                                       shadow_.indices_identity};
        const float* positions = nullptr;
        const std::uint32_t* extras = nullptr;
        std::uint64_t revision = 0;
        unsigned refusal = 0;
        const std::size_t window = window_of(0u);
        const auto& vp = shadow_.viewport;
        if (n && call.topology == D3DPT_TRIANGLELIST && !call.indexed && !call.user_memory && !call.first &&
            count <= bolt_footprint::max_vertices && window < motion_matrix_windows_max && shadow_.rows_known[window] &&
            vp.known && vp.width && vp.height && shadow_.stream0_stride == bolt_footprint::stride &&
            fade_region::locked_prefix_vertices(query, count, &positions, &extras, &revision, &refusal)) {
            const std::uint32_t period = bolt_footprint::detect_period(extras, count);
            const float* rows = shadow_.rows[window];
            // pixels per object unit at w = 1 along x (the row's xyz length, half the viewport): an instance's radius
            const float sx = scalar::sqrt(rows[0] * rows[0] + rows[1] * rows[1] + rows[2] * rows[2]) * 0.5f * float(vp.width);
            unsigned m = 0;
            if (period)
                for (std::uint32_t at = 0; at + period <= count && m < bolt_footprint::max_instances; at += period) {
                    float c[3] = {0.f, 0.f, 0.f}, lo[3] = {3.4e38f, 3.4e38f, 3.4e38f}, hi[3] = {-3.4e38f, -3.4e38f, -3.4e38f};
                    for (std::uint32_t k = 0; k < period; ++k)
                        for (unsigned j = 0; j < 3; ++j) {
                            const float x = positions[(at + k) * 3 + j];
                            c[j] += x;
                            lo[j] = x < lo[j] ? x : lo[j];
                            hi[j] = x > hi[j] ? x : hi[j];
                        }
                    for (unsigned j = 0; j < 3; ++j) c[j] /= float(period);
                    const float cw = rows[12] * c[0] + rows[13] * c[1] + rows[14] * c[2] + rows[15];
                    if (!(cw > 1e-3f)) continue;
                    const float cx = rows[0] * c[0] + rows[1] * c[1] + rows[2] * c[2] + rows[3];
                    const float cy = rows[4] * c[0] + rows[5] * c[1] + rows[6] * c[2] + rows[7];
                    const float ex = hi[0] - lo[0], ey = hi[1] - lo[1], ez = hi[2] - lo[2];
                    weapon_bolt_instances[m].x = float(vp.x) + (cx / cw + 1.f) * 0.5f * float(vp.width);
                    weapon_bolt_instances[m].y = float(vp.y) + (1.f - cy / cw) * 0.5f * float(vp.height);
                    weapon_bolt_instances[m].r = 0.5f * scalar::sqrt(ex * ex + ey * ey + ez * ez) * sx / cw;
                    weapon_bolt_instances[m].period = period;
                    ++m;
                }
            if (m) {
                for (unsigned i = 0; i < n; ++i) {
                    float hv[3];
                    const double w[3] = {double(cur.records[i].pos[0]) * double(wcore::world_scale),
                                         double(cur.records[i].pos[1]) * double(wcore::world_scale),
                                         double(cur.records[i].pos[2]) * double(wcore::world_scale)};
                    ws::to_view(v.cam, w, hv);
                    float x = -1e6f, y = -1e6f;
                    if (hv[2] > v.near_z) ws::bolt_px(v.cam, 0.5f * v.width, 0.5f * v.height, hv, &x, &y);
                    weapon_bolt_rec_px[2 * i] = x;
                    weapon_bolt_rec_px[2 * i + 1] = y;
                    weapon_bolt_rec_row[i] = cur.records[i].row;
                }
                parity = ws::bolt_parity(weapon_bolt_rec_px, weapon_bolt_rec_row, n, weapon_bolt_instances, m,
                                         wcore::bolt::parity_tol_px, &weapon_bolt_periods, weapon_bolt_keep,
                                         weapon_bolt_parity_scratch);
                keep = weapon_bolt_keep;
            }
        }
    }
    const wcore::BoltList& prev = b.lists[(b.generation - 1u) & 1u];
    const unsigned prev_n = prev.generation + 1u == b.generation ? prev.count : 0u;
    ws::BoltFrameOut out;
    out.quads = weapon_bolt_quads;
    const ws::BoltCounts bc = ws::build_bolts(v, cur.records, n, prev.records, prev_n, weapon_bolt_prev, stamp, keep, look,
                                              weapon_bolt_scratch, out);
    renderer::WeaponBoltDraw draw;
    draw.quads = weapon_bolt_quads;
    draw.lines = out.lines;
    draw.orbs = out.orbs;
    draw.waves = out.waves;
    draw.rim_lines = out.rim_lines;
    draw.rim_orbs = out.rim_orbs;
    draw.rim = v.rim && !v.simple;
    ws::bolt_pixel_constants(v.now, v.simple, draw.look);
    // The far flag's targets, bound exactly as the route binds them for the late bullet draw (B').
    bool flag = bolt_far_requested_ && target_surface_ && depth_surface_ && depth_enabled_ && sun_lane_active_ &&
                (caps_.PrimitiveMiscCaps & D3DPMISCCAPS_INDEPENDENTWRITEMASKS);
    if (flag && FAILED(bind_bolt_flag_targets(route))) {
        flag = false;
        refuse_bolt_flag(5);
    }
    draw.far_flag = flag && route.bolt_flag;
    renderer::WeaponBoltReport rep{};
    HRESULT hr = S_FALSE;
    if (draw.lines + draw.orbs + draw.waves) taa_call([&] { hr = weapon_->draw_bolts(draw, &rep); });
    if (route.bolt_flag) {
        const HRESULT back = unbind_bolt_flag_targets(route);
        if (FAILED(back)) rep.restored = false;
    }
    if (!rep.restored) {
        if (!motion_state_lost_) {
            motion_state_lost_ = true;
            motion_state_error_ = FAILED(hr) ? hr : E_FAIL;
        }
        ++counters_.restore_failures;
        invalidate_render_states();
        invalidate_taa(TaaInvalidateSite::RestoreFailed);
    }
    QueryPerformanceCounter(&end);
    const std::uint64_t frequency = engine_qpc_frequency();
    const std::uint64_t ticks = std::uint64_t(end.QuadPart - begin.QuadPart);
    weapon_bolt_build_us_ += frequency && frequency < 0x80000000ull && ticks < 0x80000000ull
                                 ? float(double(std::int32_t(ticks)) * 1e6 / double(std::int32_t(frequency)))
                                 : 0.f;
    SetLastError(saved_error);
    if (FAILED(hr)) {
        // A failed draw: the game's late copy draws natively (nothing of ours may remain bound: draw_bolts restored);
        // 64 frames native, the third consecutive failure until Reset.
        ++weapon_bolt_refused_;
        const bool final = ++weapon_bolt_failures_ >= weapon_failure_limit;
        if (final)
            weapon_bolt_failed_out_ = true;
        else
            weapon_bolt_disarmed_until_ = frame_ + wcore::disarm_frames;
        log("weapon_bolts_failed device=%llu frame=%llu result=%08lx step=%u restored=%u rim_undone=%u consecutive=%u final=%u",
            id_, frame_, hr, unsigned(rep.failed), unsigned(rep.restored), unsigned(rep.rim_undone), weapon_bolt_failures_,
            unsigned(final));
        ws::bolt_take_result(weapon_bolt_own, false); // the rest of the frame native
        weapon_bolt_armed_frame_ = ~0ull; // Present re-evaluates: disarmed, so the muzzle latch drops for the next shots
        return false;
    }
    weapon_bolt_failures_ = 0;
    ws::bolt_take_result(weapon_bolt_own, true);
    weapon_bolt_drew_ = true;
    weapon_bolt_count_ += bc.drawn;
    weapon_bolt_culled_ += bc.behind + bc.offscreen + bc.expired;
    weapon_bolt_beams_ += bc.beams;
    weapon_bolt_parity_ += parity;
    weapon_bolt_rims_ += rep.rim_quads;
    if (capture_)
        log("weapon_bolts_draw device=%llu frame=%llu listed=%u drawn=%u lines=%u orbs=%u waves=%u rims=%u behind=%u "
            "offscreen=%u expired=%u invalid=%u beams=%u streaks=%u no_prev=%u discs=%u floored_len=%u floored_body=%u "
            "capped_own=%u capped_halo=%u capped_back=%u parity_dropped=%u far_flag=%u draws=%u calls=%u",
            id_, frame_, bc.listed, bc.drawn, bc.lines, bc.orbs, bc.waves, rep.rim_quads, bc.behind, bc.offscreen,
            bc.expired, bc.invalid, bc.beams, bc.streaks, bc.no_prev, bc.discs, bc.floored_len, bc.floored_body,
            bc.capped_own, bc.capped_halo, bc.capped_back, bc.parity_dropped, unsigned(draw.far_flag), rep.draws, rep.calls);
    route.submit = false;
    route.submission_error = D3D_OK;
    return true;
}
