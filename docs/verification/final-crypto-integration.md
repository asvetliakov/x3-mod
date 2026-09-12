# Final merged CryptoAPI integration qualification

Status: **PASS** on 2026-09-13 for committed main source `ae03d9a`. This is a
standalone fixture qualification of the merged CryptoAPI cache and its current
loading/reader/ownership link graph. It is not a game run, a native-Windows run,
or evidence of end-to-end loading time.

## Execution

Preflight reported `game_guard.game_running() == []` and no active Wine runner
or fixture. The only Wine execution used the machine-wide lock:

```text
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py --holder final-crypto-sol python3 verification/probe/run_crypt_cache.py
```

The runner exited 0 and recorded `passed=true`, `phase=complete` and
`game_launched=false`. The X3 bottle record identifies `WineArch=arm64`,
`FEX_X87REDUCEDPRECISION=1` and `WINEMSYNC=1`. The suite passed **572 checks in
12 processes**: 237 real-CSP differential checks, 53 deterministic lifetime/CPU
controls and 282 hook-installer checks. The 200-message differential had zero
mismatches, 150 matching accepted signatures and 50 matching rejections in each
mode. The six host parser tests also pass.

The retained diagnostic timing was 493.509 us uncached versus 105.746 us cached
per check, or 4.67x for this fixed-order fixture. It does not measure game
loading or FPS.

## Provenance and retained evidence

The summary is
`verification/results/bottle-X3/crypt-cache-summary.json`, SHA-256
`fa2c3cbfd494fb3d13c7cc9568c8b5b843fabc3413f4cca4954f81ad2ce915dc`.
Its 72-source maps are identical before build, after build and after all runs,
and every current source digest was recomputed successfully. The manifest binds
the production loading, resource-reader, adjacency, rendering and ownership
sources linked by the synthetic installer fixture.

The 12 raw reports and their Wine stderr records are retained as
`verification/results/bottle-X3/crypt-cache-fixture.txt` and
`crypt-cache-{controls,complete,bytes,missing0..3,patch1..4}.{txt,wine.log}`.
Every retained raw and stderr digest matches the summary.

Current fixture executable SHA-256 values also match the summary:

- real-CSP fixture: `f6147d178d1a641db0ff15b1fa92d55c0e6ebc5d3b8c6d3141add073c253a6df`
- lifetime/CPU controls: `91a3000e280d3d8b77a089fd22614bb37300a471357aa608c6527126794fc1bb`
- hook installer: `903cd00630fae9ffc027b112de3535461d708019d0f7e96efe8b79eda364a538`

The Wine launcher remained
`5df16c55bbc7cd1e5b3a2b645df1026ed6ee27aa0fc8182efeb7fb51ea11be96`.
Native X3-bottle inputs were unchanged and still match their recorded hashes:
ADVAPI32 `db78c1a343ecdc22f280d3cb9c17ee00837eed27638c25107b536ee163dce4af`
and rsaenh `8864b0ee63b428d95ad17aaf63fed3a7e5c6d667fe0e5ef9bab3eac78db7a587`.

After completion, a nonblocking acquisition of `/tmp/x3-wine-runner.lock`
succeeded with an empty holder record. A second process inventory reported no
game, Wine runner or fixture process. The `final-crypto-sol` lease is released.

No production source, main DLL, installation, status/goal record or review-34
record was changed by this qualification. Native Windows behavior and the
user-managed cached/uncached loading comparison remain separate acceptance work.
