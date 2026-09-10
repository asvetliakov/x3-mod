# Finite-upload capture audit

[`analyze_finite_upload_capture.py`](../../tools/analysis/analyze_finite_upload_capture.py)
audits the capture's draw-input and finite-upload metadata without reading vertex,
index or shader payloads. It reports **input candidates**, not draws eligible for
TAA or evidence that temporal rendering ran. The runtime evidence contract and
its unsupported writes are documented in [finite-upload observer](finite-upload-observer.md).

## Reproduction and provenance

Use a completed capture or an immutable snapshot, with its expected SHA-256 when
available:

```sh
python3 tools/analysis/analyze_finite_upload_capture.py /tmp/capture-snapshot.log \
  --expected-sha256 EXPECTED_SHA256 --output /tmp/finite-upload-capture.json
python3 -m unittest discover -s verification/analysis -p test_finite_upload_capture.py
```

The report records the input path, byte count, SHA-256 and analyzer source hash.
The analyzer checks input device/inode, size and modification time before and
after streaming, plus its own source hash before and after analysis. An expected
hash mismatch or an unterminated final record fails the invocation. These checks
help detect a changing file; they do not make a live writer's file an atomic
snapshot. The CLI rejects output aliases, including existing hardlinks, and
invalidates an old output report before analysis. Consumers must require
`analysis_complete: true`; a failed new analysis leaves `false`. Alias rejection
preserves the raw input.

One input is one session; multiple session headers are rejected rather than
combining reused device IDs. Processing has explicit bounds of 2,048 frames,
200,000 draw slots and 1,000,000 recognized metadata records. Large raw shader
and constant records are streamed past, not retained or interpreted.

## Frame and draw proof

A frame contributes draw aggregates only with one matching begin/end pair,
contiguous draw indices, the declared draw count, `capture=1`, successful Present,
and one successful actual `draw_result` for every draw. Coordinates include the
device lifetime ID as well as frame and draw index. All draw records must lie
inside that frame; diagnostics must lie inside their matching draw/result
interval. A successful pre-draw capture event cannot replace an actual draw
result. Missing, duplicate, failed and out-of-scope frame/draw boundaries exclude
the frame. An unattributable recognized draw record conservatively excludes all
frame aggregates in that input.

Within a complete frame, duplicate known diagnostic records or scalar keys poison
the affected draw rather than selecting a last value. Ordinary indexed and
nonindexed calls require unique, typed arguments between draw and motion records.
Candidates require supported triangle-list/strip topology and a positive primitive
count. Typed scalar widths are checked; missing older-schema finite fields stay
unknown. Unsupported user-memory methods cannot become candidates.

The report keeps independent counts for:

- **Source qualification:** the recorded positive certificate must identify the
  actual draw's vertex shader hash and a nonzero program word count. A finite
  buffer alone does not qualify a position shader.
- **Position evidence:** requested, successful finite-view evidence must have
  a nonzero owner generation and the matching observed VB revision. State 1 with
  reason 0 is Finite; state 2 with reason 17 is NonFinite; absent or inconsistent
  evidence is Unknown. A nonfinite query can itself return S_OK.
- **Actual index bounds:** indexed calls require requested, known, successful
  evidence matching the observed IB revision and finite-view generation. The
  observed minimum/maximum must fit the declared vertex interval and signed base.
  Conservative whole-allocation extrema are accepted when they fit and are
  counted separately from exact subdraw extrema. Declared draw bounds alone are
  insufficient. Nonindexed calls report the explicit not-required range gate.
- **Legacy local gates:** blocker masks and proof masks remain separate from
  finite evidence. Candidate formation additionally requires zero blockers,
  all five recorded local proofs, positive submitted VS/PS hashes, scoped
  node/camera/registry context, and matching known before/after lifetime epochs,
  serials and mutation revision. Older captures can retain these local facts
  while their finite evidence remains unknown.

An input candidate combines all these gates, `vertex_finite_verified=1`, and
actual draw success. It is still only a recorded input observation. The analyzer
does not independently inspect shader bytes, buffer cells, native backend
qualification, matrix payloads or rendered images. It does not prove subsequent
buffer stability, previous-frame correspondence, camera-cut policy, successful
replay or TAA output. Normal registry changes between separate draws are not a
blanket frame rejection; the lifetime check compares each draw's before/after
observation.

## Cumulative owner metrics

`finite_upload_metric` is a sampled cumulative owner record. The analyzer retains
the latest sample per device owner and the latest observed sample for each owner
generation. It never adds repeated samples. Owner counters survive generation
changes, so a generation-labelled sample includes earlier generations' work;
it is not a generation-local increment. Counter/generation regressions are
reported as anomalies. A latest malformed/unavailable sample stays unknown,
rather than falling back to a previous positive value. Generation zero is an
unavailable owner observation, not proof of a stable zero-cost owner.

Reason counters and the optional first-refusal descriptor attach only to the
immediately preceding matching device/frame/phase batch. Repeated reason IDs,
first-refusal records or mismatched scopes poison that batch. The retained
first-refusal configuration can repeat across many batches; repetitions are not
new refusals. Omitted reason rows are not synthesized as zero or assumed to form
an exhaustive list. Reason zero can describe successful work, and reason counts
are not failed-draw counts.

Payload/metadata/sidecar gauges describe reservations at the sample, not bytes
uploaded or total allocation overhead. Global gauges repeat across device owners
and must not be added. Raw QPC counters use a reported frequency only when that
frequency is unambiguous. Missing optional timers remain unknown:

| Counter | Recorded scope |
| --- | --- |
| `scan_ticks` | Integer classification of mapped bytes |
| `qualifier_ticks` | Native backend and mapping/window validation |
| `query_ticks` | Complete public evidence queries, including qualifier work |

Query and qualifier scopes overlap. Their sum is not total observer overhead;
zero scan time alone does not establish zero upload-observer cost. This analyzer
makes no live-game admission-rate or loading-cost claim without an actual
completed capture.

## Verification scope

Thirty focused tests use small original traces. They cover successful indexed
and primitive candidates, conservative/exact IB bounds, finite/nonfinite/unknown
states, independent source qualification, missing legacy fields, lifetime and
proof rejection, failed draws/Present, duplicate/scoped record poisoning,
malformed arguments/topology/scalar widths, device-ID isolation, cumulative and
per-generation metrics, optional/overlapping timers, first-refusal batching,
latest-sample failure, input/source changes, missing-input stale reports and raw
input hardlink protection. No game or GPU run is needed for these parser tests.

## Completed iteration-5 compatibility control

The parent integration check ran this analyzer on the immutable 216,605,445-byte
iteration-5 snapshot with its expected SHA-256. All 28 frames and 12,957 draws
are accepted as complete successful metadata, with no parser diagnostics. The
9,246 legacy local-gate/lifetime observations agree with the earlier audit.
All finite/source/index evidence remains unknown because that installed build
did not emit `motion_geometry`; the analyzer reports zero input candidates and
does not invent upload counters. This is a compatibility control, not new game
finite evidence. The [retained summary](../../verification/results/finite-upload-old-schema-control.json)
records hashes and the reproduction command. The full analysis suite passes
279 tests, including the 30 focused analyzer tests.
