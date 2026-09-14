# Outstanding user gameplay runs

Updated 2026-09-14. Run 17 crypto acceptance and the first-person/chase
left-centre-right diagnostic are complete and are not in this queue. The agent
never launches the game. Installed build: chase firing fix with 13° pitch and
0.9 distance, softer 0.28/0.38 s follow, predictive lead marker, opt-in FP16 bloom
168 reviewed linear material pairs, the central chase display correction, and
Auto exposure capped at +1.5 EV, tighter authored glow (gain 0.375/scatter 0.65), and same-run exposure/bloom controls;
[build record](../../verification/results/linear-material-install.json).
From the repository root, paste a `./x3run` command below. The executable
[launcher script](../../x3run) handles the shared lock and log snapshots; no shell
function setup is needed. Runs 1, 3, 5, 6, 7, 8 and 9 are complete; reader/DAT/adjacency
fast co-activation passed as run 19. Run 9 is saved as run 28; its reported issues are being investigated.
Close X3 between runs and report completed numbers. After exit, the helper prints
a fresh `/tmp/x3-bottleX3-run<N>/` path containing that session’s log and referenced
captures, so later A/B runs cannot overwrite them. Vanilla/dry-run creates no snapshot.

| Run | Purpose | Sessions | Status |
| --- | --- | ---: | --- |
| 1 | Chase aiming/framing + reader/adjacency verification | 0 | Accepted as run 18 |
| 2 | Sharpen/shimmer + camera cuts with TAA | 0 | Merged into run 6 |
| 3 | Automatic exposure + bloom off/on | 0 | Completed: A run 24; B run 25 exposed bloom initialization failure |
| 4 | Vanilla double-cursor/menu-bar comparison | 1 | After any enhanced run |
| 5 | Reader/adjacency fast modes | 0 | Accepted as run 19 |
| 6 | Linear hull materials off/on plus sharpen/cuts at fixed exposure | 0 | Completed: A run 20, B runs 21–23; analysis/quality follow-ups remain |
| 7 | Fixed/automatic exposure and bloom toggles, central chase HUD and selection timing | 0 | Completed as run 26; follow-ups combined into run 8 |
| 8 | Restored glow, milder exposure and native selection-stutter trace | 0 | Completed as run 27 |
| 9 | Stronger glow and selection/voice timing | 0 | Completed as run 28 on source `d9413fc` |

**No new enhanced run is needed yet.** Run 28 analysis and the next combined
changes are underway. Run 4 remains the optional vanilla cursor comparison.
Emission stays off for this comparison; its twenty-pair live route is qualified,
but gameplay appearance and cost will need separate acceptance.

Completed run commands and instructions are preserved in
[the completed-run archive](../archive/user-runs-completed.md); they are provenance,
not rerun requests.

## 4. Vanilla window/cursor comparison — Ready after any enhanced run

```sh
./x3run --direct --vanilla
```

Alt-tab out and back once. Report whether both the macOS arrow and game cursor
appear, whether their positions differ, and whether the macOS menu bar overlaps
the game. Compare the same screen as the enhanced run; load the save if the
problem only appears during gameplay. No F8 capture is needed.
