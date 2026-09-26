# Loading interval evidence verification

**Removed 2026-09-25** (user decision): `--loading-intervals`, `X3M_LOADING_INTERVALS`, the interval recorder in
`src/proxy/loading_trace_light.cpp` and its fixture, runner, host test and analyzer are gone; this ledger is history
(`docs/verification/launcher-options-inventory.md`, "4. Removed").

Owning mechanism and limits:
[loading observations](../reverse-engineering/loading-observations.md#bounded-interval-recorder-for-the-next-consolidated-diagnostic-2026-09-15).
This diagnostic has no game capture yet. Native Windows execution is unverified;
source uses documented portable Windows APIs and cross-compiles for x86.

## 2026-09-15 — source and host checkpoint

- Isolated implementation based on `ac72cfd`; no game, install, candidate DLL
  build or commit. Parent owns Wine execution and candidate integration.
- Focused host command:
  `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_loading_intervals verification.analysis.test_loading_phase_markers verification.analysis.test_game_phases verification.analysis.test_snapshot_x3_run verification.analysis.test_game_phase_install`.
  Before the final parser/CLI additions: **32 tests passed**, 10.618 s.
  Final changed interval module: **7 tests passed**, 1.416 s; unchanged affected
  modules were not rerun. No full discovery suite was needed.
- The actual integer gate/ring host fixture completed **124 checks, 0 failures**:
  freeze before/after admission increment, accepted nested completions,
  abandoned token, saturation, 100 four-writer publication races, ring overwrite,
  clock regression and sequence saturation. Parser cases cover clipping,
  overlapping threads, reused TIDs, gaps, empty calls, intersecting retention
  loss, malformed/truncated metadata and bounded snapshot references.
- `sh verification/probe/build_loading_intervals.sh` and
  `sh verification/probe/build_loading_trace.sh`: x86 fixture builds passed.
  `python3 verification/probe/build_game_phase_cpu.py`: actual marker/CPU fixture
  build and callback audit passed. Its test-only freeze bridge and missing
  pre-existing profiler/dll-load link stubs do not affect production code.
- `python3 verification/probe/check_loading_intervals_cpu.py <exe>` passed for
  both `loading_intervals_fixture.exe` and `loading_trace_fixture.exe`: three
  inlined reachable Span bodies, zero XMM/MMX/x87 or allocation/formatting/EH
  violations. The new strict audit exposed the old SJLJ registration path;
  compile-only baseline reproduction used unchanged `ac72cfd` light source.
- Host `c++ -O2` gate-only timing observed **6 ns/call at one thread**, **92 ns/call
  at four threads**, 200,000 calls/thread. This is host atomic/control work,
  excludes Win32 TLS, QPC, ring publication and export, and is not FEX or game
  performance evidence.
- Local build/audit/test outputs: `/tmp/x3-loading-intervals-{build.log,cpu.json,forwarder-cpu.json,game-phase-build.json,host-tests.log,parser-tests.log,host-perf.txt}`.

## Owner-only runtime commands and gates

After the fixture builds, from the implementation checkout, the consume-only
command is:

```sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py --timings-json /tmp/x3-loading-intervals-wine-lock.json python3 verification/probe/run_loading_intervals.py
```

It writes `verification/results/bottle-X3/loading-intervals-summary.json` with
bottle name, WineArch and the FEX/WINEMSYNC configuration, binary identities,
per-command elapsed seconds and compact results. Raw outputs remain local.
It runs seven standalone modes (normal, abandonment, 17 threads, allocation
failure, TLS allocation/set failure, TID reuse), then the **112-check** existing
forwarder with intervals enabled. The forwarder checks native result/LastError
and IAT rollback, exports once, and the runner verifies binary counts, marker
identity and a complete reduction. The standalone normal mode checks live x87,
MXCSR and caller/callee LastError for cold and warm registration, plus nested
finish after freeze; reads are refused while an accepted token remains active.

The paired benchmark runs three off/on pairs for each of one/four threads and
one/two nested spans (100,000 iterations/thread). Report medians and every pair.
The coarse ceiling is approximately **13.245 microseconds added/span**: 2% of
21.702451 s at 32,771 calls. The runner uses a slightly conservative 13.244424 us
threshold. Its delta excludes the new disabled admission check and is not an
old-build baseline; thread startup is included. Exporter ticks are separate.
Native Windows and actual game-loading overhead remain open even after a clean
CrossOver fixture result. Only the already-planned consolidated user capture can
establish real marker/retention completeness, exporter duration and loading cost.

## Owner runtime qualification and independent review

Independent deep source/evidence review accepted this checkpoint. Reviewer
reproduced the affected seven-test module in 1.776 s and verified fixture and
export hashes, linked CPU paths and the old SJLJ allocation witness.

The single locked X3 queue passed **32 process invocations / 163 checks** with
zero failures; child elapsed 168.168 s, lock wait 0.0000035 s. Runtime was bottle
X3, WineArch arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`. The
[compact runtime result](../../verification/results/bottle-X3/loading-intervals-summary.json)
records all pairs and binary identities. Three fixture-local stub DLL hashes
were unchanged before/after execution.

The forwarder exported 20,040 records in 481,824 bytes, complete with zero loss
flags; export took 1.0698 ms in this fixture. Median enabled-minus-disabled
costs were 17.384 / 26.000 / 20.889 / 17.399 ns/span for thread/nesting pairs
1/1, 1/2, 4/1 and 4/2. These are CrossOver diagnostic measurements, not game
loading speed, native Windows behavior or an old-build comparison.

The separate game-marker CPU fixture was built and audited, not executed; its
freeze bridge is stubbed. Actual marker-to-recorder integration, real retention
completeness and game export overhead await the consolidated user capture.

## Run 25 / snapshot run60 — complete game retention

The [compact run60 observations](../../verification/results/run60-observations.json)
bind the local log and interval binary by SHA-256. The save-load markers span
7.2794795 s. All 11,867 records are retained and completed on TID 220,
generation 1, with zero overwrite/loss flags. This establishes real marker and
recorder integration for this capture. The presenting TID matches; that alone
does not establish engine main-thread identity.

The exact clipped wrapper union is 1.6703689 s (22.95% of the marker interval),
and the largest interval with no retained hooked activity is 0.8344746 s.
These are occupancy measurements, including time blocked inside wrappers.
They do not assign CPU or wait causes to the remaining interval. Run48's
21.702 s load is a different session/loading sequence, so this is neither a
measured optimization nor an attribution of the earlier stall. Raw artifacts
and the analyzer reduction remain in `/tmp/x3-bottleX3-run60` and
`/tmp/x3-run60-analysis`; no additional fixture execution was needed.
