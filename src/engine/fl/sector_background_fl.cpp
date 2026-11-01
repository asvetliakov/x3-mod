// FL edition of the sector-background reader (docs/architecture/farnhams-legacy-sector.md): X3FL.exe's
// layout for the shared value-only sector_background::sample() (src/proxy/sector_background.h, AP proof
// docs/reverse-engineering/sector-fog.md section 11). Read-only: no patch, no hook, no engine call. Every
// address is fl::module_base() + an RVA from the generated sector_background_fl_inc.h
// (docs/reverse-engineering/fl/sector-fog.md section 6); the four slots are admitted only after all
// generated anchors (whole instructions, relocated operands recomputed for the live base) match. The
// differences from AP are the four slot addresses, the view-distance field config +0x758 (AP +0x768) and
// the name copy that stops at its NUL (FL names are strlen+1 allocations). Called once per frame from
// capture.cpp's sector_background_context (render thread, inside the device call's CPU boundary).
#include "sector_background_fl.h"
#include "sector_background_fl_inc.h"
#include "../../proxy/config.h"
#include "../../proxy/engine_memory.h"
#include "executable_identity_fl.h"
#include <windows.h>
#include <atomic>
#include <initializer_list>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "Verified x86 image layout only");
namespace x3m::fl::sector_background_fl {
namespace {
namespace sb = x3m::sector_background;
// The shared reader's literal offsets (sector_background.h, capture.cpp's [sector+8]) must be X3FL.exe's.
static_assert(registry_map == 0 && registry_active_handle == 0x10 && map_buckets == 0 && map_capacity == 4 &&
                  link_next == 0 && link_key == 4 && link_value == 8,
              "cockpit registry map layout as in sample()");
static_assert(cockpit_ref_object == 0xc && cockpit_sector == 0x54 && cockpit_camera == 0x58 && object_id == 8 &&
                  object_class == 0x48 && object_parent == 0x54 && sector_class == 1,
              "cockpit and object fields as in sample() and walk_anchor()");
static_assert(sector_background_index == 0x13c && sector_stars == 0x140 && sector_nebula == 0x144,
              "sector fields as in sample()");
static_assert(record_stride == 0xdb8 && record_rebase == 0x44 && record_name == 0 && record_rates == 0xd0 &&
                  rate_count == 8 && record_dust == 0xf0 && record_fog_near == 0x104 && record_fog_far == 0x108 &&
                  record_stardust == 0x10c && record_span == 0x120,
              "TBackgrounds record as in read_record()");
static_assert(camera_flags == 0x270 && camera_fog_flag == 0x10000 && camera_fog_near == 0x36c &&
                  camera_fog_far == camera_fog_near + 4,
              "sector camera fields as in sample()");
static_assert(view_distance_high == 3 && view_distance_mid == 2 && far_floor_high <= 0x7fffffff &&
                  far_floor_mid <= 0x7fffffff,
              "the far-floor thresholds sample() applies");
constexpr std::uint32_t max_anchor_length = 128;
constexpr bool anchors_fit() {
    for (unsigned i = 0; i < anchor_count; ++i) {
        const Anchor& a = anchors[i];
        if (a.length > max_anchor_length || a.operands > 3) return false;
        for (unsigned k = 0; k < a.operands; ++k)
            if (a.operand_offset[k] + 4 > a.length) return false;
    }
    return true;
}
static_assert(anchors_fit(), "every anchor fits the check buffer, its operands inside its span");
constexpr std::uint32_t highest_rva() {
    std::uint32_t top = cockpit_registry_slot_rva;
    for (std::uint32_t v : {background_table_slot_rva, background_count_slot_rva, config_slot_rva})
        if (v > top) top = v;
    for (unsigned i = 0; i < anchor_count; ++i)
        if (anchors[i].rva + anchors[i].length > top) top = anchors[i].rva + anchors[i].length;
    return top;
}

// The layout sample() is instantiated with: the four slots are written once at admission, before `decided` is
// released, and never again.
struct FlLayout {
    static std::uint32_t registry, table, count, config;
    static constexpr std::uint32_t view_distance = config_view_distance;
    static constexpr std::int32_t far_floor_high = std::int32_t(sector_background_fl::far_floor_high),
                                  far_floor_mid = std::int32_t(sector_background_fl::far_floor_mid);
    static constexpr bool name_to_nul = true;
};
std::uint32_t FlLayout::registry = 0, FlLayout::table = 0, FlLayout::count = 0, FlLayout::config = 0;
std::atomic<const char*> state{"undecided"};
// Admission is decided on the first call and kept for the process; `decided` publishes the slots.
std::atomic<bool> decided{false};
const char* failed_anchor = nullptr;
// The periodic row: GetTickCount64 checked once per sample; counts since the previous row.
unsigned long long period_ms = 0, due_ms = 0;
unsigned long rows = 0, samples = 0, ready = 0;

bool read(std::uintptr_t address, void* out, std::size_t size) {
    return x3m::engine_memory::read(address, out, size);
}
// The span at base + rva holds the anchor's bytes with each relocated operand word equal to base + operand rva.
bool anchor_matches(std::uintptr_t base, const Anchor& a) {
    unsigned char actual[max_anchor_length];
    if (!read(base + a.rva, actual, a.length)) return false;
    for (unsigned k = 0; k < a.operands; ++k) {
        std::uint32_t operand = 0;
        std::memcpy(&operand, actual + a.operand_offset[k], 4);
        if (operand != std::uint32_t(base + a.operand_rva[k])) return false;
        std::memset(actual + a.operand_offset[k], 0, 4);
    }
    return std::memcmp(actual, a.bytes, a.length) == 0;
}
// X3M_FL_SECTOR_LOG (fl_sector_log): seconds between fl.sector_sample rows, 0 off, unset 10. Digits only, else
// the default.
unsigned log_seconds() {
    wchar_t text[8]{};
    const DWORD length = x3m::config::get(L"X3M_FL_SECTOR_LOG", text, 8);
    if (length == 0 || length >= 8) return 10;
    unsigned value = 0;
    for (DWORD i = 0; i < length; ++i) {
        if (text[i] < L'0' || text[i] > L'9') return 10;
        value = value * 10 + unsigned(text[i] - L'0');
    }
    return value > 3600 ? 3600 : value;
}
bool admit(std::uintptr_t base, bool identity) {
    const char* result = "ready";
    failed_anchor = nullptr;
    if (!identity) {
        result = "executable_mismatch";
    } else if (!base || base > UINT32_MAX - highest_rva()) {
        result = "base_overflow";
    } else {
        for (unsigned i = 0; i < anchor_count && !failed_anchor; ++i)
            if (!anchor_matches(base, anchors[i])) failed_anchor = anchors[i].name;
        if (failed_anchor) result = "anchor_mismatch";
    }
    const bool ok = result[0] == 'r';
    FlLayout::registry = ok ? std::uint32_t(base + cockpit_registry_slot_rva) : 0;
    FlLayout::table = ok ? std::uint32_t(base + background_table_slot_rva) : 0;
    FlLayout::count = ok ? std::uint32_t(base + background_count_slot_rva) : 0;
    FlLayout::config = ok ? std::uint32_t(base + config_slot_rva) : 0;
    const unsigned seconds = log_seconds();
    period_ms = 1000ull * seconds;
    due_ms = 0; // the first sample writes a row
    rows = samples = ready = 0;
    state.store(result, std::memory_order_relaxed);
    decided.store(true, std::memory_order_release);
    log("fl.sector status=%s base=0x%08lx registry_slot=0x%08lx table_slot=0x%08lx count_slot=0x%08lx "
        "config_slot=0x%08lx view_distance=+0x%x anchors=%u failed=%s log_s=%u",
        result, static_cast<unsigned long>(base), static_cast<unsigned long>(FlLayout::registry),
        static_cast<unsigned long>(FlLayout::table), static_cast<unsigned long>(FlLayout::count),
        static_cast<unsigned long>(FlLayout::config), unsigned(config_view_distance), anchor_count,
        failed_anchor ? failed_anchor : "none", seconds);
    return ok;
}
void sample_row(const sb::Sample& v) {
    ++samples;
    ready += v.status == sb::Status::Ready;
    if (!period_ms) return;
    const unsigned long long now = GetTickCount64();
    if (now < due_ms) return;
    due_ms = now + period_ms;
    log("fl.sector_sample row=%lu samples=%lu ready=%lu status=%s sector=%08x class48=%d index=%d count=%d "
        "name_valid=%u name=\"%s\" dust=%d near=%d far=%d stardust=%d config758=%d far_floor=%d effective_far=%d "
        "camera_check=%s anchor_check=%s walk=%s",
        ++rows, samples, ready, sb::name(v.status), v.sector, int(v.class48), v.index, v.count, unsigned(v.name_valid),
        v.family, v.dust, v.fog_near, v.fog_far, v.stardust, v.config, v.far_floor, v.effective_far,
        sb::name(v.camera_check), sb::name(v.anchor_check), sb::name(v.anchor_walk));
    samples = ready = 0;
}
} // namespace

bool admitted() {
    if (decided.load(std::memory_order_acquire)) return state.load(std::memory_order_relaxed)[0] == 'r';
    const DWORD error = GetLastError();
    const bool identity = x3m::fl::verified();
    const bool result = admit(identity ? x3m::fl::module_base() : 0, identity);
    SetLastError(error);
    return result;
}
const char* status() {
    return state.load(std::memory_order_relaxed);
}
bool sample(sb::Sample& out, unsigned walk) {
    if (!admitted()) return false;
    const DWORD error = GetLastError();
    x3m::engine_memory::revalidate(); // an epoch only: not a Present (engine_memory.h)
    auto reader = [](std::uintptr_t p, void* buffer, std::size_t n) { return x3m::engine_memory::read(p, buffer, n); };
    out = sb::sample<decltype(reader), FlLayout>(reader, walk);
    sample_row(out);
    SetLastError(error);
    return true;
}
#ifdef X3M_SECTOR_BACKGROUND_FIXTURE
bool fixture_admit(std::uintptr_t base) {
    decided.store(false, std::memory_order_relaxed);
    const DWORD error = GetLastError();
    const bool result = admit(base, true);
    SetLastError(error);
    return result;
}
const char* fixture_failed_anchor() {
    return failed_anchor;
}
void fixture_force_sample() {
    due_ms = 0;
}
#endif
} // namespace x3m::fl::sector_background_fl
