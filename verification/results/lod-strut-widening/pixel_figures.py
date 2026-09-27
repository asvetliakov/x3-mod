"""Derived pixel figures for the note (arithmetic on the census outputs): k = F s / (r_raw 640) px per raw unit,
1 px in raw units, the far-band entry size s_far = r_engine 640 / (15.4 km x 505 u/m), and W_u for s_d = T_pad/2, /3."""
F = 1280.0
bodies = {  # name: (r_raw, r_engine, T_pad, stand s, [(material, width p10, width p50)])
 'argon_L_solarpowerplant (B)': (65648, 1569969, 212, 65, [(11, 96, 247), (17, 33.6, 445), (7, 51, 278)]),
 'argon_spacedock': (65576, 1954820, 278, 188, [(39, 38.7, 85.3), (17, 13.4, 37.3), (16, 25, 102.6)]),
 'argon_tech_L_shield_F (E)': (67118, 728276, 173, 95, [(15, 75.8, 298.3), (17, 88.6, 425.3)]),
 'military_outpost_middleb': (65792, 2255558, 150, 147, [(16, 97.1, 289.7), (39, 84.5, 272.1)]),
}
for name, (r_raw, r_eng, tpad, s_stand, mats) in bodies.items():
    k = lambda s: F * s / (r_raw * 640)
    print(f'{name}: T_pad {tpad} switch {r_eng*640/tpad/505/1000:.1f} km; stand s {s_stand} = {r_eng*640/s_stand/505/1000:.1f} km;'
          f' 1 px = {1/k(tpad):.0f} units at the switch, {1/k(s_stand):.0f} at the stand; s_far(15.4 km) = {r_eng*640/(15.4e3*505):.0f};'
          f' W_u(T_pad/2) = {1/k(tpad/2):.0f} units = {k(tpad)/k(tpad/2):.1f} px at the switch, {k(s_stand)/k(tpad/2):.2f} px at the stand;'
          f' W_u(T_pad/3): {k(s_stand)/k(tpad/3):.2f} px at the stand')
    for m, p10, p50 in mats:
        print(f'   mat {m}: width p10 {p10} / p50 {p50} units -> switch {p10*k(tpad):.2f} / {p50*k(tpad):.2f} px, stand {p10*k(s_stand):.2f} / {p50*k(s_stand):.2f} px, 1080 rows at the stand {p10*k(s_stand)*0.75:.2f} / {p50*k(s_stand)*0.75:.2f}')
