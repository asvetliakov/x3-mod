"""Native chase_view_restore triage (session 20261011-225940). Usage: python3 triage.py C:/X3/x3m.log
Prints restore rows, sector transits (volumetric_fog_sector) and chase_camera transitions in mode/snaps/cockpits_seen."""
import re, sys
log = sys.argv[1]
kv = re.compile(r'(\w+)=(\S+)')
prev = None
counts = {}
for n, line in enumerate(open(log, errors='replace'), 1):
    head = line.split(' ', 1)[0]
    if head.startswith('chase_view_restore'):
        counts[head] = counts.get(head, 0) + 1
        if head != 'chase_view_restore_site':
            print('L%d %s' % (n, line[:160].rstrip()))
    elif head == 'volumetric_fog_sector':
        d = dict(kv.findall(line))
        print('L%d fog_sector frame=%s reason=%s sector=%s' % (n, d.get('frame'), d.get('reason'), d.get('sector')))
    elif head == 'chase_camera' and ' frame=' in line:
        d = dict(kv.findall(line))
        key = (d.get('mode'), d.get('snaps'), d.get('cockpits_seen'), d.get('snap_reason'), d.get('connect'))
        if key != prev:
            print('L%d chase_camera frame=%s mode=%s connect=%s cockpits_seen=%s snaps=%s coalesced=%s snap_reason=%s applied=%s refused_inactive=%s admitted_unbound=%s' % (
                n, d.get('frame'), d.get('mode'), d.get('connect'), d.get('cockpits_seen'), d.get('snaps'), d.get('coalesced'), d.get('snap_reason'), d.get('applied'), d.get('refused_inactive'), d.get('admitted_unbound')))
            prev = key
print('counts', counts)
