// Weapon effects, step 1: the shield hit (included by motion_output.cpp inside namespace x3m; AP only;
// weapon_effects_patch.h, weapon_effects_stage.h, renderer/weapon_effects_pass.h; docs/architecture/weapon-effects.md
// 2.6, 5.1 and 5.1.1). X3M_WEAPON_EFFECTS=shields: the impact hook records every hit into the proxy ring and marks a
// shield hit stage-owned (flag_stage) while the stage owned the previous frame and skips the engine's own impact for it
// (core::suppress_native_shield_hit, coordinator decision 2026-11-06: the patch alone). This
// stage draws the hex patches and flat decals of the ring's
// stage-owned records as an act of the temporal resolve's stage bracket, after the plumes and the own sun when they
// draw (one callback slot: the stage chains the callback it found, own_sun_with_plumes_'s pattern), reading the lane the
// resolve reads (occlusion by the lane alone; the depth surface is unbound before the callback).
// The hull-hit, explosion, bolt and muzzle stages were removed with the look layer on 2026-11-08
// (docs/archive/effects-look-2026-11-08/index.md): only the shield hit is drawn.
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
    if (queried) {
        if (taa_busy_)
            hr = weapon_->attach(device_, native_, caps_, display.Format);
        else
            taa_call([&] { hr = weapon_->attach(device_, native_, caps_, display.Format); });
    }
    const bool attached = queried && SUCCEEDED(hr) && weapon_->caps().enabled;
    const auto& c = weapon_->caps();
    log("weapon_effects_device device=%llu frame=%llu attached=%u reason=%s result=%08lx fp16_blending=%08lx shell_vs_slots=%u shell_ps_slots=%u decal_vs_slots=%u decal_ps_slots=%u bubble_ps_slots=%u max_ps_slots=%lu retry=reset",
        id_, frame_, unsigned(attached), attached ? "ok" : queried ? c.reason : "adapter_query", hr, c.fp16_blending,
        c.shell_vs_slots, c.shell_ps_slots, c.decal_vs_slots, c.decal_ps_slots, c.bubble_ps_slots,
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
}
HRESULT MotionOutput::run_weapon_effects() noexcept {
    namespace wcore = weapon_effects::core;
    LARGE_INTEGER begin{}, end{};
    QueryPerformanceCounter(&begin);
    weapon_effects::stage::Camera camera;
    for (unsigned i = 0; i < 9; ++i) camera.r[i] = camera_scene_.r[i];
    for (unsigned i = 0; i < 3; ++i) camera.t[i] = camera_scene_.t[i];
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
#if X3M_EDITION_FL
    // X3FL.exe's body cache slot (base + 0x1aa100), bound with the other globals at install (core::fl_globals).
    tuning.body_cache_root = weapon_effects::hook_state().globals.body_cache_slot;
#endif
    // The frame's draws (64 x sizeof(WeaponPatch), about 124 KB: static, off the game thread's stack; the game thread is
    // the only caller and run() consumes them before returning).
    static renderer::WeaponPatch patches[wcore::max_patches];
    static renderer::WeaponDecal decals[wcore::max_decals];
    weapon_bubble_cache.rescope(weapon_effects::scope_changes());
    // The own ship in the cockpit view (5.1.1, Run236 Flight C): its patches at core::own_cockpit_gain, and (Run240
    // Flight G) its bubble the eye sphere, so both are known before the build. The own ship is resolve_own_ship's root
    // node (cached per frame); the view is the chase camera's last active-control cockpit visit this frame or the
    // previous one (cockpit+0x150, 1 = the cockpit; stage::CockpitClock stamps the frame its visit count last moved: a
    // camera no longer visiting is stale, no dim), read only when a stage-owned record belongs to the own ship. Without the
    // chase camera installed the view is unknown: nothing is dimmed and the own ship keeps its hull bubble.
    weapon_cockpit_ = false;
    std::uint32_t own = 0;
    {
        const wcore::Ring& ring = weapon_effects::hook_state().ring;
        bool staged = false;
        for (unsigned i = 0; i < wcore::ring_capacity && !staged; ++i)
            staged = ring.slots[i].sequence && (ring.slots[i].flags & wcore::flag_stage);
        if (staged) {
            resolve_own_ship();
            own = std::uint32_t(own_ship_node_);
            bool any_own = false;
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
    // The owners this frame draws and those in view (the build adds its own): after the build every live record is
    // stamped (core::ring_stamp_hidden), so a full ring evicts an undrawn owner's records first and the next frame's
    // builds pick on-screen owners first. The far plane from the projection (m22 = f / (f - n), m32 = -n f / (f - n):
    // f = m32 / (1 - m22)); none when m22 <= 1; reset keeps it VisibleOwners::far_margin (5 %) out, since m22 near 1
    // leaves the float recovery about 2 % off. Its located table reads each owner past a cap once a frame for the
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
    wcore::ring_stamp_hidden(weapon_effects::hook_state().ring, visible);
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
#if X3M_EDITION_FL
    // FL bodies are assumed to keep AP's normalised LOD extents [0, 0x20000] (fl/weapon-effect-spawn.md section 5 [i]);
    // the AP bound stays the runtime guard (the walk fails to the fallback sphere) and its first refusal is one row.
    static bool extent_logged = false;
    if (n.bubble.extent_refused && !extent_logged) {
        extent_logged = true;
        log("weapon_bubble_extent edition=fl state=refused frame=%llu refused=%u walks=%u bound=%ld",
            static_cast<unsigned long long>(frame_), n.bubble.extent_refused, n.bubble.walks,
            static_cast<long>(weapon_effects::stage::hull_extent_max));
    }
#endif
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
                                             bool* drew, unsigned drops[13]) noexcept {
    *patches = weapon_patches_;
    *decals = weapon_decals_;
    *retired = weapon_retired_;
    *stage_us = weapon_drew_ ? weapon_stage_us_ : 0.f;
    *drew = weapon_drew_;
    for (unsigned i = 0; i < 13; ++i) {
        drops[i] = weapon_drops_[i];
        weapon_drops_[i] = 0;
    }
    weapon_patches_ = weapon_decals_ = weapon_retired_ = 0;
    weapon_stage_us_ = 0.f;
    weapon_drew_ = false;
    // Ownership is earned again by the next frame's resolve (the own sun's rule).
    weapon_armed_ = false;
    weapon_callback_set_ = weapon_callback_ok_ = weapon_owned_ = false;
}
