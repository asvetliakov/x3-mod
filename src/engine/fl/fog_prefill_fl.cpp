// FL edition of the fog transit prefill walk (docs/architecture/farnhams-legacy-prefill.md): X3FL.exe's layout for
// the shared value-only fog_prefill::walk() (src/proxy/fog_prefill.h; AP proof docs/reverse-engineering/
// sector-transit-order.md section 5, FL proof docs/reverse-engineering/fl/fog-prefill.md section 7). Read-only: no
// patch, no hook, no engine call. Every address is fl::module_base() + an RVA from the generated
// fog_prefill_fl_inc.h; the slots are admitted only after all generated anchors (whole instructions, relocated
// operands recomputed for the live base) match. The differences from AP are the four slot addresses, the record's
// NUL-stopped name copy (sector_background_fl's) and one read of the allocator's recovery guard before the walk: a
// resource callback inside allocation recovery can see a linked, typed, alive sector whose scene and index are not
// yet initialised (fog-prefill.md section 7.2, answer 4), so a nonzero guard refuses the poll. Called from
// capture.cpp's fog_prefill_poll (the CreateTexture/CreateVertexBuffer wrappers, render thread, during a stall).
#include "fog_prefill_fl.h"
#include "fog_prefill_fl_inc.h"
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
namespace x3m::fl::fog_prefill_fl {
namespace {
namespace fp = x3m::fog_prefill;
namespace sbf = x3m::fl::sector_background_fl;
// The shared walk's literal offsets (fog_prefill.h) must be X3FL.exe's.
static_assert(list_header == 8 && header_tailpred == 8 && list_tailpred == list_header + header_tailpred &&
                  link_next == 0 && link_prev == 4,
              "walk(): tailpred at M+0x10, head sentinel M+8, prev link +4");
static_assert(object_class == 0x48 && object_subtype == object_class + 2 && sector_class == 1,
              "walk(): one dword at +0x48, class word 1 and subtype word 0 above it");
static_assert(object_marker == 0x9c && alive_marker == 0xcafe && dead_marker != alive_marker && object_id == 8,
              "walk(): marker +0x9c == 0xcafe, id +8");
static_assert(sector_scene == 0x130 && sector_background_index == sector_scene + 12,
              "walk(): one 0x20-byte read at +0x130, the index its dword 3");
static_assert(guard_set != 0 && guard_clear == 0, "walk(): a nonzero guard is inside allocation recovery");
// The record recipe and the shared slots are sector_background_fl's (the count slot is anchored there).
static_assert(record_stride == sbf::record_stride && sbf::record_stride == 0xdb8 && sbf::record_rebase == 0x44,
              "read_record(): stride 0xdb8, +0x44 rebase");
static_assert(background_count_slot_rva == sbf::background_count_slot_rva &&
                  background_table_slot_rva == sbf::background_table_slot_rva &&
                  object_manager_slot_rva == sbf::object_manager_slot_rva && config_slot_rva == sbf::config_slot_rva,
              "the prefill and sector generators agree on the shared slots");
// AP's budget (kReadBudget) plus the guard read, with the record name copied one byte per read (at most 32).
constexpr unsigned read_budget = 1 + (2 + 2 * fp::kMaxSteps - 1 + 3 + 3) + 32;
static_assert(read_budget == 56, "guard, manager, tailpred, 8 types and 7 links, marker, block, id, count, table, "
                                 "row and a 32-byte name");
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
    std::uint32_t top = object_manager_slot_rva;
    for (std::uint32_t v : {background_count_slot_rva, background_table_slot_rva, recovery_guard_slot_rva})
        if (v > top) top = v;
    for (unsigned i = 0; i < anchor_count; ++i)
        if (anchors[i].rva + anchors[i].length > top) top = anchors[i].rva + anchors[i].length;
    return top;
}

// The layout walk() is instantiated with: the four slots are written once at admission, before `decided` is
// released, and never again. Record: read_record's layout, the NUL-stopped name of sector_background_fl.
struct RecordLayout {
    static constexpr bool name_to_nul = true;
};
struct FlLayout {
    static std::uint32_t manager, count, table, guard;
    static constexpr bool guarded = true;
    using Record = RecordLayout;
};
std::uint32_t FlLayout::manager = 0, FlLayout::count = 0, FlLayout::table = 0, FlLayout::guard = 0;
std::atomic<const char*> state{"undecided"};
// Admission is decided on the first call and kept for the process; `decided` publishes the slots.
std::atomic<bool> decided{false};
const char* failed_anchor = nullptr;
// The periodic row: GetTickCount64 checked once per walk; counts since the previous row.
unsigned long long period_ms = 0, due_ms = 0;
unsigned long rows = 0, polls = 0, found = 0, guarded = 0;

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
// X3M_FL_PREFILL_LOG (fl_prefill_log): seconds between fl.prefill_sample rows, 0 off, unset 10. Digits only, else
// the default.
unsigned log_seconds() {
    wchar_t text[8]{};
    const DWORD length = x3m::config::get(L"X3M_FL_PREFILL_LOG", text, 8);
    if (length == 0 || length >= 8) return 10;
    unsigned value = 0;
    for (DWORD i = 0; i < length; ++i) {
        if (text[i] < L'0' || text[i] > L'9') return 10;
        value = value * 10 + unsigned(text[i] - L'0');
    }
    return value > 3600 ? 3600 : value;
}
// `refusal`: nullptr to check the base and the anchors, else the identity or sector refusal to record.
bool admit(std::uintptr_t base, const char* refusal) {
    const char* result = "ready";
    failed_anchor = nullptr;
    if (refusal) {
        result = refusal;
    } else if (!base || base > UINT32_MAX - highest_rva()) {
        result = "base_overflow";
    } else {
        for (unsigned i = 0; i < anchor_count && !failed_anchor; ++i)
            if (!anchor_matches(base, anchors[i])) failed_anchor = anchors[i].name;
        if (failed_anchor) result = "anchor_mismatch";
    }
    const bool ok = result[0] == 'r'; // "ready"; every refusal starts with another letter
    FlLayout::manager = ok ? std::uint32_t(base + object_manager_slot_rva) : 0;
    FlLayout::count = ok ? std::uint32_t(base + background_count_slot_rva) : 0;
    FlLayout::table = ok ? std::uint32_t(base + background_table_slot_rva) : 0;
    FlLayout::guard = ok ? std::uint32_t(base + recovery_guard_slot_rva) : 0;
    const unsigned seconds = log_seconds();
    period_ms = 1000ull * seconds;
    due_ms = 0; // the first walk writes a row
    rows = polls = found = guarded = 0;
    state.store(result, std::memory_order_relaxed);
    decided.store(true, std::memory_order_release);
    log("fl.prefill status=%s base=0x%08lx manager_slot=0x%08lx count_slot=0x%08lx table_slot=0x%08lx "
        "guard_slot=0x%08lx anchors=%u failed=%s log_s=%u",
        result, static_cast<unsigned long>(base), static_cast<unsigned long>(FlLayout::manager),
        static_cast<unsigned long>(FlLayout::count), static_cast<unsigned long>(FlLayout::table),
        static_cast<unsigned long>(FlLayout::guard), anchor_count, failed_anchor ? failed_anchor : "none", seconds);
    return ok;
}
void walk_row(const fp::Result& w) {
    ++polls;
    found += w.status == fp::Walk::Found;
    guarded += w.status == fp::Walk::Guarded;
    if (!period_ms) return;
    const unsigned long long now = GetTickCount64();
    if (now < due_ms) return;
    due_ms = now + period_ms;
    log("fl.prefill_sample row=%lu polls=%lu found=%lu guarded=%lu walk=%s steps=%u reads=%u node=%08x id=%u "
        "index=%d count=%d name_valid=%u name=\"%s\" near=%d far=%d",
        ++rows, polls, found, guarded, fp::name(w.status), w.steps, w.reads, w.node, w.id, w.sample.index,
        w.sample.count, unsigned(w.sample.name_valid), w.sample.family, w.sample.fog_near, w.sample.fog_far);
    polls = found = guarded = 0;
}
} // namespace

bool admitted() {
    if (decided.load(std::memory_order_acquire)) return state.load(std::memory_order_relaxed)[0] == 'r';
    const DWORD error = GetLastError();
    const char* refusal = !x3m::fl::verified() ? "executable_mismatch" : !sbf::admitted() ? "sector_refused" : nullptr;
    const bool result = admit(refusal ? 0 : x3m::fl::module_base(), refusal);
    SetLastError(error);
    return result;
}
const char* status() {
    return state.load(std::memory_order_relaxed);
}
bool walk(fp::Result& out, std::uint32_t last_ready_id) {
    if (!admitted()) return false;
    const DWORD error = GetLastError();
    x3m::engine_memory::revalidate(); // the stall reallocates: an epoch only, not a Present (engine_memory.h)
    auto reader = [](std::uintptr_t p, void* buffer, std::size_t n) { return x3m::engine_memory::read(p, buffer, n); };
    out = fp::walk<decltype(reader), FlLayout>(reader, last_ready_id);
    walk_row(out);
    SetLastError(error);
    return true;
}
#ifdef X3M_FOG_PREFILL_FIXTURE
bool fixture_admit(std::uintptr_t base) {
    decided.store(false, std::memory_order_relaxed);
    const DWORD error = GetLastError();
    const bool result = admit(base, nullptr);
    SetLastError(error);
    return result;
}
const char* fixture_failed_anchor() {
    return failed_anchor;
}
void fixture_force_row() {
    due_ms = 0;
}
#endif
} // namespace x3m::fl::fog_prefill_fl
