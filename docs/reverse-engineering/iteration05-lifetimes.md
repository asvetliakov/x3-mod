# Iteration 0.5: observed object lifetimes and reload boundary

The completed user-run capture contains verified before/after storage lifetimes
for **all 12,753 scoped draws** out of 12,957 successful draws (98.43%). The other
204 draws are unscoped. The final burst observes the renderer-load epoch advance
from 1 to 2 and reused game handles receiving different pointers and observer
serials. There are no observed within-draw epoch, revision or serial changes.
This establishes useful live storage-lifetime coverage in this run; it does not
establish motion correspondence, a camera-cut detector or rendered TAA.

## Immutable evidence and reproduction

This report supersedes the preliminary running-snapshot analysis. The user
finished the requested sequence and exited the game before the final snapshot:

- Snapshot: `/tmp/x3-iteration05-completed-snapshot.log`.
- Size: **216,605,445 bytes**.
- SHA-256: `e5beaa861d04659fe9c7df05a01845bd05d656a33c643f4b484ff379cf3ccaf8`.
- 28 complete captured frames, all on device lifetime ID 1, in seven four-frame
  bursts; all captured draw results and Presents succeeded.
- [Compact derived report](../../verification/results/iteration05-lifetimes.json).
  Raw trace and large generic per-draw summaries remain local/untracked.

```sh
python3 tools/analysis/analyze_iteration05_lifetimes.py \
  /tmp/x3-iteration05-completed-snapshot.log \
  --expected-sha256 e5beaa861d04659fe9c7df05a01845bd05d656a33c643f4b484ff379cf3ccaf8 \
  --session-complete --output verification/results/iteration05-lifetimes.json
python3 -m unittest discover -s verification/analysis -p test_iteration05_lifetimes.py
```

The analyzer reads the fixed file once, hashes its bytes, requires the expected
hash and stable size/mtime, and retains only relevant metadata. Explicit limits
bound it to 100,000 draws and 1,024 frames. It keys diagnostics by device, frame
and draw index, rejects duplicate lifetime records for known evidence, excludes
failed draws and incomplete frames from lifetime aggregates, and never fills
missing fields with apparently stable zero epochs. Thirteen original fixture tests
cover these gates, duplicate frame-end poisoning, strict bounded pointer/handle
parsing, coordinate mismatch, load/serial changes, malformed raw matrix rows and
limits. The report records analyzer and local producer-source hashes
for interpretation; those source hashes do not independently attest the DLL
which produced the user-run trace.

The user described stationary first-person, turning/steering first-person,
third-person while approximately stationary, third-person moving/steering, and a
different planet save followed by capture. Seven bursts are present rather than
five labels. Their exact correspondence is not forced from that description.

## Activation, scope and per-draw evidence

The startup record is `active=1 status=active_without_baseline`, with
`baseline_complete=0 baseline_entries=0 recovery_required=0`. That record does
not identify which initial-baseline precondition was unavailable. Nevertheless,
by the first captured frame, every scoped draw has known node and camera serials
from observed registry insertions. The earlier concern that a missing initial
baseline could leave every existing camera permanently unknown did not occur in
this run. A successful populated installation baseline is not demonstrated here.

| Captured frame range | Successful draws | Known consistent lifetimes | Unscoped draws |
| --- | ---: | ---: | ---: |
| 120–123 | 2,564 | 2,548 | 16 |
| 1794–1797 | 3,200 | 3,168 | 32 |
| 1975–1978 | 1,617 | 1,585 | 32 |
| 2435–2438 | 1,672 | 1,640 | 32 |
| 2806–2809 | 1,072 | 1,040 | 32 |
| 3047–3050 | 960 | 928 | 32 |
| 4096–4099 | 1,872 | 1,844 | 28 |

Every draw has both `motion_input` and `motion_lifetime` diagnostics, and every
scoped context has valid mask 127. `lifetime_verified=1` occurs on the same 12,753
scoped draws. There are no missing/duplicate lifetime records and no mismatches
between the before/after known flags, observer/load/registry epochs, mutation
revision, node serial or camera serial.

All 204 unscoped draws contain reason 8 (`LookupUnavailable`) and unknown
lifetime flags. In this producer, `read_draw_input` initializes that reason when
the observer is active and leaves it unchanged when no usable object scope
exists; it does **not** call registry lookup in that path. Thus these rows are
unscoped placeholders, not 204 failed registry lookups or missed object births.
Their zero epoch/serial values are unavailable fields, not a stable lifetime.
The report lists their seven shader-pair paths without assigning object matches.

## Serial and load-epoch consistency

Across known records, the observer epoch remains 1 and registry epoch remains 2.
The registry pointer is `021fb3c0`. The load epoch is 1 through frame 3050 and 2
in frames 4096–4099. This renderer-load boundary requires invalidating prior temporal history even
though the registry allocation/generation remained the same; the live history
rendering pipeline was not active.

Known draws expose **943 distinct node serials and 22 distinct camera serials**.
No observer serial maps to more than one observed registry/pointer/handle tuple.
No tuple within the same observer/load/registry epoch maps to multiple serials.
Shared storage identities retain their serials across each of the 21 adjacent
captured-frame pairs. This is a storage-identity consistency check; it does not
pair submitted draw transforms or establish geometry correspondence. Gapped
bursts are explicitly marked nonadjacent and cannot supply immediate prior-frame
history merely because some objects remain alive.

**Seventeen observed handles reappear across the load epochs** under the same
registry pointer, with changed pointers and serials. For example:

| Role / handle | Load epoch 1 | Load epoch 2 |
| --- | --- | --- |
| Camera / 24613 | Pointer `20c876d8`, serial 24612 | Pointer `0f74b640`, serial 33678 |
| Node / 24614 | Pointer `0efdfc40`, serial 24613 | Pointer `3ec927c0`, serial 33675 |
| Node / 24615 | Pointer `0efdfec0`, serial 24614 | Pointer `3ec92a40`, serial 33676 |

This is direct live evidence that a game handle must not stand in for a storage
lifetime. The observer and load epoch distinguish the new allocations. These
samples do not demonstrate same-pointer-and-handle reuse; the synthetic fixture
covers that mechanism separately.

## Mutation revision and camera storage versus camera motion

Known mutation revisions are constant within each captured frame except
**frame 2438**. Known draws 1–387 use revision **59702**; known draws 388–415 use
**59706**. No individual draw crosses that change. A statement that the entire
frame had no registry mutations would therefore be false. This alone does not
require every temporal consumer to discard the frame: birth/removal of other
objects can coexist with stable serials for unaffected objects. Per-object
serials, load/registry epochs, submitted transforms, geometry revisions and
camera policy must determine eligibility. The log does not identify which four
revision increments occurred between those draws.

The most frequently submitted camera in each group is:

| Captured frames | Pointer / handle / serial | Raw view observation |
| --- | --- | --- |
| 120–123 | `0f14a9c8` / 21925 / 21924 | Unchanged view bits across the burst |
| 1794–1797 | `6f02f300` / 29922 / 29921 | Unchanged view bits across the burst |
| 1975–1978 | Same `6f02f300` / 29922 / 29921 | Different view bits in all four frames |
| 2435–2438 | Same `6f02f300` / 29922 / 29921 | Unchanged view bits across the burst |
| 2806–2809 | Same `6f02f300` / 29922 / 29921 | Different view bits in all four frames |
| 3047–3050 | Same `6f02f300` / 29922 / 29921 | Different view bits in all four frames |
| 4096–4099 | `378dabf0` / 28878 / 35766 | Different view bits in all four frames; load epoch 2 |

“Most frequently submitted” is a count-based label, not a claim that all passes
use one main camera. Multiple other cameras are present. The analyzer hashes
complete four-row raw view/projection bit records; it performs no approximate
matrix decomposition. The dominant camera's projection bits remain unchanged
from frame 1794 through 4099, while its view changes as shown. Frame 120's
projection differs from those later bursts.

The persistent camera pointer/handle/serial across frames 1794–3050 is compatible
with both stationary and moving views and with the user's camera-mode sequence.
Storage continuity cannot identify exactly when the user switched modes, prove
that a cut did not happen in a gap, or distinguish smooth motion from a teleport.
A separate conservative camera-cut/history policy remains necessary. These live
facts validate useful lifetime metadata; they do not remove geometry, finite
vertex data, reactive-material or rendering acceptance gates owned by the other
iteration analyses.
