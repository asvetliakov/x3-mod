#!/usr/bin/env python3
"""One fixed offline preview of finite, disjoint world-space fog banks."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import time

import numpy as np


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("fog_distance_replay", HERE / "fog_distance_replay.py")
fog = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fog)

F = np.float32
P = fog.PERIOD
R = P / 2
CORE = 3 * R / 4
CELL = 3 * P
MASK32 = 0xffffffff
SUN = np.array([1., 0., 0.], F)
CANONICAL_SIZE = (256, 144)
PATH_SIZE = (64, 36)


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def mix32(value: int) -> int:
    value &= MASK32
    value ^= value >> 16; value = (value * 0x7feb352d) & MASK32
    value ^= value >> 15; value = (value * 0x846ca68b) & MASK32
    return (value ^ (value >> 16)) & MASK32


def bank_center(cell: tuple[int, int, int]) -> np.ndarray:
    i, j, k = (value & MASK32 for value in cell)
    h = mix32((i * 0x9e3779b9) ^ (j * 0x85ebca6b) ^ (k * 0xc2b2ae35) ^ 0x58434647)
    offsets = []
    for axis in range(3):
        ha = mix32(h + ((axis + 1) * 0x9e3779b9 & MASK32))
        offsets.append(((ha & 0xffff) - 32768) * (P / 4) / 32768)
    return CELL * (np.asarray(cell, np.float64) + .5) + np.asarray(offsets)


def envelope(distance: np.ndarray) -> np.ndarray:
    return (1 - fog.smoothstep(CORE, R, np.asarray(distance, np.float64))).astype(F)


def nearby_banks(origin: np.ndarray) -> list[dict]:
    base = np.floor(np.asarray(origin, np.float64) / CELL).astype(np.int64)
    rows = []
    for dz in range(-3, 4):
        for dy in range(-3, 4):
            for dx in range(-3, 4):
                cell = tuple((base + [dx, dy, dz]).tolist())
                center = bank_center(cell)
                distance = float(np.linalg.norm(center - origin))
                if distance - R <= fog.FAR:
                    rows.append({"cell": cell, "center": center, "distance": distance})
    rows.sort(key=lambda row: row["distance"])
    return rows


def ray_sphere(origin: np.ndarray, direction: np.ndarray, center: np.ndarray,
               limit: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    offset = np.asarray(origin, np.float64) - np.asarray(center, np.float64)
    b = np.sum(np.asarray(direction, np.float64) * offset, axis=1)
    discriminant = b*b - (np.dot(offset, offset) - R*R)
    root = np.sqrt(np.maximum(discriminant, 0))
    start = np.maximum(-b-root, 0)
    end = np.minimum.reduce((-b+root, np.asarray(limit, np.float64), np.full(len(limit), fog.FAR)))
    hit = (discriminant >= 0) & (end > start)
    return start, end, hit


def integrate_banks(volume: np.ndarray, origin: np.ndarray, direction: np.ndarray,
                    limit: np.ndarray, sigma: float, spacing: float, sun: np.ndarray,
                    banks=None, chunk: int = 64, collect_cost: bool = False) -> dict:
    banks = nearby_banks(origin) if banks is None else banks
    result_S = np.zeros((len(direction), 3), F); result_T = np.ones(len(direction), F)
    last_end = np.zeros(len(direction), np.float64); hit_count = np.zeros(len(direction), np.int32)
    sample_count = np.zeros(len(direction), np.int32); chord_total = np.zeros(len(direction), np.float64)
    bank_pixel_counts = []
    for bank in banks:
        start, end, hit = ray_sphere(origin, direction, bank["center"], limit)
        if not hit.any():
            bank_pixel_counts.append(0); continue
        if np.any(start[hit] + 1e-7 < last_end[hit]):
            raise ValueError("bank interval order violation")
        bank_pixel_counts.append(int(hit.sum())); last_end[hit] = end[hit]
        hit_count[hit] += 1; chord_total[hit] += end[hit] - start[hit]
        indices = np.flatnonzero(hit)
        for first in range(0, len(indices), chunk):
            selected = indices[first:first+chunk]
            length = end[selected] - start[selected]
            counts = np.maximum(1, np.ceil(length / spacing).astype(np.int32))
            sample_count[selected] += counts
            count = int(counts.max())
            row = np.arange(count)[:, None]
            ds0 = length / counts
            ds = np.broadcast_to(ds0, (count, len(selected))).copy()
            active = row < counts[None, :]
            ds[~active] = 0
            distance = start[selected][None, :] + (row + .5) * ds0[None, :]
            points = origin + direction[selected][None, :, :] * distance[..., None]
            rgba = fog.sample_level(volume, points)
            e = envelope(np.linalg.norm(points - bank["center"], axis=-1))
            rgba *= e[..., None]; rgba[~active] = 0
            segment_S, segment_T, _ = fog.integrate_samples(
                rgba, ds.astype(F), distance, sigma, direction[selected], True, sun)
            result_S[selected] += result_T[selected, None] * segment_S
            result_T[selected] *= segment_T
    result = {"S": result_S, "T": result_T,
              "tau": (-np.log(np.maximum(result_T, np.finfo(F).tiny))).astype(F)}
    if collect_cost:
        result["cost"] = {"candidate_spacing": spacing,
                          "sphere_hit_count": hit_count, "chord_length": chord_total.astype(F),
                          "sample_count": sample_count, "atlas_fetch_count": sample_count*2,
                          "bank_projected_pixel_counts": bank_pixel_counts,
                          "enumerated_banks": len(banks),
                          "visible_banks_on_raster": int(np.count_nonzero(bank_pixel_counts))}
    return result


def compose(front: dict, back: dict) -> dict[str, np.ndarray]:
    return {"S": front["S"] + front["T"][:, None] * back["S"],
            "T": front["T"] * back["T"]}


def composite_over_source(source_rgba: np.ndarray, transport: dict) -> np.ndarray:
    result=np.asarray(source_rgba,F).copy()
    result[...,:3]=transport["S"]+transport["T"][...,None]*result[...,:3]
    return result


def camera_rays(origin: np.ndarray, target: np.ndarray, width: int, height: int):
    forward = np.asarray(target, np.float64) - np.asarray(origin, np.float64)
    forward /= np.linalg.norm(forward)
    up = np.array([0., 1., 0.]); right = np.cross(up, forward); right /= np.linalg.norm(right)
    true_up = np.cross(forward, right)
    x, y = np.meshgrid(np.arange(width), np.arange(height))
    u = (x.ravel()+.5)/width; v = (y.ravel()+.5)/height
    tan30 = math.tan(math.radians(30)); aspect = width/height
    local = np.stack(((2*u-1)*aspect*tan30, (1-2*v)*tan30, np.ones_like(u)), axis=-1)
    direction = local[:, 0, None]*right + local[:, 1, None]*true_up + local[:, 2, None]*forward
    direction /= np.linalg.norm(direction, axis=1)[:, None]
    return direction.astype(F)


def canonical_poses() -> list[dict]:
    center = bank_center((0, 0, 0)); plus_z = np.array([0., 0., 1.])
    return [{"name":"center","origin":center,"target":center+plus_z},
            {"name":"core-edge","origin":center+plus_z*CORE,"target":center},
            {"name":"outside-1.05R","origin":center+plus_z*(1.05*R),"target":center},
            {"name":"10km","origin":center+plus_z*50000,"target":center},
            {"name":"30km","origin":center+plus_z*150000,"target":center},
            {"name":"38km","origin":center+plus_z*190000,"target":center}]


def metric(values: np.ndarray) -> dict[str, float | int]:
    a = np.asarray(values, np.float64).ravel()
    return {"count":int(a.size), "p50":float(np.percentile(a,50)) if a.size else 0.,
            "p95":float(np.percentile(a,95)) if a.size else 0.,
            "p99":float(np.percentile(a,99)) if a.size else 0., "max":float(a.max(initial=0))}


def error(candidate: dict, reference: dict, select=None) -> dict:
    select = np.ones(len(candidate["T"]), bool) if select is None else np.asarray(select, bool)
    return {"count":int(select.sum()),"T":metric(np.abs(candidate["T"][select]-reference["T"][select])),
            "S_normalized_unit_radiance":[metric(np.abs(candidate["S"][select,c]-reference["S"][select,c])) for c in range(3)]}


def numerical_pass(row: dict, convergence=False) -> bool:
    if convergence: return row["T"]["p99"] <= .00025 and row["T"]["max"] <= .00075
    return (row["T"]["p99"] <= .001 and row["T"]["max"] <= .003 and
            all(item["p99"] <= .0005 and item["max"] <= .002 for item in row["S_normalized_unit_radiance"]))


def cost_metrics(cost: dict) -> dict:
    return {"enumerated_banks":cost["enumerated_banks"],"visible_banks_on_raster":cost["visible_banks_on_raster"],
            "rays":len(cost["sample_count"]),"rays_hitting_any_bank":int(np.count_nonzero(cost["sphere_hit_count"])),
            "projected_occupancy_fraction":float(np.mean(cost["sphere_hit_count"]>0)),
            "sphere_hits":metric(cost["sphere_hit_count"]),"chord_render_units":metric(cost["chord_length"]),
            "samples":metric(cost["sample_count"]),"atlas_fetches":metric(cost["atlas_fetch_count"]),
            "bank_projected_pixel_counts":cost["bank_projected_pixel_counts"]}


def boundary_pixels(mask: np.ndarray, shape: tuple[int,int]) -> int:
    image=np.asarray(mask,bool).reshape(shape); boundary=np.zeros(shape,bool)
    boundary[:,1:] |= image[:,1:] != image[:,:-1]; boundary[:,:-1] |= image[:,1:] != image[:,:-1]
    boundary[1:,:] |= image[1:,:] != image[:-1,:]; boundary[:-1,:] |= image[1:,:] != image[:-1,:]
    return int(boundary.sum())


def _rgb(values, shape, maximum, signed=False):
    image=values.reshape(shape+(3,)); image=.5+image/(2*maximum) if signed else image/maximum
    return (np.clip(image,0,1)**(1/2.2)*255+.5).astype(np.uint8)


def _scalar(values, shape, maximum, signed=False):
    image=values.reshape(shape); image=.5+image/(2*maximum) if signed else image/maximum
    image=(np.clip(image,0,1)*255+.5).astype(np.uint8); return np.repeat(image[...,None],3,axis=-1)


def write_sheet(output: Path, stem: str, control: dict, candidate: dict, reference: dict,
                shape: tuple[int,int], scale=3) -> str:
    from PIL import Image, ImageDraw
    columns=[("current 2.4km",control["S"],1-control["T"]),("bank ref64",reference["S"],1-reference["T"]),
             ("bank candidate500",candidate["S"],1-candidate["T"])]
    panels=[[_rgb(S,shape,.03),_scalar(opacity,shape,.4),_scalar(1-opacity,shape,1.)] for _,S,opacity in columns]
    panels.append([_rgb(candidate["S"]-reference["S"],shape,.002,True),
                   _scalar(candidate["T"]-reference["T"],shape,.003,True),
                   _scalar(np.abs(candidate["T"]-reference["T"]),shape,.003)])
    labels=[row[0] for row in columns]+["candidate-reference"]
    h,w=shape; top,left=18,80; sheet=Image.new("RGB",(left+4*w*scale,top+3*h*scale),"black"); draw=ImageDraw.Draw(sheet)
    for col,(label,group) in enumerate(zip(labels,panels)):
        draw.text((left+col*w*scale+2,3),label,fill="white")
        for row,panel in enumerate(group):
            sheet.paste(Image.fromarray(panel).resize((w*scale,h*scale),Image.Resampling.NEAREST),(left+col*w*scale,top+row*h*scale))
    for row,label in enumerate(("S / .03","opacity / .4","T / 1 or |dT|/.003")): draw.text((2,top+row*h*scale+3),label,fill="white")
    path=output/f"{stem}-cloud-only.png"; sheet.save(path); return path.name


def current_control(volume, origin, direction, limit, sigma, sun):
    return fog.near24(volume,origin,direction,limit,sigma,sun)


def full_resolution_coverage(origin, target, banks) -> dict:
    width,height=1280,768; direction=camera_rays(origin,target,width,height); limit=np.full(len(direction),fog.FAR,F)
    hits=np.zeros(len(direction),bool); edge_union=np.zeros(len(direction),bool); visible=0
    for bank in banks:
        _,_,hit=ray_sphere(origin,direction,bank["center"],limit)
        if hit.any():
            visible+=1; hits|=hit
            edge=np.zeros((height,width),bool); image=hit.reshape(height,width)
            changed=image[:,1:]!=image[:,:-1]; edge[:,1:]|=changed; edge[:,:-1]|=changed
            changed=image[1:,:]!=image[:-1,:]; edge[1:,:]|=changed; edge[:-1,:]|=changed
            edge_union|=edge.ravel()
    return {"pixels":len(hits),"bank_pixels":int(hits.sum()),"projected_occupancy_fraction":float(hits.mean()),
            "visible_banks":visible,"support_edge_pixels_requiring_full_resolution_repair_if_marked":int(edge_union.sum()),
            "edge_population_definition":"union of each visible bank's projected support edge; retains edges overlapping another bank"}


def intersection_inventory(origin, direction, limit, banks) -> list[dict]:
    rows=[]
    for bank in banks:
        start,_,hit=ray_sphere(origin,direction,bank["center"],limit)
        if hit.any():
            minimum=float(start[hit].min()); median=float(np.median(start[hit]))
            rows.append({"cell":list(bank["cell"]),"camera_to_center_render_units":bank["distance"],
                         "camera_to_center_km":bank["distance"]/5000,"rays":int(hit.sum()),
                         "minimum_entry_distance_render_units":minimum,"minimum_entry_distance_km":minimum/5000,
                         "median_entry_distance_render_units":median,"median_entry_distance_km":median/5000})
    return rows


def endpoint_rays(meta, depth_path):
    return fog.ray_set(meta,depth_path,64,36)


def laws(volume: np.ndarray, sigma: float) -> dict:
    center=bank_center((0,0,0)); direction=np.array([[1.,0,0.],[0,1.,0.]],F)
    zero=integrate_banks(volume,center,direction,np.zeros(2,F),sigma,500.,SUN)
    outside=integrate_banks(volume,center+[0,0,200000+R+1],np.array([[0,0,-1]],F),np.array([fog.FAR],F),sigma,500.,SUN,
                            banks=[{"cell":(0,0,0),"center":center,"distance":200000+R+1}])
    nonempty=integrate_banks(volume,center,np.array([[1.,0,0.]],F),np.array([R],F),sigma,500.,SUN,
                             banks=[{"cell":(0,0,0),"center":center,"distance":0.}])
    source=np.array([[.1,.2,.3,.37]],F); composited=composite_over_source(source,nonempty)
    centres=[bank_center((i,j,k)) for i,j,k in ((0,0,0),(1,0,0),(0,1,0),(0,0,1),(-1,0,0))]
    minimum=min(np.linalg.norm(a-b) for index,a in enumerate(centres) for b in centres[index+1:])
    return {"envelope_core_exact_one":bool(np.array_equal(envelope(np.array([0.,CORE])),np.ones(2,F))),
            "envelope_outer_and_beyond_exact_zero":bool(np.array_equal(envelope(np.array([R,R+1])),np.zeros(2,F))),
            "nonempty_finite_bounded_transport":bool(np.isfinite(nonempty["S"]).all() and np.isfinite(nonempty["T"]).all() and
                                                     np.all((nonempty["T"]>=0)&(nonempty["T"]<=1))),
            "zero_or_invalid_depth_identity_exact":bool(np.array_equal(zero["S"],np.zeros_like(zero["S"])) and np.array_equal(zero["T"],np.ones_like(zero["T"]))),
            "source_alpha_preserved_exact":bool(np.array_equal(composited[:,3],source[:,3])),
            "bank0_beyond_40km_support_identity_exact":bool(np.array_equal(outside["S"],np.zeros_like(outside["S"])) and np.array_equal(outside["T"],np.ones_like(outside["T"]))),
            "sampled_centres_disjoint":bool(minimum>2*R),"sampled_minimum_centre_separation":float(minimum)}


def depth_provenance(capture: Path) -> dict:
    paths=[capture/f"depth_1_{frame}.rgba32f" for lo,hi,_ in fog.BURSTS for frame in range(lo,hi+1)]
    hashes={path.name:digest(path) for path in paths}; endpoints={f"depth_1_{frame}.rgba32f" for lo,hi,_ in fog.BURSTS for frame in (lo,hi)}
    aggregate=hashlib.sha256(b"".join(bytes.fromhex(hashes[path.name]) for path in paths)).hexdigest()
    return {"count":len(paths),"aggregate_sha256":aggregate,
            "endpoint_sha256":{name:hashes[name] for name in sorted(endpoints)}}


def captured_boundary(geometry: np.ndarray, limit: np.ndarray, shape=(36,64)) -> np.ndarray:
    geometry=np.asarray(geometry,bool).reshape(shape); depth=np.asarray(limit,F).reshape(shape); result=np.zeros(shape,bool)
    for dy,dx in ((0,1),(1,0)):
        a=(slice(None,-1),slice(None)) if dy else (slice(None),slice(None,-1))
        b=(slice(1,None),slice(None)) if dy else (slice(None),slice(1,None))
        transition=geometry[a]!=geometry[b]; both=geometry[a]&geometry[b]
        smaller=np.minimum(depth[a],depth[b]); jump=both&(smaller>0)&(np.abs(depth[a]-depth[b])>.05*smaller)
        edge=transition|jump; result[a]|=edge; result[b]|=edge
    return result.ravel()


def manual_composition(volume, origin, direction, limit, sigma, spacing, sun, banks):
    S=np.zeros((len(direction),3),F); T=np.ones(len(direction),F)
    for bank in banks:
        part=integrate_banks(volume,origin,direction,limit,sigma,spacing,sun,banks=[bank])
        S += T[:,None]*part["S"]; T *= part["T"]
    return {"S":S,"T":T}


def run(capture: Path, asset_data: Path, output: Path, architecture: Path) -> dict:
    started=time.monotonic(); output.mkdir(parents=True,exist_ok=True)
    manifest_path=asset_data/"manifest.json"; manifest=json.loads(manifest_path.read_text())
    log_paths=sorted(capture.glob("session-*.log"))
    if len(log_paths)!=1: raise ValueError("expected one capture log")
    metadata,metadata_sha=fog.load_metadata(log_paths[0]); profiles={row["name"]:row for row in manifest["profiles"]}
    sources={"analysis_sha256":digest(Path(__file__)),"distance_replay_sha256":digest(HERE/"fog_distance_replay.py"),
             "architecture_sha256":digest(architecture),"manifest_sha256":digest(manifest_path),
             "selected_capture_metadata_sha256":metadata_sha,"capture_log":str(log_paths[0]),
             "capture_log_bytes":log_paths[0].stat().st_size,"depth_files":depth_provenance(capture),"packets":{}}
    families={}; all_candidate_pass=True; all_reference_converged=True; all_laws=True; images=[]
    for lo,hi,family in fog.BURSTS:
        volume=fog.decode_packet(asset_data/f"{family}.fogbin",manifest); sigma=float(profiles[family]["base_sigma"])*1.5
        sources["packets"][family]={"sha256":digest(asset_data/f"{family}.fogbin"),"decoded_sha256":profiles[family]["decoded_sha256"]}
        family_laws=laws(volume,sigma); all_laws &= all(value for key,value in family_laws.items() if isinstance(value,bool))
        canonical=[]
        for pose in canonical_poses():
            origin=pose["origin"]; direction=camera_rays(origin,pose["target"],*CANONICAL_SIZE); limit=np.full(len(direction),fog.FAR,F)
            banks=nearby_banks(origin); ref128=integrate_banks(volume,origin,direction,limit,sigma,128.,SUN,banks)
            ref64=integrate_banks(volume,origin,direction,limit,sigma,64.,SUN,banks)
            candidate=integrate_banks(volume,origin,direction,limit,sigma,500.,SUN,banks,collect_cost=True)
            control=current_control(volume,origin,direction,limit,sigma,SUN)
            convergence=error(ref128,ref64); candidate_error=error(candidate,ref64)
            converged=numerical_pass(convergence,True); passed=numerical_pass(candidate_error)
            all_reference_converged &= converged; all_candidate_pass &= passed
            hit=candidate["cost"]["sphere_hit_count"]>0
            sheet=write_sheet(output,f"{family}-canonical-{pose['name']}",control,candidate,ref64,(CANONICAL_SIZE[1],CANONICAL_SIZE[0]))
            images.append(sheet)
            full=full_resolution_coverage(origin,pose["target"],banks)
            canonical.append({"pose":pose["name"],"origin":origin.tolist(),"target":pose["target"].tolist(),
                              "reference_128_vs_64":convergence,"reference_converged":converged,
                              "candidate500_error":candidate_error,"candidate_passed":passed,
                              "reference_clear_rays":int(np.count_nonzero(ref64["T"]==1)),
                              "candidate_exact_vacuum_rays":int(np.count_nonzero(candidate["T"]==1)),
                              "candidate_cost":cost_metrics(candidate["cost"]),"full_resolution_coverage":full,"image":sheet})
        # Fixed boundary traversal; every ray remains independent.
        center=bank_center((0,0,0)); path_rows=[]; previous_candidate=None; previous_reference=None
        delta_T=[]; delta_S=[[],[],[]]; path_candidate_pass=True; path_converged=True
        key_frames={0,32,64,96,128}
        for path_index,t in enumerate(np.linspace(.5,1.5,129)):
            origin=center+np.array([0.,0.,t*R]); direction=camera_rays(origin,center,*PATH_SIZE); limit=np.full(len(direction),fog.FAR,F)
            banks=nearby_banks(origin); ref128=integrate_banks(volume,origin,direction,limit,sigma,128.,SUN,banks)
            ref64=integrate_banks(volume,origin,direction,limit,sigma,64.,SUN,banks)
            candidate=integrate_banks(volume,origin,direction,limit,sigma,500.,SUN,banks,collect_cost=True)
            convergence=error(ref128,ref64); candidate_error=error(candidate,ref64)
            converged=numerical_pass(convergence,True); passed=numerical_pass(candidate_error)
            path_converged &= converged; path_candidate_pass &= passed
            if previous_candidate is not None:
                delta_T.append(np.abs((candidate["T"]-previous_candidate["T"])-(ref64["T"]-previous_reference["T"])))
                for channel in range(3):
                    delta_S[channel].append(np.abs((candidate["S"][:,channel]-previous_candidate["S"][:,channel])-
                                                    (ref64["S"][:,channel]-previous_reference["S"][:,channel])))
            image=None
            if path_index in key_frames:
                control=current_control(volume,origin,direction,limit,sigma,SUN)
                image=write_sheet(output,f"{family}-path-{path_index:03d}",control,candidate,ref64,(PATH_SIZE[1],PATH_SIZE[0]),5); images.append(image)
            path_rows.append({"index":path_index,"t_R":float(t),"origin":origin.tolist(),"reference_converged":converged,
                              "candidate_passed":passed,"candidate_error":candidate_error,"cost":cost_metrics(candidate["cost"]),"image":image})
            previous_candidate=candidate; previous_reference=ref64
        temporal_error={"count":128*PATH_SIZE[0]*PATH_SIZE[1],"T":metric(np.concatenate(delta_T)),
                        "S_normalized_unit_radiance":[metric(np.concatenate(rows)) for rows in delta_S]}
        temporal_pass=numerical_pass(temporal_error); all_candidate_pass &= path_candidate_pass and temporal_pass
        all_reference_converged &= path_converged
        # Captured endpoints are coverage/geometry witnesses, not pose constraints.
        endpoints=[]
        for frame in (lo,hi):
            origin,direction,limit,geometry=endpoint_rays(metadata[frame],capture/f"depth_1_{frame}.rgba32f")
            sun=fog.captured_sun(metadata[frame]); banks=nearby_banks(origin)
            ref128=integrate_banks(volume,origin,direction,limit,sigma,128.,sun,banks)
            ref64=integrate_banks(volume,origin,direction,limit,sigma,64.,sun,banks)
            candidate=integrate_banks(volume,origin,direction,limit,sigma,500.,sun,banks,collect_cost=True)
            control=current_control(volume,origin,direction,limit,sigma,sun); boundary=captured_boundary(geometry,limit)
            groups={"all":np.ones(len(limit),bool),"sky":~geometry,"geometry":geometry,"boundary":boundary}
            conv={name:error(ref128,ref64,select) for name,select in groups.items()}
            cand={name:error(candidate,ref64,select) for name,select in groups.items()}
            converged=all(numerical_pass(row,True) for row in conv.values()); passed=all(numerical_pass(row) for row in cand.values())
            all_reference_converged &= converged; all_candidate_pass &= passed
            image=write_sheet(output,f"{family}-captured-{frame}",control,candidate,ref64,(36,64),5); images.append(image)
            nearest=min(bank["distance"] for bank in banks); inside=any(bank["distance"]<R for bank in banks)
            endpoints.append({"frame":frame,"origin":origin.tolist(),"rays":len(limit),"groups":{k:int(v.sum()) for k,v in groups.items()},
                              "boundary_definition":"four-neighbor captured 64x36 geometry/sky transition or valid-geometry reconstructed radial-limit jump >5% of smaller radial limit",
                              "nearest_bank_center_distance":nearest,"camera_inside_bank":inside,
                              "reference_128_vs_64":conv,"candidate500_error":cand,"reference_converged":converged,
                              "candidate_passed":passed,"candidate_cost":cost_metrics(candidate["cost"]),"image":image})
        # Synthetic foreground clipping on canonical centre-view rays.
        pose=canonical_poses()[2]; origin=pose["origin"]; direction=camera_rays(origin,pose["target"],*PATH_SIZE); banks=nearby_banks(origin)
        x=np.tile(np.arange(PATH_SIZE[0]),PATH_SIZE[1]); one_column=np.full(len(direction),fog.FAR,F); one_column[x==PATH_SIZE[0]//2]=0
        step=np.where(x<PATH_SIZE[0]//2,400.,fog.FAR).astype(F); clipping={}
        for name,limits in (("one_column_invalid_depth",one_column),("fixed_foreground_depth_step",step)):
            result=integrate_banks(volume,origin,direction,limits,sigma,500.,SUN,banks)
            clipping[name]={"identity_rays":int(np.count_nonzero(limits==0)),
                            "zero_limit_identity_exact":bool(np.array_equal(result["S"][limits==0],np.zeros((np.sum(limits==0),3),F)) and
                                                             np.array_equal(result["T"][limits==0],np.ones(np.sum(limits==0),F))),
                            "fog_through_foreground_rays":int(np.count_nonzero((limits<=400)&(result["T"]<1)))}
        # Explicit ordered composition parity on four fixed rays.
        check_dirs=direction[[0,PATH_SIZE[0]-1,len(direction)//2,len(direction)-1]]; check_limit=np.full(4,fog.FAR,F)
        combined=integrate_banks(volume,origin,check_dirs,check_limit,sigma,500.,SUN,banks)
        manual=manual_composition(volume,origin,check_dirs,check_limit,sigma,500.,SUN,banks)
        composition_max=float(max(np.max(np.abs(combined["S"]-manual["S"])),np.max(np.abs(combined["T"]-manual["T"]))))
        # Camera-inside and near-surface cost stresses.
        stresses=[]
        for name,stress_origin in (("camera_inside",center),("near_surface",center+np.array([0.,0.,.99*R]))):
            stress_direction=camera_rays(stress_origin,center+np.array([0.,0.,1.]) if name=="camera_inside" else center,*CANONICAL_SIZE)
            stress=integrate_banks(volume,stress_origin,stress_direction,np.full(len(stress_direction),fog.FAR,F),sigma,500.,SUN,
                                   nearby_banks(stress_origin),collect_cost=True)
            stresses.append({"name":name,"cost":cost_metrics(stress["cost"])})
        all_costs=[row["candidate_cost"] for row in canonical]
        all_costs += [row["cost"] for row in path_rows]
        all_costs += [row["candidate_cost"] for row in endpoints]
        all_costs += [row["cost"] for row in stresses]
        observed_max_hits=max(row["sphere_hits"]["max"] for row in all_costs)
        observed_max_samples=max(row["samples"]["max"] for row in all_costs)
        required_laws={"ordered_composition_exact":composition_max==0,
                       "synthetic_zero_limit_identity":all(row["zero_limit_identity_exact"] for row in clipping.values()),
                       "synthetic_no_fog_through_foreground":all(row["fog_through_foreground_rays"]==0 for row in clipping.values()),
                       "observed_sphere_hit_bound_at_most_4":observed_max_hits<=4,
                       "observed_sample_bound_at_most_264":observed_max_samples<=264}
        all_laws &= all(required_laws.values())
        families[family]={"laws":family_laws,"required_experiment_laws":required_laws,"canonical":canonical,
                          "boundary_path":{"poses":129,"rows":path_rows,"reference_converged":path_converged,
                                           "candidate_passed_per_pose":path_candidate_pass,"temporal_change_error":temporal_error,
                                           "temporal_passed":temporal_pass},"captured_endpoints":endpoints,
                          "synthetic_geometry_clipping":clipping,"ordered_composition_max_delta":composition_max,
                          "cost_stresses":stresses,"observed_cost_bounds":{"max_sphere_hits":observed_max_hits,"max_samples":observed_max_samples}}
        del volume
    image_paths=sorted(output.glob("*.png")); image_hashes={path.name:digest(path) for path in image_paths}
    status="passed-500-unit-sampling" if all_candidate_pass and all_reference_converged and all_laws else "failed-500-unit-sampling"
    result={"schema":1,"result":status,"authored_medium_preview_completed":True,"sources":sources,
            "contract":{"period":P,"radius":R,"core_radius":CORE,"cell_width":CELL,"hash_seed":"0x58434647",
                        "envelope":"1-smoothstep(.75R,R,r), equal on density and premultiplied RGB","underlayer":False,
                        "atlas":"original family field at global world phase","strength":.03,"density_scale":1.5,
                        "candidate_spacing_max":500,"reference_spacings":[128,64],"window":"1-smoothstep(150000,200000,s)",
                        "canonical_image":[256,144],"boundary_path_image":[64,36],"lighting":"+X synthetic or captured dir1; normalized unit radiance"},
            "gates":{"reference_converged":all_reference_converged,"candidate500_passed":all_candidate_pass,"laws_passed":all_laws},
            "families":families,"cost_model":{"atlas_reads_per_sample":2,"shadow_reads_per_sample_when_valid_map":1,
              "offline_shadow_reads":0,"conservative_max_banks_per_40km_ray":4,"conservative_max_samples_per_ray":264,
              "conservative_max_atlas_reads_per_ray":528,"half_resolution_FP16_transport_target_bytes":1966080,
              "per_visible_bank_fullscreen_transport_read_write":True,"GPU_time_or_FPS_claim":False},
            "images":image_hashes,"limitations":["This is one fixed authored-medium preview; no radius, spacing, hash, anchor, strength, density, or sampling search was performed.",
              "A 500-unit numerical failure does not by itself reject the converged authored-medium reference images.",
              "Captured endpoint absence is expected outside banks and is not a pose-matching gate.",
              "Cloud-only normalized-light previews are not game composites or production-scaled lighting.",
              "CPU timings and read counts are not GPU FPS; production state, Reset, recovery, native Windows, TAA, and flight appearance remain open."],
            "host_seconds":time.monotonic()-started}
    report=output/"report.json"; report.write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+"\n")
    canonical_rows=[row for family in families.values() for row in family["canonical"]]
    occupancy=[row["candidate_cost"]["projected_occupancy_fraction"] for row in canonical_rows]
    visible=[row["candidate_cost"]["visible_banks_on_raster"] for row in canonical_rows]
    lines=["# Fixed finite world-space fog-bank preview","",f"500-unit sampling result: **{status}**. Authored-medium reference preview: **completed**.","",
           f"Twelve canonical 256x144 images cover both families and six frozen poses. Candidate projected bank occupancy is {min(occupancy):.4f}..{max(occupancy):.4f}; raster-visible banks are {min(visible)}..{max(visible)}. These are fixed witnesses, not visual acceptance.",
           f"Reference convergence: {all_reference_converged}; 500-unit numerical gates: {all_candidate_pass}; transport/support laws: {all_laws}. The report retains the 258-pose boundary paths, captured endpoint coverage, synthetic depth clips, costs, and fixed-scale images.",
           "Nearby fog absence outside a bank is expected. Bank interiors preserve the original family field scale and phase; no underlayer or density compensation was added.","",
           "The candidate sampling verdict and authored-medium reference appearance are separate. A candidate failure does not erase or reject the converged reference previews, and a pass would not select production rendering.",
           "Counts include atlas reads, conditional shadow reads, visible-bank draws, projected occupancy, full-resolution support-edge repair populations, and inside/surface stresses. They are not GPU timing or FPS.",
           f"Host runtime: {result['host_seconds']:.2f}s. No game, Wine, build, production change, install, or commit was performed.",""]
    (output/"report.md").write_text("\n".join(lines)); (output/"summary.json").write_text(json.dumps({"result":status,
        "authored_medium_preview_completed":True,"report_sha256":digest(report),"analysis_sha256":sources["analysis_sha256"],"images":len(image_hashes)},indent=2,sort_keys=True)+"\n")
    return result


def run_bank0_diagnostics(asset_data: Path, output: Path) -> dict:
    """Authorized fixed-camera bank0 isolation; it does not replace all-bank previews."""
    started=time.monotonic(); manifest=json.loads((asset_data/"manifest.json").read_text()); profiles={row["name"]:row for row in manifest["profiles"]}
    center=bank_center((0,0,0)); bank0={"cell":(0,0,0),"center":center,"distance":0.}; rows=[]; images={}
    for family in ("bluewell","foggreenoutlands"):
        volume=fog.decode_packet(asset_data/f"{family}.fogbin",manifest); sigma=float(profiles[family]["base_sigma"])*1.5
        for pose in canonical_poses()[3:]:
            origin=pose["origin"]; direction=camera_rays(origin,pose["target"],*CANONICAL_SIZE); limit=np.full(len(direction),fog.FAR,F)
            banks=nearby_banks(origin); inventory=intersection_inventory(origin,direction,limit,banks)
            isolated=dict(bank0); isolated["distance"]=float(np.linalg.norm(origin-center))
            ref128=integrate_banks(volume,origin,direction,limit,sigma,128.,SUN,[isolated])
            ref64=integrate_banks(volume,origin,direction,limit,sigma,64.,SUN,[isolated])
            candidate=integrate_banks(volume,origin,direction,limit,sigma,500.,SUN,[isolated],collect_cost=True)
            control=current_control(volume,origin,direction,limit,sigma,SUN)
            image=write_sheet(output,f"{family}-canonical-{pose['name']}-bank0-only",control,candidate,ref64,
                              (CANONICAL_SIZE[1],CANONICAL_SIZE[0])); path=output/image; images[image]=digest(path)
            rows.append({"family":family,"pose":pose["name"],"camera_origin":origin.tolist(),"bank0_center":center.tolist(),
                         "nominal_bank0_center_distance_render_units":float(np.linalg.norm(origin-center)),
                         "nominal_bank0_center_distance_km":float(np.linalg.norm(origin-center))/5000,
                         "all_bank_intersections_near_to_far":inventory,
                         "nearer_intersected_banks_than_bank0":[row for row in inventory if row["cell"]!=[0,0,0] and
                                                                 row["minimum_entry_distance_render_units"] < next(item["minimum_entry_distance_render_units"] for item in inventory if item["cell"]==[0,0,0])],
                         "bank0_reference_128_vs_64":error(ref128,ref64),"bank0_candidate500_error":error(candidate,ref64),
                         "bank0_reference_nonidentity_rays":int(np.count_nonzero(ref64["T"]<1)),
                         "bank0_candidate_cost":cost_metrics(candidate["cost"]),"image":image})
        del volume
    result={"schema":1,"purpose":"same-camera bank0-only diagnostic; original all-bank previews remain authoritative for layout",
            "analysis_sha256":digest(Path(__file__)),"rows":rows,"images":images,"host_seconds":time.monotonic()-started}
    path=output/"bank0-diagnostics.json"; path.write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+"\n")
    return result


def finalize_cached(capture: Path, asset_data: Path, output: Path) -> dict:
    """Apply report/law bindings to the frozen numerical run without relabeling it."""
    numeric_report=output/"numeric-report.json"; numeric_source=output/"numeric-source.py"
    result=json.loads(numeric_report.read_text()); executed_sha=digest(numeric_source)
    if result["sources"].get("analysis_sha256")!=executed_sha: raise ValueError("cached numerical source/report mismatch")
    manifest=json.loads((asset_data/"manifest.json").read_text()); profiles={row["name"]:row for row in manifest["profiles"]}
    all_laws=True
    for family,data in result["families"].items():
        volume=fog.decode_packet(asset_data/f"{family}.fogbin",manifest); sigma=float(profiles[family]["base_sigma"])*1.5
        family_laws=laws(volume,sigma); data["laws"]=family_laws
        pose_by_name={pose["name"]:pose for pose in canonical_poses()}
        for row in data["canonical"]:
            pose=pose_by_name[row["pose"]]
            row["full_resolution_coverage"]=full_resolution_coverage(pose["origin"],pose["target"],nearby_banks(pose["origin"]))
        for endpoint in data["captured_endpoints"]:
            endpoint["boundary_definition"]="four-neighbor captured 64x36 geometry/sky transition or valid-geometry reconstructed radial-limit jump >5% of smaller radial limit"
        all_costs=[row["candidate_cost"] for row in data["canonical"]]
        all_costs += [row["cost"] for row in data["boundary_path"]["rows"]]
        all_costs += [row["candidate_cost"] for row in data["captured_endpoints"]]
        all_costs += [row["cost"] for row in data["cost_stresses"]]
        max_hits=max(row["sphere_hits"]["max"] for row in all_costs); max_samples=max(row["samples"]["max"] for row in all_costs)
        required={"ordered_composition_exact":data["ordered_composition_max_delta"]==0,
                  "synthetic_zero_limit_identity":all(row["zero_limit_identity_exact"] for row in data["synthetic_geometry_clipping"].values()),
                  "synthetic_no_fog_through_foreground":all(row["fog_through_foreground_rays"]==0 for row in data["synthetic_geometry_clipping"].values()),
                  "observed_sphere_hit_bound_at_most_4":max_hits<=4,"observed_sample_bound_at_most_264":max_samples<=264}
        data["required_experiment_laws"]=required; data["observed_cost_bounds"]={"max_sphere_hits":max_hits,"max_samples":max_samples}
        all_laws &= all(value for value in family_laws.values() if isinstance(value,bool)) and all(required.values())
        del volume
    result["gates"]["laws_passed"]=all_laws
    result["sources"].pop("analysis_sha256")
    diagnostic_path=output/"bank0-diagnostics.json"
    if not diagnostic_path.exists(): raise ValueError("bank0 diagnostics missing")
    diagnostic=json.loads(diagnostic_path.read_text())
    if diagnostic["analysis_sha256"]!=digest(Path(__file__)): raise ValueError("bank0 diagnostic source mismatch")
    result["sources"].update({"numeric_execution_analysis_sha256":executed_sha,"numeric_report_sha256":digest(numeric_report),
                              "final_report_analysis_sha256":digest(Path(__file__)),"bank0_diagnostics_sha256":digest(diagnostic_path)})
    result["bank0_diagnostics"]={"purpose":diagnostic["purpose"],"rows":diagnostic["rows"]}
    result["images"].update(diagnostic["images"])
    result["authored_medium_selection"]={"selected":False,"decision_source":"user visual preference after fixed preview",
        "reason":"prefer broader, connected clouds","scope":"rejects this fixed finite-bank reference appearance; no replacement recipe is selected here"}
    result["provenance_note"]=("Original transport/error arrays and 26 images are cached unchanged from numeric-report.json and numeric-source.py. "
                               "Finalization recomputes analytic full-resolution coverage, adds required laws/cost gates/boundary labels/source bindings, "
                               "and merges separately bound bank0 diagnostic arrays and 6 sheets.")
    report=output/"report.json"; report.write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+"\n")
    canonical=[row for data in result["families"].values() for row in data["canonical"]]
    failed=[row for row in canonical if not row["candidate_passed"]]
    all_candidate_errors=[row["candidate500_error"] for row in canonical]
    p99=max(row["T"]["p99"] for row in all_candidate_errors); maximum=max(row["T"]["max"] for row in all_candidate_errors)
    refmax=max(row["reference_128_vs_64"]["T"]["max"] for row in canonical)
    occupancy=[row["candidate_cost"]["projected_occupancy_fraction"] for row in canonical]
    visible=[row["full_resolution_coverage"]["visible_banks"] for row in canonical]
    repair=[row["full_resolution_coverage"]["support_edge_pixels_requiring_full_resolution_repair_if_marked"] for row in canonical]
    max_hits=max(data["observed_cost_bounds"]["max_sphere_hits"] for data in result["families"].values())
    max_samples=max(data["observed_cost_bounds"]["max_samples"] for data in result["families"].values())
    lines=["# Fixed finite world-space fog-bank preview","",f"500-unit sampling result: **{result['result']}**. Authored-medium reference preview: **not selected; user prefers broader, connected clouds**.","",
           f"Twelve canonical 256x144 reference images cover both families and six frozen poses. Direct 128/64 reference convergence passes (worst T max {refmax:.9g}). Candidate500 fails {len(failed)}/12 canonical views; worst T p99/max is {p99:.9g}/{maximum:.9g} (gates .001/.003). Boundary-path temporal gates also fail for green; fixed reference previews remain valid for authored-medium review.",
           f"Canonical projected occupancy is {min(occupancy):.4f}..{max(occupancy):.4f}, with {min(visible)}..{max(visible)} full-resolution raster-visible banks. At nominal 30/38 km poses, cell(0,0,1) intersects substantially before bank0; the added same-camera bank0-only sheets isolate the intended-distance bank without replacing the all-bank layout previews.",
           f"All required support/transport/depth/alpha/order laws pass. Across canonical, path, endpoint, and stress rays, observed maxima are {max_hits} bank hits and {max_samples} samples, within fixed bounds 4/264. Canonical full-resolution support-edge populations are {min(repair)}..{max(repair)} pixels if marked for repair.","",
           "Cloud-only sheets compare the current 2.4 km control, converged bank reference, candidate500, and signed errors at fixed scales. No scene background was fabricated.",
           "The sampling verdict and authored-medium appearance are separate: candidate500 failure closes that sampling rule, not the fixed bank reference recipe. No radius, spacing, hash, anchor, strength, density, or sample-count adjustment followed.",
           "The original transport/error arrays and 26 images remain cached unchanged. Finalization recomputes analytic full-resolution coverage and merges the separately bound bank0 diagnostic arrays and six sheets, alongside required law/cost gates and depth-boundary labels.",
           "CPU runtime and read/storage counts are not GPU timing or FPS. Production transaction/state/Reset/recovery, native Windows, TAA, and user flight appearance remain open.",
           f"Numerical host runtime: {result['host_seconds']:.2f}s. No game, Wine, build, production change, install, or commit was performed.",""]
    (output/"report.md").write_text("\n".join(lines))
    checkpoint={"result":result["result"],"authored_medium_selection":result["authored_medium_selection"],"gates":result["gates"],
        "counts":{"canonical_views":len(canonical),"boundary_path_poses":sum(data["boundary_path"]["poses"] for data in result["families"].values()),
                  "captured_endpoints":sum(len(data["captured_endpoints"]) for data in result["families"].values()),"images":len(result["images"])},
        "metrics":{"canonical_candidate_T_worst_p99":p99,"canonical_candidate_T_worst_max":maximum,
                   "canonical_reference_T_worst_max":refmax,"canonical_projected_occupancy_fraction":[min(occupancy),max(occupancy)],
                   "full_resolution_visible_banks":[min(visible),max(visible)],"full_resolution_support_edge_pixels":[min(repair),max(repair)],
                   "observed_max_sphere_hits":max_hits,"observed_max_samples":max_samples},
        "bank0_interposition":{"10km":"no nearer intersected bank","30km":"cell(0,0,1) intersects before bank0",
                               "38km":"cells(0,0,1),(-1,0,1),(1,0,1) intersect before bank0"},
        "sources":result["sources"],"original_full_report_sha256":result["sources"]["numeric_report_sha256"],
        "final_full_report_sha256":digest(report),"images":result["images"],"limitations":result["limitations"]}
    checkpoint_path=output/"checkpoint.json"; checkpoint_path.write_text(json.dumps(checkpoint,indent=2,sort_keys=True)+"\n")
    (output/"summary.json").write_text(json.dumps({"result":result["result"],"authored_medium_selected":False,
        "report_sha256":digest(report),"checkpoint_sha256":digest(checkpoint_path),"numeric_execution_analysis_sha256":executed_sha,
        "numeric_report_sha256":result["sources"]["numeric_report_sha256"],"final_report_analysis_sha256":result["sources"]["final_report_analysis_sha256"],
        "images":len(result["images"])},indent=2,sort_keys=True)+"\n")
    return result


def main() -> int:
    parser=argparse.ArgumentParser(); parser.add_argument("--capture",type=Path,required=True); parser.add_argument("--asset-data",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True); parser.add_argument("--architecture",type=Path,default=Path("/tmp/x3-fog-next-architecture.md"))
    parser.add_argument("--finalize-cached",action="store_true"); parser.add_argument("--bank0-diagnostics-only",action="store_true")
    args=parser.parse_args()
    if args.finalize_cached and args.bank0_diagnostics_only: parser.error("choose one postprocessing mode")
    result=(run_bank0_diagnostics(args.asset_data,args.output) if args.bank0_diagnostics_only else
            finalize_cached(args.capture,args.asset_data,args.output) if args.finalize_cached else
                                     run(args.capture,args.asset_data,args.output,args.architecture))
    print(json.dumps({"result":result.get("result",result.get("purpose")),"output":str(args.output),"seconds":result["host_seconds"]},sort_keys=True)); return 0


if __name__ == "__main__": raise SystemExit(main())
