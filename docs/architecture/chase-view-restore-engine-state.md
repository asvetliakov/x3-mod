# Chase view restore from engine state (no dependency on the compiled script)

Design note, 2026-10-12, for the question: restore the rear chase view (mode
258) after a sector change without depending on `L/x3story.obj` (its global
cell indices, class-variable layouts, CODE PCs and return-address prefixes),
keeping the guarantees of the current ticket (fail-closed arming from an
applied 258 frame, one use, never a ship the player no longer flies or after
death, never a write the script overwrites). Status: **ratified and
implemented 2026-10-12** as amended in section 4 ("Amendments"); the design
text below is kept as written, with its superseded parts marked. Owning notes: this file for the
design; [chase-view-transition.md](../reverse-engineering/chase-view-transition.md)
for the measured gate sequence; [chase-view-restore-and-hud-anchor.md](chase-view-restore-and-hud-anchor.md)
item 1 for the superseded script-seam design.

Evidence: the native session triaged under
`verification/results/native-chase-view-restore-2026-10-11/` (five transits,
mode 258 before and 1 after each, 2,696 arm refusals with reason 4 under the
Mayhem program), the stock-program runs 56/60/78/315 in the RE note, and
read-only Ghidra queries on the analysed `X3AP.exe` this session
(`ghidra_design_queries.sh` beside the triage; raw output untracked).
"Measured" below means observed in a logged run; "static" means read from the
executable; "inferred" is marked.

## Recommendation in two sentences

**Ratified 2026-10-12 (main session)** with one amendment: the option keeps its current default (on with `--camera chase`, as the launcher and `x3m.ini` template have had it since 2026-09-25), not "default off". (The `cancel=no_learned_key` row of the original ratification went with the learned key, amendment 3; the engine-only limit is now `cancel=no_monitor`.)

**Implemented 2026-10-12 under three further ratified amendments** (recorded in
section 4, "Amendments"): the key-replay write path of the text below was
replaced by the engine's own by-name script call `SelectMode(258)` on the
monitor object captured from the VM frame at the mode store, the trigger is the
cockpit replacement itself (no ship-id or sector gate), and `other_mode` is
terminal. Sections 1 to 3 and the trigger of section 4 stand; the "Learning the
key" subsection and the key-replay text are history.

Trigger from engine state only (armed cockpit freed through the cockpit-free
command, a fresh cockpit bound to the same ship-script id and the active
control handle, with a published sector different from the armed one), and
perform the restore by replaying the player's own last external-view input
through the engine's input-to-script call `0x0049f570` from the input routine
`0x00410100`, so the script's own `SelectMode(258)` runs and script and native
state stay coherent. Remove the script-seam implementation (the seven
`restore_specs` sites and the `0xf0c4b` payload mutation) rather than keep it
as a fallback: it can never fire under the user's program and a second state
machine on the same ticket doubles the fixture surface for no case the engine
path does not cover.

## 1. Who resets `+0x150` from 258 to 1 (question 1)

Nothing resets it in place. The gate and jumpdrive path frees the cockpit and
creates a new one; the "reset" is the fresh cockpit's first mode write:

- Writers of cockpit `+0x150` in the image (static, image-wide scan of
  register-relative `MOV [reg+0x150]` stores, 18 rows): constructor
  `0x0041fbe8` (writes 0), save deserializer `0x00419e06`, and the script
  command `INS_CockpitSetViewMode` (dispatcher `0x0042d340` case `0x30`, store
  `0x0042e742`, EAX = cockpit, ECX = mode). The other fifteen rows are
  `ESP`-relative locals or stores in routines outside the cockpit code
  (`0x004304b0`, `0x00434e40`, `0x004524d0`, `0x004c0150`); the earlier
  displacement scan reached the same three writers. There is no engine-side
  store of 1, conditional or not, and the per-frame update `0x004205e0` does
  not write the field.
- Sequence on a gate (measured, run56/run60, stock program; the same engine
  path under any program): `WarpToSector` → `StopAllMonitors` →
  `INS_CockpitFree` → native caller `0x0042d402` → destructor `0x0041ffc0`
  (old cockpit still mode 258, registry row already removed) … destination
  activation … `RestartAllMonitors` → `SelectMode(1)` writes the **script's**
  monitor mode cell → `StartMonitor` → `INS_CockpitAlloc` (constructor,
  caller `0x0042d397`, mode 0) → `INS_CockpitSetViewMode(1)` at `0x0042e742`
  (0 → 1) → `INS_CockpitSetSectorSpace` → sector publication (`+0x1fc`) on
  the second update.
- The script re-applies its mode cell on every monitor `Show`
  (`StartMonitor` → `0x0042e742` with the cell value): measured 560 ms after
  the sector publication on the gate (run56 event 32, `280.Open`), before the
  free (event 22, `280.Close`), and on docked menus (run315 event 51). So a
  bare native write `+0x150 = 258` is stable only until the next monitor
  `Show`; it is then overwritten with 1 **by the script's own cell through
  the same engine store**, not by engine logic. Any design that writes the
  native field without the script's cell being 258 fights the script on every
  menu open or close.

## 2. How the engine enters 258 on the player's key (question 2)

There is no engine-side "set view 258" entry point. Static facts:

- Main loop `0x00403840` → `0x00403db8` → `0x00404280` → `0x00410100(object,
  flag)` once per loop iteration; `object` is the active cockpit's `+0xc` (or
  `+0x10` when mode bit 2 is set, `0x00404344`), `flag` is `(state != 2)`.
  `0x00410100` is 8,446 bytes with exactly two callers; `0x00404280` has one.
- Input events are raw: keyboard keys `0..0x1ff` (down table
  `*0x00606f3c + 0x10 + key`), joystick buttons `0x128 + i`, hat `0x140..0x147`,
  mouse buttons `0x2126/0x2127`, shift state adds `0x1000`
  (`[*0x00606f3c + 0x444] != 0`). Keyboard key-down calls the script
  `TranslateKey(key)`; the translated value lands in `[*0x0057fc60 + 0x499]`
  (0, the key itself, or ≥ `0x400` mean "untranslated"; then the press time
  is stored at `0x00607cf8[key]`). Keyboard key-up calls `Input(key | 0x2000)`
  for a tap (held ≤ 500 ms) or `Input(key | 0x4000)` for a hold
  (`0x00410ac3..0x00410b61`). Joystick, hat and mouse call `TranslateKey`
  then `Input` on press. After each call the engine sets `[0x00607c58] = 1`
  if `+0x499 == -1`, `[0x00607c60] = 1`, and clears the press time.
- All of these are by-name script calls through `0x0049f570`: cdecl, seven
  dwords `(object_id = [*0x0057fc60 + 0x418], 0, class 0x96, name, 1, argc,
  value)`, caller pops `0x1c`. The wrapper resolves the method (`0x0049f330`),
  pushes `argc` tag-1 cells and the context cell, runs the task synchronously
  (`0x0049f430` → interpreter `0x004a26a0`) and frees the task if it is still
  in the VM's task map. `[game + 0x490]` is zeroed (word) before each call.
  The class id `0x96` and the names `TranslateKey`, `Input`, `InputAction`
  are constants in the EXE, so every program the engine can run has them; the
  key-to-view binding is script and user-configuration data.
- The engine knows no view action id: the `InputAction` ids in the EXE are
  the hat directions `0x2cd..0x2d4` (`0x0040f850`) and mouse steering
  `0x230/0x238` (`0x00411da6`); `INS_SetExternalView` (`0x4e`) is a no-op;
  the view-mode command is inline in the dispatcher and reachable only with a
  marshalled task. The view key's script path is measured (run315 events
  59–78): `96::Input → 96::Vbi → 25d::Vbi → ChangeView2 →
  ChangeViewForMonitor → SelectMode(258)` → cell write → `0x0042e742`.

Consequently the only script-coherent native path is the engine's own
input-to-script call. The design replays the exact `(method, value)` the
player's own key produced, through the same wrapper, from the same routine.
This is "mimicking the key press" at the engine's seam, not at the OS level.

## 3. Identity and liveness from engine state (question 3)

| Old ticket check (script) | Engine-state equivalent | Status |
| --- | --- | --- |
| Global cell 9 == ship `+0x94` (player ship) | Ship script id `[ship + 0x94]` (engine field written by the engine, read by `SA_GetEventObject` `0x004607f4`) of the cockpit's `+0x10`, with `+0x10 == +0xc != 0`; equal before and after the transit. Survives the native body recreation: run60 event 29 keeps `fffefa78` while the native id changes `8db → 415` (measured, stock); the field is engine-owned, so the same holds under Mayhem (inferred; the Mayhem arm rows read it as `0xffff1766`) | kept |
| Global cell 8 (controller) | `[*0x0057fc60 + 0x418]`, the engine's own copy of the class-0x96 controller object it calls `Input` on; scalar, compared arm vs. consume | kept |
| Class-0x96 cell 3 warp phase | Dropped. The warp is proven by the engine sequence itself: armed cockpit freed through caller `0x0042d402`, fresh cockpit from caller `0x0042d397`, published `+0x1fc` ≠ armed sector | dropped |
| Class-0x96 cell 6 killed | Ship liveness: native registry membership of `+0x10` by `[ship + 8]` through `*0x0060850c + 0x14` (already in `IdentityReader::capture`, valid under Mayhem: `native_bit` set), `[ship + 0x40] & 0x08000000` clear (deferred-delete flag, inferred from the sweep `0x0045b660`), re-read at consume. Death replaces the player ship (`SetPlayerShip`), so `+0x94` differs | replaced |
| Dynamic VM context membership (`vm + 0x12d0`) | Dropped: it refused in every run including stock run60 (`identity_refused` 3696) and the Mayhem rows (2696); it never contributed | dropped |
| Monitor class `0x25e`, cells 0/1/11/16/17, seam PCs, return prefixes | Dropped: program layout | dropped |
| Ship switch (`ChangePlayerShipTo`, `e5da1`) | `+0x94` differs on the new cockpit → cancel; a switch inside the sector also fails the sector test | covered |
| Foreign monitor | Active-control proof `chase_transition::active_control_cockpit` (registry `*0x00608504 + 0x10` handle maps to the cockpit) on both the armed and the new cockpit; `+0x10 == +0xc` once bound (run78: `+0x10` is 0 for the first 36 updates of a fresh generation, so wait for it) | kept |
| Id reuse | `+0x94` equality is only accepted with registry membership, the active-control proof and the ship as the engine's own controlled object (`[ESP+4]` of `0x00410100` at the injection site, `0x00404344..0x00404352`); the window is bounded by the expiry | kept |
| Docking kind 7 (deserializer `0x00419e06`) | Unchanged, engine site | kept |
| VM epoch (construct `0x0049c9a0`, clear `0x0049ea80`, load `0x004a0880`, EH adapter `0x0052f298`) | Engine sites, program-independent; keep load and clear as cancels. The seam-specific task/abort sites `0x004a2260`/`0x004a2420` are dropped with the seam | kept (3 of 7) |
| Expiry | Unchanged: 600 cockpit updates from pending, cancel only | kept |
| Deliberate internal view then jump | Still indistinguishable from the reset; the trigger is "258 applied on the frame before the free" as the brief fixes it | accepted |

Docking is excluded by construction: the dock path frees and recreates the
cockpit in the same sector (run315), so `+0x1fc == armed sector` cancels
(`cancel=same_sector`); the user dropped the dock restore on 2026-09-25.

## 4. Recommended design (question 4)

### Amendments (ratified 2026-10-12, implemented in `chase_transition_restore_core.h`)

1. **View-cycle keys (superseded by 3).** A learned key may be a view toggle,
   so `result=other_mode` was to retry with a cycle detector
   (`cancel=cycle_no_258`) and `chase_view_restore_retries` became 0..5,
   default 3. The default stays 3; the cycle logic went with the key replay.
2. **Trigger is the cockpit replacement, not the sector change, and it
   restores across ship changes.** Step 4 (Due) drops the "`+0x94` equal to
   the armed value" and "`+0x1fc` differs from the armed sector" conditions
   (`cancel_same_sector` and the ship-id identity cancel are gone); kept are
   the bound cockpit being the player's active-control cockpit
   (`+0x10 == +0xc != 0`, active-control proof, registry-live ship with the
   deferred-delete flag clear), mode 1 for two consecutive updates, connect 0,
   `+0x1a0 & 4` clear, the engine's controller object unchanged
   (`cancel=identity`), kind-7 cancel, `cancel_mode_left`,
   expiry. Pending/bound stay tied to the free command `0x0042d402` and the
   alloc command `0x0042d397` so a load chain still cancels. The outcome row
   logs `ship_id_from/to` and `sector_from/to` instead of gating on them.
   Rationale: the cockpit-based trigger restores whatever the player now
   flies; the old ship-switch protection guarded a script-side ticket for a
   specific monitor, which no longer exists.
3. **Direct `SelectMode(258)` instead of key replay** (the user judged key
   replay too fragile). Site L, the learned-key state, the retry-on-cycle
   logic and `no_learned_key` are dropped. Consume stays at site I
   (`0x00410100` entry, the input phase, outside the registry walk) and calls
   `0x0049f570(0, 0, monitor, "SelectMode", 1, 1, 258)` with a mod-owned
   `"SelectMode\0"`. The monitor object comes from engine state: at the kind-6
   observer `0x0042e742` (the script's `StartMonitor` storing mode 1 into the
   fresh cockpit, and every later `Show`) the dispatcher frame gives
   `[ebp+4] == 0x4a3909`, `[ebp+0xc]` = task, `[task+0x3c]` = the executing
   script object's context, `[context]` = its object id, `[context+8]` = its
   class descriptor, `[desc]` = class id (0x25e on stock; read, not assumed).
   Established with Ghidra this session (static): (a) `0x0049f330` writes
   `task+0x3c` from the context `0x0049f1e0` resolves and `task+0x30` from the
   first argument, the interpreter's own `DYNCALL` (opcode 0x85) likewise sets
   `task+0x3c` to the object context it resolved through `0x0049f1e0`; (b) the
   third argument of `0x0049f570` is the lookup key, not a class hint: `EAX =
   [ESP+0x44]` (arg 3) at `0x0049f59c` feeds `0x0049f1e0`, which takes a
   non-negative value to the static class table (`0x004b06f0`) and a negative
   one to the dynamic object table at `vm+0x12d0` keyed `~id`; the method is
   then searched by a content `strcmp` (`0x004b0740`, binary search over
   `[desc+0x2c]`, count `[desc+0x28]`, parent chain `[desc+0x14]`); passing the
   monitor's own (negative) id as both object and key reproduces the
   interpreter's `monitors[0].SelectMode(1)` frame; (c) the wrapper returns
   `AL = 0` when the object or the method is missing (`0x0049f66c`), which the
   ticket logs as `result=call_refused`. The name is matched by content, so
   the EXE needs no `SelectMode` literal. Both compiled programs carry exactly
   one method row named `SelectMode` with argument count 1 (stock
   `addon/04.cat`, CODE entry `0xf07cc`; Mayhem `addon/07.cat`, `0x106b1f`;
   `verification/results/native-chase-view-restore-2026-10-11/check_selectmode.py`).
   A bound cockpit with no captured monitor cancels `cancel=no_monitor`;
   `result=other_mode` is terminal (`cancel=other_mode`); `no_effect` keeps
   its bounded retry (`chase_view_restore_retries`, default 3, ≥ 60 updates
   apart, then `cancel=retries`). Program dependency left: the method name
   `SelectMode` and its one-argument convention, checked at run time by the
   call result.

Two corrections from implementation: the input routine's body is
`[0x00410100, 0x00412250)` (Ghidra function body: 13 ranges, last byte
`0x0041224f`, the `0x004121fe` figure below summed the size over the alignment
gaps), and `cancel=second_destruction` applies only while nothing is bound yet
(after the fresh cockpit is bound, only its own destruction cancels,
`cancel=lifetime`). Review fixes 2026-10-12: the monitor capture is keyed
by (cockpit address, generation), so a fresh cockpit reusing the freed address
restores (host case 17, the reviewer's repro kept under
`verification/results/native-chase-view-restore-2026-10-11/reuse_repro.cpp`);
only a negative (dynamic) object id is accepted from the VM frame; the
injection entry checks the epoch; a call that never reports back expires as
`cancel=call_unwound`; a refusal on a cockpit other than the armed one no
longer cancels the arm; the VM frame is read only once a ticket is past the
arm; re-arm frames of the armed (cockpit, generation) skip the registry walk,
the ship hash and the controller read (the identity is re-proved at due and
at the injection entry; `rearm_frames` counts them).

### Learning the key (superseded by amendment 3)

- Site L, `0x0049f570` entry (`83 ec 0c 53 55 56 57`, 7 bytes, whole
  instructions). Prefilter: return address `[ESP]` in
  `[0x00410100, 0x004121fe)`; otherwise return at once (this wrapper also
  carries `KilledBy`, `MakeDamage`, `CanLand` and the per-object collision
  calls, so the stub stays at a few compares). For an input-routine caller
  with class `0x96` and name pointer `0x0055537c` (`Input`) or `0x0055539c`
  (`TranslateKey`), record `{name, value = [ESP+0x1c], thread, serial}` as
  the in-flight input call (thread-local, overwritten per call, cleared at
  the next `0x00410100` entry).
- The existing kind-6 observer at `0x0042e742` learns: requested 258, cockpit
  is the active-control cockpit, `+0x10 == +0xc`, previous mode ≠ 258, and an
  in-flight input call on this thread → `learned = {name, value}` (most recent
  wins; one `chase_view_restore_learn` row per distinct value, bounded to 8
  rows per session). Nothing is learned from loads (kind 7) or from writes
  outside an input call (menus, script restarts).
- No learned key means no restore: a session that reached 258 only through a
  save load logs `cancel=no_learned_key` at the first due ticket, the user
  presses the key once, and every later transit restores. This is the
  honest limit of an engine-only design; see §6 for what could lift it.

### Ticket state machine (engine observations only)

1. **Arm** on every applied rear frame at the chase hook `0x00420e06` (mode
   258, connect 0, `+0x1a0 & 4` clear, `+0x10 == +0xc != 0`, active-control
   proof, registry-live ship): store generation, cockpit pointer as an opaque
   key, `+0x94`, controller id, sector `+0x1fc`. Re-armed each frame; eleven
   scalars, no allocation. Arm refusals keep reason codes 1–3 and gain
   `arm_ship_dead`.
2. **Pending** at the destructor `0x0041ffc0` when the cockpit is the armed
   one and the native return caller is `0x0042d402` (the free command); any
   other caller (`0x0041ccfb` deleting destructor, load chain) cancels
   (`cancel_destructor`). A second destruction while pending cancels.
3. **Bound** at constructor completion (`0x0041fe04`) of a cockpit whose
   construction caller was `0x0042d397`; a load-chain constructor (call at
   `0x0041f83b`, recorded return address `0x0041f840`) cancels
   (`cancel_lifetime`).
4. **Due** at the chase hook on the bound cockpit when, for two consecutive
   updates: mode 1, connect 0, `+0x1a0 & 4` clear, `+0x10 == +0xc != 0`,
   active-control proof, registry-live ship with `+0x94` equal to the armed
   value, controller id unchanged, `+0x1fc` non-null. `+0x1fc ==` armed sector
   cancels (`cancel_same_sector`: dock, carrier, in-sector ship change).
   (2026-10-12, after amendment 2: `+0x1fc` is no condition at all, because it
   is script-published, written only from a script call's result at
   `0x004210d6` and zeroed each update at `0x00421024`, and reads 0 on every
   frame under Mayhem, so Run 146 A never bound; the outcome row logs it and
   the `+0x54` sector pointer as `sector_from/to` and `sector54_from/to`.) Mode
   ≠ 1 before due cancels (`cancel_mode_left`: the player already pressed a
   key, or a cinematic). 600 updates from pending cancels (`cancel_expiry`).
5. **Consume** at site I, `0x00410100` entry (`55 8b ec 83 e4 f0`, 6 bytes),
   on the first entry while due: fresh re-check of 4 plus `[ESP+4]` (the
   engine's controlled object for this call) `== +0xc` of the bound cockpit
   and `[*0x0057fc60 + 0x4a0] & 1` clear (the main loop skips the sector
   update under that bit; inferred "paused or menu" from
   [main-loop-input-region.md](../reverse-engineering/main-loop-input-region.md)).
   Then replay the engine's own key-up sequence with the learned value:
   `[game+0x490] = 0` (word); `0x0049f570(controller, 0, 0x96, name, 1, 1,
   value)` with the EXE's own string pointer; `if ([game+0x499] == -1)
   [0x00607c58] = 1`; `[0x00607c60] = 1`. The ticket is cleared **before**
   the call (one use). The kind-6 observer sees the result synchronously:
   a 258 store on the bound cockpit inside the call → `result=restored`;
   another value → `result=other_mode` (no retry); no store →
   `result=no_effect` and at most `retries` (default 2) further attempts
   spaced ≥ 60 updates while 4 still holds, then `cancel_retries`. A retry
   after `no_effect` does not fight anything: the script changed nothing.
6. After `restored`, the script's cell is 258, so every later `Show`
   re-applies 258 and the view keys behave as after a manual press; the
   chase camera applies the pose from the next update as it does today.

### Cancels not yet listed

Deserializer write (kind 7) at any time; VM load `0x004a0880` and clear
`0x0049ea80` advance the epoch and clear everything; learned key stays. A
`0x0049f570` failure (method not found: the wrapper returns 0) logs
`result=call_refused` and cancels.

### Hook-site, ABI and lifetime contract for the engine call

- Site I runs inside the main loop's input phase, outside the registry walk
  `0x0041cde0` and outside every cockpit update, which is why the call is
  not made from the chase hook: the script may free or recreate the cockpit
  that `0x004205e0` is updating. At site I the engine itself is about to make
  the same call with the same arguments shape; VM reentrancy, task lifetime
  and script yields are the engine's own.
- The stub saves and restores GPRs, flags and LastError around the handler
  and replays the displaced prologue; the handler holds no lock, no RAII
  object and no borrowed engine pointer across the call (C++ EH unwinding
  from a script error passes through the frame; the EH adapter `0x0052f298`
  epoch bump stays as the cancel). All engine addresses are read through the
  existing checked readers; the string pointers are verified against the
  EXE bytes at install (`"Input\0"`, `"TranslateKey\0"`), with a refuse
  path if they differ (foreign EXE).
- The two new sites are plain whole-instruction copies; the site verifier
  must prove no `.text` branch targets their interiors, as
  `verify_chase_restore_sites.py` does for the seam spans.

### Config, logging, removal

As implemented (supersedes the bullets that follow): option name unchanged,
default on with `--camera chase`; `X3M_CHASE_VIEW_RESTORE_RETRIES` /
`--chase-view-restore-retries` 0..5, default 3, an invalid value refuses the
install; install line `chase_view_restore installed=1 requested=1 path=engine
sites=1 cancel_sites=3 inject_site=0x00410100 call=0x0049f570
method=SelectMode mode=258 observers=1 retries=3 retries_valid=1
expiry_updates=600 due_frames=2 retry_spacing=60`; rows `chase_view_restore
result=… cancel=… attempts= monitor=0x… monitor_class=0x… mode_seen=
generation_from/to= ship_id_from/to= sector_from/to= sector54_from/to= wait_updates= frame=
soft_unreadable= soft_mode_zero= soft_connect_flags_view= soft_no_handle= soft_ship_dead=
skip_caller= first_skip_caller= first_soft=reason,+0x1fc,+0x54,mode,handle`
once per ticket outcome, `chase_view_restore_arm_refused` once per reason,
the windowed `chase_view_restore_state` (phase, counters, results, cancels),
no learn row. The handler moves the ticket to `phase_calling` (never again
due, `attempts` counted) before the engine call rather than clearing it, so
the kind-6 observer can attribute the store inside the call; a call that
never reports back (a script error unwinding through it) expires on the
600-update clock as `cancel=call_unwound`.

Original bullets (superseded):

- Option name unchanged: `--chase-view-restore` / `X3M_CHASE_VIEW_RESTORE=1`,
  requires `--camera chase`, default off. New `X3M_CHASE_VIEW_RESTORE_RETRIES`
  (default 2, 0..5). The install line becomes `chase_view_restore installed=1
  path=engine sites=2 learn_site=0x0049f570 inject_site=0x00410100
  retries=2 expiry_updates=600`.
- Rows: `chase_view_restore_learn method=Input value=0x… cockpit=0x…
  mode_before=1 frame=…` (bounded); `chase_view_restore
  result=restored|no_effect|other_mode|call_refused|cancelled
  cancel=<reason> method=… value=0x… attempts=n generation_from=… to=…
  ship_id=0x… sector_armed=0x… sector_now=0x… wait_updates=… frame=…` once
  per ticket outcome; the windowed `chase_view_restore_state` line keeps its
  counters with the seam fields removed; `chase_view_restore_arm_refused`
  keeps its shape with the identity bits reduced to the engine set.
- The script path is removed: `restore_specs` (seven sites), the `0x004a3ffd`
  seam decoder, prefix walks and the class/cell constants in
  `chase_transition_restore_core.h`. The RE note keeps the seam design as
  history. Run78 remains the only consumed stock witness; the engine path is
  re-validated on stock by the same acceptance rows when the user next runs
  the stock program, not as a prerequisite for the Mayhem install.

### Run147 crash: argument 0 of the engine call (2026-10-12)

Run147 passed the monitor id as argument 0. Measured from the sector-change minidump (`verification/results/native-chase-view-restore-crash-2026-10-12/`): `SelectMode(258)` resolved by name and ran to its return (the crash reporter's `GetMode(258)` label is the task PC having run off the end of SelectMode into the next method's entry); then the task-completion code `0x004a2260` -> `0x004ab170` took `task+0x30` = argument 0 = -531 as a completion-callback slot, passed the only bound check (`cmp ecx,0x20; jge`), loaded `[vm + slot*24 + 0x34]` from heap data and executed `call eax` (eip 0x4d4d5544). The engine's 46 callers pass 0 (no callback) or the game's registered slot `[game+0x418]` (whose callback `0x004a4910` they prime by clearing word `game+0x490`); argument 1 is the callback cookie, argument 2 the lookup key, argument 4 the return kind. The call is now `0x0049f570(0, 0, monitor, "SelectMode", 1, 1, 258)`: with slot 0 the completion step skips the callback entirely (`0x004a2268 test ecx,ecx; je`). The CPU fixture's mock asserts argument 0 == 0.

### Cost on the hot path

- Chase hook: about fifteen integer compares and an eleven-dword copy per
  cockpit update, as today; the registry-live and active-control walks are
  the ones the chase camera already performs on these frames.
- Site L: one load of `[ESP]`, two compares and a return for every by-name
  script call outside the input routine; inside it, three loads and a
  thread-local store. Expected tens of nanoseconds per call (inferred; the
  `chase_view_restore_timing` row measures it per kind as the seam timing did).
- Site I: once per main-loop iteration, one state load when idle.
- Consume: one engine call that runs the script's own view selection, once
  per transit; the same work a key press costs.

### Native Windows

Same non-relocatable image, same cdecl call, documented memory APIs only for
the page checks; no Wine-specific export or layout. The engine call is the
first proxy hook that calls back into the EXE; nothing in it is platform
specific. Unverified on native Windows like every hook; the acceptance run is
native.

### Verification

- Host fixture: the state machine is pure (`chase_transition_restore_core.h`
  rewritten to engine events). Cases: applied 258 → free (caller `0x0042d402`)
  → construct (`0x0042d397`) → two due frames with a new sector → one
  injection with the learned value, cleared before the call; same sector →
  `cancel_same_sector`; `+0x94` differs → `cancel_identity`; view unbound
  for 36 updates then bound → due only after binding (run78 shape); load
  chain constructor → `cancel_lifetime`; kind 7 → cancel; mode left before
  due → cancel; no learned key → `cancel_no_learned_key`; `no_effect` → at
  most `retries` attempts spaced ≥ 60 updates; a learn event outside an
  input call or from kind 7 learns nothing; a learn with a different value
  replaces the old one; expiry.
- CPU boundary fixture: sites L and I with the driver mirroring the displaced
  instructions, register/flag/LastError preservation, the return-address
  prefilter, and a mock `0x0049f570` that records its seven arguments and the
  `+0x490/+0x499/0x00607c58/0x00607c60` side effects; rollback of a refused
  site; `check_no_x87.py` on the DLL.
- Acceptance (native, user): with `--chase-view-restore`, press the external
  view key once after loading, then one gate jump and one jumpdrive. Expected
  rows: one `chase_view_restore_learn`, then per transit `chase_view_restore
  result=restored attempts=1`, the following `chase_camera` window
  `mode=258`, the view keys normal afterwards, and no view change on the next
  menu open or close (the coherence check that distinguishes this design from
  a bare write). `result=no_effect` on both transits is the signal for §6's
  first unknown.

## 5. Alternatives considered

- **Bare `+0x150 = 258` at the chase hook after sector change (triage shape
  a/b).** Engine-only and cheap, but §1 shows the script re-applies its cell
  (1) on every monitor `Show`: the view would revert at the first menu after
  each jump (measured re-application 560 ms after the gate in run56). Re-
  writing on each revert is the persistent force the RE note forbids. Loses.
- **Rewrite ECX at `0x0042e742` for the fresh cockpit's first store.** Same
  incoherence (cell stays 1), and the store site cannot tell a re-application
  from a deliberate internal selection without script provenance. Loses.
- **By-name call of script internals** (`25d::ChangeViewForMonitor`,
  `25e::SelectMode`) through `0x0049f570`. Originally rejected because the
  monitor object id had no engine-side source; it has one (the VM task frame
  at the kind-6 store, amendment 3), and the remaining program dependency is
  the method name and its one-argument convention, which the call result
  checks at run time. **Adopted (amendment 3)** over key replay, which depends
  on the binding, the press state and view-cycle semantics.
- **`InputAction` with a view id.** The EXE defines no such id (§2). Loses.
- **OS-level key synthesis** (`SendInput`, `PostMessage`). Depends on the
  physical binding, the window focus and the input backend; the engine seam
  gives the same effect one level down with the binding learned. Loses.
- **258 → internal without a sector change (shape c).** Cannot separate a
  key press from the reset; rejected as the brief states.
- **Keep the script-seam path as fallback (shape d).** It fires on no
  program but stock, its cancel sites are silent under a mod, and two state
  machines on one ticket need a merged fixture. Removal is cheaper and
  safer; stock is re-validated by the same acceptance rows.

## 6. Unknown, and what settles it

1. **Whether one replayed `Input(key | 0x2000)` selects the view** (the key-
   up event alone, without the preceding key-down `TranslateKey` frames). The
   measured chain starts at `96::Input` (run315), which favours the key-up
   event, but the script may keep press state from `TranslateKey`. Settled by
   the acceptance run: `result=no_effect` on both transits means the learner
   must also keep the last `TranslateKey` value for the same key code and the
   replay must issue it on the previous loop iteration (two-step replay, same
   sites). Stock-only static disassembly of `96::Input`/`96::TranslateKey`
   (the installed `x3story.obj` is Mayhem's; the stock listing is available)
   would also answer it before the run.
2. **Input during the warp-out scene or a menu.** If the script ignores the
   replay there, the retries cover it; `[game+0x4a0] & 1` as "menu or pause"
   is inferred from the main-loop skip and should be confirmed by one
   `0x00404280` listing (`0x0040429a..`) before implementation.
3. **`+0x94` continuity across the transit under Mayhem** is inferred from
   engine ownership of the field; the first run's `chase_view_restore` row
   reports `ship_id` on both sides and settles it.
4. **Key rebinding mid-session** is not detected; the most recent learned
   value wins and the replay validates its effect, not its meaning. A
   rebinding between the learn and a transit would replay the old key's new
   action once. Lifting this needs the native command `TranslateKey` uses to
   read the binding table (a disassembly of that script method on stock and
   of its native group/command in `0x004a3880`'s tables), which would also
   allow pre-learning from the configuration after a load in 258.
5. **Per-call cost of site L** under combat load (many by-name calls per
   frame) is inferred; the timing row measures it.
