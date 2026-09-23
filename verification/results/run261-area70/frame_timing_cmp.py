"""Median of frame_timing dt_p50_us / draws_p50 over rows with dt, frame>3000, and over rows with slow=0. usage: LOG..."""
import sys, re, statistics as st
for log in sys.argv[1:]:
    r = [dict(re.findall(r'(\w+)=(\S+)', l)) for l in open(log, errors='replace') if l.startswith('frame_timing ')]
    r = [x for x in r if 'dt_p50_us' in x and int(x['frame']) > 3000]
    q = [x for x in r if x.get('slow') == '0']
    for name, s in (('all', r), ('slow=0', q)):
        if s: print(log.split('/')[2], name, len(s), 'dt_p50 median', st.median(int(x['dt_p50_us']) for x in s), 'draws_p50 median', st.median(int(x['draws_p50']) for x in s))
