// FL edition of x3m::compositor_owner (src/proxy/compositor_owner.h; the ap target keeps
// src/proxy/compositor_owner.cpp): the IDirect3DDevice9 X3FL.exe's compositor draws with, read as the compositor
// itself selects it (docs/reverse-engineering/fl/compositor-owner.md sections 1 and 4): renderer =
// *(base + renderer_slot_rva), record = [renderer + 0x18], device = [record]. Three fault-free reads of the own
// process (ReadProcessMemory), no manager word: FL's minimal owner contract carries none. The offsets come from
// the generated scene_hook_fl_inc.h, whose owner and device anchors the FL scene hook checks at the live base
// before it admits that base here; until then every read refuses. Also the compositor's own glow test
// (config = *(base + config_slot_rva), byte +0xf0, bit 0x80), which capture.cpp's bridge consults instead of
// AP's +0x100 byte. Called from capture.cpp (begin_scene, the compositor bridge's pre/post) under the capture
// mutex or from the bridge's full CPU boundary; LastError preserved.
#include "../../proxy/compositor_owner.h"
#include <windows.h>
#include <atomic>

static_assert(sizeof(void*) == 4, "The compositor owner layout is x86 only");
namespace {
namespace layout {
#include "scene_hook_fl_inc.h"
}
static_assert(layout::record_offset == 0x18 && layout::device_offset == 0, "compositor-owner.md section 4");
std::atomic<std::uintptr_t> admitted{0};
std::atomic<unsigned long> reads_ok{0}, reads_failed{0};
std::atomic<unsigned> last_failure{0};
bool read_self(std::uint32_t address, void* out, std::size_t size) noexcept {
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &copied) &&
           copied == size;
}
x3m::compositor_owner::Result collect(std::uintptr_t base, x3m::compositor_owner::Snapshot& out) noexcept {
    using x3m::compositor_owner::Result;
    out = {};
    constexpr std::uint32_t last = UINT32_MAX;
    if (!base || base != admitted.load(std::memory_order_acquire) || base > last - layout::renderer_slot_rva - 3u)
        return Result::InvalidAddress;
    x3m::compositor_owner::Snapshot value{};
    if (!read_self(static_cast<std::uint32_t>(base) + layout::renderer_slot_rva, &value.renderer, 4))
        return Result::Unreadable;
    if (!value.renderer || value.renderer > last - layout::record_offset - 3u) return Result::InvalidAddress;
    if (!read_self(value.renderer + layout::record_offset, &value.record, 4)) return Result::Unreadable;
    if (!value.record || value.record > last - 3u) return Result::InvalidAddress;
    if (!read_self(value.record + layout::device_offset, &value.device, 4)) return Result::Unreadable;
    if (!value.device) return Result::OwnerMismatch; // the record exists, CreateDevice has not published (or retired)
    out = value;
    return Result::Ok;
}
}
namespace x3m::compositor_owner {
void admit(std::uintptr_t base) noexcept {
    admitted.store(base, std::memory_order_release);
}
Result read(std::uintptr_t base, Snapshot& out) noexcept {
    const DWORD error = GetLastError();
    const Result result = collect(base, out);
    if (result == Result::Ok)
        reads_ok.fetch_add(1, std::memory_order_relaxed);
    else {
        reads_failed.fetch_add(1, std::memory_order_relaxed);
        last_failure.store(unsigned(result), std::memory_order_relaxed);
    }
    SetLastError(error);
    return result;
}
bool glow_enabled(std::uintptr_t base) noexcept {
    const DWORD error = GetLastError();
    std::uint32_t settings = 0;
    unsigned char flags = 0;
    const bool on = base && base == admitted.load(std::memory_order_acquire) &&
                    base <= UINT32_MAX - layout::config_slot_rva - 3u &&
                    read_self(static_cast<std::uint32_t>(base) + layout::config_slot_rva, &settings, 4) && settings &&
                    settings <= UINT32_MAX - layout::glow_flag_offset &&
                    read_self(settings + layout::glow_flag_offset, &flags, 1) && (flags & layout::glow_flag_mask);
    SetLastError(error);
    return on;
}
ReadCounts take_read_counts() noexcept {
    ReadCounts counts{};
    counts.ok = reads_ok.exchange(0, std::memory_order_relaxed);
    counts.failed = reads_failed.exchange(0, std::memory_order_relaxed);
    counts.last_failure = counts.failed ? Result(last_failure.load(std::memory_order_relaxed)) : Result::Ok;
    return counts;
}
const char* result_name(Result result) noexcept {
    switch (result) {
    case Result::Ok: return "none";
    case Result::InvalidAddress: return "invalid_address";
    case Result::Unreadable: return "unreadable";
    case Result::OwnerMismatch: return "no_device";
    }
    return "unknown";
}
} // namespace x3m::compositor_owner
