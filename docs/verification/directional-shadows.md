# Directional-sun-share verification ledger

2026-09-15 source-artifact checkpoint. Independent deep review: **ACCEPT**.
The new off-by-default extraction API has no live-renderer caller, configuration
change or resource allocation. Its source checkpoint was committed as `0234178`;
this ledger makes no installed-build claim. Current installation state is in
[status](../status.md).

## Host evidence

The focused host suite passed **13 tests in 26.331 s**. It covers 432 extracted
variants from 108 originals and 152 authored sun-colour MAD consumers. The
baseline retained **1,388 legacy outputs byte-exact**. The host tail/oracle
checks cover 432 depth-on tails, 108 zero-sun cases, 152 isolated authored
lobes, 28 component-domain cases, and 39 synthetic planner/resource cases.
Forced allocation rollback preserves output and failure state at 75 hull and
101 XT allocation points.

Generated variants use **136–342 weighted slots** (an addition of 32–45) and
remain within the 512-slot target. Those figures are static host results;
create-time latency, GPU execution cost and GPU register pressure are
unmeasured.

## Boundaries

This checkpoint proves neither live integration nor GPU execution. No GPU,
Wine, game, Reset/recovery, resource-publication or consumer-integration proof
ran. It also does not establish native-Windows shader creation or raster
semantics. The extraction contract and family-specific evidence are in
[sun-share-material-contract.md](../reverse-engineering/sun-share-material-contract.md).

## Native bytecode blocker at the original checkpoint

Native ps_3_0 validity is currently blocked by an inherited ordinary-material
sanitizer instruction: it reads two distinct float constants in one `MAX` (for
example, `c5` and `c212.y`). All 108 ordinary PS programs have this violation.
The documented ps_3_0 float-constant register limit is one read port per
instruction ([Microsoft register reference](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-ps-3-0)).
The new sun-share instructions comply, but cannot make the containing programs
valid. The repair subsequently landed as `5b3b5c3`, with legacy/fill/fade GPU parity
([repair evidence](linear-material-constant-port.md)). Native Windows runtime
verification remains open.

## 2026-09-15 runtime integration qualification in progress

The isolated runtime implementation at `/tmp/x3-sun-share-runtime` is not yet
integrated. Independent review accepted the corrected cutout positive control;
actual MotionOutput execution and composition coverage remain acceptance gates.
The following retained X3 GPU results were revalidated by that reviewer:

- Material extraction: 216 cases, 55,296 valid drawn pixels (27,648 positive
  and 27,648 zero sun share), 256 empty controls, no invalid pixels; maximum
  subtraction error 0.000686797113. Raw report:
  `/tmp/x3-sun-share-material-gpu/report.txt`.
- Temporal channel copying: eight history twins, four copy failures, two sizes
  across two generations, and 24 hostile-state restorations. Summary:
  `/tmp/x3-sun-share-runtime/verification/results/bottle-X3/sun-share-temporal-summary.json`.

Both executions used the single Wine lock, bottle X3, WineArch arm64,
`FEX_X87REDUCEDPRECISION=1` and `WINEMSYNC=1`. These fixtures do not execute
the actual MotionOutput qualification/publication path. They therefore do not
establish live receiver admission, composition exclusion or complete recovery.
The separate qualification object build passed with zero warnings; its local
record is `/tmp/x3-sun-runtime-fixture-build.json`. It is not an install candidate.
Native Windows execution, gameplay coverage and GPU performance remain open.
