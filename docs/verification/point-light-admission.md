# Point-light root admission: verification ledger

Feature: `--point-light-root-admission` / `X3M_POINT_LIGHT_ROOT_ADMISSION=1`,
`src/proxy/point_light_admission.cpp`, site `0x004c27af`
([camera-and-lights.md](../reverse-engineering/camera-and-lights.md), "Point-light
admission site", "Implementation"). Default off; nothing is written unless the
variable is exactly `1`, the executable hash matches and the window bytes verify.

| Date | Check | Command | Result |
| --- | --- | --- | --- |
| 2026-09-16 | Site qualification on the installed EXE (`fdbf3418…`): 28-byte window, whole `jg` instruction targeting `0x004c29f5`, admit/reject target boundaries and prefixes, the only branch into the window is `0x004c2737 → 0x004c27b7`, four sources of the reject target, source constants, encoders | `PYTHONPATH=verification/probe python3 verification/probe/verify_point_light_site.py` | PASS, 11/11 checks, 4695 instructions decoded in `0x004c0150..0x004c40fb` |
| 2026-09-16 | Host tests: verifier on a synthetic image (changed site/window/reject bytes and an interior branch refused), constants, encoders, log parser, core predicate compiled with the host compiler over synthetic chains (root/depth 1–10/cycles/unreadable/exact reach/extremes), launcher gate | `PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p 'test_point_light_admission.py'` | 9 tests OK (rerun after the memo) |
| 2026-09-16 | X3 CPU fixture: synthetic copy of the engine window with the reject target at +0x240, the production module patching it; per-node admit identical to native (ZF/SF variants), rejected node admitted through its root (run-22 clamp geometry), root reject (outpost), exact-reach edge, negative reach, extreme coordinates, null parent, depth 1–7 admit / 8 reject, three cycle shapes, parent on an uncommitted page, parent in the kernel range with LastError preserved, root fields crossing into an uncommitted page, live registers/ESP locals/empty x87 on both paths, outcome counters (23 walks, one per scenario), memo: same-frame repeat answered without a walk, another node/light walks, a new frame re-walks and sees a moved root, rejection memoised, exact rollback, option off/`0`/engine site absent untouched, changed bytes refused, closed install window refused | `python3 verification/probe/build_point_light_admission.py` (module audit: no x87/MMX/XMM, no EH symbols) then `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_point_light_admission.py` | 134 checks, 0 failures; `verification/results/point-light-admission-cpu.json`; bench per harness call: native admit 22.1 ns, native reject 21.9 ns, patched admit 21.9 ns, patched reject with a depth-1 root walk 87.8 ns, patched reject answered from the memo 22.7 ns (Wine/FEX, not game FPS) |
| 2026-09-16 | Clean DLL build and the no-x87 walk including the handler | `cmake --build build --clean-first -j4`; `python3 verification/probe/check_no_x87.py build/d3d9.dll` | 0 warnings; PASS, 229 reachable functions, `_x3m_point_light_root_admits` walked, no violations; `build/d3d9.dll` sha256 `7085449d9f23a9cf49e65e1af4f6522af3a641ce2228a41b940bfebf1aa8402e` (worktree build, not a candidate) |
| 2026-09-16 | Launcher dry run | `./x3run --camera chase --point-light-root-admission --dry-run` | env carries `X3M_POINT_LIGHT_ROOT_ADMISSION=1`, `X3M_CAMERA=chase`; no launch |

## Acceptance criteria for the user run (open)

Not installed; waits for a run with `--point-light-root-admission` after the
user confirms the docking-module cliff under the accepted look.

1. One `point_light_root_admission requested=1 patched=1 reason=ok write=plain`
   line in the session log.
2. F8 pair at the run-51 spot (≈350 m / ≈210 m): `i0.x = 1` on all ten clamp
   nodes in both frames, the body admitted, the 16 km outpost rejected. This
   table is also the positive witness for the **root-class assumption**: that
   the station body node is the root the clamps reach through `+0x18` (its own
   `+0x18 == 0`) for every node class submitted through `0x004c0150`. A clamp
   still at `i0.x = 0` with the body in range means the chain does not end at
   the body (a different root class, a non-node link, or a depth beyond 8) and
   the outcome counters, not the range, are the next thing to read.
3. Same-surface far→near median gain of the far-dark points ≤ 1.7 (run-22
   prediction 1.46–1.71 from attenuation alone).
4. `frame_end` window median with the option on versus off **in the same
   sector and view** within the run-to-run noise of the two baselines (the
   memo bounds the added work to the distinct (node, light) pairs per frame;
   a measurable delta means the memo is missing, e.g. more than 256 live pairs
   colliding, and the outcome/memo counters say which).
