# Production spatial fog fixture

This detached fixture links `src/renderer/fog_pass.cpp`, its generated programs,
and the production `fog_field_assets` decoder with both baked RCDATA resources.
The old `fog_pass_fixture.cpp` and `run_fog_pass.py` parser remain historical;
their entrypoints refuse to build/run the superseded analytic pass.

Only the root Wine queue owner runs shader generation or `--run-existing`.
Never launch the game. Native Windows runtime and game/TAA behavior remain
unverified by this fixture. Use a new build and output prefix for every changed
executable; frozen source inputs, input captures and readbacks are never replaced.

Host build, after the existing shader generator has refreshed both fog programs:

```sh
python3 verification/probe/fog_spatial_build.py \
  --asset-root /tmp/x3-fog-field-assets \
  --asset-data /tmp/x3-fog-assets-integration-build/generated/fog_field \
  --output build/fog-production-r2
```

An integrated checkout can use itself as `--asset-root`; `--asset-data` is its
CMake `generated/fog_field` directory. The build refuses stale shader provenance
and any existing fixture executable. It cross-compiles with SSE2 and the four-byte
incoming stack contract. Resource packets are linked into the executable; atlas
files in the reference dataset are used only by the host numerical checker.

Prepare immutable references to the four qualified actual inputs (no raw copies):

```sh
python3 verification/probe/fog_spatial_run.py \
  --data /tmp/x3-fog-volume-gpu/build/fog-volume-gpu-composite-r2/data \
  --prototype-root /tmp/x3-fog-volume-gpu \
  --build build/fog-production-r2 --output build/fog-production-r2/state-bound \
  --prepare-only
```

Then root runs the same arguments through `wine_lock.py`, replacing
`--prepare-only` with `--run-existing --state`, and setting
`X3M_FIXTURE_BOTTLE=X3`. Repeat preparation with a new `numeric-bound` output
and run without `--state` for four views × ten variants: qualified default,
borrowed-open default, linear encoding, isotropic and g=.9 phase, colored E,
all invalid geometry depths, and invalid half-depth neighbors with valid full
pixels; dark-map and all-lit-map versions of that same full-pixel repair.
The dark-map repair is compared to a CPU zero-incident-radiance reference
while retaining transmission; all-lit repair must exactly match unshadowed
repair and differ from dark repair. Numeric analysis follows the run automatically; `--analyze-existing`
reanalyzes unchanged readbacks into a distinct report filename.

State checks cover hostile state, all stream tuples and auxiliary/depth bytes,
scene suspension and ordinary recovery, explicit injected loss versus real
blocker-induced failed Reset and retry, partial allocations/uploads, resource
lookup failure, one field cache, stable warmed references and full CPU/LastError
preservation. Preparation wall times are separate from execution; no GPU timing
or game FPS claim is made here. Numerical gates retain qualified ST and composite
limits, exact alpha and actual-input empty identity. Full repair ST is not
separately read back; CPU float32 empty repair identity is checked independently.

Focused host checks:

```sh
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_fog_spatial_reference
c++ -std=c++17 -O2 -Wall -Wextra -Werror verification/probe/fog_spatial_math_fixture.cpp -o /tmp/fog-spatial-math
/tmp/fog-spatial-math
```

The root-requested 32-frame production replay uses the same executable and frozen
`/tmp/x3-fog-sequence-inputs/build/fog-volume-sequence/data-i` dataset. Prepare a
new `sequence-bound` output with `--prepare-only --sequence`; the reader selects
exactly its 32 `captured_frame_ids` and excludes the separate seam fixtures.
Run that prepared output without `--state` only after state and four-view gates
pass. The ten variants remain enabled at 120x72; existing-TAA replay consumes
only `<frame>-v0.composite.rgba16f`. Preparation is not runtime evidence.

`inputs.json` binds both the physical input hashes and the exact executable
`cases.txt` routing. Original execution records bind that prepared manifest,
routing, frozen executable and build record before launching; analysis rejects
mismatches, and its report records runner/reference hashes. Modified analysis
uses a new report path and never overwrites the original runtime report.


Spatial shaft qualification adds actual R32F map cases to `--state` (132 named
checks): dark/lit independently at s4–s6, stale/unavailable/malformed fallback,
fractional 2×2 PCF, blending into lit/dark coarser maps, exact T/empty identity,
CPU preservation and no retained map references. Hostile state includes all
seven used samplers and 22 modified pixel constants. Numeric mode has 40
variants and 88 checks over four views. The separate host captured-map witness
is `fog_shadow_replay.py`; its optional `--step 2` selects actual half-grid rays.
It neither launches Wine nor establishes GPU/flight appearance.
