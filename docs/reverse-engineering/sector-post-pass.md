# `0x0045b720`: the per-sector post pass and its 380 ms stall

2026-09-16. Static study of X3AP.exe, SHA-256 `fdbf3418d8f0a897…` (the bottle
copy); nothing was launched. Call lists and string refs from Ghidra headless on
`/tmp/x3-ghidra-research/X3Render` (`X3CallTree.java`, `X3DecompileFunctions.java`);
every byte, span, edge and caller claim from `i686-w64-mingw32-objdump` on the
file bytes, re-checked by `verification/probe/verify_post_phase_sites.py`
(`PASS`). Raw decompiler output stayed local and untracked. Inferences marked.

**Question.** Run 33 session B (run96, `docs/verification/sampling-profiler.md`)
puts 99.8 % of a ~380 ms frame in the `sector_post` interval — the call at
`0x0043a39a`/`0x0043a39b` to `0x0045b720` — on 70 of 70 slow frames, with one
sector and one container walked; a normal frame spends well under a millisecond
there. What inside `0x0045b720` grows to 380 ms?

**Answer (leading hypothesis, one runtime count from proof).** `0x0045b720` is
the **per-sector media-cue selector**. Its tail restarts the sector's
soundtrack/video cue through the play helper `0x004f65f0` (`0x0045c607`)
whenever the selected cue is *not currently registered as playing*.
`0x00498140` links a media record only when the DirectShow graph constructor
`0x004cf460` succeeds, and frees it and returns 0 when it fails
([voice-startup-sequence.md](voice-startup-sequence.md) §3). A cue whose graph
cannot be built is therefore retried **every frame, forever**, each retry a full
file-probe + `CoCreateInstance` + graph-render attempt. Under CrossOver that
lands in winegstreamer and fails there: the launcher's stderr shows
`gst_element_set_state`/`gst_object_unref` criticals once or twice per slow
frame during the stall, and exactly twice (start, exit) in a healthy session.
Nothing else in the routine can reach 380 ms (§4).

## 1. Shape of the routine

`0x0045b720`–`0x0045c77d`, `ret 4` (stdcall, one argument = the sector
container), 0x1060 bytes, prologue `sub esp,0x70; push ebx/ebp/esi/edi`,
`edi` = the argument. Exactly one caller image-wide: `0x0043a39b`, the
`sector_post` call of the per-sector update driver `0x0043a360`
([main-loop-input-region.md](main-loop-input-region.md) §2). 49 call sites: 12
`_malloc`, 7 `_free`, 3 `_memset`, 4 indirect (`call eax`, all in the
allocation-failure retry blocks of the 16-byte list-node helper).

| Phase | Range | Walks | Per-item work |
| --- | --- | --- | --- |
| Walk 1 | `0x0045b730`–`0x0045b813` | sector objects via the cursor `0x0044e600` | class 5/6/7 → `0x0044b750`; class `0x12` → `0x0044b0c0` + a sub-list via `0x00444170`; else timestamp `[[obj+0x50]+0x180]`; clear `[obj+0x40] & 0x12000000` |
| Walk 2 | `0x0045b8c4`–`0x0045c1d9` | all 32 class buckets of `[sector+0x50]`, stride `0xc` | build ≤4 distance-sorted candidate lists of class-`0x14` objects (distance `0x004801a0`); `0x004510a0` for objects flagged `[obj+0x44] & 0x100000` |
| Score | `0x0045bed0`–`0x0045c1d4` | every record of the `Videos` table | sum `0x7fffffff / distance` over matching candidates; `rand()` breaks ties |
| Restart | `0x0045c1da`–`0x0045c60c` | — | if the winner is not already playing: `0x00498810` (stop), then `0x004f65f0` (start) |
| Teardown | `0x0045c60f`–`0x0045c77d` | the four candidate lists | `_free` each node |

`0x0044e600` (0x7a B, `EAX` = cursor or 0, `EDI` = sector) is a *per-sector*
iterator, not a global chain: it walks the 32 lists at `[sector+0x50]` and
returns the next object with bit 20 of `[obj+0x40]` set, resuming from the
bucket index it reads out of the object's own class word `[obj+0x48]`. **The
class word is the bucket index** — objects are bucketed by class in the sector's
32-entry table. This corrects the [main-loop-input-region.md](
main-loop-input-region.md) row that called it "a global object chain seeded by
`0x0044e600`"; the pass is per-sector throughout.

Classes here: 1 = the sector/container (`[obj+0x54]`, the parent, is tested for
class 1 before every distance call); 5/6/7 and `0x12` take walk 1's
visual/scene-node update; `0x14` is what the cue selector scores
(**inference**: an object carrying a media surface or emitter — it owns
`[[obj+0x50]+4]` = a cue key and `[[obj+0x50]+8]` = the assigned cue slot,
written back here, plus flag `[obj+0x44] & 0x400000`).

## 2. The media tables

`DAT_006070b8` records of `0x30` bytes at `DAT_00606fb4`, loaded at
`0x0043520b`–`0x00435374` from `addon\types\Videos` (strings `0x0055f8a4`,
`0x0055f8ac`); the group table `DAT_006070bc`/`DAT_006070c0` comes next from
`addon\types\VideoLists` (`0x0055f8c0`). Record layout from the parse loop:
`+0x00` = count of non-zero ids among four slots; `+0x04`…`+0x10` = four
object-type ids, a **negative** value being `-1-index` into the `VideoLists`
groups (`0x0045c04a`); `+0x14` = the media id handed to `0x004f65f0` in `ESI`;
`+0x18`…`+0x2c` = six playback parameters (**inference**), pushed as arguments.

`0x0045c607` is `push 0; push [rec+0x2c] … push [rec+0x18]; push 0x5a;
call 0x004f65f0`, `add esp,0x20` (cdecl, eight arguments); `0x5a` is the *kind*
written to manager-record `+0x30`. `0x004f65f0` looks the id up in
`*DAT_00606f44`, calls `0x00498140` when absent, then sets `+0x2c |= 4`,
`+0x30 = 0x5a` and calls `0x00498c90`. `0x004cf460` resolves the id as
`%05d.dat` under `addon\mov\%s` or the configured movie directory, with
`soundtrack\%05d.mp3` / `.wma` fallbacks — all in
[voice-startup-sequence.md](voice-startup-sequence.md) §3–4, which already lists
`0x0045c607` as one of the three `0x004f65f0` call sites.

## 3. The retry loop, exactly

Head, `0x0045b83e`–`0x0045b8b5`: if `[sector+0x174] == 0` and
`[sector+0x178] >= 0`, scan `*DAT_00606f44` for a record with
`[rec+0x10] == Videos[[sector+0x178]].id`; the "already playing" flag is set
only if that record also has `[rec+0x30] == 0x5a` and `[rec+0x2c] & 4`.

Tail, `0x0045c1e4`: if that flag is clear the restart flag is forced to 1, the
selector re-runs and `0x004f65f0` is called; if it is set the restart flag is
`score[current] != max(score)`, the normal "a better cue now wins" path.

`0x00498140` on failure (`0x004981e3`) frees the unlinked `0x40`-byte record and
returns 0, so **nothing is added to `*DAT_00606f44`**; next frame the head probe
fails again and the whole attempt repeats. There is no negative cache anywhere
on this path. It is the failure shape the voice note calls "the fast failure"
(§6) — only there it was fast, and here the graph attempt is slow.

## 4. Why the other candidates cannot reach 380 ms

Order-of-magnitude estimates for FEX on the X3 bottle, anchored on the measured
89.7 ns per lean-stamp dispatch (`LOOP PHASE BENCH`).

* **Walk 1** — `0x0044b750` (0xed0 B, 3 matrix concats, 2 `sqrtf`, node
  attach/detach `0x00487e30`/`0x004880e0`/`0x00487be0`) at 1–3 us per object
  would need ~10^5 objects in one sector; X3 sectors hold hundreds. **Rejected.**
* **Score, sorts, allocator churn** — `DAT_006070b8` × ≤4 slots × ≤5 candidates
  plus the group scan is a few 10^4 pointer steps (~0.1 ms); the four insertion
  sorts are `O(k²)` with `k` capped at 5; the four `malloc`+`memset` blocks are
  `DAT_006070b8 × 4…16` bytes. **Rejected.**
* **`0x004510a0`** (objects with `[obj+0x44] & 0x100000`; `"SelfDestruct"`,
  `"DetailSwitch"`) loads a body (`0x00492970`) and **builds a collision tree**
  (`0x0047eb90`, the top loading hot spot); one object re-triggering its detail
  switch per frame could cost tens of ms. **The surviving alternative**, in
  walk 2.
* **Media restart** — one `0x004cf460` per frame: ≤4 catalogue-resolved file
  probes (`0x004e6fa0` + `fopen`), ≤8 `CoCreateInstance`, filter connection and
  `OpenFile`/`Render`. A failing winegstreamer build of 100–400 ms matches the
  stderr cadence. **Consistent with 380 ms at a count of 1.**

The sector property behind the stall is therefore not object count but *which
cue the sector's class-`0x14` objects select* — a sector whose winning `Videos`
record names a media id whose graph cannot be built (missing `addon\mov` member,
missing `soundtrack\%05d.*`, or a filter the pipeline cannot instantiate). That
fits "certain sectors that are not visually busy" and predicts the same stall on
stock Windows when the file or filter is absent.

## 5. Proposed `--post-phases` stamps (four, accumulate-only)

Ledger format as [main-loop-input-region.md](main-loop-input-region.md) §4;
`ret_pop` and `rel32_offset` are 0 for all four — no span carries a relative
control transfer, so every arena copy is byte-identical.

| # | Name | Address | Bytes | Len | Closes / opens |
| --- | --- | --- | --- | ---: | --- |
| 0 | `post_enter` | `0x0045b720` | `83 ec 70 53 55` | 5 | opens walk 1; counts calls |
| 1 | `post_select` | `0x0045b814` | `8d 44 24 74 89 44 24 7c` | 8 | closes walk 1, opens walk 2 + scoring |
| 2 | `post_media` | `0x0045c5e8` | `6a 00 8b 48 2c` | 5 | closes scoring, opens the media restart |
| 3 | `post_media_end` | `0x0045c60f` | `8b 35 b8 70 60 00` | 6 | closes the restart, opens teardown |

Three intervals (0→1, 1→2, 2→3) and four counts. **The decisive number is site
2's count:** ≥1 per slow frame with interval 2→3 ≈ the whole frame confirms §3;
interval 0→1 or 1→2 dominating instead points at `0x004510a0`. Site 0 duplicates
the loop group's `sector_post` interval and can be dropped, leaving three. At
148 B per site (six loop sites = 888 B, `docs/verification/sampling-profiler.md`)
four sites need 592 B of the 632 B free with every group on: it fits with 40 B
to spare, only as the last group added; three sites need 444 B. Rate is ~4
dispatches per active sector per frame.

### Hook-site suitability (`verify_post_phase_sites.py` → `PASS`)

* **Instruction boundaries.** `0x0045b720`–`0x0045c780` decodes gap-free (1,285
  instructions, last ending at the `ret 4`); all four spans start and end on a
  decoded boundary, are ≥ 5 bytes, non-overlapping, and hold no
  `call`/`jmp`/`jcc`, so the arena copy is position-independent.
* **Incoming edges.** `post_select` ← `0x0045b739`; `post_media_end` ←
  `0x0045c5c9`, `0x0045c5da`, both on the span start; sites 0 and 2 none. No
  direct branch targets a span interior. The raw sweep of all 1,247,232 `.text`
  offsets finds two candidate encodings (`0x0045c59f`, `0x0045c5c5`) that are
  not instruction starts in the gap-free decode and cannot execute; the verifier
  accepts exactly those and fails on any hit elsewhere.
* **ESP.** Own frame (`sub esp,0x70`), no `enter`/`leave`; every ESP write is a
  push/pop, an argument-cleanup `add esp,N`, one of three `lea esp,[esp+0x0]`
  no-ops, the prologue `sub` or the epilogue `add`. Site 0's span *is* the
  prologue and site 2's opens with `push 0x0` (first of eight arguments); both
  run in the arena tail at the game's exact ESP. Sites 1 and 3 do not touch ESP;
  site 2's start precedes any pending push, and at site 3 `add esp,0x20`
  (`0x0045c60c`) has already run on the taken path.
* **Registers and flags.** All registers are preserved by the stub; incoming
  flags are dead at all four sites, sites 1–3 write no flags, and site 0's `sub`
  sets flags nothing reads before `0x0045b737 test esi,esi`. Unlike loop sites 3
  and 5, no span leaves a flag consumer depending on the stub.
* **Re-entrancy and conflicts.** One `e8` caller image-wide (`0x0043a39b`), no
  aligned dword reference to a span byte outside `.rsrc`, so no indirect entry;
  main-loop thread only, but `0x004510a0` dispatches scripts (`0x0049f4c0`), so
  a foreign-thread or re-entrant hit must still be counted and dropped. All four
  spans are disjoint from every installed `game_phase`, `frame_phase`,
  `pass_phase` and `loop_phase` span.

## 6. Can a trampoline bound it?

1. **Negative-cache the failed media create — recommended, behaviour-neutral.**
   Hook `0x00498140` (entry `53 8b 5c 24 08` = `push ebx; mov ebx,[esp+8]`, a
   clean 5-byte whole-instruction span; `EBX` = media id, `EAX` = flags in,
   result in `EAX`; **span not qualified here**). If the same id failed within
   the last N seconds, return 0 at once: the game's state after that early
   return is *identical* to its state after a real failure (record freed,
   nothing linked, caller takes the same branch), only 380 ms cheaper. No AI,
   economy, physics or script input changes; the one observable difference is
   that a cue which would have succeeded later is delayed, so use a backoff and
   never a permanent block. It covers all five `0x00498140` call sites including
   speech — a benefit and a risk to weigh with the voice owner.
2. **Make the graph succeed** (plugin/bottle side, not a trampoline). The
   project already ships `src/proxy/voice_dmo_fallback.cpp` for the
   `X WMSpeech Decoder DMO` leg of the same constructor; if the failing cue is
   an MP3/MPEG leg, extending that coverage ends the retry loop by itself, with
   no behaviour change and the cue actually playing. Needs the failing media id
   from §5's stamps to scope.
3. **Rate-limit the selection** (walk 2 + scoring every N frames): cheap, but
   **changes behaviour** — cue assignment to class-`0x14` objects
   (`[[obj+0x50]+8]`, flag `0x400000`) lags and the `0x004510a0` detail switches
   are delayed with it. Not recommended.
4. **Skip `0x0045b720` entirely: not safe.** Walk 1 clears
   `[obj+0x40] & 0x12000000` and stamps `[[obj+0x50]+0x180]` and
   `[[obj+0x50]+0x1c0…0x1dc]` from the game clock every frame, and other passes
   read those; skipping it changes simulation state.

## 7. What this does not establish

* No runtime measurement: §3 is a code-path argument plus the launcher's
  GStreamer stderr. Site 2's count turns it into a fact.
* Which media id the stalling sector selects, and whether it resolves under
  `addon\mov` or `soundtrack\`, is unknown — `Videos`/`VideoLists` are runtime
  data, not EXE bytes. `0x004510a0`'s trigger rate was not decompiled.
* "Media surface / emitter" for class `0x14` and record fields `+0x18`…`+0x2c`
  as playback parameters are inferences from layout and call shape.
* The `0x00498140` hook span of §6.1 was read from the file bytes but has no
  incoming-edge, ESP or conflict qualification yet.

## Reproduce

```sh
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
  /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless \
  /tmp/x3-ghidra-research X3Render -process X3AP.exe -noanalysis -readOnly \
  -scriptPath tools/analysis -postScript X3CallTree.java /tmp/out/post_tree.txt 2 \
     0045b720 0044e600 004f65f0 00498810 00498140 004cf460
PYTHONPATH=verification/probe python3 verification/probe/verify_post_phase_sites.py
```
