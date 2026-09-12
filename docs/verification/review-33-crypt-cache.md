# Review 33: CryptoAPI cache admission and lifetime

Status: independent review accepted for the corrected branch checkpoint
(2026-09-13). Main-tree integration and the user-managed game run remain
separate acceptance steps.
This review covers the crypto branch based on `1b8b585` and its corrective patch.
No game or Wine fixture is launched by the reviewer. The corrected source has fresh Steam and X3 fixture evidence below; older
interrupted runs remain superseded.

## Contract under review

The optimization is restricted to the six validated return addresses of the
original signature-check routine, the exact scratch container/provider/type and
flags, and bounded RSA public-key input. Hashing and signature verification
remain native. All four import routes must be installed and their protection
restored before publishing active cache routing. Failed installation must leave
only ordinary forwarding behavior, including surviving foreign chains.

Retaining a provider or imported key changes its physical lifetime. This is a
specialized game-sequence optimization, not an implementation of arbitrary
CryptoAPI semantics. Container deletion results are ignored by the reviewed game
routine, but first real deletion, new-container errors and native verification
results must remain accounted for. Suppressed successful cleanup cannot be
advertised as independent proof that all future CSP calls would succeed.

## Findings and review gates

- Previously identified: partial installation could expose only some lifetime
  hooks; arbitrary API calls were eligible; CSP cleanup ran from DllMain.
  The current patch uses active-last group installation, validated game call
  sites and removes loader-lock cleanup. The fresh installer controls now pass.
- Outstanding-key lifetime: provider release must not permit reissuing a cached
  provider while a previously issued key remains outstanding. Cross-thread or
  out-of-sequence cleanup must have an explicit fallback/retirement policy.
  Corrected: outstanding-key release now retires the provider and calls native;
  foreign key destruction retires that key and calls native. Source review
  accepts both changes, and the fresh runtime controls pass.
- Late native publication: a lock-protected mutation generation now prevents
  acquire/import results from populating cache state after an intervening
  observed lifecycle operation. Reentrant native shims exercise this schedule.
  The acquire control initially retained a reusable provider and therefore
  bypassed its intended native callback; its setup now explicitly evicts that
  provider before arming reentry. Source review accepts the correction.
- CPU-state evidence: the integer-only/no-SSE hot path avoids added arithmetic,
  but that alone does not prove native-versus-hit outgoing FP/LastError parity.
  Seeded runtime controls and emitted code checks are separate gates.
- CSP calls must occur outside cache locks. Shutdown is an explicit quiescent
  operation only; normal process exit must not acquire a CSP from loader lock.
  Any deliberately retained process-lifetime handles and scratch container must
  be described accurately, without claiming exit-time registry cleanup.

The documented Windows API states that releasing a context invalidates its
session keys and hashes, and that nonzero release flags can fail while still
releasing the context. The fallback must not retain a stale cache entry after
such a call. [Microsoft CryptReleaseContext](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-cryptreleasecontext).

## Harness refinements reviewed

The corrected runner records current ADVAPI32/CSP module provenance in addition
to the Wine launcher and rejects nonfinite/negative diagnostic timings. The
synthetic image now executes one actual qualified IAT call at the reviewed
return address, complementing classification-helper tests with emitted routing.
The fixture-owned cdecl assembly shim copies all five stdcall arguments, enters
the mapped call instruction and returns through its authored continuation.
These source changes and their fresh execution evidence are accepted; they do not broaden
production scope. A fixture-only base-address setter permits its synthetic PE
to use an available allocation after fixed-address allocation failed under Wine.
Production retains a constexpr `0x00400000` base and the same six preferred
return addresses. Signature reads, IAT immediates and the actual mapped stdcall
all use the same fixture delta; generated code is flushed before execution.
The first native run also moved CSP module-path reporting before final signer
cleanup so the module is still loaded. Both are harness corrections, requiring
fresh evidence rather than promoting the interrupted run.

## Final evidence and limits

Each bottle passes **572 checks across 12 processes**: 237 real-CSP differential
checks, 53 deterministic lifetime/CPU checks, and 282 installer checks in ten
modes. The reviewer independently re-ran the offline parsers against every raw
report and all six host tests passed; no duplicate Wine execution was performed.

- Steam summary SHA256: `5b9e2d6d064560b5a16e113e37ccabb6704985afe94113fbaaeddcbcce0ab649`.
- X3 summary SHA256: `16739a934c3feac4fd96b4f5ee3fce0cb8d9d93a67143dc5fff096ce4f07119e`.
- Production DLL: `1f06ebcf7adc78ba207144e4ff741f73ccec07f46afd033651290e706f68312f`.

Both summary source maps agree before build, after build and after execution;
every recorded source, native ADVAPI32/CSP, Wine launcher and raw stdout/stderr
hash was independently recomputed and matches. The current X3-built three fixture
executables match their recorded hashes. The earlier Steam executables were
replaced by the fresh X3 build, so their identity is supported by the runner's
recorded within-run checks, not a claim that those old bytes remain on disk.
The production DLL/source hashes and the retained no-x87 audit agree: 210
reachable functions, zero reported violations. The cache's integer-only hot
paths and the seeded controlled miss/hit witnesses remain a narrower claim than
identical volatile FP behavior of every CSP implementation.

The real-CSP fixture compares every verification result and failing-call error,
including valid, corrupted and truncated signatures, and explicitly cleans its
own named container. That cleanup is a fixture operation outside loader lock;
it does not imply that production process exit deletes its retained container.
The production persistent-container difference is documented and accepted only
for the scoped game sequence. External keyset writers, unobserved handle escapes,
and mutation of the admitted game code/IAT remain outside that contract.

Current mean core-cache/CSP timings are 528.208 → 132.208 µs per check on Steam
and 444.229 → 107.378 µs on X3. These fixed-order 200-message differentials include
neither a full instrumented loading envelope nor gameplay. The wrapper adds a
bounded six-site integer comparison and activation read; fixed provider/key
lookups and blob comparisons allocate nothing per hit. Telemetry miss cost and
an actual loading improvement remain to be measured in the user-run build.

No remaining scoped code, fixture or provenance blocker was found. Native
Windows execution remains unverified. Merge/integration verification and the
user's script/mission behavior and loading comparison are still required.
