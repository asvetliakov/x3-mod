"""Float32 production controls + invalid-depth twin around the frozen atlas sampler.

Caller explicitly imports the reviewed prototype helper; it supplies only sample,
volume extraction and metrics. No production source imports verification code.
"""
import numpy as np
F=np.float32

def classes(depth):
    geom=(depth[...,0]>=0)&(depth[...,0]<=1)
    invalid=geom&(~np.isfinite(depth[...,2])|(depth[...,2]<=0))
    return geom,invalid

def rays(depth,c,x,y):
    uvx=(np.asarray(x,F)+F(.5))/c[1,0];uvy=(np.asarray(y,F)+F(.5))/c[1,1]
    vx=(uvx*F(2)-F(1)-c[0,2])/c[0,0]
    vy=(F(1)-uvy*F(2)-c[0,3])/c[0,1]
    length=np.sqrt(vx*vx+vy*vy+F(1))
    world=np.stack([vx*c[i,0]+vy*c[i,1]+c[i,2] for i in (4,5,6)],-1)
    world/=np.sqrt(np.sum(world*world,axis=-1))[...,None]
    geom,invalid=classes(depth)
    z=np.where(invalid,F(0),depth[...,2])
    limit=np.where(geom,np.minimum(z*length,c[3,3]),c[3,3]).astype(F)
    return world,limit,invalid

def march(helper,volume,c,direction,limit,g=.3,radiance=(1,1,1),visibility=None):
    ds=limit/F(24);T=np.ones(limit.shape,F);S=np.zeros(limit.shape+(3,),F)
    cosine=direction[...,0]*c[3,0]+direction[...,1]*c[3,1]+direction[...,2]*c[3,2]
    g=F(g);a,b,n=(F(1.09),F(.6),F(.91)) if g==F(.3) else (F(1)+g*g,F(2)*g,F(1)-g*g)
    base=a-b*cosine;phase=(n/(F(4)*base*np.sqrt(base)))[...,None]*np.asarray(radiance,F)
    for k in range(24):
        rgba=helper.sample(volume,c[2,:3]+direction*(ds*F(k+.5))[...,None]);rho=rgba[...,3]
        opacity=F(1)-np.exp(-c[2,3]*rho*ds);chroma=rgba[...,:3]/np.maximum(rho[...,None],F(1e-8))
        incident=(T*opacity)[...,None]*phase*chroma
        if visibility is not None:incident*=visibility(direction*(ds*F(k+.5))[...,None])[...,None]
        S+=incident;T*=F(1)-opacity
    return np.concatenate([S,T[...,None]],-1)

def composite(helper,volume,c,depth,scene,half,g=.3,gamma=2.2,radiance=(1,1,1)):
    h,w=depth.shape[:2];hh,hw=half.shape[:2];geom,invalid=classes(depth);y,x=np.mgrid[:h,:w]
    total=np.zeros((h,w,4),F);weight=np.zeros((h,w),F);present=np.zeros((h,w),bool);nonempty=np.zeros((h,w),bool)
    fx=(x%2).astype(F)*F(.5);fy=(y%2).astype(F)*F(.5)
    for dy in (0,1):
        for dx in (0,1):
            qx=np.minimum(x//2+dx,hw-1);qy=np.minimum(y//2+dy,hh-1);hd=depth[2*qy,2*qx];hg,hi=classes(hd)
            selected=(geom==hg)&~invalid&~hi
            tap_weight=(fx if dx else F(1)-fx)*(fy if dy else F(1)-fy)
            both=selected&geom;relative=np.ones((h,w),F)
            relative[both]=np.exp(-np.abs(hd[...,2][both]-depth[...,2][both])/np.maximum(F(.05)*np.minimum(hd[...,2][both],depth[...,2][both]),F(1e-6)))+F(1e-4)
            tap_weight*=relative;tap_weight[~selected]=0;tap=half[qy,qx].astype(F).copy()
            active=tap_weight>0;present|=active;nonempty|=active&(np.any(tap[...,:3]!=0,axis=-1)|(tap[...,3]!=1))
            tap[...,3]=F(1)-tap[...,3];total+=tap_weight[...,None]*tap;weight+=tap_weight
    repair=(weight==0)&~invalid;st=np.divide(total,weight[...,None],out=np.zeros_like(total),where=weight[...,None]>0);st[...,3]=F(1)-st[...,3]
    if repair.any():
        direction,limit,_=rays(depth[repair],c,x[repair],y[repair]);st[repair]=march(helper,volume,c,direction,limit,g,radiance)
    empty=(np.all(st[...,:3]==0,axis=-1)&(st[...,3]==1))|invalid
    source=scene.astype(F);gamma=F(gamma)
    rgb=np.power(np.maximum(np.power(np.maximum(source[...,:3],F(0)),gamma)*st[...,3:4]+st[...,:3],F(0)),F(1/2.2) if gamma==F(2.2) else F(1)/gamma)
    out=np.concatenate([rgb,source[...,3:4]],-1).astype('<f2');out[empty]=scene[empty]
    return out,repair,empty,(present&~nonempty)|invalid

def numeric_groups(helper,gpu,reference,depth,repair=None):
    geom,invalid=classes(depth);boundary=np.zeros(geom.shape,bool)
    boundary[1:]|=geom[1:]!=geom[:-1];boundary[:-1]|=geom[1:]!=geom[:-1];boundary[:,1:]|=geom[:,1:]!=geom[:,:-1];boundary[:,:-1]|=geom[:,1:]!=geom[:,:-1]
    masks={'all':np.ones(geom.shape,bool),'geometry':geom,'sky':~geom,'boundary':boundary}
    if repair is not None:masks['repair']=repair
    result={}
    for name,mask in masks.items():
        if repair is None:
            error=np.abs(gpu.astype(F)-reference.astype(F));T=helper.metrics(error[...,3][mask]);S=[helper.metrics(error[...,i][mask]) for i in range(3)]
            passed=T['p99']<=.001 and T['max']<=.003 and all(m['p99']<=.0005 and m['max']<=.002 for m in S)
            result[name]=dict(count=int(mask.sum()),T=T,S=S,passed=bool(passed))
        else:
            error=helper.metrics(np.abs(gpu[...,:3].astype(F)[mask]-reference[...,:3].astype(F)[mask])/np.maximum(np.abs(reference[...,:3].astype(F)[mask]),F(.001)))
            result[name]=dict(count=int(mask.sum()),relative=error,passed=bool(error['p99']<=.005 and error['max']<=.02))
    return result
