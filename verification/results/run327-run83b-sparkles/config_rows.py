# Q1/Q5 config rows of a session log: selected proxy_options keys, the thin-region source, TAA mode/debug and readback
# counts (incl. the age readback), frame timing, sun occlusion rows. usage: config_rows.py LOG
import sys,re
keys=('X3M_GPU_SYNC_TIMING','X3M_ORIGINAL_FILL','X3M_ORIGINAL_FILL_DEFAULT','X3M_TAA_THIN_REGION_SOURCE','X3M_TAA_THIN_REGION_SOURCE_DEFAULT','X3M_TAA_DEBUG','X3M_SUN_OCCLUSION','X3M_SUN_OCCLUSION_CORE_F','X3M_SUN_OCCLUSION_LOG','X3M_TAA_BOX_RESOLUTION','X3M_TAA_FAR_STABILISER','X3M_TAA_THIN_REGION')
pick=(b'taa_thin_region_source ',b'motion_output_taa_box_resolution device=1 requested',b'motion_output_taa_history_taps',b'sun_occlusion',b'fov ',b'proxy_identity')
cnt={}
for l in open(sys.argv[1],'rb'):
    if l.startswith(b'proxy_options'):
        d=dict(kv.split('=',1) for kv in l.decode().split()[1:] if '=' in kv)
        print('proxy_options '+' '.join(f"{k}={d.get(k,'<absent>')}" for k in keys))
    elif l.startswith(b'motion_output_mode '):
        print('motion_output_mode '+' '.join(x for x in l.decode().split() if x.split('=')[0] in ('taa','taa_debug','jitter_samples','sentinel','sky_history')))
    elif l.startswith(b'motion_output_taa device'):
        print('motion_output_taa '+' '.join(x for x in l.decode().split() if x.split('=')[0] in ('history_weight','far_weight','far_f0','far_f1','far_speed_lo','far_speed_hi','thin_region','thin_relax','thin_gate','thin_emissive')))
    elif l.startswith(pick): print(l.decode(errors='replace').strip()[:260])
    for t in (b'motion_output_taa_readback',b'motion_output_taa_age_readback',b'hdr_readback',b'motion_output_present_readback',b'sun_lens_readback'):
        if l.startswith(t+b' '): cnt[t.decode()]=cnt.get(t.decode(),0)+1
print('readback rows',cnt)
