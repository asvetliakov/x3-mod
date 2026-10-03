// Engine heat shimmer (included by motion_output.cpp inside namespace x3m; engine_shimmer_core.h,
// renderer/engine_shimmer_pass.h; docs/architecture/engine-exhaust-gap-analysis.md gap 9, phase 5;
// effects-modernisation-opus.md section 3.8). With the plumes requested and X3M_ENGINE_SHIMMER on (the default), a frame
// whose plume stage drew gets a screen-space heat distortion behind its nearest nozzles: after the temporal resolve on
// the FP16 route (the resolved image exists; the HDR write-back and the bloom candidate have not read it), the scene
// view's records are turned into at most engine_shimmer_max (default 4, up to 16) rects by the plume builder itself (engine_shimmer::collect: nozzles under
// 24 px projected get none), the pass copies the resolved image over their union and draws the refraction into it.
// The resolved image is the TAA history: before Present the copy goes back (revert_engine_shimmer), so the next
// resolve never reads the shimmer. Ctrl+Alt+F7 turns it off and on per device (one engine_shimmer_toggle row).
// Fail closed: a refused attach or a failed run or revert logs one engine_shimmer_device / engine_shimmer_failed row and
// turns the shimmer off until Reset (a failed revert also restarts the TAA history: invalidate site engine_shimmer);
// nothing else changes. --debug: one engine_shimmer row at the engine_frame cadence.
void MotionOutput::configure_engine_shimmer() noexcept {
    const DWORD error = GetLastError();
    bool on = true;
    float px = engine_shimmer::default_px;
    unsigned limit = engine_shimmer::default_max;
    wchar_t word[16]{}, amount[16]{}, most[16]{};
    const DWORD n = x3m::config::get(L"X3M_ENGINE_SHIMMER", word, 16);
    const DWORD pn = x3m::config::get(L"X3M_ENGINE_SHIMMER_PX", amount, 16);
    const DWORD mn = x3m::config::get(L"X3M_ENGINE_SHIMMER_MAX", most, 16);
    char shown[16]{}, px_shown[16]{}, max_shown[16]{};
    for (DWORD i = 0; i < n && i < 15; ++i) shown[i] = word[i] > 0x20 && word[i] < 0x7f ? char(word[i]) : '?';
    for (DWORD i = 0; i < pn && i < 15; ++i) px_shown[i] = amount[i] > 0x20 && amount[i] < 0x7f ? char(amount[i]) : '?';
    for (DWORD i = 0; i < mn && i < 15; ++i) max_shown[i] = most[i] > 0x20 && most[i] < 0x7f ? char(most[i]) : '?';
    // Unset = on, 1.5 px and 4 rects; anything else refused (the default, status invalid_setting or too_long).
    const bool ok = !n || (n < 16 && engine_shimmer::parse_mode(word, n, &on));
    if (!ok) on = true;
    const bool px_ok = !pn || (pn < 16 && engine_shimmer::parse_px(amount, pn, &px));
    if (!px_ok) px = engine_shimmer::default_px;
    const bool max_ok = !mn || (mn < 16 && engine_shimmer::parse_max(most, mn, &limit));
    if (!max_ok) limit = engine_shimmer::default_max;
    shimmer_px_ = px;
    shimmer_max_ = limit;
    shimmer_requested_ = plumes_requested_ && on && px > 0.f && limit > 0;
    shimmer_on_ = true;
    shimmer_attach_failed_ = shimmer_failed_ = false;
    shimmer_frame_ = ~std::uint64_t(0);
    if (plumes_requested_ || n || pn || mn)
        log("engine_shimmer_config requested=%u plumes=%u setting=%s status=%s px=%.3f px_setting=%s px_status=%s max=%u max_setting=%s max_status=%s gate_px=%.0f toggle=ctrl+alt+f7",
            unsigned(shimmer_requested_), unsigned(plumes_requested_), n ? shown : "-",
            ok ? "ok" : n >= 16 ? "too_long" : "invalid_setting", double(px), pn ? px_shown : "-",
            px_ok ? "ok" : pn >= 16 ? "too_long" : "invalid_setting", limit, mn ? max_shown : "-",
            max_ok ? "ok" : mn >= 16 ? "too_long" : "invalid_setting", double(engine_shimmer::gate_px));
    SetLastError(error);
}
int MotionOutput::engine_shimmer_toggle() noexcept {
    if (!shimmer_requested_) return -1;
    shimmer_on_ = !shimmer_on_;
    log("engine_shimmer_toggle device=%llu frame=%llu on=%u failed=%u source=hotkey", id_, frame_, unsigned(shimmer_on_),
        unsigned(shimmer_failed_ || shimmer_attach_failed_));
    return shimmer_on_ ? 1 : 0;
}
// Attached once per device at the first frame with rects (inside the resolve's taa_call: its reference accounting
// covers the programs); refused until Reset with one engine_shimmer_device row.
bool MotionOutput::attach_engine_shimmer() noexcept {
    if (shimmer_attach_failed_) return false;
    if (!shimmer_) {
        shimmer_.reset(new (std::nothrow) renderer::EngineShimmerPass);
        if (!shimmer_) {
            shimmer_attach_failed_ = true;
            log("engine_shimmer_device device=%llu frame=%llu attached=0 reason=allocation retry=reset", id_, frame_);
            return false;
        }
    }
    if (shimmer_->caps().enabled) return true;
    D3DDISPLAYMODE display{};
    HRESULT hr = native<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &display);
    const bool queried = SUCCEEDED(hr);
    if (queried) {
        if (taa_busy_)
            hr = shimmer_->attach(device_, native_, caps_, display.Format);
        else
            taa_call([&] { hr = shimmer_->attach(device_, native_, caps_, display.Format); });
    }
    const bool attached = queried && SUCCEEDED(hr) && shimmer_->caps().enabled;
    const auto& c = shimmer_->caps();
    log("engine_shimmer_device device=%llu frame=%llu attached=%u reason=%s result=%08lx fp16_filter=%08lx ps_slots=%u max_ps_slots=%lu retry=reset",
        id_, frame_, unsigned(attached), attached ? "ok" : queried ? c.reason : "adapter_query", hr, c.fp16_filter, c.ps_slots,
        static_cast<unsigned long>(caps_.MaxPixelShader30InstructionSlots));
    if (!attached) shimmer_attach_failed_ = true;
    return attached;
}
void MotionOutput::fail_engine_shimmer(const char* step, HRESULT hr) noexcept {
    if (!shimmer_failed_)
        log("engine_shimmer_failed device=%llu frame=%llu step=%s result=%08lx pass_step=%u calls=%u retry=reset", id_, frame_,
            step, hr, unsigned(shimmer_report_.failed), shimmer_report_.calls);
    shimmer_failed_ = true;
}
void MotionOutput::release_engine_shimmer() noexcept {
    if (shimmer_) {
        taa_call([&] { shimmer_->detach(); });
        shimmer_.reset();
    }
}
// A frame that drew no shimmer: the scratch (an FP16 copy at the target's size, 59 MB at 5120x1440) goes at once when the
// shimmer is toggled off, and after shimmer_idle_limit (300) consecutive frames without a drawn shimmer (no nozzle
// qualified); the next drawn frame recreates it. Never while a revert is pending. Inside the resolve's taa_call, whose
// reference accounting covered the creation.
void MotionOutput::idle_engine_shimmer(bool off) noexcept {
    if (shimmer_idle_frames_ < shimmer_idle_limit) ++shimmer_idle_frames_;
    if (!shimmer_ || !shimmer_->scratch_width() || shimmer_->revert_pending()) return;
    if (!off && shimmer_idle_frames_ < shimmer_idle_limit) return;
    shimmer_->release_scratch();
    if (!shimmer_->scratch_width()) ++shimmer_scratch_releases_;
}
// Reset: the block and the scratch go (the target, the TAA history, goes with the pass's own before_reset); a refusal or
// a failure is retried after it.
void MotionOutput::engine_shimmer_before_reset() noexcept {
    if (shimmer_) taa_call([&] { shimmer_->before_reset(); });
    shimmer_attach_failed_ = shimmer_failed_ = false;
    shimmer_idle_frames_ = 0;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if (fixture_shimmer_probe_.stage >= 2 && fixture_shimmer_probe_.stage < 5) fixture_shimmer_probe_ = {}; // the texture goes
#endif
}
void MotionOutput::engine_shimmer_after_reset(HRESULT result) noexcept {
    if (shimmer_) shimmer_->after_reset(result);
}
void MotionOutput::run_engine_shimmer(IDirect3DTexture9* output, IDirect3DSurface9* output_surface, IDirect3DTexture9* lane,
                                      UINT width, UINT height) noexcept {
    if (!shimmer_requested_) return;
    shimmer_frame_ = frame_;
    shimmer_report_ = {};
    shimmer_stats_ = {};
    shimmer_us_ = 0.f;
    // Only while the plume stage drew this frame (the producer is on screen), on, and not failed until Reset; the
    // engine_shimmer row's skipped= says which.
    const char* idle = !shimmer_on_                                    ? "off"
                       : shimmer_failed_ || shimmer_attach_failed_      ? "failed"
                       : !plumes_armed_ || !plumes_ran_ || !plumes_report_.drew || !engine_ring_ || !engine_ring_->count
                           ? "no_plumes"
                       : !output || !output_surface || !width || !height ? "output"
                       : !camera_scene_.valid                            ? "camera"
                                                                         : nullptr;
    if (idle) {
        shimmer_report_.skipped = idle;
        idle_engine_shimmer(!shimmer_on_);
        return;
    }
    LARGE_INTEGER begin{}, end{};
    QueryPerformanceCounter(&begin);
    // The plume stage's inputs (motion_output_engine_plumes_inc.h run_engine_plumes): the scene camera's view rows, the
    // scene view's records, the preset, the look, the stage's clock and its dynamics (the flow accumulator and the SETA
    // travel weight, advanced by this frame's stage; its per-nozzle memory read for the phases, never written).
    engine_plumes::View view{};
    for (unsigned j = 0; j < 3; ++j) {
        view.rows[j * 4] = camera_scene_.r[j];
        view.rows[j * 4 + 1] = camera_scene_.r[3 + j];
        view.rows[j * 4 + 2] = camera_scene_.r[6 + j];
        view.rows[j * 4 + 3] = camera_scene_.t[j];
    }
    view.m00 = camera_scene_.m00;
    view.m11 = camera_scene_.m11;
    view.height = float(height);
    engine_shimmer::Projection projection{};
    projection.m00 = camera_scene_.m00;
    projection.m11 = camera_scene_.m11;
    projection.m20 = camera_scene_.m20; // unjittered: the resolved image is on the history's grid
    projection.m21 = camera_scene_.m21;
    projection.m22 = camera_scene_.m22 != 0.f ? camera_scene_.m22 : projection_default_m22;
    projection.m32 = camera_scene_.m32 != 0.f ? camera_scene_.m32 : projection_default_m32;
    projection.width = float(width);
    projection.height = float(height);
    view.near_z = -projection.m32 / projection.m22;
    if (!(view.near_z > 0.f) || !(view.near_z < 1e30f)) view.near_z = -projection_default_m32 / projection_default_m22;
    std::uint32_t scene_camera = 0;
    engine_plumes::ViewRule rule = engine_plumes::ViewRule::none;
    if (!engine_plumes::scene_view_camera(engine_ring_->camera, engine_ring_->scene, engine_ring_->own, engine_ring_->count,
                                          &scene_camera, &rule)) {
        shimmer_report_.skipped = "no_scene_view";
        idle_engine_shimmer(false);
        return;
    }
    engine_plumes::ViewFilter filter{};
    filter.camera = engine_ring_->camera;
    filter.scene = engine_ring_->scene;
    filter.handle = scene_camera;
    float seconds = 0.f;
    engine_clock_.wrapped(&seconds);
    engine_plumes::Dynamics dynamics{};
    dynamics.flow = engine_flow_.nozzle_widths;
    dynamics.travel = engine_travel_weight_;
    dynamics.transients = engine_transients_.get(); // read only: each rect's nozzle phase as the stage advanced it
    const unsigned count = engine_shimmer::collect(engine_ring_->records, engine_ring_->count, &engine_plumes_body, view,
                                                   projection, plumes_preset_, seconds, &filter, &plumes_look_,
                                                   &plumes_tables_, engine_ring_->parent_radius, shimmer_rects_,
                                                   &shimmer_stats_, shimmer_max_, &dynamics);
    HRESULT hr = S_FALSE;
    const bool attached = count && attach_engine_shimmer();
    if (!attached) shimmer_report_.skipped = count ? "attach" : "no_rects";
    if (attached) {
        renderer::EngineShimmerFrame f{};
        f.width = width;
        f.height = height;
        f.target = output;
        f.target_surface = output_surface;
        f.lane = lane;
        f.rects = shimmer_rects_;
        f.rect_count = count;
        engine_shimmer::amplitude_px(shimmer_px_, float(height), &f.amplitude_px);
        f.seconds = seconds;
        f.caller_scene_open = scene_open_;
        f.caller_stateblock_recording = shadow_.recording;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
        const bool probing = fixture_shimmer_probe_.stage == 1;
        const std::uint64_t pre = probing ? fixture_shimmer_hash(output) : 0;
#endif
        hr = shimmer_->run(f, &shimmer_report_);
#ifdef X3M_MOTION_OUTPUT_FIXTURE
        if (probing && shimmer_report_.drew) {
            auto& p = fixture_shimmer_probe_;
            p.pre = pre;
            p.drawn = fixture_shimmer_hash(output);
            p.texture = output;
            p.frame = frame_;
            p.rects = count;
            p.stage = 2;
        }
#endif
        if (shimmer_report_.stale_revert) ++shimmer_stale_reverts_;
        if (FAILED(shimmer_report_.restore)) invalidate_render_states();
        if (FAILED(hr) && hr != D3DERR_DEVICELOST && hr != D3DERR_DEVICENOTRESET) fail_engine_shimmer("run", hr);
    }
    if (shimmer_report_.drew)
        shimmer_idle_frames_ = 0;
    else
        idle_engine_shimmer(false);
    QueryPerformanceCounter(&end);
    const std::uint64_t frequency = engine_qpc_frequency();
    shimmer_us_ = frequency ? float(double(end.QuadPart - begin.QuadPart) * 1e6 / double(frequency)) : 0.f;
}
// Before Present (after the write-back, the bloom candidate and the compositor have read the resolved image): the
// history gets its unshimmered rect back. A failed revert leaves the displaced image in the history: the history
// restarts (invalidate site engine_shimmer) and the shimmer is off until Reset.
void MotionOutput::revert_engine_shimmer() noexcept {
    shimmer_revert_ = S_FALSE;
    shimmer_revert_us_ = 0.f;
    if (!shimmer_ || !shimmer_->revert_pending()) return;
    LARGE_INTEGER begin{}, end{};
    QueryPerformanceCounter(&begin);
    shimmer_revert_ = shimmer_->revert();
    QueryPerformanceCounter(&end);
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if (fixture_shimmer_probe_.stage == 2 && shimmer_revert_ == S_OK) {
        fixture_shimmer_probe_.reverted = fixture_shimmer_hash(fixture_shimmer_probe_.texture);
        fixture_shimmer_probe_.stage = 3;
    }
#endif
    const std::uint64_t frequency = engine_qpc_frequency();
    shimmer_revert_us_ = frequency ? float(double(end.QuadPart - begin.QuadPart) * 1e6 / double(frequency)) : 0.f;
    if (FAILED(shimmer_revert_) && shimmer_revert_ != D3DERR_DEVICELOST && shimmer_revert_ != D3DERR_DEVICENOTRESET) {
        invalidate_taa(TaaInvalidateSite::EngineShimmer);
        fail_engine_shimmer("revert", shimmer_revert_);
    }
}
// --debug, at the engine_frame cadence (after engine_effects_frame_end: a frame with a candidate, or its quiet row). A
// frame whose resolve did not reach the shimmer (no successful FP16 resolve this frame) says skipped=no_resolve.
void MotionOutput::log_engine_shimmer() noexcept {
    if (!shimmer_requested_ || !engine_census_ || !log_tier::cached_debug) return;
    if (!engine_counts_.candidates && engine_quiet_frame_ != frame_) return;
    const bool ran = shimmer_frame_ == frame_;
    const engine_shimmer::Stats s = ran ? shimmer_stats_ : engine_shimmer::Stats{};
    const renderer::EngineShimmerReport r = ran ? shimmer_report_ : renderer::EngineShimmerReport{};
    log("engine_shimmer device=%llu frame=%llu on=%u failed=%u ran=%u rects=%u candidates=%u small=%u behind=%u refused=%u offscreen=%u capped=%u scissor_px=%u copy_px=%u drew=%u px=%.2f result=%08lx step=%u skipped=%s calls=%u cpu_us=%.1f revert=%08lx revert_us=%.1f stale_reverts=%u scratch=%u scratch_releases=%u",
        id_, frame_, unsigned(shimmer_on_), unsigned(shimmer_failed_ || shimmer_attach_failed_), unsigned(ran), s.kept,
        s.candidates, s.small, s.behind, s.refused, s.offscreen, s.capped, r.scissor_px, r.copy_px, unsigned(r.drew),
        double(shimmer_px_), r.operation, unsigned(r.failed), r.skipped ? r.skipped : ran ? "-" : "no_resolve", r.calls,
        double(ran ? shimmer_us_ : 0.f), shimmer_revert_, double(shimmer_revert_us_), shimmer_stale_reverts_,
        unsigned(shimmer_ && shimmer_->scratch_width()), shimmer_scratch_releases_);
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
// The two-frame history probe (engine_effects_fixture.cpp "armed"; x3m_engine_shimmer_fixture_probe / _status): armed,
// it hashes the resolved image before and after the next drawn shimmer and after that frame's revert (frame N), then
// the same texture before the next resolve and that resolve's history use (frame N+1). Keys: 0 the stage (5 done), 1..4
// the low 32 bits of the hashes (pre, drawn, reverted, history), 5..8 the equalities pre == reverted, pre != drawn,
// reverted == history, hashes read, 9 frame N+1's resolve used history, 10 its output is the other texture (the
// history it read is frame N's), 11 frame N's rects, 12 the frames between N and N+1's resolve.
std::uint64_t MotionOutput::fixture_shimmer_hash(IDirect3DTexture9* texture) noexcept {
    if (!texture) return 0;
    D3DSURFACE_DESC desc{};
    IDirect3DSurface9* level = nullptr;
    IDirect3DSurface9* copy = nullptr;
    std::uint64_t h = 0;
    HRESULT hr = texture->GetLevelDesc(0, &desc);
    if (SUCCEEDED(hr)) hr = texture->GetSurfaceLevel(0, &level);
    if (SUCCEEDED(hr))
        hr = native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, desc.Width, desc.Height, desc.Format,
                                                                    D3DPOOL_SYSTEMMEM, &copy, nullptr);
    if (SUCCEEDED(hr)) hr = native<GetRtDataFn>(GetRenderTargetData)(device_, level, copy);
    D3DLOCKED_RECT lock{};
    if (SUCCEEDED(hr)) hr = copy->LockRect(&lock, nullptr, D3DLOCK_READONLY);
    if (SUCCEEDED(hr)) {
        const unsigned bytes = desc.Format == D3DFMT_A16B16G16R16F ? 8u : 4u;
        h = 1469598103934665603ull;
        for (UINT y = 0; y < desc.Height; ++y) {
            const auto* row = static_cast<const unsigned char*>(lock.pBits) + std::size_t(y) * lock.Pitch;
            for (std::size_t x = 0; x < std::size_t(desc.Width) * bytes; ++x) h = (h ^ row[x]) * 1099511628211ull;
        }
        copy->UnlockRect();
        ++fixture_shimmer_probe_.hashed;
    }
    release(copy);
    release(level);
    return h;
}
void MotionOutput::fixture_shimmer_probe(bool arm) noexcept {
    fixture_shimmer_probe_ = {};
    fixture_shimmer_probe_.stage = arm ? 1u : 0u;
}
unsigned MotionOutput::fixture_shimmer_status(unsigned key) const noexcept {
    const auto& p = fixture_shimmer_probe_;
    switch (key) {
    case 0: return p.stage;
    case 1: return unsigned(p.pre);
    case 2: return unsigned(p.drawn);
    case 3: return unsigned(p.reverted);
    case 4: return unsigned(p.history);
    case 5: return p.stage >= 3 && p.pre == p.reverted ? 1u : 0u;
    case 6: return p.stage >= 3 && p.pre != p.drawn ? 1u : 0u;
    case 7: return p.stage >= 4 && p.reverted == p.history ? 1u : 0u;
    case 8: return p.hashed;
    case 9: return p.used_history;
    case 10: return p.other_target;
    case 11: return p.rects;
    case 12: return unsigned(p.next_frame - p.frame);
    }
    return 0;
}
// The resolve's hooks: before taa_->run (frame N+1: frame N's image as the resolve finds it) and after it.
void MotionOutput::fixture_shimmer_before_resolve() noexcept {
    auto& p = fixture_shimmer_probe_;
    if (p.stage != 3) return;
    p.history = fixture_shimmer_hash(p.texture);
    p.stage = 4;
}
void MotionOutput::fixture_shimmer_after_resolve(HRESULT hr, bool used_history, IDirect3DTexture9* color) noexcept {
    auto& p = fixture_shimmer_probe_;
    if (p.stage != 4) return;
    p.used_history = SUCCEEDED(hr) && used_history ? 1u : 0u;
    p.other_target = SUCCEEDED(hr) && color && color != p.texture ? 1u : 0u;
    p.next_frame = frame_;
    p.stage = 5;
}
#endif
