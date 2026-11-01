# Farnham's Legacy fog-card replacement contracts

Offline comparison, 2026-10-29, against the shared AP card path at checkpoint
`afda27d0`. **FL's selected base fog programs, screen/depth contract and resolved
dust-body geometry match AP. This establishes a reusable card classification
and masking path, not FL renderer activation.** The FL live vertex declaration,
inherited state, scene placement and successful replacement transaction still
need the scoped acceptance below. No production edit, build, Wine/native
execution, game launch, installation or commit was performed.

Inputs: [AP fog](sector-fog.md), [FL sector authority](sector-fog.md),
[FL resource resolver](resource-resolver.md),
[resource callers](resource-callers.md),
[depth-writer selection](depth-writer-selection.md), and
[artistic texture policy](../../architecture/farnhams-legacy-fog-texture-context.md).
The CPU source is already implemented and qualified separately. **Correction
(2026-11-01, step 6):** the shipped FL build has no `fl_family_unqualified` gate
in `fog_frame_prerequisite`; the FL sector sample is taken at the first
BeginScene ([FL sector note](../../architecture/farnhams-legacy-sector.md)) and
`fog_card_match.h` admits both exact pairs below in the FL edition. No
installed-build claim is made here.

## 1. Exact resources and changed/unchanged programs

The [producer](../../../verification/results/fl-port/fog-cards/reproduce.py) uses
`fl_resources.Resources('/mnt/c/X3FL-orig', mods=())`, with qualified contiguous
root/addon2 mounting and exact namespace preservation. It selects each fog
effect by its complete key, compares the actual selected bytes with the
existing inventory, and independently reads AP's corresponding selected
resource. This is a bounded refresh of the fog subset, not a second full shader
audit. Whole-image hashes are provenance rather than runtime capability gates.

Measured **96 effect paths and 96 passes**: six quality directories
`1_1/1_4/2_0/2_a/2_b/3_0`, four base/toggle directories, and four effect stems.
All use `DEFAULT/P0`. The label `3_0` does not change these programs' SM1.1
version words.

| Effect stems | Paths | Actual VS / PS | Comparison with AP |
| --- | ---: | --- | --- |
| `nebulafog`, `nebulafog2s` | 48 | `7b6393fe2d3e1d85` / `f7e0b6647a3bfa62` | Both complete programs byte-identical; container differs and adds `g_MatQualityColor` metadata. |
| `nebulafog_0000`, `nebulafog_0001` | 48 | `ececc28e00592d35` / `214fc10d3672ab92` | Complete programs and containers byte-identical. |

The base VS is **512 bytes / 128 DWORDs**; PS **304 bytes / 76 DWORDs**.
Their SHA-256 identities are respectively
`5efc6bda105f178cc9044047f3981e5b05b020ad96876021fb938d51daa46363`
and `90c0d2c218724d258877cb9cc0c6e46c47913d78a2ca775ab346de906ae05448`.
The producer compares full bytes as well as these hashes; matching names or
FNV alone is not its equivalence proof.

The legacy and base programs also have **identical GPU token bytes after
comment removal, and identical CTAB bindings**, per stage. Their distinct full
hashes therefore do not imply different shading arithmetic. The VS consumes
v0/v1, writes position and oT0, uses world-view-projection c0–3 and texture
matrix c4–5, with a private DEF at c6. The PS samples t0, uses `g_AlphaValue`
at c0 and its own DEF at c1, writes SM1 r0, and has no TEXKILL or depth output.
The extra FL `g_MatQualityColor` has no binding in these unchanged programs.
These facts authorize investigation of explicit aliases; they do not authorize
arbitrary same-name, same-model or fingerprint fallback.

## 2. Render, sampler and body contracts

Every inspected base and legacy pass sets these **literal** states, unchanged
from AP: `ColorWriteEnable=7`, `ZEnable=0`, `ZWriteEnable=0`, `CullMode=1`
(NONE), `AlphaBlendEnable=1`, `SrcBlend=2` (ONE), `DestBlend=4`
(INVSRCCOLOR), `BlendOp=1` (ADD). The RGB equation is
`source + destination*(1-source)`. Thus successful pass application excludes
depth writes without classifying the shared VS as a global nonwriter.

The base pass additionally sets `BlendOpAlpha=ADD`, `AlphaFunc=GREATER_EQUAL`,
`AlphaRef=1`, `Lighting=0`, `ColorVertex=0`. It does **not** set
`AlphaTestEnable`, `SeparateAlphaBlendEnable`, `StencilEnable` or `FillMode`.
Those remain runtime gates; static pass defaults cannot replace a successful
device-state read. The legacy pass sets `AlphaTestEnable=0` explicitly and
omits those additional base assignments. Its inherited stencil/fill/separate
alpha states still need the same checks.

Both bind `DiffuseTexSampler` at s0 to `t_DiffuseTexture`. Base filtering is
literal LINEAR for min/mag/mip, CLAMP for U/V, `MaxMipLevel=0`, with an FXLC
expression for anisotropy. Legacy filter/address/anisotropy settings are FXLC
expressions, with `MaxMipLevel=0`. AP/FL equality checks include the complete
expression payload hashes; the producer does not evaluate them. Masking all
RGB writes makes these sampler differences irrelevant to suppression safety.
No texture capture, readback, resource-name lookup or texture-hash gate is
needed per draw. Native texture-selection context remains the separate,
already documented artistic-policy boundary, including the possible `_l`
request from high dynamic registration indices.

Measured clean FL TBackgrounds: **110 rows, 23 with positive dust, 16 families**.
The union of their positive-weight parts requests **105 family/part pairs**;
**89 resolve**, each with one LOD, four source points, one part, two triangles,
one `nebulafog.fx` material. **All 89 decoded bodies are byte-identical to AP**.
All 89 materials share the authored screen-blend/disabled-ZWrite parameters;
their `g_ZEnable=1`, `g_CullMode=2`, `g_ColorWriteEnable=15` differ from the
compiled pass's literal 0/NONE/7. Those authored parameters are not the final
state evidence. Thirteen distinct material/texture contracts are retained.

Missing rated slots are `xtmgreenring` parts 1–6 and parts 7–8 of `earth`,
`fogkhaak`, `khaakhive`, `uranus`, `uranus3`: **16 misses**. `fogblue` requests
only parts 1–5 under the inspected weights. This census does not synthesize
missing bodies or reinterpret dust count as physical density. The 14 compiled
family identities and compiled-first/file-family fallback stay unchanged;
earth/xtmgreenring need their existing explicit supported-input policy.

**Declaration limit:** identical source quads and VS inputs do not prove FL's
actual D3D declaration, stream packing or index binding. AP's admitted draw is
an indexed, non-UP triangle list, two primitives/four vertices, stream0-only,
24-byte stride, position offset 0/type FLOAT16_4, exact declaration hash
`0cdf6a8c884ad955`, stream frequency 1. Keep that predicate strict. A different
live FL declaration needs its own bounded geometry/packing proof; do not
widen the predicate from body names, source point count or an SM1 VS alone.

## 3. Runtime admission and minimal shared reuse

Reuse [`FogCardShape/FogCardStates`](../../../src/proxy/fog_card_match.h),
[`FogCardPolicy`](../../../src/proxy/fog_card_policy.h),
[`FogCardMask`](../../../src/proxy/fog_card_mask.h) and
[`prepare_fog_card/finish_fog_card`](../../../src/proxy/motion_output_fog_inc.h).
No new native hook or caller ABI is required for this classification/masking
operation. Existing D3D proxy entry boundaries and CPU/LastError envelopes
remain authoritative. No interior-site liveness or reentrancy proof is inferred
from this resource comparison. If later integration proposes a new borrow or
hook, the parent must route its ABI/lifetime reconstruction to
`disassemble_deep` before changing production.

For the eventual FL consumer, the necessary conditions are cumulative:

1. Freeze the **value-only** sample obtained by the existing successful,
   matched Scene Clear ticket, associated with its FL scene/camera key. The key
   carries borrow/pass/request, observer/load/registry/revision, activation,
   reset and origin identity. No BeginScene/Present, background/cube/auxiliary
   ticket, old cached sample or new dust-camera activation may mint sector
   authority. Later card draws consume the frozen owner value, never reread
   sector objects. Require the accepted active-sector-camera consistency,
   Ready/row/name/anchor validation, positive dust and supported family.
2. Retain exact cached source/pair checks, the full shape predicate and known
   frequency 1. The existing base pair needs no changed program identity.
   **Recommend both proven exact pairs**, with both PS source recognition
   **and** VS/PS pair recognition changed together, retaining the same draw
   gates. Observe/refuse every proven fog PS even when its current VS/pair is
   unadmitted; recognize only the two witnessed pairings, not arbitrary mixed
   combinations. The legacy path's runtime selection remains unresolved, but
   that need not block safe complete handling under these exact gates.
   The current classifier observes
   only base PS; simply ignoring a reachable legacy draw cannot establish
   replacement coverage and could leave native cards stacked with the medium.
3. Require the live owning open/bound scene, no recording/queries, no
   composition recursion or state loss, active HDR target, no main MSAA,
   active jittered TAA with resolve still pending, current linear-depth
   A32B32G32R32F producer, healthy sun lane, and fog pass not already executed.
   At scene-end also require published same-frame depth, the actual HDR target
   and valid cascades/camera/sun inputs. Card nonwriter proof does not qualify
   FL's other depth families or its entire sun/depth coverage.
4. Check actual states: ZEnable 0/1, ZWrite 0, AlphaTest 0/1, blend on, RGB
   mask7, cull NONE/CW, stencil off, SOLID fill, ONE/INVSRCCOLOR/ADD,
   SeparateAlphaBlend 0/1. Unknown state refuses. The allowed AP docked and
   cockpit variants remain safe because the masked draw has neither colour,
   depth nor stencil writes; do not widen for an unobserved FL variant.
5. Require prepared resources for this exact size/profile/recipe/field
   generation and valid frame parameters before the first mask; for stored
   density require prepared/full far readiness and drawable coverage. Bind
   readiness to the same frozen owner scene. A readiness result for a previous
   camera/scene/sector cannot authorize the first current card.

As written (2026-10-29) the runtime did not connect conditions 1 and 3–5 for
FL. Step 6 (2026-11-01) takes the shared BeginScene sample instead of condition
1's frozen Scene Clear ticket and admits both exact pairs (condition 2); there was
no `fl_family_unqualified` gate to remove. The minimal renderer change is
an edition-qualified scene/sample/depth/sun admission into the existing pass and
transaction, with both explicit exact aliases and full source/refusal coverage.
No fog equation, density redesign, shader rewrite or texture resolver in the
draw path follows from this note.

## 4. Transitions, fallback and lifetime of the mask

The source-shader latch is diagnostic. It cannot retain sector authority across
a gate, load, clear sector or a failed scene ticket. Reuse the sector-frame key
and warm-up rules, while also retiring the FL frozen owner/readiness on its
load/request/reset/scene-domain changes. This matters when a sector address is
reused or two sectors use the same family/placement: unchanged artistic field
data does not carry permission to suppress a new scene's cards.

On a new/rejected/missing sample, unsupported family, key change, frame gap,
toggle/zero strength, Reset or incomplete density coverage, keep native cards
and rearm only through the shared successful warm-up transaction. The ordinary
warm-up stacks a successful medium frame before suppression. Existing stored
density cold-step arming is the explicitly qualified exception: it may arm
before the first card only when that frame's prepared/full-ready transaction
can complete. FL R3 prefill remains unqualified and provides no shortcut.

Each admitted card sets native `COLORWRITEENABLE=0`, forwards the original
draw, then restores saved mask7 on **every** return, including a failed draw.
This is a draw bracket, never a mask spanning cameras or an entire scene.
Failure to set the mask explicitly attempts restoration; restoration failure
latches state loss. Preserve the original draw HRESULT and CPU/LastError.
Before any suppression, a refusal keeps native rendering. Once any card was
suppressed, failed replacement cannot recreate its discarded colours: latch
the shared terminal replacement fault until Reset. Toggle/rearm cannot erase
that evidence. Successful scene-end completion and temporal invalidation must
remain exactly paired with this frozen scene, including refusal and unwind.

## 5. Required scoped acceptance and performance

This task performed only the host resource comparison, not the following GPU
acceptance. Reuse existing unchanged AP tests/evidence instead of rerunning a
full fixture chain. The owner should add the FL adapter to the affected shared
card/mock and fog-pass fixture cases, then run the selected fixture through the
single Wine queue with `X3M_FIXTURE_BOTTLE=X3`. Existing
`run_fog_pass.py --cards-only` checks masked colour/depth preservation,
readiness and Reset, but by itself does not exercise FL scene/sample admission
or establish the game's real declaration.

The scoped acceptance must demonstrate a successful fog composite after masks
using the selected FL programs and exact declaration; unchanged native draw
results/colour when refused; retained depth and other MRT values; hostile state
restoration; failed mask/restore/draw/composite; Reset/loss/recovery; and exact
legacy-pair source coverage, including a recognized fog PS bound with a refused
VS/pair. Its FL scene sequence must
cover matched Scene authority, background/cube/auxiliary/nested exclusions,
docked anchor validation, same-family transit, clear/unsupported destination,
same-address reuse/load epoch, missing/stale sample, resource/ready generation
changes, camera cuts and warm-up/terminal fault behavior. Existing fixture
fault injection and owner retirement should be extended, not bypassed.

A detached GPU fixture proves the supplied declaration and sequence, not live
FL draw selection. A user-supplied FL flight capture remains necessary to bind
actual fog-card declaration/stride/frequency, inherited states and ordering to
the same owner scene, including a gate/load and docked/view switch. Native
Windows source/API compatibility follows from the shared documented D3D9 path;
verified native-Windows FL behavior remains a separate gap.

Inferred incremental classification cost is constant-time cached hash/pair
checks, with at most one extra pair/PS comparison for explicit aliases. Keep
the existing per-card frequency/state reads after cheap gates, readiness once
per frozen scene, and two native colour-mask setters per suppressed draw.
Resource indexing, byte comparison, family/material scans and texture selection
belong offline or at existing family preparation, never per draw. No new lock,
allocation, GetFunction scan or native payload read belongs in a card bracket.
The fog GPU programs and density work are unchanged by this classification;
their runtime cost and first-draw compilation still need the owning feature's
measurement. The **1.23 s** producer duration is measured host analysis time,
not a game-frame or GPU-performance result.

## 6. Reproduction and evidence limits

```sh
python3 verification/results/fl-port/fog-cards/reproduce.py
python3 -m py_compile verification/results/fl-port/fog-cards/reproduce.py
```

The retained [summary](../../../verification/results/fl-port/fog-cards/summary.json)
binds the source hashes, ordered clean resource context, 96 selected effects,
four full/GPU-token program identities, two pass/sampler contracts, 89 body
hashes, 13 deduplicated material contracts and 16 missing-part witnesses.
The producer validates selected inventory freshness, direct FL/AP byte
comparisons, literal screen/depth states, expression-payload equality,
comment-free alias equality, expected counts and JSON round-trip. No complete
shader bytecode or decompiler listing is emitted; exploratory operand records
stay local under `/tmp/fl-fog-card-*.local.json`. This evidence establishes
static contracts only; the remaining runtime gates are explicit above.

Review correction: the AP body comparison now calls the qualified
`bob1.resolve_body` helper, preserving catalogue-layer priority before extension
rank. The former direct `.pbd`-before-`.pbb` candidate selection was incomplete.
Three synthetic witnesses exercise the producer's actual comparison seam:
higher-layer binary over text, higher-layer text over binary, and binary over
text within one layer. All pass. The regenerated result retains all **89**
AP/FL body equalities and every headline count; the clean conclusions are
unchanged. The previous result is preserved locally at
`/tmp/fl-fog-cards-before-ap-resolver-p2.local.json`. The summary records the
qualified resolver, its three witnesses and each AP winning source/member.
