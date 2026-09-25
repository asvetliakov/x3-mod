# Data-driven fog families: mod and missing-asset nebulae

Design note, 2026-09-23, for the user decision "extend the card replacement to nebula families
the stock inventory does not cover". No code changes here. Depends on the family inputs of
[volumetric-fog.md](volumetric-fog.md), the asset-backed expansion checkpoint of the
[fog ledger](../verification/volumetric-fog.md#asset-backed-family-expansion-host-checkpoint-2026-09-20),
`tools/build/bake_fog_fields.py`, `tools/fog_field_recipe.py`, `tools/build/fog_family_chroma.py`,
`src/renderer/fog_field_assets.*`, `src/renderer/fog_pass.cpp` (`prepare_field`, `field_family`),
`src/proxy/fog_sector_policy.h`, `src/proxy/sector_background.h` and
[sector-fog.md §11](../reverse-engineering/sector-fog.md).

**Outcome.** Ship the profile set as one generated binary file `<game>/x3m/fog-families.bin`
(plus a JSON record beside it) produced offline by a new `tools/analysis/fog_families.py` from the
installed catalogues, mod catalogues included, through the `sector_fog_census.Assets` resolver that
`bob1.py` and `lod_batch_census.py` already use. The file carries, per family, the same
`X3FOGPK` v1 packet the DLL decodes today plus the row constants (`base_sigma`, occupancy, palette,
mean chroma); the DLL reads the table once at the first sector sample and one packet per family
switch through documented Win32 file APIs, validates every field against fixed bounds and the
existing packet checksum, and falls back to the 14 compiled profiles whenever the file is absent,
unreadable or invalid. Per-family fields stay full packets (option C below) because that keeps the
decoder, the GPU path and the pinned provenance unchanged; the CPU-composed compact form
(option A) is the follow-up if the Mayhem file size (about 128 MB, estimate) turns out to matter.

## 1. What identifies a family at runtime, and what a mod supplies

The detector (`sector_background::sample` → `fog_sector_frame`) reads two fields of the active
`TBackgrounds` row in place (rebased `R`, sector-fog.md §11.1) and nothing else:

| Input | Where | Rule today |
| --- | --- | --- |
| Body family name | `R+0x00`, `char*` (column 7 of the record) | Read into a 32-byte span; printable ASCII (0x20–0x7e) terminated within those 32 bytes, else `name_invalid`; escaped into `family[65]`; exact `strcmp` against the 14 names of `family_profiles` |
| `NumDustInstances` | `R+0xf0` | 0 = `clear`, outside 0..64 = `bad_dust`; positive selects |

Textures, dust body ids, `DustBodyRate` and the fade distances are read into the sample (for the
log) but never choose a profile. Card suppression keys on the `nebulafog` program pair
(`fog_card_match.h`: VS `7b6393fe2d3e1d85` / PS `f7e0b6647a3bfa62`), not on textures. So a
family's runtime identity is the string in its `TBackgrounds` row plus a positive count; the
profile is whatever the table maps that string to.

What the mod trees provide (measured, `verification/results/fog-family-data/mod_family_census.py`,
output `mod-family-census-2026-09-23.txt`, both trees overlaid on the installed X3 bottle):

| Tree | TBackgrounds source | Records | Positive records / families | Not compiled | Dust parts per family | Material effect | Diffuse textures |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `/tmp/x3-mod1` (Mayhem 3, cats 05–12) | its `addon/07.cat` | 250 | 56 / 56 (`litcube0`…`litcube230`, 8–10 chars) | 56 | 8 (`.pbd`, text) | `nebulafog.fx` in all 448 | 8 distinct per family, 384 total; all uncompressed 24-bit DDS 1024², resolved from the mod cats; the material path names another folder (`environments\nebulae\bluenexus\dust\nebula_litcube0_1_dust_diff.tga`) |
| `/tmp/x3-mod2` (Renegades add-ons, cat 12) | the game's stock `addon/03.cat` | 83 | 23 / 16 | 2 (`earth`, `xtmgreenring`) | 6 stock; `xtmgreenring` 0 | `nebulafog.fx` | stock DXT1; `earth` references `nebula_standardblack_dust_diff`, missing |

Mayhem's map (`addon/07.cat`, 256 sectors) uses all 56 positive families, one sector each, all
with `FogNear/FogFar` 50,000,000/60,000,000; its rate vectors are mostly `1×8`. Neither tree ships
a shader member, so the `nebulafog` hash rule holds. The mod names stay well inside the 32-byte
bound and the ASCII rule, so the sampler accepts them unchanged. A mod family therefore provides
everything the detector consumes (name, count) and everything the profile tool consumes (bodies,
rated parts, resolvable textures); the two stock exceptions lack one of the tool's inputs each.

## 2. Making the profile set data-driven

### What is per family and what is shared

`bake_fog_fields.py` computes one shared field (`bake_fields`: warped carrier, cavity mask,
eligibility threshold `t1`, chroma interpolation; 128³, period 32,768) and then, per family,
`family_density(occupancy)` (a threshold of the shared carrier at the occupancy quantile),
`colour_volume` (density × the four-stop palette lerped by the shared interpolation volume) and
`atlas_from_volume`/`packetize` (RGBA16F 1560×1430 atlas, zero/literal runs, FNV-1a checksum).
So the baked atlas is a pure function of (shared field, occupancy, palette); `base_sigma` and
the mean chroma (`fog_family_chroma.py`: `sum(rgb)/sum(density)`) are constants beside it.
Measured on this host (`bake_timing.py`): shared bake 2.9–3.9 s; per family density 0.02 s, colour
+ atlas 0.15 s, packetize 1.2–1.5 s (a pure-Python run loop; vectorisable); one packet
2,281,060 bytes for 17,846,400 decoded (258,371 nonzero texels at occupancy 0.12).

Families with the same occupancy share the density volume exactly; only the colour differs.
That is what makes option A possible later, and what makes option C's file redundant but simple.

### Recommended: option C, packets in a file, unchanged decoder

- **File** `<game>/x3m/fog-families.bin` (the `x3m/` drop-in directory already used by
  `x3m/voice-decoder`), sidecar `x3m/fog-families.json` for humans and `--check`. Layout: a
  64-byte header (`X3FOGFAM`, version 1, header size, `recipe_id` 1, family count, packet count,
  table offset/bytes, FNV-1a of the table bytes, total file size); a fixed-size family row per
  family (name[32] NUL-terminated printable ASCII, profile id u32, packet index u32, `base_sigma`
  f32, occupancy f32, chroma f32[3], colours f32[4][3], flags u32); a packet row per distinct
  packet (offset u64, size u64, width, height, texel bytes, decoded bytes, decoded FNV-1a, decoded
  SHA-256 for provenance); then the `X3FOGPK` v1 packets verbatim, deduplicated by decoded hash.
  The JSON record and the binary table say the same thing; the DLL reads only the binary.
- **Profile ids.** Dynamic ids are `0x10000 | (fnv1a32(name) >> 15)`-style name hashes assigned
  by the tool, checked unique, never in 1..14. `fog_sector_placement` hashes the id into the
  per-sector translation, so a name-derived id keeps a mod sector's cloud placement stable when
  the file is regenerated with more families; a table index would not.
- **Loader** (`fog_field_assets`, new `FamilyTable`): built once per process on the render
  thread at the first `fog_sector_frame` evaluation (before the first name scan), from
  `GetModuleFileNameW(nullptr)`'s directory + `x3m\fog-families.bin`, or the path in
  `X3M_FOG_FAMILIES` (`none` disables; the launcher passes the variable through unchanged for
  fixtures and opt-out). `CreateFileW`/`GetFileSizeEx`/`SetFilePointerEx`/`ReadFile`, opened per
  read and closed, so a regenerating tool never fights a held handle. The table (≤ 40 KB at the
  256-family cap) is read once; a packet (≈2.3 MB) is read at a family switch inside the same
  `taa_call` bracket that decodes a resource today, and decoded by the unchanged `decode_packet`
  with a `ProfileInfo` built from the packet row. Process lifetime, never freed, never reloaded:
  `FogSectorFrame::reason` points into it, as it points into `family_profiles` today.
- **Selection.** `fog_sector_frame` scans the 14 compiled names, then the table. A compiled name
  always wins; the tool does not emit rows for the 14 (it records them as `covered_by_build`), so a
  file cannot re-tune a pinned stock family. `field_family` (stored-density chroma and sigma) and
  `profile_info` consult the table for dynamic ids.
- **Validation contract** (all failures reject the whole file with one
  `volumetric_fog_families event=rejected reason=…` line and leave the compiled 14 in force):
  file size ≥ header; magic, version and header size exact; `recipe_id == qualified_recipe_id`;
  family and packet counts in 1..256; table inside the file and its FNV-1a matching; each name
  1..31 printable ASCII bytes, unique, not one of the 14; each id ≥ 0x10000 and unique; `base_sigma`
  finite in (0, 1e-4]; occupancy in [0.01, 0.5]; chroma and colours finite in [0, 1]; each packet
  row with width 1560, height 1430, texel bytes 8, decoded bytes 17,846,400, nonzero checksum,
  `offset + size` inside the file and `size ≤ 56 + decoded bytes`. The DLL never sizes an
  allocation from the file beyond the fixed atlas (`atlas_bytes_`) and one read buffer bounded by
  the validated packet size; run structure, truncation, half validity and the checksum are the
  decoder's existing 14 statuses. A packet that fails at switch time (file swapped underneath,
  disk error) marks that family row disabled and the next frame's scan returns
  `family_unsupported` (native cards), instead of `fault_fog_cards("prepare")`, which is the
  right outcome for a compiled resource but would blind the session for one bad mod row.
- **Hot path.** Nothing per draw or per frame changes; the per-sample name scan grows from 14 to
  at most 270 `strcmp`s of ≤ 32 bytes (≤ 10 µs, estimate; short-circuit on an unchanged escaped
  name if it ever shows in `volumetric_fog_prepare cpu_us`). Warm-up adds one file read per family
  switch on top of the measured 3–4 ms decode and 18 ms first upload (ledger); expect ≈ 20–25 ms
  per switch (estimate), the same order as today.
- **Native Windows.** Documented Win32 only; the same relative location next to `X3AP.exe`;
  no bottle paths, no `Z:` drives. The tool takes `--game` for a Windows install path; a Windows
  user runs it with the same Python and NumPy 2.0.2 pin the build already requires. Unverified on
  native Windows like the rest of the pass; add the row to `platform-portability.md` when built.

### Options considered

| Option | Why it loses |
| --- | --- |
| **A. Compact rows + CPU compose in the DLL** (embed one density+interpolation packet per occupancy class, compose density × palette at warm-up) | File shrinks to kilobytes, but the DLL gains a new packet type, a 2.2 M-texel compose loop on the render thread at every switch (tens of ms under FEX, estimate) and a software float32→half round-to-nearest that must match NumPy bit for bit to keep the GPU fixture's pinned hashes. More code and new invariants for a size win nobody has asked for yet. The natural second step if 128 MB matters. |
| **B. Palette in the shader** (sample density + interpolation, four-stop lerp per march sample) | Adds work to the 24-step march per pixel, requalifies the march/composite programs and the accuracy gates, and drops the pinned-atlas provenance. Hot-path cost for no visual gain. |
| **C'. Rebuild the DLL per mod** (extend `fog_field_recipe.py` and embed the mod packets) | No loader or tool, but +128 MB of DLL resources for Mayhem, a rebuild for every mod change, no path for a user without the toolchain, and the candidate/install record binds a mod's content into the production DLL hash. |
| **D. Derive profiles in the DLL from the game's texture loads** | Needs hooks on the texture path, an in-process bake (the host bake is 3–4 s of NumPy; longer under emulation) and gives no offline validation or record. |
| **E. JSON rows, bake the noise in the DLL** | Same in-process bake cost as D at first use, plus a JSON parser in the proxy. |

## 3. Provisional profile from textures alone

The ratified stock rule (occupancy 0.12, `base_sigma` 2.5e-6, palette from the winning DXT1
diffuse, luminance bands 25–40 / 40–55 / 55–70 / 70–85 %, peak-normalised) generalises to any
family as follows; a single texture with equal rates reduces to it exactly.

1. **Records.** All rows of the family with `NumDustInstances > 0`. Part weight
   `w_i = Σ_records DustBodyRate[i]`, `i = 1..8`; parts with `w_i = 0` are skipped.
2. **Bodies.** `objects/environments/nebulae/<family>/nebula_<family>_dust_part{i:02d}` resolved
   by `Assets.logical` over `.pbd/.bod` (text, `sector_fog_census.body_metadata`) and
   `.pbb/.bob` (binary, `bob1.parse` + `materials`). A rated part without a body: skip it; a family
   with no body at all: refuse, reason `no_dust_bodies`.
3. **Textures.** Every `MATERIAL6` whose effect is `nebulafog.fx`: `t_DiffuseTexture` → `dds/<stem>`
   through the overlay precedence (`.pck`, `.dds`, `.tga`); decode level 0 with
   `lod_atlas.decode_dds` (DXT1/3/5, 24/32-bit uncompressed; anything else refuses with
   `texture_format_unsupported`). A missing texture refuses the family (`texture_missing`) unless
   overridden (below). Each distinct decoded texture (by SHA-256) is decoded once.
4. **Pixels.** sRGB → linear (piecewise EOTF), `Y = 0.2126 R + 0.7152 G + 0.0722 B`, drop
   `Y == 0`, ignore alpha (the cards blend `ONE / INVSRCCOLOR`; alpha is not coverage). Pixel weight
   `w_i / N_i` with `N_i` the texture's nonzero count, so every part contributes in proportion to
   its rate, not its resolution.
5. **Stops.** Weighted percentiles `p25, p40, p55, p70, p85` of `Y`; stop `k` = weighted mean
   linear RGB over `[p25,p40), [p40,p55), [p55,p70), [p70,p85]`; each stop divided by its largest
   component. An empty band or a zero peak refuses (`palette_degenerate`).
6. **Constants.** Occupancy 0.12, `base_sigma` 2.5e-6, `density_status: provisional_artistic`;
   chroma from the baked packet as `fog_family_chroma.py` defines it. Per-family overrides
   (`--profile NAME=occupancy,sigma`) are recorded, never silent.

The 12 stock provisional palettes are the regression: the tool must reproduce
`fog_field_recipe.PROFILES[*]['colours']` to the float32 digit from the installed `01.cat`.
Unknown: whether the original derivation (local evidence `/tmp/x3-fog-family-palettes.md`; no
script in the repository) used `numpy.percentile`'s linear interpolation or a rank cut at the band
edges; the host test settles it, and if neither reproduces every family the note is amended
before any pin moves.

**`earth` and `xtmgreenring`.** Refuse both by default. `xtmgreenring` has no dust bodies, so
the engine draws no cards there: replacing nothing with fog changes a sector's look, and no stock
or Mayhem sector uses either record (0 sectors each, measured). `earth` has six bodies whose
texture is missing; what the engine draws for a nebulafog material with an unresolved diffuse is
unknown (settle by disassembling the texture-miss path of the material loader, or by a flight in
a sector remapped to record 77). The opt-in `--background-palette NAME` derives the palette from
the family's sky parts (`nebula_<family>_background_part_01..04_diff`, equal weights, same steps
4–5) and records `palette_source: background`; the user asked for these two, so the switch exists,
but the default must not invent fog silently.

## 4. The tool

`tools/analysis/fog_families.py`, beside the census and overlay tools that share the resolver.

- **Inputs.** `--game DIR` (default `bob1.DEFAULT_GAME`); `--mod-cat PATH …` extra layers for an
  uninstalled tree (the census's `mods=`, used above); `--family NAME …` to restrict;
  `--profile NAME=occupancy,sigma`; `--background-palette NAME …`; `--jobs N` for the per-family
  bake. NumPy 2.0.2 is required exactly as the build requires it.
- **Outputs.** `--out DIR` writes `DIR/x3m/fog-families.bin` and `DIR/x3m/fog-families.json`
  (record: tool and recipe hashes, catalogue layers, TBackgrounds member and SHA-256, per family:
  records, rates, parts, textures with member/source/SHA-256/format/size, stops, occupancy, sigma,
  chroma, packet decoded SHA-256 and FNV-1a, status or refusal reason, `covered_by_build` for the
  14, bake seconds). `--dry-run` prints the family table and refusals without baking. `--check`
  validates an installed file against the current catalogues (stale texture or TBackgrounds hash).
- **Install.** `--install` writes into `<game>/x3m/`, refuses while `X3AP` runs
  (`game_guard.game_running`, `--force-running` overrides), refuses to overwrite unless
  `--replace`, keeps one `fog-families.bin.previous` for rollback; removing the two files reverts.
  Exactly one of `--out` and `--install`, and `--out` must be outside the game directory, as in
  `lod_overlay.py`.
- **Batch flow.** After the mod (or the LOD overlay) is installed and before launch:
  `lod_batch_census` → `lod_overlay --install` → `fog_families --install` → `manage.py launch`.
  The launcher's `--dry-run` should report the file's presence and family count; nothing in the
  build changes.
- **Cost (estimates from the measured per-family numbers).** Mayhem: 3–4 s shared bake + 56 ×
  ≈1.5 s ≈ 90 s before vectorising `packetize`; ≈ 128 MB on disk (56 × 2.28 MB; the 40 families with
  equal rates still have distinct textures, so dedup does not help here). Stock with the two
  overrides: seconds and < 5 MB.

## 5. Verification plan

- **Host, loader** (`verification/probe/fog_field_assets_fixture.cpp` extended or a sibling,
  driven by `test_fog_field_assets.py`): absent file → 14 profiles, no log line beyond `absent`;
  directory in place of the file, zero-length, truncated header, bad magic, version 2, header
  size 0, recipe 2, 257 families, table past EOF, table FNV mismatch, duplicate name, a name equal
  to `bluewell`, a 32-byte name, id 3, sigma 0 / NaN / 1e-3, occupancy 0.6, colour 1.5, packet
  offset past EOF, packet size 0 → each rejected with its reason and the 14 intact; a valid file
  → 14 + N, lookup by name, id equals the tool's; the 11 packet corruptions per profile reused on a
  file packet → the row disabled, not a session fault. The `X3M_FOG_FAMILIES` override exercised
  by every case.
- **Host, tool** (`test_fog_families.py`): the 12 stock palettes reproduced bit-exact from the
  installed `01.cat` (skipped without the game, with synthetic DDS cases for the weighted rule,
  the three DDS formats, refusal reasons, dedup, `--check` staleness, install refusals).
- **Host, policy** (`test_fog_sector_policy.py`): a sample named `litcube0` with dust 50 selects
  the dynamic id; an unknown name stays `family_unsupported`; `everywhere` still forces 1.
- **Fog pass fixture** (`fog_family_gpu.py`, under the Wine lock, `X3M_FIXTURE_BOTTLE=X3`): one
  synthetic family (a recoloured bluewell packet named `zzsynthetic`) in a temporary file passed by
  `X3M_FOG_FAMILIES`; the case must produce the same witnesses as a compiled family
  (`switch`, `one_atlas`, `warm_reuse`, `nonempty_alpha`, `stale_refused`) and pass the existing
  transmission/scattering gates against the CPU reference.
- **Flight** (user, after installing Mayhem 3): one modded launch with `--volumetric-fog
  --volumetric-fog-cards replace` in any `litcube` sector; the log must show
  `volumetric_fog_families event=loaded families=56`, `volumetric_fog_prepare profile=0x1…`,
  card suppression armed, and a screenshot; then Argon Prime (bluewell) in the same session for an
  unchanged stock appearance. Mayhem's cats are numbered 05–12 and the installed `addon/05` is
  the LOD overlay, so the install order is a separate decision for the orchestrator.

## Implementation (2026-09-23)

Option C as built. Where it differs from §2–§4 above, this section is the contract.

**Palette convention, settled (amends §3 steps 3–5).** The 12 provisional stock palettes of
`fog_field_recipe.py` reproduce bit for bit (float32 of the recipe values) only with: DXT colour
blocks decoded with RGB565 endpoints expanded by integer floor (`c * 255 // 31`, `g * 255 // 63`)
and truncating palette interpolation (`(2a + b) // 3`, `(a + 2b) // 3`, 3-colour `(a + b) // 2`),
not `lod_atlas.decode_dds` (bit replication, rounded interpolation: stops off by up to 0.09,
band counts off by about 2,000 texels); percentile edges by `numpy.percentile` (linear); bands
inclusive at both ends, `edge_k <= Y <= edge_k+1` (not half-open); stops rounded to 9 decimals,
then float32. Uncompressed 24/32-bit textures use their exact 8-bit channels. With uniform pixel
weights (one texture, every stock family) that is the whole rule; with non-uniform weights the
edges are interpolated at the plotting positions `(S_k - w_k) / (S_n - w_n)` of the Y-sorted
texels (numpy's linear rule for equal weights) and band means are weighted. `bluewell` and
`foggreenoutlands` do not reproduce (hand-tuned before the rule, as the proposal records). Test:
`test_fog_families.StockPaletteRegression` (skipped without the installed game).

**Tool** `tools/analysis/fog_families.py`; its docstring is the usage and rule reference. Options as
§4 plus `--force-running`; `--install` checks for a running game before the bake and again just
before it moves anything, and a failed write or post-write validation restores the moved
`.previous` pair; `--jobs N` runs palette + bake per family in spawn worker processes (the
shared field is baked once in the parent and handed to the workers; texture bytes are read at
submission, at most 2N families in flight); output is byte-identical for any N. Families sort by
name; identical decoded atlases share one packet whose X3FOGPK header carries the first family's
id. Refusals as §3 plus `no_nebulafog_material`, `name_invalid`, `profile_id_collision`,
`table_full` and `background_missing`. `--background-palette` as built: the note's
`nebula_<f>_background_part_0N_diff` textures exist for neither stock exception, so the fallback is
the diffuse textures of `nebula_<f>_background_01`'s materials (`nebula.fx` ones, else all real
diffuse maps; equal weights): `earth` gets a palette from its 7 sky textures (solar-system
quadrants, planet, clouds, haze), `xtmgreenring` refuses `background_missing` (no body folder).
`--check` re-reads the file with the loader's rules (`read_file`) and compares the TBackgrounds,
body and texture hashes and the baker/recipe hashes with the record. Without the game the stock
regression is skipped; everything else runs on synthetic catalogues.

**File as built.** Little endian. Header 64: `X3FOGFAM`, version 1, header bytes 64, recipe id 1,
family count 1..256, packet count 1..families, family row bytes 112, packet row bytes 80, table
offset 64, table bytes, reserved 0, FNV-1a 64 of the table, file size. Family row 112: name[32]
NUL-padded, profile id, packet index, `base_sigma`, occupancy, chroma[3], colours[4][3], flags
(1 background palette, 2 `--profile` override). Packet row 80: offset, size (u64), width 1560,
height 1430, texel bytes 8, decoded bytes 17,846,400, decoded FNV-1a 64, the packet's header
profile id, reserved 0, decoded SHA-256. Then the packets. Profile id = FNV-1a 32 of the name with
bit 16 set (31 bits of hash, never 1..14); the loader recomputes it from the name. Table at most
49,152 bytes.

**Validation as built (differs from §2: row-level, not whole-file).** Header and table failures
reject the file: `truncated_header`, `oversized` (above 64 + 49,152 + 256 × (56 + 17,846,400)
bytes), `bad_magic`, `version`, `header_size`, `recipe`, `family_count`, `packet_count`,
`row_size`, `table_offset`, `table_bytes`, `reserved`, `file_size` (header vs actual),
`table_past_eof`, `table_checksum`, `open_failed`, `read_failed`. Row failures disable that row
only: `name` (1..31 printable ASCII, NUL-terminated and padded), `name_compiled` (a compiled name
never loads, so the file cannot re-tune a pinned family: compiled first, as §2 says),
`profile_id`, `packet_index`, `sigma` (finite, (0, 1e-4]), `occupancy` ([0.01, 0.5]), `chroma`,
`colour` (finite, [0, 1]), `flags`, `name_duplicate`, `profile_id_duplicate` (checked against every
earlier row, disabled or not, so `find()` and `row()` never resolve one family to two rows). Names
with `"` or `\` are refused (tool `name_invalid`, loader `name`): the sampler escapes them in the
name it compares, so such a row could never match. Packet-row failures
disable every row using the packet: `packet_dimensions`, `packet_checksum_zero`, `packet_profile`
(must be a referencing row's id and a file id, never a compiled one), `packet_size` ((56, 56 + decoded]), `packet_offset` (after the
table, inside the file), and `packet_header` (the packet's own 56-byte header, read at load, must
agree with its row). The run data and decoded checksum are the unchanged decoder's at the switch:
any failure there (its statuses, `read_failed`, the file's size changed since load) disables the
rows of that packet, except that a first `allocation` failure is retried at the next latch (a
second one disables); `FogPass::prepare_field` returns `field_row_disabled` (0x80040F4D) and the
proxy logs it and does not call `fault_fog_cards`; the next sample's scan leaves native cards.

**Loader placement.** `fog_field::load_family_table()` (`src/renderer/fog_field_assets.cpp`) runs
once per process from the first `MotionOutput::volumetric_fog_sector_sample` (render thread, inside
the BeginScene reader's CPU boundary and LastError restore; the loader also restores LastError),
before the first `fog_sector_frame`, which now takes the table (`family_table()`, nullptr until the
load has completed) and scans it only after the 14 compiled names miss. Path: `X3M_FOG_FAMILIES` verbatim, else `GetModuleFileNameW(nullptr)`'s
directory + `x3m\fog-families.bin`; `0` or `none` disables. The table lives in static
storage (`FamilyTable`, 256 rows; header and table read into a static buffer), never freed:
`FogSectorFrame::reason` points at a row name. One `CreateFileW` per load and per switch, closed at
once. A switch reads one packet into a buffer bounded by the validated size and frees it before
the upload. Transient memory per file-family switch: the read buffer (the packet, at most
56 + 17,846,400 bytes, 2.28 MB for every Mayhem family) plus the decoded atlas (17,846,400 bytes,
the same cached field a compiled family decodes into), so up to about 36 MB at the peak, before
the existing SYSTEMMEM upload texture. Families sharing one packet (same decoded atlas) share the
decoded field and the GPU atlas: `FogPass` keys its cache on the packet's own id and only swaps
the row constants. Per frame: one branch; per sample: up to 256 `strcmp` only when no compiled name
matched. Stored range: `FogPass::field_family` returns the row's chroma for a file family; the
placement key hashes the name-derived id, so it survives regeneration. The launcher is unchanged
(the variable passes through the environment) except for one report line: `manage.py launch`
prints `fog families: <path> present bytes=… families=… packets=…` (or `absent`, `disabled
(X3M_FOG_FAMILIES)`, `header invalid`) from the file's 64-byte header, also as `fog_families` in the
`--dry-run` JSON.

**Log lines.** Once per process:
`volumetric_fog_families device=… frame=… event=absent|disabled|loaded|rejected families=N packets=P rows_disabled=D bytes=B reason=<ok|header reason|env_disabled|absent> fallback=compiled_first|compiled_only path="…"`,
then up to 8 `volumetric_fog_family … event=row_disabled row=i name="…" profile=… reason=…`; per
switch-time failure one `volumetric_fog_family … event=row_disabled name="…" profile=… reason=packet_… fallback=native_cards`
or, for a first allocation failure, `event=switch_retry … reason=allocation`; each at most once per row.
`volumetric_fog_prepare profile=` prints a file id in decimal.

**Measured (host, 2026-09-23;
[summary](../../verification/results/fog-family-data/tool-dryrun/summary.txt), produced by
`run_tool_evidence.py` beside it).** Stock bottle X3: 16 positive families, 14 `covered_by_build`
(12 / 12 provisional palettes match the build), `earth` `texture_missing`, `xtmgreenring`
`no_dust_bodies`; dry run 1.3 s. With `--background-palette earth xtmgreenring`: one file family
(`earth`), 2,281,316 bytes, 8.9 s. Synthetic vanilla + Mayhem 3 + Renegades root
(`make_mod_root.py`, the lod-overlay-mods recipe): 56 `ok` (TBackgrounds from `addon/07.cat`), all
on the weighted path; dry run 11.9 s (jobs 4); `--out` 114,063,336 bytes, 56 families, 50 packets
(two groups of 5 and 3 families share an atlas), 114.1 s with `--jobs 1`, 39.0 s with `--jobs 4`,
files byte-identical; `--check` PASS; the host build of the DLL loader loads it and decodes all 56
rows (`decoded_ok=56`). Per family: palette 0.55 s, palette + bake 2.16 s (medians).

**Native Windows.** The same documented Win32 calls and the same location next to `X3AP.exe`
([platform-portability.md](platform-portability.md)); cross-compiled (MinGW i686, SSE2); the i686
`fog_family_file_fixture.exe` (CMake target; `--self-test DIR`, `--probe` with `X3M_FOG_FAMILIES`)
passed its self-test under Wine on bottle X3 (62 cases, the build before review round 1; evidence
`verification/results/fog-family-data/fixture-wine/`); not run natively.

**Mod flow (2026-09-25).** One entry point: `python3 tools/manage.py fog-families --bottle X3
--install` (add `--replace` over an existing file; without a mode flag it runs `--check`; any
other `fog_families.py` option is forwarded, `--game-dir` overrides the bottle). Re-run it after
installing, removing or updating a mod that ships catalogues or a TBackgrounds, and after the
LOD overlay (`lod_batch_census` → `lod_overlay --install` → `manage.py fog-families --install
--replace`). The record now carries `launch_inputs` (`tools/analysis/fog_family_inputs.py`,
standard library only): the catalogue layer list, size and mtime of every `.cat`/`.dat`
(including `--mod-cat` layers) and of the loose `types/TBackgrounds.{pck,txt}` in the base and
addon trees. Every modded `manage.py launch` prints one line and never blocks: `fog families:
missing; run … to cover mod sectors, compiled 14 names only` (no file: mod families get native
cards); `fog families: stale (<reason>; N families, M packets still load)` (the stat
fingerprint, the record's file size or the record itself differs; the file still loads, but
families a mod added since are absent and changed palettes are old); `fog families: ok (N
families, M packets)`; header truncated/invalid when the proxy would reject the file; `disabled`
under `X3M_FOG_FAMILIES=0`. Nothing under `--vanilla`. The launch comparison stats about twenty
files and reads no texture; loose bodies and textures outside the catalogues are not in it, so
`--check` (every input hash) stays the authority, and a `--check` PASS refreshes a mismatched
fingerprint (a touched or re-copied catalogue with unchanged content). Records written before
2026-09-25 lack `launch_inputs` and are compared by catalogue list only.

## Unknowns

- ~~The exact percentile convention of the stock palette derivation~~: settled, see
  "Implementation".
- What the engine draws for `earth` (texture-miss path; disassembly or a remapped flight).
- Whether 24-bit uncompressed textures change anything about how Mayhem's cards blend (they do
  not affect the palette rule; a flight will show the cards before suppression).
- Native Windows behaviour of the file read and of the pass, as for every fog feature.
