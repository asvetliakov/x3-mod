#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include "chase_camera_math.h"

namespace x3m::chase_lead::core {
struct Identity {
    std::uint64_t generation = 0, serial = 0;
    std::uint32_t thread = 0, cockpit = 0, ship = 0, ship_id = 0, target = 0, target_id = 0, camera = 0, scene = 0,
                  sector = 0, overlay = 0, marker = 0;
};
inline bool same(const Identity &a, const Identity &b) {
    return a.generation && a.serial && a.generation == b.generation && a.serial == b.serial &&
           a.thread == b.thread && a.cockpit == b.cockpit && a.ship == b.ship && a.ship_id == b.ship_id &&
           a.target == b.target && a.target_id == b.target_id && a.camera == b.camera && a.scene == b.scene &&
           a.sector == b.sector && a.overlay == b.overlay && a.marker == b.marker;
}
struct Projection {
    std::int32_t position[3]{}, basis[12]{}, fov = 0, plane[2]{}, viewport[4]{}, screen[2]{};
};
inline bool same(const Projection &a, const Projection &b) {
    return std::memcmp(&a, &b, sizeof a) == 0;
}
struct Pixel {
    std::int32_t x = 0, y = 0;
    double depth = 0;
};
inline bool valid(const Projection &p) {
    if (p.fov < 0x106 || p.fov >= 0x8000 || p.plane[0] <= 0 || p.plane[1] <= 0 || p.screen[0] <= 0 ||
        p.screen[1] <= 0 || p.screen[0] > 32767 || p.screen[1] > 32767 || p.viewport[0] < 0 ||
        p.viewport[0] >= p.viewport[1] || p.viewport[1] > 65536 || p.viewport[2] < 0 ||
        p.viewport[2] >= p.viewport[3] || p.viewport[3] > 65536)
        return false;
    const auto b = chase::from_fixed(p.basis);
    const double d = chase::determinant(b);
    if (!(d >= 0.98 && d <= 1.02))
        return false;
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j) {
            const double e = chase::dot(chase::row(b, i), chase::row(b, j)) - (i == j ? 1.0 : 0.0);
            if (!(e >= -0.02 && e <= 0.02))
                return false;
        }
    return true;
}
inline bool project(const Projection &p, const std::int32_t point[3], Pixel &out) {
    if (!valid(p))
        return false;
    const auto b = chase::from_fixed(p.basis);
    const auto v = chase::mul(chase::Vec3{double(point[0]) - p.position[0], double(point[1]) - p.position[1],
                                          double(point[2]) - p.position[2]},
                              chase::transpose(b));
    if (!(v.z > 0))
        return false;
    // Native integer viewport extents, preserving its truncation before the
    // camera-space projection. Normalized bounds are y0,y1,x0,x1.
    const int w = (p.screen[0] * (p.viewport[3] - p.viewport[2]) + 32768) / 65536;
    const int h = (p.screen[1] * (p.viewport[1] - p.viewport[0]) + 32768) / 65536;
    if (w <= 0 || h <= 0)
        return false;
    const double t = std::tan(chase::pi * double(p.fov) / 65536.0);
    const double x = double(w / 2) * v.x / (v.z * t * (p.plane[0] / 65536.0));
    const double y = -double(h / 2) * v.y / (v.z * t * (p.plane[1] / 65536.0));
    if (!(x >= INT32_MIN && x <= INT32_MAX && y >= INT32_MIN && y <= INT32_MAX))
        return false;
    // Native pixel helpers use signed IDIV: truncate toward zero. This new
    // camera-space projection avoids their intermediate fixed-point losses.
    out = {std::int32_t(x), std::int32_t(y), v.z};
    return true;
}
inline bool project_direction(const Projection &p, const std::int32_t direction[3], Pixel &out) {
    if (!valid(p))
        return false;
    const auto b = chase::from_fixed(p.basis);
    const auto v = chase::mul(chase::Vec3{double(direction[0]), double(direction[1]), double(direction[2])},
                              chase::transpose(b));
    if (!(v.z > 0))
        return false;
    const int w = (p.screen[0] * (p.viewport[3] - p.viewport[2]) + 32768) / 65536;
    const int h = (p.screen[1] * (p.viewport[1] - p.viewport[0]) + 32768) / 65536;
    if (w <= 0 || h <= 0)
        return false;
    const double t = std::tan(chase::pi * double(p.fov) / 65536.0);
    const double x = double(w / 2) * v.x / (v.z * t * (p.plane[0] / 65536.0));
    const double y = -double(h / 2) * v.y / (v.z * t * (p.plane[1] / 65536.0));
    if (!(x >= INT32_MIN && x <= INT32_MAX && y >= INT32_MIN && y <= INT32_MAX))
        return false;
    out = {std::int32_t(x), std::int32_t(y), v.z};
    return true;
}
struct Pending {
    Identity owner{};
    Projection projection{};
    std::int32_t point[3]{};
    bool admitted = false, published = false;
    void clear() { *this = {}; }
    void admit(const Identity &i) {
        clear();
        owner = i;
        admitted = true;
    }
    bool matches(const Identity &i) const { return admitted && same(owner, i); }
    bool consume(const Identity &i, Pending &out) {
        const bool ok = published && matches(i);
        if (ok)
            out = *this;
        clear();
        return ok;
    }
};
} // namespace x3m::chase_lead::core
