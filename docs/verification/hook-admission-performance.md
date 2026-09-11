# Actual hook admission performance

The final uninstalled DLL was measured through real D3D9 calls under CrossOver Preview. Admission adds measurable work to intercepted calls: about 134–154 ns for the single-boundary routes here, and 285–316 ns for the capture-plus-ownership routes. These are differences between separate process medians, not a game frame-time estimate.

## Method and retained builds

[The complete report](../../verification/results/hook-admission-performance.json) binds the fixture, seven isolated processes, raw reports, Wine logs, loaded-module paths and source/native DLL hashes. Each process runs one warmup and seven measured batches of 20,000 calls per route. All seven pass exactly 640,115 checks and 28 recorded samples. Per-call result checks and loop work remain inside timing. Getter output, final setter state and render-target identity are verified. No drawing, Present, copy or readback occurs; state setters can still enqueue backend work.

The current DLL is `5a5f8a78d7c9a802d844368c7a68572c009edd1272b03e8dab306e6bcda39007`, copied from the accepted fresh 26-case integration build. The retained previous DLL is `aa61e7ff2fcc4541f42d961359bdb7f2f815f6dc3c97b0cb50832ad51aa39ed9`. Its [historical provenance](../../verification/results/hook-admission-baseline-provenance.json) was archived before the current build replaced the main build manifest; no old-source rebuild was used. One frozen benchmark executable runs all cases. The previous source map is historical, not presented as current-source evidence.

The runner verifies the actual module through the bottle's recorded drive mappings, requires a wrapped factory for ownership cases, rejects fallback, and verifies enabled admission recorded real roots followed by balanced final teardown. It records unchanged mappings and hashes after the run. A failed preliminary runner attempt assumed the proxy path would use Z:; Wine reported its equivalent Y: mapping. The corrected runner resolves that mapping to the exact per-case DLL and this report comes from a complete fresh rerun.

## Whole-call medians

Nanoseconds per call, rounded to one decimal:

| Route | Native | Previous proxy | Current proxy off | Current proxy on | Previous ownership | Current ownership off | Current ownership on |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GetRenderState | 40.7 | 40.5 | 38.2 | 38.8 | 76.5 | 81.9 | 216.1 |
| SetRenderState | 41.7 | 51.1 | 42.6 | 42.7 | 84.1 | 82.1 | 219.1 |
| Rejected SetRenderTarget | 32.7 | 138.7 | 169.4 | 312.0 | 186.7 | 231.2 | 546.9 |
| Rejected Clear | 318.4 | 481.4 | 475.7 | 629.7 | 512.4 | 516.0 | 801.4 |

Get/SetRenderState are unhooked controls in proxy-only cases and intercepted ownership calls in ownership cases. SetRenderTarget uses a null primary target; Clear requests depth clearing without a depth surface. Both must return `D3DERR_INVALIDCALL`, so neither measures successful drawing cost. The full report retains all raw samples and their minima/maxima.

## Measured increments

Differences of separate medians, ns/call:

| Route | Proxy off minus native | Ownership off minus native | Proxy admission on minus off | Ownership admission on minus off | Current proxy off minus previous | Current ownership off minus previous |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| GetRenderState | -2.4 | 41.2 | 0.5 | 134.2 | -2.3 | 5.4 |
| SetRenderState | 0.8 | 40.4 | 0.1 | 137.0 | -8.5 | -2.0 |
| Rejected SetRenderTarget | 136.6 | 198.5 | 142.6 | 315.6 | 30.7 | 44.5 |
| Rejected Clear | 157.3 | 197.6 | 154.0 | 285.4 | -5.7 | 3.6 |

The on/off comparison isolates the configuration change within the same current binary, while still including complete native calls and measurement variation. The old/current off comparison includes all intervening changes on each path; it cannot attribute the entire difference solely to `CpuCallBoundary`. In particular Clear already had a CPU boundary in the retained build. Small negative differences on unchanged controls are measurement variation, not evidence that interception accelerates the native API.

The complete disabled boundary is included in current off-mode values. Its emitted/native preservation evidence is separate from timing: the existing CPU helper calls GetLastError before saving FP state and SetLastError after restoring it, so the tested runtime witnesses do not establish helper-independent preservation on every backend. The fixture's seven sequential samples per route are a warm synthetic measurement on this machine, not randomized process-order statistics or a Windows-native measurement.

No per-frame projection, gameplay FPS benefit, successful-draw overhead, callback-free replay guarantee or live replay activation follows from these results. Live replay remains disabled. See [proxy admission coverage and limits](proxy-application-admission.md).
