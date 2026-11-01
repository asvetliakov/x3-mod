# FL gun instance selection and conservative writer membership

2026-10-30. Focused static follow-up to
[loaded ship sources](ship-source.md). The source note and its
producer were left untouched while independently reviewed. This note resolves
the actual selector and identifies a bounded alternative that avoids treating
the current ship selection as future writer authority. It does not implement
a live adapter or ratify architecture.

The [producer](../../../verification/results/fl-ship-turret-selection/verify.py)
and [receipt](../../../verification/results/fl-ship-turret-selection/evidence.json)
read the original FL image with project PE/Capstone tools and reuse the
effective resource reader for two stock symbolic source comparisons:

```sh
python3 verification/results/fl-ship-turret-selection/verify.py
python3 -m py_compile verification/results/fl-ship-turret-selection/verify.py
```

Raw Ghidra output stays in
`~/.cache/x3m-port/fl-ship-turret-selection/`, from the existing read-only
`FLRegistry/X3FL.exe` project and project scripts. No PE/native execution,
native build, Wine, game, install or production change occurred.

## 1. Exact selector: a node lookup, not an allocated gun object

`44c010..44c140` takes **ECX=ship**, stack `(group,slot)` and returns a node
in EAX, callee cleaning eight bytes. It requires class word `ship+48==7`.
It sign-extends subtype `ship+4a`, then calculates:

`type = [5a83e4] + subtype*0xdb8`

`entry_base = type + 0x18*(13*group + slot)`.

| Input field | Meaning in this function |
| --- | --- |
| `ship+70` | Physical root node |
| `ship+98` | Zero selects immediate root-return path; no deeper semantic name is established |
| `entry_base+1a4/+1a8` | Primary requested model ID / source **array index** |
| `entry_base+1ac/+1b0` | Secondary requested model ID / source array index |
| Node `+144/+25c` | Values compared to those two keys |
| Node `+c`, then `+0` | Direct-child list, terminating at the native null-next sentinel |

The complete native behavior is:

1. Wrong class returns null. `ship+98==0` returns root without consulting its
   key. A primary key equal to root returns root immediately, bypassing any
   secondary lookup.
2. Otherwise search **only root's direct children**, in native list order,
   for the first exact `(model,index)` primary match. No match returns null.
   It does not recursively look through grandchildren.
3. Keep that primary node as the fallback. Only when secondary model is
   positive, secondary index is nonnegative, and **some** 0x60-byte gun-dummy
   row has `row+0 == primary.model`, search the primary's direct children for
   the first exact secondary `(model,index)` match. Return it if found;
   otherwise return the primary fallback. The existence search does not
   require GUNF_ROTATE or a matching row's scene/slot policy.

Neither search compares node source **scene ID** `+258`. The source index is
part of the key, not the authored P number. A reimplementation that searches
recursively, ignores the part index, or chooses a later duplicate is not this
selector. Under a borrowed copy, validate the actual native sentinel, parent
links, bounds and cycles; do not copy native unchecked traversal behavior.

The selector has **no CALL instructions**, allocations, callbacks, FS/SEH,
XMM/x87 work or MXCSR changes. EBX/EBP/ESI/EDI are preserved on the paths that
use them; EAX/ECX/EDX and flags are scratch. It reads fields and returns an
existing node, not a separate gun-instance allocation. No call from the
injected helper is proposed. Native subtype/group/slot indexing is unchecked
here; a reader would need count and checked-span validation before indexing.
The class-7 type-count guard at `446000..44601f` uses `[5a834c]`; it must not
be inferred from a plausible type pointer alone.

## 2. How the gun updater uses that node

`450800` reads group count `type+180` and visits groups **1..count-1**
(`45084e`, `451403..451416`), not group zero. For each group it reads gun
count `type+198+group*0x138`; slot stride is 0x18
(`450edb`, `4513d7`, `4513ee`). There is no loop cap of thirteen inside the
selector/updater: thirteen is the addressing multiplier between groups, so
copying counts requires explicit format/span/budget checks.

At `450ef4/450ef5/450ef6`, it calls `44c010(ship,group,slot)`. With
`slot_ptr=type+1ac+group*0x138+slot*0x18`, it additionally requires:

* returned node S is nonzero;
* `slot_ptr[0]`, the secondary model ID, is positive;
* `P=S.parent` (`450f13`) and `P.parent` (`450f1e`) are nonzero;
* model resolver `4a1a40(secondary_model)` succeeds at `450f28`.

The global row selection key is **`slot_ptr[-8]`**, the primary model ID
at `+1a4` (`450f3c`). The following loop examines all matching gun-dummy rows,
not just the selector's first existence hit. Eligible row bit 2 selects the
row's source scene (`450f66..450f7e`). It scans every source part in reverse,
then all three `(model,flags)` slots. For an active slot it walks **P's direct
children** and updates the **first child with matching model ID**
(`4510a7/4510ad`), independently of that child's source scene/index.

This establishes both the actual instance relation and why a shortcut such as
`affected_parent.model == dummy_row.model` is not generally sound. If the
secondary lookup falls back to the primary, the updater's target parent is
the primary's parent. The native ancestor guards still apply, but they do not
prove that target parent's model equals the row key. Source/model equality
cannot substitute for reconstructing this fallback path.

The visibility writer `44c150` reuses the selector and only sets/clears node
`+12c` bit `0x100000`; it does not change the type keys, raw scale, topology or
basis. The inspected selector and updater read the type relation; this study
does **not** establish every type-table loader/reload or script writer, or
declare the currently selected instance immutable for the whole interval.

## 3. Bounded conservative superset without a selected-instance dependency

A sufficient writer-membership superset can avoid type/group/slot selection:
copy the complete capped gun-dummy table at `[5a8488]`, count `[5a83c0]`,
stride 0x60, under the same qualified borrow/resource lifetime.

For every distinct actual model-bearing node ID, consider **every rotating
row and all three slots** whose model ID equals it and whose low three axis
flags are active. This is a superset because every observed attitude write
first performs exactly that model-ID match. Do not restrict it by current
parent model, current selected gun, first matching row, or source scene.
Treating later duplicate rows or currently unselected nodes conservatively
can enlarge eligibility/refusal but cannot omit an actual writer.

For each affected model, require the existing exact authored identity
quaternion, ordinary scale/axis/source policy and no-gamma eligibility for
**every** applicable row/slot; unknown bits or any gamma writer refuse that
path. It is unsafe to choose the favorable no-gamma row and ignore an
additional gamma row. All-table membership is only the rotor-writer part of
admission; it does not supersede the source key, scale, construction or
topology contracts.

**Shared source-scene mutation is a separate global check.** Scene cache
records are shared. Another ship selecting a row can execute
`450ff2..45104e` and enable the scene transform even if this ship selects no
gun or has no locally matching slot. For every source scene used by this
ship, inspect every rotating row whose scene ID equals it, and prove that
the complete source part set contains **no part model equal to that row's
dummy ID**. Otherwise the ordinary untransformed profile must refuse, or
the parent must separately qualify the actual transform. Checking only this
ship's selected rows or only its matching slot models would miss this writer.

These are comparisons of qualified runtime integer IDs and copied source
records. The offline names below only corroborate membership on current stock
inputs; mesh names alone cannot supply the live join. The superset does not
close other scene-transform writers such as construction centring, resource
reload or arbitrary scripts. Those remain within the separate source/writer
scope decision; no observer is proposed as a substitute.

## 4. Stock corroboration and cost

The effective stock Dummies input contains **26 gun rows**, **12 rotating
rows**, of which **five contain gamma**. The rotating slots reference twelve
distinct no-gamma models and ten distinct gamma models, with **zero overlap**
between those sets. Thus considering all rows does not introduce a gamma
conflict into the stock no-gamma model set merely because the current selector
was omitted. This is measured symbolic source membership, not a live ID-table
capture or proof about future resource replacements.

| Source comparison | Source scenes / parts | Distinct model references including root | All-table rotor candidates | Gamma / future scene-transform matches |
| --- | ---: | ---: | ---: | ---: |
| Argon M4 `SS_SH_A_M4` | 1 / 25 | 6 | 0 | 0 / 0 |
| Argon M2 `SS_SH_A_M2`, gun substitutions included | 3 / 41 | 10 | 3 distinct models | 0 / 0 |

The M2 candidates are body alpha, left weapon alpha|beta and right weapon
alpha|beta. Both substituted scenes are checked independently and contain
no part model equal to their selecting dummy ID. The M4 has no rotating
candidate or shared rotating scene in this comparison. These two ordinary
source layouts therefore survive the proposed conservative **rotation**
membership check; the strategy is not tied to an M2 roster entry. The
comparison expands gun substitutions only and does not qualify other dummy
kinds, scale modes, complete live construction, a full ship sphere or flight.

Host selector witnesses cover root shortcuts, exact primary/secondary keys,
fallbacks, missing dummy rows, nonrecursive search and the parent-model-filter
counterexample; a separate witness records the foreign-ship shared-scene case.
They support the dataflow explanation, not authority to retain native pointers.

For a proposed 512-row refusal cap, copying the gun table is at most 49152
bytes and considering three slots is at most 1536 records. With 256 nodes a
simple capped product remains finite; deduplicate actual model/source IDs in
existing scratch or build a temporary fixed-size membership array when useful.
Current stock has only 26 rows. This is ordinary read-time or cold-generation
work, not per-draw work; it needs no model load, vertex scan, native callback,
new registry, observer or lock. No game performance was measured.

Remaining implementation evidence is the actual ID-table snapshot and lifetime
join, full table/count/span and duplicate-row refusal tests, all affected model
quaternion/scale checks, complete source-scene membership including foreign
writers, and the already open source construction/scale/topology qualification.
If the parent chooses exact selection instead of this superset, the type-field
loader/reload and selected-relation stability also need explicit qualification.
The existing complete CPU/LastError/MXCSR and abnormal-exit/refusal envelope
remains required at actual arrival/E borrows; this study creates no new hook.

Independent source/evidence review accepted this bounded contract and reproduced
the static receipt. The stated runtime integration and completeness gaps remain
open; review grants no native execution or flight acceptance.
