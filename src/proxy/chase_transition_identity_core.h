#pragma once
#include <cstdint>
#include <cstring>

namespace x3m::chase_transition::detail {
// Fresh scalar observations, never retained engine references. A valid zero is
// distinct from unavailable (neither bit) and refused (refused bit). Live bits
// require registry membership, not merely a readable scalar or equal pointer.
struct Identity {
    enum : std::uint32_t {
        vm_bit = 1,
        native_bit = 2,
        player_bit = 4,
        controller_bit = 8,
        monitor_bit = 16,
        mode_bit = 32,
        ref_bit = 64,
        warp_bit = 128,
        killed_bit = 256,
        player_live_bit = 512,
        controller_live_bit = 1024,
        native_live_bit = 2048
    };
    std::uint32_t valid = 0, refused = 0, vm = 0, native_id = 0, native_script = 0, player = 0, controller = 0,
                  monitor = 0, mode = 0, ref = 0, warp = 0, killed = 0;
    std::uint32_t tags[6]{};      // player, controller, mode, ref, warp, killed
    std::uint32_t class_ids[4]{}; // native script, player, controller, monitor; evidence only
};
// Limits are diagnostic refusal limits, not invented engine capacities. All
// pointer arithmetic, tags, membership and selected descriptor IDs are checked.
// No registry walk, borrowed pointer or C++ lock survives the callback.
template <class Reader> struct IdentityReader {
    Reader bytes;
    // Process roots (VM pointer, native body registry); fixtures may alias them.
    std::uint32_t vm_root = 0x6085e4, registry_root = 0x60850c;
    std::uint32_t vm = 0, classes = 0, count = 0;
    static bool pointer(std::uint32_t p) { return p && !(p & 3); }
    bool read(std::uint32_t p, unsigned off, void* out, unsigned n) {
        return p && off <= UINT32_MAX - p && n && n <= UINT32_MAX - (p + off) && bytes(p, off, out, n);
    }
    template <class T> bool field(std::uint32_t p, unsigned off, T& out) { return read(p, off, &out, sizeof out); }
    bool root() {
        return field(vm_root, 0, vm) && pointer(vm) && field(vm, 0x1c, count) && count && count <= 4096 &&
               field(vm, 0x20, classes) && pointer(classes) && count <= (UINT32_MAX - classes) / 0x38;
    }
    bool descriptor(std::uint32_t p, std::uint32_t (&row)[14]) {
        return p >= classes && (p - classes) % 0x38 == 0 && (p - classes) / 0x38 < count && field(p, 0, row) &&
               row[0] <= INT32_MAX && row[2] == p && row[7] <= 65536;
    }
    bool static_class(std::uint32_t id, std::uint32_t& p) {
        unsigned lo = 0, hi = count;
        // Native 4b06f0 uses signed-ID binary search over 0x38-byte rows.
        for (unsigned step = 0; lo < hi && step < 13; ++step) {
            const auto mid = lo + (hi - lo) / 2;
            std::uint32_t found = 0;
            const auto at = classes + mid * 0x38;
            if (!field(at, 0, found) || found > INT32_MAX) return false;
            if (found == id) {
                p = at;
                return true;
            }
            if (found < id)
                lo = mid + 1;
            else
                hi = mid;
        }
        return false;
    }
    bool hash(std::uint32_t table, std::uint32_t key, std::uint32_t& value) {
        std::uint32_t bucket[2]{}, link = 0, found = 0;
        bool matched = false;
        if (!pointer(table) || !field(table, 0, bucket) || !pointer(bucket[0]) || !bucket[1] || bucket[1] > 65536 ||
            (bucket[1] & (bucket[1] - 1)) || !field(bucket[0], 4 * (key & (bucket[1] - 1)), link))
            return false;
        for (unsigned i = 0; link && i < 32; ++i) {
            std::uint32_t row[3]{};
            if (!pointer(link) || !field(link, 0, row)) return false;
            if (row[1] == key) {
                if (matched) return false;
                matched = true;
                found = row[2];
            }
            link = row[0];
        }
        // No partial-chain or duplicate-key identity, including cycles.
        if (link || !matched || !pointer(found)) return false;
        value = found;
        return true;
    }
    bool context(std::uint32_t id, std::uint32_t& p, std::uint32_t (&desc)[14]) {
        if (id <= INT32_MAX) {
            if (!static_class(id, p)) return false;
        } else {
            std::uint32_t table = 0;
            if (!field(vm, 0x12d0, table) || !hash(table, ~id, p)) return false;
        }
        std::uint32_t head[3]{};
        return pointer(p) && field(p, 0, head) && head[0] == id && descriptor(head[2], desc);
    }
    bool cell(std::uint32_t context, const std::uint32_t (&desc)[14], unsigned index, std::uint32_t& tag,
              std::uint32_t& value) {
        std::uint32_t cells = 0;
        unsigned char raw[5]{};
        if (index >= desc[7] || !field(context, 0xc, cells) || !read(cells, 5 * index, raw, 5)) return false;
        tag = raw[0];
        std::memcpy(&value, raw + 1, 4);
        // 4a8600 / integer literal handlers produce tag 1. Other tags are raw
        // diagnostic payloads only; never coerce strings/references/return PCs.
        return tag == 1;
    }
    void capture(std::uint32_t ship, std::uint32_t monitor_context, Identity& out) {
        using I = Identity;
        auto mark = [&](unsigned bit, bool ok) { (ok ? out.valid : out.refused) |= bit; };
        // SA_GetEventObject 4607f4 reads +94 only after native ID lookup.
        if (ship) {
            std::uint32_t registry = 0, table = 0, live = 0;
            const bool ok = pointer(ship) && field(ship, 8, out.native_id) && field(registry_root, 0, registry) &&
                            pointer(registry) && field(registry, 0x14, table) && hash(table, out.native_id, live) &&
                            live == ship && field(ship, 0x94, out.native_script);
            mark(I::native_bit, ok);
        }
        const bool rooted = root();
        out.vm = vm;
        mark(I::vm_bit, rooted);
        if (!rooted) {
            out.refused |= I::player_bit | I::controller_bit | I::warp_bit | I::killed_bit;
            if (monitor_context) out.refused |= I::monitor_bit | I::mode_bit | I::ref_bit;
            return;
        }
        std::uint32_t global[14]{}, warp_desc[14]{}, warp_context = 0;
        const bool globals = descriptor(classes, global) && global[0] == 0;
        mark(I::player_bit, globals && cell(classes, global, 9, out.tags[0], out.player));
        mark(I::controller_bit, globals && cell(classes, global, 8, out.tags[1], out.controller));
        auto live_id = [&](unsigned value_bit, unsigned live_bit, std::uint32_t id, unsigned class_index) {
            if (!(out.valid & value_bit)) return;
            std::uint32_t p = 0, d[14]{};
            // A zero scalar is measurable, but is not a live player object.
            const bool ok = id != 0 && context(id, p, d);
            if (ok) out.class_ids[class_index] = d[0];
            mark(live_bit, ok);
        };
        live_id(I::player_bit, I::player_live_bit, out.player, 1);
        live_id(I::controller_bit, I::controller_live_bit, out.controller, 2);
        live_id(I::native_bit, I::native_live_bit, out.native_script, 0);
        const bool warp_ok = context(0x96, warp_context, warp_desc) && warp_desc[0] == 0x96;
        mark(I::warp_bit, warp_ok && cell(warp_context, warp_desc, 3, out.tags[4], out.warp));
        mark(I::killed_bit, warp_ok && cell(warp_context, warp_desc, 6, out.tags[5], out.killed));
        if (monitor_context) {
            std::uint32_t resolved = 0, d[14]{};
            const bool ok = pointer(monitor_context) && field(monitor_context, 0, out.monitor) &&
                            context(out.monitor, resolved, d) && resolved == monitor_context;
            if (ok) out.class_ids[3] = d[0];
            const bool monitor_ok = ok && d[0] == 0x25e;
            mark(I::monitor_bit, monitor_ok);
            mark(I::mode_bit, monitor_ok && cell(resolved, d, 0, out.tags[2], out.mode));
            mark(I::ref_bit, monitor_ok && cell(resolved, d, 11, out.tags[3], out.ref));
        }
    }
};
}
