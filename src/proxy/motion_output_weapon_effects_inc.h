// Weapon effects, step 1: the shield hit (included by motion_output.cpp inside namespace x3m; AP only;
// weapon_effects_patch.h, weapon_effects_stage.h, renderer/weapon_effects_pass.h; docs/architecture/weapon-effects.md
// 2.6, 5.1 and 5.1.1). X3M_WEAPON_EFFECTS=shields: the impact hook records every hit into the proxy ring and marks a
// shield hit stage-owned (flag_stage) while the stage owned the previous frame; the engine's own impact still spawns
// (user decision 2026-11-06: the native sprite plus the patch; core::suppress_native_shield_hit would skip it). This
// stage draws the hex patches and flat decals of the ring's
// stage-owned records as an act of the temporal resolve's stage bracket, after the plumes and the own sun when they
// draw (one callback slot: the stage chains the callback it found, own_sun_with_plumes_'s pattern), reading the lane the
// resolve reads (occlusion by the lane alone; the depth surface is unbound before the callback).
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
    log("weapon_effects_device device=%llu frame=%llu attached=%u reason=%s result=%08lx fp16_blending=%08lx shell_vs_slots=%u shell_ps_slots=%u decal_vs_slots=%u decal_ps_slots=%u max_ps_slots=%lu retry=reset",
        id_, frame_, unsigned(attached), attached ? "ok" : queried ? c.reason : "adapter_query", hr, c.fp16_blending,
        c.shell_vs_slots, c.shell_ps_slots, c.decal_vs_slots, c.decal_ps_slots,
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
    renderer::WeaponPatch patches[wcore::max_patches];
    renderer::WeaponDecal decals[wcore::max_decals];
    const weapon_effects::stage::Counts n =
        weapon_effects::stage::build(weapon_effects::hook_state().ring, weapon_effects::game_ms(), &weapon_effects_read,
                                     camera, weapon_effects::stage::Tuning{}, patches, wcore::max_patches, decals,
                                     wcore::max_decals);
    f.patches = patches;
    f.patch_count = n.patches;
    f.decals = decals;
    f.decal_count = n.decals;
    weapon_retired_ += n.retired;
    weapon_drops_[0] += n.dropped;
    weapon_drops_[1] += n.cells_dropped;
    weapon_drops_[2] += n.hits_dropped;
    weapon_drops_[3] += n.spills;
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
void MotionOutput::weapon_effects_take_frame(unsigned* patches, unsigned* decals, unsigned* retired, float* stage_us,
                                             bool* drew, unsigned drops[4]) noexcept {
    *patches = weapon_patches_;
    *decals = weapon_decals_;
    *retired = weapon_retired_;
    *stage_us = weapon_drew_ ? weapon_stage_us_ : 0.f;
    *drew = weapon_drew_;
    for (unsigned i = 0; i < 4; ++i) {
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
