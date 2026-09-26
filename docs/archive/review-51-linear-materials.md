# Review 51: scene-linear Argon materials

Independent source and evidence review, 2026-09-13. Scope: the first Argon
material shader conversion, MotionOutput registration and draw routing, sampler
state tracking, launcher controls, structural and numerical fixtures, and the
retained production candidate. The previously reviewed offline profile proof and
reference model were reused without rerunning their 33 tests.

## Result

**The opt-in first material slice is approved with no open finding.** It covers
nine original shaders and ten Argon SM3 DEFAULT-pool VS/PS pairings. Admission
requires MotionOutput, FP16 HDR, AgX, gamma-2.2 input decode, the exact reviewed
pair, and known linear sampling on stages 0--3. Refusal retains ordinary temporal
motion, and feature-off behavior remains unchanged.

The transformer starts from immutable original bytecode, proves the original
instruction partition, and merges the independent temporal edits with the
material edits. The material RGB path uses full-precision VS `o8` / PS `v7`;
the original `COLOR0.w`, texture fetches, alpha chain, position and temporal
outputs remain intact. Sanitization uses ordered source-first MAX/MIN operations,
an exact nonpositive-to-zero path, a 65504 cap and bounded finite shader-local
gains. Directional and point inputs are decoded; the game's already-scaled
material emissive retains authored strength, while the lightmap is decoded
separately. The final linear sum is bounded before compatibility encoding.

Combined shaders are cached beside ordinary motion variants and require an exact
registered pair. Creation and replacement release all owned COM references,
including non-null outputs from failed creation calls. A combined bind failure
restores the original pair and permits one ordinary temporal retry. Sampler sRGB
state is cached at attach, successful setters, Reset, and state-block boundaries;
unknown or enabled state refuses conversion. Admission adds no per-draw COM
query, allocation, shader hash, or bytecode validation.

## Findings resolved during review

| # | Severity | Finding and resolution |
| --- | --- | --- |
| R1 | low | Material configuration initially inherited permissive legacy parsing and stale hook comments. The live gate now validates explicit bounded `agx`/`1` and gamma-2.2 values, and the comments describe the material-driven sampler hook lifecycle. Failed shader creation also releases a non-null returned object symmetrically. |
| R2 | medium | The GPU evidence did not initially bind two semantic inputs. The runner now records and rechecks both `motion_output_profiles.h` and `linear-material-profiles.json`. |
| R3 | medium | The first numerically successful GPU run lingered during teardown. The fixture now releases all D3D objects before destroying the window and unloading D3D9, prints `RESULT` only after cleanup, and the clean rerun exits zero. |
| R4 | medium | The first live Reset scenario rewrote all four sRGB sampler states before each draw, masking whether Reset resynchronization worked. Frames 0 and 10 now omit those setters, so attach and successful Reset getter refresh are required for admission. |
| R5 | low | The live runner initially checked input hashes only before execution. It now rechecks the fixture, seam DLL and local shader programs after the run. |

## Evidence assessment

The structural driver covers **72 variants with 749 checks** and reconstructs
every original instruction except the intended RGB edits. It proves alpha,
position, comments, relative-loop operands, resource declarations, exact
temporal-byte identity, select polarity and the new full-precision varying.
Maximum weighted instruction counts are VS 77 and PS 164, below the SM3 limit of
512.

The detached [GPU result](../../verification/results/bottle-X3/linear-material-gpu.json)
passes **167 cases, 1,503 RGB samples and 42,752 exact alpha/temporal pixels**.
It covers all ten pairs, both face signs, zero/one/eight lights, fixed point
lighting, isolated sources, gains, HDR values, half endpoints and exceptional
inputs. All 56 shader creations succeeded; maximum RGB error used 15.9% of the
specified tolerance. The final fixture exits cleanly.

The [live-route result](../../verification/results/bottle-X3/linear-material-live.json)
passes **1,220 checks over 96 frames and eight feature/ownership/TAA twins**.
Each enabled case admits six frames and refuses six. It covers attach and Reset
sampler refresh, recorded/applied/restored state blocks, a shared temporal VS
with a non-Argon PS, immutable gains, exact alpha and RT1/RT2 twins, and final
zero device/factory references. Enabling materials adds exactly two held shader
references in every twin and retirement removes them.

I independently ran the **17 new focused tests**: four transformer tests, three
live control-flow tests, eight GPU report/parser tests and two live-result twin
tests. All passed. The unchanged capture/bloom lifetime test also passed while
checking the touched capture path. Production translation units were separately
compiled warning-clean with the required x86 SSE2 and stack flags. Scripted host
tests cover unknown sampler getters and shader creation, bind and restoration
failures.

The one retained clean candidate is SHA-256
`aeb40a3e5759b4e6b96d0840c8e8fffcf843985984faa5e85e14787cc27a755a`
(12,345,449 bytes). Its clean build passed in 6.933 seconds, the x87 audit reached
211 functions with zero violations, its import inventory stayed at 194 symbols from
15 DLLs, and the focused load smoke passed 8 checks over 17 exports.

## Limits

The GPU and live-route evidence uses detached synthetic scenes. It does not
verify native Windows execution, game appearance, gameplay, installed behavior
or frame rate. GPU timings are diagnostic submission/completion measurements,
not game FPS. Unknown-getter and injected creation/bind/restore failures remain
host-only tests. This slice does not establish legacy emissive tint colorimetry,
linearize every scene writer, or provide verified HDR display output.

This review performed no Wine run, production build, installation, game launch
or commit.
