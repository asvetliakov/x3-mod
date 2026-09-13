# Linear distance-fade composition: Run 27 investigation

Status: reviewed detached prototype with X3 GPU qualification, 2026-09-14.
Runtime admission, shared temporal-mask integration and installation are pending.
This note owns the fade-route proposal; the [coverage ledger](material-coverage.md)
owns the captured material population. The [native fade analysis](../reverse-engineering/asteroid-fog-temporal.md)
already establishes the engine mechanism, so another broad trace or disassembly
sweep is unnecessary before a bounded prototype.

## What the capture proves

Run 27 is `/tmp/x3-bottleX3-run27/session-20260914-000122-216.log`.
The compact local reductions are `/tmp/x3-run27-draw-analysis.json` and
`/tmp/x3-run27-asteroid-constants.json`. In frame 20744:

| Draw | Node / model | Native state | Proxy outcome |
| --- | --- | --- | --- |
| 42 | `1af313b0` / `4fef` | Z-write on, blending off, RGBA writes; VS b0 false | Opaque motion/depth and linear material route |
| 43 | `1af31630` / `4fee` | Z-write off, SRCALPHA / INVSRCALPHA ADD, RGB writes; VS b0 true | Motion gate 4; no material attempt; jitter remains active |

Both use reviewed pair `167eb2d5629ab9d3/d44db87778a43b61`, LOD 0,
1,712 triangles, 1,002 vertices and the same four texture bindings. They are
**different nodes and models**, coexist in the same frame, and have different
transforms and detail/base weights. This is not a captured same-object LOD or
brightness transition, and association with the selected target is unproved.
The 1,700 explicit material refusals elsewhere in this capture are the glass
pair `c30104cb0efb6675/a66fb1981ba755b2`, reason 1. They do not count this earlier
state-gate bypass; there is no missing-variant, sampler or combined-bind failure
explaining this asteroid draw.

For the far draw, c39.x is 1 and c41 is approximately
`(1.0526316166, 2.10526366e-7, 0, 0)`. Together with b0=true these establish the
reviewed vertex fade `saturate(c41.x - c41.y * vertexDistance) * c39.x`.
They encode approximately N=250,000 and F=5,000,000 shader-world units, not the
measured distance of this object. The original PS multiplies this interpolated
alpha by the base diffuse texture alpha. Native submission enables the fog path
from **node-origin distance**, while the VS computes alpha per vertex. The
engine can therefore switch states while mesh vertices already have different
fade values. Base texture alpha must not be replaced with 1.

The four textures have 11 mips; minification is anisotropic, magnification and
mip filtering are linear, max anisotropy is 16, sRGB sampling is false and mip
bias is zero. Run 27 also has proxy mip bias disabled. This excludes a proxy
negative-bias transition here, but does not establish that normal/specular or
texture minification is temporally stable. Fixed-function `FOGENABLE` was not
captured; VS b0 is not proof of that separate D3D state.

## Why the present policies can disagree

`MotionOutput::evaluate_draw` applies reviewed VS jitter before its opaque
admission. Gate 4 then requires depth writes, no alpha blend/test and RGBA
writes, among other conditions. The far draw returns there, before combined
material availability is considered. Its original native RGB lighting remains,
whereas eligible opaque draws use linear lighting encoded back into the
engine-compatible gamma-2.2 FP16 target. An object entering these state regimes
can consequently change its **source-lighting domain** as well as following the
engine's intentional alpha fade. The captured different objects establish the
two policies, not the magnitude or identity of a visible transition.

The translucent writer also leaves RT1/RT2 unchanged. Over an opaque surface,
its color mixture inherits that underlying surface's correspondence/depth;
where no opaque writer populated those targets, it inherits the sentinel and
camera far-plane path. Frame 20744 resolves TAA with valid camera policy 2 and
history enabled. It is incorrect to describe every fade pixel as far-plane.
Neither inherited correspondence represents both moving layers in general.
This is a plausible shimmer/history contributor, not a demonstrated pixel-level
cause. The Asteroid shader's per-vertex fade, normal-map specular response and
mip aliasing remain separate possibilities. No station-specific fade contract
or target identity has yet been established by this asteroid evidence.

## Proposed bounded repair

Start with the six reviewed [Asteroid contracts](linear-asteroid-materials.md)
and their exact ordinary source-over fade state. Keep native depth test, disabled
depth writes, primitive order, original alpha and caller state. Do not relax the
opaque motion gate or force a depth-writing replacement. Other reviewed material
families can enter this route after their actual native fade contract is checked;
a blanket translucent/glass admission is not supported by these observations.

Use three simultaneous source MRT bindings, B/E/M, with a four-surface full-size
FP16 B/E/C/M pool. A is the separately owned current compatibility-encoded HDR
scene. C is a distinct composition destination: sampling A while rendering to A
is invalid, and overwriting B would destroy native recovery. Prepare B as an
exact copy of A, clear E to zero, and leave M detached during preparation.

Prefer the current emission pass's single pool owner, serialized across fade and
emission brackets, with an explicit composition-policy selector. Do not allocate
a second pool or permit nested users. If both routes are active, this shares the
existing B/E/C/M storage; if only fade is active, it still needs all four surfaces
(32 bytes/pixel, approximately 63.3 MiB at 1920x1080), in addition to A. C transfers
through the existing HDR owning-slot exchange and acknowledgement; the previous
A returns to that slot for reuse. B stays separate until successful publication
and restoration or certified native recovery. All four resources participate in
reference accounting, resizing and Reset. Each bracket still pays full-size
A-to-B copying/E clearing and A/E-to-C composition traffic, plus source MRT
writes; storage reuse does not remove that bandwidth cost.
A dedicated paired shader variant must retain the full original native RGB and
alpha path for B while additionally producing unencoded linear source RGB L and
the same source alpha a for E. Prove oC1.a equals the actual native oC0 source
blend factor, including the original direct `_pp` alpha output's precision;
recomputing an algebraically equal full-precision alpha is not enough. The
existing opaque combined variant is not that
producer: it rewrites native lighting and vertex RGB. Its native path cannot be
recovered merely by rebinding the original PS. Cache the new exact variants at
creation/bind boundaries, with no per-draw transform or shader lookup.

For each fragment, output native source to B, `(L, a)` to E, and positive RGB
with alpha 1 to M. Keep native RGB source-over blending. Enable separate alpha
blending ONE / INVSRCALPHA for E's accumulated coverage, with independent write
masks B=RGB, E=RGBA and M=RGB. B's native alpha remains unwritten. Starting at
E=(Q,q)=0, overlapping primitives within the **same** original draw then give:

```
Q' = a * L + (1 - a) * Q
q' = a     + (1 - a) * q
C.rgb = encode(Q + (1 - q) * decode(A.rgb))
C.a   = B.a
```

This preserves ordered source-over transmittance, including self-overlap; using
ordinary alpha blending on E.a would accumulate a squared alpha instead. The
formula is the interior-coverage rule, not permission for an unconditional
full-screen decode/encode. For scalar q equal to exact +0, select raw A.rgb
without conversion, preserving its FP16 bits outside raster coverage and for
zero-alpha draws. For q equal to 1, compose from Q alone; do not evaluate an
arithmetic `0 * decode(A)` that can import a nonfinite background. This establishes
background independence at full coverage, not bit-exact equality to the opaque
shader: native/linear alpha precision, intermediate rounding and caps must pass
the shader oracle before any stronger continuity claim.

Use the existing ordered RGB sanitizer S(x)=MIN(MAX(x,+0),65504), including its
qualified NaN/infinity ordering, on L at the linear FP16 source boundary and on
accumulated Q before conversion. Apply the same safe gamma transfer and finite
RGB policy to the background and composed result for interior coverage. Keep
native B RGB/alpha untouched by this sanitizer. The valid alpha domain and the
duplicated `_pp` result must make the q recurrence finite in [0,1]. Define hostile
scratch q deterministically with an ordered [0,1] clamp (NaN/negative infinity
to +0, positive infinity to 1), then use the same endpoint branches; this is a
malformed-input fallback, not proof that a native nonfinite blend factor has
meaningful parity. Keep conservative M coverage regardless of zero alpha/gain.

Compose immediately at the original draw position. Decoding already blended B,
or blending encoded linear source RGB with the native fixed-function blend,
does not implement these equations. This repairs composition in the current
compatibility scene; it does not make unconverted native writers physically
linear.

D3D9 applies blend states to all MRTs and allows independent write masks only
with the corresponding capability. Require NumSimultaneousRTs >= 3,
MRTPOSTPIXELSHADERBLENDING, FP16 post-pixel-shader blend format support,
SEPARATEALPHABLEND and INDEPENDENTWRITEMASKS explicitly.
Require fixed-function fog/dither off, no MSAA and the exact admitted blend,
write-mask and alpha-test state. Shader fog remains untouched. Validate native B
under these states on actual D3D; the independent alpha update is safe only
because the admitted original RT0 alpha mask is off. These are documented D3D9
contracts, not backend-private assumptions. [Microsoft MRT documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d9/multiple-render-targets),
[render-target alpha](https://learn.microsoft.com/en-us/windows/win32/direct3d9/render-target-alpha),
[capabilities](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dpmisccaps).

Reuse the reviewed [emission bracket's](linear-emission-composition.md) owning
slot exchange/acknowledgement, source-once and recovery rules, with an explicit
source-over composition policy rather than silently changing additive emission
semantics. Preserve the first failed HRESULT in chronological order: a failed
source dominates later cleanup errors; an earlier failed preparation/restore is
not overwritten by a subsequent recovery result. Invoke a source at most once,
including after successful source submission followed by composition failure.
Failed pre-source restoration suppresses source submission. After source,
adopting B requires successful target binding and state restoration; merely
possessing B does not certify recovery. A failed source remains Incomplete even
if best-effort B continuation succeeds, and cannot certify a complete mask.
Clean refusal before submission with restored A preserves earlier mask contents;
missing required source coverage still prevents enhanced temporal history. Keep
the existing reader/export quarantine, Reset and ownership rules. An immutable unsupported capability may
retain the baseline route before activation; it is not support for enhanced fade
on that device.

## Temporal safety and the remaining shimmer objective

Use the single shared pool owner for M and one combined frame-completeness
status. Determine the effective fade/emission producer set at frame start; its
owner clears M exactly once before either producer can submit. A later fade or
emission bracket must not clear earlier coverage. Fade alone owns this lifecycle
when emission is disabled or immutably unsupported before activation. An active
producer's transient refusal/failure cannot silently remove it from the required
source set. Clean pre-source refusal preserves existing M bytes, but if a
required draw goes uncovered the combined frame is incomplete. Failed source,
unrecovered state, or an uncertified fallback-B/post-source failure likewise
cannot be erased by another producer's later success. A certified native-B
fallback after a successful source may retain completeness only when its full
same-draw M coverage and restored/published state are established.

Union coverage under the existing source-set-complete
`SupplementalMaskWithDepthSentinel` contract, independently of the emission
enable flag. Preserve RT1/RT2 and native Z-write semantics.
Current and previous canonical masks, including the existing one-pixel expansion,
reject the mixture's invalid history and protect disappearance or return to
opaque rendering. Complete empty coverage is valid. Incomplete coverage uses
`Unavailable` with a null mask, current-only output and no completed history;
`history_allowed=false` alone is insufficient.

This is a safe first composition/temporal stage, not a promise to eliminate
shimmer: rejection can reveal current-frame spatial aliasing. Stable accumulation
of a moving transparent surface over a differently moving background needs
separate layer color/transmittance and appropriate correspondences, or another
explicitly qualified layered temporal method. A fabricated single blended motion
vector is not a proof. Keep that visual acceptance open; do not present reactive
rejection or spatial filtering alone as completed translucent TAA.

Nor can source-domain consistency remove every native threshold discontinuity:
node-origin admission, per-vertex fade and partial diffuse alpha can themselves
change coverage at the state switch. Changing those engine semantics would be a
separate decision requiring same-object evidence, not an implicit lighting fix.

## Qualification and next decision

Before integration, extend the existing detached shader/pass fixture with actual
original b0 off/on, captured c39/c41, alpha 0/partial/1, unequal backgrounds,
multiple overlapping primitives in one DIP and two ordered DIPs. Require exact
native B/alpha (including the duplicated `_pp` blend factor), analytic linear
source-over output, measured opaque-limit precision, and exact raw-A preservation
after repeated zero-alpha and outside-raster operations. Include nonfinite/HDR
backgrounds at q=0/1, finite/cap boundaries, caller-state restoration and
source-once behavior. Include alpha-mask/separate-
alpha refusal, capability refusal, native failure, recovery, exchange and Reset.
Preserve original program budgets; dual native/linear varying and instruction
capacity is a feasibility gate, not grounds to discard native recovery.

Then exercise current/previous fade coverage, changing alpha, disappearance and
opaque return with separate object/background motion and camera rotation and
translation. Cover fade-only, emission-only and interleaved producers, one M
clear, certified native-B fallback, either producer failing after earlier valid
coverage, and Reset/effective-set transitions. Reuse existing supplemental-mask
failure/no-seed tests. Actual
Asteroid normal/detail/specular inputs must be nonzero; diagnose minification
separately from route continuity rather than changing roughness or specular gain
without evidence. Paired completion timings should isolate this bracket at
representative resolutions and counts; do not infer batching safety from old
adjacency or equate component timing with game FPS.

The qualified prototype below precedes runtime integration; a gate-only installed
patch is insufficient. Eventual integrated telemetry should count recognized fade state bypasses
before material-availability counters, using cached state and integer increments.
Existing native RE and this capture already justify the prototype; no additional
broad game load is needed to choose its initial contract. Same-node transition
and station-pass identification remain necessary for later user-visible claims.

## Detached producer and composition prototype

The isolated prototype from `2cf65ae` retains each original native executable
body, then evaluates the reviewed linear body into only COLOR1 / oC1 RGB. The
original direct `MUL_pp oC0.w` is immediately duplicated with only the output
index changed to oC1; there is no alpha reconstruction through a temporary.
The six VS / four PS programs fit at maxima of 131 / 152 weighted slots,
10 / 14 temporaries and four samplers. Host checks preserve all 130 accepted
ordinary originals byte-for-byte over three configurations. They establish
construction and linkage, not driver `_pp` lowering or actual native-B parity.
Stage APIs remain independent; only the six exact pairings are fixture-admitted.

The detached fixture reuses `LinearEmissionPass` under
`X3M_LINEAR_DISTANCE_FADE_FIXTURE` for source-over state, an augmented VS and a
supplied composition program. Production-macro-off pass tokens and embedded
emission programs remain unchanged. The authored, host-exported composite is
150 DWORDs; it performs scalar q endpoint branches with all texture reads before
branching. Its exact input is pinned by the focused runner, which consumes a
prebuilt EXE and CSO without rebuilding or installing anything.

The focused X3 GPU invocation passed 65 cases plus six after actual Reset:
257 designated source calls and 255 prepared brackets. It compares separately
created original raw RGBA with dual native RGBA and direct native/E alpha
in unblended RGBA32F targets (before FP16 storage can hide a difference), and
native B under MRT blending. An independent Asteroid float64 reference plus
FP16 stores checks L, Q, q and C. Cases include fog/zero/full alpha, overlapping
primitives in one DIP, two ordered DIPs, a depth occluder, nonneutral AG normals,
zero gain with positive coverage, eight looped lights, repeated q=0,
HDR caps and nonfinite backgrounds at q=0/1. Five failure seams cover a real VS
setter mutation followed by failure, clean copy refusal, missing-IB native
failure, native-B composition recovery and post-source restoration failure.
Four capability removals and three incompatible source states must refuse
before transfer. Caller state, same-frame M re-clear refusal and resource
retirement are checked.
Diagnostic original/alpha reference draws are outside the designated source-call
count. Combined fade/emission current-and-previous-mask integration is future
work; this pass-only fixture does not qualify transparent TAA or game performance.

The [compact result](../../verification/results/bottle-X3/linear-distance-fade-gpu.json)
retains the original execution and both failure witnesses. R1 stopped at attach:
the fixture supplied an assumed color format as the adapter format; querying the
actual display format corrected setup. R2 completed all GPU cases in 8.588 s
(execution wall time, not a performance benchmark), then the initial CPU oracle
rejected 384 q values because it assumed nearest-even FP16 render-target storage.
Original/dual native RGBA and native/E alpha were already exact in RGBA32F, and
native B, caller state, five failure boundaries and Reset/resource retirement
passed. No shader or executable changed after R2.

An independent reduction identified one fixed **observed X3 render-target
round-toward-zero** rule: all 193,536 Q channels and 64,512 q values match it
exactly. All 10,272 covered C RGB channels also match; 4,718 distinguish it from
nearest-even and all select toward-zero. This is an observed CrossOver X3 store
model, not a D3D9 or native-Windows guarantee. The corrected fixture-local oracle
uses it uniformly for recurrence and C stores; shared reference/upload
nearest-even conversion is unchanged. Seven affected tests cover boundaries,
ties, subnormal/overflow edges and rejection of the wrong q rounding.

After the same independent reviewer approved that correction, a retained-data
reparse passed 580,608 numerical channel checks, 129,024 alpha values, 258,048 mask
values and 183,264 exact raw-A channels. The maximum normalized RGB tolerance
fraction is 0.000071875. R1/R2 raw failures remain local and linked from the
compact record. Native-Windows execution, combined fade/emission temporal-mask
integration and live cost remain open.


## Runtime qualification slice (source under review)

The runtime slice adds an explicit, default-off `--linear-distance-fade`
qualification option. It requires linear materials, motion/TAA and HDR with
AgX gamma-2.2 decoding; additive emission remains an independent option. The
original opaque admission gate is unchanged. Only the six exact Asteroid pairs
with the reviewed depth-read-only, RGB-only source-over state can enter the fade
bracket. Native or unsupported glass, station and alpha-tested draws are not
promoted by this change.

Shader registration retains a separate native/linear dual VS and PS. Setters and
state-block resynchronization cache the exact pairing and required sampler mask;
draw admission adds no shader lookup, compilation or allocation. Source blend
tracking remains active with the optional general state shadow disabled. The
existing composition owner saves and restores the augmented VS along with the
native PS and blend state, then executes the original source once. It reuses its
B/E/C/M pool and the exact qualified 600-byte composite; opaque and additive
shader programs are unchanged.

At the HDR latch, the shared owner qualifies additive and fade policy bits
against the actual adapter display and scene-depth formats. Reset invalidates the
display-format cache. An unresolved query or resource/program allocation failure
keeps the requested producer set incomplete and retries only at a later latch.
A proven immutable unsupported policy can remain native without activating the
supplemental route. Once supported, a temporarily unavailable producer cannot
be removed from the required set to admit partial history.

One owner clears M once before either producer, including an empty frame.
Missing required source coverage stops that frame and selects `Unavailable` with
a null mask. A clean preparation refusal retains earlier M bytes but cannot
claim a complete producer set. Native-B recovery retains completeness only when
its coverage and restoration are certified; an invalid mask cannot be healed by
a later producer. The existing current/previous supplemental-mask resolve,
reader inventory, export quarantine, actual HDR owning-slot exchange and Reset
retirement are shared by both policies, independent of additive enablement.

The performance floor remains material: each prepared source incurs a full-size
copy and composition, with an estimated 56 bytes/pixel of logical traffic per
DIP (55.1 MB at 1280×768 or 116.1 MB at 1920×1080), excluding source raster,
frame-mask clear, depth and driver traffic. The four FP16 pool targets occupy
30.0 MiB or 63.3 MiB respectively; supporting both policies adds one small shader,
not a second pool. Per-frame counters expose eligible/prepared/completed fade
DIPs and that traffic estimate. The planned live comparison holds all other
features fixed, alternates fade-off/on order, and measures fenced source-bracket
and terminal-TAA/AgX completion at 1/4/16 ordered DIPs and both sizes. It does not
infer GPU busy time or game FPS. Actual combined current/previous-mask behavior,
image/state/failure integration and this cost comparison still require the
reviewed live fixture; detached source-over evidence alone does not close them.
