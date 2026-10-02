# Run403 engine plumes triage (Run 119 A, `--engine-effects plumes --debug`)

DLL 58227081... from bfe68791. Inputs (untracked): `/tmp/x3-bottleX3-run403/session-*.log`,
reference `/tmp/x3-bottleX3-run401/session-*.log` (`off`).

- `triage.py`: streams both logs, prints arming, engine_stage statistics, captures, preset, dt comparison.
- `extra.py`: frames with ribbons drawn 0 while live > 0, frames with no appended sample.
- `output.txt`: output of `python3 triage.py; python3 extra.py`.

dt window: frames 500..12823 (run401 last frame), capture frames excluded.
