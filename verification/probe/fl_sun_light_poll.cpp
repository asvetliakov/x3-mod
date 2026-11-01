// Original synthetic fixture of the FL sun light poll (src/engine/fl/sun_light_poll_fl.cpp,
// docs/architecture/farnhams-legacy-sun-light.md): no game code, no game bytes, no launch.
// Built with -DX3M_EDITION_FL=1 -DX3M_FL_SUN_LIGHT_FIXTURE (verification/analysis/test_fl_sun_light_poll.py).
//
// A synthetic image at the base VirtualAlloc picks holds the 50 generated anchors with every relocated disp32
// written for that base, and the context slot at base + context_slot_rva. The slot names a fake render context
// whose light array (R+0x5e8c), slot count (R+0x6288) and nodes are written per case. Each node is a 0x200-byte
// block with the FL fields (flags +0x12c, position +0xb0, colour +0x154, range +0x15c, record +0x170).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "../../src/proxy/sun_light_poll.h"
#include "../../src/proxy/engine_memory.h"
namespace layout {
#include "../../src/engine/fl/sun_light_poll_fl_inc.h"
}
namespace sp = x3m::sun_light_poll;
unsigned checks = 0, failures = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}
char last_row[4096];
unsigned rows = 0;
namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(last_row, sizeof last_row, format, args);
    va_end(args);
    ++rows;
    std::printf("LOG %s\n", last_row);
}
}
bool row_has(const char* text) {
    return std::strstr(last_row, text) != nullptr;
}

unsigned char* image = nullptr; // the synthetic image
uintptr_t image_base = 0;
SIZE_T image_span = 0;
// Writes every anchor with its relocated operand naming (operand_base + target_rva).
void write_anchors(uintptr_t operand_base) {
    for (const auto& a : layout::anchors) {
        std::memcpy(image + a.rva, a.bytes, a.length);
        if (a.reloc_offset != 0xff) {
            const uint32_t operand = uint32_t(operand_base + a.target_rva);
            std::memcpy(image + a.rva + a.reloc_offset, &operand, 4);
        }
    }
}
bool make_image() {
    image_span = (layout::context_slot_rva + 0x2000) & ~SIZE_T(0xfff);
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_span, MEM_RESERVE, PAGE_NOACCESS));
    if (!image) return false;
    image_base = reinterpret_cast<uintptr_t>(image);
    bool ok = true;
    for (const auto& a : layout::anchors)
        ok = ok && VirtualAlloc(image + (a.rva & ~0xfffu), 0x2000, MEM_COMMIT, PAGE_READWRITE);
    ok = ok && VirtualAlloc(image + (layout::context_slot_rva & ~0xfffu), 0x1000, MEM_COMMIT, PAGE_READWRITE);
    return ok;
}
void set_slot(uint32_t context) {
    std::memcpy(image + layout::context_slot_rva, &context, 4);
}

// The anchor gate: accepted at its base, refused for a foreign base, refused for every single-byte flip outside a
// relocated operand, slot_unreadable for a decommitted slot page.
void gate_cases() {
    write_anchors(image_base);
    set_slot(0);
    check(sp::fixture_initialize(image_base) == nullptr && sp::available(), "relocated synthetic image admitted");
    check(row_has("fl.sun_light status=ok reason=none") && row_has("anchors=50"), "fl.sun_light status row");
    // The same bytes relocated for another base: every relocated anchor names a foreign slot.
    write_anchors(image_base + 0x10000);
    const char* foreign = sp::fixture_initialize(image_base);
    check(foreign && !std::strcmp(foreign, "anchor") && !sp::available() &&
              !std::strcmp(sp::status(), "executable_mismatch"),
          "image relocated for a foreign base refused");
    write_anchors(image_base);
    check(sp::fixture_initialize(image_base + 0x10000) != nullptr, "the image checked at another base refused");
    unsigned flips = 0, refused = 0;
    for (const auto& a : layout::anchors)
        for (unsigned i = 0; i < a.length; ++i) {
            if (a.reloc_offset != 0xff && i >= a.reloc_offset && i < a.reloc_offset + 4u) continue;
            write_anchors(image_base);
            image[a.rva + i] ^= 0x01;
            ++flips;
            refused += sp::fixture_initialize(image_base) != nullptr;
        }
    std::printf("ANCHOR flips=%u refused=%u\n", flips, refused);
    check(flips > 0 && refused == flips, "every single-byte flip of every anchor refused");
    write_anchors(image_base);
    check(VirtualFree(image + (layout::context_slot_rva & ~0xfffu), 0x1000, MEM_DECOMMIT) != 0, "slot page decommitted");
    const char* slot = sp::fixture_initialize(image_base);
    check(slot && !std::strcmp(slot, "slot") && !std::strcmp(sp::status(), "slot_unreadable"),
          "unreadable context slot refused");
    check(VirtualAlloc(image + (layout::context_slot_rva & ~0xfffu), 0x1000, MEM_COMMIT, PAGE_READWRITE) != nullptr,
          "slot page recommitted");
}

// ---- the fake context and nodes -------------------------------------------------------------------------
unsigned char* context = nullptr; // 0x7000 bytes: array at +0x5e8c, slot count at +0x6288
unsigned char* nodes = nullptr;   // node i at nodes + 0x200 i
unsigned char* records = nullptr; // light records, record i at records + 0x40 i
constexpr unsigned node_stride = 0x200, max_nodes = 260;
uint32_t addr(const void* p) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}
unsigned char* node(unsigned i) {
    return nodes + node_stride * i;
}
void write_node(unsigned char* n, uint32_t flags, int32_t x, int32_t y, int32_t z, int16_t r, int16_t g, int16_t b,
                int32_t range, uint32_t record) {
    std::memset(n + layout::node_position, 0, layout::node_record + 4 - layout::node_position);
    const int32_t position[3] = {x, y, z};
    const int16_t rgb[3] = {r, g, b};
    std::memcpy(n + layout::node_flags, &flags, 4);
    std::memcpy(n + layout::node_position, position, 12);
    std::memcpy(n + layout::node_colour, rgb, 6);
    std::memcpy(n + layout::node_range, &range, 4);
    std::memcpy(n + layout::node_record, &record, 4);
}
// The array holds the given node addresses then a terminator; slot count as given.
void set_array(const uint32_t* entries, unsigned count, uint32_t slot_count = 8) {
    std::memset(context + layout::array_offset, 0, 4 * layout::array_words);
    std::memcpy(context + layout::array_offset, entries, 4 * count);
    std::memcpy(context + layout::slot_count_offset, &slot_count, 4);
}
bool poll(sp::Sample* s) {
    x3m::engine_memory::next_frame(); // a Present between polls: every region revalidated, as in flight
    SetLastError(0x13572468);
    const bool ok = sp::read(s);
    check(GetLastError() == 0x13572468, "LastError preserved across read");
    return ok;
}
bool position_is(const int32_t* p, int32_t x, int32_t y, int32_t z) {
    return p[0] == x && p[1] == y && p[2] == z;
}
constexpr uint32_t L = layout::light_bit, D = layout::directional_bit, P = layout::point_bit,
                   S = layout::spot_bit;
void selection_cases() {
    sp::Sample s;
    set_slot(addr(context));
    // A: one dim directional light against two bright short-range point lights.
    write_node(node(0), L | P, 1, 2, 3, 30000, 30000, 30000, 1000, 0);
    write_node(node(1), L | D, -500000, 7, 9, 100, 50, 20, 0, addr(records));
    write_node(node(2), L | S, 4, 5, 6, 32000, 32000, 32000, 2000, 0);
    const float rec[3] = {1.5f, -2.25f, 3.125f};
    std::memcpy(records + layout::record_position, rec, 12);
    {
        const uint32_t e[] = {addr(node(0)), addr(node(1)), addr(node(2))};
        set_array(e, 3);
    }
    check(poll(&s) && s.status == sp::Status::Ok, "A: ok");
    check(position_is(s.position, -500000, 7, 9) && s.engine_rule && s.rules_agree, "A: directional chosen");
    check(s.score == 0x300u + 61u && s.second_score == 0, "A: score trunc(61.53) + 0x300");
    check(s.candidates == 3 && s.slot_admitted == 1 && s.directional == 1, "A: counts");
    check(s.colour[0] == 100 && s.colour[1] == 50 && s.colour[2] == 20 && s.flags == (L | D), "A: colour, flags");
    check(s.record_valid && !std::memcmp(s.record_position, rec, 12), "A: record position");
    std::printf("CASE A status=%s score=0x%x candidates=%u slot_admitted=%u admitted=%u\n", sp::status_name(s.status),
                s.score, s.candidates, s.slot_admitted, s.directional);
    // B: no directional; the range threshold admits a long-range point light, not one at the threshold.
    write_node(node(0), L | P, 11, 12, 13, 30000, 30000, 30000, layout::range_threshold, 0);
    write_node(node(1), L | P, 21, 22, 23, 200, 180, 160, layout::range_threshold + 1, 0);
    write_node(node(2), L | S, 31, 32, 33, 32000, 32000, 32000, 1000, 0);
    check(poll(&s) && position_is(s.position, 21, 22, 23) && s.engine_rule && !s.rules_agree,
          "B: long-range point light chosen by the engine rule");
    check(s.score == 183u && s.slot_admitted == 1 && s.directional == 0, "B: score trunc(183.7), no bonus");
    std::printf("CASE B status=%s score=0x%x slot_admitted=%u admitted=%u\n", sp::status_name(s.status), s.score,
                s.slot_admitted, s.directional);
    // C: truncation decides. (0,2,0) = 1.174 -> 1; (3,0,0) = 0.897 -> 0 (rounded both 1: the farther would win);
    // (128,128,128) = 127.99999999999999 -> 127 (rounded 128).
    write_node(node(0), L | D, 10, 0, 0, 0, 2, 0, 0, 0);
    write_node(node(1), L | D, 900000, 0, 0, 3, 0, 0, 0, 0);
    {
        const uint32_t e[] = {addr(node(0)), addr(node(1))};
        set_array(e, 2);
    }
    check(poll(&s) && position_is(s.position, 10, 0, 0) && s.score == 0x301u && s.second_score == 0x300u,
          "C: truncated score ranks (0,2,0) over the farther (3,0,0)");
    std::printf("CASE C1 score=0x%x second=0x%x\n", s.score, s.second_score);
    write_node(node(0), L | D, 10, 0, 0, 128, 128, 128, 0, 0);
    write_node(node(1), L | D, 20, 0, 0, 0, 1, 0, 0, 0);
    check(poll(&s) && s.score == 0x300u + 127u && s.second_score == 0x300u, "C: (128,128,128) scores 127, (0,1,0) 0");
    std::printf("CASE C2 score=0x%x second=0x%x\n", s.score, s.second_score);
    // D: nothing in the Dir slot (no 0x800000 yet, short range): the admission rule's winner, + 0x300.
    write_node(node(0), L, 41, 42, 43, 40, 40, 40, 1000, 0);
    write_node(node(1), L | P, 51, 52, 53, 30000, 30000, 30000, 1000, 0);
    write_node(node(2), L, 61, 62, 63, 10, 10, 10, 1000, 0);
    {
        const uint32_t e[] = {addr(node(0)), addr(node(1)), addr(node(2))};
        set_array(e, 3);
    }
    check(poll(&s) && !s.engine_rule && position_is(s.position, 41, 42, 43) &&
              position_is(s.admission_position, 41, 42, 43),
          "D: admission winner when the engine rule admits nothing");
    check(s.score == 0x300u + 40u && s.second_score == 0x300u + 10u && s.slot_admitted == 0 && s.directional == 2,
          "D: admission scores and counts");
    std::printf("CASE D status=%s engine_rule=%u score=0x%x second=0x%x\n", sp::status_name(s.status),
                unsigned(s.engine_rule), s.score, s.second_score);
    // E: a negative range word is not above the threshold (signed compare): a bright point light with range -1 or
    // INT32_MIN stays out of the Dir slot and the dim directional light wins. Unsigned, both would be admitted.
    write_node(node(0), L | P, 71, 72, 73, 30000, 30000, 30000, -1, 0);
    write_node(node(1), L | D, 81, 82, 83, 100, 50, 20, 0, 0);
    write_node(node(2), L | S, 91, 92, 93, 32000, 32000, 32000, INT32_MIN, 0);
    {
        const uint32_t e[] = {addr(node(0)), addr(node(1)), addr(node(2))};
        set_array(e, 3);
    }
    check(poll(&s) && position_is(s.position, 81, 82, 83) && s.slot_admitted == 1 && s.score == 0x300u + 61u,
          "E: negative range words not admitted by the threshold");
    std::printf("CASE E status=%s slot_admitted=%u score=0x%x pos=%d,%d,%d\n", sp::status_name(s.status),
                s.slot_admitted, s.score, s.position[0], s.position[1], s.position[2]);
    // F: equal scores: the farther node wins in either array order, under both rules.
    for (unsigned order = 0; order < 2; ++order) {
        write_node(node(order), L | D, 10, -20, 30, 100, 100, 100, 0, 0);
        write_node(node(1 - order), L | D, 5, -900000, 7, 100, 100, 100, 0, 0);
        const uint32_t e[] = {addr(node(0)), addr(node(1))};
        set_array(e, 2);
        check(poll(&s) && position_is(s.position, 5, -900000, 7) && position_is(s.admission_position, 5, -900000, 7) &&
                  s.score == s.second_score && s.score == 0x300u + 100u && s.rules_agree,
              "F: tie goes to the farther node");
        std::printf("CASE F order=%u score=0x%x second=0x%x pos=%d,%d,%d\n", order, s.score, s.second_score,
                    s.position[0], s.position[1], s.position[2]);
    }
    // No light admitted by either rule.
    write_node(node(0), L | P, 1, 1, 1, 100, 100, 100, 1000, 0);
    {
        const uint32_t e[] = {addr(node(0))};
        set_array(e, 1);
    }
    check(!poll(&s) && s.status == sp::Status::NoDirectional, "no admitted light: no_directional");
    std::printf("CASE none status=%s\n", sp::status_name(s.status));
}
void refusal_cases() {
    sp::Sample s;
    write_node(node(0), L | D, 1, 2, 3, 100, 100, 100, 0, 0);
    write_node(node(1), L, 4, 5, 6, 50, 50, 50, 0, 0);
    const uint32_t e[] = {addr(node(0)), addr(node(1))};
    for (uint32_t count : {0u, 7u, 9u, 0xffffffffu}) {
        set_array(e, 2, count);
        check(!poll(&s) && s.status == sp::Status::Layout, "slot count != 8 refused");
        std::printf("CASE slot_count=%u status=%s\n", count, sp::status_name(s.status));
    }
    set_array(e, 2);
    check(poll(&s), "slot count 8 accepted");
    write_node(node(1), D, 4, 5, 6, 50, 50, 50, 0, 0); // no light bit
    check(!poll(&s) && s.status == sp::Status::Layout, "a non-light entry refused");
    std::printf("CASE non_light status=%s\n", sp::status_name(s.status));
    write_node(node(1), L, 4, 5, 6, 50, 50, 50, 0, 0);
    uint32_t full[layout::array_words];
    for (unsigned i = 0; i < layout::array_words; ++i) full[i] = addr(node(i % 2));
    set_array(full, layout::array_words);
    check(!poll(&s) && s.status == sp::Status::Layout, "unterminated array (255 entries) refused");
    std::printf("CASE unterminated status=%s\n", sp::status_name(s.status));
    set_array(full, layout::array_capacity); // 254 entries and the terminator: accepted
    check(poll(&s) && s.candidates == 254, "254 entries accepted");
    set_array(e, 2);
    // An unreadable context, a null context, an unreadable node.
    auto* blocked = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x8000, MEM_RESERVE, PAGE_NOACCESS));
    set_slot(addr(blocked));
    check(!poll(&s) && s.status == sp::Status::Unreadable, "unreadable context refused");
    std::printf("CASE context_unreadable status=%s\n", sp::status_name(s.status));
    set_slot(0);
    check(!poll(&s) && s.status == sp::Status::NullContext, "null context");
    set_slot(addr(context));
    const uint32_t bad[] = {addr(node(0)), addr(blocked)};
    set_array(bad, 2);
    check(!poll(&s) && s.status == sp::Status::Unreadable, "unreadable node refused");
    VirtualFree(blocked, 0, MEM_RELEASE);
}
// Nothing read outside +0xb0..+0x173: a node whose +0xb0 starts a committed page and whose +0x174 ends it is read;
// moved one byte either way it is refused (the read would cross into the reserved, inaccessible neighbour).
void bound_cases() {
    sp::Sample s;
    const uint32_t span = layout::node_record + 4 - layout::node_position;
    std::printf("NODE_SPAN 0x%x\n", span);
    check(span == 0xc4, "node span 0xc4");
    auto* region = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x3000, MEM_RESERVE, PAGE_NOACCESS));
    check(region && VirtualAlloc(region + 0x1000, 0x1000, MEM_COMMIT, PAGE_READWRITE), "bound region");
    if (!region) return;
    unsigned char* page = region + 0x1000;
    // span == 0xc4 < 0x1000: place the node block at the page's start, then at its end.
    unsigned char* low = page - layout::node_position;           // +0xb0 is the page's first byte
    unsigned char* high = page + 0x1000 - (layout::node_record + 4); // +0x173 is the page's last byte
    unsigned tested = 0, accepted = 0, refused = 0;
    for (unsigned char* n : {low, high, low - 1, high + 1}) {
        const bool inside = n == low || n == high;
        if (inside) {
            std::memset(page, 0, 0x1000);
            write_node(n, L | D, 7, 8, 9, 60, 60, 60, 0, 0);
        } else
            write_node(n == low - 1 ? low : high, L | D, 7, 8, 9, 60, 60, 60, 0, 0); // fields readable; n's span is not
        const uint32_t e[] = {addr(n)};
        set_array(e, 1);
        const bool ok = poll(&s);
        ++tested;
        if (inside) {
            accepted += ok && position_is(s.position, 7, 8, 9);
        } else
            refused += !ok && s.status == sp::Status::Unreadable;
    }
    std::printf("BOUND tested=%u accepted=%u refused=%u\n", tested, accepted, refused);
    check(accepted == 2 && refused == 2, "node read is exactly +0xb0..+0x173");
    VirtualFree(region, 0, MEM_RELEASE);
}
void row_case() {
    sp::Sample s;
    write_node(node(0), L | D, -1, -2, -3, 100, 50, 20, 0, addr(records));
    const uint32_t e[] = {addr(node(0))};
    set_array(e, 1);
    sp::fixture_force_sample();
    const unsigned before = rows;
    check(poll(&s) && rows == before + 1, "forced fl.sun_light_sample row");
    check(row_has("fl.sun_light_sample status=ok reads=") && row_has(" candidates=1 slot_admitted=1 admitted=1 "
                                                                    "engine_rule=1 rules_agree=1 score=0x33d second=0x0 "
                                                                    "flags=0x00800004 pos=-1,-2,-3 colour=100,50,20 ") &&
              row_has(" record=1 record_pos=1.5,-2.25,3.125"),
          "fl.sun_light_sample fields");
    check(poll(&s) && rows == before + 1, "no row before the period");
}
double ns_per_poll(unsigned candidates, bool cold, unsigned iterations) {
    uint32_t e[layout::array_capacity];
    for (unsigned i = 0; i < candidates; ++i) {
        write_node(node(i), i == candidates / 2 ? L | D : L | P, int32_t(i), 0, 0, 100, 100, 100, 1000,
                   i == candidates / 2 ? addr(records) : 0);
        e[i] = addr(node(i));
    }
    set_array(e, candidates);
    sp::Sample s;
    x3m::engine_memory::next_frame();
    sp::read(&s);
    LARGE_INTEGER f{}, t0{}, t1{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    unsigned ok = 0;
    for (unsigned k = 0; k < iterations; ++k) {
        if (cold) x3m::engine_memory::next_frame();
        ok += sp::read(&s);
    }
    QueryPerformanceCounter(&t1);
    check(ok == iterations, "timed polls ok");
    return double(t1.QuadPart - t0.QuadPart) * 1e9 / double(f.QuadPart) / iterations;
}
int main() {
    // Rows by force only during the cases; the timing run re-admits with a 3600 s period (one row, one QPC per poll).
    SetEnvironmentVariableW(L"X3M_FL_SUN_LIGHT_LOG", L"0");
    check(make_image(), "synthetic image");
    context = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x7000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    nodes = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, node_stride * max_nodes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    records = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    check(context && nodes && records, "fake context allocated");
    if (failures) {
        std::printf("RESULT FAIL checks=%u failures=%u\n", checks, failures);
        return 1;
    }
    sp::Sample s;
    check(!sp::read(&s) && s.status == sp::Status::Disabled, "inactive before initialize: disabled");
    gate_cases();
    check(sp::fixture_initialize(image_base) == nullptr, "re-admitted for the cases");
    selection_cases();
    refusal_cases();
    bound_cases();
    row_case();
    SetEnvironmentVariableW(L"X3M_FL_SUN_LIGHT_LOG", L"3600");
    check(sp::fixture_initialize(image_base) == nullptr, "re-admitted for timing");
    set_slot(addr(context));
    const double warm16 = ns_per_poll(16, false, 20000), cold16 = ns_per_poll(16, true, 20000),
                 warm254 = ns_per_poll(254, false, 2000), cold254 = ns_per_poll(254, true, 2000);
    std::printf("TIMING warm16_ns=%.0f cold16_ns=%.0f warm254_ns=%.0f cold254_ns=%.0f\n", warm16, cold16, warm254,
                cold254);
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
