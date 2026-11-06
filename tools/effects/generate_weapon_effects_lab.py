#!/usr/bin/env python3
"""Fill the data block of the weapon-effects lab page from the weapon-effects census and the lab asset manifests.

  python3 tools/effects/generate_weapon_effects_lab.py [--check]

Inputs: verification/results/weapon-effects-census/<view>.json (tools/effects/weapon_effects_census.py) and
verification/results/weapon-effects-lab/<view>/manifest.json (tools/effects/weapon_effects_lab_assets.py) for the
views stock_ap, mayhem3 and fl. The page tools/effects/weapon_effects_lab.html is hand-written except the block
`/* @weapon_effects_data */ ... /* @end */`, which this script writes: per view the weapons (TBullets columns the
page draws), only the Effects rows they reach (muzzle col 21, flight col 19, impact col 20, the flak death row, the
death rows of the ship tiers, stations and asteroids, and every EEDF_PROPAGATE child, including the rows only the
manifest's extra_effects carries), the death tiers with their rows and object counts, the texture/body status
counts, and per weapon its tools/effects/weapon_catalog/<view>.json entry (tools/effects/weapon_catalog.py: laser
names, size, used-by classes, style, numbers, flags, effect summaries, description) for the page's description panel;
a view without a catalogue file, or a weapon without an entry, gets none. Body geometry and textures stay in the untracked <view>/assets.js and tex/*.png beside the manifest; the page
loads them by relative path. The generated page is also copied to verification/results/weapon-effects-lab/
weapon_effects_lab.html so it opens from the results directory with file://.
--check writes nothing and fails when either page differs from what the inputs give.
"""
import argparse
import json
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
HTML = REPO / 'tools/effects/weapon_effects_lab.html'
CENSUS = REPO / 'verification/results/weapon-effects-census'
LAB = REPO / 'verification/results/weapon-effects-lab'
CATALOG = REPO / 'tools/effects/weapon_catalog'
COPY = LAB / 'weapon_effects_lab.html'
VIEWS = ('stock_ap', 'mayhem3', 'fl')
VIEW_LABELS = {'stock_ap': 'Stock AP (pinned 01-13 + addon/01-04)', 'mayhem3': 'Mayhem 3 (installed AP layers)',
               'fl': "Farnham's Legacy (addon2)"}
BEGIN, END = '/* @weapon_effects_data */', '/* @end */'
HEADER_BEGIN, HEADER_END = '/* @weapon_effects_header */', '/* @end */'
HEADER = REPO / 'src/proxy/weapon_effects_core.h'
# The shield-hit patch constants the lab's renderOurs() and its controls default to (every control's default is the
# header's value, docs/architecture/weapon-effects.md 5.1.1). fade_ms is unsigned; cell_m, rings and the three colours
# are arrays (`name[N] = {a, b, c}`; rings holds integers). The stage's capacities come from their own headers
# (CAPACITIES: name in the page, header, C++ name), integers.
HEADER_NAMES = ('engine_units_per_metre', 'cell_m', 'rings', 'bubble_scale', 'bubble_far_gain', 'ripple_speed', 'ring_width', 'ring_gain', 'centre_gain',
                'centre_fill', 'fade_ms', 'stagger', 'edge_width', 'fill_ratio', 'cell_gain', 'colour_high', 'colour_mid', 'colour_low', 'colour_full', 'colour_half',
                'colour_empty', 'weapon_tint', 'tint_mix', 'soft', 'own_cockpit_gain', 'triplanar_sharpness', 'triplanar_floor', 'face_cull_sin', 'tracer_min_elongation', 'slug_min_elongation')
HEADER_INTS = ('fade_ms',)
CAPACITIES = (('cell_key_bias', 'src/renderer/weapon_effects_pass.h', 'weapon_cell_key_bias'),
              ('patch_cells', 'src/renderer/weapon_effects_pass.h', 'weapon_patch_cells'),
              ('patch_hits', 'src/renderer/weapon_effects_pass.h', 'weapon_patch_hits'),
              ('owner_hits', 'src/proxy/weapon_effects_core.h', 'owner_hits'),
              ('region_draws', 'src/proxy/weapon_effects_stage.h', 'region_draws'))
HEADER_INT_ARRAYS = ('rings',)
# The shimmer pass's terms the lab's approximate ring refraction uses (docs/architecture/hull-impact-classes.md 3.5): the
# displacement amplitude and the ring band, both px at the reference rows (name in the page, header, C++ name), floats.
SHIMMER = (('shimmer_px', 'src/proxy/engine_shimmer_core.h', 'default_px'),
           ('shimmer_rows', 'src/proxy/engine_shimmer_core.h', 'reference_rows'),
           ('ring_band_px', 'src/proxy/engine_shimmer_core.h', 'weapon_ring_band_px'))
NUMBER = r'-?[0-9.]+(?:e-?[0-9]+)?'
# The shared look and the hull hit (docs/architecture/weapon-look.md 5): the constants of `namespace look` and
# `namespace hull` in weapon_effects_core.h, read inside their namespace (the same names recur in later families), as
# HEADER.look and HEADER.hull; heat_ramp is a 5 x 3 array, tug_colour a 3-array, hull's mode the enum value's name.
NAMESPACES = {
    'look': ('heat_ramp', 'min_frames', 'soft_core', 'soft_halo', 'core_e', 'body_max', 'halo_max', 'halo_fold',
             'streak_cap', 'streak_core_min', 'floor_core_px', 'floor_lit_px', 'floor_len_px', 'floor_flash_px',
             'budget_weight_patch', 'budget_weight_glow', 'fireball_freq', 'fireball_advect', 'fireball_noise',
             'fireball_erode', 'fireball_core_t', 'fireball_octaves', 'budget_weight_fireball', 'budget_weight_fireball_lod'),
    # the hull-impact classes (docs/architecture/hull-impact-classes.md): one row per Anatomy of each class_* grid
    'hull': ('r_base', 'r_k', 'r_scale', 'flash_halo_t', 'spark_rest_w', 'bias', 'tug_colour', 'mode', 'spark_count_x',
             'spark_count_spread', 'spark_life_lo', 'spark_life_hi', 'spark_len_lo', 'spark_len_hi', 'spark_width_lo',
             'spark_width_hi', 'spark_curl_share', 'spark_curl_x', 'class_flash', 'class_puff', 'class_sparks',
             'class_glow', 'class_ember', 'class_ring', 'ring_t', 'ring_rise', 'ring_lead_t', 'ring_trail_t',
             'ring_trail_gain', 'ring_trail_span', 'ring_noise_freq', 'ring_noise_depth', 'ring_noise_contrast',
             'ring_wash_gain', 'ring_wash_fall', 'ring_stretch', 'ring_refract', 'ring_seed_axis', 'ring_seed_scale',
             'ring_noise_window', 'ring_front_window', 'ring_seed_element', 'budget_weight_shimmer', 'scorch', 'class_scorch', 'scorch_colour',
             'scorch_per_owner', 'class_shimmer', 'shimmer_ms', 'shimmer_max', 'streak_back', 'streak_front',
             'streak_width', 'streak_e', 'streak_fallback_halo', 'streak_ms', 'filament_r', 'filament_e',
             'filament_scale', 'filament_width', 'filament_ms', 'rehash_ms', 'flicker_ms', 'ion_colour', 'hold_ms',
             'spot_fall_ms', 'spatter_period_ms', 'spatter_cap', 'hold_r', 'spot_core', 'spot_halo', 'trail_step_r',
             'budget_weight_ring', 'budget_weight_filament', 'budget_weight_scorch', 'budget_weight_flash',
             'puff_lod_px')}
# The hull-impact class rule (docs/architecture/hull-impact-classes.md section 2; the DLL's core::hull_class_from, the
# census script verification/results/weapon-effects/hull_class_census.py) on a catalogue row, then the override file's
# `hull` pins (tools/effects/weapon_overrides.json by_id): the lab's `auto` class of a weapon (LAB_DATA weapons[].hc).
OVERRIDES = REPO / 'tools/effects/weapon_overrides.json'
CLASSES = ('energy', 'ballistic', 'plasma', 'heavy', 'ion', 'beam', 'missile', 'repair', 'asteroid')


def hull_class_rule(flags, damage, elongation):
    beam, heavy = flags & 0x6, damage >= 20000
    if beam and damage == 0:
        return 'tug'
    if flags & 0x80:
        return 'repair'
    if beam:
        return 'beam'
    if flags & 0x600:
        return 'ion'
    if flags & 0x40:
        return 'heavy' if heavy else 'ballistic'
    shape = 'wave' if flags & 0x8 else 'slug' if elongation is None else (
        'tracer' if elongation >= 4 else 'slug' if elongation >= 1.5 else 'orb')
    if shape in ('orb', 'wave'):
        return 'plasma'
    return 'heavy' if heavy else 'energy'


def hull_pins(path=OVERRIDES):
    data = json.loads(pathlib.Path(path).read_text())
    return {k: v['hull'] for k, v in data.get('by_id', {}).items() if isinstance(v, dict) and 'hull' in v}


def hull_class_of(entry, pins):
    if entry.get('id') in pins:
        return pins[entry['id']]
    fw = entry.get('flag_word')
    flags = int(fw, 16) if isinstance(fw, str) else int(fw or 0)
    damage = max(entry.get('damage_shield') or 0, entry.get('damage_hull') or 0)
    return hull_class_rule(flags, damage, entry.get('elongation'))
# The lab's presets (weapon-look.md 6): <family>/<case>, each naming its view and weapon id (--check fails when the id
# is missing from its view's census, or a family has no preset), the victim tier and preview hull, the camera, the SETA
# rate, the hits per game second, the own ship and the cockpit view, the hull-hit variant, the calibre k and the
# shield level; the hull-impact class (auto: the weapon's; lineup: the nine side by side), a beam's pulse (on, period
# game ms; 0: sustained) and sweep (r_hit per tick) and the loop (game ms; 0: the record life). Families: shield (step
# 1, accepted) and hull (step 2: the 3.1 list and one preset per class of hull-impact-classes.md section 7; --check
# fails when a class has none).
FAMILIES = ('shield', 'hull')
PRESETS = (
    dict(id='shield/fighter-ire-side', family='shield', view='mayhem3', weapon='SS_BULLET_IRE', tier=0, hull='flat_top',
         camera='side', seta=1, hps=4, own=False, cockpit=False, variant='none', k=0.33, shield=100, cls='auto', pulse=[0, 0], sweep=0, loop=0),
    dict(id='shield/capital-ppc-flank', family='shield', view='mayhem3', weapon='SS_BULLET_PPC', tier=2, hull='narrow',
         camera='side', seta=1, hps=12, own=False, cockpit=False, variant='none', k=0.8, shield=60, cls='auto', pulse=[0, 0], sweep=0, loop=0),
    dict(id='hull/fighter-ire-side', family='hull', view='mayhem3', weapon='SS_BULLET_IRE', tier=0, hull='flat_top',
         camera='side', seta=1, hps=0, own=False, cockpit=False, variant='none', k=0.33, shield=0, cls='auto', pulse=[0, 0], sweep=0, loop=0),
    dict(id='hull/fighter-ire-chase-nose', family='hull', view='mayhem3', weapon='SS_BULLET_IRE', tier=0,
         hull='chase_nose', camera='chase', seta=1, hps=0, own=True, cockpit=False, variant='none', k=0.33, shield=0, cls='auto', pulse=[0, 0], sweep=0, loop=0),
    dict(id='hull/fighter-ire-cockpit-own', family='hull', view='mayhem3', weapon='SS_BULLET_IRE', tier=0, hull='flat_top',
         camera='cockpit', seta=1, hps=8, own=True, cockpit=True, variant='none', k=0.33, shield=0, cls='auto', pulse=[0, 0], sweep=0, loop=0),
    dict(id='hull/corvette-ppc-flank', family='hull', view='mayhem3', weapon='SS_BULLET_PPC', tier=1, hull='narrow',
         camera='side', seta=1, hps=0, own=False, cockpit=False, variant='none', k=0.8, shield=0, cls='auto', pulse=[0, 0], sweep=0, loop=0),
    dict(id='hull/capital-ppc-flank-300m', family='hull', view='mayhem3', weapon='SS_BULLET_PPC', tier=2, hull='narrow',
         camera='side', seta=1, hps=0, own=False, cockpit=False, variant='none', k=0.8, shield=0, cls='auto', pulse=[0, 0], sweep=0, loop=0),
    dict(id='hull/capital-close-12hz', family='hull', view='mayhem3', weapon='SS_BULLET_PPC', tier=2, hull='sphere',
         camera='side', seta=1, hps=12, own=False, cockpit=False, variant='none', k=0.8, shield=0, cls='auto', pulse=[0, 0], sweep=0, loop=0),
    dict(id='hull/station-ppc-1km', family='hull', view='mayhem3', weapon='SS_BULLET_PPC', tier=2, hull='sphere',
         camera='far1', seta=1, hps=4, own=False, cockpit=False, variant='none', k=0.8, shield=0, cls='auto', pulse=[0, 0], sweep=0, loop=0),
    dict(id='hull/mining-asteroid-60m', family='hull', view='stock_ap', weapon='SS_BULLET_MASS', tier=1, hull='sphere',
         camera='side', seta=1, hps=4, own=False, cockpit=False, variant='asteroid', k=0.3, shield=0, cls='auto', pulse=[0, 0], sweep=0, loop=0),
    dict(id='hull/missile-hit-corvette', family='hull', view='mayhem3', weapon='SS_BULLET_IRE', tier=1, hull='narrow',
         camera='side', seta=1, hps=0, own=False, cockpit=False, variant='missile', k=0.5, shield=0, cls='auto', pulse=[0, 0], sweep=0, loop=0),
    dict(id='hull/seta6-12hz', family='hull', view='mayhem3', weapon='SS_BULLET_IRE', tier=0, hull='flat_top',
         camera='side', seta=6, hps=12, own=False, cockpit=False, variant='none', k=0.33, shield=0, cls='auto', pulse=[0, 0], sweep=0, loop=0),
    dict(id='hull/far-2km', family='hull', view='mayhem3', weapon='SS_BULLET_IRE', tier=0, hull='flat_top',
         camera='far2', seta=1, hps=4, own=False, cockpit=False, variant='none', k=0.33, shield=0, cls='auto', pulse=[0, 0], sweep=0, loop=0),
    dict(id='hull/drained', family='hull', view='mayhem3', weapon='SS_BULLET_IRE', tier=0, hull='flat_top', camera='side',
         seta=1, hps=4, own=False, cockpit=False, variant='none', k=0.33, shield=0, cls='auto', pulse=[0, 0], sweep=0, loop=0),
    dict(id='hull/repair-corvette', family='hull', view='mayhem3', weapon='SS_BULLET_RL', tier=1, hull='narrow',
         camera='side', seta=1, hps=12, own=False, cockpit=False, variant='repair', k=0.5, shield=0, cls='auto', pulse=[0, 0], sweep=0, loop=0),
    dict(id='hull/stock-vs-mayhem', family='hull', view='stock_ap', weapon='SS_BULLET_IRE', tier=0, hull='flat_top',
         camera='side', seta=1, hps=0, own=False, cockpit=False, variant='none', k=0.33, shield=0, cls='auto', pulse=[0, 0], sweep=0, loop=0),
    # the heavy families (2026-11-06): the burst, the sparks and the shock ring
    dict(id='hull/heavy-plasma-corvette', family='hull', view='mayhem3', weapon='SS_BULLET_HEPT', tier=1, hull='narrow',
         camera='side', seta=1, hps=0, own=False, cockpit=False, variant='none', k=0.63, shield=0, cls='auto', pulse=[0, 0], sweep=0, loop=0),
    dict(id='hull/heavy-flak-fighter', family='hull', view='stock_ap', weapon='SS_BULLET_FLAK_ALPHA', tier=0,
         hull='flat_top', camera='side', seta=1, hps=0, own=False, cockpit=False, variant='flak', k=0.83, shield=0, cls='auto', pulse=[0, 0], sweep=0, loop=0),
    dict(id='hull/heavy-missile-hit', family='hull', view='mayhem3', weapon='SS_BULLET_IRE', tier=0, hull='flat_top',
         camera='side', seta=1, hps=0, own=False, cockpit=False, variant='missile', k=0.5, shield=0, cls='auto', pulse=[0, 0], sweep=0, loop=0),
    # the hull-impact classes (docs/architecture/hull-impact-classes.md section 7)
    dict(id='hull/energy-ire-fighter-side', family='hull', view='mayhem3', weapon='SS_BULLET_IRE', tier=0, hull='flat_top',
         camera='side', seta=1, hps=0, own=False, cockpit=False, variant='none', k=0.33, shield=0, cls='auto', pulse=[0, 0],
         sweep=0, loop=0),
    dict(id='hull/ballistic-md-fighter-side', family='hull', view='mayhem3', weapon='SS_BULLET_MD', tier=0, hull='flat_top',
         camera='side', seta=1, hps=0, own=False, cockpit=False, variant='none', k=0.43, shield=0, cls='auto', pulse=[0, 0],
         sweep=0, loop=0),
    dict(id='hull/ballistic-gauss-capital-300m', family='hull', view='stock_ap', weapon='SS_BULLET_GAUSS', tier=2,
         hull='narrow', camera='side', seta=1, hps=0, own=False, cockpit=False, variant='none', k=0.98, shield=0, cls='auto',
         pulse=[0, 0], sweep=0, loop=0),
    dict(id='hull/plasma-hept-corvette-flank', family='hull', view='mayhem3', weapon='SS_BULLET_HEPT', tier=1, hull='narrow',
         camera='side', seta=1, hps=0, own=False, cockpit=False, variant='none', k=0.63, shield=0, cls='auto', pulse=[0, 0],
         sweep=0, loop=0),
    dict(id='hull/plasma-psp-capital-close', family='hull', view='mayhem3', weapon='SS_BULLET_PSP', tier=2, hull='sphere',
         camera='side', seta=1, hps=0, own=False, cockpit=False, variant='none', k=1.0, shield=0, cls='auto', pulse=[0, 0],
         sweep=0, loop=0),
    dict(id='hull/heavy-gke-capital-300m', family='hull', view='mayhem3', weapon='SS_BULLET_GKE', tier=2, hull='narrow',
         camera='side', seta=1, hps=0, own=False, cockpit=False, variant='none', k=1.0, shield=0, cls='auto', pulse=[0, 0],
         sweep=0, loop=0),
    dict(id='hull/heavy-rg-chase-nose', family='hull', view='mayhem3', weapon='SS_BULLET_RG', tier=0, hull='chase_nose',
         camera='chase', seta=1, hps=0, own=True, cockpit=False, variant='none', k=0.96, shield=0, cls='auto', pulse=[0, 0],
         sweep=0, loop=0),
    dict(id='hull/heavy-thin-wing-exit-jet', family='hull', view='mayhem3', weapon='SS_BULLET_RG', tier=0, hull='flat_top',
         camera='side', seta=1, hps=0, own=False, cockpit=False, variant='none', k=0.96, shield=0, cls='auto', pulse=[0, 0],
         sweep=0, loop=0),
    dict(id='hull/beam-pbc-pulsed-corvette', family='hull', view='mayhem3', weapon='SS_BULLET_PBC', tier=1, hull='narrow',
         camera='side', seta=1, hps=0, own=False, cockpit=False, variant='none', k=0.8, shield=0, cls='auto',
         pulse=[900, 2222], sweep=0, loop=4444),
    dict(id='hull/beam-pal-sustained-sweep', family='hull', view='stock_ap', weapon='SS_BULLET_PAL', tier=2, hull='narrow',
         camera='side', seta=1, hps=0, own=False, cockpit=False, variant='none', k=0.6, shield=0, cls='auto', pulse=[0, 0],
         sweep=0.3, loop=6000),
    dict(id='hull/ion-id-fighter-side', family='hull', view='mayhem3', weapon='SS_BULLET_ID', tier=0, hull='flat_top',
         camera='side', seta=1, hps=0, own=False, cockpit=False, variant='none', k=0.62, shield=0, cls='auto', pulse=[0, 0],
         sweep=0, loop=0),
    dict(id='hull/ion-ionc-capital', family='hull', view='stock_ap', weapon='SS_BULLET_IONC', tier=2, hull='narrow',
         camera='side', seta=1, hps=0, own=False, cockpit=False, variant='none', k=1.0, shield=0, cls='auto', pulse=[0, 0],
         sweep=0, loop=0),
    dict(id='hull/class-lineup-fighter-side', family='hull', view='mayhem3', weapon='SS_BULLET_IRE', tier=0, hull='flat_top',
         camera='side', seta=1, hps=0, own=False, cockpit=False, variant='none', k=0.33, shield=0, cls='lineup',
         pulse=[0, 0], sweep=0, loop=0),
    dict(id='hull/class-lineup-capital-300m', family='hull', view='mayhem3', weapon='SS_BULLET_PPC', tier=2, hull='narrow',
         camera='side', seta=1, hps=0, own=False, cockpit=False, variant='none', k=0.8, shield=0, cls='lineup',
         pulse=[0, 0], sweep=0, loop=0),
    dict(id='hull/scorch-12hps-20s', family='hull', view='mayhem3', weapon='SS_BULLET_IRE', tier=1, hull='narrow',
         camera='side', seta=1, hps=12, own=False, cockpit=False, variant='none', k=0.33, shield=0, cls='energy',
         pulse=[0, 0], sweep=0, loop=20000),
    dict(id='hull/seta6-classes', family='hull', view='mayhem3', weapon='SS_BULLET_IRE', tier=0, hull='flat_top',
         camera='side', seta=6, hps=0, own=False, cockpit=False, variant='none', k=0.33, shield=0, cls='lineup',
         pulse=[0, 0], sweep=0, loop=0),
)

PRESET_KEYS = ('id', 'family', 'view', 'weapon', 'tier', 'hull', 'camera', 'seta', 'hps', 'own', 'cockpit', 'variant', 'k',
               'shield', 'cls', 'pulse', 'sweep', 'loop')
PRESET_CLASSES = ('auto', 'lineup') + CLASSES
CAMERAS = ('side', 'chase', 'cockpit', 'far1', 'far2', 'far8')
VARIANTS = ('none', 'repair', 'tug', 'missile', 'asteroid', 'flak')
PRESETS_BEGIN = '/* @weapon_effects_presets */'


def preset_problems(presets, data):
    """Why the presets cannot be injected: an id missing from its view, an unknown key or value, a family without one."""
    out = []
    for pr in presets:
        if tuple(pr) != PRESET_KEYS:
            out.append(f"{pr.get('id')}: keys {tuple(pr)}")
        if pr.get('family') not in FAMILIES or not str(pr.get('id', '')).startswith(f"{pr.get('family')}/"):
            out.append(f"{pr.get('id')}: family {pr.get('family')!r}")
        if pr.get('camera') not in CAMERAS or pr.get('variant') not in VARIANTS:
            out.append(f"{pr.get('id')}: camera {pr.get('camera')!r} / variant {pr.get('variant')!r}")
        if pr.get('cls') not in PRESET_CLASSES:
            out.append(f"{pr.get('id')}: class {pr.get('cls')!r}")
        view = data.get(pr.get('view'))
        if view is None:
            out.append(f"{pr['id']}: view {pr.get('view')!r} not loaded")
        elif pr.get('weapon') not in {w['id'] for w in view['weapons']}:
            out.append(f"{pr['id']}: weapon {pr.get('weapon')} not in view {pr['view']}")
    for family in FAMILIES:
        if not any(pr.get('family') == family for pr in presets):
            out.append(f'family {family} has no preset')
    return out


def class_problems(presets, data):
    """hull-impact-classes.md section 7: one hull preset per class (its own class, its variant's, or its weapon's)."""
    seen = set()
    for pr in presets:
        if pr.get('family') != 'hull':
            continue
        variant = pr.get('variant')
        if variant in ('missile', 'repair', 'asteroid'):
            seen.add(variant)
        elif pr.get('cls') in CLASSES:
            seen.add(pr['cls'])
        elif pr.get('cls') == 'auto':
            weapons = {w['id']: w for w in (data.get(pr.get('view')) or {}).get('weapons', [])}
            seen.add((weapons.get(pr.get('weapon')) or {}).get('hc', 'energy'))
    return [f'class {cls} has no preset' for cls in CLASSES if cls not in seen]


def presets_block(presets):
    return ('%s\n// Generated by tools/effects/generate_weapon_effects_lab.py (PRESETS); do not edit.\nconst PRESETS = %s;\n%s'
            % (PRESETS_BEGIN, json.dumps(list(presets), separators=(',', ':')), END))


def namespace_constants(text):
    out = {}
    for ns, names in NAMESPACES.items():
        m = re.search(r'namespace %s \{\n(.*?)\n\} // namespace %s' % (ns, ns), text, re.S)
        if not m:
            raise SystemExit(f'weapon_effects_core.h: namespace {ns} not found')
        body, values = m.group(1), {}
        for name in names:
            if name == 'mode':
                mode = re.search(r'constexpr Mode mode = (\w+);', body)
                if not mode:
                    raise SystemExit(f'weapon_effects_core.h: {ns}::mode not found')
                values[name] = mode.group(1)
                continue
            grid = re.search(r'\b%s\[[^\]]*\]\[[^\]]*\] = \{(.*?)\};' % name, body, re.S)
            if grid:
                values[name] = [[float(v.strip().rstrip('f')) for v in row.split(',')]
                                for row in re.findall(r'\{([^{}]*)\}', grid.group(1))]
                continue
            array = re.search(r'\b%s\[[^\]]*\] = \{([^}]*)\}' % name, body)
            if array:
                values[name] = [float(v.strip().rstrip('f')) for v in array.group(1).split(',')]
                continue
            flag = re.search(r'\b%s = (true|false)\b' % name, body)
            if flag:
                values[name] = flag.group(1) == 'true'
                continue
            scalar = re.search(r'\b%s = (%s)f?\b' % (name, NUMBER), body)
            if not scalar:
                raise SystemExit(f'weapon_effects_core.h: constant {ns}::{name} not found')
            v = float(scalar.group(1))
            values[name] = int(v) if v.is_integer() and '.' not in scalar.group(1) else v
        out[ns] = values
    return out


def header_constants(text):
    values = {}
    for name in HEADER_NAMES:
        array = re.search(r'\b%s\[[^\]]*\] = \{([^}]*)\}' % name, text)
        if array:
            values[name] = [float(v.strip().rstrip('f')) for v in array.group(1).split(',')]
            if name in HEADER_INT_ARRAYS:
                values[name] = [int(v) for v in values[name]]
            continue
        m = re.search(r'\b%s = (%s)f?\b' % (name, NUMBER), text)
        if not m:
            raise SystemExit('weapon_effects_core.h: constant %s not found' % name)
        values[name] = int(float(m.group(1))) if name in HEADER_INTS else float(m.group(1))
    for name, path, cname in CAPACITIES:
        m = re.search(r'\b%s = (\d+)\b' % cname, (REPO / path).read_text())
        if not m:
            raise SystemExit('%s: constant %s not found' % (path, cname))
        values[name] = int(m.group(1))
    for name, path, cname in SHIMMER:
        m = re.search(r'\b%s = (%s)f?\b' % (cname, NUMBER), (REPO / path).read_text())
        if not m:
            raise SystemExit('%s: constant %s not found' % (path, cname))
        values[name] = float(m.group(1))
    values.update(namespace_constants(text))
    return values


def header_block(values):
    body = ',\n'.join('  %s: %s' % (name, json.dumps(values[name], separators=(',', ':')) if name in NAMESPACES
                                     else json.dumps(values[name]))
                       for name in HEADER_NAMES + tuple(c[0] for c in CAPACITIES + SHIMMER) + tuple(NAMESPACES))
    return ('%s\n// Generated by tools/effects/generate_weapon_effects_lab.py from src/proxy/weapon_effects_core.h; do not edit.\n'
            'const HEADER = {\n%s\n};\n%s' % (HEADER_BEGIN, body, HEADER_END))
# Death tiers the page offers: (label, TShips classes) or a non-ship source.
TIERS = (('M5/M4/M3', ('OBJ_SHIP_M5', 'OBJ_SHIP_M4', 'OBJ_SHIP_M3')),
         ('M6/M8/TS/TP', ('OBJ_SHIP_M6', 'OBJ_SHIP_M8', 'OBJ_SHIP_TS', 'OBJ_SHIP_TP')),
         ('M1/M2/M7/TL', ('OBJ_SHIP_M1', 'OBJ_SHIP_M2', 'OBJ_SHIP_M7', 'OBJ_SHIP_TL')),
         ('station', ('TDocks', 'TFactories')),
         ('asteroid', ('TAsteroids',)))
ELEMENT_KEYS = (('bodyid', 'bodyid'), ('minsize', 'min'), ('maxsize', 'max'), ('duration', 'dur'),
                ('rndsize', 'rnd'), ('lensflarenr', 'flare'), ('particlenr', 'particle'), ('atten', 'atten'),
                ('halfspeed', 'half'), ('timeoffset', 'toff'))
PROPAGATE_KEYS = (('prop_effect_id', 'pid'), ('min_props', 'pmin'), ('max_props', 'pmax'),
                  ('min_prop_delay', 'pdmin'), ('max_prop_delay', 'pdmax'), ('prop_length', 'plen'),
                  ('prop_delay', 'pdelay'), ('prop_steps', 'psteps'), ('timeoffset', 'toff'))


def short(names, prefix):
    return [n.removeprefix(prefix) for n in names]


def compact_element(el):
    out = dict(f=short(el.get('flag_names', []), 'EEDF_'), i3d=el.get('i3dflags'), body=el.get('body_key'))
    keys = PROPAGATE_KEYS if 'EEDF_PROPAGATE' in el.get('flag_names', []) else ELEMENT_KEYS
    for src, dst in keys:
        if src in el:
            out[dst] = el[src]
    if any(el.get(k) for k in ('xoffset', 'yoffset', 'zoffset')):
        out['off'] = [el.get('xoffset'), el.get('yoffset'), el.get('zoffset')]
    if 'warning' in el:
        out['warning'] = el['warning']
    return {k: v for k, v in out.items() if v not in (None, 0, [])}     # absent = 0 / none on the page


def compact_effect(e):
    if e.get('missing'):
        return dict(missing=True)
    flags = e.get('effectflags')
    out = dict(flags=short(flags.split('|'), 'EDF_') if isinstance(flags, str) else [],
               elements=[compact_element(el) for el in e.get('elements', [])])
    if 'warning' in e:
        out['warning'] = e['warning']
    return out


def compact_weapon(w):
    return dict(id=w['weapon_id'], index=w['index'], body=w['body_key'], life=w['lifetime_ms'], speed=w['speed'],
                flags=w['flag_bits'], colour=w['light_colour'], size=w['size'], beam=w['beam_length'],
                flight=w['flight_effect'], impact=w['impact_effect'], muzzle=w['muzzle_effect'],
                death=w['death_effect'], trail=w['trail_particles'])


def tier_rows(death):
    """[(label, [[row, objects], ...] most objects first)] from the census death_rows."""
    out = []
    for label, sources in TIERS:
        counts = {}
        for src in sources:
            if src == 'TAsteroids':
                for a in death.get('TAsteroids', []):
                    counts[a['row']] = counts.get(a['row'], 0) + 1
                continue
            table = death.get(src) if src in ('TDocks', 'TFactories') else death['ship_classes'].get(src, {})
            for row, n in (table or {}).items():
                counts[int(row)] = counts.get(int(row), 0) + n
        out.append([label, sorted(([r, n] for r, n in counts.items() if isinstance(r, int)),
                                  key=lambda x: (-x[1], x[0]))])
    return out


def reachable(weapons, tiers, effects):
    """Effects row ids (str) the page can play: weapon columns, tier rows and PROPAGATE children, recursively."""
    todo = [r for w in weapons for r in (w['muzzle'], w['flight'], w['impact'], w['death'])
            if isinstance(r, int) and r > 0]
    todo += [r for _, rows in tiers for r, _ in rows]
    seen = set()
    while todo:
        k = str(todo.pop())
        if k in seen:
            continue
        seen.add(k)
        for el in effects.get(k, {}).get('elements', []):
            if isinstance(el.get('pid'), int) and el['pid'] > 0:
                todo.append(el['pid'])
    return sorted(seen, key=int)


CAT_DROP = ('textures', 'light_rgb')                 # the panel names the main texture and the hex colour
LASER_DROP = ('used_by', 'size', 'name_id')          # per laser; the weapon's union is shown


def cat_entry(e):
    """The catalogue entry the description panel reads, without the per-laser used-by the weapon's union repeats."""
    out = {k: v for k, v in e.items() if k not in CAT_DROP}
    out['lasers'] = [{k: v for k, v in l.items() if k not in LASER_DROP} for l in e.get('lasers', [])]
    return out


def view_record(view, census, manifest, catalog=None):
    effects = {k: compact_effect(e) for k, e in census['effects'].items()}
    for k, e in manifest.get('extra_effects', {}).items():
        effects.setdefault(k, dict(compact_effect(e), propagated_only=True))
    weapons = [compact_weapon(w) for w in census['weapons']]
    entries = {e['id']: e for e in (catalog or {}).get('weapons', [])}
    pins = hull_pins()
    for w in weapons:
        if w['id'] in entries:
            w['cat'] = cat_entry(entries[w['id']])
        w['hc'] = hull_class_of(entries.get(w['id'], {'id': w['id']}), pins)
    tiers = tier_rows(census['death_rows'])
    ids = reachable(weapons, tiers, effects)
    tex = manifest['textures']
    status = {}
    for r in tex.values():
        status[r['status']] = status.get(r['status'], 0) + 1
    bodies = manifest['bodies']
    return dict(label=VIEW_LABELS.get(view, view), tables={k: f'{v["source"]}:{v["member"]} {v["sha256"]}'
                                                          for k, v in census['tables'].items()
                                                          if k in ('TBullets', 'Effects')},
                weapons=weapons, effects={k: effects.get(k, dict(missing=True)) for k in ids}, tiers=tiers,
                default_death_row=census['death_rows'].get('default_row'),
                status=dict(texture_names=len(tex), texture_status=dict(sorted(status.items())),
                            placeholder_bound=sorted(n for n, r in tex.items()
                                                     if any(i.get('placeholder') for i in r['images'].values())),
                            bodies=len(bodies), body_errors=sorted(k for k, b in bodies.items() if b.get('error')),
                            truncated_bodies=sorted(k for k, b in bodies.items() if b.get('truncated')),
                            scene_bodies={src: sorted(k for k, b in bodies.items() if b.get('source') == src)
                                          for src in ('cut_fallback_inferred', 'scene')}))


def block(data):
    lines = [BEGIN, 'const LAB_DATA = {']
    for view, rec in data.items():
        lines.append(f'{json.dumps(view)}: {json.dumps(rec, separators=(",", ":"), sort_keys=True)},')
    lines.append('};')
    lines.append(END)
    return '\n'.join(lines)


def render(html, data, header_values, presets=PRESETS):
    problems = preset_problems(presets, data)
    if problems:
        raise SystemExit('presets: ' + '; '.join(problems))
    pstart = html.find(PRESETS_BEGIN)
    if pstart < 0:
        raise SystemExit(f'{HTML.relative_to(REPO)}: marked block {PRESETS_BEGIN} ... {END} not found')
    pstop = html.find(END, pstart + len(PRESETS_BEGIN))
    html = html[:pstart] + presets_block(presets) + html[pstop + len(END):]
    hstart = html.find(HEADER_BEGIN)
    if hstart < 0:
        raise SystemExit(f'{HTML.relative_to(REPO)}: marked block {HEADER_BEGIN} ... {HEADER_END} not found')
    hstop = html.find(HEADER_END, hstart + len(HEADER_BEGIN))
    html = html[:hstart] + header_block(header_values) + html[hstop + len(HEADER_END):]
    start = html.find(BEGIN)
    stop = html.find(END, start + len(BEGIN))
    if start < 0 or stop < start:
        raise SystemExit(f'{HTML.relative_to(REPO)}: marked block {BEGIN} ... {END} not found')
    return html[:start] + block(data) + html[stop + len(END):]


def load(views=VIEWS, census_dir=CENSUS, lab_dir=LAB, catalog_dir=CATALOG):
    data = {}
    for view in views:
        census = json.loads((census_dir / f'{view}.json').read_text())
        manifest = json.loads((lab_dir / view / 'manifest.json').read_text())
        cat_path = catalog_dir / f'{view}.json'
        catalog = json.loads(cat_path.read_text()) if cat_path.exists() else None
        data[view] = view_record(view, census, manifest, catalog)
    return data


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--check', action='store_true', help='fail when a page differs from the inputs')
    args = ap.parse_args(argv)
    data = load()
    problems = class_problems(PRESETS, data)
    if problems:
        raise SystemExit('presets: ' + '; '.join(problems))
    header_values = header_constants(HEADER.read_text())
    html = HTML.read_text()
    fresh = render(html, data, header_values)
    counts = ' '.join(f'{v}: weapons={len(r["weapons"])} catalogued={sum(1 for w in r["weapons"] if "cat" in w)} '
                      f'effects={len(r["effects"])} '
                      f'missing_rows={sum(1 for e in r["effects"].values() if e.get("missing"))} '
                      f'textures={r["status"]["texture_status"]} body_errors={len(r["status"]["body_errors"])};'
                      for v, r in data.items())
    stale = [p for p in (HTML, COPY) if not p.exists() or p.read_text() != fresh]
    if args.check:
        if stale:
            print('FAIL: stale ' + ', '.join(str(p.relative_to(REPO)) for p in stale)
                  + '; run python3 tools/effects/generate_weapon_effects_lab.py')
            return 1
        print(f'PASS: {HTML.relative_to(REPO)} and its results copy match the inputs ({len(fresh)} bytes). {counts}')
        return 0
    for p in stale:
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(fresh)
    print(f'{"updated " + ", ".join(str(p.relative_to(REPO)) for p in stale) if stale else "already current"} '
          f'({len(fresh)} bytes). {counts}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
