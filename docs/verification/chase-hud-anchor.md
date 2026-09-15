# Chase HUD forward anchor verification

## 2026-09-15 — reviewed source checkpoint

Implemented the ratified `--chase-hud-anchor forward|centre` option in the
existing final-FOV seam; `centre` remains the default and performs no anchor
writes. [The owning note](../architecture/chase-view-restore-and-hud-anchor.md)
records the exact projection, lifetime policy and five-module test command.

- Independent consequential hook/lifetime review: PASS after fixing interrupted
  updates that could exhaust all eight tickets. The original extracted-production
  witness now reports `occupied=0 recovery_applied=1 recovery_refused=0`.
- Reviewer reproduced 81 focused host tests in 5.060 s, including 69 lead/HUD
  scenarios and 264 checks. Recovery, live-ticket preservation, final-FOV changes,
  unprojection roundtrip and memory-ownership negatives pass.
- Strict MinGW x86/SSE2 syntax checks and diff checks pass. Existing hook stubs
  and CPU/LastError preservation are unchanged; no new engine call or allocation.
  Reclamation is bounded to eight slots and does not dereference retained pointers.
- The 13° group offset is `(0,-118)`; retaining native crosshair `(0,9)` places
  its node at `(0,-109)`. Glyph hotspot calibration and gameplay bolt alignment
  remain unverified; a node coordinate is not proof of the displayed aim point.
- Review artifacts remain local at `/tmp/x3-hud-review-witness/`. No Wine run or
  game launch was performed. Native Windows runtime remains unverified.

Source is integrated separately from the fill candidate. Installation is recorded
only in [status](../status.md). View restoration remains unimplemented until the
queued gate-jump/jumpdrive telemetry establishes the reset writer and ordering.

## 2026-09-15 run25 / snapshot run60 user acceptance

The user reports that the forward crosshair/distance group is aligned at the
current camera settings. They prefer the screen-centre placement for now and
may revisit forward anchoring after camera tuning. `centre` remains the default;
`forward` remains opt-in. This is user gameplay evidence for the tested setup,
not a measured glyph calibration across all camera settings. No code/default
change is required. The concurrent gate-view reset concerns restoration, a
separate feature not implemented in this diagnostic build.

## 2026-09-16 camera framing defaults (pitch 0.5°, offset_y 0.50)

After run 26 accepted the raised camera, the user made the near-parallel
elevated row of [chase-hud-reticle-survey.md](../architecture/chase-hud-reticle-survey.md)
the default in both the DLL fallback (`src/proxy/chase_camera_math.h`) and the
launcher (`tools/manage.py`, which now always forwards the two framing
constants in chase mode). The `chase_lead` geometry oracle pins the forward
vanishing point at 0.5°: −tan(0.5°)·512 rows = −4.47, expectation `(0,−4)`
within the existing 1 px tolerance, i.e. inside the centre crosshair glyph, so
the default `centre` anchor is now a true boresight cue. `forward` is unchanged
and still opt-in. Host-only evidence; no game or Wine run.

```sh
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_chase_camera verification.analysis.test_chase_lead   # 53 tests OK; chase_lead_host scenarios=69 checks=268 failures=0
./x3run --camera chase --motion-output --hdr --hdr-tonemap --dry-run            # X3M_CHASE_PITCH_DOWN_DEG=0.5, X3M_CHASE_OFFSET_Y=0.5
```
