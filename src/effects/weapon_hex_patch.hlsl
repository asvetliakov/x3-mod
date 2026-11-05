// Weapon effects, shield hit: the hex law of the patch (docs/architecture/weapon-effects.md 5.1.1), shared by the
// patch and decal pixel programs; tools/effects/weapon_effects_lab.html renders the same law in JS (hexCell, hexHash,
// hexPatch). Everything is planar: q is the pixel's position in the lattice plane (view units), each hit a point in
// that plane. A hexagon lattice of centre spacing `cell` (pointy rows: centres (i, j sqrt 3) and (i + 1/2,
// (j + 1/2) sqrt 3) in cells); per cell a hash of its integer key and the anchor seed gives a brightness stagger and a
// fade-rate stagger (deterministic, no per-frame randomness). Per hit: the ring front r = v age (v = ripple_speed R)
// lights a cell when it reaches the cell's centre (age >= d_centre / v), the lit cell fades linearly over its own fade
// time, dimmed toward the rim by 1 - (d_centre / R)^2 and cut to 0 at the pixel distance R (a feather of
// feather_cells); a soft Gaussian ring at r fading as it nears the rim; a Gaussian core at the hit for core_s. Cells
// combine as the maximum over the hits (a repeated hit re-lights a cell, it does not add), rings and cores add. Edges
// (the band within edge_width of the hexagon's border, anti-aliased by fwidth and faded to its area share once the
// band is under a pixel) are brighter than the fill (fill_ratio). Ages are game seconds.
float4 lattice : register(c2);   // 1 / cell, cell, seed, 2 x the stereographic scale (patches; 0 for decals)
float4 colour : register(c3);    // rgb (linear), unused
float4 patch : register(c4);     // R (lit radius), 1 / R, 1 / v (seconds per unit), v (units per game second)
float4 timing : register(c5);    // 1 / fade (per game second), stagger, 1 / core life, edge width (cells)
float4 gains : register(c6);     // cell (edge) gain, fill ratio, ring gain, core gain
float4 shape : register(c7);     // 0.5 - edge width, 1 / (ring width R), 1 / feather, 1 / core radius^2
float4 hits[4] : register(c11);  // hit position in the plane (view units), age (game seconds), on (0 / 1)
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
float3 hex_patch(float2 q, const int count) {
    float2 g = q * lattice.x;
    float2 key;
    float3 c = hex_cell(g, key);
    float2 centre = (g - c.xy) * lattice.y;     // the cell centre in the plane (view units)
    float2 h = hex_hash(key, lattice.z);
    float fw = max(fwidth(c.z), 1e-5);
    float edge = saturate((c.z - shape.x) / fw + 0.5);
    float coverage = 1.0 - 4.0 * shape.x * shape.x;    // the band's share of the hexagon
    float mask = lerp(edge, coverage, saturate(fw / timing.w - 1.0));
    float bright = gains.x * (1.0 + timing.y * (h.x - 0.5)) * lerp(gains.y, 1.0, mask);
    float inv_fade = timing.x / (1.0 + timing.y * (h.y - 0.5));
    float cells = 0.0, glow = 0.0;
    [unroll] for (int i = 0; i < count; ++i) {
        [branch] if (hits[i].w > 0.5) { // uniform per draw: an empty slot costs one test
        float2 dcv = centre - hits[i].xy;
        float2 dqv = q - hits[i].xy;
        float dc = sqrt(dot(dcv, dcv));
        float dq2 = dot(dqv, dqv);
        float dq = sqrt(dq2);
        float age = hits[i].z;
        float gate = saturate((patch.x - dq) * shape.z) * hits[i].w;
        float rel = dc * patch.y;
        float since = age - dc * patch.z;
        float env = saturate(1.0 - since * inv_fade) * saturate(1.0 - rel * rel) * step(0.0, since);
        cells = max(cells, env * gate);
        float r = patch.w * age;
        float x = (dq - r) * shape.y;
        glow += gains.z * exp(-x * x) * saturate(1.0 - r * patch.y) * gate;
        glow += gains.w * exp(-dq2 * shape.w) * saturate(1.0 - age * timing.z) * gate;
        }
    }
    return colour.rgb * (bright * cells + glow);
}
