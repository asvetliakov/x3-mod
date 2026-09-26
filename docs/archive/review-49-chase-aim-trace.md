# Review 49: chase cursor-fire diagnostic trace

Independent source and evidence review, 2026-09-13. Scope: the read-only chase
cursor/fire observer, four exact game hook sites, camera-event integration,
bounded correlation and reporting, CPU/LastError preservation, partial-install
rollback, site and x86 fixtures, and the related reverse-engineering claims.
The elevated camera geometry is covered by review 48; this review does not
authorize a weapon or input behavior change.

## Result

**The diagnostic source and its X3 fixture are approved. No code, correctness,
portability or bounded-performance finding remains open in this scope.** The
observer records evidence needed to distinguish a missing script cursor update,
an admission failure, a cone-clamped ray, stale camera context and the final
per-barrel world direction. It never changes the cursor globals, fire arguments,
ray, gun state or camera state.

The final six-mode fixture passed **124 checks** in bottle X3. Current source
hashes match the summary, its pre/post-runtime source maps are identical, and
the current fixture binary matches the recorded SHA-256. The installed game was
not launched. This result qualifies the synthetic x86 hook and failure paths;
live game observations, visible aiming behavior, game FPS and native-Windows
execution remain unverified. The full candidate build/audit and isolated X3
load controls also pass below; installation and the user-managed flight remain.

## Hook and observation assessment

The four `SiteSpec`s match the installed X3AP executable and disassemble into
whole instructions:

| Site | Bytes | Decoded lengths | Observation |
| --- | --- | --- | --- |
| `0x00445a15` | `8b43148bf8` | 3, 2 | fire arguments and admission inputs |
| `0x00445b70` | `8945e48b45e4` | 3, 3 | admitted pre-cone ray, depth and group origin |
| `0x0044605a` | `8b53088b4a70` | 3, 3 | final world direction, muzzle and endpoint per barrel |
| `0x004074de` | `890de87c6000` | 6 | cursor-state writer inputs in ECX/EDX/EAX |

None contains relative control flow. Complete objdump decoding of the two
containing functions finds no direct branch into a span interior. The verifier
correctly treats the apparent opcode bytes near the ray site as an instruction
immediate rather than a branch. Its source-parity check binds the independently
derived addresses, bytes, lengths and zero relocation fields to the production
declarations.

Initialization occurs only after the chase camera installed successfully, and
the trace independently requires telemetry, the verified executable and the
open patch window. All four sites must claim and chain successfully. A partial
failure restores every live earlier patch and leaves observation disabled; a
restore failure is reported explicitly while `enabled` remains false. Allocated
stub storage intentionally lasts for the process, as required by the shared
patch arena's execution-lifetime contract.

The generated stubs preserve EFLAGS, all general registers and XMM0–7 before
calling C++. `PreserveCpuState` transports the live x87 stack/control/status,
MXCSR and LastError, then the callback initializes a private masked
round-to-nearest arithmetic state. The trace translation unit is built with
SSE2, four-byte incoming-stack realignment and `-fno-exceptions`. The last flag
is necessary: normal MinGW SJLJ registration can otherwise execute outside the
RAII state boundary. The exact production audit object has no unwind,
personality or throw reference and has the expected one `fnsave`, two `frstor`,
one `stmxcsr`, one `fninit`, two `ldmxcsr` and one `ret`; `GetLastError` is the
first call and no call follows `SetLastError`.

The same review exposed the pre-existing SJLJ gap in `chase_camera.cpp`.
CMake now applies source-specific `-fno-exceptions` to that injected callback as
well. The generated CMake recipe retains `-msse2 -mfpmath=sse -mstackrealign
-mincoming-stack-boundary=2`; inspection of its camera object found no exception
runtime reference and the same required state envelope. The rationale and its
strict claim limits are recorded in `docs/verification/chase-cpu-boundary.md`.
This is a compiler-boundary correction, not an established explanation for a
gameplay symptom. The final candidate audit described below confirms the same
boundary in the exact linked camera and trace objects.

## Context, correlation and report bounds

The camera callback publishes context only for the active control cockpit,
after its final applied/pass-through decision. Readable inactive monitor visits
return before the trace callback. An unreadable visit invalidates context only
when its cockpit address equals the current accepted cockpit, while a known
active cockpit whose required reads fail clears the current context. This keeps
an unrelated unreadable monitor from erasing valid player identity. Internal
pass-through remains recorded for the intended first-person/chase comparison.

Admission coherence requires the live resolved cockpit, firing ship and camera
pointers to equal the captured context, both ship IDs to be readable and equal,
and the context QPC age to be known and at most two seconds. The age is a
diagnostic label only; it neither rejects nor changes native firing. Each event
also records the camera handler frame, mode, applied state, camera/ship/cockpit
pose fields and explicit validity bits, including camera/default view planes
and screen dimensions.

Four static thread slots correlate entry, admitted-ray and final-barrel phases
by thread, EBX, EBP and a monotonic serial. Every accepted new entry invalidates
the prior slot before argument reads or sample-capacity decisions. Therefore a
dropped entry, failed read, reused EBP/EBX pair, report reset or out-of-order
phase cannot append to an older event. Eight events are retained per report;
entry/ray/final totals count all player-filtered calls, omitted follow-ups and
true orphans are separate, and cone-clamp counts are explicitly labeled as
sampled. Multiple barrels retain the first and latest final direction with a
count on their one entry.

The cursor hook observes every script-state write because the command owns a
global cursor state. It records the input registers before the native store,
thread, QPC and camera sequence, with bounded first/latest samples in unknown,
internal and external buckets. Every retained fire event links the latest
writer record. This supplies the requested distinction between no update and an
explicit cursor disable without adding another instrumented load.

All records and pending slots use fixed storage. Every game-memory read uses the
bounded reader and explicit validity. Accepted player-fire phases and cursor
writer calls take one diagnostic lock; report copies at most eight events and
six writer samples, releases the lock, then logs on the existing Present report
cadence. There is no per-call logging, heap allocation or draw-time work.

The reported handler timing includes player-fire work after the early player
filter and all cursor-writer work after their first QPC, including lock wait.
It excludes the generated stub, full CPU boundary, first QPC and the fire
player/AI filter. Rejected AI phases still pay the stub, state boundary and one
bounded ship-pointer read. Camera-handler timing includes the added context
snapshot while the trace is enabled. These are diagnostic CPU intervals, not
total instrumentation cost or game FPS.

## Findings resolved during review

| # | Severity | Finding and resolution |
| --- | --- | --- |
| R1 | medium | An unreadable cockpit initially cleared player context regardless of identity. The callback now invalidates only a matching current cockpit; readable inactive monitors never call it, while known-active read failure still clears context. |
| R2 | medium | Camera/fire coherence initially compared ship IDs without exact live identity or freshness. Admission now requires resolved cockpit, ship and camera equality plus a reported two-second QPC age limit. QPC-frequency failure also refuses installation. |
| R3 | medium | Early correlation revisions could leave a sampled slot reachable after EBP reuse, an unsampled entry or report reset, and aggregate phase counters covered samples rather than all calls. New entries now invalidate first, reports clear every pending slot, serials guard retained records, and omitted follow-ups are separate from orphans. Hostile controls cover each case. |
| R4 | medium | Default MinGW exception machinery placed SJLJ work outside the trace CPU-state/LastError boundary. Source-specific `-fno-exceptions`, object inspection and the actual x86 state fixture close that gap. The same existing issue in the camera callback received a separately documented CMake correction. |
| R5 | low | Initial logs and timing text called the fire arguments generic `flags` and described combined fire/writer timings as after a player filter. The fields now say `fire_flags`, remain distinct from cockpit `flags_1a0`, and accurately state the player-fire/cursor-writer timing scope. |
| R6 | test blocker | The first X3 fixture tried to reserve the blanket range `0x00400000..0x00620000`; existing low mappings made it fail before a hook ran. A second page-granular attempt confirmed the same conflict and was preserved as diagnostic provenance. The final fixed-base PE owns a zero-initialized, initially non-executable `.x3map` section at `0x00401000..0x00620000`, places real fixture code at `0x00630000` or above, asserts runtime module/site ownership, and makes only exact code pages executable. Production addresses and specs are unchanged. |

## Verification

I independently ran the 11 focused host controls and the read-only installed-EXE
site verifier. The final verifier reports PASS for executable SHA-256, size, PE
base, source parity, complete function decoding and all four sites. It confirms
instruction lengths `[3,2]`, `[3,3]`, `[3,3]`, `[6]`, with no relative control
in a span and no decoded interior branch. `git diff --check` passes.

The final X3 run used the serialized Wine lease and passed these cases:

| Mode | Checks | Coverage |
| --- | ---: | --- |
| `complete` | 68 | actual production stubs and displaced spans; EFLAGS/GPR/XMM0–7/x87/MXCSR/LastError preservation; admission; writer registers; identity/age; correlation, bounds and report reset |
| `bad0` | 10 | first-site byte mismatch, no live patch |
| `bad1` | 11 | second-site mismatch and actual first-site-byte restoration |
| `bad2` | 12 | third-site mismatch and actual prior-site restoration |
| `bad3` | 13 | fourth-site mismatch and actual prior-site restoration |
| `late` | 10 | closed install-window refusal with no live patch |

The runner's PE audit requires x86 PE32 at fixed image base `0x00400000`, no
dynamic-base flag, exact zero `.x3map` extent, writable but initially
non-executable mapping, pairwise non-overlapping sections, real sections and
entry outside the synthetic range, every required production site/global range
inside it, and source parity with the independent inventory. Runtime then
requires the module at that base and every site committed as `MEM_IMAGE` owned
by the fixture before applying exact page protections. This executes the
unchanged production addresses instead of substituting fixture-only specs.

Final evidence:

- Summary: `verification/results/bottle-X3/chase-aim-trace-summary.json`,
  SHA-256 `a8ca59a5020ef29abda3bafa6f86ed99864e1ff9c8ffef9a75aafaf9fd7a82e8`.
- Fixture executable: SHA-256
  `84d2dfbb8fac825206f3d581c0034b02b6bdb774d682bbaacd777b68265d78e9`.
- Bottle: X3, `WineArch=arm64`, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`.
- Six cases, 124 checks, zero failures; `game_launched=false`,
  `runtime_verified=true`, and source/binary hashes stable across execution.

## Candidate integration evidence

After the trace was resolved, the orchestrator clean-built the complete PE32
candidate from the frozen source. The host/build summary
`verification/results/chase-elevated-host-build-summary.json` is PASS, SHA-256
`e46026ec7c42edac2ae12090e5cc19bddf76385e3a3be8c363dc75c5722d6969`.
It binds identical before/after manifests for 164 CMake/source files and records:

- 994 discovered host tests and 67 focused camera/camera-site/aim-site tests
  passed; both installed-executable site verifiers passed;
- a clean 42-object build produced `build/d3d9.dll`, 11,709,039 bytes,
  SHA-256 `2981bf032be8c7e91013e1de7f355d83fb49f4b2ba778b2b5917787f48a9297c`;
- all 42 disk/archive objects were fresh and byte-identical, with exactly one
  camera and one aim object linked;
- both final callback objects passed the no-exception-runtime and full CPU
  boundary audits; the separate light-boundary audit found zero x87 violations
  across 211 reachable functions;
- all 17 installed export names remain, and the candidate has 14 import DLLs
  and 193 symbols. The sole added symbol is the documented public UCRT
  `hypot` used by the elevated camera geometry; no private backend dependency
  was added.

The isolated X3 camera host also passed against that exact candidate and the
compiled 20-degree, 0.6-distance, 0.22/0.30-second defaults. Its local summary
is `/tmp/x3-chase-elevated-qualification-UwDzq7/camera-host/summary.json`,
SHA-256 `6d825febdbd5a122502c75326d8e0114c754cfdc2733a698e572aa1c000cd831`.

Finally, the writable and read-only X3 proxy-load/export cases each passed eight
checks against the same `2981bf…a9297c` DLL. They resolved all 17 D3D9 exports,
exercised the available forward/fallback paths and verified capture-directory
fallback from a read-only game directory. The local summary is
`/tmp/x3-chase-elevated-qualification-UwDzq7/final-exports/d3d9-exports-summary.json`,
SHA-256 `35b16703e626314dad672b0dcec66f812d8defdd9d3a49461edbf80deed155b0`.
Both records say `game_launched=false`; they are CrossOver X3 evidence, not
native-Windows or gameplay verification.

Reviewed source identities:

| File | SHA-256 |
| --- | --- |
| `src/proxy/chase_aim_trace.cpp` | `9f6e1f7af8802da5d68eebf0bcdc8699de97c79f58f20814fba93be8e3f90c2d` |
| `src/proxy/chase_aim_trace.h` | `29da515414a3291e905cbd81743abe86a9de5aa6538200b0c2aa50f5fadb4947` |
| `verification/probe/chase_aim_trace_fixture.cpp` | `f525a94c54bf3cd1aa10bf23f332ec0da3f78e933a8d23b750fe80457b453078` |
| `verification/probe/run_chase_aim_trace.py` | `220039abf6ec6cff60b6c2ff0ad54f034b6cacd4d585e8a98d2a6d4ee69f086f` |
| `verification/probe/verify_chase_aim_sites.py` | `03753f999ec53d8c23c5a109c9e374c234ce8206abd44c6f349efb5ea3e93619` |
| `verification/analysis/test_chase_aim_sites.py` | `6d0d4971b7ff4215e2af9e16cb7e0c7e79bdca403a041296ea4b459a2d03834a` |
| `docs/reverse-engineering/chase-mouse-fire.md` | `10dd57c3f0ec076ce3d29cecfdb0a2a321797339768f13892f010dc859453337` |
| `docs/verification/chase-cpu-boundary.md` | `39bf18442cf771a41b95d31498e174c927ef1409686423258894f940e0a9b7d9` |

## Remaining acceptance

The trace establishes a safe, bounded observation mechanism. It does not yet
show which gate or geometry differs in the user's scene, and it does not fix
straight-ahead chase fire. After the orchestrator completes the reversible
install, the user must launch X3 and capture first-person and chase cursor
sweeps. Those logs can determine whether script
state, fire flags, context age, the native cone or finite muzzle convergence is
responsible. Native Windows remains a required but unexecuted target; the
production path uses documented Win32 APIs and game-internal hooks without a
Wine-private dependency.
