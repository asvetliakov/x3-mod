# FL Background temporal contract

Offline qualification, 2026-10-28. FL executable
`/mnt/c/X3FL-orig/X3FL.exe`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Addresses below are preferred VAs (base `0x400000`), not runtime read authority.
Owning requirement: [Background temporal policy](../../architecture/farnhams-legacy-taa-coverage.md#background-temporal-policy-remains-required).

**Stock stars and nebulae have a native camera-relative placement mechanism;
Background as a whole does not.** Stable admitted components can use the existing
rotation-only sky history. Finite planets/moons and animated or unqualified
components need their own motion or bounded current-only coverage. Blanket
Background current-only remains an unfinished port, not final parity.

This establishes equations and a bounded implementation contract. It does not
activate rendering or claim FL GPU/flight acceptance. The existing reviewed
[camera publication](camera-buffers.md),
[material borrow](material-submit.md), and connected compositor
ownership remain the authority; no new cockpit/scene pointer walk is proposed.

## 1. Camera relationship and geometry placement

| Evidence | Derived contract |
| --- | --- |
| INS string table `0x5a3d98`, cases 5/6 in dispatch `0x432488`; stores `0x43073d`, `0x430784` | Cockpit `+0x58` is sector camera; `+0x5c` is galaxy camera. The handle is resolved by `0x4a2470` before storage. KC `606::ShowSpace` binds these through `0x1885d0/0x1886f9`. |
| Cockpit update `0x421f50`, `0x422cb6..0x422cda` | Both cameras receive the same FOV word at camera `+0x298`. This does not prove equality of their dimension fields or every later writer. |
| `0x422e93..0x422eb6`, helper `0x412c60` | Galaxy base position is the sector base position divided by **10**, signed truncation toward zero: divisor `0xa0000`, helper computes signed `(component << 16) / divisor`. It is not a zero-translation camera. |
| `0x422eba..0x422ed8` | Sector basis at `+0x40/+0x50/+0x60` is copied to the galaxy basis, three 16-byte rows. |
| Perspective builder `0x4dec70`; ordinary setter `0x4ded90` | Both use the same builder, but P also depends on camera `+0x300/+0x304` or display fallback. V rotation is the fixed-point basis transpose; `t = -s C R`, with scene/context scale `s = context+0x2c`. Copy actual owned P/V; never infer equality from identical FOV or viewport alone. |
| KC `2001::DoGalaxyTake`, `0x108210..0x108265` and `0x108296..0x108304` | Star and nebula nodes receive zero base position, size 5,000,000, and Flags2 OR `0x200000 | 0x400000`. These are finite meshes with a placement rule, not vertices proved to lie at infinity. |
| B3D string table `0x5a5e78`, SetFlags2 case `0x2d` in `0x4b297c`; `0x4b0ce6` | Flags2 writes node `+0x130`; GetFlags2 case `0x2b`, `0x4b0c87`, reads that field. This binds the KC flag to the native field. |
| `0x4947b0`, test `0x4947b9`, writes `0x4947ce..0x4947f1` | If `node+0x130 & 0x400000`, effective position at `+0xb0/+0xb4/+0xb8` becomes **base node position + camera base position**. Then the helper computes camera-relative position. |
| World setup `0x4de570`, `0x4deb15..0x4deb66` | Effective position is multiplied by the same context scale used by V. Ordinary submitted world/view multiplication therefore cancels camera translation for an admitted centered node. |
| `0x422edc..0x422f65` | Selected star/nebula model IDs receive basis rows built from sector `+0x160/+0x164/+0x168` through `0x50d400`. Their model orientation is an input, not a promise that the mesh never rotates. |

For row vectors, let local vertex be `q`, effective linear model transform be
`A`, base node position be `B`, galaxy camera position be `C`, and view basis
be `R`. The centered branch yields

```
world = q A + s(B + C)
view  = world R - s C R = (q A + s B) R
```

For unchanged `A`, `sB`, geometry and material, camera translation cancels.
Direction reprojection `P_current^-1 R_current^-1 R_previous P_previous`
(the existing `camera_far_plane_reprojection` implementation uses a genuine
inverse, not an orthonormality shortcut) is consequently valid for this finite
camera-relative mesh. Zero B is the stock witness, but constant nonzero B also
works algebraically. Float conversion/multiply order can leave residual error;
the analytic cancellation is not a measured GPU pixel-error bound.

Without the centering rule, `view = (q A + s B - s C) R` contains translation.
Normal class-4 primary geometry goes to the galaxy scene at
`0x452c56..0x452c78`, but scene membership supplies no centering guarantee.
Planet/moon material names and far-looking depth likewise do not supply it.
The [placement study](depth-writer-selection.md#3-normal-background-versus-scene-placement)
proves Background-before-Scene-clear ordering, not infinity. Keep those draws
excluded unless their actual owned transform supports a stronger contract.

**Owned implementation inputs:** `camera_background_` currently receives a
validated copied state in `MotionOutput::fl_camera_complete(key,false)`;
`camera_scene_` receives the correlated Scene record. Add the matching Background
key and successful-history prior sample. The material's existing 340-byte owned
node copy already includes base position `+0x30`, effective position `+0xb0`,
basis `+0xc0`, scale, flags and model/LOD. `render_engine_state::DrawSample`
currently exposes only effective position, not base position. Exposing the
three base words from that existing owned copy is a bounded source change;
following its camera/parent addresses is not authorized. Alternatively qualify
the submitted clip transform against the owned camera and stable geometry with
an independently tested numerical criterion. A Flags2 test alone is weaker:
model/basis, context scaling and per-draw private projections remain inputs.

## 2. Animation and exact shader limits

Local real-program inspection used the existing token parser and CTAB reader:

| Exact program | Input relevant to history |
| --- | --- |
| VS `7b6393fe2d3e1d85`, `ececc28e00592d35` (128 DWORDs each) | Position uses c0–3 world-view-projection. Two UV dot products at DWORDs 116/120 use **c4/c5 `g_TexMatrix`**, after building the homogeneous UV input. These shader identities are shared by nebula and GUI resources; only actual Background provenance can admit sky. |
| PS `6109cf64c03529dd`, `d0aacb2c65e011ed` (76 DWORDs each) | One diffuse texture sampler, plus **c0 `g_AlphaValue`**. Colour comes from the texture; alpha includes the mutable uniform. They are not time-invariant merely because no time uniform exists. |

These four programs establish a useful stable-sky candidate family, not a
hash-wide sky allowlist or an inventory of all Background contributors.
Moon/planet programs have additional texture/light inputs and retain bounded
exclusion in the minimum implementation. Fog, other nebula variants, unknown
programs and modified resources require separate exact qualification or masking.

Native material code also supplies concrete animation evidence. Negative
material/texture selection enters `0x4e10f2`; the per-node table at
`+0x1ac` has **0x80-byte records**, selected texture word at record `+0x70`
(`0x4e113f`) and texture matrix at `+0x40` (`0x4e1159/0x4e1162`). The
matrix selected into EBP-0x5c is uploaded as `g_TexMatrix` at
`0x4e3e2b..0x4e3e8b`, or identity when absent. Another selection reaches
`0x4e1928..0x4e1930`. This is evidence that frame selection and UV transforms
are material inputs; it does not establish that each stock sky material animates.
Do not extend the native borrow into that table: successful public shader
constant/texture setters and owned resource metadata are the consumer boundary.

For the candidate pair, stable admission needs known UV rows, alpha, sampler
and relevant blend/alpha-test/render state; exact geometry/declaration and
vertex/index content identity; model/LOD, basis/scale/base-position stability;
and texture **content/incarnation**, not just a COM pointer. State-block Apply,
Reset, resource updates, render-to-texture, missing successful setters and
unobserved content changes must revoke the corresponding stability claim.
A pointer reused for another animation frame is not sufficient history identity.
Immutable preflight-backed resources can discharge content stability; otherwise
public write/update tracking is required. No such complete Background signature
is established by current source. First observations do not establish a prior.

Animated/finite contributors may use exact bounded geometric current-only
coverage, including transparent/cutout holes conservatively. A changed stable
contributor needs its old as well as new influence rejected: retain projected
previous coverage, or invalidate Background history on changed/missing contributor
census for that transition. Do not merely mark the current triangles and leave
old sentinel-colour history reusable where the contributor disappeared. Stable
contributors elsewhere can resume/retain history under the chosen bounded rule.

## 3. AP history contract to preserve

The authoritative implementation is `src/renderer/camera_reprojection.h`,
`src/proxy/motion_output.cpp`, `src/temporal/resolve.hlsl`, and
`src/renderer/temporal_pass.cpp`; older prose mentioning loose-by-default or
512-slot ceilings is historical. Current launch policy defaults to **strict**,
confirmed by `verification/analysis/test_taa_sky_history.py`.

1. `camera_sentinel_policy`: Auto uses valid current/prior camera samples,
   rejects failed transforms, and makes a camera cut above the configured
   rotation bound (default **20 degrees**). No valid previous sample means
   policy 1 for bootstrap; a valid pair gives `sentinel_camera`, policy **2**.
   Preserve normal object-displacement/missing-key cuts and dimension/epoch,
   Reset, failed-read/failed-resolve invalidation. Prior camera must correspond
   to the image actually committed to temporal history, not simply the preceding
   Present or most recent activation.
2. The uploaded matrix has **zero z column and no translation**. RT2 sentinel
   <= -0.5 enters far-plane history; invalid depth -0.25 remains invalid,
   never sky. The alpha -1 / sentinel pair can keep the camera path only on
   its own correspondence. Routed alpha 1 is never strict sky. Missing data,
   NaNs, invalid clip W and offscreen correspondence reject history.
3. Existing 3x3 closest-depth dilation and depth proof remain. Strict c7.z=3
   rejects all finite prior geometry depths, including exactly 1, at an
   unrouted sky pixel with no nearer geometry in its current 3x3. History
   sentinel taps can prove; unknown coverage cannot be promoted to sentinel.
4. Preserve **five bilinear Catmull–Rom colour samples**, the exact-grid point
   branch, current 3x3 neighbourhood clipping, luminance-domain rules and the
   matching previous-reactive footprint test. Both current and prior unsupported
   masks must participate before accepting colour/history age. A bilinear depth
   proof alone does not cover the colour filter's wider footprint.
5. Under strict, the silhouette band rejects when routed correspondence differs
   from the rotation-only camera reference by >= **3 px/frame** by default
   (configured range 1..16). Translation-aware rows would defeat this SETA
   protection. The age variants mark accepted moving-band age negative at the
   **0.25 px/frame** exit floor by default; a subsequent strict-sky lookup of
   that mark restarts at current-only/fresh age. Own routed correspondence and
   pure-pan behavior retain their exclusions. Preserve configured motion-weight
   and rotation-aware caps, thin/far/hold/lock behavior.
6. Keep the established D3D9 half-texel and current-jitter convention; no
   previous-jitter term. Background is not Scene-jittered. Qualification must
   compare actual rasterized rotating sky against the resolve convention under
   nonzero Scene jitter, not use an identity-only matrix oracle. Any correction
   needs a separately reviewed pixel-class rule and AP comparison.

See [strict sky/SETA](../../architecture/seta-motion.md) and
[exit reset](../../architecture/seta-sky-hull-share-decay.md) for accepted behavior.
This analysis reuses AP source contracts; it does not requalify AP binary hooks.

## 4. Minimum production slice and qualification

**A. Retain the right camera pair.** Store an owned current Background state and
key, plus its previous successful-history state/key alongside the Scene pair.
Reset/revoke/start-of-frame must not leave an old Background record usable.
Commit both priors only with the same temporal-history publication; fail, unwind,
skip, wrong main pair, nested activation and stale event cannot advance them.
Cross-frame compare stable incarnation/load/scene/camera/device identity, while
requiring each frame's fresh activation/pass/Reset/request generation. Do not
require activation serial equality across different frames. The existing
`fl_camera_complete`/`fl_camera_invalidate` and successful TAA publication are the
bounded source sites; no additional game hook is needed.

**B. Use the Background transform without corrupting Scene camera gates.** For
the first bounded equivalent, require current **and retained prior** Background
rotation and projection-direction terms (m00/m11/m20/m21) to agree with the
corresponding Scene sample. Native basis/FOV copying supports this ordinary
flight case; runtime owned comparison closes dimensions/later-writer gaps.
Translation need not agree. Build sky history from the Background pair, retaining
Scene camera depth/lane parallax for geometry. If the two rotation/projection
paths do not agree, deny Background reuse for that frame or implement separate
sky and Scene reference matrices; silently substituting Background into all
Scene camera gates is incorrect. Choose/test the numerical comparison explicitly.

**C. Admit stable draws, mask the rest.** Add the candidate shader identities,
owned centered-node/geometry and stable-material predicate above. Extend the
current geometric exclusion transaction to actual Background draws before the
Scene depth clear; allocate/clear once at Background start and preserve its
coverage through Scene, unioning Scene exclusions. Current implementation
prepares/clears coverage at Scene start, so unmodified lifetime loses Background
coverage. Reuse the established original VS/geometry mask transaction, successful
state capture/restoration, UP/instancing handling and reentrant/SEH failure
invalidation. No arbitrary per-draw shader patch or native descriptor traversal.
An unsupported Background mesh whose conservative footprint covers the whole
screen may temporarily deny history there; persistent ordinary-flight total
coverage requires tighter support and cannot pass as completed parity.

**D. Enable the existing resolve policy after A–C.** Supply policy 2, strict term,
band/exit and cap settings through the actual FL resolve path; preserve RT2
invalidation and current/prior colour mask filtering. Do not alter AP policy.
Reset or mask preparation/stamping/restoration failure must deny publication,
including fresh age/no lock/hold vote under current-only exclusion. Log separate
stable-sky/finite/animated/unknown draws and coverage, camera agreement/refusal,
and Background history commits; a global `used_history` bit can come from hulls
and does not prove sky reuse.

**Fixture owner:** `fl_taa_integrated_fixture` owns the combined GPU fixture and
its generated synthetic owned-authority seam
(`verification/probe/fl_taa_integrated_api.h`). Reuse that fixture; its current
synthetic Background selector bootstrap is not Background temporal evidence.
Add these finite cases after production implementation:

| Case | Required observable |
| --- | --- |
| Centered mesh, camera translation, unchanged R/P | Nonzero stable-sky area reuses history; exact static-grid image/age oracle and translation cancellation within a stated raster error bound. |
| Same shader on fixed finite geometry; planet/moon proxy | Finite footprint is current-only and old footprint rejects after movement/removal; stable sky outside it keeps history. |
| Rotation plus FOV/aspect change, nonzero Scene jitter | Known sky correspondence/reference readback; current/prior R/P mismatch refuses reuse; no Scene-parallax gate corruption. |
| UV c4/c5, PS alpha c0, texture-content/identity, model/basis/LOD changes | Each independently revokes stable admission; pointer reuse and disappearance never retain stale colour/age. |
| Background exclusion, then successful Scene depth clear, then routed hull | Mask persists; overlap is conservative; RT2 -0.25 cannot become -1 sky or stale finite depth. Reverse contributor ordering too. |
| Strict silhouette: translation 5 px/frame, pure pan, below/above band and exit floor | Existing AP strict-depth, band and signed-age behavior; no SETA silhouette trail or pure-pan over-rejection. |
| Camera cut, missing/stale key, alternate camera, failed viewport/Clear, Reset (failed/reentrant too), compositor unwind/resolve failure | Zero stale Background publication; next valid frame bootstraps, then history resumes with the correct pair. |
| Wider colour taps, transparency/cutout, state-block, busy/queries, UP/instancing, failure restore | All contributing unsupported texels reject; original native results/state survive; failure enters existing recovery. |

Performance contract: shader classification at registration/preflight, bounded
owned scalar/signature comparisons per Background draw, one camera copy already
provided per activation, no per-draw allocation, native reread, logging or GPU
readback. Mask redraws cost actual geometry; measure their counts/coverage and
GPU time in the combined fixture. Runtime FL user flight must still establish
ordinary stable-sky coverage, translation/rotation, planets, SETA, cuts and
animated Background behavior. Native Windows runtime remains unmeasured.

## Boundary, liveness and evidence limits

Native helper ABI evidence: `0x412c60` takes ECX=input vector, stack+4=output,
stack+8=divisor pointer, returns EAX=output and `ret 8` (`0x412d08`);
`0x4947b0` takes ECX=node and stack+4=camera and returns `ret 4`
(`0x494af8`), recursively forwarding the same camera at `0x494ae4..0x494ae7`.
The centering stores precede its relative-coordinate calculations. These are
analysis targets, not proposed injection seams.

No hook is added by this contract. Existing ordinary publication seam
`[0x4defa4,0x4defaa)` remains after the last view store and before SetViewport;
EBX=camera, ESI=context, EDI=device and FP/SIMD state remain live. Material
observation continues to use its reviewed per-invocation owned record and native
argument transport. The new policy must never reopen native memory from retained
identity labels. Preserve GPR/EFLAGS/LastError/full FP state through existing
bridges, owner-thread admission, nested-record restoration, foreign-SEH outward
unwind cleanup and monotonic Reset revocation. Search/ContinueExecution retains
the invocation; cleanup reads no native camera/node. No locks span D3D/original
forwarding. Existing checked boundaries and rollback are reused, not newly
proved by the following static sample.

Reproduce derived evidence (no game, Wine or native fixture):

```sh
python3 verification/results/fl-port/background-temporal/derive.py
python3 -m py_compile verification/results/fl-port/background-temporal/derive.py
```

Measured static checks: **30 native operand anchors, six complete regions / 201
instructions, three dispatch mappings, eight KC calls plus 12 KC operand checks,
four exact shader programs**. The producer and `report.json` are retained in
that local ignored result directory; raw Ghidra/disassembly remains there too.
The algebraic witness at width 1280, m00=1.2, depth 100 and translation 10 gives
**0 px centered error versus 76.8 px fixed finite displacement** (calculated,
not GPU measured). Exact region hashes bind the static findings; this small
producer is not a runtime profile/admission verifier or a full alias census.

Specific unmeasured work is A–D plus fixture/flight acceptance, complete stable
resource-content provenance, actual sky draw/resource inventory, numerical
raster error under changing rotation/jitter and conservative-mask coverage cost.
These are finite consumer/resource checks, not a request for a new engine
lifetime audit. No production source, game assets, shared DLL, installation or
commit was changed by this study.

Independent RE/design review accepted this contract with no blocking finding.
The reviewer reproduced the 30 anchors, six regions/201 instructions, three
mappings, eight KC calls/12 operands and four shader contracts, independently
checked the native camera/centering dataflow, and passed nine AP sky-history
source tests. This is sufficient to implement slices A–D; it does not establish
GPU raster equivalence, stable-sky inventory, contributor disappearance handling,
or actual flight acceptance. The 0 px / 76.8 px example remains calculated.

## 7. Submitted ordinary material transform — 2026-10-28

**Ordinary material WVP uses a freshly constructed local world/view product,
not the mutable global view buffer.** The global-view identity overwrite by an
orthographic instance therefore does not establish contamination of this specific
ordinary material path. `0x4e3eb6..0x4e429c` constructs double W and V directly
from the material's current node, camera and supplied context. The call-free
multiply at `0x4e42b0..0x4e4319` writes a double matrix; `0x4e431e..0x4e436e`
converts it once to float at `[ebp-0x478]`. Both ordinary branches
`0x4e43c0` and `0x4e448d` multiply **that local WV** by perspective `0x5aae78`.
Their orthographic alternatives `0x4e4399` and `0x4e4466` use global W/V and the
separate orthographic projection, and remain excluded.

The numeric contract differs from the earlier instance/world builder:

- Node basis and each axis scale are signed integers converted to float, then
  multiplied by float `1/65536`, then widened to double. Each world basis element
  multiplies, in order, that basis value, raw signed node `+0x70`, context scale
  widened from float, and the widened axis scale (`0x4e3ed2..0x4e40af`). It does
  not use the instance builder's rounded fixed-point axis product.
- World translation is raw signed effective position converted directly to
  double and multiplied by widened context scale (`0x4e3f6f`, `0x4e400b`,
  `0x4e40b7` and following instructions). Camera basis follows the same
  int→float→fixed-scale conversion as the ordinary activation view, then widens
  to double. Camera position converts directly to double before scaling.
- View translation accumulates **Y term, X term, Z term**, then negates
  (`0x4e41d8..0x4e428c`). The W×V loop starts with positive zero and accumulates
  products in **k=0,1,2,3** order using separate `mulsd`/`addsd`; every result
  receives one `cvtpd2ps` conversion. Native rounding residuals are retained.

The new pure helper reproduces that exact operation sequence from owned node
values and ordinary camera inputs. Centering requires `0x400000`, excludes
`0x200`, and checks each effective coordinate equals base+camera in signed
64-bit arithmetic without an int32 overflow. Missing input authority, zero
scales, nonpositive/nonfinite context scale and nonfinite results refuse proof.
No native storage is read by this helper. Because the material may call out
between its entry snapshot and matrix construction, **the expected matrix alone
never grants admission**: actual successful VS c0–c3 shadow values must match.

For the qualified symmetric perspective, XY are one float multiply each and W
is the local WV Z column. The other products in those three columns are finite
zeros, so D3DX multiplication parenthesization does not introduce an unexplained
tolerance. Nonzero projection offsets or an unsupported matrix shape are
refused. Compare submitted column-major c0/c1/c3 exactly, accepting numerical
signed-zero equality only. Projection writers between ordinary activations
change depth terms; nested activations retire the current owned key. Private or
otherwise changed submitted transforms still have to pass the same exact
constant comparison.

Depth is checked too. Submitted c2 must exactly match all four rows under one
of the two fixed native wrapper laws: default `(0x3f800019,0xc0c00026)` at
`0x4e711e/0x4e7125`, or alternate `(0x3f800001,0xbeb33335)` at
`0x4e7106/0x4e710d`. Each expected row computes two rounded float products and
one float addition. Dynamic depth at `0x4e7065/0x4e7075`, private clipping laws,
NaN/Inf or any single-ULP mismatch are excluded. The nonzero accepted depth-law
ID is part of the stable contributor signature; changing it revokes old as
well as current influence. An XY/W match cannot silently admit changed clipping.

`verification/probe/build_fl_background_transform.py` extracts exactly
`[0x4e3ebc,0x4e4373)`: **253 instructions, zero calls**, internal branches only,
and one relocated constant-address operand. The generated raw bytes remain
local/untracked. `fl_background_transform_fixture.cpp` executes this native
kernel as an independent oracle; it does not duplicate the helper's equations
for W×V. Native arithmetic execution and refusal counts are recorded in the
[Background temporal ledger](../../verification/farnhams-legacy-background-temporal.md).
This validates arithmetic and refusal, not shader upload orientation, live
signature completeness, GPU history, or flight behavior; those remain
consumer/integration obligations.
