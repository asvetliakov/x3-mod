# FL effective gate solids and offline clearance

2026-10-29. This bounded checkpoint implements the offline input and pure
geometry portion of the [accepted traffic design](../../architecture/gate-traffic-controller.md).
It does not change or admit runtime traffic, lane, docking, launch placement or
warning suppression. The [arrival receipt](gate-traffic.md) still
owns the ABI and native state publication. No executable compilation, Wine/native
execution, game launch, installation or game-file write occurred.

## Reproduce and scope

```sh
python3 verification/results/fl-gate-traffic/verify_clearance.py
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_fl_gate_clearance verification.analysis.test_fl_resources
python3 -m py_compile tools/analysis/fl_gate_clearance.py verification/analysis/test_fl_gate_clearance.py verification/results/fl-gate-traffic/verify_clearance.py verification/results/fl-gate-traffic/clearance_facts.py
```

The [producer](../../../tools/analysis/fl_gate_clearance.py),
[stock derivation](../../../verification/results/fl-gate-traffic/clearance.json),
[verification producer](../../../verification/results/fl-gate-traffic/verify_clearance.py),
[frozen operand anchors](../../../verification/results/fl-gate-traffic/clearance_facts.py)
and [verification result](../../../verification/results/fl-gate-traffic/clearance-verification.json)
contain only derived names, hashes, values and findings. The stock input is
`/mnt/c/X3FL-orig`, selected mods `[]`, with the clean FL executable already
identified in the arrival receipt. Hashes identify evidence; no hash whitelist
admits a gate. `--game` and ordered repeated `--mod` arguments select another
effective resource snapshot. CLI writes derived JSON only.

`Resources` is reused directly, including its qualified loose-first stem,
package-before-format, namespace-preserving and failure rules. In particular,
this tool never uses the LOD generator's original-resource view: generated
collision LODs that actually win must be measured. An explicit original view
refuses. Effective TGates, Bodies, BodyData, BodyDummies and Dummies requests, every scene
and every body winner carry packed and decoded hashes; missing body requests
are retained too. `snapshot_current` re-resolves through a newly constructed
resolver and checks selected bytes, so a same-name override, new loose file,
newly appearing missing input or changed selection invalidates old metadata.
This is an offline snapshot check, not a runtime handoff/refresh protocol.

## Native selection and coordinate contract

The verifier re-decodes **115 frozen FL operand anchors** (measured), independently
of payload hashes. Important consequences, all preferred VAs:

- Scene builder `0x446e3c..446e90` excludes 2D/2D2, JET and dock nodes, otherwise
  requests the last LOD for gate owners (`0x446e81/87/89`). Body setter
  `0x4a36e9/f2` first requests a tree for flagged `LOD & 0x10`. The first built
  tree persists; later calls reuse model `+0x5c`, `0x497ba5/bb0`. This cache
  belongs to the shared model, not an individual gate/root/scene instance.
- `0x4876d0` returns BodyData collision metadata, record `+4`. Tree builder
  `0x497b72` refuses -1; a negative value uses the caller's LOD, a nonnegative
  value uses its low byte, bounded to the last record. Bit `0x100`, or LOD flag
  `0x10`, includes all parts/groups. For negative metadata the producer encloses
  every possible caller-requested LOD; earlier users outside gate construction
  can have built the shared tree. Nonnegative metadata forces the same bounded
  low-byte LOD for every caller, so it remains a single candidate.
  Native filtering uses **loaded** part flags `0xa0` and group flag 1
  (`0x497c01`, `0x497cb3..cbb`, `0x497ce5..ced`). The loader stores authored
  flags at `0x49bb75`, sets bit `0x20` at `0x49bba2`, then clears it for a
  colliding material group at `0x49bd47`. Therefore authored bit `0x20` alone
  can never exclude an opaque group. After group filtering, the producer only
  needs authored bit `0x80` as an independent part exclusion; force-all mode
  bypasses both masks. Numeric
  names use the established fixed-ID/BodyData mapping, including named fixed
  aliases. Unsupported numeric IDs and ambiguous aliases refuse.
- Loader `0x49bd34/3b` sets the group exclusion from material flags `0x486`.
  For an effect record, `0x49ad99..49adee` adds 2 for enabled LONG
  `g_AlphaBlendEnable` without enabled LONG `g_ALPHATESTENABLE`, or 1 with
  alpha test; authored flag bits remain in force. Thus the hsap scene is
  meshless by native collision selection, independently of its appearance.
- Binary coordinate reads at `0x49b832/83f/84d` arithmetic-shift positions
  right by two and store signed words. Tree emission at `0x497d0c..7e25` uses
  these words times the measured double at `0x591a28`, exactly `1/16384`.
  Uniform node scale is the LOD-0 value (`0x4a3668/66b`); subsequent LOD values
  are thresholds. Axis scales initialize to 65536. Ordinary scene nodes are
  zero-initialized (`0x4a2278..2280`) and linked at `0x4a5809`; the reviewed
  ordinary scene builder does not set node flag `0x40`. Their supported local
  offsets therefore retain engine-unit scale. The existing child-transform
  receipt owns the conditional parent-uniform/axis scaling rules.

The supported reconstructed vertex is exactly
`signed16(file_position >> 2) * LOD0_value / 16384 + static_scene_offset`.
The tool reuses `bob1` and `scene_radius.parts`; it validates complete scene
headers and keys before using parsed positions. All stock gate parts have
identity attitude and no F parent. Identical repeated position/attitude keys
in catapult and hsap are constant geometry. Modified rotation, extra scale/key
channels, moving keys, F nesting, binary CUT1 and unqualified dummy/body forms
produce explicit unknown geometry. They are not treated as identity transforms.
Native dynamic transforms, rounding during live world transforms and arbitrary
mod animation are not certified by these offline values.

Two narrow gate-local parser extensions preserve existing global parsers:
`GateTextReader` retains empty effect parameter string values; FL's STRING
case reads through `0x507720` at `0x48a38c`, then calls `0x488f40` at
`0x48a3a8` without a nonempty-value test. The text string path copies the
possibly empty token (`0x5077ce..50781a`). This is needed by hsap's empty
`t_AlphaTexture`; it does not change collision LONG parsing. MATERIAL3 record
fields are consumed to reuse `bob1`'s geometry reader, while every group is
conservatively included. Classic/global material eligibility is deliberately
not reconstructed; such geometry can only be enlarged, never dropped. The
Materials table consequently cannot narrow these bounds. BodyDummies and
qualified Bodies exclusions remain explicit effective inputs.

The effective `addon2/types/Dummies` table is also required: FL loads the typed
Dummies input at `0x43eea3/eebf`; the ordinary scene builder searches the five
dummy tables at `0x4469cd -> 0x445f80` and may load a child scene at
`0x446a02`. Every lexed Dummies token is conservatively considered a possible
substituted body identity, including fixed aliases. A referenced scene body
matching any token, or the DOCKPOINT list, refuses because its children are not
measured here. Scanning all tokens can over-refuse numeric mod references
(e.g. a state count equal to a body ID); it cannot silently omit a matching
child scene. All stock gate body names are absent. No CutData lookup is needed
for those proved nonmatches. This boundary is checked before classifying any
modified referenced dummy as meshless.

## Bounds and stock results

Each gate has at most two solids: a root envelope and one envelope enclosing
all its potentially cached scene-tree triangles. Each envelope is a closed coaxial annulus with axial
bounds. The inner radius is the minimum axis distance to projected triangles,
not the minimum vertex radius. Exact integer/rational arithmetic handles
projected triangle interiors and degenerate edges; inner radius rounds down,
outer radius up, and axial minimum/maximum outwards. Values below are measured
in engine units, 500 units/metre; they replace the rounded historical fractions
for this snapshot.

| TGates rows | Scene | Clear radius | Outer radius | z minimum | z maximum | Candidate triangles |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 0–3 | Argon | 411590 | 678580 | -441636 | 445867 | 42859 |
| 4 | Destroyed Argon | 17170 | 1265814 | -513189 | 306908 | 20478 |
| 5–8 | Catapult | 454759 | 618642 | -3539 | 3590550 | 57775 |
| 9–20 | Terraformer active/inactive | 412052 | 679474 | -443789 | 213206 | 37782 |
| 21–24 | Terran | 467046 | 731699 | -353800 | 128875 | 120174 |
| 25 | hsap | meshless | — | — | — | 0 |

The table encloses a union of possible cached trees; the candidate triangles
do not all coexist in one native tree. All 26 scene assignments and seven
unique scenes are derived. The hsap body
has scale 468919, one LOD and two material records; the selected LOD uses
material 1, which is excluded by the native blend/alpha-test rule. Both records
have that exclusion, but unused material 0 is not evidence of a collision group.
Row 25 still has the root obstacle.

TGates root body 0 contributes **60 conservatively included triangles**, clear
radius 0, outer radius **58**, z **-47..47**. Tree presence alone is not a
claim that every native mask/order can hit it; including it regardless is safe
for these offline checks. Root body 477 on row 4 is missing and remains unknown.
It is not silently called meshless or excluded from TGates. Thus **25 rows
have usable complete bounds; one has unknown root geometry**.

The historical projected-distance routine in `gate_hole.py` classifies every
collinear projected triangle as inside because all edge signs can be zero.
The new fixture `(5,-2),(5,2),(5,0)` has exact axis distance 5 while that old
predicate returns zero. The verifier counts **two** triangles with the same false-axis condition on
the actual selected destroyed-gate mesh (measured). This, plus native point
quantization, explains why its newly measured clear radius differs from the
old zero; the tiny aperture is not a qualification of destroyed-gate traffic.

An independent NumPy triangle oracle reconstructs loaded part flags and
enumerates all possible earlier caller-requested cached trees, without trusting
the producer's candidate LOD list or part mask. It verifies **316,910 possible-cache
triangle checks**, including the conservatively retained root, and separately checks
an axis-parallel example sweep against actual triangles on each of the 25
usable rows: radius 100000 (200 m), lateral 200000 (400 m), from the
combined envelope maximum z plus 100002 to its minimum z minus 100002. This
traverses every possible cached tree's full axial extent. The native warp span
z=1348650 to 0 is checked additionally. These are synthetic hull spheres on measured stock gate resources, not a
claim that every stock capital hull fits or that opposing traffic is separated.
These checks use the expanded catapult interval **-3539..3590550**, not its
previous coarsest-LOD interval. Central sweeps refuse because of the root. The pure checker also covers exterior
and axial separation, strict threshold/touch refusal, unknown widths/solids,
lateral paths across the root, and blocked cases. There is no warning bypass.

## Cost and remaining boundary

Measured verifier elapsed time is about three seconds on this host (exact run in
its result); it includes resource indexing, derivation, anchor checks and oracle.
The producer reads/decompresses 20 winners, parses eight bodies, retains one
missing-body attempt, derives seven scenes and deduplicates repeated root/body
work. No generic asset registry, per-frame work, allocation or lock is added to
runtime code. Diagnostic time is not game FPS or an FL loading measurement.

The focused suite has **52 passing tests** (15 new gate tests plus 37 existing
resolver tests). Host Python/file APIs are portable in source; Windows execution
is unmeasured. Native hook/CPU/LastError/rollback fixtures are inapplicable here
because there is no hook edit. Runtime producer layout, effective-input refresh,
launch attitude, collision-order exemptions, hull envelopes, live sweeps and
useful separated placement remain parent decisions before activation.


## Review correction: loaded flags and shared tree cache

The original frozen patch SHA-256
`32929a115d90cfe0380dac33363b7af6e697d5edebff084110791557e9123550`
was rejected for two unsafe assumptions: authored bit `0x20` was used as a part
exclusion, and root/scene LOD choices were treated as independent even though
model `+0x5c` caches the first tree. Its 33,793-triangle oracle repeated those
assumptions. That checkpoint is superseded by this possible-cache union.

The compact verification result retains executable witnesses from the original
producer **and original oracle**, loaded from the frozen source snapshot:

```sh
python3 verification/results/fl-gate-traffic/verify_clearance.py --prior-root /home/asvetl/.cache/x3m-port/fl-gate-clearance-review2-baseline
```

The prior source hashes are recorded with the producing command in the receipt.
Without `--prior-root`, the current oracle and negative fixtures still run;
executing the original-source comparison additionally needs that local snapshot
(or the same files recovered from the named original frozen patch).

1. An opaque synthetic triangle authored with part flags `0x20` loads with that
   bit cleared. The original producer called it meshless and certified a sphere
   sweep through it; its oracle counted zero triangles. The independent loaded
   flag oracle counts one and finds zero separation; the correction refuses.
2. One synthetic model is shared by a root and a scene instance translated by
   1000 units. LOD 0 has a triangle at x=100, LOD 1 at x=2000. With LOD 0 cached
   first, a sweep at x=1100 hits the scene; with LOD 1 first, x=2000 hits the
   root. The original producer certified both sweeps and its per-instance oracle
   reported 1000 units separation. The independent possible-cache oracle finds
   zero separation for both, and the correction refuses both. A focused third-
   LOD case also covers an earlier requester outside these two node roles.

Explicit nonnegative BodyData LOD selection and bit `0x100` force-all behavior
remain covered separately. The corrected stock derivation still has 25 feasible
example sweeps and one unknown root, with expanded envelopes and offline cost;
none of these corrections activate runtime traffic.

Independent correction review accepted this bounded offline checkpoint: 52 tests
passed and the reviewer reproduced 115 anchors, 316,910 possible-cache triangle
checks and 25 whole-axial sweeps. Regenerated evidence matched the frozen results
excluding timings. Runtime placement and actual FL gameplay remain unqualified.
