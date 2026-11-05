#pragma once
#include <cstdint>

// Sector key (docs/reverse-engineering/sector-identity.md section 3; docs/architecture/sector-background-modern.md,
// "Implementation A, per-sector seed"): the universe map x/y of a native sector object S, read through its KC object of
// class 2001, as pure templates the host test compiles against a fabricated memory. Read-only: no hook, no patch, no
// engine call, no allocation, no floating point, no CRT. The edition part (src/proxy/sector_key.cpp) compares the byte
// windows below once against the running image, binds the VM global and supplies the bounded reader.
//
// Chain: S ([S+0x48] == 1, alive marker 0xcafe at +0x9c, native id [S+8]) -> [S+0x94] = the KC id (negative; set by
// 2001::Activate through SA_SetEventObject) -> the VM's object hash [VM+0x12d0] keyed by ~id -> the KC object
// {+0 id, +8 class record (+0 = 2001), +0xc member cells (5 bytes: tag, dword; tag 1 = int)} -> m0 = x, m1 = y; the
// native-id member (m34 AP, m36 FL) must equal [S+8] (the object owns this sector). Key = (x + 1) * 0x10000 + (y + 1):
// never 0 for a known sector, so 0 keeps meaning "no sector".
namespace x3m::sector_key::core {
constexpr std::uint32_t preferred_base = 0x00400000;
constexpr unsigned sector_header_begin = 0x08, sector_header_end = 0x9e; // one span: id .. alive marker
constexpr unsigned sector_id = 0x08, sector_class = 0x48, sector_event_object = 0x94, sector_alive = 0x9c;
constexpr std::uint16_t class_sector = 1, alive_marker = 0xcafe;
constexpr unsigned vm_object_hash = 0x12d0;
constexpr std::uint32_t hash_capacity_max = 1u << 22;
constexpr unsigned hash_link_limit = 64; // nodes visited per walk at most (the note's bound; not a measured length)
constexpr std::int32_t kc_class_sector = 2001;
constexpr unsigned kc_cell = 5;
constexpr std::uint8_t kc_tag_int = 1;
constexpr std::int32_t axis_max = 255; // x, y accepted in 0..255 (maps measured: x 0..23, y 0..19)

inline constexpr std::uint64_t key_of(std::int32_t x, std::int32_t y) {
    return std::uint64_t(std::uint32_t(x) + 1u) * 0x10000u + std::uint64_t(std::uint32_t(y) + 1u);
}

// ---- The byte windows (the verifier's table, verification/results/sector-identity/verify_sector_identity.py, hex at
// the preferred VA). relocs: offsets of HIGHLOW dwords the loader rebases (FL is relocatable); the comparator expects
// the preferred value - preferred_base + base there. ----
struct Window {
    const char* name;
    std::uint32_t va;
    const char* hex;
    unsigned relocs;
    unsigned reloc[3];
};
constexpr unsigned hex_length(const char* s) {
    unsigned n = 0;
    while (s[n]) ++n;
    return n / 2;
}
// clang-format off
constexpr Window ap_windows[] = {
    {"player_root", 0x42c890, "a10c85600083ec08535556578b7838", 0, {}},
    {"parent_is_sector", 0x42c8a9, "8b475485c00f84b10000006683784801", 0, {}},
    {"sa_alloc_sector", 0x46070d, "6880010000e8296308008bf083c4048974241033c03bf0898424ec03000074c58bc3e89c1dffffeb", 0, {}},
    {"sa_set_event_object", 0x46078b, "8b75188b5601a10c856000e8559dfdff85c0742f8a4e0580f90175178b4e06898894000000"
                                      "8b15e48560006a0052e93b26000080f90a750b8b56068b0a898894000000", 0, {}},
    {"sa_get_event_object", 0x4607dc, "8b45188b5001a10c856000e8049dfdff85c00f8411ffffff8b809400000050e9f2250000", 0, {}},
    {"kc_object_lookup", 0x49f1e0, "85c07d3283c9ff2bc88b86d01200008b1085d2741a8b400483e80123c18b048285c0740b3948"
                                   "0474098b0085c075f533c0c38b4008c38b561c508b4620e8ce14010083c404c3", 0, {}},
    {"kc_ldmem", 0x4a283c, "0fbf078b4c24208d048003410c83eb058bf3895c2414e8995c000083c702", 0, {}},
    {"kc_this", 0x4a3052, "8b4c24208b0183eb05c60301894301", 0, {}},
    {"kc_new_object_id", 0x4a2e0f, "8b5424188b74242c8b82d012000056e89dce040083c9ff2bc883c404890e", 0, {}},
    {"kc_class_bsearch", 0x4b06f0, "85d2538b5c240856578bf8762a8d49008bcad1e98d04cd000000002bc18b34c73bde8d04c77c16"
                                   "7e108d783883c8ff2bc103d085d277d933c05f5e5bc38bd1ebf2", 0, {}},
    {"object_init_event_zero", 0x44004f, "899d80000000899d94000000899d9800000066c7859c0000", 0, {}},
    {"restore_event_object", 0x430130, "898780000000e8e5920b00898794000000", 0, {}},
    {"vm_create_store", 0x4031e8, "e8237e0a003bc6a3e4856000", 0, {}},
};
constexpr Window fl_windows[] = {
    {"player_root", 0x42f913, "a1ec825a005355578b5838", 1, {1}},
    {"parent_is_sector", 0x42f92e, "8b435485c00f849601000066837848010f858b010000", 0, {}},
    {"sa_alloc_sector", 0x46a71e, "6880010000e8f8e9060083c4048945acc745fc0000000085c0740a568bc8e8cf19ffff", 0, {}},
    {"sa_set_event_object", 0x46a7c1, "8b73188b0dec825a00ff7601e85e95fdff8bd085d274328a46053c01751c8b4e066a00898a94"
                                      "0000008b0d74a15a0057e84a710500e9a39500003c0a750b8b46068b008982940000008b0d74"
                                      "a15a006a0057e828710500e981950000", 3, {5, 43, 75}},
    {"sa_get_event_object", 0x46a81d, "8b43188b0dec825a00ff7001e80295fdff85c00f841affffff8b80940000008b0d74a15a0050"
                                      "57e8f7700500e950950000", 2, {5, 33}},
    {"kc_object_lookup", 0x4bb1b0, "8b54240485d279318b81d0120000f7d28b0885c974188b40044823c28b048185c0740b395004"
                                   "740b8b0085c075f533c0c204008b4008c20400528b511c8b4920e88b1f010083c404c20400", 0, {}},
    {"kc_ldmem", 0x4bef32, "0fbf0683ef058b4de0897de8897de48d048003410c8bcf50e821650000", 0, {}},
    {"kc_this", 0x4bf9eb, "8b45e083ef058b00c60701894701e9d3f3ffff", 0, {}},
    {"kc_new_object_id", 0x4bf7b2, "8b45ec8bf18b7de88bd68b88d0120000e819d8040083c9ff2bc8890e", 0, {}},
    {"kc_class_bsearch", 0x4cd180, "538bd9565785d274328b7c24100f1f008bcad1e98d04cd000000002bc18b34c38d04c33bfe7c0e"
                                   "7e148d583883c8ff2bc103d0eb028bd185d275d533c05f5e5bc3", 0, {}},
    {"restore_event_object", 0x434658, "8bcb6a010bd08d4424146a0450899794000000", 0, {}},
};
// clang-format on
// The edition's two bindings beside the windows: the VM global (preferred VA) and the KC member that holds the
// native id (2001::Activate's `stmem N` after SA_AllocObject).
struct Edition {
    const char* name;
    std::uint32_t vm_global_va;
    unsigned native_id_member;
    const Window* windows;
    unsigned window_count;
};
constexpr Edition ap{"ap", 0x006085e4, 34, ap_windows, sizeof ap_windows / sizeof ap_windows[0]};
constexpr Edition fl{"fl", 0x005aa174, 36, fl_windows, sizeof fl_windows / sizeof fl_windows[0]};
constexpr unsigned window_capacity = 128;
constexpr bool windows_fit(const Edition& e) {
    for (unsigned i = 0; i < e.window_count; ++i) {
        const Window& w = e.windows[i];
        const unsigned n = hex_length(w.hex);
        if (!n || n > window_capacity || w.relocs > 3 || w.va < preferred_base) return false;
        for (unsigned k = 0; k < w.relocs; ++k)
            if (w.reloc[k] + 4 > n) return false;
    }
    return e.vm_global_va >= preferred_base;
}
static_assert(windows_fit(ap) && windows_fit(fl), "a window exceeds the compare buffer or a fixup its window");

inline constexpr unsigned nibble(char c) {
    return c >= '0' && c <= '9' ? unsigned(c - '0') : c >= 'a' && c <= 'f' ? unsigned(c - 'a' + 10) : 0u;
}
inline std::uint32_t le32(const unsigned char* p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
// The windows of `e` at module base `base`: nullptr when every window matches, else the first window's name.
// `read(at, out, n)` reads n code bytes (false when unreadable). A relocated dword must hold its preferred value
// rebased to `base`; every other byte must equal the file's.
template <class Read> const char* first_mismatch(Read&& read, const Edition& e, std::uint32_t base) {
    for (unsigned i = 0; i < e.window_count; ++i) {
        const Window& w = e.windows[i];
        const unsigned n = hex_length(w.hex);
        const std::uint64_t at = std::uint64_t(base) + (w.va - preferred_base);
        unsigned char expected[window_capacity], actual[window_capacity];
        if (n > window_capacity || at + n > 0x100000000ull || !read(std::uint32_t(at), actual, n)) return w.name;
        for (unsigned k = 0; k < n; ++k)
            expected[k] = (unsigned char)(nibble(w.hex[2 * k]) << 4 | nibble(w.hex[2 * k + 1]));
        for (unsigned r = 0; r < w.relocs; ++r) {
            const unsigned o = w.reloc[r];
            const std::uint32_t want = le32(expected + o) - preferred_base + base;
            if (le32(actual + o) != want) return w.name;
            for (unsigned k = 0; k < 4; ++k) actual[o + k] = expected[o + k];
        }
        for (unsigned k = 0; k < n; ++k)
            if (actual[k] != expected[k]) return w.name;
    }
    return nullptr;
}

// ---- The read (section 3, steps 4 to 10). Mem supplies `bool read(uint32_t at, void* out, unsigned n)`: a validated
// span, false ends the read with a named reason and no cache write. ----
enum class Reason : unsigned {
    none,         // known
    disabled,     // the windows or the executable refused at attach (the key is off; the pass is not)
    no_sector,    // the caller had no class-1 sector object (the cockpit sample's status names why)
    loading,      // the background table is empty: a load (the cache is dropped)
    sector_read,  // the header span [S+8, S+0x9e) is unreadable
    sector_class, // [S+0x48] != 1
    sector_freed, // [S+0x9c] != 0xcafe (0xefac: freed)
    sector_id,    // [S+8] == 0
    sector_kc,    // [S+0x94] >= 0: no KC object linked
    no_vm,        // the VM global is 0 or unreadable
    kc_map,       // the object hash header or a bucket slot is unreadable or malformed
    kc_missing,   // the chain ended without the key
    kc_limit,     // hash_link_limit nodes visited without the key
    kc_object,    // the object is unreadable, misaligned or its +0 is not the id
    kc_class,     // its class record is not 2001
    kc_cells,     // m0/m1 unreadable, not int cells, or outside 0..255
    kc_mismatch   // the native-id member is not an int equal to [S+8]
};
inline const char* name(Reason r) {
    switch (r) {
    case Reason::none: return "none";
    case Reason::disabled: return "disabled";
    case Reason::no_sector: return "no_sector";
    case Reason::loading: return "loading";
    case Reason::sector_read: return "sector_read";
    case Reason::sector_class: return "sector_class";
    case Reason::sector_freed: return "sector_freed";
    case Reason::sector_id: return "sector_id";
    case Reason::sector_kc: return "sector_kc";
    case Reason::no_vm: return "no_vm";
    case Reason::kc_map: return "kc_map";
    case Reason::kc_missing: return "kc_missing";
    case Reason::kc_limit: return "kc_limit";
    case Reason::kc_object: return "kc_object";
    case Reason::kc_class: return "kc_class";
    case Reason::kc_cells: return "kc_cells";
    case Reason::kc_mismatch: return "kc_mismatch";
    }
    return "invalid";
}
// The running edition's bindings (the VM global rebased to the module base).
struct Layout {
    std::uint32_t vm_global = 0;
    unsigned native_id_member = 0;
};
inline Layout layout(const Edition& e, std::uint32_t base) {
    return Layout{e.vm_global_va - preferred_base + base, e.native_id_member};
}
struct Result {
    bool known = false, cached = false;
    Reason reason = Reason::disabled;
    std::uint32_t sector = 0, id = 0, kc = 0; // kc: the raw [S+0x94] (a negative KC id)
    std::int32_t x = -1, y = -1;
    std::uint64_t key = 0; // key_of(x, y) when known, else 0
    unsigned links = 0;    // hash nodes visited by this read (0 on a cache hit)
};
// The last known read, keyed by (S, [S+8], [S+0x94]): one header span per frame while they hold.
struct Cache {
    bool valid = false;
    std::uint32_t sector = 0, id = 0, kc = 0;
    std::int32_t x = 0, y = 0;
    void clear() { valid = false; }
};
template <class Mem> bool span(Mem& m, std::uint32_t base, std::uint64_t offset, void* out, unsigned n) {
    const std::uint64_t at = std::uint64_t(base) + offset;
    return base && at + n <= 0x100000000ull && m.read(std::uint32_t(at), out, n);
}
template <class Mem> bool u32(Mem& m, std::uint32_t base, std::uint64_t offset, std::uint32_t& out) {
    unsigned char b[4];
    if (!span(m, base, offset, b, 4)) return false;
    out = le32(b);
    return true;
}
inline Result refuse(Result r, Reason why, Cache& cache) {
    cache.clear();
    r.known = false;
    r.reason = why;
    r.key = 0;
    return r;
}
template <class Mem> Result read(Mem& m, const Layout& layout, std::uint32_t sector, Cache& cache) {
    Result r;
    r.sector = sector;
    if (!sector) return refuse(r, Reason::no_sector, cache);
    // Step 4: one header span.
    unsigned char h[sector_header_end - sector_header_begin];
    if ((sector & 3) || !span(m, sector, sector_header_begin, h, sizeof h)) return refuse(r, Reason::sector_read, cache);
    r.id = le32(h + (sector_id - sector_header_begin));
    r.kc = le32(h + (sector_event_object - sector_header_begin));
    const unsigned c = sector_class - sector_header_begin, a = sector_alive - sector_header_begin;
    const std::uint16_t klass = std::uint16_t(h[c] | h[c + 1] << 8), alive = std::uint16_t(h[a] | h[a + 1] << 8);
    if (klass != class_sector) return refuse(r, Reason::sector_class, cache);
    if (alive != alive_marker) return refuse(r, Reason::sector_freed, cache);
    if (!r.id) return refuse(r, Reason::sector_id, cache);
    if (!(r.kc & 0x80000000u)) return refuse(r, Reason::sector_kc, cache);
    // Step 5: the cached read while (S, id, kc) hold.
    if (cache.valid && cache.sector == sector && cache.id == r.id && cache.kc == r.kc) {
        r.known = r.cached = true;
        r.reason = Reason::none;
        r.x = cache.x;
        r.y = cache.y;
        r.key = key_of(r.x, r.y);
        return r;
    }
    // Step 6: the VM and its object hash.
    std::uint32_t vm = 0, map = 0, buckets = 0, capacity = 0;
    if (!u32(m, layout.vm_global, 0, vm) || !vm) return refuse(r, Reason::no_vm, cache);
    if (!u32(m, vm, vm_object_hash, map) || !map || (map & 3) || !u32(m, map, 0, buckets) || !u32(m, map, 4, capacity) ||
        !buckets || (buckets & 3) || !capacity || capacity > hash_capacity_max || (capacity & (capacity - 1)))
        return refuse(r, Reason::kc_map, cache);
    // Step 7: the bucket chain of ~id, at most hash_link_limit nodes.
    const std::uint32_t key = ~r.kc;
    std::uint32_t node = 0, object = 0;
    if (!u32(m, buckets, 4ull * (key & (capacity - 1)), node)) return refuse(r, Reason::kc_map, cache);
    for (;;) {
        if (!node) return refuse(r, Reason::kc_missing, cache);
        if (r.links == hash_link_limit) return refuse(r, Reason::kc_limit, cache);
        unsigned char link[12];
        ++r.links;
        if ((node & 3) || !span(m, node, 0, link, sizeof link)) return refuse(r, Reason::kc_map, cache);
        if (le32(link + 4) == key) {
            object = le32(link + 8);
            break;
        }
        node = le32(link);
    }
    // Step 8: the object and its class.
    unsigned char o[16];
    if (!object || (object & 3) || !span(m, object, 0, o, sizeof o) || le32(o) != r.kc)
        return refuse(r, Reason::kc_object, cache);
    std::uint32_t klass_number = 0;
    const std::uint32_t record = le32(o + 8), cells = le32(o + 12);
    if (!u32(m, record, 0, klass_number) || std::int32_t(klass_number) != kc_class_sector)
        return refuse(r, Reason::kc_class, cache);
    // Step 9: m0, m1 and the native-id member.
    unsigned char xy[2 * kc_cell], own[kc_cell];
    if (!span(m, cells, 0, xy, sizeof xy) || xy[0] != kc_tag_int || xy[kc_cell] != kc_tag_int)
        return refuse(r, Reason::kc_cells, cache);
    const std::int32_t x = std::int32_t(le32(xy + 1)), y = std::int32_t(le32(xy + kc_cell + 1));
    if (x < 0 || x > axis_max || y < 0 || y > axis_max) return refuse(r, Reason::kc_cells, cache);
    if (!span(m, cells, std::uint64_t(kc_cell) * layout.native_id_member, own, sizeof own) || own[0] != kc_tag_int ||
        le32(own + 1) != r.id)
        return refuse(r, Reason::kc_mismatch, cache);
    // Step 10.
    cache.valid = true;
    cache.sector = sector;
    cache.id = r.id;
    cache.kc = r.kc;
    cache.x = x;
    cache.y = y;
    r.known = true;
    r.reason = Reason::none;
    r.x = x;
    r.y = y;
    r.key = key_of(x, y);
    return r;
}

// ---- The consumer's rule: the field follows the last known key; an unknown read never reseeds the sky. ----
struct Hold {
    std::uint64_t key = 0; // 0: no sector known yet (the mark's own field)
    // True when a known key differs from the held one (the field must be rebuilt with it).
    bool apply(const Result& r) {
        if (!r.known || !r.key || r.key == key) return false;
        key = r.key;
        return true;
    }
};
} // namespace x3m::sector_key::core
