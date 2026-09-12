# Game signature provider cache (`X3M_CRYPT_CACHE`)

**The corrected branch passes 572 checks on each of Steam and X3/FEX.
[Game run 17](run17-crypto-loading.md) accepts the targeted cache path on X3:**
the 844-check signature probe fell from 12.835 s to 0.1353276 s with native
verification intact. The 27.574 s save gap is not a controlled whole-load A/B. The opt-in switch `--crypt-cache` targets the provider/container
churn in the game's [signature verifier](../reverse-engineering/script-signature-check.md).
Verification still uses the original CSP's hashing, RSA signature check and hash
parameters. Invalid signatures are not accepted by the cache.

## Narrow game contract

This is a game optimization, not a general CryptoAPI handle cache. The four IAT
wrappers accelerate only these six return addresses inside `0x004cabc0`:

| Operation | Return address |
| --- | --- |
| First delete | `0x004cac3e` |
| New keyset | `0x004cac57` |
| Import public key | `0x004cac85` |
| Destroy key | `0x004cae4d` |
| Release provider | `0x004cae5a` |
| Final delete | `0x004cae73` |

Before installation, the code validates each six-byte indirect call against its
resolved named IAT slot, plus the adjacent new-keyset argument sequence and
release argument sequence. The target must be based at `0x00400000`, and the
validation reads must belong to its executable allocation. No DLL hash, private
CSP structure, Wine export or backend patch is used. The decompiled game function
keeps the provider/key in local variables and uses them only for this signature
sequence; unobserved external handle use or keyset mutation is outside this
bounded contract. Later game-code or IAT mutation is not supported by this gate.

All four lifecycle hooks must install successfully, with page protections
restored, before the cache becomes active. During partial installation the shims
forward natively. Failure rolls back already installed slots; any shim retained
because restoration fails remains a native forwarder. An unqualified caller also
forwards natively even when the cache is active. This prevents general CryptoAPI
calls from accidentally inheriting the signature optimization.

The core independently checks the exact scratch name `X2EgosoftCSPContainer`,
Microsoft Base Cryptographic Provider v1.0, `PROV_RSA_FULL`, and exact
`CRYPT_NEWKEYSET`/`CRYPT_DELETEKEYSET` flags. Machine-keyset and combined flags do
not alias user-keyset requests. Imports qualify only as a 276-byte RSA-2048
`PUBLICKEYBLOB`, flags zero, with no wrapping key. Fixture initialization supplies
a separate scratch name and never touches the game's container.

## Ownership and concurrency

A provider progresses through absent → in-use → released → absent. A hit is
possible only after the complete logical release/delete cycle. A public-key hit
requires zero outstanding borrowers and the owning thread's in-use provider.
Simultaneous imports receive distinct native handles; private/session key blobs
are never cached. Releasing a provider with a key still outstanding, nonzero
flags, or the wrong owning thread retires cache tracking and calls the original
release. A foreign key destroy likewise retires that cached key and forwards.

A generation recorded before each real acquire/import prevents a late native
result from being published across another observed lifecycle mutation. The lock
covers only bookkeeping; it never spans a CSP call. Eviction transfers live
caller-owned handles back to ordinary native lifetime and destroys only keys
whose logical borrower count is already zero, after unlocking. Fixed budgets
remain four provider slots, four key slots, 96-byte names and 512-byte blob
storage (the admitted blob length is exactly 276 bytes). No hot lookup allocates.

Microsoft documents that nonzero `CryptReleaseContext` flags fail **and release
the provider**; a successful logical release cannot be substituted in that case.
It also specifies that releasing the context invalidates its session keys and
hashes. [CryptReleaseContext](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-cryptreleasecontext)

## Intentional persistent-container difference

The cache keeps a real named provider alive while emulating the two deletes read
by this game sequence as ignored return values. The scratch container therefore
exists longer than it does without the optimization. Native provider/key objects
are bounded and retained until process exit. **The named container can remain
on disk after exit**, and the next game's first real delete removes it. This is
an explicit side effect, not a claim of complete API or registry equivalence.
It does not grant permission to use the cache for external keyset users.

No CryptoAPI cleanup runs from `DllMain`. The earlier cleanup could invoke CSP
and registry code under the Windows loader lock; merely linking ADVAPI32 does
not make that safe. Explicit `shutdown()` is reserved for a quiescent ordinary
caller, outside loader lock, and releases detached native objects after dropping
the cache lock. It refuses observed outstanding ownership. At production exit
Windows reclaims process resources; it does not remove persisted key containers.
[DLL best practices](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices),
[CryptAcquireContext](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-cryptacquirecontexta)

An ephemeral `CRYPT_VERIFYCONTEXT` provider is a documented alternative for
public-key signature checks, but substituting it for this game's named
`NEWKEYSET` would change that call's failure conditions. That broader behavior
change is not part of this patch. [Microsoft guidance](https://learn.microsoft.com/en-us/troubleshoot/windows-server/certificates-and-public-key-infrastructure-pki/cryptacquirecontext-troubleshooting)

## CPU cost and verification

The cache TU contains integer-only code and is compiled without SSE/MMX. The
built DLL's `check_no_x87.py` call-graph check covers the four outer wrappers and
core cache routines. This static check is complemented by seeded x87/MXCSR and
LastError hit controls; it is not proof that every native CSP has identical
volatile floating-point side effects on success. Native Windows execution is
still untested.

The corrected runner includes three components:

- The real-CSP signature differential: 200 varied messages, valid/corrupt/truncated
  RSA signatures, twelve API stages, matching verdicts/failure errors, and exact
  underlying-call/cleanup counts in the controlled fixture container.
- Deterministic lifetime controls: simultaneous imports, wrong scope/flags/blob,
  foreign threads, outstanding ownership at release/shutdown, reentrant lifecycle
  changes during real acquire/import, cleanup callbacks into statistics, and
  seeded CPU state on provider/key hits.
- A synthetic PE32 installer fixture: all four missing imports, all four partial
  patch failures, changed argument bytes, full commit, exact caller classification,
  unqualified IAT calls and rollback/protection restoration. A mapped stdcall
  executes through the actual qualified IAT wrapper; its native failure and
  admission counter verify the emitted return-address gate. The fixture relocates
  its own mapped addresses through a compile-only seam; the production image
  base remains fixed at `0x00400000`. It uses fake native functions and never
  patches or invokes the game.

Run only under `wine_lock.py`, with the game stopped, using the selected fixture
bottle. The runner records source maps around build/run, executable and raw-log
hashes, and bottle/emulation metadata. Timing is diagnostic; a slow scheduler
sample cannot convert semantic success into a correctness failure.

Fresh corrected-source runs pass **237 real-CSP checks + 53 deterministic
lifetime/CPU checks + 282 installer checks = 572 checks per bottle**, across
12 processes. Both source maps remain identical before build, after build and
after all runs. The host parser tests pass 6/6. The rebuilt production DLL passes
the no-x87 reachable-call-graph audit (210 functions); its hash is retained in
`verification/results/crypt-cache-build-review.json`.

| Bottle | Uncached mean / median | Cached mean / median | Context calls mean, off → on |
| --- | ---: | ---: | ---: |
| Steam / Rosetta | 528.208 / 528.9 µs | 132.208 / 137.4 µs | 378.034 → 2.388 µs |
| X3 / FEX | 444.229 / 443.5 µs | 107.378 / 108.8 µs | 321.885 → 2.846 µs |

Each fixed-order differential uses 200 generated messages and the same public
key. The approximate 4.0× / 4.14× gains measure core-cache plus CSP work, not a
full loading-probe envelope or game frame/loading speed. The outer route has a
bounded six-site integer comparison and atomic activation read; there is no
allocation or computational-FP save on that route. Telemetry-on cache misses
still traverse the light probe wrappers, so the direct fixture timing must not
be presented as an end-to-end instrumented speedup.

Run B's 844 signature checks cost 12.835 s, including 10.346 s in provider
acquisition. The earlier 11–13 s estimate remains a hypothesis until the user
runs the reviewed build with `--direct --telemetry --loading-probes --crypt-cache`.

Two fixture corrections preceded acceptance: CSP module paths must be read while
the signer still retains the provider, and a fixed synthetic address range was
unavailable under Wine. The latter's superseded failure record is retained
separately; it never reached hook installation. Fresh complete runs after both
corrections are the accepted evidence.

Acceptance needs cache enabled with all lifecycle imports, signature-verifier
counts/timing, unchanged script/mission behavior, and comparison against the
same uncached loading scenario. Startup/window/session log counters describe
logical cache requests and physical pass-throughs separately. No TAA/HDR or
other rendering behavior is changed by this branch.
