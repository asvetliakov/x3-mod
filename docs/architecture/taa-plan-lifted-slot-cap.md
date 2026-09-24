# TAA thin geometry re-planned without the 512-slot cap

Question: the TAA thin-geometry plan ([taa-thin-geometry-alternatives.md](taa-thin-geometry-alternatives.md), ratified
2026-09-24) was built on `MaxPixelShader30InstructionSlots = 512` as a hard limit: A' waited on S3's slot yield, E and
the tests-into-resolve fold were refused as slot-bound, and four S3 programs dropped their exact point read at rest to
stay under 512. The slot-budget fixture shows the cap is a reported number, not an enforced one, and the user decided
(2026-09-24) to plan against the modern-driver cap of 32,768; the committed rule (AGENTS.md "Shader slot budget",
0dee63cb) logs the cap at device creation and makes creation failure the only fallback (no second program set). What is
the new budget rule, what does A' cost when written directly into the resolve, which refused consolidations are now
worth taking, in what order, and what does a larger resolve do to the first-draw compile?

Inputs: `verification/results/shader-slot-budget/summary.md` and `verification/results/bottle-X3/shader-slot-budget.json`
(fixture `shader_slot_budget_fixture.cpp`, bottle X3, 2026-09-24), the ratified alternatives note, the S1-S6 plan and
Run 77 C figures ([taa-high-resolution.md](taa-high-resolution.md); `verification/results/run289-290-march-scale/summary_out.txt`),
the refused resolve edits (engine-frame-time.md, "TAA stage cost", Rejected), the lattice review's separable-pass choice
(lattice-approach-review-2026-09-21.md section 8), and the S3 implementation in the agent worktree (`src/temporal/resolve.hlsl`,
`temporal_resolve_*_program_inc.h`, the lattice report's `RESOLVE_BUDGET` rows, `docs/verification/temporal-resolve.md`
"5-tap bilinear history"). **[M]** measured in those records, **[I]** inferred here. Nothing in this note was run under
Wine or flown; no source was edited.

## 1. Decision

**Step 1 done (2026-09-24).** A' was built (df01f23b; [temporal-resolve.md](../verification/temporal-resolve.md) "A' region
hold"), flown and accepted in Run 79 A (run299/300/302/303: no visible difference against the dilated chain, `taa_mask`
2.93 -> 1.54 ms, net -1.1 ms per frame at 5120x1440), and the acceptance clause is carried out: `--taa-region-hold`, the
dilated `far_camera` chain (the camera mask's x / y draws, `resolve_far_camera` and its 16-tap twin, the ungated box
programs) and the second mask target of camera-gate runs are removed, the hold is the camera gate's only path, and a
missing hold program turns the thin region off with one row (ledger "A' only: dilated chain removed").

**Step 2 done, opt-in (2026-09-24).** S4 is built behind `--taa-box-resolution full|half` (`X3M_TAA_BOX_RESOLUTION`, default
full until flown; ledger [temporal-resolve.md](../verification/temporal-resolve.md) "S4 half-resolution box"): the camera
gate's separable box at half resolution on every camera-gate run of an even size; per-pixel containment of the
full-resolution box: 0 violations in 12 fixture scenes; the emitter bound within 2 px. Next: the Run 81 A/B (half against
full), then the default switch. The full-resolution programs and targets stay: an odd frame size, a refused half program
and refused half targets fall back to them. **Default since Run 82:** accepted in Run 81 A launch 2 (run310, -1.06 ms box,
-1.2 ms TAA at 5120x1440), the launcher sends `half` with `X3M_TAA_BOX_RESOLUTION_DEFAULT=1` on every modded `--taa` launch
(`--taa-box-resolution full` is the opt-out, marker 0; the DLL default when unset stays full).

**Ratified 2026-09-24 (orchestrator):** the 2,048-slot per-program ceiling pinned by `RESOLVE_BUDGET`; step 1 = A' in
`far_camera` plus the exact point read at rest in the four S3 programs and the fetch sharing, one re-baseline with new
references and one flight; step 2 = S4; B stays a vote; E and the tests fold not taken. The A' slot figure and every
5120x1440 saving are estimates until the generator runs on the edited program and a `--gpu-sync-timing` flight measures
it. Supersedes the sequence in section 5 of taa-thin-geometry-alternatives.md.

**Recommendation.** Set a per-program static ceiling of 2,048 slots for the TAA programs under the committed rule
(AGENTS.md "Shader slot budget", 0dee63cb: the cap is logged once at device creation, a program the device refuses at
creation turns the owning pass off with one log row, no second program set); implement A' directly in the `far_camera`
resolve (about 40-55 slots, the region and closure holds packed into the fraction of the existing R32F age count, no new
lane), dropping the two dilation draws (-1.33 ms of 8.76 at 5120x1440 **[M]**); fold the two cheap resolve edits into the
same re-baseline (exact point read at rest, fetch sharing); then S4; B stays a vote. E and the tests fold stay out: E is
a wash on fetch issue, the tests fold breaks the box's mask gate. Expected TAA stage at 5120x1440 after the sequence:
8.76 -> about 6.0-6.5 ms turning **[I]**.

What the fixture changed in the premises (all **[M]**, bottle X3, wined3d on the arm64 bottle, adapter reported as
"NVIDIA GeForce 8800 GTX"):

| premise in the ratified plan | measured 2026-09-24 |
| --- | --- |
| a ps_3_0 program must fit 512 slots | the device reports 512; D3DX compiles 16,385-slot ps_3_0; `CreatePixelShader` accepts 262,146; the GPU executes 32,770-slot chains exactly (`executed = expected` on every row up to 32,768 mads); a 65,538-slot chain fails at the draw (`80004005`), not at creation |
| S3 frees 30-50 slots for A' | S3 freed 1 slot on `far_camera` (505 -> 504): a rolled `[loop]` is charged once (the 16 x 10-mad loop lists 18 slots and executes 160) |
| static slots measure cost | run time is executed instructions: 36-43 ns per executed mad per 262,144 px (hlsl chains 1,025 -> 16,385 slots: 0.0576 -> 0.6448 ms per 512x512 draw), so about 1.07 us per executed instruction per full frame at 5120x1440, 0.1 ms per 100; a 505-slot resolve that costs 2.8-3.5 ms is fetch-bound, not slot-bound |
| any program compiles instantly | first draw (backend compile), `first_draw_ms` in the JSON: 10 ms at 513 slots, 21 at 1,025, 55 at 2,049, 141 at 4,097, 475 at 8,193, 1,784 at 16,385 (hlsl chains); 7.2 s at 32,770 (raw). The ledger and AGENTS.md quote 39 ms / 651 ms / 8.6 s for the 513 / 4,097 / 16,385 rows, 3.9-4.8x the JSON's fields; unresolved (section 7), same superlinear growth (about n^1.35-1.4). D3DX itself takes 0.23 s at 513, 3.5 s at 2,049, 14 s at 4,097, 254 s at 16,385 |

## 2. The budget rule (question 1)

**Static ceiling: 2,048 slots per TAA program**, counted by `ps3_program_slots()` on the embedded words and pinned in the
fixture's `RESOLVE_BUDGET` rows (`slots <= 2048` replaces `within_guaranteed_512`; `device_limit` stays as a record).
Why 2,048: the first-draw compile is 55 ms there by the JSON's rows and about 250 ms by the ledger's scale (section 6),
the generator's D3DX compile 3.5 s per program, and no step in this note needs more than about 900. Programs above
1,024 (21-80 ms first draw) take a warm-up draw at `initialize` so the hitch lands in loading, not on the first TAA
frame (section 6); nothing proposed reaches it.

**Run-time cost is budgeted in executed instructions per pixel, not slots**, and proven by timing, not by the count: each
step states its executed delta per pixel (fetches and ALU on the paths most pixels take) and is accepted on the fixture's
timing rows plus one `--gpu-sync-timing` flight. Rolled loops and real branches (`ifc`/`breakc`) stay the tool that keeps
executed work below the static count; the slot-budget fixture's `500 x 64` row (508 slots, 32,000 executed, 0.94 ms per
512x512 draw) shows the reverse trap: a small program can be arbitrarily expensive.

**Capability check and fallback: the committed rule, nothing added.** AGENTS.md "Shader slot budget" (0dee63cb) fixes the
shape: `MaxPixelShader30InstructionSlots` is logged once at device creation; the programs are created regardless of the
reported figure (this bottle reports 512 and runs 32,770); a device that refuses a program at `CreatePixelShader` takes
the existing creation-failure path of `TemporalPass::initialize` / `configure_far` (one log row, the pass or that
configuration off: `far_camera_` dropped means the camera configuration is refused the way `configure_far` refuses it
today). No second program set, no probe draw, no `--taa-slot-cap` option; the S3 16-tap twins remain the
`--taa-history-taps 16` A/B option only, not a cap fallback. Native Windows: modern NVIDIA / AMD and DXVK report 32,768
(the user's stated basis, unverified here); an old part reporting 512 would refuse at creation and lose the TAA
configuration that program serves, logged, which the user accepted (modern cards are the target).

One gap the rule leaves is recorded, not designed: the fixture's 65,538-slot program was accepted at creation and failed
at the draw. A native driver that behaves so for a 550-slot program would hit the resolve draw's `fail(hr)` path every
frame (E_FAIL, history dropped, retried next run) rather than the one-row creation path. At the sizes this note proposes
(under 900 slots, 1/36 of the size that failed here) that is not expected; if a user run ever shows it, a session latch
on the draw failure is a small change to the existing path, not a program set.

Where the rule does not apply: the mask and box programs stay far below 512 (line_mask_camera 427, columns 94 **[M]**);
the fog and sun passes keep their own `ps3_program_slots` gate against the reported cap (their programs fit it; nothing in
this note changes them).

## 3. A' directly in the resolve (question 2)

### 3.1 Mechanism, as it lands in `resolve_far_camera`

*As built (2026-09-24), the mechanism below differs in five places: the closure is a peak hold of the camera gate for one
jitter cycle (the linear 4-frame reopen written here failed the fixture's motion-start bound), the screen gate is not held,
each gate reads the smaller of the pixel's and its nearest-depth neighbour's openness, the hold state takes 16 fraction bits
with the hold length set by the jitter period, and the box programs gate on the tests target inside the region. See
[temporal-resolve.md](../verification/temporal-resolve.md), "A' region hold".*

The tests draw stays and becomes the only mask draw (mode 0: r = screen openness, g = far weight, b = flag + class code,
a = camera openness, plus the S1 depth as COLOR1). The x and y draws go; the resolve reads the tests target at s8 and does
the composition itself with two temporal holds carried in the age target:

- region: `hold_r = flag ? 8 : max(hold_r_prev - 1, 0)`, `fragmented = hold_r > 0` (8 = the jitter period, so a pixel
  fragmented in any phase is in the region for the whole cycle);
- closures: `open_s = min(r, carried_s_prev)`, `carried_s = max(open_s - 1/4, 0)`, and the same for the camera gate `a`
  (a 4-frame hold, replacing the 17x17 spatial minimum);
- composition, as the y draw does it now: `b = fragmented * open_c`, `a = fragmented * open_s`, sentinel
  `b = max(b, S * open_c)` where `carriesClass(b_raw)`, far weights `g * farGate.zw`;
- the blend is unchanged: the resolve already consumes `stabilise.rgba` and the box; only the producer of `stabilise`
  moves inside.

The holds are read at the reprojected texel the age fetch already uses (`tap + rounding`, one R32F fetch) and written by
the same `emit`. Encoding: the fraction of the R32F count. Counts are integers 1..64 with the sign as the exit mark, so
16 fraction bits are exact in FP32; the holds need 4 + 3 + 3 bits (region 0..8, two closures in quarter steps). Encode
`age + (hold_r + 16 * q_s + 128 * q_c) / 1024`, decode with `frc` and two `floor`s; the read-side restart test moves from
`age <= 64` to `age < 65`. No new lane, no new bytes, no format query: the G32R32F second lane of the ratified note would
cost +8 B/px, about 0.18 ms at 5120x1440 **[I]** (59 MB at the bench's 330 MB/ms), and a `CheckDeviceFormat`; it stays
the alternative only if the fraction turns out to interfere with the capture or fixture readers of the age (they read
R32F and would take a `floor`).

Registers: the resolve uses c0-c7, c22, c24, c25 and s0-s12; farGate.zw and S move into a free register (c11); no
sampler is added (s8 is already the mask).

### 3.2 Slot cost, from the listings

Estimated on the S3 `far_camera` listing (1,987 words / 504 slots **[M]**), per item, all **[I]** until the generator
runs:

| item | slots | executed per pixel |
| --- | ---: | --- |
| hold read: `.r` of the existing age fetch, `abs`, `frc`, two `floor`/`mad` decodes | 8-10 | same fetch, +8 ALU |
| region hold: compare, `max`, select | 3-4 | +3 |
| two closure holds: two `min`, two `mad_sat` | 4-6 | +5 |
| composition (was the y draw's tail): three `mul`, one `max`, `carriesClass` (two compares), far weights (one `mul`) | 8-10 | +8 |
| hold encode into the age write: three `mad`, one `add`, a divide by a constant | 5-6 | +5 |
| moving the age read before the clip (the holds gate `stabilise`, which the clip consumes) | 0-3 | 0 |
| removed: the second `lineMask` fetch of the LINE_FILTER path is already shared in this variant | 0 | 0 |
| total | 28-39 | about +30 ALU, 0 fetches |

With the lesson of S3 (the 5-fetch form cost as much as the rolled 16-tap loop; every estimate on this program has run
high), plan for **40-55 slots: `far_camera` about 545-560**, over 512, which is exactly why A' needs the lifted cap and
no longer needs S3's yield. Executed cost: about 30-45 ALU per pixel at 1.07 us each per frame = **0.03-0.05 ms at
5120x1440 [I]**.

### 3.3 Saving

Run 77 C at 5120x1440 (`summary_out.txt`, run290 at fog scale 4, turning windows, medians as the script prints them;
`taa_copy` reads 0.02 ms, so the pair floor in these sessions is at most that and the sub-pass figures are effectively
net) **[M]**: `taa` 8.76 = `taa_copy` 0.02 + `taa_mask` 2.93 (`tests` 1.50, `x` 0.67, `y` 0.66) + `taa_box` 2.21 +
`taa_resolve` 3.48 (2.81-3.46 still, runs 289/290) + 0.12 unattributed; engine 7.33.

A' removes `x` and `y`: **-1.33 ms [M]** (their draws, fixed cost included), adds 0.03-0.05 ms of resolve ALU and no bytes;
memory -29.5 MB at 5120x1440 (the second mask target is no longer needed). Net **about -1.28 ms [I]**, `taa` 8.76 -> about
7.5; at 1080p the ratified note's 0.28-0.67 range collapses to its measured-split value once the same split is flown
there (the x/y draws are 0.245 ms on the bench).

### 3.4 Look risk and the reference it needs

Unchanged from the ratified note section 3.1, restated against the accepted constraints:

- **No lattice crawl at rest**: the region becomes the union of the tested pixels over the 8-frame cycle instead of
  the 11x11 grow of one phase. Section 13 of the lattice note measured the undilated test at 0.47 of the hot pixels per
  phase and 1.000 over any phase; the hold gives the any-phase set at every phase. Pixels more than 3 px from any class
  change in every phase lose the spatial margin; the replay decides whether the residual stays at or below 2.34 rms.
- **Bounded trails (the user tolerates a little ghosting, 2026-09-21)**: the closure now arrives from the history
  (the reprojected texel was the mover last frame, openness 0) instead of an 8-px window, so it holds at any speed; the
  4-frame reopen is the bound the fixture's motion-start case already checks (0.04 codes).
- **Pan flicker (Run 62)**: the stabiliser keeps its weight and its box; its closure is one frame late instead of 8 px
  early, on the silhouette band only, bounded by the 7x7 box.
- **Popping shards**: the 8-px halo of the region goes; the shard's own pixels stay in the region either way.

Reference: (1) `tools/analysis/taa_lattice_gate_replay.py` with the two hold rules in the oracle over the eight bursts of
section 32 (run177 rest at or below 2.34 / 22 / 0; run161 and run148 trail p99 within 0.1 codes; run177 rotation and
run209 forward within 0.02); (2) `run_temporal_pass.py` lattice mode: `thin_region_cases` with the CPU mask oracle
extended by the holds, the 21 thin-region bounds, the `facets` oracle unchanged, and the exact identity "holds forced to 0
equals the camera program on a 1x1 window" (a new `--taa-region-hold 0` path); the age readback rows take a `floor`;
(3) one flight: the run175 stand at rest, a pan, the moving truss, with `--gpu-sync-timing` showing `taa_mask` =
`taa_mask_tests`. New `temporal-pass.txt` and lattice references, `report_sha256` re-committed as provenance.

## 4. The consolidations the cap refused (question 3)

| edit | slots | ms at 5120x1440 | verdict |
| --- | ---: | ---: | --- |
| (a) exact point read at rest in thin / age / far / far_camera (S3 dropped it; `far_camera` was 522 with it **[M]**) | +18-20 each **[M]** | at rest: 4 bilinear fetches and the weight arithmetic skipped per pixel, about -0.1 to -0.15 **[I]**; 0 under motion | take with A' (same programs, same re-baseline); also removes the rest identity's dependence on the filter unit returning centres exactly, which is proven on this backend only (`FILTER_PROBE`) and inferred on native drivers |
| (b) centre-depth reuse (skip the centre tap of the 3x3 dilation, which the age variants re-fetch to save 5-7 slots **[M]**) and fetch sharing (the current-colour centre is fetched once for `color` and again in the clip loop; `lineMask` twice on some variants) | +10-20 **[I]** | 2-3 of 29 fetches per pixel; the resolve is fetch-issue bound (75 % of its cost is not bandwidth), so about -0.1 to -0.3 of 3.48 **[I]** | take with A'; its only refusal was slots and the pinned instruction order, and A' re-baselines the order anyway |
| (c) E: box columns folded into the resolve | +40-50 **[I]** (a 7-iteration loop of two FP16 fetches, the emitter select, the guard); the emitter bound's inner 3x3 costs nothing new: the resolve's clip loop already computes the weighed min/max of the current 3x3 | drops the columns draw (about half of `taa_box` 2.21: 1.0-1.1 **[I]**, 40 B/px against the rows' 28) and the resolve's 16 B box read; adds 14 fetches on every open pixel, most of the sky under the stabiliser: +0.5 to +1.1 on the fetch-bound resolve **[I]** | not taken: a wash at best, and the box's product (the sentinel bound, Run 62) is an accepted behaviour; S4 takes the same money at low risk |
| (d) tests draw folded into the resolve | +180-250 **[I]** (the tests mode of `line_mask_camera` 427: the 28-tap search loop, `gateOpen`, `emissiveVote`, `classCode`; the program is not split per mode, so no per-mode count exists); resolve about 750-850 | drops the tests draw 1.50 **[M]** but re-issues its 28 depth taps, gate and vote inside the resolve; what is actually shared is the motion texel (16 B, 0.36 ms of bandwidth) and the centre depth: net -0.4 to -0.6 **[I]**, less if the larger program's live registers cost occupancy | not now: the box draws run before the resolve and open only where the final mask has `b > a` (rows discard on pixels with no open reader within 3 rows); without a mask before the box, the box runs everywhere (+0.2-0.3 after S4 **[I]**) or gates on last frame's region (a one-frame-late stabiliser edge, the Run 59 class of symptom). It also needs a third render target for the S1 depth history (colour, age, depth; `NumSimultaneousRTs >= 3` plus `MRTINDEPENDENTBITDEPTHS`), the resolve's 3x3 depth loop walking the 16-byte lane, and the mask's c5/c6/c8-c10 renumbered. Revisit only with a fixture timing at 5120x1440 after S4 |
| (e) S4 half-resolution box | 0 (new small programs) | -1.0 to -1.2 of 2.21 **[I]** (writes 16 B per 4 px, 6 taps per pixel instead of 21) | take, second; E is not taken so nothing makes it moot |

(a) and (b) are not savings the cap can lift on their own; they are what the ratified plan called "not replaceable" and
were refused for 7 slots. With A' in the same programs they cost one re-baseline, not three.

## 5. Sequence and expected stage time (question 4)

From run290 turning 8.76 ms / still 8.64 **[M]**; every "after" figure is **[I]**:

| step | what changes | `taa` turning | `taa` still | makes moot |
| --- | --- | ---: | ---: | --- |
| 0 | slot-cap bookkeeping only: the cap log at device creation (committed rule), the fixture's `RESOLVE_BUDGET` rows pinned to `slots <= 2048`; `TemporalPass` has no slot gate to remove (only the fog and sun passes gate on the reported cap) | 8.76 | 8.64 | the "S3 first" ordering |
| 1 (done) | A' in `far_camera` (+ (a) point read at rest, (b) fetch sharing), x and y draws dropped, new references; flown Run 79 A (`taa` 7.72 -> 6.59 ms at rest, measured), off path removed | 7.2-7.4 | 6.9-7.2 | S5 (no dilations left); G |
| 2 (done, opt-in `--taa-box-resolution half`; not flown) | S4 half-resolution box (fixture box stage at 5120x1440: 2.45 -> 1.39 ms wall clock, measured; ledger "S4 half-resolution box") | 6.1-6.4 | 5.8-6.2 | E |
| 3 | B thickness flag as a vote in the tests draw (unchanged from the ratified note: upload statistic in the CloneMesh Unlock observer, `c216.z`, RT2 `.a`) | +0 | +0 | nothing; it is coverage, not cost |
| optional | tests fold (d), only on a 5120x1440 fixture timing showing -0.4 or better net of a box that runs everywhere | 5.6-6.0 | | S1's separate depth write if a third MRT is taken |

Total after 1-2: **-2.3 to -2.7 ms, 8.76 -> 6.0-6.5 ms (about 30 %)**; the engine's 7.3 ms is outside the bracket. Neither
S4 nor B is made moot by anything above; S5 and E are. What remains after step 2 is the tests draw (1.5 ms, full
resolution by the 1-px-strut argument) and the resolve at 3.2-3.5 ms, whose remaining lever is fewer pixels.

Hot path of the game: steps 0-2 add nothing per draw and nothing at initialize beyond one log row. Step 3 adds under
100 ns per routed draw, as the ratified note costed it. Native Windows: the programs are created as today and validated
by the driver against its own cap (32,768 on DXVK, NVIDIA and AMD by the user's basis; unverified); every API is the one
the pass uses today.

## 6. First-draw compile of a larger resolve (question 5)

Measured on the chains **[M]**: the backend compiles at the first draw, not at `CreatePixelShader` (create 0.0-0.9 ms
at every size; first draw 10 ms at 513 slots, 21 at 1,025, 55 at 2,049, 141 at 4,097 by the JSON's `first_draw_ms`;
39 ms at 513 and 651 ms at 4,097 by the ledger's figures for the same rows; growth about n^1.35-1.4 either way). A'
takes `far_camera` to about 550 slots: 11-42 ms **[I]**; the tests fold would take it to about 800: 17-70 ms; the 2,048
ceiling is 55-250 ms, which is why programs above 1,024 take the warm-up draw below.

For the game: `TemporalPass` creates its programs at `initialize` and draws each one the first time its configuration
is selected, so the hitch lands on the first TAA frame of the session (10-39 ms today, 11-42 ms after A'),
once per program per device. Shader objects survive `Reset` (they are not default-pool), so alt-tab does not repeat it
on wined3d **[I]** (wined3d keys its compiled program on the shader object and the state it was drawn with; a state
variant such as a different sampler format compiles again). At the 2,048 ceiling a 55 ms hitch on the first frame is
visible; above 1,024 slots a warm-up draw at `initialize` (one 1x1 quad per program into the pass's own targets,
inside the loading interval the DLL already measures) moves it off the first frame. Not needed for A'.

For the fixture and the generator: the D3DX compile is the larger number (232 ms at 513, 3.5 s at 2,049, 14 s at
4,097 **[M]** on straight chains; real code with rolled loops compiles faster, 451 ms for the 508-slot 64-iteration
loop). `generate_rigid_motion_pixel.py` compiles each variant once per regeneration (five programs plus the 16-tap
twins, under a minute at 550 slots). `run_temporal_pass.py` compiles the source variants (`plain`) beside the embedded
ones for its `RESOLVE_BUDGET` rows and draws every configuration: at 550 slots its first-draw cost stays in the tens of
milliseconds per configuration, invisible in a 500 s run; at 2,048 the D3DX compile alone would add 3.5 s per source
variant and the first draws about 0.5 s over the run. The fixture keeps the rows `instruction_slots` and `dwords`,
reports `device_limit` for the record and pins `slots <= 2048` instead of `within_guaranteed_512`. The 32,768 figure is
not a fixture target: the slot-budget fixture already records the executes-to-32,770 bound and the 65,538 draw failure.

## 7. Unknown, and what settles it

- **A' slots on the real listing** (40-55 assumed): the generator on the edited `resolve_far_camera.hlsl`; the number
  only matters for the first-draw estimate now, not for feasibility.
- **Whether the held region covers the hot pixels at rest as the 11x11 did**: the section 32 replay with the hold
  rules (host only, before any shader edit).
- **A native driver reporting 512**: whether it refuses a 550-slot program at `CreatePixelShader` (the committed rule's
  path: one row, configuration off), at the draw (the per-frame `fail(hr)` retry, section 2) or not at all; only a user
  run on such a part answers it, and modern parts report 32,768 (tracked in platform-portability.md).
- **The first-draw figures**: the JSON's `first_draw_ms` rows (10 / 141 / 1,784 ms at 513 / 4,097 / 16,385) and the
  ledger's "39 ms / 651 ms / 8.6 s" for the same rows differ by 3.9-4.8x; the fixture's `--reparse` of the kept log, or
  a second run, says which is the record. The ceiling and the warm-up threshold above are stated for both.
- **The tests fold's occupancy cost**: a fixture timing at 5120x1440 of an 800-slot resolve against the split draws,
  after S4; until then (d) is not planned.
- **The age fraction and its readers**: the capture path and the fixture read the age target as R32F counts; whether any
  reader compares it exactly (the exit-reset rows) is a grep before the encoding is chosen; the G32R32F lane is the
  fallback encoding.
- **Native `MaxPixelShader30InstructionSlots` values**: 32,768 for DXVK, NVIDIA and AMD is the user's stated basis, not
  measured here.

## 8. Considered and why they lose

| option | loses because |
| --- | --- |
| keep the 512 rule and finish the ratified order (S3 yield, then A') | S3 yielded 1 slot; A' at 545-560 never fits; the plan stalls on a number the device does not enforce |
| gate the TAA programs on the reported cap (the fog / sun passes' `compiled_slots` refusal) | on this bottle the cap reads 512 and the lifted programs would never run on the only tested target; the committed rule says do not refuse a shader for exceeding 512 |
| a known-answer probe draw at initialize, or a 512-class second program set with a `--taa-slot-cap` switch | rejected by the committed rule (0dee63cb): no second program set, creation failure is the fallback; a probe adds a draw and a readback for a case the user does not target (old parts) and cannot distinguish a draw-time refusal from a creation refusal better than the resolve draw's own `fail(hr)` |
| ceiling 512 -> 32,768 with no intermediate rule | first draw 7.2 s at 32,770 **[M]**; D3DX 254 s at 16,385; nothing in the plan needs more than 900 |
| A' with a G32R32F age lane | +8 B/px, about 0.18 ms at 5120x1440 and a format query, for three fields that fit the count's fraction exactly |
| E (columns into the resolve), or the lattice review's in-resolve box clip (48 fetches on region pixels, refused at 496 slots) | 12-14 more fetches on every open pixel of a fetch-bound program; the columns draw costs about the same; S4 saves more at lower risk |
| tests fold into the resolve now | the box's mask gate needs the mask before the box; a third MRT; the 16-byte lane in the 3x3 loop; the real shared work is one motion texel; occupancy of an 800-slot program unmeasured |
| S5 half-resolution dilations | there are no dilation draws after A' |
| G (mask every other frame) | still the one-frame-stale gate under pans that the Run 59 fix exists for; A' removes the same two draws every frame |
| B as the sole classifier | unchanged from the ratified note: no flag for unrouted far stations or unflagged buffers |
| a warm-up draw at initialize now | 11 ms once per session is below what a loading frame already costs; keep it for programs near the ceiling |
