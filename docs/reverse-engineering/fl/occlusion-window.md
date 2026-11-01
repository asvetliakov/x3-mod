# FL occlusion: mutation windows and retirement

Bounded offline native/source study, 2026-10-29. Image
`/mnt/c/X3FL-orig/X3FL.exe`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Addresses are preferred VAs, base `0x400000`; installation must resolve RVAs
against the admitted image. **[s]** = measured static/source fact; **[i]** =
inference or implementation requirement; **[u]** = unqualified. No production
edit, build, native/Wine execution, game launch, install or commit.

**The supported main-view interval excludes a native model/reparent mutation
which would stale Draw's classifier memo [i]. Draw's GPU query history still
needs an owner/generation check or invalidation/rekeying before integration;
fresh classification and a reset `Part` do not retire query-ring results [s/i].
The engine mode's previous-frame ledger separately needs mutation retirement
beyond the existing model/view/position checks [i].** Native commands can
reparent a live node between frames without changing those guarded words or its
registry identity. Disarming the current table alone does not retire that ledger.
Neither mode is enabled by this study; the shared draw integration remains
behind `ap_native_families_` [s].

Dependencies reused, not reopened: [material admission and native-command
scheduling](material-submit.md#3-actual-borrow-readiness-writers-and-reentrancy),
[own-ship/parent borrow](own-ship.md#3-why-the-existing-material-seam-can-own-this-copy),
[ordinary origin and Clear](cull-chain.md#6-scene-projection-publication-precedes-selection--2026-10-29),
[nozzle scene subsets](nozzle-scene-window.md),
[camera retirement](camera-buffers.md),
[native window/audio/close callbacks](ui-render-exclusion.md),
[typed resource recovery and playback continuation](ui-render-mutation.md),
and [body-name ownership](body-name-lifetime.md).

## 1. The interval is a selected view, not an immutable complete pass

Ordinary path [s]:

`0x48ba3a → 0x494b00` prepares the node fields; `0x48ba43 → 0x495670`
activates the camera. Within that origin, `0x495687 → 0x4ded90` publishes
the matrices, then `0x49568e → 0x4db750` reaches D3D9 Clear at `0x4db7f8`
(device slot `+0xac`). Only after origin `RET 4` at `0x4957a2` does
`0x48ba4c → 0x497720 → 0x497745 → 0x495e50` select nodes and reach
the shared cull site `0x4961d3`. Immediate submission follows at
`0x48ba73/0x48ba7f → 0x4978e0`; deferred drain at `0x48ba8d → 0x497680`.

Publish the engine table only on the successful, correlated main Scene Clear,
before returning to native selection [i]. Clear can fail or be absent while
native origin still returns and selection proceeds; those paths must leave the
table disarmed. Normal valid origin return must retain the published table
for the selector. The next camera activation must disarm it **before** native
activation work; a different camera can otherwise overwrite position/matrix
fields before reaching its selector [i].

The complete pass has these concrete model/reparent paths [s]:

| Native route | Position relative to eligible main use [i] |
| --- | --- |
| `0x48b7cd → 0x48ad90 → 0x48ae25 → 0x4a35e0` | Initial setup before sorted camera activation and Clear. |
| `0x48bc69 → 0x48af40 → 0x48b0a9 → 0x4a35e0` | Later special-scene proxy initialization. Allocation is at `0x48b09d`; attachment to engine `+0x64` scene at `0x48b0dc`; row-slot publication only at `0x48b0e8`. It does not mutate source row `+0xc`. The following special origin at `0x48bc87` precedes its selection at `0x48bc96`. |
| `0x48bded → 0x48f570`, model call `0x48fe9c`, reparent call `0x49030a` | Playback after the last material submission and native EndScene (`0x48bd9e`). Returning continuation is array cleanup, with no new selector/material use in this invocation. |

Thus there are mutations later in the complete pass, and the module's old
armed bit could remain set until a later retirement boundary [s/i]. That is
different from a mutation followed by an eligible old-view selector use.
The special nodes are a different healthy scene; normal camera retirement and
the exact native camera comparison exclude their selector. Playback's old
table must not become next frame's publication source. The source ledger, not
just the armed bit, matters there [i].

The Ghidra direct-call survey, pruning the explicitly nonreturning fatal
cleanup `0x4762d0`, located those three complete-pass model cones and the one
playback reparent cone. It located no returning direct model/reparent route
under ordinary activation, selection or submission [s]. This finite survey
is a locator, **not** a proof of indirect closure or a universal graph-write
census. The actual callback contracts below supply the additional exclusion.

### Loaded resources, recovery and reentry

Selector calls resource lookup `0x496222 → 0x4a1a40`; frustum work can also
reach it (`0x495fd3 → 0x4e9000`, lookup `0x4e926a`). A loaded-resource hit
is not a pure lookup contract: native lookup mutates resource bookkeeping.
The absent-resource path parses BOB/BOD through `0x4a1ca2 → 0x49ae00` or
`0x4a1d24 → 0x49db00`, may register body names, and can reclaim resources [s].
In particular, audio update **`0x4a1d4b → 0x4b42e0`** can invoke the fixed
VM result/ready callbacks. Their actual `0x4c1a40/0x478910` endpoints publish
results/ready state; they do not synchronously execute KC setters. Parser fatal
cleanup can reach node operations but exits the process; it is not a returning
route to another selector use. These distinctions are established in the
[body-load/recovery closure](dust-cleanup-timing.md#2-body-loading-and-recovery-do-not-acquire-this-node)
and the callback dependencies above [s/i].

Allocation recovery `*0x5aae24=0x47b500` invokes `0x485c00/0x512410` under
`0x62de10`. Usage marking reads node model IDs and children; reclamation frees
resource/mesh/texture payloads, rather than reparenting render nodes. The typed
D3D/D3DX/zlib/state-manager provenance, including texture demand loading, is
the existing render-mutation contract. Native window/audio delivery does not
run the interpreter; SC_CLOSE is the actual synchronous KC exception and must
remain deferred by common admission [i]. No lock or raw complete-pass depth
counter replaces this closure. Foreign private-address calls, altered native
callback registrations/vtables and corrupted lists remain outside the healthy
native profile [u].

## 2. Between frames: what the current guards do and do not prove

The actual KC B3D transport calls model setter at **`0x4afcb7`**, with
ECX=resolved node and one stack model argument; reparent at **`0x4b08a6`**,
with ECX=child and pushed EAX=resolved parent. The synchronous owner scheduler
can run these commands before the next complete render [s/i]. They are not
excluded merely because the previous frame had an admitted render.

Reparent `0x4a57d0` unlinks the child (`0x4a57ec/0x4a57f3`), links it into
the new parent's list, stores parent at **`0x4a5809`**, and invalidates parent
radius at `0x4a580f`. Its context propagation `0x4a5670` changes scene `+0x1c`,
recursively. Neither body directly writes model `+0x144` or guarded position
`+0xf0/+0xf4/+0xf8` [s]. Model setter `0x4a35e0` loads resources before its
model store `0x4a365d`, can destroy flagged children at `0x4a362c`, and resets
scale fields beginning at `0x4a366b`. A nonzero identical model takes its early
return; model zero is explicitly allowed to rebuild [s]. Do not reduce setter
effects to the final model store.

| Existing check or boundary [s] | Coverage and limitation [i] |
| --- | --- |
| Stub node token, camera token, frame stamp, model ID, `+0xf0..f8` window | A changed model ID or out-of-window position refuses the old entry. It has no parent, scale, basis, class revision or mutation witness. Same-model reparent passes the tuple; A→B→A model changes can restore it. The ordinary position window is an intended temporal policy, not topology identity. |
| Owned observer serial / mutation revision | Birth/removal/map changes bind identities. A surviving node's reparent/model replacement need not retire/reinsert it. Registry mutation revision is not model/reparent generation. |
| Camera `retire_dependents()` | Currently zeros small-part threshold/upper and optionally invokes sun retirement. It does not currently retire occlusion table, ledger, classifier or batcher. Its existing pre-activation/invalid/unwind sites are the right **table-disarm** boundaries. Normal successful return must preserve publication. |
| Present `occlusion_engine_cull::take()` / `disarm()` | Zeros armed bit. Does not invalidate the CPU ledger later selected by `frame == stamp-1`, `touched_n` and nonzero `view`. |
| Body metadata registration/restore | Existing entry disables metadata and advances its revision **before** forwarding. This is a useful class retirement boundary; it is not a reparent/model notification. A future occlusion consumer must actually observe that revocation/revision. |
| Whole-load / close / common admission | Supplies borrow exclusion and load/pass identity. Does not itself retire all occlusion CPU history. Unconditionally deleting the previous ledger every normal main-camera activation would defeat the intended engine duty cycle. |

Scene attachment **`0x4a56b0`** is an additional canonical parent writer:
`0x4a56cd` clears node `+0x18`, before scene publication at `0x4a56f6`.
Its KC caller is `0x4b079e`. Detach `0x4a5740` instead clears scene `+0x1c`
at `0x4a57ba`, retaining parent; it can release node GPU attachment through
`0x4a5758 → 0x4e7240` before that store. Therefore “hook reparent covers all
topology” would be false. Whole restore, creation and retirement still use
their existing load/registry boundaries. This note does not assert that these
three setters are a universal census of every aliasing field store [u].

### Draw memo, query ring and engine ledger have different lifetimes

Draw classifier memo is `(node, frame)` and returns its saved model/class/ship
before rereading inputs. It would be stale after a same-frame model/reparent
mutation. The qualified main-view interval above excludes that native schedule;
the material's owned node/model/six-ancestor sample still must be the input to
the FL classifier, with the complete current-camera route gates [i]. Between
frames the frame stamp forces fresh classification. `Batcher::note_part`
additionally resets a draw's `Part` when its ship changes. Those facts qualify
classification and batcher bookkeeping only; they do **not** qualify the
renderer decision's independent query history [s/i].

`draw_key` includes node, buffer, vertex range and model, but omits ship/parent
and any owner generation. The query `Ring::Record` and `ResultSlot` likewise
carry a key and rectangle without a ship/generation witness. In the actual
renderer, `OcclusionCullPass::part` calls `read_previous(batcher)` **before**
`batcher.note_part`. `read_previous` imports a ready result into the ring by
that key and separately calls `batcher.on_result`; a subsequent changed-ship
`Part{}` reset does not clear the ring. `decide` then calls
`ring_.lookup(part.key)` and `may_skip(previous, part.rect, part.hull_drawn)`,
without an owner comparison [s].

A bounded counterexample is: frame N issues the key's test under ship A; at
N+1 the same node/model/geometry key belongs to ship B, whose hull drew, and
its rectangle is unchanged. Ready zero-sample data from A is ingested first;
`note_part` resets the batcher entry to B with `last=none`; `decide` still
finds A's hidden result in the ring and permits a skip. This is a source-bound
scalar witness of the current core/renderer path, not a GPU execution or game
capture [i]. A fresh classifier stamp, current B hull or reset batcher `Part`
does not prevent it. An intervening successful B block may issue new queries,
but does not clear or ingest the ring's imported results before this decision [s].

Before Draw integration, the consumer must validate the result's owner/domain
generation, rekey history consistently across issue/import/lookup, or invalidate
both pending query records and imported results on the relevant retirement.
Clearing only a `Part` or only today's imported result while leaving old
queries eligible for later ingestion is insufficient [i]. This gap does not
by itself establish a new native Draw-only detour requirement: fresh owned
ancestry supplies the current owner for a consumer-side check. Metadata revision,
load/registry reuse, Reset and abnormal admission likewise need actual query
history retirement [i/u].

Engine mode deliberately republishes the preceding frame's fully skipped-node
ledger before it can obtain any current material sample for that node. Its
ledger/table have no ship/topology witness. A previous ledger followed by a KC
reparent, retaining model/view/position, is a concrete allowed native ordering
for which existing guards cannot establish mutation-coherent publication [i].
The producer's scalar witness establishes this insufficiency; it is not a
captured game failure. Existing one-to-three-frame temporal reveal latency is
separate policy, not proof that node/model/topology reuse has been retired.

## 3. Minimal retirement requirement and candidate native boundaries

For engine activation, retirement must happen **before native model/reparent
effects or their first callback-capable callee**, and must revoke both current
table eligibility and the old ledger/history's permission to republish [i].
A fixed scalar revocation consumed at the existing owner publication/history
boundary suffices; no node query, native parent walk, registry replacement,
subscriber bus, GPU call or immediate bulk clear inside the setter is required.
The current source has no such model/reparent revocation receipt [s]. A table
disarm performed after a setter returns is weaker: the setter already calls
resource work/destruction, and can unwind without returning.

The precise bounded entry candidates are [s]:

| Entry | Whole-instruction displaced interval | Native ABI / earliest frontier |
| --- | --- | --- |
| Model `0x4a35e0` | `[0x4a35e0,0x4a35e6)`, 6 bytes | ECX=node; original ESP+4=model; RET 4 at `0x4a3616/0x4a3756`. Stack alignment occurs inside this span; resource call `0x4a3603` precedes mutation/destruction. |
| Reparent `0x4a57d0` | `[0x4a57d0,0x4a57d6)`, 6 bytes | ECX=child; original ESP+4=parent; RET 4 at `0x4a5856`. Native detach call `0x4a57de` precedes unlink/parent stores and may release GPU attachments. |
| Scene attach `0x4a56b0` | `[0x4a56b0,0x4a56b6)`, 6 bytes | ECX=node; original ESP+4=scene; RET 4 at `0x4a573d`. Native detach call `0x4a56be` precedes parent clear. Needed if the retirement claim includes this known parent mutation. |

All three six-byte spans contain only whole register/stack instructions,
no relative operand or SEH installation. Each first five-byte redirect fits
one aligned qword. The linear `.text` census finds no direct branch/call into
their interiors and no HIGHLOW publication naming their entries/interiors;
this does not exclude arbitrary computed private entry [s/u]. This is boundary
evidence, **not an implemented or fixture-qualified patch**.

At entry ECX and the stack word are live; EBX/ESI/EDI/EBP and exact ESP are
native caller state. Preserve all GPRs, EFLAGS/control flags, LastError and
FP/SIMD state if a helper is introduced. Original arithmetic flags are killed
before native conditional use; that does not grant other state as scratch.
Replay all six displaced bytes once, retaining the model setter's native
stack alignment and the project's four-byte incoming helper contract [i].

Revocation should be call-free scalar work before native forwarding, with no
held lock, native read, allocation, logging or callback. Nested setter entries
may repeat retirement safely; outer completion must never re-arm an old
publication after nested revocation. Continued exceptions retain revocation;
outward unwind leaves it revoked, without dereferencing any saved native token.
No new native scope or catch-and-continue permission follows [i]. Existing
common foreign-owner/Reset request protocols still apply; this is not permission
for unsynchronized foreign native writers. Actual trampoline CPU/SEH/rollback,
failure/recovery and performance acceptance remain implementation work [u].

Extend the existing fixed camera retirement for current-table disarm, and reuse
the existing name/load/registry/Reset boundaries for their corresponding CPU
history revocations [i]. Only the uncovered native setter cases above require
additional pre-mutation entry coverage for engine history. The complete FL
occlusion feature remains gated pending that implementation, Draw query-owner
history qualification above, and native synthetic/runtime acceptance; no Windows
or CrossOver runtime behavior is verified here.

## 4. Reproduction and evidence limits

```sh
python3 verification/results/fl-port/occlusion-window/produce.py
python3 -m py_compile verification/results/fl-port/occlusion-window/produce.py
```

[Producer](../../../verification/results/fl-port/occlusion-window/produce.py) and
[derived record](../../../verification/results/fl-port/occlusion-window/contract.json)
check **17 regions, 2,325 decoded instructions, 66 operand anchors, eight
rejected semantic negatives and 31 source clauses**, plus **six structural
checks** of the Draw key/Ring/renderer path [s]. The source-bound Draw witness
demonstrates the old-owner hidden-result reuse after a `Part` reset and checks
four refusal controls; it does not execute the actual C++ core or D3D. The whole-text direct
caller census independently reproduces **79 model, 24 reparent and 56 scene
attachment calls** [s]. Guard counterexamples are explicitly synthetic, not
observed game schedules. Semantic negatives bypass the image identity gate;
operand assertions do not depend on the recorded region hashes. The producer
records no executable bytes or decompiled bodies.
Raw listings, Ghidra graph/decompilation and the local research script remain
untracked under `~/.cache/x3m-port/fl-occlusion-window-research/`; read-only
Ghidra used the existing research project through `/tmp/x3m-fl-occlusion-ghidra`.
Project tools: `inspect_x3.PE32`, Capstone, `X3FunctionBody.java` and
`X3DecompileFunctions.java`. The direct-call graph was used only as described
above; actual supported indirect callback behavior comes from the named owning
notes, not graph absence. Static and scalar-model results do not execute a
setter, an emitted stub, D3D, a native exception path or a game frame.
