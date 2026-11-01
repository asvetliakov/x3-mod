# AP/FL UI save-load comparison

Offline, 2026-10-28. Scope follows the binding [UI clarification](../../architecture/farnhams-legacy-port.md#ui-scope-clarification-2026-10-28):
the user cannot change resolution in game and reports working AP scaled-UI
save/load. Compare a saved game restored at the **same configured scale and
resolution**, hence the same virtual screen-query dimensions. Native VAs below
are preferred image addresses; KC addresses are file CODE offsets.

## Conclusion and minimal port boundary

**No additional KC refresh hooks or coordinate repairs are justified by this
comparison for ordinary same-dimension save/load.** Both editions restore saved
UI members, then run native `Restart → _Restart`. Their selected UI restart
suffixes are identical after symbolic relocation. Current-menu Resize, clipping,
mouse mapping, monitor positions/visibility and steering reset already run.
The retained cursor, dormant-menu limits and message positions singled out by
the earlier studies are also retained in AP. Retention alone is not an error
when their coordinate system has not changed.

Leave the proposed KC task-binding/completion hooks, outer-loop refresh
controller, early Restart cursor clamp, dormant-menu getter repair, explicit
message/timer repositioning and extra UpdateCursor dispatch **unintegrated**.
They are not activation prerequisites for this port. Do not require a new
layout generation merely because a save was loaded. The existing static
completion/repair investigations remain useful conditional engineering evidence,
not evidence of a reproduced game defect or a necessary FL feature.

The minimal implementation boundary is to establish the configured UI scale
and matching screen-query/input/projection conversions before native UI use,
then preserve the game's own save/load and Restart sequence. Normal D3D
Reset/resource recovery still applies. Borrowed camera/object state must still
be invalidated and safely rebound across actual native destruction/reload;
load/render admission, owner thread, reentrancy and native exception handling
remain necessary wherever the implementation takes those borrows. This result
does not authorize stale camera pointers or weaken the separately established
[camera lifetime](ui-camera-lifetime.md) and
[load readiness](ui-load-readiness.md) contracts.

For a native role borrow, require normal successful load and the actual
required cockpit/camera restoration/publication, not just a nonnull old slot.
The native parent return alone can bypass cockpit restoration when its registry
owner is null. Do not reinterpret that borrow readiness as proof of KC task
completion, nor require KC task completion to publish an unchanged coordinate
system. A failed or unwound load must not publish success from an observer.
The native Restart remains native execution; no new mod-owned KC dispatch or
member-array borrow is needed for the comparison's layout conclusion.

Actionable integration decision for the existing 20-seam UI foundation:
retain `State::finish(Load&)` readiness on **normal successful native parent
return plus successful cockpit restoration and intact role tracking**. Its
`finish_scope` already records the cockpit result separately and keeps render
admission closed during load. Native Restart then follows in the outer loop
before ordinary input/render. No concrete supported reentrant consumer edge
identified in this bounded study needs a new Restart-completion gate at the
unchanged scale. This is not a claim that KC callbacks cannot reenter; it is the
absence of a demonstrated coordinate inconsistency requiring that extra gate.
Keep the existing borrow, close-deferral and exception protections. Initial
display publication is still required; do not connect the provisional refresh
controller or add Reset relayout as part of it.

The AP source baseline supports this distinction:
[`ui_scale.cpp`](../../../src/engine/common/ui_scale.cpp) installs its scale cells at
`device_created`, updates the camera exclusion in `present`, and changes cells
in automatic `after_reset` only when the computed scale changes. It has no
KC save-load repair or Restart task-completion controller. AP's working-save
report therefore does not imply hidden mod-owned KC repair machinery.

This is a static finding supporting the minimum port, **not runtime FL
acceptance**. User testing must still exercise scaled menu/graph/input,
save/load, monitors and shown FL timers; no such game run was performed here.

## 1. Provenance and comparison method

[verify_fl_ap_ui_save_load.py](../../../verification/probe/verify_fl_ap_ui_save_load.py)
reads clean `/mnt/c/X3AP-orig` and `/mnt/c/X3FL-orig` directly. AP uses
`addon/04.cat:L/x3story.obj`; FL uses `addon2/03.cat:addon2/L/x3story.obj`.
No Mayhem resource substitution was needed to compare clean edition behavior;
the user's AP experience is external runtime evidence, not a run measured here.

| Input | SHA-256 / build |
| --- | --- |
| AP EXE | `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` |
| FL EXE | `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` |
| AP story | `ed5786a0c603802d5735fb36e0332d7c732b981d2b64dcee4174890284faff7a`, build1510658513 |
| FL story | `dbb732716e1acab6b8cc7f87f5d31324fb4764dfc22d1cec808749a4f45ec7b9`, build1636047691 |

The corrected shared KC reader includes SYMB globals (AP184, FL243) in method
boundaries and decodes opcode84 as a direct global call. The verifier compares
whole selected methods, resolving calls and string operands symbolically and
branch/switch destinations to instruction indices. It preserves opcodes,
constants, field indices and control flow. Of **25 shared methods, 23 match
exactly** after that relocation; the other two match only with the explicit
field changes below. This is not whole-story equivalence.

## 2. Native load, persistence and Restart

| Operation | AP | FL |
| --- | --- | --- |
| Outer-loop native load call | `4038ba → 404cc0`; testEAX `4038c2`, zero branches to teardown `4041e5` | `477c52 → 479430`; success branch `477c59 → 477c6a` |
| Story-state load before renderer restore | `40507b → 4a0880`, then `40508d → 47a720`; story object reload `4a0941 → 49d030` | `47983c → 4bcb10`, then `47984c → 493510`; story object reload `4bcc27 → 4b8e20` |
| Conditional cockpit restoration | `405112 → 41f720`; failed AL branches `405119 → 4051a0` | `4798bf → 420d30`; failed AL branches `4798c6 → 479965` |
| Post-success named Restart dispatch | `4038ee → 49f570`, name`5556e8`; continuation`4038f6 → 403a9d` | `477c8f → 4bb5b0`, name`58ba28`; continuation`477c97 → 477e6d` |
| OBJE singleton save | `4a01b6..4a024d`, class array`+20`, count`+1c`, record stride`38`, member array`+c`, cell stride5 | `4bc314..4bc403`, same offsets/strides |
| Integer tag1 write | table`49f8f4 → 49f810`, payload`[ESI+1] → 4e94a0` | table`4bb8e0 → 4bb843`, payload`[EDI+1] → 5069f0` |
| Singleton member restore | `4a1227/4a1231/4a124d`: member array`+c`, reader`4a0640`, stride5 | `4bd61d/4bd622/4bd649`: member array`+c`, reader`4bc8c0`, stride5 |
| Dynamic menu/object restore | class`+8` at`4a1463`, members`+c` at`4a1511`, reader`4a159c`, stride5 at`4a15ac` | class`+8` at`4bd826`, members`+c` at`4bd8d0`, reader`4bd93e`, stride5 at`4bd94f` |
| Integer tag1 restore | table`4a0844 → 4a06ec`; setter`4a8600` writes payload`+1`, tag1`+0` at`4a8610/4a8613` | table`4bcac8 → 4bc96a`; setter`4c5590` writes at`4c55a1/4c55a6` |

Thus both editions serialize integer coordinate cells without scale conversion
and restore dynamic menu members instead of re-running their constructors.
The owning [saved-layout study](ui-saved-layout.md) correctly
identified persistence; its inference that this necessarily requires repair
was too broad for the now-binding unchanged-dimension scope.

Both Restart call sites are complete five-byte CALL instructions and use seven
caller-cleaned words: callback context, zero token, class, name, result mode1,
argc1, saved owner argument`+4e0`; cleanup is`add esp,1c`. AP's live outer owner
is ESI, zero register EBX and constant1 EBP; FL's outer owner is EBX and EBP is
its enclosing frame. These register contracts are not interchangeable.
The internal task runner also differs: AP`49f430` consumes task in ESI;
FL`4bb470` consumes task in ECX. No proposed observer may use a no-argument cast.

Native load already owns SEH registration/restoration (AP`404cce/405189`,
FL`479446/479946`). If an actual borrow requires a wrapper, preserve native
stack cleanup, return registers, flags, FP/XMM/MXCSR and LastError and release
only its own scope on unwind. Do not keep KC/member/task pointers across
callbacks or reentrant loads. This study adds no injection site; it does not
claim install atomicity, arbitrary indirect-entry exclusion or a new safe
unwind adapter from the five-byte boundaries.

Both dispatchers mask interpreter completion: AP`49f4ac → 4a26a0` then AL1 at
`49f4b2`, outer diagnostic scan`49f65d → 4a21a0` then AL1 at`49f662`;
FL counterparts are`4bb4f0 → 4bed90`, scan`4bb69c → 4be7d0`, AL1 at`4bb6a1`.
The diagnostic scan does not destroy the task. Accordingly, AL1 is not a
positive completion witness in either edition. The
[completion study](ui-refresh-completion.md) remains valid on
that ABI distinction. It does not demonstrate a need to add a mod-owned
completion transaction to unchanged-dimension save/load.

## 3. Selected KC sequence and fields

`150::Restart` is AP`13a7d`, FL`16af4`; each calls global `_Restart`, then
`X2_SetModified`. The only normalized method difference is a member-index
change29→31. `_Restart` is AP`b223`, FL`d1bb`. FL adds
`InitShipBoardingTable`, `ResetAbbreviations` and`700::LoadSequentialTextFiles`
earlier in the call sequence. The **42 instructions from SetClipping through
the end of the global function match**, including the operations below.

| Operation/owner | AP CODE | FL CODE | Established comparison |
| --- | --- | --- | --- |
| Restart SetClipping call | `b3a0` | `d353` | Same clip members8..11; dimensions queried anew, cursor0/1 retained. |
| InitLastProfile / SetMouseMapping calls | `b3aa/b3b4` | `d35d/d367` | Same ordering and mapping method. |
| Monitor UpdatePositions / UpdateVisibility calls | `b3be/b3c8` | `d371/d37b` | Same manager methods; existing positions update reaches monitor descendants. |
| Menu Reload call / method | `b3d2 / 21b3f` | `d385 / 28a52` | Same current-menu Resize and existing auxiliary abort/stop/cache/close operations. |
| ResetFollowSteering call | `b3dc` | `d38f` | Same method; no extra UpdateCursor, CenterCursor or MoveCursorAbsolute call in either suffix. |
| Cursor UpdateCursor | `e6e8e` | `17dec9` | Same clamp of0/1 against8..11, instance position, steering/listener behavior. It exists in both; no new saved-load call is introduced. |
| Base menu Resize | `f6348` | `18dc0d` | Same virtual max-height/width getters, stores80/81, chosen height71, SpecialResize and state-dependent Update. |
| Dormant CreatePrevious | `f5ea9` | `18d768` | Same reactivation via Init, without rewriting80/81. Create/CreateNext establish them in both. |
| Graph max width/height | `1646e8/164754` | `23b2d6/23b342` | Same dimension formulas. Graph2327 already exists in AP. |
| Graph SpecialResize | `16646c` | `23d821` | Same operations and BuildGraphData call; instance member82/83 moves to99/100 because base2200 grows82→99 members. Actual graph data-building content is outside this equivalence claim. |
| Short message632 Show / Update | `fa2ca/fa67f` | `195d1d/1960d2` | Same placement formula, ownership and absence of position rewrites in Update. IsShown/Hide also match. |

Two bounded FL differences deserve explicit treatment:

- Monitor606 `UpdateViewportPos` is AP`f15c8` (139 instructions) versus
  FL`188a7b` (151). FL adds two calls to`605::GetRightOffset` at
  `188b77/188b81` in the right-monitor placement formula. Getter`186a55`
  returns605 member47. The same manager UpdatePositions already calls this
  method; its field-write sequence and remaining call sequence match AP.
  This is existing FL placement support, not a missing load-refresh call.
- Timer633 is absent in the clean AP story and present in FL with21 members.
  `ShowTimer19634b` computes x/y in4/5 from screen dimensions and width/height6/7;
  y is `H - 250 - height/2`. `Update19663a` recomputes x, retains y and
  reapplies instance position at`19666a`; `RunTimer1967bc` drives its updates.
  This is a real new owner, but unchanged H and unchanged restored height
  preserve the same y. No static evidence here shows it moving incorrectly
  on ordinary save/load. Re-running ShowTimer would alter tasks/ownership and
  is not justified as a port requirement.

## 4. What changed dimensions would mean

For the supported comparison, W/H and scale are equal before save and after
load. A cursor previously inside the same clip rectangle remains inside it;
a cached menu maximum computed from the same W/H remains in the same units;
message y=`H-150-height/2` and timer y=`H-250-height/2` remain equal for restored
height. Native active-menu and monitor refreshes still run as described.
These are conditional mathematical invariants, not measured gameplay states or
a proof that arbitrary saved state is valid.

The earlier counterexamples deliberately changed dimensions (cursor clip
1920×1200→960×600, graph maximum1024→592, timer y1158→438). They demonstrate
possible inconsistency **if the coordinate system changes**, not an FL-specific
failure of unchanged-scale save/load. AP has the same retained-coordinate
behavior for the shared owners. Changing scale/resolution between launches,
loading a save produced without scaling, and adding a live layout-resize
feature are separate cases. No requirement to implement those migrations or
hardening is inferred here. Reopen a finite repair only for an agreed case and
concrete missing refresh/result, with the native lifetime protections required
by any actual added access.

## Verification and limits

Measured offline: **92 native instruction/operand anchors**, four integer-tag
dispatch-table targets, two Restart CALL boundaries, **25 shared KC methods**
(23 relocation-identical, two with explicit field remaps), one identical
42-instruction global UI suffix, the differing monitor method and FL timer
coverage. Three hash-bypassing mutations reject changed AP cell stride,
changed Restart stack cleanup and a clipping store redirected to cursor0.

```sh
python3 verification/probe/verify_fl_ap_ui_save_load.py --raw
python3 -m py_compile verification/probe/verify_fl_ap_ui_save_load.py
git diff --check -- docs/reverse-engineering/farnhams-legacy-ui-save-load-comparison.md verification/probe/verify_fl_ap_ui_save_load.py
```

Producer invocation, validated JSON summary and optional raw assets/listings
remain ignored under `verification/results/fl-port/ui-save-load-comparison/`.
The tracked probe produces its own `reproduce.sh`; no raw KC bytes or decompiler
output are tracked. No production changes, Wine/native runtime tests, game
launch, build, install or commit. No runtime performance claim. The finite
comparison does not audit every script callback or prove save compatibility
across arbitrary external mods/configuration changes.

## Independent factual review

2026-10-28: the main agent reran the verifier and inspected normalization of
symbolic calls, branch/switch destinations and the two explicit member remaps.
All other operands remain compared. The selected native anchors, Restart suffix
and retained-field comparison support the scoped conclusion; they do not claim
FL gameplay acceptance or correctness after changing configuration dimensions.
