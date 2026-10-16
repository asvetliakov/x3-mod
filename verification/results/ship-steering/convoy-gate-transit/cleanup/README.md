Cleanup 2026-10-16 (docs/verification/ship-steering.md "Cleanup 2026-10-16"), from the repository root, no Wine, no game:
`python3 -I verification/results/ship-steering/convoy-gate-transit/laneon_diff.py --base 7d19e33e` -> `laneon_diff.txt` (the identity check against Run178's accepted state, about 5 minutes);
`python3 -I .../cleanup/deadscan.py .` -> `deadscan.txt` (after) and the same on a `git archive 7d19e33e src verification/probe tools` tree -> `deadscan_7d19e33e.txt` (before);
`python3 -I .../cleanup/x12rc_probe.py . <scratch dir>` -> `x12rc_probe.txt` (the x12_rc branch is reachable on main, so it stays).
