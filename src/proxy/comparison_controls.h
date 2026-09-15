#pragma once
#include <cstdint>

namespace x3m {
// One sample at the frame boundary. A key held through focus changes or a
// modifier change cannot become a fresh press. No OS calls or draw-path work.
// The emitter keys (F5/F6/F4) only flip which prebuilt pixel-shader variant
// the per-draw path binds; they create nothing. F7 is the telemetry marker
// and F8 the capture key, so the effect family took the next free key below.
struct ComparisonKeys {
    bool foreground = false, control = false, shift = false;
    bool exposure = false, bloom = false, ambient_occlusion = false; // F9, F10, F11
    bool screen_additive = false, engine_gain = false, effect_gain = false; // F5, F6, F4
};
struct ComparisonActions {
    bool exposure = false, bloom = false, ambient_occlusion = false;
    bool screen_additive = false, engine_gain = false, effect_gain = false;
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
            result.ambient_occlusion = keys.ambient_occlusion && !ambient_occlusion_down_;
            result.screen_additive = keys.screen_additive && !screen_additive_down_;
            result.engine_gain = keys.engine_gain && !engine_gain_down_;
            result.effect_gain = keys.effect_gain && !effect_gain_down_;
        }
        latch(keys);
        return result;
    }
    void reset_focus() noexcept { focused_ = false; }
    bool bloom_requested = true;
private:
    void latch(const ComparisonKeys& keys) noexcept {
        exposure_down_ = keys.exposure; bloom_down_ = keys.bloom; ambient_occlusion_down_ = keys.ambient_occlusion;
        screen_additive_down_ = keys.screen_additive; engine_gain_down_ = keys.engine_gain; effect_gain_down_ = keys.effect_gain;
        modifiers_down_ = keys.control && keys.shift;
    }
    bool focused_ = false, exposure_down_ = false, bloom_down_ = false, ambient_occlusion_down_ = false, modifiers_down_ = false;
    bool screen_additive_down_ = false, engine_gain_down_ = false, effect_gain_down_ = false;
};
} // namespace x3m
