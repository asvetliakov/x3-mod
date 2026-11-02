#pragma once
#include <cstddef>
#include <cstdint>
#include "cull_census_core.h"

// The engine body table, read by name (body-format-bob1.md 6; the recipe of the deleted lens-flare cull, kept for
// engine_effects_core.h's resolver): the manager header through the image global, a slot's name pointer, the id a
// slot stands for, the default `v\NNNNN` form and the engine's _stricmp. Portable: no Windows dependency, the host
// tests compile it (verification/analysis/test_engine_body_resolver.py).
namespace x3m::engine_body_resolver {
namespace census = x3m::cull_census::core;
// Ids a per-id table covers: fixed 0..999 and 9000..19999, dynamic 20000..32767 (20000 + registration order; the
// Mayhem 3 saves hold about 2200 dynamic names). A name that resolves beyond the span is not mapped.
constexpr std::uint32_t id_span = 0x8000;
static_assert(id_span % 32 == 0 && id_span <= 0x7fffffffu, "whole words, positive ids");

// The id a default-form name stands for: `v\` then exactly five decimal digits, nothing else.
inline bool default_name_id(const char* name, std::int32_t* id) {
    if (!name || name[0] != 'v' || name[1] != '\\') return false;
    std::int32_t v = 0;
    for (unsigned i = 2; i < 7; ++i) {
        if (name[i] < '0' || name[i] > '9') return false;
        v = v * 10 + (name[i] - '0');
    }
    if (name[7] != 0) return false;
    *id = v;
    return true;
}
// The engine's _stricmp for body names: ASCII case folded, `\` and `/` distinct (body-format-bob1.md 6).
inline bool name_equal(const char* a, const char* b) {
    for (;; ++a, ++b) {
        unsigned char x = static_cast<unsigned char>(*a), y = static_cast<unsigned char>(*b);
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
        if (!x) return true;
    }
}
// The slot's id (the inverse of census::body_slot): fixed slots 0..999 are their id, 1000..10999 are id - 9000
// reversed, dynamic slots are 20000 + index.
inline std::int32_t slot_id(std::uint32_t slot, std::int32_t fixed) {
    if (std::int32_t(slot) >= fixed) return 20000 + (std::int32_t(slot) - fixed);
    return slot < 1000 ? std::int32_t(slot) : std::int32_t(slot) + 9000;
}

// Bounded memory reader (engine_memory::read in the DLL, a synthetic image in the tests): false = unreadable.
using Reader = bool (*)(std::uintptr_t address, void* out, std::size_t size);
struct Table {
    std::uint32_t manager = 0, slots = 0;
    std::int32_t fixed = 0, dynamic = 0;
    bool valid = false;
};
// The body manager's header through the image global (two reads; the same recipe as the census's body_table()).
inline Table read_table(Reader read, std::uintptr_t global_va) {
    Table t;
    std::uint32_t g = 0;
    std::int32_t head[3]{};
    if (!read(global_va, &g, 4) || !g || !read(std::uintptr_t(g) + census::body_fixed_count_offset, head, sizeof head))
        return t;
    t.manager = g;
    t.fixed = head[0];
    t.dynamic = head[1];
    t.slots = std::uint32_t(head[2]);
    t.valid = head[0] == census::body_fixed_count && head[1] >= 0 && head[1] < census::body_dynamic_limit &&
              head[2] != 0;
    return t;
}
// The name pointer of a slot; false when the slot address would wrap or is unreadable.
inline bool slot_name_pointer(Reader read, const Table& t, std::uint32_t slot, std::uint32_t* p) {
    const std::uint64_t at = std::uint64_t(t.slots) + std::uint64_t(slot) * census::body_slot_stride +
                             census::body_slot_name_offset;
    return at <= 0xfffffffcu && read(std::uintptr_t(at), p, 4);
}
}
