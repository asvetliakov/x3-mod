"""Float32 comparison-filtered spatial fog lighting twin; no game execution."""
import numpy as np
F=np.float32

def pcf(depth,position,bias):
    n=depth.shape[0]
    texel=(position[...,:2]*np.array([.5,-.5],F)+F(.5))*F(n)
    base=np.floor(texel).astype(np.int64);fraction=texel-base.astype(F)
    reference=position[...,2]-F(bias)
    taps=[]
    for y,x in ((0,0),(0,1),(1,0),(1,1)):
        taps.append((depth[np.clip(base[...,1]+y,0,n-1),np.clip(base[...,0]+x,0,n-1)]>=reference).astype(F))
    a=taps[0]+(taps[1]-taps[0])*fraction[...,0]
    b=taps[2]+(taps[3]-taps[2])*fraction[...,0]
    return a+(b-a)*fraction[...,1]

def weights(positions,valid):
    left=np.ones(positions[0].shape[:-1],F);out=[]
    for p,ok in zip(positions,valid):
        m=np.maximum(np.abs(p[...,0]),np.abs(p[...,1]))
        inside=(m<=F(.95))&(p[...,2]>=0)&(p[...,2]<=1)&ok
        w=left*inside*(F(1)-np.clip((m-F(.85))*F(10),0,1))
        out.append(w);left-=w
    return out

def visibility(view_position,cascades):
    if not cascades:return np.ones(view_position.shape[:-1],F)
    positions=[view_position@np.asarray(c['rows'],F)[:,:3].T+np.asarray(c['rows'],F)[:,3] for c in cascades]
    shade=np.zeros(view_position.shape[:-1],F)
    for c,p,w in zip(cascades,positions,weights(positions,[c['valid'] for c in cascades])):
        active=w>0
        if active.any():shade[active]+=w[active]*(F(1)-pcf(c['map'],p[active],c['bias']))
    return np.clip(F(1)-shade,0,1)

def world_visibility(constants,cascades):
    # Uploaded inverse columns map view to world. Invert exactly as the rigid
    # camera contract does; the callback receives a camera-relative world point.
    world_to_view=np.linalg.inv(constants[4:7,:3].T).astype(F)
    return lambda point:visibility(point@world_to_view,cascades)
