#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "cull_census_core.h"
#include "cull_small_parts_core.h"

// Portable core of the engine-side lens-flare cull (X3M_LENS_FLARE_GAIN=0;
// docs/reverse-engineering/lod-selection.md, "Lens-flare cull on the small-parts
// site"; docs/verification/sun-occlusion.md, "lens-flare gain 0 culls in the
// engine"): a second stub on the small-parts claim of the per-node cull/LOD pass
// 0x0047cfe0 at 0x0047d2a2 (cull_small_parts_core.h holds the window, the site
// and the liveness facts; both stubs chain on one engine_patch claim). The lens
// block 0x00472466..0x00472476 walks the lens scene ([0x608518]+0x64) through
// 0x0047e780, which calls the pass on every root (0x0047e7a5) before the
// traversal 0x0047e6e0 draws it, so a flare sprite node whose renderable bit
// the pass clears is never drawn. The stub tests the node's model id (+0x140,
// the word the pass itself reads at 0x0047d2ec) against a bitmap of the flare
// body ids and sends a member down the engine's own size-cull instruction at
// 0x0047d2c3. The bitmap is filled from the engine's body table by name
// (body-format-bob1.md 6): no string compare runs per node.
namespace x3m::lens_flare_cull::core {
namespace small = x3m::cull_small_parts::core;
namespace census = x3m::cull_census::core;
constexpr std::uintptr_t site_va = small::site_va, cull_va = small::cull_va;
constexpr unsigned model_offset = census::model_offset; // node+0x140, the body id (model+0x08)
// Ids the bitmap covers: fixed 0..999 and 9000..19999, dynamic 20000..32767 (20000 + registration order; the
// Mayhem 3 saves hold about 2200 dynamic names). A name that resolves beyond the span is counted, not mapped.
constexpr std::uint32_t id_span = 0x8000;
constexpr unsigned bitmap_words = id_span / 32;
static_assert(id_span % 32 == 0 && id_span <= 0x7fffffffu, "bitmap of whole words, positive ids");

// The flare bodies by name, `v\NNNNN` = objects\v\NNNNN (the engine's default name for a fixed slot without one,
// and the literal name a scene or table registers for ids the fixed map has no slot for: 1000..8999 are invalid
// as numbers, so `v\01006` is a dynamic registration). Stock types/Lensflares.pck groups 0..8
// (lens-flare-visibility.md 15: core discs 719..722, ray stars 752..754 and 760, streaks 761..766, flare cards
// 11000..11011, ghosts 61, 548..550, 740, 741, 744, 745, rings 735, 739, 778) plus the sprites run385 drew in the
// lens bracket on Mayhem 3 (`v\00781`, `v\01006`, `v\01011`, `v\01016`, `v\01019`).
constexpr const char* const body_names[] = {
    "v\\00061", "v\\00548", "v\\00549", "v\\00550", "v\\00719", "v\\00720", "v\\00721", "v\\00722", "v\\00735",
    "v\\00739", "v\\00740", "v\\00741", "v\\00744", "v\\00745", "v\\00752", "v\\00753", "v\\00754", "v\\00760",
    "v\\00761", "v\\00762", "v\\00763", "v\\00764", "v\\00765", "v\\00766", "v\\00778", "v\\00781", "v\\01006",
    "v\\01011", "v\\01016", "v\\01019", "v\\11000", "v\\11001", "v\\11002", "v\\11003", "v\\11004", "v\\11005",
    "v\\11006", "v\\11007", "v\\11008", "v\\11009", "v\\11010", "v\\11011"};
constexpr unsigned body_name_count = sizeof body_names / sizeof body_names[0];
static_assert(body_name_count == 42, "the documented set");
constexpr unsigned name_cap = 8; // every name above is 7 characters + NUL; a longer slot name can never match

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
// The engine's _stricmp for the names here: ASCII case folded, `\` and `/` distinct (body-format-bob1.md 6).
inline bool name_equal(const char* a, const char* b) {
    for (;; ++a, ++b) {
        unsigned char x = static_cast<unsigned char>(*a), y = static_cast<unsigned char>(*b);
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
        if (!x) return true;
    }
}
struct Bitmap {
    std::uint32_t words[bitmap_words];
    void clear() { std::memset(words, 0, sizeof words); }
    bool set(std::int32_t id) {
        if (id < 0 || std::uint32_t(id) >= id_span) return false;
        words[std::uint32_t(id) >> 5] |= 1u << (std::uint32_t(id) & 31);
        return true;
    }
    bool test(std::int32_t id) const {
        return id >= 0 && std::uint32_t(id) < id_span && (words[std::uint32_t(id) >> 5] >> (std::uint32_t(id) & 31)) & 1u;
    }
};
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
// The body manager's header through the image global (5 reads; the same recipe as the census's body_table()).
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
// Up to name_cap bytes at p in page-bounded pieces (a 7-character name ending just before an unreadable page still
// reads); false without a NUL inside name_cap bytes (such a name cannot be one of ours).
inline bool read_short_name(Reader read, std::uint32_t p, char out[name_cap]) {
    unsigned have = 0;
    while (have < name_cap) {
        const std::uintptr_t at = std::uintptr_t(p) + have;
        unsigned chunk = unsigned(0x1000 - (at & 0xfff));
        if (chunk > name_cap - have) chunk = name_cap - have;
        if (!read(at, out + have, chunk)) return false;
        for (unsigned i = have; i < have + chunk; ++i)
            if (!out[i]) return true;
        have += chunk;
    }
    return false;
}
// The slot's name as the engine would print it (its `+0x0c` string, or `v\%05d` of its id when null), into out;
// false when unreadable or longer than our names.
inline bool slot_name(Reader read, const Table& t, std::uint32_t slot, char out[16]) {
    std::uint32_t p = 0;
    if (!slot_name_pointer(read, t, slot, &p)) return false;
    if (!p) {
        census::body_default_name(slot_id(slot, t.fixed), out);
        return true;
    }
    return read_short_name(read, p, out);
}
struct Resolution {
    std::uint32_t resolved = 0; // names found in the table
    std::uint32_t mapped = 0;   // ... whose id fits the bitmap (the set the stub tests)
    std::uint32_t scanned = 0;  // slots read by name
};
// A name resolved in a dynamic slot (id = 20000 + index): re-read every frame, because a game load can free and
// refill the array at the same address with the same or a larger count, re-binding the ids.
struct Mapping {
    std::uint32_t slot;
    unsigned name;
};
struct Mappings {
    Mapping entries[body_name_count];
    unsigned count = 0;
    void clear() { count = 0; }
    void add(std::uint32_t slot, unsigned name) {
        if (count < body_name_count) entries[count++] = Mapping{slot, name};
    }
};
// Whether every recorded dynamic slot still carries its name (at most `count` slot-pointer and short-name reads);
// false on any mismatch or unreadable slot, and when a mapping lies beyond the table (the caller restarts).
inline bool mappings_hold(Reader read, const Table& t, const Mappings& m) {
    char name[16];
    for (unsigned i = 0; i < m.count; ++i) {
        const Mapping& e = m.entries[i];
        if (e.slot >= std::uint32_t(t.fixed) + std::uint32_t(t.dynamic) || !slot_name(read, t, e.slot, name) ||
            !name_equal(name, body_names[e.name]))
            return false;
    }
    return true;
}
// Resolves the names in [0, body_name_count) that `found` does not mark yet. First by the id a default-form name
// stands for (its slot's name null or equal to the name, so `v\00752` is 752 and `v\11000` is 11000 without a
// scan); then, for the names still missing, by scanning the slots [slot_from, slot_to) by name (a `v\01006` lives in
// a dynamic slot under that literal name). The bitmap gets every id found; `found` is updated so a later
// incremental scan reads only new slots. Reads are bounded by the reader; an unreadable slot is skipped.
inline Resolution resolve(Reader read, const Table& t, Bitmap* map, bool* found, std::uint32_t slot_from,
                          std::uint32_t slot_to, Mappings* dynamic = nullptr) {
    Resolution r;
    const std::uint32_t slot_count = std::uint32_t(t.fixed) + std::uint32_t(t.dynamic);
    if (!t.valid) return r;
    unsigned missing = 0;
    char name[16];
    for (unsigned i = 0; i < body_name_count; ++i) {
        if (found[i]) continue;
        std::int32_t id = 0;
        std::uint32_t slot = 0;
        if (default_name_id(body_names[i], &id) && census::body_slot(id, t.fixed, t.dynamic, &slot) &&
            slot_name(read, t, slot, name) && name_equal(name, body_names[i])) {
            found[i] = true;
            ++r.resolved;
            if (map->set(id)) ++r.mapped;
            if (dynamic && slot >= std::uint32_t(t.fixed)) dynamic->add(slot, i);
        } else
            ++missing;
    }
    if (slot_to > slot_count) slot_to = slot_count;
    for (std::uint32_t slot = slot_from; missing && slot < slot_to; ++slot) {
        std::uint32_t p = 0;
        if (!slot_name_pointer(read, t, slot, &p) || !p || !read_short_name(read, p, name)) continue;
        ++r.scanned;
        for (unsigned i = 0; i < body_name_count; ++i) {
            if (found[i] || !name_equal(name, body_names[i])) continue;
            found[i] = true;
            --missing;
            ++r.resolved;
            if (map->set(slot_id(slot, t.fixed))) ++r.mapped;
            if (dynamic && slot >= std::uint32_t(t.fixed)) dynamic->add(slot, i);
            break;
        }
    }
    return r;
}

// The stub (81 bytes), entered by the dispatcher's `jmp [entry]` (or the small-parts stub's `jmp [next]`, whichever
// was pushed later) with the site's exact register state and ESP (no return address):
//    0  83 3d abs32 00      CMP  dword [enabled],0      ; 0 unless the gain is 0 and the set is mapped
//    7  74 42               JE   continue
//    9  8b 87 40 01 00 00   MOV  EAX,[EDI+0x140]        ; model id (EAX dead at the site: written at 0x0047d2a7)
//   15  3d 00 80 00 00      CMP  EAX,id_span            ; unsigned: a negative id (no model) fails too
//   20  73 35               JAE  continue
//   22  8b c8               MOV  ECX,EAX                ; ECX dead at the site (written by the displaced load)
//   24  c1 e9 05            SHR  ECX,5
//   27  8b 0c 8d abs32      MOV  ECX,[bitmap+ECX*4]
//   34  0f a3 c1            BT   ECX,EAX                ; CF = bit (id mod 32)
//   37  73 24               JAE  continue               ; CF = 0: not a flare body
//   39  8b 4f 18            MOV  ECX,[EDI+0x18]         ; 0x0047d2a2..0x0047d2b9 replayed so ECX/EAX arrive at the
//   42  85 c9               TEST ECX,ECX                ;   cull exactly as the engine leaves them (both dead there),
//   44  8b 87 d8 01 00 00   MOV  EAX,[EDI+0x1d8]        ;   as the small-parts stub does
//   50  74 0c               JE   cull
//   52  8b 89 d8 01 00 00   MOV  ECX,[ECX+0x1d8]
//   58  3b c8               CMP  ECX,EAX
//   60  7e 02               JLE  cull
//   62  8b c1               MOV  EAX,ECX
//   64  ff 05 abs32         INC  dword [culled]         ; cull: running count, render thread only
//   70  e9 rel32            JMP  0x0047d2c3             ; the engine's `and [edi+0x12c],~2; jmp 0x0047d2d1`
//   75  ff 25 abs32         JMP  [next]                 ; continue: the previous chain head (the small-parts stub
//                                                       ;   or the tail: displaced MOV+TEST, jump back to 0x0047d2a7)
// No call, no Win32, no floating point: LastError and the x87 stack are untouched by construction. EFLAGS are
// dead on every exit (the tail's displaced TEST regenerates them for the JE at 0x0047d2ad, the cull AND overwrites
// them). ESP, EDI, ESI, EBX, EBP and EDX are not written. Per node with the flag on: one compare, one load, one
// range compare, a shift, one bitmap load and a bit test (the "few compares" budget); with it off, one compare.
constexpr unsigned stub_length = 81, stub_model = 9, stub_replay = 39, stub_cull = 64, stub_continue = 75;
inline void encode_stub(std::uint32_t at, std::uint32_t enabled, std::uint32_t bitmap, std::uint32_t culled,
                        std::uint32_t cull_target, std::uint32_t next_slot, unsigned char out[stub_length]) {
    out[0] = 0x83;
    out[1] = 0x3d;
    std::memcpy(out + 2, &enabled, 4);
    out[6] = 0x00;
    out[7] = 0x74;
    out[8] = static_cast<unsigned char>(stub_continue - 9);
    out[9] = 0x8b;
    out[10] = 0x87;
    out[11] = model_offset & 0xff;
    out[12] = model_offset >> 8;
    out[13] = 0x00;
    out[14] = 0x00;
    out[15] = 0x3d;
    const std::uint32_t span = id_span;
    std::memcpy(out + 16, &span, 4);
    out[20] = 0x73;
    out[21] = static_cast<unsigned char>(stub_continue - 22);
    out[22] = 0x8b;
    out[23] = 0xc8;
    out[24] = 0xc1;
    out[25] = 0xe9;
    out[26] = 0x05;
    out[27] = 0x8b;
    out[28] = 0x0c;
    out[29] = 0x8d;
    std::memcpy(out + 30, &bitmap, 4);
    out[34] = 0x0f;
    out[35] = 0xa3;
    out[36] = 0xc1;
    out[37] = 0x73;
    out[38] = static_cast<unsigned char>(stub_continue - 39);
    out[39] = 0x8b;
    out[40] = 0x4f;
    out[41] = 0x18;
    out[42] = 0x85;
    out[43] = 0xc9;
    out[44] = 0x8b;
    out[45] = 0x87;
    out[46] = 0xd8;
    out[47] = 0x01;
    out[48] = 0x00;
    out[49] = 0x00;
    out[50] = 0x74;
    out[51] = static_cast<unsigned char>(stub_cull - 52);
    out[52] = 0x8b;
    out[53] = 0x89;
    out[54] = 0xd8;
    out[55] = 0x01;
    out[56] = 0x00;
    out[57] = 0x00;
    out[58] = 0x3b;
    out[59] = 0xc8;
    out[60] = 0x7e;
    out[61] = static_cast<unsigned char>(stub_cull - 62);
    out[62] = 0x8b;
    out[63] = 0xc1;
    out[64] = 0xff;
    out[65] = 0x05;
    std::memcpy(out + 66, &culled, 4);
    out[70] = 0xe9;
    const std::uint32_t rel = cull_target - (at + stub_continue);
    std::memcpy(out + 71, &rel, 4);
    out[75] = 0xff;
    out[76] = 0x25;
    std::memcpy(out + 77, &next_slot, 4);
}
// The engine instructions the claim relies on beyond the small-parts window, pinned at install (fail closed):
// the lens block's cull walk of the lens scene and the walker's call of the pass, and the pass's own read of the
// model id after the cull point (the word the stub tests is the one the pass resolves).
//   00472466  a1 18 85 60 00       MOV  EAX,[0x00608518]        ; body manager g
//   0047246b  8b 48 64             MOV  ECX,[EAX+0x64]          ; the lens scene
//   0047246e  51                   PUSH ECX
//   0047246f  8b fe                MOV  EDI,ESI                 ; the view
//   00472471  e8 0a c3 00 00       CALL 0x0047e780              ; per root node: the pass
//   0047e7a0  6a 00                PUSH 0
//   0047e7a2  57                   PUSH EDI                     ; view
//   0047e7a3  8b ce                MOV  ECX,ESI                 ; node
//   0047e7a5  e8 36 e8 ff ff       CALL 0x0047cfe0
//   0047d2ec  8b 87 40 01 00 00    MOV  EAX,[EDI+0x140]         ; model id
constexpr std::uintptr_t lens_walk_va = 0x00472466, walker_call_va = 0x0047e7a0, model_read_va = 0x0047d2ec;
constexpr unsigned lens_walk_length = 16, walker_call_length = 10, model_read_length = 6;
constexpr unsigned char lens_walk[lens_walk_length] = {0xa1, 0x18, 0x85, 0x60, 0x00, 0x8b, 0x48, 0x64,
                                                       0x51, 0x8b, 0xfe, 0xe8, 0x0a, 0xc3, 0x00, 0x00};
constexpr unsigned char walker_call[walker_call_length] = {0x6a, 0x00, 0x57, 0x8b, 0xce, 0xe8, 0x36, 0xe8, 0xff, 0xff};
constexpr unsigned char model_read[model_read_length] = {0x8b, 0x87, 0x40, 0x01, 0x00, 0x00};
constexpr std::uintptr_t lens_scene_offset = 0x64, walker_va = 0x0047e780;
static_assert(((std::uint64_t(lens_walk_va) + 11 + 5 + 0x0000c30au) & 0xffffffffu) == walker_va,
              "the lens block's call reaches the walker");
static_assert(((std::uint64_t(walker_call_va) + 5 + 5 + 0xffffe836u) & 0xffffffffu) == small::function_va,
              "the walker calls the pass");
}
