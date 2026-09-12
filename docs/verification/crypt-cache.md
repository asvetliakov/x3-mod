# CryptoAPI context/key cache (X3M_CRYPT_CACHE)

`src/proxy/crypt_cache.{h,cpp}` is a cache of the ADVAPI32 provider handle and
imported public key behind the game's script signature check `0x004cabc0`
([script-signature-check.md](../reverse-engineering/script-signature-check.md)),
routed through the loading-trace import hooks (`src/proxy/loading_trace.cpp`).
It targets the save-load "script/XML" stall: run B on the X3/FEX bottle measured
844 checks / 12.835 s (76.6 % of the 16.758 s stall), of which
`CryptAcquireContextA` alone — three calls per check, one of them the failing
"delete the container that does not exist" — cost 10.346 s (4.086 ms each).

## Switches and gate

* `X3M_CRYPT_CACHE=1` (`tools/manage.py launch --crypt-cache`). No `--telemetry`
  needed: `capture.cpp` initializes the loading-trace machinery when the switch
  is set, and with telemetry off `install()` patches only the four rows the
  cache needs (`CryptAcquireContextA`, `CryptReleaseContext`, `CryptImportKey`,
  `CryptDestroyKey`); every other row stays unpatched and no mesh/adjacency
  service starts. The session log shows `crypt_cache requested=1 enabled=…
  telemetry=… imports=… provider_slots=4 key_slots=4 blob_limit=512`, the
  `loading_hook` lines and `loading_trace … scope=crypt_cache` (or
  `scope=gz_buffer+crypt_cache` with `--gz-buffer`).
* With telemetry on the four rows are patched even without `--loading-probes`,
  and the cache's real calls go through the light traced wrappers: the
  `CryptAcquireContextA`/`CryptImportKey` `loading_metric` rows then count only
  the pass-throughs (expected: 2 acquires and 1 import for the whole load),
  while `CryptCreateHash`/`CryptHashData`/`CryptVerifySignatureA`/
  `CryptGetHashParam`/`CryptDestroyHash` keep counting every call as before
  (they are not touched by the cache and need `--loading-probes` to be patched).
* With the cache off nothing changes: the four slots forward to the light traced
  wrappers exactly as before.

## Semantics

Per `(container, provider, type)` one entry with a three-phase state machine;
the game's sequence per check is delete → create → import → hash → verify →
destroy key → release → delete:

| Call | Cache off | Cache on |
| --- | --- | --- |
| `CryptAcquireContextA(…, CRYPT_DELETEKEYSET)` while the entry is *absent* (the game's first delete) | real call fails, `NTE_BAD_KEYSET` (0x80090016) | **emulated**: `FALSE`, last error = the error recorded from the first real failure (`NTE_BAD_KEYSET` if none was seen), no ADVAPI32 call |
| `CryptAcquireContextA(…, CRYPT_NEWKEYSET)` | real call creates the container (~4 ms under Wine) | first time: real call, handle cached (*in use*); after that: the cached handle, `TRUE`, no ADVAPI32 call |
| `CryptImportKey(prov, blob, len, 0, flags, &key)` on a cached provider | real import | first time per (provider, flags, blob bytes ≤ 512): real import, key cached; after that: the cached key |
| `CryptCreateHash`, `CryptHashData`, `CryptVerifySignatureA`, `CryptGetHashParam`, `CryptDestroyHash` | real | real, untouched (not even hooked without telemetry) |
| `CryptDestroyKey(cached key)` | real | suppressed, `TRUE` |
| `CryptReleaseContext(cached handle)` | real | suppressed, entry *released*, `TRUE` |
| `CryptAcquireContextA(…, CRYPT_DELETEKEYSET)` while *released* (the game's last delete) | real call deletes the container | **emulated**: `TRUE`, entry *absent*, no ADVAPI32 call |

Everything else passes through: acquires with other flags on a busy entry,
imports with a wrapping key or a blob over 512 bytes, releases/destroys of
unknown handles. A delete while the handle is out (the game never does this)
evicts the entry without releasing anything — the delete passes through, the
caller keeps its handle and key and their later release/destroy pass through —
and the next create caches again (`evictions` counter). The verification verdict
is therefore computed by the CSP exactly as before; the game reads nothing the
cache invents except the two ignored delete results and the `TRUE` of the
suppressed release/destroy.

Thread safety: one spinlock around every state change (the script VM runs the
check on one thread; `0x004cae90` is the second caller). The unit is compiled
without SSE/MMX, integer only, no logging, like `loading_trace_light.cpp`
(`check_no_x87.py` walks `crypt_acquire`/`crypt_release`/`crypt_import`/
`crypt_key_destroy` and the `x3m::crypt_cache::*` functions: PASS on the
build below). Only the thread's last error is transported, and it is set
deliberately on the emulated failure.

Teardown: `crypt_cache::shutdown()` destroys the cached keys, releases the
cached handles and issues a real `CRYPT_DELETEKEYSET` for every container whose
last game-side action was a delete, so the key store is left as the game leaves
it. It runs from `DllMain(DLL_PROCESS_DETACH)` (ADVAPI32 is the proxy's own
import and outlives it; a lock still held by a terminated thread makes it a
no-op) and from `loading_trace::shutdown()` for the fixtures. Until then the
container `X2EgosoftCSPContainer` exists in the bottle registry while the game
runs — it does uncached too, between a create and the delete 5 ms later.

## Log lines

* `crypt_cache scope=window acquires=… hits=… misses=… failed_passthrough=…
  releases_suppressed=… imports=… import_hits=… deletes_emulated=…
  deletes_passthrough=… busy_passthrough=… evictions=… releases=…
  import_passthrough=… destroys=… destroys_suppressed=… providers_cached=…
  keys_cached=… probe_error=0x…` — one per telemetry window with activity
  (deltas since the previous window line), from `loading_trace::report()`.
* `crypt_cache scope=session …` — cumulative totals, logged when the last
  device is destroyed (with or without telemetry) and by
  `loading_trace::shutdown()` (fixtures) followed by `crypt_cache_shutdown
  released=1`.

For a save load with 844 checks the session line should read `acquires=2532
hits=843 misses=1 failed_passthrough=1 releases_suppressed=844 imports=844
import_hits=843 deletes_emulated=1687 deletes_passthrough=1 evictions=0
providers_cached=1 keys_cached=1 probe_error=0x80090016` (plus whatever
checks the menus and the story object add before and after).

## Fixture

`verification/probe/crypt_cache_fixture.cpp` (built by `build_crypt_cache.sh`,
run by `run_crypt_cache.py` under `wine_lock.py`; host tests
`verification/analysis/test_crypt_cache.py`) links the production module and
drives the bottle's real ADVAPI32/rsaenh:

1. generates an RSA-2048 signature key pair in its own container, exports the
   public `PUBLICKEYBLOB` (asserted to be the game's 276 bytes) and signs one
   distinct 0.5–4 KB message per iteration; every fourth signature is
   corrupted, every sixteenth truncated, so both verdicts are exercised;
2. runs the exact 12-call sequence of `0x004cabc0` 200 times with the cache off
   (real calls through counting shims) and 200 times with the cache on (the
   module bound to the same shims), recording every call's `BOOL`, the
   `GetLastError` of every failing call, the verdict and the digest bytes;
3. requires the two passes to agree call by call (`CRYPT_COMPARE mismatches=0`),
   150 verified / 50 rejected in both, the cache's counters to be exactly the
   predicted ones (3 acquires per check, 1 miss then hits, 1 real failed delete
   whose error is replayed), the real-call counts to be 600 acquires + 200
   releases off versus 2 acquires + 0 releases + 1 import on;
4. exercises the eviction path (delete while the handle is out) and the
   recovery after it, then `shutdown()`: exactly one real destroy, release and
   delete, no live provider/key left in the shims (`CRYPT_LEAK 0 0`), and the
   container absent afterwards;
5. reports the per-iteration time of both passes (`CRYPT_MODE … mean_us=…
   context_mean_us=…`, the latter the four context calls alone).

The fixture never touches the game's container name (host test).

### Recorded runs

2026-09-13, 200 iterations per pass, 237 checks, 0 failures, `mismatches=0`,
`verified_off=150 verified_on=150`, `CRYPT_LEAK live_providers=0 live_keys=0`,
`container_absent=1`, cache counters exactly `acquires=600 hits=199 misses=1
failed_passthrough=1 releases_suppressed=200 imports=200 import_hits=199
deletes_emulated=399 deletes_passthrough=1 evictions=0` (after the eviction case
`evictions=1`, `misses=2`), `probe_error=0x80090016` (`NTE_BAD_KEYSET`, recorded
from the real first delete and replayed 199 times identically):

| Bottle | Pass | Real acquires / releases / imports | Mean per check | Median | Context calls (delete, create, release, delete) per check |
| --- | --- | --- | --- | --- | --- |
| Steam (x86_64 Wine, Rosetta) — `verification/results/crypt-cache-summary.json` | off | 600 / 200 / 200 | 522.9 µs | 526.4 µs | 374.8 µs |
| | on | 2 / 0 / 1 | 128.1 µs | 131.8 µs | 2.2 µs |
| X3 (arm64 Wine + FEX, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`) — `verification/results/bottle-X3/crypt-cache-summary.json` | off | 600 / 200 / 200 | 482.9 µs | 487.2 µs | 362.9 µs |
| | on | 2 / 0 / 1 | 103.8 µs | 107.3 µs | 2.4 µs |

Speed-up 4.1× (Steam) and 4.7× (X3) per check; the four context calls go from
~365 µs to ~2 µs, and what remains (~100 µs) is the MD5 + RSA-2048 verify +
hash object churn that the cache leaves to the CSP. Note the magnitude: in the
fixture process a container create/delete costs ~90 µs, in the game process
run B measured 4.086 ms per acquire. The fixture proves the calls vanish and
the outcomes are identical; the 11–13 s figure is run B's own measurement and
the next in-game run confirms it.

## Expected in-game effect and what the next run must show

From run B (X3/FEX bottle, 844 checks): the 2,532 acquires (10.346 s) drop to
2 real ones and the 844 imports to 1, leaving the MD5 + RSA verify (~2 s) — an
estimated 11.3–12.8 s off the 16.758 s stall (save load 38.5 → 26–28 s). The
next `launch --direct --telemetry --loading-probes --crypt-cache` run must show:

* `crypt_cache requested=1 enabled=1 imports=1` at startup and the
  `crypt_cache scope=window` lines during the load with the counts above
  (`hits` ≈ `acquires`/3 − 1, `deletes_emulated` ≈ 2·checks − 1, `evictions=0`);
* the `signature_check` probe row (`0x004cabc0`) at 844 calls with an inclusive
  time near 2 s instead of 12.8 s, and the `CryptAcquireContextA` metric row at
  2 calls;
* the same script/mission behaviour as before (the verdicts are unchanged by
  construction; the fixture is the proof, the run is the confirmation);
* at exit, the container gone from the bottle registry
  (`HKCU\Software\Microsoft\Cryptography\UserKeys\X2EgosoftCSPContainer`
  absent in `user.reg`), i.e. the detach shutdown ran.
