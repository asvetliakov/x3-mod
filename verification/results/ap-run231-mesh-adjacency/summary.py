"""Run 231 (AP, mesh_adjacency = verify): final metric row figures and the derived rates.
Usage: python3 summary.py /home/asvetl/x3m-logs/ap-run231-adjacency-verify.log"""
import re, sys
rows = [l for l in open(sys.argv[1], errors='replace') if l.startswith('mesh_adjacency_metric')]
f = dict(kv.split('=', 1) for kv in rows[-1].split()[1:])
n = lambda k: float(f[k])
print(f"calls={f['calls']} computed={f['computed']} fallbacks={f['fallbacks']} verify_meshes={f['verify_meshes']} "
      f"verify_equal={f['verify_equal']} verify_mismatched={f['verify_mismatched']} verify_entries={f['verify_entries']}")
print(f"verify_admitted={f['verify_admitted']} verify_refused_competing={f['verify_refused_competing']} "
      f"refused_share={n('verify_refused_competing')/n('verify_meshes'):.3f}")
print(f"module_us={n('fast_us'):.0f} mean={n('fast_us')/n('computed'):.1f} max={n('fast_max_us'):.1f}")
print(f"native_us={n('native_us'):.0f} mean={n('native_us')/n('calls'):.1f} max={n('native_max_us'):.1f} "
      f"ratio={n('native_us')/n('fast_us'):.2f}")
gate = sum(1 for l in open(sys.argv[1], errors='replace') if l.startswith('mesh_adjacency_gate'))
print(f"gate_rows={gate} mismatch_rows={sum(1 for l in open(sys.argv[1], errors='replace') if l.startswith('mesh_adjacency verify '))}")
