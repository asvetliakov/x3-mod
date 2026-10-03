// Engine ribbons, phase 3 (included by motion_output.cpp inside namespace x3m after the plumes' include;
// engine_ribbons_core.h, renderer/engine_ribbons_pass.h, renderer/fog_transmittance.h;
// docs/architecture/engine-effects-modern.md sections 3-5). The plume stage's second draw: a ribbon trail behind every
// recorded main-jet nozzle, from the pass's ring buffers, after the plumes inside the same resolve bracket; and the fog
// law both draws take.
//
// Lifetime signals: the resolve's camera cut (FrameInputs::cut = counters_.cut || decision.cut || chase_snap, the TAA
// cut verdict) reaches the pass through note_cut only on a resolve with that verdict set, drawn or not, and clears the
// pool at its next update;
// Reset clears it in before_reset; the object_lifetime load epoch the recogniser read with the jet's node serial
// (engine_load_epoch_) clears it when it changes; and a stage gap of 0.3 s on the stage's clock (engine_clock_: an F8
// capture's stall is not a gap) evicts every ribbon by itself.

// The ribbon pass, attached at the arming latch with the plume pass (programs and buffers at the arming, not at the
// first ribbon); refused until Reset on failure with one engine_ribbons_device row, the plumes drawing without ribbons.
bool MotionOutput::attach_engine_ribbons() noexcept {
    if (ribbons_attach_failed_) return false;
    if (!ribbons_) {
        ribbons_.reset(new (std::nothrow) renderer::EngineRibbonsPass);
        if (!ribbons_) {
            ribbons_attach_failed_ = true;
            log("engine_ribbons_device device=%llu frame=%llu attached=0 reason=allocation retry=reset", id_, frame_);
            return false;
        }
    }
    if (ribbons_->caps().enabled) return true;
    D3DDISPLAYMODE display{};
    HRESULT hr = native<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &display);
    const bool queried = SUCCEEDED(hr);
    if (queried) {
        if (taa_busy_)
            hr = ribbons_->attach(device_, native_, caps_, display.Format);
        else
            taa_call([&] { hr = ribbons_->attach(device_, native_, caps_, display.Format); });
    }
    const bool attached = queried && SUCCEEDED(hr) && ribbons_->caps().enabled;
    const auto& c = ribbons_->caps();
    log("engine_ribbons_device device=%llu frame=%llu attached=%u reason=%s result=%08lx fp16_blending=%08lx vs_slots=%u ps_slots=%u max_vs_slots=%lu max_ps_slots=%lu vb_bytes=%u pool=%u samples=%u retry=reset",
        id_, frame_, unsigned(attached), attached ? "ok" : queried ? c.reason : "adapter_query", hr, c.fp16_blending,
        c.vs_slots, c.ps_slots, static_cast<unsigned long>(caps_.MaxVertexShader30InstructionSlots),
        static_cast<unsigned long>(caps_.MaxPixelShader30InstructionSlots), renderer::EngineRibbonsPass::vertex_bytes,
        engine_ribbons::max_ribbons, engine_ribbons::samples_per_ribbon);
    if (!attached) ribbons_attach_failed_ = true;
    return attached;
}
// The stage's second draw (run_engine_plumes, after the plumes): the pool follows this frame's records, then every
// drawable ribbon in one indexed draw. S_FALSE without a pass or with nothing drawable.
HRESULT MotionOutput::run_engine_ribbons(const renderer::EnginePlumesFrame& in) noexcept {
    ribbons_report_ = {};
    if (!ribbons_ || !ribbons_->caps().enabled) return S_FALSE;
    renderer::EngineRibbonsFrame f{};
    f.base = &in;
    f.seconds = engine_clock_.seconds; // the stage's clock (run_engine_plumes stepped it): a capture stall is not a gap
    f.cut = false; // note_cut at the resolve
    f.load_epoch = engine_load_epoch_;
    return ribbons_->run(f, &ribbons_report_);
}
// The fog law of this frame: on only when the density composite applied this frame (run_volumetric_fog runs before
// the resolve); the march's own extinction (family sigma x density_scale x ready_far x the look's sigma factor), the
// family's occupancy (the file row's, else the compiled table's) and chroma, the look's extinction tint and column.
void MotionOutput::engine_plumes_fog(renderer::FogTransmittanceLaw* law) noexcept {
    *law = renderer::FogTransmittanceLaw{};
    if (fog_density_applied_frame_ != frame_ || !fog_) return;
    const auto profile = static_cast<renderer::fog_field::Profile>(fog_sector_.profile);
    float occupancy = renderer::fog_compiled_occupancy(fog_sector_.profile);
    if (renderer::fog_field::is_file_profile(profile)) {
        const auto* table = renderer::fog_field::family_table();
        const auto* row = table ? table->row(profile) : nullptr;
        occupancy = row ? row->occupancy : 0.f;
    }
    const auto& look = fog_density_config_.look;
    const float sigma_eff = fog_density_config_.sigma * fog_sector_.density_scale * fog_->density_status().ready_far *
                            look.sigma_scale;
    renderer::fog_transmittance_law(sigma_eff, occupancy, fog_density_config_.chroma, look.extinction_tint, law);
    law->cap = look.sky_cap;
    law->start = look.taper_start <= look.sky_cap - 1000.f ? look.taper_start : .75f * look.sky_cap;
}
