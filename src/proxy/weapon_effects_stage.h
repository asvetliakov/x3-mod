#pragma once
#include "../renderer/weapon_effects_pass.h"
#include "weapon_effects_core.h"
#include "sse_scalar.h"

// The weapon effects' scene-end step (docs/architecture/weapon-effects.md 3.1, 5.1 item 3): the ring's stage-owned
// shield hits become shells (one per hit owner, its newest ring_count hits) and, on an owner larger than
// shell_radius_max_u, ripple decals at the hit point. The owner's rows are read here, at scene end, through the
// caller's reader (engine_memory::read in production) and validated: obj+0x70 == node, node+0x28 == handle and the
// dying bit obj+0x40 & 0x8000000 clear, else every record of that owner leaves the ring (a dead or replaced owner
// draws nothing). Ages are game time (cfg+0x718): SETA shortens the ripple exactly as the engine's own effects.
// World = engine integers x world_scale (0.01); world -> view in double (camera_scene_'s rows), so a far camera keeps
// the shell on its owner. No allocation; at most max_shells + max_decals outputs, O(ring_capacity x owners) with
// owners <= max_shells + max_decals.
namespace x3m::weapon_effects::stage {
namespace core = x3m::weapon_effects::core;
using Reader = bool (*)(std::uintptr_t address, void* out, std::size_t size);
struct Camera {
    float r[9]{}, t[3]{}; // view_j = sum_i world_i r[i*3+j] + t_j (renderer::CameraState)
    float m00 = 0, m11 = 0, m20 = 0, m21 = 0, m22 = 0, m32 = 0;
};
struct Owner {
    std::int32_t pos[3]{}, rows[12]{}, radius = 0;
};
enum class OwnerStatus : std::uint8_t { ok, unreadable, mismatch, dying };
inline OwnerStatus read_owner(Reader rd, const core::Event& e, Owner* out) noexcept {
    std::uint32_t node = 0, handle = 0, flags = 0;
    if (!rd(e.owner + core::layout::obj_node, &node, 4) || !rd(e.owner + core::layout::obj_flags, &flags, 4))
        return OwnerStatus::unreadable;
    if (node != e.node) return OwnerStatus::mismatch;
    if (!rd(node + core::layout::node_handle, &handle, 4)) return OwnerStatus::unreadable;
    if (handle != e.handle) return OwnerStatus::mismatch;
    if (flags & core::layout::obj_dying) return OwnerStatus::dying;
    if (!rd(node + core::layout::node_pos, out->pos, 12) || !rd(node + core::layout::node_rot, out->rows, 48) ||
        !rd(e.owner + core::layout::obj_radius, &out->radius, 4))
        return OwnerStatus::unreadable;
    return OwnerStatus::ok;
}
inline void to_view(const Camera& c, const double w[3], float out[3]) noexcept {
    for (unsigned j = 0; j < 3; ++j)
        out[j] = float(w[0] * double(c.r[j]) + w[1] * double(c.r[3 + j]) + w[2] * double(c.r[6 + j]) + double(c.t[j]));
}
inline void rotate_view(const Camera& c, const double w[3], float out[3]) noexcept {
    for (unsigned j = 0; j < 3; ++j)
        out[j] = float(w[0] * double(c.r[j]) + w[1] * double(c.r[3 + j]) + w[2] * double(c.r[6 + j]));
}
// The owner's shell in view space: centre = its node position, axes = its local axes (16.16 rows) x radius.
inline bool shell_geometry(const Owner& o, const Camera& c, float radius_scale, float centre[3], float axes[9]) noexcept {
    const double s = double(core::world_scale);
    const double w[3] = {double(o.pos[0]) * s, double(o.pos[1]) * s, double(o.pos[2]) * s};
    to_view(c, w, centre);
    const double R = double(o.radius > 0 ? o.radius : 0) * s * double(radius_scale) / 65536.0;
    if (!(R > 0.0)) return false;
    for (unsigned k = 0; k < 3; ++k) {
        const double a[3] = {double(o.rows[4 * k]) * R, double(o.rows[4 * k + 1]) * R, double(o.rows[4 * k + 2]) * R};
        rotate_view(c, a, axes + 3 * k);
    }
    return true;
}
// The hit point in view space: node position + sum_j pos_j row_j (the engine's local -> world, 16.16 rows).
inline void hit_point(const Owner& o, const std::int32_t pos[3], const Camera& c, float out[3]) noexcept {
    const double s = double(core::world_scale);
    double w[3];
    for (unsigned i = 0; i < 3; ++i) {
        double v = double(o.pos[i]);
        for (unsigned j = 0; j < 3; ++j) v += double(o.rows[4 * j + i]) * double(pos[j]) / 65536.0;
        w[i] = v * s;
    }
    to_view(c, w, out);
}
inline void hit_direction(const std::int32_t pos[3], float out[3]) noexcept {
    const float v[3] = {float(pos[0]), float(pos[1]), float(pos[2])};
    const float n = scalar::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!(n > 0.f)) {
        out[0] = out[1] = 0.f;
        out[2] = 1.f;
        return;
    }
    for (unsigned k = 0; k < 3; ++k) out[k] = v[k] / n;
}
inline float mean_axis(const float axes[9]) noexcept {
    float m = 0.f;
    for (unsigned k = 0; k < 3; ++k)
        m += scalar::sqrt(axes[3 * k] * axes[3 * k] + axes[3 * k + 1] * axes[3 * k + 1] + axes[3 * k + 2] * axes[3 * k + 2]);
    return m * (1.f / 3.f);
}
struct Counts {
    unsigned shells = 0, decals = 0, retired = 0, dropped = 0, hits = 0;
};
struct Tuning {
    float tint_mix = core::tint_mix, calibre_gain = core::calibre_gain, ring_gain = core::ring_gain;
    float radius_scale = core::shell_radius_scale, radius_max_u = core::shell_radius_max_u;
    float decal_radius_u = core::decal_radius_u, soft = core::soft;
};
// Builds the frame's shells and decals from the ring (see the header). now = cfg+0x718.
inline Counts build(core::Ring& ring, std::uint32_t now, Reader rd, const Camera& cam, const Tuning& t,
                    renderer::WeaponShell* shells, unsigned shell_cap, renderer::WeaponDecal* decals, unsigned decal_cap) noexcept {
    Counts n{};
    struct Seen {
        std::uint32_t owner;
        OwnerStatus status;
        int shell;
        Owner data;
        std::uint32_t newest_t0;
    };
    constexpr unsigned seen_cap = core::max_shells + core::max_decals;
    Seen seen[seen_cap];
    unsigned seen_count = 0;
    for (unsigned i = 0; i < core::ring_capacity; ++i) {
        core::Event& e = ring.slots[i];
        if (!e.sequence || e.role != core::role_shield_hit || (e.flags & core::flag_forwarded)) continue;
        const std::uint32_t age = core::age_ms(now, e.t0_game_ms);
        if (age > e.lifetime_ms) continue; // Present releases it
        ++n.hits;
        Seen* s = nullptr;
        for (unsigned k = 0; k < seen_count; ++k)
            if (seen[k].owner == e.owner) s = &seen[k];
        if (!s) {
            if (seen_count >= seen_cap) {
                ++n.dropped;
                continue;
            }
            s = &seen[seen_count++];
            s->owner = e.owner;
            s->shell = -1;
            s->newest_t0 = 0;
            s->status = read_owner(rd, e, &s->data);
        }
        if (s->status != OwnerStatus::ok) {
            e.flags |= core::flag_owner_retired;
            n.retired += core::ring_retire_owner(ring, e.owner);
            continue;
        }
        const float calibre = core::calibre_of(e);
        const float amp = core::ring_amplitude(calibre, t.ring_gain, t.calibre_gain);
        const float age_s = float(age) * 1e-3f;
        if (float(s->data.radius) > t.radius_max_u) {
            if (n.decals >= decal_cap) {
                ++n.dropped;
                continue;
            }
            renderer::WeaponDecal& d = decals[n.decals++];
            hit_point(s->data, e.pos, cam, d.centre);
            d.radius = t.decal_radius_u * core::world_scale * (0.5f + calibre);
            d.age_s = age_s;
            d.amp = amp;
            core::event_tint(e, t.tint_mix, d.tint);
            continue;
        }
        if (s->shell < 0) {
            if (n.shells >= shell_cap) {
                ++n.dropped;
                continue;
            }
            renderer::WeaponShell& sh = shells[n.shells];
            if (!shell_geometry(s->data, cam, t.radius_scale, sh.centre, sh.axes)) {
                ++n.dropped;
                continue;
            }
            sh.soft = t.soft * mean_axis(sh.axes);
            sh.hits = 0;
            s->shell = int(n.shells++);
        }
        renderer::WeaponShell& sh = shells[s->shell];
        // Newest first, at most four: insertion by age.
        unsigned at = sh.hits;
        while (at > 0 && sh.age_s[at - 1] > age_s) --at;
        if (at >= 4) continue;
        const unsigned last = sh.hits < 4 ? sh.hits : 3;
        for (unsigned k = last; k > at; --k) {
            std::memcpy(sh.dir[k], sh.dir[k - 1], 12);
            sh.age_s[k] = sh.age_s[k - 1];
            sh.amp[k] = sh.amp[k - 1];
        }
        hit_direction(e.pos, sh.dir[at]);
        sh.age_s[at] = age_s;
        sh.amp[at] = amp;
        if (sh.hits < 4) ++sh.hits;
        if (at == 0 || e.t0_game_ms >= s->newest_t0) { // the newest hit's colour tints the shell
            s->newest_t0 = e.t0_game_ms;
            core::event_tint(e, t.tint_mix, sh.tint);
        }
    }
    return n;
}
} // namespace x3m::weapon_effects::stage
