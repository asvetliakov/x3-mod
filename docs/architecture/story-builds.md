# Story builds: which KC object the VM holds, and the run-time site detection

2026-10-23. Static study of `X3AP.exe` (SHA-256 `fdbf3418…`) and the catalogues under `C:\X3`; nothing launched.
`seta_restore` and `music_queue` used to bind to the Mayhem story's CODE offsets (build 1462141839). They now detect
their sites in the loaded object at run time (`src/proxy/story_detect.h`). The two known builds remain only as an
expectation the detection must reproduce (`src/proxy/story_builds.h`). Evidence:
[`story_builds_census.py`](../../verification/results/story-builds/story_builds_census.py) →
`story_builds_census.txt`, and the two verifiers below. **[m]** = measured, **[s]** = static disassembly, **[i]** =
inferred.

## 1. The three build ids of Run 191

The VM (`*0x006085e4`) runs one KC object at a time. `+0x143c` holds the build id of the object it loaded last
(loader `0x0049d2a7`). The `music_queue_kc` rows logged that field, so they show every object the VM ran, not only the
story. **[m]** (census, catalogue headers):

| Build id | Object | Catalogue | Size | When the VM holds it |
| --- | --- | --- | --- | --- |
| 1462141340 | `L/x3config.obj` | `addon/07.cat` | 661 B | Process init, before the first frame (Run 191 frame 0: `code=0 size=0`) |
| 1462141839 | `L/x3story.obj` | `addon/07.cat` | 2,182,503 B | A game (new game or save load), frames 640–49289 |
| 1462141842 | `L/x3intro.obj` | `addon/07.cat` | 134,461 B | The main menu (frame 49296: `size=107358`, intro track 8404 played at KC IP `0x1a4d`) |
| 1462141841 | `L/x3galedit.obj` | `addon/07.cat` | 2,007,913 B | `-galedit` (not seen in flight) |

The stock AP objects in `addon/04.cat` are: story 1510658513, config and intro both 1510658518, galedit
1510658517. No catalogue holds a language variant `x3story-L<lang>.obj`, and no loose `L\*.obj` exists. The Mayhem
build ids are Unix timestamps of one compile run (2016-05-01, 22:22:20 to 22:30:42 UTC) **[i]**.

**How the VM gets each object** **[s]** (census section 2 pins the bytes). The object names sit in a table at
`0x0057c8c8`: `x3story`, `x3intro`, `x3tutorial`, `x3config`, `x3galedit`, `x3demo`, `x3pdemo`.

* **Process init.** `0x004031e8` creates the VM (`0x004ab010` → `*0x006085e4`). Then `0x004031fd` loads `x3config`
  through `0x00497160`, which clears the VM (`0x00497190`) and loads the object (`0x0049d030`). That is the frame-0
  build. Its CODE buffer was already released by the first Present, but `+0x143c` keeps the id.
* **The main loop.** It switches on the mode `[*0x0057fc60+0x4d8]` at `0x004036e0` (table `0x00403818`): 1 = intro
  (the menu), 2 = story, 3 = galedit, 4 = tutorial, 5/6 = demos. `0x00401b90` stores the chosen name at `+0x4d4`, and
  the session function `0x00403840` runs.
* **The session function.** It first stops all media (`0x0040387d`, the `session_start` row). It then either restores
  a save through `0x00404cc0`, where `0x004a0880` reloads the story by its saved name (gate-queue-script.md §7), or
  clears the VM and loads the `+0x4d4` object (`0x00403957`..`0x0040396e`).

**Run 191, frame 49289 to 49296.** A `session_start` stop-all, then the intro object, then menu music: the user left
to the main menu. It was not a save load, because a save load reloads the story and keeps build 1462141839. The
queue's patch stayed refused because no story was loaded again before `session_end` at 49632. That was correct. With
detection the intro now logs `action=no_sites detect=no_method method=150::WarpToSector`. The earlier
`session_start` rows at frames 4 and 88 are menu sessions. They logged no `music_queue_kc` row because the result did
not change. **[m]** (census section 3).

## 2. Are the sites byte-identical across builds?

No. Every site moves between Mayhem and stock (table in §4). Normalised with jump targets relative to the method entry
and call or native operands by name, the five music methods are equal in both builds: `PlayMusicWithFade`,
`LoopMusic`, `ChangeTrackMusic`, `MusicFadeOut`, `StopMusic`. `WarpToSector`, `StartGateWarp` and `StopFastForward`
differ in member indices and other code, but each site has the same shape in both builds **[m]**
(`verify_music_queue_kc.py`). So a fixed offset never carries over between builds, but a signature does.

## 3. Run-time detection (`src/proxy/story_detect.h`)

**VM layout used** **[s]** (static only; no live process has been read yet):

| Field | Meaning | Source |
| --- | --- | --- |
| `+0x08`, `+0x10` | CODE buffer and size | `0x0049d332..0x0049d34e` |
| `+0x14`, `+0x18` | Decoded string table and size | `0x004b0740` |
| `+0x1c`, `+0x20` | Class record count and records | `0x0049f1e0` → `0x004b06f0` |
| class records | `0x38` B, sorted by the signed key at `+0`, class at `+8` | `0x004b06f0` |
| class | `+0x14` parent, `+0x28` method count, `+0x2c` rows | `0x0049f330` |
| method rows | 16 B: CODE entry and name offset, sorted by name | `0x004b0740` |
| `+0x40 + g·0x18`, g = 1..31 | Native group name tables | `0x0049e1a0` |

The load pass `0x0049e1a0` stores multi-byte operands little-endian, the u16 counts of `78`/`79` included. It turns
`82 <name>` into `82 <u16 group> <u16 command>`, choosing the first group and index whose name matches. The peephole
pass `0x0049e4f0` then makes two walks **[s]**. The first rewrites the opcode byte of patterns that start with
`01`/`02`, `0d`–`0f`/`14`–`16`, `17` or `64` into fused opcodes `8c`–`b1`, and the first three bytes of
`0d 0001 14 0002 24 83` into `83 83 83` (up to three bytes, not only the first). The second threads the operands of
`32`/`33`/`34` through `jmp` chains and constant conditionals. No signature byte is rewritten: the native (`82`), call
(`86`/`88`), fork (`6f`) and child opcodes never head a pattern, and only jump operands are threaded.

**Decode walk** **[s]**, **[m]**. A signature is tested only at instruction starts of a walk from the method's entry
(`unique_site`), so a copy of its bytes inside another instruction's operand never matches. The walk uses the in-memory
widths of the pass's own second walk (`0x0049e810`: byte table `0x0049e9d4`, jump table `0x0049e9a0`). Its table makes
each fused opcode cover its whole pattern (`95` 2 bytes, `8c`–`94` 4, `97`–`99` 5, `96` 6, `a4`/`a5` 8, `a0`/`a1` 9,
`a6`/`a7` 10, `a2`/`a3` 11; `aa`–`b1` 1, the jump after them its own instruction). An opcode the interpreter
`0x004a26a0` has no handler of its own for fails the method as `decode_error`: `00`, above `b1`, or an opcode it sends
to its default handler `0x004a4301` (53 of `01`–`b1`) other than `70`. So does an instruction that ends past the method's end.
Over the host images (peephole modelled), the walk from offset 1 consumes every CODE byte of all five objects. It lands
on every method entry (Mayhem 6,815/6,815, stock 6,818/6,818, intro 426, galedit 6,218) and meets no handler-less opcode
([`story_detect_decode_walk.txt`](../../verification/results/story-builds/story_detect_decode_walk.txt) §1).

**Method and native lookup.** A method is found by class key and name, the way the engine's resolver does it: a binary
search of the class records, then the class chain, then a binary search of the rows. The neighbouring rows must not
carry the same name. A method ends at the smallest entry above it among all rows of all classes. A native is found by
name in the group tables.

**Sites.** Each is the only match at an instruction start of its method; otherwise the detection fails:

| Feature | Method | Signature | Derived |
| --- | --- | --- | --- |
| SETA | `150::StopFastForward` | `82 <TI_SetTimeWarpFactor>` | stop IP = site + 5 |
| SETA | `150::StartGateWarp` | `86`/`88 <StopFastForward entry>` | gate return = site + 5 |
| SETA rows | `ToggleFastForward`, `StartSectorWarp`, `QuickWarp`, `2004::JumpToSector` | method ranges | return names |
| queue | `150::WarpToSector` | `6f 33 <p+20>` + `06 f401 02 06 c800 86 <StopMusic entry> 24 70`; no dword of the method in `(p, p+6)` | window, continuation, window and patch bytes |
| queue | `200::PlayMusicWithFade` | `82 <MOV_StopMovie>`, `82 <MOV_PlayMovie>` | stop IP, play IP |
| queue | `200::MusicFadeOut` | `82 <MOV_StopMovie>` | fade stop IP |
| queue | `200::LoopMusic`, `200::ChangeTrackMusic` | call of `PlayMusicWithFade` / `LoopMusic` | the two return cells |

**Failure reasons.** `no_vm`, `unreadable`, `no_code`, `bad_layout`, `no_class`, `no_method`, `duplicate_method`,
`method_range`, `no_native`, `no_site`, `ambiguous_site`, `branch_into_window`, `table_mismatch` (a known build whose
detection differs from `story_builds.h`), `decode_error` (the decode walk above), `read_budget` (more than 16,384
reader calls in one detection), `retry_exhausted` (below). Each failure names the method. That object then gets no SETA
arm, no KC write and no deferral. An unknown build relies on the detection alone.

**When it runs** **[s]**. Each feature keeps one detection per VM key (pointer, CODE buffer, size, build): a success or
a definitive failure. A transient failure (`unreadable`, `method_range`, `no_code`: the VM read at a bad moment) is not
kept. It is detected again at the next due check, at most 8 retries per key; the ninth transient failure is kept as
`retry_exhausted` (`story_detect.h` `Retries`).

* **SETA.** At the first SETA stop after the key changes, inside the TI case handler and before the lock; after a
  transient failure, at the next SETA stop. A failure gives
  `seta_restore refused reason=story_sites detect=<reason> method=<method>`, one row per build and reason.
* **Queue.** From Present, at every due check (8 frames) while no detection is kept for the current key. The load
  gate's observation then holds the key, that check's detection outcome and window offset, and the 6 window bytes read
  live there. A detection is kept only when two consecutive checks agree on all of them, or at the first check after
  the `save_load_complete` marker. The patch is written at the next check that again observes the same window, so 16
  to 24 frames after a load completes. A failure gives `music_queue_kc action=no_sites detect=… method=…`, one row per
  build. The T2/T3 handlers use only a kept detection whose key equals the VM's current key; otherwise nothing defers.

**Cost** **[m]**, host reader over the in-memory images: 2,154–2,754 reads per detection (seta 2,726/2,754/2,688 and
queue 2,198/2,234/2,154 for Mayhem/stock/galedit), 7 for x3config, 30 for x3intro
([`story_detect_decode_walk.txt`](../../verification/results/story-builds/story_detect_decode_walk.txt) §2). The
budget of 16,384 is about 6 times the largest. SETA detects once per key. The queue detects at each due check until
one is kept, normally twice per story load. The decode walk is CPU work over the 16 KiB scratch, with no reads. The
read count on the engine reader (one lock and a cached VirtualQuery per region per frame) is not timed.

**Combat without ids.** `150::NotifyAlert` runs inside the commit of the alert record `R = *0x00608504` (`[R+4]`
committed, `[R+8]` this frame's level).

* The resume remembers a sector track only when the script's stop arrives on the rising edge (`[R+4] = 0`, `[R+8] ≠ 0`;
  Run 189 0/1). A play with either level nonzero is a combat track (`keep`). The first start-0 replay of the
  remembered id with both levels 0 seeks.
* The queue still refuses every stop whose alert is not 0/0. `combat_old` now means "the id of the last music play,
  made with the alert on".
* No combat-id range or list is used anywhere. Death (13), landing (14) and boarding (8233–8237) remain immediate ids.
  `verify_music_queue_kc.py` checks that they are the same literals `ChangeTrackMusic` is called with in both builds.

## 4. What is detected, and what still needs a constant

| Item | Run-time source | Constant, and why |
| --- | --- | --- |
| SETA stop IP, gate return, row names | detected | — |
| KC window, continuation, window and patch bytes | detected | the StopMusic(500) child's literal bytes (500, 200) are the signature itself |
| Queue stop IP, fade stop IP, play IP, two return cells | detected | — |
| Fork child's stack index −19 and cell depths 18/13/11/10/9/7 | **constant** | Follows from three methods' frames and needs a symbolic stack walk. Measured on Mayhem (Run 189 `k = −19`); the methods are equal in both builds. Each stop still checks the tags and the detected return offsets before deferring |
| Class keys 150/200/2004, method and native names | **constant** | They are the engine's own lookup contract |
| VM and class layout offsets | **constant** | EXE layout, pinned with the EXE hash |
| Known builds (`story_builds.h`) | last-resort check | A known build that detects differently stays inert |
| Combat | alert record | no ids |

**Expected detections** **[m]**: `story_detect_host` reproduces these from each object on disk.

| Build | SETA stop IP / gate return | `WarpToSector` / window / continuation | Stop / fade / play IP | Loop / change return |
| --- | --- | --- | --- | --- |
| Mayhem 1462141839 | `1558d` / `15ef8` | `16185` / `161c3` / `161d7` | `0db54` / `0da17` / `0dbf7` | `0dc84` / `0dcdb` |
| Stock AP 1510658513 | `157a6` / `160d1` | `1635e` / `1639c` / `163b0` | `0d273` / `0d136` / `0d316` | `0d3a3` / `0d3fa` |

`x3config` fails with `bad_layout` (no classes) and `x3intro` with `no_method`. `x3galedit` runs the game script in the
editor, so it detects sites of its own: Mayhem SETA `14f11`/`1587c` and window `15b47`; stock `13ed1`/`147fc` and
`14ac7`.

## 5. Not verified

* **The in-memory layout is static only.** Class records, rows, string table and group tables have not been read from
  a live process. The host image models them from the disassembly. The first flight with this build shows it:
  `music_queue_kc action=applied detect=none` after the story load. A layout mistake fails closed
  (`bad_layout`/`no_class`/`no_method`/`unreadable`) and leaves both features inert. It cannot mis-patch: the KC write
  still requires the window's exact vanilla bytes.
* **The peephole pass is modelled from the static disassembly only** (`story_vm_image.py` `peephole`: both walks).
  The flown Mayhem window bytes `6f33d7610100` (Run 191) agree with it: the window's jump is not threaded. No live CODE
  buffer has been compared with the model.
* **Stock AP cannot be flown here.** The user has no stock install. The stock entry is decoded and detected
  statically; no stock flight exists. The same holds for the fork child's depth on stock (inferred from equal methods)
  and for the combat edge values on stock.
* **The alert-edge classification is unflown.** The rising edge at the combat-start stop is measured (Run 189 0/1).
  That the combat track's play arrives with the alert on, and the sector replay with both levels 0, is inferred from
  the commit order. The resume and `combat_old` now depend on it.

## Reproduce

```sh
python3 -I verification/results/story-builds/story_builds_census.py /mnt/c/X3 <x3m.log> > story_builds_census.txt
python3 -I verification/results/story-builds/story_detect_decode_walk.py > verification/results/story-builds/story_detect_decode_walk.txt
python3 -I verification/results/seta-script-toggle/verify_seta_restore_sites.py
python3 -I verification/results/music-script-driver/verify_music_queue_kc.py
PYTHONPATH=verification/probe:verification/analysis /usr/bin/python3 -m unittest verification.analysis.test_music_keep verification.analysis.test_seta_restore
```
