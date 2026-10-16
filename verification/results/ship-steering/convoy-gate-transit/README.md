`keyoff_diff.py` / `keyoff_diff.txt` are retired (2026-10-15): stage B, the key-off path they kept identical, was deleted; `laneon_diff.py` → `laneon_diff.txt` is the identity check now.

`engine_paths/` (2026-10-15): the fixture engine paths round (convoy-gate-transit.md section 11 "Fixture engine paths"): `run.sh` -> `item_a.txt` (the engine model old / corrected / flown over the active casts) and one `<cast>.txt` per parked cast; `summary.py` -> `summary.txt`.

`cleanup/` (2026-10-16): the cleanup after Run178 (far bound, lateral shift, dead helpers, fail-before switches, historical-core guards deleted); `laneon_diff.py --base 7d19e33e` -> `cleanup/laneon_diff.txt` is the identity check now. Directories whose scripts depend on what it deleted say so in their own `README.md` (arr_mode, arr_far, arrival_round, arrival_round/review, review_abc, round_b, round1_head, round2_head, round2_review, round2_close, review_head, final_head); their results stay as the ledger's evidence.
