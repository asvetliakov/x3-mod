# usage: python3 locked_prefix_rows.py x3m.log ; prints locked_prefix_window fields and per-lock (copy_us+scan_us)/staged
import re,sys
for l in open(sys.argv[1],'rb'):
    s=l.decode('latin1')
    if s.startswith('locked_prefix_window'):
        d=dict(re.findall(r'(\w+)=(\S+)',s)); g=lambda k:float(d.get(k,0))
        print(d['frame'],*(f"{k}={d.get(k)}" for k in('locks','staged','copy_bytes','copy_us','scans','scan_us','sentinel_us','unstaged_total','nested_flushes_total','open_stages')),"perstaged_us=%.2f"%((g('copy_us')+g('scan_us'))/max(g('staged'),1)))
