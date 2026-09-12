# Independent architecture assessment, 2026-09-12

Scope: does the current d3d9-proxy architecture survive the rest of the roadmap
(HDR scene path, bloom/exposure, GTAO/SSR, shadows, clustered lighting,
volumetrics, TAA), or should we hook different parts of the game? Read-only
review of `docs/architecture/*`, `docs/reverse-engineering/*`, `docs/status.md`,
`src/proxy/{capture,motion_output,loading_trace}.cpp`,
`src/renderer/{material_motion,temporal_pass,scene_boundary.h}`.

## Verdict: keep, with four additions. No rewrite.

The proxy is the only layer that can do the thing every remaining feature needs
— **own the render targets and the composition order** — and it already does.
Nothing in options B–E replaces that. The per-draw shader substitution is the
right mechanism for *adding outputs* (motion, depth, later normals) and the
wrong mechanism for *replacing shading* (stage 6); that transition is a later,
localized change, not a rewrite.

What is actually wrong today is narrower than "the architecture":

1. **Pass boundaries are inferred when they are known.** `0x00471f50` is a fully
   mapped frame routine: `BeginScene 0x004720c2`, traversal loop
   `0x004720e1–0x0047215e`, bloom `0x004c4750` called at `0x004721b1`,
   `EndScene 0x00472574`. `scene_boundary.h` reconstructs that from Clear/draw/
   StretchRect patterns and four hard-coded bloom shader hashes, and fails
   closed on anything unseen. We are inferring a fact we can read.
2. **The per-draw cost model is inverted.** The game itself issues ≤5
   `SetVertexShaderConstantF` + ≤1 `…ConstantI` per material draw (all from the
   two state managers, 12 callsites total, `constant-uploads.md`). Our route
   adds roughly 25 device calls on top of that per routed draw — ~4× the
   application's own state traffic, ~256 routed draws/frame (17,390/68 in
   iteration 6). Most of it is avoidable.
3. **The engine-level hooks are already proven live and under-used.**
   `object_trace` at `0x004c5228` and `object_lifetime` at
   `0x004efbf0/0x004efd39/0x004efe10/0x0040508d` carried 17,390 routed draws at
   99.5% matched history across sector changes and a ship kill. (The RE docs
   still say "never run against the live game"; iteration 6–8 supersedes that —
   gate 5 cannot pass without both.) That de-risks option C almost entirely, and
   we are still not reading the camera, the light nodes or the pass boundaries
   from the engine even though the addresses are documented.
4. **TAA's insertion point is a game video option.** The resolve runs only when
   the selector reaches `AwaitCopy`, i.e. only if the game performs its bloom
   copy. Bloom is one call at `0x004721b1` gated on a render option; the
   selector explicitly "rejects … missing bloom". With glow disabled, TAA
   silently does nothing.

The four additions, in order: **(i)** engine-level pass boundaries and camera
state; **(ii)** collapse the per-draw route cost; **(iii)** render-target
redirection so the scene renders into our own FP16 target; **(iv)** move shader
substitution into the effect blob at `D3DXCreateEffect` when we start replacing
shading, not before.

## Alternatives

| | Buys for the roadmap | Costs | Wine/CrossOver risk | Native Windows risk | Replace or add |
|---|---|---|---|---|---|
| **A. Status quo** (proxy + per-draw substitution + `0x004c5228` + state-manager facts) | Everything delivered so far: motion, current depth, jitter, TAA resolve, 169/180 SM3 pairs, 99.97% key match | ~25 device calls/routed draw; boundaries inferred; no normals, no light list, no pass insertion point | MRT RGBA32F+R32F+A8R8G8B8 already self-tested; `SetRenderTarget` churn is the suspect in 16.1→38.5 ms | Untested end to end; `CheckDeviceFormatConversion` gate exists but unverified | — |
| **B. D3DX effect layer** (rewrite the `.fb` blob at `D3DXCreateEffect` `0x004bafca`; and/or hook `BeginPass` `0x004c3ff6`/`SetMatrix` `0x004c21ff`) | Scene draws emit motion/normals/depth *natively* → per-draw route cost →≈0; the only sane vehicle for replacing lighting/material shaders (stage 6) and for replacing `bloom.fb` | Must re-emit a valid D3DX FX container; per-device capability gating is lost (a device without 3 MRTs gets MRT shaders anyway — harmless but unconditional); loses the "fail closed per draw" property | Low: `D3DXCreateEffect` is already IAT-hooked (`0x00532324`); ~18 effect creations per *session* (9 menu/0.113 s, 4 save/0.055 s), so no load-time cost and no cache needed | Same DLL, same code path; low | **Addition**, later. Replaces only the substitution mechanism, not the proxy |
| **C. Engine hooks** (frame routine `0x00471f50`; world build `0x004bdee0` + globals `*0x00608a38/40/44/48`; traversal `0x0047d9c0`/`0x0047e6e0`) | Exact pass boundaries; exact view/projection instead of factorizing constants; directional-light nodes (already at `ESP+0x14/0x18` of `0x004c0150`); a place to insert shadow/prepass; a place to jitter once per submission | Fixed VAs, relocations stripped → one build only; must be SHA-gated like the existing hooks; scratch globals are per-submission, not per-frame | Identical on both: these are game-code hooks, not backend hooks. Already proven live at two seams | Same | **Addition**. Highest value per unit of risk |
| **D. Modern API / translation** (DXVK d3d9 under our proxy; D3D11/Metal) | The *only* route to real HDR presentation. `hdr-transfer.md` shows the D3D9/DXGI shared-HANDLE route does not share pixels on this install; the alternative is a source-built WineD3D export (no matching Preview source) | Whole-backend risk: RESZ, MRT, state-block and reset semantics all revalidated; a bundled backend becomes a runtime dependency | DXVK i386 d3d9 is already present in the bottle (`lib/dxvk/i386-windows/d3d9.dll`) but unproven with X3 | Shipping DXVK on Windows is a support burden and must stay optional with an SDR fallback on system d3d9 | **Addition at the presentation boundary only**, and not now |
| **E. Offline transformation of all 817 pairs, shipped as a mod** | Same per-draw saving as B | Couples us to the user's installed archives and to other mods; slow iteration; no fallback when a program is unknown; `.fb` override precedence is undocumented | Same both platforms | Same | **Dominated by B.** Do not pursue |

Notes where the table is too terse:

- **B is mechanically plausible and cheap.** The container format is already
  parsed by `tools/analysis/effect_passes.py` (magic `0xfeff0901`, state ops 146
  /147, trailing resource records with explicit sizes), archives decode with
  XOR `0x33`, and there is exactly one creation callsite (`0x004bae10` →
  `0x004bafca`, null macros/include/pool). Growing a shader resource means
  re-emitting the trailing resource list only. It is still unproven that
  `d3dx9_37` accepts a re-emitted container — that is a one-afternoon probe, not
  a design risk. The reason to defer B is that it buys per-draw cost we can get
  more cheaply first (see below), and its real payoff is stage 6.
- **D changed less than hoped but not nothing.** "Not first" still holds for the
  renderer. But roadmap stage 5 (HDR presentation) has no path on this machine
  through installed D3D9 interfaces at all, and the intermediate the user asked
  about — keep our proxy, forward to DXVK's d3d9 — is the cheapest form of a
  coherent backend, because DXVK exposes Vulkan interop on its D3D9 objects
  (`ID3D9VkInteropDevice`/`…Texture` in DXVK's own headers) that would let a
  small Vulkan/MoltenVK compositor consume the FP16 scene image on both targets
  with one code path. I am asserting that interop from knowledge of DXVK, not
  from evidence in this repo: treat it as a probe to run (does X3 render
  correctly on DXVK i386 under Preview; does the interop interface exist in the
  bundled build; does RESZ still work), not as a decision.
- **E's only advantage over B is zero runtime work**, and B's runtime work is 18
  transforms per session. E also violates the spirit of "reversible app-local
  installs" less cleanly than a fake-patch CAT suggests, because mods and the
  addon catalogues override in an order we have not established.

## Specific questions

**Is the scene-boundary selector fragile?** Yes, in a specific and fixable way.
It is correct, conservative and now structural (68/68 iteration-6 frames after
the background allowlist was removed — the previous hash-based version lost 32
of 68). But it still hard-codes the four bloom pairs, requires the full
bloom sequence to reach `Selected`, and requires the bloom copy to reach
`AwaitCopy`, which is where TAA runs. It will reject: glow disabled, a shader
mod, the conditional second scene at `0x00472201` (second `BeginScene` at
`0x00472236`), and probably station/map/cutscene frame shapes.

**Is callsite-based pass identification strictly better?** No — it answers a
different question. The engine tells us *when* and *why*; only D3D tells us
*which surfaces*. The right design is a hybrid, and the engine side is cheap:
`0x00471f50`'s internal callsites give scene-begin, scene-end and
bloom-begin exactly, and `0x004c5228` (already hooked) already says "this draw
is a material submission". Keep the selector as a *validator* of surface
identity and as the fallback when the engine hooks refuse to install; stop
letting it be the sole gate for whether TAA runs. One caveat: the per-view clear
`0x004bb280` is reached through the generic dispatcher `0x004e3e70`, so its
frame position is not statically resolvable — the initial latching Clear still
has to be recognized at the D3D level.

**Is the history key robust for future passes?** For what it does, yes:
0 duplicates in 8,957 keyable scene draws, 99.97% matched across 18 adjacent
pairs, and the weaker K2 key produces 108 ambiguous sub-mesh groups. Two
forward problems:
- **No pass discriminator.** A shadow pass or a depth prepass draws the same
  node/buffers/range a second time in one frame; today the second occurrence
  consumes the previous entry and the first gets nothing. Add a pass/callsite
  field to `RigidDrawKey` before any second pass exists, not after.
- **No camera-cut signal.** Camera serial and load/registry epochs were constant
  within every burst (29921, epochs 1/2), including a third-person camera change
  that kept the same camera pointer. The displacement cut detector is doing the
  real work; that is fine, but the key must not be mistaken for a cut source.
- Dynamic/rewritten buffers (stardust) satisfy the key while their contents
  change; they are excluded today and must stay excluded.

**Per-draw cost model.** Scene time 16.1 ms (route only, iteration 6) → 38.5 ms
(route + TAA, iteration 7), uncontrolled and unattributed. The route's own
per-routed-draw budget is roughly: 6 `GetRenderState` + 1 `GetStreamSourceFreq`
(≈7), 2 jitter constant writes, 2 `SetRenderTarget` + 2 `SetRenderState` and
their 4 restores (≈8), 2 shader sets + 2 restores, 2 constant sets + up to 2
restores. Ordered by expected saving:
1. **Bind the MRTs per scene *phase*, not per draw** (lazy mode is the first
   step; the endpoint is bind once at phase entry and toggle only
   `COLORWRITEENABLE1/2`). `SetRenderTarget` is the expensive call on both
   backends (command-stream packet + attachment rebind), ~1,024 of them per
   frame today.
2. **Shadow the render states.** `SetRenderState` is *not* hooked at all today;
   the route reads ZENABLE/ZWRITEENABLE/ALPHABLENDENABLE/ALPHATESTENABLE/
   SRGBWRITEENABLE/COLORWRITEENABLE back per draw. Each Get crosses into the
   backend and takes its global lock. Hooking `SetRenderState` also closes the
   documented lazy-mode hole ("an application write or read of
   COLORWRITEENABLE1/2 between two routed draws … is not intercepted").
3. **Jitter in the engine, not per draw.** The projection the WVP is built from
   is `*0x00608a38`, written per submission by `0x004bdee0` and consumed at
   `0x004c21c0–0x004c2303`. Jittering there removes two 4-row constant uploads
   from every scene draw. The ABI already reserves `c216.zw` for the previous
   jitter, which is exactly what this change requires.
4. **Effect-level substitution (B)** removes the last 4 shader calls.

Also worth measuring separately: the proxy's global recursive mutex is held
across backend calls in the draw/Clear/target/Present hooks (1.495 s of
`lock_wait` over 7.19 M acquisitions in one loading run).

**Wine/CrossOver-specific risks.**
- MRT with mixed bit depths is self-tested, but **MRT + alpha blending** needs
  `D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING`; the phase-level MRT binding above
  makes this matter for the blended scene draws. Gate it explicitly.
- The 8-bit↔FP16 `StretchRect` pair around the resolve is accepted by Wine
  unconditionally; `CheckDeviceFormatConversion` is the only native guard and no
  native run exists. Once the scene renders into our own FP16 target, both
  copies disappear — another reason to do RT redirection early.
- R32F depth readback and RESZ: RESZ is a driver extension, not a contract. For
  GTAO/SSR, RT2 (92.7% of scene draws; depth-only and blended draws missing)
  leaves holes. Prefer a real depth source for depth-consuming effects and keep
  RT2 for the reactive mask.
- Memory at 5120×1440: 118 MB motion (RGBA32F) + 29 MB depth + ~180 MB FP16
  history/scratch. RGBA32F for motion is 4× larger than it needs to be; a
  compact encoding is worth doing before adding a normals target, since D3D9
  gives us only 4 MRTs and stage 4 will want RT0 to be FP16.

**Loading: proxy or engine patches?** Both, and the proxy is running out of
reach. Measured: menu load 30.1 s, save load 106.7 s, of which ~70% crosses no
hooked boundary at all. What the proxy *can* own is real and already built:
`GenerateAdjacency` 14.49 s per run (11.33 s of the save load alone), texture
decode 8.80 s over 988 MB of byte-identical input, `CreateFileA` 6.86 s. The
adjacency cache measures 1.163 ms/hit against 97.3 ms native on its fixture and
would also defeat the engine's body-cache flush (a menu *return* re-runs the
identical 1,017-call / 298 MB vector in the same process). It has never been
enabled in a real run — `mesh_cache requested=0` in both iteration-8 sessions.
Enable and measure it before designing anything else here. What the proxy cannot
reach — the 1 KiB inflate chunk at `0x004e8d55`, `gzread` at 3.11 bytes/call,
the O(n²) type rescan at `0x004381c2`, the resolver with no cache at
`0x004e7590`, the script VM `0x004ab880` — is where the other ~70% is, and those
are EXE patches. Note the `gzread` hook itself costs ≥1.91 s of the measured
save load; instrumentation is now first-order there.

## Recommended sequence

| Stage | Hooks it needs | Does the current architecture block it? |
|---|---|---|
| **Finish TAA** (sentinel pixels still crawl) | `clip_to_previous` from view+projection. `object_trace::Snapshot` **already reads** `*0x00608a40` (view) and `*0x00608a38` (projection) per draw — no new hook, only live validation. Then RT1 alpha 0 for unrouted pixels and `sentinel_camera` policy 2 | No. This is the cheapest remaining visual win |
| **Route cost + robustness** | `SetRenderState` shadow; phase-level MRT binding; engine pass boundary at `0x00471f50` replacing the bloom-copy dependency for the resolve point | No, but the bloom-option dependency is a live correctness bug |
| **4 — HDR scene** | Redirect `SetRenderTarget(0, main)` during the scene phase to an owned A16B16G16R16F target; tone-map + composite at scene end; replace bloom by skipping `0x004721b1` and running our own | No — this is exactly what a proxy is for. Prerequisite: establish whether the main scene target is the backbuffer or a texture (one log line; it decides whether redirection or format substitution is used) |
| **5 — HDR presentation** | Backend decision (option D). Nothing else unblocks it on macOS | The proxy does not block it; the *backend* does |
| **6 — materials / clustered lighting** | Point lights are `g_LightPoint` VS c0–c23 (8 max) with the count in `i0`, uploaded as one 24-register write inside `BeginPass`; accumulate distinct entries across a frame from the existing constant shadow → a global light list with **no new hook**. Directional lights arrive as two render-node pointers at `ESP+0x14/0x18` of `0x004c0150`, which the existing `0x004c5228` hook already forwards | **Yes, here.** Replacing the lighting model by bytecode splicing is the wrong tool; this is where effect-level replacement (B) becomes the mechanism |
| **7 — GTAO/SSR/shadows** | Normals: either a 4th MRT via the same transformer, or derived from depth first. Depth: prefer a real depth copy over RT2's 92.7%. Shadows: record `(VB, IB, decl, stride, range, rows)` during the scene phase and **re-issue the recorded draws in the same frame** into a shadow map with a light matrix — the buffers are still bound and alive, so none of the shelved lease/admission machinery is needed. World matrices from `0x004bdee0`'s globals or `WVP·inv(VP)` | No, provided the history key gains a pass field first and the camera matrices are validated live |
| **8 — volumetrics/SSR quality** | Sun/fog parameters from the same engine camera/light state | No |

## Add these hooks now

Ordered by value ÷ risk. Each is an addition to `capture.cpp`/`motion_output.cpp`
or one new engine seam; none changes the existing design.

1. **`SetRenderState`/`GetRenderState` shadow** (proxy, vtable slots alongside
   the existing `X3M_SHADOW_HOOK` table). Evidence: `evaluate_draw` issues 6
   `GetRenderState` per candidate draw; `telemetry.md` documents the lazy-mode
   `COLORWRITEENABLE1/2` hole. Risk: low — state blocks and `Apply` already
   resync the shadow; the same path covers render states.
2. **Per-draw caller return address, bucketed.** Zero cost (already on the
   stack in the draw hook), and it gives an independent per-draw pass label to
   correlate against the selector — the game draws through its own render-object
   vtable slot `+0x148` at `0x004c403c`, inside `BeginPass`/`EndPass`. Use it to
   (a) validate the selector on the next capture, (b) supply the missing pass
   field of `RigidDrawKey`. Risk: low; a return address is not a stable ABI, so
   treat it as a bucket id, never as a semantic gate on its own.
3. **Frame-routine pass hook at `0x00471f50`** — scene begin after
   `0x004720c2`, scene end before the bloom call at `0x004721b1`, and the
   conditional second scene at `0x00472201`. Evidence: `ghidra-render-map.md`
   plus the callsite inventory in the RE notes; the same E8-callsite technique
   already proven at `0x004c5228`. Buys: a resolve point that does not depend on
   the player's glow setting, and the insertion point for every later pass.
   Risk: medium — one more fixed VA in a relocation-stripped image; must be
   SHA-gated and must degrade to the existing selector when it refuses.
4. **Live capture of the camera globals** `*0x00608a38/40/44/48`.
   `object_trace` already reads all four; no gameplay capture has ever recorded
   them. Buys: exact `clip_to_previous` instead of the ill-conditioned
   factorization (projection `P[2][2]` within 3e-6 of 1, far plane recoverable
   only to 2.00–2.06 M, per-draw cancellation up to 0.0483), and the three
   coordinate regimes made explicit. Risk: none beyond logging; the values are
   per-submission scratch, so they must be sampled inside the hook, not polled.
5. **Jitter at the projection**, once per submission, replacing the two per-draw
   constant writes. Depends on 4. Risk: medium — the jittered projection must
   not reach HUD/GUI or the bloom quads; verify the regime split first and keep
   `X3M_MOTION_JITTER` as the A/B switch.

Two more, lower priority but cheap: **enable and measure the mesh adjacency
cache** (built, 741 + 12,905 checks, never once active in a real run, bounded
upside 14.49 s/run), and **add `FindFirstFileA`/`FindNextFileA`/`FindClose` to
the IAT set** (`0x005321c0`/`0x005321bc`/`0x005321b8`) to size the uncached
resolver at `0x004e7590` before deciding whether loading work moves into the EXE.

## Where I think the current code is on a wrong path

- **`scene_boundary.h` as the gate for whether TAA runs at all.** It is a good
  validator and a bad switch. Bloom is a game option.
- **Per-draw `SetRenderTarget`.** Binding a target per draw to write a
  per-pixel attribute is backwards on any backend; the write masks are the
  per-draw knob, the targets are a per-phase knob.
- **Reading render state back per draw** while shadowing everything else.
- **RGBA32F motion.** It is the largest allocation we make and it will compete
  with the FP16 scene target and a normals target for the 4 MRT slots and for
  bandwidth at 5120×1440.
- **Treating the shelved replay/lease machinery as the only way to get a second
  pass.** Same-frame re-issue of recorded draw parameters needs none of it.

No claim here is a gameplay measurement; the timing attribution above is a
budget argument, and the telemetry work now in flight is what should settle it.
