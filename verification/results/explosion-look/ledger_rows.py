#!/usr/bin/env python3
"""The step-3 figures docs/verification/weapon-effects.md "Explosions (step 3)" quotes, from the two native records of
verification/results/native-windows/weapon-effects/ (run_weapon_effects.py --host native): the hook fixture's death-site
checks, the audit's death stubs and EXE windows, the forward state and the nested rows; the D3D fixture's death_* checks,
the EXPLOSION rows and the EXPLOSION_TIMING rows (EVENT-fenced best of 60 at 1080p and 5120x1440).
usage: python3 verification/results/explosion-look/ledger_rows.py [--markdown [--big | --wave | --rays] | --fire]
--markdown prints the ledger's gate table and budget line from the record (the ledger's numbers are pasted from it);
with --big the big tiers' table ("Explosions: the big tiers": the clone row R', the swap, the pre-roll gates); with
--wave section 13's ("Explosions: the fire wave and the composed body"), the engine shimmer record's wave rows with it;
with --rays section 14's ("Explosions: radiance, rays and sparks"); --fire the fire material's gate table ("Fire material",
docs/architecture/fire-material.md section 6) with its slots and cost rows."""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1] / 'native-windows/weapon-effects'
hook = json.loads((HERE / 'hook-summary.json').read_text())
d3d = json.loads((HERE / 'summary.json').read_text())
rows = d3d['metrics']['explosion']
timing = {(r['width'], r['case']): r for r in d3d['metrics']['explosion_timing']}
checks = dict(d3d['checks'])


def row(case, **match):
    for r in rows:
        if r.get('case') == case and all(r.get(k) == v for k, v in match.items()):
            return r
    raise KeyError((case, match))


def plain():
    a = hook['build']['audit']
    deaths = [c for c in hook['checks'] if c['name'].startswith('death_')]
    print('hook', hook['passed'], hook['run']['result'], 'death_checks', len(deaths), 'failed',
          [c['name'] for c in deaths if not c['passed']], 'commit', hook['source']['commit'])
    print('audit stub_death_ok', a['stub_death_ok'], 'death_windows_exe', a['death_windows_exe'], 'calls',
          {k: v for k, v in a['handler_calls'].items() if 'death' in k}, 'x87', len(a['handler_x87']),
          'instructions', {k: v for k, v in a['handler_instructions'].items() if 'death' in k})
    print('death_forward_state', hook['death_forward_state'])
    print('death_nested', hook['death_nested'], 'death_state', hook['death_state'])
    dc = [c for c in d3d['checks'] if c[0].startswith('death_')]
    print('d3d', d3d['passed'], d3d['run']['result'], 'death_checks', len(dc), 'failed', [c[0] for c in dc if not c[1]],
          'commit', d3d['source']['commit'], 'adapter', d3d['host']['adapter']['description'])
    for r in rows:
        print('EXPLOSION', ' '.join(f'{k}={v}' for k, v in r.items()))
    for r in d3d['metrics']['explosion_timing']:
        print('TIMING', ' '.join(f'{k}={v}' for k, v in r.items()))


def markdown():
    ok = lambda name: 'PASS' if checks.get(name) else 'FAIL'
    s = {(r['tier'], r['age_frac']): r['outline_over_law'] for r in rows if r.get('case') == 'size'}
    fl = row('floor')
    b = row('body')
    core = row('core')
    pop = row('no_pop')
    st = [r for r in rows if r.get('case') == 'star']
    seta = [r for r in rows if r.get('case') == 'seta']
    sh = [r for r in rows if r.get('case') == 'shock']
    she = row('shock_end')
    shp = row('shock_production')
    em = row('embers')
    gl = row('glow')
    ha = [r for r in rows if r.get('case') == 'halo']
    he = row('halo_end')
    sm = row('smoke')
    co = row('cooling')
    ne = row('nested')
    eye = {(r['eye_m'], r['age_frac']): r for r in rows if r.get('case') == 'eye_inside'}
    lod = row('octave_lod')
    cpu = row('cpu')
    ref = row('refused')
    shr = row('shock_refused')
    att = row('attach')
    t5 = lambda c: timing[(5120, c)]['gpu_us']
    sw = timing[(5120, 'shock_weight')]
    out = [
        '| Gate | Result [M] | Bound |', '| --- | --- | --- |',
        f"| `death_tier_sizes` ({ok('death_tier_sizes')}; core off) | outline / R(t) at 0.1 / 0.3 L: fighter 600 m {s[('fighter_600m', 0.1)]:.3f} / {s[('fighter_600m', 0.3)]:.3f}, missile 300 m {s[('missile_300m', 0.1)]:.3f} / {s[('missile_300m', 0.3)]:.3f}; at 0.6 / 0.8 L {s[('fighter_600m', 0.6)]:.3f} / {s[('fighter_600m', 0.8)]:.3f} and {s[('missile_300m', 0.6)]:.3f} / {s[('missile_300m', 0.8)]:.3f} (recorded); the 3 km fighter at 1440 rows: law {fl['law_px']} px, drawn {fl['drawn_px']} | 5 % at 0.1 / 0.3 L (D1); floor 12 px |",
        f"| `death_body_contrast` / `_silhouette_lobed` / `_colour_core_rim` ({ok('death_body_contrast')} / {ok('death_body_silhouette_lobed')} / {ok('death_body_colour_core_rim')}; 16 seeds, core off) | dark {b['dark_share']:.3f}, high-pass {b['hp_std']:.3f}; max / min {b['lobe_max_over_min']:.2f}; core T {b['core_T']}, rim T {b['rim_T']} | 0.15, 0.10; 1.25; >= 0.75, <= 0.5 |",
        f"| `death_body_erodes_outside_in` ({ok('death_body_erodes_outside_in')}; 16 seeds, core off) | lit inside R(t) at 0.1 / 0.5 / 0.9 / 1.0 L: {b['lit_01']:.3f} / {b['lit_05']:.3f} / {b['lit_09']:.3f} / {b['lit_10']} | >= 0.8 / 0.35-0.75 / <= 0.20 (D1) / 0 |",
        f"| `death_body_white_hot_core` ({ok('death_body_white_hot_core')}; 8 seeds) | median radiance inside 0.3 R(t): {core['median_core_01']:.2f} at 0.1 L, {core['median_core_04']:.2f} at 0.4 L | >= 3; <= E_b once gone (D5) |",
        f"| `death_no_pop_at_birth` ({ok('death_no_pop_at_birth')}) | first body {pop['first_px']} px against R_0 {pop['r0_px']}; the star at 60 m {pop['star_half_px_60m']} px half against the {pop['cap_px']} px cap | |",
        f"| `death_flash_star_3_frames`, `death_seta6_three_frame_floor` ({ok('death_flash_star_3_frames')}, {ok('death_seta6_three_frame_floor')}) | lit frames {st[0]['lit_frames']} / {st[1]['lit_frames']} at 16 / 100 ms; moment ratio {st[0]['moment_ratio']}; off the arms {st[0]['off_arm']}; the body's last frame {seta[0]['body_last_age_ms']} / {seta[1]['body_last_age_ms']} ms of {seta[0]['life_ms']} | 3; >= 2; <= 0.02 |",
        f"| `death_shock_is_a_shell` ({ok('death_shock_is_a_shell')}; the missile's shell forced on: `missile_shock` {shp['missile_shock']} in production, `death_missile_shock_off` {ok('death_missile_shock_off')}) | farthest pixel {sh[0]['outline_max_px']} / {sh[1]['outline_max_px']} px against the law's {sh[0]['law_px']} / {sh[1]['law_px']} at 0.1 / 0.2 tau; interior {sh[0]['interior_over_limb']}; peak {sh[0]['shock_peak']} / {sh[1]['shock_peak']}; boxes after shock_end {she['boxes_after_end']}, with the eye inside {she['boxes_eye_inside']} | 5 %; <= 0.02; <= 0.7 E_b |",
        f"| `death_embers_random_and_cooling` ({ok('death_embers_random_and_cooling')}) | seed correlation {em['seed_correlation']}; T {em['T_03']} / {em['T_07']} / {em['T_11']} at 0.3 / 0.7 / 1.1 L; farthest {em['far_px']} px at 1.19 L against v_e 1.2 L = {em['reach_px']} (speeds 0.5-1.5 v_e) | < 0.5; >= 0.75 / >= 0.5 / <= 0.3; 0.9 x 0.5 .. 1.1 x 1.5 of the reach |",
        f"| `death_embers_hidden_in_body` ({ok('death_embers_hidden_in_body')}; D5) | lit share over the body's disc at 0.1 L {em['lit_over_body_01']}; {em['at_birth']} of {em['count']} drawn at birth | <= 0.15; at most half |",
        f"| `death_embers_lengths_and_widths` ({ok('death_embers_lengths_and_widths')}; D5) | {em['drawn_06']} drawn at 0.6 L, lengths {em['len_mean_px']} +- {em['len_std_px']} px (cap {em['cap_px']}), widths {em['width_px']} px | std >= 0.2 mean; 1.5-2.5 px |",
        f"| `death_glow_is_the_bodys_light` ({ok('death_glow_is_the_bodys_light')}; D3, a hull plane 20 m behind) | peak {gl['glow_peak_01']} / {gl['glow_peak_03']} at 0.1 / 0.3 L; 10 % equivalent radius {gl['glow_eq_r_px']} px against min(1.6 R(t), 2 R_f) = {gl['glow_R_px']}; over sky {gl['sky']}, after L {gl['after_L']} | <= 0.4 E_b; <= the law |",
        f"| `death_halo_in_air` ({ok('death_halo_in_air')}; D3) | farthest pixel over R(t) " + ' / '.join(f"{r['ratio']:.3f}" for r in ha) + ' at ' + ' / '.join(f"{r['age_frac']}" for r in ha) + ' L, peaks ' + ' / '.join(f"{r['peak']}" for r in ha) + f" (the erosion fade); {he['after_L']} after L, {he['eye_inside']} with the eye inside the body | <= 1.8 R(t) |",
        f"| `death_smoke_darkens_surfaces_only` ({ok('death_smoke_darkens_surfaces_only')}; 0.5 grey) | darkest {sm['darkest_peak']} at its peak (bound {sm['bound']}); {sm['darkest_end']} at 1.6 L, {sm['sky']} over sky | |",
        f"| `death_cools_through_ramp` ({ok('death_cools_through_ramp')}; 8 seeds), `death_fades_to_zero_at_life` ({ok('death_fades_to_zero_at_life')}) | T {co['T_02']} / {co['T_06']} / {co['T_10']} at 0.2 / 0.6 / 1.0 L; every part 0 at its life: {co['zero_at_life']} | >= 0.75 / >= 0.5 / <= 0.25 |",
        f"| `death_nested_three_drawn` ({ok('death_nested_three_drawn')}; three fighters 12 m apart) | 3 bodies, worst silhouette correlation {ne['worst_correlation']} (each alone); cluster N {ne['max_cluster']}; the three's peak {ne['peak_lit_01']} against one death's {ne['single_lit_01']} (x {ne['ratio']}); the embers' own peak {ne['peak_embers_01']} | < 0.8; N > 1; <= 1.4 x one |",
        f"| `death_eye_inside_bounded` ({ok('death_eye_inside_bounded')}; D2, own death in chase, the eye 24 m out) | 0.25 L: lit {eye[(24, 0.25)]['lit_share']}, std / mean {eye[(24, 0.25)]['std_over_mean']}; 0.6 L: dark {eye[(24, 0.6)]['dark_share']}; the 10 m eye {eye[(10, 0.25)]['std_over_mean']} / {eye[(10, 0.6)]['dark_share']} (recorded); cost {t5('eye_inside_fighter')} us | lit >= 0.9 and >= 0.05; dark >= 0.15; <= 300 us |",
        f"| `death_octave_lod_hysteresis` ({ok('death_octave_lod_hysteresis')}) | entered on frame {lod['entered_frame']}, held {lod['coarse_frames']} frames, left on frame {lod['left_frame']} | hold 3 |",
        f"| `death_stage_cpu_32_deaths` ({ok('death_stage_cpu_32_deaths')}) | {cpu['bodies']} bodies, {cpu['quads']} streaks, {cpu['glows']} glows: {cpu['median_us']} us median of 201 builds | <= 50 us |",
        f"| `death_budget_5120` ({ok('death_budget_5120')}) | fighter 600 m {t5('fighter_600m')}, 20 fighters {t5('fighters_20_600m')}, missile {t5('missile_300m')}, nested three {t5('nested_3_fighters')}, eye inside {t5('eye_inside_fighter')} us | the far cases <= 100 / 150, eye <= 300 |",
        f"| `death_programs_attached`, `death_programs_refused_keep_hull`, `death_shock_refused_deaths_drawn`, `death_reset_recovers` ({ok('death_programs_attached')}, {ok('death_programs_refused_keep_hull')}, {ok('death_shock_refused_deaths_drawn')}, {ok('death_reset_recovers')}) | shock {att['shock_ps_slots']} and halo {att['halo_ps_slots']} slots (`MaxPixelShader30InstructionSlots` {att['max_ps_slots']}); fault bit 8: the hull kept, boom off ({ref['reason']}), {ref['bodies_drawn']} body drawn and {ref['halos_drawn']} halo; fault bit 9: the deaths kept (shock {shr['shock']}, {shr['shock_reason']}), {shr['bodies_drawn']} bodies and {shr['halos_drawn']} halos drawn, {shr['shocks_drawn']} of {shr['shocks_built']} shells; the halo drawn after a Reset | |",
        '',
        f"Budget rows (5120x1440, EVENT-fenced best of 60, gpu_us / weight over 177.5 us) [M]: the fence alone {t5('fence_empty')}; body alone (eye inside, 3 octaves, the core) {t5('body_alone_fills_frame')} / {timing[(5120, 'body_alone_fills_frame')]['weight']}; smoke alone {t5('smoke_alone_fills_frame')} / {timing[(5120, 'smoke_alone_fills_frame')]['weight']}; halo alone (the eye in its sphere) {t5('halo_alone')} / {timing[(5120, 'halo_alone')]['weight']}; star {t5('star_alone')}; the missile's shell (forced on) over {sw['area_screens']} screens {sw['gpu_us']} us: {sw['per_screen_weight']} a screen with the fence removed (`budget_weight_shock` {sw['pinned']}).",
    ]
    print('\n'.join(out))


def big_markdown():
    """The big tiers (docs/verification/weapon-effects.md "Explosions: the big tiers"): the hook's clone row and swap rows,
    the D3D fixture's big-tier gates and the close capital's fenced frames."""
    ok = lambda name: 'PASS' if checks.get(name) else 'FAIL'
    hk = {c['name']: c['passed'] for c in hook['checks']}
    hok = lambda name: 'PASS' if hk.get(name) else 'FAIL'
    c, s, n, w = hook['death_clone'], hook['death_swap_state'], hook['death_nested_swap'], hook['death_swap']
    a = hook['build']['audit']
    sc, sl = hook['death_scan_timing'], hook['death_slots']
    sz = {(r['tier'], r['age_frac']): r['outline_over_law'] for r in rows if r.get('case') == 'size'}
    cl = row('clamp')
    pre = {r['box']: r for r in rows if r.get('case') == 'preroll'}
    hl = row('hull_light')
    pop = {r['tier']: r for r in rows if r.get('case') == 'no_pop'}
    sh = [r for r in rows if r.get('case') == 'shock' and r['tier'] == 'corvette_1500m']
    bl = row('big_light')
    so = row('shock_off')
    hb = row('halo_big')
    eye = {r['age_frac']: r for r in rows if r.get('case') == 'eye_inside' and r['tier'] == 'capital_720m'}
    seta = row('seta', tier='capital')
    cpu = row('cpu', big=6)
    close = [r for (wd, case), r in sorted(timing.items()) if wd == 5120 and case.startswith('capital_close_') and
             case != 'capital_close_all_shed']
    worst = max(close, key=lambda r: r['gpu_us'])
    allshed = timing[(5120, 'capital_close_all_shed')]
    out = [
        '| Gate | Result [M] | Bound |', '| --- | --- | --- |',
        f"| hook: `death_clone_row_built`, `death_clone_malloc_prologue_refused` ({hok('death_clone_row_built')}, {hok('death_clone_malloc_prologue_refused')}) | row 37 -> slot {c['slot']}: {c['count']} elements ({c['masked']} DISTANT copies, {c['kept']} empty nodes, {c['dropped']} PROPAGATE dropped), row flags {c['row_flags']}, FADEOUTBODY from {c['fade_start']} ms, the hold to {c['hold_end']} ms; row 40 -> {c['corvette_count']} elements (P 0); row 201 fade from {c['station_fade_start']} ms; {c['mallocs']} arrays from the EXE malloc stand-in; a changed prologue builds none | |",
        f"| hook: `death_swaps_death_column_big_tiers`, `death_swap_callee_state` ({hok('death_swaps_death_column_big_tiers')}, {hok('death_swap_callee_state')}) | the callee read R' in its column, the original back after; ESP {s['esp_callee']} against the frame's {s['esp_frame']} (32 below), return {s['return']} inside the stub {s['stub']}, ECX {s['ecx']} / EDX {s['edx']}, flags {s['eflags_callee']} | |",
        f"| hook: `death_nested_lifo_restore`, `death_nested_after_return_swap` ({hok('death_nested_lifo_restore')}, {hok('death_nested_after_return_swap')}) | {n['entries']} entries in one outer call: outer M2 {n['outer_m2']}, inner M6 {n['inner_m6']}, inner M2 {n['inner_m2']}; after: M2 {n['after_m2']}, M6 {n['after_m6']}, depth {n['depth']}, {n['restores']} restores | |",
        f"| hook: `death_clone_not_ready_forwards_exact`, `death_clone_rebuilt_after_reload`, `death_clone_slot_taken_refused`, `death_swap_leak_restored_at_present` ({hok('death_clone_not_ready_forwards_exact')}, {hok('death_clone_rebuilt_after_reload')}, {hok('death_clone_slot_taken_refused')}, {hok('death_swap_leak_restored_at_present')}) | totals: swapped {w['swapped']}, restores {w['restores']}, changed {w['changed']}, leaked {w['leaked']}, clone_missing {w['clone_missing']}, swap_full {w['swap_full']}, builds {w['builds']}, invalidated {w['invalidated']} | |",
        f"| hook: `death_clone_scan_refused_while_loading`, `death_clone_source_vanished_drops`, `death_clone_source_changed_new_slot`, `death_clone_free_slot_skips_referenced` ({hok('death_clone_scan_refused_while_loading')}, {hok('death_clone_source_vanished_drops')}, {hok('death_clone_source_changed_new_slot')}, {hok('death_clone_free_slot_skips_referenced')}) | a scan due while loading added nothing; a vanished source's pair dropped, its slot {sl['slot650']} left as it was; row 37 moved: slot {sl['old37']} abandoned, R' again in {sl['new37']} (rows 995 / 994 referenced, skipped); {sl['abandoned']} slots abandoned | |",
        f"| hook: `death_clone_scan_time` ({hok('death_clone_scan_time')}; the installed tables' {sc['records']} records, the fixture's engine memory) | {sc['pairs']} pairs, {sc['median_us']} us median of 15 scans, {sc['max_us']} us the first (cold) | <= {sc['bound_us']} us |",
        f"| audit | death stubs {a['stub_death_ok']}; calls {dict((k, v) for k, v in a['handler_calls'].items() if 'death' in k)}; x87 {len(a['handler_x87'])}; restore {a['handler_instructions'].get('_x3m_weapon_death_restore')} instructions | 0 calls, 0 x87 |",
        f"| `death_big_tier_sizes` ({ok('death_big_tier_sizes')}; core off) | outline / R(t) at 0.1 / 0.3 L after P: corvette 1.5 km {sz[('corvette_1500m', 0.1)]:.3f} / {sz[('corvette_1500m', 0.3)]:.3f}, capital 1.5 km {sz[('capital_1500m', 0.1)]:.3f} / {sz[('capital_1500m', 0.3)]:.3f}, station 3 km {sz[('station_3km', 0.1)]:.3f} / {sz[('station_3km', 0.3)]:.3f}; R_f M7 {cl['m7_R_f']} m, shipyard {cl['shipyard_R_f']} m | 5 % |",
        f"| `death_preroll_cadence_and_hull_light` ({ok('death_preroll_cadence_and_hull_light')}; capital 1.5 km, 4 ms steps) | capsule: {pre[0]['births']} births {pre[0]['first']}-{pre[0]['last']} ms, {pre[0]['inside']} inside; cached box: {pre[1]['births']}, {pre[1]['inside']} inside (bubble flags {pre[1]['bubble_flags']}); bodies before P {pre[0]['bodies_before_P']}; first body {pre[0]['first_body_R_over_R_f']} R_f; hull light {hl['peak_0']} / {hl['peak_P']} / {hl['peak_P500']} against {hl['want_0']} / {hl['want_P']} / {hl['want_P500']} at 0 / P - 16 / P + 500 ms, after the fade {hl['hull_lights_after_fade']} | gaps 20 % + 4 ms; 10 %; 0.5 R_f +- 3 % |",
        f"| `death_big_no_pop_at_birth` ({ok('death_big_no_pop_at_birth')}) | first body over R_0: corvette {pop['corvette_1500m']['first_over_r0']}, capital {pop['capital_1500m']['first_over_r0']}; rise <= 2x over P +- 32 ms: {pop['capital_1500m']['rise_ok']}; the capital's star {pop['capital_1500m']['star_half_px']} px half against {pop['capital_1500m']['cap_px']} | |",
        f"| `death_shock_off_all_tiers` ({ok('death_shock_off_all_tiers')}; D14) | `shock_tiers` {so['shock_tiers']}: {so['shocks']} shells over the three tiers at 0.02 / 0.1 / 0.2 L | 0 |",
        f"| `death_big_halo_capped` ({ok('death_big_halo_capped')}; D15, capital 1.5 km, the halo alone) | at 0.2 L: farthest pixel {hb['ratio']} R(t) ({hb['max_r_px']} px of {hb['R_t_px']}), peak {hb['peak']}; halos at 0.6 L {hb['halos_06']} | <= 1.25 R(t); <= 0.2 E_b; 0 |",
        f"| `death_big_shock_is_a_shell` ({ok('death_big_shock_is_a_shell')}; corvette, forced on: the law's evidence only) | farthest {sh[0]['outline_max_px']} / {sh[1]['outline_max_px']} px against {sh[0]['law_px']} / {sh[1]['law_px']} at 0.1 / 0.2 tau; interior {sh[0]['interior_over_limb']}; peak {sh[0]['shock_peak']} / {sh[1]['shock_peak']} | 5 %; <= 0.02; <= 0.7 E_b |",
        f"| `death_big_light_and_fade` ({ok('death_big_light_and_fade')}) | capital glow peak {bl['glow_peak_01']} / {bl['glow_peak_03']} at 0.1 / 0.3 L, over sky {bl['sky']}; elements left at P + 1.6 L: {bl['left_at_life']} | <= 0.4 E_b; 0 |",
        f"| `death_big_eye_inside_bounded` ({ok('death_big_eye_inside_bounded')}; the eye 720 m from a capital) | 0.25 L: lit {eye[0.25]['lit_share']}, std / mean {eye[0.25]['std_over_mean']}; 0.6 L: dark {eye[0.6]['dark_share']} | D2 |",
        f"| `death_big_seta6` ({ok('death_big_seta6')}; 100 ms steps) | {seta['secondaries_born']} secondaries born; body last at {seta['body_last_age_ms']} of {seta['life_ms']} ms; star {seta['star_frames']} frames | 8; within a step; 3 |",
        f"| `death_budget_capital_close_5120` ({ok('death_budget_capital_close_5120')}) | worst {worst['gpu_us']} us ({worst['case']}, {worst['screens']} screens, shed {worst['shed']}) over {len(close)} frames P - 100 .. P + 2000 ms; every step shed {allshed['gpu_us']} us | <= 450; <= 300 |",
        f"| `death_big_stage_cpu` ({ok('death_big_stage_cpu')}) | 4 capitals, 2 stations, 26 fighters: {cpu['bodies']} bodies, {cpu['secondaries']} secondaries, {cpu['quads']} quads: {cpu['median_us']} us median ({cpu['us_per_quad']} us a quad) | 0.08 us a quad, 100 us |",
    ]
    print('\n'.join(out))


def wave_markdown():
    """Section 13 (docs/verification/weapon-effects.md "Explosions: the fire wave and the composed body"): the D3D
    fixture's wave_* and compose_* gates, their rows and timing, and the engine shimmer fixture's band and strip rows."""
    ok = lambda name: 'PASS' if checks.get(name) else 'FAIL'
    at = row('attach')
    rad = sorted((r for r in rows if r.get('case') == 'wave_radius'), key=lambda r: r['tau_frac'])
    life, th, circ, col = row('wave_life'), row('wave_thickness'), row('wave_circumference'), row('wave_colour')
    sh, ep, rp = row('wave_sheet'), row('wave_eye_in_plane'), row('wave_ramp')
    f4 = next((r for r in d3d['metrics']['explosion_timing'] if r.get('case') == 'fine4_weight' and r['width'] == 5120), {})
    cpu = [r for r in rows if r.get('case') == 'cpu']
    st, sa, sw = row('wave_strips'), row('wave_strip_area'), row('wave_swept_hull')
    sil = {r['tier']: r for r in rows if r.get('case') == 'compose_silhouette'}
    tab = {r['tier']: r for r in rows if r.get('case') == 'compose_table'}
    opt = lambda case: next((r for r in rows if r.get('case') == case), {})
    eye, lod, box, cw = opt('compose_eye'), opt('compose_lod'), opt('compose_box'), row('close_capital_wave')
    he, fh = opt('wave_hard_edge'), opt('wave_fire_hue')  # v7 (explosion-look.md D52 on)
    shim = json.loads((HERE.parent / 'engine-shimmer/summary.json').read_text()) if (HERE.parent / 'engine-shimmer/summary.json').exists() else None
    t5 = lambda case: timing[(5120, case)]['gpu_us']
    ww = next(r for r in d3d['metrics']['explosion_timing'] if r.get('case') == 'wave_weight' and r['width'] == 5120)
    cf = {(r['n'], r['shared']): r['factor'] for r in d3d['metrics']['explosion_timing']
          if r.get('case') == 'compose_factor' and r['width'] == 5120}
    close = [r for (wd, case), r in sorted(timing.items()) if wd == 5120 and case.startswith('capital_close_') and
             case != 'capital_close_all_shed']
    worst = max(close, key=lambda r: r['gpu_us'])
    s = lambda t: sil[t]
    compose = bool(sil)  # 2026-11-07 v4: the compose gates skipped with boom::compose_enabled false
    out = [
        '| Gate | Result [M] | Bound |', '| --- | --- | --- |',
        *([] if compose else ["| `compose_*` | skipped: `boom::compose_enabled` false (the kill, 2026-11-07 v4) | |"]),
        f"| `wave_program_attached` ({ok('wave_program_attached')}) | wave_ps {at['wave_ps_slots']} slots, compose_ps {at.get('compose_ps_slots', '-')} slots, the device's {at['max_ps_slots']} | created |",
        f"| `wave_radius_law` ({ok('wave_radius_law')}; capital 5 km face-on) | the lit bins' median edge radius over r(t) at 0.1 / 0.5 / 1.0 tau: {' / '.join(str(r['ratio']) for r in rad)} (the median outer 10 % radius {' / '.join(str(r['outer_px']) for r in rad)} px against r(t) + 1.52 w_e {' / '.join(str(r['law_outer_px']) for r in rad)}: the tongues) | 5 % |",
        f"| `wave_sheet_thick`, `wave_hot_edge_trailing_sheet`, `wave_flame_tongues` ({ok('wave_sheet_thick')}, {ok('wave_hot_edge_trailing_sheet')}, {ok('wave_flame_tongues')}; 5 km, 0.5 tau) | band + sheet {sh['thick_rf']} R_f at 10 % of each ray's peak; edge over the sheet's mean 0.2-0.6 R_f behind {sh['edge_over_sheet']}, the sheet at 0.7 R_f behind {sh['sheet_07']} of the peak; the lit bins' outer radii spread {sh['tongue_depth_rf']} R_f | >= 0.6; >= 1.4, >= 0.01; >= 0.2 |",
        f"| `wave_never_brightest` ({ok('wave_never_brightest')}) | wave peak {' / '.join(str(r['peak']) for r in rad)} against the body's {' / '.join(str(r['body_peak']) for r in rad)}; after L_w {life['after_life']}, other tiers {life['other_tiers']} | <=; 0; 0 |",
        f"| `wave_thick_not_a_line` ({ok('wave_thick_not_a_line')}) | face-on band {th['band_px']} px (need {th['band_need_px']}); edge-on bar from above {th['bar_px']} px (need {th['bar_need_px']}) | >= 0.2 R_f f / z |",
        (f"| `wave_broken_circumference` ({ok('wave_broken_circumference')}) | the 64 bins' band means max / min {circ['arc_max_over_min']}, CV {circ['arc_cv']}; the dimmest bin's peak {circ['min_bin_peak']} of the ring's (lit share at 10 / 30 / 50 % {circ['lit_share']} / {circ['lit30']} / {circ['lit50']}) | >= 2, >= 0.25; >= 0.05 |"
         if 'arc_cv' in circ else
         f"| `wave_broken_circumference` ({ok('wave_broken_circumference')}) | lit share of 64 bins {circ['lit_share']}, max / min {circ['max_over_min']} | 0.40-0.85; >= 2 |"),
        *([f"| `wave_no_hard_edge` ({ok('wave_no_hard_edge')}; 5 km, 0.5 tau) | along the band at 36 angles: {he['band_drops']} drops past 50 % over 2 degrees, the worst 0.25-degree step {he['worst_step']} of the brighter end ({he['hard_drops']} hard); outward of {he['rays']} lit rays' peaks: {he['outer_rises']} rises, the worst 1-px fall {he['worst_outer_fall']} of the ray's peak | <= 0.35; >= 18, 0, <= 0.3 |",
           f"| `wave_fire_hue` ({ok('wave_fire_hue')}; 5 km, 0.5 tau, the dump's sRGB) | {fh['lit_px']:.0f} lit pixels: mean chroma {fh['mean_chroma']}, hue in 10-40 degrees {fh['hue_10_40_share']}, mean hue {fh['mean_hue']} | >= 0.5; >= 0.8 |"]
          if he and fh else []),
        f"| `wave_fire_not_grey` ({ok('wave_fire_not_grey')}) | edge / band {col['edge_over_band']}, band R / G {col['band_r_over_g']}, hue {col['hue_02']} / {col['hue_09']} at 0.2 / 0.9 L_w, chroma {col['chroma_02']} / {col['chroma_09']} | >= 1.3; >= 1.3; >= 0.5 / <= 0.35; >= 0.2 |",
        f"| `wave_refraction_strips_present_and_capped` ({ok('wave_refraction_strips_present_and_capped')}; capital 1.5 km) | at birth {st['early_strips']} strips, {st['early_screens']} screens, request {st['early_request']}; at P + 200 ms wave {st['late_waves']}, strips {st['late_strips']} (capped {st['late_capped']}, {st['late_screens']} screens) | 8; <= 0.25 |",
        f"| `wave_shimmer_strips_area` ({ok('wave_shimmer_strips_area')}) | at r 1,000 px: {sa['strips']} strips {sa['strip_screens']} screens against the box {sa['box_screens']}: {sa['ratio']} | <= 0.6 |",
        f"| `wave_lights_swept_hulls` ({ok('wave_lights_swept_hulls')}; a hull plane through the centre, 2 R_f out) | {sw['light_cross']} while the front crosses ({sw['t_cross_ms']} ms), {sw['light_late']} at 0.5 tau | >= 0.1; 0 |",
        *([f"| `compose_silhouette_cv` ({ok('compose_silhouette_cv')}; median of 8 seeds) | CV at 0.3 / 0.6 L: fighter {s('fighter_600m')['cv_03']} / {s('fighter_600m')['cv_06']}, capital {s('capital_1500m')['cv_03']} / {s('capital_1500m')['cv_06']}, station {s('station_3km')['cv_03']} / {s('station_3km')['cv_06']}; the single body {s('fighter_600m')['single_cv_03']} / {s('capital_1500m')['single_cv_03']} / {s('station_3km')['single_cv_03']} | >= 0.18 |"] if compose else []),
        *([f"| `compose_silhouette_changes` ({ok('compose_silhouette_changes')}) | correlation / RMS between the ages: fighter {s('fighter_600m')['corr']} / {s('fighter_600m')['rms']}, capital {s('capital_1500m')['corr']} / {s('capital_1500m')['rms']}, station {s('station_3km')['corr']} / {s('station_3km')['rms']} | <= 0.7 or >= 0.15 |"] if compose else []),
        *([f"| `compose_not_grapes`, `compose_under_one_sun` ({ok('compose_not_grapes')}, {ok('compose_under_one_sun')}) | largest blob {s('fighter_600m')['blob_share']} / {s('capital_1500m')['blob_share']} / {s('station_3km')['blob_share']}; peak over the single body {s('fighter_600m')['peak_over_single']} / {s('capital_1500m')['peak_over_single']} / {s('station_3km')['peak_over_single']}, mean lit {s('fighter_600m')['mean_over_single']} / {s('capital_1500m')['mean_over_single']} / {s('station_3km')['mean_over_single']} | >= 0.8; <= 1.4, 0.7-1.3 |"] if compose else []),
        *([f"| `compose_births_spread`, `compose_jets_capital` ({ok('compose_births_spread')}, {ok('compose_jets_capital')}) | N / jets: fighter {tab['fighter_600m']['n']} / {tab['fighter_600m']['jets']}, capital {tab['capital_1500m']['n']} / {tab['capital_1500m']['jets']}, station {tab['station_3km']['n']} / {tab['station_3km']['jets']}; born at 0: {tab['capital_1500m']['born_at_0']}, by 0.15 L all; at L {tab['capital_1500m']['gone_at_L']}; jets {tab['capital_1500m']['jet_ratio']}:1 | |"] if compose else []),
        *([f"| `compose_eye_inside_shared` ({ok('compose_eye_inside_shared')}) | own death (24 m): drawn {eye['own_drawn']}, shared {eye['own_shared']}; close capital at 0.25 L: drawn {eye['close_drawn']}, shared {eye['close_shared']} | <= 4, shared |"] if compose else []),
        *([f"| `compose_lod_floor` ({ok('compose_lod_floor')}) | a budget of nothing: {lod['drawn']} drawn, jets {lod['jets']}, shared {lod['shared']}, {lod['dropped']} dropped | 2, 0 |"] if compose else []),
        *([f"| `compose_box_holds_spheres` ({ok('compose_box_holds_spheres')}; 8 seeds x 3 tiers, a 1.6 x box) | lit pixels outside the box: {box['worst_clipped_px']} | 0 |"] if compose else []),
        *([f"| `compose_clumps_survive`, `compose_no_white_region` ({ok('compose_clumps_survive')}, {ok('compose_no_white_region')}) | blobs of >= 1 % of the lit pixels at 0.6 L: fighter {s('fighter_600m')['clumps_06']}, capital {s('capital_1500m')['clumps_06']}, station {s('station_3km')['clumps_06']}; widest white run at 0.3 / 0.6 L (core on) {s('fighter_600m')['white_rf']} / {s('capital_1500m')['white_rf']} / {s('station_3km')['white_rf']} R_f | >= 2; <= 0.3 |"] if compose else []),
        f"| `wave_fire_not_grey` ramp terms, `wave_eye_in_plane_no_whiteout` ({ok('wave_eye_in_plane_no_whiteout')}) | mean chroma (0.5 tau, 0.2 / 0.9 L_w) {rp['mean_chroma']}, on the ramp {rp['on_ramp']}; eye in the plane at 1.5 km: worst mean 1 - e^(-Y) {ep['worst_mean']} (P + {ep['at_ms']} ms), worst share above 0.9 {ep['worst_hot_share']} | >= 0.35, >= 0.9; <= 0.35, <= 0.4 |",
        *([f"| `compose_capital_structure` ({ok('compose_capital_structure')}; capital 1.5 km, 0.6 L) | blobs {s('capital_1500m')['clumps_06']}, lit share inside the outline {s('capital_1500m')['lit_in_06']}; the fourth octave {f4.get('four_us')} us against three {f4.get('three_us')} us a frame-filling body: {f4.get('weight4')} a screen (pinned {f4.get('pinned4')}) | >= 3; <= 0.75 |"] if compose else []),
        f"| `death_stage_cpu_32_deaths`, `death_big_stage_cpu` ({ok('death_stage_cpu_32_deaths')}, {ok('death_big_stage_cpu')}) | 32 fighters {cpu[0]['median_us']} us (single body {cpu[0].get('single_body_us')}); the big set {cpu[1]['median_us']} us, {cpu[1]['us_per_quad']} a quad (repeats {cpu[0].get('repeats_us')} / {cpu[1].get('repeats_us')}) (single body {cpu[1].get('single_body_us')}, no wave {cpu[1].get('no_wave_us')}) | 50; 0.08 |",
        f"| `death_budget_capital_close_5120` re-run ({ok('death_budget_capital_close_5120')}) | worst {worst['gpu_us']} us ({worst['case']}, shed {worst['shed']}); the wave drawn on {cw['wave_frames']} of the {cw['span']} frames P .. P + L_w it is on screen (`wave_drawn_close_capital` {ok('wave_drawn_close_capital')}) | <= 450; >= 80 % |",
        f"| timing (5120x1440) | wave alone {t5('wave_alone_fills_frame')} us ({ww['alone_screens']} screens drawn: {ww['alone_weight']:.3f} a screen), the sheet's octave shed {t5('wave_one_octave_alone')} us ({ww['one_octave_weight']:.3f}), the box at a station's birth {t5('wave_box_alone')} us ({ww['box_weight']:.3f}); compose factors N 2 / 3 / 4 / 5 {cf.get((2, 0))} / {cf.get((3, 0))} / {cf.get((4, 0))} / {cf.get((5, 0))}, shared N 4 {cf.get((4, 1))}; eye inside N 4 {t5('eye_inside_fighter_n4')} us, eye inside {t5('eye_inside_fighter')} us | pinned {ww['pinned']} / {ww['pinned_one']}; <= 300 |",
    ]
    if shim:
        rep = shim.get('report') or {}
        wv, wt = rep.get('wave') or [], rep.get('wave_timing') or []
        for r in wv:
            out.append(f"| engine shimmer `wave_band_displaced_outward_sky_included`, `wave_zero_outside_band_front_hull_and_strips` ({r['width']}x{r['height']}) | band {r['band']} px, {r['moved']} moved, {r['outward']} outward, mean {r['mean_px']} px; sky band {r['sky_band']}, {r['sky_moved']} moved; beyond {r['beyond_diff']} / front hull {r['front_diff']} / outside {r['outside_diff']} changed | >= 80 %, 95 %; 0 |")
        for r in wt:
            out.append(f"| engine shimmer `wave_shimmer_strips_timing` ({r['width']}x{r['height']}, r {r['r_px']} px) | strips {r['strips_gpu_ms']} ms against the box {r['box_gpu_ms']} ms: {r['ratio']} (copy {r['strips_copy_px']} / {r['box_copy_px']} px) | <= 0.6 |")
    print('\n'.join(out))


def rays_markdown():
    """Section 14 (docs/verification/weapon-effects.md "Explosions: radiance, rays and sparks"): the radiant core, the
    starburst, the spark spray, the meter and their timing rows."""
    ok = lambda name: 'PASS' if checks.get(name) else 'FAIL'
    core, sp, rs, ms = row('core_radiance'), row('sparks'), row('rays_summary'), row('meter_summary')
    rsoft = row('rays_soft')
    rays = [r for r in rows if r.get('case') == 'rays']
    meter = [r for r in rows if r.get('case') == 'meter']

    def meter_worst(d):
        """The worst |ev - base| per scene (black, dim, lit) of the death at d m."""
        out = [0.0, 0.0, 0.0]
        for r in meter:
            if r['tier'] != f'fighter_{d}m_5120':
                continue
            base = [float(x) for x in str(r['base']).split(',')]
            for k, key in enumerate(('ev_black', 'ev_dim', 'ev_lit')):
                out[k] = max(out[k], abs(r[key] - base[k]))
        return out
    emb, sb = row('embers'), row('sparks_birth')
    nopop = [r for r in rows if r.get('case') == 'no_pop' and 'worst_rise' in r]
    cpu = [r for r in rows if r.get('case') == 'cpu']
    t5 = lambda case: timing[(5120, case)]['gpu_us']
    fence = t5('fence_empty')
    shed = timing[(5120, 'spark_shed')]
    close = [r for (wd, case), r in sorted(timing.items()) if wd == 5120 and case.startswith('capital_close_') and
             case != 'capital_close_all_shed']
    worst = max(close, key=lambda r: r['gpu_us'])
    r300 = [r for r in rays if r['z_m'] == 300]
    r600 = [r for r in rays if r['z_m'] == 600]
    out = [
        '| Gate | Result [M] | Bound |', '| --- | --- | --- |',
        f"| `death_core_radiance` ({ok('death_core_radiance')}; fighter 600 m, 8 seeds) | median radiance inside 0.2 R(t) at 0.05 L {core['median_02R_005']} (seeds {core['min']}..{core['max']}); `death_body_white_hot_core` median at 0.1 L {row('core')['median_core_01']} | >= 6 |",
        f"| `death_rays_starburst` ({ok('death_rays_starburst')}; 5120x1440, 4 seeds) | angular peaks at 0.1 L, 300 m: at 0.5 R(t) {', '.join(str(r['peaks_05R']) for r in r300)} for {', '.join(str(r['drawn']) for r in r300)} rays drawn; at 1.5 R(t) {', '.join(str(r['peaks_01']) for r in r300)}; at 0.6 L {max(r['peaks_06'] for r in r300)}; 600 m (recorded) {', '.join(str(r['peaks_05R']) for r in r600)} / {', '.join(str(r['peaks_01']) for r in r600)} | drawn +- 1; >= 3; <= 2 |",
        f"| `death_rays_soft` ({ok('death_rays_soft')}; 300 m, 0.1 L) | median FWHM across the rays at 1.0 R(t) {rsoft['fwhm_10R_median_px1080']} px at 1080 rows ({rsoft['fwhm_count']} rays; per seed {', '.join(str(r['fwhm_10R_px1080']) for r in r300)}); the worst ray peak over the death under it at 0.5 R(t) {rsoft['worst_peak_over_under_05R']} (per seed {', '.join(str(r['peak_over_under_05R']) for r in r300)}) | >= 4; <= 0.4 |",
        f"| `death_rays_differ` ({ok('death_rays_differ')}) | two deaths' 360-bin profiles at 0.1 L correlate {rs['seed_correlation']} | < 0.3 |",
        f"| `death_sparks_dense` ({ok('death_sparks_dense')}; fighter 600 m, 0.3 L) | {sp['count']} sparks, {sp['drawn']} drawn, {sp['on_screen']} heads on screen, {sp['image_heads']} local maxima in the ember part; lengths {sp['len_mean_px']} +- {sp['len_std_px']} px (std / mean {sp['std_over_mean']}) | >= 100; >= 0.4 |",
        f"| `death_spark_heads` ({ok('death_spark_heads')}; fighter 600 m, 1080p, 0.3 L) | median head FWHM {sp['head_fwhm_median_px1080']} px at 1080 rows over {sp['head_fwhm_n']} image heads (the narrowest of the row, column and diagonal half widths) | >= 3 |",
        f"| `death_embers_hidden_in_body`, `death_embers_lengths_and_widths`, `death_embers_random_and_cooling` ({ok('death_embers_hidden_in_body')}, {ok('death_embers_lengths_and_widths')}, {ok('death_embers_random_and_cooling')}) | {sb['at_birth']} at birth, {sb['after_spread']} of {sb['count']} once the births are out (0.05 L); widths {emb['width_px']} px; seed correlation {emb['seed_correlation']}, T {emb['T_03']} / {emb['T_07']} / {emb['T_11']} at 0.3 / 0.7 / 1.1 L, farthest {emb['far_px']} px | > 0, >= 80 %; std >= 0.4 mean; < 0.5, >= 0.75 / >= 0.5 / <= 0.3 |",
        f"| `death_exposure_meter_shift` ({ok('death_exposure_meter_shift')}; the production meter's fresh target against the dim and lit scenes alone; fighter 300 m and 120 m, 5120x1440, Auto <= +1.3 EV, gamma 2.2) | worst shift over dim / lit 300 m {ms['worst_300m_ev']} EV, 120 m {ms['worst_120m_ev']} EV (black sky informational, not gated: an empty frame scores 0 EV and any signal lifts it to the cap; {ms['black_shift_ev']} EV); " + '; '.join(f"{d} m: black / dim 0.15 / lit 0.5 worst {' / '.join(f'{s:.3f}' for s in meter_worst(d))} EV (frame peaks {' / '.join(str(r['peak']) for r in meter if r['tier'] == f'fighter_{d}m_5120')})" for d in (300, 120)) + " | 300 m <= 0.5 EV; 120 m <= 1.3 EV (Auto's upper limit) |",
        f"| `death_no_pop_at_birth`, `death_big_no_pop_at_birth` ({ok('death_no_pop_at_birth')}, {ok('death_big_no_pop_at_birth')}) | worst frame-to-frame peak rise {', '.join(r['tier'] + ' ' + str(r['worst_rise']) for r in nopop)} | <= 2 |",
        f"| `death_rays_sparks_budget_5120` ({ok('death_rays_sparks_budget_5120')}) | fighter 600 m {t5('fighter_600m')} / {t5('fighter_600m_rays')} / {t5('fighter_600m_sparks')} us at 0.1 / 0.05 / 0.3 L; 20 fighters {t5('fighters_20_600m')} / {t5('fighters_20_600m_rays')} / {t5('fighters_20_600m_sparks')} us (spark halvings {shed['twenty_halvings']}: {shed['twenty_sparks']} drawn, {shed['twenty_shed']} shed, {shed['twenty_rays']} rays); fence {fence} us | <= 60; <= 150 |",
        f"| `death_budget_capital_close_5120` ({ok('death_budget_capital_close_5120')}) | worst {worst['gpu_us']} us ({worst['case']}, shed {worst['shed']}) | <= 450 |",
        f"| rays alone (5120x1440) | fighter 300 m {t5('rays_alone_fighter_300m')} us ({timing[(5120, 'rays_alone_fighter_300m')]['quads']} quads), close capital {t5('rays_alone_capital_close_720m')} us ({timing[(5120, 'rays_alone_capital_close_720m')]['quads']} quads), the fence {fence} us | |",
        f"| `death_stage_cpu_32_deaths`, `death_big_stage_cpu` ({ok('death_stage_cpu_32_deaths')}, {ok('death_big_stage_cpu')}) | 32 fighters {cpu[0]['median_us']} us, {cpu[0]['quads']} quads (repeats {cpu[0]['repeats_us']}; the parent `d3e7698bf` 42.1 us, 588 quads); the big set {cpu[1]['median_us']} us, {cpu[1]['us_per_quad']} a quad | 50; 0.08 |",
    ]
    print('\n'.join(out))


def fire_markdown():
    """docs/verification/weapon-effects.md "Fire material" (docs/architecture/fire-material.md section 6): the fire
    gates on the hull burst (CLASS rows), the death bodies and the wave (EXPLOSION rows), the slots and the cost rows."""
    ok = lambda name: 'PASS' if checks.get(name) else 'FAIL'
    crow = lambda case, **m: next(r for r in d3d['metrics']['class'] if r.get('case') == case and
                                  all(r.get(k) == v for k, v in m.items()))
    h, sm, sun = crow('fire_hull_median'), crow('burst_small_median'), crow('twenty_hits_cores_on')
    f, c = row('fire_body_median', tier='fighter_200m'), row('fire_body_median', tier='capital_3km')
    w = row('wave_not_a_tyre')
    ht = {(r['width'], r['case']): r for r in d3d['metrics']['hull_timing'] if 'gpu_us' in r}
    t5 = lambda case: timing[(5120, case)]['gpu_us']
    close = [r for (wd, case), r in sorted(timing.items()) if wd == 5120 and case.startswith('capital_close_') and
             case != 'capital_close_all_shed']
    worst = max(close, key=lambda r: r['gpu_us'])
    on, off = ht[(5120, 'burst_alone_fire_tongues')]['gpu_us'], ht[(5120, 'burst_alone_fire_tongues_off')]['gpu_us']
    hseam = sorted(r['seam'] for r in d3d['metrics']['class'] if r.get('case') == 'fire_hull')
    hseam_med = 0.5 * (hseam[len(hseam) // 2 - 1] + hseam[len(hseam) // 2]) if len(hseam) % 2 == 0 else hseam[len(hseam) // 2]
    att = row('attach')
    hs, cs_ = d3d['metrics']['hull_slots'][0], d3d['metrics']['class_slots'][0]
    h2 = crow('fire_hull_round2')
    f2, c2 = row('fire_body_round2', tier='fighter_200m'), row('fire_body_round2', tier='capital_3km')
    er, pg = crow('burst_erosion_median'), crow('plasma_globe')
    rings = [r for r in d3d['metrics']['class'] if r.get('case') == 'burst_centre_ring_median']
    fr = {(r['tier'], r['age_frac']): r.get('front_over_law') for r in rows if r.get('case') == 'size'}
    so = {(r['tier'], r['age_frac']): r.get('outline_over_law') for r in rows if r.get('case') == 'size'}
    wc_ = row('wave_circumference')
    bc = next(r for r in d3d['metrics']['hull'] if r.get('case') == 'burst_colour')
    b2, bb, co = row('body_round2'), row('body', tier='fighter_600m'), row('cooling', tier='fighter_600m')
    he_, wcol, wr = row('wave_hard_edge'), row('wave_colour'), row('wave_ramp')
    hb = lambda k: f"{h[k]}"
    bd = lambda k: f"{f[k]} / {c[k]}"
    out = [
        '| Gate | Result [M] (hull; fighter / capital) | Bound |', '| --- | --- | --- |',
        f"| `fire_luminance_thin_hull`, `_death` ({ok('fire_luminance_thin_hull')}, {ok('fire_luminance_thin_death')}) | share > 0.8 peak {hb('thin')}; {bd('thin')}; equal-area disc under 0.1 peak {h2['disc_dark']}; {f2['disc_dark']} / {c2['disc_dark']} (the outline's gaps under 0.25 peak {hb('gaps')}; {bd('gaps')}, printed) | 0.10-0.35; >= 0.20 (vanilla 0.096) |",
        f"| `fire_not_pink_hull`, `_death` ({ok('fire_not_pink_hull')}, {ok('fire_not_pink_death')}) | AgX pink share at 0.2, 0.3, 0.5 L {h2['pink']}; {f2['pink']} / {c2['pink']} | the medians' largest < 0.02 |",
        f"| `fire_quad_edge_dark_hull`, `_death` ({ok('fire_quad_edge_dark_hull')}, {ok('fire_quad_edge_dark_death')}) | the quad's boundary over the peak, worst hit {h2['quad_edge_worst']}; {f2['quad_edge_worst']} / {c2['quad_edge_worst']} | < 0.02 |",
        f"| `fire_tongues_radial_hull`, `_death` ({ok('fire_tongues_radial_hull')}, {ok('fire_tongues_radial_death')}) | radial / tangential {hb('aniso')}; {bd('aniso')} | <= 0.75 |",
        f"| `fire_edge_licks_hull`, `_death` ({ok('fire_edge_licks_hull')}, {ok('fire_edge_licks_death')}) | lit boundary / outline boundary {hb('licks')}; {bd('licks')} | >= 2.0 |",
        f"| `fire_temporal_hull` ({ok('fire_temporal_hull')}) | high-pass corr +16 ms {h['hp16']}, +100 ms {h['hp100']}; low-pass +16 ms {h['lp16']} | 0.5-0.9; <= 0.6; >= 0.9 |",
        f"| `fire_colour_ramp_hull`, `_death` ({ok('fire_colour_ramp_hull')}, {ok('fire_colour_ramp_death')}) | hot {hb('hot')} @ {hb('hot_r')}, red {hb('cold')} @ {hb('cold_r')}, mid {hb('mid')}; hot {bd('hot')} @ {bd('hot_r')}, red {bd('cold')} @ {bd('cold_r')}, mid {bd('mid')} | >= 0.25 @ <= 0.5; 0.05-0.25 @ >= 0.65; <= 0.5 |",
        f"| `fire_core_bloom_hull`, `_death` ({ok('fire_core_bloom_hull')}, {ok('fire_core_bloom_death')}) | peak inside 0.3 R {hb('core40')} (40 ms), {bd('core10')} (0.1 L); outside 0.5 R {hb('out40')}; {bd('out10')} | >= 3.0; <= 1.5 |",
        f"| `hull_20_hits_no_white_sun` ({ok('hull_20_hits_no_white_sun')}) | cores on: worst share above 3.0 {sun['worst_share_above_3']} | <= 0.01 (cores off <= 3.0 as before) |",
        f"| `fire_small_reads` ({ok('fire_small_reads')}) | 30 px far hit at 0.4 L_b: holes {sm['dark_share']}, lobes {sm['lobe_max_over_min']}, equal-area radius / R(t) {sm.get('eq_r')} (lit share inside R(t) {sm['lit_share']}, printed) | >= 0.10; >= 1.2; >= 0.45 |",
        f"| `wave_not_a_tyre` ({ok('wave_not_a_tyre')}; 5 km face-on, 0.5 tau) | band mean / edge {w['mean_over_edge']}, dark share {w['dark_share']}, anisotropy {w['aniso']}, grey {w['grey_share']}, longest rim arc >= 0.5 peak {w.get('rim_arc_deg')} deg | <= 0.35; >= 0.40; <= 0.75; <= 0.10; <= 60 |",
        f"| `hull_burst_erodes_outside_in` ({ok('hull_burst_erodes_outside_in')}) | equal-area radius / R(t) {er.get('eq_r')}, over its 0.1 L_b value {er.get('eq_ratio')}; mean radius 0.8 / 0.2 {er['mean_r_ratio']}; after L_b {er['bursts_after']} (lit share {er['lit_share']}, printed) | 0.35-0.9, <= 0.4; <= 0.8; 0 |",
        f"| `hull_burst_no_centre_ring` ({ok('hull_burst_no_centre_ring')}; over R_eq) | worst prominence {', '.join(str(r['prominence_max']) for r in rings)}; annulus ratio {', '.join(str(r['annulus_ratio']) for r in rings)} (0.5 / 0.6 / 0.7 L_b) | <= 0.15; <= 1.3 |",
        f"| `death_tier_sizes`, `death_big_tier_sizes` ({ok('death_tier_sizes')}, {ok('death_big_tier_sizes')}) | the tongues' front / R(t) at 0.1 / 0.3 L: {'; '.join(f'{t} {fr[(t, 0.1)]} / {fr[(t, 0.3)]}' for t in ('fighter_600m', 'missile_300m', 'corvette_1500m', 'capital_1500m', 'station_3km'))} (mean outline {'; '.join(f'{t} {so[(t, 0.3)]}' for t in ('fighter_600m', 'capital_1500m'))} at 0.3 L) | 0.80-1.15 |",
        f"| `wave_broken_circumference` ({ok('wave_broken_circumference')}) | longest run of bins under 5 % {wc_['dark_run_bins']} (min bin {wc_['min_bin_peak']}), arcs max / min {wc_['arc_max_over_min']}, CV {wc_['arc_cv']} | <= 3; >= 2.0; >= 0.25 |",
        f"| `hull_burst_colour_white_core_at_birth`, `_dark_red_at_end` ({ok('hull_burst_colour_white_core_at_birth')}, {ok('hull_burst_colour_dark_red_at_end')}) | birth g / r {bc['birth_g_over_r']}, b / r {bc['birth_b_over_r']}; end g / r {bc['end_g_over_r']}, b / r {bc['end_b_over_r']} | >= 0.75, >= 0.25; <= 0.35, <= 0.2 |",
        f"| `death_body_erodes_outside_in`, `_colour_core_rim`, `death_cools_through_ramp` ({ok('death_body_erodes_outside_in')}, {ok('death_body_colour_core_rim')}, {ok('death_cools_through_ramp')}; fighter 600 m) | equal-area radius {b2['eq_r']}, ratio {b2['eq_ratio']}; core T {bb['core_T']}, rim T {bb['rim_T']}; T at 0.2 / 0.6 / 1.0 L {co['T_02']} / {co['T_06']} / {co['T_10']} | 0.35-0.9, <= 0.45; >= 0.5 and > rim, <= 0.5; >= 0.75, >= 0.25 and <= T(0.2), <= 0.25 |",
        f"| `wave_no_hard_edge`, `wave_fire_not_grey` ({ok('wave_no_hard_edge')}, {ok('wave_fire_not_grey')}) | hard drops {he_['hard_drops']}, rays {he_['rays']}, rises {he_['outer_rises']}, worst fall {he_['worst_outer_fall']}; hue at 0.2 / 0.9 L_w {wcol['hue_02']} / {wcol['hue_09']}, on the fire ramp {wr['on_ramp']} | 0; >= 18; <= rays / 10; <= 0.3; >= 0.5 / <= 0.35; >= 0.9 |",
        f"| `hull_plasma_globe_shrinks_reddens` ({ok('hull_plasma_globe_shrinks_reddens')}) | R {pg['R']} px; b / r {pg['b_over_r']} at 0.3 / 0.65 / 0.95 L | shrinks, reddens, lit at 0.95 L |",
        f"| `fire_no_seam_hull`, `_death`, `_wave` ({ok('fire_no_seam_hull')}, {ok('fire_no_seam_death')}, {ok('fire_no_seam_wave')}) | median over the seeds {hseam_med:.4f}; {bd('seam')}; {w['seam']} (worst seed {h['seam_max']}; {bd('seam_max')}) | <= 0.1 (median) |",
        f"| program slots (device creation, max {att['max_ps_slots']}) | fireball_ps {hs['fireball_ps']}, wave_ps {att['wave_ps_slots']}, ring_ps {cs_['ring_ps']}; fireball vertex {hs['fireball_vertex_bytes']} bytes | the modern cap 32768 |",
        f"| `hull_fire_within_weight` ({ok('hull_fire_within_weight')}) | tongues on {on} us ({on / 177.5:.3f}), off {off} us ({off / 177.5:.3f}) at 5120x1440: + {(on - off) / 177.5:.3f} a screen | within 0.1 of budget_weight_fire - budget_weight_fireball |",
        f"| alone rows (5120x1440) | body {t5('body_alone_fills_frame')} us, wave {t5('wave_alone_fills_frame')} us (one octave {t5('wave_one_octave_alone')}), eye inside a fighter {t5('eye_inside_fighter')} us | eye inside <= 300 |",
        f"| `death_budget_capital_close_5120` ({ok('death_budget_capital_close_5120')}) | worst {worst['gpu_us']} us ({worst['case']}, shed {worst['shed']}) | <= 450 |",
        f"| `death_budget_5120`, `death_rays_sparks_budget_5120` ({ok('death_budget_5120')}, {ok('death_rays_sparks_budget_5120')}) | fighter 600 m {t5('fighter_600m')} us, 20 fighters {t5('fighters_20_600m')} us | <= 60; <= 150 |",
        f"| `hull_budget_20_hits_5120`, `hull_budget_capital_close_64_within_budget` ({ok('hull_budget_20_hits_5120')}, {ok('hull_budget_capital_close_64_within_budget')}) | 20 hits {ht[(5120, 'fighter_200m_20_hits')]['gpu_us']} us, 60 m {ht[(5120, 'fighter_60m_20_hits')]['gpu_us']} us, capital 64 {ht[(5120, 'capital_close_64_hits')]['gpu_us']} us | <= 80; <= 1420 |",
    ]
    print('\n'.join(out))


if '--fire' in sys.argv:
    fire_markdown()
elif '--markdown' in sys.argv:
    (rays_markdown() if '--rays' in sys.argv else wave_markdown() if '--wave' in sys.argv else
     big_markdown() if '--big' in sys.argv else markdown())
else:
    plain()
