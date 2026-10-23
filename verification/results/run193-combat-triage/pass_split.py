# Per-300-frame window split of view_submit (draw_trace run): frame_phases, frame_timing, pass/residual/light phases.
# residual = view_submit - sum - self (pass_phases). usage: python3 pass_split.py x3m.log
import re,sys
T={'frame_phases':['dt_p50_us','views_p50','view_setup_p50_us','view_submit_p50_us','pre_render_p50_us'],
   'frame_timing':['draws_p50','gap_draw_p50_us','gap_draw_per_draw_us','draw_p50_us','draw_native_p50_us','state_calls_p50'],
   'pass_phases':['passes_p50','apply_p50_us','draw_p50_us','end_p50_us','sum_p50_us','view_submit_p50_us','self_p50_us'],
   'residual_phases':['materials_p50','particle_views_p50','prepare_p50_us','setup_p50_us','particles_p50_us','other_p50_us','self_p50_us','prepare_skipped'],
   'light_phases':['traversal_calls_p50','traversal_p50_us','traversal_p95_us','cockpit_p50_us','unknown_p50_us','self_p50_us']}
W={}
for l in open(sys.argv[1],'rb'):
    s=l.decode('latin1'); k=s.split(' ',1)[0]
    if k in T:
        d=dict(re.findall(r'(\w+)=(\S+)',s)); f=int(d['frame'])
        W.setdefault(f,{})[k]={x:d.get(x,'?') for x in T[k]}
for f in sorted(W):
    w=W[f]; print('frame',f)
    for k in T:
        if k in w: print('  ',k,' '.join(f'{a}={b}' for a,b in w[k].items()))
    p=w.get('pass_phases'); ft=w.get('frame_timing')
    if p:
        vs,sm,se=int(p['view_submit_p50_us']),int(p['sum_p50_us']),int(p['self_p50_us'])
        ours=se+(int(ft['state_calls_p50'])*59/1000 if ft else 0)
        print(f'   residual={vs-sm-se} ours_self+state59={ours:.0f}')
