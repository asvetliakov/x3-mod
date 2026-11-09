#pragma once
#include "weapon_effects_bolt.h"
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
// expiry ext+0, birth ext+8 and the game ms of this tap. A full pool counts the overflow: that bullet is drawn by nobody
// while the batch skip is latched, so the overflow is the kill criterion for raising the capacity. The stage (render
// thread = the game thread in X3) validates each entry through the bounded engine reader before every read and retires
// an expired, recycled or unreadable one, and one the tap has not seen for stale_ms game ms (at least three game
// steps: SETA-aware); Present empties the pool on a load or a sector change. A table generated from another TBullets
// (its row count differs from the engine's) resolves no body: the stage stays off (stale).
//
// The muzzle (docs/architecture/muzzle-flash-look.md (superseded) section 4 and "Implementation (2026-11-09)"; FL
// 0x04f639 -> the spawner 0x0150e0, reached only for TBullets rows whose col 21 is not 0): projectile_muzzle reads the
// shot's body (word +0 of the TBullets record at ESI) and tests it against the same enabled set (bits) the batch stub
// tests. An enabled body's shot relights its (ship, gun slot) MuzzleRecord (t0, the shooter's node and handle) and is
// skipped (the spawner not called, the game's flash not made) while Present latched skip_on, the record then ours; with
// the latch off it is forwarded (muzzle_unskipped). Any other body forwards untouched. The object tap matches a newly
// registered entry to the ours record of its shooter (ext+4) fired at its birth (t0 == ext+8: one clock, cfg +0x708)
// and hands the stage the shooter's node and handle; an entry registered more than flash_stale_steps game steps after
// its birth gets no flash. The flash's life is wall clock, stamped by the stage on its first frame (flash_born_us).
namespace x3m::weapon_effects::core {
namespace projectile {
constexpr unsigned pool_capacity = 512, slot_count = 1024, max_probes = 32;
constexpr unsigned max_bodies = 64, body_bits = 65536; // a body id is a zero-extended word (movzx, 0x049b70)
constexpr unsigned type_body = 0x0;                    // TBullets +0x0: the body id (word) = node +0x144 (note section 4)
constexpr unsigned node_velocity = 0x100;              // engine units per second (includes the shooter's)
constexpr unsigned ext_expiry = 0, ext_created = 8;    // game ms
// The engine's game-time step of the frame (fl/seta.md: AP cfg +0x714, FL +0x704; ms, SETA included).
constexpr unsigned cfg_step_ms = edition_fl ? 0x704 : 0x714;
// An entry the object tap has not seen for this long (game ms; at least three game steps) retires: absent from the
// update pass means not drawn.
constexpr std::uint32_t stale_ms = 250;
// The comet's ring (projectile-look.md section 2, FL 60 A): the heads drawn before the previous frame's, at most this
// many, so a bolt is at most 1 + trail_ring quads.
constexpr unsigned trail_ring = 4;
// The muzzle flash: the bullet extension's shooter, the registration's staleness in game steps, the entry's flash state.
constexpr unsigned ext_owner = 4;
constexpr std::uint32_t flash_stale_steps = 2;
enum FlashState : std::uint8_t { flash_none = 0, flash_pending = 1, flash_live = 2, flash_done = 3 };
static_assert((slot_count & (slot_count - 1u)) == 0 && slot_count >= 2 * pool_capacity, "half full at most");
}
struct ProjectileEntry {
    std::uint32_t node, obj, handle; // the key, its object and node +0x28 at the last tap
    std::uint32_t expiry, created;   // ext+0 / ext+8 at the last tap (game ms)
    std::uint32_t body;              // index into ProjectilePool::bodies (its per-body look, resolved once)
    std::uint32_t seen_ms;           // the game ms (cfg +0x708) of the last tap that saw it
    // The stage's bridge state (weapon_projectile_stage.h build; integers only, the tap only zeroes stage_frame on a
    // registration and a recycled node): the next frame's streak starts where this frame's ended.
    std::int32_t read_pos[3];  // node +0x30 at the stage frame stage_frame (engine units)
    std::uint32_t head_px[2];  // the head drawn at that frame on screen (unjittered raster px, float bits): the next
    std::uint32_t head_z;      // frame's tail, and its view depth (float bits; 0: none, the next frame is a first frame)
    std::uint32_t changed_ms;  // the game ms of the last stage frame whose read position changed
    std::uint32_t static_us;   // game us the head was extrapolated since then (render dt x SETA rate, bounded)
    std::uint32_t stage_frame; // the device frame of that stage frame (0: none, the next frame is a first frame)
    // The comet's trail (valid only with a bridge from stage_frame): the game time of head_px (game us, wrapping: the
    // last change's ms x 1000 + static_us), and the ring of the heads before it, newest first (raster px, float bits),
    // with their game times; ring_count of them are valid.
    std::uint32_t head_us;
    std::uint32_t ring_px[projectile::trail_ring][2];
    std::uint32_t ring_us[projectile::trail_ring];
    std::uint32_t ring_count;
    std::uint32_t spot_state; // the last frame's: 1 a spot, 2 a streak or comet (the spot's hysteresis)
    // The muzzle flash (muzzle-flash-look.md): the tap's match at registration, then the stage's state.
    std::uint32_t shooter;      // ext+4 at registration (the firing object; the stage reads it through the bounded reader)
    std::uint32_t flash_born_us; // the birth's wall clock (QPC us, wrapping), set by the stage on the flash's first frame
    std::uint8_t flash;         // projectile::FlashState
    std::uint8_t flash_frames;  // stage frames the flash was drawn on (saturating at 255; diagnostic and first-frame test)
    std::uint8_t flash_pad[2];
    std::uint32_t flash_node, flash_handle; // the shooter's node and node+0x28 at the shot (its MuzzleRecord)
    std::uint32_t flash_off[3];  // the muzzle in the shooter's frame (engine units along its rows, float bits)
    std::uint32_t flash_axis[3]; // the shot's axis in the shooter's frame (unit, float bits)
};
struct ProjectileBody {
    std::uint32_t id;         // the engine body id (node +0x144)
    std::uint16_t row;        // the enabled row that named it (its colour and calibre)
    std::uint8_t family, pad; // weapon_projectile_family.h families[]
};
struct ProjectileCounters {
    std::uint32_t taps = 0, registered = 0, recycled = 0, overflow = 0, probe_full = 0;
    // the muzzle site: calls, enabled-body shots skipped and forwarded (latch off or no shooter node), records evicted;
    // the tap's flash match at registration: matched to an ours record, unmatched, registered too late
    std::uint32_t muzzle_calls = 0, muzzle_skipped = 0, muzzle_unskipped = 0, muzzle_evicted = 0;
    std::uint32_t flash_matched = 0, flash_unmatched = 0, flash_stale = 0;
};
// The batch stub reads skip_on, bumps skipped and tests bits at fixed offsets (the static_assert below).
struct ProjectilePool {
    volatile std::uint32_t skip_on = 0; // Present's latch: the stage drew the previous frame, the bodies are resolved
    std::uint32_t skipped = 0;          // batch submits the stub skipped (monotonic)
    std::uint32_t bits[projectile::body_bits / 32]{}; // the enabled bodies
    std::uint8_t armed = 0;             // the object tap registers (FL stage modes, set at install)
    std::uint8_t resolved = 0;          // the rows became bodies (the engine's TBullets was readable)
    std::uint8_t stale = 0;             // resolved, but the table's row count is not the engine's: no body enabled
    std::uint32_t table_rows = 0;       // TBullets rows the table was generated from (core::Table::rows; 0 no table)
    std::uint32_t engine_rows = 0;      // the engine's count at the resolve
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
    std::int32_t stage_eye[3]{};   // the eye (engine units) of the stage frame stage_eye_frame (a first frame's camera travel)
    std::uint32_t stage_eye_frame = 0;
    std::uint32_t stage_wanted = 0; // the quads the last stage frame's bolts wanted untrimmed (the adaptive ring)
    // The muzzle records of the enabled bodies' shots (projectile_muzzle; one per (ship, gun slot), the oldest replaced).
    std::uint32_t muzzle_sequence = 0;
    MuzzleRecord muzzles[muzzle::record_capacity]{};
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
    p.engine_rows = std::uint32_t(rows);
    if (p.table_rows && p.table_rows != std::uint32_t(rows)) { // core's stale_table rule: never a wrong body
        p.stale = 1;
        p.resolved = 1;
        return true;
    }
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
// A registration's muzzle-flash match (a new entry, or a recycled node: another bullet): the ours record of the entry's
// shooter fired at its birth makes it pending; none, or registered more than flash_stale_steps game steps after the
// birth (cfg +0x704, SETA-scaled), no flash.
static inline __attribute__((always_inline)) void projectile_flash_match(ProjectileEntry& e, ProjectilePool& p,
                                                                         std::uintptr_t ext, std::uintptr_t cfg,
                                                                         std::uint32_t now, std::uint32_t created) noexcept {
    e.shooter = ext ? rd32(ext + projectile::ext_owner) : 0u;
    e.flash_born_us = 0;
    e.flash_frames = 0;
    e.flash = projectile::flash_none;
    if (!e.shooter || !created) {
        ++p.counters.flash_unmatched;
        return;
    }
    const std::uint32_t step = cfg ? rd32(cfg + projectile::cfg_step_ms) : 0u;
    if (std::int32_t(now - created) > std::int32_t(projectile::flash_stale_steps * (step < 1000u ? step : 1000u))) {
        ++p.counters.flash_stale;
        return;
    }
    for (unsigned i = 0; i < muzzle::record_capacity; ++i) {
        const MuzzleRecord& m = p.muzzles[i];
        if (!m.sequence || !m.ours || m.t0 != created || m.ship != e.shooter) continue;
        e.flash_node = m.node; // the shooter's node and handle at the shot: the stage's first frame compares them
        e.flash_handle = m.handle;
        e.flash = projectile::flash_pending;
        ++p.counters.flash_matched;
        return;
    }
    ++p.counters.flash_unmatched;
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
    const std::uintptr_t cfg = p.globals.cfg_slot ? rd32(p.globals.cfg_slot) : 0u;
    const std::uint32_t now = cfg ? rd32(cfg + layout::cfg_game_ms) : 0u;
    unsigned h = projectile_home(std::uint32_t(node)), free_slot = projectile::slot_count;
    for (unsigned k = 0; k < projectile::max_probes; ++k, h = (h + 1u) & (projectile::slot_count - 1u)) {
        const unsigned s = p.slots[h];
        if (!s) {
            free_slot = h;
            break;
        }
        ProjectileEntry& e = p.entries[s - 1u];
        if (e.node != std::uint32_t(node)) continue;
        if (e.obj != std::uint32_t(obj) || e.handle != handle) {
            ++p.counters.recycled;
            e.stage_frame = 0; // another bullet: no bridge from the old one's streak
            projectile_flash_match(e, p, ext, cfg, now, created);
        }
        e.obj = std::uint32_t(obj);
        e.handle = handle;
        e.expiry = expiry;
        e.created = created;
        e.body = body;
        e.seen_ms = now;
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
    e.seen_ms = now;
    e.stage_frame = 0;
    projectile_flash_match(e, p, ext, cfg, now, created);
    p.slots[free_slot] = std::uint16_t(p.count + 1u);
    ++p.count;
    ++p.counters.registered;
}
// The FL muzzle site's body (0x04f639 -> 0x0150e0): the ship [esp+8], EDX the gun slot, ESI the TBullets record offset
// (index x 0xdb8; the window's own col-21 read used it). Returns 1 to skip the spawner (the game's muzzle flash) for a
// shot of an enabled body while skip_on is latched, else 0 (forward). Plain loads of what the engine itself just read
// (the TBullets record, the ship and its node); no call, no allocation.
static inline __attribute__((always_inline)) int projectile_muzzle(std::uintptr_t ship, std::uint32_t slot,
                                                                   std::uint32_t esi, ProjectilePool& p) noexcept {
    ++p.counters.muzzle_calls;
    if (!p.armed || !p.resolved || p.stale || !p.body_count || !p.globals.bullet_table_slot) return 0;
    if (esi % layout::type_stride || (p.engine_rows && esi / layout::type_stride >= p.engine_rows)) return 0;
    const std::uintptr_t table = rd32(p.globals.bullet_table_slot);
    if (!table || !projectile_body_enabled(p, rd16(table + esi + projectile::type_body))) return 0;
    const std::uintptr_t node = live_node(ship);
    if (!node) { // no record without the shooter's node: the game keeps its flash
        ++p.counters.muzzle_unskipped;
        return 0;
    }
    const std::uint32_t handle = rd32(node + layout::node_handle);
    const std::uintptr_t cfg = p.globals.cfg_slot ? rd32(p.globals.cfg_slot) : 0u;
    const std::uint32_t now = cfg ? rd32(cfg + layout::cfg_game_ms) : 0u;
    MuzzleRecord* r = nullptr;
    MuzzleRecord* oldest = nullptr; // the first free record, else the one shot longest ago
    for (unsigned i = 0; i < muzzle::record_capacity; ++i) {
        MuzzleRecord& m = p.muzzles[i];
        if (!m.sequence) {
            if (!oldest || oldest->sequence) oldest = &m;
            continue;
        }
        if (m.ship == std::uint32_t(ship) && m.slot == std::uint16_t(slot)) {
            r = &m;
            break;
        }
        if (!oldest || (oldest->sequence && now - m.t0 > now - oldest->t0)) oldest = &m;
    }
    if (r && r->handle == handle && r->node == std::uint32_t(node)) {
        ++r->shots;
    } else { // a new (ship, slot), or another ship at the same object: the record starts over
        if (!r) {
            r = oldest;
            if (r->sequence) ++p.counters.muzzle_evicted;
            r->ship = std::uint32_t(ship);
            r->slot = std::uint16_t(slot);
        }
        r->shots = 1;
    }
    r->node = std::uint32_t(node);
    r->handle = handle;
    r->t0 = now;
    r->row = std::uint16_t(esi / layout::type_stride);
    r->sequence = ++p.muzzle_sequence ? p.muzzle_sequence : ++p.muzzle_sequence;
    const int skip = p.skip_on ? 1 : 0;
    r->ours = std::uint8_t(skip);
    if (skip)
        ++p.counters.muzzle_skipped;
    else
        ++p.counters.muzzle_unskipped;
    return skip;
}
} // namespace x3m::weapon_effects::core
