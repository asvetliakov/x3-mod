# LOD occlusion patch: verification ledger

Feature: `--lod-occlusion record0|all` / `X3M_LOD_OCCLUSION`, `src/proxy/lod_occlusion.cpp`,
site header `src/proxy/lod_occlusion_sites.h`. It is the occlusion gate `jne 0x004c35c6` at `0x004c34f7`
in the material submission `0x004c0150`
([texture-lookup.md](../reverse-engineering/texture-lookup.md) §12, as built §12.4). The default
is `record0`, which leaves the engine's bytes. `all` sets the jne's rel32 at `0x004c34f9` to 0
with one `lock cmpxchg8b`, so every LOD record binds its material's `t_OcclusionTexture`. Opt-in by
user decision 2026-09-24 (Run 79 A).

| Date | Check | Command | Result |
| --- | --- | --- | --- |
| 2026-09-24 | Site qualification on the installed EXE (structural identity; hash is INFO only). The 31-byte window `0x004c34e7..0x004c3505` is `8b4d0c 83b94c01000000 8b15746f6000 0f85c9000000 837c247400 89542418`, six whole instructions, with the handle test before it and the `jl` after it. The function ends with the `ret` at `0x004c40fb`, not at `0x004c40f3`. Other checks: the placeholder bind `0x004c35c6` is reached only from `0x004c34f7`; the LOD-0 path (57 instructions reachable from `0x004c34fd` before the join `0x004c35d6`, `0x004c34fd..0x004c35d3`, including the placeholder-bind tail `0x004c35c9..0x004c35d3`) is closed and never reads `+0x14c`; the gate's flags are dead after the fall-through; no direct branch lands in the window; no dword in the image points into the window; the rel32 sits inside one aligned qword; no other claim overlaps (161 others). Two raw rel8 encodings land in the window (`0x004c34c1`, `0x004c3500`); both start mid-instruction and none lands on `0x004c34f8..fc`. The patched decode is `jne 0x004c34fd` with every other instruction unchanged | `cd verification/probe && python3 verify_lod_occlusion_site.py` | PASS 20/20 over 4,695 instructions (measured) |
| 2026-09-24 | Identity variants with the new verifier | `python3 verification/results/executable-identity/run_verifiers.py --output verification/results/executable-identity/verifiers.json` | PASS. Shipped, laa_cleared, ntcore_4gb and unknown_hash pass 25/25 each. The different build fails 25/25 with identity false. Four site-corrupt cases FAIL with identity true, the new one being `0x004c34f9` `c9→c8`. 41 anchors; 40.6 s (measured) |
| 2026-09-24 | Host tests. `test_lod_occlusion_site` (10 tests) covers the header compiled on the host: the window, site and rel32 bytes; refusal of 10 changed window bytes and of an already patched window; the `record0`/`all` parser against 16 rejected spellings; and install/read-back/rollback/restore against a copied window at the engine's qword offset (`bytes_mismatch`, `late_claim`, `invalid_site`, `unreadable`, `ok` atomic, `patch_rolled_back`, `protect_failed`, `rollback_failed`, `restore_failed`, and `restore_not_owned` for foreign or unreadable bytes with nothing written). It also covers the Python twin, both log-row parsers, the production wiring, the verifier on the installed EXE and its FAIL on a patched copy, and the launcher (default `record0`, a stale value never travels, `--vanilla` refused, invalid choices refused, launch command unchanged). `test_lod_occlusion_patch_result` (9 tests) covers the fixture record | `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_lod_occlusion_site verification.analysis.test_lod_occlusion_patch_result verification.analysis.test_exe_identity verification.analysis.test_terran_lod_site verification.analysis.test_terran_lod_patch_result verification.analysis.test_lod_scale_launch` | 66 tests OK (rerun after the restore change: 66 OK). The shared-file and launcher neighbours (`test_collide_box_cull`, `test_collide_narrow_census`, `test_collide_sat_sse2`, `test_game_phase_install`, `test_light_phases`, `test_runner_housekeeping`, `test_submit_phases`, `test_point_light_admission`, `test_comparison_hotkeys`, `test_hdr_dither_launch`, `test_launcher_stderr_tee`, `test_voice_decoder_launch`): 112 tests OK (measured) |
| 2026-09-24 | DLL build (worktree) and the no-x87 walk | `cmake --build build -j8`; `python3 verification/probe/check_no_x87.py build/d3d9.dll` | 0 warnings; PASS, 683 reachable functions, no violations; the same after the restore change (measured) |
| 2026-09-24 | Real write path under Wine. `verification/probe/lod_occlusion_patch_fixture.cpp` links the unchanged `lod_occlusion.cpp` (through a force-included fault seam) and `engine_patch.cpp` into an EXE based at `0x00400000`. The image section `.x3mocc` at `0x004c3000` holds a stub around the header's window, so `initialize()` patches `0x004c34f9` on a MEM_IMAGE `PAGE_EXECUTE_READ` page; `.x3mocd` at `0x00606000` backs the window's `mov edx,[00606f74]`. The stub returns 1 on the LOD-0 path (placeholder stored at `[esp+0x18]`) and 2 at the placeholder bind. A VirtualAlloc page, a hot RWX page and a read-only executable view are also driven | `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_lod_occlusion_patch.py` | PASS 100/100 in bottle X3 (rerun after the restore change) (arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`); record `verification/results/bottle-X3/lod-occlusion-patch.json`, bound to the SHA-256 of the five linked production sources. LOD 0/1 paths: 1/2 before, 1/1 after the patch, 1/2 after the restore, every rollback, `protect_failed` and the late refusal. `write=atomic` on the engine and private pages. The engine page reads 0x20 → 0x80 (`PAGE_EXECUTE_WRITECOPY`, Wine) during → 0x20 after. The read-only view refuses the raise (error 87 → `protect_failed`). Install rows: `patched ok` ×4, `off record0` ×2 (unset and `record0`), and every refusal/rollback reason once or twice. Restore rows: `restored found=00000000` ×5, `restored found=c9000000` ×1, `restore_failed found=00000000 registered=1` ×2, and `restore_not_owned registered=1` ×2 (`c8000000` after `rollback_failed`, `--` unreadable), each with no protect, write or flush. LastError preserved on 29 calls. The new rel32 executed without `FlushInstructionCache` (FEX caught the store; the flush stays for Windows). Install 235.3 µs, restore 180.6 µs, one-off, fixture-inclusive. Fixture 5.3 s (measured) |

**Not covered.** Native Windows execution: the source uses only documented Win32 (`VirtualProtect`,
`FlushInstructionCache`, `GetEnvironmentVariableW`, `WriteFile`) and cross-compiles, but it was not
run on Windows. Another thread executing the site during the write was not exercised; the claim that
a racing fetch sees either `jne` rests on the single aligned 8-byte store and the unchanged boundaries
(inferred). Not flown.

**Run 80 check (open).** Launch with `--lod-occlusion all`. The session log must carry
`lod_occlusion site=004c34f7 status=patched reason=ok mode=all setting=all write=atomic`. Then
`python3 verification/results/run299-303-run79a/terran-colour/occl_lod_census.py <session log>`
must show the `lod>0 slot=06` draws binding the 2048² 12-level `terran_uscdock_e_occl` at s5 instead of the 32×32
6-level `NONE_OCCL_DECAL`. In Run 79 A every such draw bound the placeholder: 1,174 / 768 / 592 draws
in run299/300/302. The `lod>0 slot=-` vanilla draws should bind real maps as well. That is the side
effect: vanilla lower records sample occlusion through a UV2 about 1 % off the unwrap, and partly outside [0, 1],
so they may show misplaced occlusion (inferred). The user look check: the coarse ODS parts lose the
~1.4× brightening against record 0.

**Restore rule (review F2, 2026-09-24).** `shutdown()` writes the original rel32 back only over the patched
`00 00 00 00`, like `engine_patch::restore`. The Terran station LOD patch is different: it overwrites
whatever it finds. Here, foreign or unreadable bytes give `restore_not_owned` with nothing written, and the
site stays registered (`registered=1`).

**Side effects of `all` (Run 80 watches both).**

- First-use load. LOD > 0 draws take the LOD-0 path's first-use load of the occlusion texture
  (`0x004f5280`, call at `0x004c3546`) for bodies never seen at LOD 0. That is up to 2048² DXT5, about
  5.6 MB with mips (inferred), loaded at draw time on the render thread. Expect a possible hitch and extra
  VRAM. Watch loading time and a stutter on first sight of distant stations.
- Materials without an occlusion map. The behaviour is that of LOD 0. A negative id binds nothing and the
  handle keeps its previous texture. A material without a `t_OcclusionTexture` value keeps the per-group
  id 0, which binds texture-table entry 0, or the placeholder if that entry has no D3D texture. What entry 0
  is was not traced.
- Vanilla lower records. They get occlusion through a UV2 about 1 % off the unwrap (above).
