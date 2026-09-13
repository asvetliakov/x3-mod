#pragma once
#include <cstddef>
#include <cstdint>

// Exact native owner of the verified game's compositor, not a camera/view
// register or a device selected by target size. Call only after executable and
// callsite admission. See docs/reverse-engineering/bloom-invocation-owner.md.
namespace x3m::compositor_owner {
struct Snapshot {
    std::uint32_t renderer = 0, record = 0, device = 0;
    std::uint32_t manager = 0, manager_device = 0;
};
enum class Result : unsigned { Ok, InvalidAddress, Unreadable, OwnerMismatch };

// One synchronous observation, not a lifetime pin or cross-thread exclusion.
// All addresses are game x86 addresses. Output is empty on any failure.
// Preserves LastError; the caller's CPU bridge preserves registers/FP state.
Result read(std::uintptr_t admitted_image_base, Snapshot& out) noexcept;
inline bool same(const Snapshot& a, const Snapshot& b) noexcept {
    return a.renderer && a.record && a.manager && a.device
        && a.device == a.manager_device && b.device == b.manager_device
        && a.renderer == b.renderer && a.record == b.record
        && a.device == b.device && a.manager == b.manager;
}

namespace detail {
// Reader must copy exactly size bytes or return false. Kept platform-neutral
// so fixtures exercise the production lookup with hostile synthetic memory.
template<class Reader>
Result collect(std::uintptr_t base, Snapshot& out, Reader&& reader) noexcept {
    out = {};
    constexpr std::uint32_t last = UINT32_MAX;
    constexpr std::uint32_t renderer_rva = 0x208b3c;
    if (!base || base > last - renderer_rva - 3u) return Result::InvalidAddress;
    Snapshot value{};
    if (!reader(static_cast<std::uint32_t>(base) + renderer_rva,
                &value.renderer, sizeof(value.renderer))) return Result::Unreadable;
    // Read the adjacent record/manager fields together (four reads total).
    if (!value.renderer || value.renderer > last - 0x1fu) return Result::InvalidAddress;
    std::uint32_t fields[2]{};
    if (!reader(value.renderer + 0x18u, fields, sizeof(fields))) return Result::Unreadable;
    value.record = fields[0]; value.manager = fields[1];
    if (!value.record || value.record > last - 3u
        || !value.manager || value.manager > last - 7u) return Result::InvalidAddress;
    if (!reader(value.record, &value.device, sizeof(value.device))
        || !reader(value.manager + 4u, &value.manager_device, sizeof(value.manager_device)))
        return Result::Unreadable;
    if (!value.device || value.device != value.manager_device) return Result::OwnerMismatch;
    out = value;
    return Result::Ok;
}
} // namespace detail
} // namespace x3m::compositor_owner
