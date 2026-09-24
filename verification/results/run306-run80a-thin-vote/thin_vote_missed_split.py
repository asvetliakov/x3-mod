import sys,re,glob
# Per-frame split of thin_vote_frame missed for logs without the explicit fields: rest = missed - queued - dropped
# = already_queued (a subset past read_attempts is Unreadable, counted under unreadable, not missed). Cap frames: queued == reads_per_frame (16).
log=glob.glob(f"/tmp/x3-bottleX3-run{sys.argv[1]}/session-*.log")[0]
fr=[]
for l in open(log,errors='replace'):
    if l.startswith('thin_vote_frame '):
        d=dict(re.findall(r'(\w+)=(\S+)',l)); m,q,dr=int(d['missed']),int(d['queued']),int(d['dropped'])
        if m: fr.append((d['frame'],m,q,dr,m-q-dr,int(d['retries'])))
cap=[f for f in fr if f[2]==16]
print('frames_with_misses',len(fr),'cap_frames',len(cap),'missed',sum(f[1] for f in fr),'queued',sum(f[2] for f in fr),
      'dropped',sum(f[3] for f in fr),'already_queued',sum(f[4] for f in fr),'already_queued_on_cap_frames',sum(f[4] for f in cap),'max_retries',max(f[5] for f in fr))
print('top (frame,missed,queued,dropped,already_queued)',[f[:5] for f in sorted(fr,key=lambda f:-f[1])[:5]])
