# Offline shader-pair coverage census

Proactive counterpart to `shader-coverage-census.md`, which derives unknown
pairs from flight logs after the fact. This census enumerates every vertex/pixel
pair the installed archives can bind and states, per pair, whether the
motion/sun-shadow registry knows it and whether the offline rewriter model
would accept it. Tool: `tools/analysis/shader_coverage_offline.py`; result:
`verification/results/shader-coverage-offline.json` (786 KB — query it, do not
read it whole); tests: `verification/analysis/test_shader_coverage_offline.py`
(14 checks).

No Wine run is needed and none was made: every `shader/**/*.fb` archive entry
is already a compiled D3DXFX container (magic `0xfeff0901`), so nothing is
compiled by `d3dx9` at load and there is no macro/`#define` set to reproduce.
The variants the game can select are directories inside the archive: the
shader-profile trees `1_1`, `1_4`, `2_0`, `2_a`, `2_b`, `3_0` (the shader
quality setting) and the toggle trees `hueshift_off`, `hue_lights_off`,
`v_lights_off`; all are enumerated. Archive bytes are read in memory only.

Run (3.1 s, no Wine, no game launch):

```sh
python3 tools/analysis/shader_coverage_offline.py \
  --game ~/Library/Application\ Support/CrossOver/Bottles/X3/drive_c/X3 \
  --flight-pairs <pairs.txt> \
  --output verification/results/shader-coverage-offline.json
```

`<pairs.txt>` is any text holding `vs=<hex> ps=<hex>`; the run below used
`grep -aoE 'vs=[0-9a-f]{16} ps=[0-9a-f]{16}' /tmp/x3-bottleX3-run{172..175}/session-*.log | sort -u`
(streamed, no log read whole).

## Counts (2026-09-19, bottle X3 install)

| quantity | value |
| --- | ---: |
| `shader/**/*.fb` archive entries | 3480 |
| effective effects after override precedence (addon/late catalogue wins) | 2352 |
| entries hidden by an override | 1128 |
| technique passes in the effective set | 4528 |
| distinct (vs, ps) pairs | 817 |
| registered pairs (`motion_output_profiles_inc.h` rows) | 171 |
| jitter-only depth-prepass vertex rows (`depth_prepass_profiles.h`) | 2 |
| rewritable but unregistered | 0 |
| not rewritable by the current transformer | 643 |
| pair with an unparseable vertex program (`standard_lighting_0000`) | 1 |

Registry rows absent from the archive: none (all 171 rows resolve to a pass
that exists in the installed data).

By shader model — the decisive shape of the gap:

| vs/ps model | pairs | registered |
| --- | ---: | ---: |
| 3_0 / 3_0 | 180 | 171 |
| 2_0 / 2_0, 2_0 / 2_1, 2_1 / 2_1 | 466 | 0 |
| 1_1 / 1_1, 1_1 / 1_4, 1_1 / null | 170 | 2 (prepass jitter only) |

The nine unregistered SM3 pairs are all `bloom` / `bloom_0000` post passes,
blocked by `vs_position_unknown:position_write_is_mov` (a full-screen quad
writes `oPos` with MOV, not the four clip-row dots the transformer splices
after); two of them additionally have no free pixel input register. They are
post-process passes, not scene geometry, so no coverage is lost by refusing
them.

Everything else unregistered is SM1/SM2. The archive holds one variant of each
material effect per profile directory (`argon.fb` in `1_1`, `1_4`, `2_0`,
`2_a`, `2_b`, `3_0`, plus the `_0000`/`_0001` variant files), and only the
`3_0` variants carry rows. **Registration coverage is therefore complete for
the highest shader-quality profile and zero for every lower one**: a user who
runs the game at a lower shader quality gets an unregistered pair for
essentially every scene draw. `ps_1_x` has no second colour target at all;
the SM2 pairs would need the separately documented `ps_2_0` fragment host.

## The three Argon Prime pairs, named

| vs / ps | effect file | technique / pass | model | verdict |
| --- | --- | --- | --- | --- |
| `ac2319bc3953efc6` / `03a16e5c63daa6e8` | `adeffects.fb`, `adeffects2s.fb` | `DEFAULT` / `P0` | vs_2_0 / ps_2_0 | not rewritable: SM2, no generated row |
| `c78b4c68a87fce74` / `0000000000000000` | `z_only.fb` | `Z_Only_Fast` / `P0` | vs_1_1 / none | already a `depth_prepass_profiles.h` row: jittered, never routable (no PS to append to) |
| `803ebfd17f79e413` / `652a7c5d1e9909a0` | `z_only.fb` | `Z_Only_Alpha` / `P0` | vs_1_1 / ps_1_1 | same table, same verdict |

So two of the three "dangerous" pairs are the known depth-only prepass
programs — the engine's `Z_Only_*` pass, which the route deliberately jitters
without routing — and the third is the station advertisement billboard effect
(`adeffects`), whose `ZEnable`/`ZWriteEnable`/`AlphaBlendEnable` are driven by
an FXLC preshader, which is exactly why the flight saw mixed `zwrite=0,1` on
one pair. The sun-shadow lane refusing on all three is a lane-gate question,
not a missing-registration question: no table row can be added for any of them
with the current SM3-only transformer.

Every one of the 15 unregistered pairs observed in flight (of 28 distinct pairs
across runs 172-175) now has a name:

| vs / ps | effects | technique |
| --- | --- | --- |
| `1279d081455f5815` / `ff6eed5a5ddf3a3a` | bloom | DEFAULT |
| `6059306306203243` / `241c3fa33270f58e`, `f3172baa8dd19a40` | bloom | DEFAULT, HDR |
| `cbbf26102694c961` / `1c90e79667bdaddf` | bloom | DEFAULT, HDR |
| `36f98d151fd6b0c6` / `222bee0defcb1852` | particles, particles2s | DEFAULT |
| `5e484a06672e28fb` / `0a523f33ac47ae05` | stardust | INSTANCE |
| `f36fc43f30b19d71` / `0a523f33ac47ae05` | gui2d | INSTANCE |
| `7b6393fe2d3e1d85` / `6109cf64c03529dd` | gui2d, nebula, nebula2s | DEFAULT |
| `7b6393fe2d3e1d85` / `f7e0b6647a3bfa62` | nebulafog, nebulafog2s | DEFAULT |
| `72f8dbb8567bbf88` / `00fcc903c7f085d5` | planet_v, planet_v2s | DEFAULT |
| `be199829a9bb78db` / `cd6d6eb4b3d99443` | planet_haze | DEFAULT |
| `d5e1c75351ed3f04` / `8360f422de08b5bd` | effects, effects2s, engine, engine2s | DEFAULT |
| `ac2319bc3953efc6` / `03a16e5c63daa6e8` | adeffects, adeffects2s | DEFAULT |
| `803ebfd17f79e413`, `c78b4c68a87fce74` | z_only | Z_Only_Alpha, Z_Only_Fast |

## Unregistered depth-only prepass programs not in the jitter table

`z_only_0000.fb` and `z_only_0001.fb` (base `01.cat`, not overridden — they are
separate virtual paths, present in every profile and toggle directory) hold a
second pair of `Z_Only_*` programs with the same shape and exact DWORD lengths
as the registered ones but different bytes:

| technique | registered (from `addon/01.cat` `z_only.fb`) | unregistered (`z_only_0000/0001`) | dwords |
| --- | --- | --- | ---: |
| `Z_Only_Fast` | `c78b4c68a87fce74` (null PS) | `4b63594a775cbde0` (null PS) | 89 |
| `Z_Only_Alpha` | `803ebfd17f79e413` / `652a7c5d1e9909a0` | `d2e63b1e5b0e24df` / `9b8fd38f3c962220` | 95 |

These are the only two unregistered pairs in the whole archive whose container
sets `ZWriteEnable = 1` literally (everything else is preshader-driven), and
they are colour-masked depth-only writers — exactly the shape
`depth_prepass_profiles.h` exists for. No flight has bound them yet. If the
engine ever selects the `_0000`/`_0001` variant files, the prepass draw goes
out unjittered and the jittered draws depth-tested against it lose facets
(`asteroid-fog-temporal.md`). Reviewing those two vertex programs for a
prepass row is the one mechanical coverage extension this census found. This
census does not change any table; the owner of the shadows/registration work
decides.

## Pass classification and its limit

The class of a pass comes from the pass's own state assignments. Of the 4528
passes, only a minority set the depth and blend states as container literals:
`ZWriteEnable` is a literal in 792 state assignments (648 × 0, 144 × 1) and an
FXLC preshader in 3528. Counting pairs with at least one pass of a class: 769
of 817 pairs have a `depth_dynamic` pass and 29 a `depth_inherited` one (the
pass assigns no depth state at all), against 4 `depth_only_mask`, 13 `blended`
and 3 `additive`.

The practical conclusion: **offline state classification cannot separate opaque
from blended for the X3 material effects** — the engine decides per draw
through the effect's preshaders — so an unregistered pair must be treated as a
potential depth writer unless its container proves otherwise. The census
reports it that way (`depth_candidate`, and `unknown_depth_breakdown`:
2 literal depth writers, 628 dynamic or inherited).

## Cross-check against flight evidence

28 distinct `(vs, ps)` pairs appear in the `motion_route` /
`sun_shadow_lane_writer` rows of runs 172-175; all 28 are present in the
offline set (`flight_cross_check.passed = true`), 13 registered and 15 not.
The 10 `(vs, ps)` pairs spelled out in `shader-coverage-census.md` (runs
116-175) are likewise all present offline. That is what validates the hash
method: the offline FNV-1a 64 over the complete
token stream (version, comments/CTAB and END included) reproduces the identity
the proxy computes at `CreateVertexShader`/`CreatePixelShader` time, and the
archive enumeration with override precedence covers everything the engine
actually bound in flight.

## Limitations

- Effects the engine builds or overrides at run time are not enumerated; every
  pair seen in flight so far came from the archives.
- Rewritability is the generator model's verdict
  (`inspect_motion_output_profiles`, verdict source recorded in the JSON with
  its SHA-256). The C++ transformer revalidates every structural assumption
  before it splices, so a verdict of "rewritable" is a candidate, not a row.
- A null-PS pair can never be rewritten in place: there is no pixel program to
  append the motion write to.
