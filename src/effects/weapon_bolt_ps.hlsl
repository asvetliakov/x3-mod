// Weapon effects, step 4: the bolt capsule's pixel program (docs/architecture/bolt-muzzle-look.md 2.1-2.6; drawn in
// place of the late bullet draw into the FP16 scene, ONE/ONE, LESSEQUAL with Z write off). Per pixel (VPOS, D3D9's
// integer pixel centres) the distance to the bolt's core polyline in pixels: the projected axis (head .. back, core then
// tail) and the screen streak from the head towards the previous head. Line kinds (tracer, slug, heavy): rho = r / (w/2)
// with the body width interpolated between the ends; a white-hot core (c_r 0.4, heavy 0.55, at least 1 px), the tinted
// body (its edge the deeper tint t_c) and a Gaussian halo of the saturated tint; the tracer's pointed tip over its front
// 1 w and its core tapering over its back 2 w into the tail root; the ball head (x (1 + head_gain (1 - s / w)); the heavy's
// head is the same round cap at c_r 0.55, no disc: 2026-11-08 #6); the core's whiteness h x saturate((w_px - 3) / 3) so a
// floor-width bolt keeps its tint (#5); the body's radiance E_b per bolt (p6.x of a line, a hand pin may lower it); the
// tail: a linear taper to 0 and a linear fall (the slug's white -> t -> 0.4 t). The streak:
// over its first core length at full radiance, beyond it the stretch falls as (1 - u)^2; the bolt x g_s (the stage's
// energy factor), the core held at max(g_s E_c, core_min). Orbs: a disc of the orb diameter with 7-9 rotating lobes (amp
// 0.05: 2026-11-08 #10), the
// body boiling with two octaves of value noise (x (0.75 + 0.5 n), h breathing 0.5 .. 0.9), the halo stretched 1.3 : 1
// along the travel and a broken wisp tail. Waves: a crescent across the travel (chord S, sagitta 0.25 S x c bowing
// forward, thickness 0.5 w, a sheet behind it at 0.15); below c 0.4 a soft filled lens of diameter S, no arc (#2: the
// fill 0.15 at the centre rising as (r / R)^4 to the tinted rim at 1.2, the halo outside), the crescent fading in over
// c 0.4 .. 0.5. The rim pass (c1.z = 1, ZERO / INVSRCCOLOR) returns the multiply band s = rim_dark x [rho in lo .. hi]
// (soft 0.4; rim_rho: 2.4 .. 3.6, under 5 px of body 1 .. 4 px past its edge, #5); its
// undo (c1.z = 2, DESTCOLOR / ONE: dest (1 + s / (1 - s)) = dest / (1 - s)) the band's inverse, drawn only when the
// capsule pass failed after the rim (WeaponEffectsPass::draw_bolts). A line's E_h is per end (the bounded halo cap: p6.w
// at the head, p2.z at the back, interpolated along the axis); its streak's stretch begins at the core length p1.y. A line's shading reach covers the halo's 3 sigma and,
// in the rim passes, the band's outer edge (1.8 w). Radiance x the fade. With BOLT_FAR_FLAG (weapon_bolt_flag_ps.hlsl) oC1 = oC0 and oC2 = far_flag_k x max(rgb). Compiled with
// generate_weapon_effects_programs.py (ps_3_0).
float4 look0 : register(c0); // e_core, e_body (orbs and waves; a line's is its p6.x), unused, far_flag_k
float4 look1 : register(c1); // head_gain, game time (s), rim pass (0 capsule / 1 rim / 2 rim undo), simple mode (0 / 1)
float4 look2 : register(c2); // rim_dark, rim band lo, hi (rho), soft edge (rho)
float4 look3 : register(c3); // kind (0 line, 1 orb, 2 wave), fewest lobes (+ 0..2 by the seed), lobe amplitude, turns/s
// Literals mirroring weapon_effects_core.h bolt:: (the fixture's gates measure them): rim_px_band {1, 4}, rim_px_w 5,
// rim_px_blend_w 7; core_h_w0 3, core_h_span 3; orb_lobes_spread 2; wave_lens_fill_e 0.15, wave_lens_rim_e 1.2,
// wave_lens_blend_c 0.1.
#ifdef BOLT_FAR_FLAG
struct Target {
    float4 c0 : COLOR0;
    float4 c1 : COLOR1;
    float4 c2 : COLOR2;
};
#endif
float hash2(float2 p) {
    p = frac(p * float2(0.1031, 0.1030));
    p += dot(p, p.yx + 33.33);
    return frac((p.x + p.y) * p.x);
}
float vnoise(float2 p) {
    float2 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash2(i), b = hash2(i + float2(1.0, 0.0)), c = hash2(i + float2(0.0, 1.0)), d = hash2(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}
// The radial profile at rho (body half widths) and distance da (px): (core, body, halo) weights.
float3 profile(float rho, float da, float cr, float sigma) {
    float core = 1.0 - smoothstep(0.75 * cr, 1.25 * cr, rho);
    float body = 1.0 - smoothstep(0.8, 1.0, rho);
    float halo = exp(-da * da / max(2.0 * sigma * sigma, 1e-4));
    return float3(core, body, halo);
}
float3 shade(float3 w, float rho, float3 t, float3 tc, float h, float e_core, float e_halo, float e_body) {
    float3 c_core = lerp(t, float3(1.0, 1.0, 1.0), h);
    float3 c_body = lerp(t, tc, smoothstep(0.5, 1.0, rho));
    return c_core * (e_core * w.x) + c_body * (e_body * max(w.y - w.x, 0.0)) + tc * (e_halo * w.z);
}
// The rim band (rho) of a body w px wide (stage::bolt_rim_rho): 1 .. 4 px past the edge under 5 px, the w band
// (look2.y .. z) from 7 px, linear between.
float2 rim_rho(float wpx) {
    float half_w = 0.5 * max(wpx, 1e-3);
    float2 px = 1.0 + float2(1.0, 4.0) / half_w;
    return lerp(px, look2.yz, saturate((wpx - 5.0) * 0.5));
}
#ifdef BOLT_FAR_FLAG
Target main(float2 vpos : VPOS, float4 p0 : TEXCOORD0, float4 p1 : TEXCOORD1, float4 p2 : TEXCOORD2,
            float4 p3 : TEXCOORD3, float4 p4 : TEXCOORD4, float4 p5 : TEXCOORD5, float4 p6 : TEXCOORD6)
#else
float4 main(float2 vpos : VPOS, float4 p0 : TEXCOORD0, float4 p1 : TEXCOORD1, float4 p2 : TEXCOORD2,
            float4 p3 : TEXCOORD3, float4 p4 : TEXCOORD4, float4 p5 : TEXCOORD5, float4 p6 : TEXCOORD6) : COLOR0
#endif
{
    float2 P = vpos;
    float2 H = p0.xy, ax = p0.zw;
    float A = p1.x, Lc = p1.y, T = p1.z, D = p1.w;
    float heavy = p5.x > 3.5 ? 1.0 : 0.0;
    float style = p5.x - 4.0 * heavy;
    float tracer = style < 0.5 ? 1.0 : 0.0;
    float slug = (style > 0.5 && style < 1.5) ? 1.0 : 0.0;
    float g_s = max(p2.w, 1e-3);
    float e_core = max(g_s * look0.x, 1.2) / g_s; // after x g_s: max(g_s E_c, core_min)
    float e_halo = p6.w;
    float3 t = p4.rgb;
    float tmin = min(t.r, min(t.g, t.b));
    // The edge tint: the same hue fully saturated, (t - min t) / (1 - min t) (the note's t^2 / max t^2 turns an orange's
    // hue by 10 degrees: fixture bolt_colour_core_rim); t itself under a quarter's saturation.
    float3 tc = (1.0 - tmin) < 0.25 ? t : (t - tmin) / max(1.0 - tmin, 1e-3);
    float h = p4.w;
    float kind = look3.x;
    float rim_pass = look1.z;
    float3 rgb = 0.0;
    float rim = 0.0;
    float2 d = P - H;
    [branch] if (kind < 0.5) {
        // ---- line kinds: the axis (core then tail) and the streak ----
        float sraw = dot(d, ax);
        [branch] if (rim_pass > 0.5) {
            // the rim pass: the band along the core's own silhouette (the tracer's pointed tip, no back cap across the
            // tail root where a tail follows), nothing else evaluated
            float wpx0 = lerp(p3.x, p3.y, A > 1e-3 ? Lc / A : 0.0);
            float s_end = max(Lc - 0.5 * wpx0, 0.0);
            float s_r = clamp(sraw, 0.0, s_end);
            float wpx_r = lerp(p3.x, p3.y, A > 1e-3 ? s_r / A : 0.0);
            float rs_r = (tracer > 0.5 && A > 2.0 * wpx_r) ? sqrt(saturate(sraw / wpx_r)) : 1.0;
            float rho_r = length(d - ax * s_r) / max(0.5 * wpx_r * rs_r, 0.35);
            float2 band = rim_rho(wpx_r);
            rim = saturate((rho_r - band.x) / look2.w) * saturate((band.y - rho_r) / look2.w);
            rim *= (T > 0.5 && sraw > s_end) ? 0.0 : 1.0;
        } else {
        float s = clamp(sraw, 0.0, A);
        float da = length(d - ax * s);
        float ss = 0.0, ds = 1e6;
        [branch] if (D > 0.5) {
            ss = clamp(dot(d, p2.xy), 0.0, D);
            ds = length(d - p2.xy * ss);
        }
        float reach = 0.5 * max(p3.x, p3.y) + 3.0 * max(p3.z, p3.w) + 1.0;
        [branch] if (min(da, ds) > reach) {
            rgb = 0.0; // outside every term's reach: no shading
        } else {
            // The core from its own segment [0, Lc] (its round cap and halo stay whole behind its end: an end-on bolt is
            // a disc with its halo), the tail from (Lc, A] on its own, the brighter of the two.
            // (the segment ends half a body width short of Lc, so the round cap ends at the core length)
            float wpx0 = lerp(p3.x, p3.y, A > 1e-3 ? Lc / A : 0.0);
            float s_c = clamp(sraw, 0.0, max(Lc - 0.5 * wpx0, 0.0));
            float d_c = length(d - ax * s_c);
            float tt = A > 1e-3 ? s_c / A : 0.0;
            float wpx = lerp(p3.x, p3.y, tt), sigma = lerp(p3.z, p3.w, tt);
            float e_halo_c = lerp(p6.w, p2.z, tt); // the bounded cap's per-end E_h (stage: the head's p6.w, the back's p2.z)
            float cr = max(heavy > 0.5 ? 0.55 : 0.4, 2.0 / max(wpx, 1e-3));
            float rs = 1.0;
            if (tracer > 0.5 && A > 2.0 * wpx) rs = sqrt(saturate(sraw / wpx));
            if (tracer > 0.5 && Lc > 4.0 * wpx) rs *= lerp(1.0, p5.z, saturate((s_c - (Lc - 2.0 * wpx)) / (2.0 * wpx)));
            float half_w = max(0.5 * wpx * rs, 0.35);
            float rho = d_c / half_w;
            {
                float e_b = p6.x;
                float hl = h * saturate((wpx - 3.0) / 3.0);
                float3 w = profile(rho, d_c, cr, sigma);
                w.x *= 1.0 + look1.x * saturate(1.0 - s_c / max(wpx, 1e-3));
                rgb = shade(w, rho, t, tc, hl, e_core, e_halo_c, e_b);
                [branch] if (T > 0.5 && sraw > Lc) {
                    float st = clamp(sraw - Lc, 0.0, T);
                    float d_t = length(d - ax * (Lc + st));
                    float fall = saturate(1.0 - st / T);
                    float tt_t = A > 1e-3 ? (Lc + st) / A : 0.0;
                    float wpx_t = lerp(p3.x, p3.y, tt_t), sigma_t = lerp(p3.z, p3.w, tt_t);
                    float half_t = max(0.5 * wpx_t * p5.z * fall, 0.35);
                    float3 wt = profile(d_t / half_t, d_t, cr, sigma_t);
                    float x = st / T;
                    float3 c_tail = t;
                    if (slug > 0.5) c_tail = x < 0.6 ? lerp(float3(1.0, 1.0, 1.0), t, x / 0.6) : lerp(t, 0.4 * t, (x - 0.6) / 0.4);
                    rgb = max(rgb, c_tail * (e_b * p5.w * fall * wt.y) + tc * (lerp(p6.w, p2.z, tt_t) * fall * wt.z));
                }
                [branch] if (D > 0.5 && ds <= reach) {
                    float u = D > Lc + 1e-3 ? saturate((ss - Lc) / (D - Lc)) : 0.0;
                    float sfall = ss <= Lc ? 1.0 : (1.0 - u) * (1.0 - u);
                    float rho_s = ds / max(0.5 * p3.x, 0.35);
                    float3 ws = profile(rho_s, ds, cr, p3.z);
                    rgb = max(rgb, shade(ws, rho_s, t, tc, hl, e_core, e_halo, e_b) * sfall);
                }
            }
        }
        }
    } else if (kind < 1.5) {
        // ---- orbs: a lobed, boiling disc of the orb diameter p3.x ----
        float len = length(d);
        float2 n = len > 1e-4 ? d / len : float2(1.0, 0.0);
        float ph = 6.2831853 * (look3.w * look1.y + p6.x);
        float cs = cos(ph), sn = sin(ph);
        float c = n.x * cs - n.y * sn;
        // cos(N theta), N = look3.y + 0, 1 or 2 by the seed: the Chebyshev recurrence T_k+1 = 2 c T_k - T_k-1 to N 9
        float nl = look3.y + floor(frac(p6.x * 7.31) * 2.999);
        float tm = 1.0, tk = c, tn = c;
        [unroll] for (int k = 2; k <= 9; ++k) {
            float tq = 2.0 * c * tk - tm;
            tm = tk;
            tk = tq;
            tn = abs(float(k) - nl) < 0.5 ? tq : tn;
        }
        float lobe = 1.0 + look3.z * (look1.w > 0.5 ? 0.0 : 1.0) * tn;
        float r_orb = 0.5 * p3.x;
        float rho = len / max(r_orb * lobe, 0.35);
        [branch] if (rim_pass > 0.5) {
            float2 band = rim_rho(p3.x);
            rim = saturate((rho - band.x) / look2.w) * saturate((band.y - rho) / look2.w);
        } else {
        float2 q = d / max(p3.x, 1e-3);
        float2 adv = float2(look1.y * 3.0, p6.x * 17.0);
        float nz = 0.6 * vnoise(q * 3.0 + adv) + 0.4 * vnoise(q * 6.4 - adv.yx);
        if (look1.w > 0.5) nz = 0.5;
        float along = dot(d, ax), across = dot(d, float2(-ax.y, ax.x));
        float dh = sqrt(along * along / (1.69) + across * across);
        float3 w = profile(rho, dh, 0.4, p3.z);
        w.y *= 0.75 + 0.5 * nz;
        float hh = lerp(0.5, 0.9, nz);
        float3 orb_rgb = shade(w, rho, t, tc, hh, e_core, e_halo, look0.y);
        // the wisp behind the orb (the axis from the centre, Lc = 0)
        float s = clamp(along, 0.0, A);
        float da = length(d - ax * s);
        float fall = T > 0.5 ? saturate(1.0 - s / T) : 0.0;
        float rho_t = da / max(0.5 * p3.x * p5.z * fall, 0.35);
        float brk = saturate((nz - 0.35) / 0.2);
        float3 wt = profile(rho_t, da, 0.4, p3.z);
        float3 wisp = t * (look0.y * p5.w * fall * wt.y * brk) * (along > 0.0 ? 1.0 : 0.0);
        // the streak
        float ss = clamp(dot(d, p2.xy), 0.0, D);
        float ds = length(d - p2.xy * ss);
        float u = D > p2.z + 1e-3 ? saturate((ss - p2.z) / (D - p2.z)) : 0.0;
        float sfall = ss <= p2.z ? 1.0 : (1.0 - u) * (1.0 - u);
        float rho_s = ds / max(r_orb, 0.35);
        float3 ws = profile(rho_s, ds, 0.4, p3.z);
        float3 streak_rgb = shade(ws, rho_s, t, tc, hh, e_core, e_halo, look0.y) * sfall;
        rgb = max(orb_rgb + wisp, D > 0.5 ? streak_rgb : 0.0);
        }
    } else {
        // ---- waves: the crescent across the travel, the ring end-on ----
        float2 fwd = -ax, nrm = float2(-ax.y, ax.x);
        float x = dot(d, nrm), y = dot(d, fwd);
        float S = p6.z, c = p6.y;
        float sag = 0.25 * S * c;
        float half_s = 0.5 * S;
        float xc = clamp(x, -half_s, half_s);
        float k = xc / max(half_s, 1e-3);
        float ya = sag * (1.0 - k * k);
        float slope = -2.0 * sag * xc / max(half_s * half_s, 1e-3);
        float d_arc = abs(x) <= half_s ? abs(y - ya) / sqrt(1.0 + slope * slope) : length(float2(abs(x) - half_s, y));
        float wpx = p3.x;
        float3 wa = profile(d_arc / max(0.5 * wpx, 0.35), d_arc, 0.4, p3.z);
        float3 arc_rgb = shade(wa, d_arc / max(0.5 * wpx, 0.35), t, tc, h, e_core, e_halo, look0.y);
        float sheet = (abs(x) < half_s && y > 0.0 && y < ya) ? 0.15 : 0.0;
        // the lens: no arc, no white core; the fill rises as (r / R)^4 to the rim's 1.2, the rim a body-wide band of the
        // tinted body at R (its peak 1.2: the halo yields inside the band), the halo about the rim
        float r = length(d);
        float u = saturate(r / max(half_s, 1e-3));
        float u2 = u * u;
        float d_rim = abs(r - half_s);
        float rho_r = d_rim / max(0.5 * wpx, 0.35);
        float rim_m = 1.0 - smoothstep(0.8, 1.0, rho_r);
        float e_lens = r < half_s ? lerp(0.15, 1.2, u2 * u2) : 0.0;
        float3 c_rim = lerp(t, tc, 0.5);
        float3 lens_rgb = c_rim * max(e_lens, 1.2 * rim_m) +
                          tc * (e_halo * (1.0 - rim_m) * exp(-d_rim * d_rim / max(2.0 * p3.z * p3.z, 1e-4)));
        rgb = lerp(lens_rgb, arc_rgb + t * sheet, saturate((c - 0.4) / 0.1));
    }
    rgb = max(rgb * (g_s * p5.y), 0.0);
    float sr = look2.x * rim * p5.y;
    sr = rim_pass > 1.5 ? sr / max(1.0 - sr, 1e-3) : sr;
    float4 outc = rim_pass > 0.5 ? float4(sr, sr, sr, 0.0) : float4(rgb, 0.0);
#ifdef BOLT_FAR_FLAG
    Target o;
    o.c0 = outc;
    o.c1 = outc;
    float m = rim_pass > 0.5 ? 0.0 : look0.w * max(rgb.r, max(rgb.g, rgb.b));
    o.c2 = float4(m, m, m, 0.0);
    return o;
#else
    return outc;
#endif
}
