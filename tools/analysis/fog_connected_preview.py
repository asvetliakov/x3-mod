#!/usr/bin/env python3
"""Small reference-only preview of fixed paired-lobe connected fog regions."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import time

import numpy as np


HERE=Path(__file__).resolve().parent
SPEC=importlib.util.spec_from_file_location("fog_distance_replay",HERE/"fog_distance_replay.py")
fog=importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(fog)
F=np.float32; P=fog.PERIOD; CELL=5*P; MASK32=0xffffffff
AXES=np.array([1.5*P,P,.75*P],np.float64); LOBE_BOUND=2.175*P
WIDTH,HEIGHT=128,72; SUN=np.array([1.,0.,0.],F)


def digest(path: Path) -> str:
    h=hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda:stream.read(1<<20),b""): h.update(block)
    return h.hexdigest()


def mix32(value: int) -> int:
    value &= MASK32; value ^= value>>16; value=(value*0x7feb352d)&MASK32
    value ^= value>>15; value=(value*0x846ca68b)&MASK32
    return (value^(value>>16))&MASK32


def rotations(angles: np.ndarray) -> np.ndarray:
    x,y,z=angles; cx,sx=math.cos(x),math.sin(x); cy,sy=math.cos(y),math.sin(y); cz,sz=math.cos(z),math.sin(z)
    rx=np.array([[1,0,0],[0,cx,-sx],[0,sx,cx]],np.float64)
    ry=np.array([[cy,0,sy],[0,1,0],[-sy,0,cy]],np.float64)
    rz=np.array([[cz,-sz,0],[sz,cz,0],[0,0,1]],np.float64)
    return rz@ry@rx


def region(cell: tuple[int,int,int]) -> dict:
    i,j,k=(value&MASK32 for value in cell)
    h=mix32((i*0x9e3779b9)^(j*0x85ebca6b)^(k*0xc2b2ae35)^0x58434647)
    hashes=[mix32(h+(((axis+1)*0x9e3779b9)&MASK32)) for axis in range(3)]
    jitter=np.array([((value&0xffff)-32768)*(P/2)/32768 for value in hashes])
    center=CELL*(np.asarray(cell,np.float64)+.5)+jitter
    angles=2*math.pi*(np.array([(value>>16)+.5 for value in hashes])/65536)
    Q=rotations(angles); local_x=Q[:,0]
    lobes=[center-.5*P*local_x,center+.5*P*local_x]
    return {"cell":cell,"center":center,"Q":Q,"angles":angles,"lobes":lobes,"hash":h,"axis_hashes":hashes}


def nearby_regions(origin: np.ndarray) -> list[dict]:
    base=np.floor(np.asarray(origin,np.float64)/CELL).astype(np.int64); rows=[]
    for dz in range(-2,3):
        for dy in range(-2,3):
            for dx in range(-2,3):
                row=region(tuple((base+[dx,dy,dz]).tolist()))
                if np.linalg.norm(row["center"]-origin)-2.675*P <= fog.FAR: rows.append(row)
    return rows


def lobe_envelope(points: np.ndarray, descriptor: dict, center: np.ndarray) -> np.ndarray:
    local=(np.asarray(points,np.float64)-center)@descriptor["Q"]
    y=local/AXES; q=np.linalg.norm(y,axis=-1); n=np.divide(y,q[...,None],out=np.zeros_like(y),where=q[...,None]>0)
    nx,ny,nz=n[...,0],n[...,1],n[...,2]
    h=1+.20*(2*nx*ny)+.15*((3*nz*nz-1)/2)+.10*(3*math.sqrt(3)*nx*ny*nz)
    e=1-fog.smoothstep(.75*h,h,q); e=np.where(q==0,1,e)
    return np.asarray(e,F)


def union_envelope(points: np.ndarray, regions: list[dict]) -> tuple[np.ndarray,np.ndarray]:
    maximum=np.zeros(np.asarray(points).shape[:-1],F); active=np.zeros_like(maximum,np.int16)
    for descriptor in regions:
        for center in descriptor["lobes"]:
            value=lobe_envelope(points,descriptor,center); maximum=np.maximum(maximum,value); active += value>0
    return maximum,active


def ray_sphere(origin,direction,center,limit):
    offset=np.asarray(origin,np.float64)-center; b=np.sum(np.asarray(direction,np.float64)*offset,axis=1)
    disc=b*b-(np.dot(offset,offset)-LOBE_BOUND*LOBE_BOUND); root=np.sqrt(np.maximum(disc,0))
    start=np.maximum(-b-root,0); end=np.minimum.reduce((-b+root,np.asarray(limit,np.float64),np.full(len(limit),fog.FAR)))
    return start,end,(disc>=0)&(end>start)


def merged_intervals(origin,direction,limit,regions):
    per_ray=[[] for _ in range(len(direction))]; lobe_hits=np.zeros(len(direction),np.int16); region_hits=np.zeros(len(direction),np.int16)
    simultaneous=np.zeros(len(direction),np.int16)
    for descriptor in regions:
        region_hit=np.zeros(len(direction),bool)
        for center in descriptor["lobes"]:
            start,end,hit=ray_sphere(origin,direction,center,limit); lobe_hits += hit; region_hit |= hit
            for index in np.flatnonzero(hit): per_ray[index].append((float(start[index]),float(end[index])))
        region_hits += region_hit
    merged=[]
    for ray,rows in enumerate(per_ray):
        rows.sort(); output=[]; events=[]
        for start,end in rows: events.extend(((start,1),(end,-1)))
        count=peak=0
        for _,delta in sorted(events,key=lambda item:(item[0],-item[1])): count+=delta; peak=max(peak,count)
        simultaneous[ray]=peak
        for start,end in rows:
            if output and start <= output[-1][1]: output[-1]=(output[-1][0],max(output[-1][1],end))
            else: output.append((start,end))
        merged.append(output)
    return merged,{"candidate_lobes":lobe_hits,"candidate_regions":region_hits,"simultaneous_bound_overlaps":simultaneous}


def support_counts(intervals,origin,direction,regions,spacing,chunk=64):
    envelope_evaluations=np.zeros(len(direction),np.int32); true_support=np.zeros(len(direction),np.int32)
    max_parts=max((len(rows) for rows in intervals),default=0)
    for part in range(max_parts):
        selected=np.array([index for index,rows in enumerate(intervals) if len(rows)>part],np.int32)
        for first in range(0,len(selected),chunk):
            idx=selected[first:first+chunk]; starts=np.array([intervals[i][part][0] for i in idx]); ends=np.array([intervals[i][part][1] for i in idx])
            length=ends-starts; counts=np.maximum(1,np.ceil(length/spacing).astype(np.int32)); count=int(counts.max()); envelope_evaluations[idx]+=counts
            row=np.arange(count)[:,None]; ds0=length/counts; active=row<counts[None,:]
            distance=starts[None,:]+(row+.5)*ds0[None,:]; points=origin+direction[idx][None,:,:]*distance[...,None]
            e,_=union_envelope(points,regions); true_support[idx]+=((e>0)&active).sum(axis=0)
    return envelope_evaluations,true_support


def integrate_union(volume,origin,direction,limit,sigma,spacing,sun,regions=None,start_clip=0.,end_clip=fog.FAR,chunk=64,collect=False):
    regions=nearby_regions(origin) if regions is None else regions
    intervals,bounds=merged_intervals(origin,direction,np.minimum(limit,end_clip),regions)
    intervals=[[(max(a,start_clip),min(b,end_clip)) for a,b in rows if min(b,end_clip)>max(a,start_clip)] for rows in intervals]
    max_parts=max((len(rows) for rows in intervals),default=0); S=np.zeros((len(direction),3),F); T=np.ones(len(direction),F)
    merged_length=np.array([sum(b-a for a,b in rows) for rows in intervals],np.float64)
    samples=np.zeros(len(direction),np.int32); true_samples=np.zeros(len(direction),np.int32); true_length=np.zeros(len(direction),np.float64)
    max_active=np.zeros(len(direction),np.int16)
    for part in range(max_parts):
        selected=np.array([index for index,rows in enumerate(intervals) if len(rows)>part],np.int32)
        for first in range(0,len(selected),chunk):
            idx=selected[first:first+chunk]; starts=np.array([intervals[i][part][0] for i in idx]); ends=np.array([intervals[i][part][1] for i in idx])
            length=ends-starts; counts=np.maximum(1,np.ceil(length/spacing).astype(np.int32)); count=int(counts.max()); samples[idx]+=counts
            row=np.arange(count)[:,None]; ds0=length/counts; active=row<counts[None,:]; ds=np.broadcast_to(ds0,(count,len(idx))).copy(); ds[~active]=0
            distance=starts[None,:]+(row+.5)*ds0[None,:]; points=origin+direction[idx][None,:,:]*distance[...,None]
            e,envelope_count=union_envelope(points,regions); e[~active]=0; envelope_count[~active]=0
            max_active[idx]=np.maximum(max_active[idx],envelope_count.max(axis=0)); supported=e>0
            true_samples[idx]+=supported.sum(axis=0); true_length[idx]+=np.sum(ds*supported,axis=0)
            rgba=np.zeros(points.shape[:-1]+(4,),F)
            if supported.any(): rgba[supported]=fog.sample_level(volume,points[supported])*e[supported,None]
            segment_S,segment_T,_=fog.integrate_samples(rgba,ds.astype(F),distance,sigma,direction[idx],True,sun)
            S[idx]+=T[idx,None]*segment_S; T[idx]*=segment_T
    result={"S":S,"T":T,"tau":(-np.log(np.maximum(T,np.finfo(F).tiny))).astype(F)}
    if collect:
        eval256,true256=support_counts(intervals,origin,direction,regions,256.)
        result["cost"]={**bounds,"merged_bound_length":merged_length.astype(F),"reference_samples":samples,
                        "true_support_samples":true_samples,"true_support_length":true_length.astype(F),
                        "max_active_envelopes":max_active,"regions_considered":len(regions),
                        "implied_256_envelope_evaluations":eval256,"implied_256_true_support_samples":true256}
    return result


def camera_rays(origin,forward,up,width=WIDTH,height=HEIGHT):
    forward=np.asarray(forward,np.float64); forward/=np.linalg.norm(forward); up=np.asarray(up,np.float64); up/=np.linalg.norm(up)
    right=np.cross(up,forward); right/=np.linalg.norm(right); true_up=np.cross(forward,right)
    x,y=np.meshgrid(np.arange(width),np.arange(height)); u=(x.ravel()+.5)/width; v=(y.ravel()+.5)/height
    tan30=math.tan(math.radians(30)); local=np.stack(((2*u-1)*(width/height)*tan30,(1-2*v)*tan30,np.ones_like(u)),axis=-1)
    direction=local[:,0,None]*right+local[:,1,None]*true_up+local[:,2,None]*forward
    direction/=np.linalg.norm(direction,axis=1)[:,None]; return direction.astype(F)


def poses():
    zero=region((0,0,0)); C,Q=zero["center"],zero["Q"]; forward=Q[:,1]; up=Q[:,2]
    return [{"name":"inside-core","origin":C,"forward":forward,"up":up},
            {"name":"30km","origin":C+150000*forward,"forward":-forward,"up":up},
            {"name":"38km","origin":C+190000*forward,"forward":-forward,"up":up}]


def metric(values):
    a=np.asarray(values,np.float64).ravel()
    return {"count":int(a.size),"mean":float(a.mean()) if a.size else 0.,"p50":float(np.percentile(a,50)) if a.size else 0.,
            "p95":float(np.percentile(a,95)) if a.size else 0.,"p99":float(np.percentile(a,99)) if a.size else 0.,"max":float(a.max(initial=0))}


def convergence(a,b): return {"T":metric(np.abs(a["T"]-b["T"])),"S_normalized_unit_radiance":[metric(np.abs(a["S"][:,c]-b["S"][:,c])) for c in range(3)]}
def convergence_pass(row): return row["T"]["p99"]<=.00025 and row["T"]["max"]<=.00075


def cost_row(cost):
    implied={"256":{"envelope_evaluations":metric(cost["implied_256_envelope_evaluations"]),
                    "true_support_samples":metric(cost["implied_256_true_support_samples"]),
                    "atlas_reads_two_per_true_support_sample":metric(cost["implied_256_true_support_samples"]*2)},
             "128":{"envelope_evaluations":metric(cost["reference_samples"]),"true_support_samples":metric(cost["true_support_samples"]),
                    "atlas_reads_two_per_true_support_sample":metric(cost["true_support_samples"]*2)}}
    return {"candidate_regions":metric(cost["candidate_regions"]),"candidate_lobes":metric(cost["candidate_lobes"]),
            "simultaneous_bound_overlaps":metric(cost["simultaneous_bound_overlaps"]),"max_active_envelopes":metric(cost["max_active_envelopes"]),
            "merged_conservative_interval_length":metric(cost["merged_bound_length"]),"true_support_length_at_reference128":metric(cost["true_support_length"]),
            "true_support_samples_at_reference128":metric(cost["true_support_samples"]),"implied_work":implied,"regions_considered":cost["regions_considered"],
            "cell_candidates_examined_per_view":125,"lighting_shadow_reads_excluded":True,"GPU_or_FPS_claim":False}


def _rgb(values,shape,maximum,signed=False):
    image=values.reshape(shape+(3,)); image=.5+image/(2*maximum) if signed else image/maximum
    return (np.clip(image,0,1)**(1/2.2)*255+.5).astype(np.uint8)


def _scalar(values,shape,maximum,signed=False):
    image=values.reshape(shape); image=.5+image/(2*maximum) if signed else image/maximum
    image=(np.clip(image,0,1)*255+.5).astype(np.uint8); return np.repeat(image[...,None],3,axis=-1)


def write_view(output,stem,control,ref64,ref128,shell,shape=(HEIGHT,WIDTH)):
    from PIL import Image,ImageDraw
    columns=[("current 2.4km",control["S"],1-control["T"],control["T"]),
             ("connected 40km",ref64["S"],1-ref64["T"],ref64["T"]),
             ("30-40km shell",shell["S"],1-shell["T"],shell["T"])]
    panels=[[_rgb(S,shape,.03),_scalar(opacity,shape,.4),_scalar(T,shape,1.)] for _,S,opacity,T in columns]
    panels.append([_rgb(ref64["S"]-ref128["S"],shape,.002,True),_scalar(ref64["T"]-ref128["T"],shape,.00075,True),
                   _scalar(np.abs(ref64["T"]-ref128["T"]),shape,.00075)])
    labels=[row[0] for row in columns]+["ref64-ref128"]; scale=5; h,w=shape; top,left=18,82
    sheet=Image.new("RGB",(left+4*w*scale,top+3*h*scale),"black"); draw=ImageDraw.Draw(sheet)
    for col,(label,group) in enumerate(zip(labels,panels)):
        draw.text((left+col*w*scale+2,3),label,fill="white")
        for row,panel in enumerate(group): sheet.paste(Image.fromarray(panel).resize((w*scale,h*scale),Image.Resampling.NEAREST),(left+col*w*scale,top+row*h*scale))
    for row,label in enumerate(("S / .03","opacity / .4","T / 1 or |dT|/.00075")): draw.text((2,top+row*h*scale+3),label,fill="white")
    path=output/f"{stem}-cloud-only.png"; sheet.save(path); return path.name


def write_slice(output,family,volume):
    from PIL import Image,ImageDraw
    descriptor=region((0,0,0)); axis=(-3*P)+(np.arange(128)+.5)*(6*P/128); yy,xx=np.meshgrid(axis,axis,indexing="ij")
    local=np.stack((xx,yy,np.zeros_like(xx)),axis=-1); points=descriptor["center"]+local@descriptor["Q"].T
    regions=nearby_regions(descriptor["center"]); E,_=union_envelope(points,regions); density=fog.sample_level(volume,points)[...,3]*E
    support=(np.clip(E,0,1)*255+.5).astype(np.uint8); detail=(np.clip(density,0,1)*255+.5).astype(np.uint8)
    image=np.concatenate((np.repeat(support[...,None],3,axis=-1),np.repeat(detail[...,None],3,axis=-1)),axis=1)
    sheet=Image.new("RGB",(256*4,18+128*4),"black"); draw=ImageDraw.Draw(sheet); draw.text((2,3),"union envelope E / 1",fill="white"); draw.text((514,3),"E * original density / 1",fill="white")
    sheet.paste(Image.fromarray(image).resize((256*4,128*4),Image.Resampling.NEAREST),(0,18))
    path=output/f"{family}-localXY-support-detail.png"; sheet.save(path)
    return path.name,{"grid":[128,128],"local_bounds_render_units":[-3*P,3*P],"envelope":metric(E),"modulated_density":metric(density),
                      "support_fraction":float(np.mean(E>0)),"core_fraction":float(np.mean(E==1))}


def composite_over_source(source,transport):
    result=np.asarray(source,F).copy(); result[...,:3]=transport["S"]+transport["T"][...,None]*result[...,:3]; return result


def laws():
    descriptor=region((0,0,0)); Q=descriptor["Q"]; midpoint=descriptor["center"][None,:]
    e=[float(lobe_envelope(midpoint,descriptor,center)[0]) for center in descriptor["lobes"]]
    volume=np.empty((8,8,8,4),F); volume[...,3]=.5; volume[...,:3]=.5*np.array([.2,.5,.8],F)
    direction=np.array([Q[:,1],-Q[:,1]],F)
    vacuum=integrate_union(volume,descriptor["center"],direction,np.array([0.,-1.],F),4e-6,512.,SUN,[descriptor])
    nonempty=integrate_union(volume,descriptor["center"],direction[:1],np.array([20000.],F),4e-6,512.,SUN,[descriptor])
    source=np.array([[.2,.3,.4,.17],[.7,.6,.5,.83]],F)
    return {"orientation_orthonormal":bool(np.allclose(Q.T@Q,np.eye(3),rtol=0,atol=2e-15)),
            "orientation_right_handed":bool(abs(np.linalg.det(Q)-1)<2e-15),
            "paired_midpoint_both_full_strength":bool(e==[1.,1.]),
            "union_uses_max_not_sum":bool(float(union_envelope(midpoint,[descriptor])[0][0])==1.),
            "zero_and_negative_depth_operator_identity_exact":bool(np.array_equal(vacuum["S"],np.zeros_like(vacuum["S"])) and np.array_equal(vacuum["T"],np.ones_like(vacuum["T"]))),
            "nonempty_operator_transport_finite_bounded":bool(np.isfinite(nonempty["S"]).all() and np.all((nonempty["T"]>=0)&(nonempty["T"]<=1)) and float(nonempty["T"][0])<1),
            "operator_composite_source_alpha_preserved_exact":bool(np.array_equal(composite_over_source(source,vacuum)[:,3],source[:,3]))}


def sampled_bound_intersections(origin,direction,limit,regions):
    visible_regions=0; visible_lobes=0
    for descriptor in regions:
        region_hit=np.zeros(len(direction),bool)
        for center in descriptor["lobes"]:
            _,_,hit=ray_sphere(origin,direction,center,limit)
            if hit.any(): visible_lobes+=1; region_hit |= hit
        visible_regions+=int(region_hit.any())
    return visible_regions,visible_lobes


def endpoint_inventory(meta,depth_path):
    origin,direction,limit,geometry=fog.ray_set(meta,depth_path,64,36); regions=nearby_regions(origin)
    _,counts=merged_intervals(origin,direction,limit,regions); envelope,_=union_envelope(origin[None,:],regions)
    visible_regions,visible_lobes=sampled_bound_intersections(origin,direction,limit,regions)
    nearest=min(float(np.linalg.norm(row["center"]-origin)) for row in regions)
    return {"origin":origin.tolist(),"rays":len(direction),"sky_rays":int((~geometry).sum()),"geometry_rays":int(geometry.sum()),
            "camera_union_envelope":float(envelope[0]),"camera_inside_support":bool(envelope[0]>0),
            "nearest_region_center_distance_render_units":nearest,"nearest_region_center_distance_km":nearest/5000,
            "bound_regions_intersected_by_sampled_64x36_rays":visible_regions,"bound_lobes_intersected_by_sampled_64x36_rays":visible_lobes,
            "per_ray_candidate_regions":metric(counts["candidate_regions"]),"per_ray_candidate_lobes":metric(counts["candidate_lobes"]),
            "scope":"coverage inventory only; no density integration or preview image"}


def run(capture: Path,asset_data: Path,output: Path,design: Path):
    started=time.monotonic(); output.mkdir(parents=True,exist_ok=True)
    manifest_path=asset_data/"manifest.json"; manifest=json.loads(manifest_path.read_text()); profiles={row["name"]:row for row in manifest["profiles"]}
    logs=sorted(capture.glob("session-*.log"))
    if len(logs)!=1: raise ValueError("expected one capture log")
    metadata,metadata_sha=fog.load_metadata(logs[0]); endpoint_depth=[capture/f"depth_1_{frame}.rgba32f" for lo,hi,_ in fog.BURSTS for frame in (lo,hi)]
    sources={"analysis_sha256":digest(Path(__file__)),"distance_replay_sha256":digest(HERE/"fog_distance_replay.py"),
             "design_sha256":digest(design),"manifest_sha256":digest(manifest_path),"selected_capture_metadata_sha256":metadata_sha,
             "capture_log":str(logs[0]),"capture_log_bytes":logs[0].stat().st_size,
             "endpoint_depth_sha256":{path.name:digest(path) for path in endpoint_depth},"packets":{}}
    all_converged=True; family_rows={}; images=[]; law_rows=laws(); all_laws=all(law_rows.values())
    for lo,hi,family in fog.BURSTS:
        packet=asset_data/f"{family}.fogbin"; volume=fog.decode_packet(packet,manifest); sigma=float(profiles[family]["base_sigma"])*1.5
        sources["packets"][family]={"sha256":digest(packet),"decoded_sha256":profiles[family]["decoded_sha256"]}
        views=[]
        for pose in poses():
            origin=pose["origin"]; direction=camera_rays(origin,pose["forward"],pose["up"]); limit=np.full(len(direction),fog.FAR,F); regions=nearby_regions(origin)
            ref128=integrate_union(volume,origin,direction,limit,sigma,128.,SUN,regions,collect=True)
            ref64=integrate_union(volume,origin,direction,limit,sigma,64.,SUN,regions)
            shell=integrate_union(volume,origin,direction,limit,sigma,64.,SUN,regions,fog.WINDOW_START,fog.FAR)
            control=fog.near24(volume,origin,direction,limit,sigma,SUN)
            errors=convergence(ref128,ref64); converged=convergence_pass(errors); all_converged &= converged
            image=write_view(output,f"{family}-{pose['name']}",control,ref64,ref128,shell); images.append(image)
            total_envelope,_=union_envelope(origin[None,:],regions)
            views.append({"pose":pose["name"],"origin":origin.tolist(),"forward":pose["forward"].tolist(),"up":pose["up"].tolist(),
                          "camera_union_envelope":float(total_envelope[0]),"reference_128_vs_64":errors,"converged":converged,
                          "complete_opacity":metric(1-ref64["T"]),"complete_clear_below_0.002":float(np.mean((1-ref64["T"])<.002)),
                          "shell_30_40km_opacity":metric(1-shell["T"]),"shell_clear_below_0.002":float(np.mean((1-shell["T"])<.002)),
                          "cost":cost_row(ref128["cost"]),"image":image})
        slice_image,slice_metrics=write_slice(output,family,volume); images.append(slice_image)
        endpoints=[]
        for frame in (lo,hi): endpoints.append({"frame":frame,**endpoint_inventory(metadata[frame],capture/f"depth_1_{frame}.rgba32f")})
        family_rows[family]={"views":views,"slice":{"image":slice_image,**slice_metrics},"captured_endpoint_coverage":endpoints}
        del volume
    status="passed-connected-reference-preview" if all_converged and all_laws else "inconclusive-connected-reference-preview"
    image_paths=[output/name for name in sorted(images)]; result={"schema":1,"result":status,"appearance_selection":"pending parent/user review",
        "sources":sources,"contract":{"cell_width":CELL,"axes":AXES.tolist(),"lobe_offset":.5*P,"lobe_bound_radius":LOBE_BOUND,
        "support":"max of all paired irregular-lobe envelopes; original density and premultiplied RGB sampled once","underlayer":False,
        "reference_spacings":[128,64],"views":6,"image_grid":[WIDTH,HEIGHT],"strength":.03,"density_scale":1.5,
        "lighting":"synthetic +X sun, normalized unit radiance, g=.3","window":"1-smoothstep(150000,200000,s)"},
        "gates":{"reference_converged":all_converged,"laws_passed":all_laws,"laws":law_rows},"families":family_rows,
        "operation_scope":{"candidate_integrator":False,"GPU_or_FPS_claim":False,"tile_lists_or_repair":False,
        "atlas_reads_per_true_support_sample":2,"shadow_reads_excluded":True,"CPU_host_seconds":time.monotonic()-started},
        "images":{path.name:digest(path) for path in image_paths},"limitations":["Reference convergence and geometric support connectivity do not select appearance or a production integrator.",
        "Synthetic cloud-only unit-radiance images are not game composites or production-scaled lighting.",
        "Cost counts exclude lighting, shadows, repair, tile lists, state traffic and GPU timing.",
        "Captured endpoints are coverage inventories only; nearby absence is not a failure.",
        "No size, spacing, radius, harmonic, seed, density, strength or sample search was performed."],"host_seconds":time.monotonic()-started}
    report=output/"report.json"; report.write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+"\n")
    rows=[row for data in family_rows.values() for row in data["views"]]; worstp=max(row["reference_128_vs_64"]["T"]["p99"] for row in rows); worst=max(row["reference_128_vs_64"]["T"]["max"] for row in rows)
    clear=[row["complete_clear_below_0.002"] for row in rows]; maxreads=max(row["cost"]["implied_work"]["128"]["atlas_reads_two_per_true_support_sample"]["max"] for row in rows)
    convergence_sentence=(f"All six 128x72 cloud-only views pass 128/64 convergence; worst T p99/max is {worstp:.9g}/{worst:.9g} (gates .00025/.00075)." if all_converged else
                          f"At least one of six 128x72 cloud-only views fails 128/64 convergence; worst T p99/max is {worstp:.9g}/{worst:.9g} (gates .00025/.00075).")
    lines=["# Fixed paired-lobe connected-cloud reference preview","",f"Result: **{status}**. Appearance selection remains pending.","",
           f"{convergence_sentence} Complete-column clear fraction below .002 is {min(clear):.4f}..{max(clear):.4f}; this is descriptive, not acceptance.",
           f"The analytic reference census reaches {maxreads:.0f} implied atlas reads per ray at128 spacing before lighting, shadows or repair. This is not GPU timing or a candidate-integrator result.",
           "Sheets show the current2.4km control, complete40km connected-region reference,30-40km shell, and64/128 error at fixed scales. Two fixed localXY sheets separate macro support from fine-density holes.",
           "Run200 endpoints are coverage inventories only; no endpoint density replay or capture composite was produced.","",
           "Parent and user must judge broadness, visible connection, clear pockets, distant taper and repeated detail. No production choice or parameter iteration follows automatically.",
           f"Host runtime: {result['host_seconds']:.2f}s. No game, Wine, build, production change, install, or commit was performed.",""]
    (output/"report.md").write_text("\n".join(lines)); (output/"summary.json").write_text(json.dumps({"result":status,"report_sha256":digest(report),
        "analysis_sha256":sources["analysis_sha256"],"design_sha256":sources["design_sha256"],"images":len(result["images"])},indent=2,sort_keys=True)+"\n")
    return result


def main():
    parser=argparse.ArgumentParser(); parser.add_argument("--capture",type=Path,required=True); parser.add_argument("--asset-data",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True); parser.add_argument("--design",type=Path,default=Path("/tmp/x3-fog-connected-design.md"))
    args=parser.parse_args(); result=run(args.capture,args.asset_data,args.output,args.design)
    print(json.dumps({"result":result["result"],"output":str(args.output),"seconds":result["host_seconds"]},sort_keys=True)); return 0


if __name__=="__main__": raise SystemExit(main())
