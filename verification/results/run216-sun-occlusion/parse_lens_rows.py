"""Run216: parse sun_visibility and sun_lens_draw rows from x3m.log into a compact CSV-ish summary.
Usage: python3 parse_lens_rows.py /mnt/c/X3/x3m.log [out.json]"""
import re, sys, json, collections
log = sys.argv[1]
kv = re.compile(r'(\w+)=(\S+)')
vis, lens = {}, []
with open(log, 'r', errors='replace') as f:
    for line in f:
        if 'sun_visibility ' in line and 'sun_visibility device' in line:
            d = dict(kv.findall(line)); vis[int(d['frame'])] = d
        elif 'sun_lens_draw ' in line:
            d = dict(kv.findall(line))
            if d.get('rows_known') == '1': lens.append(d)
out = {'vis': vis, 'lens': lens}
json.dump(out, open(sys.argv[2] if len(sys.argv) > 2 else '/dev/stdout', 'w'))
