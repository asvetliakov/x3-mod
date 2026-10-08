# Project status

The single current-state file (updated 2026-10-09 00:25, Run139 = the engine light hold (a ship with no main-jet record keeps its light and plates for `engine_light_hold` frames, default 60, while an anchored hull node is drawn, re-expressed from the hull's current rows, fading over the last third; user report: nozzles leaving the frustum cut the light and flicker close up; [design](architecture/engine-light.md) "Hold") + the engine-side occlusion skip (`occlusion_cull = engine`: a third stub chained on the 0x0047d2a2 cull claim skips a part's whole render visit when the previous frame's batched test hid all its draws, verdicts published at the sector view's Clear with model/stamp/position guards; expected 0.7-1.9 ms per frame at close capital views, inferred; [RE note](reverse-engineering/engine-side-occlusion-cull.md), [design](architecture/occlusion-cull.md), [ledger](verification/occlusion-cull.md)) + `occlusion_cull` default off, DLL SHA-256 `d48d2bab029bad3053b21766fd51554e0dab807bd49127416dd1d79d7ed9fd11` (59,502,807 bytes) built once from clean main `8f03c306` (0 warnings, x87 0 / 795 reachable, schema 260, host tests 75 OK, all 8 engine records bound after reruns at fb95cfa0; [candidate](../verification/results/run139-candidate-build.json), [install](../verification/results/run139-candidate-install.json)), installed 2026-10-09 00:19, rollback Run138 `850342c0…` (/tmp/x3-run138-candidate/build/d3d9.dll); Run 139 A queued: one build, `occlusion_cull = engine` vs `off` at matched close-capital draws plus the hold check with nozzles crossing the screen edge. Earlier (2026-10-08 22:15, Run138 = the occlusion cull batched per ship (one block at the ship's first part draw after its hull, one state swap, rectangles through c252-c253 over a static strip, last frame's parts re-projected through the hull's rows; parts whose hull draws after them dropped) with visible parts re-tested every 8 frames on a load-balanced per-part phase (`occlusion_cull_retest`) and hidden parts every frame; fixture 1.21 us/test + 7.4 us/block on DXVK vs Run137's 6.2 us/test; [design](architecture/occlusion-cull.md), [ledger](verification/occlusion-cull.md)), DLL SHA-256 `850342c036202a3a830a14cf3a4c7602bdcaa3da2cd8c53daecdda98a0e4f30e` (59,403,525 bytes) built once from clean main `937b8ab3` (host suite 290/3,058/0, 0 warnings, x87 0, schema 259, engine records bound; [candidate](../verification/results/run138-candidate-build.json), [install](../verification/results/run138-candidate-install.json)), installed 22:11, rollback Run136 `d03e597e…`; Run 138 A completed (run17 on / run19 off, same build: parity on DXVK, `view_submit` 6.54 vs 6.63 ms at matched close-capital draws while issued draws halve; the game is bound by its own per-object CPU work, not by forwarded draws). Decision: `occlusion_cull` default off (schema `builtin='off'`, template regenerated, in main since this commit; the user's x3m.ini already says off), the code kept as an opt-in and as the hidden-part oracle; the draw-call culling line of work is closed on this backend. Earlier the same day: Run137 = occlusion cull of hull-hidden sub-parts (user decision after the capture estimate of 67-91 hidden draws per frame at close capital views; per candidate a depth-tested screen rectangle under an occlusion query, zeros to every bound output under a ZERO/ONE blend, skip the next frame on a zero result, latest ready result up to two frames old; every candidate tested, fixed pool 1,024, `X3M_OCCLUSION_CULL` default on, off without shadow cascades; 2.9 us CPU + 1.3 us pipeline per test on DXVK vs 26 us per saved draw; [design](architecture/occlusion-cull.md), [ledger](verification/occlusion-cull.md)), DLL SHA-256 `c920eef8b9cbcf5f212356bed0c76a33b33ed9fbdf18a2e38eabb8e8b2433c48` (59,329,428 bytes) built once from clean main `1db749d6` (host suite 290/3,056/0, 0 warnings, x87 0, schema 258, engine records bound; [candidate](../verification/results/run137-candidate-build.json), [install](../verification/results/run137-candidate-install.json)), installed 18:52, rollback Run136 `d03e597e…`; Run 137 A completed (run16: a net loss, `view_submit` 6.4 -> 8.1 ms at close capital views: each in-flight test costs 10-25 us (per-part state swap, two pipeline rebinds) against ~10 us saved per skipped draw; the fixture's 3 us did not carry over; batched per-ship tests + an 8-frame re-test cadence for visible parts in progress as Run138; `occlusion_cull = off` in x3m.ini disables it meanwhile). Dropped the same day: the per-draw plate cull (hull pieces split by material keep every plate), the pipeline_key diagnostic row, the shader warm-up (per-state pipelines cost like the first compile; no shipped cache, no recorded list, user decisions). Earlier the same day: Run136 = engine light plate cap 8 -> 72 (user decision: no artificial limits; installed tree max 66 main nozzles; two registers per plate c55..c197, twelve uniform-branched tiers; one-plate ships bit-identical) + the shader transformers follow the device ps_3_0 slot cap (`ps3_slot_budget.h`, 32768 when the device reports <= 512) instead of a hard 512 (largest twin 904 slots), DLL SHA-256 `d03e597e44db6a63b386621b111477da638f484f609435c3b5a02ae35cec481a` (59,172,098 bytes) built once from clean main `6d912d8e` (host suite 289/3,046/0, 0 warnings, x87 0, shader checks PASS, all 8 engine records bound from the committed tree; [candidate](../verification/results/run136-candidate-build.json), [install](../verification/results/run136-candidate-install.json)), installed 15:47, rollback Run135 `7f7c4659…`; Run 136 A completed (run15: hull lights working, no stutters, but async pop-in on the Ocelot's close LOD: `dxvk.conf` deleted the same day by user decision, pipelines compile synchronously again; the DXVK state cache persists across launches, so only never-seen variants stall). Next by user decision: a per-draw plate cull (keep only the plates whose reach touches the hull piece) to bound the per-pixel walk; deferred lighting deferred to a design note if the user opens it. Earlier the same day: Run135 = capture band readback (F8 snapshots read in ~4 MB bands through cached small surfaces, peak staging 4.2 GB per burst -> 16 MB, files byte-identical; run12 on DXVK aborted 33 s after three captures when the 32-bit address space filled) + engine hull light per nozzle plate (user decision: each pixel lit by the nearest plate within reach, up to 8 plates per ship; single-engine ships bit-identical; the Ocelot's facing secondary nozzle was unlit head-on under one-light-per-ship), DLL SHA-256 `7f7c46596952340e6c4ed43f2cbe1804ca0193ae046db48967485c7fcdaad3f3` (59,149,150 bytes) built once from clean main `15d731a2` (host suite 289/3,044/0, 0 warnings, x87 0, schema 257, shader checks PASS, all 8 engine records bound; [candidate](../verification/results/run135-candidate-build.json), [install](../verification/results/run135-candidate-install.json)), installed 14:09, rollback Run134 `a9d4559a…`; Run 135 A completed (run14: better; two Ocelot nozzles lose their plate to the 8-plate cap -> Run136 in progress; present stalls after new lit draws = first-use DXVK pipeline compiles). Between 14:40 and Run 136 A the game directory carried `dxvk.conf` with `dxvk.enableAsync = True`; removed after run15 (pop-in rejected). Run 134 A completed (run12): exposure gate fine. Earlier the same day: Run134 = HDR meter lock gate (one EVENT query per readback surface issued after its copy; the latch polls without flushing and skips the lock, holding exposure, when the GPU has not finished, 16-skip cap; removes the 636 ms stall class of run11), DLL SHA-256 `a9d4559a83278e21e41facdb766864a51082d01708e1876b97e5edb54f0a7474` (59,122,340 bytes) built once from clean main `90a8b4d4` (host suite 289/3,043/0, 0 warnings, x87 0; [candidate](../verification/results/run134-candidate-build.json), [install](../verification/results/run134-candidate-install.json)), installed 04:46, rollback Run133 `1a8de604…`; Run 134 A queued. Earlier the same day: Run133 = HDR meter readback double-buffered (the latch locks the previous latch's copy, so LockRect never waits on same-frame GPU work; on DXVK that lock was 3.6 ms per frame, run10 A/B; exposure two frames old, user accepted), DLL SHA-256 `1a8de604945e3bd2686c372c5cff70aba24b46d6f17721af61a374c5665a6f00` (59,117,732 bytes) built once from clean main `d8024b96` (host suite 289/3,043/0, 0 warnings, x87 0; [candidate](../verification/results/run133-candidate-build.json), [install](../verification/results/run133-candidate-install.json)), installed 03:54, rollback Run132 `5da36870…`; Run 133 A completed (run11, DXVK: readback lock p50 3,592 -> 7.7 us, `view_setup` 4.32 -> 0.87 ms, in-flight `dt_p50` 15.8 -> 12.7 ms and `dt_p95` 18.4 -> 15.9 ms, every pass armed; open: one 636 ms lock stall with `meter_event_ready=0` at frame 7059, and the flag reads 0 on 89 % of frames while the lock stays under 1 ms; [triage](../verification/results/run133-dxvk-triage/)). The DXVK backend is the user's running configuration (wined3d run9 `dt_p50` 16.1 ms). Earlier the same day: Run132 = DXVK resource-identity fix installed, DLL SHA-256 `5da368709076dd9801bfab82c0c7815fa581eb878da68066170700538dd5cbed` (59,106,468 bytes) built once from clean main `79e15e2f` (host suite 289/3,043/0, 0 warnings, x87 0, schema check PASS, shader/site/identity reused; [candidate](../verification/results/run132-candidate-build.json), [install](../verification/results/run132-candidate-install.json)), rollback Run130 `e7ab879a…`; Run 132 A completed (run5/run6: everything working on the DXVK backend, every pass armed, in-flight `dt_p50` 14.7-15.5 ms against 18.3 ms on wined3d, first-run hitches from DXVK pipeline compilation, [triage](../verification/results/run132-dxvk-triage/)); the DXVK backend (CrossOver Preview.app files + bottle `dxvk`) is the user's current configuration. Run 131 A (run4) = the DXVK backend trial over the Run130/1.0.0 install: the game rendered, no proxy pass armed because DXVK answers GetPrivateData for an unset tag with D3DERR_INVALIDCALL/size 0 ([triage](../verification/results/run131-dxvk-triage/)); CrossOver Preview.app now carries MoltenVK 1.4.2 and the Gcenx PR #20 DXVK 1.10 build as its `dxvk` backend, user-authorised 2026-10-08, originals restorable with `~/crossover-preview-backup-2026-10-08/RESTORE.sh`; the bottle's `CX_GRAPHICS_BACKEND` is `dxvk` by the user's hand; [findings](architecture/d3d9-to-d3d11-translation.md), 2026-10-08 paragraphs). Rules: [AGENTS.md](../AGENTS.md). All goals were
marked completed on 2026-09-26 by the user's decision ([goals](goals.md)). The agent never launches the game.

## Installed build

Run139 = **engine light hold + engine-side occlusion skip** over Run138: DLL SHA-256 `d48d2bab029bad3053b21766fd51554e0dab807bd49127416dd1d79d7ed9fd11` (59,502,807 bytes) built once from clean main `8f03c306`, installed 2026-10-09 00:19 ([candidate](../verification/results/run139-candidate-build.json), [install](../verification/results/run139-candidate-install.json)); rollback Run138 `850342c0…`. New settings: `engine_light_hold` (frames, default 60, 0 = the old one-frame cut) and the `engine` value of `occlusion_cull` (builtin off). The user's x3m.ini carries `occlusion_cull = off`.

Bottle **X3**, **CrossOver Preview.app**, game tree modded with **Mayhem 3** since 2026-09-28 (user install,
`addon/05..12.cat` plus loose `addon/` files; 0.7.0 LOD overlay and fog families regenerated 2026-09-29 03:33).
Run118 = **engine effects phase 1** (design [engine-effects-modern.md](architecture/engine-effects-modern.md), user go 2026-10-01): DLL SHA-256
`924d12bdd9f4e26d8968cdd564561324bc59a3196ab340e689ee86eaae9ecb10` (58,008,081 bytes, unstripped) built once from clean main `526a741c`
(host suite 284/2,990/0, 0 warnings, x87 0, 27 verifiers PASS incl. `verify_engine_effects_sites` 20/20; [candidate](../verification/results/run118-candidate-build.json)),
installed 2026-10-01 17:49 with the engine body table `<game>/x3m/engine_bodies.json` (253 bodies, `45a1215b…`; [install](../verification/results/run118-candidate-install.json)).
Renderer equal to Run117 on the default flight: the new option `engine_effects` (`--engine-effects native|off|plumes`, ini key) defaults to `native` and the
launcher sends it only when given. `off` = two install-time call redirects in `0x00414590` (ship engine sprite + lens flare, Particles3 engine trail; missiles forwarded)
plus draw-path suppression of every JET-flagged glow draw (ships and missiles), armed only while both redirects are live; `plumes` = `off` until phase 2 lands
([ledger](verification/engine-effects.md), [RE](reverse-engineering/engine-effects.md)). Run 118 A queued = flight A, the suppression-only look. Rollback Run117.
Run130 = **co-located layers unfloored, hotkeys removed** (Run 129 A, run417: the look accepted by the user, "exactly this"; the Split Raptor's third
engine had no plume; the user asked to drop the F6/F7 keys): DLL SHA-256 `e7ab879ac7442a78f24b49a66d0da089cd5e44e374b1b55729824c968844574f`
(59,097,184 bytes) built once from clean main `01114822` (host suite 289/3,041/0, 0 warnings, x87 0, shader/site/identity reused; fixtures plumes
322/322, effects 8 modes, ribbons 44/44, shimmer 43/43, light 12 plate modes, seam 107; [candidate](../verification/results/run130-candidate-build.json)),
installed 2026-10-04 22:32 ([install](../verification/results/run130-candidate-install.json)). The layer merge no longer drops the smaller glow: a layer
(same parent and kind, ratio 0.35..0.75, parallel, within the larger's size AND within 1.5 x its own size) is drawn at its natural value without the floor
(`engine_stage unfloored=`); the Scorpion's tiny stays 5.04 inside the nor's 23.6 (side total 1.07 of the nor alone), the Raptor's big2s at 1.8 / 2.1 x their
size from the big3 are real engines, all three floored equal ([look critique](architecture/engine-exhaust-look-critique.md) section 6, "No floor instead
of the drop"). Ctrl+Alt+F6 (preset cycle) and Ctrl+Alt+F7 (shimmer toggle) removed with their rows and tests; `engine_effects_preset` and
`engine_shimmer` stay as load-time options; F8 under `--debug` is the only in-game key. All 16 look images byte-identical to Run129. Run 130 A completed in run418: all good. Release 1.0.0 in preparation from
082e1d8c (the version bump over 01114822). Rollback Run129.
Run129 = **end-on disc at the nozzle opening** (Run 128 A, run416: no visible change; the user asked for the excess glow around nozzles to go): DLL SHA-256
`87b2389f8515808954ca35f4f4a23cefef40337c4fe16e1e0d19f283047f4068` (59,101,965 bytes) built once from clean main `5e44a667` (host suite 289/3,042/0,
0 warnings, x87 0, shader checks 50 + 9 + shimmer, site/identity reused; fixtures plumes 316/316, effects 8 modes, ribbons 44/44, shimmer 43/43, light 12 plate
modes, seam 107; [candidate](../verification/results/run129-candidate-build.json)), installed 2026-10-04 21:18
([install](../verification/results/run129-candidate-install.json)). Measured on run416's captures ([triage](../verification/results/run416-engine-glow/)): the
end-on discs were drawn to twice the hull's nozzle ring (bright to 0.45 n, edge 0.70 n, the ring at 0.30 n), so a capital's nozzles 0.6-0.7 n apart overlapped
into one mass already in HDR; the halo carried 1-5 %, bloom 2 %, hull textures 2-7 %. `Look::disc_halo` 2.82 -> 1.0 and `disc_radius` 0.5 (the disc's whole
profile at half the radius, per-pixel radiance unchanged, energy x 0.27; the own ship included at the user's word; c19.x carries 1 / disc_radius for the
handover, 924 slots); 02b 5 % radius 0.64 -> 0.32 n, own chase disc 43 -> 21 px; nine side views byte-identical; end-on energy gates re-floored x 0.25; the spill
case draws at disc_radius 1 ([look critique](architecture/engine-exhaust-look-critique.md) section 6, "Disc radius"). Run 129 A completed in run417: "Exactly this. All good now"; the Raptor's third engine missing (-> Run130). Rollback Run128.
Run128 = **disc distance dimming** (Run 127 A, run415: the close-up glow confirmed back; the user asked to dim the end-on discs with distance, the capital
from straight behind reading as lamps): DLL SHA-256 `61f6be83c58f5a8c033d324e7e4434c63cd73bb5ff561dbb6c15bec4b503af65` (59,101,453 bytes) built once
from clean main `f3abf618` (host suite 289/3,042/0, 0 warnings, x87 0, shader/site/identity reused; fixtures plumes 312/312, effects 8 modes, ribbons 44/44,
shimmer 43/43, light 12 plate modes, seam 107; [candidate](../verification/results/run128-candidate-build.json)), installed 2026-10-04 19:49
([install](../verification/results/run128-candidate-install.json)). The end-on disc (body, halo, ring, hot centre) x 0.5 + 0.5 smoothstep(20, 160, px) of the
drawn nozzle width (`Look::disc_far_low`, `disc_px_min`, `disc_px_full`; the far law under 12 px and the chase fade multiply on top); the own ship exempt
(`Ring::own`); the axial body, side views and ribbons unchanged (nine side images byte-identical). 20 / 65 / 160 px -> 0.50 / 0.62 / 1.0; five end-on gates
re-floored by the law's factor ([look critique](architecture/engine-exhaust-look-critique.md) section 6, "Distance dimming of the disc"). Run 128 A completed in run416: no visible
change to the eye (the 0.62 factor sits under the AgX shoulder); the capital-from-behind lamps look is the tonemapper desaturating bright red, which only a
hue-preserving emitter tone curve would change; the engine exhaust work is closed here by the user's acceptance. Rollback Run127.
Run127 = **mouth whiteness reverted** (user decision after Run 126 A): DLL SHA-256 `2433cda8c265975d2706c9460b36729e377fba9295bb4d56baf8463427d6fd75`
(59,098,881 bytes) built once from clean main `6e7d8daa` (host suite 289/3,042/0, 0 warnings, x87 0, shader/site/identity reused; fixtures plumes 300/300,
effects 8 modes, ribbons 44/44, shimmer 43/43, light 12 plate modes, seam 107; [candidate](../verification/results/run127-candidate-build.json)), installed
2026-10-04 18:50 ([install](../verification/results/run127-candidate-install.json)). `Look::heat` 0.7, `head_min` 0.75, I(s) 1.2..4.0 (the Run123 values; the
Run124 change dimmed red heads to 0.78 for a side-view clip reduction nobody noticed); everything after Run124 kept. Side views and far dots byte-identical to
the Run123 images, red total 1.27 of Run126, 02b red centre whiter but unclipped. Run 127 A completed in run415: the close-up glow back, the look accepted (-> Run128 distance dimming). Rollback Run126.
Run126 = **engine rings and close-up glow** (Run 125 A, run413: own ship better; the Ocelot's stern white rings with pink centres, the glow gone at close
range): DLL SHA-256 `b7e41991543c7b7b80dadc9099707e20f10b9a632a6a9f15d8eac5e513a49c20` (59,099,909 bytes) built once from clean main `00e184cf` (host
suite 289/3,042/0, 0 warnings, x87 0, shader checks 50 + 9 + shimmer at 4712dbd8 reused, site/identity reused; fixtures plumes 300/300, effects 8 modes,
ribbons 44/44, shimmer 43/43, light 12 plate modes, seam 107, hull light-map gain 108; [candidate](../verification/results/run126-candidate-build.json)),
installed 2026-10-04 16:24 ([install](../verification/results/run126-candidate-install.json)). Measured on run413's captures
([triage](../verification/results/run413-engine-rings/)): the white ring is the hull's white light-map annulus x gain 4 on every nozzle except the one holding
the engine light (one light per ship), the pink band the plume's own disc ring (x 8); at close range the near cap shrank the nozzle width with the length
(k 0.26..0.36) so the disc sat inside the plate; the merge dropped the Ocelot's big3 side nozzles (own rims and plates). Fixes: (1) up to 8 nozzle plates per
lit ship at c190-c197 (tier c202.w, two uniform if_ne branches on temps so a fighter pays one slot; largest twin 368 slots; 100 of 104 gained programs carry
plates, the 4 others are glass without gain; [engine-light.md](architecture/engine-light.md) "Nozzle plates"); (2) `disc_cap` 1.0, `disc_ring` 3 (red
end-on peak 2.17 -> 1.5, no clip, the ring no longer reads on cyan, seven end-on gates re-floored); (3) the body keeps the near cap, the end-on disc and halo
the natural nozzle width under a 0.35 H radius cap (near side views bit-identical, 02a/03b brighter: full-size discs); (4) `merge_layers` ratio window
[0.35, 0.75] (the Scorpion's pair merges, the Ocelot's side nozzles kept); review fixes (temp-operand branches, the plate upload split from the DEF'd c198/c199,
disc shape.y natural). Run 126 A completed in run414 (features live: plates rows up to 8, merged=1 on the own ship): the user reads it as about the same; oblique and side views good, the capital straight from behind still pink-white lamps (the AgX shoulder desaturates bright red toward white; the plate textures are bright at gain 1; the Ocelot has 10 main nozzles against 8 plate slots). Accepted as the look for now. Open: the disc's occlusion bias still from the shrunk value; native Windows unverified for the if_ne twins; plate slots 8 < 10 on the Ocelot; heat 0.1 / head_min 0.42 unjudged on side views. Rollback Run125.
Run125 = **chase-view engine blob** (Run 124 A, run412: the own ship's engine from behind a clipped pink-white disc 4x the nozzle opening, no visible
difference from Run124): DLL SHA-256 `e7f34300c30845284c52cf48e2f753f42f1c781d07ca9c1ff135d64abce477b6` (59,079,594 bytes) built once from clean main
`6741928d` (host suite 289/3,041/0, 0 warnings, x87 0, shader/site/identity evidence reused unchanged; fixtures effects 8 modes, plumes 294/294, ribbons
44/44, shimmer 43/43, light 9 cases + plate 8/8, seam 107, hull light-map gain 108 programs; [candidate](../verification/results/run125-candidate-build.json)),
installed 2026-10-04 14:30 ([install](../verification/results/run125-candidate-install.json)). Measured on run412's F8 HDR captures
([triage](../verification/results/run412-engine-disc/)): the white is the hull light-map gain 4 on the Scorpion's pure-white recessed nozzle plates (~4.0
neutral, display white from 2.1 at that exposure); the size and the red are two floored glow layers of one nozzle (xtc_red_nor 10 + xtc_red_tiny 5 at one
origin, both raised to ~20-23) stacked 1.74x, plus an end-on disc the chase fade could not dim. Three fixes: (1) the end-on disc's own near-camera fade to
`chase_disc_floor` 0.4 (was max(body fade, 0.6)); (2) co-located layers merged into one plume/disc/ribbon/far record (same parent and kind, the smaller
<= 0.75 of the larger, axes parallel, origins within the larger's pre-floor size; the capital's 4 of 8 big3 inside its 2 huge merge too; `engine_stage merged=`);
(3) in the 100 engine-light twins that host the light-map gain, the gain falls back to 1 within 0.75 value_eff of the engine light (0 from 1.0; +4 slots, largest
twin 331; [engine-light.md](architecture/engine-light.md) "Nozzle plates": limits one light per ship, the unbind jump, `engine_light` off keeps the gain).
Expected (inferred): the plate's neutral excess 3.7/4.3/4.3 -> ~0.7/1.3/1.3, the plume centre ~4.4/1.4/1.4, so the engine reads red. Run 125 A completed in run413: own ship better, the Ocelot still bright, the glow gone at close range (-> Run126).
Rollback Run124.
Run124 = **plumes default + mouth whiteness** (Run 123 A, run409: no issues, the default preset's mouths too white, restrained preferred): DLL SHA-256
`f35a07f1412b0a8afbeaa93f1ae9b9d7f85d79a09029bf5931c08e54ac721b3e` (59,061,764 bytes) built once from clean main `9c0c57ab` (host suite 289/3,040/0,
0 warnings, x87 0, shader checks reused (no shader change), `verify_engine_effects_sites` 20/20 + identity 27/27; fixtures effects 8 modes, patch 62/62,
plumes 286/286, ribbons 44/44, shimmer 43/43, light 9 cases + seam 107; [candidate](../verification/results/run124-candidate-build.json)), installed
2026-10-04 05:19 ([install](../verification/results/run124-candidate-install.json)). `engine_effects` unset or empty is now `plumes` in the DLL, the redirect
module, the schema, the ini template and the launcher (an invalid setting still falls back to native; `--engine-effects native` restores the game's effects),
so a plain launch flies the Run 123 A setup. Mouth whiteness ([look critique](architecture/engine-exhaust-look-critique.md) section 6, "Mouth whiteness"):
`Look::heat` 0.7 -> 0.1, `head_min` 0.75 -> 0.42, I(s) x 1.04; constants only. Measured on the fixture images: nozzle pixels clipped to white at the default
preset 0 on every s 1 side view where restrained gave 0, 0.006 / 0.011 at the two 40 px views (restrained 0.034 / 0.019); body energy, far dots and every
gate within 1 %; red plumes 0.78 of their previous total (the dimmer head), red end-on centre still 0.046 clipped. Run 124 A completed in run412: no visible difference, the chase-view blob (-> Run125). Rollback Run123.
Run123 = **engine exhaust batch** (Run 122 A findings + the X4/Everspace 2 gap analysis [engine-exhaust-gap-analysis.md](architecture/engine-exhaust-gap-analysis.md)
+ the look critique [engine-exhaust-look-critique.md](architecture/engine-exhaust-look-critique.md)): DLL SHA-256
`a252c522991d9407d3371df215b6d205b24e27957372581e66560c89afc1ae89` (59,061,764 bytes) built once from clean main `c8f36eb6` (host suite 289/3,040/0,
0 warnings, x87 0, shader checks 50 + 9 + shimmer, 27 verifiers; fixtures plumes 286/286, ribbons 44/44, effects 8 modes, shimmer 43/43, light 9 cases + seam 107,
patch 54/54, cull 256; [candidate](../verification/results/run123-candidate-build.json)), installed 2026-10-04 03:30 ([install](../verification/results/run123-candidate-install.json)).
Over Run122, all behind `plumes`: far jets recorded by the small-parts cull stub (no engine submission, distance law 0.15 at 2 px, 2x4 px floor); halo 0.20,
floor 0.5; world-unit flow, two-tone colour, nozzle spill, idle floor, RCS attack, SETA travel look (`cfg+0xcc`, fail closed); engine light on the hull
(`engine_light`, twins of the reviewed hull programs, one light per ship from the previous frame); heat shimmer after the resolve (`engine_shimmer`, max 4,
Ctrl+Alt+F7); the single fire law (thin hot core, dark-gap cells, 4.5:1 streaks, saturated outer sheath, detail parameter below 40 px; ps 921 slots).
Five reviews with fixes. Run 123 A completed in run409: no issues (frame time median 18 ms, plumes armed throughout, far sparks, hull light, shimmer and SETA rows clean), restrained preferred over default for the white mouths. Rollback Run122.
Run122 = **plume floor from the ship radius + mouth fix** (Run 121 A findings): DLL SHA-256
`4d5cbb9cba631486e0dc02c0e8ba697bc57cca1af0036d6870c450b7cc8f03af` (58,511,846 bytes) built once from clean main `192e1e3f` (host suite 286/3,011/0,
0 warnings, x87 0, shader checks 50 + 9, 27 verifiers; fixtures plumes 118/118, ribbons 44/44, effects 8 modes, patch 54/54 rerun;
[candidate](../verification/results/run122-candidate-build.json)), installed 2026-10-03 16:35 ([install](../verification/results/run122-candidate-install.json)).
Over Run121: a main nozzle draws `min(max(value, k(R)·R), 4·value)` with R = the ship root node's cached subtree radius (`+0xa4`, read through the jet's
parent at draw time, four-entry memo, plausibility bound 10,000× the value) and k log-linear through (150, 0.35), (500, 0.25), (5,000, 0.10) record
units, RCS/brake excluded, `engine_plume_floor` scaling the curve (0..3, 0 off); the per-frame ship map is gone (the game culls nozzle nodes one by one,
run406). The mouth terms follow the body's throttle curve and the body ramps in over 0.3 L (mouth/body 0.60/0.66/0.77 at s 1/0.5/0, gated ≤ 0.85).
`engine_draw` rows carry `radius=`/`value_eff=` (first live check of the radius read). Reviewed (Opus, 12 items fixed). Run 122 A queued. Rollback Run121.
Run121 = **plume adjustments after flight C** (Run 120 A accepted the look at `engine_plume_nozzle 0.5`): DLL SHA-256
`a124a5afd0fe0f40c2abcb15fa55d4a826c17e320960969af5ddad516d329571` (58,489,756 bytes) built once from clean main `8fd8d48d` (host suite 286/3,008/0,
0 warnings, x87 0, shader checks 50 + 9, 27 verifiers; all four engine fixtures rerun: patch 54/54, plumes 116/116, ribbons 44/44, effects 8 modes;
[candidate](../verification/results/run121-candidate-build.json)), installed 2026-10-03 13:48 ([install](../verification/results/run121-candidate-install.json)).
Over Run120: nozzle default 0.5; the end-on disc represents the whole plume (8-sample axial average, facing blend 0.3-0.7, soft cap 1.5x the side peak,
L/n gain bounded at 8); per-ship floor 0.45 x the ship's largest nozzle for secondary jets (`Snapshot::parent` = node+0x18, RCS and brake jets excluded,
open-addressing map of 2,048); mouth terms as a soft maximum with the ring 0.3 and the shock cells ramping in; the chase cap/fade keyed on the body width
with the disc kept >= 0.6 under the fade; the plume (ps 680 slots) and ribbon passes no longer refuse on the reported 512 cap (creation is the test);
a refused/disarmed stage forwards the glow natively (`forwarded_stage_off`). Reviewed (Opus, 11 findings fixed). Run 121 A queued. Rollback Run120.
Run120 = **plume look port** (the Engine Exhaust Lab law, `tools/effects/engine_exhaust_lab.html`, user settings of 2026-10-03): DLL SHA-256
`c44a0887262e0ae03ec9392e9c798e9971fd372a46adc0e8c6c03032c7493527` (58,432,188 bytes) built once from clean main `0cc93fa8` (host suite 286/3,008/0,
0 warnings, x87 0, shader checks 50 + 9, 27 verifiers; fixtures plumes 90/90, ribbons 44/44, effects all modes, patch 54/54 carried;
[candidate](../verification/results/run120-candidate-build.json)), installed 2026-10-03 04:29 ([install](../verification/results/run120-candidate-install.json)).
Over Run119: the plume pixel law is the mock-up's (bulge-and-taper body, 3-octave value noise flowing at constant speed via a CPU phase, shock diamonds,
white-hot core, nozzle ring, 25 % length pulse; ps 385 slots), halo at full strength, chase cap on the drawn width, the stage clock capped on capture frames
(ribbons survive F8s), `engine_draw` rows on every F8 frame, one knob `engine_plume_nozzle` (default 0.25 = nozzle width as a quarter of the glow body;
`--engine-plume-nozzle 0.5` for the A/B). Two reviews (Opus). Run 119 A (run403) had rejected the first analytic cone as static. Run 120 A queued = flight C.
Rollback Run118 (the Run119 candidate DLL is no longer retained; rebuildable from `bfe68791`).
Run119 = **engine plumes + ribbons** (phases 2 + 3 of [engine-effects-modern.md](architecture/engine-effects-modern.md)): DLL SHA-256
`582270816fabd97b719c9c87f08770abc1fde76691099829eceb575fe9da8c04` (58,400,309 bytes, unstripped) built once from clean main `bfe68791`
(host suite 286/3,005/0, 0 warnings, x87 0, shader checks 50 + 9, 27 verifiers; fixtures plumes 72/72, ribbons 44/44, effects armed 15, patch 54/54;
[candidate](../verification/results/run119-candidate-build.json)), installed 2026-10-03 02:20 after Run 118 A accepted the suppression-only look
([install](../verification/results/run119-candidate-install.json); the engine body table unchanged). Default `native` (renderer equal to Run118 on the
default flight); `--engine-effects plumes` arms the proxy stage in the TAA resolve's step-0 bracket: one batched plume draw (axial trapezoid + nozzle disc,
throttle from the jet's z-scale, tint from the body table, soft depth-lane occlusion, chase cap) and one ribbon draw (256 ring buffers of 16 distance-spaced
samples, length T(value)·v·s, 3 px floor, 0.3 s fade), fog transmittance on both, presets `--engine-effects-preset restrained|default|strong` cycled by
Ctrl+Alt+F6; two reviews (Opus 11 fixes, Fable 4 fixes) ([ledger](verification/engine-effects.md)). Run 119 A queued = flight B. Rollback Run118.
Run118 = **engine effects phase 1** (design [engine-effects-modern.md](architecture/engine-effects-modern.md), user go 2026-10-01): DLL SHA-256
`924d12bdd9f4e26d8968cdd564561324bc59a3196ab340e689ee86eaae9ecb10` (58,008,081 bytes, unstripped) built once from clean main `526a741c`
(host suite 284/2,990/0, 0 warnings, x87 0, 27 verifiers PASS incl. `verify_engine_effects_sites` 20/20; [candidate](../verification/results/run118-candidate-build.json)),
installed 2026-10-01 17:49 with the engine body table `<game>/x3m/engine_bodies.json` (253 bodies, `45a1215b…`; [install](../verification/results/run118-candidate-install.json)).
Renderer equal to Run117 on the default flight: the new option `engine_effects` (`--engine-effects native|off|plumes`, ini key) defaults to `native` and the
launcher sends it only when given. `off` = two install-time call redirects in `0x00414590` (ship engine sprite + lens flare, Particles3 engine trail; missiles forwarded)
plus draw-path suppression of every JET-flagged glow draw (ships and missiles), armed only while both redirects are live; `plumes` = `off` until phase 2 lands
([ledger](verification/engine-effects.md), [RE](reverse-engineering/engine-effects.md)). Run 118 A queued = flight A, the suppression-only look. Rollback Run117.
**Run119 candidate, not installed** (built 2026-10-01 from clean `bfe68791` = engine effects phases 2 + 3: plume stage and ribbon trails in the TAA
resolve's step-0 bracket, fog transmittance, presets `--engine-effects-preset restrained|default|strong` with Ctrl+Alt+F6, two reviews with fixes):
DLL `582270816fabd97b719c9c87f08770abc1fde76691099829eceb575fe9da8c04` (58,400,309 bytes) at `/tmp/x3-run119-candidate/build/d3d9.dll`
([candidate](../verification/results/run119-candidate-build.json): host suite 286/3,005/0, shader checks 50 + 9, 27 verifiers, fixtures plumes 72/72,
ribbons 44/44, effects armed 15, patch 54/54). Default `native`; it waits for the Run 118 A verdict (flight A), then installs as Run119 and
flight B = `--engine-effects plumes` with the F8 set of the design note (own ship at rest/full speed in chase, capital from behind and from the
front, fighters over a starfield, one F8 with a target selected for `engine_stage view_rule=`/`skipped_other_view=`).

Run117 = **release 0.9.0**: stripped DLL SHA-256 `72db0628651f7afac8bd5073e9a93a8317d3988d7c6c2bb9dc1aadeb30d96d0f`
(39,610,685 bytes; unstripped `89fd0e0b…`, debug file `bd63b95b…` under `/tmp/x3m-release-0.9.0/`), built once from clean
main `c1baa169` (host suite 280/2,950/0, 0 warnings, x87 0 on both DLLs, strip identity PASS, zip entries exactly 22, regenerate
binaries reused from 0.8.0; [release record](../verification/results/release-0.9.0.json)), installed 2026-09-30 with the zip's
16 font files in `<game>/f/` ([install](../verification/results/run117-release-install.json)). Renderer code equal to Run116
(`846f9c16`) plus the 0.9.0 defaults: `ui_scale = auto` (height/1080 rounded to the nearest half: 1440 -> 1.5, 2160 -> 2) and
`text_density = auto`. Zip `/tmp/x3m-release-0.9.0/x3m-0.9.0.zip` (`068ab902…`, 59,642,766 bytes: d3d9.dll, x3m.ini,
x3m-regenerate.exe, f/<16 fonts>, OFL-NotoSans.txt, OFL-Exo2.txt, README.txt). `X3AP.exe`, `cxbottle.conf`, the user's `x3m.ini`
and the 0.8.0 `x3m-regenerate` binaries unchanged.

Rollback chain: Run122 `4d5cbb9c…` at `/tmp/x3-run122-candidate/build/d3d9.dll`, then Run121 `a124a5af…` at `/tmp/x3-run121-candidate/build/d3d9.dll`, then Run120 `c44a0887…` at `/tmp/x3-run120-candidate/build/d3d9.dll`, then Run118 `924d12bd…` at `/tmp/x3-run118-candidate/build/d3d9.dll` (Run119 `58227081…` not retained), then Run117 = release 0.9.0 `72db0628…` at `/tmp/x3m-release-0.9.0/zip-extract/d3d9.dll`, then Run116 `68bd5f41…` at `/tmp/x3-run116-candidate/build/d3d9.dll`, then Run115 `fe31263b…` at `/tmp/x3-run115-candidate/build/d3d9.dll` (fonts under its `build/fonts/F`), then Run114 `41ff9b41…` at `/tmp/x3-run114-candidate/build/d3d9.dll` (its fonts under `/tmp/x3-run114-candidate/build/fonts/F`), then Run113 `44208d1f…` at `/tmp/x3-run113-candidate/build/d3d9.dll` (fonts in `f/` stay; a non-density DLL never requests them), then Run112 `ea3ad671…` at `/tmp/x3-run112-candidate/build/d3d9.dll`, then Run111 = release 0.8.1 `e123b7ed…` at `/tmp/x3m-release-0.8.1/d3d9.dll`, then Run110 `e5e7ac15…` at `/tmp/x3-run110-candidate/build/d3d9.dll`, then Run109 `09f08cff…` at `/tmp/x3-run109-candidate/build/d3d9.dll`, then Run108 `c57556bb…` at `/tmp/x3-run108-candidate/build/d3d9.dll`, then Run107 `b9e8793c…` at `/tmp/x3-run107-candidate/build/d3d9.dll`, then Run106 `d07848e8…`, then Run104 = release 0.8.0 `f34d3ab4…` (zip at `/tmp/x3m-release-0.8.0/`, entry `d3d9.dll`, extracted copy
at `/tmp/x3m-release-0.8.0/zip-extract/d3d9.dll`), then Run103 `1f3ad3db…` at `/tmp/x3-run103-candidate/build/d3d9.dll`.

Run117 = 0.9.0 over Run116: defaults `ui_scale = auto` and `text_density = auto` (the bare DLL and the launcher default flight
both scale the UI now; `--vanilla` unaffected), the density fonts committed under `assets/fonts/generated/F` and shipped in the zip
with the OFL texts, version 0.9.0. Run 116 A (run394 at 1.25, run395 at 1.5) accepted the text readability; the user prefers 1.5
at 5120x1440 and made it the 1440-row auto value; the fractional engine font scale is not pursued (large blast radius).
Run116 over Run115: Run 115 A (run393) had every letter present but uneven stroke weight (2-texel stems on 1.25 screen px under the
1.6x minification; the engine's samplers were already anisotropic/linear, so the override now raises POINT only). The Tahoma family
default is now Noto wght 600 with the grey stroke floor (stems ~2.66 texels, antialiased edges; chosen from simulated minification
previews, `tools/fonts/preview_minified.py`); the LARGE family is unchanged ([fonts](architecture/font-assets.md) "Readability under
minification"). Not yet flown.
Run115 over Run114: Run 114 A (run392) showed the mechanism working (density 2, rows 32/8/28, fonts scaled, shadows built, layout right) but
narrow glyphs (i, l, r) vanished: the generator's left-edge snap had a sign error cutting up to a texel of ink (1.15-texel stems) and the
scaled text quads sampled POINT (the engine's material routine sets it from the body record flag 0x100 copied before the row edit) under
the 1.6x minification. Fixed: the generator snap plus a stroke floor (every stroke >= d texels, `stem_coverage.py` PASS for all families)
and a proxy-side sampler override (MIN/MAG LINEAR around every draw whose stage-0 texture is a flagged row's D3D texture when s != d;
`text_density_filter` rows) ([RE](reverse-engineering/font-rendering.md) section 5, [ledger](verification/text-density.md)). Flown as Run 115 A (run393): all letters present.
Run114 over Run113: `text_density` (`--text-density auto|1|2|3`, ini `text_density`, default `auto` = ceil(`ui_scale`), 1 when the UI
scale is 1): sharp text under the UI scale through the engine's own retail `-fontscale` path at integer density d. The DLL writes the
config field at CreateDevice, the font-open detour (`0x0048cdc0`) requests `F\<name><S*d>` (generated by `tools/fonts/generate_fonts.py`
from bundled OFL fonts: Noto Sans for Tahoma, Exo 2 for Zekton/ZektonES/Harrier; installed as loose `f/` files by `manage.py install`),
the redirected Materials load flags the 35 writable generated rows `MPF_FONTSCALE`, blits of unscaled sources into flagged rows go through
a d-times shadow cache, the style table factor is patched for d = 3; all-or-none, refused when any font pair is missing or the caps do not fit.
At s = 1.25, d = 2 minifies 1.6x (bilinear) instead of magnifying 1x glyphs ([note](architecture/text-density.md),
[fonts](architecture/font-assets.md), [RE](reverse-engineering/font-rendering.md), [ledger](verification/text-density.md)). Flown as Run 114 A (run392): the text-target set held (no double-size text, no black icons).
Run113 over Run112: two more `ui_scale` claims, G (`0x0042ece0`, INS case 0x64) and H (`0x0042ddf1`, case 0x28), multiply
the script's virtual click point by s before the native bracket hit test `0x004299a0`, fixing click selection of ships in the
3D view (Run 112 A / run390: layout, menus, map and main menu correct, bracket clicks dead; [RE](reverse-engineering/gui-scale.md) section 6).
Bitmap text under the scale was soft/broken at 1.25 (user report), addressed by Run114.
Run112 over Run111: `ui_scale` (`--ui-scale S|auto`, ini `ui_scale`, default `1` = off; `auto` = back-buffer height / 1080
snapped down to quarter steps, 1440 -> 1.25, 2160 -> 2) enlarges the in-game 2D UI (script menus, sidebars, panels, ticker) by
scaling the engine's pixel orthographic projection for node-flag-`0x200` instances about their anchor, telling the script a virtual
screen size W/s x H/s, dividing script mouse deltas by s with a remainder accumulator and keeping the native cursor globals in real
pixels; the active cockpit's HUD camera is excluded by identity so the crosshair group, target icons and the lead marker stay in
real pixels; the main menu uses the perspective path and is untouched. Eight sites, one transaction at CreateDevice, `ui_scale_install`
row; bitmap text is magnified with the texture filter (soft at non-integer scales; sharp-text strategy deferred)
([note](architecture/ui-scale.md), [RE](reverse-engineering/gui-scale.md), [ledger](verification/ui-scale.md)).
Run110 over Run109: `lens_flare_gain 0` now culls the lens-flare sprite nodes in the engine's own cull pass (a second
stub chained on the small-parts claim at `0x0047d2a2`; the 42 flare bodies resolved from the body table; the proxy skip
stays as fallback; [ledger](verification/sun-occlusion.md), [note](reverse-engineering/lod-selection.md) "Lens-flare cull"),
and the dock-port cull `cull_dock_parts_px` (default 12 screen px, `--cull-dock-parts`, 0 = off): carrier launch tubes
and hangars, stock dock scenes with inline bodies and a single LOD record ([note](reverse-engineering/ship-scene-parts.md)),
are culled in the same stub below that size (run385 replay: 159/27/27 of the bursts' 186/27/52 dock draws;
[ledger](verification/cull-small-parts.md) "Dock ports"). Purpose: measure whether not issuing the draws recovers the
engine's ~24 us per draw (the proxy-side flare skip of Run 105 A recovered ~1 ms only).
Run109 over Run108: the `dust_leak_fix` engine patch, on by default (`--dust-leak-fix off` / ini `dust_leak_fix = off`;
one 5-byte claim at `0x0041f4d1`, the dust-scene fill loop's common tail: a node whose dust body failed to load is
released through the engine's own `0x00487be0` and the fill ends for that frame; [ledger](verification/dust-leak-fix.md),
[note](reverse-engineering/object-lifetimes.md) Run383 "Patch"), and `cull_small_props` on by default (unset = on,
`off` turns it off; the `no_px` and `route_off` gates unchanged, so `--vanilla` and the bare DLL stay off).
Run108 over 0.8.0: the `scene_graph_census` row (engine node/scene/cut/task counts, newest unattached nodes by body,
insert-caller histogram; [note](reverse-engineering/object-lifetimes.md) Run382); four `loop_phases` stamps around the three calls of the main loop's `input_part=0` region
(`cutevent`, `containers`, `sweep`, `region`; [note](reverse-engineering/main-loop-input-region.md) §6); `lens_flare_gain 0` skips the admitted lens-flare draws instead of drawing them invisibly, and the
opt-in `cull_small_props` (proxy-side skip of `ships\props\` draws under the small-parts pixel threshold, own ship and
target exempt, no engine write; since Run106 the size test uses the draw's own vertex extent, the engine's part box
being 4.7x+ the drawn geometry on Mayhem turrets, and `frame_end` carries `issued=`) ([cull-small-parts](verification/cull-small-parts.md),
[sun occlusion](verification/sun-occlusion.md)). Motivation: run375 draw attribution
([results](../verification/results/run375-draw-attribution/)): 352 game draws at a Mayhem battle group = 169 lens-flare
sprites (stock bodies `objects/v/00752..00766`, ~8 per engine) + 82 Split turret props (76 under 4 px) + 28 ship hulls +
25 carrier + 14 station + 10 engine glows + 5 sky + 9 HUD.

Last release: **0.9.0** from `c1baa169` ([release record](../verification/results/release-0.9.0.json)); 0.8.1 from `f4439590` before it.

## Main beyond the installed build

Engine hotkeys removed (2026-10-04, user decision; not yet in a candidate, the installed build still has both): Ctrl+Alt+F6 (plume preset cycle) and Ctrl+Alt+F7 (heat shimmer toggle) are gone, F8 under `--debug` is the only in-game key; `engine_effects_preset` and `engine_shimmer` stay load-time options ([in-game keys](architecture/comparison-hotkeys.md), [ledger](verification/engine-effects.md)).

Nothing beyond the installed Run117 (`c1baa169`); release 0.9.0 is the last packaged build; release 0.8.1 (`f4439590`) is the last packaged build.

## Run queue

Run 117 A completed (run400): release 0.9.0 confirmed on the auto defaults (ui_scale 1.5 at 1440 rows, text density 2). No run open. Run 115 A completed (run393): all letters present, stroke weight uneven (addressed by Run116). Run 114 A completed (run392): mechanism worked, narrow glyphs vanished (fixed in Run115). Run 113 A completed (run391): bracket click selection works; the ui_scale layout path is accepted at 1.25. Run 112 A completed (run390): UI scaled correctly, menus/map/main menu fine; bracket clicks dead (fixed in Run113), text soft/broken (open).
Run 110 A completed (run386-389, [results](../verification/results/run386-389-cull-ab/)): at one carrier
view without `--perf`, the dock-port cull (12 px) saves 2.4-3.7 ms per frame (about 57 fps against 47-50 with it off,
measured from 300-frame clock windows, one run per setting); the engine-side flare cull saves 0 ± 0.5 ms (1.2 ms under
`--perf`, where the flare draws carry the instrument cost). Both culled exactly what they should (166 flares per frame
culled = the 166 drawn before; `dock_culled` equals the census `culled_dock`, largest culled part s=7). Decision: the
dock cull stays on by default; `lens_flare_gain 0` keeps the engine cull but a flare batcher is not worth building.
`v\01009` and `v\10667` are flare-like bodies outside the 42-name set (about 0.2 draws per frame, left alone). Still
open from this pair of runs: frame time creeps with time since load at a constant draw count (run385: 16.4 -> 19.3 ms
over 3,600 frames at 335 draws), the pre-render drift noted below.

Run 109 A completed (run384, [results](../verification/results/run384-dust-leak-fix/)): with
`dust_leak_fix` patched the census stays flat (engine_nodes 4,515 -> 4,853 over 19,826 frames on `litcube75`, hits=300
per row), the animation tick `cutevent` stays at 0.1-0.4 ms (run383: 0.5 -> 10.8 ms) and the shimmer did not return.
Late frames (run384/run385 triage, [attribution](../verification/results/run385-draw-attribution/)): frame time follows
the draw count (engine `views` 24-26 us per issued draw); LOD selection correct in every census row (Raptor switch 5.7 km,
Ocelot 6.4 km); burst A's 642 draws = 186 dock-port parts + 168 flare sprites + 77 props + 48 hulls + 68 asteroids;
a second slow drift in the game's pre-render outside the stamped region (3.6 -> 5.8 ms over the stay) is unexplained.

Run 108 A completed (run383, [census](../verification/results/run383-scene-census/),
[dust leak](../verification/results/run383-dust-leak/)): the Mayhem 3 scene-node leak is the engine's dust-scene fill
`0x0041efc0` (the worker behind `INS_UpdateDustScene`, run every frame by the cockpit update): it allocates each dust
node before loading its body and drops the node on the unattached list when the body is missing, so the next frame
allocates again. Rate = the background's `NumDustInstances` (27 on `litcube75`). ZMap's Fog slider sets dust rates and
density on every row, but only 134 of the 317 nebula families in the Mayhem cats ship dust bodies: 170 of 248 rows
leak (stock: 1 row, `xtmgreenring`). `registry_live == engine_nodes` on every census row; the animation tick
`0x0048f550` costs 76.5 ns per node per frame (56 -> 33 fps over 5,100 frames). Report for the mod's maintainers:
[report-for-mayhem-author.md](../verification/results/run383-dust-leak/report-for-mayhem-author.md). The same 170
families are the fog baker's `no_dust_bodies` refusals, which explains the fog `card_refused` seen in run374; the user
chose to keep fog only where dust bodies exist (sky-derived fog not pursued).

## Open items

- From Run 91 A: bullets behind distant objects = the game's early bullet copy overpainted by later opaque draws while
  the additive route brightens both copies (fixed: Run94 composites the bolt back over held far/thin pixels, accepted in Run 94 A at W 1); station blur under a pan =
  history weight (0.85 accepted in Run 92 A; the rotation-aware weight stays opt-in and off after Run 93 A: the user prefers blur to shimmer).

- Run374 performance triage ([results](../verification/results/run374-performance/)): 23 ms p50 at 5120x1440 on Mayhem,
  the proxy's own CPU work about 1.2 ms of it, the rest in the game's pre-render/submit (CPU vs GPU not split without
  `--gpu-sync-timing`); Mayhem draws 1.6x stock; volumetric fog never applied in that sector (`card_refused`): the family has no dust bodies (Run 108 A).
- Native Windows runtime behaviour is unverified; the source cross-compiles, gaps are tracked in
  [platform portability](architecture/platform-portability.md).
- Config design steps 3 (typed values per family) and 4 (generated inventory tables) are still to come
  ([config-file.md](architecture/config-file.md)).
- MetalSharp: `~/.metalsharp/sharp-library/library.json` is absent, so `prepare_metalsharp.py --check` fails
  ([experiment](verification/metalsharp-experiment.md)).
- `x3m-regenerate` 0.7.0 ran on Mayhem 3 (2026-09-29 03:33: 1,157 bodies baked, 2,014 refused, 70 fog families); the visual
  check of the new bodies is still deferred by the user. Sky-derived fog for the 170 `no_dust_bodies` families is
  undecided ([probe](../verification/results/fog-families-mayhem/)).

History: handoffs, status history and completed runs are in [docs/archive/](archive/).
