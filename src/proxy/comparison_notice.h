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
    void show(std::uint64_t now_ms) noexcept { until_ = now_ms + 3000; }
    void hide() noexcept { until_ = 0; }
    bool visible(std::uint64_t now_ms) const noexcept { return until_ && now_ms < until_; }
    void text(const char* first, const char* second) noexcept;
    ComparisonNoticeResult draw(IDirect3DDevice9* device, void* const* native,
                                unsigned target_count) noexcept;
private:
    static constexpr unsigned columns = 36, capacity = 2 * columns * 35;
    std::uint64_t until_ = 0;
    char lines_[2][columns + 1]{};
    D3DRECT pixels_[capacity]{};
    D3DRECT clipped_[capacity]{};
    unsigned count_ = 0, width_ = 0;
};
} // namespace x3m
