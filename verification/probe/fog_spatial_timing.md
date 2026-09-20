# Actual production fog transaction timing

This separate executable includes the frozen R2 correctness fixture utilities and
links the same production FogPass/programs with the reviewed production asset
decoder. It does not edit or rebuild R2. Native Windows runtime remains unverified.

The fixture keeps four inputs resident at each resolution and one FogPass per
family (two fixture-resident atlases, one atlas and one decoded CPU buffer per
pass). This avoids family upload/decode work inside the alternating-view batch;
it does not propose a two-atlas production cache. Inputs reuse the immutable
prototype timing workload: four actual 1280x768 captures and four nearest-resized,
capture-derived 1920x1080 performance-only inputs. The latter are not native1080
captures or additional quality/TAA acceptance.

Before each transaction the fixture restores the pristine scene, binds caller
MRT/depth and all stream tuples, completes an END-only EVENT prefence, then opens
the caller scene. QPC t0 → actual FogPass execute → QPC t1 → EVENT END and bounded
GetData(FLUSH) completion → QPC t2. The caller closes its scene only after t2.
The pass's EndScene/copy/BeginScene, parameter validation, state capture, 24-step
march, full-pixel repairs, composite, explicit stream restoration and all transient
Releases occur inside t0..t1. Queries are idle throughout execute. Polling remains
inside t0..t2; fenced-minus-submit is only an estimate, not GPU timestamp time.
GPU timestamps are deliberately not collected to preserve the no-active-query
contract; hardware timestamp support is not assessed.

Each resolution has 16 warm and 64 measured, round-robin samples (16 measured per
view), without filtering/retries. Allocation and COM reference baselines are taken
after warmup. Untimed borrowed-open baseline readbacks must match accepted actual
production output at1280 and preserve finite values/alpha at both sizes. Last
measured scene/ST readbacks must exactly match each baseline. Extra scene resets,
witness copies and all readbacks are outside the interval. Five-second fence
failure/timeout or any failed transaction rejects the batch.

Fixed gates: CPU-submit median <=0.25ms at both sizes; EVENT-completed median
<=1.25ms at1280 and <=2ms at1920; 1920 p95 <=2.5ms. Percentiles use NumPy's linear
method. These are detached wall times, not flight FPS. Resource decode+allocation+
upload is reported as a combined field preparation measurement, separately from
target preparation and total input/residency setup. Field and target entries are
CPU wall/submit times; total residency setup includes an EVENT completion. The frozen prepare_field API
does not expose separate internal decode/upload clocks. No subtraction is used.

Host preparation (references frozen data without copying or regenerating it):

```sh
python3 verification/probe/fog_spatial_timing_prepare.py \
  --workload-manifest /tmp/x3-fog-volume-gpu/build/fog-volume-gpu-timing/data/manifest.json \
  --accepted-report build/fog-production-r2/numeric-bound/report.json \
  --output build/fog-production-timing-source/data
```

After independent source review, host build with optimized reviewed root decoder:

```sh
python3 verification/probe/fog_spatial_timing_build.py \
  --asset-root /Users/asvetl/x3-mod \
  --asset-data /tmp/x3-fog-assets-integration-build/generated/fog_field \
  --output build/fog-production-timing-r1
```

Root alone runs the executable, serialized:

```sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py \
  python3 verification/probe/fog_spatial_timing_run.py \
  --build build/fog-production-timing-r1 \
  --data build/fog-production-timing-source/data \
  --output build/fog-production-timing-r1/readback
```

The manifest binds case routing, input/source hashes and accepted production
readbacks. The build record binds compiler flags/toolchain, exact compiled inputs,
resource packets and executable. Execution binds those records; reanalysis checks
the same original bindings and emits a distinct report with current checker hashes.
Focused host checks: `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_fog_spatial_timing`.
