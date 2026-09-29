#pragma once
// Lens-flare gain (X3M_LENS_FLARE_GAIN, 0..1, 1 = off): the colour contribution of every ONE/ONE/ADD draw inside the
// lens bracket (sun_occlusion.h, the engine's call 0x00472491) is multiplied by G for that draw only, through the
// documented blend constant: SRCBLEND = BLENDFACTOR with D3DRS_BLENDFACTOR = G in every lane (D3DPBLENDCAPS_BLENDFACTOR),
// or, for G = 0, no draw at all (the admitted draw is not submitted: the hook returns S_OK with no state change, since
// 0 * src + dst is dst). The draw's own pixel program, textures and DESTBLEND are untouched, so the result is
// G * src + dst exactly (up to the 8-bit constant). docs/reverse-engineering/lens-flare-visibility.md,
// docs/architecture/sun-partial-occlusion.md ("Lens-flare gain"). Plain data and integer code: the per-draw caller
// runs on the draw hooks' light CPU boundary (no x87; the factor is quantised once at configuration).
#include <d3d9.h>

namespace x3m::lens_flare_gain {
constexpr float gain_min = 0.f, gain_max = 1.f;
struct Law {
    float gain = 1.f;
    bool active = false;   // G < 1: the only case with any per-draw work
    bool constant = false; // 0 < G < 1: BLENDFACTOR is set (and its cap required)
    bool skip = false;     // G = 0: an admitted draw is not submitted (no setter, no cap needed)
    DWORD factor = 0xffffffffu;
};
// A validated G (finite, 0..1); anything else is the caller's refusal and stays 1.
inline Law law(float gain) noexcept {
    Law out;
    if (!(gain >= gain_min && gain <= gain_max)) return out; // NaN fails both comparisons
    out.gain = gain;
    out.active = gain < 1.f;
    out.constant = gain > 0.f && gain < 1.f;
    out.skip = gain == 0.f;
    const DWORD q = DWORD(gain * 255.f + .5f);
    out.factor = (q << 24) | (q << 16) | (q << 8) | q;
    return out;
}
// The admitted blend law: blending on, ONE / ONE / ADD (the lens chain's additive cards).
inline bool admits(DWORD enable, DWORD src, DWORD dst, DWORD op) noexcept {
    return enable != FALSE && src == D3DBLEND_ONE && dst == D3DBLEND_ONE && op == D3DBLENDOP_ADD;
}
// The states in apply order (BLENDFACTOR first, so the constant is in place when SRCBLEND starts to read it); the
// restore walks them back in reverse. Returns the count (1 or 2). Not called for a skip law (G = 0), whose SRCBLEND
// ZERO step remains only as the law's exact equivalent.
inline unsigned steps(const Law& law, D3DRENDERSTATETYPE* states, DWORD* values) noexcept {
    unsigned n = 0;
    if (law.constant) {
        states[n] = D3DRS_BLENDFACTOR;
        values[n++] = law.factor;
    }
    states[n] = D3DRS_SRCBLEND;
    values[n++] = law.constant ? DWORD(D3DBLEND_BLENDFACTOR) : DWORD(D3DBLEND_ZERO);
    return n;
}
// Refusal reasons, one log row per reason and device.
enum Refusal : unsigned { Caps = 0, Route, Recording, RoutedDraw, StateUnknown, BlendLaw, Device, refusal_count };
inline const char* refusal_name(unsigned r) noexcept {
    static constexpr const char* names[refusal_count] = {"caps", "route", "recording", "routed_draw", "state_unknown",
                                                         "blend_law", "device"};
    return r < refusal_count ? names[r] : "unknown";
}
}
