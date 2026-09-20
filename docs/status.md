# Project status

Updated 2026-09-21: Run54 qualification is held after repeated Session A crashes. The agent never launches the
game. See the [run queue](verification/user-runs.md), [goals](goals.md) and
[original objective](user-objective.md). The prior narrative is preserved in
[the Run53 status archive](archive/status-through-run53-2026-09-20.md).

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run54 DLL SHA-256:
`124d98898a66413d2400f99c851e8e2574c6a8238b8d584d06db2a6d48d4d6da`
(58,746,286 bytes), built once from clean reviewed source
`693a0670c8428dccac95da5f1dc384710d22df88` on
`feat/media-production-integration`. Retained DLL:
`/tmp/x3-run54-candidate/build/d3d9.dll`.

This combines owned ID2 media playback, fourteen source-backed spatial fog
profiles with directional shafts, the camera-cut/native-card flicker correction,
and the protected lattice state observer. Moving-lattice visual improvement is
not claimed. Production changes remain off main while the candidate qualifies.
Use the exact integration-launcher paths in Run54: they include the reviewed
media-package preflight and launch/install coordination.

The [qualification record](../verification/results/run54-candidate-qualification.json)
binds the full host pass (**2,638 tests, two skips**, 756.110 s), linked audit
(**95 roots, 543 reachable functions, zero violations**), two selected actual-DLL
HDR/ownership and HDR/TAA smoke cases (**43/83 checks**) and a successful affected
launch dry-run. The smoke runner is a partial selection, not a full rendering-suite
pass. No game was launched; native Windows runtime remains unverified.

Installation used the reviewed `tools/manage.py install --bottle X3 --dll-source
/tmp/x3-run54-candidate/build/d3d9.dll --media-package
/tmp/x3-media-local-stage-v1/package.json` from the integration checkout.
The [install record](../verification/results/run54-candidate-install.json) verifies
installed bytes, the full media selection and unchanged X3AP.exe, cxbottle.conf
and original `mov/00002.dat`. The exact Run53 DLL/manifest rollback pair is under
`drive_c/X3/x3-modern-media/rollback/3b6d43c534de4e69ad077650d67203a2`.
The app-local provider/source cache is managed separately from original game files.

Existing launcher defaults retain lazy motion RT binding, original hull shading,
small-parts culling at 2 px/all, SSE2 collision plus memoization, and light-map
far fade **80,220,1**. Fog remains opt-in/off by default; Run54 explicitly uses
**0.03 = 1.50×** density, the user's Run53 preference. Unsupported or unavailable
fog families retain native cards. Far stabiliser 0.985 and thin region 0.97 remain
explicit options; stationary improvement is accepted, moving crawl is open.

## Current work and pending acceptance

- **Media:** source integration and scoped runtime qualification are complete.
  The connected fixture has 20 exact RGB readbacks covering both sequence-zero
  first pictures, concurrent playback, retirement/reuse and natural completion.
  These authored engine continuations and native copies do not prove game
  playback or elimination of stutters. Run54 A returned run195/run196/run197 with repeated crashes; cause is under
  investigation. Playback and stutter acceptance remain open. Detailed evidence
  remains in the integration branch's media ledger and the qualification record.
- **Fog:** fourteen profiles and shafts are qualified in fixtures. Run194's
  repeated camera-cut warmups explain the native-card flicker; the correction
  preserves replacement across cuts while retaining sector/Reset/failure recovery.
  Run54 A reports fog flickering/disappearing in first person while chase view
  renders it. Camera eligibility and authority are under investigation; visual
  acceptance and shafts remain open.
  [Run53 findings](../verification/results/run53b-triage/main.md).
- **Lattice:** the observer guard passes **830 native checks** across both RT
  modes, real resource-release callbacks, Reset, failure and target retirement.
  It fixes diagnostic interference, not the visible crawl. Run54 B is held
  pending crash diagnosis before repeating the three bounded state captures. [Owning note §27](architecture/taa-lattice-crawl.md#27-bound-observer-reference-callbacks-and-device-lifetime-2026-09-20).
  The requested external-engine design research is complete; a bounded offline
  stage-attribution audit on existing captures is complete: TAA reduces measured
  variation, but geometry ownership remains unqualified. No new renderer patch
  is selected. [Bounded negative result](architecture/taa-lattice-crawl.md#29-existing-capture-stage-attribution-bounded-negative-2026-09-21).
- **Collision:** paused by user request. Snapshot lifetime remains unproved;
  no moving-collision patch or capture is queued.
- **Engine/proxy timing:** Run52 accepted lazy RT binding. Corrected attribution
  does not justify another engine patch or repeat busy-view timing flight.
  [Decision](architecture/engine-frame-time.md#run52-corrected-attribution-and-remaining-optimization-scope-2026-09-20).

Run53 A/B are received and archived as run193/run194. Their feature acceptance
remains separate from completing the capture analysis. Run51's optional old retry
counter is superseded by the replacement-media flight, not reported as flown.
Six host fixture/setup/source assertions failed the first combined discovery run;
all received independent review and bounded repairs, and the clean full rerun
passed. No production change was needed for those host repairs.

Run54 crash reports: run195 reads `FFFFFFFF` at `6EB2413A`; run197 reads
`00000004` at `6EB2A0FA`; run196 also crashed without a supplied address.
Module attribution and cause are not established yet. No replacement DLL has
been installed; the retained rollback remains available.
