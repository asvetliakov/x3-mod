# Farnham's Legacy: sun light poll (step 7)

`src/engine/fl/sun_light_poll_fl.cpp` implements `src/proxy/sun_light_poll.h` for X3FL.exe; the fl target links it
instead of the AP `sun_light_poll.cpp`. Hook-free and read-only, no patch. Contract and anchors:
[sun-light.md, "2026-11-01: poll contract"](../reverse-engineering/fl/sun-light.md); constants from the generated
`src/engine/fl/sun_light_poll_fl_inc.h`.

**What is read.** Once, in `load_backend` after the camera provider: the AP latch (`X3M_MOTION_OUTPUT=1`,
`X3M_SHADOW_REPLAY_DEPTH=1`, a cascade list, `X3M_SHADOW_SUN_POLL` not 0), `fl::verified()`, then all 50 anchors
at `module_base()` + RVA through `ReadProcessMemory`. Each anchor is a whole instruction (or run) compared byte for
byte, with its relocated disp32 required to equal base + target RVA. Then the slot `base+0x1aa100` must be readable.
Per poll, on the render thread at the frame's first routed z-writing draw (`MotionOutput::poll_point_sun`), all
through `engine_memory::read`: R = the slot (4 B); R+0x5e8c, 255 node words plus the slot count at R+0x6288 (1,024 B);
per array entry node+0xb0..+0x173 (0xc4 B: position, flags +0x12c, colour int16 +0x154/156/158, range +0x15c,
record +0x170); the chosen node's record+0x34 (12 B, diagnostics). Nothing is kept across polls but counters.

**Differences from AP.** RVA + base instead of absolute VAs. Colour, range and record each sit 4 bytes later, so a
node read is 0xc4 bytes, not 0xc0. The score is the engine's `trunc((0.587 G + 0.299 R) + 0.114 B)` in SSE2 double
precision, in the engine's order, plus 0x300 when directional. The AP reader rounds (`(w + 500) / 1000`), which is
left unchanged. The identity gate is the 50 FL anchors, not the AP exact-executable check. Refusals map onto the AP
`Status` names: an anchor mismatch is `executable_mismatch` with `reason=anchor anchor_rva=…` in the row. Selection,
tie rule (farther node), validation (slot count == 8, terminator within 255, light bit on every entry) and both
winners are as on AP. Flags: `-fno-exceptions`, SSE2 kept, because the poll runs in the proxy's draw path and not
inside engine code.

**Expected log rows.** At load: `fl.sun_light status=ok reason=none base=0x… slot=base+0x1aa100 context=0x00000000
anchors=50 anchor_rva=0x00000000 log_s=10`. The context is null before the engine builds its renderer. Without
cascades: `status=disabled reason=setting`. Every 10 s (`fl_sun_light_log`), at a poll:
`fl.sun_light_sample status=ok reads=… ok=… unreadable=0 null_context=… layout=0 no_directional=… context=0x…
candidates=… slot_admitted=… admitted=… engine_rule=1 rules_agree=1 score=0x3xx second=… flags=0x008000x4
pos=… colour=… admission_pos=… record=1 record_pos=…`. `reads` and the status counts cover the period; the rest
describe the row's own poll. Downstream: `shadow_replay_sun_source … source=point reason=point poll=ok`,
`shadow_sun_frame … source=point`, and `volumetric_fog_frame … sun=tracked` (the fog's sun radiance from the polled
colour) with no `volumetric_fog_sun … source=fallback` row; that row appears once only when the fog falls back.

**What a flight must show.** In a sector with a sun: `ok` ≈ `reads`, `layout=0`, `unreadable=0`, `engine_rule=1`,
`score` ≥ 0x300, `flags` with 0x800000, one stable `pos` per sector (it changes with the sector or a gate jump),
`record=1`. `shadow_replay_sun_source` must settle on `point`, not `disagrees` or `unavailable`, with
`agreement_deg` small in `shadow_sun_frame` (perf tier). `shadow_replay_sun_point … record_scale` (perf tier: the
chosen node's record x over its integer x) must read about 0.01. The shared `point_sun_context_scale` (0.01, from
AP) converts FL positions too, and sun-light.md warns that scale is unverified for FL; another value means the
point sun is mis-scaled on FL and the poll must not drive shadows until the scale is owned. Shadows must keep the
run 10 direction. `layout` > 0
means the slot count is not 8 on this adapter (renderer caps +0x4f4) or a non-light entry appeared; `no_directional`
in flight means the sun lacks 0x800000 and the range rule. Neither should occur; either is a finding for
sun-light.md, not a reason to relax the gate. A sector without a sun may show `no_directional` or the admission
winner (`engine_rule=0`). Menus and loading screens may count `null_context`.
