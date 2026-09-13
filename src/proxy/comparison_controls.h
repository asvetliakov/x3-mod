#pragma once
#include <cstdint>

namespace x3m {
// One sample at the frame boundary. A key held through focus changes or a
// modifier change cannot become a fresh press. No OS calls or draw-path work.
struct ComparisonKeys {
    bool foreground = false, control = false, shift = false;
    bool exposure = false, bloom = false;
};
struct ComparisonActions { bool exposure = false, bloom = false; };
class ComparisonControls {
public:
    ComparisonActions sample(const ComparisonKeys& keys) noexcept {
        ComparisonActions result{};
        if (!keys.foreground) { focused_ = false; return result; }
        if (!focused_) {
            focused_ = true;
            exposure_down_ = keys.exposure; bloom_down_ = keys.bloom;
            modifiers_down_ = keys.control && keys.shift;
            return result;
        }
        // Arm the chord in a previous foreground sample. This also rejects
        // the usual held chord on return if rendering paused while unfocused.
        if (modifiers_down_ && keys.control && keys.shift) {
            result.exposure = keys.exposure && !exposure_down_;
            result.bloom = keys.bloom && !bloom_down_;
        }
        exposure_down_ = keys.exposure; bloom_down_ = keys.bloom;
        modifiers_down_ = keys.control && keys.shift;
        return result;
    }
    void reset_focus() noexcept { focused_ = false; }
    bool bloom_requested = true;
private:
    bool focused_ = false, exposure_down_ = false, bloom_down_ = false, modifiers_down_ = false;
};
} // namespace x3m
