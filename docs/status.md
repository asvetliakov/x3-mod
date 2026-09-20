# Project status

Updated 2026-09-21: Run55 (run199) confirms the first-person fog fix in flight;
the crash recurred. Run56 omits ID2 animated video and is ready for the stability flight. The agent never launches the game. See the [run queue](verification/user-runs.md),
[goals](goals.md) and [original objective](user-objective.md).

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run56 DLL SHA-256:
`a51d1e75fa80d7d07bab7ab66004291f7248bc5693e6585171e564a90fa96e56`
(54,386,310 bytes), built once from clean reviewed source
`85da89a8955a72b20d92f2309e4b9a4cb4dd325e` on **`fix/id2-video-omission`**,
checkout `/tmp/x3-run56-integration`. Retained DLL:
`/tmp/x3-run56-candidate/build/d3d9.dll`. Production remains off main while
this candidate qualifies.

The user accepts missing ID2 animated textures. The candidate skips their silent
video construction through the existing allocator gate and removes the owned
playback runtime, startup/consumer/destination hooks, LAV SDK dependency and
media-package launch prerequisite. Speech, music and unrelated media keep their
existing paths. The first-person fog correction, fourteen spatial profiles/shafts
and protected lattice observer remain. **Crash-free flight is not yet verified.**

[Qualification](../verification/results/run56-candidate-qualification.json): full
host discovery completed **2,627 tests in 735.394 s**, two skips, one stale
CreateDevice fixture failure. That sole failure was corrected and independently
reviewed; its focused rerun passes **19 scenarios / 202 checks**. The initial
full run was not an all-pass run. The focused x86 omission fixture passes
**3,904 checks**, including 96,000 foreign-thread calls. Linked audit passes
**95 roots / 542 reachable functions / zero violations**. Two selected checks
on the actual DLL pass: ownership/HDR **43 checks / 39 restorations**, TAA/HDR
**83 / 51**; this is not a full renderer-suite pass or native Windows execution.

Installation used `python3 tools/manage.py install --bottle X3 --dll-source
/tmp/x3-run56-candidate/build/d3d9.dll` from the new checkout. The
[install record](../verification/results/run56-candidate-install.json) verifies
installed bytes and unchanged X3AP.exe, cxbottle.conf and original `mov/00002.dat`.
Active media selection is removed; the exact Run55 DLL/manifest and its provider
files remain valid for rollback under
`drive_c/X3/x3-modern-media/rollback/fb0fda8949bf4d6c8cb91f6a055361f8`.
The affected launch dry-run passed; no game was launched. Use the Run56 command
in the [run queue](verification/user-runs.md).

## Current work and pending acceptance

- **Crash / media decision:** Run55 run199 repeats the crash in native
  `LAVVideo.ax`, RVA `0xa413a`, on its video output thread. Exception handlers
  and disassembly place it in Concurrency scheduler initialization; the origin
  of the invalid pointer remains unproved. [Crash evidence](../verification/results/run55-crash-triage/result.md).
  The user accepts missing ID2 animated textures. The replacement machinery
  is retired; Run56 checks stability and preserved speech/music with targeted
  refusal before graph construction. [Omission ledger](verification/media-cues.md#id2-video-omission-and-owned-playback-retirement-2026-09-21).
- **Fog:** the user confirms Run197 F8 was first-person. All 32 captured matrices
  failed the old fog-only tolerance despite passing the camera reader; corrected
  helper and D3D-route checks pass. Run199 confirms the visible fix by user report, with F8 in first person. Density
  remains 0.03 (the user's 1.50× preference); fog remains opt-in/off by default.
  Fourteen-family visuals, shafts and clear-sector travel retain their flight
  acceptance gaps. Camera-cut native-card replacement protection remains enabled.
- **Lattice:** diagnostic reference protection is qualified, but Run54 B is held
  until the crash investigation permits it. The offline stage audit is a bounded
  negative: TAA reduces measured variation, while geometry ownership remains
  unqualified. [Owning note §29](architecture/taa-lattice-crawl.md#29-existing-capture-stage-attribution-bounded-negative-2026-09-21).
  No new renderer correction was selected.
- **Collision:** paused by user request; no moving-collision test is queued.
- **Engine/proxy timing:** lazy RT remains accepted. Corrected attribution
  does not justify another engine patch or busy-view timing flight.
  [Decision](architecture/engine-frame-time.md#run52-corrected-attribution-and-remaining-optimization-scope-2026-09-20).

Other defaults remain lazy motion RT binding, original hull shading, 2 px/all
small-parts culling, SSE2 collision plus memoization and light-map fade **80,220,1**.
Far stabiliser 0.985/thin region 0.97 remain explicit; stationary lattice
improvement is accepted and moving crawl is open. Native Windows runtime remains
unverified. Main records the handoff/evidence; the combined production source
has not been merged to main.
