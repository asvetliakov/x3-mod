# FL sector background (step 6)

The volumetric fog and the dust motes key on the active sector's TBackgrounds record. On FL the shared
value-only reader [`sector_background.h`](../../src/proxy/sector_background.h) now runs over X3FL.exe's layout
from [`sector_background_fl.cpp`](../../src/engine/fl/sector_background_fl.cpp). Read-only: no patch, no hook,
no engine call. Layout proof: [FL sector fog §6](../reverse-engineering/fl/sector-fog.md#6-verification-pass-for-the-step-6-reader-2026-11-01);
AP contract: [sector fog §11](../reverse-engineering/sector-fog.md). Host evidence: [ledger](../verification/farnhams-legacy.md),
"Step 6 — sector background".

**What is read.** Once per frame, at the proxy's first successful BeginScene (Present fallback for frames
without one), inside capture.cpp's existing device-call boundary, the same chain as AP: `[registry slot]` →
`+0x10` active handle, `+0` map → bucket walk (≤ 32 links) → cockpit; `cockpit+0x54` sector (class word
`+0x48 == 1`), `+0x13c` index bounded by `0 ≤ index < [count slot] ≤ 4096`; one `0x120`-byte row copy at
`[table slot] + index·0xdb8 + 0x44`; the name; `sector+0x140/+0x144`; `cockpit+0x58` camera fog pair and
`+0x270`; `[config slot]+0x758` view distance; the `cockpit+0xc` ref-object anchor and, with `--fog-docked`,
the bounded parent walk. All four slots are re-read every sample; nothing but value copies is retained.

**When.** The first BeginScene of X3FL.exe's frame is `call [ecx+0xa4]` at RVA `0x8b859`, inside the render pass
`0x8b6d0..0x8be31`, which the main loop calls at `0x782cf` right after the cockpit update `0x782ca` (whose fog
copy refreshes the sector camera's `+0x36c/+0x370`), but before the pass's per-view camera activation. This
deviates from [FL sector fog §6.4](../reverse-engineering/fl/sector-fog.md#64-mutability-and-the-fl-read-window-question-4)'s
"at or after the main camera activation". It is acceptable because the camera activation writes nothing the
sample reads, and a sample that is stale against this frame's row fails the raw camera comparison (the copy sets
`+0x270 |= 0x10000` unconditionally, so `camera_check` is always evaluated) and is refused for that frame as
`camera_mismatch`: no fog for one frame, never the wrong fog.

**Differences from AP.** The slots are `fl::module_base()` + RVA `0x1a82e4` (registry), `0x1a83d0` (table),
`0x1a8338` (count), `0x1a83b0` (config) from the generated
[`sector_background_fl_inc.h`](../../src/engine/fl/sector_background_fl_inc.h); the view distance is
`config+0x758` (AP `+0x768`; the shared log key stays `config768`); the name is copied one byte per read and
stops after its NUL (FL names are `strlen+1` allocations, ≤ 22 bytes), where AP keeps its single 32-byte span
read through `engine_memory` (refused, never faulting, at a page end). The gate is `fl::verified()` plus all 22
generated anchors (1,112 bytes, 27 relocated operand words recomputed for the live base), decided once per
process. The layout is a type parameter (`ApLayout` / `FlLayout`), so the AP objects are byte-identical.
Unchanged and still AP-only: the R3 transit prefill (`fog_prefill_poll`, AP list layout, refused on FL by the
constant `object_trace::executable_verified()`); FL fog cold-starts at the first Ready sample after a transit.
`volumetric_fog_handover_mode … prefill=1` is still logged on FL (the setting's value); the poll does nothing there.

**Fog cards.** X3FL.exe draws its nebula fog cards with two exact program pairs: the AP pair
`7b6393fe2d3e1d85/f7e0b6647a3bfa62` (`nebulafog`, `nebulafog2s`) and `ececc28e00592d35/214fc10d3672ab92`
(`nebulafog_0000`, `nebulafog_0001`), [FL fog cards §1, §3.2](../reverse-engineering/fl/fog-cards.md), confirmed by
the offline inventory ([`fog_card_pairs.py`](../../verification/results/fl-port/sector-background/fog_card_pairs.py),
[output](../../verification/results/fl-port/sector-background/fog_card_pairs.txt)). In the FL edition
[`fog_card_match.h`](../../src/proxy/fog_card_match.h) treats both pixel programs as card sources (observed,
masked under the medium or refused with a named gate) and admits only the two witnessed pairings. AP's resolved
set holds the same programs, but AP keeps its captured single pair.

**Expected rows (first flight).** Once: `fl.sector status=ready base=0x… registry_slot=<base+0x1a82e4>
table_slot=<base+0x1a83d0> count_slot=<base+0x1a8338> config_slot=<base+0x1a83b0> view_distance=+0x758
anchors=22 failed=none log_s=10`. Every 10 s (`fl_sector_log`): `fl.sector_sample row=N samples=… ready=…
status=ready sector=… class48=1 index=… count=110 name_valid=1 name="<family>" dust=… near=… far=…
config758=… effective_far=… camera_check=match anchor_check=match walk=none`. With `sector_background=1`:
`sector_background … status=ready … count=110 … name_valid=1 name="<family>" … camera_check=match …
config768=<view distance> …`, replacing today's `status=foreign_executable`. In a fogged sector:
`volumetric_fog_frame … applied=1 reason=ok … profile=<nonzero> … sun=tracked` (FL sun-light poll, run 12;
`sun=fallback` when the poll refuses; `sun=none` is printed only for a skipped frame), dust motes and fog medium
visible, and `volumetric_fog_cards … suppressed>0` while cards are on screen.

**What a flight must show.** Start or load in a fogged sector and fly through one gate. Accept: `status=ready`
on every in-flight sample, `name` and `index` change at the gate, `camera_check=match`, `count=110`,
`volumetric_fog_frame applied=1` in the fogged sector and the fog visible. Wrong: `fl.sector
status=anchor_mismatch failed=<anchor>` (a different X3FL.exe build), `status=executable_mismatch`,
`camera_mismatch` (wrong row or index) on more than isolated frames, `bad_index`/`bad_count` in flight,
`name_valid=0` in flight; stacked cards (native fog cards visible under the medium, or `volumetric_fog_cards …
observed>0 suppressed=0` without a refusal reason).
`no_cockpit`/`loading` on menus and load screens are expected. If the sector is `ready` but the fog row still
says `reason=card_refused` or another gate, the `volumetric_fog_cards … refusal=` row names it (outside this step).
Cost (fixture, measured, not game FPS): 2.8 µs per Ready sample with the docked walk and a 21-character name,
2.2 µs without the name, dominated by `engine_memory`'s per-sample revalidation (one VirtualQuery per touched
region); about 45 bounded reads, no allocation, one GetTickCount64 for the row cadence.

**Where to fly.** [`fogged_sectors.py`](../../verification/results/fl-port/sector-background/fogged_sectors.py)
([output](../../verification/results/fl-port/sector-background/fogged_sectors.txt)): of FL's 298 sectors, 48 have
dust and all 48 families have a compiled fog profile (11 families). Examples by map x,y: `foggreenoutlands`
(dense, near 3 M) at 2,0 / 3,1 / 2,2 / 3,2; `bluewell` at 0,3 and 1,3; `fogparanid` at 1,8 / 1,9 / 2,9;
`fogdeepred` at 8,14 / 9,14 / 10,14; `foggreeneye` at 19–21,12. Names come from the 0001-L044 text pages by
coordinate (31 of 48 resolve, e.g. "Argon Prime" at 1,3, "The Hole" at 2,2); they are not verified against
FL's in-game map, so navigate by the family the `fl.sector_sample` row prints.
