#pragma once
#include <cstdint>

namespace x3m {
// One sample at the frame boundary. A key held through focus changes or a
// modifier change cannot become a fresh press. No OS calls or draw-path work.
// The emitter keys (F4/F5/F6) only flip which prebuilt pixel-shader variant
// the per-draw path binds; they create nothing. F7 is the telemetry marker
// and F8 the capture key. F12 is the sun-shadow A/B: it flips one scene-end
// boolean (the cascade replay and the apply quad), nothing per draw. F11 is
// the fog shadow-pass A/B: it flips the grid variant the next owner latch
// hands to the fog pass, nothing per draw; Ctrl+Alt+F11 (Shift up, its own raw latch) is the fog dust motes' on/off.
struct ComparisonKeys {
    bool foreground = false, control = false, shift = false;
    bool exposure = false, bloom = false; // F9, F10
    bool screen_additive = false, source_gain = false; // F5, F6
    bool hull_gain = false; // F4
    bool sun_shadow = false; // F12
    bool fog_shadow_pass = false; // F11 (with --fog-shadow-pass on)
    bool fog_dust_motes = false; // F11 raw (with --fog-dust-motes): fires on Ctrl+Alt with Shift up, disjoint from Ctrl+Shift+F11
    bool fog_toggle = false, fog_step = false; // F9, F10 raw (with --volumetric-fog): fire on Ctrl+Alt with Shift up, disjoint from Ctrl+Shift+F9/F10
    bool alt = false; // polled with --fps-overlay only
    bool fps_overlay = false; // F7 (with --fps-overlay): fires on Ctrl+Alt with Shift up, disjoint from the Ctrl+Shift+F7 telemetry marker
    bool effects_stage = false; // F5 raw (with --effects-stage): fires on Ctrl+Alt with Shift up, disjoint from Ctrl+Shift+F5 (the additive bullets)
};
struct ComparisonActions {
    bool exposure = false, bloom = false;
    bool screen_additive = false, source_gain = false;
    bool hull_gain = false;
    bool sun_shadow = false;
    bool fog_shadow_pass = false;
    bool fog_dust_motes = false;
    bool fog_toggle = false, fog_step = false;
    bool fps_overlay = false;
    bool effects_stage = false;
};
class ComparisonControls {
public:
    ComparisonActions sample(const ComparisonKeys& keys) noexcept {
        ComparisonActions result{};
        if (!keys.foreground) { focused_ = false; return result; }
        if (!focused_) {
            focused_ = true; latch(keys);
            return result;
        }
        // Arm the chord in a previous foreground sample. This also rejects
        // the usual held chord on return if rendering paused while unfocused.
        if (modifiers_down_ && keys.control && keys.shift) {
            result.exposure = keys.exposure && !exposure_down_;
            result.bloom = keys.bloom && !bloom_down_;
            result.screen_additive = keys.screen_additive && !screen_additive_down_;
            result.source_gain = keys.source_gain && !source_gain_down_;
            result.hull_gain = keys.hull_gain && !hull_gain_down_;
            result.fog_shadow_pass = keys.fog_shadow_pass && !fog_shadow_pass_down_;
            result.sun_shadow = keys.sun_shadow && !sun_shadow_down_;
        }
        // The effects stage chord is Ctrl+Alt+F5 with Shift up (F3 is the game's target view), on F5's own raw latch
        // beside the Ctrl+Shift+F5 emitter key: a held F5 never becomes a press by changing modifiers.
        result.effects_stage = keys.control && keys.alt && !keys.shift && keys.effects_stage && !effects_stage_down_;
        // The overlay chord is Ctrl+Alt with Shift up, edged here outside the
        // Ctrl+Shift arm on the raw F7 latch (a held F7 never becomes a press
        // by changing modifiers); the focus latch above still applies.
        result.fps_overlay = keys.control && keys.alt && !keys.shift && keys.fps_overlay && !fps_overlay_down_;
        // The volumetric fog chords (Ctrl+Alt+F9 on/off, Ctrl+Alt+F10 strength step) under the same rule on their own raw latches.
        result.fog_toggle = keys.control && keys.alt && !keys.shift && keys.fog_toggle && !fog_toggle_down_;
        result.fog_step = keys.control && keys.alt && !keys.shift && keys.fog_step && !fog_step_down_;
        result.fog_dust_motes = keys.control && keys.alt && !keys.shift && keys.fog_dust_motes && !fog_dust_motes_down_;
        latch(keys);
        return result;
    }
    void reset_focus() noexcept { focused_ = false; }
    bool bloom_requested = true;
private:
    void latch(const ComparisonKeys& keys) noexcept {
        exposure_down_ = keys.exposure; bloom_down_ = keys.bloom;
        screen_additive_down_ = keys.screen_additive; source_gain_down_ = keys.source_gain; hull_gain_down_ = keys.hull_gain;
        sun_shadow_down_ = keys.sun_shadow; fog_shadow_pass_down_ = keys.fog_shadow_pass; fps_overlay_down_ = keys.fps_overlay; fog_toggle_down_ = keys.fog_toggle; fog_step_down_ = keys.fog_step;
        fog_dust_motes_down_ = keys.fog_dust_motes; effects_stage_down_ = keys.effects_stage;
        modifiers_down_ = keys.control && keys.shift;
    }
    bool focused_ = false, exposure_down_ = false, bloom_down_ = false, modifiers_down_ = false;
    bool screen_additive_down_ = false, source_gain_down_ = false, hull_gain_down_ = false, sun_shadow_down_ = false, fog_shadow_pass_down_ = false;
    bool fps_overlay_down_ = false, fog_toggle_down_ = false, fog_step_down_ = false, fog_dust_motes_down_ = false, effects_stage_down_ = false;
};
} // namespace x3m
