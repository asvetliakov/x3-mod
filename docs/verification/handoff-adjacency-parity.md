# Handoff: adjacency parity after run 11 (2026-09-13, paused mid-verification)

Uncommitted working tree on `main` (HEAD `a40909a`); another agent (review 30)
has unrelated uncommitted edits in the same checkout. The game data of this
investigation lives only under `/tmp/x3-bottleX3-run11/` (session log, the 37
`dumps/mesh-adjacency-<n>.bin`) and `/tmp/x3-adj-bisect*/` (truncated copies of
dump 13); decompiler output under `/tmp/x3-adj-run11-out/`. **None of it may
enter the repository** (copyrighted mesh data; DLL data).

## 1. Did the 37 dumps reproduce offline?

Yes, and the reproduction located the cause. `tools/analysis/replay_mesh_adjacency.py`
(`--wine` runs the fixture's `replay` mode under the Wine lock):

| Replay | native d3dx9_37 vs the in-game array | module vs that bottle's native |
|---|---|---|
| bottle X3 (FEX), review-28 module | equal on 37/37 (`native_equal_dump=1`) | mismatched on 37/37, the game's counts |
| bottle Steam (Rosetta), review-28 module | **differs on 36/37** | equal on 33/37 |
| host build (`1/sqrt`), review-28 rules | - | 29/37 mismatch the in-game array; 8 equal |
| **after the fix**, X3 (`normalize=generic`) | 37/37 | **37/37 equal** (`MESH ADJACENCY REPLAY dumps=37 equal=37 mismatched=0`) |
| after the fix, Steam (`normalize=sse2`) | 36/37 differ (expected: other table) | 33/37 equal; residual below |
| after the fix, host (`--normalize generic`) | - | 37/37 equal to the in-game array |

So d3dx9_37 itself computes different adjacency under FEX than under Rosetta;
the review-28 module matched Rosetta's. Nothing in the call context (declaration,
stride, flags, FP state: all dumps stride 64, position first, epsilon 1e-6,
x87 `0x023f`, MXCSR `0x9fc0`) differed from the fixture's replay.

## 2. Rule found (with addresses)

`D3DXVec3Normalize` of the candidate score is dispatched per process
(`0x00587e6b`, [rules doc section 4](../reverse-engineering/d3dx-generate-adjacency.md)):

* registry `HKLM\Software\Microsoft\Direct3D` `DisablePSGP` / `DisableD3DXPSGP`
  (`0x00587aa0`; absent in both bottles);
* feature check `0x00587e06`: on NT 5+ (`0x00587d96`) `IsProcessorFeaturePresent(7)`
  for 3DNow and `(6)` for SSE; SSE2 from CPUID leaf 1 EDX bit 26 (`0x00587c94`);
* 3DNow reported -> the 3DNow installer `0x0074a996`, which installs only when
  its own CPUID check (`0x0074a892`: MMX bit 23 and extended EDX bit 31) passes,
  otherwise the process keeps the **generic** table (`0x0058844c` slot 7 =
  `0x005881fc`); no 3DNow -> SSE2 table (`0x00756246`, normalize `0x00756732`,
  `rsqrtss` + Newton) or SSE table (`0x0075385d`).
* FEX's Wine answers `IsProcessorFeaturePresent(7)` = 1 with no CPUID 3DNow bit
  (probes on both bottles, scratch), so **on X3 D3DX scores with the generic
  normalize**: x87 at 53 bits, 512-segment linear interpolation of `1/sqrt`
  indexed by the float bits of `len2` (`(bits >> 15) & 0x1ff`), mantissa
  re-exponented with `| 0x3f000000`, scale `((0xbeffffff - bits) >> 1) & 0xff800000`,
  vectors with `|float(len2 - 1)| <= 0x3727c5ac` copied unnormalized, zero
  length -> zero. The table `DAT_0076c2a0` is static `.data` and is reproduced
  512/512 from its generating rule (`r0 = float(1/sqrt(lo))`, `r1 = float(1/sqrt(hi))`,
  `A = float((r1 - r0)/(hi - lo))`, `B = float(r1 - A*hi)`), so nothing is copied.
* Evidence: the game's DLL's `D3DXVec3Normalize` equals the generic model on
  20,014/20,014 vectors on X3 and the `rsqrtss` model on 2,012/2,012 on Steam;
  the score is stored as a float before the compare (`0x005906ac`-`0x005906d4`),
  so the few-ulp difference between the two normalizes flips near-ties.

## 3. Implemented (all uncommitted)

* `src/proxy/mesh_adjacency_fast.{h,cpp}`: `Normalize { Sse2, Generic }` in
  `Policy` (default `Sse2`), `normalize_generic` (double arithmetic, regenerated
  table via `_mm_sqrt_sd`), `normalize_name()`.
* `src/proxy/loading_trace.{h,cpp}`: `D3dxMathTable d3dx_math_table()` mirrors
  the dispatch from the documented inputs (registry, `IsProcessorFeaturePresent`,
  `__get_cpuid`); the service selects the policy, falls back to native for the
  3DNow/SSE tables (`AdjacencyFallback::MathTable`, metric field
  `fallback_math_table`, `AdjacencyStatistics.fallback_reasons` now 7); init log
  line adds `math_table=... normalize=...`; the dump header's `reserved[0]` =
  1 + table (0 in the run-11 dumps).
* Fixture `mesh_adjacency_fast_fixture.cpp`: detects the table (`MESH ADJACENCY
  MATH_TABLE` line, refuses 3DNow/SSE), `normalize=` on every case/replay line,
  `other_normalize` policy variant, new case `near-unit-normals` (synthetic:
  native picks face 2 on X3, face 1 on Steam; `other_normalize` mismatches 3 on
  both). Direct runs on Steam: cache-off `checks=35984`, cache-on `checks=36041`
  (written into `run_loading_trace.py` `MESH_ADJACENCY_CHECKS`; the runner itself
  has **not** been run since).
* Host driver (14th input field `normalize`, 13th head field), Python port
  (`normalize=` argument, `RSQRT_TABLE`, `_normalize_generic`), replay tool
  (`--normalize {auto,sse2,generic}`, header field, `other_normalize` variant,
  forwards the `MATH_TABLE` line), host tests (23, all pass:
  `GenericNormalize` x3, `test_normalize_paths_match_reference`).
* Builds: `sh verification/probe/build_loading_trace.sh` and `cmake --build build -j4`
  succeed; `check_no_x87.py build/d3d9.dll` PASS (196 reachable functions, 0
  violations); `build/d3d9.dll` SHA-256
  `ef190bcbf84b8088378872e05bbfa77be437636a57b9c77a4db0e21377fd903f`. Not installed.
* Docs: rules doc (address table, section 4 rewritten, section 6),
  `mesh-adjacency-fast.md` "Run 11 and the normalize dispatch".

## 4. Open: the Steam residual (4 of 37, Rosetta only)

With the SSE2 path the module still differs from Steam's native on dumps 13/3/34
(one mesh, 7 entries from index 309) and 9 (4 entries from 18818). Facts so far:

* Not arithmetic: D3DX's own score function `0x005904bd`, called directly on
  Steam (scratch probe, stdcall 8 args, vertex base + stride), returns for dump
  13 face 103 slot 0 exactly the module's scores (candidate 446 `0x3f7ffffe`,
  789 `0x3f7ffffd`, all of x87 PC 24/53/64), so the compare must replace the
  chain head 789 by 446; native pairs 103 with 789 and leaves 446's slot -1.
* Minimal reproduction on Steam: a dump with only faces `[103, 446, 789]` (even
  renumbered to its 7 vertices, `/tmp/x3-adj-bisect2/mesh-adjacency-3faces-9v.bin`)
  gives native `0<->2`, module `0<->1`. Two-face dumps `[103,446]`, `[103,789]`,
  `[446,1406]` all pair natively. On X3 (generic) the same 3-face dumps are equal.
* Truncations of dump 13 to faces `0..N` mismatch from N = 790 (face 789 present).
* All these meshes are 16-bit (`options 0x990`): the 16-bit converter
  (`0x00597e88`) has not been read; the rules were derived from the 32-bit one
  (`0x00598226`). Hypotheses to test next, in order: (a) the 16-bit converter
  passes the lookup/score arguments in another order (a different query
  "other", or a swapped edge), which would leave X3 correct by chance; (b) the
  x87 stack/flags path of the lookup's compare (`fcompp`/`fnstsw`/`test $5,%ah`/`jp`)
  under Rosetta; (c) a Rosetta-only effect in `rsqrtss` for these exact inputs.
  Test (a) by decompiling `0x00597e88` and by replaying the 3-face mesh as a
  32-bit dump (set options bit 0 and write 4-byte indices) on Steam.

## 5. Exact next commands (all Wine commands under the lock; never while the game runs)

```
# host tests (pass now)
PYTHONPATH=verification/probe python3 -m unittest verification/analysis/test_mesh_adjacency_fast.py
# replay of the 37 run-11 dumps (expect 37/37 on X3; Steam: 33/37 until section 4 is solved)
X3M_FIXTURE_BOTTLE=X3 python3 tools/analysis/replay_mesh_adjacency.py --wine --normalize generic /tmp/x3-bottleX3-run11/dumps
python3 tools/analysis/replay_mesh_adjacency.py --wine --normalize generic /tmp/x3-bottleX3-run11/dumps
# suites (not rerun since the change; inventories updated from direct fixture runs)
python3 verification/probe/wine_lock.py --holder adjacency python3 verification/probe/run_loading_trace.py
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py --holder adjacency python3 verification/probe/run_loading_trace.py
python3 verification/probe/wine_lock.py --holder adjacency python3 verification/probe/run_mesh_adjacency_cache.py
python3 verification/probe/wine_lock.py --holder adjacency python3 verification/probe/run_mesh_cache_hook.py
python3 verification/probe/check_no_x87.py build/d3d9.dll
PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis
# decompile the 16-bit converter for section 4 (retry on a Ghidra lock)
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless /tmp/x3-ghidra-d3dx D3DX -process d3dx9_37.dll -noanalysis -readOnly -scriptPath tools/analysis -postScript X3DecompileFunctions.java /tmp/x3-adj-run11-out/converter16.c 0x00597e88 0x0059edc6
```

Then: suite record and timings into `mesh-adjacency-fast.md` (review 28: grid
11.8 ms, split-150 6.1, star 2.7, split-16 2.5), `status.md` bullet, commit.
Fast mode stays blocked until an in-game verify run on X3 shows
`verify_mismatched=0` with `math_table=generic normalize=generic` in the
`mesh_adjacency mode=` line.
