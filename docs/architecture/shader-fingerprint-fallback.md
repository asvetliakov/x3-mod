# Shader fingerprint fallback

Design note, 2026-09-17, **ratified by the orchestrator 2026-09-17** (one fallback tier: literal-blind, comment-free fingerprint; anything looser refused; implementation not yet scheduled). Question: when a program's exact
hash misses every transform table, how can a structural fingerprint still
bind the reviewed transform to a recompiled or lightly edited program without
weakening the fail-closed property described in
[mod-compatibility.md](mod-compatibility.md)? No code, Wine or game was used;
the numbers below come from a host script over the local corpus
(`/tmp/x3-shader-sweep/programs`, 751 programs, untracked).

## Recommendation

Add one fallback tier, **layout-preserving literal-blind match**, and no other:

1. **Fingerprint** = FNV-1a 64 over the program with every comment block
   (`0xFFFE` tokens and payload) removed and the four literal words of every
   `def` zeroed; everything else byte-exact (version token, every opcode token
   with its length and modifiers, every operand token including register
   type, number, swizzle, mask, `_pp`/`_sat`, relative bits, every `dcl`, the
   `def` opcode and destination register, END). Temporaries are **not**
   canonicalised and instruction order is **not** relaxed.
2. **Table** (`shader_fingerprint_table_inc.h`, generated from the local
   corpus, derived numbers only): `{fingerprint, kind, reviewed_hash,
   reviewed_words, comment_lengths[2]}` for every program in the transform
   registries (`linear_material.cpp` and XT: 137; `motion_output_profiles_inc.h`:
   142; `material_exposure_profiles_inc.h`: 108; `linear_emission*.cpp`: 33;
   175 distinct programs). Every archive program has one or two comment
   blocks, all immediately after the version token (measured over the 750
   corpus programs the proxy keys on anywhere: 684 two-block, 66 one-block,
   750/750 at front), so two lengths suffice.
3. **Image**: on an exact miss, compute the fingerprint (one pass), look it up,
   and build a create-time copy of the candidate whose leading comment blocks
   are resized to `comment_lengths` (payload truncated or zero-padded; missing
   blocks inserted empty; further blocks dropped). Because the instruction
   stream is byte-identical, every instruction now sits at the reviewed DWORD
   offset and the image has exactly `reviewed_words` words. Comment payloads
   are ignored by the shader runtime, so the image executes as the candidate.
4. **Identity override**: the transform entry points (`transform()` in
   `linear_material.cpp`, the XT variant at `linear_xt_material_inc.h:182`,
   `material_motion_*_variant`, `linear_emission*`) take an optional
   `identity`; when set they use it in place of `material_motion_fingerprint`
   for row, pair, palette, exposure and profile lookup, and require
   `words == reviewed_words`. **Nothing else changes**: `structure()`,
   `pixel_sites()`/`vertex_sites()`, `palette_sites()`, `glass_color_sites()`,
   the exposure reservations, `linear_material_fill_sum`, the sun-share
   dataflow planner and the motion transformer's full revalidation all run on
   the real instruction words of the image, as they do on originals today. If
   any refuses, the program is drawn native, exactly as an unknown program is.
5. The registry entry stores `identity` (the reviewed hash) and per-draw
   consumers (pair gating in `before_draw`, depth-only aliases, sampler masks)
   read the identity, not the raw hash. The draw path is otherwise untouched.

Every transform binds through the same mechanism, because every registry is
offset-keyed and validated by the same planners; the distinction the brief
asks for (index-patching versus structurally locating) does not split the
registries, it splits the *tiers*: index-patching transforms are exactly what
tier 1 serves, and the two structural locators (fill sum, sun-share seeds)
only run after a row was found, so they inherit it. A looser tier (renumbered
temporaries, reordered instructions, appended instructions) is **refused**
under this note; see "Alternatives" for why.

## Why this and not more

- Every row is an absolute-offset contract. `Pixel` carries `texture[]`,
  `affine_end`, `clamp`, `final_rgb`, `color_source[]`, `rgb[]`; planners
  derive further sites from them (`p.final_rgb-4`, `alpha+5`,
  `texture[0]+4`, `linear_material.cpp:796-830`). `MotionOutputProfile`
  carries `position_dp4_dwords`, the four insert offsets and `pixel_append_dword`
  and states that "offsets and registers are inputs to a transformation that
  must still revalidate the program" (`motion_output_profiles_inc.h` header).
  The emission `Profile` carries `declaration`, `copy_before`, `native_output`.
  The planners *verify* those offsets with `exact(code,s,offset,opcode,dst,srcs)`;
  they do not locate sites. Preserving layout is therefore the only fallback
  that reuses the reviewed proof unchanged.
- Rows also name physical registers (`clamp_temporary`, `point_temporary`,
  `temporal_temporary_base`, the free set checked by `original_temp_count`,
  `pixel_temporary_base`, the r12/r13/r16-r23/c204-c221 reservations in
  [sun-share-material-contract.md](../reverse-engineering/sun-share-material-contract.md)).
  Canonicalising temporaries would require re-deriving the free set and
  every fragment's registers per program; that is a new proof, not a fallback.
- The sun-share contract is explicit that seeds come from `color_source`
  offsets, "do not infer sun from instruction order", and that witnesses are
  "not permission to match unprofiled motifs". Reordering tolerance would
  contradict the contract the extraction was proved under.
- `def` literals are the one place literal values carry contract meaning:
  `palette_sites()` compares the literal words against `palette_color_bits`
  (`linear_material.cpp:933-937`) and refuses on mismatch, so a palette mod
  that retunes colours is refused rather than mis-linearised. All other
  transforms read live operands, so a retuned literal propagates into the
  output the way the mod intends.

Measured on the corpus (script in the session scratchpad, not tracked):
the literal-blind fingerprint has **zero collisions** inside the 137
linear-material programs, the 142 motion rows and the 108 exposure seeds.
The 33 emission programs form 11 groups (32 programs) that differ **only in
comments**; two of those groups also contain three *uncovered* archive
programs (`7c50897e8cef0a13`, `41c960621d22671f` share the stream of covered
`5e484a06672e28fb`; `369ce62e02a24ac0` shares `a520be365951c9dc`). Across all
751 archive programs, 97 of 98 same-stream groups differ only in comments and
one also in `def` literals: comment-only twins are what the archive already
produces, so this tier matches how the compiler actually varies output.

Rule for collisions: the generator emits one table row per fingerprint. If
the fingerprint belongs to more than one **reviewed** identity, all identities
are listed and the runtime tries them in order, applying the first whose full
planner succeeds; if two succeed with rows that differ in any field, refuse as
ambiguous. If the fingerprint also belongs to an **uncovered** archive program,
the row is emitted with `uncovered_twin=1` and the runtime refuses it, because
exact keying deliberately left that program native and the fallback must not
overrule a review by construction. Today that excludes two emission
fingerprints and nothing in the material, motion or exposure sets.

## Reporting

At create time, alongside the existing `motion_output_variant` /
`linear_material_variant` lines (`src/proxy/motion_output.cpp:2740-2790`):

```
shader_fingerprint_match kind=ps|vs id=<hash> identity=<reviewed hash> words=<n>/<reviewed> comments=<a>,<b>-><c>,<d> defs_changed=<n> transform=<result>
```

`transform` is the same enumerator the exact path logs, so a planner refusal
is visible as `ProfileMismatch` with a fingerprint line rather than a silent
native draw. `ShaderPopulation` gains a fingerprint provider so the classifier
counts the candidate as `fingerprint` rather than `unknown`:
`shader_population known=<n> fingerprint=<n> unknown=<n> overflow=<n>`. With
telemetry off the match still applies (it is a feature, not diagnostics) and
nothing is logged; classification stays create-time only.

## Cost

Create time only, on an exact miss: one FNV pass over at most 1883 words,
one table probe over 175 rows (binary search on fingerprint), one copy of at
most 1883 words for the image, then the planners that already run today. The
proxy dedupes creation by hash, so this happens once per distinct program per
device. No per-draw work, no draw-path allocation, no new lock. Table size is
about 175 x 32 bytes.

## Native Windows

Comment tokens are part of the documented shader token format
(`D3DSIO_COMMENT`, length in bits 16-30, payload ignored by the runtime), and
the variant is created through the same `CreateVertexShader`/`CreatePixelShader`
calls as today, so the mechanism relies on no Wine or CrossOver behaviour. The
image's truncated CTAB is only a concern if the game reads
`IDirect3DXShader9::GetFunction` on a proxy-created program and parses its
constant table; existing variants already return altered bytes, so this note
adds no new exposure, but it remains **unverified natively** (the user cannot
run Windows). The inherited constant-port validity issue of combined programs
(sun-share note, "Known inherited native shader-validity blocker") applies to
fingerprint-matched programs exactly as to originals.

## Residual risk

A mod that keeps the instruction stream and changes a `def` literal that a
planner does not inspect can change what an instruction *means* while every
structural check passes: a selector literal flipped from 1 to 0 turns a lobe
MAD into a pass-through, and the transform still treats it as the lobe sum.
The output is then the mod's law with the proxy's edit applied at a
semantically wrong site; it cannot crash or corrupt other programs (the image
is a private copy, the planner bounds every write), but it can shade wrongly
for that material. Mitigations: the fixture below, `defs_changed` in the log
line, and the palette literal check as the model for any future literal the
planners come to depend on. This risk is accepted because the exact path has
the same shape of exposure to the game's own future patches and because the
alternative is no fallback at all.

Refusal cases that a user may regard as "still not working": a real
`fxc` recompile with a different compiler version that reallocates registers
or reschedules instructions lands outside this tier and is reported as
`shader_unknown`. That is intended: nothing below exact structure has a proof.

## Acceptance fixture

Host-only, no Wine: `verification/probe/shader_fingerprint_fixture.cpp`
(including `linear_material.cpp` as `original_sun_share_structure.cpp` does,
so refusals stay out of the public API) driven by
`verification/analysis/test_shader_fingerprint.py` over the local corpus. For
each of the 175 transform-keyed programs (the 137 linear-material originals in
both depth modes, fill 0/.06, fade where eligible):

1. Identity: fingerprint maps to its own hash, table unique, image of the
   original equals the original byte-for-byte.
2. Comment perturbations (payload +N/-N words, block dropped, extra `PRES`
   block, payload bytes flipped): must match; image words equal reviewed words;
   transform output equals the exact-path output modulo comment payload.
3. Non-palette `def` literal changed: must match and apply; output equals the
   exact-path output with the same literal substituted.
4. Palette colour literal changed (the 32 `palette_programs`): must match and
   refuse with `ProfileMismatch`, output rolled back.
5. Renumbered temporaries (one consistent r<a>/r<b> swap), two independent
   adjacent instructions reordered, one instruction appended before END, one
   operand swizzle flipped: fingerprint miss, reported as unknown, output
   rolled back.
6. The two emission fingerprints with uncovered twins: refuse; their uncovered
   twins stay unknown.
7. Invariant over all cases: every program either (matches, passes the
   planner and equals the oracle) or refuses with unchanged output; zero cases
   of match-and-pass with an oracle mismatch. The counts are the acceptance
   numbers; the existing frozen fill hash (`3db6100...`) must be unchanged
   because the exact path is untouched.

Verification that would prove it in the game: a user load with `--telemetry`
on a mod that ships edited `.fx` (none of the inspected Mayhem packages does),
checking `shader_fingerprint_match` lines and the `shader_population` split.

## Alternatives considered

- **Canonical structural fingerprint with per-site structural locators**
  (temporaries renumbered by first use, order-insensitive, sites found by
  pattern): loses because every registry's proof is offset-keyed and register-
  specific; it is a rewrite of eight planners into locators and a new
  register-allocation proof, with the sun-share contract explicitly
  forbidding order-inferred sites. It can be a later tier on top of this one.
- **Strip comments everywhere and regenerate all tables against stripped
  originals**: same reach as the recommendation but touches every row in
  every registry, every offset in the reverse-engineering notes and the
  provenance of the byte-identical off path.
- **Remap row offsets through an instruction-ordinal map**: same reach, but
  the remap must be applied at every derived-offset use inside the planners
  (`final_rgb-4`, `alpha+5`, `texture[0]+4`, ...) and every profile field;
  resizing the comment blocks achieves the identical layout with zero planner
  edits.

## Unknown

- Whether the game parses the constant table of a created shader object
  (`GetFunction` + `D3DXGetShaderConstantTable`). Settle by xref of the D3DX
  import and `GetFunction` vtable slot callers in `X3AP.exe`; if it does, the
  image must keep the candidate's full CTAB and the resize must instead pad
  to the reviewed length only when the candidate's block is shorter (and
  refuse when longer), which the corpus suggests is the common direction.
- Which `def` literals each planner reads, beyond the palette check. A grep
  audit of literal reads (`code[ins.at+2..5]`) in the `*_sites` functions
  before implementation; each literal read becomes an explicit check.
