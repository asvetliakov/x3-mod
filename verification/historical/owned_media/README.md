# Retired replacement-media implementation checks

Replacement-owned playback was removed in run56. These tests require its removed
runtime, engine/destination hooks, startup controller or clock. They are historical
checks, deliberately outside active `verification/analysis` discovery, not skipped
regression failures. The corresponding C++ fixtures and evidence parsers remain in
`verification/probe`; they do not add a production build dependency.

To reproduce the original implementation checks, use a separate checkout of the
complete baseline (do not run these relocated modules against current production):

```sh
git worktree add --detach /tmp/x3-owned-media-history 54b48c36
cd /tmp/x3-owned-media-history
PYTHONPATH=verification/probe python3 -m unittest \
  verification.analysis.test_media_startup \
  verification.analysis.test_media_root_wiring \
  verification.analysis.test_media_services \
  verification.analysis.test_media_destination \
  verification.analysis.test_media_engine_adapter \
  verification.analysis.test_media_owned_adapter \
  verification.analysis.test_media_presentation_gate \
  verification.analysis.test_media_clock_bound \
  verification.analysis.test_lav_worker_transport \
  verification.analysis.test_lav_worker_pin_discovery \
  verification.analysis.test_media_worker_clock_fixture
```

This is host testing/cross-compilation only; never launch the game. Historical
Wine fixture runners also require the matching baseline, the project Wine lock
and an explicit owner assignment. Existing research and captured results remain
at their original paths. No runtime source copy is maintained here: Git preserves
the exact baseline and avoids a second implementation that could drift.

The still-relevant factory wrapping/admission test is active as
`verification/analysis/test_loader_factory.py`. Evidence-parser mutation tests in
`test_media_worker_clock_fixture.py` remain active; only its removed C++ clock
replay class is retained here. Package transaction, legacy removal and rollback
checks remain active because they protect existing app-local installations.
