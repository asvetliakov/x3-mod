# usage: python3 draw_list.py <frame-slice.log> [first_index]
# State/texture rows FOLLOW their draw row. One line per draw: index vs ps prims abe src dst op bf zfunc zwrite, plus bound stage-0 texture row if logged.
import sys
ids={'27':'abe','19':'src','20':'dst','171':'op','193':'bf','23':'zf','14':'zw','7':'z'}
first=int(sys.argv[2]) if len(sys.argv)>2 else 0
cur={};tex=[];d=None
for l in open(sys.argv[1],errors='replace'):
    if l.startswith('state id='):
        p=l.split();k=p[1][3:]
        if k in ids: cur[ids[k]]=int(p[2][6:])
    elif l.startswith('texture_desc stage=0'): tex.append(' '.join(l.split()[2:5]))
    elif l.startswith('draw ') or l.startswith('frame_end'):
        if d and int(d['index'])>=first:
            print(d['index'],d['vs'][:8],d['ps'][:8],d.get('primitives'),' '.join('%s=%s'%(k,('%08x'%v if k=='bf' else v)) for k,v in sorted(cur.items())), tex[:1])
        cur={};tex=[]
        d=dict(x.split('=',1) for x in l.split()[1:] if '=' in x) if l.startswith('draw ') else None
