#pragma once
#include <d3d9.h>
#include <cstdint>

namespace x3m {
struct ComparisonNoticeResult {
    HRESULT operation = S_FALSE, restore = S_OK;
    bool drawn = false;
};
// A small opaque, two-line bitmap notice. No GPU objects, state blocks, font
// dependencies or allocations. Clear touches only the final backbuffer RGB(A).
// The caller owns frame/scene/query admission and pins the device through all
// native calls and Release callbacks. See comparison-hotkeys.md.
class ComparisonNotice {
public:
    // top: the panel's first row. The default is the hotkey notice; the FPS
    // overlay sits one panel lower (72) so both can be on screen at once.
    explicit ComparisonNotice(LONG top = 16) noexcept : top_(top) {}
    void show(std::uint64_t now_ms) noexcept { until_ = now_ms + 3000; }
    void hide() noexcept { until_ = 0; }
    bool visible(std::uint64_t now_ms) const noexcept { return until_ && now_ms < until_; }
    void text(const char* first, const char* second) noexcept;
    ComparisonNoticeResult draw(IDirect3DDevice9* device, void* const* native,
                                unsigned target_count) noexcept;
    // Clip passes so far: the rectangles are clipped once per (text, target
    // size) and reused by every later draw, so a shown frame with unchanged
    // text costs the Clear calls only.
    unsigned clip_passes() const noexcept { return clip_passes_; }
private:
    static constexpr unsigned columns = 36, capacity = 2 * columns * 35;
    LONG top_ = 16;
    std::uint64_t until_ = 0;
    char lines_[2][columns + 1]{};
    D3DRECT pixels_[capacity]{};
    D3DRECT clipped_[capacity]{};
    D3DRECT panel_{};
    unsigned count_ = 0, width_ = 0;
    unsigned clipped_count_ = 0, clipped_width_ = 0, clipped_height_ = 0, clip_passes_ = 0;
};
} // namespace x3m
