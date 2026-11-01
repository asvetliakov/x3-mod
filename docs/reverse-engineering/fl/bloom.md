# FL bloom compositor and shared replacement contract

Offline study, 2026-10-29. FL input `/mnt/c/X3FL-orig/X3FL.exe`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
AP comparison `/mnt/c/X3AP-orig/X3AP.exe`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`,
the image owned by the [AP compositor study](../compositor-and-glow.md).
Addresses are preferred FL VAs, base `0x400000`; runtime descriptors use checked
loaded-base RVAs. No production source, build, Wine, device, game or install was
changed or executed.

**FL's reviewed `3_0` `bloom.fb` `DEFAULT` technique supports the existing
original-once, pre-original scene retention, post-original RGB replacement
design.** Direct FL extraction establishes the four pairs and states below;
AP's whitelist alone supplies no FL admission. The missing integration is a
typed FL handoff/admission adapter around the existing full compositor bridge,
not another bloom implementation. Resource provenance and static equivalence
do not certify a live FL invocation or its GPU execution.

## 1. Selected effect, provenance and variants

The [authoritative selection contract](depth-writer-selection.md#authoritative-selection-and-configuration)
already establishes device-record **+0x38** as the full effect-directory string.
High quality (default 2) preserves the result of D3DXGetPixelShaderProfile;
`ps_3_0` selects `3_0`. Low selects `1_1`; medium normally selects `1_4`.
Bits `0x1800` at config+0xf0 append `hueshift_off`, `v_lights_off` or
`hue_lights_off`. The current `shader_profile_fl` reader copies only those four
exact `3_0` strings into owned enums, at the admitted outer complete-pass entry.
CreateDevice and the initial BeginScene are too early. Reuse that snapshot and
its device/common/Reset binding; a device's SM3 capability does not prove the
selected directory.

At compositor `0x4e67f0/0x4e67f8`, the requested effect is **bloom**, variant
index zero. Effect loader `0x4db337` constructs
`shader\<full-profile>\<effect-name>`; `0x4db3bf` requests `fb` through the
[reviewed resolver](resource-resolver.md), then `0x4db49a`
creates the effect. There is no retry of another profile or lower archive on
payload failure. FL mounts root catalogs, then addon2 catalogs, each ascending
to the first gap, then explicitly selected mods; a root-relative loose exact
resource overrides catalogs. The physical addon directory is not an FL mount
layer. This producer models the clean **no-selected-mod** roots and checks for
loose shader overrides; it does not attest a running process's mod selection.

Measured FL winners below are all **51,720 bytes**, in `addon2/01.cat`; offsets
are DAT file offsets before XOR decoding:

| Exact member | DAT offset | Decoded payload SHA-256 |
| --- | ---: | --- |
| `shader/3_0/bloom.fb` | 456392797 | `7d5903685d65bf183ad0e23173688be3aada6db7bcb4980f377a02e8348021b3` |
| `shader/3_0/hue_lights_off/bloom.fb` | 457173261 | `9de52f442b6fe61d7254a99050f715aa479922e3b41e036c3c788ca107241afb` |
| `shader/3_0/hueshift_off/bloom.fb` | 459850873 | `5baa14f0e72cd0ac371193a8da3d7eb84259ad42016746cc0e5ce19dcd749888` |
| `shader/3_0/v_lights_off/bloom.fb` | 464013437 | `faa2da09806fbe5fa315aba94f4228ccd58982e9ad1a56619dd5e925fe5f1714` |

Measured across **48 FL / 48 AP** effective `bloom.fb` and `bloom_0000.fb`
members: all corresponding recorded pass, shader, relevant parameter-default,
texture-annotation and sampler contracts match; **24 complete effect payloads**
are byte-identical and 24 differ. This is equality of the extracted contract,
not a claim that all unused embedded script/UI metadata is equivalent. The
records reduce to **eight distinct contracts**. The four members above have
the same reviewed DEFAULT contract; the **44 other members** fail that bounded
SM3 DEFAULT contract. Lower-quality programs and `bloom_0000` are not implicitly
eligible. The real compositor requests `bloom`, not `bloom_0000`.

The SM3 payload also contains an **HDR** technique. Native code explicitly
looks up **DEFAULT** at `0x4e68ff`, sets it at `0x4e69be`, and only calls
FindNextValidTechnique at `0x4e692b` if the named handle is absent. The clean
payload has that handle. HDR's final PS is `b40d09effa812ec8`, reads both scene
and glow, and has different blend states; it is outside this DEFAULT contract.
Modified effects/selection must refuse replacement while forwarding the genuine
compositor and ordinary HDR writeback. A native failed/unchecked SetTechnique
does not establish successful DEFAULT execution.

## 2. Four passes, texture flow and pass states

These are **directly measured FL identities**, complete-program FNV-1a64;
per-program SHA-256, models, lengths and CTAB bindings are in the evidence.
Every listed VS/PS is model 3.0. Pass counts come from the selected technique,
not a hardcoded native loop bound.

| DEFAULT pass | VS / PS | Input | RenderColorTarget0 | PS texture instructions |
| --- | --- | --- | --- | ---: |
| 0 DownSample | `cbbf26102694c961 / 1c90e79667bdaddf` | SceneSampler s0 = t_SceneMap | t_GlowMap1 | 1 |
| 1 BlurGlowBuffer_Horz_X | `6059306306203243 / f3172baa8dd19a40` | GlowSamp1 s0 = t_GlowMap1 | t_GlowMap2 | 25 |
| 2 BlurGlowBuffer_Vert_Y | `6059306306203243 / 241c3fa33270f58e` | GlowSamp2 s0 = t_GlowMap2 | t_GlowMap1 | 25 |
| 3 FinalCombine | `1279d081455f5815 / ff6eed5a5ddf3a3a` | GlowSamp1 s0 = t_GlowMap1 | **annotation absent** | 1 |

All four passes explicitly set ColorWriteEnable=15/RGBA, CullMode=1/NONE,
ZEnable=0. The first three disable alpha blending. The final pass enables
blending and separate alpha, disables alpha test, and sets RGB
**ONE / INVSRCCOLOR / ADD**, alpha **ZERO / ZERO / ADD**. All referenced sampler
blocks bind the named texture parameter and explicitly set U/V=CLAMP,
MIN/MAG=LINEAR, MIP=POINT. The texture requests and annotations specify one mip;
do not confuse MIP=POINT with MIN/MAG=POINT.

These pass assignments do not initialize every D3D state. In particular, the
first three do not explicitly disable alpha test, and the effect does not
establish every scissor, stencil, sRGB or additional-MRT condition. Incoming
state, native state-manager behavior and actual API success remain relevant.
An offline four-pair match cannot prove full output coverage.

Native texture globals are scene **0x5aae88**, glow1 **0x5aae84**, glow2
**0x5aae74**. The [existing creation study](compositor.md#3-compositor-target-flow-and-scene-end-hook-abi)
binds actual CreateTexture requests to A8R8G8B8, scene display dimensions and
integer-halved glow dimensions. Effect annotations independently agree:
ViewPortRatio=1 for scene, 0.5 for both glow targets, MipLevels=1,
Format=A8R8G8B8. Its DepthBuffer annotation says D24X8, but this compositor
does **not** bind that annotated depth resource: it saves the incoming depth,
binds null and restores the saved depth. Do not infer actual resource ownership
or successful allocation from annotations.

## 3. Native input, output, alpha and post state

The existing [compositor](compositor.md#3-compositor-target-flow-and-scene-end-hook-abi)
and [owner](compositor-owner.md) contracts supply the live borrow
and original-once seam. New bounded decoding rechecks **620 instructions** in
the compositor/viewport helper and **25 anchors**:

* Incoming RT0 is saved at `0x4e6849`, incoming depth at `0x4e6897`; null depth
  is set at `0x4e68c3`. The input is the **actual incoming RT0**, which must be
  correlated with the admitted logical-main image after ordinary HDR writeback.
* Native ColorFill prepares the three surfaces. `0x4e6c63` copies saved RT0
  to scene-map level zero with null rectangles and filter=LINEAR. It checks the
  copy HRESULT, retries once on failure, and can invoke existing allocation
  recovery. This is a display-encoded, A8 scene copy after HDR writeback, not
  the retained FP16 scene needed by the modern bloom extractor.
* Begin at `0x4e6cdc` uses DONOTSAVESTATE=1. Per-pass GetPass/annotation/string
  queries select a known target. The final pass's **absent** target annotation
  selects saved RT0 at `0x4e6d66`. An unknown nonempty annotation is unsafe: it
  retains the previous target local; it is not an equivalent saved-RT fallback.
* SetRenderTarget at `0x4e6d72`, helper call `0x4e6d7b`, and DrawPrimitive at
  `0x4e6d89` are unchecked. Helper `0x4e82e0` uses surface GetDesc at
  `0x4e82f3` and SetViewport at `0x4e8338`: X=Y=0, dimensions from the selected
  surface, MinZ=0, MaxZ=1. Its query/set can fail. The final attempted target
  and viewport are therefore full incoming RT0 only on the successful path.
* End/texture unbinding and surface releases follow; depth restore is attempted
  at `0x4e6e42`. RT0/viewport are not unconditionally restored afterward. Mixed
  VP can enable software VP at `0x4e6c47` and leave it enabled. Preserve the
  genuine outgoing state using the shared executor's documented state bracket.

The reviewed native extraction stores **scene-alpha × scene-RGB** as colored
glow, and a complementary thresholded white-highlight term in alpha. The
complete identical programs bind the AP study's established dataflow to FL;
this extraction independently checks their identities and CTAB. FL defaults
also match: threshold approximately 0.87, colored gain 2, highlight gain 0.9,
blur-weight modifier 1.2. These are defaults, not measured live uniform values.
Final PS samples only blurred glow; native destination RGB participates through
the screen blend `G + D*(1-G)`. Shader output alpha is distinct from target
alpha: separate ZERO/ZERO ADD makes **covered, successfully drawn destination
alpha zero** in DEFAULT. Failures, scissor or uncovered pixels can leave other
values; do not synthesize a zero-alpha image from this static fact.

Shared replacement already has the correct two alpha contracts:
`MotionHdrScene.scene` retains pre-original FP16 alpha for authored extraction
(including any enabled shared TAA alpha-history policy), while
`BloomPass::commit` sets a **RGB-only mask 7** and preserves actual outgoing
destination alpha. Candidate alpha is ignored. Never replace retained source
alpha with native final zero, nor force destination alpha from the source or
from this static prediction. Backup/recovery must preserve the complete genuine
post-original image, including alpha.

## 4. Minimal FL adapter and remaining qualification

The small adapter should reuse the shared `BloomPrepare`, `BloomPass`, shader
bundle, retain-once callback behavior and exact display snapshot. Extend FL's
existing per-invocation storage to retain scene, optional bolt scene, main,
depth, candidate and device pins; feed a callback to its current
`scene_end_hook()` call. The callback only retains/copies. It must not prepare,
resolve again, meter again or reenter the renderer. Prepare after the ordinary
writeback/restoration has completed, using its actual AgX/decode/sharpen/bolt
snapshot and the shared authored-glow gains. Do not sample original-bloomed RGB.

FL admission uses the exact selected registry device and its existing common,
camera, pass, request and Reset generations; the complete pass must have the
reviewed owned profile and reviewed effective resource domain. AP manager
agreement, AP config RVA **0x206f34/+0x100** and AP executable gates do not
belong here. FL native glow reads **0x5a83b0/+0xf0 bit0x80** at
`0x4e6790/0x4e6795`; capability uses record+0x80. A new synchronous FL value
reader must run under the existing common borrow and its excluded config/owner
retirement interval, copy owned values call-free, and recheck generations before
acting. Existing owner snapshots alone contain no glow flag. This study does
not implement or independently qualify that reader's full configuration lifetime.
Alternatively, speculative preparation followed by a strict successful native
event witness can avoid that new read, at the cost of preparing declined frames;
it still cannot commit merely because the post bindings equal the pre bindings.

Forward `0x48b929 → 0x4e6770` exactly once. At post, requalify the invocation
and exact main/depth/resource descriptors, known viewport/MRT and active-query,
state-recording/lost-device conditions. Use observed successful native API/draw
events or the reviewed resource/selection contract plus actual post state;
never invent execution from four attempted draws. A wrong native target, failed
copy/state restore, unexpected technique/pair or revoked generation declines
replacement. A missing handoff keeps ordinary writeback and native forwarding.
Glow-off keeps that ordinary resolve and produces no modern replacement, as AP
does. Failed candidate preparation leaves original execution intact. Commit
uses the shared backup, RGB draw, restoration and recovery semantics.

No new hook boundary is proposed: the five-byte call seam, no-argument/plain
RET ABI, ESI/EBX/EBP caller liveness, GPR/EFLAGS/LastError/full FP-SIMD
preservation, quiet installation, nested revocation and foreign-SEH
ContinueExecution/outward-unwind distinction are inherited from the reviewed
owner/bridge contract. Retain invocation pins through invalidation and cleanup;
no common metadata mutex may cross COM/native forwarding. Every Reset attempt
revokes readiness, including failed/reentrant/foreign-thread attempts. This
study supplies no new rebase proof or reason to patch a second seam.

Needed acceptance is an actual FL adapter/scene-handoff fixture, with no-handoff,
glow-off, unsupported profile/override, wrong device, nesting, Reset/reuse,
abnormal unwind and partial original failure cases, then shared D3D resource,
state and recovery execution on supported backends and one user-controlled FL
flight witness. Native Windows behavior remains unmeasured. Host metadata
checks are not substitute fixture eligibility. Profile/resource preflight is
once per supported process/incarnation; no per-draw file scans or native owner
walks are needed.

Inferred cost: reuse adds no edition-specific shader or GPU pass. The genuine
stock copy, clears and four draws remain (PS texture instructions 1/25/25/1),
alongside the existing modern pyramid/candidate/backup/commit work. The adapter
adds bounded per-boundary value checks and invocation pins; shader attachment
and resource resizing remain lazy shared costs. Gameplay FPS, loading/first-draw
compile cost and steady-state GPU cost remain unmeasured for FL. Do not optimize
by suppressing native passes or source alpha without another explicit contract.

## Reproduction and limits

```sh
python3 verification/results/fl-port/bloom-contract/reproduce.py
python3 -m py_compile verification/results/fl-port/bloom-contract/reproduce.py
```

The [producer](../../../verification/results/fl-port/bloom-contract/reproduce.py)
records derived hashes, offsets, defaults, annotations, CTAB and counts in
`summary.json`, with **eight rejected mutations**: extra pass, unknown target,
wrong pair, alpha-blend change, point filtering, wrong input, copy-filter change
and wrong depth-restore method. Native mutations bypass region hashes to test
the semantic anchors. The 44 denied archive members exercise the bounded
profile matcher, not production renderer admission. No shader words, effect
scripts or raw disassembly are retained in the tracked evidence. Local raw
inspection remained under `/tmp/`; no runtime fixture or game was launched.

## Appendix: finite glow-flag read and output qualification, 2026-10-29

**The existing healthy compositor borrow permits a fresh configuration-root
and glow-byte copy; no configuration observer, new hook or native lock is
required.** This closes §4's reader-lifetime gap. It also resolves its
conservative discussion of native pass-event qualification: the shared
original-then-replacement route needs actual post-output state, not a new
four-draw success counter. The earlier shader/resource evidence is unchanged.

### Configuration allocation, retirement and actual writers

Reuse the [configuration ownership proof](font-selector-readiness.md#1-exact-layout-and-ownership)
and [mount failure closure](font-selector-readiness.md#4-mount-correspondence-and-failure-qualifications).
New bounded witnesses recheck the following native edges rather than treating
the configuration pointer as permanently cold:

| Edge | Established effect |
| --- | --- |
| `0x509e3c/0x509e46` | Allocate **0x7a0 bytes**. Early publication `0x509e4d` precedes zeroing `0x509e63` and construction `0x509e6d`. This publication alone is not readiness. |
| `0x476c96/0x476c9e` | Startup calls `0x509e00` and republishes its normal returned pointer. Failed initialization does not authorize a render borrow. |
| `0x50b54c..0x50b579` | Destruct configuration members, load G, free G through `0x52f4e5` at `0x50b55d`, then clear global G at `0x50b579`. |
| Destructor callers | **Three direct edges**: shutdown `0x476a91`, catalog-mount allocation failures `0x50ad85/0x50afd6`. The latter destroy/clear G; their normal continuations cannot produce a healthy unchanged-owner display/render pass. |
| Root stores | **Six** direct stores: `0x476c9e`, `0x509e4d`, `0x50a082`, `0x50a821`, `0x50afdb`, `0x50b579`. The first two publish; the others clear on failure/teardown. `0x50afdb` stores ESI after testing failed allocation, hence zero. |
| Defaults | `0x4d7178` initializes config+0xf0 to `0x281`, including glow bit 0x80. |
| Registry | `0x4d770d` names `VideoD3DFlags2`; successful query publishes the whole DWORD at `0x4d7725`. |
| Capability | Display initialization tests record+0x80 at `0x4f2ed7`; absence clears config glow bit at `0x4f2ee0`. |
| Graphics dialog | Control `0x4da` at `0x4fadfb`; checked/unchecked branches set/clear only bit0x80 at `0x4fae10/0x4fae1c`. |
| Native command | **P_SetSysD3DFlags2**, index10: name cell `0x5a6150 → 0x58dba4`; jump cell `0x4b3f34+10*4 → 0x4b3bf7`; whole-word store at `0x4b3c08`. This is a real runtime flag writer, not merely a getter. |

The command's registration at `0x4b2e98` installs dispatcher **0x4b3b00** in
`VM+(type_index+2)*24`, equivalently `VM+0x30+type_index*24`; `0x4b2ecf`
publishes name table `0x5a6128` in that type record. It therefore joins the
already-proven [synchronous interpreter transport and owner scheduling](material-submit.md#native-command-scheduling-closure),
including interpreter dispatch `0x4c0783`. It is not a newly discovered
parallel worker or callback registration. Runtime writes before a copy are
allowed; use the resulting current value. The known render-time config+0xf0
OR operations at `0x4da199` (0x40) and `0x4e62fa` (0x100) preserve bit0x80;
do not compare the entire DWORD and reject unrelated flag changes.

The [window/audio/close exclusion](ui-render-exclusion.md)
and [typed recovery closure](ui-render-mutation.md) remain
essential. Owner-thread execution alone does not prevent message reentrancy.
SC_CLOSE stays deferred until complete native render depth is zero, and the
known audio callbacks only publish completion. Healthy native recovery may
retire textures/effects, but does not run configuration construction/mounting
or a surviving configuration teardown. Normal shutdown exits instead of
returning a retired config to this render continuation. Foreign unwind from
fatal cleanup, arbitrary private-address calls or newly replaced callback
implementations remain outside the existing qualified profile.

Thus a synchronous **call-free** copy on the admitted owner thread cannot
dispatch a dialog, interpreter command, load or retirement between its two
loads. A pointer reread by itself would not establish this lifetime contract;
the finite owner/load/close admission does. No persistent config pin or new
retirement observer is indicated by the inspected native paths.

### Minimal reader and validation policy

Use the recognized FL loaded base and existing nonnested compositor/complete-pass
borrow. Before the native read interval, require the same live borrow, owner
thread, pass serial/incarnation/load generation and current invalidation/Reset
request. Read **one DWORD** at `base+0x1a83b0` into local G; check nonzero,
four-byte alignment and `G <= UINT32_MAX-0xf0`; then read **one unsigned byte**
at `G+0xf0`. Derive `(byte & 0x80) != 0`. Check loaded-base addition too.
Only G as a non-dereferenceable identity and the owned boolean may escape.
Publish the output only after validating the still-current borrow/request;
refusal clears output. A healthy false value is distinct from an unavailable
read, though both decline bloom and still allow ordinary scene-end forwarding.

The two native loads and local output copy must be inline scalar volatile
operations, with no engine/OS/page-query/COM/logging/allocation or metadata-lock
call inside that interval. Reuse the existing full bridge CPU, EFLAGS,
LastError, FP/SIMD and foreign-SEH envelope; a read fault invalidates admission
and uses its existing cleanup, never a stale-output retry. This is an
implementation obligation, not an executed reader fixture in this study.

After any potentially reentrant COM/native work, validate the ticket **before
another native read**, then repeat the fresh root-plus-byte copy. In particular,
refresh after pin/scene retention and preparation, before using readiness, and
after original forwarding before commit. Require the same G identity within
the same still-live incarnation and glow enabled. Never dereference saved G
after a call merely because it previously matched; same-address reuse is not
permission. A changed/false/unavailable value revokes modern replacement while
retaining invocation references until ordinary cleanup. This costs two scalar
loads per boundary refresh, not a per-draw native walk or file check.

### Runtime observations required by the shared executor

There is **no need to observe four successful stock draws** to reuse
`BloomPass`. The current AP bridge itself does not count those draws or query
native pass return values. The replacement's RGB comes from an independently
retained pre-original FP16 scene and exact ordinary-writeback snapshot; the
stock result is never its sampling source. The stock compositor can return
early or fail partway. Its actual outgoing image, alpha and state are still
the result to preserve if modern work declines, and the shared backup/recovery
operates on that genuine image. A scalar return or an attempted-draw count
cannot establish that output.

The existing FL post bridge already queries actual RT0, additional MRTs,
depth (including known-null), and viewport. Combine that successful binding
result with original-returned, exact same device/main/depth identities and
descriptors, reviewed owned profile/resource domain, a fresh enabled glow
snapshot, and unchanged pass/common/camera/Reset/query/state-recording/loss
admission. Let shared `BloomPass::commit` save/revalidate actual state, back up
the native output, write RGB only and restore its real outgoing state. Its
validation explicitly compares exact main/depth and their descriptors; its
post operation uses its own full viewport and restores the captured outgoing
viewport/scissor. The existing FL correlator may impose the stricter full-main
viewport policy; do not weaken that while adding bloom.

Wrong/unknown outgoing state, missing retained input, native unwind, nesting,
Reset or failed shared validation decline the candidate. A native copy/draw
failure which leaves an otherwise admitted actual boundary is **not by itself
a reason to add an event observer**: the shared independent candidate and
RGB-only commit remain defined there. This supersedes §4's provisional
suggestion that every native failed copy must be independently observed and
rejected. Do not skip the genuine original, infer a successful stock image or
force alpha to the static zero prediction. No native-pass suppression or
late-view-state optimization is authorized by this reader result.

### Additional evidence

```sh
python3 verification/results/fl-port/bloom-contract/glow_read_contract.py
python3 -m py_compile verification/results/fl-port/bloom-contract/glow_read_contract.py
```

The [additional producer](../../../verification/results/fl-port/bloom-contract/glow_read_contract.py)
and adjacent JSON preserve the original shader evidence unchanged. Measured:
**14 bounded regions, 152 instructions, 34 anchors, five rejected mutations**.
A separate **428,814-instruction** direct census finds **943 root references**,
the six stores and three destructor callers listed above. The mutation checks
bypass whole-image identity and change the native flag offset, dialog glow bit,
free target, command jump cell or command-name cell. These are static
instruction/layout witnesses, not an alias theorem, production admission test
or executed D3D fixture. No shader sweep, build, Wine/native run or game launch
was repeated.
