#!/usr/bin/env python3
"""The step-3 figures docs/verification/weapon-effects.md "Explosions (step 3)" quotes, from the two native records of
verification/results/native-windows/weapon-effects/ (run_weapon_effects.py --host native): the hook fixture's death-site
checks, the audit's death stubs and EXE windows, the forward state and the nested rows; the D3D fixture's death_* checks,
the EXPLOSION rows and the EXPLOSION_TIMING rows (EVENT-fenced best of 60 at 1080p and 5120x1440).
usage: python3 verification/results/explosion-look/ledger_rows.py [--markdown [--big]]
--markdown prints the ledger's gate table and budget line from the record (the ledger's numbers are pasted from it);
with --big the big tiers' table ("Explosions: the big tiers": the clone row R', the swap, the pre-roll gates)."""
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


if '--markdown' in sys.argv:
    big_markdown() if '--big' in sys.argv else markdown()
else:
    plain()
