# Project status

Updated 2026-09-21: Run55 is ready for the first-person fog check and crash
diagnosis. The agent never launches the game. See the [run queue](verification/user-runs.md),
[goals](goals.md) and [original objective](user-objective.md).

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run55 DLL SHA-256:
`4b47f636acd66eba35c61a3f9a4d4d18d911470f46b2df7b64aaf1465cdaa012`
(58,746,286 bytes), built once from clean reviewed source
`54b48c36f6d7d6aa846992fe5412fbaee5f26219` on
`feat/media-production-integration`, checkout `/tmp/x3-media-production-integration`.
Retained DLL: `/tmp/x3-run55-candidate/build/d3d9.dll`.
Production changes remain off main while the candidate qualifies.

The change since Run54 is the fog camera precision check: it now accepts the
near-rigid matrices admitted by the camera reader while retaining the true inverse,
determinant, finite-value and downstream rejection guards. Owned ID2 media,
fourteen spatial fog profiles/shafts and the protected lattice observer are
unchanged. **No media crash fix or moving-lattice visual fix is claimed.**

The [qualification record](../verification/results/run55-candidate-qualification.json)
binds the full host pass (**2,639 tests, two skips**, 768.380 s), linked audit
(**95 roots, 543 reachable functions, zero violations**) and the scoped D3D fog
route (**515 checks**, 33 captured rotations and four malformed-camera
refusals/recoveries). The route uses real parameters/FogPass with synthetic
translation/sun/owner; it is not game appearance or native Windows acceptance.
Unchanged feature evidence is reused; Run54's actual-DLL smoke is not presented
as a Run55 execution. The affected launch dry-run passed; no game was launched.

Installation used the integration checkout's `tools/manage.py install --bottle X3
--dll-source /tmp/x3-run55-candidate/build/d3d9.dll --media-package
/tmp/x3-media-local-stage-v1/package.json`. The
[install record](../verification/results/run55-candidate-install.json) verifies
installed bytes, full media selection, and unchanged X3AP.exe, cxbottle.conf and
original `mov/00002.dat`. The exact Run54 DLL/manifest rollback pair is under
`drive_c/X3/x3-modern-media/rollback/4e5c47e4d53c45b78d00d86960ce2c96`.
Use the integration launcher in the run queue for media preflight/coordination.

## Current work and pending acceptance

- **Run54 crashes:** runs195/196/197 failed in flight. Three logged anchors
  establish both addressed faults outside the proxy, but the external module
  and cause remain unknown. [Reproducible triage](../verification/results/run54a-triage/result.md).
  Run197 proves an owned copy occurred before its F8 snapshot, not at crash time.
- **Media:** three bounded original-voice/owned-worker cases did not reproduce
  the crash or GStreamer criticals. Active playback includes 15 exact copies,
  overlapping original audio progress and guarded retirement/reuse; this does
  not exonerate either path. [Record](../verification/results/media-run54-coexistence-2026-09-21.json).
  Run55 adds `CX_DEBUGMSG=+timestamp,+tid,+seh,+loaddll`. Existing Wine/WineDbg
  produced correct module/register/stack evidence in two deliberate background
  read faults, including a relocated DLL; game output is not guaranteed.
  [Diagnostic qualification](../verification/results/run54-crash-trace-qualification.json).
- **Fog:** the user confirms Run197 F8 was first-person. All 32 captured matrices
  failed the old fog-only tolerance despite passing the camera reader; corrected
  helper and D3D-route checks pass. Run55 checks the visible result. Density
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
