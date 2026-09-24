# Screen size of the terran_spp_panel features against the LOD metric s = r*640/D (lod-selection.md), r = 65554 raw
# units (record 0 radius; census radius 2169156 engine units, scale cancels). px per raw unit k = focal*s/(r*640) with
# focal = (rows/2)/tan(58.72 deg/2) (run315 fov row): 1280 px at 1440 rows, 960 px at 1080 rows. Inferred arithmetic.
import math
feat = {'slat pitch (pane 1222.6 / beam 1229)': 1222.6, 'beam width 151 (height 150)': 151, 'louvre gap at 45 deg incidence (346 sin - 105 cos)': 346*math.sin(math.radians(45))-105*math.cos(math.radians(45)),
        'louvre step in y 346': 346, 'rim width 5.5': 5.5, 'rim end 34': 34, 'beam texture period 1256': 1256, 'row pitch 9000': 9000}
for rows, focal in ((1440, 1280.0), (1080, 960.0)):
    print(f'--- {rows} rows, focal {focal:.0f} px')
    for name, u in feat.items():
        print(f'  {name:52s} ' + ' '.join(f's={s}:{u*focal*s/(65554*640):6.2f}px' for s in (73, 90, 104, 217, 250, 266)))
    print(f'  s below which the slat pitch is under 2 px: {2*65554*640/(1222.6*focal):.1f}; s below which the beam is under 1 px: {65554*640/(151*focal):.1f}')
