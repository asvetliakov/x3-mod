// Host check of src/proxy/sector_key_core.h for verification/analysis/test_sector_key.py: the sector key read against a
// fabricated memory image (both editions' bindings: the VM global, the native-id member), every refusal of the chain,
// the 64-link cap, the cache on (S, [S+8], [S+0x94]) and a stale S, the byte-window comparator at the preferred and a
// relocated base, and the consumer's hold rule. Prints one JSON object (the window tables included, for the test's
// comparison with the verifier). No Windows, no game bytes.
#include "../../src/proxy/sector_key_core.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace core = x3m::sector_key::core;

static unsigned checks = 0, failures = 0;
static std::string failed;
static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) {
        ++failures;
        if (failed.size() < 2000) failed += std::string(failed.empty() ? "" : ",") + what;
    }
}
// A sparse little-endian memory: byte map; a read of any unmapped byte fails (an unreadable span).
struct Memory {
    std::map<std::uint32_t, unsigned char> bytes;
    unsigned reads = 0;
    bool read(std::uint32_t at, void* out, unsigned n) {
        ++reads;
        auto* o = static_cast<unsigned char*>(out);
        for (unsigned i = 0; i < n; ++i) {
            auto it = bytes.find(at + i);
            if (it == bytes.end()) return false;
            o[i] = it->second;
        }
        return true;
    }
    void put(std::uint32_t at, const void* p, unsigned n) {
        for (unsigned i = 0; i < n; ++i) bytes[at + i] = static_cast<const unsigned char*>(p)[i];
    }
    void u8(std::uint32_t at, std::uint8_t v) { put(at, &v, 1); }
    void u16(std::uint32_t at, std::uint16_t v) {
        unsigned char b[2] = {std::uint8_t(v), std::uint8_t(v >> 8)};
        put(at, b, 2);
    }
    void u32(std::uint32_t at, std::uint32_t v) {
        unsigned char b[4] = {std::uint8_t(v), std::uint8_t(v >> 8), std::uint8_t(v >> 16), std::uint8_t(v >> 24)};
        put(at, b, 4);
    }
    void zero(std::uint32_t at, unsigned n) {
        for (unsigned i = 0; i < n; ++i) bytes[at + i] = 0;
    }
    void cell(std::uint32_t cells, unsigned member, std::uint8_t tag, std::uint32_t value) {
        u8(cells + 5 * member, tag);
        u32(cells + 5 * member + 1, value);
    }
};
// One fabricated world: a sector S of native id `id` linked to KC id `kc` (negative), the VM, its object hash of
// `capacity` buckets with `before` non-matching nodes ahead of the object's node in its chain, the object of class
// 2001 with m0 = x, m1 = y and the native id in member `member`.
struct World {
    static constexpr std::uint32_t S = 0x10000000, vm = 0x20000000, map = 0x20100000, buckets = 0x20200000,
                                   nodes = 0x20300000, object = 0x20400000, record = 0x20500000, cells = 0x20600000;
    Memory m;
    core::Layout layout;
    std::uint32_t id = 3221, kc = std::uint32_t(-1234), capacity = 256;
    World(const core::Edition& e, std::uint32_t base, std::int32_t x, std::int32_t y, unsigned before = 0) {
        layout = core::layout(e, base);
        m.zero(S, 0x180);
        m.u32(S + 8, id);
        m.u16(S + 0x48, 1);
        m.u32(S + 0x94, kc);
        m.u16(S + 0x9c, 0xcafe);
        m.u32(layout.vm_global, vm);
        m.zero(vm, 0x1300);
        m.u32(vm + 0x12d0, map);
        m.u32(map, buckets);
        m.u32(map + 4, capacity);
        m.zero(buckets, 4 * capacity);
        const std::uint32_t key = ~kc;
        // The chain: `before` decoys (other keys of the same bucket), then the object's node.
        std::uint32_t first = before ? nodes : nodes + 12 * 200;
        for (unsigned i = 0; i < before; ++i) {
            m.u32(nodes + 12 * i, i + 1 < before ? nodes + 12 * (i + 1) : nodes + 12 * 200);
            m.u32(nodes + 12 * i + 4, key + capacity * (i + 1)); // same bucket, another key
            m.u32(nodes + 12 * i + 8, 0x30000000 + i);
        }
        m.u32(nodes + 12 * 200, 0);
        m.u32(nodes + 12 * 200 + 4, key);
        m.u32(nodes + 12 * 200 + 8, object);
        m.u32(buckets + 4 * (key & (capacity - 1)), first);
        m.u32(object, kc);
        m.u32(object + 4, 0);
        m.u32(object + 8, record);
        m.u32(object + 12, cells);
        m.u32(record, 2001);
        m.zero(cells, 5 * 40);
        m.cell(cells, 0, 1, std::uint32_t(x));
        m.cell(cells, 1, 1, std::uint32_t(y));
        m.cell(cells, e.native_id_member, 1, id);
    }
    core::Result read(core::Cache& cache) { return core::read(m, layout, S, cache); }
};
static bool is(const core::Result& r, core::Reason why) { return !r.known && r.reason == why && !r.key; }

int main() {
    // ---- 1. Both editions: known (x, y), the key, the links, the cache hit's single span.
    {
        World ap(core::ap, core::preferred_base, 1, 3);
        core::Cache cache;
        const auto r = ap.read(cache);
        check(r.known && r.reason == core::Reason::none && r.x == 1 && r.y == 3 && r.key == 0x20004 && r.links == 1 &&
                  !r.cached && r.id == 3221 && r.kc == std::uint32_t(-1234) && cache.valid,
              "ap_known");
        ap.m.reads = 0;
        const auto again = ap.read(cache);
        check(again.known && again.cached && again.key == r.key && again.links == 0 && ap.m.reads == 1,
              "ap_cached_one_span");
        World fl(core::fl, 0x00e30000, 12, 18);
        core::Cache fc;
        const auto f = fl.read(fc);
        check(fl.layout.vm_global == 0x005aa174 - 0x00400000 + 0x00e30000 && fl.layout.native_id_member == 36,
              "fl_layout_rebased");
        check(f.known && f.x == 12 && f.y == 18 && f.key == (13ull << 16 | 19), "fl_known");
        // The native id in m34 only (AP's member): FL refuses; in m36 only: AP refuses.
        World ap_on_fl(core::fl, 0x00e30000, 1, 3);
        ap_on_fl.m.cell(World::cells, 36, 1, 0);
        ap_on_fl.m.cell(World::cells, 34, 1, ap_on_fl.id);
        core::Cache c1;
        check(is(ap_on_fl.read(c1), core::Reason::kc_mismatch), "fl_member36_cross_check");
        World fl_on_ap(core::ap, core::preferred_base, 1, 3);
        fl_on_ap.m.cell(World::cells, 34, 1, 0);
        fl_on_ap.m.cell(World::cells, 36, 1, fl_on_ap.id);
        core::Cache c2;
        check(is(fl_on_ap.read(c2), core::Reason::kc_mismatch), "ap_member34_cross_check");
    }
    // ---- 2. The key: never 0, distinct per (x, y) over the accepted range.
    {
        bool distinct = core::key_of(0, 0) == 0x10001 && core::key_of(255, 255) == (256ull << 16 | 256);
        std::map<std::uint64_t, int> seen;
        for (int x = 0; x <= 255; ++x)
            for (int y = 0; y <= 255; ++y) distinct = distinct && core::key_of(x, y) && seen.emplace(core::key_of(x, y), 1).second;
        check(distinct, "key_nonzero_distinct");
    }
    // ---- 3. The cross-checks and the refusals of each step (each refusal clears the cache).
    {
        struct Case {
            const char* name;
            core::Reason reason;
            void (*edit)(World&);
        };
        const Case cases[] = {
            {"native_id_not_int", core::Reason::kc_mismatch, [](World& w) { w.m.u8(World::cells + 5 * 34, 2); }},
            {"native_id_other", core::Reason::kc_mismatch, [](World& w) { w.m.u32(World::cells + 5 * 34 + 1, 2317); }},
            {"native_id_unreadable", core::Reason::kc_mismatch,
             [](World& w) { w.m.bytes.erase(World::cells + 5 * 34 + 4); }},
            {"class_not_2001", core::Reason::kc_class, [](World& w) { w.m.u32(World::record, 2000); }},
            {"class_unreadable", core::Reason::kc_class, [](World& w) { w.m.u32(World::object + 8, 0x7f000000); }},
            {"object_id_other", core::Reason::kc_object, [](World& w) { w.m.u32(World::object, std::uint32_t(-99)); }},
            {"object_null", core::Reason::kc_object, [](World& w) { w.m.u32(World::nodes + 12 * 200 + 8, 0); }},
            {"object_misaligned", core::Reason::kc_object,
             [](World& w) { w.m.u32(World::nodes + 12 * 200 + 8, World::object + 2); }},
            {"x_tag", core::Reason::kc_cells, [](World& w) { w.m.u8(World::cells, 5); }},
            {"y_tag", core::Reason::kc_cells, [](World& w) { w.m.u8(World::cells + 5, 0); }},
            {"x_negative", core::Reason::kc_cells, [](World& w) { w.m.u32(World::cells + 1, std::uint32_t(-1)); }},
            {"y_256", core::Reason::kc_cells, [](World& w) { w.m.u32(World::cells + 6, 256); }},
            {"cells_unreadable", core::Reason::kc_cells, [](World& w) { w.m.u32(World::object + 12, 0x7e000000); }},
            {"sector_class", core::Reason::sector_class, [](World& w) { w.m.u16(World::S + 0x48, 7); }},
            {"sector_freed", core::Reason::sector_freed, [](World& w) { w.m.u16(World::S + 0x9c, 0xefac); }},
            {"sector_id_zero", core::Reason::sector_id, [](World& w) { w.m.u32(World::S + 8, 0); }},
            {"sector_kc_positive", core::Reason::sector_kc, [](World& w) { w.m.u32(World::S + 0x94, 1234); }},
            {"sector_kc_zero", core::Reason::sector_kc, [](World& w) { w.m.u32(World::S + 0x94, 0); }},
            {"sector_unreadable", core::Reason::sector_read, [](World& w) { w.m.bytes.erase(World::S + 0x9d); }},
            {"vm_zero", core::Reason::no_vm, [](World& w) { w.m.u32(w.layout.vm_global, 0); }},
            {"vm_global_unreadable", core::Reason::no_vm, [](World& w) { w.m.bytes.erase(w.layout.vm_global); }},
            {"map_zero", core::Reason::kc_map, [](World& w) { w.m.u32(World::vm + 0x12d0, 0); }},
            {"capacity_not_pow2", core::Reason::kc_map, [](World& w) { w.m.u32(World::map + 4, 255); }},
            {"capacity_zero", core::Reason::kc_map, [](World& w) { w.m.u32(World::map + 4, 0); }},
            {"capacity_huge", core::Reason::kc_map, [](World& w) { w.m.u32(World::map + 4, 1u << 23); }},
            {"buckets_zero", core::Reason::kc_map, [](World& w) { w.m.u32(World::map, 0); }},
            {"bucket_unreadable", core::Reason::kc_map,
             [](World& w) { w.m.bytes.erase(World::buckets + 4 * ((~w.kc) & (w.capacity - 1))); }},
            {"node_misaligned", core::Reason::kc_map,
             [](World& w) { w.m.u32(World::buckets + 4 * ((~w.kc) & (w.capacity - 1)), World::nodes + 1); }},
            {"chain_empty", core::Reason::kc_missing,
             [](World& w) { w.m.u32(World::buckets + 4 * ((~w.kc) & (w.capacity - 1)), 0); }},
            {"key_other", core::Reason::kc_missing, [](World& w) { w.m.u32(World::nodes + 12 * 200 + 4, 5); }},
        };
        unsigned passed = 0;
        for (const Case& c : cases) {
            World w(core::ap, core::preferred_base, 4, 5);
            core::Cache cache;
            const bool first = w.read(cache).known && cache.valid;
            c.edit(w);
            // A header refusal ends a warm cache too and clears it; an edit past the header is not seen while
            // (S, [S+8], [S+0x94]) hold (the per-frame cost: one header span), the cached value is returned.
            const bool header = c.reason == core::Reason::sector_read || c.reason == core::Reason::sector_class ||
                                c.reason == core::Reason::sector_freed || c.reason == core::Reason::sector_id ||
                                c.reason == core::Reason::sector_kc;
            const auto warm = w.read(cache);
            const bool ok = first && (header ? is(warm, c.reason) && !cache.valid
                                             : warm.known && warm.cached && cache.valid);
            // The same edit on a fresh cache (a new sector, a clear) refuses with its reason and stores nothing.
            core::Cache fresh;
            const auto f = w.read(fresh);
            const bool same = is(f, c.reason) && !fresh.valid;
            check(ok && same, c.name);
            passed += ok && same;
        }
        std::printf("{\"refusal_cases\": %u, \"refusal_passed\": %u,\n", unsigned(sizeof cases / sizeof cases[0]), passed);
    }
    // ---- 4. The 64-link cap: the object at node 64 is found, at node 65 it is kc_limit.
    unsigned links_at_cap = 0;
    {
        World at(core::ap, core::preferred_base, 2, 2, 63);
        core::Cache cache;
        const auto r = at.read(cache);
        links_at_cap = r.links;
        check(r.known && r.links == 64, "link_64_found");
        World past(core::ap, core::preferred_base, 2, 2, 64);
        core::Cache c2;
        const auto p = past.read(c2);
        check(is(p, core::Reason::kc_limit) && p.links == 64, "link_65_limit");
        // A cycle in the chain ends at the cap too.
        World loop(core::ap, core::preferred_base, 2, 2, 2);
        loop.m.u32(World::nodes + 12, World::nodes); // node 1 -> node 0
        core::Cache c3;
        check(is(loop.read(c3), core::Reason::kc_limit), "chain_cycle_limit");
    }
    // ---- 5. A stale S: freed after a cached read, then reused at the same address by another sector.
    {
        World w(core::fl, 0x00e30000, 7, 9);
        core::Cache cache;
        const auto first = w.read(cache);
        w.m.u16(World::S + 0x9c, 0xefac); // SA_FreeObject's marker
        const auto freed = w.read(cache);
        const bool cleared = !cache.valid;
        // Reused: a new native id (2317 -> 3221 style), another KC id and object with other x/y.
        w.m.u16(World::S + 0x9c, 0xcafe);
        w.m.u32(World::S + 8, 4000);
        w.m.u32(World::S + 0x94, std::uint32_t(-77));
        const std::uint32_t key = ~std::uint32_t(-77);
        w.m.u32(World::buckets + 4 * (key & (w.capacity - 1)), World::nodes + 12 * 300);
        w.m.u32(World::nodes + 12 * 300, 0);
        w.m.u32(World::nodes + 12 * 300 + 4, key);
        w.m.u32(World::nodes + 12 * 300 + 8, 0x20700000);
        w.m.u32(0x20700000, std::uint32_t(-77));
        w.m.u32(0x20700004, 0);
        w.m.u32(0x20700008, World::record);
        w.m.u32(0x2070000c, 0x20800000);
        w.m.zero(0x20800000, 5 * 40);
        w.m.cell(0x20800000, 0, 1, 20);
        w.m.cell(0x20800000, 1, 1, 0);
        w.m.cell(0x20800000, 36, 1, 4000);
        const auto reused = w.read(cache);
        check(first.known && is(freed, core::Reason::sector_freed) && cleared, "stale_freed");
        check(reused.known && !reused.cached && reused.x == 20 && reused.y == 0 && reused.key == (21ull << 16 | 1),
              "stale_reused_rewalks");
        // Same S and id, the KC id changed only (a load restoring another link): walked again, not cached.
        World v(core::ap, core::preferred_base, 1, 1);
        core::Cache vc;
        v.read(vc);
        v.m.u32(World::S + 0x94, std::uint32_t(-5));
        check(!v.read(vc).cached, "kc_change_rewalks");
        // An explicit clear (the route's load epoch) walks again with the same (S, id, kc).
        World u(core::ap, core::preferred_base, 1, 1);
        core::Cache uc;
        u.read(uc);
        uc.clear();
        check(!u.read(uc).cached, "clear_rewalks");
    }
    // ---- 6. The consumer's hold: unknown keeps, a new known key changes once, key 0 never.
    {
        core::Hold h;
        core::Result unknown;
        core::Result a;
        a.known = true;
        a.key = core::key_of(1, 3);
        core::Result b = a;
        b.key = core::key_of(2, 3);
        core::Result zero = a;
        zero.key = 0;
        const bool s0 = !h.apply(unknown) && h.key == 0;
        const bool s1 = h.apply(a) && h.key == a.key;
        const bool s2 = !h.apply(a) && !h.apply(unknown) && h.key == a.key;
        const bool s3 = h.apply(b) && h.key == b.key && !h.apply(zero) && h.key == b.key;
        const bool s4 = h.apply(a) && h.key == a.key; // a return to the first sector: its own field again
        check(s0 && s1 && s2 && s3 && s4, "hold_rule");
    }
    // ---- 7. The windows at the preferred base and, for FL, rebased.
    unsigned ap_bytes = 0, fl_bytes = 0, fl_relocs = 0;
    {
        auto image = [](const core::Edition& e, std::uint32_t base, bool rebase, Memory& m, unsigned* bytes,
                        unsigned* relocs) {
            for (unsigned i = 0; i < e.window_count; ++i) {
                const auto& w = e.windows[i];
                const unsigned n = core::hex_length(w.hex);
                unsigned char b[core::window_capacity];
                for (unsigned k = 0; k < n; ++k) b[k] = (unsigned char)(core::nibble(w.hex[2 * k]) << 4 | core::nibble(w.hex[2 * k + 1]));
                if (rebase)
                    for (unsigned r = 0; r < w.relocs; ++r) {
                        const std::uint32_t v = core::le32(b + w.reloc[r]) - core::preferred_base + base;
                        std::memcpy(b + w.reloc[r], &v, 4);
                    }
                m.put(w.va - core::preferred_base + base, b, n);
                if (bytes) *bytes += n;
                if (relocs) *relocs += w.relocs;
            }
        };
        auto reader = [](Memory& m) { return [&m](std::uint32_t at, unsigned char* out, unsigned n) { return m.read(at, out, n); }; };
        Memory a;
        image(core::ap, core::preferred_base, true, a, &ap_bytes, nullptr);
        check(!core::first_mismatch(reader(a), core::ap, core::preferred_base), "ap_windows_match");
        a.bytes[0x0049f1e0 + 69] ^= 1;
        const char* bad = core::first_mismatch(reader(a), core::ap, core::preferred_base);
        check(bad && !std::strcmp(bad, "kc_object_lookup"), "ap_last_byte_compared");
        Memory gap;
        image(core::ap, core::preferred_base, true, gap, nullptr, nullptr);
        gap.bytes.erase(0x004031e8 + 11);
        bad = core::first_mismatch(reader(gap), core::ap, core::preferred_base);
        check(bad && !std::strcmp(bad, "vm_create_store"), "ap_unreadable_refused");
        const std::uint32_t base = 0x00e30000;
        Memory f;
        image(core::fl, base, true, f, &fl_bytes, &fl_relocs);
        check(!core::first_mismatch(reader(f), core::fl, base), "fl_rebased_match");
        Memory raw;
        image(core::fl, base, false, raw, nullptr, nullptr); // the file's bytes at a moved base: the fixups refuse
        bad = core::first_mismatch(reader(raw), core::fl, base);
        check(bad && !std::strcmp(bad, "player_root"), "fl_unrebased_refused");
        Memory pref;
        image(core::fl, core::preferred_base, true, pref, nullptr, nullptr);
        check(!core::first_mismatch(reader(pref), core::fl, core::preferred_base), "fl_preferred_match");
        f.bytes[0x0046a7c1 - core::preferred_base + base + 44] ^= 0x80; // inside the +43 fixup of sa_set_event_object
        bad = core::first_mismatch(reader(f), core::fl, base);
        check(bad && !std::strcmp(bad, "sa_set_event_object"), "fl_fixup_value_compared");
        // The AP windows on an FL image (and the reverse) refuse.
        check(core::first_mismatch(reader(pref), core::ap, core::preferred_base) != nullptr &&
                  core::first_mismatch(reader(a), core::fl, core::preferred_base) != nullptr,
              "editions_disjoint");
    }
    std::printf("\"links_at_cap\": %u, \"ap_window_bytes\": %u, \"fl_window_bytes\": %u, \"fl_fixups\": %u,\n",
                links_at_cap, ap_bytes, fl_bytes, fl_relocs);
    std::printf("\"windows\": {");
    const core::Edition* editions[] = {&core::ap, &core::fl};
    for (unsigned e = 0; e < 2; ++e) {
        std::printf("%s\"%s\": [", e ? ", " : "", editions[e]->name);
        for (unsigned i = 0; i < editions[e]->window_count; ++i) {
            const auto& w = editions[e]->windows[i];
            std::printf("%s[\"%s\", %u, \"%s\", [", i ? ", " : "", w.name, unsigned(w.va), w.hex);
            for (unsigned r = 0; r < w.relocs; ++r) std::printf("%s%u", r ? ", " : "", w.reloc[r]);
            std::printf("]]");
        }
        std::printf("]");
    }
    std::printf("},\n\"bindings\": {\"ap\": [%u, %u], \"fl\": [%u, %u]},\n", unsigned(core::ap.vm_global_va),
                core::ap.native_id_member, unsigned(core::fl.vm_global_va), core::fl.native_id_member);
    std::printf("\"ok\": %s, \"checks\": %u, \"failures\": %u, \"failed\": \"%s\"}\n", failures ? "false" : "true", checks,
                failures, failed.c_str());
    return failures ? 1 : 0;
}
