# Engine trail origin: where a Particles3 trail particle is placed, and where to move it

2026-10-14. Question: where does a ship's engine trail (the `types/Particles3` generator at the engine emitter,
TShips column 49, [engine-effects.md](engine-effects.md) §3 and §7) take the world-space origin and direction of a
newly spawned particle, and is there one per-frame site where the proxy could move that origin along the nozzle axis
by a per-emitter length (the end of our drawn plume) without touching the particle buffer? Symptom (user): when a ship
moves or turns, our plume is straight behind the nozzle but the trail ribbon curves away from the nozzle itself, so
plume and trail both start at the engine; the trail should start where the plume ends.

Method: native `X3AP.exe` at `/mnt/c/X3/X3AP.exe`, SHA-256 `fdbf3418…f8ab` (the same build as engine-effects.md);
Ghidra 12.1.4 project `~/x3-ghidra-research` (`X3DecompileFunctions.java`,
[`X3DecContaining.java`](../../verification/results/engine-effects/X3DecContaining.java)) and a local
`i686-w64-mingw32-objdump -M intel` listing; raw decompiler output and the listing stay in `~/x3-nav-study/trail/`
(untracked). Byte checks: [`trail_origin_sites.py`](../../verification/results/engine-trail-origin/trail_origin_sites.py)
→ [`trail_origin_sites_out.txt`](../../verification/results/engine-trail-origin/trail_origin_sites_out.txt),
**18/18 PASS** plus the E8 callers of the four functions (0.04 s). No game launch, no Wine, no build.
Tags: **[s]** static, instruction bytes read in the listing (and, where the address is in the script, byte-checked);
**[m]** measured from data or captures; **[i]** inferred (decompiler reading not checked instruction by instruction,
or reasoning); **[u]** not established.

## Answer

1. **The visible ship trail under `engine_effects = plumes` (the default, and the user's ini) is not the game's.** Site
   B (`0x0041482c`) skips the trail generator for class 7, so ships have no Particles3 trail at all
   ([engine-effects-modern.md](../architecture/engine-effects-modern.md) Decision; `engine_effects_patch.cpp` stub B)
   [s]. The curving ribbon on ships is the proxy's own ribbon (`src/proxy/engine_ribbons_core.h`): its head and every
   sample are `rec.origin`, the nozzle position (`update()`: the jump test, `estimate_speed`, the append test, the
   sample write and `r.head`; `build`: `world[0] = r.head`) [s: source]. That is exactly the symptom: the plume runs
   along `rec.axis` from the nozzle, the ribbon follows the path the nozzle took. **The fix is in the proxy, not in the
   engine** (section 6). Missiles (class 10) are the exception: stub B forwards them, so a missile keeps its native
   trail beside a proxy plume, and the native findings below apply to it.
2. **The native trail never reads the emitter node's world matrix.** A trail particle's position is
   `Hermite(root position path, u) + slerp(root orientation, u) · gen+0x10`, where `gen+0x10` is the emitter offset in
   the ship's model frame (the jet's part translation × the root's per-axis scale, unrotated), refreshed every frame by
   site B; `u` places the particle at its own spawn time within the frame. The direction is the **ship's** −z (row 2 of
   the interpolated root rotation), not the jet node's axis [s, `0x0046c5c2..0x0046c840`].
3. **Hook site for a native shift: the existing stub B entry** (callee entry of `0x00412d70`): `[esp+0x10]` = `&pos`,
   a dead 16-byte caller local; EBX = the jet node. Subtracting the plume length along model −z from `pos` before
   forwarding moves every subsequently spawned particle of that emitter, with the engine's own interpolation, without
   touching the particle pool [s for the contract, i for the effect].
4. **No live anchor.** The vertex fill reads only particle records; nothing draws from the live emitter to the newest
   particle [i]. Mayhem's ship rows are `PEDF_2D` billboards (no `PEDF_TRAIL`) [m], so the native "trail" is a chain of
   overlapping puffs, each fixed where it spawned.
5. **Spacing is spatial, not temporal:** at speed the gap between two particles ≈ `Density · size`, independent of
   speed; size rises with throttle. A shift applied at spawn needs no velocity term [i].

## 1. Objects and layouts

### Link and generator [s: `0x00412d70` listing; field roles i where marked]

Link (`0x14` bytes, list `*0x00607cf4`): `+0` next, `+4` prev, `+8` generator, `+0xc` k (ordinal of the root's JET
children, engine-effects.md §7), `+0x10` obj. **The generator has no node pointer**; its identity is (obj, k).

| generator (`0xb0` bytes) | content | written by |
| --- | --- | --- |
| `+0x00/+0x04` | list links in `*(0x00608518)+0x6318` (sentinel `+0x631c`, tail `+0x6320`) | create |
| `+0x08` | registered handle (`0x004efcc0`) | create |
| `+0x0c` | Particles3 id (type `*(0x00608518)+0x6328 + id·0x10`) | create |
| **`+0x10..+0x1c`** | **emitter offset**, ship model frame, engine units, root scale applied, unrotated (`pos` of site B; `pos.w` uninitialised stack) | create and **every call of site B**: `0x00412e3c..0x00412e56` [s] |
| `+0x50` | speed ratio 16.16 = `obj+0x10 / (vmax · (1 + engine bonus))`, unclamped | `0x00416d1a` every frame [s] |
| `+0x60..+0x6c` | root world position (`root+0x30`) at the last emit | create; `0x0046c98e` [s] |
| `+0x70..+0x7c` | root velocity (`root+0x100`, units/s, the forward velocity without slip; [ship-movement.md](ship-movement.md)) at the last emit | `0x0046c9a8` [s] |
| `+0x80` | time of the last emit (ms, `*(0x00606f34)+0x718`) | create; `0x0046c986` [s] |
| `+0x90..+0x9c` | root orientation quaternion at the last emit | create (`0x004f2470`); emit epilogue [s] |
| `+0xa0` | render context = `root+0x1c` | `0x00416d39` every frame [s] |
| `+0xa4` | per-emitter-definition state array, stride `0x20`: `+0` definition, `+4` last spawn time, `+0x10..+0x1c` last trail point (`PEDF_TRAIL` only; zeroed when no camera is in range) | create; emit [s] |

Particles3 definition (stride `0x80`, `*(0x00608518) + (id + 0x633)·0x10`, count at `+0x632c + id·0x10`) [s
offsets, i names from the loader order]: `+0` MatID, `+4..+0x10` uv rect, `+0x14` flags (`PEDF_2D 1`, `PEDF_TRAIL 2`),
`+0x40` Density, `+0x44` RndVariation, `+0x48/+0x4c` Min/MaxSpeed, `+0x50` Lifetime, `+0x54/+0x58` Min/MaxSize,
`+0x5c` lifetime variation, `+0x60/+0x64` lateral speed spread, `+0x6c` fade duration.

### Particle pool [s for the list offsets; i for field meanings]

One global pool at `*(0x00608518)`: free list `{+0x62ec first, +0x62f0 sentinel, +0x62f4 tail}`, live list
`{+0x62f8 first (oldest), +0x62fc sentinel, +0x6300 tail}`. A spawn takes the free head, else recycles the oldest live
particle when its state is 4 (`0x0046c460..0x0046c48a`); otherwise the spawn is dropped. Particle fields: `+0x10..+0x1c`
position (engine units), `+0x20..+0x2c` previous trail point (`PEDF_TRAIL`), `+0x30..+0x5c` three orientation rows
(non-2D only), `+0x60..+0x68` velocity, `+0x70` render context, `+0x74..+0x84` MatID and uv rect, `+0x88` flags `& 3`,
`+0x8c` spawn (then last-move) time, `+0x90` state, `+0x94` size, `+0x98` death time, `+0x9c` rotation, `+0xa0` fade.
The particle carries no generator, obj or node reference, only the render context; that is why the Particles3 VB is not
separable per ship (engine-effects-modern.md).

## 2. The per-frame chain

| step | address | what [tag] |
| --- | --- | --- |
| a | main loop `0x00403b17` → `0x0043a360` → … `0x004596e0` → `0x0045ac7d` → `0x00414590` | per ship: the JET child walk computes `pos` (engine-effects.md §7 "`&pos`") and calls site B `0x0041482c` → `0x00412d70`, which refreshes `gen+0x10..+0x1c = pos` [s] |
| b | main loop `0x00403f2a` → `0x00416750`, `0x00416c77..0x00416dc4` | when `VideoD3DFlags & 0x40000000`: per link, `gen+0x50` = speed ratio, quaternion of `root+0x40` (the root's rotation rows, `0x00416d28`) into a local, `gen+0xa0 = root+0x1c`, rotation term `(|obj+0x24| + |obj+0x28| + |obj+0x2c|) · 6·obj+0xa4` (16.16; angular rates × object size [i]), then **`0x0046c170(gen, root+0x30, &quat, root+0x100, obj+0x10, rot, now)`** at `0x00416db8` [s] |
| c | `0x0046c170` (`__stdcall`, 7 args, `ret 0x1c` at `0x0046c9ed`) | per definition: spawn particles for the elapsed time (section 3) [s] |
| d | `0x00416dd6` → `0x0046bec0` | every live particle: `pos += vel · dt / 1000` (also the trail point), fade, state 4; expired ones to the free list [i] |
| e | `0x00472141` → `0x004be7d0` (sole caller) | vertex fill: walks the live list, keeps particles whose `+0x70` is the view's context, writes 6 vertices of `0x20` bytes each, positions = particle ints × the context's float `+0x2c` (render scale, 0.01 measured on run406, [engine-nozzle-source.md](../architecture/engine-nozzle-source.md)) [i]; drawn by `0x004bf4c0` |

Steps a and b both run in one main-loop iteration with a before b (`0x00403b17` precedes `0x00403f2a` in `0x00403840`
[s]; that both execute in every frame is [i]). `0x0046c170` has a second caller, `0x004785b5` (the other particle
sources) [s].

## 3. Spawn position and direction (question 2)

Inside `0x0046c170`, per definition k (state `S = gen+0xa4 + k·0x20`) [s unless marked]:

1. `dt = now − gen+0x80`, clamped to 0..1000 ms (`0x0046c1da..0x0046c1f5`).
2. `size = lerp(MinSize, MaxSize, ratio)`; initial speed `v0 = lerp(MinSpeed, MaxSpeed, ratio)`, 0 for `PEDF_TRAIL`
   (`lerp` is `(a+b)/2 + (b−a)(ratio − 0.5)` in 16.16, ends clamped) [i].
3. Camera gate: a camera node of the context (`+0x270 & 0x4001`) within `size · 1024` units (Chebyshev); otherwise
   `S+0x10..+0x1c` is cleared and nothing spawns [i].
4. Interval: `I = Density · (size · 1000 / (|speed| + v0 + rot)) · (1 − RndVariation · rand)` ms
   (`0x0046c374..0x0046c43d`; `0x00469b70` is `abs(EAX)`); while `now − S+4 > I`: spawn one particle at
   `t = S+4 + I` (not earlier than `gen+0x80`) and set `S+4 = t`.
5. `u = (t − gen+0x80) / dt` (16.16): the particle's place within this frame (`0x0046c598..0x0046c5c0`).
6. **Position**: tangents `T0 = gen+0x70 · dt / 1000`, `T1 = root+0x100 · dt / 1000` (`0x00469ad0`, `ret 8`);
   `P = 0x004f2c80(gen+0x60, root+0x30, T0, T1, u)`, a cubic Hermite with `h00 = 1 − (3u² − 2u³)`,
   `h01 = 3u² − 2u³`, `h10 = u³ − 2u² + u`, `h11 = u³ − u²` (coefficients read at `0x004f2c92..0x004f2d12`) [s];
   `q = slerp(gen+0x90, quat_now, u)` (`0x004f2920`) → rotation rows `R` (`0x004f2300`) [s for the calls, i for the
   maths]; `O = gen+0x10 · R` (`0x004f0da0`: `O[j] = Σ_i off[i] · R[i][j]`, 16.16) [i]; **particle `+0x10..+0x18` =
   `P + O`**, `+0x1c` = `P.w` (`0x0046c80f..0x0046c83d`) [s].
7. **Direction**: velocity `+0x60..+0x68 = −R[2] · v0`, plus `rand`-weighted spreads along `R[0]` and `R[1]`; zero for
   `PEDF_TRAIL` (`0x0046c653..0x0046c7ef`) [s for the row index, i for the spreads]. Orientation rows copied to
   `+0x30..+0x5c` unless `PEDF_2D` [s].
8. `PEDF_TRAIL`: `+0x20..+0x2c` = the previous `S+0x10` point (or the new position when that was ~0), then
   `S+0x10 = ` the new position (`0x0046c846..0x0046c8da`) [s].
9. Epilogue (`0x0046c97d`): `gen+0x80 = now`, `gen+0x60 = root+0x30`, `gen+0x70 = root+0x100`, `gen+0x90 = quat_now`.

So the origin is the **root's** interpolated pose plus the jet's part offset; the jet node's own matrix, rotation and
z-scale are never read here. For the 231 installed glow parts with a key rotation (engine-effects.md §6) the native
trail still leaves along the ship's −z [i].

## 4. Hook sites (question 3)

| candidate | contract | verdict |
| --- | --- | --- |
| **H1: stub B entry**, the callee entry of the redirected `call 0x00412d70` at `0x0041482c` (`engine_effects_patch.cpp`, `x3m_engine_effects_stub_b`) | Whole-instruction boundary (the stub runs after the complete 5-byte call). Entry: `[esp+4]` k, `[esp+8]` Particles3 id, `[esp+0xc]` obj, `[esp+0x10]` **`&pos`** (the caller's `[esp+0x20]`, 4 dwords, dead after the call, engine-effects.md §7) [s]. **EBX = the jet node** (live across, must be preserved), EBP frame, ESI/EDI callee-saved; EAX (= id), ECX (= obj), EDX (= k) and EFLAGS dead after the return [s, §7]. ESP `16n + 12` at entry (`-mincoming-stack-boundary=2`). `__stdcall`, the callee pops `0x10`. No x87 live in the caller (§7). One call per emitter per frame from this site; not re-entered (§7 call graph) [s] | **Use this one.** Write `pos.z −= L_e` (unrotated parts) in place, then `jmp` to the original callee; the generator copies it to `gen+0x10` (refresh `0x00412e45..0x00412e56`, or at creation) and the emit later in the frame rotates it with the interpolated pose. Changes EFLAGS only besides the dead stack local; preserve EAX/ECX/EDX anyway since the callee reads its arguments from the stack only |
| H2: `call 0x0046c170` at `0x00416db8` | `[esp+4]` = gen (EDI), ESI already overwritten with obj (`0x00416d9b`); the link is only in `[esp+0x20]` of the caller [s] | Worse: gen carries no node, k → node needs a second child walk; also shared particle maths |
| H3: inside `0x0046c170` after `0x0046c803` (`[esp+0x40]` rotated offset, EBX particle) | mid-function, shared with `0x004785b5` (weapons, other sources) [s] | Reject |

What the proxy needs at H1 [i]: the class gate (`obj+0x48`), the emitter node (EBX) and a length in engine units.
`L_e` can be computed in the stub from the node alone: the plume law is `L = max(z, idle_length) · value` with
`z = node+0x88 / 65536` (`engine_plumes_core.h`; drop the per-nozzle pulse, which is the stage's noise) and `value` in
record units (render), converted with the render scale (engine = render / 0.01). Alternatively the plume stage publishes
`L` per node identity (`node+0x28` handle + `node+0x140` model, the ribbon key) one frame late; the stub then looks it
up by EBX. Whether the jet drive writes `node+0x88` before `0x0045ac7d` in the same pass is [u]. Rotated parts need
the nozzle axis in the ship model frame: the record's world axis × the transpose of the root rows `root+0x40` [i]; the
native direction stays the ship's −z either way.

Scope: for ships this only matters if native trails are ever re-enabled (today stub B skips class 7 and returns before
any of this); for missiles it is the stub's forward path. Other readers of `gen+0x10` were not searched: the per-view
generator walk `0x00473e10` (engine-effects.md §7) reads generator fields and was not traced field by field [u].

## 5. Anchor and speed (questions 4 and 5)

**Anchor [i].** `0x004be7d0` reads the live particle list and particle fields only (position, trail point, rows, size,
fade, material); it reads no generator and no node. A `PEDF_TRAIL` particle draws the segment from its spawn position to
the previous spawn position (`+0x20`), so the newest segment ends at the newest particle, up to one interval behind the
emitter; nothing is anchored to the live emitter. A shift at spawn (H1) is therefore complete: no second anchor exists.
Mayhem's ship rows are `PEDF_2D` without `PEDF_TRAIL` (e.g. id 34: two emitters, MatID 406, density 0.4/0.38, speed
10–1,000, life 1,500/750 ms, size 4,000–12,000 / 1,429–4,286) [m, `engine_effects_data_out.txt`,
`particles3_materials_out.txt`]: camera-facing puffs with a random rotation, each drifting at `v0` ≤ 1,000 units/s
(2 m/s) along the ship's −z of its spawn moment.

**Speed [i].** From step 4: spacing in distance `d = I · |speed| / 1000 = Density · size · |speed| / (|speed| + v0 + rot)
· (1 − rnd)`, so at cruise `d ≈ Density · size`: id 34 at full throttle ≈ 0.4 × 12,000 = 4,800 units ≈ 9.6 m, about
2.5 overlapping puffs per size. Size grows with the speed ratio, the interval shrinks with speed, the trail length is
lifetime × speed (1.5 s). Because each particle is placed with its own interpolated pose (`u`), an offset added to
`gen+0x10` lands at the plume end of that moment: **no velocity term is needed**. The only lags are `L` itself (one
frame if the stage publishes it; the throttle is rate-limited to 0.004 per ms, engine-effects.md §2) and up to one
spacing between the plume end and the newest puff. During a hard turn the puffs follow the path of the plume end, which
sweeps faster than the nozzle; that is the intended look.

## 6. Re-create in the proxy instead (the actual fix for ships)

Ships already have it: the ribbon of `engine_ribbons_core.h` is the trail, and its native counterpart is suppressed.
The change is local to that file [i, recommendation]: sample and draw the ribbon from the plume end
`E = rec.origin + rec.axis · L̄` instead of `rec.origin`, with `L̄ = max(z, idle_length) · plume value · travel length`
(the plume's length without the pulse, the same floored value the plume uses), i.e. in the append test, the sample
write and `r.head`. Keep `rec.origin` for the jump test and for `estimate_speed` (the ribbon's length law is a function
of the ship's speed; the end of a long plume sweeps faster in a turn and would lengthen the ribbon). The ribbon's head
width may then follow the plume's tip width rather than the nozzle's. No engine hook, no Wine-specific path, and the
particle VB stays untouched (it cannot be filtered per ship anyway). Missiles would need H1 or the same ribbon.

Revised 2026-10-14 after Run 170 (flown: "plume ends, then empty space, then the trail starts"). Two causes, read from
`engine_plumes_core.h`: the plume's axial alpha (`law::tail`, the pixel program's fade) starts falling at u 0.4 and
reaches 0.1 of its peak at u 0.848, so the visible plume ends near 0.85 L̄, not at L̄; and in chase view
`build_nozzle` shrinks the plume about its nozzle by the near-camera factor k (0.28 in the harness's chase case),
while E stayed at the uncapped L̄. The ribbon is now anchored inside the plume and fades in under it, as modern
engines attach thruster trails: `A = rec.origin + rec.axis · 0.6 L_vis` (`L_vis = u_vis L̄`, u_vis from the tail law
at 0.1 of its peak, so A = 0.509 L̄ at the default look) replaces E in the append test, the sample write, the drop
rule and `r.head`; the head's half-width is the plume's at A (`law::width`, 0.226 value). The strip's radiance ramps
from 0 at the head to full at L_vis, or earlier where its centre line leaves the plume's visible body (a hard turn
carries it out through the side). For the cap the builder evaluates build_nozzle's k in its own view (the same
arithmetic, `near_cap_factor`, checked against build_nozzle in the host harness) and draws only the live head at
`origin + axis · k A`, the fade-in × k; the samples keep their world positions, so the per-view factor does not move
them with the camera. The plume's pulse (L × 1 ± 0.25) is not followed: at its minimum the visible end lies where
the ribbon's fade-in is 0.375 [i].

## Unknown

- Whether `0x004596e0` writes `node+0x88` before the `0x0045ac7d` call in the same pass (decides whether a stub-side
  `L_e` is this frame's or the last) [u].
- The exact maths of `0x004f0da0`/`0x004f2920` (register arguments; the decompiler drops them) beyond the call
  contracts read in the listing [i].
- The jet node's local rotation field (for rotated parts at H1) [u].
- That the user's curving ribbon is the proxy ribbon rests on the `engine_effects` default (`plumes`, unset = plumes in
  `engine_effects_patch.h`) and the user's `C:\X3\x3m.ini` line being commented [s]; no launch or capture confirms it,
  and no ini switch for the ribbons alone was found [u].

## Reproduce

```sh
python3 verification/results/engine-trail-origin/trail_origin_sites.py            # 18/18 PASS, E8 callers
JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64 flock /tmp/x3-ghidra.lock \
  ~/ghidra_12.1.4_PUBLIC/support/analyzeHeadless ~/x3-ghidra-research X3Render \
  -process X3AP.exe -readOnly -noanalysis -scriptPath tools/analysis \
  -postScript X3DecompileFunctions.java <out> 00412d70 0046c170 00416750 004bf4c0 \
  004f2c80 004f0da0 004f2920 004f2300 00469b70 00469ad0 004f2470 0043a690
# X3DecContaining (verification/results/engine-effects) on 46bed0 4be940 4bea44: 0x0046bec0, 0x004be7d0
# listing windows: 0x00416c40..0x00416dd0, 0x0046c170..0x0046c9f0, 0x00412d70..0x00412f7a, 0x004f2c80..0x004f2e00
```
