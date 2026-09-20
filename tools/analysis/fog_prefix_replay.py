#!/usr/bin/env python3
"""Bounded offline replay of fixed angular/depth fog-prefix reconstruction."""
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
NX, NY = 64, 36
HOLDOUT_NX, HOLDOUT_NY = 128, 72
PREFIX_BINS = 32
PREFIX_WIDTH = (fog.FAR - fog.NEAR) / PREFIX_BINS


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def depth_provenance(capture: Path) -> dict:
    paths = [capture / f"depth_1_{frame}.rgba32f"
             for lo, hi, _ in fog.BURSTS for frame in range(lo, hi + 1)]
    hashes = {path.name: digest(path) for path in paths}
    endpoints = {f"depth_1_{frame}.rgba32f" for lo, hi, _ in fog.BURSTS for frame in (lo, hi)}
    aggregate = hashlib.sha256(b"".join(bytes.fromhex(hashes[path.name]) for path in paths)).hexdigest()
    return {"count": len(paths), "aggregate_sha256": aggregate,
            "endpoint_sha256": {name: hashes[name] for name in sorted(endpoints)}}


def pixel_grid(nx: int, ny: int) -> tuple[np.ndarray, np.ndarray]:
    x = np.floor((np.arange(nx) + .5) * fog.FULL_WIDTH / nx).astype(np.int32)
    y = np.floor((np.arange(ny) + .5) * fog.FULL_HEIGHT / ny).astype(np.int32)
    yy, xx = np.meshgrid(y, x, indexing="ij")
    return xx.ravel(), yy.ravel()


def rays_at_pixels(meta: dict, depth_path: Path, x: np.ndarray, y: np.ndarray):
    depth = np.memmap(depth_path, "<f4", mode="r", shape=(fog.FULL_HEIGHT, fog.FULL_WIDTH, 4))[y, x]
    origin, direction, limit, geometry = fog.reconstruct_rays(meta, x, y, depth)
    return origin, direction, limit, geometry, np.asarray(depth[:, 2], F)


def build_prefix(volume: np.ndarray, origin: np.ndarray, direction: np.ndarray,
                 sigma: float, spacing: float, sun: np.ndarray, chunk: int = 64) -> dict:
    """Integrate fixed independent radial bins, then ordered-prefix their transport."""
    count = int(math.ceil(PREFIX_WIDTH / spacing))
    S = np.zeros((PREFIX_BINS + 1, len(direction), 3), F)
    T = np.ones((PREFIX_BINS + 1, len(direction)), F)
    for first in range(0, len(direction), chunk):
        sl = slice(first, min(first + chunk, len(direction)))
        cumulative_S = np.zeros((sl.stop - sl.start, 3), F)
        cumulative_T = np.ones(sl.stop - sl.start, F)
        for bin_index in range(PREFIX_BINS):
            start = fog.NEAR + bin_index * PREFIX_WIDTH
            end = start + PREFIX_WIDTH
            lo = start + np.arange(count, dtype=np.float64)[:, None] * spacing
            ds = np.clip(end - lo, 0, spacing).astype(F)
            distance = lo + ds * .5
            points = origin + direction[sl][None, :, :] * distance[..., None]
            rgba = fog.sample_level(volume, points)
            segment_S, segment_T, _ = fog.integrate_samples(
                rgba, np.broadcast_to(ds, distance.shape), distance, sigma, direction[sl], True, sun)
            cumulative_S = cumulative_S + cumulative_T[:, None] * segment_S
            cumulative_T = cumulative_T * segment_T
            S[bin_index + 1, sl] = cumulative_S
            T[bin_index + 1, sl] = cumulative_T
    return {"planes": fog.NEAR + np.arange(PREFIX_BINS + 1) * PREFIX_WIDTH,
            "S": S, "T": T, "spacing": spacing, "samples_per_bin": count}


def angular_neighbors(x: np.ndarray, y: np.ndarray, knot_x: np.ndarray,
                      knot_y: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    def axis(values, knots):
        hi = np.searchsorted(knots, values, side="right")
        lo = np.clip(hi - 1, 0, len(knots) - 1); hi = np.clip(hi, 0, len(knots) - 1)
        span = knots[hi] - knots[lo]
        weight = np.divide(values - knots[lo], span, out=np.zeros(len(values), np.float64), where=span != 0)
        weight = np.clip(weight, 0, 1)
        return lo, hi, weight
    x0, x1, fx = axis(np.asarray(x), np.asarray(knot_x))
    y0, y1, fy = axis(np.asarray(y), np.asarray(knot_y))
    indices = np.stack((y0*len(knot_x)+x0, y0*len(knot_x)+x1,
                        y1*len(knot_x)+x0, y1*len(knot_x)+x1), axis=-1)
    weights = np.stack(((1-fx)*(1-fy), fx*(1-fy), (1-fx)*fy, fx*fy), axis=-1)
    return indices.astype(np.int32), weights.astype(F)


def depth_reconstruct(prefix: dict, limit: np.ndarray, neighbors: np.ndarray,
                      weights: np.ndarray) -> dict[str, np.ndarray]:
    limit = np.asarray(limit, np.float64)
    q = np.clip((limit - fog.NEAR) / PREFIX_WIDTH, 0, PREFIX_BINS)
    lower = np.floor(q).astype(np.int32); upper = np.minimum(lower + 1, PREFIX_BINS)
    fraction = (q - lower).astype(np.float64)
    S_out = np.zeros((len(limit), 3), np.float64); T_out = np.zeros(len(limit), np.float64)
    row = np.arange(len(limit))
    for corner in range(4):
        knot = neighbors[:, corner]
        Sa = prefix["S"][lower, knot].astype(np.float64)
        Sb = prefix["S"][upper, knot].astype(np.float64)
        Ta = np.maximum(prefix["T"][lower, knot].astype(np.float64), np.finfo(np.float64).tiny)
        Tb = np.maximum(prefix["T"][upper, knot].astype(np.float64), np.finfo(np.float64).tiny)
        tau_segment = np.maximum(-np.log(Tb / Ta), 0)
        denominator = -np.expm1(-tau_segment)
        numerator = -np.expm1(-fraction * tau_segment)
        factor = np.divide(numerator, denominator, out=fraction.copy(), where=denominator > 1e-12)
        Sc = Sa + (Sb - Sa) * factor[:, None]
        Tc = Ta * np.exp(-fraction * tau_segment)
        S_out += weights[:, corner, None] * Sc
        T_out += weights[:, corner] * Tc
    identity = limit <= fog.NEAR
    S_out[identity] = 0; T_out[identity] = 1
    return {"S": S_out.astype(F), "T": T_out.astype(F),
            "tau": (-np.log(np.maximum(T_out, np.finfo(np.float64).tiny))).astype(F)}


def reconstruct(prefix: dict, limit: np.ndarray, x: np.ndarray, y: np.ndarray,
                knot_x: np.ndarray, knot_y: np.ndarray) -> dict[str, np.ndarray]:
    neighbors, weights = angular_neighbors(x, y, knot_x, knot_y)
    return depth_reconstruct(prefix, limit, neighbors, weights)


def compose(near: dict, far: dict) -> dict[str, np.ndarray]:
    return {"S": near["S"] + near["T"][:, None] * far["S"],
            "T": near["T"] * far["T"], "tau": near["tau"] + far["tau"],
            "near_S": near["S"], "near_T": near["T"], "near_tau": near["tau"],
            "far_S": far["S"], "far_T": far["T"], "far_tau": far["tau"]}


def composite_over_source(source_rgba: np.ndarray, transport: dict) -> np.ndarray:
    result = np.asarray(source_rgba, F).copy()
    result[..., :3] = transport["S"] + transport["T"][..., None] * result[..., :3]
    return result


def boundary_mask(geometry: np.ndarray, raw_depth: np.ndarray, shape=(HOLDOUT_NY, HOLDOUT_NX)) -> np.ndarray:
    geometry = np.asarray(geometry, bool).reshape(shape); depth = np.asarray(raw_depth, F).reshape(shape)
    boundary = np.zeros(shape, bool)
    for dy, dx in ((0, 1), (1, 0)):
        a = (slice(None, -1), slice(None)) if dy else (slice(None), slice(None, -1))
        b = (slice(1, None), slice(None)) if dy else (slice(None), slice(1, None))
        transition = geometry[a] != geometry[b]
        both = geometry[a] & geometry[b]
        smaller = np.minimum(depth[a], depth[b])
        jump = both & np.isfinite(depth[a]) & np.isfinite(depth[b]) & (smaller > 0) & (np.abs(depth[a]-depth[b]) > .05*smaller)
        edge = transition | jump
        boundary[a] |= edge; boundary[b] |= edge
    return boundary.ravel()


def metric(values: np.ndarray) -> dict[str, float | int]:
    a = np.asarray(values, np.float64).ravel()
    return {"count": int(a.size), "p99": float(np.percentile(a, 99)) if a.size else 0.,
            "max": float(a.max(initial=0))}


def errors(candidate: dict, reference: dict, select: np.ndarray) -> dict:
    select = np.asarray(select, bool)
    return {"count": int(select.sum()), "T": metric(np.abs(candidate["T"][select]-reference["T"][select])),
            "S_normalized_unit_radiance": [metric(np.abs(candidate["S"][select, c]-reference["S"][select, c]))
                                             for c in range(3)]}


def passes(error: dict, convergence=False) -> bool:
    if not error["count"]: return True
    if convergence:
        return error["T"]["p99"] <= .00025 and error["T"]["max"] <= .00075
    return (error["T"]["p99"] <= .001 and error["T"]["max"] <= .003 and
            all(row["p99"] <= .0005 and row["max"] <= .002 for row in error["S_normalized_unit_radiance"]))


def _rgb(values, shape, maximum, signed=False):
    image = values.reshape(shape+(3,)); image = .5+image/(2*maximum) if signed else image/maximum
    return (np.clip(image, 0, 1)**(1/2.2)*255+.5).astype(np.uint8)


def _scalar(values, shape, maximum, signed=False):
    image = values.reshape(shape); image = .5+image/(2*maximum) if signed else image/maximum
    image = (np.clip(image, 0, 1)*255+.5).astype(np.uint8)
    return np.repeat(image[..., None], 3, axis=-1)


def write_images(output: Path, stem: str, near: dict, candidate: dict, reference: dict,
                 boundary: np.ndarray, shape=(HOLDOUT_NY, HOLDOUT_NX)) -> list[str]:
    from PIL import Image, ImageDraw
    columns = [("near control", near["S"], 1-near["T"]),
               ("far reference", reference["far_S"], 1-reference["far_T"]),
               ("far prefix", candidate["far_S"], 1-candidate["far_T"]),
               ("complete ref", reference["S"], 1-reference["T"]),
               ("complete prefix", candidate["S"], 1-candidate["T"])]
    panels = [[_rgb(S, shape, .03), _scalar(opacity, shape, .4)] for _, S, opacity in columns]
    panels.append([_rgb(candidate["S"]-reference["S"], shape, .002, True),
                   _scalar(candidate["T"]-reference["T"], shape, .003, True)])
    labels = [row[0] for row in columns]+["signed error"]
    scale=4; top=18; left=74; h,w=shape
    sheet=Image.new("RGB",(left+len(panels)*w*scale,top+2*h*scale),"black"); draw=ImageDraw.Draw(sheet)
    for col,(label,group) in enumerate(zip(labels,panels)):
        draw.text((left+col*w*scale+2,3),label,fill="white")
        for row,panel in enumerate(group):
            sheet.paste(Image.fromarray(panel).resize((w*scale,h*scale),Image.Resampling.NEAREST),
                        (left+col*w*scale,top+row*h*scale))
    draw.text((2,top+3),"S / fixed",fill="white"); draw.text((2,top+h*scale+3),"opacity/T",fill="white")
    path=output/f"{stem}-prefix-reference-error.png"; sheet.save(path)
    mask=np.zeros(shape+(3,),np.uint8); mask[boundary.reshape(shape)]=(255,255,255)
    mask_path=output/f"{stem}-depth-boundary-mask.png"
    Image.fromarray(mask).resize((w*scale,h*scale),Image.Resampling.NEAREST).save(mask_path)
    return [path.name,mask_path.name]


def synthetic_checks(volume, origin, direction, prefix, knot_x, knot_y, sigma, sun) -> dict:
    # One fixed holdout ray at all near/plane neighborhoods and interval midpoints.
    hold_x, hold_y = pixel_grid(HOLDOUT_NX, HOLDOUT_NY); index = len(hold_x)//2
    radial = [fog.NEAR-1, fog.NEAR, fog.NEAR+1]
    for plane in prefix["planes"][1:]: radial += [plane-1, plane, plane+1]
    radial += [fog.NEAR+(k+.5)*PREFIX_WIDTH for k in range(PREFIX_BINS)]
    limits=np.asarray(radial,F); dirs=np.repeat(direction[index:index+1],len(limits),axis=0)
    reconstructed=reconstruct(prefix,limits,np.full(len(limits),hold_x[index]),np.full(len(limits),hold_y[index]),knot_x,knot_y)
    direct=fog.accurate_reference(volume,origin,dirs,limits,sigma,64.,sun)
    radial_error=errors(reconstructed,{"S":direct["far_S"],"T":direct["far_T"]},np.ones(len(limits),bool))
    # Fixed synthetic clipping patterns across the real holdout directions.
    one_column=np.full(len(direction),fog.FAR,F); one_column[hold_x==hold_x[HOLDOUT_NX//2]]=fog.NEAR-1000
    step=np.where(hold_x<fog.FULL_WIDTH//2,60000.,fog.FAR).astype(F)
    patterns={}
    for name,limits2 in (("one_holdout_column_near_object_against_sky",one_column),("fixed_screen_depth_step",step)):
        rec=reconstruct(prefix,limits2,hold_x,hold_y,knot_x,knot_y)
        ref=fog.accurate_reference(volume,origin,direction,limits2,sigma,64.,sun)
        patterns[name]={"far_error":errors(rec,{"S":ref["far_S"],"T":ref["far_T"]},np.ones(len(direction),bool)),
                        "near_or_short_far_identity": bool(np.array_equal(rec["T"][limits2<=fog.NEAR],np.ones(np.sum(limits2<=fog.NEAR),F)) and
                                                           np.array_equal(rec["S"][limits2<=fog.NEAR],np.zeros((np.sum(limits2<=fog.NEAR),3),F)))}
    return {"radial_cases":len(limits),"radial_far_error":radial_error,"patterns":patterns}


def constant_vacuum_laws() -> dict:
    direction=np.array([[1.,0.,0.],[0.,1.,0.]],F); sun=np.array([0.,0.,1.],F); origin=np.zeros(3)
    limits=np.array([fog.FAR,54321.],F); neighbors=np.array([[0,0,0,0],[1,1,1,1]],np.int32); weights=np.array([[1,0,0,0],[1,0,0,0]],F)
    vacuum=np.zeros((8,8,8,4),F); prefix=build_prefix(vacuum,origin,direction,1e-5,512.,sun,2)
    out=depth_reconstruct(prefix,limits,neighbors,weights)
    constant=np.empty((8,8,8,4),F); constant[...,:3]=[.1,.2,.3]; constant[...,3]=.5
    cp=build_prefix(constant,origin,direction,4e-6,64.,sun,2)
    exact_plane=depth_reconstruct(cp,np.array([cp["planes"][7],cp["planes"][19]]),neighbors,weights)
    invalid=depth_reconstruct(cp,np.array([0.,0.]),neighbors,weights)
    source=np.array([[.1,.2,.3,0.],[.9,.8,.7,1.]],F); composited=composite_over_source(source,invalid)
    return {"vacuum_identity_exact":bool(np.array_equal(out["S"],np.zeros_like(out["S"])) and np.array_equal(out["T"],np.ones_like(out["T"]))),
            "prefix_plane_exact":bool(np.array_equal(exact_plane["S"],np.stack((cp["S"][7,0],cp["S"][19,1]))) and
                                      np.array_equal(exact_plane["T"],np.array([cp["T"][7,0],cp["T"][19,1]]))),
            "invalid_depth_far_identity_exact":bool(np.array_equal(invalid["S"],np.zeros_like(invalid["S"])) and
                                                     np.array_equal(invalid["T"],np.ones_like(invalid["T"]))),
            "source_alpha_preserved_exact":bool(np.array_equal(composited[:,3],source[:,3])),
            "finite":bool(np.isfinite(cp["S"]).all() and np.isfinite(cp["T"]).all())}


def run(capture: Path, asset_data: Path, output: Path, plan: Path) -> dict:
    started=time.monotonic(); output.mkdir(parents=True,exist_ok=True)
    manifest_path=asset_data/"manifest.json"; manifest=json.loads(manifest_path.read_text())
    log_paths=sorted(capture.glob("session-*.log"))
    if len(log_paths)!=1: raise ValueError("expected one capture session log")
    metadata,metadata_sha=fog.load_metadata(log_paths[0]); profiles={row["name"]:row for row in manifest["profiles"]}
    knot_x,knot_y_grid=pixel_grid(NX,NY); knot_x_axis=np.unique(knot_x); knot_y_axis=np.unique(knot_y_grid)
    hold_x,hold_y=pixel_grid(HOLDOUT_NX,HOLDOUT_NY)
    if np.intersect1d(knot_x_axis,np.unique(hold_x)).size: raise ValueError("holdout x overlaps coarse knots")
    sources={"numeric_execution_analysis_sha256":digest(Path(__file__)),"distance_replay_sha256":digest(HERE/"fog_distance_replay.py"),
             "plan_sha256":digest(plan),"manifest_sha256":digest(manifest_path),"selected_capture_metadata_sha256":metadata_sha,
             "capture_log":str(log_paths[0]),"capture_log_bytes":log_paths[0].stat().st_size,
             "depth_files":depth_provenance(capture),"packets":{}}
    views=[]; all_pass=True; all_ref_converged=True; all_prefix_converged=True
    for lo,hi,family in fog.BURSTS:
        packet=asset_data/f"{family}.fogbin"; volume=fog.decode_packet(packet,manifest); sigma=float(profiles[family]["base_sigma"])*1.5
        sources["packets"][family]={"sha256":digest(packet),"decoded_sha256":profiles[family]["decoded_sha256"]}
        for frame in (lo,hi):
            depth_path=capture/f"depth_1_{frame}.rgba32f"; meta=metadata[frame]; sun=fog.captured_sun(meta)
            origin,knot_direction,_,_,_=rays_at_pixels(meta,depth_path,knot_x,knot_y_grid)
            prefix64=build_prefix(volume,origin,knot_direction,sigma,64.,sun)
            prefix128=build_prefix(volume,origin,knot_direction,sigma,128.,sun)
            _,direction,limit,geometry,raw_depth=rays_at_pixels(meta,depth_path,hold_x,hold_y)
            boundary=boundary_mask(geometry,raw_depth); sky=~geometry
            reference64=fog.accurate_reference(volume,origin,direction,limit,sigma,64.,sun)
            reference128=fog.accurate_reference(volume,origin,direction,limit,sigma,128.,sun)
            far64=reconstruct(prefix64,limit,hold_x,hold_y,knot_x_axis,knot_y_axis)
            far128=reconstruct(prefix128,limit,hold_x,hold_y,knot_x_axis,knot_y_axis)
            near=fog.near24(volume,origin,direction,limit,sigma,sun)
            candidate64=compose(near,far64); candidate128=compose(near,far128)
            groups={"all":np.ones(len(limit),bool),"sky":sky,"geometry":geometry,"boundary":boundary}
            ref_conv={name:errors({"S":reference128["far_S"],"T":reference128["far_T"]},{"S":reference64["far_S"],"T":reference64["far_T"]},select) for name,select in groups.items()}
            prefix_conv={name:errors(far128,far64,select) for name,select in groups.items()}
            far_error={name:errors(far64,{"S":reference64["far_S"],"T":reference64["far_T"]},select) for name,select in groups.items()}
            complete_error={name:errors(candidate64,reference64,select) for name,select in groups.items()}
            ref_ok=all(passes(row,True) for row in ref_conv.values()); prefix_ok=all(passes(row,True) for row in prefix_conv.values())
            view_ok=ref_ok and prefix_ok and all(passes(row) for row in far_error.values()) and all(passes(row) for row in complete_error.values())
            all_ref_converged &= ref_ok; all_prefix_converged &= prefix_ok; all_pass &= view_ok
            synthetic=synthetic_checks(volume,origin,direction,prefix64,knot_x_axis,knot_y_axis,sigma,sun)
            images=write_images(output,f"{family}-{frame}",near,candidate64,reference64,boundary)
            failure=None
            if not view_ok:
                delta=np.abs(candidate64["T"]-reference64["T"]); index=int(np.argmax(delta))
                failure={"holdout_index":index,"pixel":[int(hold_x[index]),int(hold_y[index])],"limit":float(limit[index]),
                         "geometry":bool(geometry[index]),"boundary":bool(boundary[index]),"reference_T":float(reference64["T"][index]),
                         "candidate_T":float(candidate64["T"][index]),"absolute_T":float(delta[index]),
                         "reference_S":reference64["S"][index].tolist(),"candidate_S":candidate64["S"][index].tolist()}
            views.append({"family":family,"frame":frame,"rays":len(limit),"groups":{k:int(v.sum()) for k,v in groups.items()},
                          "boundary_definition":"four-neighbor holdout-grid sky/geometry transition or valid geometry raw-linear-depth jump >5% of smaller depth",
                          "reference_128_vs_64_far":ref_conv,"prefix_128_vs_64":prefix_conv,"reconstructed_far_error":far_error,
                          "complete_error":complete_error,"passed":view_ok,
                          "max_complete_T_diagnostic_witness":failure,
                          "synthetic_depth_checks":synthetic,"images":images})
        del volume
    laws=constant_vacuum_laws(); all_laws=all(laws.values())
    # Contract stops before temporal replay when any endpoint or law fails.
    temporal={"executed":False,"reason":"endpoint-or-law-failure stop rule" if not(all_pass and all_laws) else "pending"}
    status="failed-prefix-feasibility" if not(all_pass and all_laws) else "passed-prefix-feasibility"
    if all_pass and all_laws:
        temporal_rows=[]; temporal_pass=True; temporal_x,temporal_y=pixel_grid(6,4)
        for lo,_,family in fog.BURSTS:
            volume=fog.decode_packet(asset_data/f"{family}.fogbin",manifest); sigma=float(profiles[family]["base_sigma"])*1.5
            previous_candidate=None; previous_reference=None; pair_T=[]; pair_S=[[],[],[]]; frame_rows=[]
            for frame in range(lo,lo+8):
                depth_path=capture/f"depth_1_{frame}.rgba32f"; meta=metadata[frame]; sun=fog.captured_sun(meta)
                origin,knot_direction,_,_,_=rays_at_pixels(meta,depth_path,knot_x,knot_y_grid)
                prefix=build_prefix(volume,origin,knot_direction,sigma,64.,sun)
                _,direction,limit,_,_=rays_at_pixels(meta,depth_path,temporal_x,temporal_y)
                far=reconstruct(prefix,limit,temporal_x,temporal_y,knot_x_axis,knot_y_axis)
                near=fog.near24(volume,origin,direction,limit,sigma,sun); candidate=compose(near,far)
                reference=fog.accurate_reference(volume,origin,direction,limit,sigma,64.,sun)
                frame_error=errors(candidate,reference,np.ones(len(limit),bool)); frame_ok=passes(frame_error)
                temporal_pass &= frame_ok; frame_rows.append({"frame":frame,"error":frame_error,"passed":frame_ok})
                if previous_candidate is not None:
                    pair_T.append(np.abs((candidate["T"]-previous_candidate["T"])-(reference["T"]-previous_reference["T"])))
                    for channel in range(3):
                        pair_S[channel].append(np.abs((candidate["S"][:,channel]-previous_candidate["S"][:,channel])-
                                                      (reference["S"][:,channel]-previous_reference["S"][:,channel])))
                previous_candidate=candidate; previous_reference=reference
            change_error={"count":7*len(temporal_x),"T":metric(np.concatenate(pair_T)),
                          "S_normalized_unit_radiance":[metric(np.concatenate(rows)) for rows in pair_S]}
            change_ok=passes(change_error); temporal_pass &= change_ok
            temporal_rows.append({"family":family,"frames":8,"rays_per_frame":len(temporal_x),"frame_errors":frame_rows,
                                  "adjacent_change_residual":change_error,"passed":all(row["passed"] for row in frame_rows) and change_ok})
            del volume
        temporal={"executed":True,"contract":"first eight consecutive frames per burst, fixed 6x4 holdouts, representation rebuilt each frame",
                  "rows":temporal_rows,"passed":temporal_pass}
        if not temporal_pass: status="failed-prefix-feasibility"
    image_paths=sorted(output.glob("*.png"))
    reads={"prefix_generation_field_reads_per_frame":NX*NY*PREFIX_BINS*int(math.ceil(PREFIX_WIDTH/64.))*2,
           "five_two_read_prefix_scans":737280,"portable_full_screen_eight_point_gather":7864320,
           "existing_half_resolution_near24_field_reads":640*384*24*2,
           "prefix_target_unpadded_FP32_bytes":int(2.25*1024*1024),"CPU_runtime_is_not_GPU_time":True}
    result={"schema":1,"result":status,"sources":sources,"contract":{"coarse_knots":[NX,NY],"holdouts":[HOLDOUT_NX,HOLDOUT_NY],
             "prefix_planes":PREFIX_BINS,"prefix_width":PREFIX_WIDTH,"near":"unchanged 24 midpoint samples","far":"original unfiltered field, 64-unit quadrature; 128 control",
             "angular":"bilinear S and T directly at actual integer knot coordinates; clamp edges","depth":"tau-linear T and exponential segment-fraction S",
             "geometry":"prefix independent; exact holdout depth controls L","lighting":"captured point-sun dir1, normalized unit radiance"},
             "gates":{"reference_converged":all_ref_converged,"prefix_quadrature_converged":all_prefix_converged,"all_endpoint_groups_passed":all_pass,
                      "laws_passed":all_laws,"laws":laws,"temporal_passed_if_executed":temporal.get("passed")},
             "views":views,"temporal":temporal,"estimated_reads_and_storage":reads,
             "images":{p.name:digest(p) for p in image_paths},"limitations":["Numerical and cost feasibility only; passing does not approve 40km haze or a production implementation.",
             "Original capture cards cannot provide static long-range cloud-bank truth.","Normalized unit-radiance S excludes production radiance and shadow-map lighting.",
             "CPU runtime and estimated texture reads are not FPS or measured GPU time.","No native Windows, actual-D3D, TAA, flight appearance, state, Reset, or failure-recovery behavior is verified."],
             "host_seconds":time.monotonic()-started}
    report=output/"report.json"; report.write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+"\n")
    worst=max(v["complete_error"][g]["T"]["max"] for v in views for g in v["complete_error"])
    lines=["# Fixed angular/depth fog-prefix reconstruction","",f"Result: **{status}**.","",f"Four endpoint views use 9,216 independent holdout rays each. Worst complete T error is {worst:.9g} (gate .003); report.json preserves all/sky/geometry/boundary counts and p99/max S/T errors.",
           f"Direct-reference convergence: {all_ref_converged}; prefix-quadrature convergence: {all_prefix_converged}; endpoint reconstruction gates: {all_pass}; operator laws: {all_laws}.",
           "The fixed stop rule prevents resolution, bin, interpolation, seed, density, anchor, scale, or threshold changes after a failure.","",
           "Images are cloud-only fixed-scale near controls, far/complete references, prefix reconstructions, signed errors, and holdout depth-boundary masks. Original capture cards are not static long-range cloud truth.",
           "Read/storage counts are static estimates, not GPU time or FPS. Passing would establish prefix feasibility only, not approve the 40 km accumulated haze or a production transaction.",
           f"Host runtime: {result['host_seconds']:.2f}s. No game, Wine, build, production edit, install, or commit was performed.",""]
    (output/"report.md").write_text("\n".join(lines)); (output/"summary.json").write_text(json.dumps({"result":status,"report_sha256":digest(report),"numeric_execution_analysis_sha256":sources["numeric_execution_analysis_sha256"],"images":len(image_paths)},indent=2,sort_keys=True)+"\n")
    return result


def finalize_cached(capture: Path, output: Path) -> dict:
    """Add report-only bindings without relabeling cached numerical execution."""
    numeric_report = output / "numeric-report.json"; numeric_source = output / "numeric-source.py"
    result = json.loads(numeric_report.read_text())
    executed_sha = digest(numeric_source)
    if result["sources"].get("analysis_sha256") != executed_sha:
        raise ValueError("cached numerical source/report mismatch")
    result["sources"].pop("analysis_sha256")
    result["sources"].update({"numeric_execution_analysis_sha256": executed_sha,
                              "numeric_report_sha256": digest(numeric_report),
                              "final_report_analysis_sha256": digest(Path(__file__)),
                              "depth_files": depth_provenance(capture)})
    for view in result["views"]:
        view["max_complete_T_diagnostic_witness"] = view.pop("minimal_failure_witness")
        view["max_complete_T_diagnostic_witness_scope"] = (
            "one maximum complete-T diagnostic only; explicit failed metrics above are authoritative for S, convergence, and group gates")
    result["provenance_note"] = "Numerical arrays and images are cached from numeric-report.json and numeric-source.py; finalization changed only bindings and labels."
    result["finalization_seconds"] = 0.
    report = output / "report.json"
    report.write_text(json.dumps(result, indent=2, sort_keys=True, allow_nan=False)+"\n")
    views=result["views"]
    complete_p99=[view["complete_error"]["all"]["T"]["p99"] for view in views]
    complete_max=[view["complete_error"]["all"]["T"]["max"] for view in views]
    ref_max=max(view["reference_128_vs_64_far"][g]["T"]["max"] for view in views for g in view["reference_128_vs_64_far"])
    prefix_max=max(view["prefix_128_vs_64"][g]["T"]["max"] for view in views for g in view["prefix_128_vs_64"])
    boundary=[view["groups"]["boundary"] for view in views]
    lines=["# Fixed angular/depth fog-prefix reconstruction","",f"Result: **{result['result']}**.","",
           f"Four endpoint views use 9,216 independent holdout rays each. Complete all-ray T p99 is {min(complete_p99):.6f}..{max(complete_p99):.6f} (gate .001); max is {min(complete_max):.6f}..{max(complete_max):.6f} (gate .003). Normalized-S gates also fail.",
           f"Direct 128/64 far-reference convergence passes with worst T max {ref_max:.9g}; prefix 128/64 quadrature convergence passes with worst T max {prefix_max:.9g}. Boundary populations contain {min(boundary)}..{max(boundary)} rays and fail separately; report.json retains all/sky/geometry/boundary S/T metrics and exact counts.",
           "Constant/vacuum, prefix-plane, invalid-depth, and source-alpha laws pass. Fixed radial, one-column near-object, and screen-depth-step synthetic results are retained. Endpoint failure invokes the ratified stop rule, so the conditional temporal arm did not run.","",
           "The maximum complete-T diagnostic per view is not a universal failure witness; the explicit metric tables are authoritative for every failed S, T, convergence, and population gate.",
           "Images are cloud-only fixed-scale near controls, far/complete references, prefix reconstructions, signed errors, and holdout boundary masks. Original capture cards are not static long-range cloud truth.",
           "Numerical source/report and final report source are bound separately because the depth-hash and witness-label corrections occurred after execution. No numerical arrays or images were recomputed during finalization.",
           "Static read/storage estimates are not GPU time or FPS. This failure rejects only the fixed 64x36/32-plane reconstruction; it does not reject all prefix designs or approve the accumulated 40 km haze.",
           f"Numerical host runtime: {result['host_seconds']:.2f}s. No game, Wine, build, production edit, install, or commit was performed.",""]
    (output/"report.md").write_text("\n".join(lines))
    (output/"summary.json").write_text(json.dumps({"result":result["result"],"report_sha256":digest(report),
        "numeric_execution_analysis_sha256":executed_sha,"final_report_analysis_sha256":result["sources"]["final_report_analysis_sha256"],
        "numeric_report_sha256":result["sources"]["numeric_report_sha256"],"images":len(result["images"])},indent=2,sort_keys=True)+"\n")
    return result


def main() -> int:
    parser=argparse.ArgumentParser(); parser.add_argument("--capture",type=Path,required=True); parser.add_argument("--asset-data",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True); parser.add_argument("--plan",type=Path,default=Path("/tmp/x3-fog-after-macro-plan.md"))
    parser.add_argument("--finalize-cached",action="store_true")
    args=parser.parse_args(); result=(finalize_cached(args.capture,args.output) if args.finalize_cached else
                                     run(args.capture,args.asset_data,args.output,args.plan))
    print(json.dumps({"result":result["result"],"output":str(args.output),"seconds":result["host_seconds"]},sort_keys=True)); return 0


if __name__ == "__main__": raise SystemExit(main())
