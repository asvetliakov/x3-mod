"""Batch lab shots for the bolt/muzzle visual re-review: one Chromium page, many (weapon, scene, t) frames.
Usage: python3 batch_shots.py <lab.html> <outdir> <plan.json>   (plan: list of shot dicts; see make_plan.py)
Writes <outdir>/<name>.png and <outdir>/shots.jsonl (caption, detail, look numbers per shot)."""
import sys, json, asyncio, time
from playwright.async_api import async_playwright
page_path, outdir, plan_path = sys.argv[1], sys.argv[2], sys.argv[3]
plan = json.load(open(plan_path))
SETUP = """(spec)=>{
  const $=id=>document.getElementById(id);
  if (state.view !== spec.view) { $('view').value = spec.view; state.view = spec.view; fillWeapons(); }
  setTab(spec.family);
  const set=(k,v)=>{const e=$(k); if(!e)return; if(e.type==='checkbox') e.checked=!!v; else e.value=v;};
  set(spec.family==='bolt'?'c_b_scene':'c_m_scene', spec.scene); set('c_b_rows',1440); set('c_b_fov','vanilla');
  set('c_b_dist',spec.dist); set('c_b_angle',spec.angle); set('c_b_seta',spec.seta); set('c_b_own',!!spec.own);
  set('c_b_hull',spec.hull||0); set('c_m_hz',spec.hz||4);
  const j=data().weapons.findIndex(w=>w.id===spec.weapon); if (j<0) return 'no weapon '+spec.weapon;
  state.weapon=j; $('weapon').value=j; state.scenario=''; state.playing=false;
  settle(state.view); return 'ok';
}"""
FRAME = """(t)=>{ state.t = t; state.playing=false; state.dirty=true; draw();
  const $=id=>document.getElementById(id); const w = weapon(); const l = boltLook(w); const law = boltLaw(l, l.v, false);
  const c = boltControls();
  return {caption: ($('caption')||{}).textContent||'', detail: ($('caption_detail')||{}).textContent||'',
          tread: ($('tread')||{}).textContent||'', loop: loopMs(),
          look: {k:l.k, style:l.style, tier:l.tier, range:l.range, v:l.v, heavy:l.heavy, w:l.w, tint:l.tint, mclass:l.mclass,
                 L:law.L, T:law.T, chord:law.chord, kind:law.kind, sigma_w:law.sigma_w, h:law.h, cs:law.cs},
          cat: {style: w.cat&&w.cat.style, spm: w.cat&&w.cat.shots_per_min, light: w.cat&&w.cat.light, flags: w.cat&&w.cat.flags, size: w.cat&&w.cat.size, hc: w.hc, colour: w.colour}}; }"""
async def main():
    async with async_playwright() as p:
        b = await p.chromium.launch()
        pg = await b.new_page(viewport={'width': 1400, 'height': 1100})
        errs = []
        pg.on('pageerror', lambda e: errs.append(str(e)))
        pg.on('console', lambda m: errs.append(m.text) if m.type == 'error' else None)
        await pg.goto('file://' + page_path)
        await pg.wait_for_function('window.labReady !== undefined')
        await pg.evaluate('window.labReady')
        out = open(f'{outdir}/shots.jsonl', 'a')
        last = None
        for s in plan:
            key = json.dumps({k: s[k] for k in s if k not in ('name', 't', 'tfrac')}, sort_keys=True)
            if key != last:
                r = await pg.evaluate(SETUP, s)
                if r != 'ok': print('SKIP', s['name'], r); continue
                await pg.evaluate('window.labReady')
                last = key
            t = s.get('t')
            if t is None:
                loop = await pg.evaluate('boltLoopMs(boltControls(), weapon())')
                t = s['tfrac'] * loop
            t0 = time.time()
            info = await pg.evaluate(FRAME, t)
            await pg.locator('canvas:visible').first.screenshot(path=f"{outdir}/{s['name']}.png")
            info.update(name=s['name'], t=t, spec=s, errors=errs[-2:] if errs else [])
            out.write(json.dumps(info) + '\n'); out.flush()
            print(f"{s['name']:48s} t={t:7.1f} {time.time()-t0:5.2f}s | {info['detail'][:110]}")
        await b.close()
asyncio.run(main())
