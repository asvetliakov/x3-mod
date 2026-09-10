# Consecutive turning capture: camera and motion evidence

The schema-2 user-assisted capture contains **two four-frame bursts** on device
lifetime ID 1: 3021–3024 and 3398–3401. Unlike the earlier sparse flight capture,
all six adjacent transitions show camera **rotation and translation** changes.
The named world/view-inverse/WVP convention remains numerically consistent.
This establishes useful temporal inputs, not a finished TAA or validated motion
buffer. No game rendering was changed for this offline analysis.

## Inputs and reproducibility

Raw input is local `X3/x3-modern-captures/session-20260910-210505-212.log`.
Its bytes at analysis have SHA-256
`71e0a99b98a4b5d65fbb6f565c1218f720f811dea832e03060ecb94d790eb69d`.
All required metadata already exists in `verification/results/shader-registers.json`;
re-extracting CTAB produced identical metadata, so no duplicate metadata artifact
was retained. Metadata SHA-256 is
`5c78f49db7aa4732cc6dcc46c59d98cf7e4fc3978cfaf17558d61d44cdb8051a`.
Raw shader bytes, mutable buffers and the full capture log remain untracked.

Derived reports:

- [Camera numerics](../../verification/results/game-turning-camera-numerics.json)
  records all per-frame camera groups, factorization residuals and source hashes.
- [Motion observations](../../verification/results/game-turning-motion.json)
  records consecutive camera changes, unique candidate matches, ambiguous keys,
  transform changes and synthetic local-origin projection changes.
- [Motion test results](../../verification/results/motion-analysis-tests.txt)
  cover resource/range/device/topology keys, draw reordering, duplicate rejection,
  changed world transforms, camera angles and failed/UP stream exclusion.

```sh
python3 tools/analysis/analyze_camera.py \
  "$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/x3-modern-captures/session-20260910-210505-212.log" \
  --metadata verification/results/shader-registers.json \
  --output verification/results/game-turning-camera-numerics.json
python3 tools/analysis/analyze_motion.py \
  "$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/x3-modern-captures/session-20260910-210505-212.log" \
  --metadata verification/results/shader-registers.json \
  --output verification/results/game-turning-motion.json
python3 -m unittest discover -s verification/analysis -p test_motion_analysis.py -v
```

## Camera factorization survives turning

Using register float4s as rows of column-vector matrices, the relationship remains
`WVP = P * inverse(ViewInverse) * World`. See the
[convention and precision discussion](camera-numerics.md). Each camera group uses
one median recovered projection across different world matrices; no draw index
is assumed to identify an object.

| Frame | Total draws | Named factorization draws | Exact camera groups |
| --- | ---: | ---: | ---: |
| 3021 | 67 | 50 | 2 |
| 3022 | 68 | 51 | 2 |
| 3023 | 69 | 52 | 2 |
| 3024 | 69 | 52 | 2 |
| 3398 | 111 | 93 | 3 |
| 3399 | 121 | 103 | 3 |
| 3400 | 126 | 108 | 3 |
| 3401 | 131 | 113 | 3 |

All eight frames complete with successful Present and successful captured draw
results. No named camera factorization query is rejected. The largest relative
reconstruction error across all groups is **2.67e-7**. The small-coordinate and
large-coordinate camera groups still share the same captured rotation and
approximately 10,000× different translations. The second burst additionally
contains the near-identity camera for 13 final depth-disabled effects draws.
Their indices shift from 99–111 to 119–131; fixed indices cannot identify this pass.

The following forward-axis angular changes normalize each camera's third basis
column before taking the angle. This avoids pretending slightly non-orthogonal
captured matrices are exact rotations. Translation distances are in the
large-coordinate regime's unknown engine units; no wall-clock velocity is inferred.
The diagnostic selector chooses the group with largest camera-translation norm,
which works for these captures and is not a proposed production classifier.

| Adjacent transition | Forward-axis turn | Translation distance |
| --- | ---: | ---: |
| 3021→3022 | 0.78177° | 38.3906 |
| 3022→3023 | 0.78163° | 38.3201 |
| 3023→3024 | 0.80959° | 39.6153 |
| 3398→3399 | 1.19295° | 52.3262 |
| 3399→3400 | 1.34355° | 58.8524 |
| 3400→3401 | 1.42639° | 62.3605 |

## Resource correspondence: useful candidates, substantial ambiguity

The motion analyzer forms a key from device lifetime ID, buffered geometry,
stream resource IDs/offsets/strides/frequencies, index resource/range, draw
arguments, topology, primitive count, vertex declaration, shader pair, textures,
render/depth targets, viewport and render states. It excludes pointer addresses,
world matrices and draw indices. It accepts only keys occurring **exactly once
in each frame** of a comparison. Duplicate occurrences are reported, not paired
by order or nearest transform.

| Transition | Unique candidate matches | Same world matrix | Changed world matrix | Shared ambiguous keys | Matches with shifted draw index |
| --- | ---: | ---: | ---: | ---: | ---: |
| 3021→3022 | 20 | 10 | 10 | 1 | 0 |
| 3022→3023 | 20 | 10 | 10 | 1 | 0 |
| 3023→3024 | 20 | 10 | 10 | 1 | 0 |
| 3398→3399 | 44 | 25 | 19 | 10 | 20 |
| 3399→3400 | 54 | 32 | 22 | 10 | 25 |
| 3400→3401 | 54 | 32 | 22 | 15 | 15 |

Some repeated geometry/material keys have many different world matrices in one
frame. Resource identity is therefore demonstrably insufficient as object
identity. In the second burst, many unique candidates also shift draw indices
by five or ten positions while retaining the same resource/range key.

Unchanged world candidates provide useful camera-motion checks: for example,
draw 28 in frame 3398 matches draw 38 in frame 3399 with identical world matrix.
Its synthetic local origin projects by approximately **(+10.766, +0.684) pixels**
using the recorded WVPs at 1280×768. These are algebraic predictions for an origin,
not measured pixels or proof that this origin is part of the mesh. Multiple draw
keys may share a world matrix, so these counts do not count independent objects.

Changed-world candidates show why camera-only reprojection is incomplete. For
example, the candidate at draw 60 in frame 3398 and draw 70 in frame 3399 changes
world translation by approximately `(0, -0.6501, +34.4395)` while its linear world
part is unchanged. Draws 18→18 in the first burst change both translation and
linear world coefficients. Early small-coordinate worlds also move with the
camera; a changed matrix alone does not identify an independently moving ship.

## What remains unproven

Even a unique resource/range match may correspond to another instance that
replaces a disappearing one. Vertex buffers can be rewritten without changing
identity, and shader constants may animate vertices beyond the world matrix.
This capture does not record buffer contents/write generations or engine object
IDs. Duplicate keys, newly visible geometry, particles and UI need explicit
policies before per-object history is safe.

Next useful checks are instruction-level position consumption on a known scene
shader; stable instance correspondence or a rejection policy for ambiguity;
GPU scene-depth sampling; and a reversible scene-only jitter experiment with
unjittered effects/HUD. Visible pixel tracking, depth reconstruction, moving
object vectors, disocclusion rejection and temporal resolve remain unverified.
