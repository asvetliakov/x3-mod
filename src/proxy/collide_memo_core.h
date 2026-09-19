#pragma once
#include <cstdint>
#include <cstring>

// Portable core of the temporal no-contact memo of the engine's mesh-pair collision query
// (docs/reverse-engineering/sector-collide.md section 14). No Windows dependency: the host tests compile it.
//
// 0047f30d  8b 84 24 94 00 00 00 / 51 / 8d 4c 24 34 / d9 1c 24 / 51 / 8b 8c 24 98 00 00 00 / 8d 54 24 68 / 52
//           MOV EAX,[cap]; PUSH (slot); LEA ECX,&T1; FSTP [ESP] (s1); PUSH ECX; MOV ECX,[flags]; LEA EDX,&R1; PUSH EDX
// 0047f329  e8 c2 36 06 00                         CALL 0x004e29f0      <- the site (sole caller in the image)
// 0047f32e  33 c0 / 83 c4 28 / 39 05 4c 85 60 00 / 5e / 0f 95 c0 / 83 c4 64 / c3     result = [0x0060854c] != 0
//
// At the site: ECX = flags, EAX = contact cap, [ESP+4..] = &R1, &T1, s1, model a, &R2, &T2, s2, model b, tolerance
// and a tenth argument the caller pushes first (`push edi` at 0x0047f1d7, EDI being a register argument of
// 0x0047f1b0): the running-minimum pointer 0x004e29f0 stores in [0x00608540] (0 at 0x0048a9a5, a local float at
// 0x0048a69e). Everything the query reads is in
// those words, the two models and the value behind that pointer, so they are the key, compared bit for bit:
// the 26 transform floats, tolerance, flags, cap, the pointer's null-ness and the float behind it, both model
// pointers, both 24-byte model headers ([+0] root box, [+0x14] == 3 built) and both 0x48-byte root boxes as a content
// stamp. An entry also expires unless it was stored or hit in this frame or the one before, so a model address
// reused after a load, a sector change or an object's destruction cannot meet an old entry: that would take the same
// pointers, headers, root boxes and transform bits again within one frame.
namespace x3m::collide_memo::core {
constexpr std::uintptr_t memo_site_va = 0x0047f329, memo_target_va = 0x004e29f0, memo_return_va = 0x0047f32e;
constexpr std::uintptr_t caller_va = 0x0047f1b0, query_va = 0x004e2780, descent_va = 0x004e2530, leaf_va = 0x004e2190, triangle_va = 0x004e2a50;
constexpr std::uintptr_t sat_va = 0x004e3280, matrix_helpers_va = 0x004e1ff0, vector_helpers_va = 0x004dfd80, ftol_va = 0x0052b5d0;
constexpr unsigned call_length = 5, memo_pre_length = 28, memo_post_length = 19;
constexpr unsigned caller_length = 0x179, target_length = 0x60, query_length = 0x1e9, descent_length = 0x247, leaf_length = 0x39e, triangle_length = 0x830;
constexpr unsigned sat_length = 0x62e, matrix_helpers_length = 0x197, vector_helpers_length = 0x163, ftol_length = 0xab;
constexpr unsigned char memo_pre_window[memo_pre_length] = {
    0x8b,0x84,0x24,0x94,0x00,0x00,0x00, 0x51, 0x8d,0x4c,0x24,0x34, 0xd9,0x1c,0x24, 0x51, 0x8b,0x8c,0x24,0x98,0x00,0x00,0x00, 0x8d,0x54,0x24,0x68, 0x52};
constexpr unsigned char memo_post_window[memo_post_length] = {0x33,0xc0, 0x83,0xc4,0x28, 0x39,0x05,0x4c,0x85,0x60,0x00, 0x5e, 0x0f,0x95,0xc0, 0x83,0xc4,0x64, 0xc3};
// FNV-1a 64 of the pinned image. The descent and the leaf are hashed with the bytes other modules may already have
// rewritten zeroed: the census's entry claims (+0..+4 of each) and the SAT module's rel32 (descent +0x74..+0x77).
constexpr unsigned entry_hole = 5, sat_rel32_offset = 0x74, sat_rel32_length = 4;
constexpr std::uint64_t caller_fnv1a = 0xfdd929ef070d4324ull, target_fnv1a = 0x9d655aae0a820ae8ull, query_fnv1a = 0x5a4d7c6efe584a18ull;
constexpr std::uint64_t descent_fnv1a = 0xcef4870863cdd1deull, leaf_fnv1a = 0xa9766de75c6ecfcaull, triangle_fnv1a = 0x90c2eb0126ac4f2aull;
// The SAT 0x004e3280, the matrix helpers 0x004e1ff0/0x004e20d0/0x004e2130, the vector helpers 0x004dfd80/0x004dfe60/0x004dfeb0
// and ftol 0x0052b5d0 (which reads the process-constant SSE2 flag [0x006619ec]: cvttsd2si when set, otherwise an x87
// path under the control word). The SAT module patches the SAT's call site inside the descent (a hole above), not these.
constexpr std::uint64_t sat_fnv1a = 0xad8a2cb66c0bf30cull, matrix_helpers_fnv1a = 0xd21cd0c0e39e9854ull, vector_helpers_fnv1a = 0xc5107d96ea42adcdull, ftol_fnv1a = 0xc648662b5a549dd9ull;
// What a query that finds no contact writes, all of it a function of the key (section 14.2): the mode globals
// 0x00608534 (flags), 0x00608538 (cap), 0x0060853c (tolerance as integer), 0x00608540 (running-minimum pointer), the
// counters 0x00608544 (node pairs), 0x00608548 (triangle tests), 0x0060854c (contacts = 0), and the root transform
// block 0x00596928..0x0059695f (T, first-contact flag, scale, R). The contact record 0x0060851c..0x00608533 and the
// float behind the running-minimum pointer are written on a contact only.
constexpr std::uintptr_t flags_va = 0x00608534, cap_va = 0x00608538, tolerance_va = 0x0060853c, minimum_va = 0x00608540;
constexpr std::uintptr_t visits_va = 0x00608544, triangles_va = 0x00608548, contacts_va = 0x0060854c, root_block_va = 0x00596928;
constexpr unsigned root_block_words = 14, header_words = 6, box_words = 18, model_built = 3, model_state_word = 5;

constexpr unsigned key_words = 26 + 5 + 2 + 2 * header_words + 2 * box_words;   // 81
// Word 30 is the float behind the running-minimum pointer. The engine reads it in one place, the leaf 0x004e2190
// (`fcom [ecx]` at 0x004e2478, after a triangle pair intersected), and every entry into the leaf increments
// [0x00608548] first (0x004e22a5, straight-line from the entry). A stored run with `triangles == 0` therefore never
// entered the leaf and cannot have depended on that float: such an entry also answers a query that differs from it
// in word 30 alone. The pointer's null-ness (word 29) stays in the key, and the hash leaves word 30 out.
constexpr unsigned minimum_value_word = 30, model_words_begin = 31;
struct Key { std::uint32_t words[key_words]; };
struct Outputs { std::uint32_t visits, triangles, tolerance_integer, root_block[root_block_words]; };
struct Entry { Key key; Outputs outputs; std::uint32_t frame; bool valid; };
constexpr unsigned ways = 4, sets = 256;
// Why a query missed, judged against the last entry stored for the same two models with the same transform of a (or,
// failing that, of b): the first group of key words that differs. `expired`: the same key, no longer live.
enum MissClass : unsigned { miss_none_found, miss_xform_a, miss_xform_b, miss_scale, miss_mode, miss_models, miss_min_value, miss_expired, miss_classes };
struct Counters { std::uint32_t hits, misses, stored, contacts, ineligible, evictions, skipped_visits, skipped_triangles, verified, verify_mismatches, foreign_thread, reentered, clears, stuck_busy;
                  std::uint32_t min_relaxed_hits, miss_count[miss_classes], miss_visits[miss_classes]; };
// The expiry clock is Present. Should the simulation ever run this many queries without one (a loading loop, a paused
// renderer), the table is dropped rather than trusted: about 1e2 queries make a frame, so this is ~1e3 frames' worth.
constexpr std::uint32_t queries_without_tick_limit = 100000;

inline std::uint32_t hash(const Key& key) {
    std::uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < 33; ++i) if (i != minimum_value_word) h = (h ^ key.words[i]) * 0x01000193u;   // transforms, mode and the model pointers; the stamps only confirm
    return h ^ (h >> 15);
}
// An entry is live when it was stored or hit in `frame` or the frame before (unsigned, so a wrapped counter still works).
inline bool live(const Entry& e, std::uint32_t frame) { return e.valid && frame - e.frame <= 1u; }

inline std::uint32_t hash_words(const Key& key, unsigned begin, unsigned end) {
    std::uint32_t h = (0x811c9dc5u ^ key.words[model_words_begin]) * 0x01000193u;
    h = (h ^ key.words[model_words_begin + 1]) * 0x01000193u;
    for (unsigned i = begin; i < end; ++i) h = (h ^ key.words[i]) * 0x01000193u;
    return h ^ (h >> 15);
}
inline bool same_words(const Key& x, const Key& y, unsigned begin, unsigned end) { return !std::memcmp(x.words + begin, y.words + begin, 4 * (end - begin)); }
// Equal in everything, or in everything but the running minimum when the stored run never reached a leaf.
inline bool answers(const Entry& e, const Key& key) {
    if (!same_words(e.key, key, 0, minimum_value_word) || !same_words(e.key, key, model_words_begin, key_words)) return false;
    return e.key.words[minimum_value_word] == key.words[minimum_value_word] || e.outputs.triangles == 0;
}

class Table {
public:
    Entry* find(const Key& key, std::uint32_t frame) {
        Entry* set = entries_ + (hash(key) % sets) * ways;
        for (unsigned w = 0; w < ways; ++w)
            if (live(set[w], frame) && answers(set[w], key)) return &set[w];
        return nullptr;
    }
    // Stores a no-contact result; the way taken is a dead one, else the one touched longest ago. True when a live entry was evicted.
    bool store(const Key& key, const Outputs& outputs, std::uint32_t frame) {
        Entry* set = entries_ + (hash(key) % sets) * ways;
        Entry* victim = nullptr;
        for (unsigned w = 0; w < ways && victim == nullptr; ++w) if (!live(set[w], frame)) victim = &set[w];
        bool evicted = false;
        if (victim == nullptr) {
            victim = set;
            for (unsigned w = 1; w < ways; ++w) if (frame - set[w].frame > frame - victim->frame) victim = &set[w];
            evicted = true;
        }
        victim->key = key; victim->outputs = outputs; victim->frame = frame; victim->valid = true;
        const std::uint16_t index = static_cast<std::uint16_t>(victim - entries_ + 1);
        by_a_[hash_words(key, 0, 12) % (ways * sets)] = index;
        by_b_[hash_words(key, 13, 25) % (ways * sets)] = index;
        return evicted;
    }
    // Diagnostic only (a miss has already been decided): which group of words keeps the nearest stored query from answering.
    MissClass classify(const Key& key) const {
        const Entry* e = candidate(by_a_[hash_words(key, 0, 12) % (ways * sets)], key, 0, 12);
        if (e == nullptr) return candidate(by_b_[hash_words(key, 13, 25) % (ways * sets)], key, 13, 25) != nullptr ? miss_xform_a : miss_none_found;
        if (!same_words(e->key, key, model_words_begin + 2, key_words)) return miss_models;
        if (!same_words(e->key, key, 26, 30)) return miss_mode;
        if (e->key.words[12] != key.words[12] || e->key.words[25] != key.words[25]) return miss_scale;
        if (!same_words(e->key, key, 13, 25)) return miss_xform_b;
        return e->key.words[minimum_value_word] != key.words[minimum_value_word] ? miss_min_value : miss_expired;
    }
    void clear() { for (Entry& e : entries_) e.valid = false; std::memset(by_a_, 0, sizeof by_a_); std::memset(by_b_, 0, sizeof by_b_); }
private:
    const Entry* candidate(std::uint16_t index, const Key& key, unsigned begin, unsigned end) const {
        if (index == 0) return nullptr;
        const Entry& e = entries_[index - 1];
        return e.valid && same_words(e.key, key, model_words_begin, model_words_begin + 2) && same_words(e.key, key, begin, end) ? &e : nullptr;
    }
    Entry entries_[ways * sets];
    std::uint16_t by_a_[ways * sets], by_b_[ways * sets];   // last entry stored per (models, transform of a) and per (models, transform of b)
};
}
