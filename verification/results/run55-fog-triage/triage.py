#!/usr/bin/env python3
"""Produce a bounded Run55 F8/camera/fog admission witness; no Wine or game use."""
from __future__ import annotations

import collections
import datetime as dt
import hashlib
import json
import math
import os
import subprocess
from pathlib import Path

OUT = Path("/tmp/x3-run55-fog-triage")
LOG = Path("/tmp/x3-bottleX3-run199/session-20260921-005922-212.log")
SOURCE = Path("/tmp/x3-media-production-integration")
START, END = 2188, 2219


def fields(line: str) -> dict[str, str]:
    return dict(token.split("=", 1) for token in line.split() if "=" in token)


def gram_error(r: list[float]) -> float:
    return max(abs(sum(r[3*i+k] * r[3*j+k] for k in range(3)) - (1.0 if i == j else 0.0))
               for i in range(3) for j in range(3))


def determinant(r: list[float]) -> float:
    return (r[0]*(r[4]*r[8]-r[5]*r[7]) - r[1]*(r[3]*r[8]-r[5]*r[6])
            + r[2]*(r[3]*r[7]-r[4]*r[6]))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    cameras: dict[int, dict[str, str]] = {}
    motion: dict[int, dict[str, str]] = {}
    sectors: dict[int, dict[str, str]] = {}
    fog_rows: list[dict[str, str]] = []
    card_rows: list[dict[str, str]] = []
    all_fog = collections.Counter()
    all_cards = collections.Counter()
    for line in LOG.open(errors="replace"):
        key = line.split(" ", 1)[0]
        if key not in {"camera_state", "motion_output_frame", "sector_background",
                       "volumetric_fog_frame", "volumetric_fog_cards"}:
            continue
        row = fields(line)
        try:
            frame = int(row.get("frame", "-1"))
        except ValueError:
            continue
        if key == "volumetric_fog_frame":
            all_fog[row.get("reason", "missing")] += 1
            if START <= frame <= END:
                fog_rows.append(row)
        elif key == "volumetric_fog_cards":
            all_cards[(row.get("refused", "missing"), row.get("applied", "missing"),
                       row.get("fault", "missing"))] += 1
            if START <= frame <= END:
                card_rows.append(row)
        elif START <= frame <= END:
            if key == "camera_state": cameras[frame] = row
            elif key == "motion_output_frame": motion[frame] = row
            else: sectors[frame] = row
    expected = set(range(START, END + 1))
    assert set(cameras) == expected, (len(cameras), sorted(expected - set(cameras)))
    assert set(motion) == expected, (len(motion), sorted(expected - set(motion)))
    rows = []
    for frame, row in sorted(cameras.items()):
        rotation = [float(row[f"r{i}{j}"]) for i in range(3) for j in range(3)]
        translation = [float(v) for v in row["t"].split(",")]
        rows.append({"frame": frame, "rotation": rotation, "translation": translation,
                     "gram_error": gram_error(rotation), "determinant": determinant(rotation),
                     "camera_valid": row.get("valid"), "camera_reason": row.get("reason"),
                     "camera_cut": row.get("camera_cut")})
    camera_input = OUT / "f8-cameras.txt"
    camera_input.write_text("".join(" ".join([str(x["frame"])] + [repr(v) for v in x["rotation"]]
                                               + [repr(v) for v in x["translation"]]) + "\n"
                                      for x in rows))
    witness_cpp = OUT / "admission_witness.cpp"
    witness_cpp.write_text(r'''#include "fog_volume_math.h"
#include <cassert>
#include <cmath>
#include <cstdio>
int main() { unsigned frame=0,n=0,accepted=0; double r[9],t[3],sun[3]={0,0,1},worst=0,detmin=2,detmax=-2;
  while (std::scanf("%u",&frame)==1) { for(double& x:r) assert(std::scanf("%lf",&x)==1); for(double& x:t) assert(std::scanf("%lf",&x)==1);
    x3m::renderer::FogWorldBasis basis; const bool ok=x3m::renderer::fog_world_basis(r,t,sun,basis); accepted+=ok; ++n;
    double det=r[0]*(r[4]*r[8]-r[5]*r[7])-r[1]*(r[3]*r[8]-r[5]*r[6])+r[2]*(r[3]*r[7]-r[4]*r[6]); detmin=fmin(detmin,det); detmax=fmax(detmax,det);
    for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) { double dot=0; for(unsigned k=0;k<3;++k) dot+=r[3*i+k]*r[3*j+k]; worst=fmax(worst,fabs(dot-(i==j?1.:0.))); }
  }
  std::printf("rows=%u accepted=%u gram_max=%.12g det_min=%.12g det_max=%.12g\n",n,accepted,worst,detmin,detmax);
  assert(n==32 && accepted==32);
}''')
    binary = OUT / "admission_witness"
    command = ["clang++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I", str(SOURCE / "src/renderer"), str(witness_cpp), "-o", str(binary)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(binary)], stdin=camera_input.open(), text=True,
                            capture_output=True, check=True)
    (OUT / "admission_witness.log").write_text(result.stdout)
    cut_summary = collections.Counter((r.get("camera_valid"), r.get("camera_cut"), r.get("cut"),
                                       r.get("taa_history")) for r in motion.values())
    sector_summary = collections.Counter((r.get("status"), r.get("name"), r.get("camera_check"),
                                          r.get("anchor_check")) for r in sectors.values())
    source_header = SOURCE / "src/renderer/fog_volume_math.h"
    record = {
        "generated_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "log": str(LOG), "log_mtime_utc": dt.datetime.fromtimestamp(LOG.stat().st_mtime, dt.timezone.utc).isoformat(),
        "log_sha256": sha256(LOG),
        "source_commit": subprocess.run(["git", "-C", str(SOURCE), "rev-parse", "HEAD"], text=True, capture_output=True, check=True).stdout.strip(),
        "source_header": str(source_header), "source_header_sha256": sha256(source_header),
        "f8_range": [START, END], "f8_frames": len(expected),
        "camera_rows": len(cameras), "camera_valid_reason_cut": {"|".join(k): v for k, v in collections.Counter((r["camera_valid"], r["camera_reason"], r["camera_cut"]) for r in rows).items()},
        "gram_max": max(r["gram_error"] for r in rows), "det_min": min(r["determinant"] for r in rows), "det_max": max(r["determinant"] for r in rows),
        "helper_witness": result.stdout.strip(), "compile_command": command,
        "motion_rows": len(motion), "motion_camera_cut_history": {"|".join(k): v for k, v in cut_summary.items()},
        "sector_rows": len(sectors), "sector_status": {"|".join(k): v for k, v in sector_summary.items()},
        "f8_fog_rows": fog_rows, "f8_card_rows": card_rows,
        "session_fog_reasons": dict(all_fog), "session_card_states": {"|".join(k): v for k, v in all_cards.items()},
        "pixel_queries": 0,
        "limits": ["camera values are logger-precision values, not raw D3D matrix bytes",
                   "volumetric_fog_frame is sparse: no such row lies in the F8 interval",
                   "one card-state row samples F8 state but does not enumerate every FogPass transaction",
                   "no image pixel statistic can isolate the fog contribution from this composited capture"],
    }
    (OUT / "result.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record, indent=2))

if __name__ == "__main__": main()
