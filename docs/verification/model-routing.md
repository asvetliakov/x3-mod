# Model routing evidence

Ledger for routing comparisons over real checkpoints (`AGENTS.md`, "Compare
routing over real checkpoints"). Append; do not mirror into status or goals.

## 2026-09-22: triage A/B on the Opus 5.5 release

Blind re-triage of three flown, already-ledgered runs, same brief per case,
agents forbidden from reading `docs/` and `verification/results/`. Ground
truth: `temporal-resolve.md` "Run 244", `../architecture/engine-frame-time.md`
"Run 245-247" and "Run 248". Read-only, no Wine. Sonnet and Opus-medium arms
ran as general-purpose agents with the triage system text inlined (a new agent
definition does not register mid-turn); the Opus-low arm was the `triage`
definition itself.

| Case | Opus 5.5 low (tokens / tool calls / s) | Opus 5.5 medium | Sonnet 5 medium |
| --- | --- | --- | --- |
| A run244 bursts, SETA leg, gate6, unmatched-static, cuts, mode | 23k / 5 / 71 | 54k / 14 / 99 | 67k / 25 / 150 |
| B run245-247 lod-scale ladder, model 000053a0, timing, extra ship | 32k / 7 / 50 | 79k / 12 / 112 | 77k / 28 / 171 |
| C run248 draws/frame, scene passes, buckets, cost estimate | 34k / 19 / 111 | 91k / 25 / 193 | 82k / 21 / 143 |

Correctness against the ledgers:

- Opus 5.5 low: every asked number matched in all three cases. Extras: run247
  carries models absent from the other runs (B); run248's lod-scale line is
  `applied=0 reason=game_value_pending` before a later apply (C, unresolved:
  which burst frames had scaling active).
- Opus 5.5 medium: every number matched, plus findings the ledgers lack:
  `gate6` is the history gate counting draws without previous rows
  (`motion_output.cpp`), not disoccluded pixels (A); gate6 spikes at 3761/3762
  with no unmatched-static line (A); other station parts step through the LOD
  ladder in graded fashion, run247's own extra model `00004f72` (B); the 8
  views per frame split into pre-scene, main camera and post/HUD groups, and
  `draw_accounting.py`'s default per-draw cost (dt/draws) is about twice the
  measured gap cost (C).
- Sonnet 5 medium: all core numbers matched. Misses: reported object-context
  rows per frame (277) for nodes per frame (59) and 4 of the 6 extra run246
  models (B); could not join vb/ib per draw, so the no-duplicate-pass
  conclusion rested on primitive counts and shaders only (C).

At $4/$20 vs $2/$10 per MTok, Opus 5.5 low cost roughly 0.8x the Sonnet run
per case and missed nothing; Opus 5.5 medium cost roughly 2x and found more
than the ledgers hold. Decision: `triage` stays Opus 5.5 low for locating
evidence; use medium (`triage-deep`, added the same day) only when
the question is "why", not "where". Nine agents, all read-only, no repository
edits; scratch tables under the session scratchpad only.
