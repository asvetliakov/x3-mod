# Farnham's Legacy disassembly notes (inherited, unverified)

These 106 notes were written during the abandoned first port attempt
(branch `fl-codex`, October 2026). They are static disassembly fact sheets for
one build of `X3FL.exe`:

| field | value |
| --- | --- |
| SHA-256 | `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` |
| size | 2,804,736 bytes |
| link time | 2021-11-04 (`0x618416c8`), version 1.3.0.0 |
| preferred base | `0x400000` |

Read them with these caveats:

- **Nothing here was confirmed in the running game.** Every address came from
  offline decoding (Capstone, some Ghidra) and at most a synthetic host
  fixture. Treat each address as a candidate until a runtime log confirms it.
- **FL is relocatable.** Unlike `X3AP.exe`, the FL image has `.reloc` and
  `IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE`, so the addresses in these notes are
  preferred-base VAs: subtract `0x400000` for the RVA and add the live module
  base. The image also carries `.gfids` (Control Flow Guard metadata).
- Links into `../../architecture/` and `../../verification/` that do not
  resolve point at notes that exist only on `fl-codex`; the design and process
  notes were deliberately not carried over. The producing scripts
  (`verification/probe/verify_fl_*.py`, `compare_fl_engine.py`) are on that
  branch too.
- Five process memos (`mixed-escort`, `mixed-gate-dock`, `gate-transform-math`,
  `arrival-recommand`, `lane-command-transaction`) were left behind.

New FL findings go into new notes in this folder, written only when the code
depends on the fact and the fact has been checked against the game.

Notes added for the current port (static, checked against the hash above; not yet confirmed in flight):

- [weapon-effect-spawn.md](weapon-effect-spawn.md): the FL counterparts of the weapon-effect call sites (impact,
  inner taps, bolt flight and object tap, muzzle, death), their windows, frames and record offsets (2026-11-08).
- [bullet-draw-batch.md](bullet-draw-batch.md): the instanced bullet batch (loop, submit, records, draw shape), one
  body per draw, the traversal fallback, the per-body identity at `0x097af8`, the bullet record units and the
  corrected bolt length rule (2026-11-08).
- [muzzle-flash-draw.md](muzzle-flash-draw.md): the game's muzzle flash body (Effects CUT scene, body 445), its
  draw through the material submit, the bound state (ONE/ONE, z-write off), its animation and sharing (2026-11-09).
