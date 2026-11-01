# Farnham's Legacy resource inventory

This note records a read-only comparison of clean media at
`/mnt/c/X3AP-orig` and `/mnt/c/X3FL-orig`, made for the 1.7.3 port start.
It is an inventory, not evidence that any renderer, UI feature or game
behaviour has been ported.

**Resolver follow-up:** the [reviewed executable study](resource-resolver.md)
now establishes mounting and two reader policies. It does not support the
global namespace fallback used in this initial comparison model: callers retain
their full member keys, and extension-aware versus exact opens have different
loose-file precedence. The historical counts below remain model results, not an
engine-accurate input manifest for regeneration. Per-family caller mapping is
required before replacing that model in production tooling.

## Resolver boundary

The FL executable's configuration descriptor has patterns for root `%02d.cat`
and `addon2\\%02d.cat` (engine finding: `0x5a5ca0`, installed at
`0x509e95`; fields `+0x34` and `+0x38`).  It also names
`addon2\\mods\\%s.cat` at `+0x40`.  No `addon\\` catalogue path string was
found.  Therefore this inventory's FL headline view uses the contiguous root
`01`–`13` and `addon2/01`–`05` layers.  AP uses root `01`–`13` and
`addon/01`–`04`.

Within a candidate namespace, later numbered catalogues win and loose resource
files win over catalogue members.  The candidate-key model removes the leading
`addon/` or `addon2/` namespace, with `addon2 > addon > root`; this extends
the established AP `Assets.candidates` rule in
`tools/analysis/sector_fog_census.py`.  FL's final lookup/fallback ordering has
not yet been disassembled.  Consequently the counts below are a resolver-model
inventory, not a claim that every FL lookup behaves this way.

The probe validates each CAT's paired DAT length, applies CAT's `0x33` member
XOR and the established plain/obfuscated gzip PCK decoding.  It records only
derived paths, winner provenance, stored sizes and (where content is read)
decoded lengths and SHA-256 values; it never emits resource bytes.
`namespaced candidate paths` are not raw archive-member counts: they retain an
`addon`/`addon2` prefix but collapse case and packed aliases such as `.pck` and
`.pbb`/`.pbd` before the later logical-key selection.

## Measured inventory (2026-10-27)

| view | effective resources | namespaced candidate paths | loose winners |
| --- | ---: | ---: | ---: |
| AP root + addon | 10,424 | 11,084 | 899 |
| FL root + addon2 | 11,040 | 11,647 | 1,229 |

The FL candidate view retains 10,347 AP logical paths, adds 693 and lacks 77.
For the selected port-relevant families, 3,987 shared decoded resources were
compared: 2,103 are byte-identical and 1,884 differ.  The remaining 6,360
shared paths are mesh/texture/other content whose bytes were deliberately not
read; their path and winner provenance remain counted.

| family | AP | FL | FL new | shared decoded changes |
| --- | ---: | ---: | ---: | ---: |
| shaders | 2,352 | 2,352 | 0 | 1,128 |
| effects | 362 | 363 | 1 | 4 |
| UI/load screens | 47 | 47 | 1 | 9 |
| scripts/director | 1,228 | 1,573 | 413 | 720 |
| types | 55 | 58 | 3 | 23 |
| fonts | 12 | 12 | 0 | 0 |

The 77 AP-only resolver-model paths split into 68 scripts, 1 UI path and 8
other paths.  The 10,347 shared paths establish only that both media views
resolve a resource under the same logical name; they do not identify the
winning FL layer and therefore do not quantify a root-data dependency.  Nor
does this inventory establish a runtime dependency on the physical `addon/`
directory: that directory is not currently in the FL executable's evidenced
catalogue set.

`addon2` contains both `addon2/`-prefixed members and global paths (including
shader, object and DDS members), so it cannot be treated merely as an isolated
new namespace.  The shader corpus has the same 2,352 effective names but 1,128
decoded replacements; port code must not reuse AP shader byte assumptions.

## Legacy-addon sensitivity

For packaging investigation only, adding FL's physically present `addon/01`–
`04` before `addon2` produces 11,117 effective resources and 12,590 namespaced
candidate paths: 77 additional logical paths and 59 decoded replacements
relative to root + addon2.  This is not the headline view and must not be used
to claim that FL mounts `addon/` until the resolver loop is traced.

## Graph-script names

The FL comparison exposes new graph helper names including
`!lib.graph.adjustdata`, `!lib.graph.build.axis`, `!lib.graph.build.data`,
`!lib.graph.build.series`, `!lib.graph.get.extremes`,
`graph.data.collector`, `graph.draw.networth` and the `graph.draw.multi.station.*`
set.  It also changes the existing logical scripts `graph.get.data` and
`!lib.graph.getkeycolours`, with player/property graph scripts such as
`graph.pl.property` and `graph.pl.station.trade` in the changed set.

These filenames establish only resource presence/change.  No rendering
dataflow, script call graph, UI control relationship or native graph API is
established by this inventory.

## Reproduction

```sh
PYTHONPATH=tools/analysis python3 tools/analysis/compare_fl_resources.py \
  --ap /mnt/c/X3AP-orig --fl /mnt/c/X3FL-orig \
  --output verification/results/fl-port/resource-inventory.json
PYTHONPATH=tools/analysis python3 tools/analysis/compare_fl_resources.py \
  --ap /mnt/c/X3AP-orig --fl /mnt/c/X3FL-orig \
  --output verification/results/fl-port/resource-inventory.json --check
```

The producing JSON is ignored at
`verification/results/fl-port/resource-inventory.json`; `--check` validates
its schema, winner records, comparison arithmetic and category totals without
rereading the media.  The output destination is rejected if it overlaps either
game input.  Remaining work is to trace FL's catalogue enumeration, loose-file
phase and namespace fallback, then replace the model boundary above with
executable evidence.
