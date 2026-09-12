# Review 34 final integration artifact audit

Independent artifact review on 2026-09-13 of the final pre-install integration
checkpoint at committed main source `ae03d9a`. **Verdict: PASS for the selected
X3 motion/HDR integration and standalone X3 CryptoAPI fixture.** This is not a
new full motion-suite pass, a gameplay result, native-Windows verification or
installation approval by itself.

## Selected motion and HDR integration

The retained partial summary contains exactly the eleven requested cases:
production off/on, production ownership, production HDR ramp, production
TAA/AgX, seam exposure and offset exposure, wrapped exposure, tonemap-fault
recovery, meter self-test unlock refusal, and seam TAA/automatic exposure. All
eleven exited zero and passed their individual validators, totalling **1,156
checks across 435 frames**. The aggregate correctly remains `status=PARTIAL`,
`passed=false`, with no benchmark entries; the separate durable audit uses
`SELECTED_VALIDATED` and explicitly records `full_suite_passed=false`.

I independently recomputed every case-local trace, fixture executable and DLL
digest. Each case directory contains exactly one session trace matching its
summary digest. The other ten cases also have matching copied traces; the
disabled production case retains its valid trace in its case directory, as the
runner intends. The partial JSON and text report match the durable audit, and
the text report contains the same eleven case headings with `exit=0`.

All 141 source hashes are identical before the clean build and after execution,
and every entry matches the current tree. The expanded manifest includes all
ten added execution inputs: the four numerical/analysis oracles, all three
ownership/capture helpers, and `bottle.py`, `game_guard.py` and `wine_lock.py`.
Both reviewed local shader inputs also match their recorded hashes. The
historical 98-case X3 full-suite JSON and text report remain byte-identical to
the pre-run preservation record; they were not rerun or relabelled as final-main
evidence.

The candidate production DLL is `build/d3d9.dll`, SHA-256
`ae2482fd5146c62898fbe20c45441d9d14183d705c50e3d03872094ec635b193`.
Its 40 production objects match the CMake response, archive and on-disk
inventory. The seam's 34 discovered shared objects equal the production
response after removing its six explicitly recompiled seam objects. The DLL is
x86 PE (`0x014c`) and has exactly the 17 exports declared by the project, all
with nonzero local RVAs and no forwarders.

An independent rerun of the host no-x87 auditor passed with 211 reachable
functions and zero violations. The locked shader generator check exited zero
and reported `PASS` for all ten authored programs; all 32 recorded generator,
source, generated-header and result-manifest hashes match the current files.
The complete retained record is
[`integrated-motion-validation.json`](../../verification/results/bottle-X3/integrated-motion-validation.json),
SHA-256 `05fc451d3da720ad5c7a182371305a0347e0838455f1306087cb2a2a4ed22c76`.

## Final merged CryptoAPI fixture

The X3 CryptoAPI summary reports `passed=true`, `phase=complete`,
`game_launched=false` and exit zero. It passed **572 checks in 12 processes**:
237 real-CSP differential checks, 53 lifetime/CPU controls and 282 hook
installer checks. The 200-message differential reports zero mismatches.

I recomputed the summary's 72-source maps and found the before-build,
after-build and after-run maps identical and current. The three fixture
executables, all twelve stdout reports, all twelve Wine stderr records, the
CrossOver Preview Wine launcher and both X3-bottle native inputs (`advapi32.dll`
and `rsaenh.dll`) match their recorded hashes. The retained summary is
[`crypt-cache-summary.json`](../../verification/results/bottle-X3/crypt-cache-summary.json),
SHA-256 `fa2c3cbfd494fb3d13c7cc9568c8b5b843fabc3413f4cca4954f81ad2ce915dc`;
the execution record is
[`final-crypto-integration.md`](final-crypto-integration.md).

No Wine command, compiler build, game launch, install or commit was performed
by this reviewer. The tests establish modular co-link and synthetic execution
on CrossOver Preview's X3 bottle. Real game co-activation, visual acceptance,
loading-time measurement and native-Windows runtime behavior remain unverified.
