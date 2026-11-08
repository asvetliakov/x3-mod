import json, sys
CAT = '/home/asvetl/x3-mod/tools/effects/weapon_catalog/%s.json'
STOCK = ['IRE','PAC','HEPT','EBC','PBE','MASS','PRG','PD','PSG','PSP','PPC','FLAK_ALPHA','FLAK_CLUSTER','CIG','ESR','EMP','MAM','TERANTIFIGHTER','GAUSS','IPG','IBL']
MAYHEM = ['IRE','PAC','HEPT','EBC','PBE','MD','PRG','PBG','PSG','PSP','PPC','GKE','ISR','FAA','CFA','CIG','EMPC','SSC','MAML','PAA','GC','IBL','APC','PG','ID','GRC','FBL','AKE','BKE','RG','TBC','FBC','IC','PALC','IPG']
DIST = {'small': 600, 'medium': 1500, 'big': 2000}
plan = []
meta = {}
for view, ids in (('stock_ap', STOCK), ('mayhem3', MAYHEM)):
    cat = {w['id']: w for w in json.load(open(CAT % view))['weapons']}
    for short in ids:
        wid = 'SS_BULLET_' + short; w = cat[wid]
        spm = max([l.get('shots_per_min') or 0 for l in w['lasers']] or [0]); hz = round(spm / 60, 2) if spm else 4
        period = 1000 / hz; v = w['speed_mps']; D = DIST.get(w['size'], 600)
        tag = f"{'s' if view=='stock_ap' else 'm'}_{short}"
        meta[tag] = dict(view=view, id=wid, hz=hz, v=v, D=D, size=w['size'], style=w['style'], light=w['light'], flags=w['flags'], range=w['range_m'], dmg=[w['damage_shield'], w['damage_hull']], texture=w.get('texture'))
        base = dict(view=view, weapon=wid, dist=D, angle=90, seta=1, own=False, hull=0, hz=hz)
        def b(name, **kw):
            return  # muzzle-only plan
        def m(name, **kw):
            d = dict(base, family='muzzle', **kw); d['name'] = f'{tag}__{name}'; plan.append(d)
        b('b_side_a', scene='single', tfrac=0.5)
        b('b_side_b', scene='single', tfrac=0.36)
        b('b_hull', scene='single', hull=1, tfrac=0.5)
        b('b_seta6', scene='single', seta=6, tfrac=0.5)
        b('b_own_a', scene='own_fire', own=True, t=700)
        b('b_own_b', scene='own_fire', own=True, t=1310)
        b('b_in300', scene='incoming', t=300 / v * 1000)
        b('b_in60', scene='incoming', t=540 / v * 1000)
        b('b_stream', scene='stream', dist=1000, hull=1, t=500)
        m('m_chase_a', scene='own_chase', own=True, t=20)
        m('m_chase_b', scene='own_chase', own=True, t=period / 2 + 20)
        m('m_cockpit', scene='own_cockpit', own=True, t=20)
        m('m_range', scene='capital_600m', t=20)
        m('m_side', scene='own_side', own=True, t=20)
        m('m_seta6', scene='own_chase', own=True, seta=6, t=20)
json.dump(plan, open('plan.json', 'w'), indent=0)
json.dump(meta, open('meta.json', 'w'), indent=1)
print(len(plan), 'shots,', len(meta), 'weapons')
