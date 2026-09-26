# Iteration 5 follow-up review

The completed user run is analyzed separately from subsequent source changes.
The installed iteration-5 DLL remains the hash recorded in
[iteration-05-install.json](../../verification/results/iteration-05-install.json).
No visual TAA/HDR feature is enabled by these checkpoints.

## Shader operation metadata and fixed replay program

Independent review checked all 234 row-dot profiles against local raw programs,
including shader model, homogeneous MAD construction and output component order.
The metadata checkpoint passes 751 actual program lookups and 547,927 mutation
controls. Old aggregate initialization leaves new qualification fields unknown.

The fixed replay program qualifies 32 exact SM3 profiles and refuses 202 legacy
profiles and 17,475 single-word mutations. Review found that matching cleared
render-target texels could falsely satisfy an arithmetic sweep. A third MRT now
writes a coverage marker for every submitted point; each comparison requires it.
The final run passes 1,573,392 component comparisons and 134 bilateral raster/depth
cases. The independent reviewer verified source, executable, report, backend and
local archive-input hashes. No remaining finding applies to that checkpoint.

The evidence is bounded pipeline/readback parity with fixed rows and stored D24
precision. Exceptional input tests do not prove preservation of internal NaN bits
or authorize removal of the finite-position gate. The initial program checkpoint
does not itself connect game draws to motion replay. See
[replay verification](../verification/rigid-replay-program.md).

## Fixed-program integration

The detached rigid-motion pass now owns the fixed vertex program. Its production
source token is issued through exact archived-program qualification; synthetic
issuance exists only under the verification build flag. The finite-position proof
is a separate required field. Upstream remains responsible for binding the token
to the actual submitted shader. Independent review found no remaining issue.
The integrated fixture passes 102 numerical samples, 117 checks and 30 state
comparisons across Reset and two resource generations. Production-object symbol
inspection independently confirms that the synthetic issuer is absent.

## Captured storage lifetimes

Independent review reproduced two parser issues: a duplicate successful frame end
could replace a failed end, and malformed or alternate zero pointer/handle text
could pass a nonempty-string check. The analyzer now poisons duplicate ends and
validates numeric fields against their schema and bounded widths. Thirteen focused
tests pass, including the new negative cases. The reviewer reproduced the report
exactly from the completed immutable snapshot and checked its source hashes.

The retained result is 28 complete captured frames, 12,957 successful draws,
12,753 consistent known scoped lifetimes and 204 unscoped placeholders. Seventeen
handles recur across the load boundary with different pointers and serials.
Ordinary registry mutations between draws are distinguished from mutations within
a draw and from load/registry epoch changes; they do not mandate discarding all
unaffected object correspondences. Camera storage continuity is not camera-cut
continuity. See [live lifetime analysis](../reverse-engineering/iteration05-lifetimes.md).

## Captured depth and motion-input bookkeeping

Review separated actual draw results from the success-shaped `draw_begin`
placeholder and required every ordinary record to lie inside its exact frame.
Further controls reject duplicate scalar keys and absent/zero shader identities.
All 18 focused tests pass; an independent reviewer reproduced the final report
exactly. The full Python analysis suite passes 246 tests. The retained report
confirms 24 gameplay boundaries and four initial color-only rejections; all 7,202
zero-blocker gameplay candidates precede their selected depth Clear. Finite
vertex payload remains unverified, and API copy/epoch bookkeeping is explicitly
separate from numerical GPU-depth sampling.

## Dynamic SYSTEMMEM cache admission

Targeted game disassembly identifies four dynamic system-memory mesh option
variants. Independent backend review establishes the bounded ordinary readonly
mapping/unmapping path; the live gate admits only those exact variants and known
buffer contracts. The default-false detached capability does not broaden arbitrary
callers. Review also checked immutable release/acquire publication of bounded
rejection diagnostics. Native/wrapped fixtures pass 12,781 checks, the detached
core passes 733, and loading regressions pass 68 + 123. Every retained source,
binary and result hash matches. The generic unmap allocation-failure branch is
unreachable under the documented preallocated immediate-queue assumptions;
managed usage does not introduce another packet allocator. No measured game
speedup or installation follows from these synthetic results.

## Combined follow-up DLL

A fresh production build after the reviewed source checkpoints passes all 15
actual-DLL integration cases. Forced adoption failure also passes with all 16
current proxy/renderer objects linked, including the fixed replay program. The
production object contains the qualification function and excludes the synthetic
issuer. Full source and binary hashes remain stable throughout the matrix and
fallback run. The source build DLL SHA-256 is
`d25daf93af6c61cd9e8bd7f4f688e70476f2931bf04bdaaa1b00844af3987503`.

Evidence: [build manifest](../../verification/results/ownership-integration-build.json),
[case verification](../../verification/results/ownership-integration-verification.json),
and [fallback manifest](../../verification/results/ownership-integration-fallback.json).
These paths track the latest run; the historical 15-case artifacts described
here are preserved in commit `437e95b`. The later finite-upload matrix is documented
in [review 6](review-06.md).
Reproduce with `run_ownership_integration.py`, then
`verify_ownership_integration.py` and `run_ownership_integration_fallback.py` in
`verification/probe/`, with the user game stopped and synthetic GPU work serialized.

This DLL is **not installed**. The installed iteration-5 DLL was rehashed and
remains `ed19a7abf54ae2b9debf912f3d343a0c9217038a2162cb6a9eb174fc8050bbd5`.
The next consolidated build still needs the upload-based finite-position producer;
no additional user game run was requested for these source-only checkpoints.
