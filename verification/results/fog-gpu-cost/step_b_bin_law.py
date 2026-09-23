#!/usr/bin/env python3
"""Fog route step B (docs/architecture/fog-gpu-cost.md): the look's bin law at 40 and 24 far bins, from the shader's own
rules (fog_density_field_inc.h under FOG_LOOK, bin centres, fine readiness 1): far step, last far sample, samples inside
the taper and the LOD blend, atlas fetches per ray (level_sample = 2 fetches per level; fine only below s 20,000, both
levels over the 20,000-30,000 blend, far only above), plus 6 per non-empty bin (sun-ward far tap 2 + shaft 2x2 PCF 4).
Static counts per ray, not measured GPU cost. Usage: /usr/bin/python3 verification/results/fog-gpu-cost/step_b_bin_law.py"""
CAP, TAPER, NEAR_END, NODE_FAR = 112500., 65000., 12000., 4096.


def law(far_bins, L=CAP):
    near, far = min(L, NEAR_END) / 24, max(L - NEAR_END, 0.) / far_bins
    centres = [near * (i + .5) for i in range(24)] + [NEAR_END + far * (j + .5) for j in range(far_bins)] if far > 0 else [near * (i + .5) for i in range(24)]
    def fetches(s):
        t = min(max((s - 20000.) / 10000., 0.), 1.); lam = 1 - t * t * (3 - 2 * t)
        return 2 * (lam > 0) + 2 * (lam < 1)
    atlas = sum(fetches(s) for s in centres)
    far_centres = centres[24:]
    return dict(far_bins=far_bins, L=L, far_step=far, step_over_far_node=far / NODE_FAR, iterations=24 + far_bins, samples=len(centres),
                last_far_centre=far_centres[-1] if far_centres else None, far_samples_in_taper=sum(TAPER <= s for s in far_centres),
                far_samples_in_blend=sum(20000. <= s < 30000. for s in far_centres), far_samples_fine_only=sum(s < 20000. for s in far_centres),
                atlas_fetches_empty_ray=1 + atlas, atlas_fetches_all_bins_fogged=1 + atlas + 6 * len(centres))


def main():
    for L in (CAP, 200000., 60000., 11000.):
        for bins in (40, 24):
            r = law(bins, L)
            print(' '.join(f'{k}={v:.1f}' if isinstance(v, float) else f'{k}={v}' for k, v in r.items()))
    a, b = law(40), law(24)
    print(f"sky ray to the cap: iterations {a['iterations']} -> {b['iterations']} ({1 - b['iterations'] / a['iterations']:.1%} fewer), "
          f"empty-ray fetches {a['atlas_fetches_empty_ray']} -> {b['atlas_fetches_empty_ray']}, all-fogged fetches {a['atlas_fetches_all_bins_fogged']} -> {b['atlas_fetches_all_bins_fogged']}")


if __name__ == '__main__':
    main()
