# Handoff: fixture runners and the X3 bottle (2026-09-12, paused)

Paused on the orchestrator's request (account switch). Nothing was committed
by this task.

## Done

- `verification/probe/bottle.py` (new, **untracked**): `BOTTLE`
  (`X3M_FIXTURE_BOTTLE`, default `Steam`), `wine_args()`, `results_dir(root)`
  (Steam -> `verification/results`, else `verification/results/bottle-<name>/`),
  `bottle_dir()`, `game_dir()`, `describe()` (name, WineArch, the
  `FEX_X87REDUCEDPRECISION`/`WINEMSYNC` lines from `cxbottle.conf`), `label()`.
- 48 scripts under `verification/probe/` import it: every `--bottle Steam`
  literal is `bottle.BOTTLE`, every results directory is
  `bottle.results_dir(...)`, every `Bottles/Steam/...` path is
  `bottle.bottle_dir()`/`bottle.game_dir()`, and every summary dict carries
  `bottle: bottle.describe()` (`run_telemetry.py` writes no JSON, so only its
  paths changed). `run_rigid_motion.py` keeps reading the generated
  `rigid-motion-pixel-program.json` from the shared results path.
- `run_sampling_profiler.py`: `wait_for_idle_wine()` skips `pgrep -fl` lines
  whose first token is not a pid (continuation lines of multi-line commands
  raised `ValueError` before).
- `AGENTS.md`: "Test coordination" paragraph (bottles, one runner at a time).
- `docs/verification/bottles.md`: design and the validation table (all X3 rows
  still "not run").
- All `verification/probe/*.py` byte-compile; `bottle.py` prints the right
  record for both bottles (`python3 verification/probe/bottle.py`,
  `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/bottle.py`).

## Important for the next commit

Commit `a8d4309` (made by the main session at 17:23, "Record the evening session
handoff") already contains the edited runners (they `import bottle`), but
`verification/probe/bottle.py` is still untracked. HEAD's runners therefore fail
at import until `bottle.py` is committed: `git add verification/probe/bottle.py`
must be part of the next checkpoint, together with `AGENTS.md`,
`docs/verification/bottles.md`, this file, and the later
`run_sampling_profiler.py` idle-wait fix (uncommitted, `git status`).

## Not done

- No suite has a recorded X3 result. Suite 1 (`run_sampling_profiler.py`) was
  started but the other agents' mesh-adjacency and motion-output fixtures held
  the Wine slot for 20 minutes; the runner was interrupted while its `off` pass
  had just begun, and the partial `verification/results/bottle-X3/` was
  deleted so no half record exists.
- Suites 2-5 not run. Timing comparisons and FEX findings therefore absent.

## Exact next command (one suite at a time, after the guard prints `[]` and no
other `verification/probe/run_*.py` / fixture `.exe` / `wine ... fixture`
process exists)

```sh
cd /Users/asvetl/x3-mod
python3 -c "import sys;sys.path.insert(0,'verification/probe');import game_guard;print(game_guard.game_running())"
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/run_sampling_profiler.py
python3 -c "import json;s=json.load(open('verification/results/bottle-X3/sampling-profiler-summary.json'));print(s['passed'],s['bottle'],[k for k,v in s['checks'].items() if not v],s['notes']['sampler_cost'],s['notes']['overhead'])"
```

Then, in order: `run_loading_trace.py`, `run_motion_output.py`,
`run_temporal_pass.py`, `run_ownership_integration.py`, each with
`X3M_FIXTURE_BOTTLE=X3`, and fill the table in `bottles.md` (pass/fail, counts,
timings vs the Steam records, FEX-specific findings). Do not alter fixture
expectations for environment differences; record the differing check.
