# Bloom replacement: bounded late-view state analysis

2026-09-13. This narrows one open item in
[bloom-compositor-skip.md](bloom-compositor-skip.md): whether ordinary views
after the compositor consume its inherited render states. **The first captured
late GUI draw is insensitive to the listed separate-alpha changes; alpha-test
equivalence is conditional on the actual boundary state, which is unrecorded.
Arbitrary later materials remain unqualified.** No replacement was implemented or run.

Evidence: read-only Ghidra inspection of the existing X3AP project; targeted
in-memory parsing of `shader/3_0/{bloom,gui2d,effects}.fb` in root and addon
`01.cat`; and queries over the existing
`verification/results/game-flight-capture-summary.json`. Private derivations,
scripts and assembly are under `/tmp/x3-bloom-skip/`. No Wine/game/build/install
command was executed. Executable provenance remains as recorded in
[compositor-and-glow.md](compositor-and-glow.md).

## Ordinary path before its first material draw

After `0x004721b1`, the ordinary-view path calls camera setup `0x0047c840`
and then geometry collection/submission helpers. Camera setup reaches
`0x004be520` (matrices/viewport), optional Clear, and `0x0047c640` (light-list
selection). It is not a general render-state reset. `0x0047e620` sorts the
collected list; `0x0047e6e0` traverses it, prepares the object's transforms and
calls `0x004c4fc0`, which ultimately calls the material routine `0x004c0150`.
The environment-map branch's explicit `0x004b9660` reset is not universal to
this ordinary path.

The material routine binds indices through manager slot `0x68` at `0x004c0481`,
and its declaration through slot `0x60` at `0x004c0be5`. It selects its own
effect technique, begins with `D3DXFX_DONOTSAVESTATE`, then reaches `BeginPass`
at `0x004c3ff6` and the render object's draw dispatch at `0x004c403c`.
Thus render-state coverage depends on the selected effect/pass, not just the
camera or material function having run.

The final material-side glow branch at `0x004c3e06..0x004c3fc5` controls colour
mask and separate-alpha state conditionally. With glow/capability available,
`view+0x270 & 0x200000` clear branches straight past that setup at
`0x004c3e3a`; it does not unconditionally repair inherited blend state.
The applicable subpaths can set mask 15, ZERO/ZERO alpha factors and separate
alpha true, or mask 7; the unavailable-glow path sets mask 7 and separate alpha
false. Effect `BeginPass` follows and can override these settings.

## What frame 5449 draw 99 proves

The captured original compositor occupies draws 95–98. Draw 99 has VS
`f36fc43f30b19d71`, PS `0a523f33ac47ae05`, 10 triangle-strip primitives and
main RT0. This pair matches `gui2d.fb`, technique `INSTANCE`, pass `P0` in both
inspected archives. The root/addon effect files have different complete hashes
but agree on this shader pair and the relevant pass-state records; executable
identity alone must not be treated as effect-state provenance.

| State | Stock bloom DEFAULT FinalCombine | Stock gui2d INSTANCE/P0 | Consequence for draw 99 |
| --- | --- | --- | --- |
| Alpha test enable | Explicit false | Not assigned | INSTANCE sets ALPHAFUNC=GREATEREQUAL (7), reference 1. Arbitrary inherited enable values are not equivalent; the last observed pre-compositor draw had enable=false, but boundary state is unrecorded. |
| Alpha function/reference | Not assigned | GREATEREQUAL / 1 | With alpha test enabled, zero-alpha fragments can be rejected and RGB coverage can differ. |
| Colour mask | Explicit 15 | Explicit 7 | GUI writes RGB only; incoming alpha blend state cannot alter its stored alpha. |
| Separate alpha blend | Explicit true | Not assigned | Captured true is inherited; restoring pre-bloom false is harmless for this RGB-only draw. |
| Alpha blend factors/op | ZERO / ZERO / ADD | Only alpha op ADD assigned | Factors are irrelevant while alpha writes are masked. |
| RGB blend | Enabled, ONE / INVSRCCOLOR, ADD | Enabled, SRCALPHA / INVSRCALPHA, ADD | GUI establishes its own complete RGB equation. |
| Z enable / write | Z enable false; write not assigned | Both false | GUI establishes both. |
| Cull | NONE | NONE | GUI establishes its own mode. |
| Fog / sRGB write | Neither assigned | Neither assigned | Stock bloom does not introduce a difference here; exact incoming restoration preserves their pre-bloom state. |

The capture corroborates the narrower inherited-state fact: draw 94 has
`SEPARATEALPHABLENDENABLE=0`, draw 98 has 1, and draw 99 still has 1. Masks
are respectively 7, 15 and 7. This difference is **not** evidence of a visible
draw-99 error: separate alpha processing changes alpha, and mask 7 prevents
alpha writes. The draw's RGB factors also do not read destination alpha. Separately, alpha
test enable is 0 in draws 94, 98 and 99. Draw 94 is the last observed
pre-compositor draw, not a boundary-state capture: EndPass/End and later
helpers (`0x004bf4c0`, `0x00489bf0`, `0x004715d0`) may run before the next
iteration reaches `0x004721b1`. Their intervening alpha-test writes have not
been inventoried. Equivalence is therefore conditional on boundary alpha test
being disabled; direct boundary-state tracing or targeted helper inspection
must establish it. GREATEREQUAL is 7; ALWAYS would be 8.

Both sampled flight frames 5449 and 5652 have 24 draws after the four-pass
compositor: two draws with this INSTANCE pair and mask 7, nine with gui2d's
DEFAULT pair and mask 7, and thirteen with a third pair and mask 15. This is
useful sample coverage, not a census of all late-view materials or scenes.

## What remains open

`effects.fb` technique `INSTANCE_BULLETS` declares colour mask 15 and omits
separate-alpha enable and factors (it sets alpha blend op). Its stored alpha
can therefore depend on inherited state. This is a **static uncovered path**;
it is not identified as frame 5449 draw 99, and is not proof that a particular
captured late draw used that technique. Do not generalize the GUI mask-7
argument to mask-15 materials or to later draws using destination alpha.

The stock bloom final pass additionally writes alpha with ZERO/ZERO factors;
a replacement retaining scene alpha can leave different destination alpha even
when its RGB is correct. Later alpha writers/readers and final presentation
alpha need their own policy and verification. This bounded study establishes
neither full-frame alpha equivalence nor that all remaining inherited states
are irrelevant.

Before enabling skip broadly, identify actual late techniques and compare their
explicit assignments against the stock compositor's mutations. Admit only
covered combinations or provide a validated state transition that maintains
both device state and game-cache coherence. Modified effects require renewed
semantic/capability admission, not hardcoded DLL or effect hashes as the normal
feature gate. A focused fixture should seed distinct incoming alpha-test,
separate-alpha and colour-mask states; prove the GUI case's separate-alpha
invariance, alpha-test dependence for zero-alpha fragments, and a mask-15
case's separate-alpha dependence. Native Windows execution remains unverified.

## Render-state cache correction

The earlier statement in [constant-uploads.md](constant-uploads.md) that pure
`SetRenderState` “does not memoize” was incorrect. Targeted assembly establishes:

- `0x004b4f80` passes the render-state key, desired value and manager tree at
  `this+0x10` to `0x004b5620`.
- After lookup, `0x004b5663` compares the existing node's value at `node+0x10`
  to EDI (desired value). Equality returns AL=0 at `0x004b5668`; a difference
  stores the new value at `0x004b5670` and returns AL=1. Missing entries are
  inserted with the desired value.
- The caller tests AL at `0x004b4f99` and skips the device call on zero at
  `0x004b4f9b`. Only the changed-state path reaches device `SetRenderState`
  at `0x004b4fab`.

The cache update precedes the device call. This is evidence about the game's
existing behavior, not a recommendation to duplicate it or write its private
tree. Replacement draws must restore every directly changed cached render
state, as well as shaders/declaration/streams/constants/textures. Arbitrary
stock-residue emulation through raw device calls can leave the tree stale.

## Reproduction and evidence limits

Additional Ghidra specs (same read-only command as the compositor note):
`dec:0047e780 dec:0047e920 dec:0047e620 dec:0047e6e0 dec:004892a0
dec:0047bc20 ins:004c0150 ins:004b4f80 ins:004b5620 dec:004c4fc0`.
Outputs: `late-material.txt` and `late-cache-proof.txt` under the private path.

`effect-state-inspect.py` and `effect-states.json` there hold the targeted
archive parsing. State operation names were independently mapped from the
installed native `d3dx9_37.dll`'s 32-byte state-name records beginning at file
offset `0xa160` (operation 0 ZENABLE; 146/147 VS/PS agree with the existing
parser). This was read-only analysis of a local DLL, not a runtime dependency
or backend-private renderer contract. Parameter-backed states must not be
interpreted as fixed scalar literals from their initial-value record; the
draw-99 qualification above uses the inspected INSTANCE pass's static states.
No state-table bytes, effect bytes or decompiler output are checked in.

Independent Sol/xhigh review verified the cache, capture and effect mapping;
it corrected the initial alpha-function enum interpretation (7 is
GREATEREQUAL, not ALWAYS). The alpha-test conclusion is restricted to the
unverified condition that alpha test is disabled at the actual boundary,
rather than treating the last pre-compositor draw as a boundary snapshot.
