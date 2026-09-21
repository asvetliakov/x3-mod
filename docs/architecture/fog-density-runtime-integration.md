# Stored-density fog: production runtime integration

**Ratified by the orchestrator 2026-09-21** for delivery in the four reviewed checkpoints of §6, on the far-only-interior-while-fine-fills reading (the user accepts soft appearance during the ramp; the plan-strict hold remains the fallback if a flight shows it popping). Out-of-scope items stand. Native Windows behaviour remains unverified until run there.

**Design note for ratification (2026-09-21). No code, build, Wine or game work is authorized by this note.** It integrates the frozen two-level representation of the [runtime plan](fog-density-runtime-plan.md) (field, 8-point prefilter, FP16, 128³ levels at 512/4096 units, LOD 4–6 km, taper 30–40 km, fixed 24+40 samples) into the production `FogPass` described by the [spatial production contract](volumetric-fog.md#spatial-production-contract-2026-09-20). Accuracy gate: T p99 ≤ .002, max ≤ .003, temporal ≤ .003 (rescaled by the parent; the host run passes at .00197 / .00236 / .00184). Fog stays opt-in, off by default; `--volumetric-fog`, Ctrl+Alt+F9 (on/off) and Ctrl+Alt+F10 (strength ladder, `sigma_eff = family_sigma * S/.02`, user at S=.03) are unchanged.

## Recommendation

Generate both density levels **at runtime on one background CPU thread**, streamed as the camera moves, into **two toroidally addressed 2-D RGBA16F atlases** (one per level, 1032×516, four Z nodes per texel, one duplicated border texel per 129² tile) uploaded through the already qualified SYSTEMMEM→DEFAULT `UpdateTexture` path. Replace the march program with a **single 64-iteration loop** (24 near bins over [0, min(L,12000)], 40 far bins over [12000, L]) that samples the fine level to 30 000 units, the far level from 20 000, blends with `lambda = 1-smoothstep(20000,30000,s)` and applies `w = 1-smoothstep(150000,200000,s)`; the horizon constant becomes 200 000. Keep the half-resolution march and the qualified 2×2 composite, but move full-pixel edge repair into a **third, separate full-resolution draw** so the composite stays under 512 slots. Chroma becomes one constant per family (the screen used the family mean chroma; the stored value is scalar density). Readiness is smooth: far-level readiness gates drawing with a 90-frame density ramp; fine-level readiness ramps `lambda` in over 90 frames. Nothing blocks a frame or the sector load; the fog appears a few seconds after sector entry and sharpens once fine is filled.

## 1. Payload production

**Decision: runtime CPU streaming for both levels; no shipped density asset; no full bake at startup.**

Why the alternatives lose:

- *Offline baked asset.* The field is world-anchored and non-periodic (plan: no tiling, no reseeding), so a shipped bake must cover every position the camera can reach. Fine spacing 102.4 m over a sector tens of km wide is hundreds of millions of nodes; the 4,194,304-node / 8 MiB figure is one camera-centred window pair, not a sector. A shipped far level anchored at the sector origin (4 MiB raw per fogged sector, ×35) would abandon the plan's camera-following far window and still leave fine to runtime generation, so it adds an asset family without removing the generator. It loses.
- *Full bake at startup.* Measured host rate is ≈210 k nodes/s (fine 192,467 nodes in .910 s, far 109,837 in .544 s, bricks 32,768 in .148 s; NumPy on the arm64 host, `report.json` `cost.lazy`/`generation_probe`). A full 4.19 M-node bake is ≈20 s host-equivalent. Native SSE2 C++ with corner sharing inside bricks is estimated 3–10× faster per core, and FEX adds an unmeasured factor; even the optimistic case is seconds of blocked loading, which conflicts with the loading-time goal. It loses.
- *Lazy streaming (selected).* Steady state is one 127² slab per node crossing: ≈16 k nodes, 5.0 ms measured per 1,024-node slab on the host, so ≈80 ms host-equivalent per slab. Fine slabs are crossed every 102.4 m of travel; far every 819.2 m. Initial fill is far first (nodes within 40 km + margin: ≈99³ ≈ 0.97 M) then fine (within 6 km + margin: ≈119³ ≈ 1.69 M), each ordered by distance from the camera. Estimated time-to-first-fog after a sector entry is 1–5 s on one core; **the checkpoint-1 microbenchmark under Wine settles this.** If it is above ~5 s, add a second worker thread before changing anything else.

**Worker contract.** One `std::thread`/`CreateThread` worker owned by the pass, started at first `prepare`, stopped and joined at `detach` and process exit; it touches no D3D object and no engine memory. It reads a request (level, sector key, family recipe, window origin) and writes bricks into a CPU cache (one 4.06 MiB array per level, storage position = node mod 128). Publication is a lock-free generation counter: a slab becomes visible to the render thread only when all its nodes are written. The render thread, at the existing HDR-owner latch (`prepare_volumetric_fog_targets`, never in a draw bracket), copies completed dirty slabs into the SYSTEMMEM texture (`LockRect` + `AddDirtyRect`) and issues one `UpdateTexture` per level per frame, with a per-frame cap (proposed: 8 tiles ≈ 1 MiB). Origin and data are published together: the shader constant for a level's window origin advances only after every slab the new window needs is resident on the GPU.

**Refill triggers.** Plan values: fine refill when the camera is within .25 km of the window edge (.4512 km minimum margin), far at 4 km remaining (11.61 km margin). Under SETA the fine window can fall behind; then fine readiness drops and `lambda` ramps to 0 (far-only, still a complete world field), ramping back when fine catches up. This is a deliberate reading of the plan's "expose not-ready rather than drawing partial range": the range is never partial; only the LOD is. The plan-strict alternative (hide all fog until both levels are ready) is the fallback if the flight shows the far-only interior as objectionable.

**Per-sector variation (amended 2026-09-21, orchestrator-ratified).** `FogSectorFrame` already carries `sector`, `table`, `record`, `index`, the family profile and the device `generation`, and its `same_key` change disarms replacement, invalidates TAA once and requires a new warmup. `sector`, `table` and `record` are engine heap tokens: they may detect a change but must not place the field, or a sector's clouds would move on every reload and session. Derive a world offset `O_s = delta_far * (hash(index, profile, recipe) mod 4096 - 2048)` per axis from the session-stable background record index, family profile and recipe (sectors sharing a record share a placement; a reallocation neither re-keys nor refills) and bake `rho(delta*k + offsets + O_s)`: a translation of a stationary field, so the screen's accuracy statistics carry over unchanged, and node keys stay world integers. The cache identity is (level, sector key, recipe, O_s); a sector change is a cold start, which the existing contract already treats as a rewarm. Different seeds per sector would also be deterministic but would not inherit the validated numbers; not selected.

## 2. GPU resource layout

**Decision: two 2-D `D3DFMT_A16B16G16R16F` atlases, 1032×516 each, toroidal storage with a one-texel duplicate border; not volume textures.**

- Packing as in the plan: 32 Z groups of four nodes in RGBA lanes, tiles 8×4, but tiles are 129×129 (column/row 128 duplicates storage index 0) so hardware bilinear across the toroidal seam reads the correct neighbour. Storage index is `node mod 128`, independent of the window origin; a window shift uploads only the entering slab plus its border copies (no 4 MiB relayout). Z is not filtered by hardware: the two group fetches wrap naturally (group 31 lane 3 → group 0 lane 0).
- Per level: DEFAULT 1032×516×8 B = 4.06 MiB, SYSTEMMEM staging 4.06 MiB (retained for Reset re-upload), CPU cache 4.06 MiB. Totals: GPU 8.1 MiB (versus 17.0 MiB today), CPU 16.3 MiB (versus 17.0 MiB). The 34.1 MB RCDATA family packets are no longer needed for density; only 14 mean-chroma constants remain (computed offline from the accepted packets).
- Addressing (ps_3_0, per level, all float): `q = (cam_local + dir*s)/delta; b = floor(q); f = q-b; sxy = b.xy - 128*floor(b.xy/128); z0 = b.z - 128*floor(b.z/128); g0 = floor(z0/4); lane0 = z0-4*g0; z1 = z0+1 wrapped; uv = ((g mod 8)*129 + sxy + f.xy + .5, floor(g/8)*129 + ... ) / (1032,516)`; two `tex2Dlod`, lane select by a compare-built mask dot, `lerp` on `f.z`. About 30 ALU + 2 fetches per level sample. `cam_local` is the camera minus the level's window origin in render units, computed in double on the CPU from the existing `fog_world_basis` translation; it is at most 63·delta + one node, so float32 keeps ray positions to < .02 units at 200 000 units (frac error < 5e-6 of a node). The `origin_mod` period check in `fog_valid_params` is replaced by `|cam_local| ≤ 64*delta` per level.
- The 8-point prefilter is a bake-time definition of the stored node, not a runtime filter; runtime reconstruction is trilinear of stored nodes, and hardware bilinear in XY plus a shader lerp in Z is exactly that up to filter precision. The existing atlas witness measured filtering error ≤ .001953 (four binary16 steps) at one point; that is a density error, worth ≈1e-5 in per-sample optical depth at fine spacing, far below the T gate. A texel-exact 8-fetch path (512 fetches per pixel) is not needed; **the checkpoint-2 fixture measures the actual GPU-vs-CPU parity** and would force the manual path only if it fails.
- FP16 law: node = round-to-nearest-even half of the float32 mean of eight rho evaluations, identical to NumPy `astype(float16)`. The C++ generator needs a software RNE conversion (F16C is not in the SSE2 baseline).
- Reset: `before_reset` drops both DEFAULT atlases and the targets; CPU caches and window origins persist; `after_reset` re-uploads both full textures lazily at the next latch (2 × 4.06 MiB), the same rule as today. Capability: unrestricted NPOT 2-D, FP16 linear filter query and `UpdateTexture` are already required by `attach`; the maximum-dimension check becomes ≥ 1032×516. No new format class.

Volume textures (`D3DRTYPE_VOLUMETEXTURE`, A16B16G16R16F or L16 with `D3DTADDRESS_WRAP` and hardware trilinear) would halve fetches and remove the lane arithmetic, and wrap addressing would remove the border. They lose now because filtered volume FP16/L16 is not an established capability on either target (`D3DPTEXTURECAPS_VOLUMEMAP`, `VolumeTextureFilterCaps`, `CheckDeviceFormat` for the volume type are all unqualified here), and a second capability-gated path doubles qualification. Revisit only if the 2-D route fails its timing gate.

## 3. Shader integration

- **March**: one `[loop]` of 64 iterations; iteration `i<24` uses bin `[i*nd,(i+1)*nd]` with `nd = min(L,12000)/24`, otherwise `[12000+(i-24)*fd, ...]` with `fd = max(L-12000,0)/40` and zero width when `L ≤ 12000` (so a near-only ray keeps exactly the screen's 24 samples and 48 reads). A single loop inlines each level sampler and the shaft `fog_visibility` once, which is what keeps the program under 512 slots; two loops would inline them twice. Per sample: `[branch] if (lambda>0)` fine, `[branch] if (lambda<1)` far, `rho = w*(lambda*fine+(1-lambda)*far)`, then the unchanged `a = 1-exp(-sigma*rho*ds)`, `S += T*a*phase*chroma*visibility`, `T *= 1-a`. Sky rays use `L = 200000`; geometry uses `min(depth.b*length(view), 200000)`; invalid depth keeps identity.
- **Shafts** are unchanged: `fog_visibility` runs only for `rho>0` with shadows enabled, and `shadow_weight` is zero outside the cascades' 0.95 extent, so far samples beyond the cascades cost no map reads. The bias, PCF and cascade selection are untouched.
- **Card families and activation** are unchanged: the sector sample selects the family (chroma constant, sigma), the 600/90-frame latch stays observational, replacement warmup and the Reset-only fault latch keep their rules. The new readiness weights multiply `density_scale` (far) and `lambda` (fine); they ramp by 1/90 per frame and do not call `fog_transition_invalidate` per frame (fog has no history; TAA sees a slow content change, not a cut).
- **Camera correction**: the run197 `1e-3` Gram admission, true inverse and jitter/pixel-centre correction stay; only the origin representation changes as described above.
- **Composite and repair**: the composite keeps its qualified integer-pixel footprint, class-compatible weights and exact empty identity, but on `weight == 0` it returns the scene instead of marching. A third full-resolution draw (`fog_density_repair_ps`) recomputes the same five depth taps, `clip`s (and dynamically branches around the loop) where `weight > 0`, and otherwise marches and composites from the pristine `scratch_` copy. The transaction becomes march → composite → repair inside the same state capture; 8 pixel samplers (s7 = far atlas) and ≈4 more constant registers.
- **Slots (estimate)**: march ≈ 315 − 20 (old sampler) + 2×45 (two level samplers) + 30 (bins, LOD, taper) ≈ 400–450; composite ≈ 190; repair ≈ march + 60. All under 512 only if the single-loop form holds; the shader generator's slot count at checkpoint 2 is the fact. Registers: c0–c21 today plus fine/far `cam_local`, `1/delta`, chroma, readiness ≈ c22–c25.
- **Resolution**: keep the half-resolution march (640×384 at 1280×768, 245,760 pixels) and the full-resolution composite. Quarter resolution would need a new footprint law and re-qualification of the composite; interleaved or jittered sampling would push noise into TAA, which must not be weakened. Not selected.

## 4. Cost on the hot path and native behaviour

Measured today: 24 samples × 2 reads = 48 atlas reads per half pixel; whole transaction EVENT-completed median 1.084 ms / p95 1.173 ms at 1280×768 and 1.981 / 2.093 ms at 1920×1080 (production pass fixture; march share not split in the timing record); CPU submit median .132 ms; the user reports ≈2 FPS for this at 1280×768. Screen intent counts: 132 reads on a sky ray, 172 worst case at L = 29 300, dense-scan mean 133.9, geometry witness mean 108.

Estimates (not measured): 2.75× the fetches and ≈5× the march ALU (64 × ≈45 versus 24 × ≈20 instructions), so the march is 3–5× today's march; the repair draw adds ≈1 M pixels of prologue (five taps, ≈20 ALU) plus marches on the few repair pixels (322 across 32 reduced frames previously). Expected transaction 2–4 ms at 1280×768, i.e. roughly 4–8 FPS if the user's 2 FPS scales linearly. Proposed provisional timing gate for the fixture: completed median ≤ 3.0 ms and p95 ≤ 4.0 ms at 1280×768, ≤ 5.0 / 6.0 ms at 1920×1080, CPU submit ≤ .30 ms; the parent sets the final numbers and the user judges the flight FPS. Per-frame CPU work outside the GPU: one slab copy and `UpdateTexture` when a slab completes (bounded by the tile cap), zero when idle; the worker runs off the render thread. Loading: no blocking work; time-to-first-fog is logged as a new `volumetric_fog_cache` line.

Native Windows: only `CreateTexture` (SYSTEMMEM/DEFAULT), `LockRect`, `AddDirtyRect`, `UpdateTexture`, `CheckDeviceFormat`, `SetPixelShaderConstantF` and ps_3_0 are used; the thread is plain Win32. FP16 bilinear precision is not specified by D3D9, so the parity gate below carries margin; native execution stays unverified and is recorded in `platform-portability.md`.

## 5. Verification plan

- **Host (checkpoint 1)**: C++ generator versus `fog_density_runtime_screen.py`'s field on the screen's brick/slab node sets and 1,000 random signed keys with `O_s = 0` and one nonzero offset: hash corners exact, FP16 words equal on ≥ 99.99% with ≤ 1 half-ulp on the rest; RNE conversion versus NumPy exact; toroidal address/border/seam/lane laws (reuse the 12 cache-shift witnesses); throughput of a 32³ brick and the initial far+fine fill, host and as an x86 fixture under `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py …`, reported not gated.
- **GPU numerics (checkpoint 2)**: extend the detached `FogPass` fixture with host-generated static caches for the screen's pose A/B windows. Readback the half-resolution `(S,T)` target and the composite. Gates: GPU versus CPU float32 on the identical 24+40 grid and identical FP16 nodes, T max ≤ .001, S channel max ≤ 3e-5 (implementation parity); GPU versus CPU dense64 rho_hat, T p99 ≤ .002, max ≤ .003, S per channel .0005/.002; seven-shift camera translations, temporal residual ≤ .003; 5 witness rays × 7 depths, invalid 0/NaN/+inf identity, zero cache identity, lane 3→0 and seam-crossing rays, source alpha exact, exact empty-pixel identity through composite and repair.
- **State/recovery/timing (checkpoint 3)**: hostile state on 8 samplers and all constants, slab upload then origin advance with byte-identical unchanged pixels, not-ready ramps, real Reset with re-upload from retained caches, detach joins the worker, injected loss; paired transaction timing with the existing 16+64 protocol against the current pass.
- **Route (checkpoint 4)**: the 515-check route bridge with sector key → offset, cache publication, warmup and Reset; cross-compile with the standard SSE2/incoming-stack flags; selected host suite.
- The Wine runs prove API sequence, slot limits, filtering on the arm64/FEX translation, state restoration and Reset; they do not prove native Windows.
- **Single user flight** after the candidate: one accepted fogged sector, third-person and F8 first-person, a gate jump into a fogged sector (time until fog appears, no pop), a SETA burst (far-only fallback), Ctrl+Alt+F9 A/B with the FPS overlay, approach to a distant patch across 4–6 km (no LOD seam) and toward the 30–40 km taper (no edge). The user reports appearance versus the accepted images, FPS delta and any stutter; the session log supplies `volumetric_fog_frame` and `volumetric_fog_cache` lines.

## 6. Delivery order

1. Generator, address law, FP16 conversion, host tests, throughput microbenchmark (host + one Wine x86 run). Accept: word-identity gate passes; throughput numbers recorded in the ledger.
2. Shaders, generated `_inc.h`, detached fixture with static caches. Accept: all three programs < 512 slots; numerics gates above pass in one Wine run.
3. Cache manager, worker, uploads, readiness, Reset in the pass; state/recovery and paired timing fixtures. Accept: state checks pass; timing within the ratified gate.
4. Proxy integration (sector offset, ramps, launcher option, logging), route bridge, cross-compile, candidate build by the owner, one `--dry-run`, the flight above.

Out of scope: any change to the field, prefilter, spacing, LOD range, taper or sample counts; spatially varying chroma (the accepted images used constant chroma; the loss versus today's per-voxel chroma is a flight observation); volume-texture or GPU-bake paths; shipped per-sector bakes; quarter-resolution or temporally interleaved marching; shaft coverage beyond the current cascades; TAA changes.

## 7. Unknown, and what settles it

- Native x86 generation rate under FEX and on Windows: checkpoint-1 microbenchmark; Windows remains an estimate.
- Actual slot counts of the three programs: the shader generator at checkpoint 2.
- GPU transaction time and the FPS the user will see: checkpoint-3 timing, then the flight.
- Hardware FP16 bilinear precision on the 1032×516 atlas: the checkpoint-2 parity gate; native hardware unverified.
- Whether far-only interior during fine fill is acceptable: the flight's gate-jump and SETA observations; the plan-strict hold is the fallback.
- Constant chroma versus today's spatial chroma: the flight.

## Checkpoint 2 review carry-overs for checkpoint 3 (2026-09-21)

Checkpoint 2 (shaders, fragments, detached fixture) is accepted with non-blocking
findings. Orchestrator ruling: the in-scatter S gates are display-scaled like T
(p99 ≤ .002, max ≤ .003) because the host's frozen candidate already exceeds the
old .0005 gate; implementation identity rests on the texel-exact march
(T 1.07e-6 / 2.21e-6, S max 5.2e-7 against the host). Checkpoint 3 must:

1. Give the repair draw its own `c0` projection correction (`+0.5/W`, `−0.5/H`
   at full resolution); today it reuses the half-resolution quad-centre
   correction and shades half a pixel down-right of its depth tap.
2. Make the fixture's repair reference independent of that offset; the current
   check is tautological for it.
3. Put the production RGBA16F bilinear row and the temporal row into the PASS
   predicate, and encode the display-scaled S gates in `fog_density_shader_run.py`
   so `summary.json` no longer lists failing gates beside PASS; the bilinear
   parity-S figure (5.6e-5) is reported, not gated.
4. Rename `LodWeights::far` (collides with `windef.h`'s `far` macro).
5. Handle odd half-target sizes in the `sizes.zw - 1.0` footprint clamp.
6. Exercise the march's `fog_visibility` shaft path and add an explicit
   seam-crossing / lane 3→0 assertion.
7. Treat repair's 510 of 512 slots as the whole budget; fixture timing on this
   backend is not a GPU cost and must not be cited as performance.

**Checkpoint 3 outcome (2026-09-21).** All seven items are closed in the
[ledger entry](../verification/volumetric-fog.md#stored-density-runtime-integration-checkpoint-3-cache-manager-worker-uploads-ramps-reset-2026-09-21).
Item 1 closed differently from its wording: under the pass contract (`m20/m21` carry the
full-resolution quad term) the repair ray already passes through its depth tap, a CPU march
confirms it to 4.9e-4 and rejects a half-pixel shift at .0193, so no +0.5/W term was added.
As built, uploads use `UpdateSurface` rectangles (budget 1,065,024 B and 64 rectangles per
frame) rather than `AddDirtyRect` + `UpdateTexture`, and retargets trigger at 2 fine / 9 far
nodes off the window centre.

## Look presets L0-L3 (2026-09-21)

Spec: `fog-visual-direction-review-2026-09-21.md` sections 3-4, plus the later "visible bulbs" feedback
(rounded value-noise silhouettes where patches fade into void). One build, A/B in flight:
`--volumetric-fog-look {0,1,2,3}` (`X3M_VOLUMETRIC_FOG_LOOK`) sets the starting preset; with the stored range the
default is **2** in the launcher and in the DLL's own fallback (`fog_look_default`, since 2026-09-22; `--volumetric-fog-look 0`
selects the unshaped law, the legacy range has no look),
**Ctrl+Alt+F11** cycles it, the FPS overlay shows `FOG 1.50x L2`. Atlas format, cache, uploads and the
composite/repair split are unchanged.

| Preset | Law |
| --- | --- |
| L0 | Current law: the three base programs, byte-identical bytecode (march `4dacf7e4...`), rows c0-c24 only. |
| L1 "shaped" | Density remap `rho' = saturate((rho-c-dc)/(1-c))^p` (c .35, p 2: exact zero below the coverage, zero-slope toe) with sigma x8; coverage moved by `dc = .12 x mean of three parabolic-sine plane waves` along (1,-2,1), (2,1,-1), (-1,1,2) cycles per 65536 units (26756 units = 5.35 km each at 5000 units per km, world anchored, evaluated at the warped position) so an edge is not one iso-surface of the stored noise and no term is constant along a world axis; fetch-free domain warp of the lookup position (two octaves of a parabolic sine, 5 and 13 whole cycles per 65536 units so it is world anchored under the camera modulo, 1400 + 500 units, each axis driven by the two others); two-lobe phase `.7 HG(.75) + .3 HG(-.15)` per pixel; two-colour ambient `S += albedo x ambient x (1-T)`, ambient = lerp(away, sun-side, .5+.5 cos) with sun-side = family chroma and away = `.3 (chroma + chroma.brg)`, both x `.35 x mean(E/pi)`; albedo `lerp(chroma, 1, .5)`; tinted extinction `T_rgb = T^k`, `k = 1 + .6 (1-chroma)` in composite and repair (no new target); multiple-scatter lift `.5 x 1/4 x sum T (1-exp(-.5 sigma rho ds))` with shaft floor .5; shaft visibility floor .15 on the sun term (softer umbra); every ray, sky or geometry, ends at the column cap (`SKY_CAP`, 112500 units = 22.5 km) with a smoothstep fade that starts at `TAPER_START` (65000 units = 13 km), so a distant hull and the sky beside it agree. `--volumetric-fog-anisotropy` has no effect. |
| L2 | L1 + one far-level tap toward the sun at 3000 units standing for 9000 units of path: Beer `exp(-3 tau)` and powder `1 - .5 exp(-2 (3 tau + 3 sigma rho 3000))`. |
| L3 | L2 + per-pixel per-frame sample offset: interleaved gradient noise on the half-resolution pixel, shifted by `5.588238 x TAA jitter index`, +-half a bin on all 64 bins. Repair pixels keep bin centres. |

Every scalar is `FogLookTuning` (`src/renderer/fog_look_math.h`), overridable once at init by
`X3M_FOG_LOOK_<NAME>` with NAME one of `COVERAGE`, `EXPONENT`, `SIGMA_SCALE`, `COVERAGE_VARIATION`,
`WARP_CYCLES_NEAR`, `WARP_NEAR`, `WARP_CYCLES_FAR`, `WARP_FAR`, `FORWARD_G`, `FORWARD_WEIGHT`, `BACK_G`,
`ALBEDO_WHITE`, `AMBIENT_GAIN`, `EXTINCTION_TINT`, `SCATTER_LIFT`, `LIFT_FLOOR`, `SHADOW_FLOOR`, `SKY_CAP`, `TAPER_START`,
`SELF_SHADOW`, `POWDER`, `TAP_DISTANCE`, `TAP_LENGTH`, `JITTER_NEAR`, `JITTER_FAR` (floats, ranges in
`fog_look_fields`), plus `X3M_FOG_LOOK_AMBIENT_SUN` / `_AWAY` = `r,g,b` in 0..4; out-of-range values keep
the default and the session log prints the resolved set (`volumetric_fog_look_mode`). The launcher
passes the inherited variables through.

Fade range. `X3M_FOG_LOOK_TAPER_START` (0-199000) and `X3M_FOG_LOOK_SKY_CAP` (20000-200000) are the start and end of the
distance fade in render units (5000 per km); a start later than cap - 1000 falls back to the last quarter of the
cap. To compare in one flight: 14 km `TAPER_START=52500 SKY_CAP=70000` (the Run 61 law), 22 km the default
`65000 / 112500`, 35 km `150000 / 200000` (the L0 range). The 64 bins stay 24 over [0,12000] and 40 over [12000,cap]:
far bins are 2512 units at the default, 4700 at 200000 (coarser than the 4096-unit far node; numbers in the ledger).

Implementation. `FOG_LOOK` 1 and 2 variants of march and repair plus one composite variant (five programs,
`src/fog/fog_density_*_look*_ps.hlsl`), created in `FogPass::density_resources` with the base programs and
released with them; pixel shaders survive Reset. A frame carries `FogFrame::look` and `look_phase`;
`execute` fills rows c25-c35 with `fog_look_constants`, multiplies c2.w and picks the variant: no creation,
allocation or lock on the hotkey or draw path, and the D3DSBT_ALL block restores the rows. If the variants
cannot be created the base path stays and every frame draws L0 (`volumetric_fog_look_refused`, once).

ps_3_0 budget. CrossOver reports `MaxPixelShader30InstructionSlots = 512`, so every variant has to fit the
base ceiling. Microsoft-table slots / static texture instructions: march L0 415/17, L1 349/13, L2-3 422/15;
repair L0 510/22, L1 450/18, L2-3 510/20; composite 203/10 and 210/10. The 64-bin march stays one `rep`
loop in every program. What paid for the look terms: (1) the look programs read **two** shaft cascades (the
two coarsest current maps, compacted into slots 0-1 by `execute`) with a hard switch at the blend-band start
and one 2x2 comparison, instead of three with cross-fade (207 slots in L0); (2) `level_sample` interpolates Z
with a tent over the four lanes of one texel plus lane 0 of the next group: same texels and weights, smaller
body. Fetches per non-empty sample: L0 4 atlas + up to 12 shaft; L1 4 + 4; L2/L3 6 + 4. More exactly-empty
samples (45-69 % of the fixture's sky rays end exactly empty) skip the shaft and tap reads entirely. This D3DX compiler emits a
truncated program without an output write when the second lane fetch is made conditional, so it stays
unconditional; a second sun-ward tap needs an inner loop that puts repair L2 past 512.

L3 caveats. Repair pixels (depth-class edges with no compatible half sample) march bin centres while their
neighbours are offset per frame: after the TAA resolve they hold the mean the neighbours converge to, but a
one-pixel outline can show while history is short (cuts, fast pans). With TAA off the offset phase is held
at 0, so L3 is a static dither rather than an animated one. A two-cascade cross-fade (.85 to .95 like the base
law) was compiled and measured at 546 slots for repair L2 against 503 with the hard switch (510 since the 2026-09-22 coverage waves): it does not fit.

Not done, by decision or cost: edge erosion from the fine grid at another scale (a scaled lookup of a
toroidal *window* is not a periodic field: it shows the window seam and pops as nodes are replaced) and a
generator-side recipe with warped or gradient-noise octaves (the right fix for sub-400 m structure; needs a
new recipe id, host twin and fixtures). Cascade cross-fade inside the look programs. Dust motes.

