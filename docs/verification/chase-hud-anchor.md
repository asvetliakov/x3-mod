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
