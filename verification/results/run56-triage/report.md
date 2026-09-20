Outcome: Run56 capture witness is ready for the checkpoint.

Evidence: Runtime session line 3 identifies `C:\\X3\\d3d9.dll` as 54,386,310 B with SHA-256 `a51d1e75fa80d7d07bab7ab66004291f7248bc5693e6585171e564a90fa96e56` and source commit `85da89a8955a72b20d92f2309e4b9a4cb4dd325e`, matching the candidate sidecar. Capture ranges are 16450–16481 and 43051–43082 (64 frames). Session/stderr scans found zero crash, fatal, segmentation-fault, unhandled-exception, and page-fault markers. Media skip is configured active (`id2_video_skip=1`); per-call execution is not observable because it returns before cue telemetry. Final-burst aggregate lightmap rows are admitted=60, faded=0, min_gain=4, camera=1; all-session motion apply/restore failures are zero.

Files changed: `/tmp/x3-run56-triage/triage.py`, `/tmp/x3-run56-triage/summarize.py`, `/tmp/x3-run56-triage/run56-capture-triage.json`, `/tmp/x3-run56-triage/run56-capture-summary.json`, `/tmp/x3-run56-triage/report.md`.

Open issues: The user is uncertain which burst was first/latest. No explicit process exit status is captured; final Wine `PROCESS_DETACH` only establishes termination. LAV runtime use is not established by the loader log.
