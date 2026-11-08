#pragma once
#include "weapon_effects_core.h"
#include <cstddef>

// The projectile redo's game-thread half (docs/architecture/projectile-look.md, "Implementation (2026-11-08)"; the FL
// anchors in docs/reverse-engineering/fl/bullet-draw-batch.md). Integer only: compiled into weapon_effects_hook.cpp with
// -mno-sse -mfpmath=387 and holding no floating point; no call, no allocation, no Win32 or CRT. Portable (the host
// tests, both fixtures and the stage compile it).
//
// The pool of live bullets of the enabled bodies: the object tap (FL 0x0645bd -> 0x014d90, every live object of the
// sector once per update pass) registers each class-0 object whose node body id (node +0x144) is in the enabled set,
// keyed by its node in a 1024-slot open-addressed table (linear probing, at most max_probes, no tombstones: removal
// shifts the chain back), the entries dense (512, swap-removed). The enabled set is built once: the table load names the
// enabled rows (weapon_projectile_family.h resolve_rows), and the first tap that can read the engine's TBullets turns
// each row into its body id (word +0 of the row) in a fixed list of at most 64 bodies and a 65536-bit set (the batch
// stub's and the draw hook's test). A seen entry refreshes its object, handle (node +0x28: a recycled node starts over),
// expiry ext+0 and birth ext+8. A full pool counts the overflow: that bullet is drawn by nobody while the batch skip is
// latched, so the overflow is the kill criterion for raising the capacity. The stage (render thread = the game thread
// in X3) validates each entry through the bounded engine reader before every read and retires an expired, recycled or
// unreadable one.
namespace x3m::weapon_effects::core {
namespace projectile {
constexpr unsigned pool_capacity = 512, slot_count = 1024, max_probes = 32;
constexpr unsigned max_bodies = 64, body_bits = 65536; // a body id is a zero-extended word (movzx, 0x049b70)
constexpr unsigned type_body = 0x0;                    // TBullets +0x0: the body id (word) = node +0x144 (note section 4)
constexpr unsigned node_velocity = 0x100;              // engine units per second (includes the shooter's)
constexpr unsigned ext_expiry = 0, ext_created = 8;    // game ms
// The engine's game-time step of the frame (fl/seta.md: AP cfg +0x714, FL +0x704; ms, SETA included).
constexpr unsigned cfg_step_ms = edition_fl ? 0x704 : 0x714;
static_assert((slot_count & (slot_count - 1u)) == 0 && slot_count >= 2 * pool_capacity, "half full at most");
}
struct ProjectileEntry {
    std::uint32_t node, obj, handle; // the key, its object and node +0x28 at the last tap
    std::uint32_t expiry, created;   // ext+0 / ext+8 at the last tap (game ms)
    std::uint32_t body;              // index into ProjectilePool::bodies (its per-body look, resolved once)
};
struct ProjectileBody {
    std::uint32_t id;         // the engine body id (node +0x144)
    std::uint16_t row;        // the enabled row that named it (its colour and calibre)
    std::uint8_t family, pad; // weapon_projectile_family.h families[]
};
struct ProjectileCounters {
    std::uint32_t taps = 0, registered = 0, recycled = 0, overflow = 0, probe_full = 0;
};
// The batch stub reads skip_on, bumps skipped and tests bits at fixed offsets (the static_assert below).
struct ProjectilePool {
    volatile std::uint32_t skip_on = 0; // Present's latch: the stage drew the previous frame, the bodies are resolved
    std::uint32_t skipped = 0;          // batch submits the stub skipped (monotonic)
    std::uint32_t bits[projectile::body_bits / 32]{}; // the enabled bodies
    std::uint8_t armed = 0;             // the object tap registers (FL stage modes, set at install)
    std::uint8_t resolved = 0;          // the rows became bodies (the engine's TBullets was readable)
    std::uint32_t row_count = 0;        // the enabled rows of the table load (resolve_rows)
    std::uint16_t rows[projectile::max_bodies]{};
    std::uint8_t row_family[projectile::max_bodies]{};
    std::uint32_t body_count = 0;
    ProjectileBody bodies[projectile::max_bodies]{};
    std::uint32_t count = 0;
    ProjectileEntry entries[projectile::pool_capacity]{};
    std::uint16_t slots[projectile::slot_count]{}; // entry index + 1, 0 empty
    Globals globals{};
    ProjectileCounters counters{};
};
static_assert(offsetof(ProjectilePool, skip_on) == 0 && offsetof(ProjectilePool, skipped) == 4 &&
                  offsetof(ProjectilePool, bits) == 8,
              "the batch stub's offsets (weapon_effects_hook.cpp)");
inline bool projectile_body_enabled(const ProjectilePool& p, std::uint32_t id) noexcept {
    return id < projectile::body_bits && ((p.bits[id >> 5] >> (id & 31u)) & 1u);
}
inline unsigned projectile_home(std::uint32_t node) noexcept {
    return unsigned((node * 0x9e3779b1u) >> 22) & (projectile::slot_count - 1u);
}
// The slot holding node, or slot_count.
inline unsigned projectile_slot(const ProjectilePool& p, std::uint32_t node) noexcept {
    unsigned h = projectile_home(node);
    for (unsigned k = 0; k < projectile::max_probes; ++k, h = (h + 1u) & (projectile::slot_count - 1u)) {
        const unsigned s = p.slots[h];
        if (!s) return projectile::slot_count;
        if (p.entries[s - 1u].node == node) return h;
    }
    return projectile::slot_count;
}
inline bool projectile_known(const ProjectilePool& p, std::uint32_t node) noexcept {
    return p.count && projectile_slot(p, node) < projectile::slot_count;
}
// Entry index leaves the pool: its slot is emptied and the chain behind it shifted back (no tombstone), then the last
// entry takes its place in the dense array. A caller walking the entries goes from the last to the first.
inline void projectile_remove(ProjectilePool& p, unsigned index) noexcept {
    if (index >= p.count) return;
    constexpr unsigned mask = projectile::slot_count - 1u;
    unsigned i = projectile_slot(p, p.entries[index].node);
    if (i < projectile::slot_count) {
        for (;;) {
            unsigned j = i;
            bool moved = false;
            for (unsigned k = 0; k < projectile::slot_count; ++k) {
                j = (j + 1u) & mask;
                if (!p.slots[j]) break;
                const unsigned home = projectile_home(p.entries[p.slots[j] - 1u].node);
                // the entry at j stays when its home lies cyclically in (i, j]
                if (i <= j ? (i < home && home <= j) : (i < home || home <= j)) continue;
                p.slots[i] = p.slots[j];
                i = j;
                moved = true;
                break;
            }
            if (!moved) break;
        }
        p.slots[i] = 0;
    }
    const unsigned last = p.count - 1u;
    if (index != last) {
        const unsigned s = projectile_slot(p, p.entries[last].node);
        if (s < projectile::slot_count) p.slots[s] = std::uint16_t(index + 1u);
        p.entries[index] = p.entries[last];
    }
    p.count = last;
}
inline void projectile_clear(ProjectilePool& p) noexcept {
    for (unsigned i = 0; i < projectile::slot_count; ++i) p.slots[i] = 0;
    p.count = 0;
}
// The enabled rows into body ids, once the engine's TBullets is readable (false until then, the slots unbound
// included). A row past the live count is skipped; rows sharing a body keep the first row's look.
inline bool projectile_resolve(ProjectilePool& p) noexcept {
    const Globals& g = p.globals;
    if (!g.bullet_table_slot || !g.bullet_count_slot) return false;
    const std::uintptr_t table = rd32(g.bullet_table_slot);
    const std::int32_t rows = std::int32_t(rd32(g.bullet_count_slot));
    if (!table || rows <= 0) return false;
    for (unsigned i = 0; i < p.row_count && p.body_count < projectile::max_bodies; ++i) {
        if (std::int32_t(p.rows[i]) >= rows) continue;
        const std::uint32_t id = rd16(table + std::uintptr_t(p.rows[i]) * layout::type_stride + projectile::type_body);
        if (projectile_body_enabled(p, id)) continue;
        p.bits[id >> 5] |= 1u << (id & 31u);
        ProjectileBody& b = p.bodies[p.body_count++];
        b.id = id;
        b.row = p.rows[i];
        b.family = p.row_family[i];
    }
    p.resolved = 1;
    return true;
}
// The object tap's projectile half: obj is the engine's live object of this update pass (plain loads are safe here).
static inline __attribute__((always_inline)) void projectile_tap(std::uintptr_t obj, ProjectilePool& p) noexcept {
    if (!p.armed || !obj || rd16(obj + layout::obj_class) != layout::class_bullet) return;
    const std::uintptr_t node = live_node(obj);
    if (!node) return;
    if (!p.resolved && !projectile_resolve(p)) return;
    const std::uint32_t id = rd32(node + layout::node_body);
    if (!projectile_body_enabled(p, id)) return;
    unsigned body = 0;
    while (body + 1u < p.body_count && p.bodies[body].id != id) ++body;
    ++p.counters.taps;
    const std::uintptr_t ext = rd32(obj + layout::obj_ext);
    const std::uint32_t handle = rd32(node + layout::node_handle);
    const std::uint32_t expiry = ext ? rd32(ext + projectile::ext_expiry) : 0u;
    const std::uint32_t created = ext ? rd32(ext + projectile::ext_created) : 0u;
    unsigned h = projectile_home(std::uint32_t(node)), free_slot = projectile::slot_count;
    for (unsigned k = 0; k < projectile::max_probes; ++k, h = (h + 1u) & (projectile::slot_count - 1u)) {
        const unsigned s = p.slots[h];
        if (!s) {
            free_slot = h;
            break;
        }
        ProjectileEntry& e = p.entries[s - 1u];
        if (e.node != std::uint32_t(node)) continue;
        if (e.obj != std::uint32_t(obj) || e.handle != handle) ++p.counters.recycled;
        e.obj = std::uint32_t(obj);
        e.handle = handle;
        e.expiry = expiry;
        e.created = created;
        e.body = body;
        return;
    }
    if (p.count >= projectile::pool_capacity) {
        ++p.counters.overflow;
        return;
    }
    if (free_slot >= projectile::slot_count) {
        ++p.counters.probe_full;
        return;
    }
    ProjectileEntry& e = p.entries[p.count];
    e.node = std::uint32_t(node);
    e.obj = std::uint32_t(obj);
    e.handle = handle;
    e.expiry = expiry;
    e.created = created;
    e.body = body;
    p.slots[free_slot] = std::uint16_t(p.count + 1u);
    ++p.count;
    ++p.counters.registered;
}
} // namespace x3m::weapon_effects::core
