# Run 225: black main menu after exit (triage 2026-11-02)

Log: /mnt/c/X3/x3m.log (untracked). Queries: `queries.sh`.

Measured:
- Exit-to-menu = `loading_phase save_load_begin` frame 28141 -> `save_load_complete` frame 28142, stall_ms=29546.
- During the stall (frame 28141 rows, tick 21423593..21423765): WM_ACTIVATE(0), WM_ACTIVATEAPP(0) from thread 0x2470,
  WM_DISPLAYCHANGE (5120x1440x32), WM_KILLFOCUS; game logs music_trace_stop name=alt_tab (qpc 214236228266, 29.28 s into the load).
  Frame 28142: window iconic=1 at -32000,-32000, foreground=00010742 (not the game window).
- frame_end present=88760868 (D3DERR_DEVICELOST) for frames 28142..29210 (1069 frames, qpc 214238861106..214280450389 = 41.6 s, ~26 fps avg incl. 1.3 s first frame);
  S_OK frames 29211..29794 (584). Frame 28141 last S_OK.
- Recovery: WM_ACTIVATEAPP(1) frame 29208, window restored frame 29210, reset_begin/reset_end result=0 (only reset of the session), telemetry_first_present frame 29211.
- own_sun_frame frame 28142 active=0, no own_sun_failed / *_failed rows; hdr_frame redirected=0 end=none from 28142 onward (menu).
- x3m.prev.log: no save_load / exit-to-menu, no WM_ACTIVATEAPP(0), resets=0: no comparison case.

Follow-up (2026-11-02): the user retried the exit to menu and it did not recur. Parked: the device loss followed a
focus loss during a 29.5 s load stall (ghost-window hypothesis unproven). A dev-tier diagnostic (`window_focus_lost`
foreground owner/class, message-pump gap during load stalls) is being added so the next occurrence is attributable.
