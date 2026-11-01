// FL edition of x3m::sun_light_poll (src/proxy/sun_light_poll.h): the engine's brightest directional light of
// X3FL.exe, read hook-free from the light-candidate array the collector (RVA 0x95450) rebuilds per rendered view
// (docs/reverse-engineering/fl/sun-light.md, "2026-11-01: poll contract"; docs/architecture/farnhams-legacy-sun-light.md).
// Read-only: no patch, no hook. Every address is fl::module_base() + RVA from the generated sun_light_poll_fl_inc.h;
// the context slot is admitted only after all 50 whole-instruction anchors match at the live base, each relocated
// disp32 recomputed for that base. Same Sample, Status names, selection rules and LastError discipline as the AP
// reader, with the FL node layout (colour +0x154, range +0x15c, record +0x170: a 0xc4-byte node read) and the
// engine's truncated score (the AP reader rounds). Nothing is kept across polls but counters; one fl.sun_light row
// at initialize and one fl.sun_light_sample row per X3M_FL_SUN_LIGHT_LOG seconds.
#include "../../proxy/sun_light_poll.h"
#include "../../proxy/config.h"
#include "../../proxy/engine_memory.h"
#include "executable_identity_fl.h"
#include <windows.h>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "Verified x86 image layout only");
namespace x3m::sun_light_poll {
namespace {
namespace layout {
#include "sun_light_poll_fl_inc.h"
}
using namespace layout;
static_assert(slot_count_offset == array_offset + 4 * array_words, "the slot count is the dword after the array");
static_assert(array_capacity + 1 == array_words, "254 entries and a terminator");
constexpr std::uint32_t node_block = node_position;                       // +0xb0
constexpr std::uint32_t node_block_size = node_record + 4 - node_block;   // +0xb0 .. +0x173
static_assert(node_block_size == 0xc4, "FL node read: 0xc4 bytes (AP 0xc0)");
static_assert(node_flags >= node_block && node_colour >= node_block && node_range >= node_block &&
                  node_flags + 4 <= node_record + 4 && node_colour + 6 <= node_record && node_range + 4 <= node_record,
              "every field inside the node read");
static_assert(context_slot_rva == 0x1aa100 && node_colour == 0x154 && node_range == 0x15c && node_record == 0x170,
              "FL layout, not AP's +0x150/+0x158/+0x16c");

std::uintptr_t context_slot = 0;
std::uintptr_t install_base = 0;
bool active = false;
Status state = Status::Disabled;
const char* reason = "none";         // why initialize refused (the fl.sun_light row)
std::uint32_t failed_anchor_rva = 0; // the first anchor that did not match

std::uint32_t word(const unsigned char* block, std::uint32_t offset) {
    std::uint32_t v = 0;
    std::memcpy(&v, block + (offset - node_block), 4);
    return v;
}
std::int32_t colour(const unsigned char* block, std::uint32_t offset) {
    std::int16_t v = 0;
    std::memcpy(&v, block + (offset - node_block), 2);
    return v;
}
bool flag(const wchar_t* name, bool fallback) {
    wchar_t setting[4]{};
    const DWORD n = x3m::config::get(name, setting, 4);
    return n == 1 ? setting[0] != L'0' : n == 0 ? fallback : true;
}
// Fault-free bounded read of this process (documented ReadProcessMemory on the current process): anchors only.
bool read_self(std::uintptr_t address, void* out, std::size_t size) {
    SIZE_T done = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &done) &&
           done == size;
}
// Every anchor at base: the whole instruction(s) with the relocated disp32 (if any) equal to base + target_rva.
// nullptr when all match, else the first mismatching anchor.
const SunLightAnchor* anchor_mismatch(std::uintptr_t base) {
    for (const SunLightAnchor& a : anchors) {
        unsigned char actual[sizeof a.bytes]{};
        if (a.length > sizeof a.bytes || !read_self(base + a.rva, actual, a.length)) return &a;
        if (a.reloc_offset != 0xff) {
            if (a.reloc_offset + 4u > a.length) return &a;
            std::uint32_t operand = 0;
            std::memcpy(&operand, actual + a.reloc_offset, 4);
            if (operand != std::uint32_t(base + a.target_rva)) return &a;
            std::memset(actual + a.reloc_offset, 0, 4);
        }
        if (std::memcmp(actual, a.bytes, a.length)) return &a;
    }
    return nullptr;
}
// The engine's score of 0x964c0 (FL 0x96523..0x96596): trunc((0.587 G + 0.299 R) + 0.114 B) in double precision,
// in that order (cvttsd2si), then + 0x300 for a directional node. A negative luma (no shipped light) counts as 0.
std::uint32_t truncated_luma(std::int32_t r, std::int32_t g, std::int32_t b) {
    const double sum = (double(g) * luma_green + double(r) * luma_red) + double(b) * luma_blue;
    const std::int32_t luma = std::int32_t(sum);
    return luma > 0 ? std::uint32_t(luma) : 0u;
}

// ---- diagnostics: one fl.sun_light_sample row per period, at the first poll after it elapses -------------
struct Diagnostic {
    long long period = 0, due = 0; // QPC ticks; period 0: off
    bool force = false;
    unsigned long reads = 0, by_status[8]{};
} diagnostic;
// X3M_FL_SUN_LIGHT_LOG (fl_sun_light_log): seconds between rows, 0 off, unset 10, at most 3600; digits only.
unsigned log_seconds() {
    wchar_t text[8]{};
    const DWORD length = x3m::config::get(L"X3M_FL_SUN_LIGHT_LOG", text, 8);
    if (length == 0 || length >= 8) return 10;
    unsigned value = 0;
    for (DWORD i = 0; i < length; ++i) {
        if (text[i] < L'0' || text[i] > L'9') return 10;
        value = value * 10 + unsigned(text[i] - L'0');
    }
    return value > 3600 ? 3600 : value;
}
void arm_diagnostic() {
    diagnostic = Diagnostic{};
    const unsigned seconds = log_seconds();
    LARGE_INTEGER frequency{}, now{};
    if (seconds && QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 && QueryPerformanceCounter(&now)) {
        diagnostic.period = frequency.QuadPart * static_cast<long long>(seconds);
        diagnostic.due = now.QuadPart; // the first poll logs
    }
}
std::uint32_t context_now() {
    std::uint32_t context = 0;
    if (context_slot) engine_memory::read(context_slot, &context, 4);
    return context;
}
void log_status() {
    log("fl.sun_light status=%s reason=%s base=0x%08lx slot=0x%08lx context=0x%08lx anchors=%u anchor_rva=0x%08lx "
        "log_s=%u",
        status_name(state), reason, static_cast<unsigned long>(install_base), static_cast<unsigned long>(context_slot),
        static_cast<unsigned long>(context_now()), anchor_count, static_cast<unsigned long>(failed_anchor_rva),
        log_seconds());
}
void log_sample(const Sample& s, std::uint32_t context) {
    ++diagnostic.reads;
    ++diagnostic.by_status[unsigned(s.status) & 7u];
    if (!diagnostic.period && !diagnostic.force) return;
    if (!diagnostic.force) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        if (now.QuadPart < diagnostic.due) return;
        diagnostic.due = now.QuadPart + diagnostic.period;
    }
    diagnostic.force = false;
    const unsigned long* n = diagnostic.by_status;
    log("fl.sun_light_sample status=%s reads=%lu ok=%lu unreadable=%lu null_context=%lu layout=%lu no_directional=%lu"
        " context=0x%08lx candidates=%u slot_admitted=%u admitted=%u engine_rule=%u rules_agree=%u score=0x%x"
        " second=0x%x flags=0x%08x pos=%d,%d,%d colour=%d,%d,%d admission_pos=%d,%d,%d record=%u"
        " record_pos=%.7g,%.7g,%.7g",
        status_name(s.status), diagnostic.reads, n[unsigned(Status::Ok)], n[unsigned(Status::Unreadable)],
        n[unsigned(Status::NullContext)], n[unsigned(Status::Layout)], n[unsigned(Status::NoDirectional)],
        static_cast<unsigned long>(context), s.candidates, s.slot_admitted, s.directional, unsigned(s.engine_rule),
        unsigned(s.rules_agree), s.score, s.second_score, s.flags, s.position[0], s.position[1], s.position[2],
        s.colour[0], s.colour[1], s.colour[2], s.admission_position[0], s.admission_position[1],
        s.admission_position[2], unsigned(s.record_valid), double(s.record_position[0]), double(s.record_position[1]),
        double(s.record_position[2]));
    diagnostic.reads = 0;
    std::memset(diagnostic.by_status, 0, sizeof diagnostic.by_status);
}
// initialize()'s gate after the identity: anchors, then the context slot readable. Leaves state/reason set.
bool admit(std::uintptr_t base) {
    install_base = base;
    failed_anchor_rva = 0;
    if (const SunLightAnchor* a = anchor_mismatch(base)) {
        state = Status::ExecutableMismatch;
        reason = "anchor";
        failed_anchor_rva = a->rva;
        return false;
    }
    std::uint32_t context = 0;
    if (!engine_memory::read(base + context_slot_rva, &context, 4)) {
        state = Status::SlotUnreadable;
        reason = "slot";
        return false;
    }
    context_slot = base + context_slot_rva;
    arm_diagnostic();
    active = true;
    state = Status::Ok;
    reason = "none";
    return true;
}
}
const char* status_name(Status status) {
    switch (status) {
    case Status::Ok: return "ok";
    case Status::Disabled: return "disabled";
    case Status::ExecutableMismatch: return "executable_mismatch";
    case Status::SlotUnreadable: return "slot_unreadable";
    case Status::NullContext: return "null_context";
    case Status::Unreadable: return "unreadable";
    case Status::Layout: return "layout";
    case Status::NoDirectional: return "no_directional";
    }
    return "?";
}
// The AP latch (X3M_MOTION_OUTPUT=1, X3M_SHADOW_REPLAY_DEPTH=1, a cascade list, X3M_SHADOW_SUN_POLL not 0), the
// X3FL.exe identity, the 50 anchors at the live base and a readable context slot. One fl.sun_light row.
bool initialize() {
    if (active) return true;
    const DWORD error = GetLastError();
    wchar_t list[8]{};
    const DWORD cascades = x3m::config::get(L"X3M_SHADOW_CASCADES", list, 8); // a longer list reports its length: set
    const bool requested = flag(L"X3M_MOTION_OUTPUT", false) && flag(L"X3M_SHADOW_REPLAY_DEPTH", false) &&
                           cascades != 0 && !(cascades == 1 && list[0] == L'0') && flag(L"X3M_SHADOW_SUN_POLL", true);
    if (!requested) {
        state = Status::Disabled;
        reason = "setting";
    } else if (!x3m::fl::verified()) {
        state = Status::ExecutableMismatch;
        reason = "identity";
    } else
        admit(x3m::fl::module_base());
    log_status();
    SetLastError(error);
    return active;
}
bool available() {
    return active;
}
const char* status() {
    return status_name(state);
}
// One poll: one 4-byte read of the slot, one 1,024-byte read of the array and the slot count, one 196-byte read
// per array entry, one 12-byte read of the chosen node's light record; one QPC for the row period. No allocation.
bool read(Sample* out) {
    if (!out) return false;
    *out = Sample{};
    if (!active) {
        out->status = state;
        return false;
    }
    const DWORD error = GetLastError();
    std::uint32_t context = 0;
    const auto finish = [&](Status s) {
        out->status = s;
        log_sample(*out, context);
        SetLastError(error);
        return s == Status::Ok;
    };
    if (!engine_memory::read(context_slot, &context, 4)) return finish(Status::Unreadable);
    if (!context) return finish(Status::NullContext);
    std::uint32_t entries[array_words + 1]{}; // the array and the slot count after it
    if (context > 0xffffffffu - array_offset || !engine_memory::read(context + array_offset, entries, sizeof entries))
        return finish(Status::Unreadable);
    if (entries[array_words] != slot_count_max) return finish(Status::Layout);
    std::uint32_t count = 0;
    while (count < array_words && entries[count]) ++count;
    if (count == array_words) return finish(Status::Layout); // no terminator
    out->candidates = count;
    struct Best {
        bool found = false;
        std::uint32_t score = 0, second = 0, node = 0, flags = 0, record = 0;
        std::uint64_t reach = 0;
        std::int32_t position[3]{}, rgb[3]{};
    };
    Best engine, admission;
    const auto offer = [](Best& best, std::uint32_t score, std::uint64_t reach, std::uint32_t node, std::uint32_t flags,
                          std::uint32_t record, const std::int32_t* position, const std::int32_t* rgb) {
        if (!best.found || score > best.score || (score == best.score && reach > best.reach)) {
            if (best.found) best.second = best.score;
            best.found = true;
            best.score = score;
            best.reach = reach;
            best.node = node;
            best.flags = flags;
            best.record = record;
            std::memcpy(best.position, position, 12);
            std::memcpy(best.rgb, rgb, 12);
        } else if (score > best.second)
            best.second = score;
    };
    for (std::uint32_t i = 0; i < count; ++i) {
        unsigned char block[node_block_size];
        if (entries[i] > 0xffffffffu - node_block - node_block_size ||
            !engine_memory::read(entries[i] + node_block, block, sizeof block))
            return finish(Status::Unreadable);
        const std::uint32_t flags = word(block, node_flags);
        if (!(flags & light_bit)) return finish(Status::Layout); // the collector admits lights only
        const bool directional = (flags & directional_bit) != 0,
                   slot = directional || std::int32_t(word(block, node_range)) > range_threshold,
                   admitted = !(flags & excluded_bits);
        if (!slot && !admitted) continue;
        const std::int32_t r = colour(block, node_colour), g = colour(block, node_colour + 2),
                           b = colour(block, node_colour + 4);
        const std::uint32_t luma = truncated_luma(r, g, b);
        const std::int32_t rgb[3] = {r, g, b};
        std::int32_t position[3];
        std::memcpy(position, block + (node_position - node_block), 12);
        std::uint64_t reach = 0;
        for (std::int32_t p : position) {
            const std::uint64_t a = p < 0 ? std::uint64_t(-std::int64_t(p)) : std::uint64_t(p);
            if (a > reach) reach = a;
        }
        const std::uint32_t record = word(block, node_record);
        if (slot) {
            ++out->slot_admitted;
            offer(engine, luma + (directional ? directional_bonus : 0u), reach, entries[i], flags, record, position,
                  rgb);
        }
        if (admitted) {
            ++out->directional;
            offer(admission, luma + directional_bonus, reach, entries[i], flags, record, position, rgb);
        }
    }
    if (!engine.found && !admission.found) return finish(Status::NoDirectional);
    const Best& best = engine.found ? engine : admission;
    out->engine_rule = engine.found;
    out->rules_agree = engine.found && admission.found && engine.node == admission.node;
    out->score = best.score;
    out->second_score = best.second;
    out->flags = best.flags;
    std::memcpy(out->position, best.position, 12);
    std::memcpy(out->colour, best.rgb, 12);
    if (admission.found) std::memcpy(out->admission_position, admission.position, 12);
    if (best.record && best.record <= 0xffffffffu - (record_position + 12u) &&
        engine_memory::read(best.record + record_position, out->record_position, 12))
        out->record_valid = true;
    return finish(Status::Ok);
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
void fixture_install(const std::uint32_t* slot) {
    context_slot = reinterpret_cast<std::uintptr_t>(slot);
    active = context_slot != 0;
    state = active ? Status::Ok : Status::Disabled;
    engine_memory::reset();
}
#endif
#if defined(X3M_FL_SUN_LIGHT_FIXTURE) && X3M_EDITION_FL
const char* fixture_initialize(std::uintptr_t base) {
    const DWORD error = GetLastError();
    active = false;
    context_slot = 0;
    engine_memory::reset();
    const bool ok = admit(base);
    log_status();
    SetLastError(error);
    return ok ? nullptr : reason;
}
void fixture_force_sample() {
    diagnostic.force = true;
}
#endif
}
