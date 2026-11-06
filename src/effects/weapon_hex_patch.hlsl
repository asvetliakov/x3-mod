// Weapon effects, shield hit: the hex law of the patch (docs/architecture/weapon-effects.md 5.1.1), shared by the
// patch and decal pixel programs; tools/effects/weapon_effects_lab.html renders the same law in JS (hexCell, hexHash,
// hexLaw). Everything is planar: the pixel's position in its cell's lattice plane (view units). A hexagon lattice of
// centre spacing `cell` (pointy rows: centres (i, j sqrt 3) and (i + 1/2, (j + 1/2) sqrt 3) in cells, integer key
// (2 cx, 2 cy / sqrt 3)); per cell a hash of its key and the region seed gives a brightness stagger and a fade-rate
// stagger (deterministic, no per-frame randomness). The cells are lit as wholes: a patch draw shades each of its
// cells over the hull from its constant cell map (weapon_shell_ps.hlsl; rebuilt by the stage from the owner's live
// hits; a cell lit by several hits carries the newest clock, so a later hit resets it to full and never adds), the
// decal program evaluates its one hit's footprint itself (cells whose centre lies within `reach` of the hit cell's
// centre, lit when the ring front reaches that centre). A lit cell fades linearly over its own fade time as one unit,
// so the patch boundary is made of hexagon edges. The hit's own cell is the marker (user decision 2026-11-06, no
// per-pixel core flash): a cell lit as some hit's centre (the clock stored negative; the decal's hit cell) shines at
// centre_gain with its fill at centre_fill of the edge, on the same fade. Per ring hit (the region's newest, at most
// four): the ring is a soft Gaussian band at r = v age (v = ripple_speed R, R the footprint radius) evaluated at the
// cell centre's distance from the hit cell's centre (whole cells, within the footprint), fading as it nears the rim.
// Edges (the band within edge_width of the hexagon's border, anti-aliased by fwidth and faded to its area share once
// the band is under a pixel) are brighter than the fill (fill_ratio), for the cells and the ring alike. Ages are game
// seconds.
float4 lattice : register(c2);   // 1 / cell, cell, seed, unused
float4 colour : register(c3);    // rgb (linear), unused
float4 patch : register(c4);     // R (footprint radius), 1 / R, 1 / v (seconds per unit), v (units per game second)
float4 timing : register(c5);    // 1 / fade (per game second), stagger, unused, edge width (cells)
float4 gains : register(c6);     // cell (edge) gain, fill ratio, ring gain, centre gain
float4 shape : register(c7);     // 0.5 - edge width, 1 / (ring width R), reach (+ a tolerance), centre fill / edge
float4 hits[4] : register(c11);  // the hit cell's centre in the plane (view units), age (game seconds), on (0 / 1)
float4 points[4] : register(c15); // unused by the patches; points[0].z the decal's corner scale
// Two hash values in [0, 1) from the integer cell key and the seed (Hoskins' sine-free hash22).
float2 hex_hash(float2 key, float seed) {
    float3 p3 = frac(float3(key.x, key.y, key.x + key.y + seed * 17.0) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.xx + p3.yz) * p3.zy);
}
// The cell of plane point g (cells): xy the offset from the cell centre (cells), z the hexagon distance (0 at the
// centre, 0.5 on the border); key the integer key (2 cx, 2 cy / sqrt 3).
float3 hex_cell(float2 g, out float2 key) {
    const float2 s = float2(1.0, 1.7320508);
    float2 a = g - s * floor(g / s) - s * 0.5;
    float2 h = g - s * 0.5;
    float2 b = h - s * floor(h / s) - s * 0.5;
    float2 l = dot(a, a) < dot(b, b) ? a : b;
    key = floor((g - l) * float2(2.0, 1.1547005) + 0.5);
    float hd = max(abs(l.x), max(abs(0.5 * l.x + 0.8660254 * l.y), abs(0.5 * l.x - 0.8660254 * l.y)));
    return float3(l, hd);
}
// The radiance of one cell's pixel: l the pixel's offset from the cell centre (cells), hd its hexagon distance and fw
// that distance's screen derivative (fwidth, taken by the caller outside any branch), key the cell's integer key,
// centre its centre in the plane (view units); since its clock (game seconds since lit, < 0 unlit) and marker (lit as
// a hit's centre cell) for a patch; the decal (decal true) derives both from its one hit.
float3 hex_shade(float2 l, float hd, float fw, float2 key, float2 centre, float since, bool marker, const int count,
                 const bool decal) {
    float2 h = hex_hash(key, lattice.z);
    float edge = saturate((hd - shape.x) / fw + 0.5);
    float coverage = 1.0 - 4.0 * shape.x * shape.x;    // the band's share of the hexagon
    float mask = lerp(edge, coverage, saturate(fw / timing.w - 1.0));
    float pattern = lerp(gains.y, 1.0, mask);          // edge 1, fill fill_ratio
    float inv_fade = timing.x / (1.0 + timing.y * (h.y - 0.5));
    float ring = 0.0;
    [unroll] for (int i = 0; i < count; ++i) {
        [branch] if (hits[i].w > 0.5) { // uniform per draw: an empty slot costs one test
        float2 dcv = centre - hits[i].xy;
        float dc = sqrt(dot(dcv, dcv));
        float age = hits[i].z;
        float inside = step(dc, shape.z);
        if (decal) {
            since = inside > 0.5 ? age - dc * patch.z : -1.0;
            marker = dc < 0.5 * lattice.y;
        }
        float r = patch.w * age;
        float x = (dc - r) * shape.y;
        ring += exp(-x * x) * saturate(1.0 - r * patch.y) * inside;
        }
    }
    float cells = since >= 0.0 ? saturate(1.0 - since * inv_fade) : 0.0;
    float level = marker ? gains.w * lerp(shape.w, 1.0, mask) : pattern;
    float bright = gains.x * (1.0 + timing.y * (h.x - 0.5)) * level;
    return colour.rgb * (bright * cells + gains.z * pattern * ring);
}
// The decal: the pixel's cell from its plane point q (view units), the one hit at the plane's origin.
float3 hex_patch(float2 q, const int count) {
    float2 g = q * lattice.x;
    float2 key;
    float3 c = hex_cell(g, key);
    return hex_shade(c.xy, c.z, max(fwidth(c.z), 1e-5), key, (g - c.xy) * lattice.y, -1.0, false, count, true);
}
