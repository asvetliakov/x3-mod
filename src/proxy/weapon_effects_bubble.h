#pragma once
#include "../renderer/weapon_effects_pass.h"
#include "weapon_effects_core.h"
#include "sse_scalar.h"
#include <cstdint>
#include <cstring>

// The eye sphere's geometry (docs/architecture/weapon-effects.md 5.1.1 "Placement"): the own ship in the cockpit view
// draws its shield hits on a sphere about the eye (stage::build, Run240 Flight G), each hit mapped onto it radially
// (bubble_point) and lighting both faces of every axis, a face draw's box over the sphere's sheet (bubble_box), the
// pixel program weapon_bubble_ps.hlsl drawing its near and far points. The runtime hull walk that fitted a box-bounded
// ellipsoid to a fighter or corvette was removed on 2026-11-12 (user decision FL 69 A: every ship takes the lathe of
// its offline profile, weapon_effects_lathe.h; a ship without one stays native). Windows-free; no allocation, no libm,
// SSE2 scalars only.
namespace x3m::weapon_effects::stage {
namespace core = x3m::weapon_effects::core;
using Reader = bool (*)(std::uintptr_t address, void* out, std::size_t size);
// The bubble: centre c and half-extents e in the owner frame (view units).
struct Bubble {
    float c[3]{}, e[3]{1.f, 1.f, 1.f};
};
// The hit mapping: the recorded hit point p (owner frame, view units) in the bubble's normalised space q = (p - c) / e,
// Q = q / |q|, the bubble point B = c + e Q (a hit at the centre takes Q = +z). The hit lights both faces of every axis
// (the hull decal's rule, review F1); the opposite sheet's cells lie outside their anchor's depth window and
// bubble_box drops them, so a hit near an axis plane lights the same cells whichever side of it it lies.
inline void bubble_point(const Bubble& b, const float p[3], float out_b[3], float out_q[3]) noexcept {
    float q[3], n2 = 0.f;
    for (unsigned i = 0; i < 3; ++i) {
        q[i] = (p[i] - b.c[i]) / b.e[i];
        n2 += q[i] * q[i];
    }
    if (!(n2 > 1e-24f) || !(n2 < 1e30f)) {
        q[0] = q[1] = 0.f;
        q[2] = n2 = 1.f;
    }
    const float inv = 1.f / scalar::sqrt(n2);
    for (unsigned i = 0; i < 3; ++i) {
        out_q[i] = q[i] * inv;
        out_b[i] = b.c[i] + b.e[i] * out_q[i];
    }
}
// The cosine of the triplanar cut (core::face_cull_sin): a face takes no share where the bubble's normal is tilted
// further from its axis.
inline float bubble_cut_cos() noexcept {
    return scalar::sqrt(1.f - core::face_cull_sin * core::face_cull_sin);
}
// A face draw's box over the bubble (the draw's cells from fill_region; p.anchor, anchor_u, anchor_v its face frame):
// the plane bounds of its cells clipped to the bubble's projection where the face can take a share, and along the axis
// the depths of the bubble's sheet on the face's side over each cell's rectangle (centre +- a circumradius) within that
// share region (the normal within the cut of the axis implies rho^2 <= rho2_cap in the plane's normalised coordinates),
// each cell's within its anchor's depth -+ the extent (the hull decal's footprint rule: a lit point lies within the
// extent of its anchor hit in the plane and along the axis, so a face nearly edge-on to a hit never carries its cells
// far round the bubble). The pixel program tests its points against this box. False when no cell can show (the draw is
// not needed).
// The bubble in a face's frame (a, u, v: signed owner axes): its centre and half-extents along u, v, a, the share
// region's bound rho2_cap and the depth range of the face's sheet over it ([sheet_lo, ca + ea]); false when degenerate.
struct FaceSheet {
    float cu = 0.f, cv = 0.f, ca = 0.f, eu = 0.f, ev = 0.f, ea = 0.f, rho2_cap = 0.f, sheet_lo = 0.f;
};
inline bool face_sheet(const Bubble& b, const float a[3], const float u[3], const float v[3], FaceSheet* f) noexcept {
    *f = FaceSheet{};
    for (unsigned i = 0; i < 3; ++i) {
        f->cu += b.c[i] * u[i];
        f->cv += b.c[i] * v[i];
        f->ca += b.c[i] * a[i];
        f->eu += b.e[i] * (u[i] < 0.f ? -u[i] : u[i]);
        f->ev += b.e[i] * (v[i] < 0.f ? -v[i] : v[i]);
        f->ea += b.e[i] * (a[i] < 0.f ? -a[i] : a[i]);
    }
    if (!(f->eu > 0.f) || !(f->ev > 0.f) || !(f->ea > 0.f)) return false;
    const float c = bubble_cut_cos(), E = f->eu > f->ev ? f->eu : f->ev;
    f->rho2_cap = (1.f - c * c) * E * E / (c * c * f->ea * f->ea + (1.f - c * c) * E * E);
    f->sheet_lo = f->ca + f->ea * scalar::sqrt(1.f - f->rho2_cap);
    return true;
}
inline bool bubble_box(renderer::WeaponPatch& p, const Bubble& b) noexcept {
    FaceSheet fs;
    if (!face_sheet(b, p.anchor, p.anchor_u, p.anchor_v, &fs)) return false;
    const float cu = fs.cu, cv = fs.cv, ca = fs.ca, eu = fs.eu, ev = fs.ev, ea = fs.ea, rho2_cap = fs.rho2_cap;
    const float circum = 0.5773503f * p.cell;
    float lo = 1e30f, hi = -1e30f;
    auto range = [](float x0, float x1, float* mn, float* mx) {
        const float a = x0 * x0, d = x1 * x1;
        *mn = x0 <= 0.f && x1 >= 0.f ? 0.f : (a < d ? a : d);
        *mx = a > d ? a : d;
    };
    for (unsigned k = 0; k < p.cells; ++k) {
        const std::uint32_t packed = std::uint32_t(std::int32_t(p.cell_key[k])); // exact: below 2^24
        const float qx = 0.5f * float(int(packed >> 12) - renderer::weapon_cell_key_bias) * p.cell;
        const float qy = 0.8660254f * float(int(packed & 0xfffu) - renderer::weapon_cell_key_bias) * p.cell;
        float ux0, ux1, vy0, vy1;
        range((qx - circum - cu) / eu, (qx + circum - cu) / eu, &ux0, &ux1);
        range((qy - circum - cv) / ev, (qy + circum - cv) / ev, &vy0, &vy1);
        const float r_lo = ux0 + vy0;
        if (!(r_lo <= rho2_cap)) continue; // the sheet over this cell is steeper than the cut: nothing shows
        const float r_hi = ux1 + vy1 < rho2_cap ? ux1 + vy1 : rho2_cap;
        float d_hi = ca + ea * scalar::sqrt(1.f - r_lo), d_lo = ca + ea * scalar::sqrt(1.f - r_hi);
        const float w_lo = p.cell_anchor[k] - p.extent, w_hi = p.cell_anchor[k] + p.extent;
        d_lo = d_lo > w_lo ? d_lo : w_lo;
        d_hi = d_hi < w_hi ? d_hi : w_hi;
        if (!(d_hi >= d_lo)) continue; // the sheet over this cell lies outside its anchor's window
        lo = d_lo < lo ? d_lo : lo;
        hi = d_hi > hi ? d_hi : hi;
    }
    if (!(hi >= lo)) return false;
    const float pad = 1e-3f * p.cell + 1e-5f * (ea + (ca < 0.f ? -ca : ca));
    const float ru = eu * scalar::sqrt(rho2_cap), rv = ev * scalar::sqrt(rho2_cap);
    p.box[0] = p.box[0] > cu - ru ? p.box[0] : cu - ru;
    p.box[3] = p.box[3] < cu + ru ? p.box[3] : cu + ru;
    p.box[1] = p.box[1] > cv - rv ? p.box[1] : cv - rv;
    p.box[4] = p.box[4] < cv + rv ? p.box[4] : cv + rv;
    p.box[2] = lo - pad;
    p.box[5] = hi + pad;
    if (!(p.box[3] > p.box[0]) || !(p.box[4] > p.box[1])) return false;
    p.bubble = 1;
    for (unsigned i = 0; i < 3; ++i) {
        p.bubble_c[i] = b.c[i];
        p.bubble_e[i] = b.e[i];
    }
    return true;
}
} // namespace x3m::weapon_effects::stage
