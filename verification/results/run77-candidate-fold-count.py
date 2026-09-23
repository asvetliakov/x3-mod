"""Count motion_output_taa_depth_fold lines per session log that initialised TAA (a motion_output_taa device= row)."""
import pathlib, re, sys, collections
root = pathlib.Path(sys.argv[1]); since = pathlib.Path(sys.argv[2]).stat().st_mtime
per = {}; taa_logs = 0; reasons = collections.Counter(); folded = collections.Counter()
for p in root.rglob('*.log'):
    if p.stat().st_mtime < since: continue
    t = p.read_text(errors='replace')
    fold = re.findall(r'motion_output_taa_depth_fold device=\d+ depth_fold=(\d) reason=(\S+)', t)
    taa = 'motion_output_taa device=' in t
    if not taa: continue
    taa_logs += 1
    per[str(p)] = len(fold)
    for f, r in fold: folded[f] += 1; reasons[r] += 1
dist = collections.Counter(per.values())
print(f'logs_with_taa={taa_logs} fold_lines_per_log={dict(sorted(dist.items()))} depth_fold={dict(folded)} reasons={dict(reasons)}')
for k, v in per.items():
    if v != 1: print('NOT_ONCE', v, k)
