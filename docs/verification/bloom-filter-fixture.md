# Standalone bloom GPU fixture

2026-09-13. `verification/probe/bloom_filter_fixture.cpp` and
`run_bloom_filter.py` are a numerical fixture for the authored bloom kernels.
They do not use the proxy DLL, game assets, engine hooks or the installed DLL.
The first GPU execution completed all cases but was rejected by the numerical
oracle. It does not establish GPU qualification.

The runner expands the production shader includes with the existing generator,
then cross-compiles an isolated x86 EXE using SSE2, SSE floating-point math,
four-byte incoming stack realignment, warnings as errors and no fast-math.
`--build-only` prepares the executable, expanded shader inputs, serialized case
bundle and an explicitly non-passing/non-GPU summary under a fresh temporary
directory. It never invokes Wine.

When execution is separately coordinated, run:

```
python3 verification/probe/wine_lock.py python3 verification/probe/run_bloom_filter.py
```

The runner enforces an ancestor-held Wine lock and checks `game_running()`
before starting the synthetic fixture. The shared bottle helper selects Steam
by default; `X3M_FIXTURE_BOTTLE=X3` selects X3 and its separate results
directory. Do not run either while the user is testing the game. This command
creates only a hidden fixture window/device and never launches X3AP.

## Execution and numerical contract

The EXE dynamically loads the selected bottle's native `d3dx9_37.dll`, records
its actual path through `GetModuleFileNameA`, and compiles the existing quad
vertex shader plus all eight authored bloom programs. Six extraction programs
cover gamma/sRGB/identity decode and generic/even source dimensions; downsample
and reconstruction are shared. Every program is created through D3D9.
The CPU header's validated selector chooses the extraction program, and the
runner verifies all six actually execute.

Documented format/RT/filter checks and SM3 device caps are recorded separately
from the numerical verdict. Source uploads and every intermediate/readback
use `A16B16G16R16F`. Point sampling supplies extraction/downsample texels;
linear sampling supplies reconstruction. All stages clamp, use mip zero and
disable hardware sRGB conversion. The existing quad shader receives explicit
clip-space positions with the D3D9 half-pixel offset.

Every downsample and reconstructed level is read back separately. Final
full-resolution tent reconstruction reuses the upsample shader with scatter
one and a zero fine image. This redundant fine fetch is fixture-only. It
does not test base-scene addition, AgX output, alpha ownership at the game's
main surface, engine state restoration or compositor skipping.

The 40 authored cases cover black, DC, gamma/sRGB/identity decode, level-count
and scatter endpoints, odd/thin/1×1 impulses, chroma, hard/soft threshold,
exposure/clamp ordering, the sRGB breakpoint, deterministic random content,
FP16 maximum and extraction-only nonfinite sanitization. Wide one-pixel
geometry cases at widths 82, 8462 and 15611 target the reviewed large-coordinate
sampling regression. Cases beyond device texture dimensions are explicitly
reported as skipped; only these large geometry cases may skip. The report
records the maximum dimensions actually admitted by the GPU. The independent
host geometry sweep remains required for the complete 16384-bound contract.

The independent Python oracle keeps the unoptimized nine-tap tent. It rounds
the uploaded source to FP16, uses float32-serialized host parameters, and
rounds **each stored intermediate** to FP16 before consuming it at the next
level. Each GPU RGB channel must remain finite, nonnegative, at most 65504,
and within the predeclared tolerance:

```
abs(actual - expected) <= 0.002 + 0.003 * abs(expected)
```

This allowance covers FP16 quantization, SM3 full-precision arithmetic/power
approximation and hardware bilinear precision. It must not be widened just
to fit a failure. Black scenes additionally require exact zero; bloom alpha
must always be exact zero. The separate 1×1 HDR/DC and nonconstant odd-ramp
cases are reported as sampling/format self-tests, independently of caps bits.

All cases run twice with a device Reset between generations. Per-case cleanup
unbinds textures and restores the backbuffer before releasing owned resources.
This exercises fixture resource lifetime, not caller-state restoration of a
production pass. There is no game FPS or performance measurement in this run.

## Evidence and host controls

The runner retains raw readbacks, compiled shader bytes, the EXE, expanded
inputs, logs and its full report locally. It publishes a per-bottle summary
with source, executable, case-input, expanded-source, requested/actually-loaded
compiler, runtime-module and output hashes. Source/input/runtime identities
are checked again after execution, including failed runs. Hashes record
provenance; they never gate normal feature support on exact system-DLL bytes.

`verification/analysis/test_bloom_filter_fixture.py` has four host controls:
case serialization/float widths and nonfinite payloads, explicit oracle
quantization/stage completeness, numerical verdict negative controls (shifted
pixels, bright error, NaN/Inf, negative RGB, wrong alpha and truncated data),
and strict JSON metadata/nonfinite-extraction scope. These controls do not
simulate GPU filtering or establish native Windows runtime behavior.

Host commands:

```
python3 -m unittest discover -s verification/analysis -p test_bloom_filter_fixture.py -v
python3 verification/probe/run_bloom_filter.py --build-only
```

The four host controls and isolated cross-compilation pass. The first Steam
GPU run completed all 40 cases in both generations, including Reset, all six
extraction variants and 540 stage readbacks. No dimension case skipped; the
reported device texture limit was 16384 and the largest tested width 15611.
Both sampling self-tests passed, as did every downsample stage. However,
38 reconstruction images failed the fixed tolerance, across the three bright
geometry cases and the sRGB-breakpoint case. Inputs stayed unchanged.

The [compact rejected-run record](../../verification/results/bloom-filter-first-gpu-failure.json)
binds the full locally retained report and lists rejected stages. It records a
failed run; shader creation and successful API calls do not establish numeric
acceptance. Store rounding and hardware interpolation precision are being
characterized independently before changing the oracle or filter. The fixed
tolerance has not been widened. Renderer integration and game appearance
remain untested.
