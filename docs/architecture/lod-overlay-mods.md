# Merged-LOD overlay and mods: selected packages, launch check, rebake policy

Design note, 2026-09-25. **Ratified 2026-09-25 by the main session** (derived package copy, launch line, incremental rebake with an opt-in budget). Read-only work: no bake, no census over a real tree,
no Wine. Every figure is marked measured (m) or inferred (i). Owning design:
[merged-lod-feasibility.md](merged-lod-feasibility.md) ("Overlay tooling", "Batch mode");
ledger [lod-overlay.md](../verification/lod-overlay.md).

**Outcome.** Keep the numbered-slot overlay as the only overlay for every body the numbered
catalogues win, and add a *derived package* for a selected `addon/mods/<name>.cat`: the baker
copies the package's members verbatim into `addon/mods/<name>-x3m-lod.cat/.dat`, replaces the
package's own bodies (and the numbered overlay's bodies whose textures the package overrides)
with merged ladders baked with the package as the top layer, writes a marker beside it that
records both its own hashes and the source package's fingerprint, and the user selects the
derived package in the start menu; the selection is autodetected from the registry value
`HKCU\Software\EGOSOFT\X3AP\ModName` unless `--mod <name>|none` is given. The launcher prints
one `lod overlay:` line computed from the markers, 38 stats, 19 small cat hashes and one
registry line (measured 4-9 ms + 48-50 ms marker parse + 1 ms), never blocks, and never runs the
census; rebakes stay user-triggered through the existing `--sync` incremental path keyed on
`inputs_sha256`, with an opt-in byte budget and a `--trust-tool` switch so a tool edit that does
not change the bake does not cost the 49 min full rebuild again.

## 0. What is already implemented (verified in source)

| requirement (memory 2026-09-23) | where | status |
|---|---|---|
| overlay slot chosen dynamically | `next_slot` `tools/analysis/lod_overlay.py:602-612` (contiguous from 01, next free); batch reuses the previous overlay's top slots else takes the next free (`lod_overlay.py:1969-1976`) | done; the installed overlay is `addon/05`+`06` above vanilla 01-04 (m, bottle listing) |
| survive a mod overwriting the slot | `installed_markers` `lod_overlay.py:482-521`: a marker whose recorded cat/dat sha256 differ from the files beside it is `orphaned`; `original_assets` `:551-568` reads an orphaned slot as a mod source and skips live/retired/unreadable ones; `--install` removes orphaned markers (`:1730-1733`); legacy pilot markers are proved by member sha256 (`legacy_verified` `:524-548`) | done; tests `test_split_reuse_rollback_shrink_orphan` (`verification/analysis/test_lod_overlay_batch.py:704`), `test_legacy_marker_orphaned_unless_verified` (`:411`), `test_mods_warning_and_single_mode_markers` (`:583`) |
| mod ships and stations from numbered catalogues | `lod_batch_census.run` `tools/analysis/lod_batch_census.py:524-541` walks `body_keys` / `text_body_keys` (`:510-524`) of `lod_overlay.original_assets`; the winner is `entries[-1]` (`:499`, `bob1.resolve_body` `tools/analysis/bob1.py:789-798`), and `sector_fog_census.Assets` appends layers in the order base `01..13`, `addon/01..NN` sorted, then explicit `mods` (`tools/analysis/sector_fog_census.py:66-73`), so the highest addon number wins, matching the engine rule ([body-format-bob1.md §7](../reverse-engineering/body-format-bob1.md)) | done; the mod census found 2,974 mod-catalogue winners, 1,025 eligible with text bodies (m, feasibility.md:1193-1202) |
| text bodies | `bob1.parse_text`, census `include_text` (`lod_batch_census.py:514-524`) | done |
| selected `addon/mods` package | `lod_overlay.py:1982` globs `addon/mods/*.cat`; `:2091-2100` prints `warning: addon/mods/<name>.cat (N bodies) overrides the overlay for K of its M bodies while that mod is selected in the launcher` | warning only: this note |
| launch-time staleness | `tools/manage.py` prints `fog families:` (`manage.py:185-203`, called `:2045`) but nothing about the overlay | open: this note |

The two example trees use no package: `/tmp/x3-mod1/addon` holds `05..12.cat` (`- Catalog
Files.txt`: 05-09 Litcube's Universe, 10 Mayhem main, 11 Mayhem graphics, 12 Zero Hour; dats
673 MB, 1.74 GB, 6 MB, 225 KB, 20 B, 6 MB, 607 MB, 82 MB), `/tmp/x3-mod2/addon` holds `12.cat`
(85 MB) plus `scripts` and `t`; neither has `addon/mods` (m, listing only). So the requirement's
own examples are covered by section 0; the selected-package case is the residual.

## 1. Selected mod packages

### Engine facts

- Mount: `0x004ec9e0` counts `%02d.cat` then `addon\%02d.cat` until the first missing number and
  reserves **one extra slot**; `0x004ede00` loads `addon\mods\%s.cat` (path table `0x0057c008+0x40`)
  into that slot `G+0xc8 − 1` when `G+0x79c` (the mod name) is set
  ([body-format-bob1.md](../reverse-engineering/body-format-bob1.md) table rows `0x004ec9e0`,
  `0x004ede00`; [loading-orchestration.md](../reverse-engineering/loading-orchestration.md) call-chain
  rows `0x004ec9e0`, `0x004ce080`). The resolver walks slots from `G+0xc8 − 1` down and stops at the
  first catalogue holding the stem ([body-format-bob1.md §7](../reverse-engineering/body-format-bob1.md),
  [texture-lookup.md §6](../reverse-engineering/texture-lookup.md)). Consequences (i from that code,
  not observed in game): exactly one package is mounted, it outranks every numbered slot, and **no
  numbered slot can win over a selected package**; a package member wins for its own stems only,
  every other stem still resolves to the numbered overlay.
- Bodies share one `objects\` namespace across all catalogues (body-format-bob1.md, "Installed tree",
  m over the 17 vanilla catalogues), and a selected package may omit `addon/` for `objects`, `dds`,
  `types`, `maps`, `t` (`sector_fog_census.resource_key` `:45-48`, from
  [sector-fog-census.md](../reverse-engineering/sector-fog-census.md) line 9).
- Where the name comes from (m, this session): the EXE's `.rdata` holds `ModName` at `0x0054bd6b`,
  `addon\mods\%s.cat` at `0x0054c00d`, `Software\EGOSOFT\%s` at `0x00561705` and the enumeration
  pair `addon\mods` `0x00562619` / `*.cat` `0x00562611` (file offsets mapped through the section
  table). The bottle's `user.reg` carries `[Software\\EGOSOFT\\X3AP] "ModName"=""` (m). The settings
  loader `0x004b6f60` reads that key value by value and the saver `0x004b7b40` is called from
  `0x004ce05f` ("launcher start", [alternative-video-playback.md](../reverse-engineering/alternative-video-playback.md)
  rows Load/Save), and the mod enumeration `0x004ce080` sits right after that caller; that the start
  menu's package choice is stored as `ModName` and loaded into `G+0x79c` is therefore **inferred**
  (body-format-bob1.md "Unknown": the `G+0x79c` store was not traced). Disassembly to settle it: xrefs
  to `0x0054bd6b` inside `0x004b6f60`/`0x004b7b40`, and the store to `G+0x79c` (expected in the
  post-load path of `0x00402ee6..0x00402f3e` or in `0x004ce080`'s selection handler).

### (a) Where the overlay entries go: derived package copy (recommended)

With `--mod <name>` (or autodetect, (c)) the baker:

1. Reads the package as the top source layer: `Assets(root, mods=[addon/mods/<name>.cat])`
   (`sector_fog_census.py:60`), threaded through `original_assets` and `census.run`/`_init`
   (`lod_batch_census.py:492-493`); the census key set is unchanged, the winners change.
2. Picks the *package-affected* bodies: every eligible body whose stem the package holds, plus every
   numbered-overlay body one of whose `texture_sources` (a census row field, m in the batch record)
   the package overrides. Only those bodies are baked in the package view.
3. Writes `addon/mods/<name>-x3m-lod.cat/.dat`: every package member copied byte for byte (the dat
   is a concatenation of XOR-0x33 members; the copy is I/O only), minus every body member of an
   affected stem (any of `.pbb .bob .pbd .bod`), plus the merged `.pbb` and atlas members of the
   affected bodies. The single dat must stay below 2^31 − 1 B (same `fopen`/`fseek(long)` limit as
   the numbered slots); a package that does not fit is refused, because a package cannot be split
   (only one is mounted).
4. Leaves the numbered overlay exactly as it is: while the package is not selected the numbered
   ladders draw; while `<name>-x3m-lod` is selected the copy wins for the affected stems and the
   numbered slots win for everything else (i from the resolver rule).

The user then selects `<name>-x3m-lod` in the start menu. The name is what the menu lists (the
enumeration is `addon\mods\*.cat`, so the file stem is the identity; whether the menu shows the stem
verbatim is inferred and is checked once by the user in an ordinary launch). ASCII, no dot inside the
stem (the `%s.cat` template makes a dotted stem ambiguous), `-x3m-lod` suffix so `ModName` alone tells
the tools that a derived package is selected.

Options that lose:

- *Merge the whole overlay into the package copy.* The installed overlay is already 2.72 GB
  (05: 1,992,076,927 B, 06: 730,760,949 B, m) and cannot be split inside one package (2^31 limit);
  it would also duplicate the numbered slots.
- *Patch the package in place.* Destroys the user's mod file, is not reversible and violates the
  read-only rule for mod trees (memory 2026-09-23 incident).
- *Keep the warning only.* Correct for the two example trees (no package), but a selected package
  silently loses the overlay for its own ships, which are the ones a total-conversion package ships.
- *Numbered slot above the package.* Impossible: the package is the top slot by construction.

### (b) Marker, orphan and uninstall for the package side

Marker `addon/mods/<name>-x3m-lod.x3m-lod.json` (the existing `MARKER_SUFFIX` beside the cat, as for
numbered slots; the game's enumeration only lists `*.cat`, so the marker is invisible to it). Fields:
the existing ones (`slot: null`, `overlay_sha256 {cat,dat}`, `bodies`, `batch.settings` with
`tool_sha256`, `originals`, `originals_sha256`, `originals_mode`) plus `package: <name>`,
`package_fingerprint {cat: sha256, dat: size:mtime | sha256 under --hash-archives}` and
`package_members: N`. `installed_markers` also globs `addon/mods/*.x3m-lod.json` and gives:

| status | meaning | baker | launch line |
|---|---|---|---|
| `valid` | own cat/dat hashes match | reused by `--sync` (members by sha256, as `reuse_previous`) | intact |
| `stale` | own hashes match, source package fingerprint differs (mod updated) | rebuilt from the new package on `--sync`; refused without `--sync`/`--replace` | "rebake" |
| `orphaned` | own files gone or differ | reported, removed on `--install`; the cat is never a source | "foreign package copy" |
| `source_missing` | package `<name>.cat` gone (mod uninstalled) | reported; `--install` removes the copy unless `--keep-package-copy` | "remove" |

The copy is never a body source (it is ours), the source package is a source only in the package
view, and the numbered `originals_sha256` digest excludes `addon/mods/*` as today
(`original_archives` `:402-407` globs numbered catalogues only). Uninstall: `lod_overlay.py
--remove-package <name>` deletes the three files and tells the user to re-select `<name>` in the
menu (what the engine does when `ModName` names a missing cat was not traced: unknown, harmless at
worst = no mod; the user re-selects in any case). Contiguity, retirement and the multi-slot logic do
not apply to packages.

### (c) Selecting the mod for the bake

`--mod <name>|auto|none`, default `auto`: read `ModName` from `HKCU\Software\EGOSOFT\X3AP`
(CrossOver: the `[Software\\EGOSOFT\\X3AP]` block of the bottle's `user.reg`, a text file of 85 KB,
scanned for that one line, 1.0 ms m; native Windows: `winreg`, same key; the value's text form
`"ModName"=""` shows a string). Empty = no package. A `ModName` ending in `-x3m-lod` maps back to
its source through the marker's `package` field (or is reported as an unknown package when the
marker is gone). `--mod <name>` overrides the registry; `--mod none` bakes the numbered overlay
only and prints today's warning. The sector-fog census deliberately does not infer the registry
selection (sector-fog-census.md line 9); here the inference is safe because the choice only adds a
derived package and never changes the numbered overlay.

### (d) Switching packages in the menu

- User selects another package `<other>`: the copy is not mounted; the numbered overlay applies to
  everything `<other>` does not hold; `<other>`'s own bodies draw the mod's LODs. Harmless; the
  launch line names it ("package <other> selected, no derived package: K overlay bodies shadowed").
- User selects the original `<name>` instead of the copy: the copy is inert; same as above.
- User updates the mod and keeps `<name>-x3m-lod` selected: **stale package copy**: the game plays
  the *old* package content (scripts, types, textures) until the copy is rebaked. This is the one
  real hazard of the design; it is named on the launch line (marker `stale`) and cleared by `--sync`.
- User uninstalls the mod: `source_missing`; the copy still mounts and keeps the mod alive by
  accident. Named on the launch line; removed by `--remove-package`.

## 2. Staleness at launch

One line in `tools/manage.py` next to `fog families:` (`manage.py:2045`), printed on every launch
including `--dry-run` (the report block runs before the launch decision at `:2138`) and under
`--vanilla` (the overlay is game data, not a DLL feature). Implemented as a small module
`tools/analysis/lod_overlay_check.py` (json, hashlib, os.stat only; no bob1/numpy) that
`lod_overlay.py` imports for `original_archives`, `fingerprint_files` and `originals_digest`
(`lod_overlay.py:402-407`, `:582-592`, `:478-479`) so there is one definition, and that also runs as
`lod_overlay.py --check`. On native Windows the same function is the check (manage.py is CrossOver
only).

What it reads (cost m on this machine, game closed): the markers (`addon/*.x3m-lod.json`,
`addon/mods/*.x3m-lod.json`; the two installed ones are 14.6 MB and parse in 48-50 ms), `stat` of every
`NN.cat/.dat` and `addon/NN.cat/.dat` (38 files) plus sha256 of the 19 cats (677,527 B): 3.5-8.6 ms
over two runs; the registry line: 0.5-1.0 ms (script and output:
`verification/results/lod-overlay-mods/launch_check_cost.py`, `_out.txt`); one cat read per catalogue above the overlay or per selected package (Mayhem's
largest cat is 153 KB). Total under 0.1 s.

What it reports, in one line, first match wins per clause:

```
lod overlay: addon/05-06 intact (620 bodies, 2.72 GB); sources unchanged; no mod package
lod overlay: addon/06 overwritten by a mod (marker orphaned); addon/05 intact; rebake with --sync
lod overlay: addon/05-06 intact; addon/13 above the overlay holds 41 overlay bodies; rebake with --sync
lod overlay: addon/05-06 intact; sources changed since the bake (addon/12.dat); rebake with --sync
lod overlay: addon/05-06 intact; package Foo selected (no derived package: 37 overlay bodies shadowed)
lod overlay: addon/05-06 intact; package Foo-x3m-lod selected, stale (Foo.dat changed): rebake
lod overlay: none installed
```

"Intact" = own cat/dat sha256 (cat) and dat fingerprint match the marker; the dat is *not* hashed
(2 GB), it is matched by `size:mtime_ns` as the marker's own `originals_mode: fingerprint` does (m,
marker field), so a rewrite with identical size and mtime is missed, the limitation the baker already
accepts. "Sources changed" = `originals_digest` over the current fingerprints of every numbered
catalogue outside our slots differs from the marker's `originals_sha256`; the current marker stores
only the count (`originals: 34`, m) and the digest, so naming the file needs a new marker field
`originals_fingerprints` (34 entries, about 3 KB) written at the next bake; until then the line says
"sources changed since the bake". "Above the overlay" = an `addon/NN.cat` with NN above the highest
live slot; its member stems are intersected with the marker's body list.

Must not: run the census, open a dat, read a body, hash a dat, write anything, block or change the
exit code, or read the registry for anything but `ModName`. It is a report, the same contract as the
fog-families line.

## 3. Rebake trigger and cost

Measured baseline (install-fleet4, `addon/x3m-lod-batch-summary.txt` line 10 and the markers, m):
620 bodies, census 318.2 s, bake 2,618.4 s with 2 jobs = 4.22 s per body wall, total 2,936.7 s
(49 min); atlases 571 at 1024² and 49 at 2048²; per-body overlay bytes slot 05 p50 2.85 MB, p90
8.39 MB, max 20.6 MB, slot 06 p50 5.64 MB, p90 9.45 MB; 2.72 GB in two slots; the texel rule ran at
`screen_width` 1800 = 1080·1280/768 for `--display 1920 1080` (`effective_width`
`lod_overlay.py:367-372`). Mod set (feasibility.md:1198-1202, m census, i sizes): 1,025 eligible,
593 at 1024², 427 at 2048², about 11.3 GB of atlases at the 2048² cap, so about six slots of 2 GB.
Projection (i): first full bake of the mod set 1,025 × 4.22 s ≈ 72 min at 2 jobs, more because
2048² bodies are 8.7× more frequent than in the fleet and bake slower (no per-size timing exists;
plan for 1.5-2 h); census about 10 min (4,613 bodies enumerated with text against 2,453). A Mayhem
update like `/tmp/x3-mod2` (`addon/12.cat`, ~175 ships + 30 stations, memory) rebakes at most those
bodies plus any body whose atlas reads a texture the update ships: about 205 × 4.22 s ≈ 14 min plus
the census.

Recommended policy:

1. **Trigger = the user, told by the launch line.** No bake at launch, ever (49 min, and the census
   alone is 5-10 min). The line says "rebake with --sync"; the user runs
   `lod_overlay.py --batch --sync --install --replace` with the game closed.
2. **Incremental by `inputs_sha256`** (exists: `reuse_previous` `lod_overlay.py:1898-1953`, members
   copied and verified by sha256 from any live slot). Two changes make it pay off for mods:
   `--trust-tool`: reuse bodies although `settings.tool_sha256` differs (today any edit of
   `lod_overlay.py`, `lod_atlas.py`, `bob1.py` or `lod_batch_census.py` rebuilds every body,
   `:1906-1909`; install-fleet4 paid 49 min for a recipe that touches one body, ledger 2026-09-25);
   and a package view reuses numbered-slot members for bodies whose inputs are unchanged under the
   package (the hash is over decoded body + textures, so a body the package does not touch hashes
   the same).
3. **Opt-in byte budget, not a default cut.** `--budget-bytes N` keeps bodies in priority order
   (stations and ships before `other`, then draws saved per overlay byte from the census row's
   `estimate`) and refuses the rest with reason `budget`, listed in the summary. Default: no budget;
   the expected 11.3 GB / six slots is what the mod set costs, and the user decides with the number
   in front of him. Numbers for the switch: a 8 GB budget (four slots) keeps the whole fleet
   (2.72 GB) and about 480 mod bodies at the 11 MB average of the mod set (i).

Options that lose: *bake only referenced bodies* (walk `TShips`/`TDocks`/`TFactories` scenes to the
bodies the selected content can instantiate) needs a scene-graph parser the tools do not have and
saves an unmeasured amount on a mod that ships its ships to use them; keep it as a later filter if the
budget bites. *Cap mod bodies at 1024²* halves the bytes for 427 bodies but breaks the texel rule the
user set for 1920 wide. *Per-slot budget* is already the 2^31 split and says nothing about the total.

Hot path and native behaviour: the overlay is game data; the game does the same CAT parse per mounted
catalogue as today, the derived package replaces the original in the single mod slot (same member
count plus the merged bodies), and no DLL code is involved. Windows: identical file layout; the
registry read goes through `winreg`; the launcher line is CrossOver-only, `lod_overlay.py --check`
is the portable form.

## 4. Stale documentation to fix with the implementation

| where | says | should say |
|---|---|---|
| `docs/architecture/merged-lod-feasibility.md:293` | "`NN` one past the highest installed addon slot (`addon/05` today; …)" | the next contiguous free slot (`addon/07` with install-fleet4 in 05-06), batch reuse of the previous overlay's top slots |
| `merged-lod-feasibility.md:300-301` | "`addon/mods/` is not used because a mod package is searched only when selected in the launcher" | used only as the derived package copy of a selected package (this note) |
| `docs/goals.md:29` row 18 "Next" | "the fleet batch (346+ eligible bodies …, mod catalogues supported, 1920-wide texel rule) and one busy-sector flight" | the fleet batch is installed (install-fleet4: 620 bodies in addon/05-06, run321 flown); numbered mod catalogues are census sources; open: selected packages, launch line, mod-tree bake |
| `docs/reverse-engineering/body-format-bob1.md:385-387` "For the overlay builder" | "Write … into a new `addon\05.cat`… 05 is the next free number" | the next free number is computed (`next_slot`); keep the contiguity and the package-overrides note |
| `tools/analysis/lod_overlay.py:297-298` docstring and `:2099` warning | packages are detected and warned about | describe the derived package and `--mod` |
| memory `lod-overlay-mod-support.md` ("an overlay pinned to addon/05 is overwritten by mod1's own 05.cat") | motivation | not a doc; unchanged |

## 5. Verification plan (synthetic roots only; no real-tree bake tonight)

All on `make_game` fixtures (`verification/analysis/test_lod_overlay_batch.py:88`) with
`write_catalogue`, as `test_mods_warning_and_single_mode_markers` (`:583`) already builds
`addon/mods/Big.cat`:

1. Package bake: root with `addon/01..02` + `addon/mods/Big.cat` holding a body that shadows a
   numbered one and a texture a numbered body reads. `--batch --mod Big --dry-run --out X` writes
   `X/addon/mods/Big-x3m-lod.{cat,dat}` + marker: cat lists every Big member except the affected
   stems' body members, plus the merged `.pbb` and atlases of both affected bodies; the numbered
   overlay in `X/addon/NN` is byte-identical to a `--mod none` run; a package whose copy would
   exceed the dat limit is refused with nothing written.
2. Marker statuses: `valid`; edit the copy → `orphaned`; touch/replace `Big.dat` → `stale`; delete
   `Big.cat` → `source_missing`; `installed_markers` never reads the copy as a source.
3. Selection: a fake `user.reg` snippet (CrossOver path override for tests) → `auto` picks `Big`,
   empty → none, `Big-x3m-lod` maps to `Big` through the marker, `--mod none` overrides.
4. Launch line: `test_fog_families.test_launch_line_missing_stale_ok` (`:326`) as the model; cases
   none installed / intact / slot overwritten / `addon/13` above with K overlapping stems / sources
   changed (digest, then the named file once `originals_fingerprints` exists) / package selected
   foreign / derived stale; `manage.py --dry-run` prints it and exits 0 in every case; assert no
   dat was opened (patch `open` or count `stat` vs `read` calls) and under 0.2 s on the fixture.
5. Budget: `--budget-bytes` drops the lowest-priority bodies with reason `budget`, summary line and
   record field; `--trust-tool` reuses members across a `tool_sha256` change and the summary says so.
6. Unchanged: the 49 + 6 existing tests, `test_lod_recipes`, `test_bob1`.

Real-tree proof later, in the user's hands: one `--batch --sync --dry-run` over the bottle (census
only) to see the package line, then the fleet+mod bake when disk and time allow, then a launch whose
start menu shows `<name>-x3m-lod`.

## 6. Implementation brief (for an `implement` agent)

- `tools/analysis/lod_overlay_check.py` (new, light imports): `original_archives`,
  `fingerprint_files`, `originals_digest` moved here and re-imported by `lod_overlay.py`;
  `read_mod_name(bottle_or_registry)`; `overlay_line(game, mod_name) -> str`; `--check` CLI.
- `tools/manage.py`: print `overlay_line` beside `fog families:` (`:2045`), also under `--vanilla`.
- `lod_overlay.py`: `--mod <name>|auto|none` (default auto); `original_assets(game, markers, mods=())`
  and `census.run(..., mods=())`; package-affected body set (stem in package or `texture_sources`
  overridden); derived package writer (copy members, replace affected stems, dat limit, marker with
  `package`, `package_fingerprint`, `package_members`); `installed_markers` over `addon/mods/*` with
  the four statuses; `--remove-package`; `--trust-tool`; `--budget-bytes`; marker field
  `originals_fingerprints`.
- Docs: the section 4 table; ledger entry in `docs/verification/lod-overlay.md` with the test counts.
- Acceptance: `PYTHONPATH=verification/probe:verification/analysis:. python3 -m unittest
  test_lod_overlay_batch test_lod_batch_census test_fog_families` green with the new cases; no real
  tree touched; `/tmp/x3-mod1`, `/tmp/x3-mod2` read-only.

## Unknowns

- `ModName` → `G+0x79c` (section 1, inferred; one Ghidra xref query settles it).
- Whether the start menu lists the file stem verbatim and how the engine behaves when `ModName`
  names a missing cat (one user launch; nothing in the tools depends on it).
- Bake time per 2048² body (the fleet had 49; a `--only` dry bake of ten 2048² mod bodies would give
  it) and the real disk figure of the mod set (11.3 GB is the census estimate).

## Implementation (2026-09-25)

Implemented on synthetic trees only (no bake or census over the bottle or `/tmp/x3-mod1`, `/tmp/x3-mod2`).
Ledger: [lod-overlay.md](../verification/lod-overlay.md), "2026-09-25 selected packages, launch line, rebake policy".

- **Launch check** `tools/analysis/lod_overlay_check.py` (standard library only; owns `original_archives`,
  `hash_files`, `fingerprint_files`, `originals_digest`, which `lod_overlay.py` imports). `overlay_line(game)`
  gives `lod overlay: <state> (<clauses>)[; rebake with `<command>`]`, state the worst of orphaned >
  source_missing > stale > ok, or none. Printed by `tools/manage.py launch` on stderr beside `fog families:`
  and in the `--dry-run` JSON (`lod_overlay`) for every modded launch; **nothing under `--vanilla`** (the brief
  overrides section 2's "also under --vanilla"). Also `lod_overlay.py --check` and the module's own CLI.
  "Intact" = the marker's cat sha256 plus the dat size (`overlay_dat_bytes`, else the `batch.slots` bytes); the
  dat is never opened (`--hash-archives` hashes it). New marker fields written by the baker:
  `originals_fingerprints` (game-relative path -> `size:N:mtime_ns:M` of every original catalogue file), so the
  line names the changed file, and `overlay_dat_bytes`. An older marker falls back to its path-keyed
  `originals_sha256` digest ("sources changed since the bake", no file named). A catalogue above the highest
  live slot is its own clause with the count of overlay bodies it holds. ModName: a text scan of the bottle's
  `user.reg` of the bottle the game directory sits in (a game directory outside a bottle reports `ModName
  unknown (no bottle registry)` and no package; `winreg` on native Windows). A selected plain package that shadows overlay bodies without a derived copy is `stale` (the line
  suggests `--mod <name>`). Real bottle (measured, `verification/results/lod-overlay-mods/launch_line_cost.py`):
  `ok (620 bodies in slots 05/06, 2.72 GB; sources unchanged; no package: ModName empty)`, 48-60 ms, 19 cats +
  2 markers + user.reg opened, no dat.
- **Selected package** (`--mod NAME|auto|none`, default auto; `--registry USER_REG`): a second census with the
  package as the top layer (`census.run(..., mods=[cat])`; `package_keys` drops the numbered key of a stem the
  package holds so each stem gives one row) over the held stems plus the eligible numbered bodies whose
  `texture_sources` the package overrides (compared by path without extension, conservative). The derived copy
  is streamed (kept members copied as stored bytes, the XOR is per byte), the CAT index via
  `sector_fog_census.catalogue_index`. Two additions to the design: a texture-overridden numbered body the
  package view cannot bake is **restored** as its original body in the copy (marker `restored`), so the old
  texture's atlas does not draw while the package is selected; and derived-package markers are read by
  `lod_overlay_check.package_markers` (not `installed_markers`, whose consumers group numbered slots).
  `--dry-run` writes no package (the record carries the layout). Refusals before any write: a dat above
  2^31 - 1, a duplicate member name, an explicit `--mod` that does not resolve; on write: an existing stale copy
  without `--sync`/`--replace`, a foreign file at the copy's name without `--replace`, a source package that
  changed during the run. The package copy is written first and put back if the numbered write fails.
  `--install` removes orphaned package markers and copies whose source is gone (`--keep-package-copy`).
- **Rebake policy**: `--trust-tool` (with `--sync`) reuses bodies whose settings differ only in `tool_sha256`;
  `--budget-bytes N` applies after baking on the actual member bytes (numbered and package bodies in one
  priority order; a body that does not fit is skipped and the next tried), so it saves disk, not bake time.

## Implementation: the regenerate executable

User requirement (2026-09-25): the whole mod flow is one bundled executable in the game root that regenerates
everything and overwrites the previous results. `tools/regenerate/x3m_regenerate.py` (importable, `main()`), user
guide [docs/user/regenerate.md](../user/regenerate.md), ledger [lod-overlay.md](../verification/lod-overlay.md),
"2026-09-25 x3m-regenerate".

- **Flow.** Game directory = the executable's directory when frozen (`sys.frozen`), else `--game-dir` or the
  current directory. Refuses (exit 1, nothing written) without an `X3AP.exe` there or while the game runs
  (`tasklist /FO CSV /NH` on Windows, `game_guard` over `ps` elsewhere; an unreadable process table refuses too);
  the same check replaces both tools' `running_game`, so their pre-write re-checks use it. Steps: (1)
  `fog_families.main(--install --replace)`; (2) `lod_overlay.main(--batch --sync --mod auto --install)`; (3)
  `fog_families --check`, which passes and refreshes the fog record's `launch_inputs` after step 2 changed the
  numbered catalogues (without it every run would end with the launcher's `fog families: stale`). A failing step
  is logged with its traceback and the next step still runs; step 3 only after both succeeded. Exit 0 only
  when every step succeeded; the run always ends at the key wait (`msvcrt.getch`, a raw tty read, else
  `input()`) unless `--no-wait`.
- **Console and log.** Each console line is timestamped and teed into `<game>/x3m-regenerate.log` (rewritten
  per run); the tools' own stdout/stderr (fog family table, the LOD batch summary with its per-body texel rows)
  goes to the log only, prefixed `|`. Per-item lines come from two progress hooks added for this:
  `fog_families.progress(name, done, total, result)` in `derive` and `lod_overlay.progress(name, done, total,
  package, result)` in the new `lod_overlay.bake_rows`, which replaces the two `pool.map` calls with
  `imap_unordered` (results returned in row order, so the batch output is unchanged) and also serves the serial
  path. `processing model <body> (i/n)` is printed as each body finishes; bake refusals follow at once,
  census refusals, the package/ModName notes, the slot plan and the totals are read back from
  `addon/x3m-lod-batch.json`.
- **Selected mod.** `lod_overlay_check.read_mod_name(None, game)`, the function `--mod auto` uses: the bottle's
  `user.reg` when the game directory is under a `drive_c`, else `winreg` HKCU\Software\EGOSOFT\X3AP on Windows;
  unknown is said on the console and bakes no package. The same package (for a selected `<src>-x3m-lod` copy,
  its source from the copy's marker) is passed to the fog step as `--mod-cat addon/mods/<name>.cat`, so a
  nebula family only the package defines is covered; the console prints `fog layers: ...` from the record.
- **Jobs.** `--jobs` defaults to CPU count - 1 (fog). The LOD bake takes `min(jobs, lod_overlay.default_jobs(),
  RAM // 7 GiB - 1)`: a worker on the largest stations peaks near 7 GB, and `lod_overlay.host_memory_bytes`
  (sysconf) has no Windows form, so the executable reads RAM with `GlobalMemoryStatusEx` there.
- **Interrupted writes.** Not temp-and-rename for the overlay: `commit_outputs` moves the previous slot files
  aside (`.x3m-replaced`), writes the new catalogues in place and restores on any exception, including
  Ctrl+C. A hard kill during the write (closing the console window mid-write, power loss) leaves the asides;
  the executable's first step puts every `addon/*.x3m-replaced` and `addon/mods/*.x3m-replaced` back over its
  original name (replacing a partial new file, as the in-process rollback does), logs `recovered interrupted
  write of addon/NN.cat` and proceeds; the restored overlay is then reused by `--sync`. Not covered: a new slot
  above the previous overlay (no aside) that was half written stays and reads as an orphan or mod catalogue
  until the next run replaces it. The fog pair is
  written as `.tmp` + `os.replace` with the previous pair moved to `.previous` and restored on failure.
- **Bundle.** `tools/regenerate/x3m_regenerate.spec` (PyInstaller one-file, console) with the tool modules as
  hidden imports and the hashed sources (`lod_overlay.TOOL_FILES`, `lod_recipes.py`, `fog_families.py`,
  `bake_fog_fields.py`, `fog_field_recipe.py`) as data beside them, so the frozen tool records the same
  `tool_sha256` / baker / recipe hashes as a source checkout (measured: identical records and outputs on the
  synthetic root, so `--sync` reuses across source and bundle). Not bundled: the flight radius logs and sector
  censuses under `verification/results/` (informational rows of the batch summary only). `tools/regenerate/
  build.py` builds and smoke-tests the host bundle; `--windows` builds `x3m-regenerate.exe` with Windows
  Python 3.12.10 x64 + PyInstaller in the dedicated CrossOver bottle `X3M-Build` (win10_64 template) and runs
  the smoke test there.
- **Cross-platform bytes.** On the synthetic root the Windows exe's overlay differs from the macOS one in 4
  bytes of each DXT diffuse/light atlas and in the gzip stream of the bump atlas (identical decoded bytes);
  geometry members and the fog file are identical (measured). Reuse compares inputs and settings, not outputs,
  so this does not trigger rebakes.
