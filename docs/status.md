# Project status

Updated 2026-09-13. **Per-pixel motion comes from the
game's own material draws through transformed shader variants writing a second
render target, not from deferred geometry replay.** The replay, admission,
execution-scope and geometry-lease modules stay in the tree as a numerical
reference and are no longer a prerequisite for any visual feature. See the
[live motion route](architecture/live-motion-route.md).
**TAA is done and verified in game**, including sharpen/mip-bias testing.
AgX is implemented, was seen at fixed exposure, and is retained. FP16 scene
redirection exists, but the lighting is still the game's gamma-space output
decoded into FP16: scene-referred HDR and HDR display output are not complete.
The whole-scene auto-exposure meter is wrong for space; its replacement has
passed review, is integrated and installed, and awaits game validation. Loading is down from 87 s to about
34–38 s in the recorded X3 runs. See the reconciled [goal checklist](goals.md),
[full user objective](user-objective.md) and [roadmap](architecture/roadmap.md).
Native Windows/Direct3D remains a required target alongside CrossOver Preview;
tests still run only on CrossOver. See
[portability requirements and gaps](architecture/platform-portability.md).

Current work: reviews 30–35 are complete. The existing chase-camera prototype
`7f4b251` was merged, reviewed and fixed, then qualified with all 18 runtime
suites, 907 host tests, 42 camera/site tests and 21 refreshed-result controls.
**Current install:** qualification merge `2e5f1af`, DLL SHA-256
`47f1452e09351bb306d0c5225134665aa9bf7c8cc5c46609fa67604db82027ad`
(11,587,494 bytes). The final audit matched 41 objects, 144 motion-source
hashes, 17 exports and 211 no-x87 boundary functions with zero violations.
See [review 35](verification/review-35-chase-integration.md) and the
[qualification summary](../verification/results/chase-integration-summary.json).

The camera is vanilla by default; only `--camera chase` enables it. First-game
acceptance and tuning remain pending, including menu behavior, aim alignment,
view-transition/TAA cuts, resolution changes and frame cost. Aggregate handler
timing is now available with telemetry; it is not a measured FPS result.
The user's [controlled run plan](verification/next-runs-2026-09-13.md) includes
the thirteen camera checks. Reader 2a remains the next loading test after
accepted crypto run 17; reader, adjacency and exposure game acceptance remain
pending. The agent never launches the game. Native Windows remains untested.

Installation matches the qualified DLL and app-local manifest. X3AP.exe and
cxbottle.conf are unchanged; eleven launch variants passed post-install
`--dry-run`, including the chase/TAA diagnostic with `--camera-log 1`.
Independent post-install review reproduced all eleven command/environment records
and verified the installed and preserved files. The previous DLL `ae2482fd…b193`
and manifest are retained for rollback in the
local directory recorded by
[chase-install-20260913.json](../verification/results/chase-install-20260913.json).
Older entries below are historical and do not override this installed state.

The [run-16 offline comparison](verification/run16-exposure-baseline.md) recovers
seven unresolved HDR inputs. Applying the candidate policy to them yields +2 EV
in every case because of its cap, versus +5.66…+8 for the old rule. Actual
post-TAA meter inputs and presented images are absent, so this is a scoped
counterfactual, not game acceptance of the new meter or its appearance.

## Run 17: crypto loading accepted on X3 (2026-09-13)

[Run 17](verification/run17-crypto-loading.md) confirms provider/key reuse with
native hashing and signature verification intact: the 844-check bulk recorded
844 provider and import hits, no verification failures, and 0.1353276 s in the
signature probe versus the earlier 12.835 s. The heuristic save gap was 27.574 s
versus 35.707 s; different feature flags prevent assigning that whole-load
change to the cache. Counters cover two reported windows, with no teardown
total. Independent artifact review passed after correcting a ratio typo.
That run used the preceding DLL. Reader verify (group 2a) remains the next
loading test; the chase-camera integration is now installed as recorded above.

## Handoff (2026-09-13 early morning): read docs/handoff-2026-09-13.md first

The orchestrating session ended at commit `1c8322e` (WIP checkpoint). The
self-contained handoff for the next orchestrator — goal checklist with honest
state, installed build, in-flight items with their notes, run plan, lessons —
is [handoff-2026-09-13.md](handoff-2026-09-13.md). Unmerged branches:
`worktree-agent-a157d2e7bd47eab9d` (crypto cache) and
`worktree-agent-aea55d854948bd6a0` (space-aware exposure meter).

## Crypto cache checkpoint (2026-09-13; merged, not installed)

Reviewed branch `8794a5d` passed **572 checks on each bottle**, six host tests
and its 210-function no-x87 audit. [Review 33](verification/review-33-crypt-cache.md)
closes partial hook installation, call-site/lifetime/concurrency and loader-lock
cleanup findings. The cache is opt-in; hashing and signature verification still
run through CryptoAPI. Its bounded retained handles and named scratch-container
lifetime are documented in [crypt-cache.md](verification/crypt-cache.md).

Root's merge review preserved the newer normalizer, reader initialization and
review-30 log/ABI fixes. Older branch loading reports were not substituted for
main's newer evidence. Two loading/hook source manifests now include the linked
crypto module. Merged loading/capture sources pass the production-flag syntax
check; CLI dry-run and six crypto host controls pass. Full integrated fixture
qualification follows the adjacency/reader work before installation. The
approximately 4× signature-fixture speedup is not a measured game load saving.

## Combined DLL qualification and installation (2026-09-13)

Frozen source `ae03d9a` produced installed DLL SHA-256
`ae2482fd5146c62898fbe20c45441d9d14183d705c50e3d03872094ec635b193`.
[Review 34](verification/review-34-integration.md) records eleven validated
selected motion/HDR cases (not a full-suite pass), all ten shader checks,
211 no-x87 boundary functions, 17 exports and matching object/source maps.
The final merged X3 CryptoAPI suite passed 572 checks across twelve processes.
The independently reviewed branch suites remain scoped historical evidence.
No game was launched, and no native-Windows runtime result is claimed.

## Reader and adjacency checkpoint (2026-09-13; reviewed, not installed)

[Review 31](verification/review-31-adjacency-reader.md) is closed for its
scoped fixes. Reader verifies the short-record cursor, checks rewind failure
before publishing a buffer, preserves LastError and repairs shifted diagnostic
fields; its X3 fixture passes 4,721 checks. Adjacency now refuses unsupported
FP domains and ambiguous SSE2 edge selection before writing output, and checks
registry types. Both bottles pass loading suites of 86 / 123 / 36,093 / 36,150
checks, the standalone cache's 770 checks and six hook cases totaling 13,271.

The exact-binary replay serves native-equal output for all 37 retained meshes
on both bottles. X3 computes and admits all 37; Steam conservatively falls
back for all 37. The older unseeded Steam mismatch cause remains unresolved;
this seeded replay does not justify removing that guard. Fixed-order X3
replay totals were 196 ms native versus 58 ms served; these are diagnostic
fixture timings, not a measured game load saving. Several fixture-only FP
controls now distinguish requested state from delivered state and use a real
comparison to witness status replay. No production gate was weakened.

The independent review checks source/native/retained-binary/raw-result hashes.
All game fast-mode acceptance and native-Windows runtime behavior remain
unverified. The source freeze is now entering final combined qualification;
the installed game DLL remains review 30.

## Space-aware exposure checkpoint (2026-09-13; merged, not installed)

Branch `e897011` passed [review 32](verification/review-32-exposure.md):
98 motion/HDR cases on each bottle, supporting temporal/scene/ownership suites,
strict configuration and readback-failure controls. The meter uses a lit-tile
statistic, a highlight limit, target dead band and −3…+2 EV bounds. Its visual
quality still requires the user's game capture; the FP16 input remains decoded
gamma-space lighting. Dense CPU statistics cost about 43 µs at 80×48 tiles on
X3/FEX; this excludes GPU download and is not a game-FPS result.

The merge retains complete branch result artifacts rather than mixing reports
from different runs. Review-30 reports remain available at their checkpoint.
The branch's numerical-helper hashes were recorded after its runs, with that
limitation explicit; final integration expands the before/after source manifest
and reruns selected combined paths. Root review also fixed selected-mode's
early return so it checks source stability before publishing its partial result;
four host controls passed. See [review 34](verification/review-34-integration.md).
This checkpoint does not change the installed review-30 DLL.

## Review 29 record — loading branch, adjacency parity and present readback merged (2026-09-12 night)

Reviews: [review-25.md](verification/review-25.md) (fast adjacency, engine
reads, gz buffer; three low findings fixed), [review-27.md](verification/review-27.md)
(loading branch, merged as `c152860`), [review-28.md](verification/review-28.md)
(GenerateAdjacency parity rewrite, `a839bc9`) and
[review-29.md](verification/review-29.md) (integration of the three on main
after the merges `c152860` and `0a32f33`: hook-table/gate/install-order
checks, `--dry-run` of the planned commands, full suite chain green after a
clean rebuild; the build hash to install next is recorded there). Nothing in
this checkpoint is gameplay-verified yet; the next user runs below are the
acceptance tests. The game and `tools/manage.py` default to the CrossOver
bottle **X3** (arm64 Wine + FEX); fixtures default to Steam
(`X3M_FIXTURE_BOTTLE=X3` switches them, see
[bottles.md](verification/bottles.md)). Every Wine-executing command now runs
under `verification/probe/wine_lock.py` (machine-wide lock; AGENTS.md).

- **Resource reader run C → cursor parity fix (2026-09-13, uncommitted)**: run
  C (`--resource-read verify --dat-handles`, X3) gave `verify_files=4084
  verify_mismatched=20`, all `cursor_ok=0` with equal bytes: the original's
  1 KiB chunk loop exits on `Z_STREAM_END` and leaves the record cursor
  `rem` bytes short whenever `(length − first) mod 1024` is 1…8 (six records,
  identified by an offline catalogue scan; 0.59 % of all gzip records). The
  core now predicts the loop's end from the bytes `inflate` consumed, sets the
  cursor and the stream there, verify also compares `_ftell`, and
  `resource_reader_metric` gains `cursor_short=` and the phase split
  `fast_read_us/scan/alloc/inflate`. Fixture: `RR_CASE name=cursor` (20
  sources, remainders 0…9 at 1 and 3 chunks, last-record `.dat`, pooled
  sequence). Docs: [resource-reader.md](reverse-engineering/resource-reader.md)
  "Record cursor after the chunk loop", [verification](verification/resource-reader.md)
  "Run C". Next: rerun verify (expect `verify_mismatched=0 cursor_short≈20`),
  then `fast`.
- **Fast exact-match GenerateAdjacency** (`X3M_MESH_ADJACENCY=native|verify|fast`,
  `--mesh-adjacency`; [mesh-adjacency-fast.md](verification/mesh-adjacency-fast.md)).
  Byte-identical to d3dx9_37 on 35/35 computable fixture meshes (welding with
  signed zero, head-insertion tie-breaks, degenerate rules, no double
  adjacency); 111–447× faster in the Wine fixture; MXCSR pinned to 0x1f80 and
  the caller's state restored; NaN/near-neighbour inputs fall back to native
  behind a 2·ε gate. The cache's FP gate now keys precision/FTZ instead of
  refusing them (`mesh_adjacency_cache` 767 checks, hook survey 13,271).
  **Parity fix (2026-09-12 night, after bottle X3 run 8 reported
  `verify_mismatched=167` of 7,715 meshes): the six D3DX rules the module got
  wrong were decompiled from the game's d3dx9_37 and the module, its Python
  port and the fixture rewritten to them
  ([d3dx-generate-adjacency.md](reverse-engineering/d3dx-generate-adjacency.md),
  [handoff-adjacency-parity.md](verification/handoff-adjacency-parity.md), resolved;
  reviewed in [review-28.md](verification/review-28.md), three gate/contract
  findings fixed, committed as `a839bc9`).
  Fixture evidence on both bottles (Steam/Rosetta and X3/FEX): 51/51 computable
  named cases and a 2,000-mesh random differential sweep byte-identical to
  d3dx9_37 (`mismatched=0`), 33,272 checks; the rewrite's speed regression was
  removed (host: grid 3.97 ms, split-150 1.90 ms, review 25: 4.12 / 1.83). Still
  `fast` stays off until the user's verify run on bottle X3 shows
  `verify_mismatched=0` (`launch --telemetry --mesh-adjacency verify
  --mesh-adjacency-dump`; any `mesh-adjacency-<n>.bin` goes through
  `tools/analysis/replay_mesh_adjacency.py --wine` first).**
  The thread-local arena retains ≤16 MB per loader thread for the process lifetime.
- **Direct engine reads** (`X3M_ENGINE_READS=rpm` restores ReadProcessMemory;
  [route-cost-run1.md](verification/route-cost-run1.md)). Reads are validated
  against a 32-entry VirtualQuery region cache (trusted per frame or 100 ms),
  copied with `rep movsb` from a translation unit compiled without SSE/MMX
  (0 xmm/x87 references), so the engine-thread wrappers stay state-clean.
  Fixture: object_lifetime `current` 6.9 → 0.70 µs, object_trace route reads
  3.3 → 1.3 µs, capture 7.7 → 2.2 µs; identity hashes identical. Per-draw
  telemetry stamps now need `X3M_TELEMETRY_DRAW=1`. Iteration 10 measured the
  route at 31% of the FEX frame with the old path (`route_gate` ×1.19 slower
  under FEX), so this is the main frame-time lever for the next run.
- **gz read-ahead buffer** (`X3M_GZ_BUFFER=1`, `X3M_GZ_BUFFER_KB`, `--gz-buffer`;
  [gz-buffer.md](verification/gz-buffer.md),
  [savegame-gz-stream.md](reverse-engineering/savegame-gz-stream.md)).
  Serves the savegame reader's ~3-byte `gzread` calls from a 256 KB buffer
  with zlib 1.2.3 semantics (735,871 fixture checks against the bottle's real
  zlib1.dll). Key finding: the 0.66 µs/call "zlib" cost in the profile was our
  hook envelope under FEX (raw gzread 33 ns); the buffer makes instrumented
  loads representative (removes ~16 s of instrumented stall) but the plain
  game gains ~0.5 s. The true X3 save-load time without telemetry is unknown —
  run 1 below measures it.
- **Light loading hooks, probe batch 2, fast resource reader** (loading
  branch, reviewed in [review-27.md](verification/review-27.md), merged into
  main as `c152860`; [loading-probes.md](verification/loading-probes.md),
  [resource-reader.md](verification/resource-reader.md)). The counting/timing
  import rows moved to a no-SSE unit without `CpuCallBoundary` (objdump: 0
  xmm/x87 references; envelope 355 ns vs 1,215 ns under FEX, gz fixture);
  `--loading-probes` adds 18 light import rows and 12 byte-verified
  entry/exit trampolines on the engine's loading functions with per-gap
  tables in `analyze_loading_profile.py`; `--resource-read verify|fast`
  decodes gzip resources with one fread + one inflate into the game's
  `_malloc` (no memset) with fallback to the original, `--dat-handles` pools
  the catalogue `.dat` handles (both call sites are plain `E8` sites);
  `frame_end` lines carry `elapsed_ms`/`dt_ms`/`qpc` in every mode. All three
  gated, exact-executable only, fixture-verified against the real zlib; not yet
  run in the game — the acceptance runs are listed in the two documents and
  in runs 6–7 below. A plain `--direct` run patches nothing (review 29
  checked the gates): only the `frame_end` stamps differ from the review-26 build.
- **Bottle X3 validation** ([bottles.md](verification/bottles.md)): four of
  five suites pass on X3 with comparable numbers; the sampling profiler cannot
  attribute samples under FEX (`GetThreadContext` returns creation-time
  context, also seen once on Rosetta), and the FEX CRT prints NaN/Inf as huge
  finite numbers (the exposure fixture must print bits). Loading attribution
  on X3 therefore relies on hook/trampoline counters.
- **Presented-image readback and the iteration-12 tool** (readback branch,
  merged into main as `0a32f33`; fixture evidence in
  [taa-sharpen.md](verification/taa-sharpen.md) and
  [iteration-12.md](verification/iteration-12.md), integration checked in
  review 29). `--taa-debug` capture frames now also write
  `present_<device>_<frame>.bgra8`, the game's main target after the RCAS
  sharpen draw / copy-back (8-bit route) or after the HDR write-back — the
  `taa_*` readback is the unsharpened history input, so until now no run
  could measure the sharpen in game
  ([capture-format.md](architecture/capture-format.md), [taa-sharpen.md](verification/taa-sharpen.md)).
  Fixture: byte-identical to the presented frame on 25 TAA cases, RCAS
  reference within 0.50 code, 0 outside the 3x3 bound; motion-output 94
  cases, temporal pass 386/228/2, scene capture, ownership 27, x87 PASS, 786
  analysis tests OK. `tools/analysis/analyze_iteration12.py` re-emits
  `iteration-12.json`/`.txt` (numbers unchanged from the hand-off) and adds
  §2.4: RCAS model of run 10's stationary/slow bursts — 0 of 47.2 M channels
  outside the 3x3 neighbourhood, interior gradient energy ×1.04–1.07, edge
  rise −0.03 to −0.20 px, halo excursion +0.02–0.06 of edge contrast, flicker
  energy +3–7 % evenly across classes ([iteration-12.md](verification/iteration-12.md),
  complete). Fixed: `analyze_iteration09_run2.py` burst grouping (readback
  frames only) and its `--text` crash without mesh-cache lines. The
  `struct ID3DXMesh;` forward declaration in `loading_trace.h` (the `b10d129`
  checkpoint did not compile without it) is on main since `a839bc9`; the
  merge kept one copy.
- **Analyses**: [iteration-10.md](verification/iteration-10.md) (FEX health
  clean, TAA no regression, frame time 24.1 → 8.4 ms route off, 34.2 → 16.9 ms
  route on; hook agrees 214/214, `rs_resyncs` 24 on a latch-only screen);
  [iteration-11.md](verification/iteration-11.md) (review-25 run 9: attributed
  route cost 34.4 → 11.9 µs/draw, 12.10 vs 16.91 ms at matched draw counts,
  hook Agree 162/162 with 0 disagreements and `rs_resyncs` 0, history match
  99.83 %, gz buffer 14.46 M calls → 175 real reads; per-draw stamps off, so
  the route's own cost is no longer measurable and the readback row-pair,
  temporal and depth-agreement checks were unavailable);
  [script-xml-load-stall.md](reverse-engineering/script-xml-load-stall.md)
  (the second save-load stall is a mixed asset phase: per-resource re-opens of
  the catalogue `.dat`, a redundant 22.5 MB memset, 1 KiB inflate chunks with a
  byte XOR, locked `fgetc` header parsing, and an unhooked CryptoAPI signature
  check at 0x004cabc0; CRT is static, so fixes are engine trampolines);
  [sampler-states-and-mips.md](reverse-engineering/sampler-states-and-mips.md)
  (the game shadows sampler state and never sets a mip bias);
  [native-windows-audit-2026-09-12.md](architecture/native-windows-audit-2026-09-12.md)
  (D1 format-converting StretchRect in-scene, D2 ps_3_0 with fixed-function
  VS, D3 MSAA mismatch on RT1/RT2, W1 missing d3d9 exports, W3 log path).
**Installed (2026-09-12 night, after review 26, bottle X3):** `build/d3d9.dll`
from commit `c782a5a` (sharpen, mip bias, scene hook default on with the
route), SHA-256 `8864bff00e284db0a23ff152a1cf3e26c0ffaed161d010ec305be4bf46abb532`,
through `tools/manage.py install`.

**Installed (2026-09-12 night, after review 25, bottle X3):** `build/d3d9.dll`
SHA-256 `38562f3a7e2bbb03c6ffd1e746540b062dbf1ec9d184163407d771d5cbfbc1f8`,
through `tools/manage.py install` (default bottle X3; the Steam bottle keeps
the stage-2 build `db63e120…`).

- **Merged from the worktrees, reviewed in review 26 (2026-09-12)**:
  - Post-resolve RCAS-style sharpen
    ([design](architecture/temporal-integration.md#post-resolve-sharpen-2026-09-12),
    [record](verification/taa-sharpen.md)): `X3M_TAA_SHARPEN=<0..1>`
    (`--taa-sharpen`, requires `--taa`) applies RCAS (our HLSL reimplementation
    of AMD's published FSR 1.0 RCAS, guarded and clamped to the 3×3 min/max)
    to the display image only — never to the history — on both routes: the
    8-bit route's copy-back becomes the sharpen draw inside the pass (cost
    neutral at 5120×1440: 2.252 → 2.238 ms), the HDR write-back tonemaps five
    taps then sharpens (+0.80 ms at 5120×1440). Off is bit-identical (twin
    runs byte-equal; the seven existing shader programs kept their hashes).
    Fixture measurement at 1.0: gradient-energy ratio 1.147, 10–90% rise
    1.82 → 1.49 px, MTF50 0.262 → 0.298 c/px; GPU output within 0.5 code of
    the Python reference on both routes, order verified as after-tonemap.
    Synthetic only; not gameplay-verified.
  - Mip LOD bias (TAA blur fix, sampler half): `--taa-mip-bias -0.5`
    (`X3M_TAA_MIP_BIAS`, default off, next to `--taa-k`): the route biases
    the mip-mapped stages of routed material draws while the jitter is on and
    restores before every other draw, at the scene end and before Reset;
    capture now logs `MIPMAPLODBIAS`/`MAXMIPLEVEL`. Fixture evidence in
    [taa-mip-bias.md](verification/taa-mip-bias.md); not yet seen in game.

**Installed (2026-09-12 night, after review 29, bottle X3):** `build/d3d9.dll`
from commit `a34c389` (adjacency parity, loading branch, present readback),
SHA-256 `4abd56b3a77682594756c8805f6f1661c35f8f7b611a297f312bfec2d00855a8`,
through `tools/manage.py install`. The native-Windows fixes (`20683cc`) are
merged but not installed until review 30 passes.

## Checkpoint: review 30 complete — native-Windows fixes D1–D3, W1, W3 (2026-09-13; installed)

Implements every item of the paused
[handoff](verification/handoff-windows-fixes.md) against the
[native-Windows audit](architecture/native-windows-audit-2026-09-12.md);
status per item in that document's new table, gaps in
[platform-portability.md](architecture/platform-portability.md). Nothing here
is a native-Windows run: it is Windows-compatible source verified on
CrossOver Preview (Steam bottle, plus one X3/FEX rerun of the motion suite).

- **D2 vs_3_0 quad program.** `src/temporal/quad_vs.hlsl` (compiled by the
  generator's new per-shader `target`, 43 words) and
  `src/renderer/quad_vertex_program.h`: every proxy quad (resolve, sharpen,
  copy, HDR write-back/tonemap/meter, self tests, sentinel fill) now binds one
  VS + declaration per pass instead of XYZRHW + null VS; the sentinel and
  self-test programs became ps_3_0. Fixture twin `X3M_QUAD_FVF_SWITCH` /
  `X3M_FIXTURE_QUAD_FVF=1`: byte-identical (temporal `QUAD_TWIN identical=1`
  ×6, motion `seam-taa-quad-fvf` = `seam-taa-on`).
- **D1 copy mode.** Attach runs a 4×4 StretchRect round trip per 8-bit format;
  `taa_copy=stretch` only when the adapter query and the round trip both pass,
  else `taa_copy=draw` (same-format staging copy + identity draws both ways,
  `TemporalPass::configure_copy`); `taa_reason=format_conversion` is gone.
  Temporal `COPY_MODE history_identical=1 display_max_code_difference=0`;
  motion `seam-taa-copy-draw` (`X3M_FIXTURE_STRETCH_FAULT=1`).
- **D3 MSAA refusal.** `motion_output_msaa_refused device= frame= msaa=`
  once, gate 1 for every draw, no jitter, `taa_skip=11`, `msaa=` frame field;
  fixture mode `msaa` (`seam-msaa`).
- **W1 exports.** 17 names in `d3d9.def`; naked `jmp` forwarders with `ret N`
  fallbacks, C++ `Direct3DCreate9On12[Ex]` with admission veto and
  `unproxied=` log; `test_d3d9_exports.py` (host, PE parser
  `tools/analysis/pe_exports.py`) and `run_d3d9_exports.py` (Wine).
- **W3 log directory.** `%LOCALAPPDATA%\x3-modern-renderer\captures`
  fallback, first log line `capture_dir= source=`; read-only-directory runner
  case; README and `manage.py status` name both.
- **Diagnostics.** `engine_memory phase=create|summary ...` line (integers);
  the exposure fixture prints non-finite values as IEEE bits and the runner
  decodes them, so the `hdrexposure` cases pass on the X3/FEX bottle
  ([bottles.md](verification/bottles.md) limitation 2 closed for the fixtures).
- Merge note: main's `b10d129` did not compile (`loading_trace.h` used
  `ID3DXMesh` without a declaration); a forward declaration fixes it.
- **Completed verification:** [review 30](verification/review-30.md) binds
  reused current-source evidence and fresh runs separately. Steam and X3 motion
  both pass 97 cases and 26 benches, with identical quad/copy twins and MSAA
  refusal. Temporal pass: 508 numerical / 278 state checks; temporal resolve:
  78 samples, two generations and Reset. Loading: 72,234 checks on each bottle;
  exports: 8 + 8; X3 gzip: 735,876; adjacency cache: 767; hook: 13,271. Existing
  ownership, scene capture, object lifetime/trace and bounded reader evidence
  were revalidated against their recorded sources and artifacts.
- The temporal fixture's old 60-second total budget expired during its two
  full shader compilations. The reviewed 180-second budget passes in about
  65 seconds without changing its numerical/reset contract. Scoped timeout
  child cleanup and five host controls keep the Wine lock until the child exits.
- Final host checks: **836 tests**, no-x87 **196 functions / zero violations**,
  17 PE exports, ten exact shader regenerations. The final reviewed DLL is
  SHA-256 `d648594bf346f8ccc8d5e476bcc26345d16741974f76c5b3769017075712e825`.
  It is installed at checkpoint `3124e0b`; this is still CrossOver evidence, not a
  native-Windows runtime result.
- **Separate review 31 remains open:** known SSE2 competing-normal adjacency
  differences, registry type and FP-domain admission; reader diagnostic format,
  failed final rewind and LastError transport. See [review 31](verification/review-31-adjacency-reader.md).
  These optional paths are not approved for fast gameplay by review 30. The
  final run plan requires their fixes and meaningful verify coverage first.
- Install verification: the app-local DLL and ownership manifest match the
  audited SHA-256; `X3AP.exe` and `cxbottle.conf` hashes are unchanged. The
  previous owned DLL/manifest are retained at
  `/tmp/x3-review29-rollback-n4t0lk50/`. No game was launched. Crypto/exposure
  branch verification and the review-31 fixes precede the next requested runs.

## Checkpoint: TAA tremble fixed, resolve quality pass, loading attribution (2026-09-12, superseded by the section above)

Evidence at this checkpoint (details in the linked documents):

- **TAA rerun ([iteration 8](verification/iteration-08.md))** with the corrected
  history convention: all six stationary frame pairs are `stable` (iteration 7:
  all tracked the jitter); the resolved image moves 0.14–0.31× the raw colour
  shift, and 0.05–0.11 px on a fully routed tile, i.e. at the (1−w) bound.
  Blur improved from 0.56 to 0.64–0.75 gradient-energy ratio. Remaining flicker
  sits in routed silhouette edges (share 0.78–0.89 of the residual variance)
  and thin features; routed interiors are filtered 133–151×. **Frame time is
  unchanged by TAA**: same-build run B (route on, TAA off) matches run A at
  29.9 vs 29.9 ms and 63.9 vs 64.0 ms per regime; the iteration-7 "2.6×
  regression" was regime occupancy, and is retracted. Attributable cost is
  about 70 µs per frame at 1280×768.
- **Resolve quality pass** ([design](architecture/temporal-integration.md)
  "Resolve quality", [verification](verification/temporal-resolve.md)):
  neighbourhood variance clip (mean ± 1.25σ ∩ min/max box) replaces the hard
  depth equality; the depth test is a one-sided disocclusion test with
  NaN-safe comparisons; closest-depth 3×3 dilation; 16-tap Catmull-Rom
  history; sentinel policy `c7.w` (1 current-only, 2 camera far-plane path,
  wired but off until the route supplies the camera transform). Fixture:
  silhouette ring variance reduced 178× with zero ghosting, thin-line drift
  ≤ 0.007 px and wobble ≤ 0.072 px, scrolling-sinusoid amplitude ratio 0.930
  (bilinear model 0.712). Program 1,695 → 3,794 words with no boundary-cost
  regression (resolve 0.32 ms at 1280×768, 1.62 ms at 5120×1440).
- **Route cost telemetry and lazy MRT binding**
  ([telemetry](verification/telemetry.md) "Route and boundary cost",
  [motion output](verification/motion-output.md)): per-frame CPU-inclusive
  metrics for gate, apply/undo, render-target sets, jitter writes, fills, the
  five resolve phases, copy-back and readbacks (ticks only under
  `X3M_TELEMETRY=1`); `X3M_MOTION_RT_MODE=lazy` keeps RT1/RT2 bound across
  routed draws and restores before every other device operation, with
  identical colour/motion/depth hashes in all fixture modes and 20 → 12
  render-target sets per fixture frame. Default stays `perdraw`.
- **Loading attribution ([iteration-8 loading](reverse-engineering/iteration08-loading.md))**:
  menu load 30.1 s (8.4 s instrumented, **21.7 s unexplained**), save load
  106.7 s (33.3 s instrumented, **73.4 s unexplained**); the two same-build
  runs agree on the unexplained remainder within 0.4 s, so it is deterministic
  engine CPU work, not I/O variance. The menu scene is reloaded from scratch on
  every return (1,017 adjacency calls, 298 MB re-submitted). The mesh
  adjacency cache was `enabled=0` in both runs (bounded upside 14.5 s per run).
  The savegame is read through 13.9 M `gzread` calls of 3 bytes each.
- **Loading orchestration ([disassembly](reverse-engineering/loading-orchestration.md))**:
  the resource resolver runs an uninstrumented `FindFirstFileA` directory
  enumeration per lookup with no negative or positive cache (at most one
  `CreateFileA` per resource; CAT lookup is a `bsearch`); the script VM at
  `0x004ab880` touches no hooked API; mesh adjacency is a pure temporary
  (`CleanMesh` + `OptimizeInplace` only, remaps unused); the type-table pass
  reruns on every new game/save load; no sleeps or CRC passes on the load path.
  The loading tracer now hooks `FindFirstFileA`/`FindNextFileA`/`FindClose`
  (19 boundaries, fixture 85/85).
- **In-process sampling profiler** ([verification](verification/sampling-profiler.md)),
  `X3M_PROFILE=1` / `tools/manage.py launch --profile`: a sampler thread
  suspends each game thread at 2 ms intervals, records EIP, the EBP chain and a
  conservative return-address scan, and reports per-thread module splits, top
  leaf RVAs, top main-executable frames and caller pairs every 5 s on the same
  QPC clock as the loading metrics. Fixture: 100% attribution for framed,
  frame-pointer-omitted and waiting threads, no deadlock under concurrent heap
  and file use, overhead within 1.5% (tick cost ≈150 µs per thread under
  Wine). `tools/analysis/summarize_profile.py` and `X3ProfileSymbols.java`
  map windows to functions. Not yet run in the game.
- **Architecture reassessment ([assessment](architecture/assessment-2026-09-12.md))**:
  keep the proxy architecture; add a `SetRenderState` shadow, an engine
  frame-routine boundary hook (the scene-end callsite `0x004721b1` is not
  glow-gated, so hooking it frees TAA from the bloom option), live camera
  globals, and enable the adjacency cache.
- **Camera state ([disassembly](reverse-engineering/camera-state-and-frame-routine.md))**:
  `*0x00608a38` projection, `*0x00608a40` view (row-vector, left-handed, same
  context scale as world matrices), final at the per-view Clear the proxy
  already hooks; projection uses `zn = 6`, `zf = 2,000,000`, half-FOV from a
  binary angle; `m22/m32` are rewritten before every material draw and must not
  be sampled at draws. The second scene is a six-face environment-map render
  that must be excluded from motion history. `DrawIndexedPrimitive` has no
  direct engine callsite, so return-address bucketing cannot label the main
  pass.
- [Review 18](verification/review-18.md) (NaN fail-open sentinel test fixed,
  shared game-running guard that ignores Ghidra jobs, doc/manifest fixes) and
  [review 19](verification/review-19.md) (profiler). Suites: motion output 34
  runs, ownership 26, fallback, temporal pass 318/164, temporal 78/78, scene
  capture 4,908 checks, no-x87, 503 analysis tests.

- **Camera reprojection for sentinel pixels** ([design](architecture/temporal-integration.md)
  "Camera reprojection for sentinel pixels", [review 20](verification/review-20.md)):
  the route reads the engine's projection and view buffers at the scene's
  depth Clear behind the verified-executable gate, builds the far-plane
  transform (rotation only, translation ignored) once per frame and runs
  sentinel policy 2 automatically when both frames are valid and the rotation
  is below `X3M_CAMERA_CUT_DEG` (20°; larger rotations reset history). Fixture:
  background drift under 90° yaw 0.06 px unjittered / 0.12–0.18 px jittered,
  versus 0.86–1.05 px crawl without it; a 25° jump produces an exact cut; the
  environment-map frame is rejected by the selector and drops history and
  camera state. Colour is bit-identical with the switch at 1. Resolve program
  3,840 words; boundary 0.74 ms at 1280×768, 2.24 ms at 5120×1440.
  `tools/analysis/analyze_camera_state.py` cross-checks the read matrices
  against the shadowed constant rows on the next run.
- **Loading attribution pipeline**: `tools/analysis/analyze_loading_profile.py`
  turns a `--telemetry --profile` log into per-gap tables (hooked time next to
  sampled attribution by function, symbolized through Ghidra); dry run on the
  iteration-8 log reproduces the gap figures in 1.6 s.

- **Engine boundaries and render-state shadow** ([design](architecture/live-motion-route.md)
  "Engine boundaries and state shadow", [review 21](verification/review-21.md)):
  `X3M_STATE_SHADOW` (default on) hooks `SetRenderState` and answers the
  route's per-draw state queries from an eight-state shadow (native
  `GetRenderState` calls per fixture frame 295 → 144, 423 → 126 in bursts),
  resynchronised on state-block Apply, EndStateBlock and Reset; it also closes
  the lazy-mode write-mask hole. `X3M_SCENE_HOOK` (default on with the route
  since review 26; `--scene-hook off` disables it) patches the frame routine's compositing callsite
  (`0x004721b1`, bytes verified, restored at the last device release) so the
  route learns the scene end from the engine and the TAA resolve runs there
  before the glow pass; the bloom `StretchRect` becomes the fallback. Fixture:
  hook frames bit-identical to the copy-path resolve, glow-off frames resolve
  only with the hook. The history key gained a pass field (main scene = 1)
  without changing any match. Review 21 found the hook's signal ran under the
  light CPU boundary although the resolve reaches x87 code; it now preserves
  the full state once per frame.
- **Compositor and glow ([disassembly](reverse-engineering/compositor-and-glow.md))**:
  the compositor returns at once unless the registry bit `VideoD3DFlags2`
  bit 7 ("Glow enabled") is set; with glow on it reads RT0 only through the
  bloom `StretchRect` and the final additive blend; HUD and text draw after it
  straight to the back buffer; no readback of RT0 in normal frames.
- **FP16 HDR scene path design ([design](architecture/hdr-scene-path.md))**:
  redirect RT0 to an owned A16B16G16R16F target at the latching Clear, keep the
  game's depth and the motion/depth MRTs, run TAA on HDR before tonemapping
  (reversible luminance weighting), AgX + adapted exposure at the scene-end
  hook into the game's 8-bit target so the game's glow and GUI stay unchanged
  in stage 1. The game is a gamma-space renderer (no sRGB state observed on
  762 draws), so decoding is a documented approximation.

- **FP16 HDR scene path, stage 1 ([design and implementation](architecture/hdr-scene-path.md),
  [verification](verification/hdr-scene-path.md), [review 22](verification/review-22.md))**:
  behind `X3M_HDR=1` (`--hdr`, default off) the route redirects the game's RT0
  to an owned A16B16G16R16F target at the latching Clear (caps gate plus a 4×4
  MRT and copy self test), keeps the game's depth and the motion/depth MRTs,
  suspends the redirect across application render-target switches
  (environment-map faces) and writes back through an identity tonemap at the
  engine scene-end hook, else at the bloom copy, else at Present, with a
  must-unwind ladder (shader copy → StretchRect → rebind). Fixture: presented
  frames equal the non-HDR twins on 98.6–99.0% of pixels with at most one 8-bit
  code of difference on unquantized material values (FP16 double rounding;
  background, flat and alpha exact; a mid-value draw yields exact expected
  codes); motion/depth readbacks byte-identical through the FP16 MRT; additive
  2.0 + 8.0 draws hold 10.0 in the FP16 readback; five injected write-back
  faults unwind with one log line each; Reset while active succeeds with zero
  final device references. Boundary cost +0.05 ms at 1280×768, +0.44 ms at
  5120×1440; target 7.9 MB / 59 MB. **No HDR output is visible yet**: stage 1
  is an identity path. Review 22 fixed a missing RT0 rebind before Reset, a
  write-back of an uncleared target after a failed Clear, MRT unbind order and
  a hard-coded main format.
- **Stage 2 preparation**: AgX reference (`tools/analysis/agx_reference.py`,
  Wrensch's minimal AgX constants; mid-grey 0.18 → 0.497 display), exposure
  model (`exposure_reference.py`), `src/temporal/agx.hlsl` + `agx.h` (constants
  c8–c21), 32 tests; not compiled or wired.

- **FP16 HDR scene path, stage 2 ([implementation](architecture/hdr-scene-path.md)
  "Stage 2 implementation", [verification](verification/hdr-scene-path.md),
  [review 23](verification/review-23.md))**: `X3M_HDR_TONEMAP=agx`
  (`--hdr-tonemap`, default off = stage-1 identity) writes the FP16 scene back
  through AgX (looks none/golden/punchy, decode gamma2.2/sRGB/none, optional
  clamp, alpha carried) with automatic exposure from a GPU log-luminance meter
  chain (1×1 R32F ring read back one frame later, host adaptation τ 0.4 s up /
  1.2 s down, EV range −8..+8, manual EV override). Fixture: every ramp code
  is the correctly rounded Python reference (max error ≤ 0.50 code across
  looks, decodes, clamp and EV settings); meter error 0.063%; EV replay 1.2e-6
  vs the reference; three injected tonemap faults unwind to identity. Cost
  +0.33–0.44 ms at 1280×768; the 5120×1440 figure needs a re-measure (one
  bench showed +1.7 ms with the resolve off). Infinite or NaN scene pixels
  present white on this backend (documented, host adaptation guarded). The
  presented image is AgX of a gamma-space FP16 scene (approximation), still
  LDR to the game's bloom and GUI; nothing gameplay-verified.

**Installed (2026-09-12, after review 23):** `build/d3d9.dll` from the HDR
stage-2 checkpoint, SHA-256 `db63e120afcbb38e1382f22dffb96fb6030bbab50d1c3faf2b5587e055ce7e3d`, through `tools/manage.py install`.

**Installed (2026-09-12, after review 22):** `build/d3d9.dll` from the HDR
stage-1 checkpoint, SHA-256 `4f46feee7d9204bd7fbae55378d6e15fb0c2bfe81fddb7e4c64fa0ca84388a8d`, through `tools/manage.py install`.

**Installed (2026-09-12, after review 21):** `build/d3d9.dll` from the
engine-boundary checkpoint, SHA-256 `9cd7d7d85cac213edda739611f3f477e621d0b9513776fd44d403ca8f7545993`, through `tools/manage.py install`.

**Installed (2026-09-12, after review 20):** `build/d3d9.dll` from the camera
reprojection checkpoint, SHA-256 `27f693429e2c85692db8437efdf7b8010410aba1336387f0b76bb05e4892638a`, through `tools/manage.py install`.

**Installed (2026-09-12, after review 19):** `build/d3d9.dll` from commit
`459ddfa`, SHA-256
`4380a720be5d2e786b502d338e507af750515654177ba4174cf9b346975266ca`, through
`tools/manage.py install` (bottle configuration unchanged). The previous
installed build (commit `162b2f7`, `200aefff…`) is superseded; rollbacks stay
in `artifacts/rollback/`.

Next (in order): the user runs: TAA on with
`--profile --mesh-cache` for loading attribution and quality, and a proxy run
with the route off for the cost baseline.

## Checkpoint: live same-draw motion route (2026-09-12, superseded by the section above)

The proxy can now, behind the off-by-default `X3M_MOTION_OUTPUT=1` switch,
substitute the reviewed Argon SM3 pair with its motion variant during the main
scene, bind an owned RGBA32F motion target as RT1, supply the previous frame's
submitted WVP rows from a cross-frame history keyed by node/camera lifetime
serials plus buffer identity and draw range, and restore all touched state
after each draw. The motion target is filled with the invalid sentinel at the
frame's latching Clear, released before Reset and on device release, and read
back in requested capture frames as `motion_<device>_<frame>.rgba32f`. See the
[implementation section](architecture/live-motion-route.md) for the exact
hooked slots, gates, restoration list and failure behavior, and
[motion output verification](verification/motion-output.md).

Evidence at this checkpoint:

- Synthetic actual-DLL fixture: color bit-identical with the route off and on,
  39/39 full state restorations with zero differences, 44,284 motion pixels
  against a CPU oracle with 10,261 matched (max 0.0016 px UV, 3.7e-8 depth),
  Reset and shader recreation, sentinel-only mode without object scope, and
  application writes to the reserved constants restored.
- History key validation on the 24 iteration-5 gameplay frames: the full key
  matches 99.97% of keyable scene draws across 18 adjacent frame pairs with no
  in-frame duplicates; dropping buffer identity leaves 174 ambiguous sub-mesh
  splits. See [motion history key](reverse-engineering/motion-history-key.md).
- Shader coverage is archive-wide: all 3,480 effect files were parsed for
  their 817 VS/PS pairings, and the transformer table now holds 169 of the
  180 SM3 pairs (56 class A, 101 B, 12 C, including asteroid, moon and planet
  haze programs with spaced position quads). The 11 unsupported SM3 pairs are
  bloom quads and two compare-branch damage shaders. All 169 rows pass the
  structural fixture (1,551,936 mutations) and the GPU fixture with color
  identical. Of 466 SM2 pairs, 115 could host a ps_2_0 fragment and 267 a
  ps_2_x one; SM1 has no MRT. Pair lookup is a binary search over sorted
  index tables; the constant shadow captures every matrix window the table
  names. See [motion output profiles](reverse-engineering/motion-output-profiles.md),
  [material motion](verification/material-motion.md) and reviews
  [13](verification/review-13.md), [14](verification/review-14.md),
  [15](verification/review-15.md).
- **First gameplay run ([iteration 6](verification/iteration-06.md))**: across
  several sectors and a ship kill, 17,390 routed draws with 99.5% matched
  history, zero apply/restore/fill/Reset failures, lock time 0.34% of wall.
  All 1,022,880 valid readback pixels are explained by the stored row pairs
  at 0.149 px maximum error, and an independent origin cross-check agrees in
  sign on every axis (magnitude ratio 1.0006). The main scene is 92.7% SM3;
  sub-SM3 draws there are depth-only or blended, so no shader rewrite is
  needed. The selector rejected 47% of captured frames on unseen background
  pairs and null-PS depth passes; it is now structural (background = draws
  before the scene's depth-only Clear, null PS tolerated) and replays 68/68
  iteration-6 and 24/4 iteration-5 frames correctly. Load/registry epochs did
  not advance across sector changes, so the temporal design gained a
  displacement-based cut detector.
- **Temporal integration steps 1 and 2 are implemented in source
  ([design](architecture/temporal-integration.md)).** Step 1: every one of the
  169 rows also writes current device depth to an owned R32F third target
  (exact against ZFUNC-EQUAL replay over 7.76 M pixels, max z/w error
  2.6e-6), sentinel-filled with RT1; per-draw Halton jitter behind
  `X3M_MOTION_JITTER=1` applied as explicit row writes and restored bit-exactly
  (sign convention verified by a coverage oracle over 48,957 pixels; routed
  draws upload zero prior jitter because history rows are unjittered); a
  displacement/missing-key cut detector logs per frame; the readback analyzer
  compares previous depth against the prior frame's depth image (max error 0
  on fixtures). Step 2: `TemporalPass` accepts the 8-bit main surface through
  an FP16 point copy (bit-transparent within one FP16 ulp, no gamma), takes
  R32F depth directly without the decoder, derives the reactive mask from the
  depth sentinel, honors the cut flag, exposes the resolved surface for
  copy-back and survives Reset; 154 numerical checks and 158 state
  comparisons pass. Step 3: behind `X3M_TAA=1` the route runs the resolve in the
  StretchRect hook before the game's own bloom copy and copies the result back
  into the main target; the fixture proves the presented image equals a
  standalone reference resolve byte for byte, history frames accumulate
  exactly as scripted, and failures leave the main target untouched. Measured
  boundary cost (CPU-inclusive, Preview): 0.67 ms at 1280×768 and 2.04 ms at
  5120×1440. [Review 16](verification/review-16.md) fixed a fixed-function
  texture-stage leak into the resolve and added format-conversion gating;
  verdict GO for a gameplay run. The first TAA run showed stationary
  objects trembling and blurring: the resolve read history at the previous
  position plus the previous jitter, which is right only for a raw one-frame
  history, not the accumulated one. It now reads history at the previous
  unjittered position plus the current jitter; a stationary fixture shows
  0.000 px drift and bit-stable interiors across jitter phases where the old
  shader drifted 0.46 px, and three automated negative controls reproduce the
  regression. See [review 17](verification/review-17.md) and the
  [run analysis](verification/iteration-07.md): the resolved image moved by
  exactly the logged jitter each frame (0.013 px residual), 44% of
  high-frequency detail was lost, all 118 scene frames resolved and 99.8% of
  routed draws matched history. Scene time averaged 38.5 ms versus 16.1 ms in
  iteration 6, uncontrolled (different sectors) and unattributed: telemetry
  has no boundary metric yet, and the route's per-draw render-target
  switching is a candidate alongside the resolve chain. Known limitation:
  draws without a profile row (background, particles, effects) resolve
  current-only and show sub-pixel crawl; silhouette edges whose coverage flips
  stay current-only.
- The motion-output fixture now runs in 18 environments including the
  ownership wrapper, depth copy and admission, which the gameplay run needs.
  That coverage found and fixed a refcount defect that would have leaked the
  device under the wrapper. See [motion output](verification/motion-output.md).
- [Motion readback analyzer](verification/motion-readback.md) checks capture
  readbacks without geometry: integrity, static consistency, row-pair
  consistency (3,402/3,402 fixture pixels explained at 0.0018 px), displacement
  statistics and temporal cross-checks. The depth comparison stays unavailable
  until the route writes a depth image.
- [Temporal integration design](architecture/temporal-integration.md): resolve
  at the pre-bloom copy point, current depth from a third R32F target written by
  the variants, reactive coverage derived from its sentinel, per-draw explicit
  jitter because the game's state manager skips repeated uploads.
- [Constant upload disassembly](reverse-engineering/constant-uploads.md): all
  game shader/constant setters come from its two D3DX effect state managers in
  BeginPass; no game code writes the reserved constant ranges; the pure-device
  manager memoizes the last shader pointer, so restoring VS/PS after a routed
  draw is mandatory.
- [Review 12](verification/review-12.md) fixed a possible terminate in a
  noexcept readback path, hook installation for refused devices and heavy
  FP-state saving on every constant setter; a new static checker proves the
  light hook path reaches no x87 instruction. Existing suites still pass:
  26 ownership integration cases, the fallback link, material-motion structure
  and GPU fixtures, and 388 Python analysis tests.

This is CPU/synthetic evidence. The route has not run in the game, its per-draw
cost in gameplay is unmeasured, and no temporal consumer reads the output.

**Installed for the user-managed run (2026-09-12):** `build/d3d9.dll` from
commit `66d91a4`, SHA256
`fb08b324ea8ad6303ab40e346957c8fcfeeeb996a47c679cb12b6b721191b077`, through
`tools/manage.py install` with bottle configuration unchanged. The previous
iteration-5 DLL (`ed19a7ab…`) is preserved as
`artifacts/rollback/d3d9-iteration05.dll`. The route is off unless the launcher
passes `--motion-output`; see the run command in
[motion output](verification/motion-output.md). The installed build predates the
archive-wide table and the selector correction.

## Session handoff (2026-09-12, before orchestrator compaction; completed, kept for provenance)

Committed state: `398f00e` on `main`. Installed DLL: commit `162b2f7` build,
SHA256 `200aefff27e2e36d528c5ce02e1ea5720155ed525b66840d4c75053045af1da9`
(TAA with the corrected history convention). Rollbacks:
`artifacts/rollback/d3d9-iteration05.dll`; the motion-only build is commit `66d91a4`.

Gameplay evidence on disk (local, never committed): iteration-6 log
`/tmp/x3-iteration06-snapshot.log`; run A (first TAA, trembling)
`/tmp/x3-iteration07-snapshot.log`; rerun with the fix `/tmp/x3-iteration08/`
(log + 20 color/depth/motion/taa readbacks, five bursts; user still saw edge
tremble, thin-line shimmer, and "dotted" thin lines at distance which the
vanilla game also shows); Run B without TAA, same scene, `/tmp/x3-iteration08-runB/`
(user: FPS feels the same as with TAA).

Work in flight when the session was compacted, all uncommitted in the working
tree and owned by subagents that must not be duplicated (they report by
notification; if their reports were lost, inspect `git status`/`git diff`
and finish or rerun per the briefs summarized here):

1. **Rerun analysis (Opus)**: `tools/analysis/analyze_iteration08_taa.py`,
   `verification/results/iteration-08-taa-*`, `docs/verification/iteration-08.md`:
   tremble re-measured, flicker classified per pixel class (sentinel /
   routed interior / silhouette / thin features), blur, and a controlled
   timing comparison of the TAA run versus Run B.
2. **Resolve quality pass (Fable)**: `src/temporal/resolve.hlsl` and fixtures:
   variance clipping instead of hard depth rejection, closest-depth dilated
   motion, Catmull-Rom history, thin-line and silhouette fixture cases;
   regenerated `temporal_resolve_program*`; docs.
3. **Telemetry + lazy MRT binding (Fable)**: `src/proxy/telemetry.*`,
   `src/proxy/motion_output.*`, `run_motion_output.py`: per-frame cost metrics
   (route apply/undo, jitter writes, fills, resolve split, copy-back,
   readbacks, native StretchRect) and `X3M_MOTION_RT_MODE=lazy` with an
   equivalence fixture; A/B run procedure in `docs/verification/motion-output.md`.

Then: combined review (review 18) on the final tree with the full suite chain
(one Wine runner at a time), status update, commit, build + `tools/manage.py
install`, and the next user run: TAA on, then proxy with route off
(`launch --direct --telemetry` only) for the route-cost baseline. After that:
camera reprojection for non-routed pixels (shadow view-inverse rows c34–36,
recover projection) so background and effects stop crawling when turning.

## Session handoff (2026-09-12 evening, before orchestrator compaction)

Committed: `4e1aa20` (analyses). Installed in BOTH bottles: commit `1d36c29`
build, SHA-256 `db63e120…` (HDR stages 1–2, hook off by default). The game now
lives in the CrossOver bottle **X3** (arm64 Wine + FEX, `FEX_X87REDUCEDPRECISION=1`,
`WINEMSYNC=1`); the old x86_64/Rosetta bottle **Steam** still exists.
`tools/manage.py` defaults to X3 (`X3M_BOTTLE`/`--bottle`/`--game-dir`
override) — that edit is uncommitted in the tree together with the in-flight
work below.

Today's runs (snapshots under /tmp, never committed): old bottle
`/tmp/x3-iteration09-run1..4/` (run 1 route+profile, run 2 +hook +cache
+taa-debug, run 3 route off, run 4 +hdr identity); new bottle
`/tmp/x3-bottleX3-run5/` (route off) and `/tmp/x3-bottleX3-run6/` (route +
profile + hook; path: other save → menu → new game → dock → usual flight).
Findings so far: [iteration 9](verification/iteration-09.md),
[run 2](verification/iteration-09-run2.md), [run 4](verification/iteration-09-run4.md),
[cost](verification/iteration-09-cost.md), [route cost](verification/route-cost-run1.md),
[loading profile](reverse-engineering/loading-profile-run1.md). New bottle,
route off: menu 9.7 s, save 65.5 s (39.5 s unexplained), sector 8–9 s.

In flight (uncommitted, owned by agents; if their reports were lost, inspect
`git status`/`git diff` and finish per these briefs):

1. **Review 24** of HDR stage 3 (TAA on the FP16 target, `k` luminance
   weighting, `X3M_TAA_K`): src/renderer/temporal_pass.*, src/temporal/resolve.*,
   hdr_pass.*, motion_output.*, review-24.md exists. Then commit + install.
2. **Fast exact-match GenerateAdjacency** (`X3M_MESH_ADJACENCY=native|verify|fast`,
   `--mesh-adjacency`): src/proxy/mesh_adjacency_fast.*, loading_trace.*,
   mesh_adjacency_cache.* (also widening the cache's FP-state gate that
   bypassed 100% of calls), fixtures, docs/verification/mesh-adjacency-fast.md.
3. **Route-cost fix**: src/proxy/engine_memory.* (direct validated reads
   instead of ReadProcessMemory), object_trace/object_lifetime, telemetry
   per-draw stamps behind `X3M_TELEMETRY_DRAW`, fallback `X3M_ENGINE_READS=rpm`.
4. **Fixture bottle switch**: verification/probe/bottle.py
   (`X3M_FIXTURE_BOTTLE`, default Steam), all runners switched; validation of
   key suites under the X3 bottle into verification/results/bottle-x3/ and
   docs/verification/bottles.md.
5. **Run-6 analyses** (docs only): loading profile on the new bottle
   (docs/reverse-engineering/loading-profile-bottle-x3.md) and route/TAA/FEX
   health + frame time (docs/verification/iteration-10.md).

Then: review the three source items together (review 25) with the full suite
chain (one Wine runner), commit, build, install into the X3 bottle; next user
runs on the new bottle: `--mesh-adjacency verify` (parity), then `fast`
(loading), then `--hdr --hdr-tonemap agx` (first tonemapped look, TAA on HDR),
plus the sharpen/mip-bias pass: post-resolve contrast-adaptive sharpen (target
MTF50 0.35 → 0.6 c/px) and MIPMAPLODBIAS instrumentation then −0.5 on routed
mip-mapped stages; make `--scene-hook` the default resolve point.

## Session handoff (2026-09-12 night, second account switch pause)

State: main at the commit recorded in git log ("WIP checkpoint before second
account switch"); installed in bottle X3 = review-26 build `8864bff0…`
(commit c782a5a: sharpen, mip bias, scene hook default on with the route).

Game runs today on X3 (snapshots under /tmp only): run 7 `/tmp/x3-bottleX3-run7/`
(plain `--direct`, no stamps; user stopwatch ≈40 s save load), run 8
`/tmp/x3-bottleX3-run8/` (`--telemetry --mesh-adjacency verify --gz-buffer`;
[loading-x3-run8.md](verification/loading-x3-run8.md): save 65 → 33 s, gz
buffer covers the whole savegame, `inflate` 774k calls/12 s is now the largest
hooked item; adjacency verify **167/7,715 meshes mismatch**, tie-break
related — fast mode blocked), run 9 `/tmp/x3-bottleX3-run9/` (route + TAA +
hook + gz buffer; [iteration-11.md](verification/iteration-11.md): route-on
frame 16.9 → 12.1 ms with direct engine reads, hook 162/162 agree, 0
resyncs, TAA 99.83% history match, loads menu 8.4 / save 35.7 / sector 5.9 s),
run 10 `/tmp/x3-bottleX3-run10/` (review-26 build, `--taa-sharpen 0.5
--taa-mip-bias -0.5 --taa-debug`, 3 bursts × 4 frames: both features active on
all 181 routed frames, mip bias applied on 34,625 draws with matching
restores, the game's effects write bias 0 on cube stages 3/4 and the override
handles it; analysis in flight → iteration-12.md).

Branches at the pause (since merged: the loading branch as `c152860` after
review 27, the readback branch as `0a32f33`; integration in review 29):
`worktree-agent-a68201431d638fe21`
(loading: light no-SSE hooks, probe batch 2 with 12 byte-verified
trampolines, gated fast resource reader + catalogue handle pool, frame_end
`elapsed_ms`; review 27 in progress in that worktree, see review-27.md),
`worktree-agent-a0100a8066b313223` (native-Windows fixes D1/D2/D3/W1/W3 +
engine_memory summary line + FEX NaN-bits fixture printing; see
handoff-windows-fixes.md). Main-tree uncommitted-at-pause work: adjacency
parity fix from the D3DX disassembly (mesh_adjacency_fast.*, reference,
tests, tools/analysis/replay_mesh_adjacency.py, `--mesh-adjacency-dump`; see
handoff-adjacency-parity.md and docs/reverse-engineering/d3dx-generate-adjacency.md).

Resume order: (1) finish review 27 in its worktree, merge into main, review
fixes, install; (2) finish the Windows-fixes branch, review 28, merge,
install; (3) finish adjacency parity (fixture parity against real D3DX on the
reproduced cases), then a user `--mesh-adjacency verify` run must show
`verify_mismatched=0` before `fast`; (4) read iteration-12.md for the sharpen /
mip-bias verdict and give the user the A/B screenshot pair; (5) next user
runs: `--telemetry --loading-probes` (stall decomposition), `--resource-read
verify --dat-handles` then `fast`, `--hdr --hdr-tonemap` (first tonemapped
look), and a `--vanilla` launch to A/B the double cursor on this bottle.

## Session handoff (2026-09-12 late evening, account switch pause)

State at the pause: the orchestrator asked every running agent to stop at a
safe point and write a handoff note; the tree was then committed as a WIP
checkpoint (see git log). Note that the earlier handoff commit `a8d4309`
already swept the in-flight source edits of items 1–4 above into history
under its "handoff" message; the review-25 commit finishes them.

Completed since the evening handoff:

- **Review 24 (HDR stage 3) passed** — [review-24.md](verification/review-24.md).
  Findings fixed: negative-luma weight poisoning the 3×3 stats (luma floored at
  0 both ways, resolve program 4,487 words), `X3M_TAA_K` parse check,
  identity twins assert `k=0`, runner exports `X3M_TELEMETRY_DRAW=1`. Suites:
  motion-output 90 runs PASS, temporal-pass 448/204/386, temporal_run 78/78,
  ownership 26 runs, scene-capture 4,908 checks, no-x87 0 violations, unittest
  674/675 (one import error from the in-flight `bottle` module). Bench: HDR
  TAA 1.935 ms vs 8-bit 2.317 ms at 5120×1440. Verdict: go, `--taa-k` unset
  by default; not yet seen in game.
- **New-bottle loading profile** —
  [loading-profile-bottle-x3.md](reverse-engineering/loading-profile-bottle-x3.md).
  FEX removed the adjacency bottleneck (GenerateAdjacency 69.15 s → 3.13 s per
  run; the fast replacement is now a worst-case-tail fix, not the loading
  fix). Save load on X3 = 65.5 s: a 25.3 s savegame-decode stall (13.9 M
  `gzread` calls of ~3 bytes, 9.1 s in zlib + 2.6 s inflate; engine loop
  0x004e9210) and a 25.0 s script/XML stall in the CRT per-file read path
  (1,376 files; unhooked, needs a targeted probe). `inflate` 12.2 s per run,
  `D3DXCreateMesh` 5.1 s, `gzread` per-call cost rose 0.20 → 0.66 µs under
  FEX. The FEX profiler returns a constant bogus `Eip` (0x10000): hook timings
  are the measurement, frame chains only a hint.

In flight at the pause (each wrote `docs/verification/handoff-*.md` or its
target doc with TODO marks; resume from those, do not restart from scratch):

1. Fast exact-match GenerateAdjacency — `handoff-mesh-adjacency-fast.md`.
2. Route-cost fix (direct engine reads) — `handoff-engine-reads.md`.
3. Fixture bottle switch + FEX validation — `handoff-bottle-switch.md`.
4. Run-6 route/TAA/FEX health — `docs/verification/iteration-10.md`.
5. **gz read-ahead buffer** (`X3M_GZ_BUFFER=1`, `--gz-buffer`; new
   src/proxy/gz_buffer.*; Ghidra of 0x004e9210 into
   docs/reverse-engineering/savegame-gz-stream.md) — `handoff-gz-buffer.md`.
6. Sampler-state/mip disassembly for the −0.5 mip bias —
   docs/reverse-engineering/sampler-states-and-mips.md.
7. Native-Windows portability audit (read-only) —
   docs/architecture/native-windows-audit-2026-09-12.md.

Then, unchanged: review 25 of items 1, 2 and 5 with the full suite chain,
commit, build, install into X3, next user runs (`--mesh-adjacency verify`,
`fast`, `--hdr --hdr-tonemap agx`, and `--gz-buffer` once its fixture passes),
then post-resolve sharpen (RCAS-style, never fed to history, applied after
tonemap on the HDR path), mip-bias instrumentation, and the scene hook as the
default resolve point.

## Historical user-run plan (2026-09-12 night; superseded by September 13 handoff section 5)

All runs on the X3 bottle with the installed build (see "Installed" below;
the currently installed review-26 build `8864bff0…` predates the loading
branch, the adjacency parity fix and the present readback — install the
review-29 build first, hash in [review-29.md](verification/review-29.md),
otherwise runs 2, 4, 6 and 7 cannot show the new lines);
logs land in the game's `x3-modern-captures` folder as
`session-<date>-<pid>.log`. Tell the orchestrator after each run; it snapshots
the log to /tmp and analyses it. Same save and flight path as the earlier runs.

1. **Run 1 — plain baseline (no telemetry)**: `python3 tools/manage.py launch
   --direct`. Menu → load the save → fly → sector change → exit. Purpose: the
   real X3 load times without hook overhead (the log's timestamps around the
   loads are enough).
2. **Run 2 — adjacency parity + gz gate**: `python3 tools/manage.py launch
   --direct --telemetry --mesh-adjacency verify --gz-buffer`. Acceptance:
   `verify_mismatched=0` in the mesh-adjacency summary and
   `gz_buffer requested=1 enabled=1 imports=1` plus `gz_buffer_file` lines.
3. **Run 3 — fast loading + engine reads**: `python3 tools/manage.py launch
   --direct --ownership --object-trace --object-lifetime --motion-output --taa
   --telemetry --mesh-adjacency fast --gz-buffer` (the scene hook is on by
   default with `--motion-output` since review 26). Compare load
   times with run 1 and the route cost with iteration 10 (`gate_us` needs
   `X3M_TELEMETRY_DRAW=1`, off by default; add it only if the per-draw
   attribution is wanted, it costs QPC per draw).
4. **Run 4 — TAA sharpness** (review-26 build installed): `python3
   tools/manage.py launch --direct --ownership --object-trace --object-lifetime
   --motion-output --taa --taa-debug --telemetry --gz-buffer --taa-sharpen 0.5
   --taa-mip-bias -0.5 --capture-start 999999 --capture-frames 4` (the scene
   hook is now on by default with the route). Look for over-sharpening halos,
   texture shimmer on distant hulls (the mip bias) and fill-rate cost; take
   three 4-frame capture bursts (stationary, turning, moving) for the
   MTF50/gradient comparison against iteration 9 run 2 and the per-pixel
   motion certification that run 3 lacked.
5. **Run 5 — first tonemapped look**: run 4 plus `--hdr --hdr-tonemap`
   (optionally `--hdr-look golden`, `--hdr-ev -1`). Report what looks wrong;
   the orchestrator reads the `hdr_frame` ev/luma fields and `hdr_tonemap`.
6. **Run 6 — loading stall decomposition**: `python3 tools/manage.py launch
   --direct --telemetry --loading-probes` ([loading-probes.md](verification/loading-probes.md));
   `loading_probe_site … status=late_claim` in the log would mean an
   install-order regression.
7. **Run 7 — resource reader**: `python3 tools/manage.py launch --direct
   --telemetry --resource-read verify --dat-handles`
   ([resource-reader.md](verification/resource-reader.md)); `fast` only after
   verify reports no difference.

* 2026-09-13 (adjacency parity, uncommitted, paused): run 11 (bottle X3, review-29
  install) reported `verify_mismatched=37` of 7,636; the 37 dumps replay offline and
  the cause is D3DX's `D3DXVec3Normalize` dispatch: FEX's Wine reports 3DNow through
  `IsProcessorFeaturePresent(7)` without the CPUID bit, so d3dx9_37 keeps its generic
  table-interpolation normalize on X3 while the module used `rsqrtss`. The module now
  reproduces both (`Policy::normalize`, the service mirrors the dispatch): replay of the
  37 dumps is 37/37 on X3; Steam keeps a 4/37 Rosetta-only residual under study. Suites
  not rerun yet; fast mode stays blocked until the next in-game verify run shows
  `verify_mismatched=0`. See [handoff-adjacency-parity.md](verification/handoff-adjacency-parity.md).

## Concrete next work

1. Verify camera reprojection on the next run: `analyze_camera_state.py`
   against the shadowed constant rows, policy/cut distributions, and the
   `selector_state=9` (environment-map) frequency.
2. User runs with the next installed build: TAA on with
   `--telemetry --profile --mesh-cache` (loading attribution, adjacency-cache
   hit rate, edge/thin-feature quality), then the same scene with
   `launch --direct --telemetry` only (route-cost baseline).
3. From the profile: attribute the unexplained loading seconds to engine
   functions with `X3ProfileSymbols.java`, then choose between the negative
   lookup cache, adjacency replacement, catalogue handle retention and engine
   patches per [loading orchestration](reverse-engineering/loading-orchestration.md).
4. Done in iteration 10 and review 26: the scene-end hook is confirmed in
   gameplay (214/214 agree, `draws_after_hook` max 0) and is the default
   resolve point (`--scene-hook off` restores the copy/selector boundary).
5. HDR stage 3 (TAA on HDR with luminance weighting), stage 4 (radiance
   clamp removal), stage 5 (HDR bloom) per the
   [design](architecture/hdr-scene-path.md); re-measure the 5120×1440 stage-2
   cost; continue the roadmap.
6. Loading-time gap and alt-tab cursor remain tracked.
7. **Crypt cache (2026-09-13, merged, not yet installed):** run B's stall-B
   decomposition put 12.835 of 16.758 s in the script signature check
   `0x004cabc0`, 10.346 s of it in the three `CryptAcquireContextA` container
   delete/create/delete calls per script (4.086 ms each). Decompiled in
   [script-signature-check.md](reverse-engineering/script-signature-check.md);
   `X3M_CRYPT_CACHE=1` / `launch --crypt-cache` (`src/proxy/crypt_cache.cpp`,
   no-SSE unit, no telemetry needed) caches the provider handle and the
   imported RSA-2048 key, emulates the two ignored deletes with the recorded
   `NTE_BAD_KEYSET` and leaves hash/verify to the CSP. Review corrections qualify
   six game call returns and all four lifecycle imports, reject overlapping key
   reuse/stale native publication, and remove unsafe detach-time CSP cleanup.
   The bounded provider/key cache lasts until process exit; its named scratch
   container can persist until the next startup delete. Corrected-source tests
   pass 572 checks on each bottle; the 200-sequence real-CSP differential measures
   approximately 4.0× (Steam) / 4.14× (X3) per check, excluding the probe envelope
   ([crypt-cache.md](verification/crypt-cache.md)). The 11–13 s loading estimate
   remains a hypothesis for the user's reviewed `--crypt-cache` run.
8. **Chase camera (2026-09-13, worktree branch, reviewed, not run):**
   `X3M_CAMERA=chase` / `launch --camera chase` replaces the external back
   view with a critically damped follow camera written into the engine's
   sector camera at the byte-verified cockpit-update site `0x00420e06`
   ([design](architecture/chase-camera.md), [study](reverse-engineering/external-camera.md),
   [review 31a](verification/review-31-chase-camera-architecture.md) /
   [31b](verification/review-31-chase-camera-implementation.md) applied):
   scene, HUD overlay and the mouse-aim ray (which uses cockpit `+0xf0`, also
   rewritten) stay one camera. Review fixes: the handler acts only on the
   active control cockpit (`+0x10 == +0xc`), verbatim-basis frames (connect 3,
   `+0x1a0 & 4`) pass through, the site is kept for the process lifetime,
   sector snaps after a teleport are coalesced (one TAA cut), back-view
   hysteresis, defaults `rot_tau` 0.15 / clamp 8° / `pos_tau` 0.20 /
   `pos_lag_clamp` 0.10, tunables renamed `X3M_CHASE_*` (`--chase-*`), combat
   tightness behind `X3M_CHASE_COMBAT_TIGHTNESS` on the `+0x1e4` tracking
   state (unverified), optional `X3M_CHASE_SCENE_FIX`. Host tests 32 + 8 pass,
   DLL builds clean, `check_no_x87.py` PASS, default runs patch nothing. First
   user run (`launch --direct --camera chase`): the acceptance list in review
   31a — install line, the `first_applied` inferences, `applied` growing with
   `refused`/`refused_inactive` flat and `cockpits_seen` stable in the back
   view, HUD brackets and aim consistency, one `camera_cut` per gate jump
   (`coalesced` +1), vanilla behaviour in the other views, chase kept across a
   resolution change, `tracking`/`locked` plausible with a target selected.

## Replay/admission line (reference only, superseded 2026-09-12)


The detached [same-draw material prototype](verification/material-motion.md)
now writes color and motion correspondence together for one common opaque SM3
pair. Its 82 configurations pass 1,182 checks, 2,952 numerical motion samples
and 164 bilateral depth cases. All compared color components are unchanged, and
motion matches the independent replay reference exactly. At 5120×1440, the
small synthetic workload averages 1.60 ms for one draw versus 2.24 ms for two
passes, including submission and completion; this is not a game FPS result.
Both the game's A8R8G8B8 color format plus RGBA32F motion and an equal-format
control pass. Host optimized/ASan/UBSan checks also preserve the original
programs and reject 57,152 input mutations. The transformer is **not linked into
the proxy or installed**. Live history, binding and broader material coverage
remain next work; see the [module contract](architecture/material-motion-prototype.md).

The portable geometry path removes DLL-version allowlists, private Wine buffer
layouts and native method RVAs. Eligible managed WRITEONLY buffers receive
readable native backing, preserving application-visible Usage and observing only
existing Lock/Unlock uploads. The loading cache likewise replaces DLL fingerprints
and private method addresses with public COM contracts. The
[dependency audit](architecture/runtime-dependencies.md) documents the proxy
mechanisms and remaining platform gaps. Game EXE/DLL patches, private structures
and disassembly remain explicitly allowed.

The reviewed bounded sidecar index removes the linear allocation-list lookup.
In the same synthetic Preview benchmark, acquiring and inspecting 700
many-buffer leases fell from 11.407 ms to 2.537 ms combined; the shared-buffer
control remained near 2.44 ms. The index passes 552 observer checks, 421 geometry
checks and 84 benchmark samples. This is CPU evidence, not game FPS
or proof that complete live replay is affordable. See
[sidecar index verification](verification/finite-sidecar-index.md),
[performance measurements](verification/geometry-performance.md) and
[review 10](verification/review-10.md). The installed DLL remains unchanged.

The [application admission core](verification/application-admission.md)
passes 4,865 checks in each of four standalone builds, including ASan/UBSan,
ThreadSanitizer and a CPU-only x86 Preview run. Independent review accepted its
root counting, nesting, permanent vetoes and nonblocking replay promotion.
The standalone x86 [ABI adapter](architecture/application-admission-abi.md)
passed 130 CPU-state/behavior checks and 21 timing samples at `127c3da`. Its emitted
code removes compiler exception bookends from the adapter. CMake disables
exceptions for that source file; generated ownership entry definitions use a
separate scoped option, while handwritten helpers retain exception handling.
The disabled adapter path makes no runtime calls.
Both modules are now linked into production behind the off-by-default
`X3M_ADMISSION=1` option. All 297 generated ownership entries, eleven loader
exports, thirty capture bodies, sixteen loading IAT roots and twenty-four bounded
mesh thunks enter the same process monitor before their work. The
[ownership fixture](verification/ownership-admission.md) passes 147 checks in
twelve modes, including actual callback registration and final child/parent
release. [Process configuration](verification/process-admission.md) preserves
CPU state and publishes one immutable mode. These are ordinary entry boundaries;
complete callback/window coverage, trusted native-helper authority, mapping
validation and the exclusive GPU segment still gate live replay. See
[proxy integration](verification/proxy-application-admission.md). The
[game callback disassembly](reverse-engineering/game-callback-registration.md)
identifies D3DX device routes, effect-state callbacks and window-message hazards.

The [actual-DLL cost comparison](verification/hook-admission-performance.md)
passes seven cases and 196 timing samples. Admission adds about 134–154 ns to
the tested single-boundary calls and 285–316 ns to capture-plus-ownership calls.
These are synthetic CPU timings including their native operation, not replay
GPU cost or game frame time. Loading/cache verification passes sixteen explicit
off/on cases with balanced admission retirement. See
[review 11](verification/review-11.md).

A private motion producer now connects main-scene draw observations to
bounded native geometry leases, CPU storage correspondence and an actual
pre-Clear GPU replay. It releases the motion target and replay resources within
the boundary callback. Its synthetic integration passes, but **live GPU dispatch
is refused until buffer-write/replay exclusion is implemented**: the capture
mutex alone does not serialize worker VB/IB mappings. Only a successful Clear, surviving scene selection and
successful Present can commit CPU matrix history. Camera-cut continuity and
complete scene-color coverage remain explicitly unknown; no temporal-color
consumer is enabled. See [motion capture](verification/motion-capture.md),
[geometry leases](verification/geometry-leases.md),
[execution scopes](verification/execution-state.md) and
[review 7](verification/review-07.md). The combined DLL and forced native fallback verification pass;
the installed iteration-5 DLL remains unchanged.

The finite-position source path includes the reviewed compact classification
core, public managed VB/IB descriptors and allocation-owned upload
observer. It obtains finite XYZ and actual index bounds from existing writes,
with no extra buffer Lock or game-pixel readback. The new reader can acquire a
native geometry lease while the actual getter references remain alive, then
revalidate the immutable requests at replay. See
[finite upload evidence](verification/finite-upload-observer.md) and
[draw inputs](verification/draw-input.md).

CPU correspondence now distinguishes diagnostic storage pairs from temporal
continuity; its 3,404 checks pass. The embedded motion PS is compiled from our
original HLSL and needs no runtime compiler. Detached numerical verification
also feeds the production temporal resolve, but the live diagnostic output is
not consumed by that resolve. See [motion history](verification/motion-history.md)
and [GPU motion](verification/rigid-motion.md).

The [archive position review](reverse-engineering/archive-position-paths.md)
accounts for all 256 VS: 234 homogeneous row-dot paths, 18 direct-clip bloom paths,
two direct-position GUI/effect paths and two particle billboards. The production
registry now contains all 234 row-dot programs, with separate lookup for the other
22 VS and positive-only coverage profiles for 494 PS. One malformed PS is excluded.
The reviewed registry passes 751 actual-program lookups and rejects 547,927
single-word mutation controls. The other five captured
programs require explicit separate handling; they are not excluded from final
TAA/composition scope. Particle RGB blending and missing prior particle identity
are documented in [particle inputs](reverse-engineering/particle-motion-inputs.md).
The full Python analysis suite passes **289 tests**.
The post-install source registry also retains original shader model, constructor
and position-write order, independently verified for all 234 row-dot profiles.
The detached rigid-motion pass now creates the reviewed fixed SM3 replay program
internally and requires a cached exact-source qualification token plus an explicit
finite-position attestation. The 32-profile program passes 1,573,392 covered
component comparisons and 134 bilateral raster/depth cases; independent review
checks its evidence limits. These source changes have not replaced the installed
iteration-5 DLL.

The [live draw-input reader](verification/draw-input.md) passes 260 checks,
74 caller-state comparisons and seven failed-getter controls. It reads exact
submitted rows and actual layouts/revisions, distinguishes nonindexed draws from
an unused bound IB, and keeps lifetime, source qualification and finite-payload
gates independent. Proxy
capture wiring now records these inputs and composes lifetime evidence around the
native draw. The combined fixture checks record scope and failed submission gates.

[Reactive history](verification/reactive-history.md) now owns current/prior mask
snapshots and rejects contaminated RGB independently of alpha. Actual synthetic
particle birth, disappearance, movement, reordering and occlusion pass, along with
policy/reset/failure handling: 98 numeric checks and 102 state comparisons. The
58-sample resolve and 102-sample rigid-motion regressions still pass. Producing
these masks for live game draws remains unfinished.

[Lifecycle disassembly](reverse-engineering/object-lifetimes.md) now covers the
reviewed central insertion, removal, bulk destruction and renderer-load paths.
The opt-in [observer](verification/object-lifetime-observer.md) passes 533 checks
and 72 original backend calls, including baseline adoption, reuse, foreign
unwind, hook ownership loss and retirement. Six runner-provenance tests pass.
The completed iteration-5 run verifies consistent lifetimes for all 12,753 scoped
draws out of 12,957 successful draws. The observer started without an installation
baseline, then obtained useful identities from observed insertions. Load epoch
1→2 distinguishes 17 handles reused with different storage and serials.
Camera cuts remain a separate policy; see [live lifetime evidence](reverse-engineering/iteration05-lifetimes.md).

The installed iteration-5 DLL passed all 15 integration cases and the forced native
fallback with its 15 compiled proxy/renderer objects. Its installed hash is:
SHA256 `ed19a7abf54ae2b9debf912f3d343a0c9217038a2162cb6a9eb174fc8050bbd5`.
The [installation record](../verification/results/iteration-05-install.json) verifies
unchanged game EXE and bottle configuration. The prior 0.4 DLL is preserved in
`artifacts/rollback/d3d9-iteration04.dll`; the older 0.3 rollback also remains intact.
The latest combined source DLL passes 26 actual-DLL integration cases, including
admission off/on, native escape vetoes, final transaction retirement and 24 native
Clear CPU-state witnesses. Unsafe live motion dispatch remains explicitly refused.
Its SHA256 is
`5a5f8a78d7c9a802d844368c7a68572c009edd1272b03e8dab306e6bcda39007`; it is
**not installed**. The production link contains 23 objects; the 20-object forced
native fallback also passes. See [review 11](verification/review-11.md).
The earlier `aa61e7ff` build is retained for the hook-cost comparison and described
in [review 10](verification/review-10.md). Earlier source evidence remains in
[review 8](verification/review-08.md) at `5196f31`,
[review 7](verification/review-07.md) at `0ce0814`,
[review 6](verification/review-06.md) at `c4f3d45` and
[review 5](verification/review-05.md) at `437e95b`. Shared result paths now refer
to the latest verified source; historical commits preserve their prior reports.

The [detached adjacency cache](verification/mesh-adjacency-cache.md) passes
741 checks using real native mesh acquisition, exact byte keys, bounded storage,
native downstream cleaning/optimization and computational FP-state parity.
Repeated original synthetic meshes show a large hit-time reduction including
acquisition/lookup cost. The iteration-5 run recorded 7,199 gate rejections and
no cache calls or hits.
Static analysis identifies dynamic SYSTEMMEM mesh options excluded by the installed
gate; the portable four-option implementation now passes 12,905 actual
native/wrapped checks, with 75 + 123 loading regressions and independent review.
DLL fingerprints/private method RVAs are removed, and incoming LastError is part
of the exact cache key.
It is a source change, not an installed game speedup.
The cache remains behind an off-by-default switch. Independently reviewed
[native/wrapped hook integration](verification/mesh-cache-hook.md) exercises
all four game option variants across six cases.
Known wrapper state stays truthful to the actual cache/native lock path; existing
uncertainty never becomes known through a cache hit. Acquisition
cleanup failure explicitly disables cache admission and rejects only subsequent
preparation calls intercepted by our hooks; restart is required. It never claims
to repair the native lock or contain calls outside those hooks.

The user-run 0.4 session has 20 complete captured frames and 13,431 successful
draws, including a final third-person burst. The then-installed selector rejected all
frames and attempted no depth copy: planet haze was unnecessarily mandatory,
and later ColorFill invalidation masked the first cause. Source corrections
remove the haze requirement while retaining verified background/binding rules,
check scratch-fill targets, and preserve the first rejection. The installed
corrections now report successful pre-clear copies and confirmed
boundaries in all 24 captured iteration-5 gameplay frames, including the different
planet save; the four menu frames remain rejected. This is live copy/epoch/boundary
evidence, not numerical readback of the game depth texture. The
[depth/motion audit](reverse-engineering/iteration05-depth-motion.md) finds 7,202
gameplay draws pass the current local input checks, all before the selected Clear;
finite vertex payload, replay stability and complete scene coverage remain unproved.

New [camera/object evidence](reverse-engineering/iteration04-camera-motion.md)
shows independent object motion with a stationary camera; camera-only history is
insufficient. Four changing unscoped vertex buffers require separate handling.
[Active one-/two-light inputs](reverse-engineering/iteration04-lights.md) are now
observed. [Loading analysis](reverse-engineering/iteration04-loading.md) finds
21.287 seconds of adjacency work and recurring activity, without proving exact
mesh reuse or explaining the entire 87-second presentation gap.

The [full shader sweep](reverse-engineering/shader-sweep.md) covers 751 programs
across all 3,480 effect files; all disassemble and all 57 runtime-dumped programs
match archive bytes. Static review retains unknowns and does not prove runtime
coverage. A detached, reviewed material transformer preserves HDR RGB for five
exact profiles; 42 structural checks and 192 GPU samples pass. It remains
disconnected from game rendering and needs the FP16 scene path.

Our arithmetic uses SSE2 with explicit four-byte incoming stack alignment;
[ABI verification](verification/sse2-abi.md) and object/temporal/material
regressions pass. This leaves ABI-required ST0 transfers intact.
[HDR transfer investigation](architecture/hdr-transfer.md) rejects stock
WineD3D-to-DXMT shared handles as a pixel-sharing route, while a native FP16
IOSurface GPU proof preserves values above one. Wine integration, EDR
presentation and the requested visual features remain unfinished.

## Completed

- Reversible app-local 32-bit D3D9 proxy, loader/export forwarding, identity-preserving
  per-object interception, bounded capture, F8 trigger, shader dumps and hashes.
- Preview-only installer/launcher/rollback tooling; EXE, archives and bottle
  configuration preserved. User test setting: 1280×768 windowed (was 5120×1440
  borderless). No unused launcher is intentionally left open.
- Independent FP16/depth/D3D11-scRGB capabilities; baseline/proxy smoke passes;
  87 analysis tests and compile-time ABI guards pass.
- Static archive/PE analysis, targeted Ghidra renderer map, shader index and CTAB
  register mapping, documented separately in `docs/reverse-engineering/`.
- Animated menu capture: 690 draws; user-assisted flight: two complete 122-draw
  frames. Exact archive matches for all 47 shaders recorded in flight session.

## Iteration 2 additions

- Capture v2: typed I/B/F state, per-device frame keys, resource allocation IDs,
  stream/index metadata, draw parameters/results, and texture/surface relationship.
  Actual synthetic traces verify stateblock restoration and resource recreation.
- Camera factorization: 105 draws/frame fit the same multiplication convention;
  three camera coordinate regimes prohibit a blanket single-camera assumption.
- INTZ numeric rendering/sampling: 16 pixel checks pass across 8-bit/FP16 outputs
  and reset. This is sampleable synthetic depth, not game depth substitution.
- Common shader point-light array decoded as eight pos/color/atten structures.
- User turning capture: eight complete frames, 762 successful draws, 45/45 shader
  matches. Camera convention holds through all six adjacent turns (max residual
  2.67e-7). Unique resource/range candidates include changed world transforms;
  repeated keys and reordered draws prohibit naive object matching.
- All 550 named point-light shader-stage observations have explicit count zero;
  stale float array entries are not active light evidence. See turning-camera.md
  and turning-lights.md in reverse-engineering.
- Baseline lifetime probe proves persistent native resources can prevent device
  teardown. Canonical logical COM ownership is required before depth/history.
- User reports double cursor after alt-tab and requests loading-time investigation.
  Both are tracked; flip presentation has not been shown to fix cursor behavior.

- Instruction inspection of all 11 observed vertex shaders confirms direct matrix
  position paths without positional shader animation. Five material pixel shaders
  clamp vertex lighting/emissive RGB before the target, so FP16 alone is insufficient.
  See `docs/reverse-engineering/position-shaders.md`.

### Superseded next-work list (2026-09-11)

1. Prepare limited live routing for the verified
   [motion output alongside color](architecture/motion-output-strategy.md).
   Connect existing object/transform history, define shader/constants/MRT binding
   ownership and failure handling, and invalidate motion for unsupported
   contributors. Broaden profiles only with their own register/coverage review.
   Replay remains a reference/fallback candidate; no motion route is enabled in
   gameplay yet.
2. Consolidate the next user-managed diagnostic run around the chosen motion
   route, history/coverage and loading observations. Include the existing finite
   POSITION capture only where required by retained replay or geometry work;
   same-draw shader output does not reread saved geometry. The game's dynamic
   SYSTEMMEM mesh configuration now passes native tests; its actual cache hit rate
   still needs a future run.
3. If replay remains part of the live renderer, establish explicit buffer-write/replay exclusion before enabling the private
   GPU motion diagnostic on live finite uploads. Complete
   camera-cut policy and scene-color/reactive masks before consuming its output
   in temporal resolve. Account separately for CPU-changing particles/stardust
   and overlays; storage correspondence alone does not prove temporal continuity.
4. Validate live jitter placement and position/raster equivalence using the full
   archive registry, then connect matched color/depth/motion inputs to temporal resolve.
   Camera-only reprojection cannot satisfy the observed scene; TAA remains required.
5. Establish the FP16 scene path and enable only reviewed material variants there.
   Integrate a GPU-native HDR presentation route; output conversion of clipped
   8-bit color is insufficient. Continue all remaining roadmap features.
6. Measure exact mesh-key reuse and real acquisition/lookup cost before enabling
   bounded adjacency caching. Investigate the still-unattributed loading gap.
   Revisit native/game cursor behavior with presentation changes.

## Test coordination

The user requested notification for future launches that need more than the menu,
and will handle launching/loading gameplay scenes. Do not repeat autonomous full
game launch attempts. Close any unneeded launcher immediately. Current game can
be left to the user; it contains their chosen test scene. F8 writes captures under
`X3/x3-modern-captures/`. Detailed capture causes a diagnostic hitch by design.

## Useful artifacts

- `verification/results/game-flight-capture-summary.json`
- `verification/results/game-menu-capture-summary.json`
- `verification/results/shader-registers.json`
- `verification/results/graphics-capabilities.txt`
- `verification/results/d3d9-smoke-proxy.txt`
- `docs/verification/iteration-01.md`

Raw shader bytes remain local beside X3. The large generated archive shader index
was `/tmp/x3-shader-index.json`; regenerate with `tools/analysis/index_shaders.py`
if missing. Raw game logs are also beside X3, not redistributed source assets.

The preserved 0.3 rollback DLL is `build/d3d9.dll`, checksum
`71f59c8e6422d5bbf2f55c116e2c0388026d956ba45a3a03eee53c4d85a232a6`. See
`docs/verification/iteration-03.md` for the command, coverage and test evidence.
The user supplied two four-frame turning bursts from 0.2; all captured draws
succeeded and camera/light/motion analysis is complete. The combined 0.3 session
is also complete; the user reported docking during it, without a timestamp that
locates docking within the captured bursts. All 2,914 named point-light count
observations are zero. Camera reconstruction error remains below 1.41e-7.

Texture helpers took 16.382 seconds and inflate 7.109 seconds in observed flushed
totals. An 89.092-second presentation gap remains incompletely attributed;
sampling/disassembly identifies an uncovered mesh adjacency/cleaning/optimization
path. See [loading observations](reverse-engineering/loading-observations.md).
No loading speedup is enabled in the game.

The user confirmed a game cursor and macOS arrow at different positions, persisting
after focus changes. Win32 focus/clipping/hiding restore in the trace, but native
cursor state was not sampled. See [cursor observations](reverse-engineering/cursor-observations.md)
for a scoped synthetic investigation; no cursor fix is deployed.

An independent canonical D3D9 ownership layer passes 370 baseline / 431 wrapped
fixture checks with matching shared HRESULTs and output mutations. It releases
renderer-owned resources before Reset and native device teardown. It is not yet
enabled by default; opt-in 0.4 produced 13,431 successful captured draws, without a terminal teardown summary in that log. See [ownership source](../src/ownership/README.md)
and [verification](../verification/probe/ownership.md). Generated fragments use
`*_inc.h` per the user's editor preference.

The original shader interpolation fixture passes 96/96 numeric samples across
32 cases and Reset. On this backend SM3 COLOR0 preserves values above one into
FP16; SM2 COLOR0 clips before interpolation. Explicit pixel-shader saturation
still clips both paths. See [HDR varying verification](verification/vertex-color-hdr.md).
This supports targeted SM3 material changes once the FP16 scene path exists;
it is not a game HDR implementation.

## Historical 0.4 checkpoint (gameplay analyzed above)

The ownership layer is connected to the loader behind `X3M_OWNERSHIP=1` in the
separate `build-ownership/` build. All 15 actual-DLL integration cases and a forced
adoption-failure fallback pass. Child-induced final device/factory releases now
reach the capture hooks; repeated device address reuse leaves no stale contexts.
Stencil states and depth selection status are included in consolidated capture
diagnostics. See [integration verification](../verification/probe/ownership_integration.md).

The incompatible D24X8-to-INTZ substitution experiment has been removed. The
replacement preserves the original application surface and explicitly copies it
to native D24X8 storage through RESZ. The installed binary investigation and
numeric positive/negative controls establish why D24X8-to-INTZ fails despite a
successful trigger HRESULT. See [RESZ verification](verification/depth-resolve.md)
and [backend investigation](reverse-engineering/depth-resolve-backend.md).
The opt-in `X3M_DEPTH_COPY=1` switch allocates storage and reports diagnostics;
the optional scene adapter invokes the explicit copy before a recognized destructive clear in requested capture frames. The installed rules rejected this game session; successful game depth preservation remains pending. See
[copy verification](verification/copied-depth.md): 634 checks / 32 samples pass,
with 357 additional loss-regression checks across 33 cases.

The bounded original mesh-adjacency reuse fixture passes 1,218 checks and complete
downstream mesh parity. It demonstrates a synthetic speed benefit for repeated
identical meshes, not a game loading improvement; actual repetition/cost remains
unmeasured. See [mesh preparation](verification/mesh-preparation.md).

A standalone temporal resolve shader now passes 58 numeric GPU checks including
camera/object reprojection, disocclusion, HDR preservation, actual jittered
history accumulation and Reset. Independent review caught and corrected a raw
D3D9 viewport half-texel error using a rasterized-geometry regression. See
[temporal resolve](verification/temporal-resolve.md). Camera/object-motion routing,
scene-boundary integration and gameplay TAA remain incomplete.

The native D24X8 snapshot exposes shadow comparisons rather than raw depth. A
separate GPU decoder reconstructs R32F device depth with 26 comparisons per pixel;
precision and cost limits are recorded in [decoder verification](verification/depth-decode.md).
Its isolated timings do not establish frame cost at the user's full resolution.
Independent [code review findings and fixes](verification/review-04.md) are
recorded with the checkpoint evidence.

The consolidated 0.4 diagnostic build was verified and installed for that run, with SHA256
`81e3b121659c0fa1811641a5dbe019f668341477a787c6729fb5a848e056c516`. It includes
an exact-executable engine submission scope, buffer write revisions, scene-depth
preservation during requested captures, and mesh loading timings. The standalone
scene adapter passes 20 scenarios / 2,228 checks / eight samples; buffer tracking
passes 530 checks, and mesh timing passes 68 ABI plus 123 native mesh checks.
Independent reviews found and fixed post-clear query confirmation and hook
recovery/foreign-chain defects. See [review](verification/review-04.md) and the
[completed coordinated run](verification/iteration-04.md).

The detached production temporal runtime has paired FP16 color/R32F depth history,
explicit motion policy, failure-safe publication and caller-state restoration;
44 numeric checks and 40 complete state comparisons pass. It remains disconnected
from game rendering. No camera jitter or motion producer is enabled. Verified
engine handles are not yet lifetime-safe across reload/reuse, and post-bloom color
is not a complete matched color/depth input for whole-frame TAA.

No game was launched by the agent. No visual enhancement has been enabled.
Commit each completed logical checkpoint.
