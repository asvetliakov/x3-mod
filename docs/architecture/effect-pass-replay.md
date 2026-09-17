# Effect pass replay: decision note

2026-09-16. Question: with the D3DX pass application now measured per draw
(`--pass-phases`, run95), should the proxy cut it, and how? Inputs:
[sampling-profiler.md](../verification/sampling-profiler.md) (run95, run91,
run92 sections); [engine-state-filter.md](engine-state-filter.md);
[effect-pass-loop.md](../reverse-engineering/effect-pass-loop.md);
[constant-uploads.md](../reverse-engineering/constant-uploads.md) §2;
[frame-loop-phases.md](../reverse-engineering/frame-loop-phases.md) §2. The
EXE import table and the bytes at two candidate stamp sites were re-read from
the bottle's `X3AP.exe` this session; nothing ran under Wine.

## Decision and order

1. **Run C first (no proxy code): the bottle's DXVK D3D9 backend, and the
   `d3dx9_37` builtin/native swap.** The draw call is the largest share
   (8.58 ms, 42.7 %); ~7.5 of its 8.75 µs per pass is Wine's D3D9 frontend,
   which the proxy cannot make cheaper. One session each; read `pass_phases
   draw_p50_us` and `apply_p50_us` at matched `passes_p50`.
2. **Land two host-side prerequisites for A while C runs**: the compiled-effect
   state classification (extend `tools/analysis/effect_passes.py`) and the
   two-stamp residual split (§ "The 4.5 ms residual"). Both are cheap and each
   decides a number A's estimate currently guesses.
3. **Build A (full pass replay with parameter ownership) only after 1 and 2**
   and only if the frame still needs it: 3–5 ms of 26.5 ms (11–19 %) at
   moderate confidence, plus 1–2.5 ms of the residual at low confidence, for
   the largest hot-path component the proxy would own. B collapses into A or
   into nothing (below); D is the fallback.

**Bottle experiments, status (run103, 2026-09-17).** Step 1's D3DX swap ran as
run 35 session A: the installed run34 build with `--d3dx builtin`
(`/tmp/x3-bottleX3-run103`). Visuals were unchanged, but the proxy's
`loaded_module` line could not distinguish builtin from native: under Wine a
builtin module keeps the native file's `FullDllName`, so the path and the
on-disk size on that line are the native file's even when the builtin is
mapped (probe: native `image_size=3895296 stamp=47cdef5d`, builtin
`image_size=585728 stamp=00000000`, plus the `Wine builtin DLL` marker in the
first 0x80 bytes of the module base). The line now carries `image_size=`,
`stamp=`, `exports=` and `wine_builtin=`. Over 14 busy windows matched to run95
by pass count (±15 %), per-pass medians were BeginPass 8.90 µs (run95 6.64),
draw 9.21 (8.74) and engine between passes 5.42 (4.89), with `dt` p50 27.4 ms
against 22.9 ms; the empty view gave ≈8.7 µs vs ≈6.9–7.0 µs. Because run95 is
the run33 build and the module identity was unproven, the comparison decides
nothing and the experiment repeats as run 36 A1/A2 on one build with the new
identity fields (numbers in
[sampling-profiler.md](../verification/sampling-profiler.md), "Run 35 session A").

## Measured inputs

Run95 busy window (unhooked proxy, 981 passes per frame, `view_submit_p50`
20,115 µs of `dt_p50` 26.9 ms): BeginPass 6,537 µs (6.66 µs/pass), draw
8,581 µs (8.75 µs/pass), EndPass 111 µs, engine remainder 4,520 µs (4.6
µs/draw). Run91 (hooked): 63 state calls per draw (32 SetSamplerState, 19
SetRenderState, 4.5 SetTexture, 1 each SetVertexShader/SetPixelShader/
SetVertexShaderConstantF); 94.9 % of shadowed render-state and 99.3 % of
sampler-state writes are no-ops; 6.0 % of consecutive draws share a material.
Per-call device costs (X3 bottle benchmark, state-call-fast-path.md): native
SetRenderState 14.7 ns, SetSamplerState 11.0, hooked SetTexture 122.7,
SetVertexShaderConstantF 75.2, the draw pair ~1,020.

**Where the 6.66 µs of BeginPass goes** (arithmetic, not a measurement): the
63 device calls cost 32 × 10.8 + 19 × 10.9 + 4.5 × 122.7 + 75 + 2 × ~80 +
~50 ≈ 1.4 µs; the 63 six-instruction manager thunks ≈ 0.3–0.6 µs; the
remaining **≈ 4.6–5.0 µs (4.5–4.9 ms per frame) is D3DX's own walk** (state
iteration, dirty checks, constant packing, sampler resolution). Only the walk
is removable; the calls carrying per-draw data (shaders, textures, constants,
the ~1.3 states that actually change) stay.

## Option A: pass replay behind a wrapper `ID3DXEffect`

**Entry point (verified this session).** The EXE imports exactly one effect
factory, `d3dx9_37.dll!D3DXCreateEffect`, IAT slot `0x00532324` (no `...Ex`,
no `...FromFile*`); 22 effects per load (iteration04-loading.md).
`src/proxy/loading_trace.cpp` already patches this slot for the loading
trace, so the mechanism, its byte verification (slot content equals the
resolved export) and rollback exist. The wrapper is a documented COM
interface (`d3dx9effect.h`): it forwards every method, wraps `CloneEffect`
results, and intercepts `SetStateManager` (single callsite `0x004bb07e`) so
the game's manager `0x00562a8c` stays the recording target.

**Recording.** On the first `BeginPass(i)` of each (effect, technique, pass)
D3DX applies through a recording manager that logs every manager call (all 21
slots) in order and forwards it to the game's thunk. The record is what the
loaded D3DX actually emits, independent of which `d3dx9_37` serves the game.

**The parameter problem, stated honestly.** Of the recorded calls, render
and sampler states are literals of the pass or of its `sampler_state` blocks
(replayable against the shadow as recorded), but `SetTexture` values come from
texture parameters through sampler parameters, the constant uploads
(`SetVertexShaderConstantF`, `...I`, `SetPixelShaderConstantF`) are packed
from ~6 dirty parameters (`g_mWorldViewProjection`, `g_mWorld`, `g_mWorldIT`,
`g_mViewInverse`, `g_LightPoint`, `g_nNumLightPoint`), and any state assigned
from a parameter or an expression (`AlphaRef = <x>`, `ZEnable = !g_b`) changes
with `SetValue`. **There is no conservative half-replay**: D3DX uploads
constants only inside `BeginPass` (and `CommitChanges`, which needs an open
pass, i.e. a `BeginPass`), so "replay the literal states, let D3DX do the
constants and textures" still walks the pass. The real design is **full
replay with parameter ownership**: the wrapper stores every parameter value
itself (`SetMatrix/SetVector/SetInt/SetFloat/SetBool/SetTexture/SetValue`,
parameter blocks: record between `Begin/EndParameterBlock`, re-apply on
`ApplyParameterBlock`), knows for each pass which recorded call depends on
which parameter, and regenerates those calls per draw:

- `SetTexture(stage)` ← the sampler parameter's `Texture` state ← the texture
  parameter's current pointer (stage from the PS constant table);
- constant uploads ← register ranges from `D3DXGetShaderConstantTable` on the
  pass's shader bytes at load (documented API, module already loaded), packed
  once into a local array, issued as the one `SetVertexShaderConstantF` the
  measured D3DX issues (run91: 1,003 per 1,006 draws);
- a state whose value is a parameter reference ← read from the store;
- a state whose value is an **expression** (compiled FXLC) → the pass is
  **not replayable**; the wrapper marks it and D3DX applies it as today.

Classification source: the compiled-effect container the wrapper receives in
`D3DXCreateEffect(pSrcData)`, the same layout `effect_passes.py` walks. The
tool today decodes only the shader states (ops 146/147); extending it to
classify every state record (literal / parameter reference / expression /
sampler block) is the host-side prerequisite, and the wrapper parses the same
records at load. A second, self-checking source: record the pass twice under
two parameter assignments and mark every differing call parameter-dependent;
the two must agree or the pass is excluded.

**Hot-path cost (replay, per pass).** Key lookup and dispatch ~50 ns; 51
literal render/sampler entries compared to the shadow ~0.4–0.6 µs (FEX
per-instruction cost is the uncertainty); ~1.3 changed states issued 15 ns;
4.5 textures resolved and compared, ~4 issued through the proxy's SetTexture
path ~0.5 µs; constant pack and one upload ~0.3 µs; two shader sets ~0.2 µs
(6 % elidable). **≈ 1.5–2.0 µs against 6.66 µs today: 4.5–5.0 µs per pass,
4.4–4.9 ms per frame; stated as 3–5 ms** because the FEX cost of the compare
loop and of the wrapper's ~40 parameter setters per draw (store plus dirty
flag, ~25 ns each, ~1 µs) is estimated, not measured. The parameter setters
replace D3DX's own setter path, which is inside today's 4.5 ms residual; that
is the low-confidence extra 1–2.5 ms (§ residual). Off, the wrapper is one
IAT slot left untouched: zero cost.

**Hazards and mitigations.**
- Wrong replay: visible corruption, or a crash on a stale texture pointer.
  Per-pass opt-in from the fixture's pass list, `X3M_PASS_REPLAY` default off
  until qualified, a per-pass mask for in-game bisecting, and a **verify mode**
  (replay, then D3DX apply through the recording manager, diff every call
  against the shadow, count `replay_mismatch` by effect/pass/state).
- Shadow staleness: the game's 17 direct `SetRenderState` sites (overlay
  routines), the two manager-thunk writes at `0x004c3fae`/`0x004c3fc5` and
  the proxy's own `native<>` writes bypass the wrapper. The replay issues
  through the proxy's internal set functions (shadow update plus native call);
  device-level hooks stay only to feed foreign writes into the shadow (~1,000
  calls per frame after replay, so the hybrid unhook's saving is kept);
  invalidate wholesale at `Reset`, `after_reset`, `resync_shadow`,
  state-block `Apply` and route restore failures.
- Texture pointer reuse: pointer plus the proxy's texture epoch, or clear on
  release observation. D3DXHANDLE: handles are D3DX pointers and strings are
  accepted too; a pointer set with a name-lookup fallback, mirroring D3DX.
- Multi-pass techniques, `SetTechnique`, `FindNextValidTechnique`: record per
  (effect, technique handle, pass index), issued in recorded order. Any
  unclassified manager call (SetNPatchMode, SetTransform, FVF) marks the pass
  not replayable.
- Motion-output route: it reads draw-time state through `Get*` and keys its
  168 pairs on the bound programs, which the replay sets through the same
  hooked path; verify mode's exact device-state equality protects it.

**Verification.** (1) Host: classification counts for all 22 effects; the
top-8 run91 pairs must be literal-plus-simple-parameter. (2) Wine fixture
under `wine_lock.py`, X3 bottle: for every (effect, technique, pass), D3DX
apply vs wrapper replay from a fresh shadow under two random parameter
assignments, exact `Get*` equality of every render state, sampler state,
texture and constant register, then each material pair's reference draw
hashed against the D3DX-applied draw in the motion-output fixture. (3) In
game, verify mode: `replay_mismatch` 0 over a session covering the Argon busy
view, sun-lane cutouts, glass, engine glow and the env-map pass;
`draw_pairs`/`cutout_pairs` identical; motion seams unchanged. (4) Production
mode: `pass_phases apply_p50_us` and `dt_p50` at matched `passes_p50`.

**RE prerequisites.** The container state classification (host, no Wine);
the ID3DXEffect slots the game uses against `d3dx9effect.h` (known: 9, 26,
30, 34, 38, 52, 58, 59, 62–64, 66, 67, 71, GetBool/GetInt, parameter blocks,
FindNextValidTechnique; unknown: OnLostDevice/OnResetDevice, CloneEffect);
which `d3dx9_37` loads (one `GetModuleFileNameW` line; the recording is
agnostic, the fixture's reference is not).

## Option B: reimplement BeginPass from the parsed effect

Inside `BeginPass`, besides literal state assignments, D3DX binds the pass's
shaders, resolves each sampler's `sampler_state` block and `Texture`
parameter, evaluates expression states and uploads every parameter the
shaders use (constant-uploads.md §2; one `SetVertexShaderConstantF` per draw
measured). None of that is reachable through the documented interface without
an open pass, so B cannot "leave constants and textures to D3DX" and skip the
walk; B owning all of it is A with the container parse as sole source of
truth, less safe than A's recording. The **coalescing variant** (one open pass
across consecutive same-pass draws, `CommitChanges` between) is dead by the
batchability number: 6.0 % of consecutive draws share a material, at most
~0.4 ms of the 6.5 ms. B is not carried further.

## Option C: reduce the draw share (no proxy code)

Per pass 8.75 µs: the proxy's own draw path ~1.0 µs (the benchmark's draw
pair), the two geometry calls (small), and ~7.5 µs in wined3d's d3d9 frontend
(emulated x86 PE; command-stream emission, possibly CS back-pressure,
unmeasured). DXVK's d3d9 frontend has a lighter calling-thread path;
plausible under FEX 2–6 µs per draw, **draw share 2–6 ms, saving 2.5–6.5 ms**,
but zero or a device-creation failure (MoltenVK feature gaps) is also
plausible, and `apply_p50_us` may move either way since the 63 state calls
land on DXVK's setters. Whole-backend risk (RESZ, MRT, state blocks, Reset,
the HDR path) means this ships nothing; it measures.

How: the proxy loads its backend by absolute path,
`GetSystemDirectoryW()\d3d9.dll` (`src/proxy/loader.cpp`), so CrossOver's
bottle DXVK switch reaches the proxy **only if it replaces the bottle's
`syswow64/d3d9.dll`** (hash against Wine's `58cc36cf…`, platform.md; read
the `backend path=` line). If it only sets a `DllOverride`, a
`X3M_BACKEND_PATH` override (one line, small-change class) comes first.
Companion experiment on the 6.5 ms share: once the module-identity line says
which `d3dx9_37` serves the game, force the other by `DllOverride`, one
session; outcome unknown, range −2 to +2 ms.

`tools/manage.py launch --d3dx {native,builtin}` is that switch. `native`
(default) leaves the command exactly as it is, so the bottle decides; `builtin`
appends `d3dx9_37=b` to the child's own `--dll` override
(`d3d9=n,b;d3dx9_37=b`, or `d3d9=b;d3dx9_37=b` under `--vanilla`), using the
`;` entry separator of `WINEDLLOVERRIDES` that CrossOver's `wine --dll` feeds,
so the user's other bottle overrides and every other process stay untouched.
No DLL change: the installed proxy already logs which file loaded. The run
compares one `--pass-phases` session per setting: the per-pass `BeginPass`
median in microseconds against run95's **6.7 µs**, and the `loaded_module`
line for `d3dx9_37` in each session log, which must name the game directory's
native redistributable in the `native` run and the bottle's builtin in the
`builtin` run — if both name the same file the comparison is void.

## Option D: accept 26.5 ms

Correct if C returns nothing and A's classification excludes the top pairs,
or if the residual split shows the engine, not D3DX, owns the remaining time.

## The 4.5 ms residual (4.6 µs per draw)

Plausible content, per draw: (a) ~40 D3DX parameter setters (1–5
`SetMatrix`, 10 `SetVector`, 15 `SetInt`, 10 `SetFloat`, ≤ 10 `SetBool`, 4
`GetBool`, 2 `ApplyParameterBlock`) plus 22 cached-texture-setter calls,
d3dx9 code, estimated 1.5–3 µs; (b) `Begin`/`End` per sub-mesh, 0.3–0.8 µs;
(c) engine work: `0x004c4fc0`, world matrix `0x004bdee0`, texture-animation stepper `0x004f66e0`
(not a cull; the cull/LOD pass is `0x0047cfe0`, see [shadow-caster-lifetime.md](../reverse-engineering/shadow-caster-lifetime.md) §0),
queue walk, the O(n²) sort `0x0047e620`, `0x004c0150`'s own code, the two
engine state writes. **A stamp pair EndPass(n) → BeginPass(n+1) attributes
nothing new**: in the single-pass case it spans the routine exit, the caller's
walk, the next node's entry, parameter writes and `Begin`, i.e. it is the
residual already computed as `view_submit − sum`. Splitting it needs two more
accumulate-only stamps: at the sub-mesh loop head `0x004c0223` (bytes read
this session `8b 94 24 a0 00 00 00`, `mov edx,[esp+0xa0]`) and at `Begin`'s
return point `0x004c1ec0` (the `call ecx` at `0x004c1ebe` is `ff d1`; next
bytes `8b 75 0c f7 86 30 01 00 00 00 00 02 00`, a 13-byte plain span before a
`jz`), ≈ 2 × 981 × 91 ns ≈ 0.18 ms per busy frame, giving [sub-mesh head →
Begin returned] = bind guard + `SetTechnique` + `Begin`, [Begin returned →
`pass_begin`] = (a) + guard + two RS writes, and remainder = (c) outside the
pass loop. Incoming edges, data references and ESP depth at those two sites
are **not validated**; same contract as `verify_pass_phase_sites.py`. Reducible: (a)
and (b) through A's parameter ownership (the low-confidence 1–2.5 ms); (c)
only by engine patches (the sort), a separate decision that this split would
justify or kill.

## Native Windows position

A: documented COM wrapper (`d3dx9effect.h`), documented `SetStateManager`
recording, documented `D3DXGetShaderConstantTable`, and an import-slot patch
of the game EXE's own IAT (`0x00532324`, byte-verified against the resolved
export), the same class as the loading trace's existing import hooks; no byte
touches a Microsoft DLL. The compiled-effect container parse is of the game's
data files (undocumented but stable layout, already relied on by
`effect_passes.py`). Cross-compiled and fixture-qualified under CrossOver;
native execution unverified, entry in
[platform-portability.md](platform-portability.md) when installed. C is a
CrossOver-only experiment and ships nothing. The two residual stamps are a
hash-gated EXE patch like the pass sites.

## Unknowns

The walk-versus-calls split inside BeginPass (arithmetic only; a recording
call count per pass would fix it); whether the top pairs' passes carry
expression states (classification tool); which `d3dx9_37` loads (one log
line); whether CrossOver's DXVK switch replaces `syswow64/d3d9.dll` (one
hash); the residual split (two stamps); the FEX cost of the replay compare
loop and parameter store (the fixture's benchmark row, before any game run).

**Outcome (run 36 A1/A2, run104/run105, 2026-09-17).** On one build with the
identity fields readable, the builtin D3DX raises BeginPass from 6.58 to
8.71 µs per pass (+32 %) in the busy view and from 6.33 to 8.22 µs in the
empty view; draw and EndPass unchanged. The native redistributable stays and
`--d3dx builtin` remains only as a diagnostic. Both bottle experiments are now
closed (DXVK renders black on this CrossOver Preview); what remains of the
plan is the host prerequisites and the pass-replay wrapper, only if the 3–5 ms
is still wanted ([ledger](../verification/sampling-profiler.md), run104/105).

**Environment experiments (outcome, run107/run108, 2026-09-17: FEX TSO off changes nothing within 1 %, hardware TSO on Apple silicon; wined3d CSMT off doubles the draw call and adds 46 % to the frame; both closed, CSMT stays on. Details in the profiler ledger.)** Original text: Two launcher options change only the game child's
environment, nothing in the bottle or the application. `--fex-tso {on,off}`
sets `FEX_TSOENABLED`; `off` relaxes FEX's emulation of x86 total store order,
which targets the ~11 ms of emulated engine and D3DX code per busy frame (every
guarded store in that path gets cheaper). `--wined3d CONFIG` sets
`WINE_D3D_CONFIG`, the documented environment form of the
`HKCU\Software\Wine\Direct3D` keys; the experiment uses `csmt` (`csmt=0x0`
runs the draw path on the calling thread instead of the command-stream thread)
and `renderer` (`gl` or `vulkan`), targeting the ~7 ms draw path. Risk: relaxed
store ordering removes a correctness guarantee the emulated game and Wine rely
on and can expose latent multithreading races (hangs, corruption, crashes that
do not reproduce without it), and a wined3d renderer switch can change or break
presentation; both are experiments, never defaults, and a misbehaving session
is void. Neither leaves log evidence — the proxy's `loaded_module` line for
`d3d9.dll` does not reflect either variable — so a run is judged by behaviour
and timing: one `--pass-phases` session per setting compared against run105's
native figures, BeginPass 6.58 µs, draw 8.76 µs, engine 4.45 µs and frame
dt 22.2 ms, in the same view.
## Prerequisite 1: state classification

2026-09-17, host only (no Wine, no game, no build).
`tools/analysis/effect_passes.py` now classifies every state assignment D3DX
applies for a pass, not only the two shader states:

```sh
python3 tools/analysis/effect_passes.py \
  "$HOME/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3" \
  --classification verification/results/effect-pass-classification.json
python3 tools/analysis/effect_passes.py <game> \
  --pass shader/1_1/adeffects.fb:DEFAULT:P0        # per-state detail printer
```

Source: the bottle's own numbered `*.cat`/`*.dat` archives, read in memory as
before (a virtual path present in several catalogues is taken from the last
one, i.e. what the game loads). No archive bytes enter the repository; the
summary holds counts, names and effect digests only (16 KB).

**Method.** The container walk now keeps the parameters, the full four-dword
state records and both resource scopes. A state's value source is the resource
record for its `(technique, pass, element, state)` key: absent = a literal in
the container; `usage 0` = the compiled program on a shader state and an FXLC
preshader (expression) on any other; `usage 1` = a parameter reference, whose
payload is the parameter name; `usage 2` = an array selector. Passes carry no
sampler or texture states at all in this game's effects: every one comes from a
sampler parameter whose value is its own state list, and D3DX applies that list
inside `BeginPass` for each sampler register the pass's programs declare, so
the tool attributes a sampler block to a pass through the programs' CTAB
sampler names. The D3DX state table is reconstructed from its own order and
pinned by six anchors (`Lighting` 48, `SeparateAlphaBlendEnable` 99,
`VertexShader` 146, `PixelShader` 147, `Texture` 164, `MaxAnisotropy` 174); all
98,636 resource records of the archive land on a state the walk produced, and
no operation outside the table occurs.

**Totals** (2,352 effect files, 1,708 distinct containers, 4,528 passes):
**204,416 state assignments, of which 75,984 (37.2 %)
are constant** and 128,432 (62.8 %) parameter-driven. By class:

| class | constant | parameter-driven |
| --- | --- | --- |
| render state | 21,828 | 42,440 |
| sampler state | 45,100 | 69,096 |
| texture binding | 0 | 16,896 |
| shader binding | 9,056 | 0 |
| other / unknown | 0 | 0 |

By value source: 111,536 expressions (54.6 % of all assignments), 66,928
literals, 16,896 parameter references, 8,984 shader blobs, 72 NULL shaders.
Per pass the mean is 45.1 assignments (16.8 constant), 3.7 sampler blocks:
14.2 render states, 25.2 sampler states, 3.7 texture bindings, 2.0 shaders —
the same shape as run91's measured 19 / 32 / 4.5 / 2 per draw, whose mix is the
busy view's rather than the archive average. **Only 208 of 4,528 passes
(4.6 %) contain no expression and no array selector**; the rest need FXLC
evaluation for at least one state. The archive holds six shader-model trees
(`1_1`, `1_4`, `2_0`, `2_a`, `2_b`, `3_0`) × four hue/light variants; one
session loads one tree, ~188 passes of it, 22 effects.

Top ten passes by parameter-driven count (duplicates across the variant trees
collapsed; all ten are `2_0` XT material effects):

| passes | parameter-driven / states |
| --- | --- |
| `xt_standard_lighting.fb` BUMPMAP/P0, BUMPMAP_LOW/P0 | 59 / 91 |
| `xt_standard_lighting2s.fb` BUMPMAP/P0, BUMPMAP_LOW/P0 | 59 / 91 |
| `xt_standard_lighting_damage.fb` BUMPMAP/P0, BUMPMAP_LOW/P0 | 59 / 91 |
| `xt_standard_lighting_damage2s.fb` BUMPMAP/P0, BUMPMAP_LOW/P0 | 59 / 91 |
| `xt_terraformer.fb` BUMPMAP/P0, BUMPMAP_LOW/P0 | 59 / 91 |

**What this decides for option A.** The literal-versus-parameter split the
option A estimate assumed does not hold: a majority of the states the engine's
material effects assign are compiled expressions over the `g_*` and `t_*`
parameters (`ZEnable`, `CullMode`, the whole blend group, `AddressU/V`,
`MaxAnisotropy`), not literals. Replaying only the constant assignments covers
37 % of the work and, per § "The parameter problem", cannot be combined with a
D3DX apply for the rest. Full pass replay therefore needs an FXLC preshader
evaluator (a small stack machine over the wrapper's parameter store,
re-evaluated per draw for the dirty parameters) as a third prerequisite, or it
is restricted to the 4.6 % of passes that carry none — which the run91 material
mix is not in. Sizing the evaluator (opcode inventory and expression length
over the same containers) is the next host-side step and is cheap; the
measured 3–5 ms estimate above stands only if it succeeds.

**Limits.** The classification is static: it is what the container says D3DX
will apply, not a recording of what the loaded `d3dx9_37` emits (option A's
recording manager remains the self-check). A sampler declared by both the
vertex and the pixel program is counted once, where D3DX would apply its block
per stage; no pass in the archive declares a vertex-stage sampler (checked),
so the case does not arise here. Synthetic
coverage for each class is in `verification/analysis/test_effect_passes.py`.
## Assessment after the classification and environment experiments

Ratified 2026-09-17: no FXLC-aware replay and no constant-only replay now; the two attributions first (the native-BeginPass fixture under the three setter regimes, then a profile session), then the decision between a setter-dedup wrapper, full replay, or stopping at 22 ms.

2026-09-17, host only (no build, no Wine, no game). Inputs: the sections
above, [effect-pass-loop.md](../reverse-engineering/effect-pass-loop.md),
the profiler ledger (run95, run104/105, run107/108),
`tools/analysis/effect_passes.py` and its counts file, plus the `--pass`
printer run against the bottle archive for the busy view's own passes
(`shader/3_0/argon.fb` DEFAULT/P0 and BUMPMAP/P0, `argon2s.fb` DEFAULT/P0,
`xt_standard_lighting.fb` BUMPMAP/P0; names and counts only). Wine's
`d3dx9_36/preshader.c` was not fetched; statements about it are from memory
and marked unverified.

**Where the frame stands (run105, native D3DX, busy Argon view).** Per pass
BeginPass 6.58 µs, draw 8.76, engine between passes 4.45, EndPass 0.11; 787
passes; `dt` 22.2 ms. So BeginPass is 5.2 ms (23 %), the draw 6.9 ms (31 %),
the engine remainder 3.5 ms. Every environment lever is closed: DXVK black,
builtin D3DX +32 % on BeginPass, FEX TSO within 1 %, CSMT off +46 %. Only code
remains, and only BeginPass is proxy-removable code; the draw is wined3d.

**What the busy view's passes actually contain.** The Argon hull DEFAULT/P0
pass (the run91 top pair) applies 53 assignments: 2 shader blobs, 16 literals
(Lighting, ColorVertex, BlendOp/BlendOpAlpha, AlphaRef, AlphaFunc, the whole
`CubeMapTexSampler` block, `MaxMipLevel` of the other three samplers), 4
texture parameter references, and **31 FXLC expressions**: 13 render states
(ZEnable, ZWriteEnable, CullMode, AlphaTestEnable, AlphaBlendEnable,
SeparateAlphaBlendEnable, SrcBlend/DestBlend and the alpha pair,
ColorWriteEnable, Wrap0/Wrap1) and 18 sampler states (AddressU/V,
Min/Mag/MipFilter, MaxAnisotropy on three samplers). BUMPMAP/P0 is 61 with 24
sampler expressions; `xt_standard_lighting` BUMPMAP/P0 is 77 with 57
parameter-driven. **No pass the busy view draws is expression-free**; the 208
replayable passes are irrelevant to this frame. The classification cannot say
whether an expression is trivial (a typed cast of one parameter) or real (a
select on `g_*` flags); the resource sizes would, and are not yet decoded.

### (a) Must a proxy replay evaluate FXLC?

Yes, for any pass this frame draws, and in **two forms**: the pass and
sampler-block state expressions above, and the preshaders embedded in the
compiled programs (`PRES` comment chunks; the station material's grading
matrix in PS c0..c2 is "computed by the preshader",
[station-material-distance.md](../reverse-engineering/station-material-distance.md),
and the motion-output notes record CPU preshader metadata in the PS
comments). A wrapper that owns the constant upload must produce those
registers itself, so the second evaluator is not optional. Both use the same
FXLC opcode set (Wine implements one interpreter for both; unverified).

The split of the 6.58 µs is **unknown**. Arithmetic only: the ~55–63 manager
calls cost ≈ 1.4 µs at the measured per-call figures; the remaining ≈ 4.6–5.0
µs covers ~31 expression evaluations, at least one shader preshader,
constant packing for ≤ 5 float4 ranges plus `i0`, four texture resolves and
the state walk itself. If uniform, ≈ 50 ns per item; expressions are the
largest item class and plausibly the most expensive each, but nothing here
measures them. Two cheap attributions, in order of cost:

1. **One profiler session** (`X3M_PROFILE=1`, existing, no build): the
   main thread's `leaf_d3dx` share of samples during the busy view against
   the `pass_phases` BeginPass share, and the top-48 `profile_leaf` RVAs
   inside `d3dx9_37`. A bytecode interpreter concentrates in a few RVAs;
   state iteration and packing spread. This costs one user run and gives
   the fraction without any disassembly; a ≤ 100-instruction look at the
   hottest RVA (allowed, targeted) then names it.
2. **One Wine fixture, no game** (the decisive measurement):
   `D3DXCreateEffect` on the `argon.fb` bytes read from the CAT at run time
   through the game directory's native `d3dx9_37` on the state-hook
   benchmark's synthetic device (`state_hook_benchmark.cpp` already creates
   it), with a counting state manager. Per iteration `Begin`/`BeginPass`/
   `EndPass`/`End`, timed, in three regimes: (i) the game's pattern, ~40
   per-draw setters rewritten with **unchanged** values; (ii) rewritten with
   changing values; (iii) not rewritten. (i)−(iii) is the dirty-driven part
   (expression and preshader re-evaluation, constant re-upload); (iii) is
   the fixed walk; the manager's call count per pass against the 53
   classified assignments cross-checks the classification and tells whether
   D3DX elides anything. The fixture's (ii) must land near 6.6 µs or the
   game's cost is not what the fixture models. This also answers a question
   the note has not asked: **whether native D3DX marks a parameter dirty on
   a same-value `Set*`** (Wine's setters do unconditionally, unverified; MS
   unknown). If it does, the game's 15 `SetInt` + 10 `SetFloat` + 10
   `SetVector` per draw force every dependent expression and constant to be
   redone each BeginPass even when nothing changed.

### (b) Wrapper architecture

The only sane entry stays the EXE's single `D3DXCreateEffect` import
(`0x00532324`; `src/proxy/loading_trace.cpp` already patches that slot and
forwards through the same signature). A full replacement of D3DX (own effect
runtime) is rejected: it is option A with none of D3DX's behaviour left to
compare against, and it forfeits the recording cross-check.

**Constant-only replay with D3DX still applying the rest is not faster.**
D3DX has no device shadow (94.9 % of its render-state writes are no-ops at
the device, run91) and walks every record of the pass in `BeginPass`
regardless of what the wrapper set beforehand; the wrapper's constant apply
is purely additive. The one variant that removes work is to **strip the
constant records from the container** before `D3DXCreateEffect` (rewrite the
pass state tables and renumber the resource keys, which index by state
position) and apply them from the wrapper on pass change against the proxy
shadow. Its bound is small: 16 of 53 records in the Argon DEFAULT pass, so ≤
30 % of the walk if the walk is uniform per record, ≈ 0.5–1.0 µs per pass,
**0.4–0.8 ms per frame**, minus the wrapper's own apply; a container rewrite
is fragile and the shader records (constant, 2 per pass) cannot be stripped.
Not worth building alone.

**Recording-manager cross-check** (unchanged from option A, made concrete):
in verify mode the wrapper replays, then lets D3DX apply the same pass
through a manager that diffs every call against the shadow and counts
`replay_mismatch` by (effect, technique, pass, state); the recorded call
count per pass must equal the classified count (53 for Argon DEFAULT/P0)
minus whatever D3DX demonstrably elides, or the classification is wrong for
that pass and it is excluded.

### (c) Cost, risk and the bound on the gain

Engineering: the COM wrapper (ID3DXEffect's full vtable forwarded, clones
wrapped), a parameter store keyed by D3DX handles with name fallback, the
FXLC evaluator for state expressions, the shader-preshader evaluator with
its CTAB register mapping, a constant packer that reproduces D3DX's
int/bool/matrix packing, sampler and texture resolution per CTAB register,
value-gated dirty tracking, parameter blocks, `CloneEffect`,
`OnLostDevice`/`OnResetDevice`, verify mode, and the fixture that proves
device-state equality for every pass under random parameters. Roughly the
size of the motion-output route; the largest hot-path component the proxy
would own. Risks that the fixture cannot fully retire: preshader precision
(Wine evaluates in double; native's precision unverified; a select on a
float compare can flip a state), the ~30-opcode semantics, array selectors,
shared parameters across the 22 effects, texture lifetime, and any game
parameter write path the wrapper does not see (`SetRawValue`, blocks).

Gain bound. The earlier replay estimate of 1.5–2.0 µs per pass assumed
literal compares only. Add value-gated expression evaluation (rarely fires
if D3DX's dirty-driven work is what the game pays for; every draw if not) and
the preshader: **2.0–3.0 µs per pass**, i.e. 1.6–2.4 ms against today's 5.2
ms, **saving 2.8–3.6 ms of 22.2 ms (13–16 %)**, about 45 to 51–52 fps, plus
the low-confidence 1–2.5 ms of the residual if the setters are owned. The
number holds only if the fixture confirms that most of BeginPass is D3DX's
own work rather than something the replay must also pay (the manager calls
and the constant upload are paid either way).

### (d) Recommendation

**Do not start the FXLC-aware replay now; run the two attributions first
(one profiler session, one no-game fixture), then decide by their numbers.**
Constant-only replay is rejected in both forms (additive without stripping;
≤ 0.8 ms with it). Decision rule once the fixture reports:

- If regime (i) costs markedly more than (iii) (≥ 2 µs per pass), the cheap
  lever is a **thin setter-dedup wrapper**: the same import-slot wrapper,
  forwarding `SetInt/SetFloat/SetBool/SetVector/SetMatrix/SetTexture` only
  when the value differs from its own copy, invalidating its copy on
  `ApplyParameterBlock` (2 per draw; record the block's parameters between
  `Begin/EndParameterBlock`), `SetValue`/`SetRawValue`, `CloneEffect` and
  `OnResetDevice`. No replay, no FXLC, no recording; a few hundred lines
  of documented COM; hot path ~40 compares per draw (~1 µs under FEX,
  estimated). It recovers the dirty-driven share of BeginPass and part of
  the residual's setter cost; the bound is exactly (i)−(iii) times the pass
  count plus what the setters cost inside the residual. Then reassess
  whether the remaining walk is worth full replay.
- If (i) ≈ (iii), the walk is fixed cost, the setter wrapper is worthless,
  and full replay is the only lever on the 5.2 ms. Build it only if 13–16 %
  is worth a motion-output-sized component with the risks above; otherwise
  **stop at 22 ms** and spend the effort on the draw share, which is larger
  and not proxy-owned (fewer draws: the 5 % instancing candidates and the
  engine's O(n²) sort are engine patches, a separate decision).

Cheaper levers checked against the numbers: the ~63 redundant state calls
per draw inside BeginPass cost the device 11–15 ns each, ≈ 0.5–0.7 ms per
frame if all vanished, and are already bounded and shelved in
[engine-state-filter.md](engine-state-filter.md) (0.3–1.0 ms); a replay's
shadow compare removes them as a by-product, not as a reason. `D3DXFX_DONOTSAVESTATE`
is already set by the game. No `D3DXCreateEffect` flag disables preshaders in
an already-compiled container. Nothing else in the classification suggests
a lever below the setter-dedup wrapper.

**Native Windows.** Both wrappers are documented COM over the game's own
IAT slot. The setter-dedup wrapper is semantically transparent if native
D3DX treats a same-value set as a no-op or as a dirty mark with the same
resulting device state (it does: the value is the same), so its native
behaviour is not in doubt; only its benefit is Windows-unverified. The FXLC
evaluator would have to match native preshader semantics that the user
cannot test on Windows; verify mode against the native redistributable under
CrossOver is the only guard, and that gap would be recorded in
[platform-portability.md](platform-portability.md).

**Unknowns and what settles each.** The BeginPass split and the same-value
dirty behaviour: the fixture above. The share of interpreter time: the
profiler session. Whether the expressions are trivial casts or real
selects: decode the FXLC resource blobs in `effect_passes.py` (opcode
inventory and instruction count per expression; the chunk layout is the one
Wine's parser documents, unverified here) — one host-side step, no Wine.
Native preshader precision: not answerable without disassembling
`d3dx9_37`, which the setter-dedup path never needs. The residual split: the
two stamps of § "The 4.5 ms residual", still pending.

### Fixture outcome (2026-09-17): the setter-dedup wrapper is dead

The no-game fixture ran (`verification/probe/run_effect_beginpass.py`,
`verification/results/bottle-X3/effect-beginpass.json`, numbers and scope in
[sampling-profiler.md](../verification/sampling-profiler.md), "Native
BeginPass attribution fixture"): on `shader/3_0/argon.fb` DEFAULT/P0 with the
game's own native `d3dx9_37` (`image_size=3895296`, `wine_builtin=0`), per
`BeginPass` regime (i) same-value `Set*` costs 1.60 µs, regime (iii) no
`Set*` 1.50 µs, and both issue **exactly the same 57 state-manager
callbacks**; a changed value costs 3.30 µs. So (i) ≈ (iii) by the decision
rule above — **native D3DX already charges nothing for a same-value write,
the thin setter-dedup wrapper has no gain to recover, and it is rejected**.
The rule's other branch now applies: the walk is fixed cost, and only full
replay could touch it. The fixture also narrows what replay would win: D3DX
re-applies all 57 pass states on every `BeginPass` (19 render, 28 sampler, 4
texture, 2 shader, 4 constant calls / 52 registers), so a replay must issue
those device calls too and can only save D3DX's own walk plus the 1.8 µs
dirty-driven evaluation that a changed parameter costs — and the game's real
per-draw parameter writes do change values, so that 1.8 µs is inside the
in-game 6.58 µs, not additional to it. The remaining decision (full replay
versus stopping at 22 ms) waits on the profiler session, which is the only
thing that can say how much of the in-game 6.58 µs is D3DX's own code rather
than the 57 device calls; this fixture's 1.5 µs floor on an idle device is a
lower bound on the walk, not a measurement of the in-game figure. Wine's
builtin was 6–13 % slower on `BeginPass` and ~3× slower on the parameter
setters, consistent in direction with run104/105 but far smaller here.

## Decision (2026-09-17, ratified): stop at the native path

The attribution fixture shows native D3DX's own BeginPass walk on the busy
view's material pass costs about 1.5 µs with all 57 state callbacks issued and
that a same-value `Set*` costs nothing (no dirty marking), so neither a
setter-dedup wrapper nor a constant-only replay has anything to remove; the
remaining ~5 µs of the in-game 6.58 µs per BeginPass is wined3d processing
the 57 forwarded calls against real state changes, which a proxy replay would
have to issue as well. With the bottle and environment experiments closed,
the busy frame stays at ~22 ms on this backend: draw 8.8 µs, BeginPass 6.6,
engine 4.5 per pass. The `--residual-phases` group remains available to
attribute the engine share if a future lever appears; no pass replay is
scheduled.

