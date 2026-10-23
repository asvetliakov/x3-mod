# Combat-music trigger narrowed to engagement

`src/proxy/music_alert.cpp`, `music_alert.h`, `music_alert_core.h`. Options `music_combat_trigger` and
`music_combat_hold_s` (`[audio]`), launcher `--music-combat-trigger` / `--music-combat-hold-s`. Engine facts:
[music-script-driver.md](../reverse-engineering/music-script-driver.md) sections 2, 5 and 9. Ledger:
[verification/music-combat-trigger.md](../verification/music-combat-trigger.md). Unflown as of 2026-10-23.

## Problem

The engine's alert routine `0x0042c890` raises the alert level (and so `NotifyAlert(1)`, combat music) whenever any
object hostile by the race-relation test `0x00450890` is within 2,500,000 units (largest axis) of the player ship, or
5,000,000 once the alert is on, every frame, with no damage term and no timer. A hostile station or an idle hostile
ship in range starts combat music; nothing has to happen.

## Mechanism

One six-byte claim (`engine_patch`, the backend-load install window, the claim protocol of `music_keep.cpp`) at
`0x0042c974`, the two whole instructions `mov ecx,[esi+8]; mov eax,[esi+4]` that read this frame's walk level `[R+8]`
and the committed level `[R+4]` (`R` = the cockpit registry `*0x00608504`, ESI; EDI = `P`, the player ship or 0;
EAX/ECX/EDX and the flags are dead; the only branch onto the site, the `jle` at `0x0042c96f`, lands on its first byte).
The five patch bytes `0x0042c974..0x0042c978` cross the 8-byte boundary at `0x0042c978`, so `engine_patch` writes
them with its plain copy, not `lock cmpxchg8b` (the install row says `write=plain`). That is safe here because the
write happens inside the install window on the backend-load thread, before the main loop exists and before any
thread can execute the routine; nothing reads those bytes until the game's first frame.

The integer stub saves EFLAGS and EAX/ECX/EDX, clears DF (the handler's reads are `rep movsb` copies; `popfd`
restores the engine's DF), calls `x3m_music_alert_frame(R, P)` (cdecl, four-byte stack), restores them and leaves
through the claim tail, which replays the two moves (reading the filtered `[R+8]`) and jumps to `0x0042c97a`. The
handler runs once per main-loop iteration on the main thread under `LightCallBoundary` (MXCSR and LastError), reads
engine memory only through `engine_memory::read` (VirtualQuery-validated), calls no engine routine and no script,
allocates nothing, and writes one word at most: `[R+8] = 0`.

Rule, evaluated every frame the thunk runs (`now` = the game clock `[*0x00606f34+0x718]`, game ms, so SETA scales it):

1. Stamp `last_engaged = now` if any of
   * **(a)** the player's fire counters `M+0x40` / `M+0x44` (`M = *0x0060850c`) rose since the last frame, and the
     selected target `cockpit+0x1e0` is non-null and hostile by `0x00450890`. The target pointer is never dereferenced
     on its own: it must be found in the sector's lists 5, 6, 7 or 10, and the relation test is a reimplementation of
     `0x00450890` / `0x00450810` over validated reads (both routine bodies are pinned byte for byte, so a changed routine
     refuses the install).
   * **(b)** the player ship carries Flags2 `[P+0x44] & 0x80000` (hit by a projectile this frame; the collision pass
     clears it before the alert routine each iteration).
   * **(c)** a player-owned object (`(int16)[obj+0x3c] == 10`, not P) within 2,500,000 units of P (Chebyshev over
     `[obj+0x70]+0x30..0x38`, the routine's own `0x00419280`) carries Flags2 `0x80000`, or, for a class-7 ship only
     (player stations and missiles carry group 10 too and have no order word), holds order `flight+0x258` in
     {`0x10`, `0x11`} whose target `flight+0x224` is non-null and hostile.
   * **(d)** the vanilla level `[R+8]` is 2: the walk found a missile targeting P, or the missile homing update raised
     it (a hostile missile on the player).
   * **(e)** the player's hit counters `M+0x48` / `M+0x4c` rose and an object in the lists carries the player-hit bit
     Flags2 `0x40` and is hostile (a shot that hit an unselected hostile; asteroids and friends do not count).
   * **(f)** a hostile class-7 ship holds order `0x10`/`0x11` whose target is P, or a player-owned object within
     2,500,000 units of P. The relation test runs once per ship that has such an order on such a target.

   The deliberate range for (c) and (f) is the routine's first-level range, 2,500,000 units, also while the committed
   level is nonzero (the routine's own stay range there is 5,000,000): an owned ship fighting 3 to 5 km away does not
   start or hold combat music. (a), (b), (d) and (e) stamp on any frame; (c) and (f) are evaluated only on frames
   where the vanilla level is nonzero, by one walk of lists 5, 6, 7 and 10 of P's sector (`[P+0x54]`, class 1), at
   most 4096 nodes per list (a cut is counted), stopping when every wanted answer is known. (a) and (e) share that
   walk on the frames where a counter rose.

   The counters are statistics a save restores. They are a new baseline, not a shot, on the first sample, on the
   `save_load_complete` marker (`capture.cpp` Present path, the event `fov::loaded` takes; once per process), on a
   changed player ship pointer (a load rebuilds the universe; a ship change costs one frame at most), on a clock that
   went backwards, and on a decrease.
2. `engaged = stamped && last_engaged <= now && now - last_engaged < hold_ms`. A stamp newer than `now`
   (`TI_SetAbsTime` rewound the clock) is expired.
3. If the vanilla level is nonzero and not engaged, write `[R+8] = 0`; else leave it. The vanilla range stays the
   precondition: the level is never raised, and with `[R+8]` held at 0 the committed `[R+4]` stays 0, so the stay range
   (5,000,000) applies only while engaged. A fight that starts after a hostile is already in range produces a fresh
   0 → 1 edge, so `NotifyAlert(1)` fires on the first engagement event; the music drops when the hold runs out or the
   hostiles leave range, whichever comes first.

Fail closed: `trigger=vanilla` patches nothing; an unknown word patches nothing (`invalid_setting`); the executable
identity (`object_trace::executable_verified`), the 18 windows of `music_alert_core.h` (708 bytes: the routine head,
the range test with its 2,500,000 / 5,000,000 constants, the site in context, the commit tail, the whole relation,
stance and distance routines, and the instruction witnesses of every field offset the rule reads), the site bytes and
the alert routine's call site (`0x0041cdea` → `0x0042c890`) are compared before the one write. The main loop's call
`0x00403f2f` → `0x0041cde0` is checked only by the verifier on the unmodified image: the game-phase group
(`X3M_GAME_PHASES=1` / the draw trace, `game_phase_sites.h` "game_phase_cockpits") legitimately redirects that call in
the same install window, and the site is reached from `0x0042c890` whichever caller enters the walk, so the feature
installs with game phases on. `late_claim` after the install window closes; a claim that fails after going live is
restored (`rollback_failed` keeps it registered for `shutdown()`); a stub left live with `armed_` false replays the
moves unchanged. An unreadable input (any `engine_memory::read` refusal) ends the frame's decision with no write.

## Options

| Key | Type | Default | Launcher | Effect |
| --- | --- | --- | --- | --- |
| `music_combat_trigger` | `engaged` / `vanilla` | `engaged` | `--music-combat-trigger`, sent on every modded launch, dropped and refused under `--vanilla` | `vanilla` = nothing patched |
| `music_combat_hold_s` | int 1..600 | `20` | `--music-combat-hold-s N` (range-checked), sent on every modded launch | the hold in game seconds; the file parser refuses an out-of-range value (default stands), the environment path logs `hold_source=invalid_default` |

The hold is game time (`cfg+0x718`): with SETA at 10x a 20 s hold lasts 2 s of wall time. Player mode (`--config`)
sends neither unless given explicitly.

## Log rows (`x3m.log`, `music_alert`)

* Install, once: `music_alert trigger=<engaged|vanilla|invalid> hold_ms=<n> hold_source=<default|setting|invalid_default>
  patched=<0|1> armed=<0|1> reason=<ok|vanilla|invalid_setting|executable_mismatch|late_claim|bytes_mismatch|pin_failed|
  <claim status>|chain_failed|rollback_failed> site=0x0042c974 continue=<tail> write=<plain|atomic|none> stub=<va>
  windows=19 walk_limit=4096 window=<0x…|none> window_name=<name|-> expected=<hex|-> got=<hex|unreadable|->` (`write=plain`
  is the expected kind at this site, see above; `windows=19` counts the 18 windows and the site; on `bytes_mismatch` the
  last four fields name the first window that differed and its first 8 bytes as expected and as read).
* One per edge of the filtered level (the level the engine commits): `music_alert edge from=<old> to=<new>
  vanilla=<[R+8] before> committed=<[R+4]> reason=<letters a..f|-> age_ms=<now − stamp> cause=<stamp|held|expired|
  newer_than_now|out_of_range|none> now=<game ms> step=<frame step> seta=<(cfg+0xcc·cfg+0xd0)>>16> visited=<nodes>
  owned_near=<n> hostile_orders=<n> cuts=<n>`. `reason` is this frame's stamp, else the last stamp's; `hostile_orders`
  counts the (f) candidates that took a relation test.
* One per suppressed episode (vanilla nonzero, filtered 0): `music_alert suppressed=start …` with the same fields on
  the episode's first frame, and `music_alert suppressed=end frames=<n> vanilla=<n> to=<n> now=<ms>` when it ends.
* No per-frame rows; 2,000 rows per session, then one `music_alert cap` notice.

## Cost (inferred from the code; object counts per frame are not measured)

* Every frame: the stub (`cld`, 6 pushes, a call, 4 pops, an indirect jump), the boundary (`stmxcsr`/`ldmxcsr`,
  `GetLastError`/`SetLastError`) and about 15 validated reads (`[R+8]`, `[R+4]`, `cfg`, clock, step, the two SETA
  words, `M`, four counters, `[P+0x54]`, the sector's class word, `[P+0x44]`). `engine_memory::read` is a region-cache
  lookup per read with one `VirtualQuery` per distinct region per frame on its first touch (`engine_memory.h`).
* Only on frames where the vanilla level is nonzero (a hostile in range): one walk of lists 5, 6, 7 and 10, three
  reads per node (next link, group word, class word), two more for a class-7 ship not owned by the player (flight
  pointer, order word) and, for one with an attack order, its target plus one relation test when the target is P or an
  owned object within range (about 10 reads); about 15 reads for each player-owned object within range (position,
  flags, and for a ship its order, target and one relation test). The walk ends at the first (c)/(f) event; at most
  4096 nodes per list.
* Only on frames where a fire counter rose with a selected target, or a hit counter rose: the cockpit hash lookup
  (≤ 32 links) and the same walk until the target is found / the victim with the player-hit bit is found (one Flags2
  read per node, a relation test per node carrying the bit).
* No rows on the per-frame path; rows only on edges and episode boundaries.

The vanilla routine itself already visits every object of all 32 lists with a call per object each frame, so the walk
is a subset of that work; it was not timed. The per-episode list sizes are reported in the edge rows (`visited`).

## Windows / CrossOver parity

The patch is game-internal: the same bytes, the same claim and the same reads on native Windows and under
CrossOver. Documented Win32 only: `VirtualProtect` and the code write in `engine_patch`, `GetModuleHandleExW(…PIN)`,
`VirtualQuery` in `engine_memory`, `InterlockedExchange` for the load flag, `GetLastError`/`SetLastError`. No
Wine-private prerequisite, no exact DLL hash; the executable identity check binds only the game's own image. Built with
MinGW i686, SSE2 only (`check_no_x87.py` walks the stub and the handler: no x87 reachable).

## What the first flight must show

1. The install row with `armed=1 reason=ok write=plain windows=19` (any other `reason` means vanilla behaviour), with
   the stand's `--debug` (game phases on) as well as without it.
2. Fly past a hostile station or an idle hostile ship without firing: sector music keeps playing and the log carries
   `suppressed=start` when it comes within range, `suppressed=end frames=<n>` when it leaves (no `edge` row).
3. Fire at a hostile target in range: `edge from=0 to=1 reason=a cause=stamp age_ms=0` on the first shot (combat music
   starts within the frame); being hit gives `reason=b`; a hostile missile on you `d`; a hit on an unselected hostile
   `e`; a hostile turning its attack order on you or on an owned ship nearby `f` (`hostile_orders` ≥ 1); an owned ship
   near you being hit or attacking gives `c` (with `owned_near` ≥ 1).
4. Stop fighting with the hostile still in range: `edge from=1 to=0 cause=expired age_ms=<≈hold_ms>` about 20 game
   seconds after the last stamp (with SETA on, correspondingly sooner in wall time), then `suppressed=start` while
   it stays in range; sector music resumes.
5. Leave the range while engaged: `edge from=1 to=0 cause=out_of_range`.
6. No `cuts` above 0 in any row (a list longer than 4096 nodes would need a higher bound), and `visited` small
   enough that the walk is a non-issue (hundreds, not thousands).
7. No frame-time change attributable to the site, no stall, no crash on a sector change, a save/load (no `edge` row
   on the first frame after a load; `cause=newer_than_now` is expected once if the clock was rewound) or a docking.

Not established statically, to be read from the flight: whether the AI always fights through orders `0x10`/`0x11`
(turret-only fights show only as hits, i.e. `b`/`c`/`e`); whether player missiles carry the player-shot bit (the
counters `M+0x44`/`M+0x4c`); friendly fire counts as `b`; a suitable hold.
