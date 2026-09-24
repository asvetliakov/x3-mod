# Window mode and cursor: disassembly and log evidence (2026-09-25)

Read-only scripts on `X3AP.exe` (bottle X3) and one-liners on the run320 log that
produced the facts in `docs/architecture/window-mode-and-cursor-fix.md`. No game
or Wine run. All three scripts take the EXE path from the bottle.

- `pe_imports.py [names...]`: import table (IAT addresses) and the `call [iat]`
  sites in `.text`. Result: the EXE imports `CreateWindowExA` (site 0x4dae0d),
  `RegisterClassA` (0x4dadd1), `SetWindowPos` (0x4db040), `AdjustWindowRectEx`
  (0x4db006), `GetMonitorInfoA` (0x4daeeb), `SetCursor` (0x4d3944, 0x4dae35),
  `SetCursorPos` (0x4d3978), `GetCursorPos` (0x4d383a), `DefWindowProcA`
  (0x4d3a79), `DirectInput8Create` (jmp thunk); it imports neither `ShowCursor`,
  `ClipCursor`, `SetWindowLongA`, `MoveWindow` nor `SystemParametersInfoA`, and
  none of those names occur as strings (no GetProcAddress path).
- `disasm.py <start> <end>`: capstone linear sweep with IAT names. Ranges used:
  `4dac90 4db0a0` (window routine `0x4dac90(w,h,bpp,flags)`), `4d3620 4d3a90`
  (window procedure `0x4d3620`), `4032f0 403330` and `4f95d0 4f9612` (callers:
  flags = VideoFlags | 0xe0).
- `xrefs.py <hex>|str:<name>`: `call rel32` sites and string references.
  `VideoFlags` string 0x562c78 is read at 0x4b7211 into `[[0x606f34]]`.

Log one-liners (`/tmp/x3-bottleX3-run320/session-20260925-014905-212.log`, 177 MB):

    grep -n "telemetry_presentation\|create_device" $L | head
    grep "telemetry_window device" $L | sed -E 's/.*style=([0-9a-f]+) exstyle=([0-9a-f]+) window_rect=([-0-9,]+) client_rect=([-0-9,]+)/\1 \2 \3 \4/' | sort | uniq -c
    grep "telemetry_window_context" $L | sed -E 's/.*clip=([-0-9,]+) .*/\1/' | sort | uniq -c
    grep -c "telemetry_cursor_api" $L

Results: create_before/after windowed=1 5120x1440 swap_effect=1 interval=1
refresh=0 focus_window=device_window=000a0064; four telemetry_window rows all
style=94000000 exstyle=0 window_rect=0,31,5120,1471 client_rect=0,0,5120,1440;
monitor 0,0,5120,1440 work 0,31,5120,1440; clip alternates -1512,0,5120,1440 (2)
and 0,31,5120,1440 (2); telemetry_cursor_api rows: 0 (the game never calls the
D3D9 cursor methods).

## Implementation evidence (2026-09-25)

- `dry_runs.py`: the four launcher dry runs (modded `--taa` default, `--window-monitor-rect off`,
  `--telemetry --window-trace --cursor-reassert`, `--vanilla`) against the installed bottle; prints
  the window/cursor variables each run would carry. Results in
  `docs/verification/window-and-cursor.md`.
- Wine records: `verification/results/bottle-X3/window-mode.{json,txt}` (runner
  `verification/probe/run_window_mode.py`) and `cursor-reassert.{json,txt}`
  (`verification/probe/run_cursor_reassert.py`). Summary one-liner:

      python3 -c "import json;d=json.load(open('verification/results/bottle-X3/window-mode.json'));r=d['report'];print(d['passed'],r['check_count'],r['failed_checks'],r['geometry'],r['screen'])"
