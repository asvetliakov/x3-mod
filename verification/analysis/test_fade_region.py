"""Host checks of the distance-fade region projection (fade_region_math.h).

Step-1 acceptance of docs/architecture/linear-distance-fade-region.md: random
boxes and rows with 10^4 interior points per case inside the derived rectangle,
exact hand rectangles, and full viewport for every doubt (w <= 0, nonfinite,
unknown rows, unknown or poisoned bound, non-solid fill, empty viewport), and
the bound table (fade_region_core.h) driven against fake descriptor/part/
subset-record memory and a fake buffer registry: learn, hit, back-link and
record mismatch, revision/IB/descriptor poison, eviction. Step B/D
(screen-emission notes): the locked-prefix sentinel scan and table
(locked_prefix_core.h) and the per-draw vertex hull (project_prefix) against
a rasterising oracle in the C++ driver plus a Python one here. The C++ driver
uses the production headers; a Python re-projection cross-checks a subset.
No device, no Wine.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import math
from pathlib import Path
import random
import re
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock
import sys

ROOT=Path(__file__).resolve().parents[2]
IDENTITY=[1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
EXPANSION=1/1024
EPS_DP4=2**-22   # 4 * 2^-24: fp32 dp4 rounding of a 4-term dot product
PAD_LIMIT=8

def pad_pixels(term_sum_max,w_min,half_max):
    """Python restatement of fade_region_math.h pad_pixels: the w-scaled
    conservative pad for the GPU's fp32 dp4 (screen-emission-bullet-bound.md,
    section 4)."""
    k=2*half_max*EPS_DP4*term_sum_max
    if not k>0 or not w_min>0:return 1 if w_min>0 else PAD_LIMIT
    return max(1,min(PAD_LIMIT,math.ceil(k/w_min)))

def term_sum_max(rows,centre,half):
    """Largest sum of |terms| over the x, y and w rows and the eight expanded corners."""
    best=0.0
    for corner in range(8):
        p=[centre[a]+((half[a]+EXPANSION) if corner>>a&1 else -(half[a]+EXPANSION)) for a in range(3)]
        for k in (0,1,3):
            best=max(best,sum(abs(rows[4*k+a]*p[a]) for a in range(3))+abs(rows[4*k+3]))
    return best

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
    X,Y,W,H=viewport;xs=[];ys=[];ws=[]
    for corner in range(8):
        p=[centre[a]+((half[a]+EXPANSION) if corner>>a&1 else -(half[a]+EXPANSION)) for a in range(3)]
        clip=[rows[4*k]*p[0]+rows[4*k+1]*p[1]+rows[4*k+2]*p[2]+rows[4*k+3] for k in range(4)]
        if not clip[3]>0:return None
        ws.append(clip[3])
        xs.append(X+(clip[0]/clip[3]+1)*W/2);ys.append(Y+(1-clip[1]/clip[3])*H/2)
    pad=pad_pixels(term_sum_max(rows,centre,half),min(ws),max(W,H)/2)
    l,t=math.floor(min(xs))-pad,math.floor(min(ys))-pad;r,b=math.ceil(max(xs))+pad+1,math.ceil(max(ys))+pad+1
    l,t,r,b=max(l,X),max(t,Y),min(r,X+W),min(b,Y+H)
    if r<=l or b<=t:return (X,Y,X+1,Y+1)
    return (l,t,r,b)

# Near-plane rows of the screen-emission fixtures: x' = x, y' = y,
# z' = .1 (z - 1), w = z; the D3D near plane z' = 0 sits at w = 1.
NEAR_ROWS=[1,0,0,0, 0,1,0,0, 0,0,.1,-.1, 0,0,1,0]

def reference_clip_rect(rows,centre,half,viewport):
    """Python restatement of the near cut (screen-emission-region.md, step B):
    corners with clip z >= 0 plus the crossings of the box edges with z = 0,
    projected; None when every corner is behind."""
    rows=[float(v) for v in rows];X,Y,W,H=viewport
    corners=[]
    for corner in range(8):
        p=[centre[a]+((half[a]+EXPANSION) if corner>>a&1 else -(half[a]+EXPANSION)) for a in range(3)]
        corners.append([rows[4*k]*p[0]+rows[4*k+1]*p[1]+rows[4*k+2]*p[2]+rows[4*k+3] for k in range(4)])
    points=[c for c in corners if c[2]>=0]
    if not points:return None,8
    for a in range(8):
        for axis in range(3):
            b=a^(1<<axis)
            if b<a or (corners[a][2]<0)==(corners[b][2]<0):continue
            t=corners[a][2]/(corners[a][2]-corners[b][2])
            points.append([corners[a][k]+t*(corners[b][k]-corners[a][k]) for k in range(4)])
    xs=[X+(c[0]/c[3]+1)*W/2 for c in points];ys=[Y+(1-c[1]/c[3])*H/2 for c in points]
    clamp=lambda v:max(-1e9,min(1e9,v))
    pad=pad_pixels(term_sum_max(rows,centre,half),min(c[3] for c in points),max(W,H)/2)
    l,t=math.floor(clamp(min(xs)))-pad,math.floor(clamp(min(ys)))-pad;r,b=math.ceil(clamp(max(xs)))+pad+1,math.ceil(clamp(max(ys)))+pad+1
    l,t,r,b=max(l,X),max(t,Y),min(r,X+W),min(b,Y+H)
    if r<=l or b<=t:return (X,Y,X+1,Y+1),8-len([c for c in corners if c[2]>=0])
    return (l,t,r,b),8-len([c for c in corners if c[2]>=0])

def box_of(lo,hi):
    return [(a+b)/2 for a,b in zip(lo,hi)],[(b-a)/2 for a,b in zip(lo,hi)]

def footprint_pixels(rows,positions,viewport):
    """Python oracle for project_prefix: each triangle's clip coordinates
    (exact arithmetic), cut against z >= 0 by Sutherland-Hodgman, projected,
    and every pixel whose centre lies inside the resulting convex polygon.
    Independent of the C++ derivation and of the C++ oracle."""
    rows=[float(v) for v in rows];X,Y,W,H=viewport;pixels=set()
    for t in range(0,len(positions)//3-2,3):
        poly=[]
        for k in range(3):
            p=positions[(t+k)*3:(t+k)*3+3]
            poly.append([rows[4*r]*p[0]+rows[4*r+1]*p[1]+rows[4*r+2]*p[2]+rows[4*r+3] for r in range(4)])
        cut=[]
        for i in range(3):
            a,b=poly[i],poly[(i+1)%3];ina,inb=a[2]>=0,b[2]>=0
            if ina:cut.append(a)
            if ina!=inb:
                s=a[2]/(a[2]-b[2]);cut.append([a[r]+s*(b[r]-a[r]) for r in range(4)])
        if len(cut)<3 or any(not c[3]>0 for c in cut):continue
        pts=[(X+(c[0]/c[3]+1)*W/2,Y+(1-c[1]/c[3])*H/2) for c in cut]
        lo_x=max(X,math.floor(min(p[0] for p in pts)));hi_x=min(X+W,math.ceil(max(p[0] for p in pts)))
        lo_y=max(Y,math.floor(min(p[1] for p in pts)));hi_y=min(Y+H,math.ceil(max(p[1] for p in pts)))
        def inside(px,py):
            sign=0
            for i in range(len(pts)):
                ax,ay=pts[i];bx,by=pts[(i+1)%len(pts)]
                cross=(bx-ax)*(py-ay)-(by-ay)*(px-ax)
                if cross==0:continue
                s=1 if cross>0 else -1
                if sign==0:sign=s
                elif s!=sign:return False
            return True
        for y in range(lo_y,hi_y):
            for x in range(lo_x,hi_x):
                if inside(x+.5,y+.5):pixels.add((x,y))
    return pixels

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
        f=fields(out);self.pad=int(f['pad']);return int(f['bound']),int(f['reason']),rect_of(f['rect']),float(f['f'])

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

    def near_case(self,rows,centre,half,viewport):
        args=[str(self.driver),'--near-case']+[str(v) for v in rows+list(centre)+list(half)+list(viewport)]
        f=fields(subprocess.check_output(args,text=True).strip())
        self.pad=int(f['pad'])
        return int(f['bound']),int(f['reason']),int(f['clipped']),rect_of(f['rect']),float(f['f'])

    def test_near_clip_random_points_inside_and_behind_refused(self):
        out=subprocess.run([str(self.driver),'--near','20260914','600','4000'],capture_output=True,text=True)
        self.assertEqual(out.returncode,0,out.stdout[-500:])
        rows=[fields(l) for l in out.stdout.splitlines() if l.startswith('NEAR ')]
        self.assertEqual(len(rows),600);self.assertEqual(sum(int(r['fail']) for r in rows),0)
        self.assertEqual(sum(int(r['outside']) for r in rows),0)
        # The fp32 dp4 perturbation of every interior point stays inside the
        # padded rectangle wherever the pad is not truncated by pad_limit; the
        # capped cases (w far below any game near plane) are reported, not hidden.
        self.assertEqual(sum(int(r['outside_fp32']) for r in rows if r['capped']=='0'),0,'worst-case fp32 dp4 perturbation stays inside the padded rect')
        pads=[int(r['pad']) for r in rows if r['reason']=='0']
        self.assertGreaterEqual(sum(p>1 for p in pads),40,'world-coordinate cases exercise a pad above 1 px')
        self.assertLessEqual(max(pads),PAD_LIMIT)
        behind=[r for r in rows if r['reason']=='7'];cut=[r for r in rows if r['reason']=='0' and r['clipped']!='0'];plain=[r for r in rows if r['reason']=='0' and r['clipped']=='0']
        self.assertGreaterEqual(len(behind),40);self.assertGreaterEqual(len(cut),80);self.assertGreaterEqual(len(plain),80)
        self.assertTrue(all(r['behind']=='8' for r in behind))
        # The plain projection would have refused every cut box (a corner at w <= 0) that the cut bounds.
        self.assertTrue(all(r['plain_reason'] in ('0','4') for r in cut))
        self.assertGreaterEqual(sum(r['plain_reason']=='4' for r in cut),40,'straddling boxes are now bounded instead of refused')
        for r in cut:self.assertGreater(int(r['inside'])+int(r['clipped_points']),0,r)

    def test_near_clip_hand_cases(self):
        v=(0,0,64,64);quad=(32,24,48,40)
        def contains(rect,inner):return rect[0]<=inner[0] and rect[1]<=inner[1] and rect[2]>=inner[2] and rect[3]>=inner[3]
        # Straddling: the step-C 'n' triangle (behind vertex at w = -1, far edge at w = 3) plus the zero tail.
        centre,half=box_of((0,0,-1),(1.5,.75,3))
        bound,reason,clipped,rect,f=self.near_case(NEAR_ROWS,centre,half,v)
        self.assertEqual((bound,reason,clipped),(1,0,4));self.assertEqual(rect,reference_clip_rect(NEAR_ROWS,centre,half,v)[0])
        self.assertTrue(contains(rect,(32,20,55,24)),rect);self.assertLess(f,.5)
        self.assertEqual(self.case(NEAR_ROWS,centre,half,v)[:2],(0,4),'the plain projection refuses the same box')
        # Ending exactly on the near plane (w = 1): the half-float expansion puts the near corners 2^-10 behind it.
        centre,half=box_of((0,-.25,1),(1.5,.75,3))
        bound,reason,clipped,rect,f=self.near_case(NEAR_ROWS,centre,half,v)
        self.assertEqual((bound,reason,clipped),(1,0,4));self.assertEqual(rect,reference_clip_rect(NEAR_ROWS,centre,half,v)[0])
        self.assertTrue(contains(rect,quad),rect)
        # Without the expansion the corners sit on the plane and nothing is cut.
        exact=self.near_case(NEAR_ROWS,[c for c in centre],[h-EXPANSION for h in half],v)
        self.assertEqual(exact[:3],(1,0,0));self.assertEqual(exact[3],self.case(NEAR_ROWS,centre,[h-EXPANSION for h in half],v)[2])
        # Entirely behind: refused as BehindNear (7), the caller's fallback is the full viewport.
        centre,half=box_of((0,-.25,-3),(1.5,.75,-1))
        self.assertEqual(self.near_case(NEAR_ROWS,centre,half,v)[:4],(0,7,8,(0,0,64,64)))
        self.assertIsNone(reference_clip_rect(NEAR_ROWS,centre,half,v)[0])
        # Long beam from the near plane to w = 1000: bounded well below the viewport and covering the quad.
        centre,half=box_of((0,-.25,1),(500,250,1000))
        bound,reason,clipped,rect,f=self.near_case(NEAR_ROWS,centre,half,v)
        self.assertEqual((bound,reason,clipped),(1,0,4));self.assertEqual(rect,reference_clip_rect(NEAR_ROWS,centre,half,v)[0])
        self.assertTrue(contains(rect,quad),rect);self.assertLess(f,.5);self.assertEqual(rect,(30,0,64,43))
        # A box in front of the plane: the cut changes nothing.
        for rows in (IDENTITY,perspective(1,2)):
            self.assertEqual(self.near_case(rows,[0,0,0],[.5,.5,.5],v)[3],self.case(rows,[0,0,0],[.5,.5,.5],v)[2])
        # Random straddling boxes agree with the Python restatement exactly.
        rng=random.Random(11);cut=0
        for _ in range(60):
            lo=[rng.uniform(-3,3) for _ in range(3)];hi=[l+rng.uniform(0,4) for l in lo]
            centre,half=box_of(lo,hi)
            bound,reason,clipped,rect,_=self.near_case(NEAR_ROWS,centre,half,v)
            expected,behind=reference_clip_rect(NEAR_ROWS,centre,half,v)
            if expected is None:self.assertEqual((bound,reason,clipped),(0,7,8))
            else:self.assertEqual((bound,reason,clipped,rect),(1,0,behind,expected));cut+=clipped>0
        self.assertGreater(cut,10)

    def hull_case(self,rows,viewport,positions):
        args=[str(self.driver),'--hull-case']+[str(v) for v in rows]+[str(v) for v in viewport]+[str(len(positions)//3)]+[str(v) for v in positions]
        return fields(subprocess.check_output(args,text=True).strip())

    def test_prefix_hull_random_triangles_covered(self):
        """Step D: random triangle lists (straddling, hugging and behind the near plane, bullet-scale
        cancellation) against the C++ rasterising oracle: no covered pixel outside, BehindNear covers
        nothing, the hull rectangle never exceeds the near-clipped AABB rectangle of the same vertices."""
        out=subprocess.run([str(self.driver),'--hull','20260914','600','64'],capture_output=True,text=True)
        self.assertEqual(out.returncode,0,out.stdout[-500:])
        rows=[fields(l) for l in out.stdout.splitlines() if l.startswith('HULL ')]
        self.assertEqual(len(rows),600);self.assertEqual(fields(out.stdout.splitlines()[-1])['failures'],'0')
        bound=[r for r in rows if r['reason']=='0'];behind=[r for r in rows if r['reason']=='7']
        self.assertEqual(len(bound)+len(behind),600);self.assertGreaterEqual(len(bound),300);self.assertGreaterEqual(len(behind),200)
        self.assertEqual(sum(int(r['outside']) for r in rows),0)
        self.assertGreater(sum(int(r['covered']) for r in bound),10**6)
        self.assertEqual(sum(int(r['covered']) for r in behind),0)
        self.assertTrue(all(0<int(r['hull_px'])<=int(r['aabb_px']) for r in bound))
        self.assertTrue(all(int(r['clipped'])<=int(r['behind']) for r in rows),'the -eps cut never counts more behind than the fp32 oracle')
        self.assertGreater(sum(int(r['hull_px'])<int(r['aabb_px']) for r in bound),100,'the hull is tighter than the box for most lists')
        self.assertGreater(sum(int(r['pad'])>1 for r in bound),20,'the w-scaled pad engages at bullet-scale cancellation')
        # Cap-truncated pads are reported separately, as --near does; the exact-arithmetic oracle cannot see the truncation.
        capped=[r for r in bound if int(r['capped'])]
        self.assertEqual(len(capped),int(fields(out.stdout.splitlines()[-1])['capped']));self.assertTrue(all(int(r['pad'])==PAD_LIMIT for r in capped))
        self.assertLess(len(capped),len(bound)//2,'most bound lists stay under the pad cap')
        self.assertTrue(all(int(r['clipped'])>0 for r in bound if r['kind']=='3'),'near-plane hugging lists are cut')

    def test_prefix_hull_hand_cases(self):
        """Step D hand cases through project_prefix with the Python oracle: the fixture's near-plane
        quads, a fan against its own AABB, the fp32 z tolerance and every refusal."""
        v=(0,0,96,96)
        def contains(rect,inner):return rect[0]<=inner[0] and rect[1]<=inner[1] and rect[2]>=inner[2] and rect[3]>=inner[3]
        def check(rows,viewport,positions,expect_bound=1):
            f=self.hull_case(rows,viewport,positions)
            pixels=footprint_pixels(rows,positions,viewport)
            rect=rect_of(f['rect'])
            if expect_bound:
                self.assertEqual(int(f['bound']),1,f)
                self.assertTrue(all(rect[0]<=x<rect[2] and rect[1]<=y<rect[3] for x,y in pixels),(f,len(pixels)))
                self.assertEqual(int(f['outside']),0,f)
                if pixels:self.assertTrue(contains(rect,(min(x for x,_ in pixels),min(y for _,y in pixels),max(x for x,_ in pixels)+1,max(y for _,y in pixels)+1)))
            return f,rect,pixels
        # The live fixture's near-plane cases (one copy of the six vertices; (x, y, w) through NEAR_ROWS).
        straddle=[0,0,-1, 0,.75,3, 1.5,.75,3, 0,.75,3, 0,.75,3, 0,.75,3]
        f,rect,pixels=check(NEAR_ROWS,v,straddle)
        self.assertEqual((int(f['reason']),int(f['clipped'])),(0,1));self.assertTrue(contains(rect,(48,30,84,36)),rect);self.assertGreater(len(pixels),100)
        self.assertLess(int(f['hull_px']),int(f['aabb_px']),'the vertex hull beats the AABB of the same vertices')
        exact=[0,-.25,1, .5,-.25,1, 0,.75,3, .5,-.25,1, 1.5,.75,3, 0,.75,3]
        f,rect,pixels=check(NEAR_ROWS,v,exact)
        self.assertEqual((int(f['reason']),int(f['clipped'])),(0,0),'vertices on the plane are in front');self.assertTrue(contains(rect,(48,36,72,60)),rect)
        behind=[0,-.25,-1, .5,-.25,-1, 0,.75,-3, .5,-.25,-1, 1.5,.75,-3, 0,.75,-3]
        f=self.hull_case(NEAR_ROWS,v,behind)
        self.assertEqual((int(f['bound']),int(f['reason']),int(f['clipped']),rect_of(f['rect'])),(0,7,6,(0,0,96,96)));self.assertEqual(footprint_pixels(NEAR_ROWS,behind,v),set())
        beam=[0,-.25,1, .5,-.25,1, 0,250,1000, .5,-.25,1, 500,250,1000, 0,250,1000]
        f,rect,pixels=check(NEAR_ROWS,v,beam)
        self.assertEqual(int(f['clipped']),0);self.assertLess(float(f['f']),.5);self.assertTrue(contains(rect,(48,36,72,60)),rect)
        # A fan of thin quads along a diagonal in front of a perspective camera: hull far below the AABB.
        rows=perspective(1,2);rng=random.Random(5);fan=[]
        for i in range(40):
            x,y,z=.02*i-.4,.01*i-.2,.5+.1*i
            for dx,dy in ((-.01,0),(.01,0),(-.01,.02),(.01,0),(.01,.02),(-.01,.02)):fan+=[x+dx,y+dy,z]
        f,rect,pixels=check(rows,(0,0,640,480),fan)
        self.assertGreater(len(pixels),50);self.assertLess(int(f['hull_px']),int(f['aabb_px']),f)
        # fp32 z tolerance at bullet-scale coordinates (T = 1e5 cancellation): a vertex 1e-4 clip units
        # behind the plane may still be in front for the GPU's fp32 dp4, so it is not cut; one 0.05
        # behind is.
        T=100000.0;rows_t=[1,0,0,-T, 0,1,0,-T, 0,0,.1,-.1-.1*T, 0,0,1,-T]
        def tri(z0):return [T+0,T+0,T+z0, T+.5,T+.2,T+2, T+.1,T+.6,T+2]
        f=self.hull_case(rows_t,v,tri(1-1e-3))
        self.assertEqual((int(f['bound']),int(f['clipped'])),(1,0),f)
        f=self.hull_case(rows_t,v,tri(1-.5))
        self.assertEqual((int(f['bound']),int(f['clipped'])),(1,1),f)
        self.assertGreater(int(f['pad']),1,'bullet-scale cancellation pads by more than a pixel near the plane')
        # Refusals: a count that is not a multiple of 3, a NaN position, nonfinite rows, an empty prefix.
        self.assertEqual((int(self.hull_case(NEAR_ROWS,v,exact[:12])['bound']),int(self.hull_case(NEAR_ROWS,v,exact[:12])['reason'])),(0,3))
        self.assertEqual(int(self.hull_case(NEAR_ROWS,v,['nan']+exact[1:])['reason']),5)
        self.assertEqual(int(self.hull_case(['inf']+NEAR_ROWS[1:],v,exact)['reason']),5)
        self.assertEqual(int(self.hull_case(NEAR_ROWS,v,[])['reason']),3)
        self.assertEqual(int(self.hull_case(NEAR_ROWS,(0,0,0,0),exact)['reason']),1)
        # Non-perspective rows with a vertex at w <= 0 in front of the plane: NonPositiveW, never a box.
        self.assertEqual(int(self.hull_case([1,0,0,0, 0,1,0,0, 0,0,1,1, 0,0,0,-1],v,exact)['reason']),4)
        # Random small lists through the Python oracle as well.
        rng=random.Random(20260914);lists=0
        for _ in range(40):
            n=rng.randint(1,6);positions=[]
            for _ in range(n*3):positions+=[rng.uniform(-2,2),rng.uniform(-2,2),rng.uniform(-2,4)]
            f=self.hull_case(NEAR_ROWS,v,positions)
            if int(f['bound']):check(NEAR_ROWS,v,positions);lists+=1
            else:self.assertEqual((int(f['reason']),footprint_pixels(NEAR_ROWS,positions,v)),(7,set()))
        self.assertGreater(lists,20)

    def test_w_scaled_pad_near_the_near_plane(self):
        """screen-emission-bullet-bound.md section 4: the fp32 dp4 on world
        coordinates ~1e5 costs ~0.03 clip units, several pixels at w = 6 and
        nothing at w >= 100. The pad follows 1/w and only ever grows the rect."""
        T=6e4;v=(0,0,1280,720);centre=[T,T,0];half=[.5,.5,.5]
        def rows_at(w):return [1,0,0,-T, 0,1,0,-T, 0,0,0,w*.5, 0,0,1,w]
        S=term_sum_max(rows_at(6),centre,half)
        self.assertAlmostEqual(S,2*T+.5+EXPANSION,places=3)
        self.assertAlmostEqual(S*EPS_DP4,.0286,places=3,msg='the note calibrates 1.26e5 terms to 0.03 clip units')
        expected={6:7,30:2,100:1,1000:1}
        for w,pad in expected.items():
            rows=rows_at(w)
            self.assertEqual(pad_pixels(S,w,640),pad)
            bound,reason,rect,_=self.case(rows,centre,half,v)
            self.assertEqual((bound,reason),(1,0))
            self.assertEqual(self.pad,pad,f'w={w}')
            self.assertEqual(rect,reference_rect(rows,centre,half,v))
            # The unpadded (pure double-precision) hull, and the historical 1-px rect.
            xs=[];ys=[]
            for corner in range(8):
                q=[centre[a]+((half[a]+EXPANSION) if corner>>a&1 else -(half[a]+EXPANSION)) for a in range(3)]
                clip=[rows[4*k]*q[0]+rows[4*k+1]*q[1]+rows[4*k+2]*q[2]+rows[4*k+3] for k in range(4)]
                xs.append((clip[0]/clip[3]+1)*v[2]/2);ys.append((1-clip[1]/clip[3])*v[3]/2)
            hull=(math.floor(min(xs)),math.floor(min(ys)),math.ceil(max(xs))+1,math.ceil(max(ys))+1)
            old=(hull[0]-1,hull[1]-1,hull[2]+1,hull[3]+1)
            self.assertTrue(rect[0]<=hull[0] and rect[1]<=hull[1] and rect[2]>=hull[2] and rect[3]>=hull[3],(rect,hull))
            self.assertTrue(rect[0]<=old[0] and rect[1]<=old[1] and rect[2]>=old[2] and rect[3]>=old[3],(rect,old))
            self.assertEqual((rect[0],rect[1]),(old[0]-(pad-1),old[1]-(pad-1)))
            # A point on the hull displaced by the whole fp32 error bound stays inside.
            err=EPS_DP4*S
            for sign in (-1,1):
                sx=(( .5+EXPANSION+sign*err)/w+1)*v[2]/2;sy=(1-(.5+EXPANSION+sign*err)/w)*v[3]/2
                self.assertTrue(rect[0]<=math.floor(sx)<rect[2] and rect[1]<=math.floor(sy)<rect[3],(w,rect,sx,sy))
        # A degenerate w cannot inflate the rectangle beyond the cap.
        self.assertEqual(pad_pixels(S,1e-9,640),PAD_LIMIT)
        self.assertEqual(pad_pixels(S,0,640),PAD_LIMIT)
        # Small coordinates keep the historical 1-px pad everywhere.
        self.assertEqual(pad_pixels(term_sum_max(IDENTITY,[0,0,0],[.5,.5,.5]),1,640),1)

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

    def fade_route(self,alpha,fog,fog_clip,rows,camera,threshold=500):
        """--fade-route: distance of the rows' origin, the fraction and the admission (fade_route_core.h)."""
        valid,m00,m11,m20,m21=camera
        args=[str(self.driver),'--fade-route',str(alpha),str(int(fog)),str(fog_clip[0]),str(fog_clip[1])]+[str(v) for v in rows]
        args+=[str(int(valid)),str(m00),str(m11),str(m20),str(m21),str(threshold)]
        f=fields(subprocess.check_output(args,text=True).strip())
        return int(f['ok']),float(f['distance']),float(f['fraction']),int(f['permille']),int(f['admit'])

    def test_fade_band_arm_registers_state_and_fraction(self):
        """Fade-band motion arm (fade_route_core.h): the seven fade vertex programs' g_AlphaValue /
        g_FogClip registers, the exact fade-band state, and the fraction
        COLOR0.a = alpha.x * saturate(fog.x - fog.y * distance(origin, camera)) with the
        origin distance from the clip rows and the camera's projection scales."""
        loop=('b0602757fce6e870','0c223ad11bce02d5','167eb2d5629ab9d3','330ceb9dd874ede2','4944d81dfe531b37')
        fixed=('233d17d26ce0c1fc','12b8a13f13fe8cfe')
        # fade-rt2-ownership.md section 3: the run214 station families declare the same three registers; the glass and
        # damage programs (material transparency in the fade-band state) are not rows and keep the overlay path.
        stations=('494fe349b8bc12ec','53a0a641107ed76c')
        for vs,expect in [(v,(1,39,41)) for v in loop+stations]+[(v,(1,18,20)) for v in fixed]+[(v,(0,0,0)) for v in ('c30104cb0efb6675','37c34a7478544c14','0')]:
            f=fields(subprocess.check_output([str(self.driver),'--fade-route-registers',vs],text=True).strip())
            self.assertEqual((int(f['known']),int(f['alpha']),int(f['fog'])),expect,vs)
        band=[1,0,0,1,7,0,5,6,1,0]  # ZENABLE, ZWRITE, ALPHATEST, ALPHABLEND, COLORWRITE, SRGBWRITE, SRCBLEND, DESTBLEND, BLENDOP, SEPARATEALPHA
        state=lambda v:int(fields(subprocess.check_output([str(self.driver),'--fade-route-state']+[str(x) for x in v],text=True).strip())['fade_band'])
        self.assertEqual(state(band),1)
        for index,wrong in ((0,0),(1,1),(2,1),(3,0),(4,15),(5,1),(6,2),(7,1),(8,2),(9,1)):
            v=list(band);v[index]=wrong
            self.assertEqual(state(v),0,(index,wrong))
        camera=(1,.8,4/3,0,0)  # the fixture's fake projection; no camera: (0,...)
        # The live fade fixture's inputs at the identity rows: distance 1, .625 * (.75 - .125) = .390625 -> 390, refused at 500.
        self.assertEqual(self.fade_route(.625,1,(.75,.125),IDENTITY,camera),(1,1.,.390625,390,0))
        self.assertEqual(self.fade_route(.625,1,(.75,.125),IDENTITY,camera,390),(1,1.,.390625,390,1))
        # The routed script: alpha 1, fog clip (1, 0) -> 1; the arm off (1001) admits nothing.
        self.assertEqual(self.fade_route(1,1,(1,0),IDENTITY,camera),(1,1.,1.,1000,1))
        self.assertEqual(self.fade_route(1,1,(1,0),IDENTITY,camera,1001),(1,1.,1.,1000,0))
        self.assertEqual(self.fade_route(1,1,(1,0),IDENTITY,camera,0)[4],1)
        # Fog off: the alpha value alone; a threshold equal to the estimate admits, one above refuses.
        ok,d,f,permille,admit=self.fade_route(.7,0,(.75,.125),IDENTITY,camera,700)
        self.assertEqual((ok,d,permille,admit),(1,1.,700,1));self.assertAlmostEqual(f,.7,places=6)
        self.assertEqual(self.fade_route(.7,0,(.75,.125),IDENTITY,camera,701)[4],0)
        self.assertEqual(self.fade_route(.75,0,(.5,.5),IDENTITY,camera,750),(1,1.,.75,750,1))
        # Origin off axis: clip (3, 4, ., 2) with m00 1.5 and m11 2 -> view (2, 2, 2), distance sqrt(12); without a camera the depth w = 2.
        rows=[1,0,0,3, 0,1,0,4, 0,0,1,0, 0,0,0,2]
        ok,d,f,_,_=self.fade_route(1,1,(1,.1),rows,(1,1.5,2,0,0))
        self.assertEqual(ok,1);self.assertAlmostEqual(d,math.sqrt(12),places=5);self.assertAlmostEqual(f,1-.1*math.sqrt(12),places=5)
        # Without a valid camera the depth w alone would understate the distance (2 < sqrt(12)) and
        # overstate the fraction: refused (the bracket keeps the draw). A degenerate projection likewise.
        self.assertEqual(self.fade_route(1,1,(1,.1),rows,(0,0,0,0,0))[0],0)
        self.assertEqual(self.fade_route(1,1,(1,.1),rows,(1,0,2,0,0))[0],0)
        # An off-centre projection (m20, m21) cancels: x_c = w * m20 is the axis.
        rows=[1,0,0,1, 0,1,0,-.5, 0,0,1,0, 0,0,0,2]
        self.assertEqual(self.fade_route(1,1,(1,.25),rows,(1,1,1,.5,-.25))[1:3],(2.,.5))
        # Behind the camera plane or at it (run 130: a station module the camera has entered), the
        # origin is at its Euclidean distance, sign of w regardless: w -1 is distance 1 like w 1,
        # w 0 with the translation column (0, 0) sits at the camera (distance 0). Nonfinite refuses.
        for w in (0,-1,'nan','inf'):
            rows=list(IDENTITY);rows[15]=w
            self.assertEqual(self.fade_route(1,1,(1,0),rows,camera)[0],int(w in (0,-1)),w)
        rows=list(IDENTITY);rows[15]=-1
        self.assertEqual(self.fade_route(.625,1,(.75,.125),rows,camera),(1,1.,.390625,390,0))
        off_axis=[1,0,0,3, 0,1,0,4, 0,0,1,0, 0,0,0,2]
        behind=off_axis[:15]+[-2]
        self.assertEqual(self.fade_route(1,1,(1,.1),behind,(1,1.5,2,0,0))[1],self.fade_route(1,1,(1,.1),off_axis,(1,1.5,2,0,0))[1])
        rows=list(IDENTITY);rows[15]=0
        self.assertEqual(self.fade_route(.5,1,(.75,.125),rows,camera)[1:4],(0.,.375,375))
        self.assertEqual(self.fade_route('nan',1,(1,0),IDENTITY,camera)[2:],(0.,0,0))
        self.assertEqual(self.fade_route(1,1,('inf',0),IDENTITY,camera)[2:],(0.,0,0))
        self.assertEqual(self.fade_route(1,1,(.5,1),IDENTITY,camera)[2:],(0.,0,0))     # .5 - 1 < 0 -> 0
        self.assertEqual(self.fade_route(.5,1,(4,1),IDENTITY,camera)[2:],(.5,500,1))   # saturate(3) = 1
        self.assertEqual(self.fade_route(2,1,(1,0),IDENTITY,camera)[3:],(1000,1))      # alpha above one clamps the permille

    def test_fade_route_table_nine_rows(self):
        """The register table (fade_route_core.h vertex_programs, static_assert 9): the seven distance-fade programs
        and the two run214 station families (fade-rt2-ownership.md section 3), every hash once."""
        lines=subprocess.check_output([str(self.driver),'--fade-route-table'],text=True).splitlines()
        self.assertEqual(fields(lines[0])['rows'],'9')
        rows=[fields(l) for l in lines[1:]]
        self.assertEqual(len(rows),9);self.assertEqual(len({r['vs'] for r in rows}),9)
        self.assertEqual({r['vs']:(int(r['alpha']),int(r['fog'])) for r in rows if r['vs'] in ('494fe349b8bc12ec','53a0a641107ed76c')},
                         {'494fe349b8bc12ec':(39,41),'53a0a641107ed76c':(39,41)})
        core=(ROOT/'src/proxy/fade_route_core.h').read_text()
        self.assertIn('static_assert(vertex_program_count == 9,',core)

    def test_arm_pair_identity(self):
        """fade_route::arm_pair: a registers row always; without the owner also a distance_fade_rows pair."""
        arm=lambda row,fade,owner:int(fields(subprocess.check_output([str(self.driver),'--fade-route-arm-pair',str(row),str(fade),str(owner)],text=True).strip())['arm'])
        self.assertEqual({(r,f,o):arm(r,f,o) for r in (0,1) for f in (0,1) for o in (0,1)},
                         {(0,0,0):0,(0,0,1):0,(0,1,0):0,(0,1,1):0,(1,0,0):0,(1,0,1):1,(1,1,0):1,(1,1,1):1})

    def hysteresis(self,threshold,steps,evicted=False):
        """--fade-route-hysteresis: (admit, held, entries[, evicted]) per (key, frame, permille) step."""
        args=[str(self.driver),'--fade-route-hysteresis',str(threshold)]+[f'{k}:{f}:{p}' for k,f,p in steps]
        rows=[fields(l) for l in subprocess.check_output(args,text=True).splitlines()]
        return [(int(r['admit']),int(r['held']),int(r['entries']))+((int(r['evicted']),) if evicted else ()) for r in rows]

    def test_fade_band_arm_hysteresis_eviction_counter(self):
        """Hysteresis::evicted (the fade_route_frame line's fade_evicted): 0 until the 65th key, then one per new key on
        a full table; a key already in the table never evicts. The route drains the count into its frame counter per draw."""
        steps=[(k,1,600) for k in range(1,65)]+[(65,1,600),(66,2,600),(66,3,600),(3,3,600)]
        result=self.hysteresis(500,steps,evicted=True)
        self.assertEqual([r[3] for r in result[:64]],[0]*64)
        # 65 and 66 each evict the first entry of the oldest frame (a full table), 66 again and key 3 are hits.
        self.assertEqual([r[3] for r in result[64:]],[1,2,2,2])
        self.assertEqual({r[2] for r in result[64:]},{64})
        motion=(ROOT/'src/proxy/motion_output.cpp').read_text()
        self.assertIn('if (fade_hysteresis_.evicted) { counters_.fade_evicted += fade_hysteresis_.evicted; fade_hysteresis_.evicted = 0; }',motion)
        self.assertIn('fade_evicted=%lu fade_owner=%u fade_owner_masked=%lu',motion)

    def test_fade_band_arm_hysteresis(self):
        """Hysteresis at the threshold (fade_route_core.h Hysteresis): a node admitted at >= threshold stays
        admitted down to threshold - 100 while it is seen within 8 frames; a refusal, a gap or an eviction
        starts it again at the threshold; key 0 is the threshold alone."""
        # Hover: 520 arms, 450 held twice, 380 refuses and disarms, 450 refused, 520 arms again.
        self.assertEqual(self.hysteresis(500,[(7,0,520),(7,1,450),(7,2,450),(7,3,380),(7,4,450),(7,5,520),(7,6,499)]),
                         [(1,0,1),(1,1,1),(1,1,1),(0,0,1),(0,0,1),(1,0,1),(1,1,1)])
        # The band edge: 400 is held at threshold 500, 399 is not; threshold 50 holds down to 0; threshold 0 admits everything.
        self.assertEqual(self.hysteresis(500,[(1,0,500),(1,1,400),(1,2,399)]),[(1,0,1),(1,1,1),(0,0,1)])
        self.assertEqual(self.hysteresis(50,[(1,0,50),(1,1,0)]),[(1,0,1),(1,1,1)])
        self.assertEqual(self.hysteresis(0,[(1,0,0),(2,1,0)]),[(1,0,1),(1,0,2)])
        # Off (1001): never admitted, nothing stored. Key 0: threshold alone, nothing stored.
        self.assertEqual(self.hysteresis(1001,[(1,0,1000),(1,1,1000)]),[(0,0,0),(0,0,0)])
        self.assertEqual(self.hysteresis(500,[(0,0,600),(0,1,450)]),[(1,0,0),(0,0,0)])
        # Expiry: seen 8 frames later still held, 9 frames later not; a frame going backwards (Reset) not.
        self.assertEqual(self.hysteresis(500,[(1,0,600),(1,8,450)]),[(1,0,1),(1,1,1)])
        self.assertEqual(self.hysteresis(500,[(1,0,600),(1,9,450)]),[(1,0,1),(0,0,1)])
        self.assertEqual(self.hysteresis(500,[(1,5,600),(1,4,450)]),[(1,0,1),(0,0,1)])
        # Keys are independent; a full table evicts its oldest entry, which then starts at the threshold.
        self.assertEqual(self.hysteresis(500,[(1,0,600),(2,0,450),(1,1,450),(2,1,600),(2,2,450)]),[(1,0,1),(0,0,2),(1,1,2),(1,0,2),(1,1,2)])
        fill=[(k,1,600) for k in range(2,66)]  # key 1 armed at frame 0 is the oldest of 65 keys
        steps=[(1,0,600)]+fill+[(1,2,450)]
        result=self.hysteresis(500,steps)
        self.assertEqual(result[0],(1,0,1));self.assertEqual(result[-2][2],64);self.assertEqual(result[-1],(0,0,64))

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
        """Step B/D (locked_prefix_core.h): sentinel at lock, exact scan, tails, refusals, revision, eviction, binding."""
        out=subprocess.run([str(self.driver),'--prefix','20260914','300'],capture_output=True,text=True)
        self.assertEqual(out.returncode,0,out.stdout[-500:])
        rows={l.split()[1]:fields(l) for l in out.stdout.splitlines() if l.startswith('PREFIX ')}
        resolved={l.split()[1]:fields(l) for l in out.stdout.splitlines() if l.startswith('RESOLVE ')}
        summary=fields(next(l for l in out.stdout.splitlines() if l.startswith('PREFIX_RANDOM ')))
        # Vertex i at (i, -i, 2i), 200 written behind the sentinel: exact count, exact extent per draw.
        def box(lo,hi):return '%.6f,%.6f,%.6f,%.6f,%.6f,%.6f'%((lo+hi)/2,-(lo+hi)/2,lo+hi,(hi-lo)/2,(hi-lo)/2,hi-lo)
        self.assertEqual((rows['math_96']['name'],rows['math_96']['scanned'],rows['math_96']['box'],rows['math_96']['extra']),('bound','200',box(0,95),'200'))
        self.assertEqual((rows['math_200']['box'],rows['math_1']['box']),(box(0,199),'0.000000,-0.000000,0.000000,0.000000,0.000000,0.000000'),'a draw takes exactly its own vertices: no stale superset')
        self.assertEqual(rows['math_pending']['extra'],'1','the whole window is sentinelled at the first lock of a marked buffer')
        self.assertEqual(rows['relock_pending']['extra'],'4800','the next lock sentinels the previous prefix (200 vertices x 24 bytes)')
        self.assertEqual((rows['unmarked_lock_ignored']['name'],rows['unmarked_lock_ignored']['used'],rows['unmarked_lock_ignored']['extra']),('unknown','0','1'),'unmarked buffers are never recorded, sentinelled or scanned')
        self.assertEqual((rows['marked_unknown']['name'],rows['marked_unknown']['used']),('unknown','1'),'a mark alone is no bound: the first draw is refused')
        self.assertEqual([(rows[l]['name'],rows[l]['revision']) for l in ('nested_first_unlock','nested_invalid','nested_relearned')],[('invalid','2'),('invalid','2'),('bound','3')])
        for label,name in (('math_unknown','unknown'),('math_pending','pending'),('empty','empty'),('beyond','beyond'),('beyond_max','beyond'),
                           ('relock_pending','pending'),('unlock_failed','invalid'),('non_discard','invalid'),('thread_mismatch','invalid'),
                           ('relearned','bound'),('erased','unknown'),('tail_sentinel_100','bound'),('tail_sentinel_101','beyond'),
                           ('tail_nan_100','bound'),('tail_nan_101','nonfinite'),('tail_inf_100','bound'),('tail_inf_106','nonfinite'),
                           ('tail_absurd_100','bound'),('tail_absurd_106','nonfinite'),('tail_limit_106','bound'),('tail_huge_100','bound'),('tail_huge_106','bound'),
                           ('tail_zero_100','bound'),('prefix_nan_5','bound'),('prefix_nan_6','nonfinite'),('prefix_nan_96','nonfinite'),
                           ('sentinel_vertex_50','bound'),('sentinel_vertex_51','beyond'),('evicted_oldest','unknown'),('evicted_kept','bound'),('cleared','unknown')):
            self.assertEqual(rows[label]['name'],name,label)
        self.assertEqual([rows[l]['revision'] for l in ('math_pending','relock_pending','non_discard','thread_mismatch','relearned')],['1','2','3','4','5'],'every lock advances the revision')
        # Scan counts: exact behind a sentinel tail, the window (300) over a garbage tail; window-end scans counted.
        self.assertEqual((rows['tail_sentinel_100']['extra'],rows['tail_nan_100']['extra'],rows['tail_zero_100']['extra'],rows['sentinel_vertex_50']['extra']),('100','300','300','50'))
        self.assertEqual((rows['tail_sentinel_100']['window_end'],rows['tail_zero_100']['window_end']),('0','6'))
        self.assertEqual((rows['tail_huge_100']['box'],rows['tail_huge_106']['box']),('0.000000,0.000000,0.000000,1.000000,1.000000,1.000000','499999.500000,499999.500000,499999.500000,500000.500000,500000.500000,500000.500000'),'garbage enters only a draw that covers it')
        self.assertEqual((rows['sentinel_extent_after_50']['extra'],rows['sentinel_extent_after_100']['extra']),('1200','2400'),'the sentinel extent follows the previous scan')
        self.assertEqual((rows['length_partial']['scanned'],rows['length_partial']['extra']),('10','1'),'partial trailing vertex ignored')
        self.assertEqual((rows['length_capped']['scanned'],rows['length_capped']['extra']),('6144','1'),'scan capped at 6144 vertices')
        self.assertEqual((rows['evicted_oldest']['used'],rows['evicted_oldest']['evictions'],rows['evicted_oldest']['extra']),('16','1','16'),'17th buffer evicts the oldest record; 16 pooled storage blocks')
        self.assertEqual((rows['cleared']['used'],rows['cleared']['extra'],rows['erased']['extra']),('0','16','1'),'erase and clear keep the pooled storage')
        # resolve_locked_prefix maps lookup outcomes onto the region statuses and hands the positions out.
        self.assertEqual((resolved['bound']['name'],resolved['bound']['source'],resolved['bound']['scanned'],resolved['bound']['positions'],resolved['bound']['box']),('bound','1','200','1',box(0,199)))
        for label,name,refusal in (('no_vb','no_scope','0'),('zero_count','no_scope','0'),('no_binding','content_unknown','0'),('unknown_buffer','content_unknown','1'),('beyond','invalid','5')):
            self.assertEqual((resolved[label]['name'],resolved[label]['bound'],resolved[label]['refusal'],resolved[label]['positions']),(name,'0',refusal,'0'),label)
        # Random exact-prefix property: bit-identical positions, exact count behind the sentinel, the window over garbage.
        self.assertEqual((summary['cases'],summary['failures'],summary['exact'],summary['garbage']),('300','0','150','150'))
        self.assertEqual((summary['vertices_1056'],summary['vertices_window']),('1056','6144'))
        self.assertLess(float(summary['scan_ns_1056']),float(summary['scan_ns_window']),'the sentinel scan costs less than the whole window')
        self.assertLess(float(summary['scan_ns_window']),2e6,'a 6144-vertex scan stays well under a millisecond on the host')


class FadeRouteStartupLine(unittest.TestCase):
    """Grammar of the startup `fade_route_mode` line (capture.cpp) against the arm's real
    prerequisites (docs/architecture/linear-distance-fade-region.md, "Fade-band route"):
    the arm runs under original shading, so `enabled` is threshold + TAA + HDR and the
    material request is reported beside it. The line is unconditional: the default-on
    threshold (500) must leave startup evidence even with X3M_FADE_ROUTE unset."""
    SOURCE=(ROOT/'src/proxy/capture.cpp').read_text()
    START=SOURCE.rindex('X3M_FADE_ROUTE=<permille>')  # the configuration block, not the variable's declaration comment
    BLOCK=SOURCE[START:SOURCE.index('X3M_SCREEN_EMISSION=1',START)]

    def test_line_is_unconditional_with_the_arms_true_predicate(self):
        line=self.BLOCK[self.BLOCK.index('log("fade_route_mode'):]
        line=line[:line.index(';')]
        self.assertEqual(re.findall(r'(\w+)=(?:%\w+|\S+)',line.split('"')[1]),
                         ['threshold','hysteresis','enabled','taa','hdr','linear_materials','source'])
        # enabled: the threshold, TAA and the FP16 scene; never the material route.
        self.assertIn('fade_route_threshold<=1000u&&taa_requested&&hdr_requested',line.replace(' ','').replace('\n',''))
        self.assertIn('linear_material_requested',line)
        self.assertNotIn('linear_material_requested&&',line.replace(' ',''))
        self.assertIn('fade_route_from_env?"env":"default"',line.replace(' ',''))
        # Emitted whatever the environment holds: no `if(...)` guard around the log call.
        statements=self.BLOCK[:self.BLOCK.index('log("fade_route_mode')]
        self.assertEqual(statements.rstrip()[-1],'}','the parse block is closed and no if/else guards the log')
        self.assertIn('fade_route_threshold=500;',statements.replace(' ',''))

    def test_hysteresis_field_reports_the_production_band(self):
        self.assertIn('unsigned(x3m::fade_route::Hysteresis::band)',self.BLOCK.replace(' ',''))
        core=(ROOT/'src/proxy/fade_route_core.h').read_text()
        self.assertIn('band = 100u',core)

    def test_source_field_distinguishes_default_from_env(self):
        # `off` and an in-range per mille come from the environment; an unparsable
        # or out-of-range value keeps 500 and stays source=default.
        parse=self.BLOCK[self.BLOCK.index('fade_route_from_env=false;'):self.BLOCK.index('log("fade_route_mode')].replace(' ','')
        self.assertIn('if(!wcscmp(setting,L"off")){fade_route_threshold=x3m::fade_route::threshold_off;fade_route_from_env=true;}',parse)
        self.assertIn('if(digits&&n<=1000ul){fade_route_threshold=unsigned(n);fade_route_from_env=true;}',parse)
        self.assertEqual(parse.count('fade_route_from_env=true;'),2)


# ---- X3M_FADE_RT2_OWNER (docs/architecture/fade-rt2-ownership.md) ----------------------------------------------------
IDENTITY_SOURCES=['src/renderer/shader_population.cpp','src/renderer/linear_material.cpp','src/renderer/linear_emission.cpp',
                  'src/renderer/linear_emission_sm1.cpp','src/renderer/rigid_position.cpp','src/renderer/material_radiance.cpp',
                  'src/renderer/material_motion.cpp']
IDENTITY_HARNESS=r"""
#include "fade_route_core.h"
#include "linear_distance_fade.h"
#include "material_motion.h"
#include <cstdio>
#include <cstdlib>
using namespace x3m;
int main(int argc,char**argv){
    for(int i=1;i+1<argc;i+=2){const unsigned long long vs=std::strtoull(argv[i],nullptr,16),ps=std::strtoull(argv[i+1],nullptr,16);
        fade_route::Registers r{};const bool row=fade_route::registers(vs,r),fade=renderer::linear_distance_fade_pair(vs,ps);
        std::printf("PAIR vs=%016llx ps=%016llx reviewed=%u registers=%u distance_fade=%u sampler_mask=%u arm_off=%u arm_on=%u\n",vs,ps,
            unsigned(renderer::material_motion_pair_reviewed(vs,ps)),unsigned(row),unsigned(fade),unsigned(renderer::linear_distance_fade_sampler_mask(vs,ps)),
            unsigned(fade_route::arm_pair(row,fade,false)),unsigned(fade_route::arm_pair(row,fade,true)));}
    return 0;}
"""


class FadeOwnerIdentity(unittest.TestCase):
    """The arm's pair identity with the owner off and on, against the real distance_fade_rows (the bracket's identity,
    linear_material.cpp) and the reviewed motion table (gate 3): the seven fade pairs are fade pairs either way; the run214
    station families (a registers row, no distance_fade_rows row) only with the owner, the bracket's sampler mask staying 0;
    the glass and damage programs never (the overlay path)."""
    SEVEN=[('b0602757fce6e870','517540ae6d5e5410'),('0c223ad11bce02d5','7a0c3388065bb08d'),('233d17d26ce0c1fc','7a0c3388065bb08d'),
           ('167eb2d5629ab9d3','d44db87778a43b61'),('330ceb9dd874ede2','550c2a4d4d3ed70f'),('12b8a13f13fe8cfe','550c2a4d4d3ed70f'),
           ('4944d81dfe531b37','64bac8bb307eb896')]
    RUN214=[('494fe349b8bc12ec','fffdabd910793aba'),('53a0a641107ed76c','8759c7838bbc86c2'),('4944d81dfe531b37','ca6bfa4a6cca7e2a'),
            ('53a0a641107ed76c','63f96eba9eea7880')]
    OVERLAY=[('c30104cb0efb6675','a66fb1981ba755b2'),('37c34a7478544c14','5f82ecacd39529cd')]

    @classmethod
    def setUpClass(cls):
        compiler=shutil.which('c++') or shutil.which('clang++') or shutil.which('g++')
        if compiler is None:raise unittest.SkipTest('no host C++ compiler')
        cls.temp=tempfile.TemporaryDirectory(prefix='x3-fade-owner-identity-');cls.addClassCleanup(cls.temp.cleanup)
        source=Path(cls.temp.name)/'identity.cpp';source.write_text(IDENTITY_HARNESS)
        cls.exe=Path(cls.temp.name)/'identity'
        subprocess.run([compiler,'-std=c++17','-O1','-Wall','-Wextra','-Werror','-I',str(ROOT/'src/renderer'),'-I',str(ROOT/'src/proxy'),str(source)]
                       +[str(ROOT/s) for s in IDENTITY_SOURCES]+['-o',str(cls.exe)],check=True)
        pairs=cls.SEVEN+cls.RUN214+cls.OVERLAY
        lines=subprocess.check_output([str(cls.exe)]+[h for pair in pairs for h in pair],text=True).splitlines()
        cls.rows={(f['vs'],f['ps']):f for f in map(fields,lines)}

    def test_seven_fade_pairs_are_fade_pairs_either_way(self):
        for pair in self.SEVEN:
            r=self.rows[pair]
            self.assertEqual((r['reviewed'],r['registers'],r['distance_fade'],r['arm_off'],r['arm_on']),('1','1','1','1','1'),pair)
            self.assertNotEqual(r['sampler_mask'],'0',pair)

    def test_run214_families_only_with_the_owner_and_the_bracket_admits_nothing_new(self):
        for pair in self.RUN214:
            r=self.rows[pair]
            self.assertEqual((r['reviewed'],r['registers'],r['distance_fade'],r['sampler_mask'],r['arm_off'],r['arm_on']),('1','1','0','0','0','1'),pair)

    def test_glass_and_damage_keep_the_overlay_path(self):
        for pair in self.OVERLAY:
            r=self.rows[pair]
            self.assertEqual((r['reviewed'],r['registers'],r['distance_fade'],r['sampler_mask'],r['arm_off'],r['arm_on']),('1','0','0','0','0','0'),pair)

    def test_route_uses_the_identity_function(self):
        motion=(ROOT/'src/proxy/motion_output.cpp').read_text().replace(' ','').replace('\n','')
        self.assertIn('&&fade_route::arm_pair(fade_route::registers(shadow_.vs_hash,shadow_.fade_route_registers),'
                      'renderer::linear_distance_fade_pair(shadow_.vs_hash,shadow_.ps_hash),fade_rt2_owner_&&!linear_material_requested_);',motion)
        # The widening is original shading only (the overlay arm's boundary); the bracket keeps its own identity (the sampler
        # mask), with no owner term.
        self.assertIn('renderer::linear_distance_fade_sampler_mask(shadow_.vs_hash,shadow_.ps_hash):0;',motion)


def load_manage():
    spec=importlib.util.spec_from_file_location('fade_owner_manage',ROOT/'tools/manage.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module


class FadeOwnerLaunch(unittest.TestCase):
    """--fade-rt2-owner on|off: off by default and forwarded only when given (an inherited value dropped), requires
    --taa, on also --motion-output --hdr (the arm's prerequisites), refused under --vanilla. No game, no Wine."""
    TAA=['--motion-output','--ownership','--object-trace','--object-lifetime','--taa','--hdr']

    def launch(self,directory,*args,inherited=None):
        module=load_manage()
        game=Path(directory)/'game';game.mkdir(exist_ok=True)
        (game/'X3AP.exe').touch();(game/'d3d9.dll').write_bytes(b'fixture')
        (game/'x3-modern-install.json').write_text(json.dumps({'sha256':hashlib.sha256(b'fixture').hexdigest()}))
        wine=Path(directory)/'wine';wine.touch()
        argv=['manage.py','launch','--dry-run','--game-dir',str(game),*args]
        output,error=io.StringIO(),io.StringIO()
        with mock.patch.object(sys,'argv',argv),mock.patch.object(module,'WINE',wine),mock.patch.object(module,'VOICE_DECODER_REPO',None), \
                mock.patch.dict(module.os.environ,inherited or {}), \
                mock.patch.object(module.subprocess,'call',side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output),contextlib.redirect_stderr(error):
            try:module.main()
            except SystemExit as exit_error:return exit_error.code,output.getvalue(),error.getvalue()
        return 0,output.getvalue(),error.getvalue()

    def env(self,directory,*args,inherited=None):
        code,output,error=self.launch(directory,*args,inherited=inherited)
        self.assertEqual(code,0,error)
        return json.loads(output)['env']

    def test_default_off_not_forwarded_and_inherited_dropped(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertNotIn('X3M_FADE_RT2_OWNER',self.env(directory,*self.TAA))
            self.assertNotIn('X3M_FADE_RT2_OWNER',self.env(directory,*self.TAA,inherited={'X3M_FADE_RT2_OWNER':'on'}))

    def test_on_and_off_are_forwarded(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(self.env(directory,*self.TAA,'--fade-rt2-owner','on')['X3M_FADE_RT2_OWNER'],'on')
            self.assertEqual(self.env(directory,*self.TAA,'--fade-rt2-owner','off')['X3M_FADE_RT2_OWNER'],'off')

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('1','yes','owner'):
                code,_,error=self.launch(directory,*self.TAA,'--fade-rt2-owner',value)
                self.assertNotEqual(code,0,value);self.assertIn('--fade-rt2-owner',error)
            code,_,error=self.launch(directory,'--motion-output','--fade-rt2-owner','off')
            self.assertNotEqual(code,0);self.assertIn('--fade-rt2-owner requires --taa',error)
            code,_,error=self.launch(directory,*self.TAA[:-1],'--fade-rt2-owner','on')  # no --hdr
            self.assertNotEqual(code,0);self.assertIn('--fade-rt2-owner on requires --motion-output --hdr',error)
            code,_,error=self.launch(directory,'--vanilla','--fade-rt2-owner','off')
            self.assertNotEqual(code,0);self.assertIn('--fade-rt2-owner cannot be combined with --vanilla',error)


class FadeOwnerSource(unittest.TestCase):
    """The DLL side: off by default, the transformer switched only on the arm's prerequisites, RT2 masked on fade-arm rows
    unless the row is an owner (both binding modes), the owner lanes only in c218.y/.z, the owner program recorded."""
    def test_parse_and_configuration(self):
        capture=(ROOT/'src/proxy/capture.cpp').read_text()
        self.assertIn('bool fade_rt2_owner = false;',capture)
        self.assertIn('GetEnvironmentVariableW(L"X3M_FADE_RT2_OWNER",setting,32)',capture)
        self.assertIn('const bool enabled=fade_rt2_owner&&motion_output_requested&&taa_requested&&hdr_requested&&fade_route_threshold<=1000u;',capture)
        self.assertIn('if(enabled)renderer::material_motion_configure_fade_owner(true);',capture)
        self.assertIn('hooked.motion_output.configure_fade_rt2_owner(fade_rt2_owner,enabled);',capture)

    def test_mask_upload_and_lane(self):
        motion=(ROOT/'src/proxy/motion_output.cpp').read_text()
        self.assertIn('D3DRS_COLORWRITEENABLE2, route.fade_arm && !route.fade_owner ? 0 : 15); }',motion)
        self.assertIn('const DWORD wanted = route.fade_arm && !route.fade_owner ? 0 : 15;',motion)
        self.assertNotIn('route.fade_arm ? 0 : 15',motion)
        # Routed is owner on the fade arm only (the overlay arm never sets it); the arm reads no ZWRITEENABLE setter.
        self.assertIn('route.fade_arm = true; route.fade_held = held; route.fade_owner = fade_rt2_owner_;',motion)
        self.assertIn('route.fade_permille = 1000u; route.fade_arm = true; route.overlay = true;',motion)
        arm=motion.split('bool MotionOutput::fade_arm_admits')[1].split('std::uint64_t MotionOutput::fade_identity')[0]
        self.assertNotIn('SetRenderStateFn>(SetRenderState, D3DRS_ZWRITEENABLE',arm)
        # The lane RT2: an owner binds the invalid-share twin (.g = -1) or stays masked.
        self.assertIn('if (ps == shadow_.ps_variant && shadow_.ps_sun_motion) ps = shadow_.ps_sun_motion;',motion)
        self.assertIn('else { route.fade_owner = false; ++counters_.fade_owner_masked; }',motion)
        self.assertIn('if (fade_rt2_owner_) { pixel_thin[9] = route.fade_owner ? 1.f : 0.f; pixel_thin[10] = thin_vote_upload_ || route.fade_owner ? 0.f : 1.f; }',motion)

    def test_transformer_and_program(self):
        transformer=(ROOT/'src/renderer/material_motion.cpp').read_text()
        self.assertIn('return depth && (material_motion_thin_vote() || material_motion_fade_owner()) ? 12u : 18u;',transformer)
        self.assertIn('if ((thin || owner) && !pack_motion_definitions(row, constants, body)) return MaterialMotionResult::ProfileMismatch;',transformer)
        self.assertIn('(kind == DepthFragment::Owner ? outputs == 3 && output_lanes == 15 && constants == 3',transformer)
        record=json.loads((ROOT/'verification/results/current-depth-owner-pixel-program.json').read_text())
        self.assertEqual(record['target'],'ps_3_0')
        header=(ROOT/'src/renderer/current_depth_owner_pixel_program_inc.h').read_bytes()
        self.assertEqual(hashlib.sha256(header).hexdigest(),record['header_sha256'])
        hlsl=(ROOT/'src/temporal/current_depth_owner_ps.hlsl').read_text()
        self.assertIn('return float4((clip.x / clip.y).xx, clip.y, max(clip.y * lane.z + lane.x, lane.y));',hlsl)


if __name__=='__main__':unittest.main()
