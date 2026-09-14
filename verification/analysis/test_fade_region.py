"""Host checks of the distance-fade region projection (fade_region_math.h).

Step-1 acceptance of docs/architecture/linear-distance-fade-region.md: random
boxes and rows with 10^4 interior points per case inside the derived rectangle,
exact hand rectangles, and full viewport for every doubt (w <= 0, nonfinite,
unknown rows, unknown or poisoned bound, non-solid fill, empty viewport), and
the bound table (fade_region_core.h) driven against fake descriptor/part/
subset-record memory and a fake buffer registry: learn, hit, back-link and
record mismatch, revision/IB/descriptor poison, eviction. The C++ driver uses
the production headers; a Python re-projection cross-checks a subset. No
device, no Wine.
"""
import math
from pathlib import Path
import random
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
IDENTITY=[1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
EXPANSION=1/1024

def perspective(s,d,z_row=None):
    return [s,0,0,0, 0,s,0,0]+(z_row or [0,0,.5,.5*d])+[0,0,1,d]

def fields(line):return dict(re.findall(r'(\w+)=([^ ]+)',line))

def rect_of(text):return tuple(int(v) for v in text.split(','))

def reference_rect(rows,centre,half,viewport,jitter=None):
    """Python restatement of the note's section-2 formula (double precision)."""
    rows=[float(v) for v in rows]
    if jitter:
        jx,jy,w,h=jitter;jx=2*jx/w;jy=-2*jy/h
        for k in range(4):rows[k]+=jx*rows[12+k];rows[4+k]+=jy*rows[12+k]
    X,Y,W,H=viewport;xs=[];ys=[]
    for corner in range(8):
        p=[centre[a]+((half[a]+EXPANSION) if corner>>a&1 else -(half[a]+EXPANSION)) for a in range(3)]
        clip=[rows[4*k]*p[0]+rows[4*k+1]*p[1]+rows[4*k+2]*p[2]+rows[4*k+3] for k in range(4)]
        if not clip[3]>0:return None
        xs.append(X+(clip[0]/clip[3]+1)*W/2);ys.append(Y+(1-clip[1]/clip[3])*H/2)
    l,t=math.floor(min(xs))-1,math.floor(min(ys))-1;r,b=math.ceil(max(xs))+2,math.ceil(max(ys))+2
    l,t,r,b=max(l,X),max(t,Y),min(r,X+W),min(b,Y+H)
    if r<=l or b<=t:return (X,Y,X+1,Y+1)
    return (l,t,r,b)

class FadeRegion(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=shutil.which('clang++') or shutil.which('c++')
        if not compiler:raise unittest.SkipTest('Host compiler required')
        cls.temp=tempfile.TemporaryDirectory(prefix='x3-fade-region-host-');cls.addClassCleanup(cls.temp.cleanup)
        cls.driver=Path(cls.temp.name)/'fade_region_host'
        subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror',
                        str(ROOT/'verification/probe/fade_region_host.cpp'),'-o',str(cls.driver)],check=True,capture_output=True,text=True)

    def case(self,rows,centre,half,viewport,rows_known=1,bound_known=1,fill=1,jitter=None):
        args=[str(self.driver),'--case']+[str(v) for v in rows]+[str(v) for v in centre]+[str(v) for v in half]
        args+=[str(v) for v in viewport]+[str(rows_known),str(bound_known),str(fill)]
        if jitter:args+=[str(jitter[0]),str(jitter[1])]
        out=subprocess.check_output(args,text=True).strip()
        f=fields(out);return int(f['bound']),int(f['reason']),rect_of(f['rect']),float(f['f'])

    def test_random_interior_points_inside_rectangle(self):
        out=subprocess.run([str(self.driver),'--random','20260914','400','10000'],capture_output=True,text=True)
        self.assertEqual(out.returncode,0,out.stdout[-500:])
        rows=[fields(l) for l in out.stdout.splitlines() if l.startswith('CASE ')]
        self.assertEqual(len(rows),400)
        bound=[r for r in rows if r['reason']=='0'];refused=[r for r in rows if r['reason']!='0']
        self.assertGreaterEqual(len(bound),250);self.assertGreaterEqual(len(refused),40)
        self.assertEqual(sum(int(r['outside']) for r in rows),0)
        self.assertEqual(sum(int(r['w_nonpositive']) for r in rows),0)
        for r in bound:
            self.assertEqual(int(r['inside'])+int(r['clipped']),10000,r)
            l,t,rr,b=rect_of(r['rect']);x,y,w,h=(int(v) for v in r['viewport'].split(','))
            self.assertTrue(x<=l<rr<=x+w and y<=t<b<=y+h,r)
        self.assertTrue(all(r['reason']=='4' for r in refused),'only w <= 0 refuses random finite rows')
        self.assertTrue(any(r['jitter']=='1' for r in bound))
        self.assertTrue(any(r['kind']=='5' for r in bound),'maximal |p| = 2 box bounded')
        self.assertTrue(any(r['kind']=='4' for r in bound),'zero-extent box bounded')

    def test_python_reprojection_agrees_and_contains_points(self):
        rng=random.Random(7);checked=0
        for _ in range(40):
            s=.25+rng.random()*8;d=1.5+rng.random()*50;tx=rng.random()*4-2;ty=rng.random()*4-2
            rows=[s,0,0,tx, 0,s,0,ty, 0,0,.5,.5*d, 0,0,1,d]
            centre=[rng.random()*4-2 for _ in range(3)];half=[rng.random()*1.5 for _ in range(3)]
            viewport=(rng.randrange(0,32),rng.randrange(0,32),rng.randrange(8,1920),rng.randrange(8,1080))
            jitter=(rng.random()-.5,rng.random()-.5,viewport[2],viewport[3]) if rng.random()<.5 else None
            bound,reason,rect,_=self.case(rows,centre,half,viewport,jitter=jitter[:2] if jitter else None)
            expected=reference_rect(rows,centre,half,viewport,jitter)
            self.assertEqual((bound,reason),(1,0));self.assertEqual(rect,expected)
            rows32=[float(v) for v in rows]
            for _ in range(2000):
                p=[centre[a]+(rng.random()*2-1)*half[a] for a in range(3)]
                rr=rows32[:]
                if jitter:
                    jx=2*jitter[0]/jitter[2];jy=-2*jitter[1]/jitter[3]
                    for k in range(4):rr[k]+=jx*rr[12+k];rr[4+k]+=jy*rr[12+k]
                clip=[rr[4*k]*p[0]+rr[4*k+1]*p[1]+rr[4*k+2]*p[2]+rr[4*k+3] for k in range(4)]
                self.assertGreater(clip[3],0)
                sx=viewport[0]+(clip[0]/clip[3]+1)*viewport[2]/2;sy=viewport[1]+(1-clip[1]/clip[3])*viewport[3]/2
                for px,py in ((math.floor(sx),math.floor(sy)),(math.floor(sx+.5),math.floor(sy+.5))):
                    if viewport[0]<=px<viewport[0]+viewport[2] and viewport[1]<=py<viewport[1]+viewport[3]:
                        self.assertTrue(rect[0]<=px<rect[2] and rect[1]<=py<rect[3],(rect,px,py));checked+=1
        self.assertGreater(checked,10000)

    def test_hand_rectangles(self):
        self.assertEqual(self.case(IDENTITY,[0,0,0],[.5,.5,.5],(0,0,16,16)),(1,0,(2,2,15,15),(13*13)/256))
        self.assertEqual(self.case(IDENTITY,[10,0,0],[.5,.5,.5],(0,0,16,16))[:3],(1,0,(0,0,1,1)),'off-screen: 1x1 at the origin')
        self.assertEqual(self.case(IDENTITY,[0,0,0],[.5,.5,.5],(4,4,8,8))[:3],(1,0,(4,4,12,12)),'viewport offset')
        self.assertEqual(self.case(IDENTITY,[0,0,0],[0,0,0],(0,0,16,16))[:3],(1,0,(6,6,11,11)),'zero extent keeps the expansion and margin')
        self.assertEqual(self.case(IDENTITY,[0,0,0],[2,2,2],(0,0,16,16))[:3],(1,0,(0,0,16,16)),'maximal box clamps to the viewport')
        bound,reason,rect,_=self.case(perspective(1,2),[0,0,0],[.5,.5,.5],(0,0,16,16))
        self.assertEqual((bound,reason,rect),(1,0,reference_rect(perspective(1,2),[0,0,0],[.5,.5,.5],(0,0,16,16))))
        self.assertEqual(rect,(4,4,13,13))
        # Jitter: the same rows shifted by +0.5 px right / +0.5 px down.
        self.assertEqual(self.case(IDENTITY,[0,0,0],[.5,.5,.5],(0,0,16,16),jitter=(.5,.5))[2],(3,3,15,15))
        self.assertEqual(self.case(IDENTITY,[0,0,0],[.5,.5,.5],(0,0,16,16),jitter=(-.5,-.5))[2],(2,2,14,14))

    def test_every_doubt_selects_the_full_viewport(self):
        full=(0,0,16,16);v=(0,0,16,16);box=([0,0,0],[.5,.5,.5])
        self.assertEqual(self.case(perspective(1,.5),*box,v)[:3],(0,4,full),'corner w = 0')
        self.assertEqual(self.case(perspective(1,0),*box,v)[:3],(0,4,full),'negative w')
        self.assertEqual(self.case(perspective(1,-5),*box,v)[:3],(0,4,full),'all w negative')
        self.assertEqual(self.case(['nan']+IDENTITY[1:],*box,v)[:3],(0,5,full),'nan row')
        self.assertEqual(self.case(IDENTITY[:15]+['inf'],*box,v)[:3],(0,5,full),'inf row')
        self.assertEqual(self.case(IDENTITY,['nan',0,0],[.5,.5,.5],v)[:3],(0,5,full),'nan centre')
        self.assertEqual(self.case(IDENTITY,*box,v,rows_known=0)[:3],(0,2,full),'unknown rows')
        self.assertEqual(self.case(IDENTITY,*box,v,bound_known=0)[:3],(0,3,full),'unknown, mismatched or poisoned bound')
        self.assertEqual(self.case(IDENTITY,[0,0,0],[.5,-.5,.5],v)[:3],(0,3,full),'negative extent')
        self.assertEqual(self.case(IDENTITY,*box,v,fill=0)[:3],(0,6,full),'non-solid fill')
        self.assertEqual(self.case(IDENTITY,*box,(0,0,0,16))[:3],(0,1,(0,0,64,64)),'empty viewport: the whole target')
        # Near-zero positive w is still a valid convex hull: clamped to the viewport.
        self.assertEqual(self.case(perspective(1,.5+2**-10+2**-20),*box,v)[:3],(1,0,full))
        # Near-plane crossing with w > 0 everywhere stays bounded.
        bound,reason,rect,_=self.case(perspective(1,2,[0,0,1,0]),*box,v)
        self.assertEqual((bound,reason,rect),(1,0,(4,4,13,13)))

    def test_bound_table_scenarios(self):
        """Fake descriptor/part/record memory and a fake buffer registry drive BoundTable::resolve."""
        out=subprocess.run([str(self.driver),'--table'],capture_output=True,text=True)
        self.assertEqual(out.returncode,0,out.stdout[-500:])
        rows={l.split()[1]:fields(l) for l in out.stdout.splitlines() if l.startswith('TABLE ')}
        expect={'no_table':('no_table',0,0,0),'no_scope':('no_scope',0,0,0),'miss_learn':('bound',0,0,5),'hit':('bound',1,0,0),
                'back_link':('back_link',0,0,3),'no_record':('no_record',0,0,5),'poison_revision':('poisoned',1,1,0),
                'poisoned_stays':('poisoned',1,0,0),'wrapper_learn':('bound',0,0,5),'wrapper_mismatch':('poisoned',1,1,0),'out_of_domain':('invalid',0,0,3),'ib_mismatch_new':('bound',0,0,5),'ib_mismatch_hit':('poisoned',1,1,0),
                'content_unknown':('content_unknown',0,0,5),'descriptor_mismatch':('bound',0,0,5),'descriptor_poison':('poisoned',1,1,0),
                'read_failed':('read_failed',0,0,2),'window_full':('bound',0,0,5),'evicted_relearn':('bound',0,0,5),
                'poisoned_kept':('poisoned',1,0,0),'failed_read_no_evict':('read_failed',0,0,2),'cleared':('no_table',0,0,0),
                'peek_miss':('bound',0,0,5),'peek_hit':('bound',1,0,0),'peek_invalid':('poisoned',1,0,0),'peek_poisoned':('poisoned',1,0,0),'peek_window_full':('bound',0,0,5)}
        self.assertEqual(set(rows),set(expect))
        for label,(name,hit,poisoned_now,reads) in expect.items():
            r=rows[label]
            self.assertEqual((r['name'],int(r['hit']),int(r['poisoned_now'])),(name,hit,poisoned_now),label)
            if label not in ('window_full','peek_window_full'):self.assertEqual(int(r['reads']),reads,(label,'game reads'))
        self.assertEqual(rows['peek_window_full']['reads'],str(32*5+5),'32 learns then the read-only derivation')
        self.assertEqual(rows['window_full']['reads'],'5','the evicting learn')
        # peek never changes the table: no insert on a miss, no poison on an
        # invalid hit, no eviction on a full window, same box as the learn.
        self.assertEqual((rows['peek_miss']['used'],rows['peek_miss']['evicted'],rows['miss_learn']['used']),('0','0','1'))
        self.assertEqual((rows['peek_miss']['aabb'],rows['peek_hit']['aabb']),(rows['miss_learn']['aabb'],rows['hit']['aabb']))
        self.assertEqual(rows['peek_invalid']['poisoned'],rows['no_record']['poisoned'],'peek reports Poisoned without poisoning')
        self.assertEqual(int(rows['poison_revision']['poisoned']),int(rows['peek_invalid']['poisoned'])+1)
        self.assertEqual((rows['peek_window_full']['used'],rows['peek_window_full']['evictions'],rows['peek_window_full']['evicted']),('32','0','0'))
        self.assertEqual(rows['peek_window_full']['aabb'],rows['window_full']['aabb'])
        self.assertEqual((rows['window_full']['evicted'],rows['evicted_relearn']['evicted'],rows['failed_read_no_evict']['evicted']),('1','1','0'))
        self.assertEqual((rows['window_full']['used'],rows['window_full']['evictions'],rows['poisoned_kept']['poisoned']),('32','1','1'))
        self.assertEqual(rows['descriptor_poison']['poisoned'],'4')
        self.assertEqual(rows['miss_learn']['aabb'],'400,-800,200,1200,600,300')
        self.assertEqual(rows['hit']['centre'],'%.6f,%.6f,%.6f'%(400/65536,-800/65536,200/65536))
        self.assertEqual(rows['hit']['half'],'%.6f,%.6f,%.6f'%(1200/65536,600/65536,300/65536))
        self.assertEqual((rows['miss_learn']['vb_rev'],rows['poison_revision']['vb_rev']),('7','8'))

    def test_locked_prefix_scan_table_and_resolution(self):
        """Step B (screen-emission-region.md): checkpoint math, superset with a garbage tail, refusals, revision, eviction, binding."""
        out=subprocess.run([str(self.driver),'--prefix','20260914','300'],capture_output=True,text=True)
        self.assertEqual(out.returncode,0,out.stdout[-500:])
        rows={l.split()[1]:fields(l) for l in out.stdout.splitlines() if l.startswith('PREFIX ')}
        resolved={l.split()[1]:fields(l) for l in out.stdout.splitlines() if l.startswith('RESOLVE ')}
        summary=fields(next(l for l in out.stdout.splitlines() if l.startswith('PREFIX_RANDOM ')))
        # Checkpoint k covers [0, 96(k+1)): vertex i at (i, -i, 2i).
        def box(lo,hi):return '%.6f,%.6f,%.6f,%.6f,%.6f,%.6f'%((lo+hi)/2,-(lo+hi)/2,lo+hi,(hi-lo)/2,(hi-lo)/2,hi-lo)
        self.assertEqual((rows['math_96']['name'],rows['math_96']['checkpoint'],rows['math_96']['box']),('bound','0',box(0,95)))
        self.assertEqual((rows['math_1']['checkpoint'],rows['math_1']['box']),('0',box(0,95)),'a 1-vertex draw takes checkpoint 0: at most 95 stale vertices')
        for label,cp,hi in (('math_97','1',191),('math_192','1',191),('math_193','2',199),('math_200','2',199)):
            self.assertEqual((rows[label]['name'],rows[label]['checkpoint'],rows[label]['box']),('bound',cp,box(0,hi)),label)
        self.assertEqual(rows['math_96']['extra'],'200','vertices scanned')
        for label,name in (('math_unknown','unknown'),('math_pending','pending'),('empty','empty'),('beyond','beyond'),('beyond_max','beyond'),
                           ('relock_pending','pending'),('unlock_failed','invalid'),('non_discard','invalid'),('thread_mismatch','invalid'),
                           ('relearned','bound'),('erased','unknown'),('tail_nan_100','nonfinite'),('tail_nan_96','bound'),('tail_nan_beyond','bound'),
                           ('tail_inf_100','nonfinite'),('tail_absurd_100','nonfinite'),('tail_limit_100','bound'),('tail_huge_100','bound'),
                           ('tail_huge_96','bound'),('prefix_nan','nonfinite'),('evicted_oldest','unknown'),('evicted_kept','bound'),('cleared','unknown')):
            self.assertEqual(rows[label]['name'],name,label)
        self.assertEqual([rows[l]['revision'] for l in ('math_pending','relock_pending','non_discard','thread_mismatch','relearned')],['1','2','3','4','5'],'every lock advances the revision')
        # Stale-tail garbage: a finite tail inside the covering checkpoint widens the box; the exact prefix keeps it tight.
        self.assertEqual(rows['tail_huge_96']['box'],'0.000000,0.000000,0.000000,1.000000,1.000000,1.000000')
        self.assertEqual(rows['tail_huge_100']['box'],'499999.500000,499999.500000,499999.500000,500000.500000,500000.500000,500000.500000')
        self.assertEqual((rows['length_partial']['checkpoint'],rows['length_partial']['revision']),('10','1'),'partial trailing vertex ignored')
        self.assertEqual((rows['length_capped']['checkpoint'],rows['length_capped']['revision']),('6144','64'),'scan capped at 6144 vertices, 64 checkpoints')
        self.assertEqual((rows['evicted_oldest']['used'],rows['evicted_oldest']['evictions']),('16','1'),'17th buffer evicts the oldest record')
        self.assertEqual(rows['cleared']['used'],'0')
        # resolve_locked_prefix maps lookup outcomes onto the region statuses.
        self.assertEqual((resolved['bound']['name'],resolved['bound']['source'],resolved['bound']['checkpoint'],resolved['bound']['box']),('bound','1','2',box(0,199)))
        for label,name,refusal in (('no_vb','no_scope','0'),('zero_count','no_scope','0'),('no_binding','content_unknown','0'),('unknown_buffer','content_unknown','1'),('beyond','invalid','5')):
            self.assertEqual((resolved[label]['name'],resolved[label]['bound'],resolved[label]['refusal']),(name,'0',refusal),label)
        # Random superset property: every drawn vertex inside the covering box; exact only at whole checkpoints.
        self.assertEqual((summary['cases'],summary['failures'],summary['vertices']),('300','0','6144'))
        self.assertGreater(int(summary['larger']),int(summary['exact']))
        self.assertGreater(int(summary['exact']),0)
        self.assertLess(float(summary['scan_ns']),2e6,'a 6144-vertex scan stays well under a millisecond on the host')

if __name__=='__main__':unittest.main()
