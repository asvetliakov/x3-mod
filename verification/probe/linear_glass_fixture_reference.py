"""Six SM3 glass additions to the detached fixture's unchanged binary case ABI."""
from dataclasses import replace
import copy
import struct
import linear_material_reference as ref

START = 162
PAIRS = [('c30104cb0efb6675',p) for p in ('a66fb1981ba755b2','ebc9b2b3f1564e9a')]
PAIRS += [(v,p) for v in ('e2ad860d5fbb3e59','74fdc00d802b4027')
          for p in ('f31c9e2701c8eee4','9d49f288800f898d')]
FIXED = '74fdc00d802b4027'
# Derived complete-program sizes: local originals are external test inputs.
WORDS = {('vs','c30104cb0efb6675'):550, ('vs','e2ad860d5fbb3e59'):550,
         ('vs',FIXED):505, ('ps','a66fb1981ba755b2'):309,
         ('ps','ebc9b2b3f1564e9a'):341, ('ps','f31c9e2701c8eee4'):226,
         ('ps','9d49f288800f898d'):258}


def append_cases(cases):
    # New cases use their own defaults, never mutate earlier family's cases.
    template = copy.deepcopy(cases[0])
    template.update(pair=START, normal=[0.,0.,1.], camera=[.6,0.,.8],
                    flags=0, coefficients=[.125,.0625,.0625,.03125])
    def add(label,pair,**changes):
        case=copy.deepcopy(template)
        case.update(changes,id=len(cases),pair=pair,label='glass_'+label)
        cases.append(case)
    zero={key:[0.]*3 for key in ('point','material','dir0','dir1')}
    zero['cube']=[0.,0.,0.,1.]
    for pair,(vs,ps) in enumerate(PAIRS,START):
        for depth in (0,1):
            for reverse in (0,1): add('pair_depth_face',pair,depth=depth,reverse=reverse)
        add('missing_history',pair,valid=0)
        add('fog_alpha',pair,flags=2,camera=[0.,0.,4.])
        for lights in ((1,) if vs==FIXED else (0,1,8)):
            add('point_count',pair,lights=lights)
        for gain in ([0.,0.,0.],[4.,1.,1.],[1.,16.,1.],[1.,1.,16.]):
            add('independent_gains',pair,gains=gain)
        for term in ('point','material','dir0','dir1','cube'):
            changes=copy.deepcopy(zero)
            changes[term]=template[term]
            if term=='dir1':changes.update(normal=[0.,0.,-1.],camera=[.6,0.,-.8])
            add('isolated_'+term,pair,**changes)
        for mask in (0.,1.):
            add('mask',pair,mask=mask)
            add('cube_only_mask',pair,**dict(zero,cube=[.25,.5,.125,1.]),mask=mask)
        for camera in ([0.,0.,1.],[1.,0.,0.],[.6,0.,.8],[0.,.6,.8]):
            add('fresnel_cube',pair,**dict(zero,cube=[.25,.5,.125,1.]),camera=camera,mask=1.)
        for normal in ([0.,0.,.5],[0.,0.,1.25],[0.,0.,2.5]):
            add('nonunit_normal',pair,normal=normal,fp16=0)
        for diffuse in ([1.,0.,.25,.75],[0.,0.,0.,.75]):
            add('tinted_gloss',pair,**dict(zero,dir0=[1.,.5,.25]),diffuse=diffuse,mask=1.)
            add('untinted_cube',pair,**dict(zero,cube=[.25,.5,.125,1.]),diffuse=diffuse,mask=1.)
        for camera in ([.6,0.,.8],[-.6,.2,.8],[0.,.6,-.8]):
            add('cube_direction',pair,camera=camera,flags=1,fp16=0)
        for flags in (16,16|64,16|2048):
            for reverse in (0,1):
                add('interpolation',pair,flags=flags,reverse=reverse,fp16=0)
        add('over_one_point',pair,lights=1 if vs==FIXED else 8,point=[2.,1.,.5],material=[3.,2.,1.],fp16=0)
    return cases


def f32(value): return struct.unpack('<f',struct.pack('<f',value))[0]


def varying(c,sample=(8,8),*,linear=True,flat_color=False):
    vector=lambda key:tuple(map(f32,c[key]))
    vs=PAIRS[c['pair']-START][0]
    lights=[ref.PointLight((0,0,2),vector('point'),(2,.25,.125))]*(1 if vs==FIXED else c['lights'])
    def at(x,y):
        position=(0.,0.,0.);normal=vector('normal')
        if c['flags']&16:
            sx,sy,nx,ny=vector('coefficients')
            position=(f32(sx*x),f32(sy*y),0.)
            normal=(f32(normal[0]+f32(nx*x)),f32(normal[1]+f32(ny*y)),normal[2])
        return ref.glass_vertex(position,normal,vector('camera'),vector('material'),lights,
            fixed_single=vs==FIXED,material_alpha=.625,gains=ref.Gains(*vector('gains')),
            fog_clip=vector('fog_clip') if c['flags']&2 else None,linear=linear)
    if not c['flags']&16:return at(0,0)
    if c['flags']&2:raise ValueError('gradient fog not qualified')
    ws=(1.,2.,4.) if c['flags']&64 else (1.,1.,1.)
    vertices=[at(x*w,y*w) for (x,y),w in zip(((-1,1),(3,1),(-1,-3)),ws)]
    bx,by=sample[0]/32.,sample[1]/32.
    weights=[b/w for b,w in zip((1-bx-by,bx,by),ws)]
    weights=[w/sum(weights) for w in weights]
    blend=lambda key:tuple(sum(w*getattr(v,key)[i] for w,v in zip(weights,vertices)) for i in range(3))
    a=vertices[0]  # D3D first-vertex flat COLOR; winding swap preserves vertex 0.
    return replace(a,normal=blend('normal'),view=blend('view'),reflection=blend('reflection'),
        linear_rgb=a.linear_rgb if flat_color else blend('linear_rgb'),
        fresnel=a.fresnel if flat_color else sum(w*v.fresnel for w,v in zip(weights,vertices)))


def expected(c,half_source=False,sample=(8,8),*,linear=True,cube_sampler=None,flat_color=False):
    vector=lambda key:tuple(map(f32,c[key]))
    ps=PAIRS[c['pair']-START][1]
    directions=[ref.DirectionalLight((0,0,1),vector('dir0'))]
    if ref.GLASS_PROFILES[ps].directions==2:directions.append(ref.DirectionalLight((0,0,-1),vector('dir1')))
    return ref.glass_pixel(ps,varying(c,sample,linear=linear,flat_color=flat_color),
        vector('diffuse'),f32(c['mask']),cube_sampler if c['flags']&1 else lambda _:vector('cube')[:3],
        directions,face=-1 if c['reverse'] else 1,gains=ref.Gains(*vector('gains')),
        half_source=half_source,half_target=bool(c['fp16']),linear=linear)
