# Farnham's Legacy port ledger

One ledger for the FL port, restarted 2026-11-01 after the first attempt
(branch `fl-codex`, 580 commits, no feature accepted in game) was set aside.
Rules for this port: one feature per step, built as the `fl` edition of the
same tree ([build note](../architecture/farnhams-legacy-build.md)), installed
into `C:\X3FL`, flown by the user, then recorded here. Inherited disassembly
notes: [docs/reverse-engineering/fl/](../reverse-engineering/fl/README.md).

Target: `C:\X3FL\X3FL.exe`, SHA-256 `e7f3aad8…dfbb8`, 2,804,736 B,
PE timestamp 2021-11-04, version 1.3.0.0, ASLR-relocatable (`.reloc`,
`DYNAMIC_BASE`), Control Flow Guard metadata (`.gfids`). Stock AP copy for
comparisons: `C:\X3AP-orig`.

## FL run 0 — accepted AP DLL under X3FL.exe (2026-11-01)

Purpose: prove the proxy loads under the relocatable FL EXE and measure the
pass-through baseline before any FL code exists.

Install: `C:\X3\d3d9.dll` (release 1.7.3, `bde29ab1…`, 42,535,121 B) and the
user's `x3m.ini` (one key, `hull_lightmap_gain=2`) copied to `C:\X3FL`. No
regenerated assets, no fonts.

Result (user: game loads, save loads, TAA and HDR visibly inactive; log
`C:\X3FL\x3m.log`, session 20261101-134116, measured):

| item | value |
| --- | --- |
| frames / elapsed | 36,259 / 189.7 s, no device reset, no exception |
| device | 5120x1440 fullscreen, RTX 5070 Ti, backend system32 d3d9.dll |
| engine modules | 39 `executable_mismatch` refusals, 1 `foreign_executable`, 117 skipped |
| providers | `object_trace`, `object_lifetime`, `camera_state`, `sun_light_poll`, `scene_hook` all `active=0 status=executable_mismatch` |
| loading hooks | Crypt* hooks `installed=0`; gz buffer served without patching |
| renderer devices | `hdr_device enabled=1`, `motion_output_device enabled=1`, TAA history unavailable (requires object_trace, object_lifetime) |

Conclusion: the identity gate fails closed cleanly on FL; nothing patched, no
crash over 36k frames. The renderer's engine inputs are the port order:
camera matrices (TAA reprojection, shadow cascades), object identity (TAA
history), scene hook (bloom/compositor), sector background (fog), sun poll
(shadows). Rollback: delete `C:\X3FL\d3d9.dll` and `x3m.ini`.

## FL run 1 — first `fl` edition DLL, identity line only (installed 2026-11-01)

Candidate: commit `7c038797`, `build-fl/d3d9.dll` SHA-256 `144a023b…`, 55,307,009 B
unstripped, 0 warnings, 41 s wall ([record](../../verification/results/fl-run1/candidate.json)).
Installed to `C:\X3FL\d3d9.dll`, bytes verified; rollback
`C:\X3FL\rollback-d3d9\d3d9.dll.run0` (the run 0 AP DLL). Expected: one
`fl.identity ok base=0x… ` line in `x3m.log`, no `executable_mismatch` rows
(the AP modules are not in this DLL), otherwise run 0 behaviour.

Result (user: behaves like run 0; log session 20261101-141549, measured):
`fl.identity ok base=0x004d0000 timestamp=0x618416c8 image_size=0x00391000`;
25,176 frames / 126 s, no reset, no exception; zero `executable_mismatch` rows;
the only engine refusal left is `volumetric_fog_sector reason=foreign_executable`
from the kept sector reader. **Accepted.** The live base 0x4d0000 confirms that
the inherited preferred-base addresses must be used as RVAs.
