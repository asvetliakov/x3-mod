"""The bolt budget rows of weapon-effects summary.json records, and whether their bound sources are byte-identical.

python3 fence_rows.py <label>=<summary.json> ...  (the first record is the reference for the source comparison)
Prints per record: the commit, failed checks, the stage CPU row (build_us, draw_call_us), the fenced rows
(gpu_bolts_us, gpu_muzzles_us, gpu_total_us, no_rim, one_far_bolt, collapsed_512) and the GPU timestamps.
"""
import json
import sys

ref = None
for arg in sys.argv[1:]:
    label, path = arg.split('=', 1)
    d = json.load(open(path))
    m = d['metrics']
    sha = d['source']['sha256']
    if ref is None:
        ref = sha
    same = sum(1 for k in ref if sha.get(k) == ref[k])
    cpu = next(r for r in m['bolt_cpu'] if 'build_us' in r)
    rows = {r.get('case', 'budget'): r for r in m['bolt_timing']}
    b, ts = rows['budget'], rows['timestamps']
    print(f"{label}: commit={d['source']['commit'][:9]} checks={len(d['checks'])} failed={d['failed_checks']} "
          f"sources_identical={same}/{len(ref)}")
    print(f"  stage build_us={cpu['build_us']} draw_call_us={cpu['draw_call_us']}")
    print(f"  fenced total={b['gpu_total_us']} bolts={b['gpu_bolts_us']} muzzles={b['gpu_muzzles_us']} "
          f"no_rim={rows['no_rim']['gpu_us']} one_far={rows['one_far_bolt']['gpu_us']} "
          f"collapsed_512={rows['collapsed_512']['gpu_us']}")
    print(f"  timestamps frame={ts['frame_gpu_us']} bolts={ts['bolts_us']} muzzles={ts['muzzles_us']} rims={b['rims']}")
