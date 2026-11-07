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
sys.path.insert(0, str(REPO / 'tools/effects'))
import weapon_effects_table  # noqa: E402  (the mount tier rule, step 4)
MOUNT_TIERS = weapon_effects_table.MOUNT_TIERS
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
# HEADER.look and HEADER.hull; heat_ramp is a 5 x 3 array, hull's mode the enum value's name.
NAMESPACES = {
    'look': ('heat_ramp', 'min_frames', 'soft_core', 'soft_halo', 'core_e', 'body_max', 'halo_max', 'halo_fold',
             'streak_cap', 'streak_core_min', 'floor_core_px', 'floor_lit_px', 'floor_len_px', 'floor_flash_px',
             'budget_weight_patch', 'budget_weight_glow', 'fireball_octave_freq', 'fireball_octave_w', 'fireball_advect',
             'fireball_erode_0', 'fireball_erode_t', 'fireball_erode_r', 'fireball_erode_soft', 'fireball_erode_rt',
             'fireball_column_pow', 'fireball_lobes', 'fireball_lobe_amp', 'fireball_lobe_soft', 'fireball_heat_soft',
             'fireball_rim_t', 'fireball_octaves', 'fireball_fine_px', 'fireball_fine_band_px', 'fireball_fine4_px',
             'fireball_fine4_band_px', 'fireball_octave_freq4', 'fireball_octave_w4', 'budget_weight_fireball_fine4', 'budget_weight_fireball',
             'budget_weight_fireball_coarse', 'budget_weight_smoke'),
    # the hull-impact classes (docs/architecture/hull-impact-classes.md): one row per Anatomy of each class_* grid
    'hull': ('r_base', 'r_k', 'r_scale', 'flash_halo_t', 'spark_rest_w', 'bias', 'mode', 'spark_count_x',
             'spark_count_spread', 'spark_life_lo', 'spark_life_hi', 'spark_len_lo', 'spark_len_hi', 'spark_width_lo',
             'spark_width_hi', 'spark_curl_share', 'spark_curl_x', 'class_flash', 'class_puff', 'class_sparks',
             'class_glow', 'class_ember', 'class_ring', 'ring_t', 'ring_rise', 'ring_lead_t', 'ring_trail_t',
             'ring_trail_gain', 'ring_trail_span', 'ring_noise_freq', 'ring_noise_depth', 'ring_noise_contrast',
             'ring_wash_gain', 'ring_wash_fall', 'ring_stretch', 'ring_refract', 'ring_seed_axis', 'ring_seed_scale',
             'ring_noise_window', 'ring_front_window', 'ring_seed_element', 'budget_weight_shimmer', 'scorch', 'class_scorch', 'scorch_colour',
             'scorch_per_owner', 'class_shimmer', 'shimmer_ms', 'shimmer_max', 'streak_back', 'streak_front',
             'streak_width', 'streak_e', 'streak_fallback_halo', 'streak_ms', 'ion_channels_lo', 'ion_channels_hi',
             'ion_segments_lo', 'ion_segments_hi', 'ion_branches_lo', 'ion_branches_hi', 'ion_len', 'ion_len_spread',
             'ion_dir_jitter', 'ion_jag', 'ion_lateral', 'ion_branch_t_lo', 'ion_branch_t_hi', 'ion_branch_len',
             'ion_branch_e', 'ion_depth_r', 'ion_core_w', 'ion_core_px', 'ion_branch_px', 'ion_corona_x',
             'ion_corona_share', 'ion_core_white', 'ion_channel_e', 'ion_strike_decay', 'ion_after_e', 'ion_late_w',
             'ion_late_e', 'ion_life_ms', 'ion_period_lo_ms', 'ion_period_hi_ms', 'ion_strike_ms', 'ion_late_ms',
             'ion_strike_until', 'ion_fade_from', 'ion_glow_r', 'ion_glow_e', 'ion_glow_floor', 'flicker_ms', 'ion_colour', 'beam_colour', 'beam_glow_mix', 'beam_core_mix', 'beam_colour_sat_min', 'hold_ms',
             'spot_fall_ms', 'spatter_period_ms', 'spatter_cap', 'hold_r', 'spot_core', 'spot_halo', 'trail_step_r',
             'budget_weight_ring', 'budget_weight_filament', 'budget_weight_scorch', 'budget_weight_flash',
             # the burst look (docs/architecture/hull-burst-look.md): the star, the burst's glow, the cluster rule, the
             # smoke, the asteroid's dust and the cool spot
             'star_arm_len', 'star_width', 'star_width_px', 'star_floor_px', 'star_element', 'burst_rot_element',
             'burst_lobe_element', 'glow_r_x', 'glow_e_x', 'cluster_r_x', 'cluster_n0_glow', 'cluster_n0_burst',
             'class_smoke', 'smoke_t0', 'smoke_span', 'smoke_rise', 'dust_colour', 'cool_colour', 'ember_lobe_amp'),
    # step 4, the bolts and the muzzle (docs/architecture/bolt-muzzle-look.md section 8): HEADER.bolt, HEADER.muzzle
    'bolt': ('mode', 'd_eng_m', 'd_eng_range', 'w_px_target', 'w_k', 'f_ref', 'ref_rows', 'w_cap_m', 'heavy_k', 'rest_w',
             'sv_ref_mps', 'tail_tau_ms', 'tail_max_w', 'tail_root_w', 'tail_gain', 'core_r', 'core_r_heavy', 'halo_sigma_w',
             'e_body', 'e_halo', 'e_halo_heavy', 'h', 'head_gain', 'head_ball_w', 'tip_w', 'slug_tail_white', 'slug_tail_end',
             'streak_c', 'streak_c_simple', 'core_min', 'end_ms', 'orb_freq', 'orb_boil_hz', 'orb_lobes', 'orb_lobe_amp',
             'orb_lobe_turns', 'orb_stretch', 'orb_h_lo', 'orb_h_hi', 'wave_span_w', 'wave_sagitta', 'wave_sheet_e',
             'wave_ring_c', 'trim_rs', 'rim', 'rim_dark', 'rim_band_w', 'rim_min_px', 'halo_px_max', 'floor_core_px',
             'floor_body_px', 'floor_halo_px', 'floor_len_px', 'floor_endon_px', 'own_body_max_px', 'cockpit_native',
             'far_flag_k', 'list_capacity', 'beam_w_m'),
    # step 3, the death explosion (docs/architecture/explosion-look.md): HEADER.boom, the Boom tab's laws and defaults
    'boom': ('mode', 'tier_native', 'tier_f', 'tier_min_m', 'tier_max_m', 'tier_life_ms', 'tier_preroll_ms', 'tier_rise',
             'tier_r0_obj', 'tier_r0_f', 'tier_embers', 'tier_shock', 'tier_shock_tau', 'body_grow', 'body_hold', 'body_cool',
             'body_e', 'body_bias', 'body_r_sphere', 'body_floor_px', 'core_gain', 'core_x', 'core_r', 'core_soft',
             'star_arm', 'star_core', 'star_ms', 'star_t', 'star_cap_h', 'shock_r_x', 'shock_w', 'shock_e', 'shock_limb',
             'shock_end', 'missile_shock', 'ember_cones_lo', 'ember_cones_hi', 'ember_cone_deg', 'ember_v_x',
             'ember_v_min', 'ember_v_max', 'ember_p0', 'ember_speed_lo', 'ember_speed_hi', 'ember_life', 'ember_stagger',
             'ember_e', 'ember_tau', 'ember_w_px_lo', 'ember_w_px_hi', 'ember_w_px_floor', 'spark_halo_r', 'spark_halo_e', 'spark_cap_r', 'spark_gain', 'ember_len_max', 'ember_len_lo', 'ember_hide_r',
             'ember_hide_x0', 'ember_hide_x1', 'fragment_every', 'fragment_v', 'fragment_life', 'fragment_w_px',
             'fragment_dark', 'fragment_tip_t', 'fragment_dark_r', 'glow_r_x', 'glow_r_max', 'glow_e', 'halo_soft',
             'halo_fade_x0', 'halo_fade_x1',
             'cluster_r_x', 'cluster_n0', 'smoke_r', 'smoke_t0', 'smoke_span', 'smoke_s0', 'smoke_rise', 'coverage_dim',
             'budget_weight_shock', 'budget_weight_halo', 'max_bodies', 'max_quads', 'cone_element', 'ember_element',
             # the big tiers' pre-roll (sections 2.5, 2.6, 4): the secondaries and the dying hull's light
             'tier_swap', 'tier_secondaries', 'tier_cadence', 'secondary_jitter', 'secondary_r', 'secondary_r_last',
             'secondary_r_big', 'secondary_life', 'secondary_rise', 'secondary_inset', 'secondary_capsule_len',
             'secondary_capsule_r', 'secondary_cluster_n0', 'secondary_embers', 'max_secondaries', 'secondary_element',
             'hull_light_r', 'hull_light_e0', 'hull_light_e1', 'hull_light_t', 'body_r_sphere_big', 'sphere_px0',
             'sphere_px1', 'budget_screens', 'shock_tiers', 'halo_big_r', 'halo_big_e', 'halo_big_x0',
             'halo_big_x1',
             # section 13 (2026-11-07): the fire wave and the composed body
             'wave_tiers', 'wave_r_x', 'wave_tau_x', 'wave_life_x', 'wave_creep', 'wave_stretch', 'wave_w_x', 'wave_band_x',
             'wave_slab_x', 'wave_trail_span', 'wave_e', 'wave_t_edge', 'wave_t_band0', 'wave_t_band_end', 'wave_t_sheet_end',
             'wave_t_cool', 'wave_noise_cells', 'wave_noise_radial', 'wave_noise_r_ref', 'wave_noise_w', 'wave_advect', 'wave_erode_0',
             'wave_erode_t', 'wave_erode_r', 'wave_erode_rt', 'wave_erode_soft', 'wave_heat_soft', 'wave_lobe_amp',
             'wave_lobe_soft', 'wave_floor_a', 'wave_floor_t', 'wave_lead', 'wave_ahead', 'wave_break_floor',
             'wave_rim_noise', 'wave_eye_plane_x', 'wave_eye_plane_blend_x',
             'wave_eye_fade_x', 'wave_sheet_enabled', 'compose_enabled', 'compose_count_3', 'compose_count_2',
             'wave_window_in', 'wave_window_out', 'wave_light', 'wave_m_floor', 'wave_m_contrast', 'wave_n_cells',
             'wave_n_w', 'wave_refract_px', 'budget_weight_wave', 'budget_weight_wave_miss', 'wave_seed_element',
             'compose_n_lo', 'compose_n_hi', 'compose_jets_lo', 'compose_jets_hi', 'compose_n_min', 'compose_eye_n',
             'compose_max', 'compose_r_lo', 'compose_r_span', 'compose_offset', 'compose_axis_blend', 'compose_spread',
             'compose_seed_step', 'compose_jet_long', 'compose_jet_short', 'compose_jet_cone', 'compose_jet_birth0',
             'compose_jet_birth_span', 'compose_size', 'compose_box_margin', 'compose_small_px', 'compose_element',
             'compose_erode_x0', 'compose_erode_k', 'compose_erode_cap', 'compose_pace_lo', 'compose_pace_span', 'wave_m_bias',
             'compose_cost_base', 'compose_cost_per', 'compose_cost_shared_base', 'compose_cost_shared_per',
             'wave_sheet_x', 'wave_sheet_span', 'wave_tongue', 'wave_tongue_contrast',
             'compose_soft_add',
             'compose_core_x', 'compose_offset_min', 'compose_offset_cool',
             # section 14 (2026-11-07): the radiant core, the halo spike, the rays, the spark spray
             'core_ramp_ms', 'spike_rf', 'spike_px_lo', 'spike_px_hi', 'spike_halo', 'spike_e', 'spike_x', 'spike_t',
             'ray_n_lo', 'ray_n_hi', 'ray_jitter_lo', 'ray_jitter', 'ray_len_lo', 'ray_len_hi', 'ray_phi', 'ray_phi2',
             'ray_grow0', 'ray_grow_x', 'ray_start', 'ray_fwhm_px', 'ray_fwhm_len', 'ray_in', 'ray_quad_w', 'ray_e',
             'ray_s_lo', 'ray_s_span', 'ray_x', 'ray_pow', 'ray_flicker_lo', 'ray_flicker_hi', 'ray_flicker', 'ray_t0', 'ray_t1',
             'ray_soft', 'ray_frame_cap', 'ray_shed_max', 'ray_element', 'tier_sparks_lo', 'tier_sparks_hi',
             'spark_frame_cap', 'spark_shed_max', 'spark_r0_lo', 'spark_r0_span', 'spark_cone_share', 'spark_hide_soft',
             'spark_len_lo', 'spark_len_hi', 'spark_long_every', 'spark_long_lo', 'spark_long_hi', 'spark_long_w',
             'spark_long_e', 'spark_ref_px', 'spark_t0', 'spark_birth_x', 'spark_birth_now', 'spark_in_x', 'spark_in0',
             'spark_cool_pow'),
    'muzzle': ('mode', 'class_disc', 'disc_halo_w', 'disc_halo_e', 'class_flame', 'class_star', 'star_arms', 'star_width_w',
               'star_e', 'bloom_w', 'bloom_e', 'vent_w', 'vent_ms', 'vent_e', 'afterglow_ms', 'afterglow_e', 'rise_ms',
               'class_smoke', 'class_glow', 'class_life', 'cap_core_rs', 'cap_flame_rs', 'endon_cos', 'breath', 'cluster_n0',
               'record_capacity')}
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
FAMILIES = ('shield', 'hull', 'bolt', 'muzzle', 'boom')
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
    # the burst look's saturation case (docs/architecture/hull-burst-look.md 7: the dump's energy_fighter60_20, 20 energy
    # hits within 120 ms on a fighter at 167 hits per s, then none: the stage's case, review 2026-11-07)
    dict(id='hull/energy-fighter60-20hits', family='hull', view='mayhem3', weapon='SS_BULLET_IRE', tier=0, hull='flat_top',
         camera='side', seta=1, hps=167, own=False, cockpit=False, variant='none', k=0.33, shield=0, cls='auto', pulse=[0, 0],
         sweep=0, loop=0, count=20),
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
# count: the hits of a stream before it stops (0: open-ended); the presets without one take 0.
PRESETS = tuple(p if 'count' in p else dict(p, count=0) for p in PRESETS)

# Step 4 (docs/architecture/bolt-muzzle-look.md 6, weapon-look.md 3.3's list): the bolt and muzzle presets, the shared keys
# (tier, hull 'lit' or 'space', camera, SETA, hps: a muzzle's shots per game second, own, cockpit, k the calibre) and
# BOLT_KEYS: the scene (Bolt tab: single, own_fire, incoming, stream, barrage, far; Muzzle tab: own_chase, own_cockpit,
# capital_600m, broadside_12, turret, own_side), the distance and the travel's angle off the view ray, the emulated rows and FOV.
def _b(id, view, weapon, scene, dist_m=600, angle=90, camera='side', seta=1, hps=0, own=False, cockpit=False, k=0.33,
       hull='space', rows=1440, fov='vanilla'):
    family = id.split('/')[0]
    return dict(id=id, family=family, view=view, weapon=weapon, tier=0, hull=hull, camera=camera, seta=seta, hps=hps, own=own,
                cockpit=cockpit, variant='none', k=k, shield=0, cls='auto', pulse=[0, 0], sweep=0, loop=0, count=0, scene=scene,
                dist_m=dist_m, angle=angle, rows=rows, fov=fov)


PRESETS = PRESETS + (
    # the three scenarios that prove the look (section 6)
    _b('bolt/own-fire-chase', 'stock_ap', 'SS_BULLET_IRE', 'own_fire', camera='chase', own=True, k=0.0),
    _b('bolt/incoming-cockpit', 'mayhem3', 'SS_BULLET_PAC', 'incoming', camera='cockpit', cockpit=True, k=0.48),
    _b('bolt/stream-over-station-3km', 'mayhem3', 'SS_BULLET_PSG', 'stream', dist_m=3000, hull='lit', k=0.82),
    _b('bolt/stream-over-station-3km-seta6', 'mayhem3', 'SS_BULLET_PSG', 'stream', dist_m=3000, hull='lit', seta=6, k=0.82),
    # weapon-look 3.3's list
    _b('bolt/ire-chase-ahead-600m', 'mayhem3', 'SS_BULLET_IRE', 'single', dist_m=600, angle=20, camera='chase', k=0.33),
    _b('bolt/ire-mayhem-slug-vs-tracer', 'mayhem3', 'SS_BULLET_IRE', 'single', dist_m=600, k=0.33),
    _b('bolt/pac-cockpit-own', 'stock_ap', 'SS_BULLET_PAC', 'single', dist_m=60, angle=10, camera='cockpit', own=True,
       cockpit=True, k=0.29),
    _b('bolt/ppc-slug-capital-broadside', 'mayhem3', 'SS_BULLET_PPC', 'single', dist_m=2000, k=0.8),
    _b('bolt/psp-orb', 'stock_ap', 'SS_BULLET_PSP', 'single', dist_m=200, k=1.0),
    _b('bolt/psg-wave-200m', 'stock_ap', 'SS_BULLET_PSG', 'single', dist_m=200, k=0.41),
    _b('bolt/over-station-hull-20km', 'mayhem3', 'SS_BULLET_PSG', 'stream', dist_m=20000, hull='lit', k=0.82),
    _b('bolt/seta6-stream', 'mayhem3', 'SS_BULLET_PSG', 'stream', dist_m=1000, seta=6, k=0.82),
    # section 6's additions
    _b('bolt/far-10km-floor', 'mayhem3', 'SS_BULLET_IRE', 'far', dist_m=10000, k=0.33),
    _b('bolt/far-10km-floor-ppc-remap', 'mayhem3', 'SS_BULLET_PPC', 'far', dist_m=10000, k=0.8, fov='remap'),
    _b('bolt/convergent-barrage', 'mayhem3', 'SS_BULLET_PPC', 'barrage', dist_m=2000, k=0.8),
    _b('bolt/mayhem-gke-fighter-heavy', 'mayhem3', 'SS_BULLET_GKE', 'single', dist_m=600, k=1.0),
    _b('bolt/psg-wave-endon', 'stock_ap', 'SS_BULLET_PSG', 'single', dist_m=300, angle=0, k=0.41),
    # the muzzle: 3.3's list and section 6's additions
    _b('muzzle/ire-own-chase', 'stock_ap', 'SS_BULLET_IRE', 'own_chase', camera='chase', own=True, hps=4, k=0.0),
    _b('muzzle/pac-own-cockpit', 'stock_ap', 'SS_BULLET_PAC', 'own_cockpit', camera='cockpit', own=True, cockpit=True,
       hps=4, k=0.29),
    _b('muzzle/ppc-capital-broadside-600m', 'mayhem3', 'SS_BULLET_PPC', 'capital_600m', hps=0.33, k=0.8),
    _b('muzzle/flak-alpha-turret', 'stock_ap', 'SS_BULLET_FLAK_ALPHA', 'turret', hps=1, k=0.83),
    _b('muzzle/mayhem-white-light-texture-tint', 'mayhem3', 'SS_BULLET_MD', 'own_chase', camera='chase', own=True, hps=4,
       k=0.38),
    _b('muzzle/seta6-10hz', 'mayhem3', 'SS_BULLET_IRE', 'own_chase', camera='chase', own=True, seta=6, hps=10, k=0.33),
    _b('muzzle/md-ballistic-chase', 'mayhem3', 'SS_BULLET_MD', 'own_chase', camera='chase', own=True, hps=4, k=0.38),
    _b('muzzle/gke-heavy-chase', 'mayhem3', 'SS_BULLET_GKE', 'own_chase', camera='chase', own=True, hps=1, k=1.0),
    _b('muzzle/capital-broadside-12', 'mayhem3', 'SS_BULLET_PPC', 'broadside_12', hps=0.33, k=0.8),
    # the view rule (2026-11-07): the own guns across the view keep the cone (the chase view above draws the star)
    _b('muzzle/ire-own-side-cone', 'stock_ap', 'SS_BULLET_IRE', 'own_side', camera='side', own=True, hps=4, k=0.0),
)

# Step 3 (docs/architecture/explosion-look.md 8): the Boom tab's scenarios, the vanilla death row beside ours. BOOM_KEYS:
# the scene (side, chase or cockpit: the own death's views), the distance (m), the death tier, the dying object's
# radius (m), the deaths in one frame (60 m apart); `hull` lit puts a hull plane behind (the glow and smoke land on it).
def _x(id, boom_tier, dist_m, radius_m, nested=1, seta=1, camera='side', own=False, cockpit=False, hull='space', scene='side'):
    return dict(id=id, family='boom', view='mayhem3', weapon='SS_BULLET_IRE', tier=0, hull=hull, camera=camera, seta=seta,
                hps=0, own=own, cockpit=cockpit, variant='none', k=0.33, shield=0, cls='auto', pulse=[0, 0], sweep=0, loop=0,
                count=0, scene=scene, dist_m=dist_m, boom_tier=boom_tier, radius_m=radius_m, nested=nested)


PRESETS = PRESETS + (
    _x('boom/fighter-m5-600m-side', 'fighter', 600, 12),
    # section 14 (2026-11-07): the vanilla frames' size, ours beside the vanilla row (the radiant core, rays, sparks)
    _x('boom/fighter-m5-300m-vanilla-compare', 'fighter', 300, 12),
    _x('boom/fighter-m3-2km-floor', 'fighter', 2000, 19),
    _x('boom/corvette-m6-1500m', 'corvette', 1500, 87),
    _x('boom/freighter-ts-1500m', 'corvette', 1500, 50),
    _x('boom/capital-m2-1500m-preroll', 'capital', 1500, 1288, hull='lit'),
    # section 13: the fire wave face-on (broadside) and edge-on (from above: the bar)
    _x('boom/capital-m2-1500m-wave-broadside', 'capital', 1500, 1288),
    _x('boom/capital-m2-1500m-wave-above', 'capital', 1500, 1288, scene='above'),
    _x('boom/capital-m7-far-5km', 'capital', 5000, 618),
    _x('boom/capital-m2-close-720m', 'capital', 720, 1288),
    _x('boom/station-factory-3km-preroll', 'station', 3000, 1800, hull='lit'),
    _x('boom/missile-300m', 'missile', 300, 3),
    _x('boom/missile-near-own-ship-40m', 'missile', 40, 3, hull='lit'),
    _x('boom/nested-3-fighters-area', 'fighter', 600, 14, nested=3),
    _x('boom/own-death-chase', 'fighter', 24, 14, camera='chase', own=True, scene='chase'),
    _x('boom/own-death-cockpit', 'fighter', 4, 14, camera='cockpit', own=True, cockpit=True, scene='cockpit'),
    _x('boom/seta6', 'fighter', 600, 14, seta=6),
)
BOOM_KEYS = ('scene', 'dist_m', 'boom_tier', 'radius_m', 'nested')
BOOM_TIERS = ('fighter', 'corvette', 'capital', 'station', 'missile')
PRESET_KEYS = ('id', 'family', 'view', 'weapon', 'tier', 'hull', 'camera', 'seta', 'hps', 'own', 'cockpit', 'variant', 'k',
               'shield', 'cls', 'pulse', 'sweep', 'loop', 'count')
BOLT_KEYS = ('scene', 'dist_m', 'angle', 'rows', 'fov')
BOLT_SCENES = {'bolt': ('single', 'own_fire', 'incoming', 'stream', 'barrage', 'far'),
               'muzzle': ('own_chase', 'own_cockpit', 'capital_600m', 'broadside_12', 'turret', 'own_side')}
PRESET_CLASSES = ('auto', 'lineup') + CLASSES
CAMERAS = ('side', 'chase', 'cockpit', 'far1', 'far2', 'far8')
VARIANTS = ('none', 'repair', 'tug', 'missile', 'asteroid', 'flak')
PRESETS_BEGIN = '/* @weapon_effects_presets */'


def preset_problems(presets, data):
    """Why the presets cannot be injected: an id missing from its view, an unknown key or value, a family without one."""
    out = []
    for pr in presets:
        step4 = pr.get('family') in BOLT_SCENES
        boom = pr.get('family') == 'boom'
        if tuple(pr) != PRESET_KEYS + (BOLT_KEYS if step4 else BOOM_KEYS if boom else ()):
            out.append(f"{pr.get('id')}: keys {tuple(pr)}")
        if boom and (pr.get('boom_tier') not in BOOM_TIERS or pr.get('scene') not in ('side', 'chase', 'cockpit', 'above') or
                     pr.get('hull') not in ('lit', 'space') or not 1 <= pr.get('nested', 0) <= 8):
            out.append(f"{pr.get('id')}: boom tier / scene / hull / nested")
        if step4 and (pr.get('scene') not in BOLT_SCENES[pr['family']] or pr.get('rows') not in (1440, 1080) or
                      pr.get('fov') not in ('vanilla', 'remap') or pr.get('hull') not in ('lit', 'space')):
            out.append(f"{pr.get('id')}: scene {pr.get('scene')!r} / rows / fov / hull")
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
            # step 4: the mount tier (0 fighter, 1 corvette, 2 capital; weapon_effects_table.py's rule) and range (m)
            used = set()
            for laser in entries[w['id']].get('lasers', []):
                used |= set((laser.get('used_by') or {}).keys())
            w['mt'] = MOUNT_TIERS.index(weapon_effects_table.mount_tier_of(used))
            w['rg'] = entries[w['id']].get('range_m')
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
