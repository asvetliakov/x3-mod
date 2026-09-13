# Motion/depth varying WRAP state

2026-09-13. Implementation candidate in `/tmp/x3-motion-wrap`, based on
`ddfe74d`; not installed or GPU-qualified. This addresses a documented state
contract, not an established cause of game stutters or shimmering.

Microsoft's [Shader model 3 reference](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/shader-model-3)
specifies that TEXCOORD semantics 0–15 use wrap interpolation when their
corresponding D3DRS_WRAP state is enabled. Generated previous-clip and current
clip/depth varyings require ordinary interpolation. Choosing an unused semantic
alone does not establish that interpolation state. This fix uses only documented
D3D9 render states and APIs; native Windows behavior remains untested.

## Contract and implementation

The production transformer already rejects an original declaration using either
reserved TEXCOORD. The table's compile-time checks require programs sharing a
VS/PS to agree on their insertion contract. A new bounded archive audit also
checks the actual original declarations against every compiled table row:

| Table | Pairs / original programs | Motion semantics | Consumed depth semantics | Collisions |
| --- | ---: | --- | --- | ---: |
| Current source | 169 / 140 | 4–7 | 1–3, 5–8 | 0 |
| Separate damage candidate, read-only audit | 171 / 142 | 4–7 | 1–3, 5–8 | 0 |

No damage source or material RGB transformation is merged by this change.
The ongoing material COLOR1 work remains separate.

`MotionOutput` extends the existing application render-state shadow from 8 to
24 entries, covering all sixteen WRAP states. The two non-contiguous D3D enum
ranges have direct index lookup; ordinary state lookup still scans only its
original eight entries. Each admitted draw queries only its generated motion
semantic and, when its PS writes depth, its generated depth semantic. It reads
both before changing either. A zero application state causes no setter call.

Nonzero values are saved in the stack-owned `MotionRoute` and temporarily set
to zero using native API slots, after variant, constant and target setup. This
ordering keeps the material-to-ordinary-motion bind retry independent of WRAP.
The attempted bit is set **before** each setter: a failed setter is still
rolled back, including a backend that mutated before returning failure.

`undo()` restores attempted WRAP states in reverse order before the existing
route state. Restoration is per draw even in lazy RT mode. Application WRAP
states for original shader semantics are untouched. No application state is
held between draws; public `GetRenderState`, stateblock recording and later
shader families see application values after successful restoration.

Successful application setters update the shadow; recorded setters do not.
EndStateBlock, Apply and Reset already clear the full shadow. With state shadow
off, the transaction reads the actual device values on each draw. Internal
WRAP setters bypass the application shadow, whose cached values stay logical
application values rather than temporary zeroes.

## Failure and recovery boundary

A read failure changes no WRAP state. A failed application step falls back to
one native source draw only after both lazy-target restoration and route undo
succeed. The route does not replay the source.

A failed route restoration latches `motion_state_lost_`, invalidates temporal
history and suppresses native source submission with the saved failure HRESULT.
This includes failure inside the material-bind retry's undo: a later no-op undo
must not erase the earlier failure. Rollback latches a failed lazy-binding
cleanup before invoking the later route undo, and undo preserves any already
latched HRESULT. The four existing draw hooks check the
combined submission guard before their other render preparation. The current
source, if already submitted, is not submitted a second time; subsequent
sources are blocked. Emission preparation, temporal resolve, HDR redirect/
writeback and bloom handoff also respect the latch.

The previous apply-failure path ignored rollback return values. The new policy
checks them rather than drawing with unknown shader/target/WRAP state. A native
pair may not read the reserved semantics, but a later shader could, so an
unrestored WRAP state cannot safely be ignored.

A failed Reset retains the latch. After a successful Reset, normal shadow
resynchronization runs and all sixteen actual WRAP states must be read
successfully before the latch clears. A failed read keeps submission blocked
until another successful Reset and resynchronization. No per-frame or
stateblock operation silently clears the latch.

## Verification and performance

Focused host command:

```sh
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_motion_wrap_states verification.analysis.test_linear_material_live
```

Result: **10 tests pass**, including **34,611 assertions** executing unchanged
production WRAP transaction, undo, state-shadow, stateblock and Reset methods
against a scripted API device. Coverage includes every distinct pair of the
16 semantic indices, depth on/off, shadow on/off, exact preservation of all
native semantic states, zero and mixed-zero values, invalid profile indices,
first/second failed reads, first/second failed zeroing with and without prior
mutation, both failed restoration positions, continued rollback, failed Reset,
failed post-Reset reads and successful recovery. Existing material/emission
control-flow tests additionally exercise early submission suppression and a
failed material fallback restoration. The independent review found and fixed
first-error replacement across multiple cleanup failures: combined tests now
cover material undo followed by failed deferred cleanup, and failed binding
cleanup followed by failed shader undo. The first HRESULT survives both.
The affected five-test subset passes after the fix, and `motion_output.cpp`
was cross-compiled again.

The cached-zero path executed 200,000 transactions with **zero native getters,
zero native setters and zero allocations** after its two initial cache fills.
One final host run measured 5.98 ns/transaction (native host architecture,
optimized synthetic loop; not x86 game cost, GPU time or FPS). The route adds
no lock, allocation, shader validation or hash computation per draw. Nonzero
state costs one zeroing setter plus one restoring setter per affected semantic;
shadow-off costs one getter per consumed semantic per draw.

The original-semantic audit is reproducible without Wine:

```sh
python3 verification/probe/check_motion_wrap_profiles.py --programs /tmp/x3-shader-sweep/programs
python3 verification/probe/check_motion_wrap_profiles.py --programs /tmp/x3-shader-sweep/programs --profile-root /tmp/x3-damage-motion
```

Both changed production translation units (`motion_output.cpp`, `capture.cpp`)
and the GPU fixture translation unit cross-compile with the project's x86
SSE2, stack-realignment and warnings-as-errors flags. Objects remain local under
`/tmp/x3-motion-wrap-objects/`; no production DLL was linked or installed.

An opt-in `X3M_FIXTURE_WRAP=1` extends the existing motion-output GPU fixture:
WRAP4/5 are hostile (all components) for the reference Argon motion/depth
varyings, native WRAP0 retains a separate application value, and snapshots now
watch all sixteen states. Existing image/motion/depth oracles, recorded setter,
stateblock Apply, Reset and lazy-burst scenarios exercise the new condition.
The lazy burst changes and reads back WRAP4 between existing draws, without
adding source submissions. This fixture was **compiled only**. Scoped retained-
DLL runs in X3, both depth modes and state-shadow modes, plus per-draw/lazy
state equivalence remain pending GPU qualification.

The motion-output runner accepts a retained production DLL, seam DLL and
fixture executable together. All three paths are required, as are explicit
known case selectors; this mode skips every build command and records the
supplied binary hashes, checking them again after execution. The existing
positional selectors and normal fresh-build path remain available. For the
candidate owner, the bounded hostile-WRAP burst command is:

```sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py \
  python3 verification/probe/run_motion_output.py \
  --dll /path/to/retained/d3d9.dll \
  --seam /path/to/retained/seam/d3d9.dll \
  --fixture /path/to/retained/motion_output_fixture.exe \
  seam-burst-perdraw-wrap seam-burst-lazy-wrap
```

These two selected-only cases enable the fixture's existing hostile WRAP4/5
setup. They retain the burst image, depth, restoration, routing and binding
counter oracles and require exactly nine successful application WRAP4
readbacks (95 checks per seam case). The usual partial report does not claim
a full-suite pass or perform cross-case equivalence comparisons. This pair
does not close the separate depth-mode, shadow-off, stateblock or Reset WRAP
qualification. No GPU execution follows from the runner change. Nine focused
host tests cover retained-input dispatch and mutation, rejected selectors,
the unchanged build path, strict WRAP evidence, exposure-case settings and
the render-state resync bound.

The legacy HDR inventory now owns its exposure modes instead of relying on
the old production Auto default: 14 cases explicitly request Auto (three
exposure cases, four tonemap/meter fault cases, the TAA Auto case and six AgX
benchmarks); 17 retain explicit manual EVs (ramps and TAA/sharpen twins,
including the identity ramp); the other 17 use identity write-back with the
runner's fixed-zero baseline. Manual EV environment inherited from the host
is cleared before each case's overrides. The metadata witness checks all 48
HDR cases and preserves the nonzero EV and `k=0` tests. The bench oracle still
requires successful metering and adapted steps, and the fault scripts retain
their original meter/unwind/adaptation expectations. No GPU validator was
relaxed to accommodate the fixed-zero production default.

The render-state resync bound is 24, matching the eight gate/mask states plus
WRAP0–15 in `MotionOutput::Shadow`. `render_state()` counts WRAP misses in the
same counters and `resync_shadow()` invalidates all 24 slots. A host witness
accepts 24 misses after one resync and rejects 25; the exact native-get
accounting, shadow-off accounting and zero-resync/no-miss requirement are
unchanged. This counter correction does not alter the two selected WRAP
cases' zero-resync gates.

The separate baseline capture-bloom lifetime test-double mismatch was repaired
on main in `a50809a`. Independent verification passed 33 scenarios / 139 checks.
That repair is outside this WRAP checkpoint; the unchanged WRAP tests were not
rerun for this documentation update. WRAP source review is complete. GPU
qualification remains pending as described above.

The source checkpoint `eb4e86b` integrates cleanly into main after the fused
emission optimization and Reset fixture repair. Independent integration review
finds no conflict in emission preparation, source suppression or Reset recovery.
The combined affected host scope passes 11 tests, including 34,611 WRAP
assertions and the repaired 33-scenario / 139-check lifetime fixture. No Wine
execution or installed-code change follows from this source integration.

## Retained combined candidate execution

Installed candidate `75dbbed` passes both selected hostile WRAP burst cases:
per-draw and lazy restoration, nine frames and 95 checks each, including nine
exact post-route WRAP4 reads in each case. The same retained seam also passes
120 automatic-exposure frames / 245 checks. [The compact selected result](../../verification/results/bottle-X3/combined-wrap-exposure.json)
links the immutable full report and binary hashes. No rebuild occurred inside
the runner. All three selected cases passed; the runner correctly reports
PARTIAL rather than a whole-suite pass. Native Windows/gameplay remain untested.

## Palette scalar transport extension

The source candidate in `/tmp/x3-palette-live-wrap` combines the reviewed
148-pair pure shader corpus with its required native scalar WRAP transport.
This is a source/host checkpoint; GPU interpolation and retained-DLL live
qualification remain pending. The [palette plan](linear-palette-materials.md)
defines the exact original scalar carriers and their precision requirements.

`Shadow` now caches the complete `LinearMaterialPairContract` at the existing
shader setter/registration boundary. Mask, technique, transport count and both
transport records clear together on invalidation or failed resynchronization.
There is no new draw-time shader lookup. The selected transports are:

| BUMPMAP shape | Original component | Relocated component |
| --- | --- | --- |
| Boron base/single | TEX6.X | TEX1.W |
| Boron base only | TEX6.Y | TEX2.W |
| Paranid | TEX7.X | TEX1.W |

The existing stack transaction has room for six unique state snapshots:
motion/depth, two scalar destinations and two sources. Actual Boron base uses
five with depth enabled because its sources share TEX6. Each unique state is
read once, and all reads finish before the first setter. Destination W takes
only the source's original selected bit; destination XYZ and any other bits
remain exact. The native source state is read-only. Multiple source references
share a snapshot, so mutation order cannot affect their values. Invalid count,
component, semantic, duplicate destination component or collision with a
currently generated temporal semantic refuses before any setter.

`route.linear_material`, established by the actual combined bind, selects the
transport. A failed combined bind restores its partial changes and retries
ordinary motion with that flag false; the later WRAP transaction then clears
only the ordinary motion/depth states. A later transaction failure uses the
existing complete rollback and native-submission guard. Attempted setters,
including a mutation-before-failure backend, restore in reverse order. Failed
restoration keeps the first HRESULT and quarantines submission until the
existing successful Reset plus full-state resynchronization succeeds.

All writes bypass the application shadow and restore per draw, including lazy
target binding. Recorded setters, stateblock Apply and Reset retain their
existing logical-state rules. The source adds no heap allocation or lock;
bounds are two transport records and six snapshot slots. Setter calls occur
only where the desired state differs, with one restoration per attempted write.
The unchanged zero-temporal/no-transport path still uses only its original one
or two snapshots.

Focused host results: the four-test `verification.analysis.test_motion_wrap_states`
module passes, with its affected transaction test rerun after tightening the
real-family temporal-index witnesses. The final transaction run executes
**34,773 assertions**. It covers exact Boron TEX7/TEX8 and Paranid TEX5/TEX8
motion/depth combinations in both depth modes, all native source-bit cases,
destination preservation, shared sources and cross-source aliases, every read
and attempted-write failure position, reverse restoration and first-error
retention. It composes the actual `bind_variant_pair` with the actual WRAP
transaction to prove both combined-stage failures cancel the scalar remap.
The independent malformed-contract checks execute before any API mutation.

The single affected
`verification.analysis.test_linear_material_live.LinearMaterialLiveTests.test_production_control_flow`
test also passes. It retains the existing source-suppression and rollback
witnesses and adds complete scalar-contract lifecycle checks: zero/one/two
records, same-pointer re-registration, early-exit/exception invalidation before
Release callbacks, recorded setters, Apply, failed shader resync, failed Reset
and successful recovery. Unrelated CLI and numerical suites were not repeated.

A cached Boron-base transaction (two scalar transports and depth) ran 200,000
iterations at **41.37 ns/transaction**, with zero native getters/setters,
allocations or shader lookups after initial cache fill. This is an optimized
native-host synthetic diagnostic, not x86 game cost, GPU time or FPS. The two
affected production translation units, `motion_output.cpp` and its `MotionRoute`
consumer `capture.cpp`, cross-compile with x86 SSE2, four-byte incoming-stack
realignment and warnings as errors. Objects are local `/tmp` outputs; no DLL
was built, installed or executed for this source checkpoint.
