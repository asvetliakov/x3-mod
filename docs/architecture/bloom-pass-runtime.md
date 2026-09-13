# Native D3D9 bloom resource executor

2026-09-13. `src/renderer/bloom_pass.h/.cpp` implements the resource/state work
for [the original-then-RGB-replacement boundary](hdr-bloom-boundary.md) and
[candidate composition](hdr-bloom-composition.md). It is not yet connected to
capture, the scene hook, CMake or an installed DLL. Isolated production and
fixture-enabled x86 compilation passed with the project's SSE2/four-byte
incoming-stack flags. GPU execution, failure-fixture qualification, integrated
ownership and game acceptance remain pending. Native Windows execution is
unverified; runtime feature availability does not depend on platform proof
flags, backend-private behavior, DLL identities or hashes.

## API and caller obligations

`attach` borrows a native device and its saved original method table, and
consumes a bundle of authored precompiled bytecode. The bundle includes the
pass-through quad vertex shader, six extraction programs, down/up kernels,
bloom-aware AgX, existing display RCAS and an identity display copy. It need
only remain valid through shader creation. No runtime compiler, compiler DLL
or game shader bytes are used. Public creation flags refuse pure devices and
software-only vertex processing; hardware and mixed devices are supported.
Mixed devices temporarily select hardware VP for injected draws and restore
the captured software-VP value through documented device calls.

Availability requires actual SM3/texture/filter/color-mask/StretchRect caps,
public format checks for FP16 and A8R8G8B8 render-target textures and sampling,
FP16 filtering, and successful shader/declaration creation. Dimensions also
respect texture width/height, aspect, square-only and conditional-NPOT caps.
The conditional-NPOT route uses a single mip, clamp addressing and no mip
filtering. Actual draw/copy HRESULTs govern operation success. Fixture results
are verification evidence, not arbitrary caller permission bits.

The caller must establish these conditions before `prepare` and re-establish
them after the original compositor before `commit`:

- The exact owner device, thread, frame, Reset generation and invocation are
  eligible; the application scene is already open. No Reset, reentrant call,
  active query, state-block recording or lost-device scope is admitted.
- All calls, including resource acquisition/release, run inside the existing
  native/internal-call execution scope and the owner's serialization. Every
  device operation in this module uses the supplied saved method table.
  The class does not lock, mutate game caches or update the proxy's state shadow.
- Native main/depth resources are pinned across original execution. A null
  expected depth is a known null binding. The supplied descriptors are exact;
  initial main is A8R8G8B8, DEFAULT-pool, non-MSAA, and the initial viewport
  covers main. Post-original viewport/scissor can differ and are restored
  exactly. Post-original RT0/depth must retain the expected identities.
- The scene input is the retained resolved pre-original FP16 texture, never
  the finished original-bloomed main image. The caller supplies the exact AgX
  constant block consumed by ordinary writeback, including its latched
  exposure multiplier. The decode enum must match the block. This class
  neither computes EV nor meters, resolves TAA or modifies history.
- Internal scratch/candidate surfaces must not be exposed to application
  draws or bound by the application through unrelated samplers. Only s0/s1
  are touched by these shaders and saved by this executor.

`BloomBoundary::admitted` records these execution preconditions; it is not a
backend-verification or feature-enablement flag. Descriptors, actual bindings,
thread and token generation are independently checked in the executor. Main,
depth and source device ownership are checked once during preparation through
public resource `GetDevice` calls, never inside the pyramid draw loop.

The original compositor must execute exactly once between successful
preparation and an attempted commit. This class does not call it or suppress
its draws. The caller owns the exception-safe bridge, original CPU outputs,
query/state-block tracking, invocation ticket, final-Release coordination and
Reset lifetime barrier. This executor handles D3D HRESULT failures; its C++
scope destructors are not a substitute for the game's SEH invocation bridge.

## Preparation, tokens and resources

`prepare` revokes any previous token before validating input. Size, level
count or sharpen-mode changes build a complete temporary resource set, then
swap it into place only on success. Failure releases the temporary set and
keeps the old allocation intact, but never revives the previous token. At a
fixed layout, all resources are reused. The layout has at most six downsample
levels and five separate reconstructed levels; the coarsest reconstruction
aliases the coarsest downsample and needs no extra target or draw.

The pass allocates candidate and genuine-original recovery images in
A8R8G8B8. Sharpening additionally allocates a full-resolution FP16 display
stage. It renders decode/extract, the reductions and reconstruction, then
AgX plus exposed-linear bloom. With sharpening it then executes existing
display RCAS into the complete RGBA8 candidate; without sharpening AgX writes
the candidate directly. Strength zero still prepares a replacement candidate
to remove stock glow, with the documented FP16-stage quantization distinction.

Every persistent image retains **only its level-zero surface**, matching
HdrPass's native device-reference model. Preparation acquires bounded texture
container views into an inline local array; commit acquires only the candidate
view. They are released before returning. No borrowed texture pointer is kept
after its interface is released. The candidate token contains the retained
surface identity, pass identity, epoch, serial, frame, Reset generation and
thread. It is published only after successful baseline restoration.

`commit` consumes the pending serial before any post work, including on
rejection. A copied token, reused surface pointer, repeat boundary, reprepare
or Reset cannot authorize a second copy. The caller must separately pin the
candidate/main/depth resources in its invocation record and clear those pins
before native Reset. `before_reset` first revokes the token/epoch, then releases
all own DEFAULT-pool surfaces. Shader/declaration objects survive Reset;
`shutdown` releases those too. A pass disabled by loss or failed restoration
stays disabled until a fresh attach; Reset alone does not declare it repaired.

`references()` counts persistent surfaces, shaders and the vertex declaration,
not texture-plus-surface pairs. `transient_views()` exposes currently acquired
container interfaces and returns zero at public return. `releasing()` marks
internal resource teardown so the owner can suppress final-Release recursion.
The owner must count its separately held invocation references and maintain
the native/internal-call barrier while transient getters and saved-state
references exist; it must not apply a final-Release heuristic in the middle
of this operation. The executor cannot make the capture owner immortal.

## State and recovery behavior

There is one manually saved state bracket per preparation and one per commit.
The saved set includes every supported MRT, exact depth including null,
viewport/scissor, FVF/declaration, VS/PS, stream zero and its frequency, s0/s1
textures and changed sampler fields, PS constants c0–c28, software VP, the
exact `GetNPatchMode` value and all
changed render states. UP draws clear stream zero; unaffected streams remain
untouched. Programmable quad inputs do not change fixed-function transforms,
texture-stage states, vertex constants or vertex samplers.

Setup unbinds secondary MRTs and depth, binds the authored quad/declaration,
sets stream-zero frequency one, disables alpha test, all blending, depth,
stencil, fog, scissor, hardware sRGB, dither and clip planes, and establishes
solid fill, no culling, disabled N-patch and adaptive tessellation, full multisample mask
and the required color mask. An inherited tessellation mode above one must
not affect the triangle strip, whose quad declaration has no normal input.
`D3DRS_ENABLEADAPTIVETESSELLATION` is also saved, set false and restored;
disabling ordinary N-patch mode alone does not establish the adaptive state.
Each output binds its full viewport. s0 is point/clamp, s1 linear/clamp;
both use mip zero with mip filtering/sRGB disabled. Prepare uses RGBA mask
15; the final candidate copy uses RGB mask 7, preserving the actual original
compositor's destination alpha.

Restoration unbinds injected read textures, secondary MRTs and depth before
restoring original RT0, then restores other MRTs/depth and viewport/scissor.
It restores software VP, the original N-patch mode, programmable bindings, stream/frequency, constants,
samplers/textures and changed render states. It attempts every restoration
operation and reports the first failure. Saved references release afterward.

After capturing the genuine outgoing state, commit creates the rollback image
with public same-format `StretchRect(main, recovery, NONE)` before any main
write or injected state setup. The source/destination are non-MSAA color RTs;
the texture-source capability is checked at attach for the reverse path.
The depth/stencil-only BeginScene/EndScene restriction does not prohibit this
color copy. A backup failure declines replacement with main/state untouched.

If candidate setup fails before any possible main write, commit restores only
state. A failed initial restoration retries the same snapshot; no recovery
image copy is issued, and pristine main remains untouched even if a recovery
copy fault is armed. This avoids turning a harmless setup failure into a
possible main-image corruption. State recovery and disablement are reported
separately.

After an attempted candidate write, if the write or its restoration fails,
commit uses the successful
backup to restore RGBA with `StretchRect(recovery, main, NONE)`, then retries
restoration from the **same original outgoing snapshot**. It never snapshots
partially restored state. Failed draw and failed rollback calls may have
partially modified main; neither is assumed atomic. `original_preserved` and
`state_preserved` report separate outcomes. `committed` remains false after
any recovery. Unrecovered image/state, loss or an initial restoration failure
requires requalification and disables further preparation. A failed write
whose image and state both recover cleanly remains a declined frame; the
owner may apply its broader repeated-failure policy.

## Verification seams and performance pass

`X3M_BLOOM_PASS_FIXTURE` enables independently counted faults for allocation,
state save/setup, preparation draw/restore, backup, candidate write, initial
restore, rollback and rollback restoration. Candidate-write failure is
reported after a real draw, so recovery must repair changed pixels.
Restoration faults actually omit the changed color-mask restoration while
attempting the other setters; retries must repair that state from the original
snapshot. Multiple faults can be armed together. Native method-table fixture
wrappers can additionally fail individual getters/setters/copies or emulate
partial writes. Production builds have constant-false fault checks.

The implementation has no per-scene-draw work, locks, host heap allocation or
CPU readbacks. GPU allocations occur only when the bounded layout changes;
state getters, descriptor/ownership validation and texture-container views
occur once per boundary operation. Inside the pyramid loop, work is limited
to dimension constants, output/sampler/shader binds and one fullscreen draw.
There is no repeated decoder/parameter validation or resource discovery there.

`resource_bytes()` reports the logical persistent allocation size from the
documented formats and layout, excluding driver overhead. For 1080p/five levels
with sharpen it is 44,210,880 bytes; without sharpen it is 27,622,080 bytes.
`peak_allocation_bytes()` is a conservative high-water allocation budget: on
replacement it includes the old set plus the complete requested new set,
including when allocation fails partway. It is an upper bound on requested
simultaneous storage, not a sampled driver VRAM measurement. Existing scene,
history and caller-owned surfaces are excluded.

The source was cross-compiled both with production faults disabled and with
fixture seams enabled using `-O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse
-mstackrealign -mincoming-stack-boundary=2`. An object-code spot check finds
x87 stores required to receive the C-library float-return ABI for
`exp2f` and public `GetNPatchMode`; no x87 arithmetic was introduced.
Independent source review passed after fixing inherited tessellation state and
preventing an unnecessary rollback write when no native draw was issued. Full production linking/audits,
state/failure/reset/ownership GPU fixtures,
numerical candidate comparisons and GPU/CPU timing remain integration work.
