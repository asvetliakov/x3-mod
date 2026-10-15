#pragma once
#include <cstdint>
#include "ship_avoidance_core.h"
#include "ship_gate_ctl_core.h"
// B.5 (docs/architecture/gate-traffic-controller.md "B.5 Pointer liveness"): the gate traffic controller's proof that a board member's
// engine object was live in this frame's update. Run165 crashed in the B.4 member row: launched ships stayed on the board as committed
// on order 8, their objects were freed and the address reused, and the frame-boundary row read [ship+0x4a] through the stale pointer.
//
// The invariant: the controller dereferences only pointers the engine passed to the module in the same frame's update. The live set is
// filled during the update, only from objects the engine hands over there: the avoidance snapshot walks (the engine's sector lists; the
// walk reads the id and drops deleted, attached and docked objects) and site B's steering calls of board members (the hook's object, its
// flags already tested, its id read in the call). At the frame boundary (after the update) the controller consults the set and reads no
// engine memory at all: prove() is a lookup (pointer, id; every entry was flags-ok when the engine handed it over; the set is emptied
// after each controller frame, so every entry is this frame's). An object freed after the update is not read until the engine hands it
// over again. A member neither walked nor steered this frame is unproven: unread, decided on its stored state until it leaves. It leaves
// the board once no proof (the set's, or site B's state read) is younger than kStateMaxAgeMs; a launched member (stage launch) when its
// order-8 leg ends (o8_ms + 900 ms) or when a complete snapshot of its sector misses it, whichever first. Two consecutive walked frames
// whose walks list neither the controller's sector nor a board's gate reset every board (the player changed sector); a frame without a
// walk never resets.
//
// Portable and pure; SSE2 scalar only, no allocation, no lock.
namespace x3m::ship_avoidance::ctl {
namespace lav = x3m::ship_avoidance::core;

constexpr unsigned kLiveBits = 13, kLiveSlots = 1u << kLiveBits; // open addressing, linear probe (64 KB of keys and ids)
constexpr unsigned kLiveMax = kLiveSlots / 2;                     // entries per frame at most (load <= 1/2); beyond: overflow
constexpr unsigned kLiveSectors = 4;                              // snapshots per frame (sites::kSnapshotSlots)
constexpr std::uint8_t kLiveNoSector = 0xff;                      // an entry from site B (no snapshot: its sector is not recorded)
constexpr unsigned kLiveGates = 32;                               // gate objects listed by this frame's walks (the sector check)
constexpr unsigned kResetMissFrames = 2;                          // walked frames without the controller's sector before a reset
constexpr std::uint32_t kLaunchLegMs = 900;                       // order 8's length (D.12; eta_launch, occupant)

// This frame's live pointers (handed over by the engine during the update) and the native id read then. Cleared after each controller
// frame in O(inserted).
struct LiveSet {
    std::uint32_t ptr[kLiveSlots] = {};
    std::uint32_t id[kLiveSlots] = {};
    std::uint8_t sec[kLiveSlots] = {}; // the index into sector[] of the snapshot that listed it (kLiveNoSector: site B only)
    std::uint16_t used[kLiveMax] = {};
    unsigned n = 0, n_sectors = 0;
    std::uint32_t sector[kLiveSectors] = {};
    std::uint8_t partial[kLiveSectors] = {}; // the snapshot overflowed, or its ships were not entered (its list is not every ship)
    std::uint8_t overflow = 0;               // more than kLiveMax entries or sectors: no proof this frame (fail closed)
    std::uint32_t gate[kLiveGates] = {};     // gate objects the walks listed (kind gate) and the index of their sector
    std::uint8_t gate_sec[kLiveGates] = {};
    unsigned n_gates = 0;
    __attribute__((always_inline)) static unsigned slot(std::uint32_t p) noexcept { return ((p >> 3) * 2654435761u) >> (32 - kLiveBits); }
    void clear() noexcept {
        for (unsigned i = 0; i < n; ++i) ptr[used[i]] = 0;
        n = n_sectors = n_gates = 0;
        overflow = 0;
    }
    // false: the table is full (the set stays a correct subset; overflow marks it incomplete). A pointer entered twice with two ids
    // (reused within the frame) keeps id 0: no member id is 0, so neither proves.
    bool add(std::uint32_t p, std::uint32_t i, std::uint8_t s) noexcept {
        if (!p) return true;
        unsigned k = slot(p);
        while (ptr[k]) {
            if (ptr[k] == p) {
                if (id[k] != i) id[k] = 0;
                else if (sec[k] == kLiveNoSector) sec[k] = s;
                return true;
            }
            k = (k + 1) & (kLiveSlots - 1);
        }
        if (n >= kLiveMax) {
            overflow = 1;
            return false;
        }
        ptr[k] = p;
        id[k] = i;
        sec[k] = s;
        used[n++] = std::uint16_t(k);
        return true;
    }
    __attribute__((always_inline)) bool find(std::uint32_t p, std::uint32_t* i, std::uint8_t* s) const noexcept {
        if (!p) return false;
        for (unsigned k = slot(p);; k = (k + 1) & (kLiveSlots - 1)) {
            if (!ptr[k]) return false;
            if (ptr[k] == p) {
                *i = id[k];
                *s = sec[k];
                return true;
            }
        }
    }
    // A snapshot walked in this frame's update: its sector, and its ships (kind ship: linked, not deleted, attached or docked at the
    // walk, the id the walk read) when `ships` (false: nobody on a board, only the sector is recorded). A sector already entered this
    // frame is skipped.
    void add_snapshot(const lav::Snapshot& snap, bool ships = true) noexcept {
        if (!snap.sector) return;
        for (unsigned q = 0; q < n_sectors; ++q)
            if (sector[q] == snap.sector) return;
        if (n_sectors >= kLiveSectors) {
            overflow = 1;
            return;
        }
        const std::uint8_t s = std::uint8_t(n_sectors);
        sector[n_sectors] = snap.sector;
        partial[n_sectors] = snap.overflow || !ships ? 1 : 0;
        ++n_sectors;
        for (unsigned q = 0; q < snap.count; ++q) {
            const lav::Obstacle& o = snap.items[q];
            if (o.kind == std::uint8_t(lav::Kind::gate) && n_gates < kLiveGates) {
                gate[n_gates] = o.ptr;
                gate_sec[n_gates++] = s;
            } else if (ships && o.kind == std::uint8_t(lav::Kind::ship))
                add(o.ptr, o.id, s); // a full table marks overflow (the set then proves nothing this frame)
        }
    }
    // the sector of the walk that listed this gate object (0: none this frame)
    __attribute__((always_inline)) std::uint32_t gate_sector(std::uint32_t g) const noexcept {
        for (unsigned q = 0; q < n_gates; ++q)
            if (gate[q] == g) return sector[gate_sec[q]];
        return 0;
    }
    __attribute__((always_inline)) bool built(std::uint32_t s) const noexcept {
        for (unsigned q = 0; q < n_sectors; ++q)
            if (sector[q] == s) return true;
        return false;
    }
    // every ship of the sector is in the set: walked this frame without overflow, its ships entered, the set not overflowed
    __attribute__((always_inline)) bool complete(std::uint32_t s) const noexcept {
        if (overflow || !s) return false;
        for (unsigned q = 0; q < n_sectors; ++q)
            if (sector[q] == s) return !partial[q];
        return false;
    }
};

// The liveness proof: a lookup, no engine read. The engine handed ptr over in this frame's update with this id (and flags-ok). An
// overflowed set proves nothing (fail closed).
__attribute__((always_inline)) inline bool prove(const LiveSet& s, std::uint32_t ptr, std::uint32_t id, std::uint8_t* sec) noexcept {
    std::uint32_t sid = 0;
    return ptr && id && !s.overflow && s.find(ptr, &sid, sec) && sid == id;
}
// ctl_escort (the DLL): the leader's board entry is the object the engine handed over (its id, read in the call, equals the entry's)
__attribute__((always_inline)) inline bool leader_entry_current(const Member& leader, std::uint32_t live_id) noexcept {
    return leader.id && leader.id == live_id;
}

struct LiveTrack {
    std::uint32_t sector = 0; // the controller's sector: the sector whose walk listed a board's gate (else of the members' proofs)
    unsigned miss = 0;        // consecutive walked frames whose walks listed neither that sector nor a board's gate
};
struct LiveLeave {
    std::uint8_t gate, member;
    Why why;
    std::uint8_t launched; // a launched member (stage launch)
};
struct LiveOut {
    bool reset = false;             // kResetMissFrames walked frames without the controller's sector or a board's gate: reset_boards
    bool overflow = false;          // the set overflowed: nothing proven this frame
    std::uint32_t from = 0, to = 0; // ... from that sector to this one
    unsigned proven = 0, unproven = 0, unknown = 0; // pair-role members this frame; unknown: no complete snapshot of their sector
    unsigned n_leave = 0;
    LiveLeave leave[kGates * kMembers];
};
// Every board emptied (the members of another sector are dropped): the caller logs the members first. Returns the entries dropped.
inline unsigned reset_boards(Board& b) noexcept {
    unsigned n = 0;
    for (Gate& g : b.g) {
        for (unsigned i = 0; i < g.n; ++i) n += g.m[i].ship && g.m[i].role != Role::none ? 1u : 0u;
        g = Gate{};
    }
    return n;
}
// The sector check alone (no board entry: the DLL skips the proofs): the controller's sector follows this frame's walks.
inline void live_track(LiveTrack& t, const LiveSet& s) noexcept {
    if (s.n_sectors && !s.built(t.sector)) t.sector = s.sector[0];
    t.miss = 0;
}
// The sector check (Fable review 1: the walk is on demand per sector, so a frame may walk only another active sector). A frame without
// a walk changes nothing. A walk listing a board's gate makes that sector the controller's; a frame whose walks list neither the
// controller's sector nor any board's gate counts a miss, and kResetMissFrames consecutive misses reset every board (the player changed
// sector). Returns true for a reset.
inline bool sector_check(const Board& b, LiveTrack& t, const LiveSet& s, std::uint32_t* from, std::uint32_t* to) noexcept {
    if (!s.n_sectors) return false;
    std::uint32_t gs = 0;
    for (const Gate& g : b.g)
        if (g.gate && !gs) gs = s.gate_sector(g.gate);
    if (gs || s.built(t.sector)) {
        if (gs) t.sector = gs;
        t.miss = 0;
        return false;
    }
    bool any = false;
    for (const Gate& g : b.g)
        for (unsigned i = 0; i < g.n && !any; ++i) any = g.m[i].ship && g.m[i].role != Role::none;
    if (!any || !t.sector) {
        t.sector = s.sector[0];
        t.miss = 0;
        return false;
    }
    if (++t.miss < kResetMissFrames) return false;
    *from = t.sector;
    *to = s.sector[0];
    t.sector = s.sector[0];
    t.miss = 0;
    return true;
}
// Step 1 (the frame boundary, before the sequencer; no engine read): every proven flag cleared, the sector check, then each pair-role
// member's proof (Member.proven, live_ms, live_sector). out->reset: the caller resets the boards (reset_boards) after its rows.
inline void live_prove(Board& b, LiveTrack& t, const LiveSet& s, std::uint32_t now, LiveOut* out) noexcept {
    *out = LiveOut{};
    for (Gate& g : b.g) // no proof carries over from the previous frame (a reset frame proves nothing)
        for (unsigned i = 0; i < g.n; ++i) g.m[i].proven = 0;
    if (sector_check(b, t, s, &out->from, &out->to)) {
        out->reset = true;
        return;
    }
    out->overflow = s.overflow != 0;
    for (unsigned gi = 0; gi < kGates; ++gi) {
        Gate& g = b.g[gi];
        if (!g.gate) continue;
        for (unsigned i = 0; i < g.n; ++i) {
            Member& m = g.m[i];
            if (!m.ship || !pair_role(m.role)) continue;
            std::uint8_t q = kLiveNoSector;
            if (prove(s, m.ship, m.id, &q)) {
                m.proven = 1;
                m.live_ms = now ? now : 1;
                if (q != kLiveNoSector) m.live_sector = s.sector[q];
                ++out->proven;
            } else
                ++out->unproven;
        }
    }
}
// Step 2 (after the sequencer, before the decisions): the members to leave. A launched member when its order-8 leg ended or when a
// complete snapshot of its sector misses it; any other unproven member when a complete snapshot of its sector misses it and its last
// proof (the set's, or site B's state read: the engine handed the object to that call) is older than kStateMaxAgeMs (SETA-aware).
inline void live_leaves(const Board& b, const LiveTrack& t, const LiveSet& s, std::uint32_t now, LiveOut* out) noexcept {
    out->n_leave = 0;
    for (unsigned gi = 0; gi < kGates; ++gi) {
        const Gate& g = b.g[gi];
        if (!g.gate) continue;
        for (unsigned i = 0; i < g.n; ++i) {
            const Member& m = g.m[i];
            if (!m.ship || !pair_role(m.role)) continue;
            const bool launched = m.role == Role::committed && m.sb == Sb::launch;
            const bool leg_over = launched && m.o8_ms && std::int32_t(now - (m.o8_ms + kLaunchLegMs)) >= 0;
            bool go = leg_over;
            if (!go && !m.proven) {
                const std::uint32_t sec = m.live_sector ? m.live_sector : t.sector;
                if (!s.complete(sec)) {
                    ++out->unknown;
                    continue;
                }
                std::uint32_t last = m.enter_ms;
                if (m.live_ms && std::int32_t(m.live_ms - last) > 0) last = m.live_ms;
                if (m.st.ms && std::int32_t(m.st.ms - last) > 0) last = m.st.ms;
                go = launched || now - last > age_bound(g, kStateMaxAgeMs);
            }
            if (go) out->leave[out->n_leave++] = LiveLeave{std::uint8_t(gi), std::uint8_t(i), Why::gone, std::uint8_t(launched ? 1 : 0)};
        }
    }
}
// The member row's class: the record class code site B read in the update (Member.code), only for a member proven this frame.
__attribute__((always_inline)) inline std::uint32_t member_code(const Member& m) noexcept { return m.proven ? m.code : 0xffffffffu; }
// Convoy-gate-transit.md section 12.3 (round B): the statics near Lambda from the module's snapshot, as lane_profile_build takes them: a
// station's parts from the part cache (the module's part walk), a station without parts as its fallback body (lav::fallback_radius, the
// sphere kept for the enclosure test), a rock as its sphere; gates, ships and debris are not statics here. Near: the bounding sphere
// within kLaneProfMaxM + 2 km (more than any hull's w + the margin) of Lambda's segment s in [-(kLaneFarM + the longest ramp), 0]; a part
// within that of the segment too. More than cap: *overflow (no profile: fail closed to the sphere detour). Reads the snapshot only.
inline unsigned lane_bodies(const Gate& g, const lav::Snapshot& snap, const lav::PartSet* parts, unsigned n_parts, LaneBody* out, unsigned cap,
                            bool* overflow) noexcept {
    unsigned n = 0;
    *overflow = false;
    const float s_lo = -(kLaneFarM + kLaneProfMaxM / kLaneProfTan), reach = kLaneProfMaxM + 2000.0f;
    auto near = [&](const float c[3], float r) {
        const float d[3] = {c[0] - g.G[0], c[1] - g.G[1], c[2] - g.G[2]};
        const float s = sc::dot3(d, g.F), eR = sc::dot3(d, g.R) - g.lane_c[0], eU = sc::dot3(d, g.U) - g.lane_c[1];
        return s + r >= s_lo && s - r <= 0.0f && eR * eR + eU * eU <= (r + reach) * (r + reach);
    };
    for (unsigned k = 0; k < snap.count; ++k) {
        const lav::Obstacle& o = snap.items[k];
        const bool station = o.kind == std::uint8_t(lav::Kind::station);
        if (!station && o.kind != std::uint8_t(lav::Kind::asteroid)) continue;
        if (!near(o.x, o.r)) continue;
        if (station && (o.flags & 1) && o.parts < n_parts && parts[o.parts].station == o.ptr && parts[o.parts].count) {
            const lav::PartSet& ps = parts[o.parts];
            for (unsigned q = 0; q < ps.count; ++q) {
                if (!near(ps.c[q], ps.r[q])) continue;
                if (n >= cap) {
                    *overflow = true;
                    return n;
                }
                out[n++] = LaneBody{{ps.c[q][0], ps.c[q][1], ps.c[q][2]}, ps.r[q], 0.0f, o.ptr};
            }
            continue;
        }
        if (n >= cap) {
            *overflow = true;
            return n;
        }
        out[n++] = station ? LaneBody{{o.x[0], o.x[1], o.x[2]}, lav::fallback_radius(o.r), o.r, o.ptr} : LaneBody{{o.x[0], o.x[1], o.x[2]}, o.r, 0.0f, o.ptr};
    }
    return n;
}
} // namespace x3m::ship_avoidance::ctl
