#pragma once
// Projected screen box of a draw's object-space AABB, for the object bounds log
// (docs/architecture/engine-frame-time.md, "Object bounds log"). Header-only and
// free of D3D types: the caller passes the draw's clip rows (the rows the
// same-draw motion output latches: clip.x = rows[0..3] . (x, y, z, 1), clip.y =
// rows[4..7], clip.z = rows[8..11], clip.w = rows[12..15], the same convention
// shadow_cascade_bounds_mask reads) and the box the shadow route already
// computed, so nothing here transforms geometry a second time.
#include <cmath>

namespace x3m::renderer {

// The screen box (pixels, y down, D3D viewport mapping) of the eight corners,
// clipped to the viewport, with the box's device depth range and the number of
// corners inside the frustum.
struct ObjectScreenBox {
    float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f; // viewport-clipped pixel box; x1/y1 are the far edges
    float zmin = 0.f, zmax = 0.f;                 // device depth (clip.z / clip.w) over the projectable corners
    unsigned inside = 0;                          // corners inside all six frustum planes (D3D: 0 <= z <= w)
    bool offscreen = false;                       // the screen box and the viewport do not overlap at all
    bool crosses_near = false;                    // a corner sits at or behind the eye plane: the screen box is unbounded
};

// A corner with clip.w at or below this cannot be projected (at or behind the
// eye plane); the box containing it has no bounded screen box.
constexpr float object_bounds_w_epsilon = 1e-6f;

// True when the box could be measured (finite corners, a viewport). `out` is
// always overwritten. `crosses_near`: the box straddles the eye plane, so the
// conservative screen box is the whole viewport and zmin is pinned to 0 (the box
// reaches the camera). Every corner at or behind the eye plane: offscreen, since
// nothing of the box can land on screen.
inline bool object_screen_box(const float rows[16], const float lo[3], const float hi[3],
                              unsigned width, unsigned height, ObjectScreenBox& out) noexcept {
    out = ObjectScreenBox{};
    if (!rows || !lo || !hi || !width || !height) return false;
    const float w_px = float(width), h_px = float(height);
    float x_lo = 3.4028235e38f, x_hi = -3.4028235e38f, y_lo = 3.4028235e38f, y_hi = -3.4028235e38f;
    float z_lo = 3.4028235e38f, z_hi = -3.4028235e38f;
    unsigned inside = 0, projected = 0;
    bool near_cross = false;
    for (unsigned corner = 0; corner < 8; ++corner) {
        const float x = (corner & 1) ? hi[0] : lo[0], y = (corner & 2) ? hi[1] : lo[1], z = (corner & 4) ? hi[2] : lo[2];
        const float cx = rows[0] * x + rows[1] * y + rows[2] * z + rows[3];
        const float cy = rows[4] * x + rows[5] * y + rows[6] * z + rows[7];
        const float cz = rows[8] * x + rows[9] * y + rows[10] * z + rows[11];
        const float cw = rows[12] * x + rows[13] * y + rows[14] * z + rows[15];
        if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cz) || !std::isfinite(cw)) return false;
        if (cx >= -cw && cx <= cw && cy >= -cw && cy <= cw && cz >= 0.f && cz <= cw) ++inside;
        if (cw <= object_bounds_w_epsilon) { near_cross = true; continue; }
        const float sx = (cx / cw * .5f + .5f) * w_px, sy = (.5f - cy / cw * .5f) * h_px, sz = cz / cw;
        if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sz)) return false;
        ++projected;
        if (sx < x_lo) x_lo = sx;
        if (sx > x_hi) x_hi = sx;
        if (sy < y_lo) y_lo = sy;
        if (sy > y_hi) y_hi = sy;
        if (sz < z_lo) z_lo = sz;
        if (sz > z_hi) z_hi = sz;
    }
    out.inside = inside;
    out.crosses_near = near_cross;
    if (!projected) { out.offscreen = true; return true; } // wholly at or behind the eye plane
    if (near_cross) { x_lo = 0.f; y_lo = 0.f; x_hi = w_px; y_hi = h_px; z_lo = 0.f; }
    out.zmin = z_lo; out.zmax = z_hi;
    const float cx0 = x_lo > 0.f ? x_lo : 0.f, cy0 = y_lo > 0.f ? y_lo : 0.f;
    const float cx1 = x_hi < w_px ? x_hi : w_px, cy1 = y_hi < h_px ? y_hi : h_px;
    // An edge-on (zero-area) box inside the viewport stays on screen: the
    // accounting's own "tiny" bucket, not an off-screen draw.
    out.offscreen = cx1 < cx0 || cy1 < cy0;
    if (!out.offscreen) { out.x0 = cx0; out.y0 = cy0; out.x1 = cx1; out.y1 = cy1; }
    return true;
}

}
